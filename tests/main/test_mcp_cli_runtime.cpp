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

class RecordingProvider final : public MCPCLICommandProvider {
public:
	StringName invoked_command;
	PackedStringArray invoked_arguments;
	int exit_code = 23;
	bool fail_registration = false;

	Error register_cli_commands(MCPCLICommandRegistry &r_registry, String *r_error = nullptr) override {
		MCPCLICommandDefinition first;
		first.name = "--external-first";
		first.usage = "--external-first --path <directory>";
		first.description = "First external command.";
		first.flags = MCP_CLI_COMMAND_FLAG_REQUIRE_EXPLICIT_PROJECT_PATH |
				MCP_CLI_COMMAND_FLAG_EXCLUSIVE_WITH_EDITOR |
				MCP_CLI_COMMAND_FLAG_JSON_STDOUT |
				MCP_CLI_COMMAND_FLAG_FORCE_HEADLESS;
		Error error = r_registry.register_command(first, this, r_error);
		if (error != OK || fail_registration) {
			return error == OK ? FAILED : error;
		}

		MCPCLICommandDefinition second;
		second.name = "--external-second";
		second.usage = "--external-second";
		second.description = "Second external command.";
		second.terminal = false;
		return r_registry.register_command(second, this, r_error);
	}

	int execute_cli_command(const StringName &p_command, const PackedStringArray &p_arguments) override {
		invoked_command = p_command;
		invoked_arguments = p_arguments;
		return exit_code;
	}
};

TEST_CASE("[MCP][CLIRuntime] Compile-time providers drive selection policy and dispatch") {
	RecordingProvider provider;
	MCPCLIRuntime runtime;
	String error;
	REQUIRE(runtime.register_provider(&provider, &error) == OK);
	CHECK(runtime.register_provider(&provider, &error) == ERR_ALREADY_IN_USE);
	CHECK(runtime.get_command_registry().get_commands().size() == 2);
	CHECK(runtime.append_raw_argument("--orphan", &error) == ERR_UNCONFIGURED);

	REQUIRE(runtime.select_command("--external-first", &error) == OK);
	CHECK(runtime.select_command("--external-second", &error) == ERR_ALREADY_IN_USE);
	const MCPCLICommandDefinition *selected = runtime.get_selected_definition();
	REQUIRE(selected != nullptr);
	CHECK(selected->terminal);
	CHECK(selected->usage == "--external-first --path <directory>");
	CHECK(selected->description == "First external command.");
	CHECK(selected->has_flag(MCP_CLI_COMMAND_FLAG_REQUIRE_EXPLICIT_PROJECT_PATH));
	CHECK(selected->has_flag(MCP_CLI_COMMAND_FLAG_EXCLUSIVE_WITH_EDITOR));
	CHECK(selected->has_flag(MCP_CLI_COMMAND_FLAG_JSON_STDOUT));
	CHECK(selected->has_flag(MCP_CLI_COMMAND_FLAG_FORCE_HEADLESS));
	REQUIRE(runtime.append_raw_argument("--unknown-option", &error) == OK);
	REQUIRE(runtime.append_raw_argument("raw value", &error) == OK);
	CHECK(runtime.get_raw_arguments().size() == 2);

	PackedStringArray arguments;
	arguments.push_back("C:/project");
	PackedStringArray expected_arguments = arguments;
	expected_arguments.push_back("--unknown-option");
	expected_arguments.push_back("raw value");
	int exit_code = 0;
	REQUIRE(runtime.invoke_selected(arguments, exit_code, &error) == OK);
	CHECK(exit_code == provider.exit_code);
	CHECK(provider.invoked_command == "--external-first");
	CHECK(provider.invoked_arguments == expected_arguments);

	CHECK(runtime.unregister_provider(&provider));
	CHECK_FALSE(runtime.has_selected_command());
	CHECK(runtime.get_raw_arguments().is_empty());
	CHECK(runtime.get_command_registry().get_commands().is_empty());
}

TEST_CASE("[MCP][CLIRuntime] Raw command arguments are bounded") {
	RecordingProvider provider;
	MCPCLIRuntime runtime;
	String error;
	REQUIRE(runtime.register_provider(&provider, &error) == OK);
	REQUIRE(runtime.select_command("--external-first", &error) == OK);

	for (int i = 0; i < MCPCLIRuntime::MAX_RAW_ARGUMENT_COUNT; i++) {
		REQUIRE(runtime.append_raw_argument("value", &error) == OK);
	}
	CHECK(runtime.append_raw_argument("overflow", &error) == ERR_INVALID_PARAMETER);

	runtime.clear();
	REQUIRE(runtime.register_provider(&provider, &error) == OK);
	REQUIRE(runtime.select_command("--external-first", &error) == OK);
	const String oversized = String("x").repeat((int)MCPCLIRuntime::MAX_RAW_ARGUMENT_BYTES + 1);
	CHECK(runtime.append_raw_argument(oversized, &error) == ERR_INVALID_PARAMETER);
}

TEST_CASE("[MCP][CLIRuntime] Failed providers roll back and clear supports repeated setup") {
	RecordingProvider provider;
	provider.fail_registration = true;
	MCPCLIRuntime runtime;
	String error;
	CHECK(runtime.register_provider(&provider, &error) == FAILED);
	CHECK(runtime.get_command_registry().get_commands().is_empty());

	provider.fail_registration = false;
	REQUIRE(runtime.register_provider(&provider, &error) == OK);
	REQUIRE(runtime.select_command("--external-second", &error) == OK);
	REQUIRE(runtime.get_selected_definition() != nullptr);
	CHECK_FALSE(runtime.get_selected_definition()->terminal);
	runtime.clear();
	CHECK_FALSE(runtime.has_selected_command());
	CHECK(runtime.get_raw_arguments().is_empty());
	CHECK(runtime.get_command_registry().get_commands().is_empty());
	REQUIRE(runtime.register_provider(&provider, &error) == OK);
}

TEST_CASE("[MCP][CLIRuntime] Bootstrap definitions are reusable across lifetimes") {
	for (int iteration = 0; iteration < 2; iteration++) {
		MCPCLIBootstrap bootstrap;
		String error;
		REQUIRE(bootstrap.initialize(&error) == OK);
		const Vector<MCPCLICommandDefinition> commands = bootstrap.get_runtime().get_command_registry().get_commands();
		REQUIRE(commands.size() == 2);
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
