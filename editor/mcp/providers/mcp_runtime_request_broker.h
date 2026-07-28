/**************************************************************************/
/*  mcp_runtime_request_broker.h                                          */
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

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class ScriptEditorDebugger;

// Correlates short debugger round trips and exposes non-blocking response access.
class MCPRuntimeRequestBroker {
public:
	enum WaitStatus {
		WAIT_COMPLETED,
		WAIT_TIMEOUT,
		WAIT_DISCONNECTED,
	};

	struct Response {
		String operation;
		bool ok = false;
		String code;
		String message;
		Dictionary data;
	};

	static constexpr int MAX_PENDING_REQUESTS = 1024;

private:
	struct RequestKey {
		int debugger_session = -1;
		String request_id;

		static uint32_t hash(const RequestKey &p_key);
		bool operator==(const RequestKey &p_other) const;
	};

	struct PendingRequest {
		String operation;
		String mcp_session_id;
	};

	HashMap<RequestKey, PendingRequest, RequestKey> pending_requests;
	HashMap<RequestKey, Response, RequestKey> responses;
	uint64_t next_request_id = 1;

public:
	String create_request_id(const String &p_prefix);
	bool register_request(int p_debugger_session, const String &p_request_id, const String &p_operation,
			const String &p_mcp_session_id = String());
	bool handle_response(int p_debugger_session, const Array &p_data);
	bool take_response(int p_debugger_session, const String &p_request_id, Response &r_response);
	WaitStatus wait_for_response(ScriptEditorDebugger *p_debugger, int p_debugger_session, const String &p_request_id,
			int p_timeout_msec, Response &r_response);
	void cancel_request(int p_debugger_session, const String &p_request_id);
	void release_session(const String &p_mcp_session_id);
	void clear();

	bool has_request(int p_debugger_session, const String &p_request_id) const;
	int get_request_count() const { return pending_requests.size(); }
};
