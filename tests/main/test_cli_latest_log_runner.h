/**************************************************************************/
/*  test_cli_latest_log_runner.h                                          */
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

#include "main/cli_latest_log_runner.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

namespace TestCLILatestLogRunner {

struct ParsedOptionsResult {
	CLILatestLogRunner::Options options;
	String error;
};

struct BuildOutputResult {
	Error err = OK;
	String error;
	String output;
	Dictionary summary;
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
		CLILatestLogRunner::parse_argument(E->get(), N, result.options, error);
		if (!error.is_empty()) {
			result.error = error;
			break;
		}
		E = N;
	}

	return result;
}

static void write_text_file(const String &p_path, const String &p_text) {
	DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	REQUIRE_EQ(err, OK);
	REQUIRE_FALSE(file.is_null());
	file->store_string(p_text);
	file->close();
}

static void cleanup_path(const String &p_path) {
	if (FileAccess::exists(p_path)) {
		DirAccess::remove_absolute(p_path);
	}
}

static BuildOutputResult build_output_for_log(const String &p_path, const CLILatestLogRunner::Options &p_options) {
	BuildOutputResult result;
	result.err = CLILatestLogRunner::build_output(p_path, p_path, p_options, result.output, result.summary, result.error);
	return result;
}

static Dictionary parse_last_json_line(const String &p_output) {
	const PackedStringArray lines = p_output.strip_edges().split("\n", false);
	REQUIRE_FALSE(lines.is_empty());
	const Variant parsed = JSON::parse_string(lines[lines.size() - 1]);
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	return parsed;
}

static Dictionary get_dictionary(const Dictionary &p_dict, const String &p_key) {
	return p_dict[p_key];
}

static Array get_array(const Dictionary &p_dict, const String &p_key) {
	return p_dict[p_key];
}

