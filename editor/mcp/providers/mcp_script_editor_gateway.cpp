/**************************************************************************/
/*  mcp_script_editor_gateway.cpp                                         */
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

#include "mcp_script_editor_gateway.h"

#include "mcp_script_buffer.h"
#include "mcp_script_path_resolver.h"

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

namespace {

Error fail(const String &p_message, String *r_error, Error p_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

Ref<Script> find_live_script(Node *p_node, const String &p_path) {
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
		Ref<Script> child_script = find_live_script(p_node->get_child(i, true), p_path);
		if (child_script.is_valid()) {
			return child_script;
		}
	}
	return Ref<Script>();
}

} // namespace

bool MCPScriptEditorGateway::is_gdscript(const Ref<Script> &p_script) {
	return p_script.is_valid() && p_script->get_language() && p_script->get_language()->get_extension() == "gd";
}

String MCPScriptEditorGateway::get_editor_script_path(const Ref<Script> &p_script) {
	String script_path = p_script.is_valid() ? p_script->get_path() : String();
	if (script_path.is_empty() || script_path.begins_with("::") || script_path.begins_with("local://")) {
		script_path = "res://__mcp_unsaved_scene__.tscn::GDScript_" + itos(p_script->get_instance_id());
	}
	return script_path;
}

Ref<Script> MCPScriptEditorGateway::load_script_resource(const String &p_path) {
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
					script = find_live_script(scene_root, p_path);
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

ScriptEditorBase *MCPScriptEditorGateway::find_open_editor(ScriptEditor *p_script_editor, const String &p_path) {
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

ScriptEditorBase *MCPScriptEditorGateway::find_open_editor(ScriptEditor *p_script_editor, const Ref<Script> &p_script) {
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

Error MCPScriptEditorGateway::from_editor(const String &p_path, ScriptEditorBase *p_editor, MCPScriptBuffer &r_buffer, String *r_error) {
	TextEditorBase *text_editor = Object::cast_to<TextEditorBase>(p_editor);
	CodeTextEditor *code_editor = text_editor ? text_editor->get_code_editor() : nullptr;
	CodeEdit *code_edit = code_editor ? code_editor->get_text_editor() : nullptr;
	if (!p_editor || !text_editor || !code_edit) {
		return fail("The Script editor buffer is not accessible: " + p_path, r_error, ERR_UNAVAILABLE);
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

Error MCPScriptEditorGateway::open(const String &p_path, MCPScriptBuffer &r_buffer, String *r_error) {
	r_buffer = MCPScriptBuffer();
	if (r_error) {
		*r_error = String();
	}

	String resource_path;
	String absolute_path;
	bool built_in = false;
	Error err = MCPScriptPathResolver::resolve_script(p_path, resource_path, absolute_path, built_in, r_error);
	if (err != OK) {
		return err;
	}
	if (!FileAccess::exists(absolute_path)) {
		return fail("Script does not exist: " + resource_path, r_error, ERR_FILE_NOT_FOUND);
	}

	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (!script_editor) {
		return fail("The Script editor is not available.", r_error, ERR_UNCONFIGURED);
	}
	ScriptEditorBase *editor = find_open_editor(script_editor, resource_path);
	if (!editor) {
		const Ref<Script> script = load_script_resource(resource_path);
		if (!is_gdscript(script) || script->is_built_in() != built_in) {
			return fail("GDScript could not be loaded: " + resource_path, r_error, ERR_CANT_OPEN);
		}
		if (!script_editor->edit(script, false)) {
			return fail("The internal Script editor could not open: " + resource_path, r_error, ERR_UNAVAILABLE);
		}
		editor = find_open_editor(script_editor, resource_path);
	}

	return from_editor(resource_path, editor, r_buffer, r_error);
}

Error MCPScriptEditorGateway::open(const Ref<Script> &p_script, MCPScriptBuffer &r_buffer, String *r_error) {
	r_buffer = MCPScriptBuffer();
	if (r_error) {
		*r_error = String();
	}
	if (!is_gdscript(p_script)) {
		return fail("The selected node does not have a GDScript.", r_error, ERR_INVALID_PARAMETER);
	}

	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (!script_editor) {
		return fail("The Script editor is not available.", r_error, ERR_UNCONFIGURED);
	}
	ScriptEditorBase *editor = find_open_editor(script_editor, p_script);
	if (!editor) {
		if (!script_editor->edit(p_script, false)) {
			return fail("The internal Script editor could not open the selected node script.", r_error, ERR_UNAVAILABLE);
		}
		editor = find_open_editor(script_editor, p_script);
	}

	return from_editor(get_editor_script_path(p_script), editor, r_buffer, r_error);
}
