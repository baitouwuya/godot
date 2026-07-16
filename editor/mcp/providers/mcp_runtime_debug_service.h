/**************************************************************************/
/*  mcp_runtime_debug_service.h                                           */
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

#include "core/variant/dictionary.h"

class MCPDebugCapture;
class ScriptEditorDebugger;

class MCPRuntimeDebugService {
	MCPDebugCapture *debug_capture = nullptr;

	Dictionary _resolve_session(const Dictionary &p_arguments, bool p_require_generation, ScriptEditorDebugger *&r_debugger,
			int &r_debugger_session, uint64_t &r_runtime_generation) const;
	Dictionary _refresh_tree(ScriptEditorDebugger *p_debugger, int p_timeout_msec) const;
	Dictionary _find_node(ScriptEditorDebugger *p_debugger, const String &p_path, ObjectID &r_object_id, Dictionary &r_node) const;
	Dictionary _refresh_object(ScriptEditorDebugger *p_debugger, ObjectID p_object_id, int p_timeout_msec) const;
	Dictionary _make_session_identity(int p_debugger_session, uint64_t p_runtime_generation) const;

public:
	explicit MCPRuntimeDebugService(MCPDebugCapture *p_debug_capture);

	Dictionary get_state() const;
	Dictionary play(const Dictionary &p_arguments) const;
	Dictionary stop() const;
	Dictionary set_suspended(const Dictionary &p_arguments, bool p_suspended) const;
	Dictionary next_frame(const Dictionary &p_arguments) const;
	Dictionary get_tree(const Dictionary &p_arguments) const;
	Dictionary get_properties(const Dictionary &p_arguments) const;
	Dictionary set_property(const Dictionary &p_arguments) const;
	Dictionary send_input(const Dictionary &p_arguments) const;
};
