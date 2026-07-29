/**************************************************************************/
/*  mcp_cli_http_transport.cpp                                            */
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

#include "mcp_cli_http_transport.h"

#include "core/io/http_client.h"
#include "core/io/stream_peer_tls.h"
#include "core/os/os.h"

namespace {

constexpr uint32_t HTTP_POLL_DELAY_USEC = 1000;

bool has_timed_out(uint64_t p_started_at_usec, uint64_t p_timeout_ms) {
	return OS::get_singleton()->get_ticks_usec() - p_started_at_usec >= p_timeout_ms * 1000;
}

bool is_http_failure_status(HTTPClient::Status p_status) {
	return p_status == HTTPClient::STATUS_CANT_RESOLVE ||
			p_status == HTTPClient::STATUS_CANT_CONNECT ||
			p_status == HTTPClient::STATUS_CONNECTION_ERROR ||
			p_status == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR;
}

} // namespace

Error MCPHTTPCLITransport::parse_endpoint(
		const String &p_endpoint,
		String &r_scheme,
		String &r_host,
		int &r_port,
		String &r_path,
		String &r_fragment,
		String &r_error) {
	if (p_endpoint.parse_url(r_scheme, r_host, r_port, r_path, r_fragment) != OK ||
			(r_scheme != "http://" && r_scheme != "https://")) {
		r_error = "MCP endpoint must be an HTTP or HTTPS URL.";
		return ERR_INVALID_DATA;
	}
	if (r_port == 0) {
		r_port = r_scheme == "https://" ? 443 : 80;
	}
	if (r_path.is_empty()) {
		r_path = "/";
	}
	return OK;
}

bool MCPHTTPCLITransport::is_visible_ascii_header_value(const String &p_value) {
	if (p_value.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_value.length(); i++) {
		if (p_value[i] < 0x21 || p_value[i] > 0x7e) {
			return false;
		}
	}
	return true;
}

