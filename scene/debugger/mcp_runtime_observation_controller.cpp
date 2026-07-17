/**************************************************************************/
/*  mcp_runtime_observation_controller.cpp                                */
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

#include "mcp_runtime_observation_controller.h"

#include "mcp_runtime_observation.h"

#include "core/debugger/engine_debugger.h"

void MCPRuntimeObservationController::_bind_methods() {
}

MCPRuntimeObservationController::MCPRuntimeObservationController() {
	singleton = this;
	EngineDebugger::register_message_capture("mcp_observation", EngineDebugger::Capture(this, _parse_message));
}

MCPRuntimeObservationController::~MCPRuntimeObservationController() {
	if (EngineDebugger::get_singleton()) {
		EngineDebugger::unregister_message_capture("mcp_observation");
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}

void MCPRuntimeObservationController::initialize() {
	if (!singleton) {
		memnew(MCPRuntimeObservationController);
	}
}

void MCPRuntimeObservationController::deinitialize() {
	if (singleton) {
		memdelete(singleton);
	}
}

void MCPRuntimeObservationController::_send_response(const String &p_request_id, const String &p_operation, bool p_ok,
		const String &p_code, const String &p_message, const Dictionary &p_data) const {
	if (EngineDebugger::get_singleton()) {
		EngineDebugger::get_singleton()->send_message("mcp_observation:response",
				Array{ p_request_id, p_operation, p_ok, p_code, p_message, p_data });
	}
}

Error MCPRuntimeObservationController::_parse_message(void *p_user, const String &p_message, const Array &p_arguments, bool &r_captured) {
	MCPRuntimeObservationController *controller = static_cast<MCPRuntimeObservationController *>(p_user);
	r_captured = true;
	const String operation = p_message.get_slice(":", 1);
	if (!controller || operation.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	if (p_arguments.size() != 2 || p_arguments[0].get_type() != Variant::STRING || String(p_arguments[0]).is_empty() ||
			p_arguments[1].get_type() != Variant::DICTIONARY) {
		return ERR_INVALID_DATA;
	}
	const String request_id = p_arguments[0];
	Dictionary result;
	String error_code;
	String error_message;
	const Error error = MCPRuntimeObservation::execute(operation, p_arguments[1], result, error_code, error_message);
	controller->_send_response(request_id, operation, error == OK, error_code, error_message, result);
	return OK;
}
