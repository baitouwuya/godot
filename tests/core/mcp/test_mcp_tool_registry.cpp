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
	int call_count = 0;

	Dictionary echo(const Dictionary &p_arguments, const Dictionary &p_context) {
		call_count++;
		Dictionary result;
		result["arguments"] = p_arguments;
		result["context"] = p_context;
		return result;
	}

	String invalid_result(const Dictionary &, const Dictionary &) {
		return "invalid";
	}

	Dictionary structured_result(const Dictionary &, const Dictionary &) {
		Dictionary structured_content;
		structured_content["count"] = 1;
		Dictionary result;
		result["content"] = Array();
		result["structuredContent"] = structured_content;
		return result;
	}

	Dictionary invalid_structured_result(const Dictionary &, const Dictionary &) {
		Dictionary structured_content;
		structured_content["count"] = "one";
		Dictionary result;
		result["content"] = Array();
		result["structuredContent"] = structured_content;
		return result;
	}
};

static Dictionary make_definition(const String &p_name) {
	Dictionary definition;
	definition["name"] = p_name;
	definition["description"] = "Test tool.";
	return definition;
}

TEST_CASE("[MCP][ToolRegistry] Closed schemas reject unknown arguments with valid names") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	Dictionary properties;
	properties["zeta"] = Dictionary();
	properties["alpha"] = Dictionary();
	Dictionary input_schema;
	input_schema["type"] = "object";
	input_schema["properties"] = properties;
	input_schema["additionalProperties"] = false;
	Dictionary definition = make_definition("closed");
	definition["inputSchema"] = input_schema;
	REQUIRE(registry.register_tool(definition, callable_mp(provider, &ToolProvider::echo), provider) == OK);

	Dictionary arguments;
	arguments["alpah"] = true;
	MCPToolCallContext context;
	const MCPToolRegistry::CallResult result = registry.call_tool("closed", arguments, context);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK(provider->call_count == 0);
	CHECK(bool(result.result.get("isError", false)));
	const Dictionary structured = result.result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	CHECK(error.get("code", String()) == "INVALID_ARGUMENTS");
	CHECK(String(error.get("message", String())).contains("Valid arguments: alpha, zeta."));
	const Dictionary details = error.get("details", Dictionary());
	CHECK(details.get("unknownArgument", String()) == "alpah");
	CHECK(PackedStringArray(details.get("validArguments", PackedStringArray())) == PackedStringArray{ "alpha", "zeta" });
	CHECK(details.get("keyword", String()) == "additionalProperties");
	CHECK(String(details.get("instancePath", String())).is_empty());

	registry.unregister_tools_for_owner(provider);
	memdelete(provider);
}

TEST_CASE("[MCP][ToolRegistry] Closed empty schemas explain that no arguments are accepted") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	Dictionary input_schema;
	input_schema["type"] = "object";
	input_schema["properties"] = Dictionary();
	input_schema["additionalProperties"] = false;
	Dictionary definition = make_definition("empty");
	definition["inputSchema"] = input_schema;
	REQUIRE(registry.register_tool(definition, callable_mp(provider, &ToolProvider::echo), provider) == OK);

	Dictionary arguments;
	arguments["unexpected"] = true;
	MCPToolCallContext context;
	const MCPToolRegistry::CallResult result = registry.call_tool("empty", arguments, context);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK(provider->call_count == 0);
	CHECK(bool(result.result.get("isError", false)));
	const Dictionary structured = result.result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	CHECK(error.get("code", String()) == "INVALID_ARGUMENTS");
	CHECK(String(error.get("message", String())).contains("This tool accepts no arguments."));
	const Dictionary details = error.get("details", Dictionary());
	CHECK(details.get("unknownArgument", String()) == "unexpected");
	CHECK(PackedStringArray(details.get("validArguments", PackedStringArray())).is_empty());
	CHECK(details.get("keyword", String()) == "additionalProperties");

	registry.unregister_tools_for_owner(provider);
	memdelete(provider);
}

