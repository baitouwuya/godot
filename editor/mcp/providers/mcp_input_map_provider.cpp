/**************************************************************************/
/*  mcp_input_map_provider.cpp                                            */
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

#include "mcp_input_map_provider.h"

#include "mcp_input_map_tool_utils.h"
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

MCPInputMapProvider::~MCPInputMapProvider() {
	unregister_tools();
}

Error MCPInputMapProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Input Map tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.input.get_actions", "List project Input Map actions and semantic event bindings.",
				MCPInputMapToolUtils::get_actions_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPInputMapProvider::get_actions), MCPInputMapToolUtils::get_actions_output_schema() },
		{ "godot.input.set_action", "Create or replace an Input Map action through editor undo/redo without saving project.godot.",
				MCPInputMapToolUtils::set_action_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPInputMapProvider::set_action), MCPInputMapToolUtils::action_output_schema(true) },
		{ "godot.input.remove_action", "Remove a custom Input Map action through editor undo/redo.",
				MCPInputMapToolUtils::name_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPInputMapProvider::remove_action), MCPInputMapToolUtils::remove_action_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPInputMapProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPInputMapProvider::get_actions(const Dictionary &p_arguments, const Dictionary &) {
	return input_map_service.get_actions(p_arguments);
}

Dictionary MCPInputMapProvider::set_action(const Dictionary &p_arguments, const Dictionary &) {
	return input_map_service.set_action(p_arguments);
}

Dictionary MCPInputMapProvider::remove_action(const Dictionary &p_arguments, const Dictionary &) {
	return input_map_service.remove_action(p_arguments);
}
