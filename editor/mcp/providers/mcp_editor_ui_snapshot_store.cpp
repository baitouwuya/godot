/**************************************************************************/
/*  mcp_editor_ui_snapshot_store.cpp                                     */
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

#include "mcp_editor_ui_snapshot_store.h"

MCPEditorUISnapshot MCPEditorUISnapshotStore::create_snapshot(const String &p_session_id) {
	MCPEditorUISnapshot snapshot;
	snapshot.id = "ui-" + String::num_uint64(next_snapshot_sequence++);
	snapshot.session_id = p_session_id;
	return snapshot;
}

void MCPEditorUISnapshotStore::replace_snapshot(const MCPEditorUISnapshot &p_snapshot) {
	if (current_snapshot_by_session.has(p_snapshot.session_id)) {
		snapshots.erase(current_snapshot_by_session[p_snapshot.session_id]);
	}
	current_snapshot_by_session[p_snapshot.session_id] = p_snapshot.id;
	snapshots[p_snapshot.id] = p_snapshot;
}

const MCPEditorUISnapshot *MCPEditorUISnapshotStore::get_current_snapshot(const String &p_session_id, const String &p_snapshot_id) const {
	const MCPEditorUISnapshot *snapshot = snapshots.getptr(p_snapshot_id);
	const String *current_snapshot = current_snapshot_by_session.getptr(p_session_id);
	if (!snapshot || snapshot->session_id != p_session_id || !current_snapshot || *current_snapshot != p_snapshot_id) {
		return nullptr;
	}
	return snapshot;
}

void MCPEditorUISnapshotStore::release_session(const String &p_session_id) {
	if (current_snapshot_by_session.has(p_session_id)) {
		snapshots.erase(current_snapshot_by_session[p_session_id]);
		current_snapshot_by_session.erase(p_session_id);
	}
}

void MCPEditorUISnapshotStore::clear() {
	snapshots.clear();
	current_snapshot_by_session.clear();
}
