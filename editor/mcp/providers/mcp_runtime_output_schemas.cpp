/**************************************************************************/
/*  mcp_runtime_output_schemas.cpp                                        */
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

#include "mcp_runtime_output_schemas.h"

#include "mcp_tool_utils.h"

namespace MCPRuntimeOutputSchemas {

namespace {

Dictionary _integer(const String &p_description, int64_t p_minimum = 0) {
	Dictionary schema = MCPToolUtils::make_property_schema("integer", p_description);
	schema["minimum"] = p_minimum;
	return schema;
}

Dictionary _open_object(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("object", p_description);
	schema["additionalProperties"] = true;
	return schema;
}

Dictionary _object_array(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("array", p_description);
	schema["items"] = _open_object("Array item.");
	return schema;
}

Dictionary _number_pair(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("array", p_description);
	schema["items"] = MCPToolUtils::make_property_schema("number", "Coordinate value.");
	schema["minItems"] = 2;
	schema["maxItems"] = 2;
	return schema;
}

Dictionary _session_properties() {
	Dictionary properties;
	properties["debuggerSession"] = _integer("Debugger session index.");
	properties["runtimeGeneration"] = _integer("Runtime generation.", 1);
	return properties;
}

PackedStringArray _session_required() {
	return PackedStringArray{ "debuggerSession", "runtimeGeneration" };
}

Dictionary _session_schema(Dictionary p_properties, PackedStringArray p_required, bool p_allow_additional = false) {
	p_properties.merge(_session_properties());
	PackedStringArray required = _session_required();
	required.append_array(p_required);
	return MCPToolUtils::make_object_schema(p_properties, required, p_allow_additional);
}

} // namespace

Dictionary state() {
	Dictionary session_properties;
	session_properties["debuggerSession"] = _integer("Debugger session index.");
	session_properties["current"] = MCPToolUtils::make_property_schema("boolean", "Whether this is the editor's current debugger session.");
	session_properties["processId"] = _integer("Running project process ID.");
	session_properties["breaked"] = MCPToolUtils::make_property_schema("boolean", "Whether the debugger is paused at a breakpoint.");
	session_properties["debuggable"] = MCPToolUtils::make_property_schema("boolean", "Whether debugger stepping is available.");
	session_properties["runtimeGeneration"] = _integer("Runtime generation when initialization has completed.", 1);
	Dictionary session = MCPToolUtils::make_object_schema(session_properties,
			PackedStringArray{ "debuggerSession", "current", "processId", "breaked", "debuggable" });
	Dictionary sessions = MCPToolUtils::make_property_schema("array", "Active runtime debugger sessions.");
	sessions["items"] = session;
	Dictionary properties;
	properties["playing"] = MCPToolUtils::make_property_schema("boolean", "Whether the editor is running the project.");
	properties["playingScene"] = MCPToolUtils::make_property_schema("string", "Scene path launched by the editor.");
	properties["processId"] = _integer("Editor-launched process ID, or 0 when stopped.");
	properties["sessionCount"] = _integer("Active runtime debugger session count.");
	properties["sessions"] = sessions;
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "playing", "playingScene", "processId", "sessionCount", "sessions" });
}

Dictionary play() {
	Dictionary mode = MCPToolUtils::make_property_schema("string", "Accepted launch mode.");
	mode["enum"] = PackedStringArray{ "main", "current", "scene" };
	Dictionary state = MCPToolUtils::make_property_schema("string", "Launch state.");
	state["enum"] = PackedStringArray{ "starting" };
	Dictionary properties;
	properties["accepted"] = MCPToolUtils::make_property_schema("boolean", "Whether the launch request was accepted.");
	properties["mode"] = mode;
	properties["state"] = state;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "accepted", "mode", "state" });
}

