/**************************************************************************/
/*  mcp_harness_service.cpp                                              */
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

#include "mcp_harness_service.h"

#include "mcp_harness_plan_compiler.h"
#include "mcp_tool_utils.h"
#include "mcp_trace_service.h"

namespace {

constexpr int MAX_RETAINED_JOBS = 32;

static Dictionary _error(const String &p_code, const String &p_message, const Dictionary &p_details = Dictionary()) {
	return MCPToolUtils::make_error_result(p_code, p_message, p_details);
}

static Dictionary _failure(const String &p_code, const String &p_message, const Dictionary &p_details = Dictionary()) {
	Dictionary failure;
	failure["code"] = p_code;
	failure["message"] = p_message;
	if (!p_details.is_empty()) {
		failure["details"] = p_details;
	}
	return failure;
}

} // namespace

void MCPHarnessService::set_trace_service(MCPTraceService *p_service, const Dictionary &p_project_metadata, const String &p_root_directory_override) {
	trace_service = p_service;
	trace_project_metadata = p_project_metadata.duplicate(true);
	trace_root_directory_override = p_root_directory_override;
}

String MCPHarnessService::_session_id(const MCPToolCallContext &p_context) {
	const Variant id = p_context.session.get("sessionId", Variant());
	return id.get_type() == Variant::STRING ? String(id) : String();
}

bool MCPHarnessService::_is_terminal(const Job &p_job) {
	return p_job.state == "completed" || p_job.state == "failed" || p_job.state == "cancelled";
}

bool MCPHarnessService::_is_error_result(const MCPToolRegistry::CallResult &p_result) {
	return p_result.status != MCPToolRegistry::CALL_OK || bool(p_result.result.get("isError", false));
}

Dictionary MCPHarnessService::_result_content(const MCPToolRegistry::CallResult &p_result) {
	return p_result.status == MCPToolRegistry::CALL_OK ? Dictionary(p_result.result.get("structuredContent", Dictionary())) : Dictionary();
}

Dictionary MCPHarnessService::_call_error(const MCPToolRegistry::CallResult &p_result, const String &p_tool) {
	if (p_result.status != MCPToolRegistry::CALL_OK) {
		return _failure("TOOL_DISPATCH_FAILED", p_result.message.is_empty() ? "Harness tool dispatch failed: " + p_tool : p_result.message);
	}
	const Dictionary structured = p_result.result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return _failure(error.get("code", "TOOL_FAILED"), error.get("message", "Harness tool returned an error: " + p_tool), error.get("details", Dictionary()));
}

Dictionary MCPHarnessService::_summary(const Job &p_job) {
	Dictionary summary;
	summary["jobId"] = p_job.id;
	summary["state"] = p_job.state;
	summary["name"] = p_job.plan.get("name", "harness");
	summary["debuggerSession"] = p_job.debugger_session;
	summary["runtimeGeneration"] = int64_t(p_job.runtime_generation);
	summary["currentStep"] = p_job.current_step;
	summary["stepCount"] = p_job.steps.size();
	int completed_steps = 0;
	for (int i = 0; i < p_job.report_steps.size(); i++) {
		if (String(Dictionary(p_job.report_steps[i]).get("state", String())) != "running") {
			completed_steps++;
		}
	}
	summary["completedStepCount"] = completed_steps;
	if (!p_job.child_id.is_empty()) {
		summary[p_job.child_kind == CHILD_INPUT_SEQUENCE ? "sequenceId" : "waitJobId"] = p_job.child_id;
	}
	if (!p_job.failure.is_empty()) {
		summary["failure"] = p_job.failure;
	}
	return summary;
}

Dictionary MCPHarnessService::_report(const Job &p_job) {
	Dictionary report = _summary(p_job);
	report["steps"] = p_job.report_steps;
	report["assertions"] = p_job.assertions;
	report["evidence"] = p_job.evidence;
	report["evidencePolicy"] = p_job.plan.get("evidencePolicy", Dictionary());
	return report;
}

