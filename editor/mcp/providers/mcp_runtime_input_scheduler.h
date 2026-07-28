/**************************************************************************/
/*  mcp_runtime_input_scheduler.h                                         */
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

#include "mcp_runtime_debugger_gateway.h"
#include "mcp_runtime_input_sequence.h"

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

struct MCPRuntimeInputSchedulerTestAccess;

class MCPRuntimeInputScheduler {
	struct SequenceRecord {
		String mcp_session_id;
		int debugger_session = -1;
		uint64_t runtime_generation = 0;
		Dictionary state;
	};

	static constexpr int RESPONSE_TIMEOUT_MSEC = 1000;
	static constexpr int MAX_RETAINED_SEQUENCES = 128;
	static constexpr uint64_t HEARTBEAT_INTERVAL_USEC = 1000 * 1000;

	MCPRuntimeDebuggerGateway *runtime_gateway = nullptr;
	HashMap<String, SequenceRecord> sequences;
	Vector<String> sequence_order;
	uint64_t next_sequence_id = 1;
	uint64_t last_heartbeat_usec = 0;
	bool active_epoch = false;

	Error _request(const String &p_mcp_session_id, const MCPRuntimeDebuggerGateway::Session &p_session,
			const String &p_message, const String &p_operation, const Array &p_arguments,
			Dictionary &r_data, String &r_error, int p_timeout_msec = RESPONSE_TIMEOUT_MSEC);
	static bool _is_terminal(const SequenceRecord &p_record);
	bool _prepare_sequence_capacity();
	void _release_local_session(const String &p_mcp_session_id);
	void _clear_local_state();
	Array _serialize_events(const Vector<MCPRuntimeInput::EncodedEvent> &p_events) const;
	Array _serialize_steps(const Vector<MCPRuntimeInputSequence::Step> &p_steps) const;

	friend struct MCPRuntimeInputSchedulerTestAccess;

public:
	explicit MCPRuntimeInputScheduler(MCPRuntimeDebuggerGateway *p_runtime_gateway);
	~MCPRuntimeInputScheduler();

	Error dispatch_immediate(const String &p_mcp_session_id, const MCPRuntimeDebuggerGateway::Session &p_session,
			const Vector<MCPRuntimeInput::EncodedEvent> &p_events, int &r_dispatched,
			String &r_error, int p_timeout_msec = RESPONSE_TIMEOUT_MSEC);
	Error start_sequence(const String &p_mcp_session_id, const MCPRuntimeDebuggerGateway::Session &p_session,
			const Vector<MCPRuntimeInputSequence::Step> &p_steps, Dictionary &r_result, String &r_error,
			int p_timeout_msec = RESPONSE_TIMEOUT_MSEC);
	Error get_sequence(const String &p_mcp_session_id, const String &p_sequence_id, Dictionary &r_result, String &r_error);
	Error cancel_sequence(const String &p_mcp_session_id, const String &p_sequence_id,
			const MCPRuntimeDebuggerGateway::Session &p_session, Dictionary &r_result, String &r_error);
	Error release_session_inputs(const String &p_mcp_session_id, const MCPRuntimeDebuggerGateway::Session &p_session,
			bool p_cancel_sequences, int &r_released, String &r_error);
	void release_mcp_session(const String &p_mcp_session_id);
	void release_all();
	void process();
	bool handle_sequence_message(int p_debugger_session, const String &p_message, const Array &p_data);
};
