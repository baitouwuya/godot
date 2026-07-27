/**************************************************************************/
/*  mcp_runtime_provider.cpp                                              */
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

#include "mcp_runtime_provider.h"

#include "mcp_runtime_debug_service.h"
#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

void _add_session_properties(Dictionary &r_properties, bool p_generation, bool p_timeout) {
	Dictionary debugger = MCPToolUtils::make_property_schema("integer", "Optional debugger session index. Required when multiple project instances are running.");
	debugger["minimum"] = 0;
	r_properties["debuggerSession"] = debugger;
	if (p_generation) {
		Dictionary generation = MCPToolUtils::make_property_schema("integer", "Runtime generation returned by godot.runtime.get_state or get_tree.");
		generation["minimum"] = 1;
		r_properties["runtimeGeneration"] = generation;
	}
	if (p_timeout) {
		Dictionary timeout = MCPToolUtils::make_property_schema("integer", "Maximum time to wait for a remote debugger response.");
		timeout["minimum"] = 50;
		timeout["maximum"] = 5000;
		timeout["default"] = 750;
		r_properties["timeoutMs"] = timeout;
	}
}

void _add_observation_session_properties(Dictionary &r_properties) {
	Dictionary debugger = MCPToolUtils::make_property_schema("integer", "Optional debugger session index. Required when multiple project instances are running.");
	debugger["minimum"] = 0;
	r_properties["debuggerSession"] = debugger;
	Dictionary timeout = MCPToolUtils::make_property_schema("integer", "Maximum time for this short remote debugger round trip.");
	timeout["minimum"] = 50;
	timeout["maximum"] = 1500;
	timeout["default"] = 500;
	r_properties["timeoutMs"] = timeout;
}

