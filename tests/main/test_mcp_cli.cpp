/**************************************************************************/
/*  test_mcp_cli.cpp                                                      */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_cli);

#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/mcp/mcp_protocol.h"
#include "main/mcp_cli.h"

namespace TestMCPCLI {

PackedByteArray bytes_from_string(const String &p_text) {
	const CharString utf8 = p_text.utf8();
	PackedByteArray bytes;
	bytes.resize(utf8.length());
	if (utf8.length() > 0) {
		memcpy(bytes.ptrw(), utf8.get_data(), utf8.length());
	}
	return bytes;
}

String find_request_header(const Vector<String> &p_headers, const String &p_name) {
	for (const String &header : p_headers) {
		const int separator = header.find_char(':');
		if (separator > 0 && header.substr(0, separator).strip_edges().to_lower() == p_name.to_lower()) {
			return header.substr(separator + 1).strip_edges();
		}
	}
	return String();
}

class MemoryCLIIO final : public MCPCLIIO {
public:
	Vector<String> input_lines;
	Vector<String> stdout_lines;
	Vector<String> stderr_lines;
	int next_input = 0;

	ReadStatus read_line(String &r_line, uint64_t p_max_line_bytes) override {
		if (next_input >= input_lines.size()) {
			return READ_EOF;
		}
		r_line = input_lines[next_input++];
		return (uint64_t)r_line.utf8().length() > p_max_line_bytes ? READ_TOO_LARGE : READ_LINE;
	}

	void write_stdout_line(const String &p_line) override {
		stdout_lines.push_back(p_line);
	}

	void write_stderr_line(const String &p_line) override {
		stderr_lines.push_back(p_line);
	}
};

class QueueCLITransport final : public MCPCLITransport {
public:
	struct Request {
		String endpoint;
		Vector<String> headers;
		String body;
		uint64_t timeout_ms = 0;
		uint64_t max_response_bytes = 0;
	};

	Vector<Request> requests;
	Vector<MCPCLIHTTPResponse> responses;
	int next_response = 0;

	Error post(
			const String &p_endpoint,
			const Vector<String> &p_headers,
			const String &p_body,
			uint64_t p_timeout_ms,
			uint64_t p_max_response_bytes,
			MCPCLIHTTPResponse &r_response,
			String &r_error) override {
		Request request;
		request.endpoint = p_endpoint;
		request.headers = p_headers;
		request.body = p_body;
		request.timeout_ms = p_timeout_ms;
		request.max_response_bytes = p_max_response_bytes;
		requests.push_back(request);
		if (next_response >= responses.size()) {
			r_error = "No queued response.";
			return ERR_UNAVAILABLE;
		}
		r_response = responses[next_response++];
		return OK;
	}
};

MCPCLIHTTPResponse make_response(int p_status, const String &p_body = String(), const String &p_session_id = String()) {
	MCPCLIHTTPResponse response;
	response.status_code = p_status;
	response.body = bytes_from_string(p_body);
	if (!p_session_id.is_empty()) {
		response.headers["Mcp-Session-Id"] = p_session_id;
	}
	return response;
}

void create_project_and_record(
		const Ref<DirAccess> &p_temporary_directory,
		const String &p_endpoint,
		MCPProjectIdentity &r_identity,
		MCPDiscoveryRecord &r_record,
		String &r_project_path,
		String &r_discovery_directory) {
	REQUIRE(p_temporary_directory->make_dir("project") == OK);
	r_project_path = p_temporary_directory->get_current_dir().path_join("project");
	r_discovery_directory = p_temporary_directory->get_current_dir().path_join("discovery");
	REQUIRE(MCPProjectIdentity::create(r_project_path, r_identity) == OK);
	r_record = MCPDiscoveryRecord::create(r_identity, "streamable-http", p_endpoint, "2025-06-18", "private-token", 1000);
}

TEST_CASE("[MCP][CLI] Discover routes through the command registry without exposing secrets") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_cli_discover", false, &error);
	REQUIRE(error == OK);

