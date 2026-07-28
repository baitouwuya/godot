/**************************************************************************/
/*  mcp_runtime_input_debugger_plugin.h                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "editor/debugger/editor_debugger_plugin.h"

class MCPRuntimeInputScheduler;
class MCPRuntimeDebuggerGateway;

class MCPRuntimeInputDebuggerPlugin : public EditorDebuggerPlugin {
	GDCLASS(MCPRuntimeInputDebuggerPlugin, EditorDebuggerPlugin);

	MCPRuntimeInputScheduler *scheduler = nullptr;
	MCPRuntimeDebuggerGateway *runtime_gateway = nullptr;

protected:
	static void _bind_methods();

public:
	bool capture(const String &p_message, const Array &p_data, int p_session) override;
	bool has_capture(const String &p_capture) const override;
	void set_scheduler(MCPRuntimeInputScheduler *p_scheduler);
	void set_runtime_gateway(MCPRuntimeDebuggerGateway *p_runtime_gateway);
};
