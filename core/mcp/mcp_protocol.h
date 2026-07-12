/**************************************************************************/
/*  mcp_protocol.h                                                        */
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

#pragma once

#include "core/mcp/mcp_tool_registry.h"

class MCPProtocol {
public:
	enum SessionState {
		SESSION_UNINITIALIZED,
		SESSION_INITIALIZING,
		SESSION_READY,
	};

	enum ErrorCode {
		PARSE_ERROR = -32700,
		INVALID_REQUEST = -32600,
		METHOD_NOT_FOUND = -32601,
		INVALID_PARAMS = -32602,
		INTERNAL_ERROR = -32603,
		SESSION_NOT_READY = -32002,
		SESSION_ALREADY_INITIALIZED = -32003,
	};

	struct DispatchResult {
		Variant response;

		bool has_response() const { return response.get_type() != Variant::NIL; }
	};

	static constexpr const char *PROTOCOL_VERSION_LATEST = "2025-11-25";
	static constexpr const char *PROTOCOL_VERSION_2025_06_18 = "2025-06-18";
	static constexpr const char *PROTOCOL_VERSION_2025_03_26 = "2025-03-26";

	explicit MCPProtocol(MCPToolRegistry *p_tool_registry = nullptr);

	void set_tool_registry(MCPToolRegistry *p_tool_registry);
	void set_server_info(const String &p_name, const String &p_version);
	void set_project_metadata(const Dictionary &p_project_metadata);

	SessionState get_session_state() const { return session_state; }
	String get_protocol_version() const { return protocol_version; }
	Dictionary get_client_info() const { return client_info.duplicate(true); }
	Dictionary get_client_capabilities() const { return client_capabilities.duplicate(true); }

	void reset_session();
	DispatchResult dispatch(const Variant &p_message, const Dictionary &p_session_context = Dictionary());
	DispatchResult dispatch_json(const String &p_json, const Dictionary &p_session_context = Dictionary());

	static bool is_supported_protocol_version(const String &p_version);

private:
	MCPToolRegistry *tool_registry = nullptr;
	SessionState session_state = SESSION_UNINITIALIZED;
	String protocol_version;
	String server_name = "godot-mcp";
	String server_version = "0.1.0";
	Dictionary client_info;
	Dictionary client_capabilities;
	Dictionary project_metadata;

	Dictionary _make_response(const Variant &p_id, const Variant &p_result) const;
	Dictionary _make_error(const Variant &p_id, int p_code, const String &p_message) const;
	Variant _dispatch_request(const Dictionary &p_request, bool &r_respond, const Dictionary &p_session_context);
	Variant _dispatch_batch(const Array &p_requests, const Dictionary &p_session_context);
	Variant _handle_initialize(const Dictionary &p_params, const Variant &p_id);
	Variant _handle_tools_call(const Dictionary &p_params, const Variant &p_id, const Dictionary &p_session_context);
	MCPToolCallContext _make_tool_call_context(const Variant &p_id, const Dictionary &p_session_context) const;
	bool _attach_project_metadata(Dictionary &r_result) const;
	bool _is_valid_request_id(const Variant &p_id) const;
	bool _is_batch_supported() const;
	String _negotiate_protocol_version(const String &p_requested) const;
};

VARIANT_ENUM_CAST(MCPProtocol::SessionState);
VARIANT_ENUM_CAST(MCPProtocol::ErrorCode);
