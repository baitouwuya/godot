/**************************************************************************/
/*  mcp_runtime_input_controller.cpp                                      */
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

#include "mcp_runtime_input_controller.h"

#include "core/debugger/engine_debugger.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"

namespace {

bool _has_only(const Dictionary &p_dictionary, const PackedStringArray &p_allowed, String &r_error) {
	for (const Variant &key_value : p_dictionary.keys()) {
		if ((key_value.get_type() != Variant::STRING && key_value.get_type() != Variant::STRING_NAME) || !p_allowed.has(String(key_value))) {
			r_error = "Unknown sequence step property: " + String(key_value);
			return false;
		}
	}
	return true;
}

bool _read_positive_integer(const Variant &p_value, int64_t p_maximum, uint64_t &r_value) {
	if (p_value.get_type() != Variant::INT && p_value.get_type() != Variant::FLOAT) {
		return false;
	}
	const double numeric = p_value;
	if (!Math::is_finite(numeric) || numeric != Math::floor(numeric) || numeric < 1.0 || numeric > double(p_maximum)) {
		return false;
	}
	r_value = uint64_t(numeric);
	return true;
}

} // namespace

void MCPRuntimeInputController::_bind_methods() {
}

MCPRuntimeInputController::MCPRuntimeInputController() {
	singleton = this;
	EngineDebugger::register_message_capture("mcp_input", EngineDebugger::Capture(this, _parse_message));
}

MCPRuntimeInputController::~MCPRuntimeInputController() {
	_release_all(true);
	if (process_connected && SceneTree::get_singleton()) {
		const Callable callback = callable_mp(this, &MCPRuntimeInputController::_process_frame);
		if (SceneTree::get_singleton()->is_connected("process_frame", callback)) {
			SceneTree::get_singleton()->disconnect("process_frame", callback);
		}
	}
	EngineDebugger::unregister_message_capture("mcp_input");
	singleton = nullptr;
}

void MCPRuntimeInputController::initialize() {
	if (!singleton && EngineDebugger::is_active()) {
		memnew(MCPRuntimeInputController);
	}
}

void MCPRuntimeInputController::deinitialize() {
	if (singleton) {
		memdelete(singleton);
	}
}

void MCPRuntimeInputController::_ensure_process_connected() {
	if (process_connected || !SceneTree::get_singleton()) {
		return;
	}
	SceneTree::get_singleton()->connect("process_frame", callable_mp(this, &MCPRuntimeInputController::_process_frame));
	process_connected = true;
	last_heartbeat_usec = OS::get_singleton()->get_ticks_usec();
}

void MCPRuntimeInputController::_send_response(const String &p_request_id, const String &p_operation, bool p_ok, const String &p_code,
		const String &p_message, const Dictionary &p_data) const {
	if (p_request_id.is_empty() || !EngineDebugger::get_singleton()) {
		return;
	}
	EngineDebugger::get_singleton()->send_message("mcp_input:response", Array{ p_request_id, p_operation, p_ok, p_code, p_message, p_data });
}

Dictionary MCPRuntimeInputController::_sequence_state(const Sequence &p_sequence) const {
	Dictionary state;
	state["sequenceId"] = p_sequence.id;
	state["state"] = p_sequence.state;
	state["stepIndex"] = p_sequence.step_index;
	state["stepCount"] = p_sequence.step_count;
	state["eventsDispatched"] = p_sequence.events_dispatched;
	if (p_sequence.wait_deadline_usec > 0) {
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		state["remainingMs"] = int64_t(p_sequence.wait_deadline_usec > now ? (p_sequence.wait_deadline_usec - now + 999) / 1000 : 0);
	}
	if (p_sequence.wait_frames_remaining > 0) {
		state["remainingFrames"] = int64_t(p_sequence.wait_frames_remaining);
	}
	if (!p_sequence.failure.is_empty()) {
		state["failure"] = p_sequence.failure;
	}
	return state;
}

void MCPRuntimeInputController::_send_sequence_state(const Sequence &p_sequence) const {
	if (EngineDebugger::get_singleton()) {
		EngineDebugger::get_singleton()->send_message("mcp_input:sequence_state", Array{ p_sequence.id, p_sequence.state, _sequence_state(p_sequence) });
	}
}

