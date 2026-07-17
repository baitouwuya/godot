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
#include "editor/mcp/providers/mcp_runtime_observation_debugger_plugin.h"
#include "editor/mcp/providers/mcp_runtime_observation_response_store.h"
#include "editor/mcp/providers/mcp_runtime_provider.h"
#include "editor/mcp/providers/mcp_runtime_target_action.h"
#include "scene/debugger/mcp_runtime_input_event.h"
#include "scene/debugger/mcp_runtime_observation.h"
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
	REQUIRE(names.size() == 32);
	CHECK(names[0] == "godot.runtime.get_state");
	CHECK(names[1] == "godot.runtime.play");
	CHECK(names[2] == "godot.runtime.stop");
	CHECK(names[3] == "godot.runtime.pause");
	CHECK(names[4] == "godot.runtime.resume");
	CHECK(names[5] == "godot.runtime.next_frame");
	CHECK(names[6] == "godot.runtime.get_tree");
	CHECK(names[7] == "godot.runtime.get_screenshot");
	CHECK(names[8] == "godot.runtime.get_viewport_summary");
	CHECK(names[9] == "godot.runtime.query_nodes");
	CHECK(names[10] == "godot.runtime.get_interactables");
	CHECK(names[11] == "godot.runtime.get_node_snapshot");
	CHECK(names[12] == "godot.runtime.click_target");
	CHECK(names[13] == "godot.runtime.double_click_target");
	CHECK(names[14] == "godot.runtime.hover_target");
	CHECK(names[15] == "godot.runtime.focus_target");
	CHECK(names[16] == "godot.runtime.drag_target_to_target");
	CHECK(names[17] == "godot.runtime.type_text");
	CHECK(names[18] == "godot.runtime.scroll_view");
	CHECK(names[19] == "godot.runtime.wait.start");
	CHECK(names[20] == "godot.runtime.wait.status");
	CHECK(names[21] == "godot.runtime.wait.cancel");
	CHECK(names[22] == "godot.runtime.performance.start");
	CHECK(names[23] == "godot.runtime.performance.status");
	CHECK(names[24] == "godot.runtime.performance.stop");
	CHECK(names[25] == "godot.runtime.node.get_properties");
	CHECK(names[26] == "godot.runtime.node.set_property");
	CHECK(names[27] == "godot.runtime.input.send");
	CHECK(names[28] == "godot.runtime.input.sequence");
	CHECK(names[29] == "godot.runtime.input.sequence_status");
	CHECK(names[30] == "godot.runtime.input.sequence_cancel");
	CHECK(names[31] == "godot.runtime.input.release_all");
	CHECK(registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI).is_empty());

	const Array definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(definitions.size() == 32);
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

TEST_CASE("[MCP][Provider] Runtime observation selectors are strict and match compact snapshots") {
	MCPRuntimeObservation::Selector selector;
	String error;
	Dictionary selector_value;
	selector_value["name"] = "PlayButton";
	selector_value["group"] = "menu";
	selector_value["visible"] = true;
	selector_value["nearestToScreenPoint"] = Array{ 40.0, 30.0 };
	REQUIRE(MCPRuntimeObservation::parse_selector(selector_value, selector, error) == OK);
	CHECK_FALSE(selector.is_empty());
	CHECK(selector.name == "PlayButton");

	Dictionary snapshot;
	snapshot["nodePath"] = "/root/Main/PlayButton";
	snapshot["name"] = "PlayButton";
	snapshot["type"] = "Button";
	snapshot["groups"] = Array{ "menu", "primary" };
	snapshot["visible"] = true;
	snapshot["screenRect"] = Dictionary{ { "x", 10.0 }, { "y", 20.0 }, { "width", 60.0 }, { "height", 20.0 } };
	CHECK(MCPRuntimeObservation::matches_snapshot(snapshot, selector));
	snapshot["visible"] = false;
	CHECK_FALSE(MCPRuntimeObservation::matches_snapshot(snapshot, selector));

	Dictionary invalid;
	invalid["containsText"] = "Play";
	CHECK(MCPRuntimeObservation::parse_selector(invalid, selector, error) == ERR_INVALID_PARAMETER);
	CHECK(error.contains("Unsupported"));
}

TEST_CASE("[MCP][Provider] Runtime observation responses are correlated by debugger session and operation") {
	MCPRuntimeObservationResponseStore store;
	REQUIRE(store.register_request(1, "request-1", "query_nodes"));
	CHECK_FALSE(store.register_request(1, "request-1", "query_nodes"));

	Dictionary data;
	data["count"] = 1;
	CHECK_FALSE(store.handle_response(1, Array{ "request-1", "get_interactables", true, String(), String(), data }));
	MCPRuntimeObservationResponseStore::Response response;
	CHECK_FALSE(store.take_response(1, "request-1", response));
	CHECK_FALSE(store.handle_response(0, Array{ "request-1", "query_nodes", true, String(), String(), data }));
	CHECK_FALSE(store.take_response(1, "request-1", response));
	CHECK(store.handle_response(1, Array{ "request-1", "query_nodes", true, String(), String(), data }));
	REQUIRE(store.take_response(1, "request-1", response));
	CHECK(response.ok);
	CHECK(response.operation == "query_nodes");
	CHECK(int(response.data["count"]) == 1);

}