Dictionary MCPHarnessService::_evidence_reference(const String &p_tool, const MCPToolRegistry::CallResult &p_result) {
	Dictionary reference;
	reference["tool"] = p_tool;
	reference["ok"] = !_is_error_result(p_result);
	if (p_result.status == MCPToolRegistry::CALL_OK) {
		reference["structuredContent"] = p_result.result.get("structuredContent", Dictionary());
	} else {
		reference["error"] = _call_error(p_result, p_tool);
	}
	return reference;
}

int MCPHarnessService::_active_job_count(const String &p_session_id) const {
	int count = 0;
	for (const KeyValue<String, Job> &entry : jobs) {
		if (!_is_terminal(entry.value) && (p_session_id.is_empty() || entry.value.session_id == p_session_id)) {
			count++;
		}
	}
	return count;
}

void MCPHarnessService::_prune_jobs() {
	while (job_order.size() >= MAX_RETAINED_JOBS) {
		int remove_index = -1;
		for (int i = 0; i < job_order.size(); i++) {
			const Job *job = jobs.getptr(job_order[i]);
			if (!job || _is_terminal(*job)) {
				remove_index = i;
				break;
			}
		}
		if (remove_index < 0) {
			return;
		}
		jobs.erase(job_order[remove_index]);
		job_order.remove_at(remove_index);
	}
	poll_cursor = job_order.is_empty() ? 0 : poll_cursor % job_order.size();
}

MCPHarnessService::Job *MCPHarnessService::_find_owned_job(const Dictionary &p_arguments, const MCPToolCallContext &p_context, Dictionary &r_error) {
	const Variant job_value = p_arguments.get("jobId", Variant());
	if (job_value.get_type() != Variant::STRING || String(job_value).is_empty()) {
		r_error = _error("INVALID_ARGUMENTS", "jobId must be a non-empty string.");
		return nullptr;
	}
	Job *job = jobs.getptr(String(job_value));
	const String session_id = _session_id(p_context);
	if (!job || session_id.is_empty() || job->session_id != session_id) {
		r_error = _error("HARNESS_JOB_NOT_FOUND", "Harness job was not found for this MCP session.");
		return nullptr;
	}
	int64_t generation = 0;
	int64_t debugger_session = job->debugger_session;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("runtimeGeneration", Variant()), 1, INT64_MAX, generation) ||
			(p_arguments.has("debuggerSession") && !MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, debugger_session)) ||
			uint64_t(generation) != job->runtime_generation || debugger_session != job->debugger_session) {
		r_error = _error("STALE_HARNESS_JOB", "Harness job belongs to a different runtime generation or debugger session.");
		return nullptr;
	}
	return job;
}

const MCPHarnessService::Job *MCPHarnessService::_find_owned_job(const Dictionary &p_arguments, const MCPToolCallContext &p_context, Dictionary &r_error) const {
	return const_cast<MCPHarnessService *>(this)->_find_owned_job(p_arguments, p_context, r_error);
}

