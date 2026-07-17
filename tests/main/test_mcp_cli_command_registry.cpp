/**************************************************************************/
/*  test_mcp_cli_command_registry.cpp                                     */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_cli_command_registry);

#include "main/mcp_cli_command_registry.h"

namespace TestMCPCLICommandRegistry {

TEST_CASE("[MCP][CLICommandRegistry] Exposes only the fixed MCP transport adapters") {
	MCPCLICommandRegistry registry;
	const Vector<MCPCLICommandDefinition> commands = registry.get_commands();
	REQUIRE(commands.size() == 2);
	CHECK(commands[0].name == MCP_CLI_COMMAND_DISCOVER);
	CHECK(commands[1].name == MCP_CLI_COMMAND_STDIO);

	for (const MCPCLICommandDefinition &command : commands) {
		CHECK(command.terminal);
		CHECK_FALSE(command.usage.is_empty());
		CHECK_FALSE(command.description.is_empty());
		CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_REQUIRE_EXPLICIT_PROJECT_PATH));
		CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_EXCLUSIVE_WITH_EDITOR));
		CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_JSON_STDOUT));
		CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_FORCE_HEADLESS));
		CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_PATH_ARGUMENT_ONLY));
	}

	CHECK(registry.has_command(MCP_CLI_COMMAND_DISCOVER));
	CHECK(registry.has_command(MCP_CLI_COMMAND_STDIO));
	CHECK_FALSE(registry.has_command("--mcp-test"));
	CHECK(registry.get_command("--mcp-test") == nullptr);

	Vector<MCPCLICommandDefinition> mutated = registry.get_commands();
	mutated.write[0].name = "--latest-log";
	CHECK(registry.has_command(MCP_CLI_COMMAND_DISCOVER));
	CHECK_FALSE(registry.has_command("--latest-log"));
}

TEST_CASE("[MCP][CLICommandRegistry] Legacy business CLI commands cannot enter the adapter command set") {
	MCPCLICommandRegistry registry;
	const char *legacy_commands[] = {
		"--latest-log",
		"--perf-start",
		"--perf-status",
		"--perf-stop",
		"--harness-run",
		"--harness-report",
		"--editor-ai-complete",
		"--editor-ai-lsp",
	};
	for (const char *legacy_command : legacy_commands) {
		CHECK_FALSE(registry.has_command(legacy_command));
		CHECK(registry.get_command(legacy_command) == nullptr);
	}

	for (const MCPCLICommandDefinition &command : registry.get_commands()) {
		const String name = command.name;
		CHECK(name != "--latest-log");
		CHECK_FALSE(name.begins_with("--perf-"));
		CHECK_FALSE(name.begins_with("--harness-"));
		CHECK_FALSE(name.begins_with("--editor-ai-"));
	}
}

} // namespace TestMCPCLICommandRegistry
