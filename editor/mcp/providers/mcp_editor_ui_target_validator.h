/**************************************************************************/
/*  mcp_editor_ui_target_validator.h                                      */
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

#pragma once

#include "mcp_editor_ui_types.h"

class Control;
class Node;

class MCPEditorUITargetValidator {
public:
	static String limit_text(const String &p_text);
	static String display_name(Control *p_control);
	static PackedStringArray actions_for_control(Control *p_control);
	static bool is_editable_range(Control *p_control);
	static bool is_disabled(Control *p_control);
	static bool is_safe_text_edit(Control *p_control);
	static bool belongs_to_editor(Node *p_node);

	Error validate_scope(const MCPEditorUISnapshot &p_snapshot, const ObjectID &p_scope_id, String &r_error_code, String &r_error_message) const;
	Error find_target(const MCPEditorUISnapshot &p_snapshot, const String &p_target_id, const MCPEditorUITarget *&r_target,
			String &r_error_code, String &r_error_message) const;
	Error validate_action(const MCPEditorUITarget &p_target, const String &p_action, String &r_error_code, String &r_error_message) const;
	Error validate_current(const MCPEditorUITarget &p_target, String &r_error_code, String &r_error_message) const;
	Error validate_available(const MCPEditorUITarget &p_target, const String &p_action, String &r_error_code, String &r_error_message) const;

private:
	bool _target_is_current(const MCPEditorUITarget &p_target) const;
};
