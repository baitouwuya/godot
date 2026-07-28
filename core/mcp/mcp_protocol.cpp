/**************************************************************************/
/*  mcp_protocol.cpp                                                      */
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
/* "Software"), to deal in the Software without restriction, including   */
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

#include "mcp_protocol.h"

#include "core/io/json.h"

namespace {

static void _compact_structured_tool_content(Dictionary &r_result) {
	if (r_result.get("isError", false) || !r_result.has("structuredContent")) {
		return;
	}

	Dictionary text_content;
	text_content["type"] = "text";
	text_content["text"] = "{\"structuredContentAvailable\":true}";
	Array content;
	content.push_back(text_content);
	r_result["content"] = content;
}

} // namespace

MCPProtocol::MCPProtocol(MCPToolRegistry *p_tool_registry) {
	tool_registry = p_tool_registry;
}

void MCPProtocol::set_tool_registry(MCPToolRegistry *p_tool_registry) {
	tool_registry = p_tool_registry;
}

void MCPProtocol::set_server_info(const String &p_name, const String &p_version) {
	server_name = p_name;
	server_version = p_version;
}

void MCPProtocol::set_project_metadata(const Dictionary &p_project_metadata) {
	project_metadata = p_project_metadata.duplicate(true);
}

void MCPProtocol::reset_session() {
	session_state = SESSION_UNINITIALIZED;
	protocol_version = String();
	client_info.clear();
	client_capabilities.clear();
}

bool MCPProtocol::is_supported_protocol_version(const String &p_version) {
	return p_version == PROTOCOL_VERSION_LATEST || p_version == PROTOCOL_VERSION_2025_06_18 || p_version == PROTOCOL_VERSION_2025_03_26;
}

Dictionary MCPProtocol::_make_response(const Variant &p_id, const Variant &p_result) const {
	Dictionary response;
	response["jsonrpc"] = "2.0";
	response["id"] = p_id;
	response["result"] = p_result;
	return response;
}

Dictionary MCPProtocol::_make_error(const Variant &p_id, int p_code, const String &p_message) const {
	Dictionary response;
	response["jsonrpc"] = "2.0";
	response["id"] = p_id;

	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	response["error"] = error;
	return response;
}

bool MCPProtocol::_is_valid_request_id(const Variant &p_id) const {
	switch (p_id.get_type()) {
		case Variant::NIL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
			return true;
		default:
			return false;
	}
}

String MCPProtocol::_negotiate_protocol_version(const String &p_requested) const {
	return is_supported_protocol_version(p_requested) ? p_requested : String(PROTOCOL_VERSION_LATEST);
}

bool MCPProtocol::_is_batch_supported() const {
	return session_state == SESSION_READY && protocol_version == PROTOCOL_VERSION_2025_03_26;
}

MCPToolCallContext MCPProtocol::_make_tool_call_context(const Variant &p_id, const Dictionary &p_session_context) const {
	MCPToolCallContext context;
	context.request_id = p_id;
	context.protocol_version = protocol_version;
	context.session = p_session_context.duplicate(true);
	context.session["clientInfo"] = client_info.duplicate(true);
	context.session["clientCapabilities"] = client_capabilities.duplicate(true);
	context.project = project_metadata.duplicate(true);
	return context;
}

bool MCPProtocol::_attach_project_metadata(Dictionary &r_result) const {
	if (project_metadata.is_empty()) {
		return true;
	}

	Dictionary meta;
	if (r_result.has("_meta")) {
		if (r_result["_meta"].get_type() != Variant::DICTIONARY) {
			return false;
		}
		meta = r_result["_meta"];
	}
	meta["io.godot/project"] = project_metadata.duplicate(true);
	r_result["_meta"] = meta;
	return true;
}

