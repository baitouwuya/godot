/**************************************************************************/
/*  mcp_http_health_probe.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_http_health_probe.h"

#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/os/os.h"

MCPHTTPProjectLeaseHealthProbe::MCPHTTPProjectLeaseHealthProbe(uint64_t p_timeout_usec, int64_t p_max_response_bytes) {
	ERR_FAIL_COND(p_timeout_usec == 0);
	ERR_FAIL_COND(p_max_response_bytes <= 0);
	timeout_usec = p_timeout_usec;
	max_response_bytes = p_max_response_bytes;
}

bool MCPHTTPProjectLeaseHealthProbe::is_healthy(const MCPDiscoveryRecord &p_owner) const {
	if (!p_owner.is_valid(true) || p_owner.transport != "streamable-http") {
		return false;
	}

	String scheme;
	String host;
	String path;
	String fragment;
	int port = -1;
	if (p_owner.endpoint.parse_url(scheme, host, port, path, fragment) != OK || scheme != "http://" || port <= 0) {
		return false;
	}
	if (host != "127.0.0.1" && host != "localhost" && host != "::1") {
		return false;
	}

	Ref<HTTPClient> client = HTTPClient::create();
	if (client.is_null() || client->connect_to_host(host, port) != OK) {
		return false;
	}

	const uint64_t started_at = OS::get_singleton()->get_ticks_usec();
	while (client->get_status() == HTTPClient::STATUS_RESOLVING || client->get_status() == HTTPClient::STATUS_CONNECTING) {
		if (OS::get_singleton()->get_ticks_usec() - started_at >= timeout_usec || client->poll() != OK) {
			return false;
		}
		OS::get_singleton()->delay_usec(1000);
	}
	if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		return false;
	}

	Vector<String> headers;
	headers.push_back("Accept: application/json");
	headers.push_back("Connection: close");
	if (client->request(HTTPClient::METHOD_GET, "/health", headers, nullptr, 0) != OK) {
		return false;
	}

	PackedByteArray response_body;
	bool response_complete = false;
	while (OS::get_singleton()->get_ticks_usec() - started_at < timeout_usec) {
		const HTTPClient::Status status = client->get_status();
		if (status == HTTPClient::STATUS_REQUESTING) {
			if (client->poll() != OK) {
				return false;
			}
		} else if (status == HTTPClient::STATUS_BODY) {
			if (client->poll() != OK) {
				return false;
			}
			response_body.append_array(client->read_response_body_chunk());
			if (response_body.size() > max_response_bytes) {
				return false;
			}
		} else if (status == HTTPClient::STATUS_CONNECTED || status == HTTPClient::STATUS_DISCONNECTED) {
			response_complete = client->has_response();
			break;
		} else {
			return false;
		}
		OS::get_singleton()->delay_usec(1000);
	}
	if (!response_complete || client->get_response_code() != HTTPClient::RESPONSE_OK || response_body.is_empty()) {
		return false;
	}

	JSON json;
	if (json.parse(String::utf8((const char *)response_body.ptr(), response_body.size())) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary response = json.get_data();
	return bool(response.get("ok", false)) && response.get("projectId", String()) == p_owner.project_id && response.get("instanceId", String()) == p_owner.instance_id;
}
