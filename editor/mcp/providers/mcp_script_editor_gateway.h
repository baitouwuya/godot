/**************************************************************************/
/*  mcp_script_editor_gateway.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"

class MCPScriptBuffer;
class Script;
class ScriptEditor;
class ScriptEditorBase;

class MCPScriptEditorGateway {
public:
	static bool is_gdscript(const Ref<Script> &p_script);
	static String get_editor_script_path(const Ref<Script> &p_script);
	static Ref<Script> load_script_resource(const String &p_path);
	static ScriptEditorBase *find_open_editor(ScriptEditor *p_script_editor, const String &p_path);
	static ScriptEditorBase *find_open_editor(ScriptEditor *p_script_editor, const Ref<Script> &p_script);
	static Error from_editor(const String &p_path, ScriptEditorBase *p_editor, MCPScriptBuffer &r_buffer, String *r_error = nullptr);
	static Error open(const String &p_path, MCPScriptBuffer &r_buffer, String *r_error = nullptr);
	static Error open(const Ref<Script> &p_script, MCPScriptBuffer &r_buffer, String *r_error = nullptr);
};