TEST_SUITE("[Main][CLILatestLogRunner]") {
	TEST_CASE("Parses CLI arguments") {
		SUBCASE("latest log with default line count and text format") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--latest-log" });
			CHECK(parsed.error.is_empty());
			CHECK(parsed.options.enabled);
			CHECK_EQ(parsed.options.lines, 200);
			CHECK_EQ(parsed.options.format, CLILatestLogRunner::FORMAT_TEXT);
		}

		SUBCASE("latest log with explicit line count and output format") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--latest-log", "--latest-log-lines", "25", "--latest-log-format", "both" });
			CHECK(parsed.error.is_empty());
			CHECK(parsed.options.enabled);
			CHECK_EQ(parsed.options.lines, 25);
			CHECK(parsed.options.lines_set);
			CHECK_EQ(parsed.options.format, CLILatestLogRunner::FORMAT_BOTH);
			CHECK(parsed.options.format_set);
		}

		SUBCASE("rejects unknown output format") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--latest-log", "--latest-log-format", "xml" });
			CHECK(parsed.error.contains("text, json, both or raw"));
		}
	}

	TEST_CASE("Validates CLI options") {
		String error;

		SUBCASE("rejects latest-log-lines without latest-log") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--latest-log-lines", "25" });
			CHECK(parsed.error.is_empty());
			CHECK_EQ(CLILatestLogRunner::validate_options(parsed.options, true, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--latest-log"));
		}

		SUBCASE("rejects latest-log-format without latest-log") {
			CLILatestLogRunner::Options options;
			options.format = CLILatestLogRunner::FORMAT_JSON;
			options.format_set = true;
			CHECK_EQ(CLILatestLogRunner::validate_options(options, true, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--latest-log"));
		}

		SUBCASE("rejects negative line count") {
			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.lines = -1;
			options.lines_set = true;
			CHECK_EQ(CLILatestLogRunner::validate_options(options, true, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--latest-log-lines"));
		}

		SUBCASE("rejects missing project context") {
			CLILatestLogRunner::Options options;
			options.enabled = true;
			CHECK_EQ(CLILatestLogRunner::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("project context"));
		}

		SUBCASE("rejects editor mode") {
			CLILatestLogRunner::Options options;
			options.enabled = true;
			CHECK_EQ(CLILatestLogRunner::validate_options(options, true, true, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("editor"));
		}
	}

	TEST_CASE("Resolves effective base log paths") {
		const String user_override = "user://logs/custom.log";
		const String relative_override = "relative/custom.log";
		String expected_base = ProjectSettings::get_singleton()->get_resource_path();
		if (expected_base.is_empty()) {
			expected_base = OS::get_singleton()->get_cwd();
		}

		CHECK_EQ(CLILatestLogRunner::resolve_base_log_path(user_override), ProjectSettings::get_singleton()->globalize_path(user_override).simplify_path());
		CHECK_EQ(CLILatestLogRunner::resolve_base_log_path(relative_override), expected_base.path_join(relative_override).simplify_path());
	}

	TEST_CASE("Finds the newest rotated log file") {
		const String temp_dir = TestUtils::get_temp_path("cli_latest_log_runner");
		const String base_log = temp_dir.path_join("godot.log");
		const String rotated_log = temp_dir.path_join("godot2026-04-24T10.00.00.log");

		cleanup_path(base_log);
		cleanup_path(rotated_log);
		DirAccess::make_dir_recursive_absolute(temp_dir);

		write_text_file(base_log, "base");
		OS::get_singleton()->delay_usec(1100000);
		write_text_file(rotated_log, "rotated");

		String latest_log;
		CHECK_EQ(CLILatestLogRunner::find_latest_log_path(base_log, latest_log), OK);
		CHECK_EQ(latest_log, rotated_log.simplify_path());

		cleanup_path(base_log);
		cleanup_path(rotated_log);
		DirAccess::remove_absolute(temp_dir);
	}

	TEST_CASE("Reads compact and full log output") {
		const String temp_log = TestUtils::get_temp_path("cli_latest_log_runner_tail.log");
		cleanup_path(temp_log);

		SUBCASE("reads the last N lines without loading the full file into the result") {
			write_text_file(temp_log, "line 1\nline 2\nline 3\nline 4\n");

			CLILatestLogRunner::ReadResult result;
			String error;
			CHECK_EQ(CLILatestLogRunner::read_log(temp_log, 2, result, error), OK);
			CHECK(error.is_empty());
			CHECK_EQ(result.total_lines, 4);
			CHECK_EQ(result.shown_lines, 2);
			CHECK(result.truncated);
			CHECK_EQ(result.text, String("line 3\nline 4"));
		}

		SUBCASE("reads the whole file when line count is zero") {
			write_text_file(temp_log, "alpha\nbeta");

			CLILatestLogRunner::ReadResult result;
			String error;
			CHECK_EQ(CLILatestLogRunner::read_log(temp_log, 0, result, error), OK);
			CHECK(error.is_empty());
			CHECK_EQ(result.total_lines, 2);
			CHECK_EQ(result.shown_lines, 2);
			CHECK_FALSE(result.truncated);
			CHECK_EQ(result.text, String("alpha\nbeta"));
		}

		cleanup_path(temp_log);
	}

	TEST_CASE("Keeps raw mode compatibility") {
		const String temp_log = TestUtils::get_temp_path("cli_latest_log_runner_raw.log");
		cleanup_path(temp_log);
		write_text_file(temp_log, "line 1\nline 2\nline 3\nline 4\n");

		CLILatestLogRunner::Options options;
		options.enabled = true;
		options.lines = 2;
		options.format = CLILatestLogRunner::FORMAT_RAW;

		const BuildOutputResult result = build_output_for_log(temp_log, options);
		CHECK_EQ(result.err, OK);
		CHECK(result.error.is_empty());
		CHECK(result.output.contains("[latest-log] path=" + temp_log));
		CHECK(result.output.contains("[latest-log] lines=2/4 truncated=true"));
		CHECK(result.output.contains("line 3\nline 4"));

		cleanup_path(temp_log);
	}

	TEST_CASE("Falls back from tail window to full file only when needed") {
		const String temp_log = TestUtils::get_temp_path("cli_latest_log_runner_fallback.log");
		cleanup_path(temp_log);

		SUBCASE("tail hit does not trigger full file fallback") {
			write_text_file(temp_log,
					"plain 1\n"
					"plain 2\n"
					"ERROR: tail failure 42\n"
					"   at: _ready (res://tail.gd:9)\n");

			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.lines = 2;
			options.format = CLILatestLogRunner::FORMAT_JSON;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);
			const Dictionary analysis = get_dictionary(result.summary, "analysis");
			CHECK_FALSE(bool(analysis["usedFullFileFallback"]));
			CHECK_EQ(int(analysis["windowLinesAnalyzed"]), 2);
			CHECK_EQ(int(analysis["issueBlockCount"]), 1);
		}

		SUBCASE("tail miss triggers full file fallback") {
			write_text_file(temp_log,
					"ERROR: early failure 42\n"
					"   at: _ready (res://main.gd:12)\n"
					"plain 1\n"
					"plain 2\n");

			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.lines = 2;
			options.format = CLILatestLogRunner::FORMAT_JSON;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);
			const Dictionary analysis = get_dictionary(result.summary, "analysis");
			CHECK(bool(analysis["usedFullFileFallback"]));
			CHECK_EQ(int(analysis["windowLinesAnalyzed"]), 4);
			CHECK_EQ(int(analysis["issueBlockCount"]), 1);
			CHECK_FALSE(result.summary.has("fallbackTail"));
		}

		SUBCASE("zero lines analyzes the whole file without fallback") {
			write_text_file(temp_log, "plain 1\nplain 2\n");

			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.lines = 0;
			options.format = CLILatestLogRunner::FORMAT_JSON;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);
			const Dictionary analysis = get_dictionary(result.summary, "analysis");
			CHECK_FALSE(bool(analysis["usedFullFileFallback"]));
			CHECK_EQ(int(analysis["windowLinesRequested"]), 0);
			CHECK_EQ(int(analysis["windowLinesAnalyzed"]), 2);
			CHECK(result.summary.has("fallbackTail"));
		}

		cleanup_path(temp_log);
	}

	TEST_CASE("Folds adjacent plain lines after conservative numeric normalization") {
		const String temp_log = TestUtils::get_temp_path("cli_latest_log_runner_plain_fold.log");
		cleanup_path(temp_log);

		SUBCASE("adjacent numeric differences fold into one group") {
			write_text_file(temp_log,
					"loading chunk 1\n"
					"loading chunk 2\n"
					"loading chunk 3\n");

			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.format = CLILatestLogRunner::FORMAT_JSON;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);

			const Dictionary analysis = get_dictionary(result.summary, "analysis");
			CHECK_EQ(int(analysis["foldedRepeatCount"]), 2);

			const Array top_templates = get_array(result.summary, "topTemplates");
			REQUIRE_EQ(top_templates.size(), 1);
			const Dictionary top_template = top_templates[0];
			CHECK_EQ(String(top_template["kind"]), String("line"));
			CHECK_EQ(int(top_template["count"]), 3);

			const Array numeric_slots = top_template["numericSlots"];
			REQUIRE_EQ(numeric_slots.size(), 1);
			const Dictionary slot = numeric_slots[0];
			CHECK_EQ(int(slot["count"]), 3);
			CHECK((double)slot["min"] == doctest::Approx(1.0));
			CHECK((double)slot["max"] == doctest::Approx(3.0));
			CHECK((double)slot["last"] == doctest::Approx(3.0));

			const Array fallback_tail = get_array(result.summary, "fallbackTail");
			REQUIRE_EQ(fallback_tail.size(), 1);
			CHECK_EQ(int(((Dictionary)fallback_tail[0])["repeat"]), 3);
		}

		SUBCASE("non adjacent repeats are not folded") {
			write_text_file(temp_log,
					"loading chunk 1\n"
					"other line\n"
					"loading chunk 2\n");

			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.format = CLILatestLogRunner::FORMAT_JSON;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);

			const Dictionary analysis = get_dictionary(result.summary, "analysis");
			CHECK_EQ(int(analysis["foldedRepeatCount"]), 0);

			const Array top_templates = get_array(result.summary, "topTemplates");
			REQUIRE_EQ(top_templates.size(), 2);
			CHECK_EQ(int(((Dictionary)top_templates[0])["count"]), 2);

			const Array fallback_tail = get_array(result.summary, "fallbackTail");
			REQUIRE_EQ(fallback_tail.size(), 3);
		}

		cleanup_path(temp_log);
	}

	TEST_CASE("Folds issue blocks only when callsite and stack anchor match") {
		const String temp_log = TestUtils::get_temp_path("cli_latest_log_runner_issue_fold.log");
		cleanup_path(temp_log);

		SUBCASE("adjacent issue blocks with numeric differences fold") {
			write_text_file(temp_log,
					"ERROR: Widget 1 failed\n"
					"   retry after 10 frames\n"
					"   at: _process (res://ui.gd:12)\n"
					"gdscript backtrace (most recent call first):\n"
					"   [0] update_widget (res://ui.gd:7)\n"
					"ERROR: Widget 2 failed\n"
					"   retry after 20 frames\n"
					"   at: _process (res://ui.gd:12)\n"
					"gdscript backtrace (most recent call first):\n"
					"   [0] update_widget (res://ui.gd:7)\n");

			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.format = CLILatestLogRunner::FORMAT_JSON;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);

			const Dictionary analysis = get_dictionary(result.summary, "analysis");
			CHECK_EQ(int(analysis["foldedRepeatCount"]), 1);
			CHECK_EQ(int(analysis["issueBlockCount"]), 2);

			const Dictionary counts = get_dictionary(result.summary, "counts");
			CHECK_EQ(int(counts["error"]), 2);

			const Array recent_issue_blocks = get_array(result.summary, "recentIssueBlocks");
			REQUIRE_EQ(recent_issue_blocks.size(), 1);
			const Dictionary issue = recent_issue_blocks[0];
			CHECK_EQ(int(issue["repeat"]), 2);
			CHECK_EQ(String(issue["severity"]), String("error"));

			const Array top_templates = get_array(result.summary, "topTemplates");
			REQUIRE_EQ(top_templates.size(), 1);
			const Array numeric_slots = ((Dictionary)top_templates[0])["numericSlots"];
			REQUIRE_EQ(numeric_slots.size(), 2);
			CHECK((double)((Dictionary)numeric_slots[0])["min"] == doctest::Approx(1.0));
			CHECK((double)((Dictionary)numeric_slots[0])["max"] == doctest::Approx(2.0));
			CHECK((double)((Dictionary)numeric_slots[1])["min"] == doctest::Approx(10.0));
			CHECK((double)((Dictionary)numeric_slots[1])["max"] == doctest::Approx(20.0));
		}

		SUBCASE("different callsites do not fold") {
			write_text_file(temp_log,
					"ERROR: Widget 1 failed\n"
					"   at: _process (res://ui.gd:12)\n"
					"gdscript backtrace (most recent call first):\n"
					"   [0] update_widget (res://ui.gd:7)\n"
					"ERROR: Widget 2 failed\n"
					"   at: _physics_process (res://ui.gd:20)\n"
					"gdscript backtrace (most recent call first):\n"
					"   [0] update_widget (res://ui.gd:7)\n");

			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.format = CLILatestLogRunner::FORMAT_JSON;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);
			const Dictionary analysis = get_dictionary(result.summary, "analysis");
			CHECK_EQ(int(analysis["foldedRepeatCount"]), 0);
			const Array recent_issue_blocks = get_array(result.summary, "recentIssueBlocks");
			REQUIRE_EQ(recent_issue_blocks.size(), 2);
		}

		SUBCASE("different stack anchors do not fold") {
			write_text_file(temp_log,
					"ERROR: Widget 1 failed\n"
					"   at: _process (res://ui.gd:12)\n"
					"gdscript backtrace (most recent call first):\n"
					"   [0] update_widget (res://ui.gd:7)\n"
					"ERROR: Widget 2 failed\n"
					"   at: _process (res://ui.gd:12)\n"
					"gdscript backtrace (most recent call first):\n"
					"   [0] refresh_widget (res://ui.gd:8)\n");

			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.format = CLILatestLogRunner::FORMAT_JSON;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);
			const Dictionary analysis = get_dictionary(result.summary, "analysis");
			CHECK_EQ(int(analysis["foldedRepeatCount"]), 0);
			const Array recent_issue_blocks = get_array(result.summary, "recentIssueBlocks");
			REQUIRE_EQ(recent_issue_blocks.size(), 2);
		}

		cleanup_path(temp_log);
	}

	TEST_CASE("Parses callsites and stack summaries with stable fingerprints") {
		const String temp_log = TestUtils::get_temp_path("cli_latest_log_runner_stack.log");
		cleanup_path(temp_log);
		write_text_file(temp_log,
				"SCRIPT ERROR: Invalid state 17\n"
				"   detail value 42\n"
				"   at: _ready (res://player.gd:14)\n"
				"gdscript backtrace (most recent call first):\n"
				"   [0] init_player (res://player.gd:7)\n"
				"   [1] _enter_tree (res://main.gd:3)\n");

		CLILatestLogRunner::Options options;
		options.enabled = true;
		options.format = CLILatestLogRunner::FORMAT_JSON;

		const BuildOutputResult first = build_output_for_log(temp_log, options);
		const BuildOutputResult second = build_output_for_log(temp_log, options);
		REQUIRE_EQ(first.err, OK);
		REQUIRE_EQ(second.err, OK);

		const Array recent_issue_blocks = get_array(first.summary, "recentIssueBlocks");
		REQUIRE_EQ(recent_issue_blocks.size(), 1);
		const Dictionary issue = recent_issue_blocks[0];
		const Dictionary callsite = issue["callsite"];
		CHECK_EQ(String(callsite["function"]), String("_ready"));
		CHECK_EQ(String(callsite["file"]), String("res://player.gd"));
		CHECK_EQ(int(callsite["line"]), 14);

		const Dictionary stack_summary = issue["stackSummary"];
		CHECK_EQ(String(stack_summary["header"]), String("gdscript backtrace (most recent call first):"));
		const Array frames = stack_summary["frames"];
		REQUIRE_EQ(frames.size(), 2);
		CHECK_EQ(int(((Dictionary)frames[0])["index"]), 0);
		CHECK_EQ(String(((Dictionary)frames[0])["function"]), String("init_player"));
		CHECK_EQ(String(((Dictionary)frames[1])["file"]), String("res://main.gd"));
		const Array second_recent_issue_blocks = get_array(second.summary, "recentIssueBlocks");
		REQUIRE_EQ(second_recent_issue_blocks.size(), 1);
		const Dictionary second_issue = second_recent_issue_blocks[0];
		CHECK_EQ(String(issue["stackFingerprint"]), String(second_issue["stackFingerprint"]));

		cleanup_path(temp_log);
	}

	TEST_CASE("Serializes text, json and both modes with the expected contract") {
		const String temp_log = TestUtils::get_temp_path("cli_latest_log_runner_formats.log");
		cleanup_path(temp_log);
		write_text_file(temp_log,
				"WARNING: Too many nodes 4\n"
				"   at: _ready (res://scene.gd:5)\n");

		SUBCASE("text output prints the semantic summary") {
			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.format = CLILatestLogRunner::FORMAT_TEXT;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);
			CHECK(result.output.contains("[latest-log] path="));
			CHECK(result.output.contains("[latest-log] format=text"));
			CHECK(result.output.contains("Recent issue blocks:"));
		}

		SUBCASE("json output is a single JSON line with the expected top level keys") {
			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.format = CLILatestLogRunner::FORMAT_JSON;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);
			CHECK_FALSE(result.output.contains("[latest-log] path="));

			const Variant parsed = JSON::parse_string(result.output.strip_edges());
			REQUIRE(parsed.get_type() == Variant::DICTIONARY);
			const Dictionary summary = parsed;
			CHECK(summary.has("path"));
			CHECK(summary.has("basePath"));
			CHECK(summary.has("format"));
			CHECK(summary.has("analysis"));
			CHECK(summary.has("counts"));
			CHECK(summary.has("keywordHits"));
			CHECK(summary.has("topTemplates"));
			CHECK(summary.has("recentIssueBlocks"));
			CHECK_FALSE(summary.has("fallbackTail"));
		}

		SUBCASE("both output keeps JSON on the last line") {
			CLILatestLogRunner::Options options;
			options.enabled = true;
			options.format = CLILatestLogRunner::FORMAT_BOTH;

			const BuildOutputResult result = build_output_for_log(temp_log, options);
			REQUIRE_EQ(result.err, OK);
			CHECK(result.output.contains("[latest-log] path="));
			const Dictionary summary = parse_last_json_line(result.output);
			CHECK_EQ(String(summary["format"]), String("both"));
			CHECK_EQ(int(get_dictionary(summary, "counts")["warning"]), 1);
		}
		cleanup_path(temp_log);
	}
}

} // namespace TestCLILatestLogRunner
