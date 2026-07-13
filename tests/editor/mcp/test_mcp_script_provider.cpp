/**************************************************************************/
/*  test_mcp_script_provider.cpp                                         */
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

#include "tests/test_macros.h"

#include "modules/modules_enabled.gen.h"

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

#include "core/mcp/mcp_tool_registry.h"
#include "editor/mcp/providers/mcp_script_provider.h"

#endif

TEST_FORCE_LINK(test_mcp_script_provider);

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

namespace TestMCPScriptProvider {

static String _error_code(const Dictionary &p_result) {
	const Dictionary structured = p_result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return error.get("code", String());
}

TEST_CASE("[MCP][Provider] Script tools register with optimistic edit requirements") {
	CHECK(String(MCPScriptProvider::STALE_REVISION_ERROR_CODE) == "stale_revision");

	MCPToolRegistry registry;
	MCPScriptProvider *provider = memnew(MCPScriptProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(names.size() == 5);
	CHECK(names[0] == "godot.script.create");
	CHECK(names[1] == "godot.script.get");
	CHECK(names[2] == "godot.script.usages");
	CHECK(names[3] == "godot.script.edit");
	CHECK(names[4] == "godot.script.save");
	CHECK(registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI).is_empty());

	const Array definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(definitions.size() == 5);
	const Dictionary get_schema = Dictionary(definitions[1]).get("inputSchema", Dictionary());
	const Dictionary get_properties = get_schema.get("properties", Dictionary());
	CHECK(get_properties.has("path"));
	CHECK(get_properties.has("nodePath"));
	CHECK(get_properties.has("view"));
	CHECK(get_properties.has("includeComments"));
	CHECK(get_properties.has("member"));
	const Dictionary member_schema = get_properties.get("member", Dictionary());
	const Dictionary member_properties = member_schema.get("properties", Dictionary());
	CHECK(member_properties.has("owner"));
	CHECK(member_properties.has("classPath"));
	CHECK(Array(get_schema.get("oneOf", Array())).size() == 2);
	const Dictionary usages_schema = Dictionary(definitions[2]).get("inputSchema", Dictionary());
	const Dictionary usages_properties = usages_schema.get("properties", Dictionary());
	CHECK(usages_properties.has("member"));
	CHECK(usages_properties.has("includeDeclaration"));
	CHECK(PackedStringArray(usages_schema.get("required", PackedStringArray())).has("member"));
	const Dictionary edit_schema = Dictionary(definitions[3]).get("inputSchema", Dictionary());
	const Dictionary properties = edit_schema.get("properties", Dictionary());
	CHECK(properties.has("nodePath"));
	CHECK(properties.has("expected_revision"));
	CHECK(properties.has("expected_sha256"));
	const Array edit_constraints = edit_schema.get("allOf", Array());
	REQUIRE(edit_constraints.size() == 2);
	CHECK(Array(Dictionary(edit_constraints[1]).get("anyOf", Array())).size() == 2);

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	const MCPToolRegistry::CallResult invalid_call = registry.call_tool("godot.script.create", Dictionary(), context);
	REQUIRE(invalid_call.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(invalid_call.result.get("isError", false)));

	Dictionary invalid_member;
	invalid_member["kind"] = "banana";
	invalid_member["name"] = "value";
	Dictionary member_arguments;
	member_arguments["path"] = "res://missing.gd";
	member_arguments["member"] = invalid_member;
	CHECK(_error_code(registry.call_tool("godot.script.get", member_arguments, context).result) == "INVALID_ARGUMENTS");
	CHECK(_error_code(registry.call_tool("godot.script.usages", member_arguments, context).result) == "INVALID_ARGUMENTS");

	invalid_member["kind"] = "parameter";
	invalid_member["owner"] = "Outer.call";
	member_arguments["member"] = invalid_member;
	CHECK(_error_code(registry.call_tool("godot.script.usages", member_arguments, context).result) == "INVALID_ARGUMENTS");

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
}

} // namespace TestMCPScriptProvider

#endif // MODULE_GDSCRIPT_ENABLED && !GDSCRIPT_NO_LSP
