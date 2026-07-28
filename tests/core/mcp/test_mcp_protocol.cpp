/**************************************************************************/
/*  test_mcp_protocol.cpp                                                 */
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

#include "core/mcp/mcp_protocol.h"
#include "core/object/callable_mp.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_protocol);

namespace TestMCPProtocol {

class ToolProvider : public Object {
public:
	int call_count = 0;

	Dictionary echo(const Dictionary &p_arguments, const Dictionary &p_context) {
		call_count++;
		Dictionary structured_content;
		structured_content["arguments"] = p_arguments;
		structured_content["context"] = p_context;

		Dictionary result;
		Array content;
		Dictionary text;
		text["type"] = "text";
		text["text"] = "echo";
		content.push_back(text);
		result["content"] = content;
		result["structuredContent"] = structured_content;

		Dictionary meta;
		meta["provider"] = "test";
		result["_meta"] = meta;
		return result;
	}
};

static Dictionary make_request(const String &p_method, const Variant &p_id, const Dictionary &p_params = Dictionary()) {
	Dictionary request;
	request["jsonrpc"] = "2.0";
	request["id"] = p_id;
	request["method"] = p_method;
	if (!p_params.is_empty()) {
		request["params"] = p_params;
	}
	return request;
}

static Dictionary make_initialize_request(const String &p_version, const Variant &p_id = 1) {
	Dictionary params;
	params["protocolVersion"] = p_version;
	params["capabilities"] = Dictionary();
	Dictionary client_info;
	client_info["name"] = "test-client";
	client_info["version"] = "1.0";
	params["clientInfo"] = client_info;
	return make_request("initialize", p_id, params);
}

static void finish_initialization(MCPProtocol &r_protocol, const String &p_version) {
	const MCPProtocol::DispatchResult initialize_result = r_protocol.dispatch(make_initialize_request(p_version));
	REQUIRE(initialize_result.has_response());
	Dictionary notification;
	notification["jsonrpc"] = "2.0";
	notification["method"] = "notifications/initialized";
	CHECK_FALSE(r_protocol.dispatch(notification).has_response());
	REQUIRE_EQ(r_protocol.get_session_state(), MCPProtocol::SESSION_READY);
}

static int get_error_code(const MCPProtocol::DispatchResult &p_result) {
	CHECK(p_result.has_response());
	if (!p_result.has_response()) {
		return 0;
	}
	const Dictionary response = p_result.response;
	const Dictionary error = response.get("error", Dictionary());
	return int(error.get("code", 0));
}

TEST_CASE("[MCP][Protocol] Negotiates supported protocol versions") {
	const char *versions[] = {
		MCPProtocol::PROTOCOL_VERSION_LATEST,
		MCPProtocol::PROTOCOL_VERSION_2025_06_18,
		MCPProtocol::PROTOCOL_VERSION_2025_03_26,
	};
	for (const char *version : versions) {
		MCPProtocol protocol;
		const MCPProtocol::DispatchResult dispatch_result = protocol.dispatch(make_initialize_request(version));
		REQUIRE(dispatch_result.has_response());
		const Dictionary response = dispatch_result.response;
		const Dictionary result = response.get("result", Dictionary());
		CHECK_EQ(String(result.get("protocolVersion", String())), version);
		CHECK_EQ(protocol.get_session_state(), MCPProtocol::SESSION_INITIALIZING);
	}

	MCPProtocol fallback_protocol;
	const Dictionary response = fallback_protocol.dispatch(make_initialize_request("2099-01-01")).response;
	const Dictionary result = response.get("result", Dictionary());
	CHECK_EQ(String(result.get("protocolVersion", String())), MCPProtocol::PROTOCOL_VERSION_LATEST);
}

TEST_CASE("[MCP][Protocol] Enforces session lifecycle before tools are available") {
	MCPToolRegistry registry;
	MCPProtocol protocol(&registry);

	CHECK_EQ(get_error_code(protocol.dispatch(make_request("tools/list", 1))), MCPProtocol::SESSION_NOT_READY);
	CHECK(protocol.dispatch(make_request("ping", 2)).has_response());

	const MCPProtocol::DispatchResult initialize_result = protocol.dispatch(make_initialize_request(MCPProtocol::PROTOCOL_VERSION_LATEST, 3));
	REQUIRE(initialize_result.has_response());
	CHECK_EQ(protocol.get_session_state(), MCPProtocol::SESSION_INITIALIZING);
	CHECK_EQ(get_error_code(protocol.dispatch(make_request("tools/list", 4))), MCPProtocol::SESSION_NOT_READY);

	Dictionary invalid_initialized = make_request("notifications/initialized", 5);
	CHECK_EQ(get_error_code(protocol.dispatch(invalid_initialized)), MCPProtocol::INVALID_REQUEST);
	CHECK_EQ(protocol.get_session_state(), MCPProtocol::SESSION_INITIALIZING);

	Dictionary initialized;
	initialized["jsonrpc"] = "2.0";
	initialized["method"] = "notifications/initialized";
	CHECK_FALSE(protocol.dispatch(initialized).has_response());
	CHECK_EQ(protocol.get_session_state(), MCPProtocol::SESSION_READY);
	CHECK(protocol.dispatch(make_request("tools/list", 6)).has_response());
	CHECK_EQ(get_error_code(protocol.dispatch(make_initialize_request(MCPProtocol::PROTOCOL_VERSION_LATEST, 7))), MCPProtocol::SESSION_ALREADY_INITIALIZED);

	protocol.reset_session();
	CHECK_EQ(protocol.get_session_state(), MCPProtocol::SESSION_UNINITIALIZED);
}

TEST_CASE("[MCP][Protocol] Lists and invokes tools without injecting the JSON-RPC id") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	Dictionary definition;
	definition["name"] = "echo";
	definition["description"] = "Echo arguments and context.";
	CHECK_EQ(registry.register_tool(definition, callable_mp(provider, &ToolProvider::echo), provider), OK);

