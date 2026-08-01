/**************************************************************************/
/*  mcp_runtime_observation_debugger_plugin.cpp                           */
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

#include "mcp_runtime_observation_debugger_plugin.h"

#include "mcp_runtime_debugger_gateway.h"

void MCPRuntimeObservationDebuggerPlugin::_bind_methods() {
}

bool MCPRuntimeObservationDebuggerPlugin::capture(const String &p_message, const Array &p_data, int p_session) {
	if (p_message == "mcp_observation:response" || p_message == "mcp_condition:response" ||
			p_message == "mcp_performance:response" || p_message == "mcp_property:response") {
		if (runtime_gateway) {
			runtime_gateway->handle_response(p_session, p_data);
		}
	}
	return true;
}

bool MCPRuntimeObservationDebuggerPlugin::has_capture(const String &p_capture) const {
	return p_capture == "mcp_observation" || p_capture == "mcp_condition" || p_capture == "mcp_performance" ||
			p_capture == "mcp_property";
}
