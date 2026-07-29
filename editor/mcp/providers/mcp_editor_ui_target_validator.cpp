/**************************************************************************/
/*  mcp_editor_ui_target_validator.cpp                                    */
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

#include "mcp_editor_ui_target_validator.h"

#include "core/object/object.h"
#include "editor/editor_node.h"
#include "scene/gui/base_button.h"
#include "scene/gui/button.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/link_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/range.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/window.h"

namespace {

static Error _fail(const String &p_error_code, const String &p_error_message, String &r_error_code, String &r_error_message, Error p_error) {
	r_error_code = p_error_code;
	r_error_message = p_error_message;
	return p_error;
}

} // namespace

String MCPEditorUITargetValidator::limit_text(const String &p_text) {
	static constexpr int MAX_TEXT_LENGTH = 1024;
	return p_text.length() > MAX_TEXT_LENGTH ? p_text.left(MAX_TEXT_LENGTH) : p_text;
}

String MCPEditorUITargetValidator::display_name(Control *p_control) {
	String name = p_control->get_accessibility_name().strip_edges();
	if (!name.is_empty()) {
		return limit_text(name);
	}
	if (Button *button = Object::cast_to<Button>(p_control)) {
		return limit_text(button->get_text().strip_edges());
	}
	if (LinkButton *button = Object::cast_to<LinkButton>(p_control)) {
		return limit_text(button->get_text().strip_edges());
	}
	if (!p_control->get_tooltip_text().is_empty()) {
		return limit_text(p_control->get_tooltip_text().strip_edges());
	}
	return limit_text(String(p_control->get_name()));
}

bool MCPEditorUITargetValidator::is_safe_text_edit(Control *p_control) {
	return Object::cast_to<TextEdit>(p_control) && !Object::cast_to<CodeEdit>(p_control);
}

bool MCPEditorUITargetValidator::is_editable_range(Control *p_control) {
	if (Slider *slider = Object::cast_to<Slider>(p_control)) {
		return slider->is_editable();
	}
	if (SpinBox *spin_box = Object::cast_to<SpinBox>(p_control)) {
		return spin_box->is_editable();
	}
	return false;
}

bool MCPEditorUITargetValidator::is_disabled(Control *p_control) {
	if (BaseButton *button = Object::cast_to<BaseButton>(p_control)) {
		return button->is_disabled();
	}
	return false;
}

PackedStringArray MCPEditorUITargetValidator::actions_for_control(Control *p_control) {
	PackedStringArray actions;
	if (p_control->get_focus_mode_with_override() != Control::FOCUS_NONE) {
		actions.push_back("focus");
	}
	if (Object::cast_to<BaseButton>(p_control)) {
		actions.push_back("click");
	}
	LineEdit *line_edit = Object::cast_to<LineEdit>(p_control);
	TextEdit *text_edit = Object::cast_to<TextEdit>(p_control);
	if ((line_edit && line_edit->is_editable()) || (text_edit && is_safe_text_edit(p_control) && text_edit->is_editable()) || is_editable_range(p_control)) {
		actions.push_back("set_value");
	}
	if (is_editable_range(p_control)) {
		actions.push_back("increment");
		actions.push_back("decrement");
	}
	return actions;
}

bool MCPEditorUITargetValidator::belongs_to_editor(Node *p_node) {
	EditorNode *editor = EditorNode::get_singleton();
	Control *root = editor ? editor->get_gui_base() : nullptr;
	return p_node && root && (p_node == root || root->is_ancestor_of(p_node));
}

Error MCPEditorUITargetValidator::validate_scope(const MCPEditorUISnapshot &p_snapshot, const ObjectID &p_scope_id, String &r_error_code, String &r_error_message) const {
	if (p_scope_id != p_snapshot.scope_id) {
		return _fail("STALE_UI_SNAPSHOT", "The active editor window changed after the snapshot was captured.", r_error_code, r_error_message, ERR_DOES_NOT_EXIST);
	}
	return OK;
}