Dictionary _session_schema(bool p_require_generation, bool p_timeout) {
	Dictionary properties;
	_add_session_properties(properties, p_require_generation, p_timeout);
	PackedStringArray required;
	if (p_require_generation) {
		required.push_back("runtimeGeneration");
	}
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary _play_schema() {
	Dictionary properties;
	Dictionary mode = MCPToolUtils::make_property_schema("string", "Run the project main scene, current edited scene, or a specific scene path.");
	mode["enum"] = PackedStringArray{ "main", "current", "scene" };
	mode["default"] = "main";
	properties["mode"] = mode;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Project scene path required when mode is scene.");
	Dictionary restart = MCPToolUtils::make_property_schema("boolean", "Stop the current run before starting the requested scene.");
	restart["default"] = false;
	properties["restart"] = restart;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary _tree_schema() {
	Dictionary properties;
	_add_session_properties(properties, false, true);
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary _rect_schema(const String &p_description) {
	Dictionary properties;
	properties["x"] = MCPToolUtils::make_property_schema("number", "Left coordinate in root viewport pixels.");
	properties["y"] = MCPToolUtils::make_property_schema("number", "Top coordinate in root viewport pixels.");
	properties["width"] = MCPToolUtils::make_property_schema("number", "Rectangle width in pixels.");
	properties["height"] = MCPToolUtils::make_property_schema("number", "Rectangle height in pixels.");
	Dictionary schema = MCPToolUtils::make_object_schema(properties, PackedStringArray{ "x", "y", "width", "height" });
	schema["description"] = p_description;
	return schema;
}

Dictionary _selector_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Exact absolute runtime node path.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Exact node name.");
	properties["type"] = MCPToolUtils::make_property_schema("string", "Exact runtime class name.");
	properties["group"] = MCPToolUtils::make_property_schema("string", "Exact public group name.");
	properties["text"] = MCPToolUtils::make_property_schema("string", "Exact text property value.");
	properties["is3D"] = MCPToolUtils::make_property_schema("boolean", "Match 3D or non-3D nodes.");
	properties["visible"] = MCPToolUtils::make_property_schema("boolean", "Match effective runtime visibility.");
	properties["screenRect"] = _rect_schema("Match nodes whose projected screen rectangle intersects this rectangle.");
	Dictionary nearest = MCPToolUtils::make_property_schema("array", "Select the matching node nearest to this root viewport [x, y] point.");
	nearest["items"] = MCPToolUtils::make_property_schema("number", "Root viewport pixel coordinate.");
	nearest["minItems"] = 2;
	nearest["maxItems"] = 2;
	properties["nearestToScreenPoint"] = nearest;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary _runtime_query_schema() {
	Dictionary properties;
	_add_observation_session_properties(properties);
	properties["filter"] = _selector_schema();
	Dictionary max_results = MCPToolUtils::make_property_schema("integer", "Maximum number of matching snapshots to return.");
	max_results["minimum"] = 1;
	max_results["maximum"] = 256;
	max_results["default"] = 64;
	properties["maxResults"] = max_results;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary _runtime_snapshot_schema() {
	Dictionary properties;
	_add_observation_session_properties(properties);
	properties["selector"] = _selector_schema();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "selector" });
}

Dictionary _viewport_summary_schema() {
	Dictionary properties;
	_add_observation_session_properties(properties);
	Dictionary include_counts = MCPToolUtils::make_property_schema("boolean", "Include UI, 3D, and interactable target counts.");
	include_counts["default"] = true;
	properties["includeCounts"] = include_counts;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary _screenshot_schema() {
	Dictionary properties;
	_add_observation_session_properties(properties);
	properties["name"] = MCPToolUtils::make_property_schema("string", "Optional label included in the generated temporary PNG filename.");
	properties["crop"] = _rect_schema("Optional root viewport crop rectangle.");
	return MCPToolUtils::make_object_schema(properties);
}

void _add_target_action_session_properties(Dictionary &r_properties) {
	_add_session_properties(r_properties, true, false);
	Dictionary timeout = MCPToolUtils::make_property_schema("integer", "Maximum time to resolve selectors in the running project.");
	timeout["minimum"] = 50;
	timeout["maximum"] = 1500;
	timeout["default"] = 500;
	r_properties["timeoutMs"] = timeout;
}

Dictionary _target_button_schema() {
	Dictionary button = MCPToolUtils::make_property_schema("string", "Mouse button used by the target action.");
	button["enum"] = PackedStringArray{ "left", "right", "middle" };
	button["default"] = "left";
	return button;
}

Dictionary _click_target_schema(bool p_double_click) {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["selector"] = _selector_schema();
	properties["button"] = _target_button_schema();
	Dictionary press_frames = MCPToolUtils::make_property_schema("integer", "Rendered frames between mouse press and release.");
	press_frames["minimum"] = 1;
	press_frames["maximum"] = 10;
	press_frames["default"] = 1;
	properties["pressFrames"] = press_frames;
	if (p_double_click) {
		Dictionary gap_frames = MCPToolUtils::make_property_schema("integer", "Rendered frames between the two clicks.");
		gap_frames["minimum"] = 1;
		gap_frames["maximum"] = 10;
		gap_frames["default"] = 2;
		properties["gapFrames"] = gap_frames;
	}
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "selector", "runtimeGeneration" });
}

Dictionary _single_target_schema() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["selector"] = _selector_schema();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "selector", "runtimeGeneration" });
}

Dictionary _drag_target_schema() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["fromSelector"] = _selector_schema();
	properties["toSelector"] = _selector_schema();
	properties["button"] = _target_button_schema();
	Dictionary frames = MCPToolUtils::make_property_schema("integer", "Rendered frames used to interpolate the pointer path.");
	frames["minimum"] = 1;
	frames["maximum"] = 60;
	frames["default"] = 20;
	properties["frames"] = frames;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "fromSelector", "toSelector", "runtimeGeneration" });
}

Dictionary _type_text_schema() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["selector"] = _selector_schema();
	Dictionary text = MCPToolUtils::make_property_schema("string", "Unicode text injected after the target Control accepts focus.");
	text["minLength"] = 1;
	text["maxLength"] = 100;
	properties["text"] = text;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "selector", "text", "runtimeGeneration" });
}

