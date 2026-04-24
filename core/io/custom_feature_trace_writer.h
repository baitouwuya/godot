/**************************************************************************/
/*  custom_feature_trace_writer.h                                         */
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

#pragma once

#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

class CustomFeatureTraceWriter {
public:
	struct Options {
		String base_dir_override;
		uint64_t max_event_file_bytes = 8 * 1024 * 1024;
		uint64_t inline_payload_max_bytes = 4 * 1024;
		int max_sessions = 30;
		int max_session_age_days = 14;
	};

	struct SessionInfo {
		String binary_path;
		String cwd;
		String project_path;
		String process_mode;
		int64_t pid = 0;
	};

	struct PayloadDescriptor {
		String mime;
		int64_t bytes = 0;
		String sha256;
		Variant inline_value;
		String ref_path;

		Dictionary to_dictionary() const;
	};

	struct EventRecord {
		String feature;
		String event;
		String severity = "info";
		String correlation_id;
		Dictionary data;
		Dictionary payloads;
		Dictionary error;
	};

	struct SessionDirectoryInfo {
		String path;
		uint64_t modified_time = 0;
	};

	CustomFeatureTraceWriter() = default;
	~CustomFeatureTraceWriter();

	Error initialize(const SessionInfo &p_session_info, const Options &p_options, String &r_error);
	void finalize();
	bool is_active() const;

	void set_project_path(const String &p_project_path);
	void set_process_mode(const String &p_process_mode);
	void note_feature_seen(const String &p_feature);
	Error write_event(const EventRecord &p_event, String &r_error);

	Error make_text_payload(const String &p_name, const String &p_mime, const String &p_text, PayloadDescriptor &r_descriptor, String &r_error);
	Error make_json_payload(const String &p_name, const Variant &p_value, PayloadDescriptor &r_descriptor, String &r_error);
	Error reference_existing_file(const String &p_mime, const String &p_path, PayloadDescriptor &r_descriptor, String &r_error) const;

	String get_root_dir() const { return root_dir; }
	String get_session_dir() const { return session_dir; }
	String get_session_id() const { return session_id; }

private:
	static String _sanitize_path_component(const String &p_value);
	static String _resolve_default_root_dir();
	static String _utc_now_string();
	static int64_t _unix_usec_now();
	static Error _remove_directory_recursive(const String &p_path);
	static String _payload_extension_for_mime(const String &p_mime);
	static int64_t _utf8_size(const String &p_text);
	void _reset_state_no_lock();
	void _finalize_no_lock();
	void _note_feature_seen_no_lock(const String &p_feature);

	void _cleanup_old_sessions();
	void _update_manifest();
	Error _open_next_event_file(String &r_error);
	Error _write_text_sidecar(const String &p_name, const String &p_extension, const String &p_text, String &r_ref_path, String &r_error);
	String _build_session_dir_name() const;

	Options options;
	SessionInfo session_info;
	String root_dir;
	String session_dir;
	String payload_dir;
	String session_id;
	String started_at_utc;
	String ended_at_utc;
	Vector<String> event_files;
	HashSet<String> features_seen;
	Ref<FileAccess> current_event_file;
	uint64_t current_event_file_bytes = 0;
	int current_event_file_index = 0;
	uint64_t next_payload_file_index = 1;
	uint64_t next_event_seq = 1;
	bool active = false;
	mutable Mutex mutex;
};
