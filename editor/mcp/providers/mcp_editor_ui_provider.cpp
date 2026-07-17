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

#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

static Dictionary _string_schema() {
	Dictionary schema;
	schema["type"] = "string";
	return schema;
}

static Dictionary _get_actions_schema() {
	Dictionary boolean_schema;
	boolean_schema["type"] = "boolean";
	Dictionary integer_schema;
	integer_schema["type"] = "integer";

	Dictionary max_depth = integer_schema.duplicate();
	max_depth["minimum"] = 1;
	max_depth["maximum"] = 64;
	Dictionary limit = integer_schema.duplicate();
	limit["minimum"] = 1;
	limit["maximum"] = 2048;

	Dictionary properties;
	properties["includeDisabled"] = boolean_schema;
	properties["includeValues"] = boolean_schema;
	properties["maxDepth"] = max_depth;
	properties["limit"] = limit;

	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = properties;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _perform_schema() {
	Dictionary action = _string_schema();
	PackedStringArray action_enum;
	action_enum.push_back("focus");
	action_enum.push_back("click");
	action_enum.push_back("activate");
	action_enum.push_back("select");
	action_enum.push_back("set_value");
	action_enum.push_back("expand");
	action_enum.push_back("collapse");
	action_enum.push_back("increment");
	action_enum.push_back("decrement");
	action["enum"] = action_enum;

	Dictionary properties;
	properties["snapshotId"] = _string_schema();
	properties["targetId"] = _string_schema();
	properties["action"] = action;
	Dictionary value;
	Array value_types;
	value_types.push_back("string");
	value_types.push_back("number");
	value_types.push_back("integer");
	value_types.push_back("boolean");
	value["type"] = value_types;
	properties["value"] = value;

	PackedStringArray required;
	required.push_back("snapshotId");
	required.push_back("targetId");
	required.push_back("action");

	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = properties;
	schema["required"] = required;
	schema["additionalProperties"] = false;
	return schema;
}

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

	Error error = p_registry->register_tool(
			MCPToolUtils::make_tool_definition("godot.editor.ui.get_actions", "Inspect the currently visible editor controls and their semantic actions. Returns snapshot-bound opaque target IDs.", _get_actions_schema()),
			callable_mp(this, &MCPEditorUIProvider::get_actions), this, r_error);
	if (error == OK) {
		error = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.editor.ui.perform", "Perform a semantic action on a target from the latest editor UI snapshot for this MCP session.", _perform_schema()),
				callable_mp(this, &MCPEditorUIProvider::perform), this, r_error);
	}
	if (error != OK) {
		p_registry->unregister_tools_for_owner(this);
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
	String unknown;
	PackedStringArray allowed;
	allowed.push_back("includeDisabled");
	allowed.push_back("includeValues");
	allowed.push_back("maxDepth");
	allowed.push_back("limit");
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown argument: " + unknown);
	}

	String session_id;
	Dictionary error_result;
	if (!_resolve_session(p_context, session_id, error_result)) {
		return error_result;
	}

	MCPEditorUIService::SnapshotOptions options;
	if (p_arguments.has("includeDisabled")) {
		if (p_arguments["includeDisabled"].get_type() != Variant::BOOL) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "includeDisabled must be a boolean.");
		}
		options.include_disabled = p_arguments["includeDisabled"];
	}
	if (p_arguments.has("includeValues")) {
		if (p_arguments["includeValues"].get_type() != Variant::BOOL) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "includeValues must be a boolean.");
		}
		options.include_values = p_arguments["includeValues"];
	}
	int64_t integer = 0;
	if (p_arguments.has("maxDepth")) {
		if (!MCPToolUtils::try_get_json_integer(p_arguments["maxDepth"], 1, 64, integer)) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "maxDepth must be an integer from 1 to 64.");
		}
		options.max_depth = integer;
	}
	if (p_arguments.has("limit")) {
		if (!MCPToolUtils::try_get_json_integer(p_arguments["limit"], 1, 2048, integer)) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "limit must be an integer from 1 to 2048.");
		}
		options.limit = integer;
	}

	return MCPToolUtils::make_success_result(service.capture(session_id, options));
}

Dictionary MCPEditorUIProvider::perform(const Dictionary &p_arguments, const Dictionary &p_context) {
	String unknown;
	PackedStringArray allowed;
	allowed.push_back("snapshotId");
	allowed.push_back("targetId");
	allowed.push_back("action");
	allowed.push_back("value");
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown argument: " + unknown);
	}

	const Variant snapshot = p_arguments.get("snapshotId", Variant());
	const Variant target = p_arguments.get("targetId", Variant());
	const Variant action = p_arguments.get("action", Variant());
	if (snapshot.get_type() != Variant::STRING || String(snapshot).is_empty() || target.get_type() != Variant::STRING || String(target).is_empty() || action.get_type() != Variant::STRING || String(action).is_empty()) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "snapshotId, targetId, and action must be non-empty strings.");
	}

	String session_id;
	Dictionary error_result;
	if (!_resolve_session(p_context, session_id, error_result)) {
		return error_result;
	}

	Dictionary result;
	String error_code;
	String error_message;
	const Error error = service.perform(session_id, snapshot, target, action, p_arguments, result, error_code, error_message);
	if (error != OK) {
		return MCPToolUtils::make_error_result(error_code, error_message);
	}
	return MCPToolUtils::make_success_result(result);
}
