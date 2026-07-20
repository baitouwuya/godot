/**************************************************************************/
/*  mcp_project_settings_mutation.cpp                                     */
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

#include "mcp_project_settings_mutation.h"

#include "core/config/project_settings.h"
#include "core/input/input_map.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/settings/editor_autoload_settings.h"
#include "editor/settings/project_settings_editor.h"

namespace {

static EditorAutoloadSettings *_get_autoload_settings() {
	EditorNode *editor_node = EditorNode::get_singleton();
	if (!editor_node || !editor_node->get_project_settings()) {
		return nullptr;
	}
	return editor_node->get_project_settings()->get_autoload_settings();
}

static void _apply_direct(ProjectSettings *p_settings, const String &p_name, const Variant &p_value) {
	if (p_value.get_type() == Variant::NIL) {
		if (p_settings->has_setting(p_name)) {
			p_settings->clear(p_name);
		}
	} else {
		p_settings->set_setting(p_name, p_value);
	}
}

static void _refresh_direct(MCPProjectSettingsMutation::RefreshMode p_mode) {
	if (p_mode == MCPProjectSettingsMutation::REFRESH_AUTOLOAD) {
		EditorAutoloadSettings *autoload_settings = _get_autoload_settings();
		if (autoload_settings) {
			autoload_settings->update_autoload();
		}
	} else if (p_mode == MCPProjectSettingsMutation::REFRESH_INPUT_MAP) {
		if (InputMap::get_singleton()) {
			InputMap::get_singleton()->load_from_project_settings();
		}
		if (ProjectSettingsEditor::get_singleton()) {
			ProjectSettingsEditor::get_singleton()->call("_update_action_map_editor");
		}
	}
}

} // namespace

void MCPProjectSettingsMutation::commit(const String &p_action, const String &p_name, const Variant &p_new_value,
		bool p_old_existed, const Variant &p_old_value, int p_old_order, RefreshMode p_refresh_mode) {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	if (!undo_redo) {
		_apply_direct(settings, p_name, p_new_value);
		_refresh_direct(p_refresh_mode);
		return;
	}

	undo_redo->create_action_for_history(p_action, EditorUndoRedoManager::GLOBAL_HISTORY);
	undo_redo->force_fixed_history();
	if (p_new_value.get_type() == Variant::NIL) {
		undo_redo->add_do_method(settings, "clear", p_name);
	} else {
		undo_redo->add_do_method(settings, "set_setting", p_name, p_new_value);
	}
	if (p_old_existed) {
		undo_redo->add_undo_method(settings, "set_setting", p_name, p_old_value);
		undo_redo->add_undo_method(settings, "set_order", p_name, p_old_order);
	} else {
		undo_redo->add_undo_method(settings, "clear", p_name);
	}

	if (p_refresh_mode == REFRESH_AUTOLOAD) {
		EditorAutoloadSettings *autoload_settings = _get_autoload_settings();
		if (autoload_settings) {
			undo_redo->add_do_method(autoload_settings, "update_autoload");
			undo_redo->add_undo_method(autoload_settings, "update_autoload");
		}
	} else if (p_refresh_mode == REFRESH_INPUT_MAP) {
		if (InputMap::get_singleton()) {
			undo_redo->add_do_method(InputMap::get_singleton(), "load_from_project_settings");
			undo_redo->add_undo_method(InputMap::get_singleton(), "load_from_project_settings");
		}
		if (ProjectSettingsEditor::get_singleton()) {
			undo_redo->add_do_method(ProjectSettingsEditor::get_singleton(), "_update_action_map_editor");
			undo_redo->add_undo_method(ProjectSettingsEditor::get_singleton(), "_update_action_map_editor");
		}
	}
	undo_redo->commit_action();
}