bool MCPRuntimeInputController::_is_terminal(const Sequence &p_sequence) const {
	return p_sequence.state == "completed" || p_sequence.state == "cancelled" || p_sequence.state == "failed";
}

void MCPRuntimeInputController::_release_sequence_payload(Sequence &r_sequence) {
	if (r_sequence.encoded_bytes == 0) {
		r_sequence.steps.clear();
		return;
	}
	ERR_FAIL_COND_MSG(r_sequence.encoded_bytes > queued_bytes, "MCP runtime input sequence byte accounting is inconsistent.");
	queued_bytes -= r_sequence.encoded_bytes;
	r_sequence.encoded_bytes = 0;
	r_sequence.steps.clear();
}

Error MCPRuntimeInputController::_decode_events(const Array &p_payloads, int p_limit, Vector<MCPRuntimeInputEvent> &r_events, uint64_t &r_bytes, String &r_error) const {
	r_events.clear();
	r_bytes = 0;
	if (p_payloads.is_empty() || p_payloads.size() > p_limit) {
		r_error = vformat("events must contain between 1 and %d entries.", p_limit);
		return ERR_INVALID_DATA;
	}
	for (int i = 0; i < p_payloads.size(); i++) {
		if (p_payloads[i].get_type() != Variant::DICTIONARY) {
			r_error = vformat("events[%d] must be an object.", i);
			return ERR_INVALID_DATA;
		}
		MCPRuntimeInputEvent event;
		String event_error;
		if (MCPRuntimeInputEvent::decode(p_payloads[i], event, event_error) != OK) {
			r_error = vformat("events[%d]: %s", i, event_error);
			return ERR_INVALID_DATA;
		}
		r_bytes += event.encoded_bytes;
		if (r_bytes > MAX_ENCODED_BYTES) {
			r_error = "The encoded input payload exceeds one MiB.";
			return ERR_OUT_OF_MEMORY;
		}
		r_events.push_back(event);
	}
	return OK;
}

Error MCPRuntimeInputController::_decode_steps(const Array &p_payloads, Vector<Step> &r_steps, uint64_t &r_bytes, String &r_error) const {
	r_steps.clear();
	r_bytes = 0;
	if (p_payloads.is_empty() || p_payloads.size() > MAX_SEQUENCE_STEPS) {
		r_error = vformat("steps must contain between 1 and %d compiled entries.", MAX_SEQUENCE_STEPS);
		return ERR_INVALID_DATA;
	}
	uint64_t total_wait_msec = 0;
	uint64_t total_wait_frames = 0;
	for (int i = 0; i < p_payloads.size(); i++) {
		if (p_payloads[i].get_type() != Variant::DICTIONARY) {
			r_error = vformat("steps[%d] must be an object.", i);
			return ERR_INVALID_DATA;
		}
		const Dictionary payload = p_payloads[i];
		const Variant kind_value = payload.get("kind", Variant());
		if (kind_value.get_type() != Variant::STRING) {
			r_error = vformat("steps[%d].kind must be a string.", i);
			return ERR_INVALID_DATA;
		}
		const String kind = kind_value;
		Step step;
		if (kind == "event") {
			String event_error;
			if (MCPRuntimeInputEvent::decode(payload, step.event, event_error) != OK) {
				r_error = vformat("steps[%d]: %s", i, event_error);
				return ERR_INVALID_DATA;
			}
			r_bytes += step.event.encoded_bytes;
		} else if (kind == "wait_ms" || kind == "wait_frames") {
			if (!_has_only(payload, PackedStringArray{ "kind", "amount" }, r_error) ||
					!_read_positive_integer(payload.get("amount", Variant()), kind == "wait_ms" ? 600000 : 36000, step.amount)) {
				if (r_error.is_empty()) {
					r_error = vformat("steps[%d].amount is outside its allowed range.", i);
				}
				return ERR_INVALID_DATA;
			}
			step.type = kind == "wait_ms" ? STEP_WAIT_MSEC : STEP_WAIT_FRAMES;
			if (step.type == STEP_WAIT_MSEC) {
				total_wait_msec += step.amount;
			} else {
				total_wait_frames += step.amount;
			}
		} else {
			r_error = vformat("steps[%d].kind is unsupported.", i);
			return ERR_INVALID_DATA;
		}
		if (r_bytes > MAX_ENCODED_BYTES || total_wait_msec > 600000 || total_wait_frames > 36000) {
			r_error = "The sequence exceeds its encoded byte or total wait limit.";
			return ERR_OUT_OF_MEMORY;
		}
		r_steps.push_back(step);
	}
	return OK;
}

