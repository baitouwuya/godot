/**************************************************************************/
/*  cli_harness_runner.h                                                  */
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
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class CLIAIInputServer;
class CLIPerformanceRecorder;

class CLIHarnessRunner {
public:
	enum ExitCode {
		EXIT_OK = 0,
		EXIT_FAILED = 1,
		EXIT_INVALID_ARGUMENTS = 2,
		EXIT_INITIALIZATION_FAILED = 3,
		EXIT_TIMEOUT = 4,
	};

	enum Format {
		FORMAT_TEXT,
		FORMAT_JSON,
		FORMAT_BOTH,
	};

	struct Options {
		bool enabled = false;
		String run_path;
		String report_path;
		int timeout_frames = 3600;
		Format format = FORMAT_TEXT;

		bool report_path_set = false;
		bool timeout_frames_set = false;
		bool format_set = false;
	};

	struct StartupOverrides {
		bool has_ai_agent_port = false;
		int ai_agent_port = 7010;
		bool perf = false;
		bool windowed = false;
	};

	static bool parse_argument(const String &p_arg, List<String>::Element *&r_next, Options &r_options, String &r_error);
	static Error validate_options(const Options &p_options, bool p_found_project, bool p_editor, bool p_project_manager, bool p_cmdline_tool, String &r_error);
	static bool is_enabled(const Options &p_options) { return p_options.enabled; }
	static String get_format_name(Format p_format);
	static String resolve_script_path(const String &p_path);
	static Error read_script_file(const String &p_path, Dictionary &r_script, String &r_error);
	static Error validate_script(const Dictionary &p_script, String &r_error);
	static Error extract_startup_overrides(const Dictionary &p_script, StartupOverrides &r_overrides, String &r_error);
	static bool build_ai_request_for_step(const Dictionary &p_step, int p_id, Dictionary &r_request, Dictionary &r_step_meta, String &r_error);

	CLIHarnessRunner(const Options &p_options, const String &p_project_path, CLIAIInputServer *p_ai_server, CLIPerformanceRecorder *p_perf_recorder = nullptr);

	Error initialize(String &r_error);
	void poll();
	bool is_finished() const { return finished; }
	int get_exit_code() const { return exit_code; }
	Dictionary get_report() const { return report; }
	String get_report_path() const { return effective_report_path; }

private:
	static bool _consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error);
	static bool _get_bool(const Dictionary &p_dict, const String &p_key, bool p_default, bool &r_value, String &r_error);
	static bool _get_int(const Dictionary &p_dict, const String &p_key, int p_default, int &r_value, String &r_error);
	static bool _get_double(const Dictionary &p_dict, const String &p_key, double p_default, double &r_value, String &r_error);
	static bool _get_string(const Dictionary &p_dict, const String &p_key, const String &p_default, String &r_value, String &r_error);
	static bool _dictionary_has_only_keys(const Dictionary &p_dict, const Vector<String> &p_keys, String &r_error);
	static String _utc_now_string();
	static Dictionary _make_error(const String &p_code, const String &p_message);
	static bool _step_has_command(const Dictionary &p_step);
	static bool _step_has_expectation(const Dictionary &p_step);
	static Error _build_expect_request(const Dictionary &p_step, int p_id, Dictionary &r_request, String &r_error);
	static Error _build_alias_request(const Dictionary &p_step, int p_id, Dictionary &r_request, Dictionary &r_step_meta, String &r_error);
	static void _append_key_tap(Array &r_ops, const String &p_keycode);
	static void _append_action_tap(Array &r_ops, const String &p_action, int p_frames);

	void _record_trace_event(const String &p_event, const String &p_severity, const Dictionary &p_data = Dictionary(), const Dictionary &p_payloads = Dictionary(), const Dictionary &p_error = Dictionary());
	void _start_step();
	void _finish_step(const Dictionary &p_response);
	void _finish_with_error(int p_exit_code, const String &p_code, const String &p_message, const Dictionary &p_response = Dictionary());
	void _finish_success();
	void _capture_screenshot_evidence(const String &p_name, Dictionary *r_bundle = nullptr);
	void _capture_failure_evidence();
	void _capture_success_evidence();
	void _collect_latest_log_summary();
	void _finalize_report();
	void _write_report();
	void _print_output() const;
	String _build_text_output() const;
	String _resolve_report_path() const;

	Options options;
	String project_path;
	CLIAIInputServer *ai_server = nullptr;
	CLIPerformanceRecorder *perf_recorder = nullptr;

	Dictionary script;
	Array steps;
	Dictionary evidence_options;
	Dictionary report;
	Array report_steps;
	Array assertions;
	Array evidence_items;
	Dictionary failure_bundle;
	String effective_report_path;
	String correlation_id;
	String harness_name;

	uint64_t started_frame = 0;
	uint64_t started_usec = 0;
	int current_step = 0;
	bool waiting_for_response = false;
	bool finished = false;
	int exit_code = EXIT_OK;
};
