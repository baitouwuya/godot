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

#include "mcp_debug_capture.h"

#include "core/os/os.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"

MCPRuntimeInputScheduler::MCPRuntimeInputScheduler(MCPDebugCapture *p_debug_capture) {
	debug_capture = p_debug_capture;
}

MCPRuntimeInputScheduler::~MCPRuntimeInputScheduler() {
	release_all();
}

bool MCPRuntimeInputScheduler::_resolve_debugger(int p_debugger_session, uint64_t p_runtime_generation, ScriptEditorDebugger *&r_debugger) const {
	r_debugger = nullptr;
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node || !debug_capture) {
		return false;
	}
	uint64_t current_generation = 0;
	if (!debug_capture->get_runtime_generation(p_debugger_session, current_generation) || current_generation != p_runtime_generation) {
		return false;
	}
	r_debugger = debugger_node->get_debugger(p_debugger_session);
	return r_debugger && r_debugger->is_session_active();
}

String MCPRuntimeInputScheduler::_new_request_id() {
	return "request-" + String::num_uint64(next_request_id++);
}

Error MCPRuntimeInputScheduler::_request(ScriptEditorDebugger *p_debugger, const String &p_message, const String &p_operation,
		const Array &p_arguments, Dictionary &r_data, String &r_error, int p_timeout_msec) {
	r_data.clear();
	r_error = String();
	if (!p_debugger || !p_debugger->is_session_active()) {
		r_error = "The running project debugger is not connected.";
		return ERR_CONNECTION_ERROR;
	}
	const String request_id = _new_request_id();
	Response pending;
	if (EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton()) {
		for (int i = 0; i < debugger_node->get_debugger_count(); i++) {
			if (debugger_node->get_debugger(i) == p_debugger) {
				pending.debugger_session = i;
				break;
			}
		}
	}
	if (pending.debugger_session < 0) {
		r_error = "The runtime debugger session could not be identified.";
		return ERR_DOES_NOT_EXIST;
	}
	responses.insert(request_id, pending);
	Array arguments = p_arguments.duplicate();
	arguments.push_front(request_id);
	p_debugger->send_message("mcp_input:" + p_message, arguments);

	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + uint64_t(p_timeout_msec) * 1000;
	while (p_debugger->is_session_active() && OS::get_singleton()->get_ticks_usec() < deadline) {
		p_debugger->poll_peer_messages(2000);
		Response *response = responses.getptr(request_id);
		if (response && response->received) {
			const Response completed = *response;
			responses.erase(request_id);
			if (completed.operation != p_operation) {
				r_error = "The running project returned a mismatched input response.";
				return ERR_INVALID_DATA;
			}
			if (!completed.ok) {
				r_error = completed.message.is_empty() ? completed.code : completed.message;
				return ERR_CANT_RESOLVE;
			}
			r_data = completed.data;
			return OK;
		}
		OS::get_singleton()->delay_usec(500);
	}
	responses.erase(request_id);
	r_error = "Timed out waiting for the running project to acknowledge runtime input.";
	return ERR_TIMEOUT;
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

Error MCPRuntimeInputScheduler::dispatch_immediate(const String &p_mcp_session_id, ScriptEditorDebugger *p_debugger, int p_debugger_session,
		uint64_t p_runtime_generation, const Vector<MCPRuntimeInput::EncodedEvent> &p_events, int &r_dispatched, String &r_error) {
	r_dispatched = 0;
	Dictionary data;
	const Array arguments{ "direct:" + p_mcp_session_id, p_mcp_session_id, _serialize_events(p_events) };
	const Error error = _request(p_debugger, "send", "send", arguments, data, r_error);
	if (error == OK) {
		r_dispatched = int(data.get("eventCount", 0));
	}
	return error;
}

Error MCPRuntimeInputScheduler::start_sequence(const String &p_mcp_session_id, int p_debugger_session, uint64_t p_runtime_generation,
		const Vector<MCPRuntimeInputSequence::Step> &p_steps, Dictionary &r_result, String &r_error) {
	ScriptEditorDebugger *debugger = nullptr;
	if (!_resolve_debugger(p_debugger_session, p_runtime_generation, debugger)) {
		r_error = "The running project disconnected or restarted.";
		return ERR_CONNECTION_ERROR;
	}
	const String sequence_id = "input-" + String::num_uint64(next_sequence_id++);
	const Array arguments{ sequence_id, "sequence:" + sequence_id, p_mcp_session_id, _serialize_steps(p_steps) };
	Dictionary data;
	const Error error = _request(debugger, "sequence_start", "sequence_start", arguments, data, r_error);
	if (error != OK) {
		return error;
	}
	data["debuggerSession"] = p_debugger_session;
	data["runtimeGeneration"] = int64_t(p_runtime_generation);
	SequenceRecord record;
	record.mcp_session_id = p_mcp_session_id;
	record.debugger_session = p_debugger_session;
	record.runtime_generation = p_runtime_generation;
	record.state = data;
	sequences[sequence_id] = record;
	r_result = data;
	return OK;
}

Error MCPRuntimeInputScheduler::get_sequence(const String &p_mcp_session_id, const String &p_sequence_id, Dictionary &r_result, String &r_error) {
	SequenceRecord *record = sequences.getptr(p_sequence_id);
	if (!record || record->mcp_session_id != p_mcp_session_id) {
		r_error = "Runtime input sequence was not found for this MCP session.";
		return ERR_DOES_NOT_EXIST;
	}
	ScriptEditorDebugger *debugger = nullptr;
	if (!_resolve_debugger(record->debugger_session, record->runtime_generation, debugger)) {
		record->state["state"] = "stale";
		record->state["failure"] = "The running project disconnected or restarted.";
		r_result = record->state;
		return OK;
	}
	Dictionary data;
	const Error error = _request(debugger, "sequence_query", "sequence_query", Array{ p_sequence_id, p_mcp_session_id }, data, r_error);
	if (error != OK) {
		return error;
	}
	data["debuggerSession"] = record->debugger_session;
	data["runtimeGeneration"] = int64_t(record->runtime_generation);
	record->state = data;
	r_result = data;
	return OK;
}

Error MCPRuntimeInputScheduler::cancel_sequence(const String &p_mcp_session_id, const String &p_sequence_id, int p_debugger_session,
		uint64_t p_runtime_generation, Dictionary &r_result, String &r_error) {
	SequenceRecord *record = sequences.getptr(p_sequence_id);
	if (!record || record->mcp_session_id != p_mcp_session_id) {
		r_error = "Runtime input sequence was not found for this MCP session.";
		return ERR_DOES_NOT_EXIST;
	}
	if (record->debugger_session != p_debugger_session || record->runtime_generation != p_runtime_generation) {
		r_error = "Runtime input sequence belongs to a different runtime session.";
		return ERR_INVALID_PARAMETER;
	}
	ScriptEditorDebugger *debugger = nullptr;
	if (!_resolve_debugger(p_debugger_session, p_runtime_generation, debugger)) {
		r_error = "The running project disconnected or restarted.";
		return ERR_CONNECTION_ERROR;
	}
	Dictionary data;
	const Error error = _request(debugger, "sequence_cancel", "sequence_cancel", Array{ p_sequence_id, p_mcp_session_id }, data, r_error);
	if (error != OK) {
		return error;
	}
	data["debuggerSession"] = record->debugger_session;
	data["runtimeGeneration"] = int64_t(record->runtime_generation);
	record->state = data;
	r_result = data;
	return OK;
}

Error MCPRuntimeInputScheduler::release_session_inputs(const String &p_mcp_session_id, int p_debugger_session, uint64_t p_runtime_generation,
		bool p_cancel_sequences, int &r_released, String &r_error) {
	r_released = 0;
	ScriptEditorDebugger *debugger = nullptr;
	if (!_resolve_debugger(p_debugger_session, p_runtime_generation, debugger)) {
		r_error = "The running project disconnected or restarted.";
		return ERR_CONNECTION_ERROR;
	}
	Dictionary data;
	const Error error = _request(debugger, "release_session", "release_session", Array{ p_mcp_session_id }, data, r_error);
	if (error == OK) {
		r_released = int(data.get("releasedCount", 0));
		for (KeyValue<String, SequenceRecord> &entry : sequences) {
			if (entry.value.mcp_session_id == p_mcp_session_id && entry.value.debugger_session == p_debugger_session &&
					entry.value.runtime_generation == p_runtime_generation) {
				entry.value.state["state"] = "cancelled";
			}
		}
	}
	return error;
}

void MCPRuntimeInputScheduler::release_mcp_session(const String &p_mcp_session_id) {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node) {
		return;
	}
	for (int i = 0; i < debugger_node->get_debugger_count(); i++) {
		ScriptEditorDebugger *debugger = debugger_node->get_debugger(i);
		if (debugger && debugger->is_session_active()) {
			debugger->send_message("mcp_input:release_session", Array{ String(), p_mcp_session_id });
		}
	}
}

