/**************************************************************************/
/*  test_mcp_runtime_debugger_wait.cpp                                    */
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

#include "editor/mcp/providers/mcp_runtime_debugger_wait.h"
#include "editor/mcp/providers/mcp_runtime_request_broker.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_runtime_debugger_wait);

namespace TestMCPRuntimeDebuggerWait {

class FakeDebugger {
public:
	bool connected = true;
	bool complete_after_poll = false;
	bool disconnect_after_poll = false;
	bool stale_after_poll = false;
	bool completed = false;
	bool stale = false;
	int poll_count = 0;

	bool is_session_active() const {
		return connected;
	}

	int poll_peer_messages(uint64_t) {
		poll_count++;
		completed = complete_after_poll;
		stale = stale_after_poll;
		if (disconnect_after_poll) {
			connected = false;
		}
		return 0;
	}
};

TEST_CASE("[MCP][Provider] Runtime debugger wait reports terminal states without polling") {
	CHECK(MCPRuntimeDebuggerWait::is_expired(0));
	CHECK_FALSE(MCPRuntimeDebuggerWait::is_expired(MCPRuntimeDebuggerWait::deadline_from_timeout_msec(100)));

	FakeDebugger debugger;
	debugger.completed = true;
	CHECK(MCPRuntimeDebuggerWait::wait_until(&debugger, 0, [&]() { return debugger.completed; }) ==
			MCPRuntimeDebuggerWait::WAIT_COMPLETED);
	CHECK(debugger.poll_count == 0);

	debugger.completed = false;
	debugger.stale = true;
	CHECK(MCPRuntimeDebuggerWait::wait_until(&debugger, 0, [&]() { return debugger.completed; }, [&]() { return debugger.stale; }) == MCPRuntimeDebuggerWait::WAIT_STALE);
	CHECK(debugger.poll_count == 0);

	debugger.stale = false;
	debugger.connected = false;
	CHECK(MCPRuntimeDebuggerWait::wait_until(&debugger, 0, [&]() { return debugger.completed; }) ==
			MCPRuntimeDebuggerWait::WAIT_DISCONNECTED);
	CHECK(debugger.poll_count == 0);

	debugger.connected = true;
	CHECK(MCPRuntimeDebuggerWait::wait_until(&debugger, 0, [&]() { return debugger.completed; }) ==
			MCPRuntimeDebuggerWait::WAIT_TIMEOUT);
	CHECK(debugger.poll_count == 0);
}

TEST_CASE("[MCP][Provider] Runtime debugger wait observes state changes produced by polling") {
	FakeDebugger debugger;
	debugger.complete_after_poll = true;
	CHECK(MCPRuntimeDebuggerWait::wait_until(&debugger, MCPRuntimeDebuggerWait::deadline_from_timeout_msec(100),
				  [&]() { return debugger.completed; }) == MCPRuntimeDebuggerWait::WAIT_COMPLETED);
	CHECK(debugger.poll_count == 1);

	debugger = FakeDebugger();
	debugger.stale_after_poll = true;
	CHECK(MCPRuntimeDebuggerWait::wait_until(&debugger, MCPRuntimeDebuggerWait::deadline_from_timeout_msec(100), [&]() { return debugger.completed; }, [&]() { return debugger.stale; }) == MCPRuntimeDebuggerWait::WAIT_STALE);
	CHECK(debugger.poll_count == 1);

	debugger = FakeDebugger();
	debugger.disconnect_after_poll = true;
	CHECK(MCPRuntimeDebuggerWait::wait_until(&debugger, MCPRuntimeDebuggerWait::deadline_from_timeout_msec(100),
				  [&]() { return debugger.completed; }) == MCPRuntimeDebuggerWait::WAIT_DISCONNECTED);
	CHECK(debugger.poll_count == 1);
}

TEST_CASE("[MCP][Provider] Runtime debugger wait shares one absolute deadline across stages") {
	const uint64_t deadline = MCPRuntimeDebuggerWait::deadline_from_timeout_msec(10);
	FakeDebugger first_stage;
	first_stage.complete_after_poll = true;
	CHECK(MCPRuntimeDebuggerWait::wait_until(&first_stage, deadline, [&]() { return first_stage.completed; }) ==
			MCPRuntimeDebuggerWait::WAIT_COMPLETED);

	FakeDebugger second_stage;
	CHECK(MCPRuntimeDebuggerWait::wait_until(&second_stage, deadline, [&]() { return second_stage.completed; }) ==
			MCPRuntimeDebuggerWait::WAIT_TIMEOUT);
	CHECK(MCPRuntimeDebuggerWait::is_expired(deadline));
}

TEST_CASE("[MCP][Provider] Runtime request broker isolates debugger sessions and rejects duplicate responses") {
	MCPRuntimeRequestBroker broker;
	REQUIRE(broker.register_request(1, "shared", "query_nodes", "client-a"));
	REQUIRE(broker.register_request(2, "shared", "query_nodes", "client-b"));

	Dictionary data;
	data["count"] = 1;
	CHECK(broker.handle_response(1, Array{ "shared", "query_nodes", true, String(), String(), data }));
	CHECK_FALSE(broker.handle_response(1, Array{ "shared", "query_nodes", true, String(), String(), data }));

	MCPRuntimeRequestBroker::Response response;
	CHECK_FALSE(broker.take_response(2, "shared", response));
	REQUIRE(broker.take_response(1, "shared", response));
	CHECK(response.ok);
	CHECK(int(response.data["count"]) == 1);
	CHECK(broker.has_request(2, "shared"));
}

TEST_CASE("[MCP][Provider] Runtime request broker preserves pending work after malformed responses") {
	MCPRuntimeRequestBroker broker;
	REQUIRE(broker.register_request(3, "request", "get_screenshot"));
	CHECK_FALSE(broker.handle_response(3, Array{ "request", "get_screenshot", true }));
	CHECK_FALSE(broker.handle_response(3, Array{ "request", "other_operation", true, String(), String(), Dictionary() }));
	CHECK(broker.has_request(3, "request"));
}

TEST_CASE("[MCP][Provider] Runtime request broker clears buffered responses by owner and disconnect") {
	MCPRuntimeRequestBroker broker;
	REQUIRE(broker.register_request(0, "owned", "query_nodes", "client-a"));
	REQUIRE(broker.handle_response(0, Array{ "owned", "query_nodes", true, String(), String(), Dictionary() }));
	broker.release_session("client-a");
	CHECK_FALSE(broker.has_request(0, "owned"));

	REQUIRE(broker.register_request(0, "disconnected", "query_nodes", "client-b"));
	MCPRuntimeRequestBroker::Response response;
	CHECK(broker.wait_for_response(nullptr, 0, "disconnected", 100, response) ==
			MCPRuntimeRequestBroker::WAIT_DISCONNECTED);
	CHECK_FALSE(broker.has_request(0, "disconnected"));
}

} // namespace TestMCPRuntimeDebuggerWait