Dictionary _scroll_view_schema() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["selector"] = _selector_schema();
	Dictionary direction = MCPToolUtils::make_property_schema("string", "Vertical wheel direction.");
	direction["enum"] = PackedStringArray{ "up", "down" };
	direction["default"] = "down";
	properties["direction"] = direction;
	Dictionary steps = MCPToolUtils::make_property_schema("integer", "Number of bounded wheel steps.");
	steps["minimum"] = 1;
	steps["maximum"] = 20;
	steps["default"] = 1;
	properties["steps"] = steps;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "selector", "runtimeGeneration" });
}

Dictionary _wait_condition_schema() {
	Dictionary properties;
	Dictionary kind = MCPToolUtils::make_property_schema("string", "Asynchronous runtime condition kind.");
	kind["enum"] = PackedStringArray{ "node_exists", "node_gone", "property", "scene_changed", "screenshot_diff" };
	properties["kind"] = kind;
	properties["selector"] = _selector_schema();
	Dictionary property = MCPToolUtils::make_property_schema("string", "Exact property name used by property conditions.");
	property["minLength"] = 1;
	property["maxLength"] = 128;
	properties["property"] = property;
	Dictionary equals;
	equals["description"] = "JSON-native expected value used by property conditions.";
	properties["equals"] = equals;
	Dictionary threshold = MCPToolUtils::make_property_schema("number", "Normalized screenshot difference threshold.");
	threshold["minimum"] = 0.001;
	threshold["maximum"] = 1.0;
	threshold["default"] = 0.02;
	properties["threshold"] = threshold;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "kind" });
}

Dictionary _wait_start_schema() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["condition"] = _wait_condition_schema();
	Dictionary timeout_frames = MCPToolUtils::make_property_schema("integer", "Maximum rendered frames before the job times out.");
	timeout_frames["minimum"] = 1;
	timeout_frames["maximum"] = 36000;
	timeout_frames["default"] = 300;
	properties["timeoutFrames"] = timeout_frames;
	Dictionary poll_frames = MCPToolUtils::make_property_schema("integer", "Rendered frames between condition evaluations.");
	poll_frames["minimum"] = 1;
	poll_frames["maximum"] = 600;
	poll_frames["default"] = 1;
	properties["pollEveryFrames"] = poll_frames;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "condition", "runtimeGeneration" });
}

Dictionary _wait_job_schema() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["jobId"] = MCPToolUtils::make_property_schema("string", "Opaque wait job ID returned by godot.runtime.wait.start.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "jobId", "runtimeGeneration" });
}

Dictionary _performance_start_schema() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	Dictionary name = MCPToolUtils::make_property_schema("string", "Optional label included in the structured performance summary.");
	name["maxLength"] = 256;
	properties["name"] = name;
	Dictionary top_frames = MCPToolUtils::make_property_schema("integer", "Number of bounded slow-frame snapshots retained in the summary.");
	top_frames["minimum"] = 0;
	top_frames["maximum"] = 100;
	top_frames["default"] = 10;
	properties["topFrames"] = top_frames;
	Dictionary max_frames = MCPToolUtils::make_property_schema("integer", "Optional frame count that completes the recording automatically.");
	max_frames["minimum"] = 1;
	max_frames["maximum"] = 36000;
	properties["maxFrames"] = max_frames;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "runtimeGeneration" });
}

Dictionary _performance_job_schema() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["jobId"] = MCPToolUtils::make_property_schema("string", "Opaque performance job ID returned by godot.runtime.performance.start.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "jobId", "runtimeGeneration" });
}

