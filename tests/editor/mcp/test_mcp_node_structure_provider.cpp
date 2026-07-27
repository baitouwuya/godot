/**************************************************************************/
/*  test_mcp_node_structure_provider.cpp                                  */
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

#include "core/io/dir_access.h"
#include "core/mcp/mcp_tool_registry.h"
#include "editor/mcp/providers/mcp_node_structure_provider.h"
#include "scene/gui/container.h"
#include "scene/gui/control.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#include "mcp_test_undo_redo_action.h"

TEST_FORCE_LINK(test_mcp_node_structure_provider);

namespace TestMCPNodeStructureProvider {

class NodeStructureFixture {
public:
	NodeStructureFixture() {
		scene_root = memnew(Node);
		scene_root->set_name("SceneRoot");
		SceneTree::get_singleton()->get_root()->add_child(scene_root);
		provider = memnew(MCPNodeStructureProvider(scene_root, &undo_redo));
	}

	~NodeStructureFixture() {
		if (provider) {
			provider->unregister_tools();
			memdelete(provider);
		}
		undo_redo.clear_history();
		memdelete(scene_root);
	}

	Node *add_editable(Node *p_parent, const StringName &p_name) {
		Node *node = memnew(Node);
		node->set_name(p_name);
		p_parent->add_child(node);
		node->set_owner(scene_root);
		return node;
	}

	Node *scene_root = nullptr;
	MCPTestUndoRedoAction undo_redo;
	MCPToolRegistry registry;
	MCPNodeStructureProvider *provider = nullptr;
};

class ScopedProjectResourcePath {
public:
	ScopedProjectResourcePath() {
		previous_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_valid()) {
			TestProjectSettingsInternalsAccessor::resource_path() = filesystem->get_current_dir();
		}
	}

	~ScopedProjectResourcePath() {
		TestProjectSettingsInternalsAccessor::resource_path() = previous_resource_path;
	}

private:
	String previous_resource_path;
};

static MCPToolRegistry::CallResult _call(MCPToolRegistry &p_registry, const StringName &p_name, const Dictionary &p_arguments) {
	MCPToolCallContext context;
	return p_registry.call_tool(p_name, p_arguments, context);
}

static Dictionary _success(const MCPToolRegistry::CallResult &p_result) {
	INFO(p_result.result);
	REQUIRE(p_result.status == MCPToolRegistry::CALL_OK);
	REQUIRE_FALSE(bool(p_result.result.get("isError", false)));
	return p_result.result.get("structuredContent", Dictionary());
}

TEST_CASE("[MCP][Provider] Node structure tools expose strict schemas") {
	MCPToolRegistry registry;
	MCPNodeStructureProvider *provider = memnew(MCPNodeStructureProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 6);
	CHECK(names[0] == "godot.node.delete");
	CHECK(names[1] == "godot.node.rename");
	CHECK(names[2] == "godot.node.reparent");
	CHECK(names[3] == "godot.node.move");
	CHECK(names[4] == "godot.node.duplicate");
	CHECK(names[5] == "godot.node.instantiate_scene");
	const Array definitions = registry.get_tool_definitions();
	const Dictionary delete_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK_FALSE(bool(delete_output.get("additionalProperties", true)));
	CHECK(Dictionary(delete_output.get("properties", Dictionary())).has("deleted"));
	const Dictionary rename_output = Dictionary(definitions[1]).get("outputSchema", Dictionary());
	CHECK_FALSE(bool(rename_output.get("additionalProperties", true)));
	const Dictionary rename_properties = rename_output.get("properties", Dictionary());
	CHECK(rename_properties.has("path"));
	CHECK(rename_properties.has("previousPath"));
	CHECK(rename_properties.has("parentPath"));
	CHECK(rename_properties.has("index"));

	Dictionary invalid;
	invalid["path"] = "Child";
	invalid["unknown"] = true;
	const MCPToolRegistry::CallResult result = _call(registry, "godot.node.delete", invalid);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(result.result.get("isError", false)));

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
}

