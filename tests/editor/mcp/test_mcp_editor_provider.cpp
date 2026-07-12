/**************************************************************************/
/*  test_mcp_editor_provider.cpp                                          */
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

#include "core/mcp/mcp_tool_registry.h"
#include "editor/mcp/providers/mcp_editor_provider.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_editor_provider);

namespace TestMCPEditorProvider {

TEST_CASE("[MCP][Provider] Editor tools register as MCP-only composable handlers") {
	MCPToolRegistry registry;
	MCPEditorProvider *provider = memnew(MCPEditorProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(names.size() == 3);
	CHECK(names[0] == "godot.editor.get_state");
	CHECK(names[1] == "godot.editor.undo");
	CHECK(names[2] == "godot.editor.redo");
	CHECK(registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI).is_empty());

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	const MCPToolRegistry::CallResult call_result = registry.call_tool("godot.editor.get_state", Dictionary(), context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	CHECK_FALSE(bool(call_result.result.get("isError", false)));

	const Dictionary state = call_result.result.get("structuredContent", Dictionary());
	CHECK(state.get("currentScene", Variant()).get_type() == Variant::STRING);
	CHECK(state.get("openScripts", Variant()).get_type() == Variant::PACKED_STRING_ARRAY);
	CHECK(state.get("unsavedScripts", Variant()).get_type() == Variant::PACKED_STRING_ARRAY);
	CHECK(state.get("canUndo", Variant()).get_type() == Variant::BOOL);
	CHECK(state.get("canRedo", Variant()).get_type() == Variant::BOOL);

	Dictionary unexpected_arguments;
	unexpected_arguments["unexpected"] = true;
	const MCPToolRegistry::CallResult invalid_call =
			registry.call_tool("godot.editor.get_state", unexpected_arguments, context);
	REQUIRE(invalid_call.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(invalid_call.result.get("isError", false)));

	context.surface = MCPToolRegistry::TOOL_SURFACE_CLI;
	CHECK(registry.call_tool("godot.editor.get_state", Dictionary(), context).status == MCPToolRegistry::CALL_TOOL_NOT_FOUND);

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
}

} // namespace TestMCPEditorProvider
