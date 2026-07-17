/**************************************************************************/
/*  test_mcp_runtime_performance_sampler.cpp                              */
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

#include "scene/debugger/mcp_runtime_performance_sampler.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_runtime_performance_sampler);

struct MCPRuntimePerformanceSamplerTestAccess {
	static Error create(MCPRuntimePerformanceSampler &p_sampler, const Dictionary &p_payload, String &r_code, String &r_error) {
		return p_sampler._create_job(p_payload, r_code, r_error);
	}

	static void push(MCPRuntimePerformanceSampler &p_sampler, uint64_t p_frame, const Dictionary &p_frame_metrics,
			const Dictionary &p_builtin_monitors, const Dictionary &p_custom_monitors) {
		p_sampler._push_sample_to_jobs(p_frame, p_frame_metrics, p_builtin_monitors, p_custom_monitors);
	}

	static Dictionary state(const MCPRuntimePerformanceSampler &p_sampler, const String &p_job_id) {
		const MCPRuntimePerformanceSampler::Job *job = p_sampler.jobs.getptr(p_job_id);
		return job ? p_sampler._job_state(*job, true) : Dictionary();
	}

	static int max_active_jobs() { return MCPRuntimePerformanceSampler::MAX_ACTIVE_JOBS; }
	static int max_session_jobs() { return MCPRuntimePerformanceSampler::MAX_SESSION_JOBS; }
	static int max_frames() { return MCPRuntimePerformanceSampler::MAX_FRAMES; }
	static int max_custom_monitors() { return MCPRuntimePerformanceSampler::MAX_CUSTOM_MONITORS; }
};

namespace TestMCPRuntimePerformanceSampler {

Dictionary _job(const String &p_id, const String &p_session, int p_top_frames = 10, int p_max_frames = 0) {
	Dictionary payload;
	payload["jobId"] = p_id;
	payload["mcpSessionId"] = p_session;
	payload["name"] = p_id;
	payload["topFrames"] = p_top_frames;
	payload["maxFrames"] = p_max_frames;
	return payload;
}

TEST_CASE("[MCP][Runtime Performance] Shared frame samples feed bounded aggregators") {
	MCPRuntimePerformanceSampler sampler;
	String code;
	String error;
	REQUIRE(MCPRuntimePerformanceSamplerTestAccess::create(sampler, _job("one", "client", 1, 1), code, error) == OK);
	REQUIRE(MCPRuntimePerformanceSamplerTestAccess::create(sampler, _job("two", "client", 0, 0), code, error) == OK);
	CHECK(MCPRuntimePerformanceSamplerTestAccess::create(sampler, _job("three", "client"), code, error) == ERR_OUT_OF_MEMORY);
	CHECK(code == "PERFORMANCE_JOB_LIMIT");

	Dictionary frame_metrics;
	frame_metrics["frame_time_usec"] = 20000.0;
	frame_metrics["fps"] = 50.0;
	Dictionary builtin;
	builtin["memory/static"] = 1024.0;
	Dictionary custom;
	custom["game/enemies"] = 3;
	MCPRuntimePerformanceSamplerTestAccess::push(sampler, 100, frame_metrics, builtin, custom);

	const Dictionary first = MCPRuntimePerformanceSamplerTestAccess::state(sampler, "one");
	const Dictionary second = MCPRuntimePerformanceSamplerTestAccess::state(sampler, "two");
	CHECK(first.get("state", String()) == "completed");
	CHECK(second.get("state", String()) == "running");
	CHECK(int64_t(first.get("capturedFrames", 0)) == 1);
	CHECK(int64_t(second.get("capturedFrames", 0)) == 1);
	const Dictionary first_summary = first.get("summary", Dictionary());
	const Array slow_frames = first_summary.get("slowFrames", Array());
	REQUIRE(slow_frames.size() == 1);
	const Dictionary slow_frame = slow_frames[0];
	CHECK_FALSE(slow_frame.has("builtinMonitors"));
	CHECK_FALSE(slow_frame.has("customMonitors"));
	const Dictionary summary = second.get("summary", Dictionary());
	CHECK(Dictionary(summary.get("frameMetrics", Dictionary())).has("fps"));
	CHECK(Dictionary(summary.get("builtinMonitorMetrics", Dictionary())).has("memory/static"));
	CHECK(Dictionary(summary.get("customMonitorMetrics", Dictionary())).has("game/enemies"));

	CHECK(MCPRuntimePerformanceSamplerTestAccess::max_active_jobs() == 8);
	CHECK(MCPRuntimePerformanceSamplerTestAccess::max_session_jobs() == 2);
	CHECK(MCPRuntimePerformanceSamplerTestAccess::max_frames() == 36000);
	CHECK(MCPRuntimePerformanceSamplerTestAccess::max_custom_monitors() == 256);
}

TEST_CASE("[MCP][Runtime Performance] Recording parameters are strict") {
	MCPRuntimePerformanceSampler sampler;
	String code;
	String error;
	Dictionary invalid = _job("invalid", "client");
	invalid["topFrames"] = 101;
	CHECK(MCPRuntimePerformanceSamplerTestAccess::create(sampler, invalid, code, error) == ERR_INVALID_PARAMETER);
	CHECK(code == "INVALID_ARGUMENTS");

	invalid = _job("invalid", "client");
	invalid["maxFrames"] = 36001;
	CHECK(MCPRuntimePerformanceSamplerTestAccess::create(sampler, invalid, code, error) == ERR_INVALID_PARAMETER);
	invalid = _job("invalid", "client");
	invalid["unknown"] = true;
	CHECK(MCPRuntimePerformanceSamplerTestAccess::create(sampler, invalid, code, error) == ERR_INVALID_PARAMETER);
}

} // namespace TestMCPRuntimePerformanceSampler
