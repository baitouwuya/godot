/**************************************************************************/
/*  test_mcp_runtime_input_controller.cpp                                 */
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

#include "core/input/input.h"
#include "core/input/input_map.h"
#include "scene/debugger/mcp_runtime_input_controller.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_runtime_input_controller);

struct MCPRuntimeInputControllerTestAccess {
	static bool start(MCPRuntimeInputController &p_controller, const Array &p_arguments) {
		return p_controller._handle_sequence_start(p_arguments);
	}

	static bool cancel(MCPRuntimeInputController &p_controller, const Array &p_arguments) {
		return p_controller._handle_sequence_cancel(p_arguments);
	}

	static void dispatch_owned(MCPRuntimeInputController &p_controller, const String &p_owner_id, const String &p_session_id, const MCPRuntimeInputEvent &p_event) {
		p_controller._dispatch_owned(p_owner_id, p_session_id, p_event);
	}

	static int release_owner(MCPRuntimeInputController &p_controller, const String &p_session_id, const String &p_owner_id) {
		return p_controller._release_owner(p_session_id, p_owner_id);
	}

	static int held_owner_count(const MCPRuntimeInputController &p_controller, const String &p_state_key) {
		const MCPRuntimeInputController::HeldState *state = p_controller.held_states.getptr(p_state_key);
		return state ? state->owners.size() : 0;
	}

	static int sequence_count(const MCPRuntimeInputController &p_controller) {
		return p_controller.sequences.size();
	}

	static int sequence_steps(const MCPRuntimeInputController &p_controller, const String &p_sequence_id) {
		const MCPRuntimeInputController::Sequence *sequence = p_controller.sequences.getptr(p_sequence_id);
		return sequence ? sequence->steps.size() : -1;
	}

	static int sequence_step_count(const MCPRuntimeInputController &p_controller, const String &p_sequence_id) {
		const MCPRuntimeInputController::Sequence *sequence = p_controller.sequences.getptr(p_sequence_id);
		return sequence ? sequence->step_count : -1;
	}

	static String sequence_state(const MCPRuntimeInputController &p_controller, const String &p_sequence_id) {
		const MCPRuntimeInputController::Sequence *sequence = p_controller.sequences.getptr(p_sequence_id);
		return sequence ? sequence->state : String();
	}

	static uint64_t queued_bytes(const MCPRuntimeInputController &p_controller) {
		return p_controller.queued_bytes;
	}

	static uint64_t heartbeat_timeout_usec() {
		return MCPRuntimeInputController::HEARTBEAT_TIMEOUT_USEC;
	}

	static uint64_t heartbeat(const MCPRuntimeInputController &p_controller) {
		return p_controller.last_heartbeat_usec;
	}

	static void set_heartbeat(MCPRuntimeInputController &p_controller, uint64_t p_heartbeat_usec) {
		p_controller.last_heartbeat_usec = p_heartbeat_usec;
	}

	static bool parse(MCPRuntimeInputController &p_controller, const String &p_message, const Array &p_arguments) {
		bool captured = false;
		MCPRuntimeInputController::_parse_message(&p_controller, p_message, p_arguments, captured);
		return captured;
	}

	static void process_at(MCPRuntimeInputController &p_controller, uint64_t p_now_usec) {
		p_controller._process_frame_at(p_now_usec);
	}
};

