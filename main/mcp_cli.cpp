/**************************************************************************/
/*  mcp_cli.cpp                                                           */
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

#include "mcp_cli.h"

#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/io/stream_peer_tls.h"
#include "core/mcp/mcp_protocol.h"
#include "core/os/os.h"

#ifdef TOOLS_ENABLED
#include "editor/file_system/editor_paths.h"
#endif

namespace {

constexpr uint64_t STDIN_READ_CHUNK_BYTES = 8192;
constexpr uint64_t MAX_SESSION_ID_BYTES = 1024;
constexpr uint32_t HTTP_POLL_DELAY_USEC = 1000;

void set_error(String *r_error, const String &p_error) {
	if (r_error) {
		*r_error = p_error;
	}
}

bool has_timed_out(uint64_t p_started_at_usec, uint64_t p_timeout_ms) {
	return OS::get_singleton()->get_ticks_usec() - p_started_at_usec >= p_timeout_ms * 1000;
}

bool is_http_failure_status(HTTPClient::Status p_status) {
	return p_status == HTTPClient::STATUS_CANT_RESOLVE ||
			p_status == HTTPClient::STATUS_CANT_CONNECT ||
			p_status == HTTPClient::STATUS_CONNECTION_ERROR ||
			p_status == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR;
}

String find_header(const Dictionary &p_headers, const String &p_name) {
	const String normalized_name = p_name.to_lower();
	for (const Variant &key_value : p_headers.keys()) {
		if (String(key_value).to_lower() == normalized_name) {
			return p_headers[key_value];
		}
	}
	return String();
}

bool contains_header_break(const String &p_value) {
	return p_value.contains("\r") || p_value.contains("\n");
}

bool is_visible_ascii_header_value(const String &p_value) {
	if (p_value.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_value.length(); i++) {
		if (p_value[i] < 0x21 || p_value[i] > 0x7e) {
			return false;
		}
	}
	return true;
}

bool is_jsonrpc_response(const Variant &p_value) {
	if (p_value.get_type() == Variant::ARRAY) {
		const Array responses = p_value;
		if (responses.is_empty()) {
			return false;
		}
		for (const Variant &response : responses) {
			if (!is_jsonrpc_response(response)) {
				return false;
			}
		}
		return true;
	}
	if (p_value.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary response = p_value;
	return response.get("jsonrpc", String()) == "2.0" && (response.has("result") != response.has("error"));
}

Error parse_endpoint(
		const String &p_endpoint,
		String &r_scheme,
		String &r_host,
		int &r_port,
		String &r_path,
		String &r_fragment,
		String &r_error) {
	if (p_endpoint.parse_url(r_scheme, r_host, r_port, r_path, r_fragment) != OK ||
			(r_scheme != "http://" && r_scheme != "https://")) {
		r_error = "MCP endpoint must be an HTTP or HTTPS URL.";
		return ERR_INVALID_DATA;
	}
	if (r_port == 0) {
		r_port = r_scheme == "https://" ? 443 : 80;
	}
	if (r_path.is_empty()) {
		r_path = "/";
	}
	return OK;
}

} // namespace

Error MCPHTTPCLITransport::post(
		const String &p_endpoint,
		const Vector<String> &p_headers,
		const String &p_body,
		uint64_t p_timeout_ms,
		uint64_t p_max_response_bytes,
		MCPCLIHTTPResponse &r_response,
		String &r_error) {
	r_response = MCPCLIHTTPResponse();
	r_error = String();
	if (p_timeout_ms == 0 || p_max_response_bytes == 0) {
		r_error = "MCP HTTP timeout and response limit must be positive.";
		return ERR_INVALID_PARAMETER;
	}

	String scheme;
	String host;
	String path;
	String fragment;
	int port = 0;
	Error error = parse_endpoint(p_endpoint, scheme, host, port, path, fragment, r_error);
	if (error != OK) {
		return error;
	}
	if (!fragment.is_empty()) {
		r_error = "MCP HTTP endpoint must not contain a fragment.";
		return ERR_INVALID_DATA;
	}

	Ref<HTTPClient> client = HTTPClient::create();
	if (client.is_null()) {
		r_error = "Cannot create an HTTP client for the MCP stdio bridge.";
		return ERR_CANT_CREATE;
	}
	client->set_blocking_mode(false);
	Ref<TLSOptions> tls_options;
	if (scheme == "https://") {
		tls_options = TLSOptions::client();
	}
	error = client->connect_to_host(host, port, tls_options);
	if (error != OK) {
		r_error = vformat("Cannot begin connection to MCP endpoint '%s'.", p_endpoint);
		return error;
	}

	const uint64_t started_at_usec = OS::get_singleton()->get_ticks_usec();
	while (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		if (is_http_failure_status(client->get_status())) {
			r_error = vformat("Cannot connect to MCP endpoint '%s'.", p_endpoint);
			client->close();
			return ERR_CANT_CONNECT;
		}
		if (has_timed_out(started_at_usec, p_timeout_ms)) {
			r_error = vformat("Timed out connecting to MCP endpoint '%s'.", p_endpoint);
			client->close();
			return ERR_TIMEOUT;
		}
		error = client->poll();
		if (error != OK) {
			r_error = vformat("HTTP polling failed while connecting to MCP endpoint '%s'.", p_endpoint);
			client->close();
			return error;
		}
		OS::get_singleton()->delay_usec(HTTP_POLL_DELAY_USEC);
	}

	Vector<String> request_headers = p_headers;
	const String host_header = host.contains(":") ? "[" + host + "]" : host;
	request_headers.push_back("Host: " + host_header + ":" + itos(port));
	const CharString request_body = p_body.utf8();
	error = client->request(
			HTTPClient::METHOD_POST,
			path,
			request_headers,
			(const uint8_t *)request_body.get_data(),
			request_body.length());
	if (error != OK) {
		r_error = "Cannot send MCP HTTP request.";
		client->close();
		return error;
	}

	while (client->get_status() == HTTPClient::STATUS_REQUESTING) {
		if (has_timed_out(started_at_usec, p_timeout_ms)) {
			r_error = "Timed out waiting for MCP HTTP response headers.";
			client->close();
			return ERR_TIMEOUT;
		}
		error = client->poll();
		if (error != OK || is_http_failure_status(client->get_status())) {
			r_error = "MCP HTTP request failed before receiving response headers.";
			client->close();
			return error == OK ? ERR_CONNECTION_ERROR : error;
		}
		OS::get_singleton()->delay_usec(HTTP_POLL_DELAY_USEC);
	}

	if (!client->has_response()) {
		r_error = "MCP HTTP endpoint closed without a response.";
		client->close();
		return ERR_CONNECTION_ERROR;
	}
	r_response.status_code = client->get_response_code();
	const int64_t response_length = client->get_response_body_length();
	if (response_length > (int64_t)p_max_response_bytes) {
		r_error = "MCP HTTP response exceeds the configured size limit.";
		client->close();
		return ERR_OUT_OF_MEMORY;
	}

	List<String> response_headers;
	if (client->get_response_headers(&response_headers) == OK) {
		for (const String &header : response_headers) {
			const int separator = header.find_char(':');
			if (separator > 0) {
				r_response.headers[header.substr(0, separator).strip_edges().to_lower()] =
						header.substr(separator + 1).strip_edges();
			}
		}
	}

	while (client->get_status() == HTTPClient::STATUS_BODY) {
		const PackedByteArray chunk = client->read_response_body_chunk();
		if (!chunk.is_empty()) {
			if ((uint64_t)r_response.body.size() + (uint64_t)chunk.size() > p_max_response_bytes) {
				r_error = "MCP HTTP response exceeds the configured size limit.";
				client->close();
				return ERR_OUT_OF_MEMORY;
			}
			const int old_size = r_response.body.size();
			r_response.body.resize(old_size + chunk.size());
			memcpy(r_response.body.ptrw() + old_size, chunk.ptr(), chunk.size());
			continue;
		}

		if (has_timed_out(started_at_usec, p_timeout_ms)) {
			r_error = "Timed out reading MCP HTTP response body.";
			client->close();
			return ERR_TIMEOUT;
		}
		error = client->poll();
		if (error != OK) {
			r_error = "MCP HTTP response body polling failed.";
			client->close();
			return error;
		}
		OS::get_singleton()->delay_usec(HTTP_POLL_DELAY_USEC);
	}
	client->close();

	if (response_length >= 0 && r_response.body.size() != response_length) {
		r_error = "MCP HTTP response body ended before Content-Length bytes were received.";
		return ERR_CONNECTION_ERROR;
	}
	return OK;
}

MCPCLIIO::ReadStatus MCPOSCLIIO::read_line(String &r_line, uint64_t p_max_line_bytes) {
	r_line = String();
	if (p_max_line_bytes == 0) {
		return READ_TOO_LARGE;
	}

	while (true) {
		int line_end = -1;
		for (int i = 0; i < pending_input.size(); i++) {
			if (pending_input[i] == '\n') {
				line_end = i;
				break;
			}
		}

		if (line_end >= 0 || (input_finished && !pending_input.is_empty())) {
			const int consumed_bytes = line_end >= 0 ? line_end + 1 : pending_input.size();
			int line_bytes = line_end >= 0 ? line_end : pending_input.size();
			if (line_bytes > 0 && pending_input[line_bytes - 1] == '\r') {
				line_bytes--;
			}
			const bool too_large = (uint64_t)line_bytes > p_max_line_bytes;
			Error decode_error = OK;
			if (!too_large && line_bytes > 0) {
				decode_error = r_line.append_utf8((const char *)pending_input.ptr(), line_bytes);
			}
			pending_input = pending_input.slice(consumed_bytes, pending_input.size());
			if (too_large) {
				return READ_TOO_LARGE;
			}
			return decode_error == OK ? READ_LINE : READ_INVALID_ENCODING;
		}

		if ((uint64_t)pending_input.size() > p_max_line_bytes) {
			pending_input.clear();
			return READ_TOO_LARGE;
		}
		if (input_finished) {
			return READ_EOF;
		}

		PackedByteArray chunk = OS::get_singleton()->get_stdin_buffer(STDIN_READ_CHUNK_BYTES);
		int chunk_size = chunk.size();
		// Windows keeps the requested buffer size and zero-fills bytes beyond the read count.
		for (int i = 0; i < chunk_size; i++) {
			if (chunk[i] == 0) {
				chunk_size = i;
				break;
			}
		}
		if (chunk_size == 0) {
			input_finished = true;
			continue;
		}
		const int old_size = pending_input.size();
		pending_input.resize(old_size + chunk_size);
		memcpy(pending_input.ptrw() + old_size, chunk.ptr(), chunk_size);
	}
}

void MCPOSCLIIO::write_stdout_line(const String &p_line) {
	OS::get_singleton()->print("%s\n", p_line.utf8().get_data());
}

void MCPOSCLIIO::write_stderr_line(const String &p_line) {
	OS::get_singleton()->printerr("%s\n", p_line.utf8().get_data());
}

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

Error MCPCLI::register_commands(MCPCLICommandRegistry &r_registry, String *r_error) {
	MCPCLICommandDefinition discover;
	discover.name = COMMAND_DISCOVER;
	discover.usage = "--mcp-discover --path <directory>";
	discover.description = "Print the running MCP endpoint for a Godot project.";
	Error error = r_registry.register_command(discover, this, r_error);
	if (error != OK) {
		return error;
	}

	MCPCLICommandDefinition stdio;
	stdio.name = COMMAND_STDIO;
	stdio.usage = "--mcp-stdio --path <directory>";
	stdio.description = "Bridge newline-delimited MCP stdio to the project's HTTP endpoint.";
	error = r_registry.register_command(stdio, this, r_error);
	if (error != OK) {
		r_registry.unregister_command(COMMAND_DISCOVER);
	}
	return error;
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
	if (!is_visible_ascii_header_value(p_record.auth_secret)) {
		r_error = "MCP discovery record does not contain a valid authentication secret.";
		return ERR_INVALID_DATA;
	}
	if (contains_header_break(p_record.protocol_version) || contains_header_break(p_record.auth_secret)) {
		r_error = "MCP discovery record contains an invalid header value.";
		return ERR_INVALID_DATA;
	}
	String scheme;
	String host;
	String path;
	String fragment;
	int port = 0;
	Error error = parse_endpoint(p_record.endpoint, scheme, host, port, path, fragment, r_error);
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

int MCPCLI::_run_stdio_bridge(const MCPDiscoveryRecord &p_record) {
	String session_id;
	String protocol_version = p_record.protocol_version;
	while (true) {
		String line;
		const MCPCLIIO::ReadStatus read_status = io->read_line(line, options.max_line_bytes);
		if (read_status == MCPCLIIO::READ_EOF) {
			return EXIT_OK;
		}
		if (read_status == MCPCLIIO::READ_TOO_LARGE) {
			_diagnose("stdin JSON-RPC line exceeds the configured size limit.");
			return EXIT_PROTOCOL_FAILED;
		}
		if (read_status == MCPCLIIO::READ_INVALID_ENCODING) {
			_diagnose("stdin contains invalid UTF-8.");
			return EXIT_PROTOCOL_FAILED;
		}
		if (line.strip_edges().is_empty()) {
			continue;
		}
		if ((uint64_t)line.utf8().length() > options.max_line_bytes) {
			_diagnose("stdin JSON-RPC line exceeds the configured size limit.");
			return EXIT_PROTOCOL_FAILED;
		}

		JSON request_json;
		if (request_json.parse(line) != OK ||
				(request_json.get_data().get_type() != Variant::DICTIONARY &&
						request_json.get_data().get_type() != Variant::ARRAY)) {
			_diagnose("stdin line is not a JSON-RPC object or batch.");
			return EXIT_PROTOCOL_FAILED;
		}

		const Variant request_data = request_json.get_data();
		const bool initialize_request = request_data.get_type() == Variant::DICTIONARY &&
				Dictionary(request_data).get("method", String()) == "initialize";

		Vector<String> headers;
		headers.push_back("Content-Type: application/json");
		headers.push_back("Accept: application/json, text/event-stream");
		headers.push_back("MCP-Protocol-Version: " + protocol_version);
		if (!p_record.auth_secret.is_empty()) {
			headers.push_back("Authorization: Bearer " + p_record.auth_secret);
		}
		if (!session_id.is_empty()) {
			headers.push_back("Mcp-Session-Id: " + session_id);
		}

		MCPCLIHTTPResponse response;
		String transport_error;
		const Error post_error = transport->post(
				p_record.endpoint,
				headers,
				JSON::stringify(request_json.get_data()),
				options.request_timeout_ms,
				options.max_response_bytes,
				response,
				transport_error);
		if (post_error != OK) {
			_diagnose(transport_error.is_empty() ? "HTTP transport failed." : transport_error);
			return EXIT_TRANSPORT_FAILED;
		}

		const String response_session_id = find_header(response.headers, "mcp-session-id");
		if (!response_session_id.is_empty()) {
			if ((uint64_t)response_session_id.utf8().length() > MAX_SESSION_ID_BYTES ||
					!is_visible_ascii_header_value(response_session_id)) {
				_diagnose("HTTP response contains an invalid Mcp-Session-Id header.");
				return EXIT_PROTOCOL_FAILED;
			}
			session_id = response_session_id;
		}
		if (response.status_code == HTTPClient::RESPONSE_ACCEPTED) {
			continue;
		}
		if (response.status_code != HTTPClient::RESPONSE_OK) {
			_diagnose(vformat("HTTP endpoint returned status %d.", response.status_code));
			return EXIT_TRANSPORT_FAILED;
		}

		String response_text;
		if (!response.body.is_empty() &&
				response_text.append_utf8((const char *)response.body.ptr(), response.body.size()) != OK) {
			_diagnose("HTTP response body is not valid UTF-8.");
			return EXIT_PROTOCOL_FAILED;
		}
		JSON response_json;
		if (response_text.is_empty() || response_json.parse(response_text) != OK ||
				!is_jsonrpc_response(response_json.get_data())) {
			_diagnose("HTTP response body is not a JSON-RPC response.");
			return EXIT_PROTOCOL_FAILED;
		}
		if (initialize_request && response_json.get_data().get_type() == Variant::DICTIONARY) {
			const Dictionary response_dictionary = response_json.get_data();
			const Dictionary initialize_result = response_dictionary.get("result", Dictionary());
			const Variant negotiated_version = initialize_result.get("protocolVersion", Variant());
			if (negotiated_version.get_type() == Variant::STRING) {
				if (!MCPProtocol::is_supported_protocol_version(negotiated_version)) {
					_diagnose("HTTP initialize response selected an unsupported protocol version.");
					return EXIT_PROTOCOL_FAILED;
				}
				protocol_version = negotiated_version;
			}
		}
		io->write_stdout_line(JSON::stringify(response_json.get_data()));
	}
}

int MCPCLI::run_stdio_bridge(const String &p_project_path) {
	MCPProjectIdentity identity;
	MCPDiscoveryRecord record;
	String error;
	if (resolve_discovery_record(p_project_path, identity, record, &error) != OK) {
		_diagnose(error);
		return EXIT_DISCOVERY_FAILED;
	}
	return _run_stdio_bridge(record);
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
