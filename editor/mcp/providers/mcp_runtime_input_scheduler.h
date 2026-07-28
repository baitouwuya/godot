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

#include "mcp_runtime_input_sequence.h"
#include "mcp_runtime_request_broker.h"

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class MCPDebugCapture;
class ScriptEditorDebugger;
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

	MCPDebugCapture *debug_capture = nullptr;
	MCPRuntimeRequestBroker request_broker;
	HashMap<String, SequenceRecord> sequences;
	Vector<String> sequence_order;
	uint64_t next_sequence_id = 1;
	uint64_t last_heartbeat_usec = 0;

	bool _resolve_debugger(int p_debugger_session, uint64_t p_runtime_generation, ScriptEditorDebugger *&r_debugger) const;
	Error _request(const String &p_mcp_session_id, ScriptEditorDebugger *p_debugger, const String &p_message, const String &p_operation, const Array &p_arguments,
			Dictionary &r_data, String &r_error, int p_timeout_msec = RESPONSE_TIMEOUT_MSEC);
	static bool _is_terminal(const SequenceRecord &p_record);
	bool _prepare_sequence_capacity();
	void _release_local_session(const String &p_mcp_session_id);
	void _clear_local_state();
	Array _serialize_events(const Vector<MCPRuntimeInput::EncodedEvent> &p_events) const;
	Array _serialize_steps(const Vector<MCPRuntimeInputSequence::Step> &p_steps) const;

	friend struct MCPRuntimeInputSchedulerTestAccess;

public:
	explicit MCPRuntimeInputScheduler(MCPDebugCapture *p_debug_capture);
	~MCPRuntimeInputScheduler();

	Error dispatch_immediate(const String &p_mcp_session_id, ScriptEditorDebugger *p_debugger, int p_debugger_session,
			uint64_t p_runtime_generation, const Vector<MCPRuntimeInput::EncodedEvent> &p_events, int &r_dispatched,
			String &r_error, int p_timeout_msec = RESPONSE_TIMEOUT_MSEC);
	Error start_sequence(const String &p_mcp_session_id, int p_debugger_session, uint64_t p_runtime_generation,
			const Vector<MCPRuntimeInputSequence::Step> &p_steps, Dictionary &r_result, String &r_error,
			int p_timeout_msec = RESPONSE_TIMEOUT_MSEC);
	Error get_sequence(const String &p_mcp_session_id, const String &p_sequence_id, Dictionary &r_result, String &r_error);
	Error cancel_sequence(const String &p_mcp_session_id, const String &p_sequence_id, int p_debugger_session,
			uint64_t p_runtime_generation, Dictionary &r_result, String &r_error);
	Error release_session_inputs(const String &p_mcp_session_id, int p_debugger_session, uint64_t p_runtime_generation,
			bool p_cancel_sequences, int &r_released, String &r_error);
	void release_mcp_session(const String &p_mcp_session_id);
	void release_all();
	void process();
	bool handle_runtime_message(int p_debugger_session, const String &p_message, const Array &p_data);
};
