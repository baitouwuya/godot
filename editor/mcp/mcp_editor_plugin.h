/**************************************************************************/
/*  mcp_editor_plugin.h                                                   */
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

#include "mcp_builtin_features.h"
#include "mcp_host.h"
#include "mcp_http_health_probe.h"
#include "mcp_project_heartbeat.h"

#include "core/mcp/mcp_discovery.h"
#include "core/mcp/mcp_project_lease.h"
#include "editor/plugins/editor_plugin.h"

class ConfirmationDialog;
class MCPEditorPlugin : public EditorPlugin, public MCPHostSessionObserver {
	GDCLASS(MCPEditorPlugin, EditorPlugin);

	enum StartupState {
		STARTUP_PENDING,
		STARTUP_RUNNING,
		STARTUP_WAITING_FOR_CONFLICT,
		STARTUP_DISABLED,
		STARTUP_FAILED,
	};

	static inline bool requested = false;
	static inline int requested_port = 0;
	static inline MCPEditorPlugin *singleton = nullptr;

	StartupState startup_state = STARTUP_PENDING;
	MCPToolRegistry tool_registry;
	MCPBuiltinFeatures builtin_features;
	MCPHost host;
	MCPProjectHeartbeat project_heartbeat;
	MCPHTTPProjectLeaseHealthProbe health_probe;
	MCPProjectIdentity project_identity;
	MCPProjectLease *project_lease = nullptr;
	MCPDiscoveryRecord discovery_record;
	String discovery_directory;
	String auth_secret;
	String last_start_error;
	ConfirmationDialog *conflict_dialog = nullptr;
	MCPDiscoveryRecord conflicting_owner;
	bool runtime_input_debugger_registered = false;
	bool runtime_observation_debugger_registered = false;

	void _notification(int p_what);
	Error _register_tools(String &r_error);
	void _unregister_tools();
	Error _start_mcp(String &r_error);
	void _stop_mcp();
	Error _publish_discovery(String &r_error);
	void _handle_start_error(Error p_error, const String &p_message);
	void _show_conflict_dialog();
	void _retry_after_conflict();
	void _conflict_custom_action(const String &p_action);
	void _exit_after_conflict();
	void _exit_with_error(const String &p_message);
	bool _is_headless() const;
	void on_mcp_session_removed(const String &p_session_id) override;

public:
	static void configure(bool p_requested, int p_port = 0);
	static bool is_requested() { return requested; }
	static MCPEditorPlugin *get_singleton() { return singleton; }

	MCPEditorPlugin();
	~MCPEditorPlugin() override;
};
