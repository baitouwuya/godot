/**************************************************************************/
/*  mcp_harness_evidence_collector.cpp                                   */
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

#include "mcp_harness_evidence_collector.h"

#include "mcp_harness_report_builder.h"
#include "mcp_tool_utils.h"
#include "mcp_trace_service.h"

void MCPHarnessEvidenceCollector::set_trace_service(MCPTraceService *p_service, const Dictionary &p_project_metadata, const String &p_root_directory_override) {
	trace_service = p_service;
	trace_project_metadata = p_project_metadata.duplicate(true);
	trace_root_directory_override = p_root_directory_override;
}

MCPToolRegistry::CallResult MCPHarnessEvidenceCollector::_call_tool(const MCPHarnessJob &p_job, const String &p_tool, const Dictionary &p_arguments) const {
	if (!tool_registry) {
		MCPToolRegistry::CallResult result;
		result.status = MCPToolRegistry::CALL_TOOL_UNAVAILABLE;
		result.message = "The MCP Tool Registry is unavailable.";
		return result;
	}
	return tool_registry->call_tool(p_tool, p_arguments, p_job.context);
}

void MCPHarnessEvidenceCollector::record_trace_event(MCPHarnessJob &r_job, const String &p_event, const String &p_severity, const Dictionary &p_data, const Dictionary &p_error) {
	if (!r_job.evidence_trace || !trace_service || !trace_service->is_started()) {
		return;
	}
	String trace_error;
	if (trace_service->record_job_event(r_job.id, p_event, p_severity, p_data, Dictionary(), p_error, &trace_error) != OK && !trace_error.is_empty()) {
		r_job.evidence["traceError"] = trace_error;
	}
}

void MCPHarnessEvidenceCollector::ensure_trace_started(MCPHarnessJob &r_job) {
	if (!r_job.evidence_trace || !trace_service) {
		return;
	}
	if (!trace_service->is_started()) {
		String error;
		if (trace_service->start(trace_project_metadata, "editor-mcp-harness", trace_root_directory_override, &error) != OK) {
			r_job.evidence["trace"] = Dictionary{ { "available", false }, { "error", error } };
			return;
		}
	}
	r_job.evidence["trace"] = trace_service->get_safe_reference(r_job.id);
	if (!r_job.trace_started_event) {
		Dictionary data;
		data["name"] = r_job.plan.get("name", "harness");
		data["debuggerSession"] = r_job.debugger_session;
		data["runtimeGeneration"] = int64_t(r_job.runtime_generation);
		data["stepCount"] = r_job.steps.size();
		record_trace_event(r_job, "started", "info", data);
		r_job.trace_started_event = true;
	}
}

void MCPHarnessEvidenceCollector::begin_finalization(MCPHarnessJob &r_job, const String &p_final_state, const Dictionary &p_failure) {
	r_job.final_state = p_final_state;
	r_job.cancel_requested = false;
	if (p_final_state == "cancelled") {
		r_job.screenshot_policy = "off";
		r_job.evidence_latest_log = false;
		r_job.evidence_perf_summary = false;
	}
	if (!p_failure.is_empty()) {
		r_job.failure = p_failure;
	}
	r_job.state = "collecting_evidence";
	r_job.evidence_phase = 0;
}

void MCPHarnessEvidenceCollector::begin_failure(MCPHarnessJob &r_job, const Dictionary &p_failure) {
	if (r_job.current_step >= 0 && r_job.current_step < r_job.steps.size()) {
		const Dictionary step = r_job.steps[r_job.current_step];
		const Dictionary polling = step.get("poll", Dictionary());
		if (bool(polling.get("captureOnError", false))) {
			r_job.screenshot_policy = "always";
		}
	}
	begin_finalization(r_job, "failed", p_failure);
}

void MCPHarnessEvidenceCollector::complete_job(MCPHarnessJob &r_job) {
	r_job.child_kind = MCP_HARNESS_CHILD_NONE;
	r_job.child_id = String();
	begin_finalization(r_job, "completed");
}

int MCPHarnessEvidenceCollector::start_performance(MCPHarnessJob &r_job) {
	r_job.performance_start_attempted = true;
	Dictionary arguments;
	arguments["name"] = r_job.plan.get("name", "harness");
	arguments["topFrames"] = 10;
	arguments["runtimeGeneration"] = int64_t(r_job.runtime_generation);
	arguments["timeoutMs"] = 500;
	if (r_job.debugger_session >= 0) {
		arguments["debuggerSession"] = r_job.debugger_session;
	}
	const MCPToolRegistry::CallResult result = _call_tool(r_job, "godot.runtime.performance.start", arguments);
	if (MCPHarnessReportBuilder::is_error_result(result)) {
		r_job.evidence["performance"] = MCPHarnessReportBuilder::evidence_reference("godot.runtime.performance.start", result);
		return 1;
	}
	const Dictionary content = MCPHarnessReportBuilder::result_content(result);
	r_job.performance_job_id = content.get("jobId", String());
	if (r_job.performance_job_id.is_empty()) {
		r_job.evidence["performance"] = Dictionary{ { "tool", "godot.runtime.performance.start" }, { "ok", false }, { "error", MCPHarnessReportBuilder::failure("INVALID_PERFORMANCE_RESULT", "performance.start did not return jobId.") } };
	}
	return 1;
}

