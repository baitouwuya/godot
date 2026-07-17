/**************************************************************************/
/*  mcp_runtime_observation_response_store.cpp                            */
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
/* "Software"), to deal in the Software without restriction, including    */
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

#include "mcp_runtime_observation_response_store.h"

uint32_t MCPRuntimeObservationResponseStore::RequestKey::hash(const RequestKey &p_key) {
	return hash_murmur3_one_32(HashMapHasherDefault::hash(p_key.request_id), HashMapHasherDefault::hash(p_key.session));
}

bool MCPRuntimeObservationResponseStore::RequestKey::operator==(const RequestKey &p_other) const {
	return session == p_other.session && request_id == p_other.request_id;
}

bool MCPRuntimeObservationResponseStore::register_request(int p_session, const String &p_request_id, const String &p_operation) {
	if (p_session < 0 || p_request_id.is_empty() || p_operation.is_empty()) {
		return false;
	}
	RequestKey key;
	key.session = p_session;
	key.request_id = p_request_id;
	if (pending_requests.has(key)) {
		return false;
	}
	pending_requests.insert(key, p_operation);
	responses.erase(key);
	return true;
}

bool MCPRuntimeObservationResponseStore::handle_response(int p_session, const Array &p_data) {
	if (p_data.size() != 6 || p_data[0].get_type() != Variant::STRING || p_data[1].get_type() != Variant::STRING ||
			p_data[2].get_type() != Variant::BOOL || p_data[3].get_type() != Variant::STRING ||
			p_data[4].get_type() != Variant::STRING || p_data[5].get_type() != Variant::DICTIONARY) {
		return false;
	}
	RequestKey key;
	key.session = p_session;
	key.request_id = p_data[0];
	const String *expected_operation = pending_requests.getptr(key);
	if (!expected_operation || *expected_operation != String(p_data[1])) {
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

bool MCPRuntimeObservationResponseStore::take_response(int p_session, const String &p_request_id, Response &r_response) {
	RequestKey key;
	key.session = p_session;
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

void MCPRuntimeObservationResponseStore::cancel_request(int p_session, const String &p_request_id) {
	RequestKey key;
	key.session = p_session;
	key.request_id = p_request_id;
	responses.erase(key);
	pending_requests.erase(key);
}

void MCPRuntimeObservationResponseStore::clear() {
	responses.clear();
	pending_requests.clear();
}
