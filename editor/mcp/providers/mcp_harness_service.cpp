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
#include "mcp_harness_report_builder.h"
#include "mcp_tool_utils.h"

namespace {

static Dictionary _error(const String &p_code, const String &p_message, const Dictionary &p_details = Dictionary()) {
	return MCPToolUtils::make_error_result(p_code, p_message, p_details);
}

} // namespace

MCPHarnessService::MCPHarnessService() {
	step_executor.set_evidence_collector(&evidence_collector);
}

void MCPHarnessService::set_tool_registry(MCPToolRegistry *p_registry) {
	tool_registry = p_registry;
	step_executor.set_tool_registry(p_registry);
	evidence_collector.set_tool_registry(p_registry);
}

void MCPHarnessService::set_trace_service(MCPTraceService *p_service, const Dictionary &p_project_metadata, const String &p_root_directory_override) {
	evidence_collector.set_trace_service(p_service, p_project_metadata, p_root_directory_override);
}

String MCPHarnessService::_session_id(const MCPToolCallContext &p_context) {
	const Variant id = p_context.session.get("sessionId", Variant());
	return id.get_type() == Variant::STRING ? String(id) : String();
}

MCPHarnessJob *MCPHarnessService::_find_owned_job(const Dictionary &p_arguments, const MCPToolCallContext &p_context, Dictionary &r_error) {
	const Variant job_value = p_arguments.get("jobId", Variant());
	if (job_value.get_type() != Variant::STRING || String(job_value).is_empty()) {
		r_error = _error("INVALID_ARGUMENTS", "jobId must be a non-empty string.");
		return nullptr;
	}
	MCPHarnessJob *job = job_store.find(String(job_value));
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

const MCPHarnessJob *MCPHarnessService::_find_owned_job(const Dictionary &p_arguments, const MCPToolCallContext &p_context, Dictionary &r_error) const {
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
	if (job_store.active_job_count() >= MAX_ACTIVE_JOBS) {
		return _error("HARNESS_JOB_LIMIT", "The global active Harness job limit is full.");
	}
	if (job_store.active_job_count(session_id) >= MAX_SESSION_JOBS) {
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

	MCPHarnessJob job;
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
	MCPHarnessJob *stored_job = job_store.insert(job);
	evidence_collector.ensure_trace_started(*stored_job);
	return MCPToolUtils::make_success_result(MCPHarnessReportBuilder::summary(*stored_job));
}

Dictionary MCPHarnessService::get_status(const Dictionary &p_arguments, const MCPToolCallContext &p_context) const {
	Dictionary error;
	const MCPHarnessJob *job = _find_owned_job(p_arguments, p_context, error);
	return job ? MCPToolUtils::make_success_result(MCPHarnessReportBuilder::summary(*job)) : error;
}

Dictionary MCPHarnessService::cancel(const Dictionary &p_arguments, const MCPToolCallContext &p_context) {
	Dictionary error;
	MCPHarnessJob *job = _find_owned_job(p_arguments, p_context, error);
	if (!job) {
		return error;
	}
	if (!MCPHarnessReportBuilder::is_terminal(*job)) {
		job->cancel_requested = true;
		job->state = "cancelling";
	}
	return MCPToolUtils::make_success_result(MCPHarnessReportBuilder::summary(*job));
}

Dictionary MCPHarnessService::get_report(const Dictionary &p_arguments, const MCPToolCallContext &p_context) const {
	Dictionary error;
	const MCPHarnessJob *job = _find_owned_job(p_arguments, p_context, error);
	return job ? MCPToolUtils::make_success_result(MCPHarnessReportBuilder::report(*job)) : error;
}

int MCPHarnessService::_advance_job(MCPHarnessJob &r_job) {
	if (MCPHarnessReportBuilder::is_terminal(r_job)) {
		return 0;
	}
	if (r_job.cancel_requested || r_job.state == "cancelling") {
		return step_executor.advance_cancellation(r_job);
	}
	if (r_job.state == "collecting_evidence") {
		return evidence_collector.advance(r_job);
	}
	if (r_job.evidence_perf_summary && !r_job.performance_start_attempted) {
		return evidence_collector.start_performance(r_job);
	}
	return step_executor.advance(r_job);
}

int MCPHarnessService::poll(int p_max_tool_calls) {
	const int limit = CLAMP(p_max_tool_calls, 0, MAX_TOOL_CALLS_PER_POLL);
	if (limit == 0 || job_store.poll_job_count() == 0) {
		return 0;
	}
	int calls = 0;
	int visited = 0;
	const int job_count = job_store.poll_job_count();
	while (visited < job_count && calls < limit) {
		MCPHarnessJob *job = job_store.poll_job_at(visited);
		if (job) {
			calls += _advance_job(*job);
		}
		visited++;
	}
	job_store.advance_poll_cursor(visited);
	return calls;
}

void MCPHarnessService::release_session(const String &p_session_id) {
	const Vector<String> job_ids = job_store.get_job_ids();
	for (int i = 0; i < job_ids.size(); i++) {
		MCPHarnessJob *job = job_store.find(job_ids[i]);
		if (job && job->session_id == p_session_id && !MCPHarnessReportBuilder::is_terminal(*job)) {
			job->final_state = "cancelled";
			job->state = "cancelled";
			job->cancel_requested = false;
			job->child_kind = MCP_HARNESS_CHILD_NONE;
			job->child_id = String();
			job->performance_job_id = String();
			job->failure = MCPHarnessReportBuilder::failure("SESSION_CLOSED", "The owning MCP session closed.");
			evidence_collector.record_trace_event(*job, "finished", "error", MCPHarnessReportBuilder::summary(*job), job->failure);
			job->trace_final_event = true;
		}
	}
}

void MCPHarnessService::shutdown() {
	const Vector<String> job_ids = job_store.get_job_ids();
	for (int i = 0; i < job_ids.size(); i++) {
		MCPHarnessJob *job = job_store.find(job_ids[i]);
		if (job && !MCPHarnessReportBuilder::is_terminal(*job)) {
			job->final_state = "cancelled";
			job->state = "cancelled";
			job->failure = MCPHarnessReportBuilder::failure("HOST_STOPPED", "The MCP Host stopped.");
			evidence_collector.record_trace_event(*job, "finished", "error", MCPHarnessReportBuilder::summary(*job), job->failure);
		}
	}
	job_store.clear();
}
