/**************************************************************************/
/*  test_mcp_host.cpp                                                     */
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

#include "editor/mcp/mcp_host.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_host);

namespace TestMCPHost {

MCPHTTPParser::Request request(
		const String &p_method,
		const String &p_body = String(),
		const String &p_session_id = String(),
		const String &p_protocol_version = String()) {
	MCPHTTPParser::Request result;
	result.method = p_method;
	result.path = "/mcp";
	result.body = p_body;
	result.has_content_length = p_method == "POST";
	if (p_method == "POST") {
		result.headers["content-type"] = "application/json";
		result.headers["accept"] = "application/json, text/event-stream";
	}
	if (!p_session_id.is_empty()) {
		result.headers["mcp-session-id"] = p_session_id;
	}
	if (!p_protocol_version.is_empty()) {
		result.headers["mcp-protocol-version"] = p_protocol_version;
	}
	return result;
}

MCPHost::Config make_config() {
	MCPHost::Config config;
	config.bearer_token = "test-secret";
	config.project_identity.canonical_path = "C:/project";
	config.project_identity.project_id = MCPProjectIdentity::make_project_id(config.project_identity.canonical_path);
	config.project_identity.instance_id = String("b").repeat(32);
	return config;
}

String response_session_id(const MCPHTTPResponse &p_response) {
	return p_response.headers.get("Mcp-Session-Id", "");
}

TEST_CASE("[MCP] Host allocates and deletes initialized sessions") {
	MCPToolRegistry registry;
	MCPHost host;
	const MCPHost::Config config = make_config();
	REQUIRE(host.start(config, &registry) == OK);

	const String initialize = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\",\"version\":\"1\"}}}";
	MCPHTTPResponse response = host.handle_request(request("POST", initialize));
	const String session_id = response_session_id(response);
	CHECK(response.status == 200);
	CHECK(!session_id.is_empty());
	CHECK(host.get_session_count() == 1);

	response = host.handle_request(request("POST", "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", session_id));
	CHECK(response.status == 202);
	CHECK(response_session_id(response) == session_id);

	response = host.handle_request(request("DELETE", String(), session_id));
	CHECK(response.status == 204);
	CHECK(host.get_session_count() == 0);
	host.stop();
}

TEST_CASE("[MCP] Host rejects unknown sessions and does not retain invalid initialization") {
	MCPToolRegistry registry;
	MCPHost host;
	REQUIRE(host.start(make_config(), &registry) == OK);

	MCPHTTPResponse response = host.handle_request(request("POST", "{}"));
	CHECK(response.status == 200);
	CHECK(response_session_id(response).is_empty());
	CHECK(host.get_session_count() == 0);

	response = host.handle_request(request("POST", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}", "missing"));
	CHECK(response.status == 404);
	host.stop();
}

TEST_CASE("[MCP] Host validates protocol version headers against initialized sessions") {
	MCPToolRegistry registry;
	MCPHost host;
	REQUIRE(host.start(make_config(), &registry) == OK);

	MCPHTTPResponse response = host.handle_request(request(
			"POST",
			"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}",
			String(),
			"2099-01-01"));
	CHECK(response.status == 400);
	CHECK(host.get_session_count() == 0);

	const String initialize =
			R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{)"
			R"("protocolVersion":"2025-06-18","capabilities":{},)"
			R"("clientInfo":{"name":"test","version":"1"}}})";
	response = host.handle_request(request("POST", initialize));
	const String session_id = response_session_id(response);
	REQUIRE(response.status == 200);
	REQUIRE_FALSE(session_id.is_empty());

	response = host.handle_request(request(
			"POST",
			"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",
			session_id,
			MCPProtocol::PROTOCOL_VERSION_LATEST));
	CHECK(response.status == 400);

	response = host.handle_request(request(
			"POST",
			"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",
			session_id,
			MCPProtocol::PROTOCOL_VERSION_2025_06_18));
	CHECK(response.status == 202);
	host.stop();
}

TEST_CASE("[MCP] Host requires both Streamable HTTP response media types") {
	MCPToolRegistry registry;
	MCPHost host;
	REQUIRE(host.start(make_config(), &registry) == OK);

	MCPHTTPParser::Request post_request = request(
			"POST",
			"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");
	post_request.headers.erase("accept");
	CHECK(host.handle_request(post_request).status == 406);

	post_request.headers["accept"] = "application/json";
	CHECK(host.handle_request(post_request).status == 406);

	post_request.headers["accept"] = "text/event-stream; q=0.5, application/json; charset=utf-8";
	CHECK(host.handle_request(post_request).status == 200);
	host.stop();
}

TEST_CASE("[MCP] Host reports project identity and disables SSE GET") {
	MCPToolRegistry registry;
	MCPHost host;
	const MCPHost::Config config = make_config();
	REQUIRE(host.start(config, &registry) == OK);

	MCPHTTPParser::Request health_request;
	health_request.method = "GET";
	health_request.path = "/health";
	MCPHTTPResponse response = host.handle_request(health_request);
	CHECK(response.status == 200);
	CHECK(response.body.contains(config.project_identity.project_id));
	CHECK(response.body.contains(String("b").repeat(32)));

	MCPHTTPParser::Request get_request;
	get_request.method = "GET";
	get_request.path = "/mcp";
	response = host.handle_request(get_request);
	CHECK(response.status == 405);
	CHECK(response.headers.get("Allow", "") == "POST, DELETE");
	host.stop();
}

} // namespace TestMCPHost
