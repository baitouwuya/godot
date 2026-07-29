/**************************************************************************/
/*  mcp_harness_job.h                                                    */
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

#include "core/mcp/mcp_tool_registry.h"

enum MCPHarnessChildKind {
	MCP_HARNESS_CHILD_NONE,
	MCP_HARNESS_CHILD_INPUT_SEQUENCE,
	MCP_HARNESS_CHILD_RUNTIME_WAIT,
};

struct MCPHarnessJob {
	String id;
	String session_id;
	int debugger_session = -1;
	uint64_t runtime_generation = 0;
	MCPToolCallContext context;
	Dictionary plan;
	Array steps;
	Array report_steps;
	Array assertions;
	Dictionary failure;
	Dictionary evidence;
	String state = "running";
	String final_state;
	int current_step = 0;
	MCPHarnessChildKind child_kind = MCP_HARNESS_CHILD_NONE;
	String child_id;
	bool cancel_requested = false;
	String screenshot_policy = "on_failure";
	bool evidence_latest_log = false;
	bool evidence_trace = true;
	bool evidence_perf_summary = false;
	bool performance_start_attempted = false;
	String performance_job_id;
	bool trace_started_event = false;
	bool trace_final_event = false;
	int evidence_phase = 0;
};
