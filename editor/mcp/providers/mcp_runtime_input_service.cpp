/**************************************************************************/
/*  mcp_runtime_input_service.cpp                                        */
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

#include "mcp_runtime_input_service.h"

#include "mcp_runtime_input.h"
#include "mcp_runtime_input_sequence.h"
#include "mcp_tool_utils.h"

namespace {

constexpr int DEFAULT_TARGET_TIMEOUT_MSEC = 500;
constexpr int MAX_TARGET_TIMEOUT_MSEC = 1500;

Dictionary _error(const String &p_code, const String &p_message) {
	return MCPToolUtils::make_error_result(p_code, p_message);
}

bool _target_timeout_from_arguments(const Dictionary &p_arguments, int &r_timeout) {
	int64_t timeout = DEFAULT_TARGET_TIMEOUT_MSEC;
	if (p_arguments.has("timeoutMs") &&
			!MCPToolUtils::try_get_json_integer(p_arguments["timeoutMs"], 50, MAX_TARGET_TIMEOUT_MSEC, timeout)) {
		return false;
	}
	r_timeout = int(timeout);
	return true;
}

} // namespace

MCPRuntimeInputService::MCPRuntimeInputService(MCPRuntimeDebuggerGateway *p_runtime_gateway) :
		input_scheduler(p_runtime_gateway) {
	runtime_gateway = p_runtime_gateway;
}