	MCPProtocol protocol(&registry);
	Dictionary project;
	project["projectId"] = "project-1";
	project["root"] = "C:/project";
	protocol.set_project_metadata(project);
	finish_initialization(protocol, MCPProtocol::PROTOCOL_VERSION_LATEST);

	const Dictionary list_response = protocol.dispatch(make_request("tools/list", "list-1")).response;
	const Dictionary list_result = list_response.get("result", Dictionary());
	const Array tools = list_result.get("tools", Array());
	REQUIRE_EQ(tools.size(), 1);
	CHECK_EQ(String(Dictionary(tools[0]).get("name", String())), "echo");

	Dictionary arguments;
	arguments["id"] = "business-id";
	arguments["value"] = 42;
	Dictionary call_params;
	call_params["name"] = "echo";
	call_params["arguments"] = arguments;
	Dictionary session_context;
	session_context["sessionId"] = "session-1";
	const Dictionary call_response = protocol.dispatch(make_request("tools/call", "rpc-id", call_params), session_context).response;
	const Dictionary call_result = call_response.get("result", Dictionary());
	const Array content = call_result.get("content", Array());
	REQUIRE(content.size() == 1);
	CHECK(Dictionary(content[0]).get("text", String()) == "{\"structuredContentAvailable\":true}");
	const Dictionary structured_content = call_result.get("structuredContent", Dictionary());
	const Dictionary observed_arguments = structured_content.get("arguments", Dictionary());
	const Dictionary observed_context = structured_content.get("context", Dictionary());
	CHECK_EQ(String(observed_arguments.get("id", String())), "business-id");
	CHECK_EQ(String(observed_context.get("requestId", String())), "rpc-id");
	CHECK_EQ(String(Dictionary(observed_context.get("session", Dictionary())).get("sessionId", String())), "session-1");

	const Dictionary meta = call_result.get("_meta", Dictionary());
	CHECK_EQ(String(meta.get("provider", String())), "test");
	const Dictionary project_meta = meta.get("io.godot/project", Dictionary());
	CHECK_EQ(String(project_meta.get("projectId", String())), "project-1");
	CHECK_EQ(provider->call_count, 1);

