/**************************************************************************/
/*  mcp_harness_step_executor.h                                          */
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

class MCPHarnessEvidenceCollector;

class MCPHarnessStepExecutor {
public:
	void set_tool_registry(MCPToolRegistry *p_registry) { tool_registry = p_registry; }
	void set_evidence_collector(MCPHarnessEvidenceCollector *p_collector) { evidence_collector = p_collector; }

	int advance(MCPHarnessJob &r_job);
	int advance_cancellation(MCPHarnessJob &r_job);

private:
	MCPToolRegistry *tool_registry = nullptr;
	MCPHarnessEvidenceCollector *evidence_collector = nullptr;

	Dictionary _prepare_arguments(const MCPHarnessJob &p_job, const Dictionary &p_step) const;
	MCPToolRegistry::CallResult _call_tool(const MCPHarnessJob &p_job, const String &p_tool, const Dictionary &p_arguments) const;
	int _advance_child(MCPHarnessJob &r_job);
	void _record_trace(MCPHarnessJob &r_job, const String &p_event, const String &p_severity, const Dictionary &p_data = Dictionary(), const Dictionary &p_error = Dictionary());
};
