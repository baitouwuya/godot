/**************************************************************************/
/*  mcp_http_server.h                                                     */
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

#pragma once

#include "mcp_http_parser.h"

#include "core/io/tcp_server.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

struct MCPHTTPResponse {
	int status = 200;
	Dictionary headers;
	String body;
};

class MCPHTTPRequestHandler {
public:
	virtual MCPHTTPResponse handle_request(const MCPHTTPParser::Request &p_request) = 0;
	virtual ~MCPHTTPRequestHandler() = default;
};

class MCPHTTPServer {
public:
	struct Config {
		IPAddress bind_address = IPAddress("127.0.0.1");
		uint16_t port = 0;
		String bearer_token;
		Vector<String> allowed_origins;
		int max_connections = 16;
		int64_t max_header_bytes = 16 * 1024;
		int64_t max_body_bytes = 4 * 1024 * 1024;
		uint64_t idle_timeout_usec = 30 * 1000 * 1000;
	};

private:
	struct Connection {
		Ref<StreamPeerTCP> peer;
		Vector<uint8_t> input;
		Vector<uint8_t> output;
		int output_offset = 0;
		uint64_t last_activity_usec = 0;
		bool close_after_write = false;
	};

	Ref<TCPServer> server;
	Vector<Connection> connections;
	Config config;
	MCPHTTPRequestHandler *handler = nullptr;
	bool running = false;

	static bool _constant_time_equals(const String &p_trusted, const String &p_received);
	static String _reason_phrase(int p_status);
	bool _is_origin_allowed(const String &p_origin) const;
	bool _is_authorized(const MCPHTTPParser::Request &p_request) const;
	void _accept_connections();
	void _queue_response(Connection &r_connection, const MCPHTTPResponse &p_response);
	void _queue_error(Connection &r_connection, int p_status, const String &p_message);
	bool _poll_connection(Connection &r_connection, uint64_t p_now_usec);

public:
	Error start(const Config &p_config, MCPHTTPRequestHandler *p_handler);
	void poll();
	void stop();

	bool is_running() const { return running; }
	int get_port() const;
	int get_connection_count() const { return connections.size(); }

	MCPHTTPServer();
	~MCPHTTPServer();
};
