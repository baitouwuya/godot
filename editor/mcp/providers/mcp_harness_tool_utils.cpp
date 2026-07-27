/**************************************************************************/
/*  mcp_harness_tool_utils.cpp                                            */
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

#include "mcp_harness_tool_utils.h"

#include "mcp_tool_utils.h"

namespace {

static Dictionary _open_object_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("object", p_description);
	schema["additionalProperties"] = true;
	return schema;
}

static Dictionary _non_negative_integer_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("integer", p_description);
	schema["minimum"] = 0;
	return schema;
}

static Dictionary _summary_output_properties() {
	Dictionary state = MCPToolUtils::make_property_schema("string", "Harness job lifecycle state.");
	state["enum"] = PackedStringArray{ "running", "cancelling", "collecting_evidence", "completed", "failed", "cancelled" };
	Dictionary debugger_session = MCPToolUtils::make_property_schema("integer", "Debugger session index, or -1 when auto-selected.");
	debugger_session["minimum"] = -1;
	Dictionary runtime_generation = MCPToolUtils::make_property_schema("integer", "Runtime generation that owns the job.");
	runtime_generation["minimum"] = 1;
	Dictionary properties;
	properties["jobId"] = MCPToolUtils::make_property_schema("string", "Opaque Harness job ID.");
	properties["state"] = state;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Harness plan name.");
	properties["debuggerSession"] = debugger_session;
	properties["runtimeGeneration"] = runtime_generation;
	properties["currentStep"] = _non_negative_integer_schema("Current zero-based plan step index.");
	properties["stepCount"] = _non_negative_integer_schema("Total number of plan steps.");
	properties["completedStepCount"] = _non_negative_integer_schema("Number of completed plan steps.");
	properties["sequenceId"] = MCPToolUtils::make_property_schema("string", "Active runtime input sequence ID.");
	properties["waitJobId"] = MCPToolUtils::make_property_schema("string", "Active runtime condition job ID.");
	properties["failure"] = _open_object_schema("Structured terminal failure when the job fails.");
	return properties;
}

static PackedStringArray _summary_output_required() {
	return PackedStringArray{ "jobId", "state", "name", "debuggerSession", "runtimeGeneration", "currentStep", "stepCount", "completedStepCount" };
}

} // namespace

namespace MCPHarnessToolUtils {

Dictionary start_schema() {
	Dictionary properties;
	Dictionary script = MCPToolUtils::make_property_schema("object", "Inline Harness script. Filesystem paths are not accepted.");
	script["additionalProperties"] = true;
	properties["script"] = script;
	Dictionary debugger = MCPToolUtils::make_property_schema("integer", "Optional running-project debugger session index.");
	debugger["minimum"] = 0;
	properties["debuggerSession"] = debugger;
	Dictionary generation = MCPToolUtils::make_property_schema("integer", "Runtime generation returned by godot.runtime.get_state.");
	generation["minimum"] = 1;
	properties["runtimeGeneration"] = generation;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "script", "runtimeGeneration" });
}

Dictionary job_schema() {
	Dictionary properties;
	properties["jobId"] = MCPToolUtils::make_property_schema("string", "Opaque Harness job ID returned by godot.runtime.harness.start.");
	Dictionary debugger = MCPToolUtils::make_property_schema("integer", "Optional running-project debugger session index.");
	debugger["minimum"] = 0;
	properties["debuggerSession"] = debugger;
	Dictionary generation = MCPToolUtils::make_property_schema("integer", "Runtime generation that owns the Harness job.");
	generation["minimum"] = 1;
	properties["runtimeGeneration"] = generation;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "jobId", "runtimeGeneration" });
}

Dictionary summary_output_schema() {
	return MCPToolUtils::make_object_schema(_summary_output_properties(), _summary_output_required());
}

Dictionary report_output_schema() {
	Dictionary item = _open_object_schema("Harness report entry.");
	Dictionary steps = MCPToolUtils::make_property_schema("array", "Per-step execution reports.");
	steps["items"] = item;
	Dictionary assertions = MCPToolUtils::make_property_schema("array", "Harness assertion results.");
	assertions["items"] = item;
	Dictionary properties = _summary_output_properties();
	properties["steps"] = steps;
	properties["assertions"] = assertions;
	properties["evidence"] = _open_object_schema("Collected evidence references.");
	properties["evidencePolicy"] = _open_object_schema("Effective evidence collection policy.");
	PackedStringArray required = _summary_output_required();
	required.append_array(PackedStringArray{ "steps", "assertions", "evidence", "evidencePolicy" });
	return MCPToolUtils::make_object_schema(properties, required);
}

} // namespace MCPHarnessToolUtils
