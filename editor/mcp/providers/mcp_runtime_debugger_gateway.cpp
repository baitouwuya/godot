/**************************************************************************/
/*  mcp_runtime_debugger_gateway.cpp                                     */
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

#include "mcp_runtime_debugger_gateway.h"

#include "mcp_debug_capture.h"
#include "mcp_runtime_debugger_wait.h"
#include "mcp_tool_utils.h"

#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"

namespace {

constexpr int DEFAULT_REQUEST_TIMEOUT_MSEC = 500;
constexpr int MAX_REQUEST_TIMEOUT_MSEC = 1500;

Dictionary _error(const String &p_code, const String &p_message) {
	return MCPToolUtils::make_error_result(p_code, p_message);
}

bool _read_timeout(const Dictionary &p_arguments, int &r_timeout) {
	int64_t timeout = DEFAULT_REQUEST_TIMEOUT_MSEC;
	if (p_arguments.has("timeoutMs") &&
			!MCPToolUtils::try_get_json_integer(p_arguments["timeoutMs"], 50, MAX_REQUEST_TIMEOUT_MSEC, timeout)) {
		return false;
	}
	r_timeout = int(timeout);
	return true;
}

} // namespace

MCPRuntimeDebuggerGateway::MCPRuntimeDebuggerGateway(MCPDebugCapture *p_debug_capture) {
	debug_capture = p_debug_capture;
}

Dictionary MCPRuntimeDebuggerGateway::resolve_session(const Dictionary &p_arguments, bool p_require_generation, Session &r_session) const {
	r_session = Session();
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node || !debug_capture) {
		return _error("RUNTIME_UNAVAILABLE", "The editor debugger is not available.");
	}

	if (p_arguments.has("debuggerSession")) {
		int64_t requested_session = 0;
		if (!MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, requested_session)) {
			return _error("INVALID_ARGUMENTS", "debuggerSession must be a non-negative integer.");
		}
		if (requested_session >= debugger_node->get_debugger_count()) {
			return _error("RUNTIME_NOT_FOUND", "The requested debugger session does not exist.");
		}
		ScriptEditorDebugger *candidate = debugger_node->get_debugger(int(requested_session));
		if (!candidate || !candidate->is_session_active()) {
			return _error("RUNTIME_NOT_RUNNING", "The requested debugger session is not active.");
		}
		r_session.debugger = candidate;
		r_session.debugger_session = int(requested_session);
	} else {
		for (int i = 0; i < debugger_node->get_debugger_count(); i++) {
			ScriptEditorDebugger *candidate = debugger_node->get_debugger(i);
			if (!candidate || !candidate->is_session_active()) {
				continue;
			}
			if (r_session.debugger) {
				return _error("AMBIGUOUS_RUNTIME", "Multiple running project sessions are active; provide debuggerSession.");
			}
			r_session.debugger = candidate;
			r_session.debugger_session = i;
		}
		if (!r_session.debugger) {
			return _error("RUNTIME_NOT_RUNNING", "No running project debugger session is active.");
		}
	}

	if (!debug_capture->get_runtime_generation(r_session.debugger_session, r_session.runtime_generation)) {
		return _error("RUNTIME_STARTING", "The running project has not completed debugger initialization.");
	}
	if (p_require_generation) {
		int64_t expected_generation = 0;
		if (!p_arguments.has("runtimeGeneration") ||
				!MCPToolUtils::try_get_json_integer(p_arguments["runtimeGeneration"], 1, INT64_MAX, expected_generation)) {
			return _error("INVALID_ARGUMENTS", "A positive runtimeGeneration returned by get_state or get_tree is required.");
		}
		if (uint64_t(expected_generation) != r_session.runtime_generation) {
			return _error("STALE_RUNTIME", "The running project restarted; refresh runtime state before mutating it.");
		}
	}
	return Dictionary();
}

