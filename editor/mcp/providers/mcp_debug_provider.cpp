/**************************************************************************/
/*  mcp_debug_provider.cpp                                                */
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

#include "mcp_debug_provider.h"

#include "mcp_debug_event_store.h"
#include "mcp_log_analyzer.h"
#include "mcp_project_log_reader.h"
#include "mcp_tool_utils.h"

#include "core/config/project_settings.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

struct QueryOptions {
	String source = "all";
	Vector<MCPDebugEventStore::Severity> levels;
	String query;
	uint64_t since_sequence = 0;
	int limit = 100;
	bool deduplicate = true;
	bool include_stack = false;
	int debugger_session = -1;
	uint64_t runtime_generation = 0;
};

struct Group {
	MCPDebugEventStore::Event representative;
	uint64_t count = 0;
	uint64_t first_sequence = 0;
	uint64_t last_sequence = 0;
	uint64_t first_timestamp_usec = 0;
	uint64_t last_timestamp_usec = 0;
};

static Dictionary _property_schema(const String &p_type, const String &p_description) {
	Dictionary property;
	property["type"] = p_type;
	property["description"] = p_description;
	return property;
}

static Dictionary _enum_schema(const PackedStringArray &p_values, const String &p_description, const String &p_default) {
	Dictionary property = _property_schema("string", p_description);
	property["enum"] = p_values;
	property["default"] = p_default;
	return property;
}

