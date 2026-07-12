/**************************************************************************/
/*  mcp_host.h                                                            */
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

#pragma once

#include "mcp_http_server.h"

#include "core/mcp/mcp_project_identity.h"
#include "core/mcp/mcp_protocol.h"
#include "core/templates/hash_map.h"

class MCPHostSessionObserver {
public:
	virtual void on_mcp_session_removed(const String &p_session_id) = 0;
	virtual ~MCPHostSessionObserver() = default;
};

class MCPHost : public MCPHTTPRequestHandler {
public:
	struct Config {
		MCPProjectIdentity project_identity;
		String bearer_token;
		String server_name = "godot-mcp";
		String server_version = "0.1.0";
		uint16_t port = 0;
		int max_sessions = 64;
		uint64_t session_idle_timeout_usec = 30 * 60 * 1000 * 1000ULL;
		MCPHTTPServer::Config http;
		MCPHostSessionObserver *session_observer = nullptr;
	};

private:
	struct Session {
		MCPProtocol protocol;
		uint64_t last_activity_usec = 0;

		explicit Session(MCPToolRegistry *p_tool_registry) :
				protocol(p_tool_registry) {}
	};

	MCPToolRegistry *tool_registry = nullptr;
	MCPHTTPServer http_server;
	HashMap<String, Session *> sessions;
	Config config;
	Dictionary project_metadata;
	bool running = false;

	Error _generate_session_id(String &r_session_id) const;
	Session *_create_session(String &r_session_id);
	void _remove_session(const String &p_session_id);
	void _expire_sessions(uint64_t p_now_usec);
	Dictionary _make_session_context(const String &p_session_id) const;
	MCPHTTPResponse _handle_health(const MCPHTTPParser::Request &p_request) const;
	MCPHTTPResponse _handle_mcp(const MCPHTTPParser::Request &p_request);
	MCPHTTPResponse _make_json_error(int p_status, const String &p_message) const;

public:
	Error start(const Config &p_config, MCPToolRegistry *p_tool_registry);
	void poll();
	void stop();

	MCPHTTPResponse handle_request(const MCPHTTPParser::Request &p_request) override;

	bool is_running() const { return running; }
	int get_port() const { return http_server.get_port(); }
	String get_endpoint() const;
	int get_session_count() const { return sessions.size(); }
	Dictionary get_project_metadata() const { return project_metadata; }

	~MCPHost() override;
};
