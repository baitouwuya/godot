/**************************************************************************/
/*  test_cli_performance_recorder.h                                       */
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

#include "main/cli_performance_recorder.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "main/performance.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

namespace TestCLIPerformanceRecorder {

struct ParsedOptionsResult {
	CLIPerformanceRecorder::Options options;
	String error;
};

static Variant _custom_numeric_monitor() {
	return 42;
}

static Variant _custom_float_monitor() {
	return 3.5;
}

static Variant _custom_text_monitor() {
	return "text";
}

static ParsedOptionsResult parse_cli_options(const std::initializer_list<const char *> &p_args) {
	List<String> args;
	for (const char *arg : p_args) {
		args.push_back(String(arg));
	}

	ParsedOptionsResult result;
	for (List<String>::Element *E = args.front(); E;) {
		List<String>::Element *N = E->next();
		String error;
		CLIPerformanceRecorder::parse_argument(E->get(), N, result.options, error);
		if (!error.is_empty()) {
			result.error = error;
			break;
		}
		E = N;
	}

	return result;
}

TEST_SUITE("[Main][CLIPerformanceRecorder]") {
	TEST_CASE("Parses CLI arguments") {
		SUBCASE("perf record with default start frame") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-record", "--perf-end-frame", "10" });
			CHECK(parsed.error.is_empty());
			CHECK(parsed.options.enabled);
			CHECK_EQ(parsed.options.start_frame, 0);
			CHECK_EQ(parsed.options.end_frame, 10);
		}

		SUBCASE("perf record with explicit frame window") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-record", "--perf-start-frame", "5", "--perf-end-frame", "10" });
			CHECK(parsed.error.is_empty());
			CHECK(parsed.options.enabled);
			CHECK_EQ(parsed.options.start_frame, 5);
			CHECK_EQ(parsed.options.end_frame, 10);
		}

