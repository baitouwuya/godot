/**************************************************************************/
/*  mcp_script_buffer.cpp                                                 */
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

#include "mcp_script_buffer.h"

#include "mcp_script_editor_gateway.h"
#include "mcp_script_path_resolver.h"
#include "mcp_script_revision.h"
#include "mcp_script_snapshot_reader.h"

#include "editor/script/script_editor_base.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/gui/code_edit.h"

namespace {

Error fail(const String &p_message, String *r_error, Error p_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

} // namespace

Error MCPScriptBuffer::normalize_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error) {
	return MCPScriptPathResolver::normalize_external_for_root(p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptBuffer::normalize_path(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error) {
	return MCPScriptPathResolver::normalize_external(p_path, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptBuffer::resolve_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error) {
	return MCPScriptPathResolver::resolve_external_for_root(p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptBuffer::resolve_path(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error) {
	return MCPScriptPathResolver::resolve_external(p_path, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptBuffer::resolve_script_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, bool &r_built_in, String *r_error) {
	return MCPScriptPathResolver::resolve_script_for_root(p_path, p_project_root, r_resource_path, r_absolute_path, r_built_in, r_error);
}

Error MCPScriptBuffer::resolve_script_path(const String &p_path, String &r_resource_path, String &r_absolute_path, bool &r_built_in, String *r_error) {
	return MCPScriptPathResolver::resolve_script(p_path, r_resource_path, r_absolute_path, r_built_in, r_error);
}

Error MCPScriptBuffer::read_authoritative_snapshot_for_root(const String &p_path, const String &p_project_root, Dictionary &r_snapshot, String *r_error) {
	return MCPScriptSnapshotReader::read_for_root(p_path, p_project_root, r_snapshot, r_error);
}

Error MCPScriptBuffer::read_authoritative_snapshot(const String &p_path, Dictionary &r_snapshot, String *r_error) {
	return MCPScriptSnapshotReader::read(p_path, r_snapshot, r_error);
}

Error MCPScriptBuffer::read_open_snapshots(Array &r_snapshots, String *r_error) {
	return MCPScriptSnapshotReader::read_open(r_snapshots, r_error);
}

Error MCPScriptBuffer::open(const String &p_path, MCPScriptBuffer &r_buffer, String *r_error) {
	return MCPScriptEditorGateway::open(p_path, r_buffer, r_error);
}

Error MCPScriptBuffer::open(const Ref<Script> &p_script, MCPScriptBuffer &r_buffer, String *r_error) {
	return MCPScriptEditorGateway::open(p_script, r_buffer, r_error);
}

void MCPScriptBuffer::apply_text_edit(CodeEdit *p_text_edit, const String &p_text) {
	ERR_FAIL_NULL(p_text_edit);
	p_text_edit->begin_complex_operation();
	p_text_edit->set_text(p_text);
	p_text_edit->end_complex_operation();
}

bool MCPScriptBuffer::is_valid() const {
	return !path.is_empty() && editor && text_editor && code_edit;
}

Dictionary MCPScriptBuffer::get_snapshot() const {
	Dictionary snapshot;
	if (!is_valid()) {
		return snapshot;
	}
	const String text = code_edit->get_text();
	snapshot["path"] = path;
	snapshot["text"] = text;
	snapshot["revision"] = int64_t(code_edit->get_version());
	snapshot["savedRevision"] = int64_t(code_edit->get_saved_version());
	snapshot["sha256"] = text.sha256_text();
	snapshot["unsaved"] = editor->is_unsaved();
	snapshot["sourceState"] = "editor";
	snapshot["sourceType"] = built_in ? "builtIn" : "external";
	if (built_in && !scene_path.is_empty()) {
		snapshot["scenePath"] = scene_path;
	}
	return snapshot;
}

Error MCPScriptBuffer::validate_expected_state(const Variant &p_expected_revision, const Variant &p_expected_sha256,
		Dictionary &r_current_state, String *r_error) const {
	r_current_state = Dictionary();
	if (!is_valid()) {
		return fail("The Script editor buffer is not available.", r_error, ERR_UNCONFIGURED);
	}
	return MCPScriptRevision::validate(code_edit->get_text(), code_edit->get_version(), p_expected_revision,
			p_expected_sha256, r_current_state, r_error);
}

Error MCPScriptBuffer::replace_text(const String &p_text, const Variant &p_expected_revision,
		const Variant &p_expected_sha256, bool &r_changed, Dictionary &r_current_state, String *r_error) {
	r_changed = false;
	r_current_state = Dictionary();
	if (r_error) {
		*r_error = String();
	}
	if (!is_valid()) {
		return fail("The Script editor buffer is not available.", r_error, ERR_UNCONFIGURED);
	}

	const String current_text = code_edit->get_text();
	Error err = MCPScriptRevision::validate(current_text, code_edit->get_version(), p_expected_revision,
			p_expected_sha256, r_current_state, r_error);
	if (err != OK) {
		return err;
	}
	if (current_text == p_text) {
		return OK;
	}

	apply_text_edit(code_edit, p_text);
	editor->apply_code();
	editor->validate_script();
	r_changed = true;
	return OK;
}

Error MCPScriptBuffer::save(String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!is_valid() || !ScriptEditor::get_singleton()) {
		return fail("The Script editor buffer is not available.", r_error, ERR_UNCONFIGURED);
	}
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	const Ref<Resource> resource = editor->get_edited_resource();
	if (resource.is_valid() && resource->is_built_in() && resource->get_path().get_slice("::", 0).is_empty()) {
		return fail("The built-in script cannot be saved until its scene has a file path.", r_error, ERR_FILE_CANT_WRITE);
	}
	ScriptEditorBase *previous_editor = script_editor->get_current_editor();
	const Ref<Resource> previous_resource = previous_editor ? previous_editor->get_edited_resource() : Ref<Resource>();
	if (resource.is_null() || !script_editor->edit(resource, false)) {
		return fail("The Script editor could not select the target buffer.", r_error, ERR_UNAVAILABLE);
	}
	if (script_editor->get_current_editor() != editor) {
		return fail("The target buffer could not become the current Script editor tab.", r_error, ERR_BUSY);
	}
	script_editor->save_current_script();
	const bool still_unsaved = editor->is_unsaved();
	if (previous_editor && previous_editor != editor && previous_resource.is_valid()) {
		script_editor->edit(previous_resource, false);
	}
	if (still_unsaved) {
		return fail("The Script editor did not save the target buffer.", r_error, ERR_FILE_CANT_WRITE);
	}
	return OK;
}
