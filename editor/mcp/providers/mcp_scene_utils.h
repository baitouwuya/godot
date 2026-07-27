/**************************************************************************/
/*  mcp_scene_utils.h                                                     */
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

#pragma once

#include "core/error/error_list.h"
#include "core/variant/dictionary.h"

class EditorUndoRedoManager;
class Node;

namespace MCPSceneUtils {

Node *get_edited_scene_root();
Node *find_node(Node *p_scene_root, const String &p_path);
String get_relative_path(Node *p_scene_root, Node *p_node);
bool is_node_in_scene(Node *p_scene_root, Node *p_node);
bool is_node_editable(Node *p_scene_root, Node *p_node);
Dictionary make_node_summary_schema_properties();
Dictionary make_node_summary_schema(bool p_allow_additional_properties = true);
Dictionary make_node_summary(Node *p_scene_root, Node *p_node, bool p_include_internal_children = false);
Error make_tree(Node *p_scene_root, Node *p_tree_root, int p_max_depth, bool p_include_internal_children, Dictionary &r_tree, String *r_error = nullptr);
Error begin_undo_action(EditorUndoRedoManager *p_undo_redo, const String &p_name, String *r_error = nullptr);

} // namespace MCPSceneUtils
