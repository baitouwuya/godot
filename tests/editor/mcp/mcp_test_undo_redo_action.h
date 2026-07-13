/**************************************************************************/
/*  mcp_test_undo_redo_action.h                                           */
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

#include "core/object/undo_redo.h"
#include "core/variant/callable.h"
#include "editor/mcp/providers/mcp_undo_redo_action.h"

class MCPTestUndoRedoAction : public MCPUndoRedoAction {
public:
	MCPTestUndoRedoAction() {
		undo_redo = memnew(UndoRedo);
		saved_version = undo_redo->get_version();
	}

	~MCPTestUndoRedoAction() override {
		memdelete(undo_redo);
	}

	Error begin_action(const String &p_name, String *r_error) override {
		if (r_error) {
			*r_error = String();
		}
		undo_redo->create_action(p_name);
		return OK;
	}

	void add_do_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) override {
		undo_redo->add_do_method(Callable(p_object, p_method).bindp(p_args, p_argcount));
	}

	void add_undo_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) override {
		undo_redo->add_undo_method(Callable(p_object, p_method).bindp(p_args, p_argcount));
	}

	void add_do_property(Object *p_object, const StringName &p_property, const Variant &p_value) override {
		undo_redo->add_do_property(p_object, p_property, p_value);
	}

	void add_undo_property(Object *p_object, const StringName &p_property, const Variant &p_value) override {
		undo_redo->add_undo_property(p_object, p_property, p_value);
	}

	void add_do_reference(Object *p_object) override {
		undo_redo->add_do_reference(p_object);
	}

	void add_undo_reference(Object *p_object) override {
		undo_redo->add_undo_reference(p_object);
	}

	bool can_update_node_paths() const override {
		return true;
	}

	void add_node_path_updates(Node *, Node *, const StringName &) override {
	}

	void commit_action() override {
		undo_redo->commit_action();
	}

	bool undo() {
		return undo_redo->undo();
	}

	bool redo() {
		return undo_redo->redo();
	}

	bool is_unsaved() const {
		return undo_redo->get_version() != saved_version;
	}

	void clear_history() {
		undo_redo->clear_history(false);
		saved_version = undo_redo->get_version();
	}

private:
	UndoRedo *undo_redo = nullptr;
	uint64_t saved_version = 0;
};