Dictionary MCPHarnessService::start(const Dictionary &p_arguments, const MCPToolCallContext &p_context) {
	if (!tool_registry) {
		return _error("UNAVAILABLE", "The MCP Tool Registry is unavailable.");
	}
	const String session_id = _session_id(p_context);
	if (session_id.is_empty()) {
		return _error("MCP_SESSION_REQUIRED", "Harness jobs require an initialized MCP session.");
	}
	const Variant script_value = p_arguments.get("script", Variant());
	int64_t generation = 0;
	int64_t debugger_session = -1;
	if (script_value.get_type() != Variant::DICTIONARY ||
			!MCPToolUtils::try_get_json_integer(p_arguments.get("runtimeGeneration", Variant()), 1, INT64_MAX, generation) ||
			(p_arguments.has("debuggerSession") && !MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, debugger_session))) {
		return _error("INVALID_ARGUMENTS", "script, runtimeGeneration, or debuggerSession is invalid.");
	}
	if (_active_job_count() >= MAX_ACTIVE_JOBS) {
		return _error("HARNESS_JOB_LIMIT", "The global active Harness job limit is full.");
	}
	if (_active_job_count(session_id) >= MAX_SESSION_JOBS) {
		return _error("HARNESS_SESSION_LIMIT", "This MCP session already owns the maximum active Harness jobs.");
	}

	Dictionary plan;
	String compile_error;
	if (MCPHarnessPlanCompiler::compile(script_value, plan, &compile_error) != OK) {
		return _error("INVALID_SCRIPT", compile_error);
	}
	const Array steps = plan.get("steps", Array());
	for (int i = 0; i < steps.size(); i++) {
		const Dictionary step = steps[i];
		const String tool = step.get("tool", String());
		if (tool.begins_with("godot.runtime.harness.") || tool == "godot.automation.batch") {
			return _error("RECURSIVE_HARNESS", "Harness plans cannot invoke Harness or automation batch tools.");
		}
	}

	_prune_jobs();
	Job job;
	job.id = "harness-" + String::num_uint64(next_job_id++);
	job.session_id = session_id;
	job.debugger_session = int(debugger_session);
	job.runtime_generation = uint64_t(generation);
	job.context = p_context;
	job.context.request_id = Variant();
	job.plan = plan;
	job.steps = steps;
	const Dictionary evidence_policy = plan.get("evidencePolicy", Dictionary());
	job.screenshot_policy = evidence_policy.get("screenshots", "on_failure");
	job.evidence_latest_log = evidence_policy.get("latestLog", false);
	job.evidence_trace = evidence_policy.get("trace", true);
	job.evidence_perf_summary = evidence_policy.get("perfSummary", false);
	jobs.insert(job.id, job);
	job_order.push_back(job.id);
	_ensure_trace_started(jobs[job.id]);
	return MCPToolUtils::make_success_result(_summary(jobs[job.id]));
}

Dictionary MCPHarnessService::get_status(const Dictionary &p_arguments, const MCPToolCallContext &p_context) const {
	Dictionary error;
	const Job *job = _find_owned_job(p_arguments, p_context, error);
	return job ? MCPToolUtils::make_success_result(_summary(*job)) : error;
}

Dictionary MCPHarnessService::cancel(const Dictionary &p_arguments, const MCPToolCallContext &p_context) {
	Dictionary error;
	Job *job = _find_owned_job(p_arguments, p_context, error);
	if (!job) {
		return error;
	}
	if (!_is_terminal(*job)) {
		job->cancel_requested = true;
		job->state = "cancelling";
	}
	return MCPToolUtils::make_success_result(_summary(*job));
}

Dictionary MCPHarnessService::get_report(const Dictionary &p_arguments, const MCPToolCallContext &p_context) const {
	Dictionary error;
	const Job *job = _find_owned_job(p_arguments, p_context, error);
	return job ? MCPToolUtils::make_success_result(_report(*job)) : error;
}