Error MCPEditorUITargetValidator::find_target(const MCPEditorUISnapshot &p_snapshot, const String &p_target_id, const MCPEditorUITarget *&r_target,
		String &r_error_code, String &r_error_message) const {
	r_target = p_snapshot.targets.getptr(p_target_id);
	if (!r_target) {
		return _fail("UI_TARGET_NOT_FOUND", "The target is not present in this UI snapshot.", r_error_code, r_error_message, ERR_DOES_NOT_EXIST);
	}
	return OK;
}

Error MCPEditorUITargetValidator::validate_action(const MCPEditorUITarget &p_target, const String &p_action, String &r_error_code, String &r_error_message) const {
	if (!p_target.actions.has(p_action)) {
		return _fail("UI_ACTION_UNAVAILABLE", "The requested action was not advertised for this target.", r_error_code, r_error_message, ERR_UNAVAILABLE);
	}
	return OK;
}

bool MCPEditorUITargetValidator::_target_is_current(const MCPEditorUITarget &p_target) const {
	Object *object = ObjectDB::get_instance(p_target.object_id);
	Control *control = Object::cast_to<Control>(object);
	switch (p_target.kind) {
		case MCP_TARGET_CONTROL:
			return control && control->get_class() == StringName(p_target.fingerprint) && control->get_path() == NodePath(p_target.metadata) && display_name(control) == p_target.name_fingerprint && actions_for_control(control) == p_target.actions;
		case MCP_TARGET_POPUP_ITEM: {
			PopupMenu *popup = Object::cast_to<PopupMenu>(object);
			return popup && p_target.index >= 0 && p_target.index < popup->get_item_count() && popup->get_item_id(p_target.index) == p_target.item_id && popup->get_item_text(p_target.index) == p_target.fingerprint && popup->get_item_submenu(p_target.index).is_empty();
		}
		case MCP_TARGET_OPTION_ITEM: {
			OptionButton *option = Object::cast_to<OptionButton>(control);
			return option && p_target.index >= 0 && p_target.index < option->get_item_count() && option->get_item_id(p_target.index) == p_target.item_id && option->get_item_text(p_target.index) == p_target.fingerprint && option->get_item_metadata(p_target.index) == p_target.metadata;
		}
		case MCP_TARGET_ITEM_LIST_ITEM: {
			ItemList *list = Object::cast_to<ItemList>(control);
			return list && p_target.index >= 0 && p_target.index < list->get_item_count() && list->get_item_text(p_target.index) == p_target.fingerprint && list->get_item_metadata(p_target.index) == p_target.metadata;
		}
		case MCP_TARGET_TAB: {
			TabBar *tabs = Object::cast_to<TabBar>(control);
			return tabs && p_target.index >= 0 && p_target.index < tabs->get_tab_count() && tabs->get_tab_title(p_target.index) == p_target.fingerprint && tabs->get_tab_metadata(p_target.index) == p_target.metadata;
		}
		case MCP_TARGET_TREE_CELL:
		case MCP_TARGET_TREE_BUTTON: {
			Tree *tree = Object::cast_to<Tree>(control);
			TreeItem *item = ObjectDB::get_instance<TreeItem>(p_target.auxiliary_object_id);
			return tree && item && item->get_tree() == tree && p_target.column >= 0 && p_target.column < tree->get_columns() && item->get_text(p_target.column) == p_target.fingerprint && (p_target.kind != MCP_TARGET_TREE_BUTTON || (p_target.button >= 0 && p_target.button < item->get_button_count(p_target.column) && item->get_button_id(p_target.column, p_target.button) == p_target.item_id));
		}
	}
	return false;
}

Error MCPEditorUITargetValidator::validate_current(const MCPEditorUITarget &p_target, String &r_error_code, String &r_error_message) const {
	if (!_target_is_current(p_target)) {
		return _fail("STALE_UI_SNAPSHOT", "The target changed after the UI snapshot was captured.", r_error_code, r_error_message, ERR_DOES_NOT_EXIST);
	}
	return OK;
}

