/**************************************************************************/
/*  mcp_editor_ui_service.cpp                                             */
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

#include "mcp_editor_ui_service.h"

#include "core/math/math_funcs.h"
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
#include "servers/display/display_server.h"

namespace {

static constexpr int MAX_TEXT_LENGTH = 1024;

static String _limited(const String &p_text) {
	return p_text.length() > MAX_TEXT_LENGTH ? p_text.left(MAX_TEXT_LENGTH) : p_text;
}

static Dictionary _bounds(Control *p_control) {
	const Rect2 rect = p_control->get_screen_rect();
	Dictionary bounds;
	bounds["x"] = rect.position.x;
	bounds["y"] = rect.position.y;
	bounds["width"] = rect.size.x;
	bounds["height"] = rect.size.y;
	return bounds;
}

static String _display_name(Control *p_control) {
	String name = p_control->get_accessibility_name().strip_edges();
	if (!name.is_empty()) {
		return _limited(name);
	}
	if (Button *button = Object::cast_to<Button>(p_control)) {
		return _limited(button->get_text().strip_edges());
	}
	if (LinkButton *button = Object::cast_to<LinkButton>(p_control)) {
		return _limited(button->get_text().strip_edges());
	}
	if (!p_control->get_tooltip_text().is_empty()) {
		return _limited(p_control->get_tooltip_text().strip_edges());
	}
	return _limited(String(p_control->get_name()));
}

static String _role_for_control(Control *p_control) {
	if (Object::cast_to<OptionButton>(p_control)) {
		return "select";
	}
	if (Object::cast_to<BaseButton>(p_control)) {
		return "button";
	}
	if (Object::cast_to<LineEdit>(p_control)) {
		return "text_field";
	}
	if (Object::cast_to<TextEdit>(p_control)) {
		return "text_area";
	}
	if (Object::cast_to<Range>(p_control)) {
		return "range";
	}
	if (Object::cast_to<Tree>(p_control)) {
		return "tree";
	}
	if (Object::cast_to<ItemList>(p_control)) {
		return "list";
	}
	if (Object::cast_to<TabBar>(p_control)) {
		return "tab_list";
	}
	return "control";
}

static bool _is_editable_range(Control *p_control);

static bool _is_actionable(Control *p_control) {
	return Object::cast_to<BaseButton>(p_control) || Object::cast_to<LineEdit>(p_control) || Object::cast_to<TextEdit>(p_control) ||
			_is_editable_range(p_control) || Object::cast_to<Tree>(p_control) ||
			Object::cast_to<ItemList>(p_control) || Object::cast_to<TabBar>(p_control) || p_control->get_focus_mode_with_override() != Control::FOCUS_NONE;
}

static bool _is_safe_text_edit(Control *p_control) {
	return Object::cast_to<TextEdit>(p_control) && !Object::cast_to<CodeEdit>(p_control);
}

static bool _is_editable_range(Control *p_control) {
	if (Slider *slider = Object::cast_to<Slider>(p_control)) {
		return slider->is_editable();
	}
	if (SpinBox *spin_box = Object::cast_to<SpinBox>(p_control)) {
		return spin_box->is_editable();
	}
	return false;
}

static bool _is_disabled(Control *p_control) {
	if (BaseButton *button = Object::cast_to<BaseButton>(p_control)) {
		return button->is_disabled();
	}
	return false;
}

static PackedStringArray _actions_for_control(Control *p_control) {
	PackedStringArray actions;
	if (p_control->get_focus_mode_with_override() != Control::FOCUS_NONE) {
		actions.push_back("focus");
	}
	if (Object::cast_to<BaseButton>(p_control)) {
		actions.push_back("click");
	}
	LineEdit *line_edit = Object::cast_to<LineEdit>(p_control);
	TextEdit *text_edit = Object::cast_to<TextEdit>(p_control);
	if ((line_edit && line_edit->is_editable()) || (text_edit && _is_safe_text_edit(p_control) && text_edit->is_editable()) || _is_editable_range(p_control)) {
		actions.push_back("set_value");
	}
	if (_is_editable_range(p_control)) {
		actions.push_back("increment");
		actions.push_back("decrement");
	}
	return actions;
}

static bool _try_get_number(const Dictionary &p_arguments, const StringName &p_name, double &r_value) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) {
		return false;
	}
	r_value = value;
	return Math::is_finite(r_value);
}

} // namespace

