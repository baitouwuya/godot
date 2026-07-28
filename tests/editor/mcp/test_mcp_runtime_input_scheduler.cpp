/**************************************************************************/
/*  test_mcp_runtime_input_scheduler.cpp                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "editor/mcp/providers/mcp_runtime_input_scheduler.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_runtime_input_scheduler);

struct MCPRuntimeInputSchedulerTestAccess {
	static bool retain_sequence(MCPRuntimeInputScheduler &p_scheduler, const String &p_id, const String &p_session_id, const String &p_state) {
		if (!p_scheduler._prepare_sequence_capacity()) {
			return false;
		}
		MCPRuntimeInputScheduler::SequenceRecord record;
		record.mcp_session_id = p_session_id;
		record.state["state"] = p_state;
		p_scheduler.sequences[p_id] = record;
		p_scheduler.sequence_order.push_back(p_id);
		return true;
	}

	static int max_retained_sequences() {
		return MCPRuntimeInputScheduler::MAX_RETAINED_SEQUENCES;
	}

	static int sequence_count(const MCPRuntimeInputScheduler &p_scheduler) {
		return p_scheduler.sequences.size();
	}

	static int sequence_order_count(const MCPRuntimeInputScheduler &p_scheduler) {
		return p_scheduler.sequence_order.size();
	}

	static bool has_sequence(const MCPRuntimeInputScheduler &p_scheduler, const String &p_id) {
		return p_scheduler.sequences.has(p_id);
	}

	static void activate(MCPRuntimeInputScheduler &p_scheduler) {
		p_scheduler.active_epoch = true;
	}

	static bool is_active(const MCPRuntimeInputScheduler &p_scheduler) {
		return p_scheduler.active_epoch;
	}
};

namespace TestMCPRuntimeInputScheduler {

TEST_CASE("[MCP][Provider] Runtime input scheduler bounds retained terminal sequences") {
	MCPRuntimeDebuggerGateway gateway(nullptr);
	MCPRuntimeInputScheduler scheduler(&gateway);
	const int retained_limit = MCPRuntimeInputSchedulerTestAccess::max_retained_sequences();
	for (int i = 0; i < retained_limit * 4; i++) {
		CHECK(MCPRuntimeInputSchedulerTestAccess::retain_sequence(scheduler, "terminal-" + itos(i), "session", "completed"));
		CHECK(MCPRuntimeInputSchedulerTestAccess::sequence_count(scheduler) <= retained_limit);
	}
	CHECK(MCPRuntimeInputSchedulerTestAccess::sequence_count(scheduler) == retained_limit);
	CHECK(MCPRuntimeInputSchedulerTestAccess::sequence_order_count(scheduler) == retained_limit);
	CHECK_FALSE(MCPRuntimeInputSchedulerTestAccess::has_sequence(scheduler, "terminal-0"));
	CHECK(MCPRuntimeInputSchedulerTestAccess::has_sequence(scheduler, "terminal-" + itos(retained_limit * 4 - 1)));
}

TEST_CASE("[MCP][Provider] Runtime input scheduler preserves active sequences at the retention limit") {
	MCPRuntimeDebuggerGateway gateway(nullptr);
	MCPRuntimeInputScheduler scheduler(&gateway);
	const int retained_limit = MCPRuntimeInputSchedulerTestAccess::max_retained_sequences();
	for (int i = 0; i < retained_limit; i++) {
		REQUIRE(MCPRuntimeInputSchedulerTestAccess::retain_sequence(scheduler, "active-" + itos(i), "session", "running"));
	}
	CHECK_FALSE(MCPRuntimeInputSchedulerTestAccess::retain_sequence(scheduler, "overflow", "session", "running"));
	CHECK(MCPRuntimeInputSchedulerTestAccess::sequence_count(scheduler) == retained_limit);
	CHECK(MCPRuntimeInputSchedulerTestAccess::has_sequence(scheduler, "active-0"));
}

TEST_CASE("[MCP][Provider] Runtime input scheduler releases local session and shutdown state") {
	MCPRuntimeDebuggerGateway gateway(nullptr);
	MCPRuntimeInputScheduler scheduler(&gateway);
	REQUIRE(MCPRuntimeInputSchedulerTestAccess::retain_sequence(scheduler, "sequence-a", "session-a", "running"));
	REQUIRE(MCPRuntimeInputSchedulerTestAccess::retain_sequence(scheduler, "sequence-b", "session-b", "running"));

	scheduler.release_mcp_session("session-a");
	CHECK_FALSE(MCPRuntimeInputSchedulerTestAccess::has_sequence(scheduler, "sequence-a"));
	CHECK(MCPRuntimeInputSchedulerTestAccess::has_sequence(scheduler, "sequence-b"));
	CHECK(MCPRuntimeInputSchedulerTestAccess::sequence_order_count(scheduler) == 1);

	MCPRuntimeInputSchedulerTestAccess::activate(scheduler);
	scheduler.release_all();
	CHECK(MCPRuntimeInputSchedulerTestAccess::sequence_count(scheduler) == 0);
	CHECK(MCPRuntimeInputSchedulerTestAccess::sequence_order_count(scheduler) == 0);
	CHECK_FALSE(MCPRuntimeInputSchedulerTestAccess::is_active(scheduler));
	scheduler.release_all();
	CHECK_FALSE(MCPRuntimeInputSchedulerTestAccess::is_active(scheduler));
}

} // namespace TestMCPRuntimeInputScheduler