namespace TestMCPRuntimeInputController {

static Dictionary _action_event(const StringName &p_action, bool p_pressed) {
	Dictionary event;
	event["kind"] = "event";
	event["message"] = "scene:inject_input_action";
	event["arguments"] = Array{ p_action, p_pressed, p_pressed ? 1.0 : 0.0 };
	return event;
}

static Array _sequence_arguments(const String &p_sequence_id, const String &p_owner_id, const String &p_session_id, const Dictionary &p_event) {
	return Array{ String(), p_sequence_id, p_owner_id, p_session_id, Array{ p_event } };
}

TEST_CASE("[MCP][Runtime Input][SceneTree] Sequence bytes are bounded globally and released at terminal state") {
	const StringName action = "mcp_test_queue_" + String("x").repeat(220);
	const bool action_existed = InputMap::get_singleton()->has_action(action);
	if (!action_existed) {
		InputMap::get_singleton()->add_action(action);
	}
	{
		MCPRuntimeInputController controller;
		const Dictionary event = _action_event(action, true);
		MCPRuntimeInputEvent decoded;
		String error;
		REQUIRE(MCPRuntimeInputEvent::decode(event, decoded, error) == OK);
		Array steps;
		for (int i = 0; i < 768; i++) {
			steps.push_back(event);
		}
		const uint64_t sequence_bytes = decoded.encoded_bytes * steps.size();
		for (int i = 0; i < 5; i++) {
			CHECK(MCPRuntimeInputControllerTestAccess::start(controller, Array{ String(), "accepted-" + itos(i), "owner-" + itos(i), "session", steps }));
		}
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_count(controller) == 5);
		CHECK(MCPRuntimeInputControllerTestAccess::queued_bytes(controller) == sequence_bytes * 5);

		CHECK(MCPRuntimeInputControllerTestAccess::start(controller, Array{ String(), "accepted-5", "owner-5", "session", steps }));
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_count(controller) == 5);
		CHECK(MCPRuntimeInputControllerTestAccess::queued_bytes(controller) == sequence_bytes * 5);

		CHECK(MCPRuntimeInputControllerTestAccess::cancel(controller, Array{ String(), "accepted-0", "session" }));
		CHECK(MCPRuntimeInputControllerTestAccess::queued_bytes(controller) == sequence_bytes * 4);
		CHECK(MCPRuntimeInputControllerTestAccess::start(controller, Array{ String(), "accepted-5", "owner-5", "session", steps }));
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_count(controller) == 6);
		CHECK(MCPRuntimeInputControllerTestAccess::queued_bytes(controller) == sequence_bytes * 5);
		for (int i = 1; i < 6; i++) {
			CHECK(MCPRuntimeInputControllerTestAccess::cancel(controller, Array{ String(), "accepted-" + itos(i), "session" }));
		}
		CHECK(MCPRuntimeInputControllerTestAccess::queued_bytes(controller) == 0);
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_steps(controller, "accepted-0") == 0);
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_step_count(controller, "accepted-0") == 768);
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_state(controller, "accepted-0") == "cancelled");

		CHECK(MCPRuntimeInputControllerTestAccess::start(controller, _sequence_arguments("completed", "completion-owner", "session", event)));
		MCPRuntimeInputControllerTestAccess::process_at(controller, 1000);
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_state(controller, "completed") == "completed");
		CHECK(MCPRuntimeInputControllerTestAccess::queued_bytes(controller) == 0);
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_steps(controller, "completed") == 0);
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_step_count(controller, "completed") == 1);
		CHECK_FALSE(Input::get_singleton()->is_action_pressed(action));
	}
	if (!action_existed) {
		InputMap::get_singleton()->erase_action(action);
	}
}

