/**************************************************************************/
/*  mcp_runtime_input_scheduler.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_runtime_input_scheduler.h"

#include "core/os/os.h"

MCPRuntimeInputScheduler::MCPRuntimeInputScheduler(MCPRuntimeDebuggerGateway *p_runtime_gateway) {
	runtime_gateway = p_runtime_gateway;
}

MCPRuntimeInputScheduler::~MCPRuntimeInputScheduler() {
	release_all();
}

Error MCPRuntimeInputScheduler::_request(const String &p_mcp_session_id,
		const MCPRuntimeDebuggerGateway::Session &p_session, const String &p_message, const String &p_operation,
		const Array &p_arguments, Dictionary &r_data, String &r_error, int p_timeout_msec) {
	r_data.clear();
	r_error = String();
	if (!runtime_gateway) {
		r_error = "The runtime debugger gateway is not available.";
		return ERR_UNCONFIGURED;
	}
	active_epoch = true;
	const MCPRuntimeDebuggerGateway::RoundTripResult result = runtime_gateway->round_trip(
			p_session, p_mcp_session_id, "input", "mcp_input:" + p_message, p_operation, p_arguments, p_timeout_msec);
	r_error = result.message;
	switch (result.status) {
		case MCPRuntimeDebuggerGateway::ROUND_TRIP_COMPLETED:
			r_data = result.response.data;
			return OK;
		case MCPRuntimeDebuggerGateway::ROUND_TRIP_BUSY:
			return ERR_BUSY;
		case MCPRuntimeDebuggerGateway::ROUND_TRIP_TIMEOUT:
			return ERR_TIMEOUT;
		case MCPRuntimeDebuggerGateway::ROUND_TRIP_DISCONNECTED:
			return ERR_CONNECTION_ERROR;
		case MCPRuntimeDebuggerGateway::ROUND_TRIP_STALE:
			return ERR_INVALID_DATA;
		case MCPRuntimeDebuggerGateway::ROUND_TRIP_REMOTE_ERROR:
			return ERR_CANT_RESOLVE;
	}
	return ERR_BUG;
}

bool MCPRuntimeInputScheduler::_is_terminal(const SequenceRecord &p_record) {
	const String state = p_record.state.get("state", String());
	return state == "completed" || state == "cancelled" || state == "failed" || state == "stale";
}

bool MCPRuntimeInputScheduler::_prepare_sequence_capacity() {
	while (sequences.size() >= MAX_RETAINED_SEQUENCES) {
		int remove_index = -1;
		for (int i = 0; i < sequence_order.size(); i++) {
			const SequenceRecord *record = sequences.getptr(sequence_order[i]);
			if (!record || _is_terminal(*record)) {
				remove_index = i;
				break;
			}
		}
		if (remove_index < 0) {
			return false;
		}
		sequences.erase(sequence_order[remove_index]);
		sequence_order.remove_at(remove_index);
	}
	return true;
}

void MCPRuntimeInputScheduler::_release_local_session(const String &p_mcp_session_id) {
	if (runtime_gateway) {
		runtime_gateway->release_session_requests(p_mcp_session_id);
	}

	for (int i = sequence_order.size() - 1; i >= 0; i--) {
		const SequenceRecord *record = sequences.getptr(sequence_order[i]);
		if (!record || record->mcp_session_id == p_mcp_session_id) {
			sequences.erase(sequence_order[i]);
			sequence_order.remove_at(i);
		}
	}
}

void MCPRuntimeInputScheduler::_clear_local_state() {
	if (runtime_gateway) {
		runtime_gateway->clear_requests();
	}
	sequences.clear();
	sequence_order.clear();
}

Array MCPRuntimeInputScheduler::_serialize_events(const Vector<MCPRuntimeInput::EncodedEvent> &p_events) const {
	Array events;
	for (const MCPRuntimeInput::EncodedEvent &event : p_events) {
		Dictionary encoded;
		encoded["kind"] = "event";
		encoded["message"] = event.message;
		encoded["arguments"] = event.arguments;
		events.push_back(encoded);
	}
	return events;
}

Array MCPRuntimeInputScheduler::_serialize_steps(const Vector<MCPRuntimeInputSequence::Step> &p_steps) const {
	Array steps;
	for (const MCPRuntimeInputSequence::Step &step : p_steps) {
		Dictionary encoded;
		if (step.type == MCPRuntimeInputSequence::STEP_EVENT) {
			encoded["kind"] = "event";
			encoded["message"] = step.event.message;
			encoded["arguments"] = step.event.arguments;
		} else {
			encoded["kind"] = step.type == MCPRuntimeInputSequence::STEP_WAIT_MSEC ? "wait_ms" : "wait_frames";
			encoded["amount"] = int64_t(step.amount);
		}
		steps.push_back(encoded);
	}
	return steps;
}

Error MCPRuntimeInputScheduler::dispatch_immediate(const String &p_mcp_session_id,
		const MCPRuntimeDebuggerGateway::Session &p_session, const Vector<MCPRuntimeInput::EncodedEvent> &p_events, int &r_dispatched,
		String &r_error, int p_timeout_msec) {
	r_dispatched = 0;
	Dictionary data;
	const Array arguments{ "direct:" + p_mcp_session_id, p_mcp_session_id, _serialize_events(p_events) };
	const Error error = _request(p_mcp_session_id, p_session, "send", "send", arguments, data, r_error, p_timeout_msec);
	if (error == OK) {
		r_dispatched = int(data.get("eventCount", 0));
	}
	return error;
}

Error MCPRuntimeInputScheduler::start_sequence(const String &p_mcp_session_id,
		const MCPRuntimeDebuggerGateway::Session &p_session,
		const Vector<MCPRuntimeInputSequence::Step> &p_steps, Dictionary &r_result, String &r_error, int p_timeout_msec) {
	if (!_prepare_sequence_capacity()) {
		r_error = "The retained runtime input sequence limit is full.";
		return ERR_BUSY;
	}
	const String sequence_id = "input-" + String::num_uint64(next_sequence_id++);
	const Array arguments{ sequence_id, "sequence:" + sequence_id, p_mcp_session_id, _serialize_steps(p_steps) };
	Dictionary data;
	const Error error = _request(p_mcp_session_id, p_session, "sequence_start", "sequence_start", arguments, data, r_error, p_timeout_msec);
	if (error != OK) {
		return error;
	}
	data["debuggerSession"] = p_session.debugger_session;
	data["runtimeGeneration"] = int64_t(p_session.runtime_generation);
	SequenceRecord record;
	record.mcp_session_id = p_mcp_session_id;
	record.debugger_session = p_session.debugger_session;
	record.runtime_generation = p_session.runtime_generation;
	record.state = data;
	sequences[sequence_id] = record;
	sequence_order.push_back(sequence_id);
	r_result = data;
	return OK;
}

Error MCPRuntimeInputScheduler::get_sequence(const String &p_mcp_session_id, const String &p_sequence_id, Dictionary &r_result, String &r_error) {
	SequenceRecord *record = sequences.getptr(p_sequence_id);
	if (!record || record->mcp_session_id != p_mcp_session_id) {
		r_error = "Runtime input sequence was not found for this MCP session.";
		return ERR_DOES_NOT_EXIST;
	}
	MCPRuntimeDebuggerGateway::Session session;
	if (!runtime_gateway || !runtime_gateway->resolve_current_session(record->debugger_session, record->runtime_generation, session)) {
		record->state["state"] = "stale";
		record->state["failure"] = "The running project disconnected or restarted.";
		r_result = record->state;
		return OK;
	}
	Dictionary data;
	const Error error = _request(p_mcp_session_id, session, "sequence_query", "sequence_query", Array{ p_sequence_id, p_mcp_session_id }, data, r_error);
	if (error != OK) {
		return error;
	}
	data["debuggerSession"] = record->debugger_session;
	data["runtimeGeneration"] = int64_t(record->runtime_generation);
	record->state = data;
	r_result = data;
	return OK;
}

Error MCPRuntimeInputScheduler::cancel_sequence(const String &p_mcp_session_id, const String &p_sequence_id,
		const MCPRuntimeDebuggerGateway::Session &p_session, Dictionary &r_result, String &r_error) {
	SequenceRecord *record = sequences.getptr(p_sequence_id);
	if (!record || record->mcp_session_id != p_mcp_session_id) {
		r_error = "Runtime input sequence was not found for this MCP session.";
		return ERR_DOES_NOT_EXIST;
	}
	if (record->debugger_session != p_session.debugger_session || record->runtime_generation != p_session.runtime_generation) {
		r_error = "Runtime input sequence belongs to a different runtime session.";
		return ERR_INVALID_PARAMETER;
	}
	Dictionary data;
	const Error error = _request(p_mcp_session_id, p_session, "sequence_cancel", "sequence_cancel", Array{ p_sequence_id, p_mcp_session_id }, data, r_error);
	if (error != OK) {
		return error;
	}
	data["debuggerSession"] = record->debugger_session;
	data["runtimeGeneration"] = int64_t(record->runtime_generation);
	record->state = data;
	r_result = data;
	return OK;
}

Error MCPRuntimeInputScheduler::release_session_inputs(const String &p_mcp_session_id,
		const MCPRuntimeDebuggerGateway::Session &p_session,
		bool p_cancel_sequences, int &r_released, String &r_error) {
	r_released = 0;
	Dictionary data;
	const Error error = _request(p_mcp_session_id, p_session, "release_session", "release_session", Array{ p_mcp_session_id }, data, r_error);
	if (error == OK) {
		r_released = int(data.get("releasedCount", 0));
		for (KeyValue<String, SequenceRecord> &entry : sequences) {
			if (entry.value.mcp_session_id == p_mcp_session_id && entry.value.debugger_session == p_session.debugger_session &&
					entry.value.runtime_generation == p_session.runtime_generation) {
				entry.value.state["state"] = "cancelled";
			}
		}
	}
	return error;
}

void MCPRuntimeInputScheduler::release_mcp_session(const String &p_mcp_session_id) {
	if (runtime_gateway) {
		runtime_gateway->broadcast_message("mcp_input:release_session", Array{ String(), p_mcp_session_id });
	}
	_release_local_session(p_mcp_session_id);
}

void MCPRuntimeInputScheduler::release_all() {
	if (active_epoch && runtime_gateway) {
		runtime_gateway->broadcast_message("mcp_input:release_all", Array{ String() });
	}
	active_epoch = false;
	_clear_local_state();
}

void MCPRuntimeInputScheduler::process() {
	active_epoch = true;
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	if (now - last_heartbeat_usec < HEARTBEAT_INTERVAL_USEC) {
		return;
	}
	last_heartbeat_usec = now;
	if (runtime_gateway) {
		runtime_gateway->broadcast_message("mcp_input:heartbeat", Array{ String() });
	}
}

bool MCPRuntimeInputScheduler::handle_sequence_message(int p_debugger_session, const String &p_message, const Array &p_data) {
	if (p_message == "mcp_input:sequence_state") {
		if (p_data.size() != 3 || p_data[0].get_type() != Variant::STRING || p_data[1].get_type() != Variant::STRING || p_data[2].get_type() != Variant::DICTIONARY) {
			return true;
		}
		SequenceRecord *record = sequences.getptr(p_data[0]);
		if (record && record->debugger_session == p_debugger_session && runtime_gateway &&
				runtime_gateway->is_runtime_current(record->debugger_session, record->runtime_generation)) {
			record->state = p_data[2];
			record->state["state"] = p_data[1];
			record->state["debuggerSession"] = record->debugger_session;
			record->state["runtimeGeneration"] = int64_t(record->runtime_generation);
		}
		return true;
	}
	return false;
}