	MCPProjectIdentity identity;
	MCPDiscoveryRecord record;
	String project_path;
	String discovery_directory;
	create_project_and_record(
			temporary_directory,
			"http://127.0.0.1:35101/mcp",
			identity,
			record,
			project_path,
			discovery_directory);
	REQUIRE(MCPDiscovery::write_record(discovery_directory, record, true) == OK);

	MemoryCLIIO io;
	QueueCLITransport transport;
	MCPCLI::Options options;
	options.discovery_directory = discovery_directory;
	MCPCLI cli(options, &transport, &io);
	MCPCLICommandRegistry registry;
	REQUIRE(cli.register_commands(registry) == OK);

	PackedStringArray arguments;
	arguments.push_back(project_path);
	int exit_code = -1;
	REQUIRE(registry.invoke(MCPCLI::COMMAND_DISCOVER, arguments, exit_code) == OK);
	CHECK(exit_code == MCPCLI::EXIT_OK);
	REQUIRE(io.stdout_lines.size() == 1);
	CHECK(io.stderr_lines.is_empty());
	CHECK(transport.requests.is_empty());

	const Variant parsed_output = JSON::parse_string(io.stdout_lines[0]);
	REQUIRE(parsed_output.get_type() == Variant::DICTIONARY);
	const Dictionary output = parsed_output;
	CHECK(output.get("projectId", String()) == identity.project_id);
	CHECK(output.get("instanceId", String()) == identity.instance_id);
	CHECK(output.get("endpoint", String()) == record.endpoint);
	CHECK(output.get("protocolVersion", String()) == record.protocol_version);
	CHECK(output.has("pid"));
	CHECK_FALSE(output.has("authSecret"));
	CHECK_FALSE(output.has("projectPath"));
}

TEST_CASE("[MCP][CLI] Stdio bridge keeps stdout JSON-RPC-only and carries the HTTP session") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_cli_stdio", false, &error);
	REQUIRE(error == OK);

	MCPProjectIdentity identity;
	MCPDiscoveryRecord record;
	String project_path;
	String discovery_directory;
	create_project_and_record(
			temporary_directory,
			"http://127.0.0.1:35201/mcp",
			identity,
			record,
			project_path,
			discovery_directory);
	REQUIRE(MCPDiscovery::write_record(discovery_directory, record, true) == OK);

	MemoryCLIIO io;
	io.input_lines.push_back(
			R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26",)"
			R"("capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
	io.input_lines.push_back(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
	io.input_lines.push_back(R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})");

	QueueCLITransport transport;
	transport.responses.push_back(make_response(
			200,
			"{\n  \"jsonrpc\": \"2.0\",\n  \"id\": 1,\n  \"result\": {\"protocolVersion\": \"2025-03-26\"}\n}",
			"session-abc"));
	transport.responses.push_back(make_response(202));
	transport.responses.push_back(make_response(200, R"({"jsonrpc":"2.0","id":2,"result":{"tools":[]}})"));

	MCPCLI::Options options;
	options.discovery_directory = discovery_directory;
	MCPCLI cli(options, &transport, &io);
	CHECK(cli.run_stdio_bridge(project_path) == MCPCLI::EXIT_OK);
	CHECK(io.stderr_lines.is_empty());
	REQUIRE(transport.requests.size() == 3);
	REQUIRE(io.stdout_lines.size() == 2);

	for (int i = 0; i < transport.requests.size(); i++) {
		CHECK(transport.requests[i].endpoint == record.endpoint);
		CHECK(find_request_header(transport.requests[i].headers, "authorization") == "Bearer private-token");
		CHECK(find_request_header(transport.requests[i].headers, "mcp-protocol-version") ==
				(i == 0 ? record.protocol_version : MCPProtocol::PROTOCOL_VERSION_2025_03_26));
		CHECK(find_request_header(transport.requests[i].headers, "content-type") == "application/json");
		CHECK(find_request_header(transport.requests[i].headers, "accept") ==
				"application/json, text/event-stream");
	}
	CHECK(find_request_header(transport.requests[0].headers, "mcp-session-id").is_empty());
	CHECK(find_request_header(transport.requests[1].headers, "mcp-session-id") == "session-abc");
	CHECK(find_request_header(transport.requests[2].headers, "mcp-session-id") == "session-abc");

	for (const String &stdout_line : io.stdout_lines) {
		CHECK_FALSE(stdout_line.contains("\n"));
		CHECK_FALSE(stdout_line.contains("\r"));
		const Variant response = JSON::parse_string(stdout_line);
		REQUIRE(response.get_type() == Variant::DICTIONARY);
		CHECK(Dictionary(response).get("jsonrpc", String()) == "2.0");
	}
}

