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

#include "mcp_tool_utils.h"

#include "core/object/callable_mp.h"

namespace {

static Dictionary _property_schema(const String &p_type, const String &p_description) {
	Dictionary property;
	property["type"] = p_type;
	property["description"] = p_description;
	return property;
}

static Dictionary _object_schema(const Dictionary &p_properties, const PackedStringArray &p_required) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["required"] = p_required;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _start_schema() {
	Dictionary properties;
	Dictionary script = _property_schema("object", "Inline Harness script. Filesystem paths are not accepted.");
	script["additionalProperties"] = true;
	properties["script"] = script;
	Dictionary debugger = _property_schema("integer", "Optional running-project debugger session index.");
	debugger["minimum"] = 0;
	properties["debuggerSession"] = debugger;
	Dictionary generation = _property_schema("integer", "Runtime generation returned by godot.runtime.get_state.");
	generation["minimum"] = 1;
	properties["runtimeGeneration"] = generation;
	return _object_schema(properties, PackedStringArray{ "script", "runtimeGeneration" });
}

static Dictionary _job_schema() {
	Dictionary properties;
	properties["jobId"] = _property_schema("string", "Opaque Harness job ID returned by godot.runtime.harness.start.");
	Dictionary debugger = _property_schema("integer", "Optional running-project debugger session index.");
	debugger["minimum"] = 0;
	properties["debuggerSession"] = debugger;
	Dictionary generation = _property_schema("integer", "Runtime generation that owns the Harness job.");
	generation["minimum"] = 1;
	properties["runtimeGeneration"] = generation;
	return _object_schema(properties, PackedStringArray{ "jobId", "runtimeGeneration" });
}

static MCPToolCallContext _make_context(const Dictionary &p_context) {
	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
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

static bool _has_only(const Dictionary &p_arguments, const PackedStringArray &p_allowed, Dictionary &r_error) {
	String unknown;
	if (MCPToolUtils::has_only_arguments(p_arguments, p_allowed, unknown)) {
		return true;
	}
	r_error = MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown argument: " + unknown);
	return false;
}

} // namespace

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

	struct ToolRegistration {
		const char *name;
		const char *description;
		Callable callable;
	};
	const ToolRegistration tools[] = {
		{ "godot.runtime.harness.start", "Compile and start an asynchronous inline runtime Harness job.", callable_mp(this, &MCPHarnessProvider::start) },
		{ "godot.runtime.harness.status", "Get bounded progress for a runtime Harness job.", callable_mp(this, &MCPHarnessProvider::get_status) },
		{ "godot.runtime.harness.cancel", "Cancel a runtime Harness job and its active asynchronous child.", callable_mp(this, &MCPHarnessProvider::cancel) },
		{ "godot.runtime.harness.get_report", "Get the structured report accumulated by a runtime Harness job.", callable_mp(this, &MCPHarnessProvider::get_report) },
	};
	for (int i = 0; i < 4; i++) {
		const Dictionary schema = i == 0 ? _start_schema() : _job_schema();
		const Error error = p_registry->register_tool(MCPToolUtils::make_tool_definition(tools[i].name, tools[i].description, schema),
				tools[i].callable, MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
		if (error != OK) {
			p_registry->unregister_tools_for_owner(this);
			return error;
		}
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
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "script", "debuggerSession", "runtimeGeneration" }, error) ? service.start(p_arguments, _make_context(p_context)) : error;
}

Dictionary MCPHarnessProvider::get_status(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "jobId", "debuggerSession", "runtimeGeneration" }, error) ? service.get_status(p_arguments, _make_context(p_context)) : error;
}

Dictionary MCPHarnessProvider::cancel(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "jobId", "debuggerSession", "runtimeGeneration" }, error) ? service.cancel(p_arguments, _make_context(p_context)) : error;
}

Dictionary MCPHarnessProvider::get_report(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "jobId", "debuggerSession", "runtimeGeneration" }, error) ? service.get_report(p_arguments, _make_context(p_context)) : error;
}
