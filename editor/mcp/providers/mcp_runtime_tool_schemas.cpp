/**************************************************************************/
/*  mcp_runtime_tool_schemas.cpp                                          */
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
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "mcp_runtime_tool_schemas.h"

#include "mcp_tool_utils.h"

namespace MCPRuntimeToolSchemas {

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

Dictionary _rect(const String &p_description) {
	Dictionary properties;
	properties["x"] = MCPToolUtils::make_property_schema("number", "Left coordinate in root viewport pixels.");
	properties["y"] = MCPToolUtils::make_property_schema("number", "Top coordinate in root viewport pixels.");
	properties["width"] = MCPToolUtils::make_property_schema("number", "Rectangle width in pixels.");
	properties["height"] = MCPToolUtils::make_property_schema("number", "Rectangle height in pixels.");
	Dictionary schema = MCPToolUtils::make_object_schema(properties, PackedStringArray{ "x", "y", "width", "height" });
	schema["description"] = p_description;
	return schema;
}

Dictionary _selector() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Exact absolute runtime node path.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Exact node name.");
	properties["type"] = MCPToolUtils::make_property_schema("string", "Exact runtime class name.");
	properties["group"] = MCPToolUtils::make_property_schema("string", "Exact public group name.");
	properties["text"] = MCPToolUtils::make_property_schema("string", "Exact text property value.");
	properties["is3D"] = MCPToolUtils::make_property_schema("boolean", "Match 3D or non-3D nodes.");
	properties["visible"] = MCPToolUtils::make_property_schema("boolean", "Match effective runtime visibility.");
	properties["screenRect"] = _rect("Match nodes whose projected screen rectangle intersects this rectangle.");
	Dictionary nearest = MCPToolUtils::make_property_schema("array", "Select the matching node nearest to this root viewport [x, y] point.");
	nearest["items"] = MCPToolUtils::make_property_schema("number", "Root viewport pixel coordinate.");
	nearest["minItems"] = 2;
	nearest["maxItems"] = 2;
	properties["nearestToScreenPoint"] = nearest;
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

Dictionary _target_button() {
	Dictionary button = MCPToolUtils::make_property_schema("string", "Mouse button used by the target action.");
	button["enum"] = PackedStringArray{ "left", "right", "middle" };
	button["default"] = "left";
	return button;
}

Dictionary _wait_condition() {
	Dictionary properties;
	Dictionary kind = MCPToolUtils::make_property_schema("string", "Asynchronous runtime condition kind.");
	kind["enum"] = PackedStringArray{ "node_exists", "node_gone", "property", "scene_changed", "screenshot_diff" };
	properties["kind"] = kind;
	properties["selector"] = _selector();
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

} // namespace

Dictionary session(bool p_require_generation, bool p_timeout) {
	Dictionary properties;
	_add_session_properties(properties, p_require_generation, p_timeout);
	PackedStringArray required;
	if (p_require_generation) {
		required.push_back("runtimeGeneration");
	}
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary play() {
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

Dictionary tree() {
	Dictionary properties;
	_add_session_properties(properties, false, true);
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary runtime_query() {
	Dictionary properties;
	_add_observation_session_properties(properties);
	properties["filter"] = _selector();
	Dictionary max_results = MCPToolUtils::make_property_schema("integer", "Maximum number of matching snapshots to return.");
	max_results["minimum"] = 1;
	max_results["maximum"] = 256;
	max_results["default"] = 64;
	properties["maxResults"] = max_results;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary runtime_snapshot() {
	Dictionary properties;
	_add_observation_session_properties(properties);
	properties["selector"] = _selector();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "selector" });
}

Dictionary viewport_summary() {
	Dictionary properties;
	_add_observation_session_properties(properties);
	Dictionary include_counts = MCPToolUtils::make_property_schema("boolean", "Include UI, 3D, and interactable target counts.");
	include_counts["default"] = true;
	properties["includeCounts"] = include_counts;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary screenshot() {
	Dictionary properties;
	_add_observation_session_properties(properties);
	properties["name"] = MCPToolUtils::make_property_schema("string", "Optional label included in the generated temporary PNG filename.");
	properties["crop"] = _rect("Optional root viewport crop rectangle.");
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary click_target(bool p_double_click) {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["selector"] = _selector();
	properties["button"] = _target_button();
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

Dictionary single_target() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["selector"] = _selector();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "selector", "runtimeGeneration" });
}

Dictionary drag_target() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["fromSelector"] = _selector();
	properties["toSelector"] = _selector();
	properties["button"] = _target_button();
	Dictionary frames = MCPToolUtils::make_property_schema("integer", "Rendered frames used to interpolate the pointer path.");
	frames["minimum"] = 1;
	frames["maximum"] = 60;
	frames["default"] = 20;
	properties["frames"] = frames;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "fromSelector", "toSelector", "runtimeGeneration" });
}

Dictionary type_text() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["selector"] = _selector();
	Dictionary text = MCPToolUtils::make_property_schema("string", "Unicode text injected after the target Control accepts focus.");
	text["minLength"] = 1;
	text["maxLength"] = 100;
	properties["text"] = text;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "selector", "text", "runtimeGeneration" });
}

Dictionary scroll_view() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["selector"] = _selector();
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

Dictionary wait_start() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["condition"] = _wait_condition();
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

Dictionary wait_job() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["jobId"] = MCPToolUtils::make_property_schema("string", "Opaque wait job ID returned by godot.runtime.wait.start.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "jobId", "runtimeGeneration" });
}

Dictionary performance_start() {
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

Dictionary performance_job() {
	Dictionary properties;
	_add_target_action_session_properties(properties);
	properties["jobId"] = MCPToolUtils::make_property_schema("string", "Opaque performance job ID returned by godot.runtime.performance.start.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "jobId", "runtimeGeneration" });
}

Dictionary properties() {
	Dictionary properties;
	_add_session_properties(properties, false, true);
	properties["path"] = MCPToolUtils::make_property_schema("string", "Absolute runtime node path returned by godot.runtime.get_tree.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path" });
}

Dictionary set_property() {
	Dictionary properties;
	_add_session_properties(properties, true, true);
	properties["path"] = MCPToolUtils::make_property_schema("string", "Absolute runtime node path returned by godot.runtime.get_tree.");
	properties["property"] = MCPToolUtils::make_property_schema("string", "Exact runtime property name returned by get_properties.");
	Dictionary value;
	value["description"] = "JSON-native Variant value compatible with the property's type.";
	properties["value"] = value;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "property", "value", "runtimeGeneration" });
}

Dictionary input() {
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
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "events", "runtimeGeneration" });
}

Dictionary input_sequence() {
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

Dictionary input_sequence_status() {
	Dictionary properties;
	properties["sequenceId"] = MCPToolUtils::make_property_schema("string", "Opaque sequence ID returned by godot.runtime.input.sequence.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "sequenceId" });
}

Dictionary input_sequence_cancel() {
	Dictionary properties;
	_add_session_properties(properties, true, false);
	properties["sequenceId"] = MCPToolUtils::make_property_schema("string", "Opaque sequence ID returned by godot.runtime.input.sequence.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "sequenceId", "runtimeGeneration" });
}

} // namespace MCPRuntimeToolSchemas