static Dictionary _query_schema(bool p_errors_only) {
	Dictionary properties;
	PackedStringArray sources;
	sources.push_back("all");
	sources.push_back("editor");
	sources.push_back("runtime");
	properties["source"] = _enum_schema(sources, "Filter editor or running-project events.", "all");
	properties["query"] = _property_schema("string", "Optional case-insensitive text filter.");
	Dictionary since = _property_schema("integer", "Only include events after this sequence.");
	since["minimum"] = 0;
	since["default"] = 0;
	properties["sinceSequence"] = since;
	Dictionary limit = _property_schema("integer", "Maximum number of returned groups.");
	limit["minimum"] = 1;
	limit["maximum"] = 500;
	limit["default"] = p_errors_only ? 50 : 100;
	properties["limit"] = limit;
	Dictionary debugger = _property_schema("integer", "Optional running-project debugger session index.");
	debugger["minimum"] = 0;
	properties["debuggerSession"] = debugger;
	Dictionary generation = _property_schema("integer", "Optional running-project generation returned by a previous result.");
	generation["minimum"] = 1;
	properties["runtimeGeneration"] = generation;
	Dictionary deduplicate = _property_schema("boolean", "Group identical non-adjacent events and return occurrence counts.");
	deduplicate["default"] = true;
	properties["deduplicate"] = deduplicate;
	Dictionary include_stack = _property_schema("boolean", "Include representative stack frames inline.");
	include_stack["default"] = false;
	properties["includeStack"] = include_stack;
	if (!p_errors_only) {
		Dictionary levels = _property_schema("array", "Optional severity filter.");
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
		Dictionary include_warnings = _property_schema("boolean", "Include warnings as well as errors.");
		include_warnings["default"] = true;
		properties["includeWarnings"] = include_warnings;
	}

	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = properties;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _stack_schema() {
	Dictionary properties;
	properties["errorId"] = _property_schema("string", "Stable ID returned by get_logs or get_errors.");
	Dictionary debugger = _property_schema("integer", "Debugger session index for the latest paused stack.");
	debugger["minimum"] = 0;
	properties["debuggerSession"] = debugger;
	Dictionary thread = _property_schema("integer", "Optional paused thread ID within the debugger session.");
	thread["minimum"] = 0;
	properties["threadId"] = thread;
	Dictionary max_frames = _property_schema("integer", "Maximum stack frame count.");
	max_frames["minimum"] = 1;
	max_frames["maximum"] = 256;
	max_frames["default"] = 64;
	properties["maxFrames"] = max_frames;
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = properties;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _latest_log_schema() {
	Dictionary properties;
	Dictionary max_lines = _property_schema("integer", "Maximum number of lines read from the latest configured project log.");
	max_lines["minimum"] = 1;
	max_lines["maximum"] = 10000;
	max_lines["default"] = 200;
	properties["maxLines"] = max_lines;
	PackedStringArray views;
	views.push_back("summary");
	views.push_back("raw");
	properties["view"] = _enum_schema(views, "Return a compressed analysis or the bounded raw log tail.", "summary");
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = properties;
	schema["additionalProperties"] = false;
	return schema;
}

static String _severity_name(MCPDebugEventStore::Severity p_severity) {
	switch (p_severity) {
		case MCPDebugEventStore::SEVERITY_DEBUG:
			return "debug";
		case MCPDebugEventStore::SEVERITY_WARNING:
			return "warning";
		case MCPDebugEventStore::SEVERITY_ERROR:
			return "error";
		default:
			return "info";
	}
}

static String _source_name(MCPDebugEventStore::Source p_source) {
	return p_source == MCPDebugEventStore::SOURCE_RUNTIME ? "runtime" : "editor";
}

static bool _parse_severity(const String &p_value, MCPDebugEventStore::Severity &r_severity) {
	if (p_value == "debug") {
		r_severity = MCPDebugEventStore::SEVERITY_DEBUG;
	} else if (p_value == "info") {
		r_severity = MCPDebugEventStore::SEVERITY_INFO;
	} else if (p_value == "warning") {
		r_severity = MCPDebugEventStore::SEVERITY_WARNING;
	} else if (p_value == "error") {
		r_severity = MCPDebugEventStore::SEVERITY_ERROR;
	} else {
		return false;
	}
	return true;
}

static bool _has_severity(const Vector<MCPDebugEventStore::Severity> &p_levels, MCPDebugEventStore::Severity p_value) {
	return p_levels.is_empty() || p_levels.has(p_value);
}

static String _normalized_message(const String &p_message) {
	String value = p_message.replace("\r", " ").replace("\n", " ").strip_edges();
	while (value.contains("  ")) {
		value = value.replace("  ", " ");
	}
	return value;
}

static String _group_key(const MCPDebugEventStore::Event &p_event) {
	String top_frame;
	if (!p_event.stack.is_empty()) {
		const MCPDebugEventStore::Frame &frame = p_event.stack[0];
		top_frame = frame.file + ":" + itos(frame.line) + ":" + frame.function;
	}
	return vformat("%d\x1f%d\x1f%s\x1f%d\x1f%s\x1f%s\x1f%s\x1f%s\x1f%s\x1f%d\x1f%s\x1f%s", int(p_event.source), p_event.debugger_session,
			String::num_uint64(p_event.runtime_generation), int(p_event.severity), p_event.category, _normalized_message(p_event.message),
			p_event.expression, p_event.description, p_event.file, p_event.line, p_event.function, top_frame);
}

static String _error_id(const MCPDebugEventStore::Event &p_event) {
	return "debug-" + String::num_uint64(p_event.sequence);
}

static String _safe_file_path(const String &p_path) {
	if (p_path.is_empty() || p_path.begins_with("res://")) {
		return p_path;
	}
	if (ProjectSettings::get_singleton()) {
		const String localized = ProjectSettings::get_singleton()->localize_path(p_path);
		if (localized.begins_with("res://")) {
			return localized;
		}
	}
	return "external://" + p_path.get_file();
}

static Dictionary _frame_to_dictionary(const MCPDebugEventStore::Frame &p_frame, int p_index) {
	Dictionary frame;
	frame["index"] = p_index;
	frame["file"] = _safe_file_path(p_frame.file);
	frame["line"] = p_frame.line;
	frame["function"] = p_frame.function;
	return frame;
}

static Array _frames_to_array(const Vector<MCPDebugEventStore::Frame> &p_frames, int p_max_frames, bool &r_truncated) {
	Array frames;
	const int count = MIN(p_frames.size(), p_max_frames);
	for (int i = 0; i < count; i++) {
		frames.push_back(_frame_to_dictionary(p_frames[i], i));
	}
	r_truncated = p_frames.size() > count;
	return frames;
}

static Dictionary _location(const MCPDebugEventStore::Event &p_event) {
	Dictionary location;
	location["file"] = _safe_file_path(p_event.file);
	location["line"] = p_event.line;
	location["function"] = p_event.function;
	return location;
}

static Dictionary _group_to_dictionary(const Group &p_group, bool p_include_stack) {
	const MCPDebugEventStore::Event &event = p_group.representative;
	Dictionary result;
	result["errorId"] = _error_id(event);
	result["source"] = _source_name(event.source);
	result["debuggerSession"] = event.debugger_session;
	result["runtimeGeneration"] = int64_t(event.runtime_generation);
	result["threadId"] = event.thread_id >= 0 ? Variant(event.thread_id) : Variant();
	result["level"] = _severity_name(event.severity);
	result["category"] = event.category;
	result["message"] = _normalized_message(event.message);
	if (!event.expression.is_empty()) {
		result["expression"] = event.expression;
	}
	if (!event.description.is_empty()) {
		result["description"] = event.description;
	}
	result["count"] = int64_t(p_group.count);
	result["firstSequence"] = int64_t(p_group.first_sequence);
	result["lastSequence"] = int64_t(p_group.last_sequence);
	result["firstTimestampUsec"] = String::num_uint64(p_group.first_timestamp_usec);
	result["lastTimestampUsec"] = String::num_uint64(p_group.last_timestamp_usec);
	result["rich"] = event.rich;
	result["truncated"] = event.truncated;
	result["location"] = _location(event);
	result["stackAvailable"] = !event.stack.is_empty();
	result["stackDepth"] = event.stack.size();
	if (p_include_stack) {
		bool stack_truncated = false;
		result["stack"] = _frames_to_array(event.stack, 64, stack_truncated);
		result["stackTruncated"] = stack_truncated;
	} else if (!event.stack.is_empty()) {
		result["topFrame"] = _frame_to_dictionary(event.stack[0], 0);
	}
	return result;
}

static Dictionary _invalid_arguments(const String &p_message) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", p_message);
}

static bool _parse_common_query(const Dictionary &p_arguments, bool p_errors_only, QueryOptions &r_options, String &r_error) {
	PackedStringArray allowed;
	allowed.push_back("source");
	allowed.push_back("query");
	allowed.push_back("sinceSequence");
	allowed.push_back("limit");
	allowed.push_back("debuggerSession");
	allowed.push_back("runtimeGeneration");
	allowed.push_back("deduplicate");
	allowed.push_back("includeStack");
	allowed.push_back(p_errors_only ? "includeWarnings" : "levels");
	String unknown;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown)) {
		r_error = "Unknown argument: " + unknown;
		return false;
	}

	const Variant source = p_arguments.get("source", "all");
	const Variant query = p_arguments.get("query", "");
	const Variant deduplicate = p_arguments.get("deduplicate", true);
	const Variant include_stack = p_arguments.get("includeStack", false);
	if (source.get_type() != Variant::STRING || query.get_type() != Variant::STRING || deduplicate.get_type() != Variant::BOOL || include_stack.get_type() != Variant::BOOL) {
		r_error = "source, query, deduplicate, or includeStack has an invalid type.";
		return false;
	}
	r_options.source = source;
	r_options.query = String(query).strip_edges().to_lower();
	r_options.deduplicate = deduplicate;
	r_options.include_stack = include_stack;
	if (r_options.source != "all" && r_options.source != "editor" && r_options.source != "runtime") {
		r_error = "source must be all, editor, or runtime.";
		return false;
	}

	int64_t since_sequence = 0;
	int64_t limit = p_errors_only ? 50 : 100;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("sinceSequence", 0), 0, INT64_MAX, since_sequence) ||
			!MCPToolUtils::try_get_json_integer(p_arguments.get("limit", limit), 1, 500, limit)) {
		r_error = "sinceSequence or limit is outside its allowed range.";
		return false;
	}
	r_options.since_sequence = uint64_t(since_sequence);
	r_options.limit = int(limit);

	if (p_arguments.has("debuggerSession")) {
		int64_t debugger_session = 0;
		if (!MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, debugger_session)) {
			r_error = "debuggerSession must be a non-negative integer.";
			return false;
		}
		r_options.debugger_session = int(debugger_session);
	}
	if (p_arguments.has("runtimeGeneration")) {
		int64_t runtime_generation = 0;
		if (!MCPToolUtils::try_get_json_integer(p_arguments["runtimeGeneration"], 1, INT64_MAX, runtime_generation)) {
			r_error = "runtimeGeneration must be a positive integer.";
			return false;
		}
		r_options.runtime_generation = uint64_t(runtime_generation);
	}

	if (p_errors_only) {
		const Variant include_warnings = p_arguments.get("includeWarnings", true);
		if (include_warnings.get_type() != Variant::BOOL) {
			r_error = "includeWarnings must be boolean.";
			return false;
		}
		r_options.levels.push_back(MCPDebugEventStore::SEVERITY_ERROR);
		if (include_warnings) {
			r_options.levels.push_back(MCPDebugEventStore::SEVERITY_WARNING);
		}
	} else if (p_arguments.has("levels")) {
		if (p_arguments["levels"].get_type() != Variant::ARRAY) {
			r_error = "levels must be an array.";
			return false;
		}
		const Array levels = p_arguments["levels"];
		for (const Variant &level : levels) {
			MCPDebugEventStore::Severity severity;
			if (level.get_type() != Variant::STRING || !_parse_severity(level, severity)) {
				r_error = "levels contains an unknown severity.";
				return false;
			}
			if (!r_options.levels.has(severity)) {
				r_options.levels.push_back(severity);
			}
		}
	}
	return true;
}

