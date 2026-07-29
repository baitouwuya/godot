/**************************************************************************/
/*  mcp_cli.cpp                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_cli.h"

#include "core/io/json.h"
#include "core/mcp/mcp_protocol.h"
#include "main/mcp_cli_stdio_bridge.h"

#ifdef TOOLS_ENABLED
#include "editor/file_system/editor_paths.h"
#endif

namespace {

void set_error(String *r_error, const String &p_error) {
	if (r_error) {
		*r_error = p_error;
	}
}

} // namespace

MCPCLI::MCPCLI() :
		transport(&default_transport), io(&default_io) {
}

MCPCLI::MCPCLI(const Options &p_options, MCPCLITransport *p_transport, MCPCLIIO *p_io) :
		options(p_options), transport(p_transport ? p_transport : &default_transport), io(p_io ? p_io : &default_io) {
}

String MCPCLI::get_default_discovery_directory() {
#ifdef TOOLS_ENABLED
	EditorPaths *editor_paths = EditorPaths::get_singleton();
	if (editor_paths && editor_paths->are_paths_valid()) {
		return editor_paths->get_data_dir().path_join("mcp").path_join("discovery");
	}
#endif
	return String();
}

Dictionary MCPCLI::make_public_discovery_output(const MCPDiscoveryRecord &p_record) {
	Dictionary output;
	output["projectId"] = p_record.project_id;
	output["instanceId"] = p_record.instance_id;
	output["pid"] = p_record.pid;
	output["endpoint"] = p_record.endpoint;
	output["protocolVersion"] = p_record.protocol_version;
	return output;
}

Error MCPCLI::_get_project_path_argument(
		const PackedStringArray &p_arguments,
		String &r_project_path,
		String &r_error) const {
	r_project_path = String();
	r_error = String();
	if (p_arguments.size() == 1 && !p_arguments[0].is_empty()) {
		r_project_path = p_arguments[0];
		return OK;
	}
	if (p_arguments.size() == 2 && p_arguments[0] == "--path" && !p_arguments[1].is_empty()) {
		r_project_path = p_arguments[1];
		return OK;
	}
	r_error = "MCP CLI commands require one explicit project path.";
	return ERR_INVALID_PARAMETER;
}

Error MCPCLI::_validate_discovery_record(
		const MCPProjectIdentity &p_identity,
		const MCPDiscoveryRecord &p_record,
		String &r_error) const {
	if (!p_record.is_valid(true) || p_record.project_id != p_identity.project_id ||
			p_record.project_path != p_identity.canonical_path) {
		r_error = "MCP discovery record does not match the requested project.";
		return ERR_INVALID_DATA;
	}
	if (p_record.transport != "streamable-http") {
		r_error = "MCP discovery record does not describe a Streamable HTTP Host.";
		return ERR_INVALID_DATA;
	}
	if (!MCPProtocol::is_supported_protocol_version(p_record.protocol_version)) {
		r_error = "MCP discovery record contains an unsupported protocol version.";
		return ERR_INVALID_DATA;
	}
	if (!MCPHTTPCLITransport::is_visible_ascii_header_value(p_record.auth_secret)) {
		r_error = "MCP discovery record does not contain a valid authentication secret.";
		return ERR_INVALID_DATA;
	}
	if (p_record.protocol_version.contains("\r") || p_record.protocol_version.contains("\n") ||
			p_record.auth_secret.contains("\r") || p_record.auth_secret.contains("\n")) {
		r_error = "MCP discovery record contains an invalid header value.";
		return ERR_INVALID_DATA;
	}
	String scheme;
	String host;
	String path;
	String fragment;
	int port = 0;
	Error error = MCPHTTPCLITransport::parse_endpoint(p_record.endpoint, scheme, host, port, path, fragment, r_error);
	if (error != OK) {
		return error;
	}
	if ((host != "127.0.0.1" && host != "localhost" && host != "::1") || path != "/mcp" || !fragment.is_empty()) {
		r_error = "MCP discovery endpoint must be a loopback /mcp URL without a fragment or query.";
		return ERR_INVALID_DATA;
	}
	const int authority_end = p_record.endpoint.find_char('/', p_record.endpoint.find("://") + 3);
	const String authority = authority_end < 0 ? p_record.endpoint : p_record.endpoint.substr(0, authority_end);
	if (authority.contains("@")) {
		r_error = "MCP discovery endpoint must not contain URL credentials.";
		return ERR_INVALID_DATA;
	}
	return OK;
}

Error MCPCLI::resolve_discovery_record(
		const String &p_project_path,
		MCPProjectIdentity &r_identity,
		MCPDiscoveryRecord &r_record,
		String *r_error) const {
	r_identity = MCPProjectIdentity();
	r_record = MCPDiscoveryRecord();
	set_error(r_error, String());

	String error_message;
	Error error = MCPProjectIdentity::create(p_project_path, r_identity, &error_message);
	if (error != OK) {
		set_error(r_error, error_message);
		return error;
	}
	const String discovery_directory = options.discovery_directory.is_empty() ? get_default_discovery_directory() : options.discovery_directory;
	if (discovery_directory.is_empty()) {
		set_error(r_error, "MCP discovery directory is unavailable.");
		return ERR_UNCONFIGURED;
	}
	error = MCPDiscovery::read_record(discovery_directory, r_identity.project_id, r_record, &error_message);
	if (error != OK) {
		set_error(
				r_error,
				error_message.is_empty() ? "No MCP discovery record exists for the requested project." : error_message);
		return error;
	}
	error = _validate_discovery_record(r_identity, r_record, error_message);
	if (error != OK) {
		set_error(r_error, error_message);
	}
	return error;
}

void MCPCLI::_diagnose(const String &p_message) {
	io->write_stderr_line("MCP CLI: " + p_message);
}

int MCPCLI::run_discover(const String &p_project_path) {
	MCPProjectIdentity identity;
	MCPDiscoveryRecord record;
	String error;
	if (resolve_discovery_record(p_project_path, identity, record, &error) != OK) {
		_diagnose(error);
		return EXIT_DISCOVERY_FAILED;
	}
	io->write_stdout_line(JSON::stringify(make_public_discovery_output(record)));
	return EXIT_OK;
}

int MCPCLI::run_stdio_bridge(const String &p_project_path) {
	MCPProjectIdentity identity;
	MCPDiscoveryRecord record;
	String error;
	if (resolve_discovery_record(p_project_path, identity, record, &error) != OK) {
		_diagnose(error);
		return EXIT_DISCOVERY_FAILED;
	}

	MCPCLIStdioBridge::Limits limits;
	limits.max_line_bytes = options.max_line_bytes;
	limits.max_response_bytes = options.max_response_bytes;
	limits.request_timeout_ms = options.request_timeout_ms;
	MCPCLIStdioBridge bridge(transport, io, record, limits);
	switch (bridge.run()) {
		case MCPCLIStdioBridge::RESULT_OK:
			return EXIT_OK;
		case MCPCLIStdioBridge::RESULT_TRANSPORT_FAILED:
			return EXIT_TRANSPORT_FAILED;
		case MCPCLIStdioBridge::RESULT_PROTOCOL_FAILED:
			return EXIT_PROTOCOL_FAILED;
	}
	return EXIT_PROTOCOL_FAILED;
}

int MCPCLI::execute_cli_command(const StringName &p_command, const PackedStringArray &p_arguments) {
	String project_path;
	String error;
	if (_get_project_path_argument(p_arguments, project_path, error) != OK) {
		_diagnose(error);
		return EXIT_INVALID_ARGUMENTS;
	}
	if (p_command == COMMAND_DISCOVER) {
		return run_discover(project_path);
	}
	if (p_command == COMMAND_STDIO) {
		return run_stdio_bridge(project_path);
	}
	_diagnose(vformat("unknown command '%s'.", p_command));
	return EXIT_INVALID_ARGUMENTS;
}