bool MCPRuntimeDebuggerGateway::resolve_current_session(int p_debugger_session, uint64_t p_runtime_generation,
		Session &r_session) const {
	r_session = Session();
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node || p_debugger_session < 0 || p_debugger_session >= debugger_node->get_debugger_count() ||
			!is_runtime_current(p_debugger_session, p_runtime_generation)) {
		return false;
	}
	ScriptEditorDebugger *debugger = debugger_node->get_debugger(p_debugger_session);
	if (!debugger || !debugger->is_session_active()) {
		return false;
	}
	r_session.debugger = debugger;
	r_session.debugger_session = p_debugger_session;
	r_session.runtime_generation = p_runtime_generation;
	return true;
}

MCPRuntimeDebuggerGateway::RoundTripResult MCPRuntimeDebuggerGateway::round_trip(const Session &p_session,
		const String &p_mcp_session_id, const String &p_request_id_prefix, const String &p_message,
		const String &p_operation, const Array &p_arguments, int p_timeout_msec) {
	RoundTripResult result;
	if (!p_session.debugger || !p_session.debugger->is_session_active()) {
		result.status = ROUND_TRIP_DISCONNECTED;
		result.message = "The running project debugger is not connected.";
		return result;
	}
	if (!is_runtime_current(p_session.debugger_session, p_session.runtime_generation)) {
		result.status = ROUND_TRIP_STALE;
		result.message = "The running project restarted before the debugger request was sent.";
		return result;
	}

	const String request_id = request_broker.create_request_id(p_request_id_prefix);
	if (!request_broker.register_request(p_session.debugger_session, request_id, p_operation, p_mcp_session_id)) {
		result.status = ROUND_TRIP_BUSY;
		result.message = "The runtime debugger request limit is full.";
		return result;
	}
	Array arguments = p_arguments.duplicate();
	arguments.push_front(request_id);
	p_session.debugger->send_message(p_message, arguments);

	const uint64_t deadline = MCPRuntimeDebuggerWait::deadline_from_timeout_msec(p_timeout_msec);
	const MCPRuntimeDebuggerWait::WaitStatus wait_status = MCPRuntimeDebuggerWait::wait_until(
			p_session.debugger,
			deadline,
			[&]() { return request_broker.take_response(p_session.debugger_session, request_id, result.response); },
			[&]() { return !is_runtime_current(p_session.debugger_session, p_session.runtime_generation); });
	if (wait_status != MCPRuntimeDebuggerWait::WAIT_COMPLETED) {
		request_broker.cancel_request(p_session.debugger_session, request_id);
	}
	switch (wait_status) {
		case MCPRuntimeDebuggerWait::WAIT_TIMEOUT:
			result.status = ROUND_TRIP_TIMEOUT;
			result.message = "Timed out waiting for the running project's debugger response.";
			return result;
		case MCPRuntimeDebuggerWait::WAIT_DISCONNECTED:
			result.status = ROUND_TRIP_DISCONNECTED;
			result.message = "The running project stopped before returning the debugger response.";
			return result;
		case MCPRuntimeDebuggerWait::WAIT_STALE:
			result.status = ROUND_TRIP_STALE;
			result.message = "The running project restarted while resolving the debugger request.";
			return result;
		case MCPRuntimeDebuggerWait::WAIT_COMPLETED:
			break;
	}
	if (!is_runtime_current(p_session.debugger_session, p_session.runtime_generation)) {
		result.status = ROUND_TRIP_STALE;
		result.message = "The running project restarted while resolving the debugger request.";
		return result;
	}
	if (!result.response.ok) {
		result.status = ROUND_TRIP_REMOTE_ERROR;
		result.message = result.response.message.is_empty() ? result.response.code : result.response.message;
		return result;
	}
	result.status = ROUND_TRIP_COMPLETED;
	return result;
}

