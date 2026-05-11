/**************************************************************************/
/*  gdscript_lsp_cli_runner.h                                             */
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

#include "core/object/ref_counted.h"
#include "core/templates/list.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/string/ustring.h"

class GDScriptWorkspace;

class GDScriptLSPCLIRunner {
public:
	enum ExitCode {
		EXIT_OK = 0,
		EXIT_DIAGNOSTICS_FOUND = 1,
		EXIT_INVALID_ARGUMENTS = 2,
		EXIT_INITIALIZATION_FAILED = 3,
		EXIT_QUERY_FAILED = 4,
	};

	enum DiagnosticsFormat {
		DIAGNOSTICS_FORMAT_JSONL,
		DIAGNOSTICS_FORMAT_JSON,
		DIAGNOSTICS_FORMAT_SUMMARY,
	};

	enum DiagnosticsSeverity {
		DIAGNOSTICS_SEVERITY_ERROR,
		DIAGNOSTICS_SEVERITY_WARNING,
		DIAGNOSTICS_SEVERITY_ALL,
	};

	enum DiagnosticsFailOn {
		DIAGNOSTICS_FAIL_ON_ERROR,
		DIAGNOSTICS_FAIL_ON_WARNING,
		DIAGNOSTICS_FAIL_ON_ANY,
		DIAGNOSTICS_FAIL_ON_NEVER,
	};

	struct Options {
		String query;
		bool diagnostics = false;
		String file;
		int line = -1;
		int column = -1;
		String params_json;
		bool include_declaration = true;
		DiagnosticsFormat diagnostics_format = DIAGNOSTICS_FORMAT_JSONL;
		DiagnosticsSeverity diagnostics_severity = DIAGNOSTICS_SEVERITY_ALL;
		DiagnosticsFailOn diagnostics_fail_on = DIAGNOSTICS_FAIL_ON_ANY;
	};

	static bool has_entrypoint_argument(const List<String> &p_args);
	static bool parse_argument(const String &p_arg, List<String>::Element *&r_next, bool p_has_entrypoint_argument, Options &r_options, String &r_error);
	static void apply_startup_options(const Options &p_options, bool &r_editor, bool &r_project_manager, bool &r_cmdline_tool, bool &r_wait_for_import, bool &r_quiet_stdout, bool &r_recovery_mode, String &r_audio_driver, String &r_display_driver, String &r_rendering_driver, String &r_rendering_method);
	static String normalize_file_path(const String &p_file);
	static Error build_query_params(const Options &p_options, const Ref<GDScriptWorkspace> &p_workspace, Dictionary &r_params, String &r_error);
	static Dictionary summarize_diagnostics(const Array &p_diagnostics);
	static bool should_fail_for_severity(int p_severity, DiagnosticsFailOn p_fail_on);
	static bool is_enabled(const Options &p_options);
	static Error validate_options(const Options &p_options, String &r_error);
	static int run(const Options &p_options);
#ifdef TESTS_ENABLED
	static Variant run_query_for_tests(const Options &p_options, String &r_error);
	static int run_diagnostics_for_tests(const Options &p_options);
#endif
};
