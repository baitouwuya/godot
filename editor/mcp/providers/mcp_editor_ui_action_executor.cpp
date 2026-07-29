/**************************************************************************/
/*  mcp_editor_ui_action_executor.cpp                                    */
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

#include "mcp_editor_ui_action_executor.h"

#include "core/math/math_funcs.h"
#include "core/object/object.h"
#include "editor/mcp/providers/mcp_editor_ui_target_validator.h"
#include "scene/gui/base_button.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/range.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"

namespace {

static Error _fail(const String &p_error_code, const String &p_error_message, String &r_error_code, String &r_error_message, Error p_error) {
	r_error_code = p_error_code;
	r_error_message = p_error_message;
	return p_error;
}

} // namespace

bool MCPEditorUIActionExecutor::_try_get_number(const Dictionary &p_arguments, const StringName &p_name, double &r_value) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) {
		return false;
	}
	r_value = value;
	return Math::is_finite(r_value);
}

Error MCPEditorUIActionExecutor::execute(const MCPEditorUITarget &p_target, const String &p_action, const Dictionary &p_arguments,
		String &r_error_code, String &r_error_message) const {
	Object *object = ObjectDB::get_instance(p_target.object_id);
	Control *control = Object::cast_to<Control>(object);
	if (!object) {
		return _fail("STALE_UI_SNAPSHOT", "The target no longer belongs to the visible editor UI.", r_error_code, r_error_message, ERR_DOES_NOT_EXIST);
	}

	if (p_target.kind == MCP_TARGET_CONTROL) {
		if (p_action == "focus" && control && control->get_focus_mode_with_override() != Control::FOCUS_NONE) {
			control->grab_focus();
			return OK;
		}
		if (p_action == "click" && control) {
			if (BaseButton *button = Object::cast_to<BaseButton>(control)) {
				button->activate();
				return OK;
			}
		}
		if (p_action == "set_value" && control) {
			const Variant value = p_arguments.get("value", Variant());
			if (LineEdit *line_edit = Object::cast_to<LineEdit>(control)) {
				if (!line_edit->is_editable() || value.get_type() != Variant::STRING) {
					return _fail("INVALID_UI_VALUE", "An editable LineEdit requires a string value.", r_error_code, r_error_message, ERR_INVALID_PARAMETER);
				}
				line_edit->call("_set_text", value, true);
				return OK;
			}
			if (TextEdit *text_edit = Object::cast_to<TextEdit>(control); text_edit && !Object::cast_to<CodeEdit>(control)) {
				if (!text_edit->is_editable() || value.get_type() != Variant::STRING) {
					return _fail("INVALID_UI_VALUE", "An editable TextEdit requires a string value.", r_error_code, r_error_message, ERR_INVALID_PARAMETER);
				}
				text_edit->call("_set_text", value, true);
				return OK;
			}
			if (Range *range = Object::cast_to<Range>(control); range && MCPEditorUITargetValidator::is_editable_range(control)) {
				double number = 0;
				if (!_try_get_number(p_arguments, "value", number)) {
					return _fail("INVALID_UI_VALUE", "A Range requires a finite numeric value.", r_error_code, r_error_message, ERR_INVALID_PARAMETER);
				}
				range->set_value(number);
				return OK;
			}
		}
		if ((p_action == "increment" || p_action == "decrement") && control && MCPEditorUITargetValidator::is_editable_range(control)) {
			Range *range = Object::cast_to<Range>(control);
			range->set_value(range->get_value() + (p_action == "increment" ? range->get_step() : -range->get_step()));
			return OK;
		}
	} else if (p_target.kind == MCP_TARGET_POPUP_ITEM) {
		PopupMenu *popup = Object::cast_to<PopupMenu>(object);
		if (p_action == "activate" && popup && p_target.index < popup->get_item_count() && popup->get_item_id(p_target.index) == p_target.item_id && popup->get_item_text(p_target.index) == p_target.fingerprint && popup->get_item_submenu(p_target.index).is_empty() && !popup->is_item_disabled(p_target.index) && !popup->is_item_separator(p_target.index)) {
			popup->activate_item(p_target.index);
			return OK;
		}
	} else if (p_target.kind == MCP_TARGET_OPTION_ITEM) {
		OptionButton *option = Object::cast_to<OptionButton>(control);
		if (p_action == "select" && option && p_target.index < option->get_item_count() && option->get_item_id(p_target.index) == p_target.item_id && option->get_item_text(p_target.index) == p_target.fingerprint && option->get_item_metadata(p_target.index) == p_target.metadata && !option->is_item_disabled(p_target.index)) {
			option->get_popup()->activate_item(p_target.index);
			return OK;
		}
	} else if (p_target.kind == MCP_TARGET_ITEM_LIST_ITEM) {
		ItemList *list = Object::cast_to<ItemList>(control);
		if (list && p_target.index < list->get_item_count() && list->get_item_text(p_target.index) == p_target.fingerprint && list->get_item_metadata(p_target.index) == p_target.metadata && !list->is_item_disabled(p_target.index) && list->is_item_selectable(p_target.index)) {
			if (p_action == "select") {
				if (list->get_select_mode() == ItemList::SELECT_SINGLE) {
					list->select(p_target.index);
					list->emit_signal("item_selected", p_target.index);
				} else if (list->get_select_mode() == ItemList::SELECT_TOGGLE && list->is_selected(p_target.index)) {
					list->deselect(p_target.index);
					list->set_current(p_target.index);
					list->emit_signal("multi_selected", p_target.index, false);
				} else {
					list->select(p_target.index, true);
					list->set_current(p_target.index);
					list->emit_signal("multi_selected", p_target.index, true);
				}
				return OK;
			}
			if (p_action == "activate") {
				list->select(p_target.index);
				list->emit_signal("item_activated", p_target.index);
				return OK;
			}
		}
	} else if (p_target.kind == MCP_TARGET_TAB) {
		TabBar *tabs = Object::cast_to<TabBar>(control);
		if (p_action == "select" && tabs && p_target.index < tabs->get_tab_count() && tabs->get_tab_title(p_target.index) == p_target.fingerprint && tabs->get_tab_metadata(p_target.index) == p_target.metadata && !tabs->is_tab_disabled(p_target.index) && !tabs->is_tab_hidden(p_target.index)) {
			tabs->set_current_tab(p_target.index);
			return OK;
		}
	} else if (p_target.kind == MCP_TARGET_TREE_CELL || p_target.kind == MCP_TARGET_TREE_BUTTON) {
		Tree *tree = Object::cast_to<Tree>(control);
		TreeItem *item = ObjectDB::get_instance<TreeItem>(p_target.auxiliary_object_id);
		if (!tree || !item || item->get_tree() != tree || p_target.column < 0 || p_target.column >= tree->get_columns()) {
			return _fail("STALE_UI_SNAPSHOT", "The tree item no longer exists.", r_error_code, r_error_message, ERR_DOES_NOT_EXIST);
		}
		if (p_target.kind == MCP_TARGET_TREE_BUTTON) {
			if (p_action == "click" && p_target.button >= 0 && p_target.button < item->get_button_count(p_target.column) && item->get_button_id(p_target.column, p_target.button) == p_target.item_id && !item->is_button_disabled(p_target.column, p_target.button)) {
				tree->emit_signal("button_clicked", item, p_target.column, item->get_button_id(p_target.column, p_target.button), MouseButton::LEFT);
				return OK;
			}
			return _fail("UI_ACTION_UNAVAILABLE", "A tree button only accepts click.", r_error_code, r_error_message, ERR_UNAVAILABLE);
		}
		if (p_action == "select" && item->is_selectable(p_target.column)) {
			item->select(p_target.column);
			return OK;
		}
		if (p_action == "expand" || p_action == "collapse") {
			item->set_collapsed(p_action == "collapse");
			return OK;
		}
	}

	return _fail("UI_ACTION_UNAVAILABLE", "The requested action is not available for this target.", r_error_code, r_error_message, ERR_UNAVAILABLE);
}