TEST_CASE("[MCP][ToolRegistry] Registers in order and rejects duplicates") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	const Callable handler = callable_mp(provider, &ToolProvider::echo);

	CHECK_EQ(registry.register_tool(make_definition("first"), handler, provider), OK);
	CHECK_EQ(registry.register_tool(make_definition("second"), handler, provider), OK);
	CHECK_EQ(registry.register_tool(make_definition("default_mcp"), handler), OK);

	String duplicate_error;
	CHECK_EQ(registry.register_tool(make_definition("first"), handler, provider, &duplicate_error), ERR_ALREADY_EXISTS);
	CHECK(duplicate_error.contains("already registered"));

	const Array mcp_definitions = registry.get_tool_definitions();
	REQUIRE_EQ(mcp_definitions.size(), 3);
	CHECK_EQ(String(Dictionary(mcp_definitions[0]).get("name", String())), "first");
	CHECK_EQ(String(Dictionary(mcp_definitions[1]).get("name", String())), "second");
	CHECK_EQ(String(Dictionary(mcp_definitions[2]).get("name", String())), "default_mcp");
	CHECK(Dictionary(mcp_definitions[0]).has("inputSchema"));
	Dictionary exposed_definition = mcp_definitions[0];
	exposed_definition["description"] = "mutated";
	const Array fresh_definitions = registry.get_tool_definitions();
	CHECK(String(Dictionary(fresh_definitions[0])["description"]) == "Test tool.");

	const PackedStringArray names = registry.get_tool_names();
	REQUIRE_EQ(names.size(), 3);
	CHECK_EQ(names[0], "first");
	CHECK_EQ(names[1], "second");
	CHECK_EQ(names[2], "default_mcp");

	CHECK_EQ(registry.unregister_tools_for_owner(provider), 3);
	CHECK(registry.get_tool_names().is_empty());
	CHECK(registry.get_tool_definitions().is_empty());
	memdelete(provider);
}

TEST_CASE("[MCP][ToolRegistry] Validates and isolates tool result metadata") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	const Callable handler = callable_mp(provider, &ToolProvider::echo);

	Dictionary definition = make_definition("metadata");
	Dictionary output_schema;
	output_schema["type"] = "object";
	definition["outputSchema"] = output_schema;
	Dictionary annotations;
	annotations["readOnlyHint"] = true;
	annotations["destructiveHint"] = false;
	annotations["idempotentHint"] = true;
	annotations["openWorldHint"] = false;
	definition["annotations"] = annotations;
	CHECK_EQ(registry.register_tool(definition, handler, provider), OK);

	output_schema["type"] = "array";
	annotations["readOnlyHint"] = false;
	const Dictionary registered = registry.get_tool_definitions()[0];
	CHECK(Dictionary(registered.get("outputSchema", Dictionary())).get("type", String()) == "object");
	CHECK(bool(Dictionary(registered.get("annotations", Dictionary())).get("readOnlyHint", false)));

	Dictionary invalid_output = make_definition("invalid_output");
	invalid_output["outputSchema"] = "object";
	String error;
	CHECK_EQ(registry.register_tool(invalid_output, handler, provider, &error), ERR_INVALID_PARAMETER);
	CHECK(error.contains("outputSchema"));

	Dictionary invalid_schema = make_definition("invalid_schema");
	Dictionary bad_input_schema;
	bad_input_schema["type"] = "dictionary";
	invalid_schema["inputSchema"] = bad_input_schema;
	CHECK_EQ(registry.register_tool(invalid_schema, handler, provider, &error), ERR_INVALID_PARAMETER);
	CHECK(error.contains("inputSchema"));
	CHECK(error.contains("Unknown JSON type"));

	Dictionary invalid_annotations = make_definition("invalid_annotations");
	invalid_annotations["annotations"] = Array();
	CHECK_EQ(registry.register_tool(invalid_annotations, handler, provider, &error), ERR_INVALID_PARAMETER);
	CHECK(error.contains("annotations"));

	Dictionary invalid_hint = make_definition("invalid_hint");
	Dictionary bad_annotations;
	bad_annotations["readOnlyHint"] = "true";
	invalid_hint["annotations"] = bad_annotations;
	CHECK_EQ(registry.register_tool(invalid_hint, handler, provider, &error), ERR_INVALID_PARAMETER);
	CHECK(error.contains("readOnlyHint"));

	CHECK_EQ(registry.unregister_tools_for_owner(provider), 1);
	memdelete(provider);
}

