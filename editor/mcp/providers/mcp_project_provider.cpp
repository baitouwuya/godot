/**************************************************************************/
/*  mcp_project_provider.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
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

#include "mcp_project_provider.h"

#include "mcp_project_tool_utils.h"
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

MCPProjectProvider::~MCPProjectProvider() {
	unregister_tools();
}

Error MCPProjectProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Project tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.project.get_settings", "List visible project settings with bounded optional values.",
				MCPProjectToolUtils::settings_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPProjectProvider::get_settings), MCPProjectToolUtils::settings_output_schema() },
		{ "godot.project.get_setting", "Get one project setting and its editor metadata.",
				MCPProjectToolUtils::setting_name_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPProjectProvider::get_setting), MCPProjectToolUtils::setting_output_schema(true) },
		{ "godot.project.set_setting", "Set a project setting through editor undo/redo without saving project.godot.",
				MCPProjectToolUtils::set_setting_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPProjectProvider::set_setting), MCPProjectToolUtils::named_mutation_output_schema("changed", "Whether the project setting changed.", true) },
		{ "godot.project.erase_setting", "Erase a project setting through editor undo/redo without saving project.godot.",
				MCPProjectToolUtils::setting_name_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPProjectProvider::erase_setting), MCPProjectToolUtils::named_mutation_output_schema("erased", "Whether the project setting was erased.") },
		{ "godot.project.save", "Explicitly save current project settings to project.godot.",
				MCPToolUtils::make_object_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPProjectProvider::save), MCPProjectToolUtils::save_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPProjectProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPProjectProvider::get_settings(const Dictionary &p_arguments, const Dictionary &) {
	return settings_service.get_settings(p_arguments);
}

Dictionary MCPProjectProvider::get_setting(const Dictionary &p_arguments, const Dictionary &) {
	return settings_service.get_setting(p_arguments);
}

Dictionary MCPProjectProvider::set_setting(const Dictionary &p_arguments, const Dictionary &) {
	return settings_service.set_setting(p_arguments);
}

Dictionary MCPProjectProvider::erase_setting(const Dictionary &p_arguments, const Dictionary &) {
	return settings_service.erase_setting(p_arguments);
}

Dictionary MCPProjectProvider::save(const Dictionary &, const Dictionary &) {
	return settings_service.save();
}
