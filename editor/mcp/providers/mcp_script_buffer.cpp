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
#include "editor/editor_node.h"
#include "editor/gui/code_editor.h"
#include "editor/script/script_editor_base.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/gui/code_edit.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

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

static Error _resolve_built_in_script_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error) {
	const int separator = p_path.find("::");
	if (separator < 0 || p_path.find("::", separator + 2) >= 0) {
		return _script_buffer_fail("A built-in script path must contain exactly one :: subresource separator.", r_error, ERR_INVALID_PARAMETER);
	}

	const String container_path = p_path.left(separator);
	const String subresource_id = p_path.substr(separator + 2);
	if (container_path.is_empty() || subresource_id.is_empty() || subresource_id.contains_char('/') ||
			subresource_id.contains_char('\\') || subresource_id.contains_char(':')) {
		return _script_buffer_fail("The built-in script subresource identifier is invalid.", r_error, ERR_INVALID_PARAMETER);
	}

	String container_resource_path;
	const Error resolve_error = MCPPathUtils::resolve_project_file_path_for_root(
			container_path, p_project_root, container_resource_path, r_absolute_path, r_error);
	if (resolve_error != OK) {
		return resolve_error;
	}
	r_resource_path = container_resource_path + "::" + subresource_id;
	return OK;
}

static bool _is_gdscript(const Ref<Script> &p_script) {
	return p_script.is_valid() && p_script->get_language() && p_script->get_language()->get_extension() == "gd";
}

static String _get_editor_script_path(const Ref<Script> &p_script) {
	String script_path = p_script.is_valid() ? p_script->get_path() : String();
	if (script_path.is_empty() || script_path.begins_with("::") || script_path.begins_with("local://")) {
		script_path = "res://__mcp_unsaved_scene__.tscn::GDScript_" + itos(p_script->get_instance_id());
	}
	return script_path;
}

static Ref<Script> _find_live_script(Node *p_node, const String &p_path) {
	if (!p_node) {
		return Ref<Script>();
	}
	Ref<Script> script = p_node->get_script();
	while (script.is_valid()) {
		if (script->get_path() == p_path) {
			return script;
		}
		script = script->get_base_script();
	}
	for (int i = 0; i < p_node->get_child_count(true); i++) {
		Ref<Script> child_script = _find_live_script(p_node->get_child(i, true), p_path);
		if (child_script.is_valid()) {
			return child_script;
		}
	}
	return Ref<Script>();
}