Error MCPHTTPCLITransport::request(
		RequestMethod p_method,
		const String &p_endpoint,
		const Vector<String> &p_headers,
		const String &p_body,
		uint64_t p_timeout_ms,
		uint64_t p_max_response_bytes,
		MCPCLIHTTPResponse &r_response,
		String &r_error) {
	r_response = MCPCLIHTTPResponse();
	r_error = String();
	if (p_timeout_ms == 0 || p_max_response_bytes == 0) {
		r_error = "MCP HTTP timeout and response limit must be positive.";
		return ERR_INVALID_PARAMETER;
	}

	String scheme;
	String host;
	String path;
	String fragment;
	int port = 0;
	Error error = parse_endpoint(p_endpoint, scheme, host, port, path, fragment, r_error);
	if (error != OK) {
		return error;
	}
	if (!fragment.is_empty()) {
		r_error = "MCP HTTP endpoint must not contain a fragment.";
		return ERR_INVALID_DATA;
	}

	Ref<HTTPClient> client = HTTPClient::create();
	if (client.is_null()) {
		r_error = "Cannot create an HTTP client for the MCP stdio bridge.";
		return ERR_CANT_CREATE;
	}
	client->set_blocking_mode(false);
	Ref<TLSOptions> tls_options;
	if (scheme == "https://") {
		tls_options = TLSOptions::client();
	}
	error = client->connect_to_host(host, port, tls_options);
	if (error != OK) {
		r_error = vformat("Cannot begin connection to MCP endpoint '%s'.", p_endpoint);
		return error;
	}

	const uint64_t started_at_usec = OS::get_singleton()->get_ticks_usec();
	while (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		if (is_http_failure_status(client->get_status())) {
			r_error = vformat("Cannot connect to MCP endpoint '%s'.", p_endpoint);
			client->close();
			return ERR_CANT_CONNECT;
		}
		if (has_timed_out(started_at_usec, p_timeout_ms)) {
			r_error = vformat("Timed out connecting to MCP endpoint '%s'.", p_endpoint);
			client->close();
			return ERR_TIMEOUT;
		}
		error = client->poll();
		if (error != OK) {
			r_error = vformat("HTTP polling failed while connecting to MCP endpoint '%s'.", p_endpoint);
			client->close();
			return error;
		}
		OS::get_singleton()->delay_usec(HTTP_POLL_DELAY_USEC);
	}

	Vector<String> request_headers = p_headers;
	const String host_header = host.contains(":") ? "[" + host + "]" : host;
	request_headers.push_back("Host: " + host_header + ":" + itos(port));
	const CharString request_body = p_body.utf8();
	HTTPClient::Method http_method = HTTPClient::METHOD_POST;
	switch (p_method) {
		case REQUEST_POST:
			http_method = HTTPClient::METHOD_POST;
			break;
		case REQUEST_DELETE:
			http_method = HTTPClient::METHOD_DELETE;
			break;
		default:
			r_error = "Unsupported MCP HTTP request method.";
			client->close();
			return ERR_INVALID_PARAMETER;
	}
	error = client->request(
			http_method,
			path,
			request_headers,
			request_body.length() > 0 ? (const uint8_t *)request_body.get_data() : nullptr,
			request_body.length());
	if (error != OK) {
		r_error = "Cannot send MCP HTTP request.";
		client->close();
		return error;
	}

	while (client->get_status() == HTTPClient::STATUS_REQUESTING) {
		if (has_timed_out(started_at_usec, p_timeout_ms)) {
			r_error = "Timed out waiting for MCP HTTP response headers.";
			client->close();
			return ERR_TIMEOUT;
		}
		error = client->poll();
		if (error != OK || is_http_failure_status(client->get_status())) {
			r_error = "MCP HTTP request failed before receiving response headers.";
			client->close();
			return error == OK ? ERR_CONNECTION_ERROR : error;
		}
		OS::get_singleton()->delay_usec(HTTP_POLL_DELAY_USEC);
	}

	if (!client->has_response()) {
		r_error = "MCP HTTP endpoint closed without a response.";
		client->close();
		return ERR_CONNECTION_ERROR;
	}
	r_response.status_code = client->get_response_code();
	const int64_t response_length = client->get_response_body_length();
	if (response_length > (int64_t)p_max_response_bytes) {
		r_error = "MCP HTTP response exceeds the configured size limit.";
		client->close();
		return ERR_OUT_OF_MEMORY;
	}

	List<String> response_headers;
	if (client->get_response_headers(&response_headers) == OK) {
		for (const String &header : response_headers) {
			const int separator = header.find_char(':');
			if (separator > 0) {
				r_response.headers[header.substr(0, separator).strip_edges().to_lower()] =
						header.substr(separator + 1).strip_edges();
			}
		}
	}

	while (client->get_status() == HTTPClient::STATUS_BODY) {
		const PackedByteArray chunk = client->read_response_body_chunk();
		if (!chunk.is_empty()) {
			if ((uint64_t)r_response.body.size() + (uint64_t)chunk.size() > p_max_response_bytes) {
				r_error = "MCP HTTP response exceeds the configured size limit.";
				client->close();
				return ERR_OUT_OF_MEMORY;
			}
			const int old_size = r_response.body.size();
			r_response.body.resize(old_size + chunk.size());
			memcpy(r_response.body.ptrw() + old_size, chunk.ptr(), chunk.size());
			continue;
		}
		if (client->get_status() != HTTPClient::STATUS_BODY) {
			break;
		}

		if (has_timed_out(started_at_usec, p_timeout_ms)) {
			r_error = "Timed out reading MCP HTTP response body.";
			client->close();
			return ERR_TIMEOUT;
		}
		error = client->poll();
		if (error != OK) {
			r_error = "MCP HTTP response body polling failed.";
			client->close();
			return error;
		}
		OS::get_singleton()->delay_usec(HTTP_POLL_DELAY_USEC);
	}
	const HTTPClient::Status final_status = client->get_status();
	client->close();

	if (is_http_failure_status(final_status) || final_status == HTTPClient::STATUS_DISCONNECTED) {
		r_error = "MCP HTTP response ended with a connection error.";
		return ERR_CONNECTION_ERROR;
	}
	if (response_length >= 0 && r_response.body.size() != response_length) {
		r_error = "MCP HTTP response body ended before Content-Length bytes were received.";
		return ERR_CONNECTION_ERROR;
	}
	return OK;
}
