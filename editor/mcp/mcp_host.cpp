/**************************************************************************/
/*  mcp_host.cpp                                                          */
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

#include "mcp_host.h"

#include "core/crypto/crypto.h"
#include "core/io/json.h"
#include "core/os/os.h"

namespace {

bool accepts_media_type(const String &p_accept, const String &p_media_type) {
	const PackedStringArray accepted_types = p_accept.to_lower().split(",", false);
	for (const String &accepted_type : accepted_types) {
		if (accepted_type.get_slice(";", 0).strip_edges() == p_media_type) {
			return true;
		}
	}
	return false;
}

} // namespace

Error MCPHost::_generate_session_id(String &r_session_id) const {
	Ref<Crypto> crypto = Ref<Crypto>(Crypto::create());
	ERR_FAIL_COND_V(crypto.is_null(), ERR_UNAVAILABLE);
	const PackedByteArray random_bytes = crypto->generate_random_bytes(32);
	ERR_FAIL_COND_V(random_bytes.size() != 32, FAILED);
	r_session_id = String::hex_encode_buffer(random_bytes.ptr(), random_bytes.size());
	return OK;
}

MCPHost::Session *MCPHost::_create_session(String &r_session_id) {
	if (sessions.size() >= (uint32_t)config.max_sessions) {
		return nullptr;
	}
	for (int attempt = 0; attempt < 4; attempt++) {
		if (_generate_session_id(r_session_id) != OK) {
			return nullptr;
		}
		if (sessions.has(r_session_id)) {
			continue;
		}

		Session *session = memnew(Session(tool_registry));
		session->protocol.set_server_info(config.server_name, config.server_version);
		session->protocol.set_project_metadata(project_metadata);
		session->last_activity_usec = OS::get_singleton()->get_ticks_usec();
		sessions.insert(r_session_id, session);
		return session;
	}
	return nullptr;
}

void MCPHost::_remove_session(const String &p_session_id) {
	Session **session = sessions.getptr(p_session_id);
	if (!session) {
		return;
	}
	memdelete(*session);
	sessions.erase(p_session_id);
}

void MCPHost::_expire_sessions(uint64_t p_now_usec) {
	Vector<String> expired;
	for (const KeyValue<String, Session *> &entry : sessions) {
		if (p_now_usec - entry.value->last_activity_usec > config.session_idle_timeout_usec) {
			expired.push_back(entry.key);
		}
	}
	for (const String &session_id : expired) {
		_remove_session(session_id);
	}
}

Dictionary MCPHost::_make_session_context(const String &p_session_id) const {
	Dictionary context;
	context["sessionId"] = p_session_id;
	context["projectId"] = config.project_identity.project_id;
	context["instanceId"] = config.project_identity.instance_id;
	return context;
}

MCPHTTPResponse MCPHost::_make_json_error(int p_status, const String &p_message) const {
	Dictionary error;
	error["error"] = p_message;
	MCPHTTPResponse response;
	response.status = p_status;
	response.body = JSON::stringify(error);
	return response;
}

MCPHTTPResponse MCPHost::_handle_health(const MCPHTTPParser::Request &p_request) const {
	if (p_request.method != "GET") {
		MCPHTTPResponse response = _make_json_error(405, "The health endpoint only supports GET.");
		response.headers["Allow"] = "GET";
		return response;
	}
	Dictionary health = project_metadata.duplicate(true);
	health["ok"] = true;
	health["protocolVersion"] = MCPProtocol::PROTOCOL_VERSION_LATEST;
	MCPHTTPResponse response;
	response.body = JSON::stringify(health);
	return response;
}