	CHECK_EQ(registry.unregister_tools_for_owner(provider), 1);
	memdelete(provider);
}

TEST_CASE("[MCP][Protocol] Keeps complete text tool results for legacy clients") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	Dictionary definition;
	definition["name"] = "echo";
	REQUIRE(registry.register_tool(definition, callable_mp(provider, &ToolProvider::echo), provider) == OK);

	MCPProtocol protocol(&registry);
	finish_initialization(protocol, MCPProtocol::PROTOCOL_VERSION_2025_03_26);
	Dictionary call_params;
	call_params["name"] = "echo";
	const Dictionary response = protocol.dispatch(make_request("tools/call", 1, call_params)).response;
	const Dictionary result = response.get("result", Dictionary());
	const Array content = result.get("content", Array());
	REQUIRE(content.size() == 1);
	CHECK(Dictionary(content[0]).get("text", String()) == "echo");
	CHECK(result.has("structuredContent"));

	registry.unregister_tools_for_owner(provider);
	memdelete(provider);
}

TEST_CASE("[MCP][Protocol] Returns standard JSON-RPC errors and does not execute request notifications") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	Dictionary definition;
	definition["name"] = "echo";
	CHECK_EQ(registry.register_tool(definition, callable_mp(provider, &ToolProvider::echo), provider), OK);

	MCPProtocol protocol(&registry);
	CHECK_EQ(get_error_code(protocol.dispatch_json("{")), MCPProtocol::PARSE_ERROR);

	Dictionary invalid_id = make_request("ping", true);
	CHECK_EQ(get_error_code(protocol.dispatch(invalid_id)), MCPProtocol::INVALID_REQUEST);
	finish_initialization(protocol, MCPProtocol::PROTOCOL_VERSION_LATEST);
	CHECK_EQ(get_error_code(protocol.dispatch(make_request("unknown", 1))), MCPProtocol::METHOD_NOT_FOUND);

	Dictionary invalid_params;
	invalid_params["jsonrpc"] = "2.0";
	invalid_params["id"] = 2;
	invalid_params["method"] = "tools/list";
	invalid_params["params"] = Array();
	CHECK_EQ(get_error_code(protocol.dispatch(invalid_params)), MCPProtocol::INVALID_PARAMS);

	Dictionary notification;
	notification["jsonrpc"] = "2.0";
	notification["method"] = "tools/call";
	Dictionary call_params;
	call_params["name"] = "echo";
	notification["params"] = call_params;
	CHECK_FALSE(protocol.dispatch(notification).has_response());
	CHECK_EQ(provider->call_count, 0);

	CHECK_EQ(registry.unregister_tools_for_owner(provider), 1);
	memdelete(provider);
}

TEST_CASE("[MCP][Protocol] Keeps legacy batch compatibility version-scoped") {
	MCPProtocol current_protocol;
	finish_initialization(current_protocol, MCPProtocol::PROTOCOL_VERSION_LATEST);
	Array current_batch;
	current_batch.push_back(make_request("ping", 1));
	CHECK_EQ(get_error_code(current_protocol.dispatch(current_batch)), MCPProtocol::INVALID_REQUEST);

	MCPProtocol legacy_protocol;
	finish_initialization(legacy_protocol, MCPProtocol::PROTOCOL_VERSION_2025_03_26);
	Array legacy_batch;
	legacy_batch.push_back(make_request("ping", 1));
	Dictionary notification;
	notification["jsonrpc"] = "2.0";
	notification["method"] = "unknown-notification";
	legacy_batch.push_back(notification);
	const MCPProtocol::DispatchResult legacy_result = legacy_protocol.dispatch(legacy_batch);
	REQUIRE(legacy_result.has_response());
	const Array responses = legacy_result.response;
	CHECK_EQ(responses.size(), 1);
}

} // namespace TestMCPProtocol
