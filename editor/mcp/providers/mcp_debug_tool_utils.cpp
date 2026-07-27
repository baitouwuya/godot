/**************************************************************************/
/*  mcp_debug_tool_utils.cpp                                              */
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

#include "mcp_debug_tool_utils.h"

#include "mcp_tool_utils.h"

namespace {

static Dictionary _open_object_schema() {
	Dictionary schema;
	schema["type"] = "object";
	schema["additionalProperties"] = true;
	return schema;
}

static Dictionary _object_array_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("array", p_description);
	schema["items"] = _open_object_schema();
	return schema;
}

} // namespace

namespace MCPDebugToolUtils {

Dictionary query_schema(bool p_errors_only) {
	Dictionary properties;
	PackedStringArray sources;
	sources.push_back("all");
	sources.push_back("editor");
	sources.push_back("runtime");
	properties["source"] = MCPToolUtils::make_enum_schema(sources, "Filter editor or running-project events.", "all");
	properties["query"] = MCPToolUtils::make_property_schema("string", "Optional case-insensitive text filter.");
	Dictionary since = MCPToolUtils::make_property_schema("integer", "Only include events after this sequence.");
	since["minimum"] = 0;
	since["default"] = 0;
	properties["sinceSequence"] = since;
	Dictionary limit = MCPToolUtils::make_property_schema("integer", "Maximum number of returned groups.");
	limit["minimum"] = 1;
	limit["maximum"] = 500;
	limit["default"] = p_errors_only ? 50 : 100;
	properties["limit"] = limit;
	Dictionary debugger = MCPToolUtils::make_property_schema("integer", "Optional running-project debugger session index.");
	debugger["minimum"] = 0;
	properties["debuggerSession"] = debugger;
	Dictionary generation = MCPToolUtils::make_property_schema("integer", "Optional running-project generation returned by a previous result.");
	generation["minimum"] = 1;
	properties["runtimeGeneration"] = generation;
	Dictionary deduplicate = MCPToolUtils::make_property_schema("boolean", "Group identical non-adjacent events and return occurrence counts.");
	deduplicate["default"] = true;
	properties["deduplicate"] = deduplicate;
	Dictionary include_stack = MCPToolUtils::make_property_schema("boolean", "Include representative stack frames inline.");
	include_stack["default"] = false;
	properties["includeStack"] = include_stack;
	if (!p_errors_only) {
		Dictionary levels = MCPToolUtils::make_property_schema("array", "Optional severity filter.");
		PackedStringArray severity_values;
		severity_values.push_back("debug");
		severity_values.push_back("info");
		severity_values.push_back("warning");
		severity_values.push_back("error");
		Dictionary item;
		item["type"] = "string";
		item["enum"] = severity_values;
		levels["items"] = item;
		properties["levels"] = levels;
	} else {
		Dictionary include_warnings = MCPToolUtils::make_property_schema("boolean", "Include warnings as well as errors.");
		include_warnings["default"] = true;
		properties["includeWarnings"] = include_warnings;
	}
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary latest_log_schema() {
	Dictionary properties;
	Dictionary max_lines = MCPToolUtils::make_property_schema("integer", "Maximum number of lines read from the latest configured project log.");
	max_lines["minimum"] = 1;
	max_lines["maximum"] = 10000;
	max_lines["default"] = 200;
	properties["maxLines"] = max_lines;
	PackedStringArray views;
	views.push_back("summary");
	views.push_back("raw");
	properties["view"] = MCPToolUtils::make_enum_schema(views, "Return a compressed analysis or the bounded raw log tail.", "summary");
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary stack_schema() {
	Dictionary properties;
	Dictionary error_id = MCPToolUtils::make_property_schema("string", "Stable ID returned by get_logs or get_errors.");
	error_id["minLength"] = 1;
	properties["errorId"] = error_id;
	Dictionary debugger = MCPToolUtils::make_property_schema("integer", "Debugger session index for the latest paused stack.");
	debugger["minimum"] = 0;
	properties["debuggerSession"] = debugger;
	Dictionary thread = MCPToolUtils::make_property_schema("integer", "Optional paused thread ID within the debugger session.");
	thread["minimum"] = 0;
	properties["threadId"] = thread;
	Dictionary max_frames = MCPToolUtils::make_property_schema("integer", "Maximum stack frame count.");
	max_frames["minimum"] = 1;
	max_frames["maximum"] = 256;
	max_frames["default"] = 64;
	properties["maxFrames"] = max_frames;
	Dictionary schema = MCPToolUtils::make_object_schema(properties);
	Array selector_options;
	selector_options.push_back(MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray{ "errorId" }, true));
	selector_options.push_back(MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray{ "debuggerSession" }, true));
	schema["oneOf"] = selector_options;
	return schema;
}

Dictionary query_output_schema(bool p_errors_only) {
	Dictionary non_negative_integer = MCPToolUtils::make_property_schema("integer", "Non-negative result counter.");
	non_negative_integer["minimum"] = 0;
	Dictionary properties;
	properties["generation"] = non_negative_integer;
	properties["nextSequence"] = non_negative_integer;
	properties["storeHighWatermark"] = non_negative_integer;
	properties["dropped"] = non_negative_integer;
	properties["rawCount"] = non_negative_integer;
	properties["groupCount"] = non_negative_integer;
	properties["truncated"] = MCPToolUtils::make_property_schema("boolean", "Whether additional matching groups were omitted.");
	const String collection_name = p_errors_only ? "errors" : "groups";
	properties[collection_name] = _object_array_schema(p_errors_only ? "Structured warning and error groups." : "Structured log groups.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{
			"generation", "nextSequence", "storeHighWatermark", "dropped", "rawCount", "groupCount", "truncated", collection_name });
}

