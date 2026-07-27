/**************************************************************************/
/*  test_mcp_harness_provider.cpp                                        */
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
#include "core/object/callable_mp.h"
#include "editor/mcp/providers/mcp_harness_provider.h"
#include "editor/mcp/providers/mcp_tool_utils.h"
#include "editor/mcp/providers/mcp_trace_service.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_harness_provider);

namespace TestMCPHarnessProvider {

class HarnessTools : public Object {
public:
	int call_count = 0;
	int sequence_status_count = 0;
	int wait_status_count = 0;
	int cancel_count = 0;
	int performance_start_count = 0;
	int performance_stop_count = 0;
	int latest_log_count = 0;
	bool fail_status = false;

	Dictionary get_state(const Dictionary &, const Dictionary &) {
		call_count++;
		return fail_status ? MCPToolUtils::make_error_result("RUNTIME_GONE", "The running project disconnected.") : MCPToolUtils::make_success_result(Dictionary{ { "running", true } });
	}

	Dictionary sequence_start(const Dictionary &p_arguments, const Dictionary &) {
		call_count++;
		CHECK(p_arguments.has("runtimeGeneration"));
		return MCPToolUtils::make_success_result(Dictionary{ { "sequenceId", "input-test" }, { "state", "running" } });
	}

	Dictionary sequence_status(const Dictionary &, const Dictionary &) {
		call_count++;
		sequence_status_count++;
		return MCPToolUtils::make_success_result(Dictionary{ { "sequenceId", "input-test" }, { "state", sequence_status_count < 2 ? "waiting" : "completed" } });
	}

	Dictionary wait_start(const Dictionary &p_arguments, const Dictionary &) {
		call_count++;
		CHECK(p_arguments.has("runtimeGeneration"));
		CHECK(Dictionary(p_arguments.get("condition", Dictionary())).get("kind", String()) == "node_exists");
		return MCPToolUtils::make_success_result(Dictionary{ { "jobId", "wait-test" }, { "state", "running" } });
	}

	Dictionary wait_status(const Dictionary &, const Dictionary &) {
		call_count++;
		wait_status_count++;
		return MCPToolUtils::make_success_result(Dictionary{ { "jobId", "wait-test" }, { "state", wait_status_count < 2 ? "running" : "succeeded" } });
	}

	Dictionary cancel_child(const Dictionary &, const Dictionary &) {
		call_count++;
		cancel_count++;
		return MCPToolUtils::make_success_result(Dictionary{ { "state", "cancelled" } });
	}

	Dictionary screenshot(const Dictionary &, const Dictionary &) {
		call_count++;
		return MCPToolUtils::make_success_result(Dictionary{ { "path", "harness-failure.png" }, { "temporary", true } });
	}

	Dictionary errors(const Dictionary &p_arguments, const Dictionary &) {
		call_count++;
		CHECK(int(p_arguments.get("limit", 0)) == 20);
		return MCPToolUtils::make_success_result(Dictionary{ { "count", 1 }, { "groups", Array{ Dictionary{ { "message", "failure" } } } } });
	}

	Dictionary performance_start(const Dictionary &, const Dictionary &) {
		performance_start_count++;
		return MCPToolUtils::make_success_result(Dictionary{ { "jobId", "performance-test" }, { "state", "running" } });
	}

	Dictionary performance_stop(const Dictionary &, const Dictionary &) {
		performance_stop_count++;
		return MCPToolUtils::make_success_result(Dictionary{ { "jobId", "performance-test" }, { "state", "stopped" }, { "summary", Dictionary{ { "capturedFrames", 3 } } } });
	}

	Dictionary latest_log(const Dictionary &p_arguments, const Dictionary &) {
		latest_log_count++;
		CHECK(int(p_arguments.get("maxLines", 0)) == 200);
		CHECK(String(p_arguments.get("view", String())) == "summary");
		return MCPToolUtils::make_success_result(Dictionary{ { "view", "summary" }, { "shownLines", 3 } });
	}
};

static Dictionary _definition(const String &p_name) {
	Dictionary definition;
	definition["name"] = p_name;
	return definition;
}

struct Fixture {
	MCPToolRegistry registry;
	MCPHarnessProvider *provider = memnew(MCPHarnessProvider);
	HarnessTools *tools = memnew(HarnessTools);

