/**************************************************************************/
/*  mcp_undo_redo_action.cpp                                              */
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

#include "mcp_undo_redo_action.h"

#include "mcp_scene_utils.h"

#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/main/node.h"

MCPEditorUndoRedoAction::MCPEditorUndoRedoAction(EditorUndoRedoManager *p_undo_redo) :
		undo_redo(p_undo_redo) {
}

Error MCPEditorUndoRedoAction::begin_action(const String &p_name, String *r_error) {
	return MCPSceneUtils::begin_undo_action(undo_redo, p_name, r_error);
}

void MCPEditorUndoRedoAction::add_do_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_do_methodp(p_object, p_method, p_args, p_argcount);
}

void MCPEditorUndoRedoAction::add_undo_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_undo_methodp(p_object, p_method, p_args, p_argcount);
}

void MCPEditorUndoRedoAction::add_do_property(Object *p_object, const StringName &p_property, const Variant &p_value) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_do_property(p_object, p_property, p_value);
}

void MCPEditorUndoRedoAction::add_undo_property(Object *p_object, const StringName &p_property, const Variant &p_value) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_undo_property(p_object, p_property, p_value);
}

void MCPEditorUndoRedoAction::add_do_reference(Object *p_object) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_do_reference(p_object);
}

void MCPEditorUndoRedoAction::add_undo_reference(Object *p_object) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_undo_reference(p_object);
}

bool MCPEditorUndoRedoAction::can_update_node_paths() const {
	return SceneTreeDock::get_singleton() != nullptr;
}

void MCPEditorUndoRedoAction::add_node_path_updates(Node *p_node, Node *p_new_parent, const StringName &p_new_name) {
	ERR_FAIL_NULL(p_node);
	SceneTreeDock *scene_tree_dock = SceneTreeDock::get_singleton();
	ERR_FAIL_NULL_MSG(scene_tree_dock, "The scene tree dock is required to update node path references.");

	HashMap<Node *, NodePath> path_renames;
	scene_tree_dock->fill_path_renames(p_node, p_new_parent, &path_renames);
	if (!p_new_name.is_empty()) {
		const NodePath *root_path = path_renames.getptr(p_node);
		ERR_FAIL_NULL(root_path);
		const int renamed_name_index = root_path->get_name_count() - 1;
		for (KeyValue<Node *, NodePath> &entry : path_renames) {
			Vector<StringName> names = entry.value.get_names();
			ERR_CONTINUE(renamed_name_index < 0 || renamed_name_index >= names.size());
			names.write[renamed_name_index] = p_new_name;
			entry.value = NodePath(names, true);
		}
	}
	scene_tree_dock->perform_node_renames(nullptr, &path_renames);
}

void MCPEditorUndoRedoAction::commit_action() {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->commit_action();
}
