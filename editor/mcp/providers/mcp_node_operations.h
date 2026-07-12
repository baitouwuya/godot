/**************************************************************************/
/*  mcp_node_operations.h                                                 */
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
#include "core/object/object.h"

class MCPUndoRedoAction;
class Node;
class Script;

namespace MCPNodeOperations {

bool find_property_info(Node *p_node, const StringName &p_property, PropertyInfo &r_info);
Error create_node(Node *p_scene_root, Node *p_parent, const StringName &p_type, const String &p_name,
		MCPUndoRedoAction *p_undo_redo, Node *&r_node, String *r_error = nullptr);
Error set_property(Node *p_scene_root, Node *p_node, const StringName &p_property, const Variant &p_value,
		MCPUndoRedoAction *p_undo_redo, String *r_error = nullptr);
Error attach_script(Node *p_scene_root, Node *p_node, const Ref<Script> &p_script,
		MCPUndoRedoAction *p_undo_redo, String *r_error = nullptr);

} // namespace MCPNodeOperations
