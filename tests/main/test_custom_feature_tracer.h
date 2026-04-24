/**************************************************************************/
/*  test_custom_feature_tracer.h                                          */
/**************************************************************************/

#pragma once

#include "main/custom_feature_tracer.h"

#include "tests/main/test_custom_feature_trace_helpers.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

namespace TestCustomFeatureTracer {

TEST_SUITE("[Main][CustomFeatureTracer]") {
	TEST_CASE("Captures logger errors but ignores normal print output") {
		const String root_dir = TestUtils::get_temp_path("custom_feature_tracer_root");
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(root_dir);

		CustomFeatureTracer::StartupOptions options;
		options.base_dir_override = root_dir;
		options.binary_path = "godot-dev";
		options.cwd = "E:/GitHub/godot";
		options.project_path = "E:/GitHub/godot";
		options.process_mode = "cmdline_tool";
		options.pid = 999;

		String error;
		REQUIRE_EQ(CustomFeatureTracer::initialize_singleton(options, error), OK);
		REQUIRE(error.is_empty());
		REQUIRE(CustomFeatureTracer::has_singleton());
		const String session_dir = CustomFeatureTracer::get_singleton()->get_session_dir_for_tests();

		{
			CustomFeatureTracer::ScopedEventContext context(CustomFeatureTracer::FEATURE_LATEST_LOG_CLI, "trace-logger-1");
			ERR_PRINT("custom trace logger error");
			print_line("plain stdout should not be mirrored");
		}

		CustomFeatureTracer::shutdown_singleton();

		const Vector<Dictionary> events = TestCustomFeatureTraceHelpers::read_events(session_dir);
		REQUIRE_EQ(events.size(), 1);
		const Dictionary event = events[0];
		CHECK_EQ(String(event["feature"]), String("latest_log_cli"));
		CHECK_EQ(String(event["event"]), String("engine_log_error"));
		CHECK_EQ(String(event["severity"]), String("error"));
		CHECK_EQ(String(((Dictionary)event["error"])["code"]), String("engine_log_error"));

		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(root_dir);
	}
}

} // namespace TestCustomFeatureTracer