Window *MCPEditorUIService::_resolve_scope() const {
	if (DisplayServer::get_singleton()) {
		const DisplayServerEnums::WindowID popup_id = DisplayServer::get_singleton()->window_get_active_popup();
		Window *popup = Window::get_from_id(popup_id);
		if (popup && popup->is_visible() && _belongs_to_editor(popup)) {
			return popup;
		}
	}
	Window *scope = Window::get_focused_window();
	EditorNode *editor = EditorNode::get_singleton();
	Control *editor_root = editor ? editor->get_gui_base() : nullptr;
	if (!scope || !editor_root || (scope != editor->get_window() && !editor_root->is_ancestor_of(scope))) {
		scope = editor ? editor->get_window() : nullptr;
	}
	if (scope && scope->get_focused_subwindow() && scope->get_focused_subwindow()->is_visible() && _belongs_to_editor(scope->get_focused_subwindow())) {
		scope = scope->get_focused_subwindow();
	}
	while (scope && scope->get_exclusive_child() && scope->get_exclusive_child()->is_visible()) {
		scope = scope->get_exclusive_child();
	}
	return scope;
}

bool MCPEditorUIService::_belongs_to_editor(Node *p_node) const {
	EditorNode *editor = EditorNode::get_singleton();
	Control *root = editor ? editor->get_gui_base() : nullptr;
	return p_node && root && (p_node == root || root->is_ancestor_of(p_node));
}

String MCPEditorUIService::_make_target_id(const Snapshot &p_snapshot, const Target &p_target, const String &p_suffix) const {
	return vformat("%s:target:%d", p_snapshot.id, p_snapshot.targets.size() + 1);
}

Dictionary MCPEditorUIService::_describe_target(const Target &p_target, const String &p_target_id, bool p_include_values) const {
	Dictionary item;
	item["targetId"] = p_target_id;
	Object *object = ObjectDB::get_instance(p_target.object_id);
	Control *control = Object::cast_to<Control>(object);
	if (!object) {
		return item;
	}
	item["class"] = object->get_class();
	if (control) {
		item["bounds"] = _bounds(control);
		item["focused"] = control->has_focus();
		item["disabled"] = _is_disabled(control);
	}

	switch (p_target.kind) {
		case TARGET_CONTROL: {
			if (!control) {
				return Dictionary();
			}
			item["role"] = _role_for_control(control);
			item["name"] = _display_name(control);
			item["path"] = String(control->get_path());
			item["actions"] = p_target.actions;
			if (BaseButton *button = Object::cast_to<BaseButton>(control); button && button->is_toggle_mode()) {
				item["checked"] = button->is_pressed();
			}
			if (p_include_values) {
				if (LineEdit *line_edit = Object::cast_to<LineEdit>(control); line_edit && !line_edit->is_secret()) {
					item["value"] = _limited(line_edit->get_text());
				} else if (TextEdit *text_edit = Object::cast_to<TextEdit>(control); text_edit && !Object::cast_to<CodeEdit>(control)) {
					item["value"] = _limited(text_edit->get_text());
				} else if (Range *range = Object::cast_to<Range>(control)) {
					item["value"] = range->get_value();
					item["minimum"] = range->get_min();
					item["maximum"] = range->get_max();
					item["step"] = range->get_step();
				} else if (OptionButton *option = Object::cast_to<OptionButton>(control)) {
					item["selected"] = option->get_selected();
				}
			}
		} break;
		case TARGET_POPUP_ITEM: {
			PopupMenu *popup = Object::cast_to<PopupMenu>(object);
			item["role"] = "menu_item";
			item["name"] = popup ? _limited(popup->get_item_text(p_target.index)) : String();
			item["index"] = p_target.index;
			item["disabled"] = !popup || popup->is_item_disabled(p_target.index);
			item["actions"] = p_target.actions;
			if (popup && popup->is_item_checkable(p_target.index)) {
				item["checked"] = popup->is_item_checked(p_target.index);
			}
		} break;
		case TARGET_OPTION_ITEM: {
			OptionButton *option = Object::cast_to<OptionButton>(control);
			item["role"] = "option";
			item["name"] = option ? _limited(option->get_item_text(p_target.index)) : String();
			item["index"] = p_target.index;
			item["selected"] = option && option->get_selected() == p_target.index;
			item["disabled"] = !option || option->is_item_disabled(p_target.index);
			item["actions"] = p_target.actions;
		} break;
		case TARGET_ITEM_LIST_ITEM: {
			ItemList *list = Object::cast_to<ItemList>(control);
			item["role"] = "list_item";
			item["name"] = list ? _limited(list->get_item_text(p_target.index)) : String();
			item["index"] = p_target.index;
			item["selected"] = list && list->is_selected(p_target.index);
			item["disabled"] = !list || list->is_item_disabled(p_target.index) || !list->is_item_selectable(p_target.index);
			item["actions"] = p_target.actions;
		} break;
		case TARGET_TAB: {
			TabBar *tabs = Object::cast_to<TabBar>(control);
			item["role"] = "tab";
			item["name"] = tabs ? _limited(tabs->get_tab_title(p_target.index)) : String();
			item["index"] = p_target.index;
			item["selected"] = tabs && tabs->get_current_tab() == p_target.index;
			item["disabled"] = !tabs || tabs->is_tab_disabled(p_target.index);
			item["actions"] = p_target.actions;
		} break;
		case TARGET_TREE_CELL:
		case TARGET_TREE_BUTTON: {
			TreeItem *tree_item = ObjectDB::get_instance<TreeItem>(p_target.auxiliary_object_id);
			item["role"] = p_target.kind == TARGET_TREE_BUTTON ? "tree_button" : "tree_cell";
			item["name"] = tree_item ? _limited(tree_item->get_text(p_target.column)) : String();
			item["column"] = p_target.column;
			item["disabled"] = !tree_item;
			if (p_target.kind == TARGET_TREE_BUTTON) {
				item["button"] = p_target.button;
			}
			item["actions"] = p_target.actions;
		} break;
	}
	return item;
}