	Fixture() {
		REQUIRE(provider->register_tools(&registry) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.get_state"), callable_mp(tools, &HarnessTools::get_state), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.input.sequence"), callable_mp(tools, &HarnessTools::sequence_start), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.input.sequence_status"), callable_mp(tools, &HarnessTools::sequence_status), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.input.sequence_cancel"), callable_mp(tools, &HarnessTools::cancel_child), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.wait.start"), callable_mp(tools, &HarnessTools::wait_start), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.wait.status"), callable_mp(tools, &HarnessTools::wait_status), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.wait.cancel"), callable_mp(tools, &HarnessTools::cancel_child), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.get_screenshot"), callable_mp(tools, &HarnessTools::screenshot), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.debug.get_errors"), callable_mp(tools, &HarnessTools::errors), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.performance.start"), callable_mp(tools, &HarnessTools::performance_start), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.runtime.performance.stop"), callable_mp(tools, &HarnessTools::performance_stop), tools) == OK);
		REQUIRE(registry.register_tool(_definition("godot.debug.get_latest_log"), callable_mp(tools, &HarnessTools::latest_log), tools) == OK);
	}

	~Fixture() {
		registry.unregister_tools_for_owner(tools);
		provider->unregister_tools();
		memdelete(tools);
		memdelete(provider);
	}

	MCPToolRegistry::CallResult call(const String &p_name, const Dictionary &p_arguments, const String &p_session = "session-a") {
		MCPToolCallContext context;
		context.session["sessionId"] = p_session;
		return registry.call_tool(p_name, p_arguments, context);
	}
};

static String _error_code(const MCPToolRegistry::CallResult &p_result) {
	const Dictionary structured = p_result.result.get("structuredContent", Dictionary());
	return Dictionary(structured.get("error", Dictionary())).get("code", String());
}

static Dictionary _start_arguments(const Array &p_steps) {
	Dictionary script;
	script["name"] = "provider_test";
	script["steps"] = p_steps;
	Dictionary arguments;
	arguments["script"] = script;
	arguments["debuggerSession"] = 0;
	arguments["runtimeGeneration"] = 7;
	return arguments;
}

static Dictionary _job_arguments(const String &p_job_id, int p_generation = 7) {
	Dictionary arguments;
	arguments["jobId"] = p_job_id;
	arguments["debuggerSession"] = 0;
	arguments["runtimeGeneration"] = p_generation;
	return arguments;
}

TEST_CASE("[MCP][Harness] Provider registers four MCP tools") {
	MCPToolRegistry registry;
	MCPHarnessProvider *provider = memnew(MCPHarnessProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 4);
	CHECK(names[0] == "godot.runtime.harness.start");
	CHECK(names[1] == "godot.runtime.harness.status");
	CHECK(names[2] == "godot.runtime.harness.cancel");
	CHECK(names[3] == "godot.runtime.harness.get_report");
	const Array definitions = registry.get_tool_definitions();
	const Dictionary start_input = Dictionary(definitions[0]).get("inputSchema", Dictionary());
	CHECK(PackedStringArray(start_input.get("required", PackedStringArray())).has("script"));
	CHECK(PackedStringArray(start_input.get("required", PackedStringArray())).has("runtimeGeneration"));
	const Dictionary job_input = Dictionary(definitions[1]).get("inputSchema", Dictionary());
	CHECK(PackedStringArray(job_input.get("required", PackedStringArray())).has("jobId"));
	const Dictionary summary_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK_FALSE(bool(summary_output.get("additionalProperties", true)));
	const Dictionary summary_properties = summary_output.get("properties", Dictionary());
	CHECK(summary_properties.has("jobId"));
	CHECK(summary_properties.has("runtimeGeneration"));
	CHECK(summary_properties.has("completedStepCount"));
	const Dictionary report_output = Dictionary(definitions[3]).get("outputSchema", Dictionary());
	CHECK_FALSE(bool(report_output.get("additionalProperties", true)));
	const Dictionary report_properties = report_output.get("properties", Dictionary());
	CHECK(report_properties.has("steps"));
	CHECK(report_properties.has("assertions"));
	CHECK(report_properties.has("evidence"));
	provider->unregister_tools();
	memdelete(provider);
}