TEST_CASE("[MCP][ToolRegistry] Keeps transport context separate from tool arguments") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	CHECK_EQ(registry.register_tool(make_definition("echo"), callable_mp(provider, &ToolProvider::echo), provider), OK);

	Dictionary arguments;
	arguments["id"] = "business-id";
	arguments["value"] = 42;

	MCPToolCallContext context;
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

	CHECK_EQ(registry.call_tool("missing", arguments, context).status, MCPToolRegistry::CALL_TOOL_NOT_FOUND);

	CHECK_EQ(registry.unregister_tools_for_owner(provider), 1);
	memdelete(provider);
}

TEST_CASE("[MCP][ToolRegistry] Reports invalid handler results without a transport envelope") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	CHECK_EQ(registry.register_tool(make_definition("invalid"), callable_mp(provider, &ToolProvider::invalid_result), provider), OK);

	MCPToolCallContext context;
	const MCPToolRegistry::CallResult call_result = registry.call_tool("invalid", Dictionary(), context);
	CHECK_EQ(call_result.status, MCPToolRegistry::CALL_INVALID_RESULT);
	CHECK(call_result.result.is_empty());

	CHECK_EQ(registry.unregister_tools_for_owner(provider), 1);
	memdelete(provider);
}

TEST_CASE("[MCP][ToolRegistry] Validates structured tool results against output schemas") {
	MCPToolRegistry registry;
	ToolProvider *provider = memnew(ToolProvider);
	Dictionary output_schema;
	output_schema["type"] = "object";
	Dictionary properties;
	Dictionary count_schema;
	count_schema["type"] = "integer";
	properties["count"] = count_schema;
	output_schema["properties"] = properties;
	output_schema["required"] = PackedStringArray{ "count" };
	output_schema["additionalProperties"] = false;
	Dictionary definition = make_definition("structured");
	definition["outputSchema"] = output_schema;
	REQUIRE(registry.register_tool(definition, callable_mp(provider, &ToolProvider::structured_result), provider) == OK);

	MCPToolRegistry::CallResult result = registry.call_tool("structured", Dictionary(), MCPToolCallContext());
	CHECK(result.status == MCPToolRegistry::CALL_OK);
	CHECK(int(Dictionary(result.result["structuredContent"]).get("count", 0)) == 1);

	Dictionary broken_definition = make_definition("broken_structured");
	broken_definition["outputSchema"] = output_schema;
	ToolProvider *broken_provider = memnew(ToolProvider);
	REQUIRE(registry.register_tool(broken_definition, callable_mp(broken_provider, &ToolProvider::echo), broken_provider) == OK);
	result = registry.call_tool("broken_structured", Dictionary(), MCPToolCallContext());
	CHECK(result.status == MCPToolRegistry::CALL_INVALID_RESULT);
	CHECK(result.message.contains("structuredContent"));

	Dictionary invalid_definition = make_definition("invalid_structured");
	invalid_definition["outputSchema"] = output_schema;
	ToolProvider *invalid_provider = memnew(ToolProvider);
	REQUIRE(registry.register_tool(invalid_definition, callable_mp(invalid_provider, &ToolProvider::invalid_structured_result), invalid_provider) == OK);
	result = registry.call_tool("invalid_structured", Dictionary(), MCPToolCallContext());
	CHECK(result.status == MCPToolRegistry::CALL_INVALID_RESULT);
	CHECK(result.message.contains("must be an integer"));

	registry.unregister_tools_for_owner(provider);
	registry.unregister_tools_for_owner(broken_provider);
	registry.unregister_tools_for_owner(invalid_provider);
	memdelete(provider);
	memdelete(broken_provider);
	memdelete(invalid_provider);
}

} // namespace TestMCPToolRegistry