MCPHTTPResponse MCPHost::_handle_mcp(const MCPHTTPParser::Request &p_request) {
	if (p_request.method == "GET") {
		MCPHTTPResponse response = _make_json_error(405, "SSE is not supported by this MCP Host.");
		response.headers["Allow"] = "POST, DELETE";
		return response;
	}

	const String session_id = p_request.headers.get("mcp-session-id", "");
	const String requested_protocol_version = p_request.headers.get("mcp-protocol-version", "");
	if (!requested_protocol_version.is_empty() && !MCPProtocol::is_supported_protocol_version(requested_protocol_version)) {
		return _make_json_error(400, "MCP-Protocol-Version is invalid or unsupported.");
	}
	Session *existing_session = nullptr;
	if (!session_id.is_empty()) {
		Session **session_ptr = sessions.getptr(session_id);
		if (session_ptr) {
			existing_session = *session_ptr;
			if (!requested_protocol_version.is_empty() &&
					requested_protocol_version != existing_session->protocol.get_protocol_version()) {
				return _make_json_error(400, "MCP-Protocol-Version does not match the initialized session.");
			}
		}
	}
	if (p_request.method == "DELETE") {
		if (!existing_session) {
			return _make_json_error(404, "MCP session was not found.");
		}
		_remove_session(session_id);
		MCPHTTPResponse response;
		response.status = 204;
		return response;
	}

	if (p_request.method != "POST") {
		MCPHTTPResponse response = _make_json_error(405, "The MCP endpoint only supports POST and DELETE.");
		response.headers["Allow"] = "POST, DELETE";
		return response;
	}
	if (!p_request.has_content_length) {
		return _make_json_error(400, "MCP POST requests require Content-Length.");
	}
	const String accept = p_request.headers.get("accept", "");
	if (!accepts_media_type(accept, "application/json") || !accepts_media_type(accept, "text/event-stream")) {
		return _make_json_error(406, "MCP POST requests must accept application/json and text/event-stream.");
	}
	const String content_type = String(p_request.headers.get("content-type", "")).to_lower();
	if (!content_type.begins_with("application/json")) {
		return _make_json_error(415, "MCP POST requests require application/json.");
	}

	String active_session_id = session_id;
	Session *session = nullptr;
	bool created_session = false;
	if (active_session_id.is_empty()) {
		session = _create_session(active_session_id);
		if (!session) {
			return _make_json_error(sessions.size() >= (uint32_t)config.max_sessions ? 429 : 500, "Unable to create an MCP session.");
		}
		created_session = true;
	} else {
		if (!existing_session) {
			return _make_json_error(404, "MCP session was not found.");
		}
		session = existing_session;
	}

	session->last_activity_usec = OS::get_singleton()->get_ticks_usec();
	const MCPProtocol::DispatchResult result = session->protocol.dispatch_json(p_request.body, _make_session_context(active_session_id));
	const bool session_started = session->protocol.get_session_state() != MCPProtocol::SESSION_UNINITIALIZED;
	if (created_session && !session_started) {
		_remove_session(active_session_id);
	}

	MCPHTTPResponse response;
	if (!result.has_response()) {
		response.status = 202;
	} else {
		response.body = JSON::stringify(result.response);
	}
	if (session_started) {
		response.headers["Mcp-Session-Id"] = active_session_id;
	}
	return response;
}

Error MCPHost::start(const Config &p_config, MCPToolRegistry *p_tool_registry) {
	ERR_FAIL_COND_V(running, ERR_ALREADY_IN_USE);
	ERR_FAIL_NULL_V(p_tool_registry, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(!p_config.project_identity.is_valid(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_config.bearer_token.is_empty(), ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_config.max_sessions <= 0 || p_config.session_idle_timeout_usec == 0, ERR_INVALID_PARAMETER);

	config = p_config;
	tool_registry = p_tool_registry;
	project_metadata["projectId"] = config.project_identity.project_id;
	project_metadata["instanceId"] = config.project_identity.instance_id;
	project_metadata["projectPath"] = config.project_identity.canonical_path;

	MCPHTTPServer::Config http_config = config.http;
	http_config.port = config.port;
	http_config.bearer_token = config.bearer_token;
	const Error error = http_server.start(http_config, this);
	if (error != OK) {
		tool_registry = nullptr;
		project_metadata.clear();
		return error;
	}
	running = true;
	return OK;
}

void MCPHost::poll() {
	if (!running) {
		return;
	}
	_expire_sessions(OS::get_singleton()->get_ticks_usec());
	http_server.poll();
}

void MCPHost::stop() {
	http_server.stop();
	Vector<String> session_ids;
	for (const KeyValue<String, Session *> &entry : sessions) {
		session_ids.push_back(entry.key);
	}
	for (const String &session_id : session_ids) {
		_remove_session(session_id);
	}
	tool_registry = nullptr;
	project_metadata.clear();
	running = false;
}

MCPHTTPResponse MCPHost::handle_request(const MCPHTTPParser::Request &p_request) {
	if (p_request.path == "/health") {
		return _handle_health(p_request);
	}
	if (p_request.path == "/mcp") {
		return _handle_mcp(p_request);
	}
	return _make_json_error(404, "HTTP endpoint was not found.");
}

String MCPHost::get_endpoint() const {
	return running ? "http://127.0.0.1:" + itos(get_port()) + "/mcp" : String();
}

MCPHost::~MCPHost() {
	stop();
}
