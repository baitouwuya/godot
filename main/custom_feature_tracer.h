/**************************************************************************/
/*  custom_feature_tracer.h                                               */
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

#include "core/io/custom_feature_trace_writer.h"
#include "core/io/logger.h"
#include "core/os/mutex.h"

class ScriptBacktrace;

class CustomFeatureTracer {
public:
	static constexpr const char *FEATURE_GDSCRIPT_LSP_CLI = "gdscript_lsp_cli";
	static constexpr const char *FEATURE_CLI_PERF_RECORDER = "cli_perf_recorder";
	static constexpr const char *FEATURE_LATEST_LOG_CLI = "latest_log_cli";
	static constexpr const char *FEATURE_RUNTIME_AI_AGENT_CONTROL = "runtime_ai_agent_control";

	struct StartupOptions {
		String binary_path;
		String cwd;
		String project_path;
		String process_mode;
		String base_dir_override;
		int64_t pid = 0;
	};

	class ScopedEventContext {
	public:
		ScopedEventContext() = default;
		ScopedEventContext(const String &p_feature, const String &p_correlation_id = String());
		~ScopedEventContext();

		ScopedEventContext(const ScopedEventContext &) = delete;
		ScopedEventContext &operator=(const ScopedEventContext &) = delete;
		ScopedEventContext(ScopedEventContext &&p_other) noexcept;
		ScopedEventContext &operator=(ScopedEventContext &&p_other) noexcept;

	private:
		bool active = false;
		String previous_feature;
		String previous_correlation_id;
	};

	static CustomFeatureTracer *get_singleton();
	static Error initialize_singleton(const StartupOptions &p_options, String &r_error);
	static void shutdown_singleton();
	static bool has_singleton();
	static String get_current_feature();
	static String get_current_correlation_id();
	static void update_process_context(const String &p_project_path, const String &p_process_mode);

	String next_correlation_id(const String &p_feature);
	String correlation_id_from_external_id(const String &p_feature, const Variant &p_external_id);

	void record_event(const String &p_feature, const String &p_event, const String &p_severity, const String &p_correlation_id = String(), const Dictionary &p_data = Dictionary(), const Dictionary &p_payloads = Dictionary(), const Dictionary &p_error = Dictionary());
	void record_error_event(const String &p_feature, const String &p_event, const String &p_correlation_id, const String &p_code, const String &p_message, const Dictionary &p_data = Dictionary(), const Dictionary &p_payloads = Dictionary());

	bool add_text_payload(Dictionary &r_payloads, const String &p_name, const String &p_mime, const String &p_text);
	bool add_json_payload(Dictionary &r_payloads, const String &p_name, const Variant &p_value);
	bool add_existing_file_payload(Dictionary &r_payloads, const String &p_name, const String &p_mime, const String &p_path);

	void capture_logger_error(const char *p_function, const char *p_file, int p_line, const char *p_code, const char *p_rationale, Logger::ErrorType p_type, const Vector<Ref<ScriptBacktrace>> &p_script_backtraces);

#ifdef TESTS_ENABLED
	String get_session_dir_for_tests() const;
	String get_root_dir_for_tests() const;
#endif

private:
	friend class ScopedEventContext;

	CustomFeatureTracer() = default;

	static void _set_thread_context(const String &p_feature, const String &p_correlation_id);
	static String _get_thread_feature();
	static String _get_thread_correlation_id();

	CustomFeatureTraceWriter writer;
	uint64_t correlation_sequence = 1;
	mutable Mutex mutex;
};