TEST_CASE("[MCP][Provider] Runtime target actions compose bounded existing input sequences") {
	Array steps;
	String error;
	REQUIRE(MCPRuntimeTargetAction::build_click(Vector2(30, 40), MouseButton::LEFT, 1, true, 2, steps, &error) == OK);
	CHECK(steps.size() == 8);
	Vector<MCPRuntimeInputSequence::Step> compiled;
	REQUIRE(MCPRuntimeInputSequence::compile(steps, compiled, &error) == OK);
	CHECK(compiled.size() == 8);

	REQUIRE(MCPRuntimeTargetAction::build_drag(Vector2(10, 20), Vector2(70, 80), MouseButton::LEFT,
					MCPRuntimeTargetAction::MAX_DRAG_FRAMES, steps, &error) == OK);
	CHECK(steps.size() == 2 * MCPRuntimeTargetAction::MAX_DRAG_FRAMES + 3);
	REQUIRE(MCPRuntimeInputSequence::compile(steps, compiled, &error) == OK);
	CHECK(compiled.size() == steps.size());

	const String bounded_text = String("x").repeat(MCPRuntimeTargetAction::MAX_TEXT_LENGTH);
	REQUIRE(MCPRuntimeTargetAction::build_text(bounded_text, steps, &error) == OK);
	CHECK(steps.size() == MCPRuntimeTargetAction::MAX_TEXT_LENGTH * 2);
	REQUIRE(MCPRuntimeInputSequence::compile(steps, compiled, &error) == OK);
	CHECK(MCPRuntimeTargetAction::build_text(bounded_text + "x", steps, &error) == ERR_INVALID_PARAMETER);

	Array events;
	REQUIRE(MCPRuntimeTargetAction::build_scroll(Vector2(5, 6), "down",
					MCPRuntimeTargetAction::MAX_SCROLL_STEPS, events, &error) == OK);
	CHECK(events.size() == MCPRuntimeTargetAction::MAX_SCROLL_STEPS * 2 + 1);
	CHECK(MCPRuntimeTargetAction::build_scroll(Vector2(), "sideways", 1, events, &error) == ERR_INVALID_PARAMETER);
}

TEST_CASE("[MCP][Provider] Runtime target actions require MCP ownership and expose no coordinate bypass") {
	MCPRuntimeDebugService service(nullptr);
	MCPRuntimeProvider provider(&service);
	MCPToolRegistry registry;
	REQUIRE(provider.register_tools(&registry) == OK);
	Dictionary arguments;
	arguments["selector"] = Dictionary{ { "name", "PlayButton" } };
	arguments["runtimeGeneration"] = 1;
	CHECK(_error_code(_call(registry, "godot.runtime.click_target", arguments)) == "MCP_SESSION_REQUIRED");

	const Array definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	for (const Variant &definition_value : definitions) {
		const Dictionary definition = definition_value;
		if (!String(definition.get("name", String())).ends_with("click_target")) {
			continue;
		}
		const Dictionary schema = definition.get("inputSchema", Dictionary());
		const Dictionary properties = schema.get("properties", Dictionary());
		CHECK_FALSE(properties.has("x"));
		CHECK_FALSE(properties.has("y"));
		CHECK(properties.has("selector"));
	}
}

TEST_CASE("[MCP][Provider] Runtime wait jobs require MCP ownership and generation identity") {
	MCPRuntimeDebugService service(nullptr);
	MCPRuntimeProvider provider(&service);
	MCPToolRegistry registry;
	REQUIRE(provider.register_tools(&registry) == OK);
	Dictionary condition;
	condition["kind"] = "node_exists";
	condition["selector"] = Dictionary{ { "name", "Target" } };
	Dictionary start;
	start["condition"] = condition;
	start["runtimeGeneration"] = 1;
	CHECK(_error_code(_call(registry, "godot.runtime.wait.start", start)) == "MCP_SESSION_REQUIRED");

	Dictionary session;
	session["sessionId"] = "wait-client";
	Dictionary status;
	status["jobId"] = "wait-missing";
	status["runtimeGeneration"] = 1;
	CHECK(_error_code(_call(registry, "godot.runtime.wait.status", status, session)) == "WAIT_JOB_NOT_FOUND");
}

TEST_CASE("[MCP][Provider] Runtime performance jobs require MCP ownership and expose bounded summaries") {
	MCPRuntimeDebugService service(nullptr);
	MCPRuntimeProvider provider(&service);
	MCPToolRegistry registry;
	REQUIRE(provider.register_tools(&registry) == OK);
	Dictionary start;
	start["runtimeGeneration"] = 1;
	start["topFrames"] = 100;
	start["maxFrames"] = 36000;
	CHECK(_error_code(_call(registry, "godot.runtime.performance.start", start)) == "MCP_SESSION_REQUIRED");

	Dictionary session;
	session["sessionId"] = "performance-client";
	Dictionary status;
	status["jobId"] = "performance-missing";
	status["runtimeGeneration"] = 1;
	CHECK(_error_code(_call(registry, "godot.runtime.performance.status", status, session)) == "PERFORMANCE_JOB_NOT_FOUND");
	CHECK(_error_code(_call(registry, "godot.runtime.performance.stop", status, session)) == "PERFORMANCE_JOB_NOT_FOUND");
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
