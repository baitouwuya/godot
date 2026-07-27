/**************************************************************************/
/*  mcp_node_structure_provider.cpp                                       */
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

#include "mcp_node_structure_provider.h"

#include "mcp_node_structure_operations.h"
#include "mcp_path_utils.h"
#include "mcp_scene_utils.h"
#include "mcp_tool_utils.h"
#include "mcp_undo_redo_action.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

namespace {

static Dictionary _path_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Node path relative to the edited scene root.");
	PackedStringArray required;
	required.push_back("path");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _rename_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Node path relative to the edited scene root.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "New node name.");
	PackedStringArray required;
	required.push_back("path");
	required.push_back("name");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _reparent_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Node path relative to the edited scene root.");
	properties["parentPath"] = MCPToolUtils::make_property_schema("string", "New parent path relative to the edited scene root.");
	properties["index"] = MCPToolUtils::make_property_schema("integer", "Optional 0-based child index. -1 appends the node.");
	properties["keepGlobalTransform"] = MCPToolUtils::make_property_schema("boolean", "Preserve the global transform when supported. Defaults to true.");
	PackedStringArray required;
	required.push_back("path");
	required.push_back("parentPath");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _move_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Node path relative to the edited scene root.");
	properties["index"] = MCPToolUtils::make_property_schema("integer", "0-based child index. -1 moves the node to the end.");
	PackedStringArray required;
	required.push_back("path");
	required.push_back("index");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _duplicate_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Node path to duplicate.");
	properties["parentPath"] = MCPToolUtils::make_property_schema("string", "Optional destination parent. Defaults to the source parent.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Optional name for the duplicate.");
	properties["index"] = MCPToolUtils::make_property_schema("integer", "Optional 0-based child index. -1 appends the duplicate.");
	PackedStringArray required;
	required.push_back("path");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _instantiate_schema() {
	Dictionary properties;
	properties["scenePath"] = MCPToolUtils::make_property_schema("string", "PackedScene path beginning with res://.");
	properties["parentPath"] = MCPToolUtils::make_property_schema("string", "Optional destination parent. Defaults to the edited scene root.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Optional name for the instantiated scene root.");
	properties["index"] = MCPToolUtils::make_property_schema("integer", "Optional 0-based child index. -1 appends the instance.");
	PackedStringArray required;
	required.push_back("scenePath");
	return MCPToolUtils::make_object_schema(properties, required);
}

static bool _get_required_string(const Dictionary &p_arguments, const StringName &p_name, String &r_value) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() != Variant::STRING || String(value).is_empty()) {
		return false;
	}
	r_value = value;
	return true;
}

static bool _get_optional_string(const Dictionary &p_arguments, const StringName &p_name, const String &p_default, String &r_value) {
	const Variant value = p_arguments.get(p_name, p_default);
	if (value.get_type() != Variant::STRING) {
		return false;
	}
	r_value = value;
	return true;
}

static bool _get_index(const Dictionary &p_arguments, int &r_index) {
	int64_t index = -1;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("index", -1), -1, INT32_MAX, index)) {
		return false;
	}
	r_index = int(index);
	return true;
}

static Dictionary _invalid_arguments(const String &p_message) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", p_message);
}

static Dictionary _operation_error(Error p_error, const String &p_message) {
	String code = "NODE_OPERATION_FAILED";
	if (p_error == ERR_UNAUTHORIZED) {
		code = "NODE_NOT_EDITABLE";
	} else if (p_error == ERR_ALREADY_EXISTS) {
		code = "NAME_CONFLICT";
	} else if (p_error == ERR_CYCLIC_LINK) {
		code = "CYCLIC_SCENE";
	} else if (p_error == ERR_INVALID_PARAMETER || p_error == ERR_INVALID_DATA) {
		code = "INVALID_ARGUMENTS";
	}
	return MCPToolUtils::make_error_result(code, p_message);
}