Dictionary MCPHarnessService::_prepare_arguments(const Job &p_job, const Dictionary &p_step) const {
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

MCPToolRegistry::CallResult MCPHarnessService::_call_tool(const Job &p_job, const String &p_tool, const Dictionary &p_arguments) const {
	if (!tool_registry) {
		MCPToolRegistry::CallResult result;
		result.status = MCPToolRegistry::CALL_TOOL_UNAVAILABLE;
		result.message = "The MCP Tool Registry is unavailable.";
		return result;
	}
	return tool_registry->call_tool(p_tool, p_arguments, p_job.context);
}

void MCPHarnessService::_record_step_start(Job &r_job, const Dictionary &p_step, const Dictionary &p_arguments) {
	Dictionary report_step;
	report_step["index"] = r_job.current_step;
	report_step["kind"] = p_step.get("kind", "command");
	report_step["source"] = p_step.get("source", String());
	report_step["tool"] = p_step.get("tool", String());
	report_step["arguments"] = p_arguments;
	report_step["state"] = "running";
	r_job.report_steps.push_back(report_step);
	_record_trace_event(r_job, "step_started", "info", report_step);
}

void MCPHarnessService::_record_step_end(Job &r_job, const MCPToolRegistry::CallResult &p_result, bool p_passed) {
	ERR_FAIL_COND(r_job.report_steps.is_empty());
	Dictionary report_step = r_job.report_steps[r_job.report_steps.size() - 1];
	report_step["state"] = p_passed ? "passed" : "failed";
	report_step["passed"] = p_passed;
	if (_is_error_result(p_result)) {
		report_step["error"] = _call_error(p_result, report_step.get("tool", String()));
	} else {
		report_step["result"] = _result_content(p_result);
	}
	r_job.report_steps[r_job.report_steps.size() - 1] = report_step;
	_record_trace_event(r_job, "step_completed", p_passed ? "info" : "error", report_step, p_passed ? Dictionary() : Dictionary(report_step.get("error", Dictionary())));
	const Dictionary plan_step = r_job.steps[r_job.current_step];
	if (String(plan_step.get("kind", String())) == "expect") {
		Dictionary assertion;
		assertion["index"] = r_job.current_step;
		assertion["expect"] = plan_step.get("source", String());
		assertion["passed"] = p_passed;
		assertion["result"] = _result_content(p_result);
		r_job.assertions.push_back(assertion);
	}
}

void MCPHarnessService::_record_trace_event(Job &r_job, const String &p_event, const String &p_severity, const Dictionary &p_data, const Dictionary &p_error) {
	if (!r_job.evidence_trace || !trace_service || !trace_service->is_started()) {
		return;
	}
	String trace_error;
	if (trace_service->record_job_event(r_job.id, p_event, p_severity, p_data, Dictionary(), p_error, &trace_error) != OK && !trace_error.is_empty()) {
		r_job.evidence["traceError"] = trace_error;
	}
}

void MCPHarnessService::_ensure_trace_started(Job &r_job) {
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
		_record_trace_event(r_job, "started", "info", data);
		r_job.trace_started_event = true;
	}
}

