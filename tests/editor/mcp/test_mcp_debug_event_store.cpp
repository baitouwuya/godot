/**************************************************************************/
/*  test_mcp_debug_event_store.cpp                                        */
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

#include "editor/mcp/providers/mcp_debug_event_store.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_debug_event_store);

namespace TestMCPDebugEventStore {

static MCPDebugEventStore::Event _event(const String &p_message) {
	MCPDebugEventStore::Event event;
	event.message = p_message;
	return event;
}

static MCPDebugEventStore::Frame _frame(const String &p_file, const String &p_function, int p_line) {
	MCPDebugEventStore::Frame frame;
	frame.file = p_file;
	frame.function = p_function;
	frame.line = p_line;
	return frame;
}

TEST_CASE("[MCP][Debug] Event store remains bounded and keeps monotonic sequences across clear") {
	MCPDebugEventStore store(3, UINT64_MAX, 1024);

	for (int i = 1; i <= 5; i++) {
		CHECK(store.record(_event("event " + itos(i))) == uint64_t(i));
	}

	MCPDebugEventStore::Snapshot snapshot = store.snapshot();
	CHECK(snapshot.generation == 1);
	CHECK(snapshot.next_sequence == 6);
	CHECK(snapshot.dropped == 2);
	REQUIRE(snapshot.events.size() == 3);
	CHECK(snapshot.events[0].sequence == 3);
	CHECK(snapshot.events[0].message == "event 3");
	CHECK(snapshot.events[2].sequence == 5);
	CHECK(snapshot.events[2].message == "event 5");
	CHECK(snapshot.stored_bytes > 0);

	Vector<MCPDebugEventStore::Frame> paused_frames;
	paused_frames.push_back(_frame("res://paused.gd", "paused", 8));
	store.set_paused_stack(4, paused_frames);
	store.clear();

	snapshot = store.snapshot();
	CHECK(snapshot.generation == 2);
	CHECK(snapshot.next_sequence == 6);
	CHECK(snapshot.dropped == 0);
	CHECK(snapshot.stored_bytes == 0);
	CHECK(snapshot.events.is_empty());
	CHECK_FALSE(store.get_paused_stack(4, paused_frames));
	CHECK(store.record(_event("after clear")) == 6);
}

TEST_CASE("[MCP][Debug] Event and paused stack fields are truncated without losing structure") {
	MCPDebugEventStore store(8, UINT64_MAX, 5);
	MCPDebugEventStore::Event event;
	event.category = "category";
	event.message = "message";
	event.expression = "expression";
	event.description = "description";
	event.file = "res://long_file.gd";
	event.function = "long_function";
	event.line = 12;
	event.stack.push_back(_frame("res://stack_file.gd", "stack_function", 20));
	store.record(event);

	const MCPDebugEventStore::Snapshot snapshot = store.snapshot();
	REQUIRE(snapshot.events.size() == 1);
	const MCPDebugEventStore::Event &stored = snapshot.events[0];
	CHECK(stored.truncated);
	CHECK(stored.category == "categ");
	CHECK(stored.message == "messa");
	CHECK(stored.expression == "expre");
	CHECK(stored.description == "descr");
	CHECK(stored.file == "res:/");
	CHECK(stored.function == "long_");
	CHECK(stored.line == 12);
	REQUIRE(stored.stack.size() == 1);
	CHECK(stored.stack[0].file == "res:/");
	CHECK(stored.stack[0].function == "stack");
	CHECK(stored.stack[0].line == 20);

	Vector<MCPDebugEventStore::Frame> paused_frames;
	paused_frames.push_back(_frame("res://paused_file.gd", "paused_function", 30));
	store.set_paused_stack(2, paused_frames);
	MCPDebugEventStore::PausedStack paused;
	REQUIRE(store.get_paused_stack(2, paused));
	CHECK(paused.debugger_session == 2);
	CHECK(paused.truncated);
	REQUIRE(paused.frames.size() == 1);
	CHECK(paused.frames[0].file == "res:/");
	CHECK(paused.frames[0].function == "pause");
	CHECK(paused.frames[0].line == 30);
}

TEST_CASE("[MCP][Debug] Byte limits evict complete events and report drops") {
	MCPDebugEventStore store(8, 1, 1024);
	store.record(_event("cannot fit"));

	const MCPDebugEventStore::Snapshot snapshot = store.snapshot();
	CHECK(snapshot.events.is_empty());
	CHECK(snapshot.dropped == 1);
	CHECK(snapshot.stored_bytes == 0);
	CHECK(snapshot.next_sequence == 2);
}

} // namespace TestMCPDebugEventStore
