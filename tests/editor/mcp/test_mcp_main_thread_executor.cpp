/**************************************************************************/
/*  test_mcp_main_thread_executor.cpp                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "core/object/callable_mp.h"
#include "editor/mcp/mcp_main_thread_executor.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_main_thread_executor)

namespace TestMCPMainThreadExecutor {

int add(int p_left, int p_right) {
	return p_left + p_right;
}

Array arguments(int p_left, int p_right) {
	Array result;
	result.push_back(p_left);
	result.push_back(p_right);
	return result;
}

TEST_CASE("[MCP] Main-thread executor applies bounded backpressure") {
	MCPMainThreadExecutor executor(1);
	Ref<MCPMainThreadTask> first;
	Ref<MCPMainThreadTask> second;
	CHECK(executor.submit(callable_mp_static(add), arguments(2, 3), first) == OK);
	CHECK(executor.submit(callable_mp_static(add), arguments(4, 5), second) == ERR_BUSY);
	CHECK(executor.get_pending_count() == 1);
	CHECK(executor.poll() == 1);
	CHECK(first->get_status() == MCPMainThreadTask::STATUS_COMPLETED);
	CHECK(int(first->get_result()) == 5);
}

TEST_CASE("[MCP] Main-thread executor cancels queued work on shutdown") {
	MCPMainThreadExecutor executor;
	Ref<MCPMainThreadTask> task;
	CHECK(executor.submit(callable_mp_static(add), arguments(2, 3), task) == OK);
	executor.shutdown();
	CHECK(task->get_status() == MCPMainThreadTask::STATUS_CANCELED);
	CHECK(!executor.is_accepting());
	CHECK(executor.get_pending_count() == 0);
	Ref<MCPMainThreadTask> rejected;
	CHECK(executor.submit(callable_mp_static(add), arguments(1, 1), rejected) == ERR_UNCONFIGURED);
	CHECK(executor.reset() == OK);
	CHECK(executor.is_accepting());
}

TEST_CASE("[MCP] Main-thread executor supports explicit cancellation") {
	MCPMainThreadExecutor executor;
	Ref<MCPMainThreadTask> task;
	CHECK(executor.submit(callable_mp_static(add), arguments(2, 3), task) == OK);
	CHECK(task->cancel());
	CHECK(executor.poll() == 1);
	CHECK(task->get_status() == MCPMainThreadTask::STATUS_CANCELED);
}

} // namespace TestMCPMainThreadExecutor