Dictionary latest_log_output_schema() {
	Dictionary count = MCPToolUtils::make_property_schema("integer", "Log line count.");
	count["minimum"] = 0;
	Dictionary view = MCPToolUtils::make_property_schema("string", "Returned log representation.");
	view["enum"] = PackedStringArray{ "summary", "raw" };
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Latest project log file name.");
	properties["view"] = view;
	properties["shownLines"] = count;
	properties["totalLines"] = count;
	properties["truncated"] = MCPToolUtils::make_property_schema("boolean", "Whether earlier log lines were omitted.");
	properties["text"] = MCPToolUtils::make_property_schema("string", "Bounded raw log tail.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "view", "shownLines", "totalLines", "truncated" }, true);
}

Dictionary stack_output_schema() {
	Dictionary kind = MCPToolUtils::make_property_schema("string", "Stack source.");
	kind["enum"] = PackedStringArray{ "error", "paused" };
	Dictionary nullable_integer;
	nullable_integer["type"] = PackedStringArray{ "integer", "null" };
	Dictionary depth = MCPToolUtils::make_property_schema("integer", "Total stack depth before the frame limit.");
	depth["minimum"] = 0;
	Dictionary frames = _object_array_schema("Ordered stack frames.");
	frames["maxItems"] = 256;

	Dictionary properties;
	properties["kind"] = kind;
	properties["errorId"] = MCPToolUtils::make_property_schema("string", "Source error ID, or empty for a paused stack.");
	properties["debuggerSession"] = MCPToolUtils::make_property_schema("integer", "Debugger session index, or -1 when unavailable.");
	properties["runtimeGeneration"] = MCPToolUtils::make_property_schema("integer", "Running-project generation.");
	properties["threadId"] = nullable_integer;
	properties["depth"] = depth;
	properties["frames"] = frames;
	properties["truncated"] = MCPToolUtils::make_property_schema("boolean", "Whether stack frames were omitted.");
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "kind", "errorId", "debuggerSession", "runtimeGeneration", "threadId", "depth", "frames", "truncated" });
}

} // namespace MCPDebugToolUtils
