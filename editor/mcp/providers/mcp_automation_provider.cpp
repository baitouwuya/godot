/**************************************************************************/
/*  mcp_automation_provider.cpp                                           */
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

#include "mcp_automation_provider.h"

#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

constexpr int MAX_BATCH_CALLS = 64;
constexpr const char *BATCH_TOOL_NAME = "godot.automation.batch";

static Dictionary _property_schema(const String &p_type, const String &p_description) {
	Dictionary property;
	property["type"] = p_type;
	property["description"] = p_description;
	return property;
}

static Dictionary _batch_schema() {
	Dictionary call_properties;
	call_properties["name"] = _property_schema("string", "Registered MCP tool name.");
	call_properties["arguments"] = _property_schema("object", "Arguments passed to the tool.");

	PackedStringArray call_required;
	call_required.push_back("name");
	Dictionary call_item;
	call_item["type"] = "object";
	call_item["properties"] = call_properties;
	call_item["required"] = call_required;
	call_item["additionalProperties"] = false;

	Dictionary calls = _property_schema("array", "Ordered MCP tool calls executed in the current session.");
	calls["items"] = call_item;
	calls["minItems"] = 1;
	calls["maxItems"] = MAX_BATCH_CALLS;

	Dictionary stop_on_error = _property_schema("boolean", "Stop after the first tool error.");
	stop_on_error["default"] = true;

	Dictionary properties;
	properties["calls"] = calls;
	properties["stopOnError"] = stop_on_error;

	PackedStringArray required;
	required.push_back("calls");
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = properties;
	schema["required"] = required;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _invalid_arguments(const String &p_message) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", p_message);
}

static MCPToolCallContext _make_nested_context(const Dictionary &p_context) {
	MCPToolCallContext context;
	context.request_id = p_context.get("requestId", Variant());
	context.protocol_version = p_context.get("protocolVersion", String());
	const Variant session = p_context.get("session", Dictionary());
	const Variant project = p_context.get("project", Dictionary());
	if (session.get_type() == Variant::DICTIONARY) {
		context.session = session;
	}
	if (project.get_type() == Variant::DICTIONARY) {
		context.project = project;
	}
	return context;
}

static Dictionary _call_result(int p_index, const String &p_name, const MCPToolRegistry::CallResult &p_call) {
	Dictionary result;
	result["index"] = p_index;
	result["name"] = p_name;
	if (p_call.status != MCPToolRegistry::CALL_OK) {
		result["isError"] = true;
		Dictionary error;
		error["code"] = "TOOL_DISPATCH_FAILED";
		error["message"] = p_call.message;
		result["error"] = error;
		return result;
	}

	const bool is_error = bool(p_call.result.get("isError", false));
	result["isError"] = is_error;
	result["structuredContent"] = p_call.result.get("structuredContent", Dictionary());
	return result;
}

} // namespace

MCPAutomationProvider::~MCPAutomationProvider() {
	unregister_tools();
}

Error MCPAutomationProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		if (r_error) {
			*r_error = "An MCP tool registry is required.";
		}
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		if (r_error) {
			*r_error = "Automation tools are already registered.";
		}
		return ERR_ALREADY_IN_USE;
	}

	const Error error = p_registry->register_tool(
			MCPToolUtils::make_tool_definition(BATCH_TOOL_NAME, "Execute an ordered batch through the shared MCP Tool Registry.", _batch_schema(), MCPToolUtils::TOOL_DESTRUCTIVE),
			callable_mp(this, &MCPAutomationProvider::batch), this, r_error);
	if (error == OK) {
		tool_registry = p_registry;
	}
	return error;
}

void MCPAutomationProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPAutomationProvider::batch(const Dictionary &p_arguments, const Dictionary &p_context) {
	PackedStringArray allowed;
	allowed.push_back("calls");
	allowed.push_back("stopOnError");
	String unknown;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown)) {
		return _invalid_arguments("Unknown argument: " + unknown);
	}
	if (!tool_registry) {
		return MCPToolUtils::make_error_result("UNAVAILABLE", "The MCP Tool Registry is unavailable.");
	}

	const Variant calls_value = p_arguments.get("calls", Variant());
	const Variant stop_value = p_arguments.get("stopOnError", true);
	if (calls_value.get_type() != Variant::ARRAY || stop_value.get_type() != Variant::BOOL) {
		return _invalid_arguments("calls must be an array and stopOnError must be boolean.");
	}
	const Array calls = calls_value;
	if (calls.is_empty() || calls.size() > MAX_BATCH_CALLS) {
		return _invalid_arguments(vformat("calls must contain from 1 to %d entries.", MAX_BATCH_CALLS));
	}

	for (int i = 0; i < calls.size(); i++) {
		if (calls[i].get_type() != Variant::DICTIONARY) {
			return _invalid_arguments(vformat("calls[%d] must be an object.", i));
		}
		const Dictionary call = calls[i];
		PackedStringArray call_allowed;
		call_allowed.push_back("name");
		call_allowed.push_back("arguments");
		if (!MCPToolUtils::has_only_arguments(call, call_allowed, unknown)) {
			return _invalid_arguments(vformat("calls[%d] has an unknown argument: %s", i, unknown));
		}
		const Variant name_value = call.get("name", Variant());
		const Variant arguments_value = call.get("arguments", Dictionary());
		if (name_value.get_type() != Variant::STRING || String(name_value).is_empty() || arguments_value.get_type() != Variant::DICTIONARY) {
			return _invalid_arguments(vformat("calls[%d] requires a non-empty name and optional arguments object.", i));
		}
		const String name = name_value;
		if (name == BATCH_TOOL_NAME) {
			return _invalid_arguments("Recursive automation batches are not allowed.");
		}
		if (!tool_registry->has_tool(name)) {
			return _invalid_arguments(vformat("calls[%d] references an unknown MCP tool: %s", i, name));
		}
	}

	const bool stop_on_error = stop_value;
	const MCPToolCallContext nested_context = _make_nested_context(p_context);
	Array results;
	int failed_count = 0;
	for (int i = 0; i < calls.size(); i++) {
		const Dictionary call = calls[i];
		const String name = call["name"];
		const Dictionary arguments = call.get("arguments", Dictionary());
		const MCPToolRegistry::CallResult call_result = tool_registry->call_tool(name, arguments, nested_context);
		const Dictionary result = _call_result(i, name, call_result);
		results.push_back(result);
		if (!bool(result.get("isError", false))) {
			continue;
		}

		failed_count++;
		if (stop_on_error) {
			Dictionary details;
			details["completedCount"] = i;
			details["failedIndex"] = i;
			details["failedTool"] = name;
			details["results"] = results;
			return MCPToolUtils::make_error_result("BATCH_FAILED", "Automation batch stopped after a tool error.", details);
		}
	}

	Dictionary content;
	content["callCount"] = calls.size();
	content["completedCount"] = calls.size();
	content["failedCount"] = failed_count;
	content["results"] = results;
	return MCPToolUtils::make_success_result(content);
}
