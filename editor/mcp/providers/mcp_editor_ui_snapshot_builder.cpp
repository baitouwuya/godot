/**************************************************************************/
/*  mcp_editor_ui_snapshot_builder.cpp                                   */
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

#include "mcp_editor_ui_snapshot_builder.h"

#include "core/object/object.h"
#include "editor/editor_node.h"
#include "editor/mcp/providers/mcp_editor_ui_target_validator.h"
#include "scene/gui/base_button.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/link_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/range.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

namespace {

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

static bool _is_actionable(Control *p_control) {
	return Object::cast_to<BaseButton>(p_control) || Object::cast_to<LineEdit>(p_control) || Object::cast_to<TextEdit>(p_control) ||
			MCPEditorUITargetValidator::is_editable_range(p_control) || Object::cast_to<Tree>(p_control) ||
			Object::cast_to<ItemList>(p_control) || Object::cast_to<TabBar>(p_control) || p_control->get_focus_mode_with_override() != Control::FOCUS_NONE;
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

} // namespace

Window *MCPEditorUISnapshotBuilder::_resolve_scope() const {
	if (DisplayServer::get_singleton()) {
		const DisplayServerEnums::WindowID popup_id = DisplayServer::get_singleton()->window_get_active_popup();
		Window *popup = Window::get_from_id(popup_id);
		if (popup && popup->is_visible() && MCPEditorUITargetValidator::belongs_to_editor(popup)) {
			return popup;
		}
	}
	Window *scope = Window::get_focused_window();
	EditorNode *editor = EditorNode::get_singleton();
	Control *editor_root = editor ? editor->get_gui_base() : nullptr;
	if (!scope || !editor_root || (scope != editor->get_window() && !editor_root->is_ancestor_of(scope))) {
		scope = editor ? editor->get_window() : nullptr;
	}
	if (scope && scope->get_focused_subwindow() && scope->get_focused_subwindow()->is_visible() && MCPEditorUITargetValidator::belongs_to_editor(scope->get_focused_subwindow())) {
		scope = scope->get_focused_subwindow();
	}
	while (scope && scope->get_exclusive_child() && scope->get_exclusive_child()->is_visible()) {
		scope = scope->get_exclusive_child();
	}
	return scope;
}

ObjectID MCPEditorUISnapshotBuilder::resolve_scope_id() const {
	Window *scope = _resolve_scope();
	return scope ? scope->get_instance_id() : ObjectID();
}

String MCPEditorUISnapshotBuilder::_make_target_id(const MCPEditorUISnapshot &p_snapshot) const {
	return vformat("%s:target:%d", p_snapshot.id, p_snapshot.targets.size() + 1);
}

Dictionary MCPEditorUISnapshotBuilder::_describe_target(const MCPEditorUITarget &p_target, const String &p_target_id, bool p_include_values) const {
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
		item["disabled"] = MCPEditorUITargetValidator::is_disabled(control);
	}

