/**************************************************************************/
/*  test_mcp_scene_provider.cpp                                           */
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
#include "editor/mcp/providers/mcp_scene_provider.h"
#include "editor/mcp/providers/mcp_scene_tool_utils.h"
#include "scene/main/node.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_scene_provider);

namespace TestMCPSceneProvider {

TEST_CASE("[MCP][Provider] Scene tools register in order") {
	MCPToolRegistry registry;
	MCPSceneProvider *provider = memnew(MCPSceneProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 4);
	CHECK(names[0] == "godot.scene.open");
	CHECK(names[1] == "godot.scene.get_tree");
	CHECK(names[2] == "godot.scene.get_selection");
	CHECK(names[3] == "godot.scene.save");

	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 4);
	const Dictionary open_schema = Dictionary(definitions[0]).get("inputSchema", Dictionary());
	CHECK(PackedStringArray(open_schema.get("required", PackedStringArray())).has("path"));
	CHECK(int(Dictionary(open_schema.get("properties", Dictionary())).get("path", Dictionary()).get("minLength", 0)) == 1);
	const Dictionary open_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(open_output.get("required", PackedStringArray())).has("opened"));
	const Dictionary tree_schema = Dictionary(definitions[1]).get("inputSchema", Dictionary());
	const Dictionary tree_properties = tree_schema.get("properties", Dictionary());
	CHECK(tree_properties.has("rootPath"));
	CHECK(tree_properties.has("maxDepth"));
	CHECK(tree_properties.has("includeInternal"));
	CHECK(Dictionary(tree_properties.get("rootPath", Dictionary())).get("default", String()) == ".");
	CHECK(int(Dictionary(tree_properties.get("maxDepth", Dictionary())).get("default", 0)) == -1);
	CHECK_FALSE(bool(Dictionary(tree_properties.get("includeInternal", Dictionary())).get("default", true)));
	const Dictionary tree_output = Dictionary(definitions[1]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(tree_output.get("required", PackedStringArray())).has("root"));
	const Dictionary tree_output_properties = tree_output.get("properties", Dictionary());
	const Dictionary root_output = tree_output_properties.get("root", Dictionary());
	CHECK(Dictionary(root_output.get("properties", Dictionary())).has("path"));
	const Dictionary selection_output = Dictionary(definitions[2]).get("outputSchema", Dictionary());
	const Dictionary selection_properties = selection_output.get("properties", Dictionary());
	CHECK(selection_properties.has("nodes"));
	const Dictionary selected_nodes = selection_properties.get("nodes", Dictionary());
	CHECK(Dictionary(Dictionary(selected_nodes.get("items", Dictionary())).get("properties", Dictionary())).has("path"));
	CHECK_FALSE(bool(selection_output.get("additionalProperties", true)));
	const Dictionary save_output = Dictionary(definitions[3]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(save_output.get("required", PackedStringArray())).has("saved"));

	MCPToolCallContext context;
	Dictionary invalid_arguments;
	invalid_arguments["unknown"] = true;
	const MCPToolRegistry::CallResult invalid_call = registry.call_tool("godot.scene.get_tree", invalid_arguments, context);
	REQUIRE(invalid_call.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(invalid_call.result.get("isError", false)));

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] Scene tool contracts parse arguments consistently") {
	MCPSceneToolUtils::TreeOptions options;
	String error_message;
	CHECK(MCPSceneToolUtils::parse_tree_options(Dictionary(), options, error_message));
	CHECK(options.root_path == ".");
	CHECK(options.max_depth == -1);
	CHECK_FALSE(options.include_internal);
	CHECK(error_message.is_empty());

	Dictionary tree_arguments;
	tree_arguments["rootPath"] = "Root/Child";
	tree_arguments["maxDepth"] = 64.0;
	tree_arguments["includeInternal"] = true;
	CHECK(MCPSceneToolUtils::parse_tree_options(tree_arguments, options, error_message));
	CHECK(options.root_path == "Root/Child");
	CHECK(options.max_depth == 64);
	CHECK(options.include_internal);

	tree_arguments["maxDepth"] = 1.5;
	CHECK_FALSE(MCPSceneToolUtils::parse_tree_options(tree_arguments, options, error_message));
	CHECK(error_message.contains("maxDepth"));
	tree_arguments["maxDepth"] = -2;
	CHECK_FALSE(MCPSceneToolUtils::parse_tree_options(tree_arguments, options, error_message));
	tree_arguments["maxDepth"] = 65;
	CHECK_FALSE(MCPSceneToolUtils::parse_tree_options(tree_arguments, options, error_message));

	String path;
	Dictionary open_arguments;
	open_arguments["path"] = "res://main.tscn";
	CHECK(MCPSceneToolUtils::parse_open_path(open_arguments, path, error_message));
	CHECK(path == "res://main.tscn");
	open_arguments["path"] = String();
	CHECK_FALSE(MCPSceneToolUtils::parse_open_path(open_arguments, path, error_message));
	open_arguments["path"] = 1;
	CHECK_FALSE(MCPSceneToolUtils::parse_open_path(open_arguments, path, error_message));

	CHECK(MCPSceneToolUtils::parse_save_path(Dictionary(), path, error_message));
	CHECK(path.is_empty());
	Dictionary save_arguments;
	save_arguments["path"] = "res://saved.tscn";
	CHECK(MCPSceneToolUtils::parse_save_path(save_arguments, path, error_message));
	CHECK(path == "res://saved.tscn");
	save_arguments["path"] = false;
	CHECK_FALSE(MCPSceneToolUtils::parse_save_path(save_arguments, path, error_message));
}

TEST_CASE("[MCP][Provider] Scene tree tool returns the injected edited scene") {
	Node *scene_root = memnew(Node);
	scene_root->set_name("SceneRoot");
	Node *child = memnew(Node);
	child->set_name("Child");
	scene_root->add_child(child);
	child->set_owner(scene_root);

	MCPToolRegistry registry;
	MCPSceneProvider *provider = memnew(MCPSceneProvider(scene_root));
	REQUIRE(provider->register_tools(&registry) == OK);

	Dictionary arguments;
	arguments["rootPath"] = ".";
	arguments["maxDepth"] = 1.0;
	arguments["includeInternal"] = false;
	MCPToolCallContext context;
	const MCPToolRegistry::CallResult call_result = registry.call_tool("godot.scene.get_tree", arguments, context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));

	const Dictionary content = call_result.result.get("structuredContent", Dictionary());
	const Dictionary root = content.get("root", Dictionary());
	CHECK(root.get("path", String()) == ".");
	CHECK(root.get("name", String()) == "SceneRoot");
	const Array children = root.get("children", Array());
	REQUIRE(children.size() == 1);
	CHECK(Dictionary(children[0]).get("path", String()) == "Child");

	arguments["maxDepth"] = 1.5;
	CHECK(bool(registry.call_tool("godot.scene.get_tree", arguments, context).result.get("isError", false)));
	arguments["maxDepth"] = -2.0;
	CHECK(bool(registry.call_tool("godot.scene.get_tree", arguments, context).result.get("isError", false)));
	arguments["maxDepth"] = 65.0;
	CHECK(bool(registry.call_tool("godot.scene.get_tree", arguments, context).result.get("isError", false)));

	provider->unregister_tools();
	memdelete(provider);
	memdelete(scene_root);
}

} // namespace TestMCPSceneProvider