static bool _matches(const MCPDebugEventStore::Event &p_event, const QueryOptions &p_options) {
	if (p_event.sequence <= p_options.since_sequence || !_has_severity(p_options.levels, p_event.severity)) {
		return false;
	}
	if (p_options.source != "all" && p_options.source != _source_name(p_event.source)) {
		return false;
	}
	if (p_options.debugger_session >= 0 && p_event.debugger_session != p_options.debugger_session) {
		return false;
	}
	if (p_options.runtime_generation > 0 && p_event.runtime_generation != p_options.runtime_generation) {
		return false;
	}
	if (!p_options.query.is_empty()) {
		const String haystack = (p_event.message + "\n" + p_event.description + "\n" + p_event.expression + "\n" + p_event.file + "\n" + p_event.function).to_lower();
		if (!haystack.contains(p_options.query)) {
			return false;
		}
	}
	return true;
}

static Vector<Group> _make_groups(const MCPDebugEventStore::Snapshot &p_snapshot, const QueryOptions &p_options,
		int64_t &r_raw_count, uint64_t &r_next_sequence, bool &r_truncated) {
	Vector<Group> groups;
	HashMap<String, int> index_by_key;
	r_raw_count = 0;
	r_next_sequence = p_options.since_sequence;
	r_truncated = false;
	for (const MCPDebugEventStore::Event &event : p_snapshot.events) {
		if (event.sequence <= p_options.since_sequence) {
			continue;
		}
		if (!_matches(event, p_options)) {
			r_next_sequence = event.sequence;
			continue;
		}
		const String key = p_options.deduplicate ? _group_key(event) : String::num_uint64(event.sequence);
		const int *existing = index_by_key.getptr(key);
		if (existing) {
			r_raw_count++;
			r_next_sequence = event.sequence;
			Group &group = groups.write[*existing];
			group.count++;
			group.last_sequence = event.sequence;
			group.last_timestamp_usec = event.timestamp_usec;
			group.representative = event;
			continue;
		}
		if (groups.size() >= p_options.limit) {
			r_truncated = true;
			break;
		}
		r_raw_count++;
		r_next_sequence = event.sequence;
		Group group;
		group.representative = event;
		group.count = 1;
		group.first_sequence = event.sequence;
		group.last_sequence = event.sequence;
		group.first_timestamp_usec = event.timestamp_usec;
		group.last_timestamp_usec = event.timestamp_usec;
		index_by_key.insert(key, groups.size());
		groups.push_back(group);
	}
	return groups;
}

