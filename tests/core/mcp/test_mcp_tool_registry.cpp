/**************************************************************************/
/*  test_mcp_tool_registry.cpp                                            */
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

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_tool_registry);

namespace TestMCPToolRegistry {

class ToolProvider : public Object {
public:
	Dictionary echo(const Dictionary &p_arguments, const Dictionary &p_context) {
		Dictionary result;
		result["arguments"] = p_arguments;
		result["context"] = p_context;
		return result;
	}

	String invalid_result(const Dictionary &, const Dictionary &) {
		return "invalid";
	}
};

static Dictionary make_definition(const String &p_name) {
	Dictionary definition;
	definition["name"] = p_name;
	definition["description"] = "Test tool.";
	return definition;
}

TEST_CASE("[MCP][ToolRegistry] Registers in order, filters surfaces, and rejects duplicates") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	const Callable handler = callable_mp(provider, &ToolProvider::echo);

	CHECK_EQ(registry.register_tool(make_definition("first"), handler, MCPToolRegistry::TOOL_SURFACE_MCP, provider), OK);
	CHECK_EQ(registry.register_tool(make_definition("cli_only"), handler, MCPToolRegistry::TOOL_SURFACE_CLI, provider), OK);
	CHECK_EQ(registry.register_tool(make_definition("second"), handler, MCPToolRegistry::TOOL_SURFACE_ALL, provider), OK);
	CHECK_EQ(registry.register_tool(make_definition("default_mcp"), handler), OK);

	String duplicate_error;
	CHECK_EQ(registry.register_tool(make_definition("first"), handler, MCPToolRegistry::TOOL_SURFACE_MCP, provider, &duplicate_error), ERR_ALREADY_EXISTS);
	CHECK(duplicate_error.contains("already registered"));

	const Array mcp_definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE_EQ(mcp_definitions.size(), 3);
	CHECK_EQ(String(Dictionary(mcp_definitions[0]).get("name", String())), "first");
	CHECK_EQ(String(Dictionary(mcp_definitions[1]).get("name", String())), "second");
	CHECK_EQ(String(Dictionary(mcp_definitions[2]).get("name", String())), "default_mcp");
	CHECK(Dictionary(mcp_definitions[0]).has("inputSchema"));
	Dictionary exposed_definition = mcp_definitions[0];
	exposed_definition["description"] = "mutated";
	const Array fresh_definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	CHECK(String(Dictionary(fresh_definitions[0])["description"]) == "Test tool.");

	const PackedStringArray cli_names = registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI);
	REQUIRE_EQ(cli_names.size(), 2);
	CHECK_EQ(cli_names[0], "cli_only");
	CHECK_EQ(cli_names[1], "second");

	CHECK_EQ(registry.unregister_tools_for_owner(provider), 4);
	CHECK(registry.get_tool_names().is_empty());
	CHECK(registry.get_tool_definitions().is_empty());
	memdelete(provider);
}

TEST_CASE("[MCP][ToolRegistry] Keeps transport context separate from tool arguments") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	CHECK_EQ(registry.register_tool(make_definition("echo"), callable_mp(provider, &ToolProvider::echo), MCPToolRegistry::TOOL_SURFACE_ALL, provider), OK);

	Dictionary arguments;
	arguments["id"] = "business-id";
	arguments["value"] = 42;

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	context.request_id = "rpc-id";
	context.protocol_version = "2025-11-25";
	context.session["sessionId"] = "session-1";

	const MCPToolRegistry::CallResult call_result = registry.call_tool("echo", arguments, context);
	REQUIRE_EQ(call_result.status, MCPToolRegistry::CALL_OK);
	const Dictionary observed_arguments = call_result.result.get("arguments", Dictionary());
	const Dictionary observed_context = call_result.result.get("context", Dictionary());
	CHECK_EQ(String(observed_arguments.get("id", String())), "business-id");
	CHECK_EQ(String(observed_context.get("requestId", String())), "rpc-id");
	CHECK_EQ(String(Dictionary(observed_context.get("session", Dictionary())).get("sessionId", String())), "session-1");

	context.surface = 1 << 7;
	CHECK_EQ(registry.call_tool("echo", arguments, context).status, MCPToolRegistry::CALL_TOOL_NOT_FOUND);

	CHECK_EQ(registry.unregister_tools_for_owner(provider), 1);
	memdelete(provider);
}

TEST_CASE("[MCP][ToolRegistry] Reports invalid handler results without a transport envelope") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	CHECK_EQ(registry.register_tool(make_definition("invalid"), callable_mp(provider, &ToolProvider::invalid_result), MCPToolRegistry::TOOL_SURFACE_MCP, provider), OK);

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	const MCPToolRegistry::CallResult call_result = registry.call_tool("invalid", Dictionary(), context);
	CHECK_EQ(call_result.status, MCPToolRegistry::CALL_INVALID_RESULT);
	CHECK(call_result.result.is_empty());

	CHECK_EQ(registry.unregister_tools_for_owner(provider), 1);
	memdelete(provider);
}

} // namespace TestMCPToolRegistry