void MCPRuntimeInputController::_dispatch_owned(const String &p_owner_id, const String &p_mcp_session_id, const MCPRuntimeInputEvent &p_event) {
	if (!p_event.stateful) {
		p_event.dispatch();
		return;
	}
	const HeldOwnerKey owner_key{ p_mcp_session_id, p_owner_id };
	HeldState *held = held_states.getptr(p_event.state_key);
	if (p_event.active) {
		p_event.dispatch();
		if (!held) {
			HeldState state;
			state.release_event = p_event;
			state.analog = p_event.analog;
			held_states.insert(p_event.state_key, state);
			held = held_states.getptr(p_event.state_key);
		}
		HeldOwner owner;
		owner.event = p_event;
		owner.order = next_hold_order++;
		held->owners[owner_key] = owner;
		return;
	}

	if (!held) {
		return;
	}
	if (!held->owners.has(owner_key)) {
		return;
	}
	held->owners.erase(owner_key);
	if (held->owners.is_empty()) {
		held->release_event.dispatch_release();
		held_states.erase(p_event.state_key);
		return;
	}
	if (held->analog) {
		const HeldOwner *winner = nullptr;
		for (const KeyValue<HeldOwnerKey, HeldOwner> &entry : held->owners) {
			if (!winner || entry.value.order > winner->order) {
				winner = &entry.value;
			}
		}
		if (winner) {
			winner->event.dispatch();
		}
	}
}

int MCPRuntimeInputController::_release_owner(const String &p_mcp_session_id, const String &p_owner_id) {
	const HeldOwnerKey owner_key{ p_mcp_session_id, p_owner_id };
	Vector<String> keys;
	for (const KeyValue<String, HeldState> &entry : held_states) {
		if (entry.value.owners.has(owner_key)) {
			keys.push_back(entry.key);
		}
	}
	int released = 0;
	for (const String &key : keys) {
		HeldState *held = held_states.getptr(key);
		if (!held) {
			continue;
		}
		held->owners.erase(owner_key);
		if (held->owners.is_empty()) {
			held->release_event.dispatch_release();
			held_states.erase(key);
			released++;
		} else if (held->analog) {
			const HeldOwner *winner = nullptr;
			for (const KeyValue<HeldOwnerKey, HeldOwner> &entry : held->owners) {
				if (!winner || entry.value.order > winner->order) {
					winner = &entry.value;
				}
			}
			if (winner) {
				winner->event.dispatch();
			}
		}
	}
	return released;
}

void MCPRuntimeInputController::_finish_sequence(Sequence &r_sequence, const String &p_state, const String &p_failure) {
	_release_owner(r_sequence.mcp_session_id, r_sequence.owner_id);
	r_sequence.state = p_state;
	r_sequence.failure = p_failure;
	r_sequence.wait_deadline_usec = 0;
	r_sequence.wait_frames_remaining = 0;
	_release_sequence_payload(r_sequence);
	_send_sequence_state(r_sequence);
}

int MCPRuntimeInputController::_release_session(const String &p_mcp_session_id, bool p_cancel_sequences) {
	if (p_cancel_sequences) {
		for (KeyValue<String, Sequence> &entry : sequences) {
			if (entry.value.mcp_session_id == p_mcp_session_id && !_is_terminal(entry.value)) {
				_finish_sequence(entry.value, "cancelled");
			}
		}
	}
	Vector<HeldOwnerKey> owners;
	for (const KeyValue<String, HeldState> &entry : held_states) {
		for (const KeyValue<HeldOwnerKey, HeldOwner> &owner : entry.value.owners) {
			if (owner.key.mcp_session_id == p_mcp_session_id && !owners.has(owner.key)) {
				owners.push_back(owner.key);
			}
		}
	}
	int released = 0;
	for (const HeldOwnerKey &owner : owners) {
		released += _release_owner(owner.mcp_session_id, owner.owner_id);
	}
	return released;
}

