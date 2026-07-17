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

MCPCLICommandRegistry::MCPCLICommandRegistry() {
	MCPCLICommandDefinition discover;
	discover.name = MCP_CLI_COMMAND_DISCOVER;
	discover.usage = "--mcp-discover --path <directory>";
	discover.description = "Print the running MCP endpoint for a Godot project.";
	discover.flags = MCP_CLI_COMMAND_FLAG_REQUIRE_EXPLICIT_PROJECT_PATH |
			MCP_CLI_COMMAND_FLAG_EXCLUSIVE_WITH_EDITOR |
			MCP_CLI_COMMAND_FLAG_JSON_STDOUT |
			MCP_CLI_COMMAND_FLAG_FORCE_HEADLESS |
			MCP_CLI_COMMAND_FLAG_PATH_ARGUMENT_ONLY;
	commands.push_back(discover);

	MCPCLICommandDefinition stdio;
	stdio.name = MCP_CLI_COMMAND_STDIO;
	stdio.usage = "--mcp-stdio --path <directory>";
	stdio.description = "Bridge newline-delimited MCP stdio to the project's HTTP endpoint.";
	stdio.flags = discover.flags;
	commands.push_back(stdio);
}

bool MCPCLICommandRegistry::has_command(const StringName &p_name) const {
	return get_command(p_name) != nullptr;
}

const MCPCLICommandDefinition *MCPCLICommandRegistry::get_command(const StringName &p_name) const {
	for (const MCPCLICommandDefinition &command : commands) {
		if (command.name == p_name) {
			return &command;
		}
	}
	return nullptr;
}

Vector<MCPCLICommandDefinition> MCPCLICommandRegistry::get_commands() const {
	return commands;
}
