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

class RecordingCommandHandler final : public MCPCLICommandHandler {
public:
	StringName command;
	PackedStringArray arguments;
	int exit_code = 17;

	int execute_cli_command(const StringName &p_command, const PackedStringArray &p_arguments) override {
		command = p_command;
		arguments = p_arguments;
		return exit_code;
	}
};

TEST_CASE("[MCP][CLICommandRegistry] Rejects duplicate names and invokes the registered handler") {
	MCPCLICommandRegistry registry;
	RecordingCommandHandler handler;
	MCPCLICommandDefinition definition;
	definition.name = "--mcp-test";
	definition.usage = "--mcp-test <value>";
	definition.description = "Test command.";

	String error;
	REQUIRE(registry.register_command(definition, &handler, &error) == OK);
	CHECK(registry.register_command(definition, &handler, &error) == ERR_ALREADY_EXISTS);
	CHECK_FALSE(error.is_empty());
	CHECK(registry.has_command(definition.name));
	REQUIRE(registry.get_command(definition.name) != nullptr);
	CHECK(registry.get_commands().size() == 1);

	PackedStringArray arguments;
	arguments.push_back("value");
	int exit_code = 0;
	REQUIRE(registry.invoke(definition.name, arguments, exit_code, &error) == OK);
	CHECK(exit_code == handler.exit_code);
	CHECK(handler.command == definition.name);
	CHECK(handler.arguments == arguments);

	CHECK(registry.unregister_command(definition.name));
	CHECK_FALSE(registry.has_command(definition.name));
	CHECK(registry.invoke(definition.name, arguments, exit_code, &error) == ERR_DOES_NOT_EXIST);
}

} // namespace TestMCPCLICommandRegistry