static Dictionary _make_result(Node *p_scene_root, Node *p_node, const String &p_previous_path = String()) {
	Dictionary result = MCPSceneUtils::make_node_summary(p_scene_root, p_node);
	if (!p_previous_path.is_empty()) {
		result["previousPath"] = p_previous_path;
	}
	if (p_node->get_parent()) {
		result["parentPath"] = MCPSceneUtils::get_relative_path(p_scene_root, p_node->get_parent());
		result["index"] = p_node->get_index(false);
	}
	return MCPToolUtils::make_success_result(result);
}

static Node *_find_required_node(Node *p_scene_root, const String &p_path, Dictionary &r_error) {
	Node *node = MCPSceneUtils::find_node(p_scene_root, p_path);
	if (!node) {
		r_error = MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + p_path);
	}
	return node;
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPNodeStructureProvider::MCPNodeStructureProvider(Node *p_scene_root, MCPUndoRedoAction *p_undo_redo) :
		scene_root_override(p_scene_root),
		undo_redo_override(p_undo_redo) {
}

MCPNodeStructureProvider::~MCPNodeStructureProvider() {
	unregister_tools();
}

Node *MCPNodeStructureProvider::_get_scene_root() const {
	return scene_root_override ? scene_root_override : MCPSceneUtils::get_edited_scene_root();
}

MCPUndoRedoAction *MCPNodeStructureProvider::_get_undo_redo(MCPEditorUndoRedoAction &r_editor_undo_redo) const {
	return undo_redo_override ? undo_redo_override : &r_editor_undo_redo;
}

