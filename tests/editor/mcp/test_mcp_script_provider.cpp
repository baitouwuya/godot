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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/mcp/mcp_tool_registry.h"
#include "editor/mcp/providers/mcp_script_buffer.h"
#include "editor/mcp/providers/mcp_script_buffer_service.h"
#include "editor/mcp/providers/mcp_script_provider.h"
#include "editor/mcp/providers/mcp_script_tool_utils.h"

#endif

TEST_FORCE_LINK(test_mcp_script_provider);

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

namespace TestMCPScriptProvider {

static String _error_code(const Dictionary &p_result) {
	const Dictionary structured = p_result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return error.get("code", String());
}

static Dictionary _error_details(const Dictionary &p_result) {
	const Dictionary structured = p_result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return error.get("details", Dictionary());
}

TEST_CASE("[MCP][Provider] Script tool utilities validate shared member arguments") {
	String view;
	bool include_comments = false;
	Dictionary member;
	String error;
	CHECK(MCPScriptToolUtils::parse_script_view(Dictionary(), view, include_comments, member, error));
	CHECK(view == "documentation");
	CHECK(include_comments);
	CHECK(member.is_empty());

	member["kind"] = "method";
	member["name"] = "run";
	CHECK(MCPScriptToolUtils::validate_member_query(member, error));
	member["kind"] = "unsupported";
	CHECK_FALSE(MCPScriptToolUtils::validate_member_query(member, error));
	CHECK(error == "member kind is not supported.");
}

TEST_CASE("[MCP][Provider] Script buffer service creates bounded external scripts") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_script_buffer_service", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());
	REQUIRE(temporary_directory->make_dir("project") == OK);
	const String project_root = temporary_directory->get_current_dir().path_join("project");
	const String source = "extends Node\nvar value: int = 1\n";
	MCPScriptBufferService service(project_root);

	String resource_path;
	MCPScriptBufferService::OperationResult result = service.create_external("res://scripts/example.gd", source, resource_path);
	CHECK(result.is_ok());
	CHECK(resource_path == "res://scripts/example.gd");
	CHECK(FileAccess::get_file_as_string(project_root.path_join("scripts/example.gd")) == source);

	result = service.create_external("res://scripts/example.gd", source, resource_path);
	CHECK(result.error == ERR_ALREADY_EXISTS);
	CHECK(result.error_code == "SCRIPT_EXISTS");
	result = service.create_external("res://scripts/example.txt", source, resource_path);
	CHECK(result.error == ERR_INVALID_PARAMETER);
	CHECK(result.error_code == "INVALID_PATH");

	MCPScriptBuffer buffer;
	result = service.open_selected(Dictionary(), buffer);
	CHECK(result.error == ERR_INVALID_PARAMETER);
	CHECK(result.error_code == "INVALID_ARGUMENTS");
	Dictionary conflicting_selector;
	conflicting_selector["path"] = "res://scripts/example.gd";
	conflicting_selector["nodePath"] = "Player";
	result = service.open_selected(conflicting_selector, buffer);
	CHECK(result.error == ERR_INVALID_PARAMETER);
	CHECK(result.error_code == "INVALID_ARGUMENTS");
}

TEST_CASE("[MCP][Provider] Script tools register with optimistic edit requirements") {
	CHECK(String(MCPScriptProvider::STALE_REVISION_ERROR_CODE) == "stale_revision");

	MCPToolRegistry registry;
	MCPScriptProvider *provider = memnew(MCPScriptProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 6);
	CHECK(names[0] == "godot.script.create");
	CHECK(names[1] == "godot.script.open");
	CHECK(names[2] == "godot.script.get");
	CHECK(names[3] == "godot.script.usages");
	CHECK(names[4] == "godot.script.edit");
	CHECK(names[5] == "godot.script.save");

	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 6);
	const Dictionary create_schema = Dictionary(definitions[0]).get("inputSchema", Dictionary());
	CHECK(int(Dictionary(Dictionary(create_schema.get("properties", Dictionary())).get("path", Dictionary())).get("minLength", 0)) == 1);
	const Dictionary create_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(create_output.get("required", PackedStringArray())).has("diagnostics"));
	const Dictionary open_schema = Dictionary(definitions[1]).get("inputSchema", Dictionary());
	CHECK(Array(open_schema.get("oneOf", Array())).size() == 2);
	const Dictionary open_properties = open_schema.get("properties", Dictionary());
	CHECK(int(Dictionary(open_properties.get("path", Dictionary())).get("minLength", 0)) == 1);
	const Dictionary open_output = Dictionary(definitions[1]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(open_output.get("required", PackedStringArray())).has("opened"));
	const Dictionary get_schema = Dictionary(definitions[2]).get("inputSchema", Dictionary());
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
	const Dictionary usages_schema = Dictionary(definitions[3]).get("inputSchema", Dictionary());
	const Dictionary usages_properties = usages_schema.get("properties", Dictionary());
	CHECK(usages_properties.has("member"));
	CHECK(usages_properties.has("includeDeclaration"));
	CHECK(PackedStringArray(usages_schema.get("required", PackedStringArray())).has("member"));
	const Dictionary usages_output = Dictionary(definitions[3]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(usages_output.get("required", PackedStringArray())).has("locations"));
	const Dictionary edit_schema = Dictionary(definitions[4]).get("inputSchema", Dictionary());
	const Dictionary properties = edit_schema.get("properties", Dictionary());
	CHECK(properties.has("nodePath"));
	CHECK(properties.has("expected_revision"));
	CHECK(properties.has("expected_sha256"));
	const Array edit_constraints = edit_schema.get("allOf", Array());
	REQUIRE(edit_constraints.size() == 2);
	CHECK(Array(Dictionary(edit_constraints[1]).get("anyOf", Array())).size() == 2);
	const Dictionary edit_output = Dictionary(definitions[4]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(edit_output.get("required", PackedStringArray())).has("changed"));
	const Dictionary save_output = Dictionary(definitions[5]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(save_output.get("required", PackedStringArray())).has("saved"));

	MCPToolCallContext context;
	Dictionary unexpected_arguments;
	unexpected_arguments["path"] = "res://new.gd";
	unexpected_arguments["text"] = String();
	unexpected_arguments["unexpected"] = true;
	const Dictionary unexpected_result = registry.call_tool("godot.script.create", unexpected_arguments, context).result;
	CHECK(_error_code(unexpected_result) == "INVALID_ARGUMENTS");
	CHECK(_error_details(unexpected_result).get("keyword", String()) == "additionalProperties");
	const MCPToolRegistry::CallResult invalid_call = registry.call_tool("godot.script.create", Dictionary(), context);
	REQUIRE(invalid_call.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(invalid_call.result.get("isError", false)));
	CHECK(_error_code(registry.call_tool("godot.script.open", Dictionary(), context).result) == "INVALID_ARGUMENTS");

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
