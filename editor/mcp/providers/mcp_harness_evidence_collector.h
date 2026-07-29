/**************************************************************************/
/*  mcp_harness_evidence_collector.h                                     */
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

class MCPTraceService;

class MCPHarnessEvidenceCollector {
public:
	void set_tool_registry(MCPToolRegistry *p_registry) { tool_registry = p_registry; }
	void set_trace_service(MCPTraceService *p_service, const Dictionary &p_project_metadata = Dictionary(), const String &p_root_directory_override = String());

	void record_trace_event(MCPHarnessJob &r_job, const String &p_event, const String &p_severity, const Dictionary &p_data = Dictionary(), const Dictionary &p_error = Dictionary());
	void ensure_trace_started(MCPHarnessJob &r_job);
	void begin_finalization(MCPHarnessJob &r_job, const String &p_final_state, const Dictionary &p_failure = Dictionary());
	void begin_failure(MCPHarnessJob &r_job, const Dictionary &p_failure);
	void complete_job(MCPHarnessJob &r_job);

	int start_performance(MCPHarnessJob &r_job);
	int advance(MCPHarnessJob &r_job);

private:
	MCPToolRegistry *tool_registry = nullptr;
	MCPTraceService *trace_service = nullptr;
	Dictionary trace_project_metadata;
	String trace_root_directory_override;

	MCPToolRegistry::CallResult _call_tool(const MCPHarnessJob &p_job, const String &p_tool, const Dictionary &p_arguments) const;
};