int MCPRuntimeInputController::_release_all(bool p_cancel_sequences, const String &p_failure) {
	if (p_cancel_sequences) {
		for (KeyValue<String, Sequence> &entry : sequences) {
			if (!_is_terminal(entry.value)) {
				_finish_sequence(entry.value, "cancelled", p_failure);
			}
		}
	}
	Vector<HeldOwnerKey> owners;
	for (const KeyValue<String, HeldState> &entry : held_states) {
		for (const KeyValue<HeldOwnerKey, HeldOwner> &owner : entry.value.owners) {
			if (!owners.has(owner.key)) {
				owners.push_back(owner.key);
			}
		}
	}
	int released = 0;
	for (const HeldOwnerKey &owner : owners) {
		released += _release_owner(owner.mcp_session_id, owner.owner_id);
	}
	return released;
}

void MCPRuntimeInputController::_prune_sequences() {
	while (sequences.size() >= MAX_RETAINED_SEQUENCES) {
		int remove_index = -1;
		for (int i = 0; i < sequence_order.size(); i++) {
			const Sequence *sequence = sequences.getptr(sequence_order[i]);
			if (!sequence || _is_terminal(*sequence)) {
				remove_index = i;
				break;
			}
		}
		if (remove_index < 0) {
			return;
		}
		sequences.erase(sequence_order[remove_index]);
		sequence_order.remove_at(remove_index);
	}
	sequence_cursor = sequence_order.is_empty() ? 0 : sequence_cursor % sequence_order.size();
}

bool MCPRuntimeInputController::_handle_send(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 4 || p_arguments[0].get_type() != Variant::STRING || p_arguments[1].get_type() != Variant::STRING ||
			p_arguments[2].get_type() != Variant::STRING || p_arguments[3].get_type() != Variant::ARRAY ||
			String(p_arguments[1]).is_empty() || String(p_arguments[2]).is_empty()) {
		_send_response(request_id, "send", false, "INVALID_ARGUMENTS", "send requires requestId, ownerId, mcpSessionId, and events.");
		return false;
	}
	Vector<MCPRuntimeInputEvent> events;
	uint64_t bytes = 0;
	String error;
	if (_decode_events(p_arguments[3], MAX_DIRECT_EVENTS, events, bytes, error) != OK) {
		_send_response(request_id, "send", false, "INVALID_INPUT_EVENT", error);
		return false;
	}
	for (const MCPRuntimeInputEvent &event : events) {
		_dispatch_owned(p_arguments[1], p_arguments[2], event);
	}
	Dictionary result;
	result["eventCount"] = events.size();
	_send_response(request_id, "send", true, String(), String(), result);
	return true;
}

bool MCPRuntimeInputController::_handle_sequence_start(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 5 || p_arguments[0].get_type() != Variant::STRING || p_arguments[1].get_type() != Variant::STRING ||
			p_arguments[2].get_type() != Variant::STRING || p_arguments[3].get_type() != Variant::STRING ||
			p_arguments[4].get_type() != Variant::ARRAY || String(p_arguments[1]).is_empty() ||
			String(p_arguments[2]).is_empty() || String(p_arguments[3]).is_empty()) {
		_send_response(request_id, "sequence_start", false, "INVALID_ARGUMENTS", "sequence_start requires IDs and steps.");
		return false;
	}
	int active = 0;
	for (const KeyValue<String, Sequence> &entry : sequences) {
		if (!_is_terminal(entry.value)) {
			active++;
		}
	}
	_prune_sequences();
	if (active >= MAX_ACTIVE_SEQUENCES || sequences.size() >= MAX_RETAINED_SEQUENCES || sequences.has(p_arguments[1])) {
		_send_response(request_id, "sequence_start", false, "INPUT_SEQUENCE_BUSY", "The sequence queue is full or the ID already exists.");
		return true;
	}
	for (const KeyValue<String, Sequence> &entry : sequences) {
		if (!_is_terminal(entry.value) && entry.value.owner_id == String(p_arguments[2]) && entry.value.mcp_session_id == String(p_arguments[3])) {
			_send_response(request_id, "sequence_start", false, "INPUT_OWNER_BUSY", "The MCP session already has an active sequence for this input owner.");
			return true;
		}
	}
	Vector<Step> steps;
	uint64_t bytes = 0;
	String error;
	if (_decode_steps(p_arguments[4], steps, bytes, error) != OK) {
		_send_response(request_id, "sequence_start", false, "INVALID_INPUT_SEQUENCE", error);
		return false;
	}
	if (queued_bytes > MAX_ENCODED_BYTES || bytes > MAX_ENCODED_BYTES - queued_bytes) {
		_send_response(request_id, "sequence_start", false, "INPUT_SEQUENCE_BUSY", "The runtime input sequence byte queue is full.");
		return true;
	}
	Sequence sequence;
	sequence.id = p_arguments[1];
	sequence.owner_id = p_arguments[2];
	sequence.mcp_session_id = p_arguments[3];
	sequence.steps = steps;
	sequence.step_count = steps.size();
	sequence.encoded_bytes = bytes;
	queued_bytes += bytes;
	sequences[sequence.id] = sequence;
	sequence_order.push_back(sequence.id);
	Sequence *stored = sequences.getptr(sequence.id);
	_send_response(request_id, "sequence_start", true, String(), String(), _sequence_state(*stored));
	_send_sequence_state(*stored);
	return true;
}

