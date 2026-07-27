/**************************************************************************/
/*  mcp_resource_provider.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_resource_provider.h"

#include "mcp_resource_tool_utils.h"
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

MCPResourceProvider::MCPResourceProvider(const String &p_project_root, ImportFunction p_import_function, ScanFunction p_scan_function) :
		edit_service(p_project_root),
		import_service(p_project_root, p_import_function, p_scan_function) {
}

MCPResourceProvider::~MCPResourceProvider() {
	unregister_tools();
}

Error MCPResourceProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Resource tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
			{ "godot.resource.get_properties", "Get editable Inspector properties from a cached project Resource.",
					MCPResourceToolUtils::path_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPResourceProvider::get_properties), MCPResourceToolUtils::get_properties_output_schema() },
			{ "godot.resource.set_property", "Set a cached project Resource property without saving it.",
					MCPResourceToolUtils::set_property_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPResourceProvider::set_property), MCPResourceToolUtils::set_property_output_schema() },
			{ "godot.resource.save", "Explicitly save a cached project Resource.",
					MCPResourceToolUtils::path_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPResourceProvider::save), MCPResourceToolUtils::save_output_schema() },
			{ "godot.resource.import_options", "List ResourceImporter candidates and effective import options for an external asset.",
					MCPResourceToolUtils::import_options_schema(false), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPResourceProvider::import_options), MCPResourceToolUtils::import_options_output_schema() },
			{ "godot.resource.import", "Copy an external asset into the project, roll back known target-owned artifacts, and report unowned residuals.",
					MCPResourceToolUtils::import_options_schema(true), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPResourceProvider::import_resource), MCPResourceToolUtils::import_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPResourceProvider::unregister_tools() {
	if (!tool_registry) {
		edit_service.clear();
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
	edit_service.clear();
}

Dictionary MCPResourceProvider::get_properties(const Dictionary &p_arguments, const Dictionary &) {
	return edit_service.get_properties(p_arguments);
}

Dictionary MCPResourceProvider::set_property(const Dictionary &p_arguments, const Dictionary &) {
	return edit_service.set_property(p_arguments);
}

Dictionary MCPResourceProvider::save(const Dictionary &p_arguments, const Dictionary &) {
	return edit_service.save(p_arguments);
}

Dictionary MCPResourceProvider::import_options(const Dictionary &p_arguments, const Dictionary &) {
	return import_service.get_options(p_arguments);
}

Dictionary MCPResourceProvider::import_resource(const Dictionary &p_arguments, const Dictionary &) {
	return import_service.import_resource(p_arguments);
}
