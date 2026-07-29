/**************************************************************************/
/*  mcp_cli_stdio_bridge.cpp                                              */
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

#include "mcp_cli_stdio_bridge.h"

#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/mcp/mcp_protocol.h"

namespace {

constexpr uint64_t MAX_SESSION_ID_BYTES = 1024;

String find_header(const Dictionary &p_headers, const String &p_name) {
	const String normalized_name = p_name.to_lower();
	for (const Variant &key_value : p_headers.keys()) {
		if (String(key_value).to_lower() == normalized_name) {
			return p_headers[key_value];
		}
	}
	return String();
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

String compact_json_whitespace(const String &p_json) {
	String compacted;
	compacted.reserve(p_json.length());
	bool in_string = false;
	bool escaped = false;
	for (int i = 0; i < p_json.length(); i++) {
		const char32_t character = p_json[i];
		if (in_string) {
			compacted += character;
			if (escaped) {
				escaped = false;
			} else if (character == '\\') {
				escaped = true;
			} else if (character == '"') {
				in_string = false;
			}
			continue;
		}
		if (character == '"') {
			in_string = true;
			compacted += character;
		} else if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
			compacted += character;
		}
	}
	return compacted;
}

Vector<String> make_mcp_request_headers(const MCPDiscoveryRecord &p_record, const String &p_protocol_version, const String &p_session_id, bool p_has_body) {
	Vector<String> headers;
	if (p_has_body) {
		headers.push_back("Content-Type: application/json");
	}
	headers.push_back("Accept: application/json, text/event-stream");
	headers.push_back("MCP-Protocol-Version: " + p_protocol_version);
	if (!p_record.auth_secret.is_empty()) {
		headers.push_back("Authorization: Bearer " + p_record.auth_secret);
	}
	if (!p_session_id.is_empty()) {
		headers.push_back("Mcp-Session-Id: " + p_session_id);
	}
	return headers;
}

class MCPCLISessionGuard {
	MCPCLITransport *transport = nullptr;
	MCPCLIIO *io = nullptr;
	const MCPDiscoveryRecord *record = nullptr;
	const MCPCLIStdioBridge::Limits *limits = nullptr;
	String *protocol_version = nullptr;
	String *session_id = nullptr;

public:
	MCPCLISessionGuard(MCPCLITransport *p_transport, MCPCLIIO *p_io, const MCPDiscoveryRecord &p_record,
			const MCPCLIStdioBridge::Limits &p_limits, String &r_protocol_version, String &r_session_id) {
		transport = p_transport;
		io = p_io;
		record = &p_record;
		limits = &p_limits;
		protocol_version = &r_protocol_version;
		session_id = &r_session_id;
	}

	~MCPCLISessionGuard() {
		if (!transport || !io || !record || !limits || !protocol_version || !session_id || session_id->is_empty()) {
			return;
		}

		MCPCLIHTTPResponse response;
		String cleanup_error;
		const Vector<String> headers = make_mcp_request_headers(*record, *protocol_version, *session_id, false);
		const Error error = transport->request(
				MCPCLITransport::REQUEST_DELETE,
				record->endpoint,
				headers,
				String(),
				limits->request_timeout_ms,
				limits->max_response_bytes,
				response,
				cleanup_error);
		if (error != OK || (response.status_code != HTTPClient::RESPONSE_NO_CONTENT &&
					response.status_code != HTTPClient::RESPONSE_NOT_FOUND &&
					response.status_code != HTTPClient::RESPONSE_METHOD_NOT_ALLOWED)) {
			const String detail = error != OK ? (cleanup_error.is_empty() ? error_names[error] : cleanup_error) :
					vformat("HTTP endpoint returned status %d.", response.status_code);
			io->write_stderr_line("MCP CLI: Session cleanup failed: " + detail);
		}
		session_id->clear();
	}
};

} // namespace

MCPCLIStdioBridge::MCPCLIStdioBridge(MCPCLITransport *p_transport, MCPCLIIO *p_io, const MCPDiscoveryRecord &p_record, const Limits &p_limits) {
	transport = p_transport;
	io = p_io;
	record = p_record;
	limits = p_limits;
}

void MCPCLIStdioBridge::_diagnose(const String &p_message) const {
	if (io) {
		io->write_stderr_line("MCP CLI: " + p_message);
	}
}