Error MCPNodeStructureProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Node structure tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.node.delete", "Delete a node through editor undo/redo.",
				_path_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPNodeStructureProvider::delete_node) },
		{ "godot.node.rename", "Rename a node and update scene path references through editor undo/redo.",
				_rename_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPNodeStructureProvider::rename_node) },
		{ "godot.node.reparent", "Reparent a node and update scene path references through editor undo/redo.",
				_reparent_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPNodeStructureProvider::reparent_node) },
		{ "godot.node.move", "Move a node to a sibling index through editor undo/redo.",
				_move_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPNodeStructureProvider::move_node) },
		{ "godot.node.duplicate", "Duplicate an editable node subtree through editor undo/redo.",
				_duplicate_schema(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPNodeStructureProvider::duplicate_node) },
		{ "godot.node.instantiate_scene", "Instantiate a project PackedScene through editor undo/redo.",
				_instantiate_schema(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPNodeStructureProvider::instantiate_scene) },
	};
	const Error err = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (err != OK) {
		return err;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPNodeStructureProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPNodeStructureProvider::delete_node(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary validation_error;
	String path;
	if (!_get_required_string(p_arguments, "path", path)) {
		return _invalid_arguments("A non-empty string path is required.");
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = _find_required_node(scene_root, path, validation_error);
	if (!node) {
		return validation_error;
	}
	Dictionary deleted = MCPSceneUtils::make_node_summary(scene_root, node);
	deleted["parentPath"] = MCPSceneUtils::get_relative_path(scene_root, node->get_parent());
	deleted["index"] = node->get_index(false);
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeStructureOperations::delete_node(scene_root, node, _get_undo_redo(editor_undo_redo), &operation_error);
	if (err != OK) {
		return _operation_error(err, operation_error);
	}
	Dictionary result;
	result["deleted"] = deleted;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeStructureProvider::rename_node(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary validation_error;
	String path;
	String name;
	if (!_get_required_string(p_arguments, "path", path) || !_get_required_string(p_arguments, "name", name)) {
		return _invalid_arguments("Non-empty path and name strings are required.");
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = _find_required_node(scene_root, path, validation_error);
	if (!node) {
		return validation_error;
	}
	const String previous_path = MCPSceneUtils::get_relative_path(scene_root, node);
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeStructureOperations::rename_node(scene_root, node, name, _get_undo_redo(editor_undo_redo), &operation_error);
	return err == OK ? _make_result(scene_root, node, previous_path) : _operation_error(err, operation_error);
}

Dictionary MCPNodeStructureProvider::reparent_node(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary validation_error;
	String path;
	String parent_path;
	int index = -1;
	const Variant keep_value = p_arguments.get("keepGlobalTransform", true);
	if (!_get_required_string(p_arguments, "path", path) || !_get_required_string(p_arguments, "parentPath", parent_path) ||
			!_get_index(p_arguments, index) || keep_value.get_type() != Variant::BOOL) {
		return _invalid_arguments("path, parentPath, index, or keepGlobalTransform has an invalid type or value.");
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = _find_required_node(scene_root, path, validation_error);
	if (!node) {
		return validation_error;
	}
	Node *parent = _find_required_node(scene_root, parent_path, validation_error);
	if (!parent) {
		return validation_error;
	}
	const String previous_path = MCPSceneUtils::get_relative_path(scene_root, node);
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeStructureOperations::reparent_node(scene_root, node, parent, index, keep_value,
			_get_undo_redo(editor_undo_redo), &operation_error);
	return err == OK ? _make_result(scene_root, node, previous_path) : _operation_error(err, operation_error);
}

Dictionary MCPNodeStructureProvider::move_node(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary validation_error;
	String path;
	int index = -1;
	if (!_get_required_string(p_arguments, "path", path) || !_get_index(p_arguments, index)) {
		return _invalid_arguments("A non-empty path and integer index are required.");
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = _find_required_node(scene_root, path, validation_error);
	if (!node) {
		return validation_error;
	}
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeStructureOperations::move_node(scene_root, node, index, _get_undo_redo(editor_undo_redo), &operation_error);
	return err == OK ? _make_result(scene_root, node) : _operation_error(err, operation_error);
}

Dictionary MCPNodeStructureProvider::duplicate_node(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary validation_error;
	String path;
	String parent_path;
	String name;
	int index = -1;
	if (!_get_required_string(p_arguments, "path", path) || !_get_optional_string(p_arguments, "parentPath", String(), parent_path) ||
			!_get_optional_string(p_arguments, "name", String(), name) || !_get_index(p_arguments, index)) {
		return _invalid_arguments("path, parentPath, name, or index has an invalid type or value.");
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = _find_required_node(scene_root, path, validation_error);
	if (!node) {
		return validation_error;
	}
	Node *parent = parent_path.is_empty() ? node->get_parent() : _find_required_node(scene_root, parent_path, validation_error);
	if (!parent) {
		return parent_path.is_empty() ? MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Source node has no parent.") : validation_error;
	}
	Node *duplicate = nullptr;
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeStructureOperations::duplicate_node(scene_root, node, parent, name, index,
			_get_undo_redo(editor_undo_redo), duplicate, &operation_error);
	return err == OK ? _make_result(scene_root, duplicate) : _operation_error(err, operation_error);
}

Dictionary MCPNodeStructureProvider::instantiate_scene(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary validation_error;
	String scene_path;
	String parent_path;
	String name;
	int index = -1;
	if (!_get_required_string(p_arguments, "scenePath", scene_path) || !_get_optional_string(p_arguments, "parentPath", ".", parent_path) ||
			!_get_optional_string(p_arguments, "name", String(), name) || !_get_index(p_arguments, index)) {
		return _invalid_arguments("scenePath, parentPath, name, or index has an invalid type or value.");
	}
	String normalized_path;
	String absolute_path;
	String path_error;
	if (MCPPathUtils::resolve_project_file_path(scene_path, normalized_path, absolute_path, &path_error) != OK) {
		return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
	}
	if (!FileAccess::exists(absolute_path) || !ResourceLoader::exists(normalized_path, "PackedScene")) {
		return MCPToolUtils::make_error_result("SCENE_NOT_FOUND", "Packed scene was not found: " + normalized_path);
	}
	const Ref<PackedScene> packed_scene = ResourceLoader::load(normalized_path, "PackedScene");
	if (packed_scene.is_null()) {
		return MCPToolUtils::make_error_result("SCENE_LOAD_FAILED", "Resource is not a loadable PackedScene: " + normalized_path);
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *parent = _find_required_node(scene_root, parent_path, validation_error);
	if (!parent) {
		return validation_error;
	}
	Node *instance = nullptr;
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeStructureOperations::instantiate_scene(scene_root, parent, packed_scene, normalized_path, name, index,
			_get_undo_redo(editor_undo_redo), instance, &operation_error);
	return err == OK ? _make_result(scene_root, instance) : _operation_error(err, operation_error);
}
