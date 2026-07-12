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

#include "mcp_path_utils.h"
#include "mcp_script_revision.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"
#include "editor/gui/code_editor.h"
#include "editor/script/script_editor_base.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/gui/code_edit.h"

static Error _script_buffer_fail(const String &p_message, String *r_error, Error p_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

static Error _validate_external_script_path(Error p_resolve_error, String &r_resource_path, String &r_absolute_path, String *r_error) {
	if (p_resolve_error != OK) {
		return p_resolve_error;
	}
	if (r_resource_path.get_extension().to_lower() != "gd") {
		r_resource_path = String();
		r_absolute_path = String();
		return _script_buffer_fail("Only external .gd scripts are supported.", r_error, ERR_INVALID_PARAMETER);
	}
	return OK;
}

static ScriptEditorBase *_find_open_editor(ScriptEditor *p_script_editor, const String &p_path) {
	if (!p_script_editor) {
		return nullptr;
	}
	const Array open_editors = p_script_editor->call("get_open_script_editors");
	for (int i = 0; i < open_editors.size(); i++) {
		ScriptEditorBase *editor = Object::cast_to<ScriptEditorBase>(open_editors[i]);
		const Ref<Resource> resource = editor ? editor->get_edited_resource() : Ref<Resource>();
		if (resource.is_valid() && resource->get_path() == p_path) {
			return editor;
		}
	}
	return nullptr;
}

static bool _is_current_project_root(const String &p_project_root) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return false;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	return filesystem.is_valid() && filesystem->is_equivalent(p_project_root, project_settings->get_resource_path());
}

