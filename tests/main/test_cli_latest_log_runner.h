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
#include "core/os/os.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

namespace TestCLILatestLogRunner {

struct ParsedOptionsResult {
	CLILatestLogRunner::Options options;
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

TEST_SUITE("[Main][CLILatestLogRunner]") {
	TEST_CASE("Parses CLI arguments") {
		SUBCASE("latest log with default line count") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--latest-log" });
			CHECK(parsed.error.is_empty());
			CHECK(parsed.options.enabled);
			CHECK_EQ(parsed.options.lines, 200);
		}

		SUBCASE("latest log with explicit line count") {
			const ParsedOptionsResult parsed = parse_cli_options({ "--latest-log", "--latest-log-lines", "25" });
			CHECK(parsed.error.is_empty());
			CHECK(parsed.options.enabled);
			CHECK_EQ(parsed.options.lines, 25);
			CHECK(parsed.options.lines_set);
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
}

} // namespace TestCLILatestLogRunner