Dictionary MCPRuntimeInputService::dispatch_target_events(const Dictionary &p_arguments, const String &p_mcp_session_id,
		const Array &p_events, const Dictionary &p_metadata) {
	if (p_events.is_empty() || p_events.size() > 128) {
		return _error("INVALID_TARGET_ACTION", "The target action generated an invalid number of input events.");
	}
	MCPRuntimeDebuggerGateway::Session session;
	const Dictionary session_error = runtime_gateway->resolve_session(p_arguments, true, session);
	if (!session_error.is_empty()) {
		return session_error;
	}
	int timeout_msec = DEFAULT_TARGET_TIMEOUT_MSEC;
	if (!_target_timeout_from_arguments(p_arguments, timeout_msec)) {
		return _error("INVALID_ARGUMENTS", "timeoutMs is outside the target action range.");
	}
	Vector<MCPRuntimeInput::EncodedEvent> encoded_events;
	for (int i = 0; i < p_events.size(); i++) {
		if (p_events[i].get_type() != Variant::DICTIONARY) {
			return _error("INVALID_TARGET_ACTION", vformat("Generated input event %d is invalid.", i));
		}
		MCPRuntimeInput::EncodedEvent encoded;
		String input_error;
		if (MCPRuntimeInput::encode(p_events[i], encoded, &input_error, true) != OK) {
			return _error("INVALID_TARGET_ACTION", input_error);
		}
		encoded_events.push_back(encoded);
	}
	int dispatched = 0;
	String dispatch_error;
	if (input_scheduler.dispatch_immediate(p_mcp_session_id, session, encoded_events, dispatched, dispatch_error,
				timeout_msec) != OK) {
		return _error("RUNTIME_INPUT_FAILED", dispatch_error);
	}
	Dictionary result = runtime_gateway->make_session_identity(session.debugger_session, session.runtime_generation);
	result.merge(p_metadata, true);
	result["dispatched"] = true;
	result["eventCount"] = dispatched;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeInputService::start_target_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id,
		const Array &p_steps, const Dictionary &p_metadata) {
	Vector<MCPRuntimeInputSequence::Step> steps;
	String compile_error;
	if (MCPRuntimeInputSequence::compile(p_steps, steps, &compile_error) != OK) {
		return _error("INVALID_TARGET_ACTION", compile_error);
	}
	MCPRuntimeDebuggerGateway::Session session;
	const Dictionary session_error = runtime_gateway->resolve_session(p_arguments, true, session);
	if (!session_error.is_empty()) {
		return session_error;
	}
	int timeout_msec = DEFAULT_TARGET_TIMEOUT_MSEC;
	if (!_target_timeout_from_arguments(p_arguments, timeout_msec)) {
		return _error("INVALID_ARGUMENTS", "timeoutMs is outside the target action range.");
	}
	Dictionary result;
	String scheduler_error;
	const Error error = input_scheduler.start_sequence(p_mcp_session_id, session, steps, result, scheduler_error, timeout_msec);
	if (error != OK) {
		return _error(error == ERR_BUSY ? "INPUT_SEQUENCE_BUSY" : "INPUT_SEQUENCE_FAILED", scheduler_error);
	}
	result.merge(p_metadata, true);
	result["asynchronous"] = true;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeInputService::send_input(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant events_value = p_arguments.get("events", Variant());
	if (events_value.get_type() != Variant::ARRAY) {
		return _error("INVALID_ARGUMENTS", "events must be an array.");
	}
	const Array events = events_value;
	if (events.is_empty() || events.size() > 128) {
		return _error("INVALID_ARGUMENTS", "events must contain between 1 and 128 entries.");
	}
	MCPRuntimeDebuggerGateway::Session session;
	const Dictionary session_error = runtime_gateway->resolve_session(p_arguments, true, session);
	if (!session_error.is_empty()) {
		return session_error;
	}
	Vector<MCPRuntimeInput::EncodedEvent> encoded_events;
	for (int i = 0; i < events.size(); i++) {
		if (events[i].get_type() != Variant::DICTIONARY) {
			return _error("INVALID_INPUT_EVENT", vformat("events[%d] must be an object.", i));
		}
		MCPRuntimeInput::EncodedEvent encoded;
		String input_error;
		if (MCPRuntimeInput::encode(events[i], encoded, &input_error, true) != OK) {
			return _error("INVALID_INPUT_EVENT", vformat("events[%d]: %s", i, input_error));
		}
		encoded_events.push_back(encoded);
	}
	int dispatched = 0;
	String dispatch_error;
	if (input_scheduler.dispatch_immediate(p_mcp_session_id, session, encoded_events, dispatched, dispatch_error) != OK) {
		return _error("RUNTIME_INPUT_FAILED", dispatch_error);
	}
	Dictionary result = runtime_gateway->make_session_identity(session.debugger_session, session.runtime_generation);
	result["dispatched"] = true;
	result["eventCount"] = dispatched;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeInputService::start_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant steps_value = p_arguments.get("steps", Variant());
	if (steps_value.get_type() != Variant::ARRAY) {
		return _error("INVALID_ARGUMENTS", "steps must be an array.");
	}
	Vector<MCPRuntimeInputSequence::Step> steps;
	String compile_error;
	if (MCPRuntimeInputSequence::compile(steps_value, steps, &compile_error) != OK) {
		return _error("INVALID_INPUT_SEQUENCE", compile_error);
	}
	MCPRuntimeDebuggerGateway::Session session;
	const Dictionary session_error = runtime_gateway->resolve_session(p_arguments, true, session);
	if (!session_error.is_empty()) {
		return session_error;
	}
	Dictionary result;
	String scheduler_error;
	const Error error = input_scheduler.start_sequence(p_mcp_session_id, session, steps, result, scheduler_error);
	if (error != OK) {
		return _error(error == ERR_BUSY ? "INPUT_SEQUENCE_BUSY" : "INPUT_SEQUENCE_FAILED", scheduler_error);
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeInputService::get_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant sequence_value = p_arguments.get("sequenceId", Variant());
	if (sequence_value.get_type() != Variant::STRING || String(sequence_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "sequenceId must be a non-empty string.");
	}
	Dictionary result;
	String scheduler_error;
	const Error error = input_scheduler.get_sequence(p_mcp_session_id, sequence_value, result, scheduler_error);
	if (error != OK) {
		return _error(error == ERR_DOES_NOT_EXIST ? "INPUT_SEQUENCE_NOT_FOUND" : "INPUT_SEQUENCE_QUERY_FAILED", scheduler_error);
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeInputService::cancel_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant sequence_value = p_arguments.get("sequenceId", Variant());
	if (sequence_value.get_type() != Variant::STRING || String(sequence_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "sequenceId must be a non-empty string.");
	}
	MCPRuntimeDebuggerGateway::Session session;
	const Dictionary session_error = runtime_gateway->resolve_session(p_arguments, true, session);
	if (!session_error.is_empty()) {
		return session_error;
	}
	Dictionary result;
	String scheduler_error;
	const Error error = input_scheduler.cancel_sequence(p_mcp_session_id, sequence_value, session, result, scheduler_error);
	if (error != OK) {
		return _error(error == ERR_DOES_NOT_EXIST ? "INPUT_SEQUENCE_NOT_FOUND" : "INPUT_SEQUENCE_MISMATCH", scheduler_error);
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeInputService::release_input(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	MCPRuntimeDebuggerGateway::Session session;
	const Dictionary session_error = runtime_gateway->resolve_session(p_arguments, true, session);
	if (!session_error.is_empty()) {
		return session_error;
	}
	int released = 0;
	String release_error;
	if (input_scheduler.release_session_inputs(p_mcp_session_id, session, true, released, release_error) != OK) {
		return _error("RUNTIME_INPUT_RELEASE_FAILED", release_error);
	}
	Dictionary result = runtime_gateway->make_session_identity(session.debugger_session, session.runtime_generation);
	result["releasedCount"] = released;
	result["sequencesCancelled"] = true;
	return MCPToolUtils::make_success_result(result);
}

void MCPRuntimeInputService::process() {
	input_scheduler.process();
}

void MCPRuntimeInputService::release_session(const String &p_mcp_session_id) {
	input_scheduler.release_mcp_session(p_mcp_session_id);
}

void MCPRuntimeInputService::shutdown() {
	input_scheduler.release_all();
}