Variant MCPProtocol::_handle_initialize(const Dictionary &p_params, const Variant &p_id) {
	if (session_state != SESSION_UNINITIALIZED) {
		return _make_error(p_id, SESSION_ALREADY_INITIALIZED, "MCP session is already initialized.");
	}
	if (!p_params.has("protocolVersion") || p_params["protocolVersion"].get_type() != Variant::STRING || String(p_params["protocolVersion"]).is_empty()) {
		return _make_error(p_id, INVALID_PARAMS, "initialize requires a string protocolVersion.");
	}
	if (!p_params.has("capabilities") || p_params["capabilities"].get_type() != Variant::DICTIONARY) {
		return _make_error(p_id, INVALID_PARAMS, "initialize requires client capabilities.");
	}
	if (!p_params.has("clientInfo") || p_params["clientInfo"].get_type() != Variant::DICTIONARY) {
		return _make_error(p_id, INVALID_PARAMS, "initialize requires clientInfo.");
	}

	const Dictionary requested_client_info = p_params["clientInfo"];
	if (!requested_client_info.has("name") || requested_client_info["name"].get_type() != Variant::STRING || String(requested_client_info["name"]).is_empty() ||
			!requested_client_info.has("version") || requested_client_info["version"].get_type() != Variant::STRING) {
		return _make_error(p_id, INVALID_PARAMS, "clientInfo requires string name and version fields.");
	}

	protocol_version = _negotiate_protocol_version(String(p_params["protocolVersion"]));
	client_capabilities = Dictionary(p_params["capabilities"]).duplicate(true);
	client_info = requested_client_info.duplicate(true);
	session_state = SESSION_INITIALIZING;

	Dictionary result;
	result["protocolVersion"] = protocol_version;

	Dictionary capabilities;
	Dictionary tools;
	tools["listChanged"] = false;
	capabilities["tools"] = tools;
	result["capabilities"] = capabilities;

	Dictionary server_info;
	server_info["name"] = server_name;
	server_info["version"] = server_version;
	result["serverInfo"] = server_info;
	return _make_response(p_id, result);
}

Variant MCPProtocol::_handle_tools_call(const Dictionary &p_params, const Variant &p_id, const Dictionary &p_session_context) {
	if (!p_params.has("name") || (p_params["name"].get_type() != Variant::STRING && p_params["name"].get_type() != Variant::STRING_NAME) || String(p_params["name"]).is_empty()) {
		return _make_error(p_id, INVALID_PARAMS, "tools/call requires a string tool name.");
	}
	if (p_params.has("arguments") && p_params["arguments"].get_type() != Variant::DICTIONARY) {
		return _make_error(p_id, INVALID_PARAMS, "tools/call arguments must be an object.");
	}
	if (!tool_registry) {
		return _make_error(p_id, INTERNAL_ERROR, "MCP tool registry is not configured.");
	}

	const StringName name = p_params["name"];
	const Dictionary arguments = p_params.get("arguments", Dictionary());
	const MCPToolCallContext context = _make_tool_call_context(p_id, p_session_context);
	const MCPToolRegistry::CallResult call_result = tool_registry->call_tool(name, arguments, context);
	switch (call_result.status) {
		case MCPToolRegistry::CALL_OK:
			break;
		case MCPToolRegistry::CALL_TOOL_NOT_FOUND:
			return _make_error(p_id, INVALID_PARAMS, call_result.message);
		case MCPToolRegistry::CALL_TOOL_UNAVAILABLE:
		case MCPToolRegistry::CALL_HANDLER_FAILED:
		case MCPToolRegistry::CALL_INVALID_RESULT:
			return _make_error(p_id, INTERNAL_ERROR, call_result.message);
	}

	Dictionary result = call_result.result.duplicate(true);
	if (protocol_version != PROTOCOL_VERSION_2025_03_26) {
		_compact_structured_tool_content(result);
	}
	if (!_attach_project_metadata(result)) {
		return _make_error(p_id, INTERNAL_ERROR, "Tool result _meta must be an object.");
	}
	return _make_response(p_id, result);
}

