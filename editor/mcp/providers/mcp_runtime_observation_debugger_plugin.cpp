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

void MCPRuntimeObservationDebuggerPlugin::_bind_methods() {
}

bool MCPRuntimeObservationDebuggerPlugin::capture(const String &p_message, const Array &p_data, int p_session) {
	if (p_message == "mcp_observation:response" || p_message == "mcp_condition:response") {
		response_store.handle_response(p_session, p_data);
	}
	return true;
}

bool MCPRuntimeObservationDebuggerPlugin::has_capture(const String &p_capture) const {
	return p_capture == "mcp_observation" || p_capture == "mcp_condition";
}

bool MCPRuntimeObservationDebuggerPlugin::register_request(int p_session, const String &p_request_id, const String &p_operation) {
	return response_store.register_request(p_session, p_request_id, p_operation);
}

bool MCPRuntimeObservationDebuggerPlugin::take_response(int p_session, const String &p_request_id, Response &r_response) {
	return response_store.take_response(p_session, p_request_id, r_response);
}

void MCPRuntimeObservationDebuggerPlugin::cancel_request(int p_session, const String &p_request_id) {
	response_store.cancel_request(p_session, p_request_id);
}

void MCPRuntimeObservationDebuggerPlugin::clear_requests() {
	response_store.clear();
}