int MCPHarnessEvidenceCollector::advance(MCPHarnessJob &r_job) {
	if (r_job.evidence_phase == 0) {
		if (r_job.child_kind != MCP_HARNESS_CHILD_NONE) {
			const String tool = r_job.child_kind == MCP_HARNESS_CHILD_INPUT_SEQUENCE ? "godot.runtime.input.sequence_cancel" : "godot.runtime.wait.cancel";
			Dictionary arguments;
			arguments[r_job.child_kind == MCP_HARNESS_CHILD_INPUT_SEQUENCE ? "sequenceId" : "jobId"] = r_job.child_id;
			arguments["runtimeGeneration"] = int64_t(r_job.runtime_generation);
			if (r_job.debugger_session >= 0) {
				arguments["debuggerSession"] = r_job.debugger_session;
			}
			const MCPToolRegistry::CallResult result = _call_tool(r_job, tool, arguments);
			if (MCPHarnessReportBuilder::is_error_result(result)) {
				r_job.evidence["childCleanupError"] = MCPHarnessReportBuilder::call_error(result, tool);
			}
			r_job.child_kind = MCP_HARNESS_CHILD_NONE;
			r_job.child_id = String();
			return 1;
		}
		r_job.evidence_phase++;
	}
	if (r_job.evidence_phase == 1) {
		r_job.evidence_phase++;
		if (!r_job.performance_job_id.is_empty()) {
			Dictionary arguments;
			arguments["jobId"] = r_job.performance_job_id;
			arguments["runtimeGeneration"] = int64_t(r_job.runtime_generation);
			arguments["timeoutMs"] = 500;
			if (r_job.debugger_session >= 0) {
				arguments["debuggerSession"] = r_job.debugger_session;
			}
			const MCPToolRegistry::CallResult result = _call_tool(r_job, "godot.runtime.performance.stop", arguments);
			r_job.evidence["performance"] = MCPHarnessReportBuilder::evidence_reference("godot.runtime.performance.stop", result);
			r_job.performance_job_id = String();
			return 1;
		}
	}
	if (r_job.evidence_phase == 2) {
		r_job.evidence_phase++;
		const bool capture_screenshot = r_job.screenshot_policy == "always" || (r_job.screenshot_policy == "on_failure" && r_job.final_state != "completed");
		if (capture_screenshot) {
			Dictionary arguments;
			arguments["name"] = r_job.final_state == "completed" ? "harness_completed" : "harness_failure";
			arguments["timeoutMs"] = 500;
			if (r_job.debugger_session >= 0) {
				arguments["debuggerSession"] = r_job.debugger_session;
			}
			const MCPToolRegistry::CallResult result = _call_tool(r_job, "godot.runtime.get_screenshot", arguments);
			r_job.evidence["screenshot"] = MCPHarnessReportBuilder::evidence_reference("godot.runtime.get_screenshot", result);
			return 1;
		}
	}
	if (r_job.evidence_phase == 3) {
		r_job.evidence_phase++;
		if (r_job.final_state == "failed") {
			Dictionary arguments;
			arguments["source"] = "runtime";
			arguments["limit"] = 20;
			arguments["includeWarnings"] = true;
			arguments["deduplicate"] = true;
			arguments["runtimeGeneration"] = int64_t(r_job.runtime_generation);
			if (r_job.debugger_session >= 0) {
				arguments["debuggerSession"] = r_job.debugger_session;
			}
			const MCPToolRegistry::CallResult result = _call_tool(r_job, "godot.debug.get_errors", arguments);
			r_job.evidence["errors"] = MCPHarnessReportBuilder::evidence_reference("godot.debug.get_errors", result);
			return 1;
		}
	}
	if (r_job.evidence_phase == 4) {
		r_job.evidence_phase++;
		if (r_job.evidence_latest_log) {
			Dictionary arguments;
			arguments["maxLines"] = 200;
			arguments["view"] = "summary";
			const MCPToolRegistry::CallResult result = _call_tool(r_job, "godot.debug.get_latest_log", arguments);
			r_job.evidence["latestLog"] = MCPHarnessReportBuilder::evidence_reference("godot.debug.get_latest_log", result);
			return 1;
		}
	}
	if (!r_job.trace_final_event) {
		Dictionary data = MCPHarnessReportBuilder::summary(r_job);
		data["finalState"] = r_job.final_state;
		record_trace_event(r_job, "finished", r_job.final_state == "completed" ? "info" : "error", data, r_job.failure);
		r_job.trace_final_event = true;
		if (r_job.evidence_trace && trace_service && trace_service->is_started()) {
			r_job.evidence["trace"] = trace_service->get_safe_reference(r_job.id);
		}
	}
	r_job.state = r_job.final_state.is_empty() ? "failed" : r_job.final_state;
	r_job.cancel_requested = false;
	return 0;
}