void MCPRuntimeInputScheduler::release_all() {
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node) {
		return;
	}
	for (int i = 0; i < debugger_node->get_debugger_count(); i++) {
		ScriptEditorDebugger *debugger = debugger_node->get_debugger(i);
		if (debugger && debugger->is_session_active()) {
			debugger->send_message("mcp_input:release_all", Array{ String() });
		}
	}
}

void MCPRuntimeInputScheduler::process() {
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	if (now - last_heartbeat_usec < HEARTBEAT_INTERVAL_USEC) {
		return;
	}
	last_heartbeat_usec = now;
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node) {
		return;
	}
	for (int i = 0; i < debugger_node->get_debugger_count(); i++) {
		ScriptEditorDebugger *debugger = debugger_node->get_debugger(i);
		if (debugger && debugger->is_session_active()) {
			debugger->send_message("mcp_input:heartbeat", Array{ String() });
		}
	}
}

bool MCPRuntimeInputScheduler::handle_runtime_message(int p_debugger_session, const String &p_message, const Array &p_data) {
	if (p_message == "mcp_input:response") {
		if (p_data.size() != 6 || p_data[0].get_type() != Variant::STRING || p_data[1].get_type() != Variant::STRING ||
				p_data[2].get_type() != Variant::BOOL || p_data[3].get_type() != Variant::STRING ||
				p_data[4].get_type() != Variant::STRING || p_data[5].get_type() != Variant::DICTIONARY) {
			return true;
		}
		Response *response = responses.getptr(p_data[0]);
		if (response && response->debugger_session == p_debugger_session) {
			response->received = true;
			response->operation = p_data[1];
			response->ok = p_data[2];
			response->code = p_data[3];
			response->message = p_data[4];
			response->data = p_data[5];
		}
		return true;
	}
	if (p_message == "mcp_input:sequence_state") {
		if (p_data.size() != 3 || p_data[0].get_type() != Variant::STRING || p_data[1].get_type() != Variant::STRING || p_data[2].get_type() != Variant::DICTIONARY) {
			return true;
		}
		SequenceRecord *record = sequences.getptr(p_data[0]);
		if (record && record->debugger_session == p_debugger_session) {
			record->state = p_data[2];
			record->state["state"] = p_data[1];
			record->state["debuggerSession"] = record->debugger_session;
			record->state["runtimeGeneration"] = int64_t(record->runtime_generation);
		}
		return true;
	}
	return false;
}
