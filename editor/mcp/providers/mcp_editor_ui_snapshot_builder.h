/**************************************************************************/
/*  mcp_editor_ui_snapshot_builder.h                                     */
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
/* "Software"), to deal in the Software without restriction, including    */
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

#include "mcp_editor_ui_types.h"

#include "core/variant/array.h"

class Node;
class Window;

class MCPEditorUISnapshotBuilder {
	Window *_resolve_scope() const;
	String _make_target_id(const MCPEditorUISnapshot &p_snapshot) const;
	Dictionary _describe_target(const MCPEditorUITarget &p_target, const String &p_target_id, bool p_include_values) const;
	void _append_children(Node *p_node, const MCPEditorUISnapshotOptions &p_options, MCPEditorUISnapshot &r_snapshot, Array &r_items, bool &r_truncated) const;
	void _append_node(Node *p_node, int p_depth, const MCPEditorUISnapshotOptions &p_options, MCPEditorUISnapshot &r_snapshot, Array &r_items, bool &r_truncated) const;

public:
	Dictionary build(const MCPEditorUISnapshotOptions &p_options, MCPEditorUISnapshot &r_snapshot) const;
	ObjectID resolve_scope_id() const;
};
