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

#include "mcp_automation_tool_utils.h"
#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

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

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ MCPAutomationToolUtils::batch_tool_name(), "Execute an ordered batch through the shared MCP Tool Registry.",
				MCPAutomationToolUtils::batch_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPAutomationProvider::batch), MCPAutomationToolUtils::batch_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPAutomationProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPAutomationProvider::batch(const Dictionary &p_arguments, const Dictionary &p_context) {
	if (!tool_registry) {
		return MCPToolUtils::make_error_result("UNAVAILABLE", "The MCP Tool Registry is unavailable.");
	}

	MCPAutomationToolUtils::BatchOptions options;
	String argument_error;
	if (!MCPAutomationToolUtils::parse_batch_options(p_arguments, options, argument_error)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}

	for (int i = 0; i < options.calls.size(); i++) {
		const Dictionary call = options.calls[i];
		const String name = call["name"];
		if (!tool_registry->has_tool(name)) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", vformat("calls[%d] references an unknown MCP tool: %s", i, name));
		}
	}

	const MCPToolCallContext nested_context = MCPToolUtils::make_tool_call_context(p_context);
	Array results;
	int failed_count = 0;
	for (int i = 0; i < options.calls.size(); i++) {
		const Dictionary call = options.calls[i];
		const String name = call["name"];
		const Dictionary arguments = call.get("arguments", Dictionary());
		const MCPToolRegistry::CallResult call_result = tool_registry->call_tool(name, arguments, nested_context);
		const Dictionary result = MCPAutomationToolUtils::make_call_result(i, name, call_result);
		results.push_back(result);
		if (!bool(result.get("isError", false))) {
			continue;
		}

		failed_count++;
		if (options.stop_on_error) {
			Dictionary details;
			details["completedCount"] = i;
			details["failedIndex"] = i;
			details["failedTool"] = name;
			details["results"] = results;
			return MCPToolUtils::make_error_result("BATCH_FAILED", "Automation batch stopped after a tool error.", details);
		}
	}

	Dictionary content;
	content["callCount"] = options.calls.size();
	content["completedCount"] = options.calls.size();
	content["failedCount"] = failed_count;
	content["results"] = results;
	return MCPToolUtils::make_success_result(content);
}
