/**************************************************************************/
/*  mcp_script_analysis_sync.cpp                                          */
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

#include "mcp_script_analysis_sync.h"

#include "mcp_script_buffer.h"
#include "mcp_tool_utils.h"

Error MCPScriptAnalysisSync::sync_snapshot(const Ref<MCPGDScriptSessionManager> &p_session_manager, const String &p_session_id, const Dictionary &p_snapshot, Array &r_diagnostics, String *r_error) {
	r_diagnostics.clear();
	if (r_error) {
		*r_error = String();
	}
	if (p_session_manager.is_null()) {
		if (r_error) {
			*r_error = "A GDScript analysis session manager is required.";
		}
		return ERR_UNCONFIGURED;
	}

	const Variant path_value = p_snapshot.get("path", Variant());
	const Variant text_value = p_snapshot.get("text", Variant());
	const Variant revision_value = p_snapshot.get("revision", Variant());
	if (path_value.get_type() != Variant::STRING || text_value.get_type() != Variant::STRING || revision_value.get_type() != Variant::INT) {
		if (r_error) {
			*r_error = "The authoritative Script buffer snapshot is incomplete.";
		}
		return ERR_INVALID_DATA;
	}

	const String path = path_value;
	const String text = text_value;
	const int64_t revision = revision_value;
	return p_session_manager->sync_document(p_session_id, path, text, revision, r_diagnostics, r_error);
}

Error MCPScriptAnalysisSync::sync_authoritative_path(const Ref<MCPGDScriptSessionManager> &p_session_manager, const String &p_session_id, const String &p_path, const String &p_project_root, Dictionary &r_snapshot, Array &r_diagnostics, String *r_error) {
	r_snapshot = Dictionary();
	r_diagnostics.clear();
	const Error read_error = MCPScriptBuffer::read_authoritative_snapshot_for_root(p_path, p_project_root, r_snapshot, r_error);
	if (read_error != OK) {
		return read_error;
	}
	return sync_snapshot(p_session_manager, p_session_id, r_snapshot, r_diagnostics, r_error);
}

Error MCPScriptAnalysisSync::sync_open_buffers(const Ref<MCPGDScriptSessionManager> &p_session_manager, const String &p_session_id, String *r_error) {
	Array snapshots;
	const Error read_error = MCPScriptBuffer::read_open_snapshots(snapshots, r_error);
	if (read_error != OK) {
		return read_error;
	}
	HashSet<String> open_paths;
	for (int i = 0; i < snapshots.size(); i++) {
		const Dictionary snapshot = snapshots[i];
		const Variant path_value = snapshot.get("path", Variant());
		if (path_value.get_type() != Variant::STRING || String(path_value).is_empty()) {
			if (r_error) {
				*r_error = "An open ScriptEditor snapshot has no path.";
			}
			return ERR_INVALID_DATA;
		}
		Array diagnostics;
		const Error sync_error = sync_snapshot(p_session_manager, p_session_id, snapshot, diagnostics, r_error);
		if (sync_error != OK) {
			return sync_error;
		}
		open_paths.insert(path_value);
	}
	return p_session_manager->reconcile_open_editor_documents(p_session_id, open_paths, r_error);
}

Error MCPScriptAnalysisSync::sync_saved_snapshot(const Ref<MCPGDScriptSessionManager> &p_session_manager, const String &p_session_id, Dictionary &r_snapshot, String *r_error) {
	const Variant unsaved_value = r_snapshot.get("unsaved", Variant());
	if (unsaved_value.get_type() != Variant::BOOL || bool(unsaved_value)) {
		if (r_error) {
			*r_error = "The Script editor still reports the saved snapshot as unsaved.";
		}
		return ERR_INVALID_DATA;
	}

	Array diagnostics;
	const Error sync_error = sync_snapshot(p_session_manager, p_session_id, r_snapshot, diagnostics, r_error);
	if (sync_error != OK) {
		return sync_error;
	}
	r_snapshot["saved"] = true;
	r_snapshot["diagnostics"] = diagnostics;
	return OK;
}

Dictionary MCPScriptAnalysisSync::make_failure(const Dictionary &p_snapshot, bool p_applied, bool p_changed, bool p_created, const String &p_message) {
	Dictionary details;
	details["applied"] = p_applied;
	details["changed"] = p_changed;
	details["current"] = p_snapshot.duplicate(true);
	if (p_created) {
		details["created"] = true;
	}
	return MCPToolUtils::make_error_result("ANALYSIS_SYNC_FAILED", p_message, details);
}
