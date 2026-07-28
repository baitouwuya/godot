/**************************************************************************/
/*  mcp_runtime_input_service.h                                          */
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

#include "mcp_runtime_debugger_gateway.h"
#include "mcp_runtime_input_scheduler.h"

class MCPRuntimeInputService {
	MCPRuntimeDebuggerGateway *runtime_gateway = nullptr;
	MCPRuntimeInputScheduler input_scheduler;

public:
	explicit MCPRuntimeInputService(MCPRuntimeDebuggerGateway *p_runtime_gateway);

	Dictionary dispatch_target_events(const Dictionary &p_arguments, const String &p_mcp_session_id,
			const Array &p_events, const Dictionary &p_metadata);
	Dictionary start_target_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id,
			const Array &p_steps, const Dictionary &p_metadata);
	Dictionary send_input(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary start_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary get_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary cancel_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary release_input(const Dictionary &p_arguments, const String &p_mcp_session_id);
	void process();
	void release_session(const String &p_mcp_session_id);
	void shutdown();

	MCPRuntimeInputScheduler *get_scheduler() { return &input_scheduler; }
};