Error MCPEditorUITargetValidator::validate_available(const MCPEditorUITarget &p_target, const String &p_action, String &r_error_code, String &r_error_message) const {
	Object *object = ObjectDB::get_instance(p_target.object_id);
	Node *node = Object::cast_to<Node>(object);
	Control *control = Object::cast_to<Control>(object);
	Window *window = Object::cast_to<Window>(object);
	if (!node || (control && !control->is_visible_in_tree()) || (window && !window->is_visible()) || !belongs_to_editor(node)) {
		return _fail("STALE_UI_SNAPSHOT", "The target no longer belongs to the visible editor UI.", r_error_code, r_error_message, ERR_DOES_NOT_EXIST);
	}
	if (control && is_disabled(control)) {
		return _fail("UI_TARGET_DISABLED", "The target control is disabled.", r_error_code, r_error_message, ERR_UNAVAILABLE);
	}

	switch (p_target.kind) {
		case MCP_TARGET_CONTROL:
			return OK;
		case MCP_TARGET_POPUP_ITEM: {
			PopupMenu *popup = Object::cast_to<PopupMenu>(object);
			if (p_action == "activate" && popup && p_target.index < popup->get_item_count() && popup->get_item_id(p_target.index) == p_target.item_id && popup->get_item_text(p_target.index) == p_target.fingerprint && popup->get_item_submenu(p_target.index).is_empty() && !popup->is_item_disabled(p_target.index) && !popup->is_item_separator(p_target.index)) {
				return OK;
			}
		} break;
		case MCP_TARGET_OPTION_ITEM: {
			OptionButton *option = Object::cast_to<OptionButton>(control);
			if (p_action == "select" && option && p_target.index < option->get_item_count() && option->get_item_id(p_target.index) == p_target.item_id && option->get_item_text(p_target.index) == p_target.fingerprint && option->get_item_metadata(p_target.index) == p_target.metadata && !option->is_item_disabled(p_target.index)) {
				return OK;
			}
		} break;
		case MCP_TARGET_ITEM_LIST_ITEM: {
			ItemList *list = Object::cast_to<ItemList>(control);
			if (list && p_target.index < list->get_item_count() && list->get_item_text(p_target.index) == p_target.fingerprint && list->get_item_metadata(p_target.index) == p_target.metadata && !list->is_item_disabled(p_target.index) && list->is_item_selectable(p_target.index)) {
				if (p_action == "select" || p_action == "activate") {
					return OK;
				}
			}
		} break;
		case MCP_TARGET_TAB: {
			TabBar *tabs = Object::cast_to<TabBar>(control);
			if (p_action == "select" && tabs && p_target.index < tabs->get_tab_count() && tabs->get_tab_title(p_target.index) == p_target.fingerprint && tabs->get_tab_metadata(p_target.index) == p_target.metadata && !tabs->is_tab_disabled(p_target.index) && !tabs->is_tab_hidden(p_target.index)) {
				return OK;
			}
		} break;
		case MCP_TARGET_TREE_CELL:
		case MCP_TARGET_TREE_BUTTON: {
			Tree *tree = Object::cast_to<Tree>(control);
			TreeItem *item = ObjectDB::get_instance<TreeItem>(p_target.auxiliary_object_id);
			if (!tree || !item || item->get_tree() != tree || p_target.column < 0 || p_target.column >= tree->get_columns()) {
				return _fail("STALE_UI_SNAPSHOT", "The tree item no longer exists.", r_error_code, r_error_message, ERR_DOES_NOT_EXIST);
			}
			if (p_target.kind == MCP_TARGET_TREE_BUTTON) {
				if (p_action == "click" && p_target.button >= 0 && p_target.button < item->get_button_count(p_target.column) && item->get_button_id(p_target.column, p_target.button) == p_target.item_id && !item->is_button_disabled(p_target.column, p_target.button)) {
					return OK;
				}
				return _fail("UI_ACTION_UNAVAILABLE", "A tree button only accepts click.", r_error_code, r_error_message, ERR_UNAVAILABLE);
			}
			if ((p_action == "select" && item->is_selectable(p_target.column)) || p_action == "expand" || p_action == "collapse") {
				return OK;
			}
		} break;
	}
	return _fail("UI_ACTION_UNAVAILABLE", "The requested action is not available for this target.", r_error_code, r_error_message, ERR_UNAVAILABLE);
}
