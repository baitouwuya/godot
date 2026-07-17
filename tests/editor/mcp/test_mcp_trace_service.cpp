/**************************************************************************/
/*  test_mcp_trace_service.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "editor/mcp/providers/mcp_trace_service.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_trace_service);

namespace TestMCPTraceService {

static Array _read_events(const String &p_session_directory) {
	const Dictionary manifest = JSON::parse_string(FileAccess::get_file_as_string(p_session_directory.path_join("manifest.json")));
	const Array files = manifest.get("eventFiles", Array());
	Array events;
	for (const Variant &file_value : files) {
		const PackedStringArray lines = FileAccess::get_file_as_string(p_session_directory.path_join(String(file_value))).split("\n", false);
		for (const String &line : lines) {
			if (!line.strip_edges().is_empty()) {
				events.push_back(JSON::parse_string(line));
			}
		}
	}
	return events;
}

TEST_CASE("[MCP] Trace service is opt-in and records a single explicit lifecycle") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_trace_service", false, &error);
	REQUIRE(error == OK);
	const String root = temporary_directory->get_current_dir().path_join("traces");
	MCPTraceService service;
	CHECK_FALSE(service.is_started());
	CHECK(service.record_mcp_request("ping", "raw-session", "1", Dictionary()) == OK);
	CHECK(service.record_tool_call("godot.test", "raw-session", "2", Dictionary()) == OK);
	CHECK(service.record_job_event("job", "started", "info", Dictionary()) == OK);
	CHECK_FALSE(DirAccess::dir_exists_absolute(root));

	Dictionary project;
	project["projectId"] = "project";
	project["projectPath"] = "C:/private/project/project.godot";
	project["authToken"] = "project-token";
	REQUIRE(service.start(project, "editor", root) == OK);
	CHECK(service.is_started());
	const String session_directory = service.get_session_directory();
	CHECK(DirAccess::dir_exists_absolute(session_directory));

	Dictionary nested;
	nested["password"] = "nested-password";
	nested["filePath"] = "C:/users/private/source.gd";
	nested["resourcePath"] = "res://scenes/main.tscn";
	Dictionary arguments;
	arguments["authorization"] = "Bearer raw-auth";
	arguments["nested"] = nested;
	Dictionary result;
	result["ok"] = true;
	Dictionary trace_error;
	trace_error["secret"] = "error-secret";
	const String raw_session = "session-secret-value";
	REQUIRE(service.record_mcp_request("tools/call", raw_session, "request-1", arguments, result) == OK);
	REQUIRE(service.record_tool_call("godot.scene.get_tree", raw_session, "request-1", arguments, result, trace_error) == OK);
	REQUIRE(service.record_job_event("job-1", "completed", "info", result) == OK);
	service.stop();
	service.stop();
	CHECK_FALSE(service.is_started());

	const String manifest_text = FileAccess::get_file_as_string(session_directory.path_join("manifest.json"));
	CHECK_FALSE(manifest_text.contains("project-token"));
	CHECK_FALSE(manifest_text.contains("C:/private/project"));
	CHECK(manifest_text.contains("project.godot"));
	const Dictionary manifest = JSON::parse_string(manifest_text);
	CHECK_FALSE(String(manifest.get("endedAtUtc", String())).is_empty());

	const Array events = _read_events(session_directory);
	REQUIRE(events.size() == 3);
	String serialized;
	for (const Variant &event : events) {
		serialized += JSON::stringify(event);
	}
	CHECK_FALSE(serialized.contains("raw-auth"));
	CHECK_FALSE(serialized.contains("nested-password"));
	CHECK_FALSE(serialized.contains("error-secret"));
	CHECK_FALSE(serialized.contains(raw_session));
	CHECK_FALSE(serialized.contains("C:/users/private"));
	CHECK(serialized.contains("<redacted>"));
	CHECK(serialized.contains(raw_session.sha256_text().left(16)));
	CHECK(serialized.contains("source.gd"));
	CHECK(serialized.contains("res://scenes/main.tscn"));
	CHECK(Dictionary(events[0]).get("category", String()) == "mcp");
	CHECK(Dictionary(events[1]).get("category", String()) == "tool");
	CHECK(Dictionary(events[2]).get("category", String()) == "job");
}

TEST_CASE("[MCP] Trace service bounds structured fields and summarizes binary payloads") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_trace_bounds", false, &error);
	REQUIRE(error == OK);
	MCPTraceService service;
	REQUIRE(service.start(Dictionary(), "editor", temporary_directory->get_current_dir()) == OK);

	Dictionary large_data;
	const String chunk = String("x").repeat(MCPTraceService::MAX_STRING_CHARACTERS);
	for (int i = 0; i < 40; i++) {
		large_data[vformat("field%02d", i)] = chunk;
	}
	PackedByteArray bytes;
	bytes.resize(256);
	for (int i = 0; i < bytes.size(); i++) {
		bytes.set(i, uint8_t(i));
	}
	Dictionary payloads;
	payloads["capture"] = bytes;
	REQUIRE(service.record_job_event("job-bounded", "sample", "info", large_data, payloads) == OK);
	CHECK(service.record_job_event("job-bounded", "bad", "fatal", Dictionary()) == ERR_INVALID_PARAMETER);
	const String session_directory = service.get_session_directory();
	service.stop();

	const Array events = _read_events(session_directory);
	REQUIRE(events.size() == 1);
	const Dictionary entry_data = Dictionary(events[0]).get("data", Dictionary());
	const Dictionary bounded_data = entry_data.get("data", Dictionary());
	CHECK(bool(bounded_data.get("_truncated", false)));
	CHECK(int64_t(bounded_data.get("originalBytes", 0)) > int64_t(MCPTraceService::MAX_STRUCTURED_FIELD_BYTES));
	CHECK_FALSE(String(bounded_data.get("sha256", String())).is_empty());
	const Dictionary bounded_payloads = entry_data.get("payloads", Dictionary());
	const Dictionary capture = bounded_payloads.get("capture", Dictionary());
	CHECK(capture.get("type", String()) == "bytes");
	CHECK(int(capture.get("bytes", 0)) == 256);
	CHECK(String(capture.get("sha256", String())).length() == 64);
	CHECK_FALSE(JSON::stringify(events[0]).contains(String("x").repeat(MCPTraceService::MAX_STRING_CHARACTERS + 1)));
}

} // namespace TestMCPTraceService