Error MCPScriptBuffer::normalize_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	const Error resolve_error = MCPPathUtils::resolve_project_file_path_for_root(
			p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
	return _validate_external_script_path(resolve_error, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptBuffer::normalize_path(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	const Error resolve_error = MCPPathUtils::resolve_project_file_path(p_path, r_resource_path, r_absolute_path, r_error);
	return _validate_external_script_path(resolve_error, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptBuffer::resolve_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error) {
	if (p_path.begins_with("res://")) {
		return normalize_path_for_root(p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
	}

	const String absolute_path = p_path.replace_char('\\', '/').simplify_path();
	if (!absolute_path.is_absolute_path() || absolute_path.contains("://")) {
		r_resource_path = String();
		r_absolute_path = String();
		return _script_buffer_fail("Script path must be a project res:// path or an absolute path inside the project.", r_error, ERR_INVALID_PARAMETER);
	}

	const String project_root = p_project_root.replace_char('\\', '/').simplify_path();
	if (project_root.is_empty() || !project_root.is_absolute_path()) {
		r_resource_path = String();
		r_absolute_path = String();
		return _script_buffer_fail("The project resource path is not an absolute path.", r_error, ERR_UNCONFIGURED);
	}

	const String relative_path = project_root.path_to_file(absolute_path).replace_char('\\', '/');
	if (relative_path.is_empty() || relative_path == "." || relative_path.is_absolute_path() || relative_path == ".." || relative_path.begins_with("../")) {
		r_resource_path = String();
		r_absolute_path = String();
		return _script_buffer_fail("Script path is outside the project resource directory.", r_error, ERR_INVALID_PARAMETER);
	}
	return normalize_path_for_root("res://" + relative_path, project_root, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptBuffer::resolve_path(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return _script_buffer_fail("Project settings are not available.", r_error, ERR_UNCONFIGURED);
	}
	return resolve_path_for_root(p_path, project_settings->get_resource_path(), r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptBuffer::_from_editor(const String &p_path, ScriptEditorBase *p_editor, MCPScriptBuffer &r_buffer, String *r_error) {
	TextEditorBase *text_editor = Object::cast_to<TextEditorBase>(p_editor);
	CodeTextEditor *code_editor = text_editor ? text_editor->get_code_editor() : nullptr;
	CodeEdit *code_edit = code_editor ? code_editor->get_text_editor() : nullptr;
	if (!p_editor || !text_editor || !code_edit) {
		return _script_buffer_fail("The Script editor buffer is not accessible: " + p_path, r_error, ERR_UNAVAILABLE);
	}

	r_buffer.path = p_path;
	r_buffer.editor = p_editor;
	r_buffer.text_editor = text_editor;
	r_buffer.code_edit = code_edit;
	return OK;
}

Error MCPScriptBuffer::read_authoritative_snapshot_for_root(const String &p_path, const String &p_project_root, Dictionary &r_snapshot, String *r_error) {
	r_snapshot = Dictionary();
	if (r_error) {
		*r_error = String();
	}

	String resource_path;
	String absolute_path;
	Error err = resolve_path_for_root(p_path, p_project_root, resource_path, absolute_path, r_error);
	if (err != OK) {
		return err;
	}
	if (!FileAccess::exists(absolute_path)) {
		return _script_buffer_fail("Script does not exist: " + resource_path, r_error, ERR_FILE_NOT_FOUND);
	}

	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	ScriptEditorBase *open_editor = _is_current_project_root(p_project_root) ? _find_open_editor(script_editor, resource_path) : nullptr;
	if (open_editor) {
		MCPScriptBuffer buffer;
		err = _from_editor(resource_path, open_editor, buffer, r_error);
		if (err != OK) {
			return err;
		}
		r_snapshot = buffer.get_snapshot();
		r_snapshot["sourceState"] = "editor";
		return OK;
	}

	Error read_error = OK;
	const String text = FileAccess::get_file_as_string(absolute_path, &read_error);
	if (read_error != OK) {
		return _script_buffer_fail("Script could not be read: " + resource_path, r_error, read_error);
	}
	const int64_t disk_revision = FileAccess::get_modified_time(absolute_path);
	r_snapshot["path"] = resource_path;
	r_snapshot["text"] = text;
	r_snapshot["revision"] = disk_revision;
	r_snapshot["savedRevision"] = disk_revision;
	r_snapshot["sha256"] = text.sha256_text();
	r_snapshot["unsaved"] = false;
	r_snapshot["sourceState"] = "disk";
	return OK;
}

Error MCPScriptBuffer::read_authoritative_snapshot(const String &p_path, Dictionary &r_snapshot, String *r_error) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return _script_buffer_fail("Project settings are not available.", r_error, ERR_UNCONFIGURED);
	}
	return read_authoritative_snapshot_for_root(p_path, project_settings->get_resource_path(), r_snapshot, r_error);
}

Error MCPScriptBuffer::open(const String &p_path, MCPScriptBuffer &r_buffer, String *r_error) {
	r_buffer = MCPScriptBuffer();
	if (r_error) {
		*r_error = String();
	}

	String resource_path;
	String absolute_path;
	Error err = normalize_path(p_path, resource_path, absolute_path, r_error);
	if (err != OK) {
		return err;
	}
	if (!FileAccess::exists(absolute_path)) {
		return _script_buffer_fail("Script does not exist: " + resource_path, r_error, ERR_FILE_NOT_FOUND);
	}

	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (!script_editor) {
		return _script_buffer_fail("The Script editor is not available.", r_error, ERR_UNCONFIGURED);
	}
	ScriptEditorBase *editor = _find_open_editor(script_editor, resource_path);
	if (!editor) {
		const Ref<Script> script = ResourceLoader::load(resource_path, "Script");
		if (script.is_null() || script->is_built_in()) {
			return _script_buffer_fail("External GDScript could not be loaded: " + resource_path, r_error, ERR_CANT_OPEN);
		}
		if (!script_editor->edit(script, false)) {
			return _script_buffer_fail("The internal Script editor could not open: " + resource_path, r_error, ERR_UNAVAILABLE);
		}
		editor = _find_open_editor(script_editor, resource_path);
	}

	return _from_editor(resource_path, editor, r_buffer, r_error);
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
	return snapshot;
}

Error MCPScriptBuffer::replace_text(const String &p_text, const Variant &p_expected_revision,
		const Variant &p_expected_sha256, bool &r_changed, Dictionary &r_current_state, String *r_error) {
	r_changed = false;
	r_current_state = Dictionary();
	if (r_error) {
		*r_error = String();
	}
	if (!is_valid()) {
		return _script_buffer_fail("The Script editor buffer is not available.", r_error, ERR_UNCONFIGURED);
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
		return _script_buffer_fail("The Script editor buffer is not available.", r_error, ERR_UNCONFIGURED);
	}
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	const Ref<Resource> resource = editor->get_edited_resource();
	ScriptEditorBase *previous_editor = script_editor->get_current_editor();
	const Ref<Resource> previous_resource = previous_editor ? previous_editor->get_edited_resource() : Ref<Resource>();
	if (resource.is_null() || !script_editor->edit(resource, false)) {
		return _script_buffer_fail("The Script editor could not select the target buffer.", r_error, ERR_UNAVAILABLE);
	}
	if (script_editor->get_current_editor() != editor) {
		return _script_buffer_fail("The target buffer could not become the current Script editor tab.", r_error, ERR_BUSY);
	}
	script_editor->save_current_script();
	const bool still_unsaved = editor->is_unsaved();
	if (previous_editor && previous_editor != editor && previous_resource.is_valid()) {
		script_editor->edit(previous_resource, false);
	}
	if (still_unsaved) {
		return _script_buffer_fail("The Script editor did not save the target buffer.", r_error, ERR_FILE_CANT_WRITE);
	}
	return OK;
}