TEST_CASE("[MCP][Harness] Service advances input and wait children without busy waiting") {
	Fixture fixture;
	Dictionary tap{ { "cmd", "tap_action" }, { "action", "jump" }, { "frames", 2 } };
	Dictionary expect{ { "expect", "node_exists" }, { "selector", Dictionary{ { "name", "Target" } } } };
	MCPToolRegistry::CallResult result = fixture.call("godot.runtime.harness.start", _start_arguments(Array{ tap, expect }));
	REQUIRE_FALSE(bool(result.result.get("isError", false)));
	const String job_id = Dictionary(result.result.get("structuredContent", Dictionary())).get("jobId", String());
	REQUIRE_FALSE(job_id.is_empty());
	CHECK(fixture.tools->call_count == 0);

	for (int i = 0; i < 6; i++) {
		const int before = fixture.tools->call_count;
		CHECK(fixture.provider->poll() == 1);
		CHECK(fixture.tools->call_count - before == 1);
	}
	CHECK(fixture.provider->poll() == 0);
	result = fixture.call("godot.runtime.harness.status", _job_arguments(job_id));
	CHECK(Dictionary(result.result.get("structuredContent", Dictionary())).get("state", String()) == "completed");
	result = fixture.call("godot.runtime.harness.get_report", _job_arguments(job_id));
	const Dictionary report = result.result.get("structuredContent", Dictionary());
	CHECK(Array(report.get("steps", Array())).size() == 2);
	const Array assertions = report.get("assertions", Array());
	REQUIRE(assertions.size() == 1);
	CHECK(bool(Dictionary(assertions[0]).get("passed", false)));
}

TEST_CASE("[MCP][Harness] Jobs enforce session limits, ownership, and generation") {
	Fixture fixture;
	const Dictionary arguments = _start_arguments(Array{ Dictionary{ { "cmd", "get_status" } } });
	MCPToolRegistry::CallResult first = fixture.call("godot.runtime.harness.start", arguments);
	MCPToolRegistry::CallResult second = fixture.call("godot.runtime.harness.start", arguments);
	CHECK_FALSE(bool(first.result.get("isError", false)));
	CHECK_FALSE(bool(second.result.get("isError", false)));
	CHECK(_error_code(fixture.call("godot.runtime.harness.start", arguments)) == "HARNESS_SESSION_LIMIT");
	const String job_id = Dictionary(first.result.get("structuredContent", Dictionary())).get("jobId", String());
	CHECK(_error_code(fixture.call("godot.runtime.harness.status", _job_arguments(job_id), "session-b")) == "HARNESS_JOB_NOT_FOUND");
	CHECK(_error_code(fixture.call("godot.runtime.harness.status", _job_arguments(job_id, 8))) == "STALE_HARNESS_JOB");
	fixture.provider->release_session("session-a");
	const Dictionary status = fixture.call("godot.runtime.harness.status", _job_arguments(job_id)).result.get("structuredContent", Dictionary());
	CHECK(status.get("state", String()) == "cancelled");
}