static Dictionary _query_result(MCPDebugEventStore *p_store, const Dictionary &p_arguments, bool p_errors_only) {
	QueryOptions options;
	String error;
	if (!_parse_common_query(p_arguments, p_errors_only, options, error)) {
		return _invalid_arguments(error);
	}
	const MCPDebugEventStore::Snapshot snapshot = p_store->snapshot();
	int64_t raw_count = 0;
	uint64_t next_sequence = options.since_sequence;
	bool truncated = false;
	Vector<Group> groups = _make_groups(snapshot, options, raw_count, next_sequence, truncated);
	Array results;
	for (int i = 0; i < groups.size(); i++) {
		results.push_back(_group_to_dictionary(groups[i], options.include_stack));
	}
	Dictionary content;
	content["generation"] = int64_t(snapshot.generation);
	content["nextSequence"] = int64_t(next_sequence);
	content["storeHighWatermark"] = int64_t(snapshot.next_sequence - 1);
	content["dropped"] = int64_t(snapshot.dropped);
	content["rawCount"] = raw_count;
	content["groupCount"] = groups.size();
	content["truncated"] = truncated;
	content[p_errors_only ? "errors" : "groups"] = results;
	return MCPToolUtils::make_success_result(content);
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPDebugProvider::MCPDebugProvider(MCPDebugEventStore *p_event_store) :
		event_store(p_event_store) {
}

MCPDebugProvider::~MCPDebugProvider() {
	unregister_tools();
}

Error MCPDebugProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry || !event_store) {
		_set_error(r_error, "An MCP tool registry and debug event store are required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Debug tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	Error error = p_registry->register_tool(
			MCPToolUtils::make_tool_definition("godot.debug.get_logs", "Read bounded editor and running-project logs with global deduplication.", _query_schema(false), MCPToolUtils::TOOL_READ_ONLY),
			callable_mp(this, &MCPDebugProvider::get_logs), this, r_error);
	if (error == OK) {
		error = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.debug.get_errors", "Read compressed structured warnings and errors.", _query_schema(true), MCPToolUtils::TOOL_READ_ONLY),
				callable_mp(this, &MCPDebugProvider::get_errors), this, r_error);
	}
	if (error == OK) {
		error = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.debug.get_latest_log", "Read the latest configured project log as a bounded raw tail or compressed summary.", _latest_log_schema(), MCPToolUtils::TOOL_READ_ONLY),
				callable_mp(this, &MCPDebugProvider::get_latest_log), this, r_error);
	}
	if (error == OK) {
		error = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.debug.get_stack", "Read a full error stack or the latest paused running-project stack.", _stack_schema(), MCPToolUtils::TOOL_READ_ONLY),
				callable_mp(this, &MCPDebugProvider::get_stack), this, r_error);
	}
	if (error != OK) {
		p_registry->unregister_tools_for_owner(this);
		return error;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPDebugProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPDebugProvider::get_logs(const Dictionary &p_arguments, const Dictionary &) {
	return _query_result(event_store, p_arguments, false);
}

Dictionary MCPDebugProvider::get_errors(const Dictionary &p_arguments, const Dictionary &) {
	return _query_result(event_store, p_arguments, true);
}

Dictionary MCPDebugProvider::get_latest_log(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed;
	allowed.push_back("maxLines");
	allowed.push_back("view");
	String unknown;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown)) {
		return _invalid_arguments("Unknown argument: " + unknown);
	}
	int64_t max_lines = 200;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("maxLines", 200), 1, 10000, max_lines)) {
		return _invalid_arguments("maxLines must be from 1 to 10000.");
	}
	const Variant view_value = p_arguments.get("view", "summary");
	if (view_value.get_type() != Variant::STRING) {
		return _invalid_arguments("view must be summary or raw.");
	}
	const String view = view_value;
	if (view != "summary" && view != "raw") {
		return _invalid_arguments("view must be summary or raw.");
	}

	MCPProjectLogReader::Result log;
	String read_error;
	const Error error = MCPProjectLogReader::read_latest(int(max_lines), log, &read_error);
	if (error == ERR_FILE_NOT_FOUND || error == ERR_DOES_NOT_EXIST) {
		return MCPToolUtils::make_error_result("LOG_NOT_FOUND", read_error);
	}
	if (error != OK) {
		return MCPToolUtils::make_error_result("LOG_READ_FAILED", read_error);
	}

	Dictionary content;
	content["path"] = log.path.get_file();
	content["view"] = view;
	content["shownLines"] = log.shown_lines;
	content["totalLines"] = log.total_lines;
	content["truncated"] = log.truncated;
	if (view == "raw") {
		content["text"] = log.text;
	} else {
		MCPLogAnalyzer::Options options;
		options.first_line = MAX(1, log.total_lines - log.shown_lines + 1);
		options.total_lines = log.total_lines;
		options.requested_lines = int(max_lines);
		options.truncated = log.truncated;
		content.merge(MCPLogAnalyzer::analyze(log.text, options), true);
	}
	return MCPToolUtils::make_success_result(content);
}

