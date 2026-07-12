/**************************************************************************/
/*  mcp_cli_command_registry.cpp                                          */
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

#include "mcp_cli_command_registry.h"

namespace {

void set_error(String *r_error, const String &p_error) {
	if (r_error) {
		*r_error = p_error;
	}
}

} // namespace

Error MCPCLICommandRegistry::register_command(
		const MCPCLICommandDefinition &p_definition,
		MCPCLICommandProvider *p_provider,
		String *r_error) {
	set_error(r_error, String());
	if (p_definition.name == StringName()) {
		set_error(r_error, "CLI command name cannot be empty.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_definition.usage.is_empty()) {
		set_error(r_error, vformat("CLI command '%s' requires usage text.", p_definition.name));
		return ERR_INVALID_PARAMETER;
	}
	if (p_definition.description.is_empty()) {
		set_error(r_error, vformat("CLI command '%s' requires a description.", p_definition.name));
		return ERR_INVALID_PARAMETER;
	}
	if (!p_provider) {
		set_error(r_error, vformat("CLI command '%s' requires a provider.", p_definition.name));
		return ERR_INVALID_PARAMETER;
	}
	if (commands.has(p_definition.name)) {
		set_error(r_error, vformat("CLI command '%s' is already registered.", p_definition.name));
		return ERR_ALREADY_EXISTS;
	}

	CommandEntry entry;
	entry.definition = p_definition;
	entry.provider = p_provider;
	commands.insert(p_definition.name, entry);
	registration_order.push_back(p_definition.name);
	return OK;
}

bool MCPCLICommandRegistry::unregister_command(const StringName &p_name) {
	if (!commands.erase(p_name)) {
		return false;
	}
	registration_order.erase(p_name);
	return true;
}

int MCPCLICommandRegistry::unregister_commands_for_provider(MCPCLICommandProvider *p_provider) {
	if (!p_provider) {
		return 0;
	}

	Vector<StringName> names_to_remove;
	for (const StringName &name : registration_order) {
		const CommandEntry *entry = commands.getptr(name);
		if (entry && entry->provider == p_provider) {
			names_to_remove.push_back(name);
		}
	}
	for (const StringName &name : names_to_remove) {
		unregister_command(name);
	}
	return names_to_remove.size();
}

bool MCPCLICommandRegistry::has_command(const StringName &p_name) const {
	const CommandEntry *entry = commands.getptr(p_name);
	return entry && entry->provider;
}

const MCPCLICommandDefinition *MCPCLICommandRegistry::get_command(const StringName &p_name) const {
	const CommandEntry *entry = commands.getptr(p_name);
	return entry ? &entry->definition : nullptr;
}

Vector<MCPCLICommandDefinition> MCPCLICommandRegistry::get_commands() const {
	Vector<MCPCLICommandDefinition> definitions;
	for (const StringName &name : registration_order) {
		const CommandEntry *entry = commands.getptr(name);
		if (entry) {
			definitions.push_back(entry->definition);
		}
	}
	return definitions;
}

Error MCPCLICommandRegistry::invoke(
		const StringName &p_name,
		const PackedStringArray &p_arguments,
		int &r_exit_code,
		String *r_error) const {
	r_exit_code = 1;
	set_error(r_error, String());
	const CommandEntry *entry = commands.getptr(p_name);
	if (!entry || !entry->provider) {
		set_error(r_error, vformat("CLI command '%s' is not registered.", p_name));
		return ERR_DOES_NOT_EXIST;
	}
	r_exit_code = entry->provider->execute_cli_command(p_name, p_arguments);
	return OK;
}
