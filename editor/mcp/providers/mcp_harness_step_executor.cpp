/**************************************************************************/
/*  mcp_harness_step_executor.cpp                                        */
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

#include "mcp_harness_step_executor.h"

#include "mcp_harness_evidence_collector.h"
#include "mcp_harness_report_builder.h"

Dictionary MCPHarnessStepExecutor::_prepare_arguments(const MCPHarnessJob &p_job, const Dictionary &p_step) const {
	Dictionary arguments = Dictionary(p_step.get("arguments", Dictionary())).duplicate(true);
	const PackedStringArray requirements = p_step.get("contextRequirements", PackedStringArray());
	if (requirements.has("runtimeGeneration")) {
		arguments["runtimeGeneration"] = int64_t(p_job.runtime_generation);
	}
	const String tool = p_step.get("tool", String());
	if (p_job.debugger_session >= 0 && tool != "godot.runtime.get_state" && !bool(p_step.get("futureTool", false))) {
		arguments["debuggerSession"] = p_job.debugger_session;
	}
	return arguments;
}

MCPToolRegistry::CallResult MCPHarnessStepExecutor::_call_tool(const MCPHarnessJob &p_job, const String &p_tool, const Dictionary &p_arguments) const {
	if (!tool_registry) {
		MCPToolRegistry::CallResult result;
		result.status = MCPToolRegistry::CALL_TOOL_UNAVAILABLE;
		result.message = "The MCP Tool Registry is unavailable.";
		return result;
	}
	return tool_registry->call_tool(p_tool, p_arguments, p_job.context);
}

void MCPHarnessStepExecutor::_record_trace(MCPHarnessJob &r_job, const String &p_event, const String &p_severity, const Dictionary &p_data, const Dictionary &p_error) {
	if (evidence_collector) {
		evidence_collector->record_trace_event(r_job, p_event, p_severity, p_data, p_error);
	}
}

int MCPHarnessStepExecutor::_advance_child(MCPHarnessJob &r_job) {
	String tool;
	Dictionary arguments;
	if (r_job.child_kind == MCP_HARNESS_CHILD_INPUT_SEQUENCE) {
		tool = "godot.runtime.input.sequence_status";
		arguments["sequenceId"] = r_job.child_id;
	} else {
		tool = "godot.runtime.wait.status";
		arguments["jobId"] = r_job.child_id;
		arguments["runtimeGeneration"] = int64_t(r_job.runtime_generation);
		if (r_job.debugger_session >= 0) {
			arguments["debuggerSession"] = r_job.debugger_session;
		}
	}
	const MCPToolRegistry::CallResult result = _call_tool(r_job, tool, arguments);
	if (MCPHarnessReportBuilder::is_error_result(result)) {
		const Dictionary report_step = MCPHarnessReportBuilder::record_step_end(r_job, result, false);
		const Dictionary error = MCPHarnessReportBuilder::call_error(result, tool);
		_record_trace(r_job, "step_completed", "error", report_step, error);
		if (evidence_collector) {
			evidence_collector->begin_failure(r_job, error);
		}
		return 1;
	}
	const Dictionary content = MCPHarnessReportBuilder::result_content(result);
	const String state = content.get("state", String());
	const bool running = r_job.child_kind == MCP_HARNESS_CHILD_INPUT_SEQUENCE ? (state == "running" || state == "waiting" || state == "queued") : state == "running";
	if (running) {
		return 1;
	}
	const bool passed = r_job.child_kind == MCP_HARNESS_CHILD_INPUT_SEQUENCE ? state == "completed" : state == "succeeded";
	const Dictionary report_step = MCPHarnessReportBuilder::record_step_end(r_job, result, passed);
	_record_trace(r_job, "step_completed", passed ? "info" : "error", report_step, passed ? Dictionary() : Dictionary(report_step.get("error", Dictionary())));
	r_job.child_kind = MCP_HARNESS_CHILD_NONE;
	r_job.child_id = String();
	if (!passed) {
		if (evidence_collector) {
			evidence_collector->begin_failure(r_job, MCPHarnessReportBuilder::failure("HARNESS_CHILD_FAILED", String(content.get("failure", "Asynchronous Harness step failed.")), content));
		}
		return 1;
	}
	r_job.current_step++;
	if (r_job.current_step >= r_job.steps.size() && evidence_collector) {
		evidence_collector->complete_job(r_job);
	}
	return 1;
}

