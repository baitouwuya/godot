/**************************************************************************/
/*  test_mcp_cli_runtime.cpp                                              */
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

#include "main/mcp_cli_bootstrap.h"
#include "main/mcp_cli_runtime.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_cli_runtime);

namespace TestMCPCLIRuntime {

TEST_CASE("[MCP][CLIRuntime] Selection is limited to the fixed terminal transport adapters") {
	MCPCLIRuntime runtime;
	String error;
	CHECK(runtime.get_command_registry().get_commands().size() == 2);
	CHECK(runtime.append_raw_argument("--orphan", &error) == ERR_UNCONFIGURED);

	CHECK(runtime.select_command("--latest-log", &error) == ERR_DOES_NOT_EXIST);
	CHECK_FALSE(runtime.has_selected_command());
	REQUIRE(runtime.select_command(MCPCLI::COMMAND_DISCOVER, &error) == OK);
	CHECK(runtime.select_command(MCPCLI::COMMAND_STDIO, &error) == ERR_ALREADY_IN_USE);
	const MCPCLICommandDefinition *selected = runtime.get_selected_definition();
	REQUIRE(selected != nullptr);
	CHECK(selected->terminal);
	CHECK(selected->name == MCPCLI::COMMAND_DISCOVER);
	CHECK(selected->usage == "--mcp-discover --path <directory>");
	CHECK(selected->has_flag(MCP_CLI_COMMAND_FLAG_REQUIRE_EXPLICIT_PROJECT_PATH));
	CHECK(selected->has_flag(MCP_CLI_COMMAND_FLAG_EXCLUSIVE_WITH_EDITOR));
	CHECK(selected->has_flag(MCP_CLI_COMMAND_FLAG_JSON_STDOUT));
	CHECK(selected->has_flag(MCP_CLI_COMMAND_FLAG_FORCE_HEADLESS));
	CHECK(selected->has_flag(MCP_CLI_COMMAND_FLAG_PATH_ARGUMENT_ONLY));

	PackedStringArray arguments;
	arguments.push_back("C:/project");
	int exit_code = 0;
	CHECK(runtime.invoke_selected(arguments, exit_code, &error) == ERR_UNCONFIGURED);
	CHECK(exit_code == 1);

	runtime.clear();
	CHECK_FALSE(runtime.has_selected_command());
	CHECK(runtime.get_raw_arguments().is_empty());
	CHECK(runtime.get_command_registry().get_commands().size() == 2);
}

TEST_CASE("[MCP][CLIRuntime] Raw command arguments are bounded") {
	MCPCLIRuntime runtime;
	String error;
	REQUIRE(runtime.select_command(MCPCLI::COMMAND_STDIO, &error) == OK);

	for (int i = 0; i < MCPCLIRuntime::MAX_RAW_ARGUMENT_COUNT; i++) {
		REQUIRE(runtime.append_raw_argument("value", &error) == OK);
	}
	CHECK(runtime.append_raw_argument("overflow", &error) == ERR_INVALID_PARAMETER);

	runtime.clear();
	REQUIRE(runtime.select_command(MCPCLI::COMMAND_STDIO, &error) == OK);
	const String oversized = String("x").repeat((int)MCPCLIRuntime::MAX_RAW_ARGUMENT_BYTES + 1);
	CHECK(runtime.append_raw_argument(oversized, &error) == ERR_INVALID_PARAMETER);
}

TEST_CASE("[MCP][CLIRuntime] Legacy business command families are rejected") {
	MCPCLIRuntime runtime;
	const char *legacy_commands[] = {
		"--latest-log",
		"--perf-start",
		"--perf-report",
		"--harness-run",
		"--harness-status",
		"--editor-ai-complete",
		"--editor-ai-diagnostics",
	};
	for (const char *legacy_command : legacy_commands) {
		String error;
		CHECK(runtime.select_command(legacy_command, &error) == ERR_DOES_NOT_EXIST);
		CHECK_FALSE(error.is_empty());
		CHECK_FALSE(runtime.has_selected_command());
	}
}

TEST_CASE("[MCP][CLIRuntime] Bootstrap definitions are reusable across lifetimes") {
	for (int iteration = 0; iteration < 2; iteration++) {
		MCPCLIBootstrap bootstrap;
		String error;
		REQUIRE(bootstrap.initialize(&error) == OK);
		const Vector<MCPCLICommandDefinition> commands = bootstrap.get_runtime().get_command_registry().get_commands();
		REQUIRE(commands.size() == 2);
		CHECK(commands[0].name == MCPCLI::COMMAND_DISCOVER);
		CHECK(commands[1].name == MCPCLI::COMMAND_STDIO);
		for (const MCPCLICommandDefinition &command : commands) {
			CHECK_FALSE(command.usage.is_empty());
			CHECK_FALSE(command.description.is_empty());
			CHECK(command.terminal);
			CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_REQUIRE_EXPLICIT_PROJECT_PATH));
			CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_EXCLUSIVE_WITH_EDITOR));
			CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_JSON_STDOUT));
			CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_FORCE_HEADLESS));
			CHECK(command.has_flag(MCP_CLI_COMMAND_FLAG_PATH_ARGUMENT_ONLY));
		}
	}
}

} // namespace TestMCPCLIRuntime
