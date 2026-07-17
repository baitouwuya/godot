/**************************************************************************/
/*  structured_trace_writer.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "structured_trace_writer.h"

#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/time.h"

StructuredTraceWriter::~StructuredTraceWriter() {
	close();
}

void StructuredTraceWriter::_set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

Dictionary StructuredTraceWriter::PayloadDescriptor::to_dictionary() const {
	Dictionary result;
	result["mime"] = mime;
	result["bytes"] = bytes;
	result["sha256"] = sha256;
	if (inline_value.get_type() != Variant::NIL) {
		result["inline"] = inline_value;
	}
	if (!ref_path.is_empty()) {
		result["refPath"] = ref_path;
	}
	if (external) {
		result["external"] = true;
	}
	return result;
}

bool StructuredTraceWriter::SessionSortComparator::operator()(const SessionDirectoryInfo &p_left, const SessionDirectoryInfo &p_right) const {
	return p_left.modified_time == p_right.modified_time ? p_left.path > p_right.path : p_left.modified_time > p_right.modified_time;
}

String StructuredTraceWriter::_default_root_directory() {
	String cache = OS::get_singleton()->get_cache_path();
	if (cache.is_empty()) {
		cache = OS::get_singleton()->get_temp_path();
	}
	return cache.path_join(OS::get_singleton()->get_godot_dir_name()).path_join("mcp_traces").simplify_path();
}

String StructuredTraceWriter::_utc_now() {
	return Time::get_singleton()->get_datetime_string_from_system(true, false) + "Z";
}

int64_t StructuredTraceWriter::_unix_usec_now() {
	return int64_t(Time::get_singleton()->get_unix_time_from_system() * 1000000.0);
}

int64_t StructuredTraceWriter::_utf8_size(const String &p_text) {
	return p_text.to_utf8_buffer().size();
}

String StructuredTraceWriter::_sanitize_component(const String &p_value) {
	String result = p_value.strip_edges();
	if (result.is_empty()) {
		return "payload";
	}
	static const char32_t forbidden[] = { '<', '>', ':', '"', '/', '\\', '|', '?', '*', '\0' };
	for (int i = 0; forbidden[i] != '\0'; i++) {
		result = result.replace_char(forbidden[i], '_');
	}
	result = result.replace(" ", "_");
	return result.substr(0, MIN(64, result.length()));
}

String StructuredTraceWriter::_payload_extension(const String &p_mime) {
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

Error StructuredTraceWriter::_remove_directory_recursive(const String &p_path) {
	if (!DirAccess::dir_exists_absolute(p_path)) {
		return OK;
	}
	Ref<DirAccess> directory = DirAccess::open(p_path);
	if (directory.is_null()) {
		return ERR_CANT_OPEN;
	}
	Error error = directory->erase_contents_recursive();
	return error == OK ? DirAccess::remove_absolute(p_path) : error;
}

bool StructuredTraceWriter::_is_valid_severity(const String &p_severity) {
	return p_severity == "debug" || p_severity == "info" || p_severity == "warning" || p_severity == "error";
}

bool StructuredTraceWriter::_metadata_is_bounded(const Dictionary &p_metadata) {
	return uint64_t(_utf8_size(JSON::stringify(p_metadata))) <= MAX_METADATA_BYTES;
}

void StructuredTraceWriter::_reset_no_lock() {
	options = Options();
	project_metadata.clear();
	process_metadata.clear();
	root_directory = String();
	session_directory = String();
	payload_directory = String();
	session_id = String();
	started_at_utc = String();
	ended_at_utc = String();
	event_files.clear();
	categories_seen.clear();
	current_event_file.unref();
	current_event_file_bytes = 0;
	current_event_file_index = 0;
	next_payload_file_index = 1;
	next_event_sequence = 1;
	active = false;
}

void StructuredTraceWriter::_close_no_lock() {
	if (!active) {
		return;
	}
	if (current_event_file.is_valid()) {
		current_event_file->flush();
		current_event_file->close();
		current_event_file.unref();
	}
	ended_at_utc = _utc_now();
	_write_manifest();
	active = false;
}

Error StructuredTraceWriter::_validate_options(const Options &p_options, String *r_error) const {
	if (p_options.max_event_file_bytes == 0 || p_options.max_event_file_bytes > 1024ULL * 1024 * 1024) {
		_set_error(r_error, "max_event_file_bytes must be from 1 byte to 1 GiB.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.inline_payload_max_bytes > MAX_EVENT_RECORD_BYTES) {
		_set_error(r_error, "inline_payload_max_bytes exceeds the maximum event record size.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.max_sessions < 1 || p_options.max_sessions > MAX_SESSIONS) {
		_set_error(r_error, "max_sessions must be from 1 to 30.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.max_session_age_days < 0 || p_options.max_session_age_days > MAX_SESSION_AGE_DAYS) {
		_set_error(r_error, "max_session_age_days must be from 0 to 14.");
		return ERR_INVALID_PARAMETER;
	}
	return OK;
}

String StructuredTraceWriter::_build_session_directory_name() const {
	String timestamp = started_at_utc.replace_char(':', '-');
	const int64_t pid = int64_t(process_metadata.get("pid", OS::get_singleton()->get_process_id()));
	return vformat("trace-%s-pid%d-%s", timestamp, pid, session_id);
}

Error StructuredTraceWriter::_cleanup_old_sessions(String *r_error) {
	Vector<SessionDirectoryInfo> sessions;
	const PackedStringArray directories = DirAccess::get_directories_at(root_directory);
	for (const String &name : directories) {
		if (!name.begins_with("trace-")) {
			continue;
		}
		const String path = root_directory.path_join(name).simplify_path();
		const String manifest = path.path_join("manifest.json");
		if (!FileAccess::exists(manifest)) {
			continue;
		}
		SessionDirectoryInfo info;
		info.path = path;
		info.modified_time = FileAccess::get_modified_time(manifest);
		sessions.push_back(info);
	}
	sessions.sort_custom<SessionSortComparator>();
	const uint64_t now = uint64_t(MAX(0.0, Time::get_singleton()->get_unix_time_from_system()));
	const uint64_t cutoff = options.max_session_age_days > 0 ? now - MIN(now, uint64_t(options.max_session_age_days) * 24 * 60 * 60) : 0;
	const int existing_to_keep = MAX(0, options.max_sessions - 1);
	for (int i = 0; i < sessions.size(); i++) {
		const bool beyond_count = i >= existing_to_keep;
		const bool expired = cutoff > 0 && sessions[i].modified_time > 0 && sessions[i].modified_time < cutoff;
		if ((beyond_count || expired) && _remove_directory_recursive(sessions[i].path) != OK) {
			_set_error(r_error, "Unable to remove an expired structured trace session.");
			return ERR_CANT_CREATE;
		}
	}
	return OK;
}

Error StructuredTraceWriter::_write_manifest(String *r_error) {
	Dictionary manifest;
	manifest["schemaVersion"] = 1;
	manifest["sessionId"] = session_id;
	manifest["startedAtUtc"] = started_at_utc;
	manifest["endedAtUtc"] = ended_at_utc;
	manifest["project"] = project_metadata;
	manifest["process"] = process_metadata;
	Array categories;
	for (const String &category : categories_seen) {
		categories.push_back(category);
	}
	categories.sort();
	manifest["categories"] = categories;
	Array files;
	for (const String &file : event_files) {
		files.push_back(file);
	}
	manifest["eventFiles"] = files;
	manifest["payloadDirectory"] = "payloads";

	Error error = OK;
	Ref<FileAccess> file = FileAccess::open(session_directory.path_join("manifest.json"), FileAccess::WRITE, &error);
	if (file.is_null() || error != OK) {
		_set_error(r_error, "Unable to write the structured trace manifest.");
		return error == OK ? ERR_CANT_OPEN : error;
	}
	file->store_string(JSON::stringify(manifest, "\t") + "\n");
	file->flush();
	file->close();
	return OK;
}

Error StructuredTraceWriter::_open_next_event_file(String *r_error) {
	if (current_event_file.is_valid()) {
		current_event_file->flush();
		current_event_file->close();
		current_event_file.unref();
	}
	current_event_file_index++;
	const String name = vformat("events-%04d.jsonl", current_event_file_index);
	Error error = OK;
	current_event_file = FileAccess::open(session_directory.path_join(name), FileAccess::WRITE, &error);
	if (current_event_file.is_null() || error != OK) {
		_set_error(r_error, "Unable to open a structured trace event file.");
		return error == OK ? ERR_CANT_OPEN : error;
	}
	current_event_file_bytes = 0;
	event_files.push_back(name);
	return _write_manifest(r_error);
}

Error StructuredTraceWriter::open(const Dictionary &p_project_metadata, const Dictionary &p_process_metadata, const Options &p_options, String *r_error) {
	_set_error(r_error, String());
	MutexLock lock(mutex);
	_close_no_lock();
	_reset_no_lock();
	Error error = _validate_options(p_options, r_error);
	if (error != OK) {
		return error;
	}
	if (!_metadata_is_bounded(p_project_metadata) || !_metadata_is_bounded(p_process_metadata)) {
		_set_error(r_error, "Structured trace metadata exceeds the size limit.");
		return ERR_OUT_OF_MEMORY;
	}
	options = p_options;
	project_metadata = p_project_metadata.duplicate(true);
	process_metadata = p_process_metadata.duplicate(true);
	root_directory = options.root_directory_override.is_empty() ? _default_root_directory() : options.root_directory_override.simplify_path();
	if (!root_directory.is_absolute_path()) {
		_set_error(r_error, "The structured trace root directory must be absolute.");
		_reset_no_lock();
		return ERR_INVALID_PARAMETER;
	}
	error = DirAccess::make_dir_recursive_absolute(root_directory);
	if (error != OK && error != ERR_ALREADY_EXISTS) {
		_set_error(r_error, "Unable to create the structured trace root directory.");
		_reset_no_lock();
		return error;
	}
	error = _cleanup_old_sessions(r_error);
	if (error != OK) {
		_reset_no_lock();
		return error;
	}
	started_at_utc = _utc_now();
	const String identity_source = started_at_utc + "|" + itos(OS::get_singleton()->get_process_id()) + "|" + String::num_uint64(OS::get_singleton()->get_ticks_usec());
	session_id = identity_source.sha256_text().left(16);
	session_directory = root_directory.path_join(_build_session_directory_name()).simplify_path();
	payload_directory = session_directory.path_join("payloads").simplify_path();
	error = DirAccess::make_dir_recursive_absolute(payload_directory);
	if (error != OK && error != ERR_ALREADY_EXISTS) {
		_set_error(r_error, "Unable to create the structured trace session directory.");
		_reset_no_lock();
		return error;
	}
	active = true;
	error = _open_next_event_file(r_error);
	if (error != OK) {
		active = false;
		_remove_directory_recursive(session_directory);
		_reset_no_lock();
		return error;
	}
	return OK;
}

Error StructuredTraceWriter::record(const String &p_category, const String &p_event, const String &p_severity, const String &p_correlation_id, const Dictionary &p_data, const Dictionary &p_payloads, const Dictionary &p_error, String *r_error) {
	_set_error(r_error, String());
	MutexLock lock(mutex);
	if (!active) {
		_set_error(r_error, "StructuredTraceWriter must be opened before recording events.");
		return ERR_UNCONFIGURED;
	}
	if (p_category.is_empty() || p_category.length() > 128 || p_event.is_empty() || p_event.length() > 256 || p_correlation_id.length() > 256 || !_is_valid_severity(p_severity)) {
		_set_error(r_error, "Structured trace event metadata is invalid.");
		return ERR_INVALID_PARAMETER;
	}
	Dictionary entry;
	entry["seq"] = int64_t(next_event_sequence);
	entry["tsUnixUsec"] = _unix_usec_now();
	entry["monoUsec"] = int64_t(OS::get_singleton()->get_ticks_usec());
	entry["sessionId"] = session_id;
	entry["category"] = p_category;
	entry["event"] = p_event;
	entry["severity"] = p_severity;
	entry["correlationId"] = p_correlation_id;
	entry["data"] = p_data;
	entry["payloads"] = p_payloads;
	entry["error"] = p_error.is_empty() ? Variant() : Variant(p_error);
	const String line = JSON::stringify(entry);
	const uint64_t line_bytes = uint64_t(_utf8_size(line)) + 1;
	if (line_bytes > MAX_EVENT_RECORD_BYTES) {
		_set_error(r_error, "Structured trace event exceeds the size limit.");
		return ERR_OUT_OF_MEMORY;
	}
	if (current_event_file_bytes > 0 && current_event_file_bytes + line_bytes > options.max_event_file_bytes) {
		Error error = _open_next_event_file(r_error);
		if (error != OK) {
			return error;
		}
	}
	current_event_file->store_line(line);
	current_event_file->flush();
	current_event_file_bytes += line_bytes;
	next_event_sequence++;
	if (!categories_seen.has(p_category)) {
		categories_seen.insert(p_category);
		_write_manifest();
	}
	return OK;
}

Error StructuredTraceWriter::_write_text_sidecar(const String &p_name, const String &p_extension, const String &p_text, String &r_ref_path, String *r_error) {
	const String name = vformat("payload-%06d-%s.%s", next_payload_file_index++, _sanitize_component(p_name), p_extension);
	r_ref_path = String("payloads").path_join(name);
	Error error = OK;
	Ref<FileAccess> file = FileAccess::open(session_directory.path_join(r_ref_path), FileAccess::WRITE, &error);
	if (file.is_null() || error != OK) {
		_set_error(r_error, "Unable to create a structured trace payload sidecar.");
		return error == OK ? ERR_CANT_OPEN : error;
	}
	file->store_string(p_text);
	file->flush();
	file->close();
	return OK;
}

Error StructuredTraceWriter::make_text_payload(const String &p_name, const String &p_mime, const String &p_text, PayloadDescriptor &r_descriptor, String *r_error) {
	_set_error(r_error, String());
	MutexLock lock(mutex);
	r_descriptor = PayloadDescriptor();
	if (!active) {
		_set_error(r_error, "StructuredTraceWriter must be opened before creating payloads.");
		return ERR_UNCONFIGURED;
	}
	if (p_mime.is_empty() || p_mime.length() > 128) {
		_set_error(r_error, "Payload MIME type is invalid.");
		return ERR_INVALID_PARAMETER;
	}
	r_descriptor.mime = p_mime;
	r_descriptor.bytes = _utf8_size(p_text);
	r_descriptor.sha256 = p_text.sha256_text();
	if (uint64_t(r_descriptor.bytes) <= options.inline_payload_max_bytes) {
		r_descriptor.inline_value = p_text;
		return OK;
	}
	return _write_text_sidecar(p_name, _payload_extension(p_mime), p_text, r_descriptor.ref_path, r_error);
}

Error StructuredTraceWriter::make_json_payload(const String &p_name, const Variant &p_value, PayloadDescriptor &r_descriptor, String *r_error) {
	return make_text_payload(p_name, "application/json", JSON::stringify(p_value), r_descriptor, r_error);
}

Error StructuredTraceWriter::reference_existing_file(const String &p_mime, const String &p_path, PayloadDescriptor &r_descriptor, String *r_error) const {
	_set_error(r_error, String());
	r_descriptor = PayloadDescriptor();
	const String path = p_path.simplify_path();
	if (p_mime.is_empty() || p_mime.length() > 128 || !path.is_absolute_path() || !FileAccess::exists(path) || DirAccess::dir_exists_absolute(path)) {
		_set_error(r_error, "Existing payload reference must be an absolute regular file.");
		return ERR_FILE_NOT_FOUND;
	}
	Ref<DirAccess> parent = DirAccess::open(path.get_base_dir());
	if (parent.is_null() || parent->is_link(path.get_file())) {
		_set_error(r_error, "Symbolic-link payload references are not allowed.");
		return ERR_INVALID_PARAMETER;
	}
	r_descriptor.mime = p_mime;
	r_descriptor.ref_path = path;
	r_descriptor.bytes = int64_t(FileAccess::get_size(path));
	r_descriptor.sha256 = FileAccess::get_sha256(path);
	r_descriptor.external = true;
	return OK;
}

void StructuredTraceWriter::close() {
	MutexLock lock(mutex);
	_close_no_lock();
}

bool StructuredTraceWriter::is_open() const {
	MutexLock lock(mutex);
	return active;
}

String StructuredTraceWriter::get_root_directory() const {
	MutexLock lock(mutex);
	return root_directory;
}

String StructuredTraceWriter::get_session_directory() const {
	MutexLock lock(mutex);
	return session_directory;
}

String StructuredTraceWriter::get_session_id() const {
	MutexLock lock(mutex);
	return session_id;
}
