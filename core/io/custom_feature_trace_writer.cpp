/**************************************************************************/
/*  custom_feature_trace_writer.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "custom_feature_trace_writer.h"

#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/time.h"

struct CustomFeatureTraceSessionSort {
	bool operator()(const CustomFeatureTraceWriter::SessionDirectoryInfo &p_a, const CustomFeatureTraceWriter::SessionDirectoryInfo &p_b) const {
		if (p_a.modified_time == p_b.modified_time) {
			return p_a.path > p_b.path;
		}
		return p_a.modified_time > p_b.modified_time;
	}
};

CustomFeatureTraceWriter::~CustomFeatureTraceWriter() {
	finalize();
}

void CustomFeatureTraceWriter::_reset_state_no_lock() {
	options = Options();
	session_info = SessionInfo();
	root_dir = String();
	session_dir = String();
	payload_dir = String();
	session_id = String();
	started_at_utc = String();
	ended_at_utc = String();
	event_files.clear();
	features_seen.clear();
	current_event_file.unref();
	current_event_file_bytes = 0;
	current_event_file_index = 0;
	next_payload_file_index = 1;
	next_event_seq = 1;
	active = false;
}

void CustomFeatureTraceWriter::_finalize_no_lock() {
	if (!active) {
		_reset_state_no_lock();
		return;
	}

	ended_at_utc = _utc_now_string();
	_update_manifest();

	if (current_event_file.is_valid()) {
		current_event_file->flush();
		current_event_file->close();
		current_event_file.unref();
	}

	active = false;
}

Dictionary CustomFeatureTraceWriter::PayloadDescriptor::to_dictionary() const {
	Dictionary payload;
	payload["mime"] = mime;
	payload["bytes"] = bytes;
	payload["sha256"] = sha256;
	payload["inline"] = inline_value;
	payload["refPath"] = ref_path;
	return payload;
}

String CustomFeatureTraceWriter::_sanitize_path_component(const String &p_value) {
	String sanitized = p_value.strip_edges();
	if (sanitized.is_empty()) {
		return "payload";
	}

	static const char32_t forbidden[] = { '<', '>', ':', '"', '/', '\\', '|', '?', '*', '\0' };
	for (int i = 0; forbidden[i] != '\0'; i++) {
		sanitized = sanitized.replace_char(forbidden[i], '_');
	}
	sanitized = sanitized.replace(" ", "_");
	return sanitized.substr(0, MIN(64, sanitized.length()));
}

String CustomFeatureTraceWriter::_resolve_default_root_dir() {
	const String cache_path = OS::get_singleton()->get_cache_path();
	if (!cache_path.is_empty()) {
		return cache_path.path_join(OS::get_singleton()->get_godot_dir_name()).path_join("custom_feature_traces").simplify_path();
	}
	return OS::get_singleton()->get_temp_path().path_join("godot").path_join("custom_feature_traces").simplify_path();
}

String CustomFeatureTraceWriter::_utc_now_string() {
	return Time::get_singleton()->get_datetime_string_from_system(true, false) + "Z";
}

int64_t CustomFeatureTraceWriter::_unix_usec_now() {
	return (int64_t)(Time::get_singleton()->get_unix_time_from_system() * 1000000.0);
}

int64_t CustomFeatureTraceWriter::_utf8_size(const String &p_text) {
	return p_text.to_utf8_buffer().size();
}

String CustomFeatureTraceWriter::_payload_extension_for_mime(const String &p_mime) {
	if (p_mime.contains("jsonl")) {
		return "jsonl";
	}
	if (p_mime.contains("json")) {
		return "json";
	}
	if (p_mime.contains("markdown")) {
		return "md";
	}
	return "txt";
}

Error CustomFeatureTraceWriter::_remove_directory_recursive(const String &p_path) {
	if (!DirAccess::dir_exists_absolute(p_path)) {
		return OK;
	}

	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_null()) {
		return DirAccess::remove_absolute(p_path);
	}

	Error err = dir->erase_contents_recursive();
	if (err != OK) {
		return err;
	}
	return DirAccess::remove_absolute(p_path);
}

String CustomFeatureTraceWriter::_build_session_dir_name() const {
	String safe_started = started_at_utc.replace_char(':', '-');
	return vformat("%s-pid%d-%s", safe_started, session_info.pid, session_id);
}

void CustomFeatureTraceWriter::_cleanup_old_sessions() {
	if (!DirAccess::dir_exists_absolute(root_dir)) {
		return;
	}

	Vector<SessionDirectoryInfo> sessions;
	const PackedStringArray directories = DirAccess::get_directories_at(root_dir);
	for (int i = 0; i < directories.size(); i++) {
		SessionDirectoryInfo info;
		info.path = root_dir.path_join(directories[i]);
		info.modified_time = FileAccess::get_modified_time(info.path);
		sessions.push_back(info);
	}

	sessions.sort_custom<CustomFeatureTraceSessionSort>();

	const uint64_t cutoff = (uint64_t)MAX<int64_t>(0, (int64_t)Time::get_singleton()->get_unix_time_from_system() - (int64_t)options.max_session_age_days * 24 * 60 * 60);
	for (int i = 0; i < sessions.size(); i++) {
		const bool beyond_count_limit = i >= options.max_sessions;
		const bool too_old = sessions[i].modified_time > 0 && sessions[i].modified_time < cutoff;
		if (beyond_count_limit || too_old) {
			_remove_directory_recursive(sessions[i].path);
		}
	}
}

Error CustomFeatureTraceWriter::initialize(const SessionInfo &p_session_info, const Options &p_options, String &r_error) {
	r_error = String();
	MutexLock lock(mutex);
	_finalize_no_lock();
	_reset_state_no_lock();

	options = p_options;
	session_info = p_session_info;
	root_dir = options.base_dir_override.is_empty() ? _resolve_default_root_dir() : options.base_dir_override.simplify_path();
	started_at_utc = _utc_now_string();
	ended_at_utc = String();
	session_id = String::num_uint64(OS::get_singleton()->get_ticks_usec());
	event_files.clear();
	features_seen.clear();
	current_event_file_index = 0;
	current_event_file_bytes = 0;
	next_payload_file_index = 1;
	next_event_seq = 1;

	Error err = DirAccess::make_dir_recursive_absolute(root_dir);
	if (err != OK && err != ERR_ALREADY_EXISTS) {
		r_error = vformat("Unable to create custom feature trace root directory \"%s\" (error code %d).", root_dir, (int)err);
		return err;
	}

	_cleanup_old_sessions();

	session_dir = root_dir.path_join(_build_session_dir_name()).simplify_path();
	payload_dir = session_dir.path_join("payloads").simplify_path();
	err = DirAccess::make_dir_recursive_absolute(payload_dir);
	if (err != OK && err != ERR_ALREADY_EXISTS) {
		r_error = vformat("Unable to create custom feature trace session directory \"%s\" (error code %d).", payload_dir, (int)err);
		return err;
	}

	err = _open_next_event_file(r_error);
	if (err != OK) {
		return err;
	}

	active = true;
	_update_manifest();
	return OK;
}

void CustomFeatureTraceWriter::finalize() {
	MutexLock lock(mutex);
	_finalize_no_lock();
}

bool CustomFeatureTraceWriter::is_active() const {
	MutexLock lock(mutex);
	return active;
}

void CustomFeatureTraceWriter::set_project_path(const String &p_project_path) {
	MutexLock lock(mutex);
	if (session_info.project_path == p_project_path) {
		return;
	}
	session_info.project_path = p_project_path;
	if (active) {
		_update_manifest();
	}
}

void CustomFeatureTraceWriter::set_process_mode(const String &p_process_mode) {
	MutexLock lock(mutex);
	if (session_info.process_mode == p_process_mode) {
		return;
	}
	session_info.process_mode = p_process_mode;
	if (active) {
		_update_manifest();
	}
}

void CustomFeatureTraceWriter::_note_feature_seen_no_lock(const String &p_feature) {
	if (p_feature.is_empty()) {
		return;
	}
	if (features_seen.has(p_feature)) {
		return;
	}
	features_seen.insert(p_feature);
	if (active) {
		_update_manifest();
	}
}

void CustomFeatureTraceWriter::note_feature_seen(const String &p_feature) {
	MutexLock lock(mutex);
	_note_feature_seen_no_lock(p_feature);
}

void CustomFeatureTraceWriter::_update_manifest() {
	if (session_dir.is_empty()) {
		return;
	}

	Dictionary manifest;
	manifest["sessionId"] = session_id;
	manifest["startedAtUtc"] = started_at_utc;
	manifest["endedAtUtc"] = ended_at_utc;
	manifest["pid"] = session_info.pid;
	manifest["binaryPath"] = session_info.binary_path;
	manifest["cwd"] = session_info.cwd;
	manifest["projectPath"] = session_info.project_path;
	manifest["processMode"] = session_info.process_mode;

	Array features;
	for (const String &feature : features_seen) {
		features.push_back(feature);
	}
	features.sort();
	manifest["featuresSeen"] = features;

	Array files;
	for (int i = 0; i < event_files.size(); i++) {
		files.push_back(event_files[i]);
	}
	manifest["eventFiles"] = files;
	manifest["payloadDirectory"] = String("payloads");

	Error err = OK;
	Ref<FileAccess> manifest_file = FileAccess::open(session_dir.path_join("manifest.json"), FileAccess::WRITE, &err);
	if (manifest_file.is_null() || err != OK) {
		return;
	}
	manifest_file->store_string(JSON::stringify(manifest, "\t"));
	manifest_file->store_string("\n");
	manifest_file->flush();
	manifest_file->close();
}

Error CustomFeatureTraceWriter::_open_next_event_file(String &r_error) {
	r_error = String();
	if (current_event_file.is_valid()) {
		current_event_file->flush();
		current_event_file->close();
		current_event_file.unref();
	}

	current_event_file_index++;
	const String file_name = vformat("events-%04d.jsonl", current_event_file_index);
	Error err = OK;
	current_event_file = FileAccess::open(session_dir.path_join(file_name), FileAccess::WRITE, &err);
	if (current_event_file.is_null() || err != OK) {
		r_error = vformat("Unable to open trace event file \"%s\" (error code %d).", file_name, (int)err);
		return err == OK ? ERR_CANT_OPEN : err;
	}

	current_event_file_bytes = 0;
	event_files.push_back(file_name);
	_update_manifest();
	return OK;
}

Error CustomFeatureTraceWriter::write_event(const EventRecord &p_event, String &r_error) {
	r_error = String();
	MutexLock lock(mutex);
	ERR_FAIL_COND_V_MSG(!active, ERR_UNCONFIGURED, "CustomFeatureTraceWriter must be initialized before writing events.");

	_note_feature_seen_no_lock(p_event.feature);

	Dictionary event;
	event["seq"] = (int64_t)next_event_seq++;
	event["tsUnixUsec"] = _unix_usec_now();
	event["monoUsec"] = (int64_t)OS::get_singleton()->get_ticks_usec();
	event["sessionId"] = session_id;
	event["feature"] = p_event.feature;
	event["event"] = p_event.event;
	event["severity"] = p_event.severity;
	event["correlationId"] = p_event.correlation_id;
	event["data"] = p_event.data;
	event["payloads"] = p_event.payloads;
	event["error"] = p_event.error.is_empty() ? Variant() : Variant(p_event.error);

	const String line = JSON::stringify(event);
	const uint64_t line_bytes = (uint64_t)_utf8_size(line) + 1;
	if (current_event_file_bytes > 0 && current_event_file_bytes + line_bytes > options.max_event_file_bytes) {
		Error open_err = _open_next_event_file(r_error);
		if (open_err != OK) {
			return open_err;
		}
	}

	current_event_file->store_line(line);
	current_event_file->flush();
	current_event_file_bytes += line_bytes;
	return OK;
}

Error CustomFeatureTraceWriter::_write_text_sidecar(const String &p_name, const String &p_extension, const String &p_text, String &r_ref_path, String &r_error) {
	r_error = String();
	r_ref_path = String();

	const String file_name = vformat("payload-%06d-%s.%s", next_payload_file_index++, _sanitize_path_component(p_name), p_extension);
	r_ref_path = String("payloads").path_join(file_name);
	const String full_path = session_dir.path_join(r_ref_path);

	Error err = OK;
	Ref<FileAccess> payload_file = FileAccess::open(full_path, FileAccess::WRITE, &err);
	if (payload_file.is_null() || err != OK) {
		r_error = vformat("Unable to create trace payload sidecar \"%s\" (error code %d).", full_path, (int)err);
		return err == OK ? ERR_CANT_OPEN : err;
	}

	payload_file->store_string(p_text);
	payload_file->flush();
	payload_file->close();
	return OK;
}

Error CustomFeatureTraceWriter::make_text_payload(const String &p_name, const String &p_mime, const String &p_text, PayloadDescriptor &r_descriptor, String &r_error) {
	MutexLock lock(mutex);
	r_descriptor = PayloadDescriptor();
	r_descriptor.mime = p_mime;
	r_descriptor.bytes = _utf8_size(p_text);
	r_descriptor.sha256 = p_text.sha256_text();

	if ((uint64_t)r_descriptor.bytes <= options.inline_payload_max_bytes) {
		r_descriptor.inline_value = p_text;
		return OK;
	}

	const String extension = _payload_extension_for_mime(p_mime);
	return _write_text_sidecar(p_name, extension, p_text, r_descriptor.ref_path, r_error);
}

Error CustomFeatureTraceWriter::make_json_payload(const String &p_name, const Variant &p_value, PayloadDescriptor &r_descriptor, String &r_error) {
	return make_text_payload(p_name, "application/json", JSON::stringify(p_value), r_descriptor, r_error);
}

Error CustomFeatureTraceWriter::reference_existing_file(const String &p_mime, const String &p_path, PayloadDescriptor &r_descriptor, String &r_error) const {
	r_descriptor = PayloadDescriptor();
	r_error = String();

	const String resolved_path = p_path.simplify_path();
	if (!FileAccess::exists(resolved_path)) {
		r_error = vformat("Trace payload file \"%s\" does not exist.", resolved_path);
		return ERR_FILE_NOT_FOUND;
	}

	r_descriptor.mime = p_mime;
	r_descriptor.ref_path = resolved_path;
	r_descriptor.bytes = (int64_t)FileAccess::get_size(resolved_path);
	r_descriptor.sha256 = FileAccess::get_sha256(resolved_path);
	return OK;
}