	switch (p_target.kind) {
		case MCP_TARGET_CONTROL: {
			if (!control) {
				return Dictionary();
			}
			item["role"] = _role_for_control(control);
			item["name"] = MCPEditorUITargetValidator::display_name(control);
			item["path"] = String(control->get_path());
			item["actions"] = p_target.actions;
			if (BaseButton *button = Object::cast_to<BaseButton>(control); button && button->is_toggle_mode()) {
				item["checked"] = button->is_pressed();
			}
			if (p_include_values) {
				if (LineEdit *line_edit = Object::cast_to<LineEdit>(control); line_edit && !line_edit->is_secret()) {
					item["value"] = MCPEditorUITargetValidator::limit_text(line_edit->get_text());
				} else if (TextEdit *text_edit = Object::cast_to<TextEdit>(control); text_edit && MCPEditorUITargetValidator::is_safe_text_edit(control)) {
					item["value"] = MCPEditorUITargetValidator::limit_text(text_edit->get_text());
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
		case MCP_TARGET_POPUP_ITEM: {
			PopupMenu *popup = Object::cast_to<PopupMenu>(object);
			item["role"] = "menu_item";
			item["name"] = popup ? MCPEditorUITargetValidator::limit_text(popup->get_item_text(p_target.index)) : String();
			item["index"] = p_target.index;
			item["disabled"] = !popup || popup->is_item_disabled(p_target.index);
			item["actions"] = p_target.actions;
			if (popup && popup->is_item_checkable(p_target.index)) {
				item["checked"] = popup->is_item_checked(p_target.index);
			}
		} break;
		case MCP_TARGET_OPTION_ITEM: {
			OptionButton *option = Object::cast_to<OptionButton>(control);
			item["role"] = "option";
			item["name"] = option ? MCPEditorUITargetValidator::limit_text(option->get_item_text(p_target.index)) : String();
			item["index"] = p_target.index;
			item["selected"] = option && option->get_selected() == p_target.index;
			item["disabled"] = !option || option->is_item_disabled(p_target.index);
			item["actions"] = p_target.actions;
		} break;
		case MCP_TARGET_ITEM_LIST_ITEM: {
			ItemList *list = Object::cast_to<ItemList>(control);
			item["role"] = "list_item";
			item["name"] = list ? MCPEditorUITargetValidator::limit_text(list->get_item_text(p_target.index)) : String();
			item["index"] = p_target.index;
			item["selected"] = list && list->is_selected(p_target.index);
			item["disabled"] = !list || list->is_item_disabled(p_target.index) || !list->is_item_selectable(p_target.index);
			item["actions"] = p_target.actions;
		} break;
		case MCP_TARGET_TAB: {
			TabBar *tabs = Object::cast_to<TabBar>(control);
			item["role"] = "tab";
			item["name"] = tabs ? MCPEditorUITargetValidator::limit_text(tabs->get_tab_title(p_target.index)) : String();
			item["index"] = p_target.index;
			item["selected"] = tabs && tabs->get_current_tab() == p_target.index;
			item["disabled"] = !tabs || tabs->is_tab_disabled(p_target.index);
			item["actions"] = p_target.actions;
		} break;
		case MCP_TARGET_TREE_CELL:
		case MCP_TARGET_TREE_BUTTON: {
			TreeItem *tree_item = ObjectDB::get_instance<TreeItem>(p_target.auxiliary_object_id);
			item["role"] = p_target.kind == MCP_TARGET_TREE_BUTTON ? "tree_button" : "tree_cell";
			item["name"] = tree_item ? MCPEditorUITargetValidator::limit_text(tree_item->get_text(p_target.column)) : String();
			item["column"] = p_target.column;
			item["disabled"] = !tree_item;
			if (p_target.kind == MCP_TARGET_TREE_BUTTON) {
				item["button"] = p_target.button;
			}
			item["actions"] = p_target.actions;
		} break;
	}
	return item;
}

void MCPEditorUISnapshotBuilder::_append_children(Node *p_node, const MCPEditorUISnapshotOptions &p_options, MCPEditorUISnapshot &r_snapshot, Array &r_items, bool &r_truncated) const {
	if (PopupMenu *popup = Object::cast_to<PopupMenu>(p_node)) {
		for (int i = 0; i < popup->get_item_count() && r_items.size() < p_options.limit; i++) {
			if (popup->is_item_separator(i) || !popup->get_item_submenu(i).is_empty() || (!p_options.include_disabled && popup->is_item_disabled(i))) {
				continue;
			}
			MCPEditorUITarget target;
			target.kind = MCP_TARGET_POPUP_ITEM;
			target.object_id = popup->get_instance_id();
			target.index = i;
			target.item_id = popup->get_item_id(i);
			target.fingerprint = popup->get_item_text(i);
			target.actions.push_back("activate");
			const String id = _make_target_id(r_snapshot);
			r_snapshot.targets[id] = target;
			r_items.push_back(_describe_target(target, id, p_options.include_values));
		}
	}
	if (OptionButton *option = Object::cast_to<OptionButton>(p_node)) {
		for (int i = 0; i < option->get_item_count() && r_items.size() < p_options.limit; i++) {
			if (!p_options.include_disabled && option->is_item_disabled(i)) {
				continue;
			}
			MCPEditorUITarget target;
			target.kind = MCP_TARGET_OPTION_ITEM;
			target.object_id = option->get_instance_id();
			target.index = i;
			target.item_id = option->get_item_id(i);
			target.fingerprint = option->get_item_text(i);
			target.metadata = option->get_item_metadata(i);
			target.actions.push_back("select");
			const String id = _make_target_id(r_snapshot);
			r_snapshot.targets[id] = target;
			r_items.push_back(_describe_target(target, id, p_options.include_values));
		}
	}
	if (ItemList *list = Object::cast_to<ItemList>(p_node)) {
		for (int i = 0; i < list->get_item_count() && r_items.size() < p_options.limit; i++) {
			if (!p_options.include_disabled && (list->is_item_disabled(i) || !list->is_item_selectable(i))) {
				continue;
			}
			MCPEditorUITarget target;
			target.kind = MCP_TARGET_ITEM_LIST_ITEM;
			target.object_id = list->get_instance_id();
			target.index = i;
			target.fingerprint = list->get_item_text(i);
			target.metadata = list->get_item_metadata(i);
			target.actions.push_back("select");
			target.actions.push_back("activate");
			const String id = _make_target_id(r_snapshot);
			r_snapshot.targets[id] = target;
			r_items.push_back(_describe_target(target, id, p_options.include_values));
		}
	}
	if (TabBar *tabs = Object::cast_to<TabBar>(p_node)) {
		for (int i = 0; i < tabs->get_tab_count() && r_items.size() < p_options.limit; i++) {
			if (tabs->is_tab_hidden(i) || (!p_options.include_disabled && tabs->is_tab_disabled(i))) {
				continue;
			}
			MCPEditorUITarget target;
			target.kind = MCP_TARGET_TAB;
			target.object_id = tabs->get_instance_id();
			target.index = i;
			target.fingerprint = tabs->get_tab_title(i);
			target.metadata = tabs->get_tab_metadata(i);
			target.actions.push_back("select");
			const String id = _make_target_id(r_snapshot);
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
				MCPEditorUITarget cell;
				cell.kind = MCP_TARGET_TREE_CELL;
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
				const String cell_id = _make_target_id(r_snapshot);
				r_snapshot.targets[cell_id] = cell;
				r_items.push_back(_describe_target(cell, cell_id, p_options.include_values));
				for (int button = 0; button < tree_item->get_button_count(column) && r_items.size() < p_options.limit; button++) {
					if (!p_options.include_disabled && tree_item->is_button_disabled(column, button)) {
						continue;
					}
					MCPEditorUITarget target = cell;
					target.kind = MCP_TARGET_TREE_BUTTON;
					target.button = button;
					target.item_id = tree_item->get_button_id(column, button);
					target.actions.clear();
					target.actions.push_back("click");
					const String id = _make_target_id(r_snapshot);
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

void MCPEditorUISnapshotBuilder::_append_node(Node *p_node, int p_depth, const MCPEditorUISnapshotOptions &p_options, MCPEditorUISnapshot &r_snapshot, Array &r_items, bool &r_truncated) const {
	if (!p_node || r_truncated || p_depth > p_options.max_depth) {
		return;
	}
	Control *control = Object::cast_to<Control>(p_node);
	Window *window = Object::cast_to<Window>(p_node);
	if ((control && (!control->is_visible_in_tree() || control->get_screen_rect().size.x <= 0 || control->get_screen_rect().size.y <= 0)) || (window && !window->is_visible())) {
		return;
	}
	if (control && _is_actionable(control) && (p_options.include_disabled || !MCPEditorUITargetValidator::is_disabled(control))) {
		MCPEditorUITarget target;
		target.object_id = control->get_instance_id();
		target.fingerprint = control->get_class();
		target.name_fingerprint = MCPEditorUITargetValidator::display_name(control);
		target.metadata = control->get_path();
		target.actions = MCPEditorUITargetValidator::actions_for_control(control);
		const String id = _make_target_id(r_snapshot);
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

Dictionary MCPEditorUISnapshotBuilder::build(const MCPEditorUISnapshotOptions &p_options, MCPEditorUISnapshot &r_snapshot) const {
	Window *scope = _resolve_scope();
	r_snapshot.scope_id = scope ? scope->get_instance_id() : ObjectID();

	Array items;
	bool truncated = false;
	EditorNode *editor = EditorNode::get_singleton();
	Node *root = editor ? editor->get_gui_base() : nullptr;
	if (scope && editor && scope != editor->get_window() && MCPEditorUITargetValidator::belongs_to_editor(scope)) {
		root = scope;
	}
	if (root) {
		_append_node(root, 0, p_options, r_snapshot, items, truncated);
	}

	Dictionary result;
	result["snapshotId"] = r_snapshot.id;
	result["windowTitle"] = scope ? scope->get_title() : String();
	result["items"] = items;
	result["truncated"] = truncated;
	result["count"] = items.size();
	return result;
}
