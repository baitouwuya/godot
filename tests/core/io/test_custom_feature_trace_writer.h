/**************************************************************************/
/*  test_custom_feature_trace_writer.h                                    */
/**************************************************************************/

#pragma once

#include "core/io/custom_feature_trace_writer.h"

#include "tests/main/test_custom_feature_trace_helpers.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

namespace TestCustomFeatureTraceWriter {

static void write_text_file(const String &p_path, const String &p_text) {
	DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	REQUIRE_EQ(err, OK);
	REQUIRE(file.is_valid());
	file->store_string(p_text);
	file->close();
}

TEST_SUITE("[Core][CustomFeatureTraceWriter]") {
	TEST_CASE("Creates manifest and writes rolled event files with payload descriptors") {
		const String root_dir = TestUtils::get_temp_path("custom_feature_trace_writer_root");
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(root_dir);

		CustomFeatureTraceWriter writer;
		CustomFeatureTraceWriter::Options options;
		options.base_dir_override = root_dir;
		options.max_event_file_bytes = 256;

		CustomFeatureTraceWriter::SessionInfo session_info;
		session_info.binary_path = "godot-dev";
		session_info.cwd = "E:/GitHub/godot";
		session_info.project_path = "E:/GitHub/godot";
		session_info.process_mode = "cli_project_run";
		session_info.pid = 1234;

		String error;
		REQUIRE_EQ(writer.initialize(session_info, options, error), OK);
		REQUIRE(error.is_empty());

		Dictionary payloads;
		CustomFeatureTraceWriter::PayloadDescriptor inline_payload;
		REQUIRE_EQ(writer.make_text_payload("inline", "text/plain", "small payload", inline_payload, error), OK);
		payloads["inline"] = inline_payload.to_dictionary();

		String large_text;
		for (int i = 0; i < 500; i++) {
			large_text += "0123456789";
		}
		CustomFeatureTraceWriter::PayloadDescriptor sidecar_payload;
		REQUIRE_EQ(writer.make_text_payload("large", "application/json", large_text, sidecar_payload, error), OK);
		payloads["large"] = sidecar_payload.to_dictionary();

		CustomFeatureTraceWriter::EventRecord first_event;
		first_event.feature = "latest_log_cli";
		first_event.event = "summary";
		first_event.payloads = payloads;
		REQUIRE_EQ(writer.write_event(first_event, error), OK);

		CustomFeatureTraceWriter::EventRecord second_event;
		second_event.feature = "cli_perf_recorder";
		second_event.event = "summary";
		second_event.data["text"] = large_text;
		REQUIRE_EQ(writer.write_event(second_event, error), OK);
		writer.finalize();

		const String session_dir = writer.get_session_dir();
		const Dictionary manifest = TestCustomFeatureTraceHelpers::read_manifest(session_dir);
		CHECK_EQ(String(manifest["sessionId"]), writer.get_session_id());
		CHECK_EQ(String(manifest["processMode"]), String("cli_project_run"));
		CHECK_FALSE(String(manifest["endedAtUtc"]).is_empty());
		const Array features_seen = manifest["featuresSeen"];
		CHECK(features_seen.has("latest_log_cli"));
		CHECK(features_seen.has("cli_perf_recorder"));
		const Array event_files = manifest["eventFiles"];
		CHECK(event_files.size() >= 2);

		const Vector<Dictionary> events = TestCustomFeatureTraceHelpers::read_events(session_dir);
		REQUIRE_EQ(events.size(), 2);
		const Dictionary first_payloads = events[0]["payloads"];
		const Dictionary inline_descriptor = first_payloads["inline"];
		CHECK_EQ(String(inline_descriptor["inline"]), String("small payload"));
		const Dictionary sidecar_descriptor = first_payloads["large"];
		CHECK(sidecar_descriptor["inline"].get_type() == Variant::NIL);
		CHECK_FALSE(String(sidecar_descriptor["refPath"]).is_empty());
		CHECK(FileAccess::exists(session_dir.path_join(String(sidecar_descriptor["refPath"]))));

		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(root_dir);
	}

	TEST_CASE("Keeps only the configured number of recent session directories") {
		const String root_dir = TestUtils::get_temp_path("custom_feature_trace_writer_cleanup_root");
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(root_dir);
		REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(root_dir), OK);

		for (int i = 0; i < 4; i++) {
			const String old_dir = root_dir.path_join(vformat("old-session-%d", i));
			REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(old_dir.path_join("payloads")), OK);
			Error err = OK;
			Ref<FileAccess> file = FileAccess::open(old_dir.path_join("manifest.json"), FileAccess::WRITE, &err);
			REQUIRE_EQ(err, OK);
			file->store_string("{}");
			file->close();
		}

		CustomFeatureTraceWriter writer;
		CustomFeatureTraceWriter::Options options;
		options.base_dir_override = root_dir;
		options.max_sessions = 2;

		CustomFeatureTraceWriter::SessionInfo session_info;
		session_info.binary_path = "godot-dev";
		session_info.cwd = "E:/GitHub/godot";
		session_info.project_path = "E:/GitHub/godot";
		session_info.process_mode = "cmdline_tool";
		session_info.pid = 5678;

		String error;
		REQUIRE_EQ(writer.initialize(session_info, options, error), OK);
		writer.finalize();

		const PackedStringArray remaining_sessions = DirAccess::get_directories_at(root_dir);
		CHECK(remaining_sessions.size() <= 3);

		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(root_dir);
	}

	TEST_CASE("References existing files without copying them into the session") {
		const String root_dir = TestUtils::get_temp_path("custom_feature_trace_writer_reference_root");
		const String external_file = TestUtils::get_temp_path("custom_feature_trace_writer_reference.jsonl");
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(root_dir);
		if (FileAccess::exists(external_file)) {
			DirAccess::remove_absolute(external_file);
		}

		write_text_file(external_file, "{\"frame\":1}\n");

		CustomFeatureTraceWriter writer;
		CustomFeatureTraceWriter::Options options;
		options.base_dir_override = root_dir;

		CustomFeatureTraceWriter::SessionInfo session_info;
		session_info.binary_path = "godot-dev";
		session_info.cwd = "E:/GitHub/godot";
		session_info.project_path = "E:/GitHub/godot";
		session_info.process_mode = "cli_project_run";
		session_info.pid = 4321;

		String error;
		REQUIRE_EQ(writer.initialize(session_info, options, error), OK);

		CustomFeatureTraceWriter::PayloadDescriptor descriptor;
		REQUIRE_EQ(writer.reference_existing_file("application/jsonl", external_file, descriptor, error), OK);
		CHECK_EQ(descriptor.ref_path, external_file.simplify_path());
		CHECK_GT(descriptor.bytes, 0);
		CHECK_FALSE(descriptor.sha256.is_empty());
		writer.finalize();

		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(root_dir);
		DirAccess::remove_absolute(external_file);
	}
}

} // namespace TestCustomFeatureTraceWriter
