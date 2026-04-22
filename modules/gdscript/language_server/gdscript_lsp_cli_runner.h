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

#include "core/string/ustring.h"

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
	};

	enum DiagnosticsSeverity {
		DIAGNOSTICS_SEVERITY_ERROR,
		DIAGNOSTICS_SEVERITY_WARNING,
		DIAGNOSTICS_SEVERITY_ALL,
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
	};

	static bool is_enabled(const Options &p_options);
	static Error validate_options(const Options &p_options, String &r_error);
	static int run(const Options &p_options);
};