Variant MCPProtocol::_dispatch_request(const Dictionary &p_request, bool &r_respond, const Dictionary &p_session_context) {
	r_respond = true;
	if (p_request.get("jsonrpc", String()) != "2.0" || !p_request.has("method") || p_request["method"].get_type() != Variant::STRING || String(p_request["method"]).is_empty()) {
		return _make_error(Variant(), INVALID_REQUEST, "Invalid JSON-RPC request.");
	}

	const bool has_id = p_request.has("id");
	const Variant id = p_request.get("id", Variant());
	if (has_id && !_is_valid_request_id(id)) {
		return _make_error(Variant(), INVALID_REQUEST, "JSON-RPC id must be a string, number, or null.");
	}
	r_respond = has_id;

	if (p_request.has("params") && p_request["params"].get_type() != Variant::DICTIONARY) {
		return has_id ? Variant(_make_error(id, INVALID_PARAMS, "Method params must be an object.")) : Variant();
	}
	const Dictionary params = p_request.get("params", Dictionary());
	const String method = p_request["method"];

	if (method == "initialize") {
		return has_id ? _handle_initialize(params, id) : Variant();
	}
	if (method == "notifications/initialized") {
		if (has_id) {
			return _make_error(id, INVALID_REQUEST, "notifications/initialized must not have an id.");
		}
		if (session_state == SESSION_INITIALIZING) {
			session_state = SESSION_READY;
		}
		return Variant();
	}
	if (method == "ping") {
		return has_id ? Variant(_make_response(id, Dictionary())) : Variant();
	}

	if (session_state != SESSION_READY) {
		return has_id ? Variant(_make_error(id, SESSION_NOT_READY, "MCP session is not initialized.")) : Variant();
	}
	if (method == "tools/list") {
		if (!has_id) {
			return Variant();
		}
		if (!tool_registry) {
			return _make_error(id, INTERNAL_ERROR, "MCP tool registry is not configured.");
		}
		Dictionary result;
		result["tools"] = tool_registry->get_tool_definitions();
		return _make_response(id, result);
	}
	if (method == "tools/call") {
		return has_id ? _handle_tools_call(params, id, p_session_context) : Variant();
	}

	return has_id ? Variant(_make_error(id, METHOD_NOT_FOUND, "Unknown method: " + method)) : Variant();
}

Variant MCPProtocol::_dispatch_batch(const Array &p_requests, const Dictionary &p_session_context) {
	if (p_requests.is_empty() || !_is_batch_supported()) {
		return _make_error(Variant(), INVALID_REQUEST, "JSON-RPC batch requests are not supported for this MCP session.");
	}

	Array responses;
	for (const Variant &request_value : p_requests) {
		if (request_value.get_type() != Variant::DICTIONARY) {
			responses.push_back(_make_error(Variant(), INVALID_REQUEST, "Invalid JSON-RPC request."));
			continue;
		}
		bool respond = true;
		const Variant response = _dispatch_request(request_value, respond, p_session_context);
		if (respond && response.get_type() != Variant::NIL) {
			responses.push_back(response);
		}
	}
	return responses.is_empty() ? Variant() : Variant(responses);
}

MCPProtocol::DispatchResult MCPProtocol::dispatch(const Variant &p_message, const Dictionary &p_session_context) {
	DispatchResult result;
	if (p_message.get_type() == Variant::ARRAY) {
		result.response = _dispatch_batch(Array(p_message), p_session_context);
		return result;
	}
	if (p_message.get_type() != Variant::DICTIONARY) {
		result.response = _make_error(Variant(), INVALID_REQUEST, "Invalid JSON-RPC request.");
		return result;
	}

	bool respond = true;
	result.response = _dispatch_request(p_message, respond, p_session_context);
	if (!respond) {
		result.response = Variant();
	}
	return result;
}

MCPProtocol::DispatchResult MCPProtocol::dispatch_json(const String &p_json, const Dictionary &p_session_context) {
	JSON json;
	if (json.parse(p_json) != OK) {
		DispatchResult result;
		result.response = _make_error(Variant(), PARSE_ERROR, "Parse error.");
		return result;
	}
	return dispatch(json.get_data(), p_session_context);
}
