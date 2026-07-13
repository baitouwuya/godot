/**************************************************************************/
/*  mcp_undo_redo_action.h                                                */
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

#pragma once

#include "core/error/error_list.h"
#include "core/object/object.h"

class EditorUndoRedoManager;
class Node;

class MCPUndoRedoAction {
public:
	virtual ~MCPUndoRedoAction() = default;

	virtual Error begin_action(const String &p_name, String *r_error = nullptr) = 0;
	virtual void add_do_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) = 0;
	virtual void add_undo_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) = 0;

	template <typename... VarArgs>
	void add_do_method(Object *p_object, const StringName &p_method, VarArgs... p_args) {
		Variant args[sizeof...(p_args) + 1] = { p_args..., Variant() };
		const Variant *argptrs[sizeof...(p_args) + 1];
		for (uint32_t i = 0; i < sizeof...(p_args); i++) {
			argptrs[i] = &args[i];
		}
		add_do_methodp(p_object, p_method, sizeof...(p_args) == 0 ? nullptr : argptrs, sizeof...(p_args));
	}

	template <typename... VarArgs>
	void add_undo_method(Object *p_object, const StringName &p_method, VarArgs... p_args) {
		Variant args[sizeof...(p_args) + 1] = { p_args..., Variant() };
		const Variant *argptrs[sizeof...(p_args) + 1];
		for (uint32_t i = 0; i < sizeof...(p_args); i++) {
			argptrs[i] = &args[i];
		}
		add_undo_methodp(p_object, p_method, sizeof...(p_args) == 0 ? nullptr : argptrs, sizeof...(p_args));
	}

	virtual void add_do_property(Object *p_object, const StringName &p_property, const Variant &p_value) = 0;
	virtual void add_undo_property(Object *p_object, const StringName &p_property, const Variant &p_value) = 0;
	virtual void add_do_reference(Object *p_object) = 0;
	virtual void add_undo_reference(Object *p_object) = 0;
	virtual bool can_update_node_paths() const = 0;
	virtual void add_node_path_updates(Node *p_node, Node *p_new_parent, const StringName &p_new_name = StringName()) = 0;
	virtual void commit_action() = 0;
};

class MCPEditorUndoRedoAction : public MCPUndoRedoAction {
public:
	explicit MCPEditorUndoRedoAction(EditorUndoRedoManager *p_undo_redo);

	Error begin_action(const String &p_name, String *r_error = nullptr) override;
	void add_do_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) override;
	void add_undo_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) override;
	void add_do_property(Object *p_object, const StringName &p_property, const Variant &p_value) override;
	void add_undo_property(Object *p_object, const StringName &p_property, const Variant &p_value) override;
	void add_do_reference(Object *p_object) override;
	void add_undo_reference(Object *p_object) override;
	bool can_update_node_paths() const override;
	void add_node_path_updates(Node *p_node, Node *p_new_parent, const StringName &p_new_name = StringName()) override;
	void commit_action() override;

private:
	EditorUndoRedoManager *undo_redo = nullptr;
};