Dictionary stop() {
	Dictionary properties;
	properties["stopped"] = MCPToolUtils::make_property_schema("boolean", "Whether a running project was stopped.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "stopped" });
}

Dictionary suspended() {
	Dictionary properties;
	properties["suspended"] = MCPToolUtils::make_property_schema("boolean", "Requested SceneTree suspension state.");
	properties["dispatched"] = MCPToolUtils::make_property_schema("boolean", "Whether the debugger command was dispatched.");
	return _session_schema(properties, PackedStringArray{ "suspended", "dispatched" });
}

Dictionary dispatched() {
	Dictionary properties;
	properties["dispatched"] = MCPToolUtils::make_property_schema("boolean", "Whether the debugger command was dispatched.");
	return _session_schema(properties, PackedStringArray{ "dispatched" });
}

Dictionary debug_control() {
	Dictionary operation = MCPToolUtils::make_property_schema("string", "Debugger control operation.");
	operation["enum"] = PackedStringArray{ "break", "continue", "step_into", "step_over", "step_out" };
	Dictionary properties;
	properties["operation"] = operation;
	properties["dispatched"] = MCPToolUtils::make_property_schema("boolean", "Whether the debugger command was dispatched.");
	return _session_schema(properties, PackedStringArray{ "operation", "dispatched" });
}

Dictionary tree() {
	Dictionary properties;
	properties["treeRevision"] = _integer("Remote scene tree revision.");
	properties["nodes"] = _object_array("Flat runtime scene tree nodes.");
	properties["nodeCount"] = _integer("Runtime scene tree node count.");
	return _session_schema(properties, PackedStringArray{ "treeRevision", "nodes", "nodeCount" });
}

Dictionary screenshot() {
	Dictionary properties;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Requested screenshot label.");
	properties["path"] = MCPToolUtils::make_property_schema("string", "Temporary PNG filesystem path.");
	properties["width"] = _integer("Screenshot width in pixels.", 1);
	properties["height"] = _integer("Screenshot height in pixels.", 1);
	properties["frame"] = _integer("Runtime process frame that produced the screenshot.");
	return _session_schema(properties, PackedStringArray{ "name", "path", "width", "height", "frame" });
}

Dictionary viewport_summary() {
	Dictionary properties;
	properties["windowSize"] = _number_pair("Root viewport [width, height].");
	properties["rootPath"] = MCPToolUtils::make_property_schema("string", "Root viewport node path.");
	properties["rootScene"] = MCPToolUtils::make_property_schema("string", "Current scene node path.");
	properties["contentScene"] = MCPToolUtils::make_property_schema("string", "Resolved content scene node path.");
	properties["activeCamera"] = MCPToolUtils::make_property_schema("string", "Active camera node path.");
	properties["uiTargets"] = _integer("Visible UI target count.");
	properties["interactableUiTargets"] = _integer("Interactable UI target count.");
	properties["targets3D"] = _integer("Visible 3D target count.");
	properties["interactableTargets3D"] = _integer("Interactable 3D target count.");
	properties["frame"] = _integer("Runtime process frame used for the summary.");
	return _session_schema(properties,
			PackedStringArray{ "windowSize", "rootPath", "rootScene", "contentScene", "activeCamera", "frame" });
}

Dictionary query() {
	Dictionary properties;
	properties["maxResults"] = _integer("Applied result limit.", 1);
	properties["count"] = _integer("Matching runtime node count.");
	properties["visitedCount"] = _integer("Visited runtime node count.");
	properties["nodes"] = _object_array("Compact runtime node snapshots.");
	properties["frame"] = _integer("Runtime process frame used for the query.");
	return _session_schema(properties, PackedStringArray{ "maxResults", "count", "visitedCount", "nodes", "frame" });
}

Dictionary snapshot() {
	Dictionary properties;
	properties["node"] = _open_object("Compact runtime node snapshot.");
	properties["frame"] = _integer("Runtime process frame used for the snapshot.");
	return _session_schema(properties, PackedStringArray{ "node", "frame" });
}

Dictionary target_action() {
	Dictionary action = MCPToolUtils::make_property_schema("string", "Resolved target action.");
	action["enum"] = PackedStringArray{ "click_target", "double_click_target", "hover_target", "focus_target", "drag_target_to_target", "type_text", "scroll_view" };
	Dictionary properties;
	properties["action"] = action;
	properties["dispatched"] = MCPToolUtils::make_property_schema("boolean", "Whether immediate input was dispatched.");
	properties["asynchronous"] = MCPToolUtils::make_property_schema("boolean", "Whether an input sequence owns the action.");
	properties["eventCount"] = _integer("Dispatched input event count.");
	properties["sequenceId"] = MCPToolUtils::make_property_schema("string", "Asynchronous input sequence ID.");
	properties["state"] = MCPToolUtils::make_property_schema("string", "Asynchronous input sequence state.");
	properties["resolvedTarget"] = _open_object("Resolved runtime target snapshot.");
	properties["resolvedFromTarget"] = _open_object("Resolved drag source snapshot.");
	properties["resolvedToTarget"] = _open_object("Resolved drag destination snapshot.");
	properties["position"] = _number_pair("Resolved root viewport position.");
	properties["from"] = _number_pair("Resolved drag source position.");
	properties["to"] = _number_pair("Resolved drag destination position.");
	properties["focused"] = MCPToolUtils::make_property_schema("boolean", "Whether the runtime Control accepted focus.");
	properties["button"] = MCPToolUtils::make_property_schema("string", "Mouse button used by the action.");
	properties["direction"] = MCPToolUtils::make_property_schema("string", "Scroll direction.");
	properties["frames"] = _integer("Drag frame count.", 1);
	properties["steps"] = _integer("Scroll step count.", 1);
	properties["textLength"] = _integer("Injected text length.");
	return _session_schema(properties, PackedStringArray{ "action" }, true);
}

Dictionary wait_job() {
	Dictionary state = MCPToolUtils::make_property_schema("string", "Runtime condition job state.");
	state["enum"] = PackedStringArray{ "running", "succeeded", "timed_out", "failed", "cancelled" };
	Dictionary properties;
	properties["jobId"] = MCPToolUtils::make_property_schema("string", "Runtime condition job ID.");
	properties["state"] = state;
	properties["conditionKind"] = MCPToolUtils::make_property_schema("string", "Runtime condition kind.");
	properties["startedFrame"] = _integer("Frame when condition evaluation started.");
	properties["deadlineFrame"] = _integer("Frame when the job times out.");
	properties["nextPollFrame"] = _integer("Next scheduled evaluation frame.");
	properties["pollEveryFrames"] = _integer("Frames between evaluations.", 1);
	properties["evaluationCount"] = _integer("Condition evaluation count.");
	properties["failure"] = MCPToolUtils::make_property_schema("string", "Terminal condition failure.");
	return _session_schema(properties, PackedStringArray{ "jobId", "state", "conditionKind", "startedFrame", "deadlineFrame", "nextPollFrame", "pollEveryFrames", "evaluationCount" });
}

Dictionary performance_job() {
	Dictionary state = MCPToolUtils::make_property_schema("string", "Runtime performance job state.");
	state["enum"] = PackedStringArray{ "running", "completed", "stopped", "failed", "cancelled" };
	Dictionary max_frames;
	max_frames["description"] = "Configured automatic completion frame count, or null when unbounded.";
	Dictionary properties;
	properties["jobId"] = MCPToolUtils::make_property_schema("string", "Runtime performance job ID.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Session-local performance job name.");
	properties["state"] = state;
	properties["startedFrame"] = _integer("Frame when performance capture started.");
	properties["maxFrames"] = max_frames;
	properties["capturedFrames"] = _integer("Captured performance frame count.");
	properties["failure"] = MCPToolUtils::make_property_schema("string", "Terminal performance capture failure.");
	properties["summary"] = _open_object("Structured performance aggregation summary.");
	return _session_schema(properties, PackedStringArray{ "jobId", "state", "startedFrame", "maxFrames", "capturedFrames" });
}

Dictionary properties() {
	Dictionary properties = _session_properties();
	properties["path"] = MCPToolUtils::make_property_schema("string", "Absolute runtime node path.");
	properties["objectId"] = MCPToolUtils::make_property_schema("string", "Remote runtime object ID.");
	properties["type"] = MCPToolUtils::make_property_schema("string", "Runtime node type.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Runtime node name.");
	properties["propertyCount"] = _integer("Inspectable runtime property count.");
	properties["properties"] = _object_array("Inspectable runtime properties.");
	properties["propertyLayout"] = _object_array("Runtime inspector property layout.");
	properties["objectRevision"] = _integer("Remote object revision.");
	PackedStringArray required = _session_required();
	required.append_array(PackedStringArray{ "path", "objectId", "type", "name", "propertyCount", "properties", "propertyLayout", "objectRevision" });
	return MCPToolUtils::make_object_schema(properties, required, true);
}

Dictionary set_property() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Changed runtime node path.");
	properties["objectId"] = MCPToolUtils::make_property_schema("string", "Changed remote runtime object ID.");
	properties["property"] = MCPToolUtils::make_property_schema("string", "Changed runtime property name.");
	properties["objectRevision"] = _integer("Remote object revision after the change.");
	properties["coerced"] = MCPToolUtils::make_property_schema("boolean", "Whether the runtime coerced the requested value.");
	properties["value"] = Dictionary();
	properties["valueAvailable"] = MCPToolUtils::make_property_schema("boolean", "Whether the effective value could be encoded.");
	return _session_schema(properties, PackedStringArray{ "path", "objectId", "property", "objectRevision", "coerced" });
}