void MCPHarnessService::_begin_finalization(Job &r_job, const String &p_final_state, const Dictionary &p_failure) {
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

void MCPHarnessService::_begin_failure(Job &r_job, const Dictionary &p_failure) {
	if (r_job.current_step >= 0 && r_job.current_step < r_job.steps.size()) {
		const Dictionary step = r_job.steps[r_job.current_step];
		const Dictionary polling = step.get("poll", Dictionary());
		if (bool(polling.get("captureOnError", false))) {
			r_job.screenshot_policy = "always";
		}
	}
	_begin_finalization(r_job, "failed", p_failure);
}

void MCPHarnessService::_complete_job(Job &r_job) {
	r_job.child_kind = CHILD_NONE;
	r_job.child_id = String();
	_begin_finalization(r_job, "completed");
}

int MCPHarnessService::_start_performance(Job &r_job) {
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
	if (_is_error_result(result)) {
		r_job.evidence["performance"] = _evidence_reference("godot.runtime.performance.start", result);
		return 1;
	}
	const Dictionary content = _result_content(result);
	r_job.performance_job_id = content.get("jobId", String());
	if (r_job.performance_job_id.is_empty()) {
		r_job.evidence["performance"] = Dictionary{ { "tool", "godot.runtime.performance.start" }, { "ok", false }, { "error", _failure("INVALID_PERFORMANCE_RESULT", "performance.start did not return jobId.") } };
	}
	return 1;
}

int MCPHarnessService::_advance_child(Job &r_job) {
	String tool;
	Dictionary arguments;
	if (r_job.child_kind == CHILD_INPUT_SEQUENCE) {
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
	if (_is_error_result(result)) {
		_record_step_end(r_job, result, false);
		_begin_failure(r_job, _call_error(result, tool));
		return 1;
	}
	const Dictionary content = _result_content(result);
	const String state = content.get("state", String());
	const bool running = r_job.child_kind == CHILD_INPUT_SEQUENCE ? (state == "running" || state == "waiting" || state == "queued") : state == "running";
	if (running) {
		return 1;
	}
	const bool passed = r_job.child_kind == CHILD_INPUT_SEQUENCE ? state == "completed" : state == "succeeded";
	_record_step_end(r_job, result, passed);
	r_job.child_kind = CHILD_NONE;
	r_job.child_id = String();
	if (!passed) {
		_begin_failure(r_job, _failure("HARNESS_CHILD_FAILED", content.get("failure", "Asynchronous Harness step failed."), content));
		return 1;
	}
	r_job.current_step++;
	if (r_job.current_step >= r_job.steps.size()) {
		_complete_job(r_job);
	}
	return 1;
}

int MCPHarnessService::_advance_cancellation(Job &r_job) {
	if (r_job.child_kind == CHILD_NONE) {
		_begin_finalization(r_job, "cancelled", _failure("CANCELLED", "The Harness job was cancelled."));
		return 0;
	}
	const String tool = r_job.child_kind == CHILD_INPUT_SEQUENCE ? "godot.runtime.input.sequence_cancel" : "godot.runtime.wait.cancel";
	Dictionary arguments;
	arguments[r_job.child_kind == CHILD_INPUT_SEQUENCE ? "sequenceId" : "jobId"] = r_job.child_id;
	arguments["runtimeGeneration"] = int64_t(r_job.runtime_generation);
	if (r_job.debugger_session >= 0) {
		arguments["debuggerSession"] = r_job.debugger_session;
	}
	const MCPToolRegistry::CallResult result = _call_tool(r_job, tool, arguments);
	if (_is_error_result(result)) {
		r_job.evidence["cancellationError"] = _call_error(result, tool);
	}
	r_job.child_kind = CHILD_NONE;
	r_job.child_id = String();
	_begin_finalization(r_job, "cancelled", _failure("CANCELLED", "The Harness job was cancelled."));
	return 1;
}

int MCPHarnessService::_advance_evidence(Job &r_job) {
	if (r_job.evidence_phase == 0) {
		if (r_job.child_kind != CHILD_NONE) {
			const String tool = r_job.child_kind == CHILD_INPUT_SEQUENCE ? "godot.runtime.input.sequence_cancel" : "godot.runtime.wait.cancel";
			Dictionary arguments;
			arguments[r_job.child_kind == CHILD_INPUT_SEQUENCE ? "sequenceId" : "jobId"] = r_job.child_id;
			arguments["runtimeGeneration"] = int64_t(r_job.runtime_generation);
			if (r_job.debugger_session >= 0) {
				arguments["debuggerSession"] = r_job.debugger_session;
			}
			const MCPToolRegistry::CallResult result = _call_tool(r_job, tool, arguments);
			if (_is_error_result(result)) {
				r_job.evidence["childCleanupError"] = _call_error(result, tool);
			}
			r_job.child_kind = CHILD_NONE;
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
			r_job.evidence["performance"] = _evidence_reference("godot.runtime.performance.stop", result);
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
			r_job.evidence["screenshot"] = _evidence_reference("godot.runtime.get_screenshot", result);
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
			r_job.evidence["errors"] = _evidence_reference("godot.debug.get_errors", result);
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
			r_job.evidence["latestLog"] = _evidence_reference("godot.debug.get_latest_log", result);
			return 1;
		}
	}
	if (!r_job.trace_final_event) {
		Dictionary data = _summary(r_job);
		data["finalState"] = r_job.final_state;
		_record_trace_event(r_job, "finished", r_job.final_state == "completed" ? "info" : "error", data, r_job.failure);
		r_job.trace_final_event = true;
		if (r_job.evidence_trace && trace_service && trace_service->is_started()) {
			r_job.evidence["trace"] = trace_service->get_safe_reference(r_job.id);
		}
	}
	r_job.state = r_job.final_state.is_empty() ? "failed" : r_job.final_state;
	r_job.cancel_requested = false;
	return 0;
}

int MCPHarnessService::_advance_job(Job &r_job) {
	if (_is_terminal(r_job)) {
		return 0;
	}
	if (r_job.cancel_requested || r_job.state == "cancelling") {
		return _advance_cancellation(r_job);
	}
	if (r_job.state == "collecting_evidence") {
		return _advance_evidence(r_job);
	}
	if (r_job.evidence_perf_summary && !r_job.performance_start_attempted) {
		return _start_performance(r_job);
	}
	if (r_job.child_kind != CHILD_NONE) {
		return _advance_child(r_job);
	}
	if (r_job.current_step >= r_job.steps.size()) {
		_complete_job(r_job);
		return 0;
	}

	const Dictionary step = r_job.steps[r_job.current_step];
	const String tool = step.get("tool", String());
	const Dictionary arguments = _prepare_arguments(r_job, step);
	_record_step_start(r_job, step, arguments);
	const MCPToolRegistry::CallResult result = _call_tool(r_job, tool, arguments);
	if (_is_error_result(result)) {
		_record_step_end(r_job, result, false);
		_begin_failure(r_job, _call_error(result, tool));
		return 1;
	}
	const Dictionary content = _result_content(result);
	if (tool == "godot.runtime.input.sequence") {
		const String child_id = content.get("sequenceId", String());
		if (child_id.is_empty()) {
			_record_step_end(r_job, result, false);
			_begin_failure(r_job, _failure("INVALID_CHILD_RESULT", "input.sequence did not return sequenceId."));
		} else {
			r_job.child_kind = CHILD_INPUT_SEQUENCE;
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
			_record_step_end(r_job, result, false);
			_begin_failure(r_job, _failure("INVALID_CHILD_RESULT", "runtime.wait.start did not return jobId."));
		} else {
			r_job.child_kind = CHILD_RUNTIME_WAIT;
			r_job.child_id = child_id;
			Dictionary report_step = r_job.report_steps[r_job.report_steps.size() - 1];
			report_step["startResult"] = content;
			r_job.report_steps[r_job.report_steps.size() - 1] = report_step;
		}
		return 1;
	}

	_record_step_end(r_job, result, true);
	r_job.current_step++;
	if (r_job.current_step >= r_job.steps.size()) {
		_complete_job(r_job);
	}
	return 1;
}

int MCPHarnessService::poll(int p_max_tool_calls) {
	const int limit = CLAMP(p_max_tool_calls, 0, MAX_TOOL_CALLS_PER_POLL);
	if (limit == 0 || job_order.is_empty()) {
		return 0;
	}
	int calls = 0;
	int visited = 0;
	const int job_count = job_order.size();
	while (visited < job_count && calls < limit) {
		const int index = (poll_cursor + visited) % job_count;
		Job *job = jobs.getptr(job_order[index]);
		if (job) {
			calls += _advance_job(*job);
		}
		visited++;
	}
	poll_cursor = job_order.is_empty() ? 0 : (poll_cursor + MAX(1, visited)) % job_order.size();
	return calls;
}

void MCPHarnessService::release_session(const String &p_session_id) {
	for (KeyValue<String, Job> &entry : jobs) {
		if (entry.value.session_id == p_session_id && !_is_terminal(entry.value)) {
			entry.value.final_state = "cancelled";
			entry.value.state = "cancelled";
			entry.value.cancel_requested = false;
			entry.value.child_kind = CHILD_NONE;
			entry.value.child_id = String();
			entry.value.performance_job_id = String();
			entry.value.failure = _failure("SESSION_CLOSED", "The owning MCP session closed.");
			_record_trace_event(entry.value, "finished", "error", _summary(entry.value), entry.value.failure);
			entry.value.trace_final_event = true;
		}
	}
}

void MCPHarnessService::shutdown() {
	for (KeyValue<String, Job> &entry : jobs) {
		if (!_is_terminal(entry.value)) {
			entry.value.final_state = "cancelled";
			entry.value.state = "cancelled";
			entry.value.failure = _failure("HOST_STOPPED", "The MCP Host stopped.");
			_record_trace_event(entry.value, "finished", "error", _summary(entry.value), entry.value.failure);
		}
	}
	jobs.clear();
	job_order.clear();
	poll_cursor = 0;
}