bool MCPEditorUIService::_target_is_current(const Target &p_target) const {
	Object *object = ObjectDB::get_instance(p_target.object_id);
	Control *control = Object::cast_to<Control>(object);
	switch (p_target.kind) {
		case TARGET_CONTROL:
			return control && control->get_class() == StringName(p_target.fingerprint) && control->get_path() == NodePath(p_target.metadata) && _display_name(control) == p_target.name_fingerprint && _actions_for_control(control) == p_target.actions;
		case TARGET_POPUP_ITEM: {
			PopupMenu *popup = Object::cast_to<PopupMenu>(object);
			return popup && p_target.index >= 0 && p_target.index < popup->get_item_count() && popup->get_item_id(p_target.index) == p_target.item_id && popup->get_item_text(p_target.index) == p_target.fingerprint && popup->get_item_submenu(p_target.index).is_empty();
		}
		case TARGET_OPTION_ITEM: {
			OptionButton *option = Object::cast_to<OptionButton>(control);
			return option && p_target.index >= 0 && p_target.index < option->get_item_count() && option->get_item_id(p_target.index) == p_target.item_id && option->get_item_text(p_target.index) == p_target.fingerprint && option->get_item_metadata(p_target.index) == p_target.metadata;
		}
		case TARGET_ITEM_LIST_ITEM: {
			ItemList *list = Object::cast_to<ItemList>(control);
			return list && p_target.index >= 0 && p_target.index < list->get_item_count() && list->get_item_text(p_target.index) == p_target.fingerprint && list->get_item_metadata(p_target.index) == p_target.metadata;
		}
		case TARGET_TAB: {
			TabBar *tabs = Object::cast_to<TabBar>(control);
			return tabs && p_target.index >= 0 && p_target.index < tabs->get_tab_count() && tabs->get_tab_title(p_target.index) == p_target.fingerprint && tabs->get_tab_metadata(p_target.index) == p_target.metadata;
		}
		case TARGET_TREE_CELL:
		case TARGET_TREE_BUTTON: {
			Tree *tree = Object::cast_to<Tree>(control);
			TreeItem *item = ObjectDB::get_instance<TreeItem>(p_target.auxiliary_object_id);
			return tree && item && item->get_tree() == tree && p_target.column >= 0 && p_target.column < tree->get_columns() && item->get_text(p_target.column) == p_target.fingerprint && (p_target.kind != TARGET_TREE_BUTTON || (p_target.button >= 0 && p_target.button < item->get_button_count(p_target.column) && item->get_button_id(p_target.column, p_target.button) == p_target.item_id));
		}
	}
	return false;
}