Dictionary _properties_schema() {
	Dictionary properties;
	_add_session_properties(properties, false, true);
	properties["path"] = MCPToolUtils::make_property_schema("string", "Absolute runtime node path returned by godot.runtime.get_tree.");
	PackedStringArray required;
	required.push_back("path");
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary _set_property_schema() {
	Dictionary properties;
	_add_session_properties(properties, true, true);
	properties["path"] = MCPToolUtils::make_property_schema("string", "Absolute runtime node path returned by godot.runtime.get_tree.");
	properties["property"] = MCPToolUtils::make_property_schema("string", "Exact runtime property name returned by get_properties.");
	Dictionary value;
	value["description"] = "JSON-native Variant value compatible with the property's type.";
	properties["value"] = value;
	PackedStringArray required;
	required.push_back("path");
	required.push_back("property");
	required.push_back("value");
	required.push_back("runtimeGeneration");
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary _input_schema() {
	Dictionary properties;
	_add_session_properties(properties, true, false);
	Dictionary event;
	event["type"] = "object";
	event["description"] = "A validated action, key, mouse_button, mouse_motion, joypad_button, joypad_motion, pan, or magnify event.";
	Dictionary event_properties;
	event_properties["type"] = MCPToolUtils::make_property_schema("string", "Input event type.");
	event["properties"] = event_properties;
	event["required"] = PackedStringArray{ "type" };
	event["additionalProperties"] = true;
	Dictionary events = MCPToolUtils::make_property_schema("array", "Ordered input events to inject into the running project.");
	events["items"] = event;
	events["minItems"] = 1;
	events["maxItems"] = 128;
	properties["events"] = events;
	PackedStringArray required;
	required.push_back("events");
	required.push_back("runtimeGeneration");
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary _input_sequence_schema() {
	Dictionary properties;
	_add_session_properties(properties, true, false);
	Dictionary step;
	step["type"] = "object";
	step["description"] = "Exactly one event, waitMs, waitFrames, tap, or hold operation. tap and hold accept durationMs or durationFrames.";
	step["additionalProperties"] = true;
	Dictionary steps = MCPToolUtils::make_property_schema("array", "Ordered asynchronous input operations.");
	steps["items"] = step;
	steps["minItems"] = 1;
	steps["maxItems"] = 256;
	properties["steps"] = steps;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "steps", "runtimeGeneration" });
}

Dictionary _input_sequence_status_schema() {
	Dictionary properties;
	properties["sequenceId"] = MCPToolUtils::make_property_schema("string", "Opaque sequence ID returned by godot.runtime.input.sequence.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "sequenceId" });
}

Dictionary _input_sequence_cancel_schema() {
	Dictionary properties;
	_add_session_properties(properties, true, false);
	properties["sequenceId"] = MCPToolUtils::make_property_schema("string", "Opaque sequence ID returned by godot.runtime.input.sequence.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "sequenceId", "runtimeGeneration" });
}

bool _resolve_mcp_session(const Dictionary &p_context, String &r_session_id, Dictionary &r_error) {
	r_session_id = String();
	const Variant session_value = p_context.get("session", Variant());
	if (session_value.get_type() == Variant::DICTIONARY) {
		const Variant id_value = Dictionary(session_value).get("sessionId", Variant());
		if (id_value.get_type() == Variant::STRING) {
			r_session_id = id_value;
		}
	}
	if (!r_session_id.is_empty()) {
		return true;
	}
	r_error = MCPToolUtils::make_error_result("MCP_SESSION_REQUIRED", "A valid MCP session is required for runtime input ownership.");
	return false;
}

void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPRuntimeProvider::MCPRuntimeProvider(MCPRuntimeDebugService *p_runtime_service) {
	runtime_service = p_runtime_service;
}

MCPRuntimeProvider::~MCPRuntimeProvider() {
	unregister_tools();
}

Error MCPRuntimeProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry || !runtime_service) {
		_set_error(r_error, "A tool registry and runtime debug service are required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Runtime tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.runtime.get_state", "Get editor run state and all active runtime debugger sessions.",
				MCPToolUtils::make_object_schema(Dictionary()), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_state) },
		{ "godot.runtime.play", "Run the main, current, or a specific project scene from the editor.",
				_play_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_play) },
		{ "godot.runtime.stop", "Stop the project currently launched by the editor.",
				MCPToolUtils::make_object_schema(Dictionary()), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_stop) },
		{ "godot.runtime.pause", "Suspend a running project's SceneTree without entering a script breakpoint.",
				_session_schema(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_pause) },
		{ "godot.runtime.resume", "Resume a SceneTree suspended through the debugger.",
				_session_schema(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_resume) },
		{ "godot.runtime.next_frame", "Advance one rendered frame while the running SceneTree is suspended.",
				_session_schema(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_next_frame) },
		{ "godot.runtime.debug.break", "Pause a running project at the next safe script-debugger point.",
				_session_schema(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_break) },
		{ "godot.runtime.debug.continue", "Continue a project paused at a script breakpoint.",
				_session_schema(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_continue) },
		{ "godot.runtime.debug.step_into", "Step into one GDScript statement from a paused debugger frame.",
				_session_schema(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_step_into) },
		{ "godot.runtime.debug.step_over", "Step over one GDScript statement from a paused debugger frame.",
				_session_schema(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_step_over) },
		{ "godot.runtime.debug.step_out", "Step out of the current GDScript frame from a paused debugger frame.",
				_session_schema(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_step_out) },
		{ "godot.runtime.get_tree", "Get a fresh running-project scene tree using Godot's remote debugger.",
				_tree_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_tree) },
		{ "godot.runtime.get_screenshot", "Capture the running project's root viewport to a temporary PNG.",
				_screenshot_schema(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPRuntimeProvider::_get_screenshot) },
		{ "godot.runtime.get_viewport_summary", "Get root viewport, active camera, scene, and target-count information.",
				_viewport_summary_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_viewport_summary) },
		{ "godot.runtime.query_nodes", "Query compact runtime node snapshots by path, name, type, group, text, visibility, or screen position.",
				_runtime_query_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_query_nodes) },
		{ "godot.runtime.get_interactables", "List visible runtime UI and ray-pickable 3D targets matching an optional filter.",
				_runtime_query_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_interactables) },
		{ "godot.runtime.get_node_snapshot", "Get one compact runtime node snapshot from an unambiguous selector.",
				_runtime_snapshot_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_node_snapshot) },
		{ "godot.runtime.click_target", "Resolve a runtime selector and enqueue one bounded mouse click at the target.",
				_click_target_schema(false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_click_target) },
		{ "godot.runtime.double_click_target", "Resolve a runtime selector and enqueue one bounded double-click at the target.",
				_click_target_schema(true), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_double_click_target) },
		{ "godot.runtime.hover_target", "Resolve a runtime selector and move the pointer to the target.",
				_single_target_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_hover_target) },
		{ "godot.runtime.focus_target", "Focus a runtime Control, or move the pointer to a non-Control target.",
				_single_target_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_focus_target) },
		{ "godot.runtime.drag_target_to_target", "Atomically resolve two selectors and enqueue a bounded pointer drag between them.",
				_drag_target_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_drag_target_to_target) },
		{ "godot.runtime.type_text", "Focus a runtime Control and enqueue bounded Unicode key input.",
				_type_text_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_type_text) },
		{ "godot.runtime.scroll_view", "Resolve a runtime selector and inject bounded vertical wheel input at the target.",
				_scroll_view_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_scroll_view) },
		{ "godot.runtime.wait.start", "Register a bounded runtime condition job and return immediately.",
				_wait_start_schema(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPRuntimeProvider::_start_wait) },
		{ "godot.runtime.wait.status", "Get the current state of an asynchronous runtime condition job.",
				_wait_job_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_wait_status) },
		{ "godot.runtime.wait.cancel", "Cancel an asynchronous runtime condition job owned by this MCP session.",
				_wait_job_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_cancel_wait) },
		{ "godot.runtime.performance.start", "Start bounded in-process runtime performance aggregation and return immediately.",
				_performance_start_schema(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPRuntimeProvider::_start_performance) },
		{ "godot.runtime.performance.status", "Get the current state and captured-frame count of a runtime performance job.",
				_performance_job_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_performance_status) },
		{ "godot.runtime.performance.stop", "Stop a runtime performance job and return its final structured summary.",
				_performance_job_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_stop_performance) },
		{ "godot.runtime.node.get_properties", "Get all inspectable runtime node properties, including script members and exports.",
				_properties_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_properties) },
		{ "godot.runtime.node.set_property", "Set and verify one runtime node property through Godot's remote inspector protocol.",
				_set_property_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_set_property) },
		{ "godot.runtime.input.send", "Inject validated action, keyboard, mouse, joypad, pan, and magnify input events into a running project.",
				_input_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_send_input) },
		{ "godot.runtime.input.sequence", "Run an asynchronous input sequence with millisecond or frame waits, tap, and hold operations.",
				_input_sequence_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_start_input_sequence) },
		{ "godot.runtime.input.sequence_status", "Read the current state of an input sequence owned by this MCP session.",
				_input_sequence_status_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_input_sequence) },
		{ "godot.runtime.input.sequence_cancel", "Cancel an input sequence and release inputs held by it.",
				_input_sequence_cancel_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_cancel_input_sequence) },
		{ "godot.runtime.input.release_all", "Cancel this MCP session's sequences and release all of its held runtime inputs.",
				_session_schema(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_release_input) },
	};

	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPRuntimeProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

