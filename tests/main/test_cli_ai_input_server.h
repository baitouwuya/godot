/**************************************************************************/
/*  test_cli_ai_input_server.h                                            */
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

#include "thirdparty/doctest/doctest.h"

namespace TestCLIAIInputServer {

struct ParsedOptionsResult {
	CLIAIInputServer::Options options;
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
		CLIAIInputServer::parse_argument(E->get(), N, result.options, error);
		if (!error.is_empty()) {
			result.error = error;
			break;
		}
		E = N;
	}

	return result;
}

TEST_SUITE("[Main][CLIAIInputServer]") {
	TEST_CASE("Parses AI agent CLI arguments") {
		const ParsedOptionsResult parsed = parse_cli_options({ "--ai-agent-control", "--ai-agent-port", "7011", "--ai-agent-max-ops", "32", "--ai-agent-max-line-bytes", "4096" });
		CHECK(parsed.error.is_empty());
		CHECK(parsed.options.enabled);
		CHECK(parsed.options.enabled_set);
		CHECK_EQ(parsed.options.port, 7011);
		CHECK(parsed.options.port_set);
		CHECK_EQ(parsed.options.max_batch_ops, 32);
		CHECK(parsed.options.max_batch_ops_set);
		CHECK_EQ(parsed.options.max_line_bytes, 4096);
		CHECK(parsed.options.max_line_bytes_set);
	}

	TEST_CASE("Validates AI agent options") {
		String error;

		SUBCASE("rejects invalid port") {
			CLIAIInputServer::Options options;
			options.enabled = true;
			options.port = 70000;
			CHECK_EQ(CLIAIInputServer::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--ai-agent-port"));
		}

		SUBCASE("rejects invalid max ops") {
			CLIAIInputServer::Options options;
			options.enabled = true;
			options.max_batch_ops = 0;
			CHECK_EQ(CLIAIInputServer::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--ai-agent-max-ops"));
		}

		SUBCASE("rejects editor mode when enabled") {
			CLIAIInputServer::Options options;
			options.enabled = true;
			CHECK_EQ(CLIAIInputServer::validate_options(options, true, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("CLI project runs"));
		}

		SUBCASE("allows argument overrides without enabling the server") {
			CLIAIInputServer::Options options;
			options.port = 7012;
			options.port_set = true;
			CHECK_EQ(CLIAIInputServer::validate_options(options, false, false, false, error), OK);
		}
	}

	TEST_CASE("Parses mouse buttons") {
		MouseButton button = MouseButton::NONE;
		CHECK(CLIAIInputServer::parse_mouse_button("left", button));
		CHECK(button == MouseButton::LEFT);
		CHECK(CLIAIInputServer::parse_mouse_button("wheel_down", button));
		CHECK(button == MouseButton::WHEEL_DOWN);
		CHECK(CLIAIInputServer::parse_mouse_button(8, button));
		CHECK(button == MouseButton::MB_XBUTTON1);
		CHECK_FALSE(CLIAIInputServer::parse_mouse_button("invalid", button));
	}

	TEST_CASE("Expands high-level input operations") {
		String error;
		Array expanded;

		SUBCASE("click expands to move, press, wait, release") {
			Dictionary click;
			click["cmd"] = "click";
			click["button"] = "left";
			click["x"] = 10;
			click["y"] = 20;
			CHECK(CLIAIInputServer::expand_operation_for_tests(click, expanded, error));
			CHECK(error.is_empty());
			CHECK_EQ(expanded.size(), 4);
			CHECK_EQ(String(((Dictionary)expanded[0])["cmd"]), String("mouse_motion"));
			CHECK_EQ(String(((Dictionary)expanded[1])["cmd"]), String("mouse_button"));
			CHECK_EQ(String(((Dictionary)expanded[2])["cmd"]), String("wait"));
			CHECK_EQ(String(((Dictionary)expanded[3])["cmd"]), String("mouse_button"));
		}

		SUBCASE("double click marks second press as double click") {
			Dictionary double_click;
			double_click["cmd"] = "double_click";
			double_click["x"] = 10;
			double_click["y"] = 20;
			CHECK(CLIAIInputServer::expand_operation_for_tests(double_click, expanded, error));
			CHECK(error.is_empty());
			CHECK_EQ(expanded.size(), 8);
			CHECK((bool)((Dictionary)expanded[5])["doubleClick"]);
		}

		SUBCASE("drag path expands across requested frame count") {
			Dictionary drag;
			drag["cmd"] = "drag";
			drag["button"] = "left";
			Array path;
			Array p0;
			p0.push_back(0);
			p0.push_back(0);
			Array p1;
			p1.push_back(10);
			p1.push_back(10);
			path.push_back(p0);
			path.push_back(p1);
			drag["path"] = path;
			drag["frames"] = 3;
			CHECK(CLIAIInputServer::expand_operation_for_tests(drag, expanded, error));
			CHECK(error.is_empty());
			CHECK_EQ(expanded.size(), 6);
			CHECK_EQ(String(((Dictionary)expanded[5])["cmd"]), String("mouse_button"));
			CHECK_FALSE((bool)((Dictionary)expanded[5])["pressed"]);
		}

		SUBCASE("invalid hold frame count is rejected") {
			Dictionary hold;
			hold["cmd"] = "hold";
			hold["x"] = 1;
			hold["y"] = 1;
			hold["frames"] = 0;
			CHECK_FALSE(CLIAIInputServer::expand_operation_for_tests(hold, expanded, error));
			CHECK(error.contains("hold.frames"));
		}
	}

	TEST_CASE("Reuses performance recorder for open-ended runtime sessions") {
		CLIPerformanceRecorder::Options options;
		options.enabled = true;
		options.start_frame = 5;
		options.recording_name = "probe";
		options.top_frames = 1;

		CLIPerformanceRecorder recorder(options);
		recorder.record_frame(4, 1000, 100, 50, 25, 0.016);
		recorder.record_frame(5, 1000, 100, 50, 25, 0.016);
		recorder.record_frame(6, 2000, 100, 50, 25, 0.016);
		recorder.set_summary_end_frame(6);

		const Dictionary summary = recorder.build_summary(true);
		const Dictionary recording = summary["recording"];
		CHECK_EQ((int64_t)recording["startFrame"], 5);
		CHECK_EQ((int64_t)recording["endFrame"], 6);
		CHECK_EQ((int64_t)recording["capturedFrames"], 2);
		CHECK_EQ(String(recording["name"]), String("probe"));
		CHECK((bool)recording["completed"]);
		CHECK(summary.has("slowFrames"));
	}
}

} // namespace TestCLIAIInputServer
