/**************************************************************************/
/*  mcp_script_snapshot_reader.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_script_snapshot_reader.h"

#include "mcp_script_buffer.h"
#include "mcp_script_editor_gateway.h"
#include "mcp_script_path_resolver.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "editor/script/script_editor_base.h"
#include "editor/script/script_editor_plugin.h"

namespace {

Error fail(const String &p_message, String *r_error, Error p_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

bool is_current_project_root(const String &p_project_root) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return false;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	return filesystem.is_valid() && filesystem->is_equivalent(p_project_root, project_settings->get_resource_path());
}

} // namespace

Error MCPScriptSnapshotReader::read_for_root(const String &p_path, const String &p_project_root, Dictionary &r_snapshot, String *r_error) {
	r_snapshot = Dictionary();
	if (r_error) {
		*r_error = String();
	}

	String resource_path;
	String absolute_path;
	bool built_in = false;
	Error err = MCPScriptPathResolver::resolve_script_for_root(p_path, p_project_root, resource_path, absolute_path, built_in, r_error);
	if (err != OK) {
		return err;
	}
	if (!FileAccess::exists(absolute_path)) {
		return fail("Script does not exist: " + resource_path, r_error, ERR_FILE_NOT_FOUND);
	}

	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	ScriptEditorBase *open_editor = is_current_project_root(p_project_root) ? MCPScriptEditorGateway::find_open_editor(script_editor, resource_path) : nullptr;
	if (open_editor) {
		MCPScriptBuffer buffer;
		err = MCPScriptEditorGateway::from_editor(resource_path, open_editor, buffer, r_error);
		if (err != OK) {
			return err;
		}
		r_snapshot = buffer.get_snapshot();
		r_snapshot["sourceState"] = "editor";
		return OK;
	}
	if (built_in) {
		if (!is_current_project_root(p_project_root)) {
			return fail("Built-in scripts can only be read from the active editor project.", r_error, ERR_UNCONFIGURED);
		}
		const Ref<Script> script = MCPScriptEditorGateway::load_script_resource(resource_path);
		if (!MCPScriptEditorGateway::is_gdscript(script) || !script->is_built_in()) {
			return fail("Built-in GDScript could not be loaded: " + resource_path, r_error, ERR_CANT_OPEN);
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
		return fail("Script could not be read: " + resource_path, r_error, read_error);
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

Error MCPScriptSnapshotReader::read(const String &p_path, Dictionary &r_snapshot, String *r_error) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return fail("Project settings are not available.", r_error, ERR_UNCONFIGURED);
	}
	return read_for_root(p_path, project_settings->get_resource_path(), r_snapshot, r_error);
}

Error MCPScriptSnapshotReader::read_open(Array &r_snapshots, String *r_error) {
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
		if (!MCPScriptEditorGateway::is_gdscript(script)) {
			continue;
		}
		MCPScriptBuffer buffer;
		const Error error = MCPScriptEditorGateway::from_editor(MCPScriptEditorGateway::get_editor_script_path(script), editor, buffer, r_error);
		if (error != OK) {
			return error;
		}
		r_snapshots.push_back(buffer.get_snapshot());
	}
	return OK;
}
