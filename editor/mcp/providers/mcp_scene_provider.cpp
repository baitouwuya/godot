/**************************************************************************/
/*  mcp_scene_provider.cpp                                                */
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

#include "mcp_scene_provider.h"

#include "mcp_path_utils.h"
#include "mcp_scene_utils.h"
#include "mcp_tool_utils.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "scene/main/node.h"

namespace {

static Dictionary _property_schema(const String &p_type, const String &p_description) {
	Dictionary property;
	property["type"] = p_type;
	property["description"] = p_description;
	return property;
}

static Dictionary _object_schema(const Dictionary &p_properties = Dictionary()) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _tree_schema() {
	Dictionary root_path = _property_schema("string", "Optional node path used as the returned tree root.");
	root_path["default"] = ".";
	Dictionary max_depth = _property_schema("integer", "Maximum descendant depth. -1 uses the safety limit of 64.");
	max_depth["minimum"] = -1;
	max_depth["maximum"] = 64;
	max_depth["default"] = -1;
	Dictionary include_internal = _property_schema("boolean", "Include editor-internal child nodes.");
	include_internal["default"] = false;

	Dictionary properties;
	properties["rootPath"] = root_path;
	properties["maxDepth"] = max_depth;
	properties["includeInternal"] = include_internal;
	return _object_schema(properties);
}

static Dictionary _save_schema() {
	Dictionary path = _property_schema("string", "Optional res:// path for Save As. Existing scene path is used when omitted.");
	Dictionary properties;
	properties["path"] = path;
	return _object_schema(properties);
}

static Dictionary _open_schema() {
	Dictionary path = _property_schema("string", "Project scene path beginning with res://.");
	Dictionary properties;
	properties["path"] = path;
	Dictionary schema = _object_schema(properties);
	PackedStringArray required;
	required.push_back("path");
	schema["required"] = required;
	return schema;
}

static Dictionary _unknown_argument_error(const String &p_name) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown argument: " + p_name);
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPSceneProvider::MCPSceneProvider(Node *p_scene_root) :
		scene_root_override(p_scene_root) {
}

MCPSceneProvider::~MCPSceneProvider() {
	unregister_tools();
}

Node *MCPSceneProvider::_get_scene_root() const {
	return scene_root_override ? scene_root_override : MCPSceneUtils::get_edited_scene_root();
}

Error MCPSceneProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Scene tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	Error err = p_registry->register_tool(
			MCPToolUtils::make_tool_definition("godot.scene.open", "Open a project scene in the editor.", _open_schema(), MCPToolUtils::TOOL_DESTRUCTIVE),
			callable_mp(this, &MCPSceneProvider::open), this, r_error);
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.scene.get_tree", "Get the current edited scene tree.", _tree_schema(), MCPToolUtils::TOOL_READ_ONLY),
				callable_mp(this, &MCPSceneProvider::get_tree), this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.scene.get_selection", "Get nodes selected in the editor.", _object_schema(), MCPToolUtils::TOOL_READ_ONLY),
				callable_mp(this, &MCPSceneProvider::get_selection), this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.scene.save", "Explicitly save the current edited scene.", _save_schema(), MCPToolUtils::TOOL_DESTRUCTIVE),
				callable_mp(this, &MCPSceneProvider::save), this, r_error);
	}
	if (err != OK) {
		p_registry->unregister_tools_for_owner(this);
		return err;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPSceneProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPSceneProvider::open(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}
	const Variant path_value = p_arguments.get("path", Variant());
	if (path_value.get_type() != Variant::STRING || String(path_value).is_empty()) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string path is required.");
	}

	String scene_path;
	String absolute_path;
	String path_error;
	if (MCPPathUtils::resolve_project_file_path(path_value, scene_path, absolute_path, &path_error) != OK) {
		return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
	}
	const String extension = scene_path.get_extension().to_lower();
	if (extension != "tscn" && extension != "scn") {
		return MCPToolUtils::make_error_result("INVALID_PATH", "Scene path must use the .tscn or .scn extension.");
	}
	if (!FileAccess::exists(absolute_path)) {
		return MCPToolUtils::make_error_result("SCENE_NOT_FOUND", "Scene does not exist: " + scene_path);
	}

	EditorNode *editor_node = EditorNode::get_singleton();
	if (!editor_node) {
		return MCPToolUtils::make_error_result("EDITOR_UNAVAILABLE", "The editor is not available.");
	}
	if (editor_node->is_changing_scene()) {
		return MCPToolUtils::make_error_result("EDITOR_BUSY", "The editor is already changing scenes.");
	}
	const Error err = editor_node->open_scene(scene_path);
	if (err != OK) {
		return MCPToolUtils::make_error_result("OPEN_FAILED", error_names[err]);
	}

	Dictionary result;
	result["path"] = scene_path;
	result["opened"] = true;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPSceneProvider::get_tree(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("rootPath");
	allowed_arguments.push_back("maxDepth");
	allowed_arguments.push_back("includeInternal");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

	const Variant root_path_value = p_arguments.get("rootPath", ".");
	const Variant max_depth_value = p_arguments.get("maxDepth", -1);
	const Variant include_internal_value = p_arguments.get("includeInternal", false);
	int64_t max_depth = 0;
	if (root_path_value.get_type() != Variant::STRING ||
			!MCPToolUtils::try_get_json_integer(max_depth_value, -1, 64, max_depth) ||
			include_internal_value.get_type() != Variant::BOOL) {
		return MCPToolUtils::make_error_result(
				"INVALID_ARGUMENTS", "rootPath must be a string, maxDepth an integer from -1 to 64, and includeInternal a boolean.");
	}

	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *tree_root = MCPSceneUtils::find_node(scene_root, root_path_value);
	if (!tree_root) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + String(root_path_value));
	}

	Dictionary tree;
	String tree_error;
	const Error err = MCPSceneUtils::make_tree(scene_root, tree_root, int(max_depth), bool(include_internal_value), tree, &tree_error);
	if (err != OK) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", tree_error);
	}

	Dictionary result;
	result["scenePath"] = scene_root->get_scene_file_path();
	result["root"] = tree;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPSceneProvider::get_selection(const Dictionary &p_arguments, const Dictionary &) {
	if (!p_arguments.is_empty()) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "godot.scene.get_selection does not accept arguments.");
	}

	EditorInterface *editor_interface = EditorInterface::get_singleton();
	Node *scene_root = editor_interface ? editor_interface->get_edited_scene_root() : nullptr;
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	EditorSelection *selection = editor_interface->get_selection();
	if (!selection) {
		return MCPToolUtils::make_error_result("EDITOR_UNAVAILABLE", "The editor selection is not available.");
	}

	Array selected_nodes;
	for (Node *node : selection->get_full_selected_node_list()) {
		if (MCPSceneUtils::is_node_in_scene(scene_root, node)) {
			selected_nodes.push_back(MCPSceneUtils::make_node_summary(scene_root, node));
		}
	}
	Dictionary result;
	result["count"] = selected_nodes.size();
	result["nodes"] = selected_nodes;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPSceneProvider::save(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}
	const Variant path_value = p_arguments.get("path", String());
	if (path_value.get_type() != Variant::STRING) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "path must be a string when provided.");
	}

	EditorInterface *editor_interface = EditorInterface::get_singleton();
	Node *scene_root = editor_interface ? editor_interface->get_edited_scene_root() : nullptr;
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}

	String save_path = path_value;
	if (save_path.is_empty()) {
		save_path = scene_root->get_scene_file_path();
		if (save_path.is_empty()) {
			return MCPToolUtils::make_error_result("SAVE_PATH_REQUIRED", "The unsaved scene requires an explicit res:// path.");
		}
		const Error err = editor_interface->save_scene();
		if (err != OK) {
			return MCPToolUtils::make_error_result("SAVE_FAILED", error_names[err]);
		}
	} else {
		String normalized_save_path;
		String absolute_path;
		String path_error;
		const Error path_result = MCPPathUtils::resolve_project_file_path(
				save_path, normalized_save_path, absolute_path, &path_error);
		if (path_result != OK) {
			return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
		}
		save_path = normalized_save_path;
		const String extension = save_path.get_extension().to_lower();
		if (extension != "tscn" && extension != "scn") {
			return MCPToolUtils::make_error_result("INVALID_PATH", "Scene path must use the .tscn or .scn extension.");
		}
		if (!DirAccess::dir_exists_absolute(absolute_path.get_base_dir())) {
			return MCPToolUtils::make_error_result("DIRECTORY_NOT_FOUND", "Scene parent directory does not exist.");
		}
		editor_interface->save_scene_as(save_path, false);
		if (!FileAccess::exists(absolute_path)) {
			return MCPToolUtils::make_error_result("SAVE_FAILED", "Scene file was not created: " + save_path);
		}
	}
	if (editor_interface->get_unsaved_scenes().has(save_path)) {
		return MCPToolUtils::make_error_result("SAVE_FAILED", "The editor still reports the scene as unsaved: " + save_path);
	}

	Dictionary result;
	result["path"] = save_path;
	result["saved"] = true;
	return MCPToolUtils::make_success_result(result);
}
