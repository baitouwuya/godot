/**************************************************************************/
/*  mcp_cli.h                                                             */
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

#include "core/mcp/mcp_discovery.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#include "main/mcp_cli_command_registry.h"

struct MCPCLIHTTPResponse {
	int status_code = 0;
	Dictionary headers;
	PackedByteArray body;
};

class MCPCLITransport {
public:
	enum RequestMethod {
		REQUEST_POST,
		REQUEST_DELETE,
	};

	virtual Error request(
			RequestMethod p_method,
			const String &p_endpoint,
			const Vector<String> &p_headers,
			const String &p_body,
			uint64_t p_timeout_ms,
			uint64_t p_max_response_bytes,
			MCPCLIHTTPResponse &r_response,
			String &r_error) = 0;
	virtual ~MCPCLITransport() = default;
};

class MCPCLIIO {
public:
	enum ReadStatus {
		READ_LINE,
		READ_EOF,
		READ_TOO_LARGE,
		READ_INVALID_ENCODING,
	};

	virtual ReadStatus read_line(String &r_line, uint64_t p_max_line_bytes) = 0;
	virtual void write_stdout_line(const String &p_line) = 0;
	virtual void write_stderr_line(const String &p_line) = 0;
	virtual ~MCPCLIIO() = default;
};

class MCPHTTPCLITransport final : public MCPCLITransport {
public:
	Error request(
			RequestMethod p_method,
			const String &p_endpoint,
			const Vector<String> &p_headers,
			const String &p_body,
			uint64_t p_timeout_ms,
			uint64_t p_max_response_bytes,
			MCPCLIHTTPResponse &r_response,
			String &r_error) override;
};

class MCPOSCLIIO final : public MCPCLIIO {
	Vector<uint8_t> pending_input;
	bool input_finished = false;

public:
	ReadStatus read_line(String &r_line, uint64_t p_max_line_bytes) override;
	void write_stdout_line(const String &p_line) override;
	void write_stderr_line(const String &p_line) override;
};

class MCPCLI final {
public:
	enum ExitCode {
		EXIT_OK = 0,
		EXIT_INVALID_ARGUMENTS = 2,
		EXIT_DISCOVERY_FAILED = 3,
		EXIT_TRANSPORT_FAILED = 4,
		EXIT_PROTOCOL_FAILED = 5,
	};

	struct Options {
		String discovery_directory;
		uint64_t max_line_bytes = 1024 * 1024;
		uint64_t max_response_bytes = 4 * 1024 * 1024;
		uint64_t request_timeout_ms = 10000;
	};

	static constexpr const char *COMMAND_DISCOVER = MCP_CLI_COMMAND_DISCOVER;
	static constexpr const char *COMMAND_STDIO = MCP_CLI_COMMAND_STDIO;

private:
	Options options;
	MCPHTTPCLITransport default_transport;
	MCPOSCLIIO default_io;
	MCPCLITransport *transport = nullptr;
	MCPCLIIO *io = nullptr;

	Error _get_project_path_argument(const PackedStringArray &p_arguments, String &r_project_path, String &r_error) const;
	Error _validate_discovery_record(
			const MCPProjectIdentity &p_identity,
			const MCPDiscoveryRecord &p_record,
			String &r_error) const;
	int _run_stdio_bridge(const MCPDiscoveryRecord &p_record);
	void _diagnose(const String &p_message);

public:
	MCPCLI();
	MCPCLI(const Options &p_options, MCPCLITransport *p_transport = nullptr, MCPCLIIO *p_io = nullptr);
	MCPCLI(const MCPCLI &) = delete;
	MCPCLI &operator=(const MCPCLI &) = delete;
	MCPCLI(MCPCLI &&) = delete;
	MCPCLI &operator=(MCPCLI &&) = delete;

	static String get_default_discovery_directory();
	static Dictionary make_public_discovery_output(const MCPDiscoveryRecord &p_record);

	Error resolve_discovery_record(
			const String &p_project_path,
			MCPProjectIdentity &r_identity,
			MCPDiscoveryRecord &r_record,
			String *r_error = nullptr) const;
	int run_discover(const String &p_project_path);
	int run_stdio_bridge(const String &p_project_path);
	int execute_cli_command(const StringName &p_command, const PackedStringArray &p_arguments);
};
