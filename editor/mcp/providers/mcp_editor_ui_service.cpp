/**************************************************************************/
/*  mcp_editor_ui_service.cpp                                             */
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

#include "mcp_editor_ui_service.h"

Dictionary MCPEditorUIService::capture(const String &p_session_id, const SnapshotOptions &p_options) {
	MCPEditorUISnapshot snapshot = snapshot_store.create_snapshot(p_session_id);
	Dictionary result = snapshot_builder.build(p_options, snapshot);
	snapshot_store.replace_snapshot(snapshot);
	return result;
}

Error MCPEditorUIService::perform(const String &p_session_id, const String &p_snapshot_id, const String &p_target_id, const String &p_action, const Dictionary &p_arguments, Dictionary &r_result, String &r_error_code, String &r_error_message) {
	const MCPEditorUISnapshot *snapshot = snapshot_store.get_current_snapshot(p_session_id, p_snapshot_id);
	if (!snapshot) {
		r_error_code = "STALE_UI_SNAPSHOT";
		r_error_message = "The UI snapshot is stale or belongs to another MCP session.";
		return ERR_DOES_NOT_EXIST;
	}

	Error error = target_validator.validate_scope(*snapshot, snapshot_builder.resolve_scope_id(), r_error_code, r_error_message);
	if (error != OK) {
		return error;
	}

	const MCPEditorUITarget *target = nullptr;
	error = target_validator.find_target(*snapshot, p_target_id, target, r_error_code, r_error_message);
	if (error != OK) {
		return error;
	}
	error = target_validator.validate_action(*target, p_action, r_error_code, r_error_message);
	if (error != OK) {
		snapshot_store.release_session(p_session_id);
		return error;
	}
	error = target_validator.validate_current(*target, r_error_code, r_error_message);
	if (error != OK) {
		snapshot_store.release_session(p_session_id);
		return error;
	}
	error = target_validator.validate_available(*target, p_action, r_error_code, r_error_message);
	if (error != OK) {
		return error;
	}
	error = action_executor.execute(*target, p_action, p_arguments, r_error_code, r_error_message);
	if (error != OK) {
		return error;
	}

	snapshot_store.release_session(p_session_id);
	r_result["performed"] = true;
	r_result["action"] = p_action;
	r_result["targetId"] = p_target_id;
	r_result["snapshotInvalidated"] = true;
	return OK;
}

void MCPEditorUIService::release_session(const String &p_session_id) {
	snapshot_store.release_session(p_session_id);
}

void MCPEditorUIService::clear() {
	snapshot_store.clear();
}