Dictionary MCPDebugProvider::get_stack(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed;
	allowed.push_back("errorId");
	allowed.push_back("debuggerSession");
	allowed.push_back("threadId");
	allowed.push_back("maxFrames");
	String unknown;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown)) {
		return _invalid_arguments("Unknown argument: " + unknown);
	}
	const Variant error_id_value = p_arguments.get("errorId", Variant());
	const bool has_error_id = error_id_value.get_type() != Variant::NIL;
	const bool has_debugger_session = p_arguments.has("debuggerSession");
	if (p_arguments.has("threadId") && !has_debugger_session) {
		return _invalid_arguments("threadId requires debuggerSession.");
	}
	if (has_error_id == has_debugger_session || (has_error_id && (error_id_value.get_type() != Variant::STRING || String(error_id_value).is_empty()))) {
		return _invalid_arguments("Specify exactly one non-empty errorId or debuggerSession.");
	}
	int64_t max_frames = 64;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("maxFrames", 64), 1, 256, max_frames)) {
		return _invalid_arguments("maxFrames must be from 1 to 256.");
	}

	Vector<MCPDebugEventStore::Frame> frames;
	bool truncated = false;
	String kind;
	int debugger_session = -1;
	uint64_t runtime_generation = 0;
	int64_t thread_id = -1;
	String error_id;
	if (has_error_id) {
		error_id = error_id_value;
		const MCPDebugEventStore::Snapshot snapshot = event_store->snapshot();
		for (int i = snapshot.events.size() - 1; i >= 0; i--) {
			const MCPDebugEventStore::Event &event = snapshot.events[i];
			if (_error_id(event) == error_id) {
				frames = event.stack;
				debugger_session = event.debugger_session;
				runtime_generation = event.runtime_generation;
				thread_id = event.thread_id;
				break;
			}
		}
		if (frames.is_empty()) {
			return MCPToolUtils::make_error_result("STACK_NOT_FOUND", "No retained stack is available for errorId: " + error_id);
		}
		kind = "error";
	} else {
		int64_t session = 0;
		if (!MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, session)) {
			return _invalid_arguments("debuggerSession must be a non-negative integer.");
		}
		debugger_session = int(session);
		if (p_arguments.has("threadId") && !MCPToolUtils::try_get_json_integer(p_arguments["threadId"], 0, INT64_MAX, thread_id)) {
			return _invalid_arguments("threadId must be a non-negative integer.");
		}
		MCPDebugEventStore::PausedStack paused_stack;
		const bool found = thread_id >= 0 ? event_store->get_paused_stack(debugger_session, thread_id, paused_stack) : event_store->get_paused_stack(debugger_session, paused_stack);
		if (!found || paused_stack.frames.is_empty()) {
			return MCPToolUtils::make_error_result("STACK_NOT_FOUND", "No paused stack is available for this debugger session.");
		}
		frames = paused_stack.frames;
		runtime_generation = paused_stack.runtime_generation;
		thread_id = paused_stack.thread_id;
		truncated = paused_stack.truncated;
		kind = "paused";
	}

	bool frame_limit_truncated = false;
	Dictionary content;
	content["kind"] = kind;
	content["errorId"] = error_id;
	content["debuggerSession"] = debugger_session;
	content["runtimeGeneration"] = int64_t(runtime_generation);
	content["threadId"] = thread_id >= 0 ? Variant(thread_id) : Variant();
	content["depth"] = frames.size();
	content["frames"] = _frames_to_array(frames, int(max_frames), frame_limit_truncated);
	content["truncated"] = truncated || frame_limit_truncated;
	return MCPToolUtils::make_success_result(content);
}
