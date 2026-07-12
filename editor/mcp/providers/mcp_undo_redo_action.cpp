/**************************************************************************/
/*  mcp_undo_redo_action.cpp                                              */
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

#include "mcp_undo_redo_action.h"

#include "mcp_scene_utils.h"

#include "editor/editor_undo_redo_manager.h"

MCPEditorUndoRedoAction::MCPEditorUndoRedoAction(EditorUndoRedoManager *p_undo_redo) :
		undo_redo(p_undo_redo) {
}

Error MCPEditorUndoRedoAction::begin_action(const String &p_name, String *r_error) {
	return MCPSceneUtils::begin_undo_action(undo_redo, p_name, r_error);
}

void MCPEditorUndoRedoAction::add_do_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_do_methodp(p_object, p_method, p_args, p_argcount);
}

void MCPEditorUndoRedoAction::add_undo_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_undo_methodp(p_object, p_method, p_args, p_argcount);
}

void MCPEditorUndoRedoAction::add_do_property(Object *p_object, const StringName &p_property, const Variant &p_value) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_do_property(p_object, p_property, p_value);
}

void MCPEditorUndoRedoAction::add_undo_property(Object *p_object, const StringName &p_property, const Variant &p_value) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_undo_property(p_object, p_property, p_value);
}

void MCPEditorUndoRedoAction::add_do_reference(Object *p_object) {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->add_do_reference(p_object);
}

void MCPEditorUndoRedoAction::commit_action() {
	ERR_FAIL_NULL(undo_redo);
	undo_redo->commit_action();
}
