/**************************************************************************/
/*  mcp_scene_utils.cpp                                                   */
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

#include "mcp_scene_utils.h"

#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/main/node.h"

namespace MCPSceneUtils {

static Error _fail(const String &p_message, String *r_error, Error p_error = ERR_INVALID_PARAMETER) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

static Dictionary _make_tree_entry(Node *p_scene_root, Node *p_node, int p_depth, int p_max_depth, bool p_include_internal_children) {
	Dictionary entry = make_node_summary(p_scene_root, p_node, p_include_internal_children);
	Array children;
	const int child_count = p_node->get_child_count(p_include_internal_children);
	if (p_max_depth < 0 || p_depth < p_max_depth) {
		for (int i = 0; i < child_count; i++) {
			children.push_back(_make_tree_entry(p_scene_root, p_node->get_child(i, p_include_internal_children), p_depth + 1,
					p_max_depth, p_include_internal_children));
		}
	} else if (child_count > 0) {
		entry["truncated"] = true;
	}
	entry["children"] = children;
	return entry;
}

Node *get_edited_scene_root() {
	EditorInterface *editor_interface = EditorInterface::get_singleton();
	return editor_interface ? editor_interface->get_edited_scene_root() : nullptr;
}

bool is_node_in_scene(Node *p_scene_root, Node *p_node) {
	return p_scene_root && p_node && (p_scene_root == p_node || p_scene_root->is_ancestor_of(p_node));
}

bool is_node_editable(Node *p_scene_root, Node *p_node) {
	return is_node_in_scene(p_scene_root, p_node) && (p_node == p_scene_root || p_node->get_owner() == p_scene_root);
}

Node *find_node(Node *p_scene_root, const String &p_path) {
	if (!p_scene_root) {
		return nullptr;
	}
	if (p_path.is_empty() || p_path == "." || p_path == "/" || p_path == String(p_scene_root->get_name())) {
		return p_scene_root;
	}

	String lookup_path = p_path;
	const String root_name_prefix = String(p_scene_root->get_name()) + "/";
	if (lookup_path.begins_with(root_name_prefix)) {
		lookup_path = lookup_path.trim_prefix(root_name_prefix);
	}
	const NodePath node_path = lookup_path;
	if (node_path.get_subname_count() > 0) {
		return nullptr;
	}

	Node *node = p_scene_root->get_node_or_null(node_path);
	return is_node_in_scene(p_scene_root, node) ? node : nullptr;
}

String get_relative_path(Node *p_scene_root, Node *p_node) {
	if (!is_node_in_scene(p_scene_root, p_node)) {
		return String();
	}
	return p_scene_root == p_node ? "." : String(p_scene_root->get_path_to(p_node));
}

Dictionary make_node_summary(Node *p_scene_root, Node *p_node, bool p_include_internal_children) {
	Dictionary summary;
	if (!is_node_in_scene(p_scene_root, p_node)) {
		return summary;
	}

	summary["path"] = get_relative_path(p_scene_root, p_node);
	summary["name"] = String(p_node->get_name());
	summary["type"] = p_node->get_class();
	summary["childCount"] = p_node->get_child_count(p_include_internal_children);
	summary["editable"] = is_node_editable(p_scene_root, p_node);
	summary["internal"] = p_node->is_internal();
	Node *owner = p_node->get_owner();
	summary["owner"] = is_node_in_scene(p_scene_root, owner) ? get_relative_path(p_scene_root, owner) : String();
	if (!p_node->get_scene_file_path().is_empty()) {
		summary["sceneFilePath"] = p_node->get_scene_file_path();
	}
	return summary;
}

Error make_tree(Node *p_scene_root, Node *p_tree_root, int p_max_depth, bool p_include_internal_children, Dictionary &r_tree, String *r_error) {
	r_tree = Dictionary();
	if (r_error) {
		*r_error = String();
	}
	if (!is_node_in_scene(p_scene_root, p_tree_root)) {
		return _fail("Tree root is not part of the edited scene.", r_error);
	}
	if (p_max_depth < -1 || p_max_depth > 64) {
		return _fail("Maximum depth must be between -1 and 64.", r_error);
	}

	const int effective_max_depth = p_max_depth < 0 ? 64 : p_max_depth;
	r_tree = _make_tree_entry(p_scene_root, p_tree_root, 0, effective_max_depth, p_include_internal_children);
	return OK;
}

Error begin_undo_action(EditorUndoRedoManager *p_undo_redo, const String &p_name, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}

	int history_id = EditorUndoRedoManager::GLOBAL_HISTORY;
	if (EditorNode::get_singleton()) {
		const int scene_history_id = EditorNode::get_editor_data().get_current_edited_scene_history_id();
		if (scene_history_id > 0) {
			history_id = scene_history_id;
		}
	}
	p_undo_redo->create_action_for_history(p_name, history_id);
	p_undo_redo->force_fixed_history();
	return OK;
}

} // namespace MCPSceneUtils