void MCPRuntimeProvider::process() {
	runtime_service->process_input();
}

void MCPRuntimeProvider::on_session_removed(const String &p_session_id) {
	runtime_service->release_mcp_session(p_session_id);
}

void MCPRuntimeProvider::shutdown() {
	runtime_service->shutdown_input();
}

Dictionary MCPRuntimeProvider::_get_state(const Dictionary &, const Dictionary &) {
	return runtime_service->get_state();
}

Dictionary MCPRuntimeProvider::_play(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->play(p_arguments);
}

Dictionary MCPRuntimeProvider::_stop(const Dictionary &, const Dictionary &) {
	return runtime_service->stop();
}

Dictionary MCPRuntimeProvider::_pause(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->set_suspended(p_arguments, true);
}

Dictionary MCPRuntimeProvider::_resume(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->set_suspended(p_arguments, false);
}

Dictionary MCPRuntimeProvider::_next_frame(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->next_frame(p_arguments);
}

Dictionary MCPRuntimeProvider::_debug_break(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "break");
}

Dictionary MCPRuntimeProvider::_debug_continue(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "continue");
}

Dictionary MCPRuntimeProvider::_debug_step_into(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "step_into");
}

Dictionary MCPRuntimeProvider::_debug_step_over(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "step_over");
}

