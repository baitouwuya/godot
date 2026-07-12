/**************************************************************************/
/*  test_mcp_http_server.cpp                                              */
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

#include "core/io/stream_peer_tcp.h"
#include "core/os/os.h"
#include "editor/mcp/mcp_http_server.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_http_server)

namespace TestMCPHTTPServer {

constexpr uint64_t MAX_WAIT_USEC = 2 * 1000 * 1000;

class TestHandler : public MCPHTTPRequestHandler {
public:
	int request_count = 0;

	MCPHTTPResponse handle_request(const MCPHTTPParser::Request &p_request) override {
		request_count++;
		MCPHTTPResponse response;
		response.body = "{\"path\":\"" + p_request.path + "\"}";
		return response;
	}
};

String exchange(MCPHTTPServer &p_server, const String &p_request) {
	Ref<StreamPeerTCP> client;
	client.instantiate();
	REQUIRE(client->connect_to_host(IPAddress("127.0.0.1"), p_server.get_port()) == OK);

	const uint64_t connect_started = OS::get_singleton()->get_ticks_usec();
	while (client->get_status() == StreamPeerTCP::STATUS_CONNECTING && OS::get_singleton()->get_ticks_usec() - connect_started < MAX_WAIT_USEC) {
		p_server.poll();
		client->poll();
		OS::get_singleton()->delay_usec(1000);
	}
	REQUIRE(client->get_status() == StreamPeerTCP::STATUS_CONNECTED);

	const Vector<uint8_t> request_bytes = p_request.to_utf8_buffer();
	REQUIRE(client->put_data(request_bytes.ptr(), request_bytes.size()) == OK);

	String response;
	const uint64_t response_started = OS::get_singleton()->get_ticks_usec();
	while (OS::get_singleton()->get_ticks_usec() - response_started < MAX_WAIT_USEC) {
		p_server.poll();
		client->poll();
		const int available = client->get_available_bytes();
		if (available > 0) {
			Vector<uint8_t> chunk;
			chunk.resize(available);
			int received = 0;
			REQUIRE(client->get_partial_data(chunk.ptrw(), available, received) == OK);
			response += String::utf8((const char *)chunk.ptr(), received);
		}
		if (client->get_status() != StreamPeerTCP::STATUS_CONNECTED && available <= 0) {
			break;
		}
		OS::get_singleton()->delay_usec(1000);
	}
	client->disconnect_from_host();
	return response;
}

TEST_CASE("[MCP] HTTP server requires loopback binding") {
	MCPHTTPServer server;
	TestHandler handler;
	MCPHTTPServer::Config config;
	config.bind_address = IPAddress("0.0.0.0");
	ERR_PRINT_OFF;
	CHECK(server.start(config, &handler) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
}

TEST_CASE("[MCP] HTTP server validates bearer authentication and Origin") {
	MCPHTTPServer server;
	TestHandler handler;
	MCPHTTPServer::Config config;
	config.bearer_token = "test-secret";
	REQUIRE(server.start(config, &handler) == OK);

	String response = exchange(server, "POST /mcp HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n");
	CHECK(response.contains(" 401 Unauthorized\r\n"));
	CHECK(handler.request_count == 0);

	response = exchange(server, "POST /mcp HTTP/1.1\r\nHost: localhost\r\nOrigin: https://example.com\r\nAuthorization: Bearer test-secret\r\nContent-Length: 0\r\n\r\n");
	CHECK(response.contains(" 403 Forbidden\r\n"));
	CHECK(handler.request_count == 0);

	response = exchange(server, "POST /mcp HTTP/1.1\r\nHost: localhost\r\nOrigin: http://localhost:" + itos(server.get_port()) + "\r\nAuthorization: Bearer test-secret\r\nContent-Length: 0\r\n\r\n");
	CHECK(response.contains(" 200 OK\r\n"));
	CHECK(response.contains("{\"path\":\"/mcp\"}"));
	CHECK(handler.request_count == 1);

	server.stop();
}

TEST_CASE("[MCP] HTTP health endpoint is available without the bearer token") {
	MCPHTTPServer server;
	TestHandler handler;
	MCPHTTPServer::Config config;
	config.bearer_token = "test-secret";
	REQUIRE(server.start(config, &handler) == OK);

	const String response = exchange(server, "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n");
	CHECK(response.contains(" 200 OK\r\n"));
	CHECK(handler.request_count == 1);

	server.stop();
}

} // namespace TestMCPHTTPServer