TEST_CASE("[SceneTree][MCP][Provider] Node rename reparent and move are undoable") {
	NodeStructureFixture fixture;
	Node *left = fixture.add_editable(fixture.scene_root, "Left");
	Node *right = fixture.add_editable(fixture.scene_root, "Right");
	Node *child = fixture.add_editable(left, "Child");
	fixture.add_editable(right, "Existing");
	REQUIRE(fixture.provider->register_tools(&fixture.registry) == OK);

	Dictionary rename_arguments;
	rename_arguments["path"] = "Left/Child";
	rename_arguments["name"] = "Renamed";
	Dictionary content = _success(_call(fixture.registry, "godot.node.rename", rename_arguments));
	CHECK(content.get("path", String()) == "Left/Renamed");
	CHECK(content.get("previousPath", String()) == "Left/Child");
	CHECK(child->get_name() == "Renamed");
	REQUIRE(fixture.undo_redo.undo());
	CHECK(child->get_name() == "Child");
	REQUIRE(fixture.undo_redo.redo());
	CHECK(child->get_name() == "Renamed");

	Dictionary reparent_arguments;
	reparent_arguments["path"] = "Left/Renamed";
	reparent_arguments["parentPath"] = "Right";
	reparent_arguments["index"] = 0;
	content = _success(_call(fixture.registry, "godot.node.reparent", reparent_arguments));
	CHECK(content.get("path", String()) == "Right/Renamed");
	CHECK(child->get_parent() == right);
	CHECK(child->get_index(false) == 0);
	CHECK(child->get_owner() == fixture.scene_root);
	REQUIRE(fixture.undo_redo.undo());
	CHECK(child->get_parent() == left);
	CHECK(child->get_index(false) == 0);
	REQUIRE(fixture.undo_redo.redo());
	CHECK(child->get_parent() == right);

	Dictionary move_arguments;
	move_arguments["path"] = "Right/Renamed";
	move_arguments["index"] = -1;
	content = _success(_call(fixture.registry, "godot.node.move", move_arguments));
	CHECK(int(content.get("index", -1)) == 1);
	CHECK(child->get_index(false) == 1);
	REQUIRE(fixture.undo_redo.undo());
	CHECK(child->get_index(false) == 0);
}

TEST_CASE("[SceneTree][MCP][Provider] Node duplicate and delete preserve ownership and lifetime") {
	NodeStructureFixture fixture;
	Node *source = fixture.add_editable(fixture.scene_root, "Source");
	Node *source_nested = fixture.add_editable(source, "Nested");
	source_nested->set_owner(source);
	REQUIRE(fixture.provider->register_tools(&fixture.registry) == OK);

	Dictionary duplicate_arguments;
	duplicate_arguments["path"] = "Source";
	duplicate_arguments["name"] = "Clone";
	duplicate_arguments["index"] = 0;
	Dictionary content = _success(_call(fixture.registry, "godot.node.duplicate", duplicate_arguments));
	CHECK(content.get("path", String()) == "Clone");
	Node *clone = fixture.scene_root->get_node_or_null(NodePath("Clone"));
	REQUIRE(clone != nullptr);
	CHECK(clone->get_owner() == fixture.scene_root);
	Node *nested = clone->get_node_or_null(NodePath("Nested"));
	REQUIRE(nested != nullptr);
	CHECK(nested->get_owner() == fixture.scene_root);
	REQUIRE(fixture.undo_redo.undo());
	CHECK(clone->get_parent() == nullptr);
	REQUIRE(fixture.undo_redo.redo());
	CHECK(clone->get_parent() == fixture.scene_root);

	Dictionary delete_arguments;
	delete_arguments["path"] = "Clone";
	content = _success(_call(fixture.registry, "godot.node.delete", delete_arguments));
	const Dictionary deleted = content.get("deleted", Dictionary());
	CHECK(deleted.get("path", String()) == "Clone");
	CHECK(clone->get_parent() == nullptr);
	REQUIRE(fixture.undo_redo.undo());
	CHECK(clone->get_parent() == fixture.scene_root);
	CHECK(clone->get_owner() == fixture.scene_root);
	REQUIRE(fixture.undo_redo.redo());
	CHECK(clone->get_parent() == nullptr);
}

