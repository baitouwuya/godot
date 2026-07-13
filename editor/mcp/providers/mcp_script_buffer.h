/**************************************************************************/
/*  mcp_script_buffer.h                                                   */
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
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class CodeEdit;
class Script;
class ScriptEditorBase;
class TextEditorBase;

class MCPScriptBuffer {
public:
	static Error normalize_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error = nullptr);
	static Error normalize_path(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error = nullptr);
	static Error resolve_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error = nullptr);
	static Error resolve_path(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error = nullptr);
	static Error resolve_script_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, bool &r_built_in, String *r_error = nullptr);
	static Error resolve_script_path(const String &p_path, String &r_resource_path, String &r_absolute_path, bool &r_built_in, String *r_error = nullptr);
	static Error read_authoritative_snapshot_for_root(const String &p_path, const String &p_project_root, Dictionary &r_snapshot, String *r_error = nullptr);
	static Error read_authoritative_snapshot(const String &p_path, Dictionary &r_snapshot, String *r_error = nullptr);
	static Error open(const String &p_path, MCPScriptBuffer &r_buffer, String *r_error = nullptr);
	static Error open(const Ref<Script> &p_script, MCPScriptBuffer &r_buffer, String *r_error = nullptr);
	static void apply_text_edit(CodeEdit *p_text_edit, const String &p_text);

	bool is_valid() const;
	Dictionary get_snapshot() const;
	Error replace_text(const String &p_text, const Variant &p_expected_revision, const Variant &p_expected_sha256,
			bool &r_changed, Dictionary &r_current_state, String *r_error = nullptr);
	Error save(String *r_error = nullptr);

private:
	String path;
	String scene_path;
	bool built_in = false;
	ScriptEditorBase *editor = nullptr;
	TextEditorBase *text_editor = nullptr;
	CodeEdit *code_edit = nullptr;

	static Error _from_editor(const String &p_path, ScriptEditorBase *p_editor, MCPScriptBuffer &r_buffer, String *r_error);
};
