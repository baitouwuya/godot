/**************************************************************************/
/*  mcp_cli_command_registry.h                                            */
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

#pragma once

#include "core/error/error_list.h"
#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/typed_array.h"

enum MCPCLICommandFlag : uint32_t {
	MCP_CLI_COMMAND_FLAG_NONE = 0,
	MCP_CLI_COMMAND_FLAG_REQUIRE_EXPLICIT_PROJECT_PATH = 1 << 0,
	MCP_CLI_COMMAND_FLAG_EXCLUSIVE_WITH_EDITOR = 1 << 1,
	MCP_CLI_COMMAND_FLAG_JSON_STDOUT = 1 << 2,
	MCP_CLI_COMMAND_FLAG_FORCE_HEADLESS = 1 << 3,
	MCP_CLI_COMMAND_FLAG_PATH_ARGUMENT_ONLY = 1 << 4,
};

struct MCPCLICommandDefinition {
	StringName name;
	String usage;
	String description;
	bool terminal = true;
	uint32_t flags = MCP_CLI_COMMAND_FLAG_NONE;

	bool has_flag(MCPCLICommandFlag p_flag) const { return (flags & p_flag) != 0; }
};

class MCPCLICommandHandler {
public:
	virtual int execute_cli_command(const StringName &p_command, const PackedStringArray &p_arguments) = 0;
	virtual ~MCPCLICommandHandler() = default;
};

class MCPCLICommandRegistry;

class MCPCLICommandProvider : public MCPCLICommandHandler {
public:
	virtual Error register_cli_commands(MCPCLICommandRegistry &r_registry, String *r_error = nullptr) = 0;
	~MCPCLICommandProvider() override = default;
};

class MCPCLICommandRegistry {
	struct CommandEntry {
		MCPCLICommandDefinition definition;
		MCPCLICommandProvider *provider = nullptr;
	};

	HashMap<StringName, CommandEntry> commands;
	Vector<StringName> registration_order;

public:
	MCPCLICommandRegistry() = default;
	MCPCLICommandRegistry(const MCPCLICommandRegistry &) = delete;
	MCPCLICommandRegistry &operator=(const MCPCLICommandRegistry &) = delete;
	MCPCLICommandRegistry(MCPCLICommandRegistry &&) = delete;
	MCPCLICommandRegistry &operator=(MCPCLICommandRegistry &&) = delete;

	Error register_command(
			const MCPCLICommandDefinition &p_definition,
			MCPCLICommandProvider *p_provider,
			String *r_error = nullptr);
	bool unregister_command(const StringName &p_name);
	int unregister_commands_for_provider(MCPCLICommandProvider *p_provider);
	bool has_command(const StringName &p_name) const;
	const MCPCLICommandDefinition *get_command(const StringName &p_name) const;
	Vector<MCPCLICommandDefinition> get_commands() const;
	Error invoke(
			const StringName &p_name,
			const PackedStringArray &p_arguments,
			int &r_exit_code,
			String *r_error = nullptr) const;
};
