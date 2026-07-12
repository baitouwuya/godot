/**************************************************************************/
/*  mcp_http_server.cpp                                                   */
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

#include "mcp_http_server.h"

#include "core/io/json.h"
#include "core/os/os.h"

bool MCPHTTPServer::_constant_time_equals(const String &p_trusted, const String &p_received) {
	const CharString trusted = p_trusted.utf8();
	const CharString received = p_received.utf8();
	if (trusted.length() != received.length()) {
		return false;
	}
	uint8_t difference = 0;
	for (int i = 0; i < trusted.length(); i++) {
		difference |= (uint8_t)trusted[i] ^ (uint8_t)received[i];
	}
	return difference == 0;
}

String MCPHTTPServer::_reason_phrase(int p_status) {
	switch (p_status) {
		case 200:
			return "OK";
		case 202:
			return "Accepted";
		case 204:
			return "No Content";
		case 400:
			return "Bad Request";
		case 401:
			return "Unauthorized";
		case 403:
			return "Forbidden";
		case 404:
			return "Not Found";
		case 405:
			return "Method Not Allowed";
		case 406:
			return "Not Acceptable";
		case 408:
			return "Request Timeout";
		case 413:
			return "Payload Too Large";
		case 415:
			return "Unsupported Media Type";
		case 429:
			return "Too Many Requests";
		case 431:
			return "Request Header Fields Too Large";
		case 500:
			return "Internal Server Error";
		case 503:
			return "Service Unavailable";
		default:
			return "Error";
	}
}

bool MCPHTTPServer::_is_origin_allowed(const String &p_origin) const {
	if (p_origin.is_empty()) {
		return true;
	}

	const String port = itos(get_port());
	if (p_origin == "http://127.0.0.1:" + port || p_origin == "http://localhost:" + port || p_origin == "http://[::1]:" + port) {
		return true;
	}
	for (const String &allowed_origin : config.allowed_origins) {
		if (p_origin == allowed_origin) {
			return true;
		}
	}
	return false;
}

bool MCPHTTPServer::_is_authorized(const MCPHTTPParser::Request &p_request) const {
	if (p_request.path == "/health" || config.bearer_token.is_empty()) {
		return true;
	}
	const String authorization = p_request.headers.get("authorization", "");
	return _constant_time_equals("Bearer " + config.bearer_token, authorization);
}

void MCPHTTPServer::_accept_connections() {
	while (server->is_connection_available()) {
		Ref<StreamPeerTCP> peer = server->take_connection();
		if (peer.is_null()) {
			return;
		}
		if (connections.size() >= config.max_connections) {
			peer->disconnect_from_host();
			continue;
		}

		peer->set_no_delay(true);
		Connection connection;
		connection.peer = peer;
		connection.last_activity_usec = OS::get_singleton()->get_ticks_usec();
		connections.push_back(connection);
	}
}

void MCPHTTPServer::_queue_response(Connection &r_connection, const MCPHTTPResponse &p_response) {
	String response = "HTTP/1.1 " + itos(p_response.status) + " " + _reason_phrase(p_response.status) + "\r\n";
	bool has_content_type = false;
	const Array header_keys = p_response.headers.keys();
	for (const Variant &key_variant : header_keys) {
		const String key = key_variant;
		const String value = p_response.headers[key_variant];
		if (key.to_lower() == "content-type") {
			has_content_type = true;
		}
		response += key + ": " + value + "\r\n";
	}
	if (!has_content_type && !p_response.body.is_empty()) {
		response += "Content-Type: application/json; charset=utf-8\r\n";
	}
	const Vector<uint8_t> body = p_response.body.to_utf8_buffer();
	response += "Content-Length: " + itos(body.size()) + "\r\n";
	response += "Connection: close\r\n";
	response += "Cache-Control: no-store\r\n\r\n";
	r_connection.output = response.to_utf8_buffer();
	r_connection.output.append_array(body);
	r_connection.output_offset = 0;
	r_connection.close_after_write = true;
}

void MCPHTTPServer::_queue_error(Connection &r_connection, int p_status, const String &p_message) {
	Dictionary error;
	error["error"] = p_message;
	MCPHTTPResponse response;
	response.status = p_status;
	response.body = JSON::stringify(error);
	_queue_response(r_connection, response);
}

