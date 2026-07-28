/**************************************************************************/
/*  mcp_runtime_debugger_gateway.h                                       */
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

#include "core/variant/dictionary.h"

class MCPDebugCapture;
class MCPRuntimeObservationDebuggerPlugin;
class ScriptEditorDebugger;

class MCPRuntimeDebuggerGateway {
public:
	struct Session {
		ScriptEditorDebugger *debugger = nullptr;
		int debugger_session = -1;
		uint64_t runtime_generation = 0;
	};

private:
	MCPDebugCapture *debug_capture = nullptr;
	MCPRuntimeObservationDebuggerPlugin *response_plugin = nullptr;
public:
	explicit MCPRuntimeDebuggerGateway(MCPDebugCapture *p_debug_capture);

	Dictionary resolve_session(const Dictionary &p_arguments, bool p_require_generation, Session &r_session) const;
	Dictionary request(const Dictionary &p_arguments, const String &p_capture, const String &p_operation,
			const Dictionary &p_payload, bool p_require_generation = false) const;
	Dictionary make_session_identity(int p_debugger_session, uint64_t p_runtime_generation) const;
	bool get_runtime_generation(int p_debugger_session, uint64_t &r_runtime_generation) const;
	bool is_runtime_current(int p_debugger_session, uint64_t p_runtime_generation) const;
	bool send_message(int p_debugger_session, const String &p_message, const Array &p_arguments) const;

	void set_response_plugin(MCPRuntimeObservationDebuggerPlugin *p_plugin) { response_plugin = p_plugin; }
};
