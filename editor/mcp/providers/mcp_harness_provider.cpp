/**************************************************************************/
/*  mcp_harness_provider.cpp                                             */
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

#include "mcp_harness_provider.h"

#include "mcp_harness_tool_utils.h"
#include "mcp_tool_utils.h"

#include "core/object/callable_mp.h"

MCPHarnessProvider::~MCPHarnessProvider() {
	shutdown();
	unregister_tools();
}

Error MCPHarnessProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
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
			*r_error = "Harness tools are already registered.";
		}
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.runtime.harness.start", "Compile and start an asynchronous inline runtime Harness job.",
				MCPHarnessToolUtils::start_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPHarnessProvider::start), MCPHarnessToolUtils::summary_output_schema() },
		{ "godot.runtime.harness.status", "Get bounded progress for a runtime Harness job.",
				MCPHarnessToolUtils::job_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPHarnessProvider::get_status), MCPHarnessToolUtils::summary_output_schema() },
		{ "godot.runtime.harness.cancel", "Cancel a runtime Harness job and its active asynchronous child.",
				MCPHarnessToolUtils::job_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPHarnessProvider::cancel), MCPHarnessToolUtils::summary_output_schema() },
		{ "godot.runtime.harness.get_report", "Get the structured report accumulated by a runtime Harness job.",
				MCPHarnessToolUtils::job_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPHarnessProvider::get_report), MCPHarnessToolUtils::report_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}
	tool_registry = p_registry;
	service.set_tool_registry(p_registry);
	return OK;
}

void MCPHarnessProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
	service.set_tool_registry(nullptr);
}

Dictionary MCPHarnessProvider::start(const Dictionary &p_arguments, const Dictionary &p_context) {
	return service.start(p_arguments, MCPToolUtils::make_tool_call_context(p_context));
}

Dictionary MCPHarnessProvider::get_status(const Dictionary &p_arguments, const Dictionary &p_context) {
	return service.get_status(p_arguments, MCPToolUtils::make_tool_call_context(p_context));
}

Dictionary MCPHarnessProvider::cancel(const Dictionary &p_arguments, const Dictionary &p_context) {
	return service.cancel(p_arguments, MCPToolUtils::make_tool_call_context(p_context));
}

Dictionary MCPHarnessProvider::get_report(const Dictionary &p_arguments, const Dictionary &p_context) {
	return service.get_report(p_arguments, MCPToolUtils::make_tool_call_context(p_context));
}
