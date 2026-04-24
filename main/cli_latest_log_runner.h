/**************************************************************************/
/*  cli_latest_log_runner.h                                               */
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
#include "core/templates/list.h"
#include "core/variant/variant.h"

class CLILatestLogRunner {
public:
	enum ExitCode {
		EXIT_OK = 0,
		EXIT_NOT_FOUND = 1,
		EXIT_INVALID_ARGUMENTS = 2,
		EXIT_READ_FAILED = 3,
	};

	enum Format {
		FORMAT_TEXT,
		FORMAT_JSON,
		FORMAT_BOTH,
		FORMAT_RAW,
	};

	struct Options {
		bool enabled = false;
		int lines = 200;
		bool lines_set = false;
		Format format = FORMAT_TEXT;
		bool format_set = false;
	};

	struct ReadResult {
		String text;
		int total_lines = 0;
		int shown_lines = 0;
		bool truncated = false;
	};

	static bool parse_argument(const String &p_arg, List<String>::Element *&r_next, Options &r_options, String &r_error);
	static Error validate_options(const Options &p_options, bool p_found_project, bool p_editor, bool p_project_manager, String &r_error);
	static bool is_enabled(const Options &p_options) { return p_options.enabled; }

	static String resolve_base_log_path(const String &p_log_file_override);
	static Error find_latest_log_path(const String &p_base_log_path, String &r_latest_log_path);
	static Error read_log(const String &p_log_path, int p_lines, ReadResult &r_result, String &r_error);
	static String get_format_name(Format p_format);
	static Error build_output(const String &p_log_path, const String &p_base_log_path, const Options &p_options, String &r_output, Dictionary &r_summary, String &r_error);
	static int run(const Options &p_options, const String &p_log_file_override);

private:
	static bool _consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error);
	static void _print_stdout(const String &p_text);
};
