/**************************************************************************/
/*  test_cli_harness_runner.h                                             */
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

#pragma once

#include "main/cli_ai_input_server.h"
#include "main/cli_harness_runner.h"
#include "main/custom_feature_tracer.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "tests/main/test_custom_feature_trace_helpers.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

namespace TestCLIHarnessRunner {

struct ParsedOptionsResult {
	CLIHarnessRunner::Options options;
	String error;
};

static ParsedOptionsResult parse_cli_options(const std::initializer_list<const char *> &p_args) {
	List<String> args;
	for (const char *arg : p_args) {
		args.push_back(String(arg));
	}

	ParsedOptionsResult result;
	for (List<String>::Element *E = args.front(); E;) {
		List<String>::Element *N = E->next();
		String error;
		CLIHarnessRunner::parse_argument(E->get(), N, result.options, error);
		if (!error.is_empty()) {
			result.error = error;
			break;
		}
		E = N;
	}
	return result;
}

static Dictionary make_minimal_script() {
	Dictionary script;
	script["name"] = "basic";
	Array steps;
	Dictionary step;
	step["cmd"] = "get_status";
	steps.push_back(step);
	script["steps"] = steps;
	return script;
}

TEST_SUITE("[Main][CLIHarnessRunner]") {
	TEST_CASE("Parses CLI arguments") {
		const ParsedOptionsResult parsed = parse_cli_options({ "--harness-run", "res://harness/basic.json", "--harness-report", "user://harness/report.json", "--harness-timeout-frames", "120", "--harness-format", "both" });
		CHECK(parsed.error.is_empty());
		CHECK(parsed.options.enabled);
		CHECK_EQ(parsed.options.run_path, String("res://harness/basic.json"));
		CHECK_EQ(parsed.options.report_path, String("user://harness/report.json"));
		CHECK(parsed.options.report_path_set);
		CHECK_EQ(parsed.options.timeout_frames, 120);
		CHECK(parsed.options.timeout_frames_set);
		CHECK_EQ(parsed.options.format, CLIHarnessRunner::FORMAT_BOTH);
		CHECK(parsed.options.format_set);
	}

	TEST_CASE("Validates CLI options") {
		String error;
		CLIHarnessRunner::Options options;
		options.report_path_set = true;
		CHECK_EQ(CLIHarnessRunner::validate_options(options, true, false, false, false, error), ERR_INVALID_PARAMETER);
		CHECK(error.contains("--harness-run"));

		options = CLIHarnessRunner::Options();
		options.enabled = true;
		options.run_path = "basic.json";
		options.timeout_frames = 0;
		CHECK_EQ(CLIHarnessRunner::validate_options(options, true, false, false, false, error), ERR_INVALID_PARAMETER);
		CHECK(error.contains("--harness-timeout-frames"));

		options.timeout_frames = 10;
		CHECK_EQ(CLIHarnessRunner::validate_options(options, false, false, false, false, error), ERR_INVALID_PARAMETER);
		CHECK(error.contains("project context"));

		CHECK_EQ(CLIHarnessRunner::validate_options(options, true, true, false, false, error), ERR_INVALID_PARAMETER);
		CHECK(error.contains("CLI project runs"));
	}

	TEST_CASE("Validates harness script schema") {
		String error;
		Dictionary script = make_minimal_script();
		CHECK_EQ(CLIHarnessRunner::validate_script(script, error), OK);

		script["unknown"] = true;
		CHECK_EQ(CLIHarnessRunner::validate_script(script, error), ERR_INVALID_DATA);
		CHECK(error.contains("Unknown field"));

		script = make_minimal_script();
		script.erase("steps");
		CHECK_EQ(CLIHarnessRunner::validate_script(script, error), ERR_INVALID_DATA);
		CHECK(error.contains("steps"));

		script = make_minimal_script();
		Array steps = script["steps"];
		Dictionary bad_step;
		bad_step["cmd"] = "get_status";
		bad_step["expect"] = "node_exists";
		steps[0] = bad_step;
		script["steps"] = steps;
		CHECK_EQ(CLIHarnessRunner::validate_script(script, error), ERR_INVALID_DATA);
		CHECK(error.contains("exactly one"));

		script = make_minimal_script();
		Dictionary evidence;
		evidence["screenshots"] = "sometimes";
		script["evidence"] = evidence;
		CHECK_EQ(CLIHarnessRunner::validate_script(script, error), ERR_INVALID_DATA);
		CHECK(error.contains("screenshots"));
	}

	TEST_CASE("Builds AI requests for aliases and expectations") {
		String error;
		Dictionary request;
		Dictionary meta;

		Dictionary hold;
		hold["cmd"] = "hold_action";
		hold["action"] = "ui_right";
		hold["frames"] = 5;
		REQUIRE(CLIHarnessRunner::build_ai_request_for_step(hold, 7, request, meta, error));
		CHECK_EQ(String(request["cmd"]), String("batch"));
		CHECK_EQ(((Array)request["ops"]).size(), 3);
		CHECK_EQ((int)meta["expandedSteps"], 3);

		Dictionary tap;
		tap["cmd"] = "tap_action";
		tap["action"] = "ui_accept";
		REQUIRE(CLIHarnessRunner::build_ai_request_for_step(tap, 8, request, meta, error));
		CHECK_EQ(String(request["cmd"]), String("batch"));

		Dictionary look;
		look["cmd"] = "mouse_look";
		look["relativeX"] = 12.5;
		look["relativeY"] = -3.0;
		REQUIRE(CLIHarnessRunner::build_ai_request_for_step(look, 9, request, meta, error));
		CHECK_EQ(String(request["cmd"]), String("mouse_motion"));
		CHECK_EQ((double)request["relativeX"], doctest::Approx(12.5));

		Dictionary expect;
		expect["expect"] = "node_visible";
		Dictionary selector;
		selector["name"] = "Play";
		expect["selector"] = selector;
		REQUIRE(CLIHarnessRunner::build_ai_request_for_step(expect, 10, request, meta, error));
		CHECK_EQ(String(request["cmd"]), String("wait_property"));
		CHECK_EQ(String(request["property"]), String("visible"));
		CHECK((bool)request["equals"]);

		Dictionary self_test;
		self_test["cmd"] = "input_self_test";
		Dictionary self_selector;
		self_selector["name"] = "Play";
		self_test["selector"] = self_selector;
		self_test["action"] = "ui_accept";
		Array relative;
		relative.push_back(3);
		relative.push_back(-2);
		self_test["mouseRelative"] = relative;
		REQUIRE(CLIHarnessRunner::build_ai_request_for_step(self_test, 11, request, meta, error));
		CHECK_EQ(String(request["cmd"]), String("batch"));
		CHECK((int)meta["expandedSteps"] > 6);
		CHECK((bool)meta["checksFocusAndGuiKeys"]);
		CHECK((bool)meta["checksMouseMotion"]);
	}

	TEST_CASE("Records assertion entries for expect steps") {
		const String script_path = TestUtils::get_temp_path("cli_harness_assertion_script.json");
		const String report_path = TestUtils::get_temp_path("cli_harness_assertion_report.json");
		Dictionary script;
		script["name"] = "assertions";
		Array steps;
		Dictionary expect;
		expect["expect"] = "scene_changed";
		expect["timeoutFrames"] = "not-a-number";
		steps.push_back(expect);
		script["steps"] = steps;
		{
			Ref<FileAccess> file = FileAccess::open(script_path, FileAccess::WRITE);
			REQUIRE(file.is_valid());
			file->store_string(JSON::stringify(script));
		}

		CLIAIInputServer::Options ai_options;
		ai_options.listen_tcp = false;
		CLIAIInputServer ai_server(ai_options);
		String ai_error;
		REQUIRE_EQ(ai_server.initialize(ai_error), OK);
		CLIHarnessRunner::Options options;
		options.enabled = true;
		options.run_path = script_path;
		options.report_path = report_path;
		options.report_path_set = true;
		CLIHarnessRunner runner(options, OS::get_singleton()->get_cwd(), &ai_server);
		String initialize_error;
		REQUIRE_EQ(runner.initialize(initialize_error), OK);
		for (int i = 0; i < 4 && !runner.is_finished(); i++) {
			runner.poll();
		}
		CHECK(runner.is_finished());
		CHECK_EQ(runner.get_exit_code(), CLIHarnessRunner::EXIT_FAILED);
		const Dictionary report = runner.get_report();
		REQUIRE(report.has("assertions"));
		const Array assertions = report["assertions"];
		REQUIRE_EQ(assertions.size(), 1);
		CHECK_EQ(String(((Dictionary)assertions[0])["expect"]), String("scene_changed"));
		CHECK_FALSE((bool)((Dictionary)assertions[0])["ok"]);

		DirAccess::remove_absolute(script_path);
		DirAccess::remove_absolute(report_path);
	}

	TEST_CASE("Emits trace events and report for a simple harness run") {
		const String trace_root = TestUtils::get_temp_path("cli_harness_trace_root");
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(trace_root);
		CustomFeatureTracer::StartupOptions trace_options;
		trace_options.base_dir_override = trace_root;
		trace_options.binary_path = "godot-test";
		trace_options.cwd = OS::get_singleton()->get_cwd();
		trace_options.project_path = OS::get_singleton()->get_cwd();
		trace_options.process_mode = "cli_project_run";
		trace_options.pid = 1;
		String trace_error;
		REQUIRE_EQ(CustomFeatureTracer::initialize_singleton(trace_options, trace_error), OK);
		const String session_dir = CustomFeatureTracer::get_singleton()->get_session_dir_for_tests();

		const String script_path = TestUtils::get_temp_path("cli_harness_script.json");
		const String report_path = TestUtils::get_temp_path("cli_harness_report.json");
		{
			Ref<FileAccess> file = FileAccess::open(script_path, FileAccess::WRITE);
			REQUIRE(file.is_valid());
			file->store_string(JSON::stringify(make_minimal_script()));
		}

		CLIAIInputServer::Options ai_options;
		CLIAIInputServer ai_server(ai_options);
		CLIHarnessRunner::Options options;
		options.enabled = true;
		options.run_path = script_path;
		options.report_path = report_path;
		options.report_path_set = true;
		CLIHarnessRunner runner(options, OS::get_singleton()->get_cwd(), &ai_server);
		String initialize_error;
		REQUIRE_EQ(runner.initialize(initialize_error), OK);
		for (int i = 0; i < 4 && !runner.is_finished(); i++) {
			runner.poll();
		}
		CHECK(runner.is_finished());
		CHECK_EQ(runner.get_exit_code(), CLIHarnessRunner::EXIT_OK);
		CHECK(FileAccess::exists(report_path));
		const Dictionary report = runner.get_report();
		CHECK_EQ(String(report["name"]), String("basic"));
		CHECK_EQ((int)report["exitCode"], CLIHarnessRunner::EXIT_OK);
		CHECK(report.has("steps"));

		CustomFeatureTracer::shutdown_singleton();
		const Vector<Dictionary> events = TestCustomFeatureTraceHelpers::read_events(session_dir);
		const Dictionary started = TestCustomFeatureTraceHelpers::find_event(events, CustomFeatureTracer::FEATURE_PROJECT_HARNESS, "harness_started");
		const Dictionary finished = TestCustomFeatureTraceHelpers::find_event(events, CustomFeatureTracer::FEATURE_PROJECT_HARNESS, "harness_finished");
		CHECK_FALSE(started.is_empty());
		CHECK_FALSE(finished.is_empty());
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(trace_root);
		DirAccess::remove_absolute(script_path);
		DirAccess::remove_absolute(report_path);
	}
}

} // namespace TestCLIHarnessRunner