		SUBCASE("perf record with slow-frame and samples options") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-record", "--perf-end-frame", "10", "--perf-top-frames", "3", "--perf-samples-file", "user://perf.jsonl" });
			CHECK(parsed.error.is_empty());
			CHECK(parsed.options.enabled);
			CHECK_EQ(parsed.options.top_frames, 3);
			CHECK_EQ(parsed.options.samples_file, String("user://perf.jsonl"));
		}
	}

	TEST_CASE("Validates CLI options") {
		String error;
		const String samples_path = TestUtils::get_temp_path("cli_perf_recorder_samples.jsonl");

		SUBCASE("requires perf end frame") {
			CLIPerformanceRecorder::Options options;
			options.enabled = true;
			CHECK_EQ(CLIPerformanceRecorder::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--perf-end-frame"));
		}

		SUBCASE("rejects start frame without perf record") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-start-frame", "5" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLIPerformanceRecorder::validate_options(parsed.options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--perf-record"));
		}

		SUBCASE("rejects end frame without perf record") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-end-frame", "10" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLIPerformanceRecorder::validate_options(parsed.options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--perf-record"));
		}

		SUBCASE("rejects top frames without perf record") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-top-frames", "5" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLIPerformanceRecorder::validate_options(parsed.options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--perf-record"));
		}

		SUBCASE("rejects samples file without perf record") {
			CLIPerformanceRecorder::Options options;
			options.samples_file = samples_path;
			options.samples_file_set = true;
			CHECK_EQ(CLIPerformanceRecorder::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--perf-record"));
		}

		SUBCASE("rejects negative start frame") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-record", "--perf-start-frame", "-1", "--perf-end-frame", "5" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLIPerformanceRecorder::validate_options(parsed.options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--perf-start-frame"));
		}

		SUBCASE("rejects end frame before start frame") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-record", "--perf-start-frame", "6", "--perf-end-frame", "5" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLIPerformanceRecorder::validate_options(parsed.options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--perf-end-frame"));
		}

		SUBCASE("rejects negative top frame count") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-record", "--perf-end-frame", "5", "--perf-top-frames", "-1" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLIPerformanceRecorder::validate_options(parsed.options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--perf-top-frames"));
		}

		SUBCASE("rejects editor mode") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-record", "--perf-end-frame", "5" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLIPerformanceRecorder::validate_options(parsed.options, true, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("CLI project runs"));
		}

		SUBCASE("rejects project manager mode") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-record", "--perf-end-frame", "5" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLIPerformanceRecorder::validate_options(parsed.options, false, true, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("CLI project runs"));
		}

		SUBCASE("rejects command line tools") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--perf-record", "--perf-end-frame", "5" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLIPerformanceRecorder::validate_options(parsed.options, false, false, true, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("CLI project runs"));
		}

		SUBCASE("accepts samples file during CLI project runs") {
			CLIPerformanceRecorder::Options options;
			options.enabled = true;
			options.end_frame = 5;
			options.end_frame_set = true;
			options.samples_file = samples_path;
			options.samples_file_set = true;
			CHECK_EQ(CLIPerformanceRecorder::validate_options(options, false, false, false, error), OK);
			CHECK(error.is_empty());
		}
	}

	TEST_CASE("Computes online statistics") {
		SUBCASE("serializes a multi-sample sequence") {
			CLIPerformanceRecorder::OnlineStats stats;
			stats.push_sample(1.0);
			stats.push_sample(2.0);
			stats.push_sample(3.0);
			stats.push_sample(4.0);

			const Dictionary serialized = stats.to_dictionary();
			CHECK_EQ((int64_t)serialized["count"], 4);
			CHECK((double)serialized["min"] == doctest::Approx(1.0));
			CHECK((double)serialized["max"] == doctest::Approx(4.0));
			CHECK((double)serialized["mean"] == doctest::Approx(2.5));
			CHECK((double)serialized["variance"] == doctest::Approx(1.25));
			CHECK((double)serialized["stddev"] == doctest::Approx(Math::sqrt(1.25)));
			CHECK((double)serialized["first"] == doctest::Approx(1.0));
			CHECK((double)serialized["last"] == doctest::Approx(4.0));
			CHECK((double)serialized["delta"] == doctest::Approx(3.0));
			CHECK((double)serialized["p50"] == doctest::Approx(2.5));
			CHECK((double)serialized["p95"] == doctest::Approx(3.85));
			CHECK((double)serialized["p99"] == doctest::Approx(3.97));
		}

		SUBCASE("serializes a single sample with zero variance") {
			CLIPerformanceRecorder::OnlineStats stats;
			stats.push_sample(5.0);

			const Dictionary serialized = stats.to_dictionary();
			CHECK_EQ((int64_t)serialized["count"], 1);
			CHECK((double)serialized["variance"] == doctest::Approx(0.0));
			CHECK((double)serialized["stddev"] == doctest::Approx(0.0));
		}

		SUBCASE("keeps approximate percentiles stable for larger series") {
			CLIPerformanceRecorder::OnlineStats stats;
			for (int i = 1; i <= 100; i++) {
				stats.push_sample((double)i);
			}

			const Dictionary serialized = stats.to_dictionary();
			CHECK((double)serialized["p50"] == doctest::Approx(50.5).epsilon(0.05));
			CHECK((double)serialized["p95"] == doctest::Approx(95.05).epsilon(0.05));
			CHECK((double)serialized["p99"] == doctest::Approx(99.01).epsilon(0.05));
		}

		SUBCASE("omits zero-sample metrics from the summary") {
			CLIPerformanceRecorder::Options options;
			options.enabled = true;
			options.end_frame = 0;
			options.end_frame_set = true;
			CLIPerformanceRecorder recorder(options);

			const Dictionary summary = recorder.build_summary(false);
			CHECK(((Dictionary)summary["frameMetrics"]).is_empty());
			CHECK(((Dictionary)summary["builtinMonitorMetrics"]).is_empty());
			CHECK(((Dictionary)summary["customMonitorMetrics"]).is_empty());
			CHECK(summary.has("budgetSummary"));
			CHECK(summary.has("slowFrames"));
		}
	}

	TEST_CASE("Filters monitors") {
		CHECK_FALSE(CLIPerformanceRecorder::should_include_builtin_monitor(Performance::TIME_FPS));
		CHECK_FALSE(CLIPerformanceRecorder::should_include_builtin_monitor(Performance::TIME_PROCESS));
		CHECK_FALSE(CLIPerformanceRecorder::should_include_builtin_monitor(Performance::TIME_PHYSICS_PROCESS));
		CHECK_FALSE(CLIPerformanceRecorder::should_include_builtin_monitor(Performance::TIME_NAVIGATION_PROCESS));
		CHECK(CLIPerformanceRecorder::should_include_builtin_monitor(Performance::MEMORY_STATIC));

		CHECK(CLIPerformanceRecorder::should_include_custom_monitor_value(Variant(1)));
		CHECK(CLIPerformanceRecorder::should_include_custom_monitor_value(Variant(1.5)));
		CHECK_FALSE(CLIPerformanceRecorder::should_include_custom_monitor_value(Variant(true)));
		CHECK_FALSE(CLIPerformanceRecorder::should_include_custom_monitor_value(Variant("text")));
		CHECK_FALSE(CLIPerformanceRecorder::should_include_custom_monitor_value(Variant()));
	}

	TEST_CASE("Builds the expected summary structure") {
		CLIPerformanceRecorder::Options options;
		options.enabled = true;
		options.start_frame = 0;
		options.end_frame = 0;
		options.end_frame_set = true;

		CLIPerformanceRecorder recorder(options);
		recorder.record_frame(0, 10, 20, 30, 40, 0.5);

		const Dictionary summary = recorder.build_summary(true);
		CHECK(summary.has("recording"));
		CHECK(summary.has("frameMetrics"));
		CHECK(summary.has("builtinMonitorMetrics"));
		CHECK(summary.has("customMonitorMetrics"));
		CHECK(summary.has("budgetSummary"));
		CHECK(summary.has("slowFrames"));

		const Dictionary recording = summary["recording"];
		CHECK(recording.has("startFrame"));
		CHECK(recording.has("endFrame"));
		CHECK(recording.has("capturedFrames"));
		CHECK(recording.has("completed"));
		CHECK_EQ((int64_t)recording["capturedFrames"], 1);
		CHECK((bool)recording["completed"]);

		const Dictionary frame_metrics = summary["frameMetrics"];
		CHECK(frame_metrics.has("frame_time_usec"));
		const Dictionary metric = frame_metrics["frame_time_usec"];
		CHECK(metric.has("count"));
		CHECK(metric.has("min"));
		CHECK(metric.has("max"));
		CHECK(metric.has("mean"));
		CHECK(metric.has("variance"));
		CHECK(metric.has("stddev"));
		CHECK(metric.has("first"));
		CHECK(metric.has("last"));
		CHECK(metric.has("delta"));
		CHECK(metric.has("p50"));
		CHECK(metric.has("p95"));
		CHECK(metric.has("p99"));

		const Dictionary budget_summary = summary["budgetSummary"];
		CHECK(budget_summary.has("over16_667ms"));
		CHECK(budget_summary.has("over33_333ms"));
		CHECK(budget_summary.has("over50_000ms"));

		const Array slow_frames = summary["slowFrames"];
		CHECK_EQ(slow_frames.size(), 1);
		const Dictionary slow_frame = slow_frames[0];
		CHECK_EQ((int64_t)slow_frame["frame"], 0);
	}

	TEST_CASE("Tracks frame budgets and slowest frames") {
		CLIPerformanceRecorder::Options options;
		options.enabled = true;
		options.start_frame = 0;
		options.end_frame = 2;
		options.end_frame_set = true;
		options.top_frames = 2;

		CLIPerformanceRecorder recorder(options);
		recorder.record_frame(0, 10000, 1, 1, 1, 0.016);
		recorder.record_frame(1, 40000, 1, 1, 1, 0.016);
		recorder.record_frame(2, 60000, 1, 1, 1, 0.016);

		const Dictionary summary = recorder.build_summary(true);
		const Dictionary budget_summary = summary["budgetSummary"];
		const Dictionary over_16 = budget_summary["over16_667ms"];
		const Dictionary over_33 = budget_summary["over33_333ms"];
		const Dictionary over_50 = budget_summary["over50_000ms"];
		CHECK_EQ((int64_t)over_16["overCount"], 2);
		CHECK_EQ((int64_t)over_33["overCount"], 2);
		CHECK_EQ((int64_t)over_50["overCount"], 1);
		CHECK((double)over_16["overRatio"] == doctest::Approx(2.0 / 3.0));

		const Array slow_frames = summary["slowFrames"];
		REQUIRE_EQ(slow_frames.size(), 2);
		CHECK_EQ((int64_t)((Dictionary)slow_frames[0])["frame"], 2);
		CHECK_EQ((int64_t)((Dictionary)slow_frames[1])["frame"], 1);
	}

	TEST_CASE("Writes JSONL samples for recorded frames") {
		CLIPerformanceRecorder::Options options;
		options.enabled = true;
		options.end_frame = 0;
		options.end_frame_set = true;
		options.samples_file = TestUtils::get_temp_path("cli_perf_frame_samples.jsonl");
		options.samples_file_set = true;

		CLIPerformanceRecorder recorder(options);
		String error;
		REQUIRE_EQ(recorder.initialize(error), OK);
		recorder.record_frame(0, 12345, 12, 34, 56, 0.5);
		recorder.close();

		Error file_error = OK;
		Ref<FileAccess> file = FileAccess::open(options.samples_file, FileAccess::READ, &file_error);
		REQUIRE(file.is_valid());
		REQUIRE_EQ(file_error, OK);

		const Variant parsed_line = JSON::parse_string(file->get_line());
		REQUIRE(parsed_line.get_type() == Variant::DICTIONARY);
		const Dictionary sample = parsed_line;
		CHECK((bool)sample["recorded"]);
		CHECK_EQ((int64_t)sample["frame"], 0);
		const Dictionary sample_frame_metrics = sample["frameMetrics"];
		CHECK(sample_frame_metrics.has("frame_time_usec"));
		CHECK(sample.has("customMonitors"));
	}
}

} // namespace TestCLIPerformanceRecorder
