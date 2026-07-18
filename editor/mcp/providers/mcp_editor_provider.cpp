/**************************************************************************/
/*  mcp_editor_provider.cpp                                               */
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

#include "mcp_editor_provider.h"

#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/main/node.h"

namespace {

static Dictionary _empty_input_schema() {
	Dictionary schema;
	schema["type"] = "object";
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _state_output_schema() {
	Dictionary string_property;
	string_property["type"] = "string";
	Dictionary boolean_property;
	boolean_property["type"] = "boolean";
	Dictionary string_array_property;
	string_array_property["type"] = "array";
	string_array_property["items"] = string_property;

	Dictionary properties;
	properties["currentScene"] = string_property;
	properties["unsavedScenes"] = string_array_property;
	properties["openScripts"] = string_array_property;
	properties["unsavedScripts"] = string_array_property;
	properties["canUndo"] = boolean_property;
	properties["canRedo"] = boolean_property;

	PackedStringArray required;
	required.push_back("currentScene");
	required.push_back("unsavedScenes");
	required.push_back("openScripts");
	required.push_back("unsavedScripts");
	required.push_back("canUndo");
	required.push_back("canRedo");

	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = properties;
	schema["required"] = required;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _unexpected_arguments(const String &p_tool_name) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Tool '" + p_tool_name + "' does not accept arguments.");
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPEditorProvider::~MCPEditorProvider() {
	unregister_tools();
}

Error MCPEditorProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Editor tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const Dictionary schema = _empty_input_schema();
	Dictionary state_definition = MCPToolUtils::make_tool_definition(
			"godot.editor.get_state", "Get the current scene, unsaved scenes, scripts, and undo state.", schema,
			MCPToolUtils::TOOL_READ_ONLY, _state_output_schema());
	Error err = p_registry->register_tool(
			state_definition,
			callable_mp(this, &MCPEditorProvider::get_state), this, r_error);
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.editor.undo", "Undo the latest editor action.", schema, MCPToolUtils::TOOL_DESTRUCTIVE),
				callable_mp(this, &MCPEditorProvider::undo), this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.editor.redo", "Redo the latest editor action.", schema, MCPToolUtils::TOOL_DESTRUCTIVE),
				callable_mp(this, &MCPEditorProvider::redo), this, r_error);
	}
	if (err != OK) {
		p_registry->unregister_tools_for_owner(this);
		return err;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPEditorProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPEditorProvider::get_state(const Dictionary &p_arguments, const Dictionary &) {
	if (!p_arguments.is_empty()) {
		return _unexpected_arguments("godot.editor.get_state");
	}

	String current_scene;
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node && EditorNode::get_editor_data().get_edited_scene_count() > 0) {
		Node *edited_scene_root = editor_node->get_edited_scene();
		if (edited_scene_root) {
			current_scene = edited_scene_root->get_scene_file_path();
		}
	}

	PackedStringArray open_scripts;
	PackedStringArray unsaved_scripts;
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (script_editor) {
		const Vector<Ref<Script>> scripts = script_editor->get_open_scripts();
		for (const Ref<Script> &script : scripts) {
			if (script.is_valid() && !script->get_path().is_empty()) {
				open_scripts.push_back(script->get_path());
			}
		}
		unsaved_scripts = script_editor->get_unsaved_scripts();
	}
	PackedStringArray unsaved_scenes;
	EditorInterface *editor_interface = EditorInterface::get_singleton();
	if (editor_interface && editor_node) {
		unsaved_scenes = editor_interface->get_unsaved_scenes();
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	Dictionary state;
	state["currentScene"] = current_scene;
	state["unsavedScenes"] = unsaved_scenes;
	state["openScripts"] = open_scripts;
	state["unsavedScripts"] = unsaved_scripts;
	state["canUndo"] = undo_redo && undo_redo->has_undo();
	state["canRedo"] = undo_redo && undo_redo->has_redo();
	return MCPToolUtils::make_success_result(state);
}

Dictionary MCPEditorProvider::_perform_history_action(bool p_undo) {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	if (!undo_redo) {
		return MCPToolUtils::make_error_result("EDITOR_UNAVAILABLE", "The editor undo manager is not available.");
	}
	if (p_undo && !undo_redo->has_undo()) {
		return MCPToolUtils::make_error_result("NOTHING_TO_UNDO", "There is no editor action to undo.");
	}
	if (!p_undo && !undo_redo->has_redo()) {
		return MCPToolUtils::make_error_result("NOTHING_TO_REDO", "There is no editor action to redo.");
	}

	const bool performed = p_undo ? undo_redo->undo() : undo_redo->redo();
	if (!performed) {
		return MCPToolUtils::make_error_result(p_undo ? "UNDO_FAILED" : "REDO_FAILED", p_undo ? "The editor action could not be undone." : "The editor action could not be redone.");
	}

	Dictionary result;
	result["action"] = p_undo ? "undo" : "redo";
	result["performed"] = true;
	result["canUndo"] = undo_redo->has_undo();
	result["canRedo"] = undo_redo->has_redo();
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPEditorProvider::undo(const Dictionary &p_arguments, const Dictionary &) {
	if (!p_arguments.is_empty()) {
		return _unexpected_arguments("godot.editor.undo");
	}
	return _perform_history_action(true);
}

Dictionary MCPEditorProvider::redo(const Dictionary &p_arguments, const Dictionary &) {
	if (!p_arguments.is_empty()) {
		return _unexpected_arguments("godot.editor.redo");
	}
	return _perform_history_action(false);
}
