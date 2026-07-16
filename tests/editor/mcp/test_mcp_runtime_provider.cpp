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
#include "editor/mcp/providers/mcp_runtime_input_sequence.h"
#include "editor/mcp/providers/mcp_runtime_provider.h"
#include "scene/debugger/mcp_runtime_input_event.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_runtime_provider);

namespace TestMCPRuntimeProvider {

static MCPToolRegistry::CallResult _call(MCPToolRegistry &p_registry, const StringName &p_name, const Dictionary &p_arguments, const Dictionary &p_session = Dictionary()) {
	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	if (!p_session.is_empty()) {
		context.session = p_session;
	}
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
	REQUIRE(names.size() == 14);
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
	CHECK(names[10] == "godot.runtime.input.sequence");
	CHECK(names[11] == "godot.runtime.input.sequence_status");
	CHECK(names[12] == "godot.runtime.input.sequence_cancel");
	CHECK(names[13] == "godot.runtime.input.release_all");
	CHECK(registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI).is_empty());

	const Array definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(definitions.size() == 14);
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

	PackedByteArray oversized;
	oversized.resize(MCPRuntimeInputEvent::MAX_ENCODED_EVENT_BYTES + 1);
	Dictionary oversized_payload;
	oversized_payload["kind"] = "event";
	oversized_payload["message"] = "scene:inject_input_event";
	oversized_payload["arguments"] = Array{ oversized };
	MCPRuntimeInputEvent runtime_event;
	CHECK(MCPRuntimeInputEvent::decode(oversized_payload, runtime_event, error) == ERR_OUT_OF_MEMORY);
	CHECK(error.contains("size limit"));
	CHECK(runtime_event.event.is_null());
}

TEST_CASE("[MCP][Provider] Stateful runtime input requires explicit state when requested") {
	MCPRuntimeInput::EncodedEvent encoded;
	String error;
	Dictionary key;
	key["type"] = "key";
	key["keycode"] = int64_t(Key::W);
	CHECK(MCPRuntimeInput::encode(key, encoded, &error, true) == ERR_INVALID_DATA);
	CHECK(error.contains("pressed"));

	key["pressed"] = true;
	REQUIRE(MCPRuntimeInput::encode(key, encoded, &error, true) == OK);
	CHECK(encoded.stateful);
	CHECK(encoded.active);
	CHECK_FALSE(encoded.state_key.is_empty());
	REQUIRE(encoded.release_arguments.size() == 1);
	Ref<InputEvent> release_event;
	REQUIRE(decode_input_event(encoded.release_arguments[0], release_event) == OK);
	Ref<InputEventKey> release_key = release_event;
	REQUIRE(release_key.is_valid());
	CHECK_FALSE(release_key->is_pressed());
}

TEST_CASE("[MCP][Provider] Runtime key ownership ignores press and release unicode differences") {
	Dictionary press;
	press["type"] = "key";
	press["physicalKeycode"] = int64_t(Key::W);
	press["unicode"] = int64_t('w');
	press["pressed"] = true;
	Dictionary release = press.duplicate();
	release["unicode"] = 0;
	release["pressed"] = false;

	MCPRuntimeInput::EncodedEvent encoded_press;
	MCPRuntimeInput::EncodedEvent encoded_release;
	String error;
	REQUIRE(MCPRuntimeInput::encode(press, encoded_press, &error, true) == OK);
	REQUIRE(MCPRuntimeInput::encode(release, encoded_release, &error, true) == OK);
	Dictionary press_payload;
	press_payload["kind"] = "event";
	press_payload["message"] = encoded_press.message;
	press_payload["arguments"] = encoded_press.arguments;
	Dictionary release_payload;
	release_payload["kind"] = "event";
	release_payload["message"] = encoded_release.message;
	release_payload["arguments"] = encoded_release.arguments;

	MCPRuntimeInputEvent runtime_press;
	MCPRuntimeInputEvent runtime_release;
	REQUIRE(MCPRuntimeInputEvent::decode(press_payload, runtime_press, error) == OK);
	REQUIRE(MCPRuntimeInputEvent::decode(release_payload, runtime_release, error) == OK);
	CHECK(runtime_press.state_key == runtime_release.state_key);
	CHECK(runtime_press.active);
	CHECK_FALSE(runtime_release.active);
}

TEST_CASE("[MCP][Provider] Runtime input sequences compile waits, taps, and holds") {
	Dictionary move;
	move["type"] = "key";
	move["keycode"] = int64_t(Key::D);
	Dictionary tap;
	tap["tap"] = move;
	tap["durationFrames"] = 2;

	Dictionary wait;
	wait["waitMs"] = 75;

	Dictionary key;
	key["type"] = "key";
	key["keycode"] = int64_t(Key::SHIFT);
	Dictionary hold;
	hold["hold"] = key;
	hold["durationMs"] = 250;

	Vector<MCPRuntimeInputSequence::Step> steps;
	String error;
	REQUIRE(MCPRuntimeInputSequence::compile(Array{ tap, wait, hold }, steps, &error) == OK);
	REQUIRE(steps.size() == 7);
	CHECK(steps[0].type == MCPRuntimeInputSequence::STEP_EVENT);
	CHECK(steps[0].event.active);
	CHECK(steps[1].type == MCPRuntimeInputSequence::STEP_WAIT_FRAMES);
	CHECK(steps[1].amount == 2);
	CHECK_FALSE(steps[2].event.active);
	CHECK(steps[3].type == MCPRuntimeInputSequence::STEP_WAIT_MSEC);
	CHECK(steps[3].amount == 75);
	CHECK(steps[5].amount == 250);
	CHECK_FALSE(steps[6].event.active);
}

TEST_CASE("[MCP][Provider] Runtime input sequences reject ambiguous and unbounded steps") {
	Vector<MCPRuntimeInputSequence::Step> steps;
	String error;
	Dictionary ambiguous;
	ambiguous["waitMs"] = 10;
	ambiguous["waitFrames"] = 1;
	CHECK(MCPRuntimeInputSequence::compile(Array{ ambiguous }, steps, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("exactly one"));

	Dictionary excessive;
	excessive["waitMs"] = int64_t(MCPRuntimeInputSequence::MAX_TOTAL_WAIT_MSEC);
	Dictionary extra;
	extra["waitMs"] = 1;
	CHECK(MCPRuntimeInputSequence::compile(Array{ excessive, extra }, steps, &error) != OK);
	CHECK(error.contains("exceeds"));
}

TEST_CASE("[MCP][Provider] Runtime input ownership requires and isolates MCP sessions") {
	MCPRuntimeDebugService service(nullptr);
	MCPRuntimeProvider provider(&service);
	MCPToolRegistry registry;
	REQUIRE(provider.register_tools(&registry) == OK);

	Dictionary query;
	query["sequenceId"] = "input-1";
	CHECK(_error_code(_call(registry, "godot.runtime.input.sequence_status", query)) == "MCP_SESSION_REQUIRED");

	Dictionary session;
	session["sessionId"] = "client-a";
	CHECK(_error_code(_call(registry, "godot.runtime.input.sequence_status", query, session)) == "INPUT_SEQUENCE_NOT_FOUND");
}

} // namespace TestMCPRuntimeProvider