TEST_CASE("[MCP][Harness] Cancel asynchronously releases an active child") {
	Fixture fixture;
	Dictionary tap{ { "cmd", "tap_action" }, { "action", "jump" } };
	MCPToolRegistry::CallResult result = fixture.call("godot.runtime.harness.start", _start_arguments(Array{ tap }));
	const String job_id = Dictionary(result.result.get("structuredContent", Dictionary())).get("jobId", String());
	CHECK(fixture.provider->poll() == 1);
	result = fixture.call("godot.runtime.harness.cancel", _job_arguments(job_id));
	CHECK(Dictionary(result.result.get("structuredContent", Dictionary())).get("state", String()) == "cancelling");
	CHECK(fixture.tools->cancel_count == 0);
	CHECK(fixture.provider->poll() == 1);
	CHECK(fixture.tools->cancel_count == 1);
	CHECK(fixture.provider->poll() == 0);
	result = fixture.call("godot.runtime.harness.status", _job_arguments(job_id));
	CHECK(Dictionary(result.result.get("structuredContent", Dictionary())).get("state", String()) == "cancelled");
}

TEST_CASE("[MCP][Harness] Failure report collects bounded screenshot and error references") {
	Fixture fixture;
	fixture.tools->fail_status = true;
	Dictionary script_step{ { "cmd", "get_status" } };
	Dictionary arguments = _start_arguments(Array{ script_step });
	Dictionary script = arguments["script"];
	script["evidence"] = Dictionary{ { "screenshots", "on_failure" } };
	arguments["script"] = script;
	MCPToolRegistry::CallResult result = fixture.call("godot.runtime.harness.start", arguments);
	const String job_id = Dictionary(result.result.get("structuredContent", Dictionary())).get("jobId", String());
	CHECK(fixture.provider->poll() == 1);
	CHECK(fixture.provider->poll() == 1);
	CHECK(fixture.provider->poll() == 1);
	CHECK(fixture.provider->poll() == 0);
	result = fixture.call("godot.runtime.harness.get_report", _job_arguments(job_id));
	const Dictionary report = result.result.get("structuredContent", Dictionary());
	CHECK(report.get("state", String()) == "failed");
	const Dictionary evidence = report.get("evidence", Dictionary());
	CHECK(bool(Dictionary(evidence.get("screenshot", Dictionary())).get("ok", false)));
	CHECK(bool(Dictionary(evidence.get("errors", Dictionary())).get("ok", false)));
	CHECK(Dictionary(report.get("failure", Dictionary())).get("code", String()) == "RUNTIME_GONE");
}

TEST_CASE("[MCP][Harness] Evidence policy drives performance and latest log through MCP tools") {
	Fixture fixture;
	Dictionary arguments = _start_arguments(Array{ Dictionary{ { "cmd", "get_status" } } });
	Dictionary script = arguments["script"];
	script["evidence"] = Dictionary{ { "screenshots", "off" }, { "latestLog", true }, { "perfSummary", true }, { "trace", false } };
	arguments["script"] = script;
	MCPToolRegistry::CallResult result = fixture.call("godot.runtime.harness.start", arguments);
	const String job_id = Dictionary(result.result.get("structuredContent", Dictionary())).get("jobId", String());
	REQUIRE_FALSE(job_id.is_empty());
	CHECK(fixture.provider->poll() == 1);
	CHECK(fixture.tools->performance_start_count == 1);
	CHECK(fixture.provider->poll() == 1);
	CHECK(fixture.provider->poll() == 1);
	CHECK(fixture.tools->performance_stop_count == 1);
	CHECK(fixture.provider->poll() == 1);
	CHECK(fixture.tools->latest_log_count == 1);
	CHECK(fixture.provider->poll() == 0);
	const Dictionary report = fixture.call("godot.runtime.harness.get_report", _job_arguments(job_id)).result.get("structuredContent", Dictionary());
	CHECK(report.get("state", String()) == "completed");
	const Dictionary evidence = report.get("evidence", Dictionary());
	CHECK(bool(Dictionary(evidence.get("performance", Dictionary())).get("ok", false)));
	CHECK(bool(Dictionary(evidence.get("latestLog", Dictionary())).get("ok", false)));
}

} // namespace TestMCPHarnessProvider
