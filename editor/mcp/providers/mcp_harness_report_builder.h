/**************************************************************************/
/*  mcp_harness_report_builder.h                                         */
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
/* "Software"), to deal in the Software without restriction, including   */
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

#include "mcp_harness_job.h"

class MCPHarnessReportBuilder {
public:
	static bool is_terminal(const MCPHarnessJob &p_job);
	static bool is_error_result(const MCPToolRegistry::CallResult &p_result);
	static Dictionary result_content(const MCPToolRegistry::CallResult &p_result);
	static Dictionary failure(const String &p_code, const String &p_message, const Dictionary &p_details = Dictionary());
	static Dictionary call_error(const MCPToolRegistry::CallResult &p_result, const String &p_tool);
	static Dictionary summary(const MCPHarnessJob &p_job);
	static Dictionary report(const MCPHarnessJob &p_job);
	static Dictionary evidence_reference(const String &p_tool, const MCPToolRegistry::CallResult &p_result);
	static Dictionary record_step_start(MCPHarnessJob &r_job, const Dictionary &p_step, const Dictionary &p_arguments);
	static Dictionary record_step_end(MCPHarnessJob &r_job, const MCPToolRegistry::CallResult &p_result, bool p_passed);
};
