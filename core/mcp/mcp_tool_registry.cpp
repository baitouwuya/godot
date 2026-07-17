/**************************************************************************/
/*  mcp_tool_registry.cpp                                                 */
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

#include "mcp_tool_registry.h"

Dictionary MCPToolCallContext::to_dictionary() const {
	Dictionary context;
	context["requestId"] = request_id;
	context["protocolVersion"] = protocol_version;
	context["session"] = session.duplicate(true);
	context["project"] = project.duplicate(true);
	return context;
}

void MCPToolRegistry::_invalidate_definition_cache() {
	definition_cache.clear();
	definition_cache_valid = false;
}

const Array &MCPToolRegistry::_get_cached_tool_definitions() const {
	if (definition_cache_valid) {
		return definition_cache;
	}

	definition_cache.clear();
	for (const StringName &name : registration_order) {
		const ToolEntry *entry = tools.getptr(name);
		if (entry && entry->handler.is_valid()) {
			definition_cache.push_back(entry->definition);
		}
	}
	definition_cache_valid = true;
	return definition_cache;
}

Error MCPToolRegistry::register_tool(const Dictionary &p_definition, const Callable &p_handler, Object *p_owner, String *r_error) {
	if (r_error) {
		*r_error = String();
	}

	const Variant name_value = p_definition.get("name", Variant());
	if (name_value.get_type() != Variant::STRING && name_value.get_type() != Variant::STRING_NAME) {
		if (r_error) {
			*r_error = "Tool definition requires a string 'name'.";
		}
		return ERR_INVALID_PARAMETER;
	}

	const StringName name = name_value;
	if (name == StringName()) {
		if (r_error) {
			*r_error = "Tool name cannot be empty.";
		}
		return ERR_INVALID_PARAMETER;
	}
	if (tools.has(name)) {
		if (r_error) {
			*r_error = "A tool named '" + String(name) + "' is already registered.";
		}
		return ERR_ALREADY_EXISTS;
	}
	if (p_definition.has("description") && p_definition["description"].get_type() != Variant::STRING) {
		if (r_error) {
			*r_error = "Tool definition 'description' must be a string.";
		}
		return ERR_INVALID_PARAMETER;
	}
	if (p_definition.has("inputSchema") && p_definition["inputSchema"].get_type() != Variant::DICTIONARY) {
		if (r_error) {
			*r_error = "Tool definition 'inputSchema' must be a dictionary.";
		}
		return ERR_INVALID_PARAMETER;
	}
	if (!p_handler.is_valid()) {
		if (r_error) {
			*r_error = "Tool handler is not callable.";
		}
		return ERR_INVALID_PARAMETER;
	}

	Dictionary definition = p_definition.duplicate(true);
	definition["name"] = String(name);
	if (!definition.has("description")) {
		definition["description"] = String();
	}
	if (!definition.has("inputSchema")) {
		Dictionary input_schema;
		input_schema["type"] = "object";
		definition["inputSchema"] = input_schema;
	}

	ToolEntry entry;
	entry.definition = definition;
	entry.handler = p_handler;
	entry.owner_id = p_owner ? p_owner->get_instance_id() : p_handler.get_object_id();
	tools.insert(name, entry);
	registration_order.push_back(name);
	_invalidate_definition_cache();
	return OK;
}

bool MCPToolRegistry::unregister_tool(const StringName &p_name) {
	if (!tools.erase(p_name)) {
		return false;
	}
	registration_order.erase(p_name);
	_invalidate_definition_cache();
	return true;
}

int MCPToolRegistry::unregister_tools_for_owner(Object *p_owner) {
	return p_owner ? unregister_tools_for_owner(p_owner->get_instance_id()) : 0;
}

int MCPToolRegistry::unregister_tools_for_owner(ObjectID p_owner_id) {
	if (p_owner_id.is_null()) {
		return 0;
	}

	Vector<StringName> names_to_remove;
	for (const StringName &name : registration_order) {
		const ToolEntry *entry = tools.getptr(name);
		if (entry && entry->owner_id == p_owner_id) {
			names_to_remove.push_back(name);
		}
	}
	for (const StringName &name : names_to_remove) {
		unregister_tool(name);
	}
	return names_to_remove.size();
}

bool MCPToolRegistry::has_tool(const StringName &p_name) const {
	const ToolEntry *entry = tools.getptr(p_name);
	return entry && entry->handler.is_valid();
}

Array MCPToolRegistry::get_tool_definitions() const {
	return _get_cached_tool_definitions().duplicate(true);
}

PackedStringArray MCPToolRegistry::get_tool_names() const {
	PackedStringArray names;
	for (const StringName &name : registration_order) {
		const ToolEntry *entry = tools.getptr(name);
		if (entry && entry->handler.is_valid()) {
			names.push_back(String(name));
		}
	}
	return names;
}

MCPToolRegistry::CallResult MCPToolRegistry::call_tool(const StringName &p_name, const Dictionary &p_arguments, const MCPToolCallContext &p_context) const {
	CallResult call_result;
	const ToolEntry *entry = tools.getptr(p_name);
	if (!entry) {
		call_result.status = CALL_TOOL_NOT_FOUND;
		call_result.message = "Unknown tool: " + String(p_name);
		return call_result;
	}
	if (!entry->handler.is_valid()) {
		call_result.status = CALL_TOOL_UNAVAILABLE;
		call_result.message = "Tool is no longer available: " + String(p_name);
		return call_result;
	}

	Variant arguments = p_arguments.duplicate(true);
	Variant context = p_context.to_dictionary();
	const Variant *argument_pointers[2] = { &arguments, &context };
	Variant result;
	Callable::CallError call_error;
	entry->handler.callp(argument_pointers, 2, result, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		call_result.status = CALL_HANDLER_FAILED;
		call_result.message = "Tool handler rejected the call: " + String(p_name);
		return call_result;
	}
	if (result.get_type() != Variant::DICTIONARY) {
		call_result.status = CALL_INVALID_RESULT;
		call_result.message = "Tool handler must return a dictionary: " + String(p_name);
		return call_result;
	}

	call_result.result = result;
	return call_result;
}
