/**************************************************************************/
/*  mcp_runtime_debugger_gateway.h                                       */
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

#pragma once

#include "mcp_runtime_request_broker.h"

#include "core/variant/dictionary.h"

class MCPDebugCapture;
class ScriptEditorDebugger;
struct MCPRuntimeDebuggerGatewayTestAccess;

class MCPRuntimeDebuggerGateway {
public:
	struct Session {
		ScriptEditorDebugger *debugger = nullptr;
		int debugger_session = -1;
		uint64_t runtime_generation = 0;
	};

	enum RoundTripStatus {
		ROUND_TRIP_COMPLETED,
		ROUND_TRIP_BUSY,
		ROUND_TRIP_TIMEOUT,
		ROUND_TRIP_DISCONNECTED,
		ROUND_TRIP_STALE,
		ROUND_TRIP_REMOTE_ERROR,
	};

	struct RoundTripResult {
		RoundTripStatus status = ROUND_TRIP_DISCONNECTED;
		MCPRuntimeRequestBroker::Response response;
		String message;
	};

private:
	MCPDebugCapture *debug_capture = nullptr;
	MCPRuntimeRequestBroker request_broker;

	friend struct MCPRuntimeDebuggerGatewayTestAccess;

public:
	explicit MCPRuntimeDebuggerGateway(MCPDebugCapture *p_debug_capture);

	Dictionary resolve_session(const Dictionary &p_arguments, bool p_require_generation, Session &r_session) const;
	bool resolve_current_session(int p_debugger_session, uint64_t p_runtime_generation, Session &r_session) const;
	Dictionary request(const Dictionary &p_arguments, const String &p_capture, const String &p_operation,
			const Dictionary &p_payload, bool p_require_generation = false);
	RoundTripResult round_trip(const Session &p_session, const String &p_mcp_session_id, const String &p_request_id_prefix,
			const String &p_message, const String &p_operation, const Array &p_arguments, int p_timeout_msec);
	Dictionary make_session_identity(int p_debugger_session, uint64_t p_runtime_generation) const;
	bool get_runtime_generation(int p_debugger_session, uint64_t &r_runtime_generation) const;
	bool is_runtime_current(int p_debugger_session, uint64_t p_runtime_generation) const;
	bool send_message(int p_debugger_session, const String &p_message, const Array &p_arguments) const;
	void broadcast_message(const String &p_message, const Array &p_arguments) const;
	bool handle_response(int p_debugger_session, const Array &p_data);
	void release_session_requests(const String &p_mcp_session_id);
	void clear_requests();
};