TEST_CASE("[MCP][CLI] Stdio bridge rejects unsafe HTTP session identifiers") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_cli_session", false, &error);
	REQUIRE(error == OK);

	MCPProjectIdentity identity;
	MCPDiscoveryRecord record;
	String project_path;
	String discovery_directory;
	create_project_and_record(
			temporary_directory,
			"http://127.0.0.1:35301/mcp",
			identity,
			record,
			project_path,
			discovery_directory);
	REQUIRE(MCPDiscovery::write_record(discovery_directory, record, true) == OK);

	MemoryCLIIO io;
	io.input_lines.push_back(
			R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18",)"
			R"("capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");
	QueueCLITransport transport;
	transport.responses.push_back(make_response(
			200,
			R"({"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-06-18"}})",
			"unsafe session"));

	MCPCLI::Options options;
	options.discovery_directory = discovery_directory;
	MCPCLI cli(options, &transport, &io);
	CHECK(cli.run_stdio_bridge(project_path) == MCPCLI::EXIT_PROTOCOL_FAILED);
	CHECK(io.stdout_lines.is_empty());
	CHECK_FALSE(io.stderr_lines.is_empty());
}

TEST_CASE("[MCP][CLI] Missing and invalid discovery records only report diagnostics") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_cli_bad_discovery", false, &error);
	REQUIRE(error == OK);

	MCPProjectIdentity identity;
	MCPDiscoveryRecord record;
	String project_path;
	String discovery_directory;
	create_project_and_record(
			temporary_directory,
			"http://example.com/mcp",
			identity,
			record,
			project_path,
			discovery_directory);

	SUBCASE("Missing discovery record") {
		MemoryCLIIO io;
		QueueCLITransport transport;
		MCPCLI::Options options;
		options.discovery_directory = discovery_directory;
		MCPCLI cli(options, &transport, &io);
		CHECK(cli.run_discover(project_path) == MCPCLI::EXIT_DISCOVERY_FAILED);
		CHECK(io.stdout_lines.is_empty());
		CHECK_FALSE(io.stderr_lines.is_empty());
		CHECK(transport.requests.is_empty());
	}

	SUBCASE("Non-loopback endpoint is rejected before bearer transport") {
		REQUIRE(MCPDiscovery::write_record(discovery_directory, record, true) == OK);
		MemoryCLIIO io;
		QueueCLITransport transport;
		MCPCLI::Options options;
		options.discovery_directory = discovery_directory;
		MCPCLI cli(options, &transport, &io);
		CHECK(cli.run_stdio_bridge(project_path) == MCPCLI::EXIT_DISCOVERY_FAILED);
		CHECK(io.stdout_lines.is_empty());
		CHECK_FALSE(io.stderr_lines.is_empty());
		CHECK(transport.requests.is_empty());
	}

	SUBCASE("Unknown discovery transport is rejected before output") {
		record.endpoint = "http://127.0.0.1:35401/mcp";
		record.transport = "custom";
		REQUIRE(MCPDiscovery::write_record(discovery_directory, record, true) == OK);
		MemoryCLIIO io;
		QueueCLITransport transport;
		MCPCLI::Options options;
		options.discovery_directory = discovery_directory;
		MCPCLI cli(options, &transport, &io);
		CHECK(cli.run_discover(project_path) == MCPCLI::EXIT_DISCOVERY_FAILED);
		CHECK(io.stdout_lines.is_empty());
		CHECK_FALSE(io.stderr_lines.is_empty());
		CHECK(transport.requests.is_empty());
	}
}

} // namespace TestMCPCLI
