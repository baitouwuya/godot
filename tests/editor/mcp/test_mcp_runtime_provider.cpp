/**************************************************************************/
/*  test_mcp_runtime_provider.cpp                                         */
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

#include "core/input/input_event_codec.h"
#include "core/mcp/mcp_tool_registry.h"
#include "editor/mcp/providers/mcp_runtime_debug_service.h"
#include "editor/mcp/providers/mcp_runtime_input.h"
#include "editor/mcp/providers/mcp_runtime_provider.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_runtime_provider);

namespace TestMCPRuntimeProvider {

static MCPToolRegistry::CallResult _call(MCPToolRegistry &p_registry, const StringName &p_name, const Dictionary &p_arguments) {
	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	return p_registry.call_tool(p_name, p_arguments, context);
}

static String _error_code(const MCPToolRegistry::CallResult &p_result) {
	INFO(p_result.result);
	REQUIRE(p_result.status == MCPToolRegistry::CALL_OK);
	REQUIRE(bool(p_result.result.get("isError", false)));
	const Dictionary content = p_result.result.get("structuredContent", Dictionary());
	return Dictionary(content.get("error", Dictionary())).get("code", String());
}

TEST_CASE("[MCP][Provider] Runtime tools register in a stable strict surface") {
	MCPRuntimeDebugService service(nullptr);
	MCPRuntimeProvider provider(&service);
	MCPToolRegistry registry;
	REQUIRE(provider.register_tools(&registry) == OK);
	CHECK(provider.register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(names.size() == 10);
	CHECK(names[0] == "godot.runtime.get_state");
	CHECK(names[1] == "godot.runtime.play");
	CHECK(names[2] == "godot.runtime.stop");
	CHECK(names[3] == "godot.runtime.pause");
	CHECK(names[4] == "godot.runtime.resume");
	CHECK(names[5] == "godot.runtime.next_frame");
	CHECK(names[6] == "godot.runtime.get_tree");
	CHECK(names[7] == "godot.runtime.node.get_properties");
	CHECK(names[8] == "godot.runtime.node.set_property");
	CHECK(names[9] == "godot.runtime.input.send");
	CHECK(registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI).is_empty());

	const Array definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(definitions.size() == 10);
	for (const Variant &definition_value : definitions) {
		const Dictionary schema = Dictionary(definition_value).get("inputSchema", Dictionary());
		CHECK(schema.get("type", String()) == "object");
		CHECK_FALSE(bool(schema.get("additionalProperties", true)));
	}

	Dictionary invalid;
	invalid["unknown"] = true;
	CHECK(_error_code(_call(registry, "godot.runtime.get_state", invalid)) == "INVALID_ARGUMENTS");
	CHECK(_error_code(_call(registry, "godot.runtime.get_tree", invalid)) == "INVALID_ARGUMENTS");
	provider.unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
}

TEST_CASE("[MCP][Provider] Runtime input uses Godot's debugger codec for key and mouse events") {
	Dictionary key;
	key["type"] = "key";
	key["keycode"] = int64_t(Key::A);
	key["pressed"] = true;
	key["shift"] = true;
	MCPRuntimeInput::EncodedEvent encoded;
	String error;
	REQUIRE(MCPRuntimeInput::encode(key, encoded, &error) == OK);
	CHECK(encoded.message == "scene:inject_input_event");
	REQUIRE(encoded.arguments.size() == 1);
	Ref<InputEvent> decoded;
	REQUIRE(decode_input_event(encoded.arguments[0], decoded) == OK);
	Ref<InputEventKey> decoded_key = decoded;
	REQUIRE(decoded_key.is_valid());
	CHECK(decoded_key->get_keycode() == Key::A);
	CHECK(decoded_key->is_pressed());
	CHECK(decoded_key->is_shift_pressed());

	Dictionary mouse;
	mouse["type"] = "mouse_button";
	mouse["buttonIndex"] = int64_t(MouseButton::LEFT);
	mouse["pressed"] = false;
	mouse["position"] = Array{ 12.5, 30.0 };
	REQUIRE(MCPRuntimeInput::encode(mouse, encoded, &error) == OK);
	REQUIRE(decode_input_event(encoded.arguments[0], decoded) == OK);
	Ref<InputEventMouseButton> decoded_mouse = decoded;
	REQUIRE(decoded_mouse.is_valid());
	CHECK(decoded_mouse->get_button_index() == MouseButton::LEFT);
	CHECK_FALSE(decoded_mouse->is_pressed());
	CHECK(decoded_mouse->get_position().is_equal_approx(Vector2(12.5, 30.0)));
}

TEST_CASE("[MCP][Provider] Runtime input rejects unknown and malformed events before dispatch") {
	MCPRuntimeInput::EncodedEvent encoded;
	String error;
	Dictionary unknown;
	unknown["type"] = "touch";
	CHECK(MCPRuntimeInput::encode(unknown, encoded, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("Unsupported"));

	Dictionary invalid_mouse;
	invalid_mouse["type"] = "mouse_button";
	invalid_mouse["buttonIndex"] = 0;
	CHECK(MCPRuntimeInput::encode(invalid_mouse, encoded, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("buttonIndex"));

	Dictionary invalid_position;
	invalid_position["type"] = "mouse_motion";
	invalid_position["position"] = Array{ 1.0 };
	CHECK(MCPRuntimeInput::encode(invalid_position, encoded, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("position"));

	PackedByteArray truncated;
	truncated.push_back(uint8_t(InputEventType::KEY));
	Ref<InputEvent> decoded;
	CHECK(decode_input_event(truncated, decoded) == ERR_INVALID_DATA);
	CHECK(decoded.is_null());
}

} // namespace TestMCPRuntimeProvider
