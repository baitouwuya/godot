/**************************************************************************/
/*  mcp_workspace_edit_transaction.cpp                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
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

#include "mcp_workspace_edit_transaction.h"

#include "mcp_script_analysis_sync.h"
#include "mcp_script_buffer.h"
#include "mcp_workspace_edit_planner.h"

#include "modules/gdscript/language_server/gdscript_workspace.h"

namespace {

struct DocumentChange {
	String path;
	Array edits;
	Dictionary expectation;
	Dictionary original;
	String new_text;
	MCPScriptBuffer buffer;
	bool changed = false;
};

struct DocumentChangeComparator {
	bool operator()(const DocumentChange &p_left, const DocumentChange &p_right) const {
		return p_left.path < p_right.path;
	}
};

static Error _fail(const String &p_code, const String &p_message, String &r_error_code, String &r_error, Error p_error = ERR_INVALID_DATA) {
	r_error_code = p_code;
	r_error = p_message;
	return p_error;
}

static Dictionary _snapshot_without_text(const Dictionary &p_snapshot) {
	Dictionary result = p_snapshot.duplicate(true);
	result.erase("text");
	return result;
}

static Error _normalize_expectations(const Array &p_expectations, HashMap<String, Dictionary> &r_expectations,
		String &r_error_code, String &r_error) {
	if (p_expectations.is_empty() || p_expectations.size() > 256) {
		return _fail("INVALID_ARGUMENTS", "documents must contain between 1 and 256 expectations.", r_error_code, r_error);
	}
	for (int i = 0; i < p_expectations.size(); i++) {
		if (p_expectations[i].get_type() != Variant::DICTIONARY) {
			return _fail("INVALID_ARGUMENTS", vformat("documents[%d] must be an object.", i), r_error_code, r_error);
		}
		const Dictionary expectation = p_expectations[i];
		const Variant path_value = expectation.get("path", Variant());
		if (path_value.get_type() != Variant::STRING || String(path_value).is_empty()) {
			return _fail("INVALID_ARGUMENTS", vformat("documents[%d].path must be a non-empty string.", i), r_error_code, r_error);
		}
		String resource_path;
		String absolute_path;
		bool built_in = false;
		String path_error;
		if (MCPScriptBuffer::resolve_script_path(path_value, resource_path, absolute_path, built_in, &path_error) != OK) {
			return _fail("INVALID_ARGUMENTS", path_error, r_error_code, r_error);
		}
		if (r_expectations.has(resource_path)) {
			return _fail("INVALID_ARGUMENTS", "Duplicate document expectation: " + resource_path, r_error_code, r_error);
		}
		if (!expectation.has("expected_revision") && !expectation.has("expected_sha256")) {
			return _fail("INVALID_ARGUMENTS", "Each document requires expected_revision or expected_sha256: " + resource_path, r_error_code, r_error);
		}
		r_expectations.insert(resource_path, expectation);
	}
	return OK;
}

} // namespace

Error MCPWorkspaceEditTransaction::apply(const Dictionary &p_edit, const Array &p_expectations, const Ref<GDScriptWorkspace> &p_workspace,
		const Ref<MCPGDScriptSessionManager> &p_session_manager, const String &p_session_id,
		Dictionary &r_result, String &r_error_code, String &r_error) {
	r_result = Dictionary();
	r_error_code = String();
	r_error = String();
	if (p_workspace.is_null() || p_session_manager.is_null()) {
		return _fail("ANALYSIS_UNAVAILABLE", "The GDScript workspace or session manager is unavailable.", r_error_code, r_error, ERR_UNCONFIGURED);
	}
	for (const KeyValue<Variant, Variant> &entry : p_edit) {
		if (entry.key.get_type() != Variant::STRING || String(entry.key) != "changes") {
			return _fail("UNSUPPORTED_WORKSPACE_EDIT", "Only WorkspaceEdit.changes is supported; documentChanges, annotations, and unknown fields are rejected.", r_error_code, r_error);
		}
	}
	const Variant changes_value = p_edit.get("changes", Variant());
	if (changes_value.get_type() != Variant::DICTIONARY) {
		return _fail("INVALID_WORKSPACE_EDIT", "WorkspaceEdit.changes must be an object.", r_error_code, r_error);
	}
	const Dictionary changes = changes_value;
	if (changes.is_empty() || changes.size() > 256) {
		return _fail("INVALID_WORKSPACE_EDIT", "WorkspaceEdit.changes must contain between 1 and 256 documents.", r_error_code, r_error);
	}

	HashMap<String, Dictionary> expectations;
	Error error = _normalize_expectations(p_expectations, expectations, r_error_code, r_error);
	if (error != OK) {
		return error;
	}

	int total_edit_count = 0;
	uint64_t total_source_bytes = 0;
	uint64_t total_new_text_bytes = 0;
	Vector<DocumentChange> documents;
	documents.reserve(changes.size());
	for (const KeyValue<Variant, Variant> &entry : changes) {
		if (entry.key.get_type() != Variant::STRING || entry.value.get_type() != Variant::ARRAY) {
			return _fail("INVALID_WORKSPACE_EDIT", "WorkspaceEdit changes must map file URI strings to text edit arrays.", r_error_code, r_error);
		}
		const String uri = entry.key;
		const String resolved_path = p_workspace->get_file_path(uri);
		String resource_path;
		String absolute_path;
		bool built_in = false;
		String path_error;
		if (resolved_path.is_empty() || MCPScriptBuffer::resolve_script_path(resolved_path, resource_path, absolute_path, built_in, &path_error) != OK) {
			return _fail("INVALID_WORKSPACE_EDIT", path_error.is_empty() ? "WorkspaceEdit contains an unsupported URI: " + uri : path_error, r_error_code, r_error);
		}
		const Dictionary *expectation = expectations.getptr(resource_path);
		if (!expectation) {
			return _fail("MISSING_EXPECTATION", "WorkspaceEdit document has no revision expectation: " + resource_path, r_error_code, r_error);
		}

		DocumentChange document;
		document.path = resource_path;
		document.edits = entry.value;
		document.expectation = *expectation;
		error = MCPScriptBuffer::open(resource_path, document.buffer, &r_error);
		if (error != OK) {
			return _fail("SCRIPT_OPEN_FAILED", r_error, r_error_code, r_error, error);
		}
		document.original = document.buffer.get_snapshot();
		total_source_bytes += String(document.original.get("text", String())).to_utf8_buffer().size();
		total_edit_count += document.edits.size();
		for (const Variant &edit_value : document.edits) {
			if (edit_value.get_type() == Variant::DICTIONARY) {
				const Variant new_text = Dictionary(edit_value).get("newText", Variant());
				if (new_text.get_type() == Variant::STRING) {
					total_new_text_bytes += String(new_text).to_utf8_buffer().size();
				}
			}
		}
		if (total_edit_count > 16384 || total_source_bytes > 16 * 1024 * 1024 || total_new_text_bytes > 16 * 1024 * 1024) {
			return _fail("WORKSPACE_EDIT_TOO_LARGE", "WorkspaceEdit exceeds the total edit, source text, or replacement text budget.", r_error_code, r_error, ERR_OUT_OF_MEMORY);
		}
		Dictionary current_state;
		error = document.buffer.validate_expected_state(document.expectation.get("expected_revision", Variant()),
				document.expectation.get("expected_sha256", Variant()), current_state, &r_error);
		if (error == ERR_BUSY) {
			r_result = _snapshot_without_text(document.original);
			return _fail("stale_revision", r_error, r_error_code, r_error, error);
		}
		if (error != OK) {
			return _fail("INVALID_ARGUMENTS", r_error, r_error_code, r_error, error);
		}
		error = MCPWorkspaceEditPlanner::apply(document.original.get("text", String()), document.edits, document.new_text, &r_error);
		if (error != OK) {
			return _fail("INVALID_WORKSPACE_EDIT", r_error, r_error_code, r_error, error);
		}
		documents.push_back(document);
		expectations.erase(resource_path);
	}
	if (!expectations.is_empty()) {
		return _fail("UNUSED_EXPECTATION", "documents contains an expectation not present in WorkspaceEdit.changes.", r_error_code, r_error);
	}
	documents.sort_custom<DocumentChangeComparator>();

	int applied_count = 0;
	for (DocumentChange &document : documents) {
		bool changed = false;
		Dictionary current_state;
		error = document.buffer.replace_text(document.new_text, document.original.get("revision", Variant()), Variant(), changed, current_state, &r_error);
		if (error != OK) {
			r_result["applied"] = applied_count > 0;
			r_result["partiallyApplied"] = applied_count > 0;
			r_result["appliedDocumentCount"] = applied_count;
			return _fail(error == ERR_BUSY ? "stale_revision" : "WORKSPACE_EDIT_FAILED", r_error, r_error_code, r_error, error);
		}
		document.changed = changed;
		applied_count++;
	}

	Array results;
	for (DocumentChange &document : documents) {
		Dictionary snapshot = document.buffer.get_snapshot();
		Array diagnostics;
		error = MCPScriptAnalysisSync::sync_snapshot(p_session_manager, p_session_id, snapshot, &diagnostics, &r_error);
		if (error != OK) {
			r_result["applied"] = true;
			r_result["analysisSynchronized"] = false;
			return _fail("ANALYSIS_SYNC_FAILED", r_error, r_error_code, r_error, error);
		}
		Dictionary item = _snapshot_without_text(snapshot);
		item["changed"] = document.changed;
		item["editCount"] = document.edits.size();
		item["diagnostics"] = diagnostics;
		results.push_back(item);
	}

	r_result["applied"] = true;
	r_result["analysisSynchronized"] = true;
	r_result["saved"] = false;
	r_result["documentCount"] = documents.size();
	r_result["documents"] = results;
	return OK;
}
