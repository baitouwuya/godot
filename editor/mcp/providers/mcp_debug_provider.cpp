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

#include "mcp_debug_event_query.h"
#include "mcp_debug_event_store.h"
#include "mcp_debug_tool_utils.h"
#include "mcp_log_analyzer.h"
#include "mcp_project_log_reader.h"
#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

static Dictionary _invalid_arguments(const String &p_message) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", p_message);
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

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.debug.get_logs", "Read bounded editor and running-project logs with global deduplication.",
				MCPDebugToolUtils::query_schema(false), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPDebugProvider::get_logs), MCPDebugToolUtils::query_output_schema(false) },
		{ "godot.debug.get_errors", "Read compressed structured warnings and errors.",
				MCPDebugToolUtils::query_schema(true), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPDebugProvider::get_errors), MCPDebugToolUtils::query_output_schema(true) },
		{ "godot.debug.get_latest_log", "Read the latest configured project log as a bounded raw tail or compressed summary.",
				MCPDebugToolUtils::latest_log_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPDebugProvider::get_latest_log), MCPDebugToolUtils::latest_log_output_schema() },
		{ "godot.debug.get_stack", "Read a full error stack or the latest paused running-project stack.",
				MCPDebugToolUtils::stack_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPDebugProvider::get_stack), MCPDebugToolUtils::stack_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
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
	return MCPDebugEventQuery::execute(event_store, p_arguments, false);
}

Dictionary MCPDebugProvider::get_errors(const Dictionary &p_arguments, const Dictionary &) {
	return MCPDebugEventQuery::execute(event_store, p_arguments, true);
}

Dictionary MCPDebugProvider::get_latest_log(const Dictionary &p_arguments, const Dictionary &) {
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
			if (MCPDebugEventQuery::make_error_id(event) == error_id) {
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
	content["frames"] = MCPDebugEventQuery::serialize_frames(frames, int(max_frames), frame_limit_truncated);
	content["truncated"] = truncated || frame_limit_truncated;
	return MCPToolUtils::make_success_result(content);
}