bool MCPHTTPServer::_poll_connection(Connection &r_connection, uint64_t p_now_usec) {
	if (r_connection.peer->poll() != OK) {
		return false;
	}
	const StreamPeerTCP::Status status = r_connection.peer->get_status();
	if (status == StreamPeerTCP::STATUS_NONE || status == StreamPeerTCP::STATUS_ERROR) {
		return false;
	}

	if (p_now_usec - r_connection.last_activity_usec > config.idle_timeout_usec) {
		if (r_connection.output.is_empty()) {
			_queue_error(r_connection, 408, "HTTP connection timed out.");
		} else {
			return false;
		}
	}

	if (!r_connection.output.is_empty()) {
		int sent = 0;
		const Error error = r_connection.peer->put_partial_data(r_connection.output.ptr() + r_connection.output_offset, r_connection.output.size() - r_connection.output_offset, sent);
		if (error != OK) {
			return false;
		}
		if (sent > 0) {
			r_connection.output_offset += sent;
			r_connection.last_activity_usec = p_now_usec;
		}
		return !(r_connection.close_after_write && r_connection.output_offset >= r_connection.output.size());
	}

	const int available = r_connection.peer->get_available_bytes();
	if (available < 0) {
		return false;
	}
	if (available > 0) {
		const int64_t max_request_bytes = config.max_header_bytes + config.max_body_bytes;
		const int64_t remaining = max_request_bytes - r_connection.input.size();
		if (remaining <= 0 || available > remaining) {
			_queue_error(r_connection, 413, "HTTP request exceeds the configured limit.");
			return true;
		}

		const int old_size = r_connection.input.size();
		r_connection.input.resize(old_size + available);
		int received = 0;
		const Error error = r_connection.peer->get_partial_data(r_connection.input.ptrw() + old_size, available, received);
		if (error != OK) {
			return false;
		}
		if (received < available) {
			r_connection.input.resize(old_size + received);
		}
		if (received > 0) {
			r_connection.last_activity_usec = p_now_usec;
		}
	}

	MCPHTTPParser::Request request;
	String parse_error;
	const MCPHTTPParser::ParseResult parse_result = MCPHTTPParser::parse(r_connection.input, config.max_header_bytes, config.max_body_bytes, request, parse_error);
	switch (parse_result) {
		case MCPHTTPParser::PARSE_INCOMPLETE:
			return true;
		case MCPHTTPParser::PARSE_HEADER_TOO_LARGE:
			_queue_error(r_connection, 431, "HTTP request headers are too large.");
			return true;
		case MCPHTTPParser::PARSE_PAYLOAD_TOO_LARGE:
			_queue_error(r_connection, 413, "HTTP request body is too large.");
			return true;
		case MCPHTTPParser::PARSE_BAD_REQUEST:
			_queue_error(r_connection, 400, parse_error);
			return true;
		case MCPHTTPParser::PARSE_READY:
			break;
	}

	if (request.version == "HTTP/1.1" && !request.headers.has("host")) {
		_queue_error(r_connection, 400, "HTTP/1.1 requests require a Host header.");
		return true;
	}
	if (!_is_origin_allowed(request.headers.get("origin", ""))) {
		_queue_error(r_connection, 403, "HTTP Origin is not allowed.");
		return true;
	}
	if (!_is_authorized(request)) {
		MCPHTTPResponse response;
		response.status = 401;
		response.headers["WWW-Authenticate"] = "Bearer";
		Dictionary error;
		error["error"] = "Missing or invalid bearer token.";
		response.body = JSON::stringify(error);
		_queue_response(r_connection, response);
		return true;
	}

	_queue_response(r_connection, handler->handle_request(request));
	return true;
}

Error MCPHTTPServer::start(const Config &p_config, MCPHTTPRequestHandler *p_handler) {
	ERR_FAIL_COND_V(running, ERR_ALREADY_IN_USE);
	ERR_FAIL_NULL_V(p_handler, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_config.max_connections <= 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_config.max_header_bytes <= 0 || p_config.max_body_bytes < 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_config.idle_timeout_usec == 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_config.bind_address != IPAddress("127.0.0.1") && p_config.bind_address != IPAddress("::1"), ERR_INVALID_PARAMETER);

	config = p_config;
	handler = p_handler;
	const Error error = server->listen(config.port, config.bind_address);
	if (error != OK) {
		handler = nullptr;
		return error;
	}
	running = true;
	return OK;
}

void MCPHTTPServer::poll() {
	if (!running) {
		return;
	}

	_accept_connections();
	const uint64_t now_usec = OS::get_singleton()->get_ticks_usec();
	for (int i = connections.size() - 1; i >= 0; i--) {
		if (!_poll_connection(connections.write[i], now_usec)) {
			connections.write[i].peer->disconnect_from_host();
			connections.remove_at(i);
		}
	}
}

void MCPHTTPServer::stop() {
	for (Connection &connection : connections) {
		connection.peer->disconnect_from_host();
	}
	connections.clear();
	if (server.is_valid()) {
		server->stop();
	}
	handler = nullptr;
	running = false;
}

int MCPHTTPServer::get_port() const {
	return running ? server->get_local_port() : 0;
}

MCPHTTPServer::MCPHTTPServer() {
	server.instantiate();
}

MCPHTTPServer::~MCPHTTPServer() {
	stop();
}