TEST_CASE("[MCP][Runtime Input][SceneTree] Held input owners are isolated by MCP session") {
	const StringName action = "mcp_test_owner_action";
	const bool action_existed = InputMap::get_singleton()->has_action(action);
	if (!action_existed) {
		InputMap::get_singleton()->add_action(action);
	}
	{
		MCPRuntimeInputController controller;
		MCPRuntimeInputEvent press;
		MCPRuntimeInputEvent release;
		String error;
		REQUIRE(MCPRuntimeInputEvent::decode(_action_event(action, true), press, error) == OK);
		REQUIRE(MCPRuntimeInputEvent::decode(_action_event(action, false), release, error) == OK);

		Input::get_singleton()->action_press(action);
		CHECK(Input::get_singleton()->is_action_pressed(action));
		MCPRuntimeInputControllerTestAccess::dispatch_owned(controller, "unowned", "session-a", release);
		CHECK(Input::get_singleton()->is_action_pressed(action));
		Input::get_singleton()->action_release(action);

		MCPRuntimeInputControllerTestAccess::dispatch_owned(controller, "shared-owner", "session-a", press);
		MCPRuntimeInputControllerTestAccess::dispatch_owned(controller, "shared-owner", "session-b", press);
		CHECK(MCPRuntimeInputControllerTestAccess::held_owner_count(controller, press.state_key) == 2);
		CHECK(MCPRuntimeInputControllerTestAccess::release_owner(controller, "session-a", "shared-owner") == 0);
		CHECK(MCPRuntimeInputControllerTestAccess::held_owner_count(controller, press.state_key) == 1);
		CHECK(MCPRuntimeInputControllerTestAccess::release_owner(controller, "session-b", "shared-owner") == 1);
		CHECK(MCPRuntimeInputControllerTestAccess::held_owner_count(controller, press.state_key) == 0);

		const Dictionary event = _action_event(action, true);
		CHECK(MCPRuntimeInputControllerTestAccess::start(controller, _sequence_arguments("sequence-a", "shared-owner", "session-a", event)));
		CHECK(MCPRuntimeInputControllerTestAccess::start(controller, _sequence_arguments("sequence-duplicate", "shared-owner", "session-a", event)));
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_count(controller) == 1);
		CHECK(MCPRuntimeInputControllerTestAccess::start(controller, _sequence_arguments("sequence-b", "shared-owner", "session-b", event)));
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_count(controller) == 2);
		CHECK(MCPRuntimeInputControllerTestAccess::cancel(controller, Array{ String(), "sequence-a", "session-a" }));
		CHECK(MCPRuntimeInputControllerTestAccess::cancel(controller, Array{ String(), "sequence-b", "session-b" }));
		CHECK(MCPRuntimeInputControllerTestAccess::queued_bytes(controller) == 0);
	}
	if (!action_existed) {
		InputMap::get_singleton()->erase_action(action);
	}
}

TEST_CASE("[MCP][Runtime Input][SceneTree] Only validated messages refresh the watchdog") {
	MCPRuntimeInputController controller;
	MCPRuntimeInputControllerTestAccess::set_heartbeat(controller, 100);
	CHECK_FALSE(MCPRuntimeInputControllerTestAccess::parse(controller, "unknown", Array{}));
	CHECK(MCPRuntimeInputControllerTestAccess::heartbeat(controller) == 100);
	CHECK(MCPRuntimeInputControllerTestAccess::parse(controller, "heartbeat", Array{ 1 }));
	CHECK(MCPRuntimeInputControllerTestAccess::heartbeat(controller) == 100);
	CHECK(MCPRuntimeInputControllerTestAccess::parse(controller, "heartbeat", Array{ String() }));
	CHECK(MCPRuntimeInputControllerTestAccess::heartbeat(controller) > 100);
}

TEST_CASE("[MCP][Runtime Input][SceneTree] Watchdog cancellation releases queued sequence payloads") {
	const StringName action = "mcp_test_watchdog_action";
	const bool action_existed = InputMap::get_singleton()->has_action(action);
	if (!action_existed) {
		InputMap::get_singleton()->add_action(action);
	}
	{
		MCPRuntimeInputController controller;
		const Dictionary event = _action_event(action, true);
		CHECK(MCPRuntimeInputControllerTestAccess::start(controller, _sequence_arguments("watchdog", "owner", "session", event)));
		CHECK(MCPRuntimeInputControllerTestAccess::queued_bytes(controller) > 0);

		MCPRuntimeInputControllerTestAccess::set_heartbeat(controller, 100);
		MCPRuntimeInputControllerTestAccess::process_at(controller, 100 + MCPRuntimeInputControllerTestAccess::heartbeat_timeout_usec() + 1);
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_state(controller, "watchdog") == "cancelled");
		CHECK(MCPRuntimeInputControllerTestAccess::queued_bytes(controller) == 0);
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_steps(controller, "watchdog") == 0);
		CHECK(MCPRuntimeInputControllerTestAccess::sequence_step_count(controller, "watchdog") == 1);
	}
	if (!action_existed) {
		InputMap::get_singleton()->erase_action(action);
	}
}

} // namespace TestMCPRuntimeInputController