void MCPEditorUIService::_append_children(Node *p_node, const SnapshotOptions &p_options, Snapshot &r_snapshot, Array &r_items, bool &r_truncated) {
	if (PopupMenu *popup = Object::cast_to<PopupMenu>(p_node)) {
		for (int i = 0; i < popup->get_item_count() && r_items.size() < p_options.limit; i++) {
			if (popup->is_item_separator(i) || !popup->get_item_submenu(i).is_empty() || (!p_options.include_disabled && popup->is_item_disabled(i))) {
				continue;
			}
			Target target;
			target.kind = TARGET_POPUP_ITEM;
			target.object_id = popup->get_instance_id();
			target.index = i;
			target.item_id = popup->get_item_id(i);
			target.fingerprint = popup->get_item_text(i);
			target.actions.push_back("activate");
			const String id = _make_target_id(r_snapshot, target, "menu:" + itos(i));
			r_snapshot.targets[id] = target;
			r_items.push_back(_describe_target(target, id, p_options.include_values));
		}
	}
	if (OptionButton *option = Object::cast_to<OptionButton>(p_node)) {
		for (int i = 0; i < option->get_item_count() && r_items.size() < p_options.limit; i++) {
			if (!p_options.include_disabled && option->is_item_disabled(i)) {
				continue;
			}
			Target target;
			target.kind = TARGET_OPTION_ITEM;
			target.object_id = option->get_instance_id();
			target.index = i;
			target.item_id = option->get_item_id(i);
			target.fingerprint = option->get_item_text(i);
			target.metadata = option->get_item_metadata(i);
			target.actions.push_back("select");
			const String id = _make_target_id(r_snapshot, target, "option:" + itos(i));
			r_snapshot.targets[id] = target;
			r_items.push_back(_describe_target(target, id, p_options.include_values));
		}
	}
	if (ItemList *list = Object::cast_to<ItemList>(p_node)) {
		for (int i = 0; i < list->get_item_count() && r_items.size() < p_options.limit; i++) {
			if (!p_options.include_disabled && (list->is_item_disabled(i) || !list->is_item_selectable(i))) {
				continue;
			}
			Target target;
			target.kind = TARGET_ITEM_LIST_ITEM;
			target.object_id = list->get_instance_id();
			target.index = i;
			target.fingerprint = list->get_item_text(i);
			target.metadata = list->get_item_metadata(i);
			target.actions.push_back("select");
			target.actions.push_back("activate");
			const String id = _make_target_id(r_snapshot, target, "item:" + itos(i));
			r_snapshot.targets[id] = target;
			r_items.push_back(_describe_target(target, id, p_options.include_values));
		}
	}
	if (TabBar *tabs = Object::cast_to<TabBar>(p_node)) {
		for (int i = 0; i < tabs->get_tab_count() && r_items.size() < p_options.limit; i++) {
			if (tabs->is_tab_hidden(i) || (!p_options.include_disabled && tabs->is_tab_disabled(i))) {
				continue;
			}
			Target target;
			target.kind = TARGET_TAB;
			target.object_id = tabs->get_instance_id();
			target.index = i;
			target.fingerprint = tabs->get_tab_title(i);
			target.metadata = tabs->get_tab_metadata(i);
			target.actions.push_back("select");
			const String id = _make_target_id(r_snapshot, target, "tab:" + itos(i));
			r_snapshot.targets[id] = target;
			r_items.push_back(_describe_target(target, id, p_options.include_values));
		}
	}
	if (Tree *tree = Object::cast_to<Tree>(p_node)) {
		for (TreeItem *tree_item = tree->get_root(); tree_item && r_items.size() < p_options.limit; tree_item = tree_item->get_next_visible()) {
			for (int column = 0; column < tree->get_columns() && r_items.size() < p_options.limit; column++) {
				if (!tree_item->is_selectable(column) && !tree_item->is_editable(column) && tree_item->get_button_count(column) == 0) {
					continue;
				}
				Target cell;
				cell.kind = TARGET_TREE_CELL;
				cell.object_id = tree->get_instance_id();
				cell.auxiliary_object_id = tree_item->get_instance_id();
				cell.column = column;
				cell.fingerprint = tree_item->get_text(column);
				if (tree_item->is_selectable(column)) {
					cell.actions.push_back("select");
				}
				if (tree_item->get_first_child()) {
					cell.actions.push_back(tree_item->is_collapsed() ? "expand" : "collapse");
				}
				const String cell_id = _make_target_id(r_snapshot, cell, String());
				r_snapshot.targets[cell_id] = cell;
				r_items.push_back(_describe_target(cell, cell_id, p_options.include_values));
				for (int button = 0; button < tree_item->get_button_count(column) && r_items.size() < p_options.limit; button++) {
					if (!p_options.include_disabled && tree_item->is_button_disabled(column, button)) {
						continue;
					}
					Target target = cell;
					target.kind = TARGET_TREE_BUTTON;
					target.button = button;
					target.item_id = tree_item->get_button_id(column, button);
					target.actions.clear();
					target.actions.push_back("click");
					const String id = _make_target_id(r_snapshot, target, String());
					r_snapshot.targets[id] = target;
					r_items.push_back(_describe_target(target, id, p_options.include_values));
				}
			}
		}
	}
	if (r_items.size() >= p_options.limit) {
		r_truncated = true;
	}
}

