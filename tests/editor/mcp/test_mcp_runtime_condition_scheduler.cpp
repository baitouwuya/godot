/**************************************************************************/
/*  test_mcp_runtime_condition_scheduler.cpp                              */
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

#include "scene/debugger/mcp_runtime_condition_scheduler.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_runtime_condition_scheduler);

struct MCPRuntimeConditionSchedulerTestAccess {
	static Error prepare(MCPRuntimeConditionScheduler &p_scheduler, const Dictionary &p_condition, String &r_code, String &r_error) {
		MCPRuntimeConditionScheduler::Job job;
		job.condition = p_condition;
		return p_scheduler._prepare_job(job, r_code, r_error);
	}

	static int max_active_jobs() { return MCPRuntimeConditionScheduler::MAX_ACTIVE_JOBS; }
	static int max_session_jobs() { return MCPRuntimeConditionScheduler::MAX_SESSION_JOBS; }
	static int max_timeout_frames() { return MCPRuntimeConditionScheduler::MAX_TIMEOUT_FRAMES; }
	static int max_poll_frames() { return MCPRuntimeConditionScheduler::MAX_POLL_EVERY_FRAMES; }
	static int max_screenshot_jobs() { return MCPRuntimeConditionScheduler::MAX_ACTIVE_SCREENSHOT_JOBS; }
	static uint64_t max_screenshot_bytes() { return MCPRuntimeConditionScheduler::MAX_SCREENSHOT_BYTES; }
	static uint64_t max_baseline_bytes() { return MCPRuntimeConditionScheduler::MAX_TOTAL_BASELINE_BYTES; }
};

namespace TestMCPRuntimeConditionScheduler {

Dictionary _selector_condition(const String &p_kind) {
	Dictionary condition;
	condition["kind"] = p_kind;
	condition["selector"] = Dictionary{ { "name", "Target" } };
	return condition;
}

TEST_CASE("[MCP][Runtime Wait] Condition specifications are strict and bounded") {
	MCPRuntimeConditionScheduler scheduler;
	String code;
	String error;
	CHECK(MCPRuntimeConditionSchedulerTestAccess::prepare(scheduler, _selector_condition("node_exists"), code, error) == OK);
	CHECK(MCPRuntimeConditionSchedulerTestAccess::prepare(scheduler, _selector_condition("node_gone"), code, error) == OK);

	Dictionary property = _selector_condition("property");
	property["property"] = "visible";
	property["equals"] = true;
	CHECK(MCPRuntimeConditionSchedulerTestAccess::prepare(scheduler, property, code, error) == OK);

	Dictionary scene_changed;
	scene_changed["kind"] = "scene_changed";
	CHECK(MCPRuntimeConditionSchedulerTestAccess::prepare(scheduler, scene_changed, code, error) == OK);

	Dictionary invalid = _selector_condition("node_exists");
	invalid["unknown"] = true;
	CHECK(MCPRuntimeConditionSchedulerTestAccess::prepare(scheduler, invalid, code, error) == ERR_INVALID_PARAMETER);
	CHECK(code == "INVALID_CONDITION");

	Dictionary invalid_screenshot;
	invalid_screenshot["kind"] = "screenshot_diff";
	invalid_screenshot["threshold"] = 2.0;
	CHECK(MCPRuntimeConditionSchedulerTestAccess::prepare(scheduler, invalid_screenshot, code, error) == ERR_INVALID_PARAMETER);
	CHECK(error.contains("threshold"));

	CHECK(MCPRuntimeConditionSchedulerTestAccess::max_active_jobs() == 64);
	CHECK(MCPRuntimeConditionSchedulerTestAccess::max_session_jobs() == 16);
	CHECK(MCPRuntimeConditionSchedulerTestAccess::max_timeout_frames() == 36000);
	CHECK(MCPRuntimeConditionSchedulerTestAccess::max_poll_frames() == 600);
	CHECK(MCPRuntimeConditionSchedulerTestAccess::max_screenshot_jobs() == 4);
	CHECK(MCPRuntimeConditionSchedulerTestAccess::max_screenshot_bytes() == 16 * 1024 * 1024);
	CHECK(MCPRuntimeConditionSchedulerTestAccess::max_baseline_bytes() == 64 * 1024 * 1024);
}

} // namespace TestMCPRuntimeConditionScheduler
