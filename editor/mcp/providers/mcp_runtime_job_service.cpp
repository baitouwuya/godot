/**************************************************************************/
/*  mcp_runtime_job_service.cpp                                          */
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

#include "mcp_runtime_job_service.h"

#include "mcp_tool_utils.h"

#include "core/templates/hash_set.h"

namespace {

Dictionary _error(const String &p_code, const String &p_message) {
	return MCPToolUtils::make_error_result(p_code, p_message);
}

bool _is_error_result(const Dictionary &p_result) {
	return bool(p_result.get("isError", false));
}

} // namespace

MCPRuntimeJobService::MCPRuntimeJobService(MCPRuntimeDebuggerGateway *p_runtime_gateway, const Config &p_config) {
	runtime_gateway = p_runtime_gateway;
	config = p_config;
}

void MCPRuntimeJobService::_prune_records() {
	while (jobs.size() >= config.max_records) {
		int remove_index = -1;
		for (int i = 0; i < job_order.size(); i++) {
			const JobRecord *record = jobs.getptr(job_order[i]);
			if (!record || String(record->state.get("state", String())) != "running") {
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
}

Dictionary MCPRuntimeJobService::_resolve_record(const Dictionary &p_arguments, const String &p_mcp_session_id,
		String &r_job_id, JobRecord *&r_record) {
	r_record = nullptr;
	const Variant job_value = p_arguments.get("jobId", Variant());
	if (job_value.get_type() != Variant::STRING || String(job_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "jobId must be a non-empty string.");
	}
	r_job_id = job_value;
	r_record = jobs.getptr(r_job_id);
	if (!r_record || r_record->mcp_session_id != p_mcp_session_id) {
		return _error(config.not_found_code, config.not_found_message);
	}
	int64_t requested_generation = 0;
	int64_t requested_debugger = r_record->debugger_session;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("runtimeGeneration", Variant()), 1, INT64_MAX, requested_generation) ||
			(p_arguments.has("debuggerSession") &&
					!MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, requested_debugger)) ||
			uint64_t(requested_generation) != r_record->runtime_generation ||
			int(requested_debugger) != r_record->debugger_session) {
		return _error(config.stale_code, config.stale_message);
	}
	return Dictionary();
}

Dictionary MCPRuntimeJobService::start(const Dictionary &p_arguments, const String &p_mcp_session_id, Dictionary p_payload) {
	ERR_FAIL_NULL_V(runtime_gateway, _error("RUNTIME_UNAVAILABLE", "The runtime debugger gateway is not available."));
	MCPRuntimeDebuggerGateway::Session session;
	const Dictionary session_error = runtime_gateway->resolve_session(p_arguments, true, session);
	if (!session_error.is_empty()) {
		return session_error;
	}
	_prune_records();
	if (jobs.size() >= config.max_records) {
		return _error(config.limit_code, config.limit_message);
	}
	const String job_id = config.id_prefix + "-" + String::num_uint64(next_job_id++);
	p_payload["jobId"] = job_id;
	p_payload["mcpSessionId"] = p_mcp_session_id;
	const Dictionary response = runtime_gateway->request(p_arguments, config.capture, "start", p_payload, true);
	if (_is_error_result(response)) {
		if (!config.rollback_operation.is_empty()) {
			runtime_gateway->send_message(session.debugger_session, config.capture + ":" + config.rollback_operation,
					Array{ String(), p_payload });
		}
		return response;
	}
	JobRecord record;
	record.mcp_session_id = p_mcp_session_id;
	record.debugger_session = session.debugger_session;
	record.runtime_generation = session.runtime_generation;
	record.state = response.get("structuredContent", Dictionary());
	jobs.insert(job_id, record);
	job_order.push_back(job_id);
	return response;
}

Dictionary MCPRuntimeJobService::get_status(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	String job_id;
	JobRecord *record = nullptr;
	const Dictionary record_error = _resolve_record(p_arguments, p_mcp_session_id, job_id, record);
	if (!record_error.is_empty()) {
		return record_error;
	}
	if (String(record->state.get("state", String())) != "running") {
		return MCPToolUtils::make_success_result(record->state);
	}
	Dictionary payload;
	payload["jobId"] = job_id;
	payload["mcpSessionId"] = p_mcp_session_id;
	const Dictionary response = runtime_gateway->request(p_arguments, config.capture, "status", payload, true);
	if (!_is_error_result(response)) {
		record->state = response.get("structuredContent", Dictionary());
	}
	return response;
}

Dictionary MCPRuntimeJobService::finish(const Dictionary &p_arguments, const String &p_mcp_session_id,
		const String &p_operation, const StringName &p_terminal_result_field) {
	String job_id;
	JobRecord *record = nullptr;
	const Dictionary record_error = _resolve_record(p_arguments, p_mcp_session_id, job_id, record);
	if (!record_error.is_empty()) {
		return record_error;
	}
	if (String(record->state.get("state", String())) != "running" &&
			(p_terminal_result_field.is_empty() || record->state.has(p_terminal_result_field))) {
		return MCPToolUtils::make_success_result(record->state);
	}
	Dictionary payload;
	payload["jobId"] = job_id;
	payload["mcpSessionId"] = p_mcp_session_id;
	const Dictionary response = runtime_gateway->request(p_arguments, config.capture, p_operation, payload, true);
	if (!_is_error_result(response)) {
		record->state = response.get("structuredContent", Dictionary());
	}
	return response;
}

void MCPRuntimeJobService::process() {
	for (KeyValue<String, JobRecord> &entry : jobs) {
		if (String(entry.value.state.get("state", String())) == "running" &&
				!runtime_gateway->is_runtime_current(entry.value.debugger_session, entry.value.runtime_generation)) {
			entry.value.state["state"] = "cancelled";
			entry.value.state["failure"] = "The running project disconnected or restarted.";
		}
	}
}

void MCPRuntimeJobService::release_session(const String &p_mcp_session_id) {
	HashSet<int> debugger_sessions;
	for (const KeyValue<String, JobRecord> &entry : jobs) {
		if (entry.value.mcp_session_id == p_mcp_session_id && String(entry.value.state.get("state", String())) == "running") {
			debugger_sessions.insert(entry.value.debugger_session);
		}
	}
	for (const int debugger_session : debugger_sessions) {
		Dictionary payload;
		payload["mcpSessionId"] = p_mcp_session_id;
		runtime_gateway->send_message(debugger_session, config.capture + ":release_session", Array{ String(), payload });
	}
	for (int i = job_order.size() - 1; i >= 0; i--) {
		const JobRecord *record = jobs.getptr(job_order[i]);
		if (record && record->mcp_session_id == p_mcp_session_id) {
			jobs.erase(job_order[i]);
			job_order.remove_at(i);
		}
	}
}

void MCPRuntimeJobService::release_all(const String &p_reason) {
	HashSet<int> debugger_sessions;
	for (KeyValue<String, JobRecord> &entry : jobs) {
		if (String(entry.value.state.get("state", String())) == "running") {
			debugger_sessions.insert(entry.value.debugger_session);
			entry.value.state["state"] = "cancelled";
			entry.value.state["failure"] = p_reason;
		}
	}
	for (const int debugger_session : debugger_sessions) {
		runtime_gateway->send_message(debugger_session, config.capture + ":release_all", Array{ String(), Dictionary() });
	}
}
