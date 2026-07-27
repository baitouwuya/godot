/**************************************************************************/
/*  mcp_debug_stack_query.cpp                                            */
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

#include "mcp_debug_stack_query.h"

#include "mcp_debug_event_query.h"
#include "mcp_debug_event_store.h"
#include "mcp_tool_utils.h"

namespace MCPDebugStackQuery {

Dictionary execute(MCPDebugEventStore *p_store, const Dictionary &p_arguments) {
	ERR_FAIL_NULL_V(p_store, MCPToolUtils::make_error_result("DEBUG_UNAVAILABLE", "A debug event store is required."));

	const Variant error_id_value = p_arguments.get("errorId", Variant());
	const bool has_error_id = error_id_value.get_type() != Variant::NIL;
	const bool has_debugger_session = p_arguments.has("debuggerSession");
	if (p_arguments.has("threadId") && !has_debugger_session) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "threadId requires debuggerSession.");
	}
	if (has_error_id == has_debugger_session || (has_error_id && (error_id_value.get_type() != Variant::STRING || String(error_id_value).is_empty()))) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Specify exactly one non-empty errorId or debuggerSession.");
	}
	int64_t max_frames = 64;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("maxFrames", 64), 1, 256, max_frames)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "maxFrames must be from 1 to 256.");
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
		const MCPDebugEventStore::Snapshot snapshot = p_store->snapshot();
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
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "debuggerSession must be a non-negative integer.");
		}
		debugger_session = int(session);
		if (p_arguments.has("threadId") && !MCPToolUtils::try_get_json_integer(p_arguments["threadId"], 0, INT64_MAX, thread_id)) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "threadId must be a non-negative integer.");
		}
		MCPDebugEventStore::PausedStack paused_stack;
		const bool found = thread_id >= 0 ? p_store->get_paused_stack(debugger_session, thread_id, paused_stack) : p_store->get_paused_stack(debugger_session, paused_stack);
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

} // namespace MCPDebugStackQuery
