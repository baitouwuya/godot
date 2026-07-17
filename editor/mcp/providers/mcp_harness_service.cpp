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
	job.context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	job.context.request_id = Variant();
	job.plan = plan;
	job.steps = steps;
	const Dictionary evidence_policy = plan.get("evidencePolicy", Dictionary());
	const String screenshot_policy = evidence_policy.get("screenshots", "on_failure");
	job.evidence_screenshot = screenshot_policy == "on_failure" || screenshot_policy == "always";
	jobs.insert(job.id, job);
	job_order.push_back(job.id);
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
		job->state = job->child_kind == CHILD_NONE ? "cancelled" : "cancelling";
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

void MCPHarnessService::_begin_failure(Job &r_job, const Dictionary &p_failure) {
	r_job.failure = p_failure;
	if (r_job.current_step >= 0 && r_job.current_step < r_job.steps.size()) {
		const Dictionary step = r_job.steps[r_job.current_step];
		const Dictionary polling = step.get("poll", Dictionary());
		r_job.evidence_screenshot = r_job.evidence_screenshot || bool(polling.get("captureOnError", false));
	}
	r_job.state = "collecting_evidence";
	r_job.evidence_phase = 0;
}

void MCPHarnessService::_complete_job(Job &r_job) {
	r_job.state = "completed";
	r_job.child_kind = CHILD_NONE;
	r_job.child_id = String();
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
		r_job.state = "cancelled";
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
	r_job.state = "cancelled";
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
		if (!r_job.evidence_screenshot) {
			return 0;
		}
		Dictionary arguments;
		arguments["name"] = "harness_failure";
		arguments["timeoutMs"] = 500;
		if (r_job.debugger_session >= 0) {
			arguments["debuggerSession"] = r_job.debugger_session;
		}
		const MCPToolRegistry::CallResult result = _call_tool(r_job, "godot.runtime.get_screenshot", arguments);
		r_job.evidence["screenshot"] = _evidence_reference("godot.runtime.get_screenshot", result);
		return 1;
	}
	if (r_job.evidence_phase == 1) {
		r_job.evidence_phase++;
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
	r_job.state = "failed";
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
			entry.value.state = "cancelled";
			entry.value.cancel_requested = true;
			entry.value.child_kind = CHILD_NONE;
			entry.value.child_id = String();
			entry.value.failure = _failure("SESSION_CLOSED", "The owning MCP session closed.");
		}
	}
}

void MCPHarnessService::shutdown() {
	for (KeyValue<String, Job> &entry : jobs) {
		if (!_is_terminal(entry.value)) {
			entry.value.state = "cancelled";
			entry.value.failure = _failure("HOST_STOPPED", "The MCP Host stopped.");
		}
	}
	jobs.clear();
	job_order.clear();
	poll_cursor = 0;
}