bool MCPRuntimeInputController::_handle_sequence_query(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 3 || p_arguments[0].get_type() != Variant::STRING || p_arguments[1].get_type() != Variant::STRING || p_arguments[2].get_type() != Variant::STRING) {
		_send_response(request_id, "sequence_query", false, "INVALID_ARGUMENTS", "sequence_query requires sequenceId and mcpSessionId.");
		return false;
	}
	const Sequence *sequence = sequences.getptr(p_arguments[1]);
	if (!sequence || sequence->mcp_session_id != String(p_arguments[2])) {
		_send_response(request_id, "sequence_query", false, "INPUT_SEQUENCE_NOT_FOUND", "The sequence was not found for this MCP session.");
		return true;
	}
	_send_response(request_id, "sequence_query", true, String(), String(), _sequence_state(*sequence));
	return true;
}

bool MCPRuntimeInputController::_handle_sequence_cancel(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 3 || p_arguments[0].get_type() != Variant::STRING || p_arguments[1].get_type() != Variant::STRING || p_arguments[2].get_type() != Variant::STRING) {
		_send_response(request_id, "sequence_cancel", false, "INVALID_ARGUMENTS", "sequence_cancel requires sequenceId and mcpSessionId.");
		return false;
	}
	Sequence *sequence = sequences.getptr(p_arguments[1]);
	if (!sequence || sequence->mcp_session_id != String(p_arguments[2])) {
		_send_response(request_id, "sequence_cancel", false, "INPUT_SEQUENCE_NOT_FOUND", "The sequence was not found for this MCP session.");
		return true;
	}
	if (!_is_terminal(*sequence)) {
		_finish_sequence(*sequence, "cancelled");
	}
	_send_response(request_id, "sequence_cancel", true, String(), String(), _sequence_state(*sequence));
	return true;
}

bool MCPRuntimeInputController::_handle_release_session(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 2 || p_arguments[0].get_type() != Variant::STRING || p_arguments[1].get_type() != Variant::STRING || String(p_arguments[1]).is_empty()) {
		_send_response(request_id, "release_session", false, "INVALID_ARGUMENTS", "release_session requires mcpSessionId.");
		return false;
	}
	const int released = _release_session(p_arguments[1], true);
	Dictionary result;
	result["releasedCount"] = released;
	_send_response(request_id, "release_session", true, String(), String(), result);
	return true;
}

bool MCPRuntimeInputController::_handle_release_all(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 1 || p_arguments[0].get_type() != Variant::STRING) {
		_send_response(request_id, "release_all", false, "INVALID_ARGUMENTS", "release_all accepts only requestId.");
		return false;
	}
	const int released = _release_all(true);
	Dictionary result;
	result["releasedCount"] = released;
	_send_response(request_id, "release_all", true, String(), String(), result);
	return true;
}

bool MCPRuntimeInputController::_handle_heartbeat(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 1 || p_arguments[0].get_type() != Variant::STRING) {
		_send_response(request_id, "heartbeat", false, "INVALID_ARGUMENTS", "heartbeat accepts only requestId.");
		return false;
	}
	_send_response(request_id, "heartbeat", true, String(), String());
	return true;
}

