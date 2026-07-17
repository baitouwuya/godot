/**************************************************************************/
/*  mcp_trace_service.h                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#pragma once

#include "core/io/structured_trace_writer.h"
#include "core/os/mutex.h"

class MCPTraceService {
public:
	static constexpr int MAX_DEPTH = 8;
	static constexpr int MAX_CONTAINER_ENTRIES = 128;
	static constexpr int MAX_STRING_CHARACTERS = 4096;
	static constexpr uint64_t MAX_STRUCTURED_FIELD_BYTES = 64 * 1024;

	~MCPTraceService();

	Error start(const Dictionary &p_project_metadata, const String &p_process_mode, const String &p_root_directory_override = String(), String *r_error = nullptr);
	Error record_mcp_request(const String &p_method, const String &p_session_id, const String &p_correlation_id, const Dictionary &p_arguments, const Dictionary &p_result = Dictionary(), const Dictionary &p_error = Dictionary(), String *r_error = nullptr);
	Error record_tool_call(const String &p_tool_name, const String &p_session_id, const String &p_correlation_id, const Dictionary &p_arguments, const Dictionary &p_result = Dictionary(), const Dictionary &p_error = Dictionary(), String *r_error = nullptr);
	Error record_job_event(const String &p_job_id, const String &p_event, const String &p_severity, const Dictionary &p_data, const Dictionary &p_payloads = Dictionary(), const Dictionary &p_error = Dictionary(), String *r_error = nullptr);
	void stop();

	bool is_started() const;
	String get_session_directory() const;
	Dictionary get_safe_reference(const String &p_correlation_id = String()) const;

private:
	static void _set_error(String *r_error, const String &p_message);
	static bool _is_secret_key(const String &p_key);
	static bool _is_session_key(const String &p_key);
	static bool _is_path_key(const String &p_key);
	static String _session_hash(const String &p_session_id);
	static String _sanitize_path(const String &p_path);
	static Variant _sanitize_variant(const Variant &p_value, const String &p_key_hint, int p_depth);
	static Dictionary _sanitize_dictionary(const Dictionary &p_value, int p_depth = 0);
	static Dictionary _bounded_dictionary(const Dictionary &p_value);
	static String _bounded_text(const String &p_value, int p_max_characters);

	Error _record(const String &p_category, const String &p_event, const String &p_severity, const String &p_correlation_id, const Dictionary &p_data, const Dictionary &p_error, String *r_error);

	StructuredTraceWriter writer;
	bool started = false;
	mutable Mutex mutex;
};