static Ref<Script> _load_script_resource(const String &p_path) {
	Ref<Script> script = ResourceCache::get_ref(p_path);
	if (script.is_valid()) {
		return script;
	}

	if (p_path.contains("::")) {
		const String container_path = p_path.get_slice("::", 0);
		if (EditorNode::get_singleton()) {
			EditorData &editor_data = EditorNode::get_editor_data();
			for (int i = 0; i < editor_data.get_edited_scene_count(); i++) {
				Node *scene_root = editor_data.get_edited_scene_root(i);
				if (scene_root && scene_root->get_scene_file_path() == container_path) {
					script = _find_live_script(scene_root, p_path);
					if (script.is_valid()) {
						return script;
					}
				}
			}
		}

		const Ref<PackedScene> packed_scene = ResourceLoader::load(container_path, "PackedScene");
		const Ref<SceneState> state = packed_scene.is_valid() ? packed_scene->get_state() : Ref<SceneState>();
		if (state.is_valid()) {
			script = state->get_sub_resource(p_path);
			if (script.is_valid()) {
				return script;
			}
		}
	}

	return ResourceLoader::load(p_path, "Script");
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

static ScriptEditorBase *_find_open_editor(ScriptEditor *p_script_editor, const Ref<Script> &p_script) {
	if (!p_script_editor || p_script.is_null()) {
		return nullptr;
	}
	const Array open_editors = p_script_editor->call("get_open_script_editors");
	for (int i = 0; i < open_editors.size(); i++) {
		ScriptEditorBase *editor = Object::cast_to<ScriptEditorBase>(open_editors[i]);
		if (editor && editor->get_edited_resource() == p_script) {
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

Error MCPScriptBuffer::resolve_script_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, bool &r_built_in, String *r_error) {
	r_resource_path = String();
	r_absolute_path = String();
	r_built_in = p_path.contains("::");
	if (r_built_in) {
		return _resolve_built_in_script_path_for_root(p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
	}
	return resolve_path_for_root(p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptBuffer::resolve_script_path(const String &p_path, String &r_resource_path, String &r_absolute_path, bool &r_built_in, String *r_error) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return _script_buffer_fail("Project settings are not available.", r_error, ERR_UNCONFIGURED);
	}
	return resolve_script_path_for_root(p_path, project_settings->get_resource_path(), r_resource_path, r_absolute_path, r_built_in, r_error);
}

Error MCPScriptBuffer::_from_editor(const String &p_path, ScriptEditorBase *p_editor, MCPScriptBuffer &r_buffer, String *r_error) {
	TextEditorBase *text_editor = Object::cast_to<TextEditorBase>(p_editor);
	CodeTextEditor *code_editor = text_editor ? text_editor->get_code_editor() : nullptr;
	CodeEdit *code_edit = code_editor ? code_editor->get_text_editor() : nullptr;
	if (!p_editor || !text_editor || !code_edit) {
		return _script_buffer_fail("The Script editor buffer is not accessible: " + p_path, r_error, ERR_UNAVAILABLE);
	}

	r_buffer.path = p_path;
	const Ref<Script> script = p_editor->get_edited_resource();
	r_buffer.built_in = script.is_valid() && script->is_built_in();
	if (r_buffer.built_in && script->get_path().contains("::")) {
		r_buffer.scene_path = script->get_path().get_slice("::", 0);
	}
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
	bool built_in = false;
	Error err = resolve_script_path_for_root(p_path, p_project_root, resource_path, absolute_path, built_in, r_error);
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
	if (built_in) {
		if (!_is_current_project_root(p_project_root)) {
			return _script_buffer_fail("Built-in scripts can only be read from the active editor project.", r_error, ERR_UNCONFIGURED);
		}
		const Ref<Script> script = _load_script_resource(resource_path);
		if (!_is_gdscript(script) || !script->is_built_in()) {
			return _script_buffer_fail("Built-in GDScript could not be loaded: " + resource_path, r_error, ERR_CANT_OPEN);
		}
		const String text = script->get_source_code();
		const int64_t disk_revision = FileAccess::get_modified_time(absolute_path);
		r_snapshot["path"] = resource_path;
		r_snapshot["text"] = text;
		r_snapshot["revision"] = disk_revision;
		r_snapshot["savedRevision"] = disk_revision;
		r_snapshot["sha256"] = text.sha256_text();
		r_snapshot["unsaved"] = false;
		r_snapshot["sourceState"] = "resource";
		r_snapshot["sourceType"] = "builtIn";
		r_snapshot["scenePath"] = resource_path.get_slice("::", 0);
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
	r_snapshot["sourceType"] = "external";
	return OK;
}

Error MCPScriptBuffer::read_authoritative_snapshot(const String &p_path, Dictionary &r_snapshot, String *r_error) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return _script_buffer_fail("Project settings are not available.", r_error, ERR_UNCONFIGURED);
	}
	return read_authoritative_snapshot_for_root(p_path, project_settings->get_resource_path(), r_snapshot, r_error);
}

Error MCPScriptBuffer::read_open_snapshots(Array &r_snapshots, String *r_error) {
	r_snapshots.clear();
	if (r_error) {
		*r_error = String();
	}
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (!script_editor) {
		return OK;
	}

	const Array open_editors = script_editor->call("get_open_script_editors");
	for (int i = 0; i < open_editors.size(); i++) {
		ScriptEditorBase *editor = Object::cast_to<ScriptEditorBase>(open_editors[i]);
		Ref<Script> script;
		if (editor) {
			script = editor->get_edited_resource();
		}
		if (!_is_gdscript(script)) {
			continue;
		}
		MCPScriptBuffer buffer;
		const Error error = _from_editor(_get_editor_script_path(script), editor, buffer, r_error);
		if (error != OK) {
			return error;
		}
		r_snapshots.push_back(buffer.get_snapshot());
	}
	return OK;
}

Error MCPScriptBuffer::open(const String &p_path, MCPScriptBuffer &r_buffer, String *r_error) {
	r_buffer = MCPScriptBuffer();
	if (r_error) {
		*r_error = String();
	}

	String resource_path;
	String absolute_path;
	bool built_in = false;
	Error err = resolve_script_path(p_path, resource_path, absolute_path, built_in, r_error);
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
		const Ref<Script> script = _load_script_resource(resource_path);
		if (!_is_gdscript(script) || script->is_built_in() != built_in) {
			return _script_buffer_fail("GDScript could not be loaded: " + resource_path, r_error, ERR_CANT_OPEN);
		}
		if (!script_editor->edit(script, false)) {
			return _script_buffer_fail("The internal Script editor could not open: " + resource_path, r_error, ERR_UNAVAILABLE);
		}
		editor = _find_open_editor(script_editor, resource_path);
	}

	return _from_editor(resource_path, editor, r_buffer, r_error);
}

Error MCPScriptBuffer::open(const Ref<Script> &p_script, MCPScriptBuffer &r_buffer, String *r_error) {
	r_buffer = MCPScriptBuffer();
	if (r_error) {
		*r_error = String();
	}
	if (!_is_gdscript(p_script)) {
		return _script_buffer_fail("The selected node does not have a GDScript.", r_error, ERR_INVALID_PARAMETER);
	}

	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (!script_editor) {
		return _script_buffer_fail("The Script editor is not available.", r_error, ERR_UNCONFIGURED);
	}
	ScriptEditorBase *editor = _find_open_editor(script_editor, p_script);
	if (!editor) {
		if (!script_editor->edit(p_script, false)) {
			return _script_buffer_fail("The internal Script editor could not open the selected node script.", r_error, ERR_UNAVAILABLE);
		}
		editor = _find_open_editor(script_editor, p_script);
	}

	return _from_editor(_get_editor_script_path(p_script), editor, r_buffer, r_error);
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
		return _script_buffer_fail("The Script editor buffer is not available.", r_error, ERR_UNCONFIGURED);
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
	if (resource.is_valid() && resource->is_built_in() && resource->get_path().get_slice("::", 0).is_empty()) {
		return _script_buffer_fail("The built-in script cannot be saved until its scene has a file path.", r_error, ERR_FILE_CANT_WRITE);
	}
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
