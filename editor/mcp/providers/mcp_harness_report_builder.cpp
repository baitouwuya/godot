/**************************************************************************/
/*  mcp_harness_report_builder.cpp                                       */
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

#include "mcp_harness_report_builder.h"

bool MCPHarnessReportBuilder::is_terminal(const MCPHarnessJob &p_job) {
	return p_job.state == "completed" || p_job.state == "failed" || p_job.state == "cancelled";
}

bool MCPHarnessReportBuilder::is_error_result(const MCPToolRegistry::CallResult &p_result) {
	return p_result.status != MCPToolRegistry::CALL_OK || bool(p_result.result.get("isError", false));
}

Dictionary MCPHarnessReportBuilder::result_content(const MCPToolRegistry::CallResult &p_result) {
	return p_result.status == MCPToolRegistry::CALL_OK ? Dictionary(p_result.result.get("structuredContent", Dictionary())) : Dictionary();
}

Dictionary MCPHarnessReportBuilder::failure(const String &p_code, const String &p_message, const Dictionary &p_details) {
	Dictionary result;
	result["code"] = p_code;
	result["message"] = p_message;
	if (!p_details.is_empty()) {
		result["details"] = p_details;
	}
	return result;
}

Dictionary MCPHarnessReportBuilder::call_error(const MCPToolRegistry::CallResult &p_result, const String &p_tool) {
	if (p_result.status != MCPToolRegistry::CALL_OK) {
		return failure("TOOL_DISPATCH_FAILED", p_result.message.is_empty() ? "Harness tool dispatch failed: " + p_tool : p_result.message);
	}
	const Dictionary structured = p_result.result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return failure(error.get("code", "TOOL_FAILED"), error.get("message", "Harness tool returned an error: " + p_tool), error.get("details", Dictionary()));
}

Dictionary MCPHarnessReportBuilder::summary(const MCPHarnessJob &p_job) {
	Dictionary result;
	result["jobId"] = p_job.id;
	result["state"] = p_job.state;
	result["name"] = p_job.plan.get("name", "harness");
	result["debuggerSession"] = p_job.debugger_session;
	result["runtimeGeneration"] = int64_t(p_job.runtime_generation);
	result["currentStep"] = p_job.current_step;
	result["stepCount"] = p_job.steps.size();
	int completed_steps = 0;
	for (int i = 0; i < p_job.report_steps.size(); i++) {
		if (String(Dictionary(p_job.report_steps[i]).get("state", String())) != "running") {
			completed_steps++;
		}
	}
	result["completedStepCount"] = completed_steps;
	if (!p_job.child_id.is_empty()) {
		result[p_job.child_kind == MCP_HARNESS_CHILD_INPUT_SEQUENCE ? "sequenceId" : "waitJobId"] = p_job.child_id;
	}
	if (!p_job.failure.is_empty()) {
		result["failure"] = p_job.failure;
	}
	return result;
}

Dictionary MCPHarnessReportBuilder::report(const MCPHarnessJob &p_job) {
	Dictionary result = summary(p_job);
	result["steps"] = p_job.report_steps;
	result["assertions"] = p_job.assertions;
	result["evidence"] = p_job.evidence;
	result["evidencePolicy"] = p_job.plan.get("evidencePolicy", Dictionary());
	return result;
}

Dictionary MCPHarnessReportBuilder::evidence_reference(const String &p_tool, const MCPToolRegistry::CallResult &p_result) {
	Dictionary reference;
	reference["tool"] = p_tool;
	reference["ok"] = !is_error_result(p_result);
	if (p_result.status == MCPToolRegistry::CALL_OK) {
		reference["structuredContent"] = p_result.result.get("structuredContent", Dictionary());
	} else {
		reference["error"] = call_error(p_result, p_tool);
	}
	return reference;
}

Dictionary MCPHarnessReportBuilder::record_step_start(MCPHarnessJob &r_job, const Dictionary &p_step, const Dictionary &p_arguments) {
	Dictionary report_step;
	report_step["index"] = r_job.current_step;
	report_step["kind"] = p_step.get("kind", "command");
	report_step["source"] = p_step.get("source", String());
	report_step["tool"] = p_step.get("tool", String());
	report_step["arguments"] = p_arguments;
	report_step["state"] = "running";
	r_job.report_steps.push_back(report_step);
	return report_step;
}

Dictionary MCPHarnessReportBuilder::record_step_end(MCPHarnessJob &r_job, const MCPToolRegistry::CallResult &p_result, bool p_passed) {
	ERR_FAIL_COND_V(r_job.report_steps.is_empty(), Dictionary());
	Dictionary report_step = r_job.report_steps[r_job.report_steps.size() - 1];
	report_step["state"] = p_passed ? "passed" : "failed";
	report_step["passed"] = p_passed;
	if (is_error_result(p_result)) {
		report_step["error"] = call_error(p_result, report_step.get("tool", String()));
	} else {
		report_step["result"] = result_content(p_result);
	}
	r_job.report_steps[r_job.report_steps.size() - 1] = report_step;
	const Dictionary plan_step = r_job.steps[r_job.current_step];
	if (String(plan_step.get("kind", String())) == "expect") {
		Dictionary assertion;
		assertion["index"] = r_job.current_step;
		assertion["expect"] = plan_step.get("source", String());
		assertion["passed"] = p_passed;
		assertion["result"] = result_content(p_result);
		r_job.assertions.push_back(assertion);
	}
	return report_step;
}