void MCPEditorUIService::_append_node(Node *p_node, int p_depth, const SnapshotOptions &p_options, Snapshot &r_snapshot, Array &r_items, bool &r_truncated) {
	if (!p_node || r_truncated || p_depth > p_options.max_depth) {
		return;
	}
	Control *control = Object::cast_to<Control>(p_node);
	Window *window = Object::cast_to<Window>(p_node);
	if ((control && (!control->is_visible_in_tree() || control->get_screen_rect().size.x <= 0 || control->get_screen_rect().size.y <= 0)) || (window && !window->is_visible())) {
		return;
	}
	if (control && _is_actionable(control) && (p_options.include_disabled || !_is_disabled(control))) {
		Target target;
		target.object_id = control->get_instance_id();
		target.fingerprint = control->get_class();
		target.name_fingerprint = _display_name(control);
		target.metadata = control->get_path();
		target.actions = _actions_for_control(control);
		const String id = _make_target_id(r_snapshot, target, "control");
		r_snapshot.targets[id] = target;
		r_items.push_back(_describe_target(target, id, p_options.include_values));
		if (r_items.size() >= p_options.limit) {
			r_truncated = true;
			return;
		}
	}
	if (control || Object::cast_to<PopupMenu>(p_node)) {
		_append_children(p_node, p_options, r_snapshot, r_items, r_truncated);
	}
	for (int i = 0; i < p_node->get_child_count() && !r_truncated; i++) {
		_append_node(p_node->get_child(i), p_depth + 1, p_options, r_snapshot, r_items, r_truncated);
	}
}

Dictionary MCPEditorUIService::capture(const String &p_session_id, const SnapshotOptions &p_options) {
	if (current_snapshot_by_session.has(p_session_id)) {
		snapshots.erase(current_snapshot_by_session[p_session_id]);
	}
	Snapshot snapshot;
	snapshot.id = "ui-" + String::num_uint64(next_snapshot_sequence++);
	snapshot.session_id = p_session_id;
	Window *scope = _resolve_scope();
	snapshot.scope_id = scope ? scope->get_instance_id() : ObjectID();

	Array items;
	bool truncated = false;
	EditorNode *editor = EditorNode::get_singleton();
	Node *root = editor ? editor->get_gui_base() : nullptr;
	if (scope && editor && scope != editor->get_window() && _belongs_to_editor(scope)) {
		root = scope;
	}
	if (root) {
		_append_node(root, 0, p_options, snapshot, items, truncated);
	}

	Dictionary result;
	result["snapshotId"] = snapshot.id;
	result["windowTitle"] = scope ? scope->get_title() : String();
	result["items"] = items;
	result["truncated"] = truncated;
	result["count"] = items.size();
	current_snapshot_by_session[p_session_id] = snapshot.id;
	snapshots[snapshot.id] = snapshot;
	return result;
}