Error MCPRuntimeInputController::_parse_message(void *p_user, const String &p_message, const Array &p_arguments, bool &r_captured) {
	MCPRuntimeInputController *controller = static_cast<MCPRuntimeInputController *>(p_user);
	ERR_FAIL_NULL_V(controller, ERR_UNCONFIGURED);
	r_captured = true;
	bool valid_request = false;
	if (p_message == "send") {
		valid_request = controller->_handle_send(p_arguments);
	} else if (p_message == "sequence_start") {
		valid_request = controller->_handle_sequence_start(p_arguments);
	} else if (p_message == "sequence_query") {
		valid_request = controller->_handle_sequence_query(p_arguments);
	} else if (p_message == "sequence_cancel") {
		valid_request = controller->_handle_sequence_cancel(p_arguments);
	} else if (p_message == "release_session") {
		valid_request = controller->_handle_release_session(p_arguments);
	} else if (p_message == "release_all") {
		valid_request = controller->_handle_release_all(p_arguments);
	} else if (p_message == "heartbeat") {
		valid_request = controller->_handle_heartbeat(p_arguments);
	} else {
		r_captured = false;
		return OK;
	}
	if (valid_request) {
		controller->_ensure_process_connected();
		controller->last_heartbeat_usec = OS::get_singleton()->get_ticks_usec();
	}
	return OK;
}

void MCPRuntimeInputController::_process_frame() {
	_process_frame_at(OS::get_singleton()->get_ticks_usec());
}

void MCPRuntimeInputController::_process_frame_at(uint64_t p_now_usec) {
	const uint64_t now = p_now_usec;
	bool has_active_sequence = false;
	for (const KeyValue<String, Sequence> &entry : sequences) {
		if (!_is_terminal(entry.value)) {
			has_active_sequence = true;
			break;
		}
	}
	if ((!held_states.is_empty() || has_active_sequence) && last_heartbeat_usec > 0 && now - last_heartbeat_usec > HEARTBEAT_TIMEOUT_USEC) {
		_release_all(true, "The MCP input heartbeat timed out.");
		last_heartbeat_usec = now;
	}
	if (sequence_order.is_empty()) {
		return;
	}

	int global_budget = MAX_EVENTS_PER_FRAME;
	int visited = 0;
	int index = sequence_cursor % sequence_order.size();
	while (visited < sequence_order.size() && global_budget > 0) {
		Sequence *sequence = sequences.getptr(sequence_order[index]);
		index = (index + 1) % sequence_order.size();
		visited++;
		if (!sequence || _is_terminal(*sequence)) {
			continue;
		}
		if (sequence->state == "queued") {
			sequence->state = "running";
			_send_sequence_state(*sequence);
		}
		if (sequence->wait_deadline_usec > 0) {
			if (now < sequence->wait_deadline_usec) {
				continue;
			}
			sequence->wait_deadline_usec = 0;
			sequence->step_index++;
			sequence->state = "running";
			_send_sequence_state(*sequence);
		}
		if (sequence->wait_frames_remaining > 0) {
			sequence->wait_frames_remaining--;
			if (sequence->wait_frames_remaining > 0) {
				continue;
			}
			sequence->step_index++;
			sequence->state = "running";
			_send_sequence_state(*sequence);
		}

		int sequence_budget = MIN(global_budget, MAX_EVENTS_PER_SEQUENCE_FRAME);
		while (sequence->step_index < sequence->steps.size() && sequence_budget > 0) {
			const Step &step = sequence->steps[sequence->step_index];
			if (step.type == STEP_WAIT_MSEC) {
				sequence->wait_deadline_usec = now + step.amount * 1000;
				sequence->state = "waiting";
				_send_sequence_state(*sequence);
				break;
			}
			if (step.type == STEP_WAIT_FRAMES) {
				sequence->wait_frames_remaining = step.amount;
				sequence->state = "waiting";
				_send_sequence_state(*sequence);
				break;
			}
			_dispatch_owned(sequence->owner_id, sequence->mcp_session_id, step.event);
			sequence->step_index++;
			sequence->events_dispatched++;
			sequence_budget--;
			global_budget--;
		}
		if (sequence->step_index >= sequence->steps.size()) {
			_finish_sequence(*sequence, "completed");
		}
	}
	sequence_cursor = index;
}
