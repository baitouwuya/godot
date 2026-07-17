/**************************************************************************/
/*  test_mcp_harness_plan_compiler.cpp                                   */
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

#include "editor/mcp/providers/mcp_harness_plan_compiler.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_harness_plan_compiler);

namespace TestMCPHarnessPlanCompiler {

static Dictionary _compile(const Array &p_steps) {
	Dictionary script;
	script["name"] = "test_harness";
	script["steps"] = p_steps;
	Dictionary plan;
	String error;
	REQUIRE(MCPHarnessPlanCompiler::compile(script, plan, &error) == OK);
	INFO(error);
	return plan;
}

TEST_CASE("[MCP][Harness] Compiles runtime commands to current MCP tools") {
	Dictionary viewport;
	viewport["cmd"] = "get_viewport_summary";
	viewport["includeCounts"] = false;
	Dictionary query;
	query["cmd"] = "query_nodes";
	query["filter"] = Dictionary{ { "name", "Player" } };
	query["maxResults"] = 3;
	const Dictionary plan = _compile(Array{ viewport, query });
	CHECK(int(plan.get("stepCount", 0)) == 2);
	CHECK(int(plan.get("operationCount", 0)) == 2);
	const Array steps = plan.get("steps", Array());
	REQUIRE(steps.size() == 2);
	CHECK(Dictionary(steps[0]).get("tool", String()) == "godot.runtime.get_viewport_summary");
	CHECK_FALSE(bool(Dictionary(Dictionary(steps[0]).get("arguments", Dictionary())).get("includeCounts", true)));
	CHECK(Dictionary(steps[1]).get("tool", String()) == "godot.runtime.query_nodes");
	CHECK(int(Dictionary(Dictionary(steps[1]).get("arguments", Dictionary())).get("maxResults", 0)) == 3);
}

TEST_CASE("[MCP][Harness] Expands action and mouse aliases into MCP input plans") {
	Dictionary tap;
	tap["cmd"] = "tap_action";
	tap["action"] = "jump";
	tap["frames"] = 2;
	Dictionary hold;
	hold["cmd"] = "hold_action";
	hold["action"] = "move_forward";
	hold["frames"] = 120;
	Dictionary look;
	look["cmd"] = "mouse_look";
	look["relativeX"] = 12.5;
	look["relativeY"] = -4.0;
	const Dictionary plan = _compile(Array{ tap, hold, look });
	CHECK(int(plan.get("operationCount", 0)) == 7);
	const Array steps = plan.get("steps", Array());
	REQUIRE(steps.size() == 3);
	for (int i = 0; i < 2; i++) {
		const Dictionary step = steps[i];
		CHECK(step.get("tool", String()) == "godot.runtime.input.sequence");
		CHECK(PackedStringArray(step.get("contextRequirements", PackedStringArray())).has("runtimeGeneration"));
		const Array sequence = Dictionary(step.get("arguments", Dictionary())).get("steps", Array());
		REQUIRE(sequence.size() == 3);
		CHECK(Dictionary(sequence[0]).has("event"));
		CHECK(Dictionary(sequence[1]).has("waitFrames"));
		CHECK(Dictionary(sequence[2]).has("event"));
	}
	const Dictionary look_step = steps[2];
	CHECK(look_step.get("tool", String()) == "godot.runtime.input.send");
	const Array events = Dictionary(look_step.get("arguments", Dictionary())).get("events", Array());
	REQUIRE(events.size() == 1);
	const Array relative = Dictionary(events[0]).get("relative", Array());
	REQUIRE(relative.size() == 2);
	CHECK(double(relative[0]) == doctest::Approx(12.5));
	CHECK(double(relative[1]) == doctest::Approx(-4.0));
}

TEST_CASE("[MCP][Harness] Compiles expectation aliases with polling assertions") {
	Dictionary visible;
	visible["expect"] = "node_visible";
	visible["selector"] = Dictionary{ { "name", "Reactor" } };
	visible["timeoutFrames"] = 600;
	visible["pollEveryFrames"] = 3;
	Dictionary gone;
	gone["expect"] = "node_gone";
	gone["selector"] = Dictionary{ { "path", "/root/Main/Menu" } };
	const Dictionary plan = _compile(Array{ visible, gone });
	const Array steps = plan.get("steps", Array());
	const Dictionary visible_step = steps[0];
	CHECK(visible_step.get("kind", String()) == "expect");
	CHECK(visible_step.get("tool", String()) == "godot.runtime.wait.start");
	const Dictionary visible_arguments = visible_step.get("arguments", Dictionary());
	const Dictionary visible_condition = visible_arguments.get("condition", Dictionary());
	CHECK(visible_condition.get("kind", String()) == "property");
	CHECK(visible_condition.get("property", String()) == "visible");
	CHECK(int(visible_arguments.get("timeoutFrames", 0)) == 600);
	const Dictionary visible_assertion = visible_step.get("assertion", Dictionary());
	CHECK(visible_assertion.get("kind", String()) == "property_equals");
	CHECK(visible_assertion.get("property", String()) == "visible");
	CHECK(bool(visible_assertion.get("value", false)));
	CHECK(int(Dictionary(visible_step.get("poll", Dictionary())).get("timeoutFrames", 0)) == 600);
	const Dictionary gone_step = steps[1];
	CHECK(gone_step.get("tool", String()) == "godot.runtime.wait.start");
	CHECK(Dictionary(Dictionary(gone_step.get("arguments", Dictionary())).get("condition", Dictionary())).get("kind", String()) == "node_gone");
	CHECK(Dictionary(gone_step.get("assertion", Dictionary())).get("operator", String()) == "equals");
}

TEST_CASE("[MCP][Harness] Compiles performance intent and rejects CLI startup state") {
	Dictionary script;
	script["name"] = "intent";
	script["steps"] = Array{ Dictionary{ { "cmd", "get_status" } } };
	script["startup"] = Dictionary{ { "perf", true } };
	script["evidence"] = Dictionary{ { "screenshots", "always" }, { "latestLog", true } };
	Dictionary plan;
	String error;
	REQUIRE(MCPHarnessPlanCompiler::compile(script, plan, &error) == OK);
	CHECK(Dictionary(plan.get("evidencePolicy", Dictionary())).get("screenshots", String()) == "always");
	const Array steps = plan.get("steps", Array());
	REQUIRE(steps.size() == 2);
	CHECK(Dictionary(steps[0]).get("tool", String()) == "godot.runtime.performance.start");
	CHECK(PackedStringArray(Dictionary(steps[0]).get("contextRequirements", PackedStringArray())).has("runtimeGeneration"));
	CHECK(Dictionary(Dictionary(steps[0]).get("arguments", Dictionary())).get("name", String()) == "intent");
	script["startup"] = Dictionary{ { "aiAgentPort", 7010 } };
	CHECK(MCPHarnessPlanCompiler::compile(script, plan, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("aiAgentPort"));
	script["startup"] = Dictionary{ { "windowed", true } };
	CHECK(MCPHarnessPlanCompiler::compile(script, plan, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("windowed"));
}

TEST_CASE("[MCP][Harness] Rejects ambiguous, unsupported, and unbounded steps") {
	Dictionary script;
	Dictionary plan;
	String error;
	script["steps"] = Array{ Dictionary{ { "cmd", "get_status" }, { "expect", "node_exists" } } };
	CHECK(MCPHarnessPlanCompiler::compile(script, plan, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("exactly one"));
	script["steps"] = Array{ Dictionary{ { "cmd", "click_target" }, { "selector", Dictionary() } } };
	CHECK(MCPHarnessPlanCompiler::compile(script, plan, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("Unsupported command"));
	script["steps"] = Array{ Dictionary{ { "cmd", "hold_action" }, { "action", "move" }, { "frames", MCPHarnessPlanCompiler::MAX_WAIT_FRAMES + 1 } } };
	CHECK(MCPHarnessPlanCompiler::compile(script, plan, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("frames"));
	Array too_many;
	for (int i = 0; i <= MCPHarnessPlanCompiler::MAX_SCRIPT_STEPS; i++) {
		too_many.push_back(Dictionary{ { "cmd", "get_status" } });
	}
	script["steps"] = too_many;
	CHECK(MCPHarnessPlanCompiler::compile(script, plan, &error) == ERR_INVALID_DATA);
}

TEST_CASE("[MCP][Harness] Enforces recursive text limits") {
	Dictionary script;
	script["steps"] = Array{ Dictionary{ { "cmd", "query_nodes" }, { "filter", Dictionary{ { "text", String("x").repeat(MCPHarnessPlanCompiler::MAX_TEXT_LENGTH + 1) } } } } };
	Dictionary plan;
	String error;
	CHECK(MCPHarnessPlanCompiler::compile(script, plan, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("text"));
}

} // namespace TestMCPHarnessPlanCompiler