Dictionary MCPRuntimeProvider::_debug_step_out(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "step_out");
}

Dictionary MCPRuntimeProvider::_get_tree(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_tree(p_arguments);
}

Dictionary MCPRuntimeProvider::_get_screenshot(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_screenshot(p_arguments);
}

Dictionary MCPRuntimeProvider::_get_viewport_summary(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_viewport_summary(p_arguments);
}

Dictionary MCPRuntimeProvider::_query_nodes(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->query_nodes(p_arguments);
}

Dictionary MCPRuntimeProvider::_get_interactables(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_interactables(p_arguments);
}

Dictionary MCPRuntimeProvider::_get_node_snapshot(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_node_snapshot(p_arguments);
}

Dictionary MCPRuntimeProvider::_click_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->click_target(p_arguments, session_id, false) : error;
}

Dictionary MCPRuntimeProvider::_double_click_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->click_target(p_arguments, session_id, true) : error;
}

Dictionary MCPRuntimeProvider::_hover_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->hover_target(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_focus_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->focus_target(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_drag_target_to_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->drag_target_to_target(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_type_text(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->type_text(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_scroll_view(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->scroll_view(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_start_wait(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->start_wait(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_get_wait_status(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->get_wait_status(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_cancel_wait(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->cancel_wait(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_start_performance(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->start_performance(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_get_performance_status(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->get_performance_status(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_stop_performance(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->stop_performance(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_get_properties(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_properties(p_arguments);
}

Dictionary MCPRuntimeProvider::_set_property(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->set_property(p_arguments);
}

Dictionary MCPRuntimeProvider::_send_input(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->send_input(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_start_input_sequence(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->start_input_sequence(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_get_input_sequence(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->get_input_sequence(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_cancel_input_sequence(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->cancel_input_sequence(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_release_input(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->release_input(p_arguments, session_id) : error;
}
