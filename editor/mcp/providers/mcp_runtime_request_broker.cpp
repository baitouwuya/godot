/**************************************************************************/
/*  mcp_runtime_request_broker.cpp                                        */
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

#include "mcp_runtime_request_broker.h"

#include "core/os/os.h"
#include "core/templates/vector.h"
#include "editor/debugger/script_editor_debugger.h"

uint32_t MCPRuntimeRequestBroker::RequestKey::hash(const RequestKey &p_key) {
	return hash_murmur3_one_32(HashMapHasherDefault::hash(p_key.request_id), HashMapHasherDefault::hash(p_key.debugger_session));
}

bool MCPRuntimeRequestBroker::RequestKey::operator==(const RequestKey &p_other) const {
	return debugger_session == p_other.debugger_session && request_id == p_other.request_id;
}

String MCPRuntimeRequestBroker::create_request_id(const String &p_prefix) {
	return p_prefix + "-" + String::num_int64(OS::get_singleton()->get_process_id()) + "-" + String::num_uint64(next_request_id++);
}

bool MCPRuntimeRequestBroker::register_request(int p_debugger_session, const String &p_request_id, const String &p_operation,
		const String &p_mcp_session_id) {
	if (p_debugger_session < 0 || p_request_id.is_empty() || p_operation.is_empty() ||
			pending_requests.size() >= MAX_PENDING_REQUESTS) {
		return false;
	}
	RequestKey key;
	key.debugger_session = p_debugger_session;
	key.request_id = p_request_id;
	if (pending_requests.has(key)) {
		return false;
	}
	PendingRequest request;
	request.operation = p_operation;
	request.mcp_session_id = p_mcp_session_id;
	pending_requests.insert(key, request);
	responses.erase(key);
	return true;
}

bool MCPRuntimeRequestBroker::handle_response(int p_debugger_session, const Array &p_data) {
	if (p_data.size() != 6 || p_data[0].get_type() != Variant::STRING || p_data[1].get_type() != Variant::STRING ||
			p_data[2].get_type() != Variant::BOOL || p_data[3].get_type() != Variant::STRING ||
			p_data[4].get_type() != Variant::STRING || p_data[5].get_type() != Variant::DICTIONARY) {
		return false;
	}
	RequestKey key;
	key.debugger_session = p_debugger_session;
	key.request_id = p_data[0];
	const PendingRequest *request = pending_requests.getptr(key);
	if (!request || request->operation != String(p_data[1]) || responses.has(key)) {
		return false;
	}
	Response response;
	response.operation = p_data[1];
	response.ok = p_data[2];
	response.code = p_data[3];
	response.message = p_data[4];
	response.data = p_data[5];
	responses.insert(key, response);
	return true;
}

bool MCPRuntimeRequestBroker::take_response(int p_debugger_session, const String &p_request_id, Response &r_response) {
	RequestKey key;
	key.debugger_session = p_debugger_session;
	key.request_id = p_request_id;
	const Response *response = responses.getptr(key);
	if (!response) {
		return false;
	}
	r_response = *response;
	responses.erase(key);
	pending_requests.erase(key);
	return true;
}

MCPRuntimeRequestBroker::WaitStatus MCPRuntimeRequestBroker::wait_for_response(ScriptEditorDebugger *p_debugger,
		int p_debugger_session, const String &p_request_id, int p_timeout_msec, Response &r_response) {
	if (!p_debugger || !p_debugger->is_session_active()) {
		cancel_request(p_debugger_session, p_request_id);
		return WAIT_DISCONNECTED;
	}
	const uint64_t deadline = MCPRuntimeDebuggerWait::deadline_from_timeout_msec(p_timeout_msec);
	const WaitStatus status = MCPRuntimeDebuggerWait::wait_until(p_debugger, deadline, [&]() {
		return take_response(p_debugger_session, p_request_id, r_response);
	});
	if (status != WAIT_COMPLETED) {
		cancel_request(p_debugger_session, p_request_id);
	}
	return status;
}

void MCPRuntimeRequestBroker::cancel_request(int p_debugger_session, const String &p_request_id) {
	RequestKey key;
	key.debugger_session = p_debugger_session;
	key.request_id = p_request_id;
	responses.erase(key);
	pending_requests.erase(key);
}

void MCPRuntimeRequestBroker::release_session(const String &p_mcp_session_id) {
	if (p_mcp_session_id.is_empty()) {
		return;
	}
	Vector<RequestKey> requests_to_remove;
	for (const KeyValue<RequestKey, PendingRequest> &entry : pending_requests) {
		if (entry.value.mcp_session_id == p_mcp_session_id) {
			requests_to_remove.push_back(entry.key);
		}
	}
	for (const RequestKey &key : requests_to_remove) {
		responses.erase(key);
		pending_requests.erase(key);
	}
}

void MCPRuntimeRequestBroker::clear() {
	responses.clear();
	pending_requests.clear();
}

bool MCPRuntimeRequestBroker::has_request(int p_debugger_session, const String &p_request_id) const {
	RequestKey key;
	key.debugger_session = p_debugger_session;
	key.request_id = p_request_id;
	return pending_requests.has(key);
}