int MCPHarnessStepExecutor::advance_cancellation(MCPHarnessJob &r_job) {
	if (r_job.child_kind == MCP_HARNESS_CHILD_NONE) {
		if (evidence_collector) {
			evidence_collector->begin_finalization(r_job, "cancelled", MCPHarnessReportBuilder::failure("CANCELLED", "The Harness job was cancelled."));
		}
		return 0;
	}
	const String tool = r_job.child_kind == MCP_HARNESS_CHILD_INPUT_SEQUENCE ? "godot.runtime.input.sequence_cancel" : "godot.runtime.wait.cancel";
	Dictionary arguments;
	arguments[r_job.child_kind == MCP_HARNESS_CHILD_INPUT_SEQUENCE ? "sequenceId" : "jobId"] = r_job.child_id;
	arguments["runtimeGeneration"] = int64_t(r_job.runtime_generation);
	if (r_job.debugger_session >= 0) {
		arguments["debuggerSession"] = r_job.debugger_session;
	}
	const MCPToolRegistry::CallResult result = _call_tool(r_job, tool, arguments);
	if (MCPHarnessReportBuilder::is_error_result(result)) {
		r_job.evidence["cancellationError"] = MCPHarnessReportBuilder::call_error(result, tool);
	}
	r_job.child_kind = MCP_HARNESS_CHILD_NONE;
	r_job.child_id = String();
	if (evidence_collector) {
		evidence_collector->begin_finalization(r_job, "cancelled", MCPHarnessReportBuilder::failure("CANCELLED", "The Harness job was cancelled."));
	}
	return 1;
}

int MCPHarnessStepExecutor::advance(MCPHarnessJob &r_job) {
	if (!evidence_collector) {
		return 0;
	}
	if (r_job.child_kind != MCP_HARNESS_CHILD_NONE) {
		return _advance_child(r_job);
	}
	if (r_job.current_step >= r_job.steps.size()) {
		evidence_collector->complete_job(r_job);
		return 0;
	}

	const Dictionary step = r_job.steps[r_job.current_step];
	const String tool = step.get("tool", String());
	const Dictionary arguments = _prepare_arguments(r_job, step);
	const Dictionary report_start = MCPHarnessReportBuilder::record_step_start(r_job, step, arguments);
	_record_trace(r_job, "step_started", "info", report_start);
	const MCPToolRegistry::CallResult result = _call_tool(r_job, tool, arguments);
	if (MCPHarnessReportBuilder::is_error_result(result)) {
		const Dictionary report_step = MCPHarnessReportBuilder::record_step_end(r_job, result, false);
		const Dictionary error = MCPHarnessReportBuilder::call_error(result, tool);
		_record_trace(r_job, "step_completed", "error", report_step, error);
		evidence_collector->begin_failure(r_job, error);
		return 1;
	}
	const Dictionary content = MCPHarnessReportBuilder::result_content(result);
	if (tool == "godot.runtime.input.sequence") {
		const String child_id = content.get("sequenceId", String());
		if (child_id.is_empty()) {
			const Dictionary report_step = MCPHarnessReportBuilder::record_step_end(r_job, result, false);
			const Dictionary error = MCPHarnessReportBuilder::failure("INVALID_CHILD_RESULT", "input.sequence did not return sequenceId.");
			_record_trace(r_job, "step_completed", "error", report_step, error);
			evidence_collector->begin_failure(r_job, error);
		} else {
			r_job.child_kind = MCP_HARNESS_CHILD_INPUT_SEQUENCE;
			r_job.child_id = child_id;
			Dictionary report_step = r_job.report_steps[r_job.report_steps.size() - 1];
			report_step["startResult"] = content;
			r_job.report_steps[r_job.report_steps.size() - 1] = report_step;
		}
		return 1;
	}
	if (tool == "godot.runtime.wait.start") {
		const String child_id = content.get("jobId", String());
		if (child_id.is_empty()) {
			const Dictionary report_step = MCPHarnessReportBuilder::record_step_end(r_job, result, false);
			const Dictionary error = MCPHarnessReportBuilder::failure("INVALID_CHILD_RESULT", "runtime.wait.start did not return jobId.");
			_record_trace(r_job, "step_completed", "error", report_step, error);
			evidence_collector->begin_failure(r_job, error);
		} else {
			r_job.child_kind = MCP_HARNESS_CHILD_RUNTIME_WAIT;
			r_job.child_id = child_id;
			Dictionary report_step = r_job.report_steps[r_job.report_steps.size() - 1];
			report_step["startResult"] = content;
			r_job.report_steps[r_job.report_steps.size() - 1] = report_step;
		}
		return 1;
	}

	const Dictionary report_step = MCPHarnessReportBuilder::record_step_end(r_job, result, true);
	_record_trace(r_job, "step_completed", "info", report_step);
	r_job.current_step++;
	if (r_job.current_step >= r_job.steps.size()) {
		evidence_collector->complete_job(r_job);
	}
	return 1;
}