TEST_CASE("[SceneTree][MCP][Provider] Packed scenes instantiate with undo and reject cycles") {
	ScopedProjectResourcePath project_path;
	NodeStructureFixture fixture;
	REQUIRE(fixture.provider->register_tools(&fixture.registry) == OK);
	const String scene_path = "res://tests/editor/mcp/data/mcp_instance_scene.tscn";

	Dictionary arguments;
	arguments["scenePath"] = scene_path;
	arguments["name"] = "Instanced";
	arguments["index"] = 0;
	Dictionary content = _success(_call(fixture.registry, "godot.node.instantiate_scene", arguments));
	CHECK(content.get("path", String()) == "Instanced");
	Node *instance = fixture.scene_root->get_node_or_null(NodePath("Instanced"));
	REQUIRE(instance != nullptr);
	CHECK(instance->get_scene_file_path() == scene_path);
	CHECK(instance->get_owner() == fixture.scene_root);
	CHECK(instance->get_node_or_null(NodePath("Nested")) != nullptr);
	REQUIRE(fixture.undo_redo.undo());
	CHECK(instance->get_parent() == nullptr);
	REQUIRE(fixture.undo_redo.redo());
	CHECK(instance->get_parent() == fixture.scene_root);

	fixture.scene_root->set_scene_file_path(scene_path);
	arguments["name"] = "Cyclic";
	const MCPToolRegistry::CallResult cyclic_result = _call(fixture.registry, "godot.node.instantiate_scene", arguments);
	REQUIRE(cyclic_result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(cyclic_result.result.get("isError", false)));
	const Dictionary cyclic_content = cyclic_result.result.get("structuredContent", Dictionary());
	const Dictionary error = cyclic_content.get("error", Dictionary());
	CHECK(error.get("code", String()) == "CYCLIC_SCENE");
}

TEST_CASE("[SceneTree][MCP][Provider] Node structure operations reject roots and foreign nodes") {
	NodeStructureFixture fixture;
	REQUIRE(fixture.scene_root != nullptr);
	Node *foreign_parent = memnew(Node);
	fixture.scene_root->add_child(foreign_parent);
	REQUIRE(foreign_parent->get_parent() == fixture.scene_root);
	Node *foreign = memnew(Node);
	foreign_parent->add_child(foreign);
	REQUIRE(foreign->get_parent() == foreign_parent);
	REQUIRE(fixture.provider->register_tools(&fixture.registry) == OK);

	Dictionary delete_root;
	delete_root["path"] = ".";
	MCPToolRegistry::CallResult result = _call(fixture.registry, "godot.node.delete", delete_root);
	CHECK(bool(result.result.get("isError", false)));

	Dictionary rename_foreign;
	rename_foreign["path"] = String(fixture.scene_root->get_path_to(foreign));
	rename_foreign["name"] = "Changed";
	result = _call(fixture.registry, "godot.node.rename", rename_foreign);
	CHECK(bool(result.result.get("isError", false)));
}

TEST_CASE("[SceneTree][MCP][Provider] Unique node rename conflicts preserve the unique flag") {
	NodeStructureFixture fixture;
	Node *left = fixture.add_editable(fixture.scene_root, "Left");
	Node *right = fixture.add_editable(fixture.scene_root, "Right");
	Node *first = fixture.add_editable(left, "First");
	Node *second = fixture.add_editable(right, "Second");
	first->set_unique_name_in_owner(true);
	second->set_unique_name_in_owner(true);
	REQUIRE(fixture.provider->register_tools(&fixture.registry) == OK);

	Dictionary arguments;
	arguments["path"] = "Left/First";
	arguments["name"] = "Second";
	const MCPToolRegistry::CallResult result = _call(fixture.registry, "godot.node.rename", arguments);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(result.result.get("isError", false)));
	CHECK(first->get_name() == "First");
	CHECK(first->is_unique_name_in_owner());
}

TEST_CASE("[SceneTree][MCP][Provider] Reparent undo restores Control editor layout state") {
	NodeStructureFixture fixture;
	Node *source_parent = fixture.add_editable(fixture.scene_root, "Source");
	Container *container = memnew(Container);
	container->set_name("Container");
	fixture.scene_root->add_child(container);
	container->set_owner(fixture.scene_root);
	Control *control = memnew(Control);
	control->set_name("Control");
	source_parent->add_child(control);
	control->set_owner(fixture.scene_root);
	control->set_anchor(SIDE_LEFT, 0.25);
	control->set_anchor(SIDE_TOP, 0.35);
	control->set_offset(SIDE_LEFT, 12.0);
	control->set_offset(SIDE_TOP, 18.0);
	const Dictionary original_state = control->_edit_get_state();
	REQUIRE(fixture.provider->register_tools(&fixture.registry) == OK);

	Dictionary arguments;
	arguments["path"] = "Source/Control";
	arguments["parentPath"] = "Container";
	const Dictionary content = _success(_call(fixture.registry, "godot.node.reparent", arguments));
	CHECK(content.get("path", String()) == "Container/Control");
	control->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	REQUIRE(fixture.undo_redo.undo());
	CHECK(control->get_parent() == source_parent);
	CHECK(control->_edit_get_state() == original_state);
}

} // namespace TestMCPNodeStructureProvider
