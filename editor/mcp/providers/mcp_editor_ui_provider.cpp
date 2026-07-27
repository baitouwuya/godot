/**************************************************************************/
/*  mcp_editor_ui_provider.cpp                                            */
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

#include "mcp_editor_ui_provider.h"

#include "mcp_editor_ui_tool_utils.h"
#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPEditorUIProvider::~MCPEditorUIProvider() {
	unregister_tools();
}

Error MCPEditorUIProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Editor UI tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.editor.ui.get_actions", "Inspect the currently visible editor controls and their semantic actions. Returns snapshot-bound opaque target IDs.",
				MCPEditorUIToolUtils::get_actions_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPEditorUIProvider::get_actions), MCPEditorUIToolUtils::snapshot_output_schema() },
		{ "godot.editor.ui.perform", "Perform a semantic action on a target from the latest editor UI snapshot for this MCP session.",
				MCPEditorUIToolUtils::perform_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPEditorUIProvider::perform), MCPEditorUIToolUtils::perform_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPEditorUIProvider::unregister_tools() {
	service.clear();
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

void MCPEditorUIProvider::on_session_removed(const String &p_session_id) {
	release_session(p_session_id);
}

void MCPEditorUIProvider::release_session(const String &p_session_id) {
	service.release_session(p_session_id);
}

void MCPEditorUIProvider::clear() {
	service.clear();
}

bool MCPEditorUIProvider::_resolve_session(const Dictionary &p_context, String &r_session_id, Dictionary &r_error) const {
	r_session_id = String();
	const Variant session_value = p_context.get("session", Variant());
	if (session_value.get_type() == Variant::DICTIONARY) {
		const Variant session_id = Dictionary(session_value).get("sessionId", Variant());
		if (session_id.get_type() == Variant::STRING) {
			r_session_id = String(session_id);
		}
	}
	if (r_session_id.is_empty()) {
		r_error = MCPToolUtils::make_error_result("MCP_SESSION_REQUIRED", "A valid MCP session is required for editor UI snapshots.");
		return false;
	}
	return true;
}

Dictionary MCPEditorUIProvider::get_actions(const Dictionary &p_arguments, const Dictionary &p_context) {
	String session_id;
	Dictionary error_result;
	if (!_resolve_session(p_context, session_id, error_result)) {
		return error_result;
	}

	MCPEditorUIService::SnapshotOptions options;
	String argument_error;
	if (MCPEditorUIToolUtils::parse_snapshot_options(p_arguments, options, argument_error) != OK) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}

	return MCPToolUtils::make_success_result(service.capture(session_id, options));
}

Dictionary MCPEditorUIProvider::perform(const Dictionary &p_arguments, const Dictionary &p_context) {
	MCPEditorUIToolUtils::PerformArguments arguments;
	String argument_error;
	if (MCPEditorUIToolUtils::parse_perform_arguments(p_arguments, arguments, argument_error) != OK) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}

	String session_id;
	Dictionary error_result;
	if (!_resolve_session(p_context, session_id, error_result)) {
		return error_result;
	}

	Dictionary result;
	String error_code;
	String error_message;
	const Error error = service.perform(session_id, arguments.snapshot_id, arguments.target_id, arguments.action, p_arguments, result, error_code, error_message);
	if (error != OK) {
		return MCPToolUtils::make_error_result(error_code, error_message);
	}
	return MCPToolUtils::make_success_result(result);
}
