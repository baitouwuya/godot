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

#include "mcp_scene_tool_utils.h"
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

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.scene.open", "Open a project scene in the editor.",
				MCPSceneToolUtils::open_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPSceneProvider::open), MCPSceneToolUtils::path_result_schema("opened", "Whether the scene was opened.") },
		{ "godot.scene.get_tree", "Get the current edited scene tree.",
				MCPSceneToolUtils::tree_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPSceneProvider::get_tree), MCPSceneToolUtils::tree_output_schema() },
		{ "godot.scene.get_selection", "Get nodes selected in the editor.",
				MCPToolUtils::make_object_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPSceneProvider::get_selection), MCPSceneToolUtils::selection_output_schema() },
		{ "godot.scene.save", "Explicitly save the current edited scene.",
				MCPSceneToolUtils::save_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPSceneProvider::save), MCPSceneToolUtils::path_result_schema("saved", "Whether the scene was saved.") },
	};
	const Error err = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (err != OK) {
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
	String path;
	String argument_error;
	if (!MCPSceneToolUtils::parse_open_path(p_arguments, path, argument_error)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}

	String scene_path;
	String absolute_path;
	String path_error;
	if (MCPSceneToolUtils::resolve_scene_file_path(path, scene_path, absolute_path, &path_error) != OK) {
		return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
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
	MCPSceneToolUtils::TreeOptions options;
	String argument_error;
	if (!MCPSceneToolUtils::parse_tree_options(p_arguments, options, argument_error)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}

	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *tree_root = MCPSceneUtils::find_node(scene_root, options.root_path);
	if (!tree_root) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + options.root_path);
	}

	Dictionary tree;
	String tree_error;
	const Error err = MCPSceneUtils::make_tree(scene_root, tree_root, options.max_depth, options.include_internal, tree, &tree_error);
	if (err != OK) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", tree_error);
	}

	Dictionary result;
	result["scenePath"] = scene_root->get_scene_file_path();
	result["root"] = tree;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPSceneProvider::get_selection(const Dictionary &, const Dictionary &) {
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
	String save_path;
	String argument_error;
	if (!MCPSceneToolUtils::parse_save_path(p_arguments, save_path, argument_error)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}

	EditorInterface *editor_interface = EditorInterface::get_singleton();
	Node *scene_root = editor_interface ? editor_interface->get_edited_scene_root() : nullptr;
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}

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
		const Error path_result = MCPSceneToolUtils::resolve_scene_file_path(
				save_path, normalized_save_path, absolute_path, &path_error);
		if (path_result != OK) {
			return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
		}
		save_path = normalized_save_path;
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
