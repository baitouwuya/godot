/**************************************************************************/
/*  mcp_runtime_input_controller.h                                        */
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

#pragma once

#include "mcp_runtime_input_event.h"

#include "core/object/object.h"
#include "core/templates/hash_map.h"

class MCPRuntimeInputController : public Object {
	GDCLASS(MCPRuntimeInputController, Object);

	enum StepType {
		STEP_EVENT,
		STEP_WAIT_MSEC,
		STEP_WAIT_FRAMES,
	};

	struct Step {
		StepType type = STEP_EVENT;
		MCPRuntimeInputEvent event;
		uint64_t amount = 0;
	};

	struct Sequence {
		String id;
		String owner_id;
		String mcp_session_id;
		Vector<Step> steps;
		int step_count = 0;
		int step_index = 0;
		int events_dispatched = 0;
		uint64_t encoded_bytes = 0;
		uint64_t wait_deadline_usec = 0;
		uint64_t wait_frames_remaining = 0;
		String state = "queued";
		String failure;
	};

	struct HeldOwnerKey {
		String mcp_session_id;
		String owner_id;

		static uint32_t hash(const HeldOwnerKey &p_key) {
			uint32_t hash = HashMapHasherDefault::hash(p_key.mcp_session_id);
			return hash_fmix32(hash_murmur3_one_32(HashMapHasherDefault::hash(p_key.owner_id), hash));
		}

		bool operator==(const HeldOwnerKey &p_key) const {
			return mcp_session_id == p_key.mcp_session_id && owner_id == p_key.owner_id;
		}
	};

	struct HeldOwner {
		MCPRuntimeInputEvent event;
		uint64_t order = 0;
	};

	struct HeldState {
		MCPRuntimeInputEvent release_event;
		HashMap<HeldOwnerKey, HeldOwner, HeldOwnerKey> owners;
		bool analog = false;
	};

	static constexpr int MAX_DIRECT_EVENTS = 128;
	static constexpr int MAX_SEQUENCE_STEPS = 768;
	static constexpr int MAX_ACTIVE_SEQUENCES = 32;
	static constexpr int MAX_RETAINED_SEQUENCES = 128;
	static constexpr int MAX_EVENTS_PER_FRAME = 128;
	static constexpr int MAX_EVENTS_PER_SEQUENCE_FRAME = 16;
	static constexpr uint64_t MAX_ENCODED_BYTES = 1024 * 1024;
	static constexpr uint64_t HEARTBEAT_TIMEOUT_USEC = 5 * 1000 * 1000;

	static inline MCPRuntimeInputController *singleton = nullptr;

	HashMap<String, Sequence> sequences;
	Vector<String> sequence_order;
	HashMap<String, HeldState> held_states;
	int sequence_cursor = 0;
	uint64_t queued_bytes = 0;
	uint64_t next_hold_order = 1;
	uint64_t last_heartbeat_usec = 0;
	bool process_connected = false;

	static Error _parse_message(void *p_user, const String &p_message, const Array &p_arguments, bool &r_captured);
	void _process_frame();
	void _ensure_process_connected();
	void _send_response(const String &p_request_id, const String &p_operation, bool p_ok, const String &p_code,
			const String &p_message, const Dictionary &p_data = Dictionary()) const;
	void _send_sequence_state(const Sequence &p_sequence) const;
	Dictionary _sequence_state(const Sequence &p_sequence) const;
	bool _is_terminal(const Sequence &p_sequence) const;
	void _release_sequence_payload(Sequence &r_sequence);
	Error _decode_events(const Array &p_payloads, int p_limit, Vector<MCPRuntimeInputEvent> &r_events, uint64_t &r_bytes, String &r_error) const;
	Error _decode_steps(const Array &p_payloads, Vector<Step> &r_steps, uint64_t &r_bytes, String &r_error) const;
	void _dispatch_owned(const String &p_owner_id, const String &p_mcp_session_id, const MCPRuntimeInputEvent &p_event);
	int _release_owner(const String &p_mcp_session_id, const String &p_owner_id);
	int _release_session(const String &p_mcp_session_id, bool p_cancel_sequences);
	int _release_all(bool p_cancel_sequences, const String &p_failure = String());
	void _finish_sequence(Sequence &r_sequence, const String &p_state, const String &p_failure = String());
	void _prune_sequences();
	bool _handle_send(const Array &p_arguments);
	bool _handle_sequence_start(const Array &p_arguments);
	bool _handle_sequence_query(const Array &p_arguments);
	bool _handle_sequence_cancel(const Array &p_arguments);
	bool _handle_release_session(const Array &p_arguments);
	bool _handle_release_all(const Array &p_arguments);
	bool _handle_heartbeat(const Array &p_arguments);
	void _process_frame_at(uint64_t p_now_usec);

	friend struct MCPRuntimeInputControllerTestAccess;

protected:
	static void _bind_methods();

public:
	static void initialize();
	static void deinitialize();
	static MCPRuntimeInputController *get_singleton() { return singleton; }

	MCPRuntimeInputController();
	~MCPRuntimeInputController();
};