Dictionary input_dispatch() {
	Dictionary properties;
	properties["dispatched"] = MCPToolUtils::make_property_schema("boolean", "Whether runtime input was dispatched.");
	properties["eventCount"] = _integer("Dispatched runtime input event count.");
	return _session_schema(properties, PackedStringArray{ "dispatched", "eventCount" });
}

Dictionary input_sequence() {
	Dictionary state = MCPToolUtils::make_property_schema("string", "Runtime input sequence state.");
	state["enum"] = PackedStringArray{ "queued", "running", "waiting", "completed", "cancelled", "failed", "stale" };
	Dictionary properties;
	properties["sequenceId"] = MCPToolUtils::make_property_schema("string", "Runtime input sequence ID.");
	properties["state"] = state;
	properties["stepIndex"] = _integer("Current sequence step index.");
	properties["stepCount"] = _integer("Total sequence step count.");
	properties["eventsDispatched"] = _integer("Dispatched sequence event count.");
	properties["remainingMs"] = _integer("Remaining millisecond wait.");
	properties["remainingFrames"] = _integer("Remaining frame wait.");
	properties["failure"] = MCPToolUtils::make_property_schema("string", "Terminal sequence failure.");
	return _session_schema(properties, PackedStringArray{ "sequenceId", "state", "stepIndex", "stepCount", "eventsDispatched" });
}

Dictionary release_input() {
	Dictionary properties;
	properties["releasedCount"] = _integer("Released held runtime input count.");
	properties["sequencesCancelled"] = MCPToolUtils::make_property_schema("boolean", "Whether owned input sequences were cancelled.");
	return _session_schema(properties, PackedStringArray{ "releasedCount", "sequencesCancelled" });
}

} // namespace MCPRuntimeOutputSchemas