Error MCPEditorUIService::_perform_target(const Target &p_target, const String &p_action, const Dictionary &p_arguments, String &r_error_code, String &r_error_message) {
	Object *object = ObjectDB::get_instance(p_target.object_id);
	Node *node = Object::cast_to<Node>(object);
	Control *control = Object::cast_to<Control>(object);
	Window *window = Object::cast_to<Window>(object);
	if (!node || (control && !control->is_visible_in_tree()) || (window && !window->is_visible()) || !_belongs_to_editor(node)) {
		r_error_code = "STALE_UI_SNAPSHOT";
		r_error_message = "The target no longer belongs to the visible editor UI.";
		return ERR_DOES_NOT_EXIST;
	}
	if (control && _is_disabled(control)) {
		r_error_code = "UI_TARGET_DISABLED";
		r_error_message = "The target control is disabled.";
		return ERR_UNAVAILABLE;
	}

	if (p_target.kind == TARGET_CONTROL) {
		if (p_action == "focus" && control->get_focus_mode_with_override() != Control::FOCUS_NONE) {
			control->grab_focus();
			return OK;
		}
		if (p_action == "click" && Object::cast_to<BaseButton>(control)) {
			BaseButton *button = Object::cast_to<BaseButton>(control);
			button->activate();
			return OK;
		}
		if (p_action == "set_value") {
			const Variant value = p_arguments.get("value", Variant());
			if (LineEdit *line_edit = Object::cast_to<LineEdit>(control)) {
				if (!line_edit->is_editable() || value.get_type() != Variant::STRING) {
					r_error_code = "INVALID_UI_VALUE";
					r_error_message = "An editable LineEdit requires a string value.";
					return ERR_INVALID_PARAMETER;
				}
				line_edit->call("_set_text", value, true);
				return OK;
			}
			if (TextEdit *text_edit = Object::cast_to<TextEdit>(control); text_edit && !Object::cast_to<CodeEdit>(control)) {
				if (!text_edit->is_editable() || value.get_type() != Variant::STRING) {
					r_error_code = "INVALID_UI_VALUE";
					r_error_message = "An editable TextEdit requires a string value.";
					return ERR_INVALID_PARAMETER;
				}
				text_edit->call("_set_text", value, true);
				return OK;
			}
			if (Range *range = Object::cast_to<Range>(control); range && _is_editable_range(control)) {
				double number = 0;
				if (!_try_get_number(p_arguments, "value", number)) {
					r_error_code = "INVALID_UI_VALUE";
					r_error_message = "A Range requires a finite numeric value.";
					return ERR_INVALID_PARAMETER;
				}
				range->set_value(number);
				return OK;
			}
		}
		if ((p_action == "increment" || p_action == "decrement") && _is_editable_range(control)) {
			Range *range = Object::cast_to<Range>(control);
			range->set_value(range->get_value() + (p_action == "increment" ? range->get_step() : -range->get_step()));
			return OK;
		}
	} else if (p_target.kind == TARGET_POPUP_ITEM) {
		PopupMenu *popup = Object::cast_to<PopupMenu>(object);
		if (p_action == "activate" && popup && p_target.index < popup->get_item_count() && popup->get_item_id(p_target.index) == p_target.item_id && popup->get_item_text(p_target.index) == p_target.fingerprint && popup->get_item_submenu(p_target.index).is_empty() && !popup->is_item_disabled(p_target.index) && !popup->is_item_separator(p_target.index)) {
			popup->activate_item(p_target.index);
			return OK;
		}
	} else if (p_target.kind == TARGET_OPTION_ITEM) {
		OptionButton *option = Object::cast_to<OptionButton>(control);
		if (p_action == "select" && option && p_target.index < option->get_item_count() && option->get_item_id(p_target.index) == p_target.item_id && option->get_item_text(p_target.index) == p_target.fingerprint && option->get_item_metadata(p_target.index) == p_target.metadata && !option->is_item_disabled(p_target.index)) {
			option->get_popup()->activate_item(p_target.index);
			return OK;
		}
	} else if (p_target.kind == TARGET_ITEM_LIST_ITEM) {
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
	} else if (p_target.kind == TARGET_TAB) {
		TabBar *tabs = Object::cast_to<TabBar>(control);
		if (p_action == "select" && tabs && p_target.index < tabs->get_tab_count() && tabs->get_tab_title(p_target.index) == p_target.fingerprint && tabs->get_tab_metadata(p_target.index) == p_target.metadata && !tabs->is_tab_disabled(p_target.index) && !tabs->is_tab_hidden(p_target.index)) {
			tabs->set_current_tab(p_target.index);
			return OK;
		}
	} else if (p_target.kind == TARGET_TREE_CELL || p_target.kind == TARGET_TREE_BUTTON) {
		Tree *tree = Object::cast_to<Tree>(control);
		TreeItem *item = ObjectDB::get_instance<TreeItem>(p_target.auxiliary_object_id);
		if (!tree || !item || item->get_tree() != tree || p_target.column < 0 || p_target.column >= tree->get_columns()) {
			r_error_code = "STALE_UI_SNAPSHOT";
			r_error_message = "The tree item no longer exists.";
			return ERR_DOES_NOT_EXIST;
		}
		if (p_target.kind == TARGET_TREE_BUTTON) {
			if (p_action == "click" && p_target.button >= 0 && p_target.button < item->get_button_count(p_target.column) && item->get_button_id(p_target.column, p_target.button) == p_target.item_id && !item->is_button_disabled(p_target.column, p_target.button)) {
				tree->emit_signal("button_clicked", item, p_target.column, item->get_button_id(p_target.column, p_target.button), MouseButton::LEFT);
				return OK;
			}
			r_error_code = "UI_ACTION_UNAVAILABLE";
			r_error_message = "A tree button only accepts click.";
			return ERR_UNAVAILABLE;
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

	r_error_code = "UI_ACTION_UNAVAILABLE";
	r_error_message = "The requested action is not available for this target.";
	return ERR_UNAVAILABLE;
}

Error MCPEditorUIService::perform(const String &p_session_id, const String &p_snapshot_id, const String &p_target_id, const String &p_action, const Dictionary &p_arguments, Dictionary &r_result, String &r_error_code, String &r_error_message) {
	Snapshot *snapshot = snapshots.getptr(p_snapshot_id);
	const String *current_snapshot = current_snapshot_by_session.getptr(p_session_id);
	if (!snapshot || snapshot->session_id != p_session_id || !current_snapshot || *current_snapshot != p_snapshot_id) {
		r_error_code = "STALE_UI_SNAPSHOT";
		r_error_message = "The UI snapshot is stale or belongs to another MCP session.";
		return ERR_DOES_NOT_EXIST;
	}
	Window *scope = _resolve_scope();
	if ((scope ? scope->get_instance_id() : ObjectID()) != snapshot->scope_id) {
		r_error_code = "STALE_UI_SNAPSHOT";
		r_error_message = "The active editor window changed after the snapshot was captured.";
		return ERR_DOES_NOT_EXIST;
	}
	Target *target = snapshot->targets.getptr(p_target_id);
	if (!target) {
		r_error_code = "UI_TARGET_NOT_FOUND";
		r_error_message = "The target is not present in this UI snapshot.";
		return ERR_DOES_NOT_EXIST;
	}
	if (!target->actions.has(p_action)) {
		release_session(p_session_id);
		r_error_code = "UI_ACTION_UNAVAILABLE";
		r_error_message = "The requested action was not advertised for this target.";
		return ERR_UNAVAILABLE;
	}
	if (!_target_is_current(*target)) {
		release_session(p_session_id);
		r_error_code = "STALE_UI_SNAPSHOT";
		r_error_message = "The target changed after the UI snapshot was captured.";
		return ERR_DOES_NOT_EXIST;
	}
	const Error error = _perform_target(*target, p_action, p_arguments, r_error_code, r_error_message);
	if (error != OK) {
		return error;
	}
	release_session(p_session_id);
	r_result["performed"] = true;
	r_result["action"] = p_action;
	r_result["targetId"] = p_target_id;
	r_result["snapshotInvalidated"] = true;
	return OK;
}

void MCPEditorUIService::release_session(const String &p_session_id) {
	if (current_snapshot_by_session.has(p_session_id)) {
		snapshots.erase(current_snapshot_by_session[p_session_id]);
		current_snapshot_by_session.erase(p_session_id);
	}
}

void MCPEditorUIService::clear() {
	snapshots.clear();
	current_snapshot_by_session.clear();
}
