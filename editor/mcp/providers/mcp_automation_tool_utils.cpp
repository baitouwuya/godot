/**************************************************************************/
/*  mcp_automation_tool_utils.cpp                                         */
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

#include "mcp_automation_tool_utils.h"

#include "mcp_tool_utils.h"

namespace {

static constexpr int MAX_BATCH_CALLS = 64;

} // namespace

namespace MCPAutomationToolUtils {

String batch_tool_name() {
	return "godot.automation.batch";
}

Dictionary batch_schema() {
	Dictionary call_properties;
	call_properties["name"] = MCPToolUtils::make_property_schema("string", "Registered MCP tool name.");
	call_properties["arguments"] = MCPToolUtils::make_property_schema("object", "Arguments passed to the tool.");
	const Dictionary call_item = MCPToolUtils::make_object_schema(call_properties, PackedStringArray{ "name" });

	Dictionary calls = MCPToolUtils::make_property_schema("array", "Ordered MCP tool calls executed in the current session.");
	calls["items"] = call_item;
	calls["minItems"] = 1;
	calls["maxItems"] = MAX_BATCH_CALLS;
	const BatchOptions defaults;
	Dictionary stop_on_error = MCPToolUtils::make_property_schema("boolean", "Stop after the first tool error.");
	stop_on_error["default"] = defaults.stop_on_error;

	Dictionary properties;
	properties["calls"] = calls;
	properties["stopOnError"] = stop_on_error;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "calls" });
}

Dictionary batch_output_schema() {
	Dictionary dispatch_error_properties;
	dispatch_error_properties["code"] = MCPToolUtils::make_property_schema("string", "Stable dispatch error code.");
	dispatch_error_properties["message"] = MCPToolUtils::make_property_schema("string", "Dispatch error message.");
	const Dictionary dispatch_error = MCPToolUtils::make_object_schema(dispatch_error_properties, PackedStringArray{ "code", "message" });

	Dictionary result_properties;
	Dictionary result_index = MCPToolUtils::make_property_schema("integer", "Zero-based batch call index.");
	result_index["minimum"] = 0;
	result_properties["index"] = result_index;
	result_properties["name"] = MCPToolUtils::make_property_schema("string", "Executed MCP tool name.");
	result_properties["isError"] = MCPToolUtils::make_property_schema("boolean", "Whether the nested tool call failed.");
	result_properties["structuredContent"] = MCPToolUtils::make_property_schema("object", "Nested tool structured content.");
	result_properties["error"] = dispatch_error;
	const Dictionary result_item = MCPToolUtils::make_object_schema(result_properties, PackedStringArray{ "index", "name", "isError" });

	Dictionary results = MCPToolUtils::make_property_schema("array", "Ordered nested tool results.");
	results["items"] = result_item;
	results["maxItems"] = MAX_BATCH_CALLS;
	Dictionary count = MCPToolUtils::make_property_schema("integer", "Batch call count.");
	count["minimum"] = 0;
	count["maximum"] = MAX_BATCH_CALLS;
	Dictionary properties;
	properties["callCount"] = count;
	properties["completedCount"] = count;
	properties["failedCount"] = count;
	properties["results"] = results;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "callCount", "completedCount", "failedCount", "results" });
}

bool parse_batch_options(const Dictionary &p_arguments, BatchOptions &r_options, String &r_error_message) {
	r_options = BatchOptions();
	r_error_message = String();
	const Variant calls_value = p_arguments.get("calls", Variant());
	const Variant stop_value = p_arguments.get("stopOnError", r_options.stop_on_error);
	if (calls_value.get_type() != Variant::ARRAY || stop_value.get_type() != Variant::BOOL) {
		r_error_message = "calls must be an array and stopOnError must be boolean.";
		return false;
	}
	const Array calls = calls_value;
	if (calls.is_empty() || calls.size() > MAX_BATCH_CALLS) {
		r_error_message = vformat("calls must contain from 1 to %d entries.", MAX_BATCH_CALLS);
		return false;
	}
	for (int i = 0; i < calls.size(); i++) {
		if (calls[i].get_type() != Variant::DICTIONARY) {
			r_error_message = vformat("calls[%d] must be an object.", i);
			return false;
		}
		const Dictionary call = calls[i];
		const Variant name = call.get("name", Variant());
		const Variant arguments = call.get("arguments", Dictionary());
		if (name.get_type() != Variant::STRING || String(name).is_empty() || arguments.get_type() != Variant::DICTIONARY) {
			r_error_message = vformat("calls[%d] requires a non-empty name and optional arguments object.", i);
			return false;
		}
		if (String(name) == batch_tool_name()) {
			r_error_message = "Recursive automation batches are not allowed.";
			return false;
		}
	}
	r_options.calls = calls;
	r_options.stop_on_error = bool(stop_value);
	return true;
}

Dictionary make_call_result(int p_index, const String &p_name, const MCPToolRegistry::CallResult &p_call) {
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

	result["isError"] = bool(p_call.result.get("isError", false));
	result["structuredContent"] = p_call.result.get("structuredContent", Dictionary());
	return result;
}

} // namespace MCPAutomationToolUtils
