/**************************************************************************/
/*  mcp_editor_ui_types.h                                                 */
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

#include "core/object/object_id.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

struct MCPEditorUISnapshotOptions {
	bool include_disabled = false;
	bool include_values = true;
	int max_depth = 32;
	int limit = 512;
};

enum MCPEditorUITargetKind {
	MCP_TARGET_CONTROL,
	MCP_TARGET_POPUP_ITEM,
	MCP_TARGET_OPTION_ITEM,
	MCP_TARGET_ITEM_LIST_ITEM,
	MCP_TARGET_TAB,
	MCP_TARGET_TREE_CELL,
	MCP_TARGET_TREE_BUTTON,
};

struct MCPEditorUITarget {
	MCPEditorUITargetKind kind = MCP_TARGET_CONTROL;
	ObjectID object_id;
	ObjectID auxiliary_object_id;
	int index = -1;
	int column = -1;
	int button = -1;
	int item_id = -1;
	String fingerprint;
	String name_fingerprint;
	Variant metadata;
	PackedStringArray actions;
};

struct MCPEditorUISnapshot {
	String id;
	String session_id;
	ObjectID scope_id;
	HashMap<String, MCPEditorUITarget> targets;
};
