/**************************************************************************/
/*  test_mcp_debug_provider.cpp                                           */
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

#include "core/mcp/mcp_tool_registry.h"
#include "editor/mcp/providers/mcp_debug_event_store.h"
#include "editor/mcp/providers/mcp_debug_provider.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_debug_provider);

namespace TestMCPDebugProvider {

static MCPToolRegistry::CallResult _call(MCPToolRegistry &p_registry, const StringName &p_name, const Dictionary &p_arguments) {
	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	return p_registry.call_tool(p_name, p_arguments, context);
}

static Dictionary _success(const MCPToolRegistry::CallResult &p_result) {
	INFO(p_result.result);
	REQUIRE(p_result.status == MCPToolRegistry::CALL_OK);
	REQUIRE_FALSE(bool(p_result.result.get("isError", false)));
	return p_result.result.get("structuredContent", Dictionary());
}

static String _error_code(const MCPToolRegistry::CallResult &p_result) {
	INFO(p_result.result);
	REQUIRE(p_result.status == MCPToolRegistry::CALL_OK);
	REQUIRE(bool(p_result.result.get("isError", false)));
	const Dictionary content = p_result.result.get("structuredContent", Dictionary());
	return Dictionary(content.get("error", Dictionary())).get("code", String());
}

static MCPDebugEventStore::Frame _frame(const String &p_file, const String &p_function, int p_line) {
	MCPDebugEventStore::Frame frame;
	frame.file = p_file;
	frame.function = p_function;
	frame.line = p_line;
	return frame;
}

static MCPDebugEventStore::Event _event(MCPDebugEventStore::Source p_source, MCPDebugEventStore::Severity p_severity,
		const String &p_message, const String &p_file, int p_line, int p_debugger_session = -1) {
	MCPDebugEventStore::Event event;
	event.source = p_source;
	event.severity = p_severity;
	event.message = p_message;
	event.file = p_file;
	event.function = "test_function";
	event.line = p_line;
	event.debugger_session = p_debugger_session;
	return event;
}

TEST_CASE("[MCP][Provider] Debug tools register in order with strict MCP-only schemas") {
	MCPDebugEventStore store;
	MCPToolRegistry registry;
	MCPDebugProvider provider(&store);
	REQUIRE(provider.register_tools(&registry) == OK);
	CHECK(provider.register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(names.size() == 3);
	CHECK(names[0] == "godot.debug.get_logs");
	CHECK(names[1] == "godot.debug.get_errors");
	CHECK(names[2] == "godot.debug.get_stack");
	CHECK(registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI).is_empty());

	const Array definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(definitions.size() == 3);
	for (const Variant &definition_value : definitions) {
		const Dictionary schema = Dictionary(definition_value).get("inputSchema", Dictionary());
		CHECK(schema.get("type", String()) == "object");
		CHECK_FALSE(bool(schema.get("additionalProperties", true)));
	}

	Dictionary invalid;
	invalid["unknown"] = true;
	CHECK(_error_code(_call(registry, "godot.debug.get_logs", invalid)) == "INVALID_ARGUMENTS");
	provider.unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
}

TEST_CASE("[MCP][Provider] Debug logs deduplicate non-adjacent events and apply precise filters") {
	MCPDebugEventStore store;
	const uint64_t first_sequence = store.record(_event(MCPDebugEventStore::SOURCE_EDITOR, MCPDebugEventStore::SEVERITY_INFO,
			"  repeated\r\nmessage  ", "res://source.gd", 10));
	store.record(_event(MCPDebugEventStore::SOURCE_EDITOR, MCPDebugEventStore::SEVERITY_INFO,
			"separator", "res://source.gd", 11));
	const uint64_t last_sequence = store.record(_event(MCPDebugEventStore::SOURCE_EDITOR, MCPDebugEventStore::SEVERITY_INFO,
			"repeated message", "res://source.gd", 10));
	store.record(_event(MCPDebugEventStore::SOURCE_EDITOR, MCPDebugEventStore::SEVERITY_WARNING,
			"repeated message", "res://source.gd", 10));
	store.record(_event(MCPDebugEventStore::SOURCE_RUNTIME, MCPDebugEventStore::SEVERITY_ERROR,
			"runtime boom", "res://runtime.gd", 20, 3));

	MCPToolRegistry registry;
	MCPDebugProvider provider(&store);
	REQUIRE(provider.register_tools(&registry) == OK);

	Dictionary content = _success(_call(registry, "godot.debug.get_logs", Dictionary()));
	CHECK(int64_t(content.get("rawCount", 0)) == 5);
	CHECK(int64_t(content.get("groupCount", 0)) == 4);
	const Array groups = content.get("groups", Array());
	REQUIRE(groups.size() == 4);
	const Dictionary repeated = groups[0];
	CHECK(repeated.get("message", String()) == "repeated message");
	CHECK(int64_t(repeated.get("count", 0)) == 2);
	CHECK(int64_t(repeated.get("firstSequence", 0)) == int64_t(first_sequence));
	CHECK(int64_t(repeated.get("lastSequence", 0)) == int64_t(last_sequence));

	Dictionary filtered;
	filtered["source"] = "runtime";
	filtered["query"] = "BOOM";
	filtered["debuggerSession"] = 3;
	Array levels;
	levels.push_back("error");
	filtered["levels"] = levels;
	content = _success(_call(registry, "godot.debug.get_logs", filtered));
	CHECK(int64_t(content.get("rawCount", 0)) == 1);
	const Array runtime_groups = content.get("groups", Array());
	REQUIRE(runtime_groups.size() == 1);
	CHECK(Dictionary(runtime_groups[0]).get("source", String()) == "runtime");
	CHECK(Dictionary(runtime_groups[0]).get("level", String()) == "error");

	Dictionary after_first;
	after_first["sinceSequence"] = int64_t(first_sequence);
	after_first["deduplicate"] = false;
	after_first["limit"] = 2;
	content = _success(_call(registry, "godot.debug.get_logs", after_first));
	CHECK(int64_t(content.get("rawCount", 0)) == 2);
	CHECK(int64_t(content.get("groupCount", 0)) == 2);
	CHECK(Array(content.get("groups", Array())).size() == 2);
	CHECK(bool(content.get("truncated", false)));
	CHECK(int64_t(content.get("nextSequence", 0)) == int64_t(last_sequence));
	CHECK(int64_t(content.get("storeHighWatermark", 0)) == 5);

	Dictionary invalid_levels;
	Array bad_levels;
	bad_levels.push_back("fatal");
	invalid_levels["levels"] = bad_levels;
	CHECK(_error_code(_call(registry, "godot.debug.get_logs", invalid_levels)) == "INVALID_ARGUMENTS");
}

TEST_CASE("[MCP][Provider] Debug errors expose stable IDs and retained error or paused stacks") {
	MCPDebugEventStore store;
	store.record(_event(MCPDebugEventStore::SOURCE_RUNTIME, MCPDebugEventStore::SEVERITY_WARNING,
			"runtime warning", "res://runtime.gd", 7, 5));
	MCPDebugEventStore::Event error = _event(MCPDebugEventStore::SOURCE_RUNTIME, MCPDebugEventStore::SEVERITY_ERROR,
			"runtime failure", "res://runtime.gd", 12, 5);
	error.expression = "value != null";
	error.description = "The runtime value was null.";
	error.stack.push_back(_frame("res://runtime.gd", "inner", 12));
	error.stack.push_back(_frame("res://runtime.gd", "outer", 30));
	error.stack.push_back(_frame("res://main.gd", "_ready", 4));
	store.record(error);

	Vector<MCPDebugEventStore::Frame> paused_frames;
	paused_frames.push_back(_frame("res://pause.gd", "leaf", 2));
	paused_frames.push_back(_frame("res://pause.gd", "caller", 9));
	paused_frames.push_back(_frame("res://main.gd", "_process", 20));
	store.set_paused_stack(8, paused_frames);

	MCPToolRegistry registry;
	MCPDebugProvider provider(&store);
	REQUIRE(provider.register_tools(&registry) == OK);

	Dictionary error_arguments;
	error_arguments["source"] = "runtime";
	error_arguments["includeWarnings"] = false;
	Dictionary content = _success(_call(registry, "godot.debug.get_errors", error_arguments));
	CHECK(int64_t(content.get("rawCount", 0)) == 1);
	const Array errors = content.get("errors", Array());
	REQUIRE(errors.size() == 1);
	const Dictionary returned_error = errors[0];
	const String error_id = returned_error.get("errorId", String());
	CHECK_FALSE(error_id.is_empty());
	CHECK(bool(returned_error.get("stackAvailable", false)));
	CHECK(int(returned_error.get("stackDepth", 0)) == 3);

	const Dictionary repeated_content = _success(_call(registry, "godot.debug.get_errors", error_arguments));
	const Array repeated_errors = repeated_content.get("errors", Array());
	REQUIRE(repeated_errors.size() == 1);
	CHECK(Dictionary(repeated_errors[0]).get("errorId", String()) == error_id);

	Dictionary stack_arguments;
	stack_arguments["errorId"] = error_id;
	stack_arguments["maxFrames"] = 2;
	content = _success(_call(registry, "godot.debug.get_stack", stack_arguments));
	CHECK(content.get("kind", String()) == "error");
	CHECK(content.get("errorId", String()) == error_id);
	CHECK(int(content.get("debuggerSession", -1)) == 5);
	CHECK(int(content.get("depth", 0)) == 3);
	CHECK(bool(content.get("truncated", false)));
	const Array error_frames = content.get("frames", Array());
	REQUIRE(error_frames.size() == 2);
	CHECK(Dictionary(error_frames[0]).get("function", String()) == "inner");
	CHECK(Dictionary(error_frames[1]).get("function", String()) == "outer");

	Dictionary paused_arguments;
	paused_arguments["debuggerSession"] = 8;
	paused_arguments["maxFrames"] = 2;
	content = _success(_call(registry, "godot.debug.get_stack", paused_arguments));
	CHECK(content.get("kind", String()) == "paused");
	CHECK(String(content.get("errorId", String())).is_empty());
	CHECK(int(content.get("depth", 0)) == 3);
	CHECK(bool(content.get("truncated", false)));
	const Array returned_paused_frames = content.get("frames", Array());
	REQUIRE(returned_paused_frames.size() == 2);
	CHECK(Dictionary(returned_paused_frames[0]).get("function", String()) == "leaf");
	CHECK(Dictionary(returned_paused_frames[1]).get("function", String()) == "caller");

	Dictionary missing;
	missing["errorId"] = "debug-missing";
	CHECK(_error_code(_call(registry, "godot.debug.get_stack", missing)) == "STACK_NOT_FOUND");
	CHECK(_error_code(_call(registry, "godot.debug.get_stack", Dictionary())) == "INVALID_ARGUMENTS");
	Dictionary ambiguous;
	ambiguous["errorId"] = error_id;
	ambiguous["debuggerSession"] = 8;
	CHECK(_error_code(_call(registry, "godot.debug.get_stack", ambiguous)) == "INVALID_ARGUMENTS");
	Dictionary invalid_max;
	invalid_max["errorId"] = error_id;
	invalid_max["maxFrames"] = 0;
	CHECK(_error_code(_call(registry, "godot.debug.get_stack", invalid_max)) == "INVALID_ARGUMENTS");
}

} // namespace TestMCPDebugProvider