Dictionary MCPRuntimeDebuggerGateway::request(const Dictionary &p_arguments, const String &p_capture, const String &p_operation,
		const Dictionary &p_payload, bool p_require_generation) {
	int timeout_msec = DEFAULT_REQUEST_TIMEOUT_MSEC;
	if (!_read_timeout(p_arguments, timeout_msec)) {
		return _error("INVALID_ARGUMENTS", vformat("timeoutMs must be an integer between 50 and %d for runtime observations.", MAX_REQUEST_TIMEOUT_MSEC));
	}
	Session session;
	const Dictionary session_error = resolve_session(p_arguments, p_require_generation, session);
	if (!session_error.is_empty()) {
		return session_error;
	}
	const RoundTripResult round_trip_result = round_trip(
			session, String(), "observation", p_capture + ":" + p_operation, p_operation, Array{ p_payload }, timeout_msec);
	if (round_trip_result.status == ROUND_TRIP_BUSY) {
		return _error("RUNTIME_DEBUGGER_BUSY", round_trip_result.message);
	}
	if (round_trip_result.status == ROUND_TRIP_TIMEOUT) {
		return _error("RUNTIME_TIMEOUT", round_trip_result.message);
	}
	if (round_trip_result.status == ROUND_TRIP_DISCONNECTED) {
		return _error("RUNTIME_NOT_RUNNING", round_trip_result.message);
	}
	if (round_trip_result.status == ROUND_TRIP_STALE) {
		return _error("STALE_RUNTIME", round_trip_result.message);
	}
	if (round_trip_result.status == ROUND_TRIP_REMOTE_ERROR) {
		const MCPRuntimeRequestBroker::Response &response = round_trip_result.response;
		return MCPToolUtils::make_error_result(response.code.is_empty() ? "RUNTIME_DEBUGGER_REQUEST_FAILED" : response.code,
				response.message.is_empty() ? "Runtime debugger request failed." : response.message, response.data);
	}
	Dictionary result = make_session_identity(session.debugger_session, session.runtime_generation);
	result.merge(round_trip_result.response.data, true);
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebuggerGateway::make_session_identity(int p_debugger_session, uint64_t p_runtime_generation) const {
	Dictionary identity;
	identity["debuggerSession"] = p_debugger_session;
	identity["runtimeGeneration"] = int64_t(p_runtime_generation);
	return identity;
}

bool MCPRuntimeDebuggerGateway::is_runtime_current(int p_debugger_session, uint64_t p_runtime_generation) const {
	uint64_t current_generation = 0;
	return get_runtime_generation(p_debugger_session, current_generation) &&
			current_generation == p_runtime_generation;
}

bool MCPRuntimeDebuggerGateway::get_runtime_generation(int p_debugger_session, uint64_t &r_runtime_generation) const {
	return debug_capture && debug_capture->get_runtime_generation(p_debugger_session, r_runtime_generation);
}

bool MCPRuntimeDebuggerGateway::send_message(int p_debugger_session, const String &p_message, const Array &p_arguments) const {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	ScriptEditorDebugger *debugger = debugger_node ? debugger_node->get_debugger(p_debugger_session) : nullptr;
	if (!debugger || !debugger->is_session_active()) {
		return false;
	}
	debugger->send_message(p_message, p_arguments);
	return true;
}

void MCPRuntimeDebuggerGateway::broadcast_message(const String &p_message, const Array &p_arguments) const {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node) {
		return;
	}
	for (int i = 0; i < debugger_node->get_debugger_count(); i++) {
		ScriptEditorDebugger *debugger = debugger_node->get_debugger(i);
		if (debugger && debugger->is_session_active()) {
			debugger->send_message(p_message, p_arguments);
		}
	}
}

bool MCPRuntimeDebuggerGateway::handle_response(int p_debugger_session, const Array &p_data) {
	return request_broker.handle_response(p_debugger_session, p_data);
}

void MCPRuntimeDebuggerGateway::release_session_requests(const String &p_mcp_session_id) {
	request_broker.release_session(p_mcp_session_id);
}

void MCPRuntimeDebuggerGateway::clear_requests() {
	request_broker.clear();
}
