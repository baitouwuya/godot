/**************************************************************************/
/*  mcp_cli_runtime.cpp                                                   */
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

#include "mcp_cli_runtime.h"

namespace {

void set_error(String *r_error, const String &p_error) {
	if (r_error) {
		*r_error = p_error;
	}
}

} // namespace

void MCPCLIRuntime::_clear_selection() {
	selected_command = StringName();
	selected_raw_arguments.clear();
	selected_raw_argument_bytes = 0;
}

Error MCPCLIRuntime::register_provider(MCPCLICommandProvider *p_provider, String *r_error) {
	set_error(r_error, String());
	if (!p_provider) {
		set_error(r_error, "A CLI command provider is required.");
		return ERR_INVALID_PARAMETER;
	}
	for (MCPCLICommandProvider *provider : providers) {
		if (provider == p_provider) {
			set_error(r_error, "The CLI command provider is already registered.");
			return ERR_ALREADY_IN_USE;
		}
	}

	const Error error = p_provider->register_cli_commands(command_registry, r_error);
	if (error != OK) {
		command_registry.unregister_commands_for_provider(p_provider);
		return error;
	}
	providers.push_back(p_provider);
	return OK;
}

bool MCPCLIRuntime::unregister_provider(MCPCLICommandProvider *p_provider) {
	for (int i = 0; i < providers.size(); i++) {
		if (providers[i] != p_provider) {
			continue;
		}
		command_registry.unregister_commands_for_provider(p_provider);
		providers.remove_at(i);
		if (!selected_command.is_empty() && !command_registry.has_command(selected_command)) {
			_clear_selection();
		}
		return true;
	}
	return false;
}

void MCPCLIRuntime::clear() {
	while (!providers.is_empty()) {
		MCPCLICommandProvider *provider = providers[providers.size() - 1];
		command_registry.unregister_commands_for_provider(provider);
		providers.resize(providers.size() - 1);
	}
	_clear_selection();
}

Error MCPCLIRuntime::select_command(const StringName &p_name, String *r_error) {
	set_error(r_error, String());
	if (!command_registry.has_command(p_name)) {
		set_error(r_error, vformat("CLI command '%s' is not registered.", p_name));
		return ERR_DOES_NOT_EXIST;
	}
	if (!selected_command.is_empty()) {
		set_error(r_error, "Only one registered CLI command can be used at a time.");
		return ERR_ALREADY_IN_USE;
	}
	selected_command = p_name;
	selected_raw_arguments.clear();
	selected_raw_argument_bytes = 0;
	return OK;
}

const MCPCLICommandDefinition *MCPCLIRuntime::get_selected_definition() const {
	return selected_command.is_empty() ? nullptr : command_registry.get_command(selected_command);
}

Error MCPCLIRuntime::append_raw_argument(const String &p_argument, String *r_error) {
	set_error(r_error, String());
	if (selected_command.is_empty()) {
		set_error(r_error, "A CLI command must be selected before collecting its arguments.");
		return ERR_UNCONFIGURED;
	}
	if (selected_raw_arguments.size() >= MAX_RAW_ARGUMENT_COUNT) {
		set_error(r_error, vformat("CLI command arguments exceed the limit of %d entries.", MAX_RAW_ARGUMENT_COUNT));
		return ERR_INVALID_PARAMETER;
	}

	const uint64_t argument_bytes = p_argument.utf8().length();
	if (argument_bytes > MAX_RAW_ARGUMENT_BYTES || selected_raw_argument_bytes > MAX_RAW_ARGUMENT_BYTES - argument_bytes) {
		set_error(r_error, vformat("CLI command arguments exceed the limit of %d UTF-8 bytes.", MAX_RAW_ARGUMENT_BYTES));
		return ERR_INVALID_PARAMETER;
	}

	selected_raw_arguments.push_back(p_argument);
	selected_raw_argument_bytes += argument_bytes;
	return OK;
}

Error MCPCLIRuntime::invoke_selected(const PackedStringArray &p_arguments, int &r_exit_code, String *r_error) const {
	if (selected_command.is_empty()) {
		r_exit_code = 1;
		set_error(r_error, "No CLI command is selected.");
		return ERR_UNCONFIGURED;
	}
	PackedStringArray arguments = p_arguments;
	arguments.append_array(selected_raw_arguments);
	return command_registry.invoke(selected_command, arguments, r_exit_code, r_error);
}

MCPCLIRuntime::~MCPCLIRuntime() {
	clear();
}