MCPCLIStdioBridge::Result MCPCLIStdioBridge::run() {
	if (!transport || !io) {
		return RESULT_PROTOCOL_FAILED;
	}

	String session_id;
	String protocol_version = record.protocol_version;
	MCPCLISessionGuard session_guard(transport, io, record, limits, protocol_version, session_id);
	while (true) {
		String line;
		const MCPCLIIO::ReadStatus read_status = io->read_line(line, limits.max_line_bytes);
		if (read_status == MCPCLIIO::READ_EOF) {
			return RESULT_OK;
		}
		if (read_status == MCPCLIIO::READ_TOO_LARGE) {
			_diagnose("stdin JSON-RPC line exceeds the configured size limit.");
			return RESULT_PROTOCOL_FAILED;
		}
		if (read_status == MCPCLIIO::READ_INVALID_ENCODING) {
			_diagnose("stdin contains invalid UTF-8.");
			return RESULT_PROTOCOL_FAILED;
		}
		if (line.strip_edges().is_empty()) {
			continue;
		}
		if ((uint64_t)line.utf8().length() > limits.max_line_bytes) {
			_diagnose("stdin JSON-RPC line exceeds the configured size limit.");
			return RESULT_PROTOCOL_FAILED;
		}

		JSON request_json;
		if (request_json.parse(line) != OK ||
				(request_json.get_data().get_type() != Variant::DICTIONARY &&
						request_json.get_data().get_type() != Variant::ARRAY)) {
			_diagnose("stdin line is not a JSON-RPC object or batch.");
			return RESULT_PROTOCOL_FAILED;
		}

		const Variant request_data = request_json.get_data();
		const bool initialize_request = request_data.get_type() == Variant::DICTIONARY &&
				Dictionary(request_data).get("method", String()) == "initialize";
		const Vector<String> headers = make_mcp_request_headers(record, protocol_version, session_id, true);

		MCPCLIHTTPResponse response;
		String transport_error;
		const Error post_error = transport->request(
				MCPCLITransport::REQUEST_POST,
				record.endpoint,
				headers,
				line,
				limits.request_timeout_ms,
				limits.max_response_bytes,
				response,
				transport_error);
		if (post_error != OK) {
			_diagnose(transport_error.is_empty() ? "HTTP transport failed." : transport_error);
			return RESULT_TRANSPORT_FAILED;
		}

		const String response_session_id = find_header(response.headers, "mcp-session-id");
		if (!response_session_id.is_empty()) {
			if ((uint64_t)response_session_id.utf8().length() > MAX_SESSION_ID_BYTES ||
					!MCPHTTPCLITransport::is_visible_ascii_header_value(response_session_id)) {
				_diagnose("HTTP response contains an invalid Mcp-Session-Id header.");
				return RESULT_PROTOCOL_FAILED;
			}
			session_id = response_session_id;
		}
		if (response.status_code == HTTPClient::RESPONSE_ACCEPTED) {
			continue;
		}
		if (response.status_code != HTTPClient::RESPONSE_OK) {
			_diagnose(vformat("HTTP endpoint returned status %d.", response.status_code));
			return RESULT_TRANSPORT_FAILED;
		}

		String response_text;
		if (!response.body.is_empty() &&
				response_text.append_utf8((const char *)response.body.ptr(), response.body.size()) != OK) {
			_diagnose("HTTP response body is not valid UTF-8.");
			return RESULT_PROTOCOL_FAILED;
		}
		JSON response_json;
		if (response_text.is_empty() || response_json.parse(response_text) != OK ||
				!is_jsonrpc_response(response_json.get_data())) {
			_diagnose("HTTP response body is not a JSON-RPC response.");
			return RESULT_PROTOCOL_FAILED;
		}
		if (initialize_request && response_json.get_data().get_type() == Variant::DICTIONARY) {
			const Dictionary response_dictionary = response_json.get_data();
			const Dictionary initialize_result = response_dictionary.get("result", Dictionary());
			const Variant negotiated_version = initialize_result.get("protocolVersion", Variant());
			if (negotiated_version.get_type() == Variant::STRING) {
				if (!MCPProtocol::is_supported_protocol_version(negotiated_version)) {
					_diagnose("HTTP initialize response selected an unsupported protocol version.");
					return RESULT_PROTOCOL_FAILED;
				}
				protocol_version = negotiated_version;
			}
		}
		io->write_stdout_line(compact_json_whitespace(response_text));
	}
}
