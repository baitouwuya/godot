/**************************************************************************/
/*  mcp_editor_ui_service.h                                               */
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

class Control;
class Node;
class TreeItem;
class Window;

class MCPEditorUIService {
public:
	struct SnapshotOptions {
		bool include_disabled = false;
		bool include_values = true;
		int max_depth = 32;
		int limit = 512;
	};

	Dictionary capture(const String &p_session_id, const SnapshotOptions &p_options);
	Error perform(const String &p_session_id, const String &p_snapshot_id, const String &p_target_id, const String &p_action, const Dictionary &p_arguments, Dictionary &r_result, String &r_error_code, String &r_error_message);
	void release_session(const String &p_session_id);
	void clear();

private:
	enum TargetKind {
		TARGET_CONTROL,
		TARGET_POPUP_ITEM,
		TARGET_OPTION_ITEM,
		TARGET_ITEM_LIST_ITEM,
		TARGET_TAB,
		TARGET_TREE_CELL,
		TARGET_TREE_BUTTON,
	};

	struct Target {
		TargetKind kind = TARGET_CONTROL;
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

	struct Snapshot {
		String id;
		String session_id;
		ObjectID scope_id;
		HashMap<String, Target> targets;
	};

	uint64_t next_snapshot_sequence = 1;
	HashMap<String, Snapshot> snapshots;
	HashMap<String, String> current_snapshot_by_session;

	Window *_resolve_scope() const;
	bool _belongs_to_editor(Node *p_node) const;
	String _make_target_id(const Snapshot &p_snapshot, const Target &p_target, const String &p_suffix) const;
	void _append_node(Node *p_node, int p_depth, const SnapshotOptions &p_options, Snapshot &r_snapshot, Array &r_items, bool &r_truncated);
	void _append_children(Node *p_node, const SnapshotOptions &p_options, Snapshot &r_snapshot, Array &r_items, bool &r_truncated);
	Dictionary _describe_target(const Target &p_target, const String &p_target_id, bool p_include_values) const;
	Error _perform_target(const Target &p_target, const String &p_action, const Dictionary &p_arguments, String &r_error_code, String &r_error_message);
	bool _target_is_current(const Target &p_target) const;
};
