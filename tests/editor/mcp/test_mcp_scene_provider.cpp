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
#include "scene/main/node.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_scene_provider);

namespace TestMCPSceneProvider {

TEST_CASE("[MCP][Provider] Scene tools register in order on the MCP surface") {
	MCPToolRegistry registry;
	MCPSceneProvider *provider = memnew(MCPSceneProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(names.size() == 3);
	CHECK(names[0] == "godot.scene.get_tree");
	CHECK(names[1] == "godot.scene.get_selection");
	CHECK(names[2] == "godot.scene.save");
	CHECK(registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI).is_empty());

	const Array definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(definitions.size() == 3);
	const Dictionary tree_schema = Dictionary(definitions[0]).get("inputSchema", Dictionary());
	const Dictionary tree_properties = tree_schema.get("properties", Dictionary());
	CHECK(tree_properties.has("rootPath"));
	CHECK(tree_properties.has("maxDepth"));
	CHECK(tree_properties.has("includeInternal"));

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	Dictionary invalid_arguments;
	invalid_arguments["unknown"] = true;
	const MCPToolRegistry::CallResult invalid_call = registry.call_tool("godot.scene.get_tree", invalid_arguments, context);
	REQUIRE(invalid_call.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(invalid_call.result.get("isError", false)));

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
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
	arguments["maxDepth"] = 1;
	arguments["includeInternal"] = false;
	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
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

	provider->unregister_tools();
	memdelete(provider);
	memdelete(scene_root);
}

} // namespace TestMCPSceneProvider
