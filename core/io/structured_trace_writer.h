/**************************************************************************/
/*  structured_trace_writer.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/string/ustring.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class StructuredTraceWriter {
public:
	static constexpr int MAX_SESSIONS = 30;
	static constexpr int MAX_SESSION_AGE_DAYS = 14;
	static constexpr uint64_t MAX_EVENT_RECORD_BYTES = 1024 * 1024;
	static constexpr uint64_t MAX_METADATA_BYTES = 64 * 1024;

	struct Options {
		String root_directory_override;
		uint64_t max_event_file_bytes = 8 * 1024 * 1024;
		uint64_t inline_payload_max_bytes = 4 * 1024;
		int max_sessions = MAX_SESSIONS;
		int max_session_age_days = MAX_SESSION_AGE_DAYS;
	};

	struct PayloadDescriptor {
		String mime;
		int64_t bytes = 0;
		String sha256;
		Variant inline_value;
		String ref_path;
		bool external = false;

		Dictionary to_dictionary() const;
	};

	StructuredTraceWriter() = default;
	~StructuredTraceWriter();

	Error open(const Dictionary &p_project_metadata, const Dictionary &p_process_metadata, String *r_error = nullptr);
	Error open(const Dictionary &p_project_metadata, const Dictionary &p_process_metadata, const Options &p_options, String *r_error = nullptr);
	Error record(const String &p_category, const String &p_event, const String &p_severity, const String &p_correlation_id, const Dictionary &p_data, const Dictionary &p_payloads = Dictionary(), const Dictionary &p_error = Dictionary(), String *r_error = nullptr);
	void close();

	Error make_text_payload(const String &p_name, const String &p_mime, const String &p_text, PayloadDescriptor &r_descriptor, String *r_error = nullptr);
	Error make_json_payload(const String &p_name, const Variant &p_value, PayloadDescriptor &r_descriptor, String *r_error = nullptr);
	Error reference_existing_file(const String &p_mime, const String &p_path, PayloadDescriptor &r_descriptor, String *r_error = nullptr) const;

	bool is_open() const;
	String get_root_directory() const;
	String get_session_directory() const;
	String get_session_id() const;

private:
	struct SessionDirectoryInfo {
		String path;
		uint64_t modified_time = 0;
	};

	struct SessionSortComparator {
		bool operator()(const SessionDirectoryInfo &p_left, const SessionDirectoryInfo &p_right) const;
	};

	static void _set_error(String *r_error, const String &p_message);
	static String _default_root_directory();
	static String _utc_now();
	static int64_t _unix_usec_now();
	static int64_t _utf8_size(const String &p_text);
	static String _sanitize_component(const String &p_value);
	static String _payload_extension(const String &p_mime);
	static Error _remove_directory_recursive(const String &p_path);
	static bool _is_valid_severity(const String &p_severity);
	static bool _metadata_is_bounded(const Dictionary &p_metadata);

	void _reset_no_lock();
	void _close_no_lock();
	Error _validate_options(const Options &p_options, String *r_error) const;
	Error _cleanup_old_sessions(String *r_error);
	Error _write_manifest(String *r_error = nullptr);
	Error _open_next_event_file(String *r_error);
	Error _write_text_sidecar(const String &p_name, const String &p_extension, const String &p_text, String &r_ref_path, String *r_error);
	String _build_session_directory_name() const;

	Options options;
	Dictionary project_metadata;
	Dictionary process_metadata;
	String root_directory;
	String session_directory;
	String payload_directory;
	String session_id;
	String started_at_utc;
	String ended_at_utc;
	Vector<String> event_files;
	HashSet<String> categories_seen;
	Ref<FileAccess> current_event_file;
	uint64_t current_event_file_bytes = 0;
	int current_event_file_index = 0;
	uint64_t next_payload_file_index = 1;
	uint64_t next_event_sequence = 1;
	bool active = false;
	mutable Mutex mutex;
};
