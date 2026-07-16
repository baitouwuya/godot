/**************************************************************************/
/*  mcp_editor_plugin.cpp                                                 */
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

#include "mcp_editor_plugin.h"

#include "providers/mcp_class_provider.h"
#include "providers/mcp_debug_capture.h"
#include "providers/mcp_debug_event_store.h"
#include "providers/mcp_debug_provider.h"
#include "providers/mcp_editor_provider.h"
#include "providers/mcp_editor_ui_provider.h"
#include "providers/mcp_file_provider.h"
#include "providers/mcp_node_provider.h"
#include "providers/mcp_node_structure_provider.h"
#include "providers/mcp_resource_provider.h"
#include "providers/mcp_runtime_debug_service.h"
#include "providers/mcp_runtime_provider.h"
#include "providers/mcp_scene_provider.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_paths.h"
#include "scene/gui/dialogs.h"
#include "scene/main/scene_tree.h"
#include "servers/display/display_server.h"

#include "modules/modules_enabled.gen.h"

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
#include "providers/mcp_gdscript_provider.h"
#include "providers/mcp_gdscript_session_manager.h"
#include "providers/mcp_script_provider.h"

#include "modules/gdscript/language_server/gdscript_language_protocol.h"
#endif

namespace {

constexpr uint64_t HEARTBEAT_INTERVAL_USEC = 2 * 1000 * 1000;

} // namespace

void MCPEditorPlugin::configure(bool p_requested, int p_port) {
	requested = p_requested;
	requested_port = p_port;
}

MCPMainThreadExecutor *MCPEditorPlugin::get_main_thread_executor() {
	return singleton ? &singleton->main_thread_executor : nullptr;
}

bool MCPEditorPlugin::_is_headless() const {
	return !DisplayServer::get_singleton() || DisplayServer::get_singleton()->get_name() == "headless";
}

Error MCPEditorPlugin::_register_tools(String &r_error) {
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	GDScriptLanguageProtocol *language_protocol = GDScriptLanguageProtocol::get_singleton();
	if (!language_protocol || language_protocol->get_analysis_service().is_null()) {
		r_error = "The built-in GDScript analysis service is not available.";
		return ERR_UNCONFIGURED;
	}
	gdscript_session_manager->set_analysis_service(language_protocol->get_analysis_service());
#endif

	Error error = class_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		return error;
	}
	error = debug_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
	error = runtime_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
	error = editor_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
	error = editor_ui_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
	error = file_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
	error = scene_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
	error = node_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
	error = node_structure_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	error = script_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
#endif
	error = resource_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	error = gdscript_provider->register_tools(&tool_registry, &r_error);
	if (error != OK) {
		_unregister_tools();
		return error;
	}
#endif
	return OK;
}

void MCPEditorPlugin::_unregister_tools() {
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	gdscript_provider->unregister_tools();
#endif
	resource_provider->unregister_tools();
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	script_provider->unregister_tools();
#endif
	node_structure_provider->unregister_tools();
	node_provider->unregister_tools();
	scene_provider->unregister_tools();
	file_provider->unregister_tools();
	editor_ui_provider->unregister_tools();
	editor_provider->unregister_tools();
	runtime_provider->unregister_tools();
	debug_provider->unregister_tools();
	class_provider->unregister_tools();
}

void MCPEditorPlugin::on_mcp_session_removed(const String &p_session_id) {
	if (editor_ui_provider) {
		editor_ui_provider->release_session(p_session_id);
	}
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	if (gdscript_session_manager) {
		gdscript_session_manager->release_session(p_session_id);
	}
#endif
}

Error MCPEditorPlugin::_publish_discovery(String &r_error) {
	const uint64_t now_ms = MCPDiscovery::get_current_unix_time_ms();
	discovery_record.heartbeat_at_unix_ms = now_ms;
	Error error = project_lease->refresh(discovery_record, &r_error, now_ms);
	if (error != OK) {
		return error;
	}
	return MCPDiscovery::write_record(discovery_directory, discovery_record, true, &r_error);
}

Error MCPEditorPlugin::_start_mcp(String &r_error) {
	r_error = String();
	conflicting_owner = MCPDiscoveryRecord();

	Error error = MCPProjectIdentity::create(ProjectSettings::get_singleton()->get_resource_path(), project_identity, &r_error);
	if (error != OK) {
		return error;
	}
	if (MCPProjectIdentity::generate_instance_id(auth_secret, &r_error) != OK) {
		return ERR_CANT_CREATE;
	}

	project_lease = memnew(MCPProjectLease(project_identity, EditorPaths::get_singleton()->get_project_settings_dir()));
	discovery_directory = EditorPaths::get_singleton()->get_data_dir().path_join("mcp/discovery");
	discovery_record = MCPDiscoveryRecord::create(project_identity, "streamable-http", String(), MCPProtocol::PROTOCOL_VERSION_LATEST, auth_secret);
	error = project_lease->acquire(discovery_record, health_probe, &conflicting_owner, &r_error);
	if (error != OK) {
		return error;
	}

	error = _register_tools(r_error);
	if (error != OK) {
		project_lease->release();
		return error;
	}

	MCPHost::Config host_config;
	host_config.project_identity = project_identity;
	host_config.bearer_token = auth_secret;
	host_config.port = requested_port;
	host_config.server_version = VERSION_FULL_CONFIG;
	host_config.session_observer = this;
	error = host.start(host_config, &tool_registry);
	if (error != OK) {
		_unregister_tools();
		project_lease->release();
		r_error = "Unable to start the MCP Streamable HTTP Host.";
		return error;
	}

	discovery_record.endpoint = host.get_endpoint();
	error = _publish_discovery(r_error);
	if (error != OK) {
		host.stop();
		_unregister_tools();
		project_lease->release();
		return error;
	}

	debug_event_store->clear();
	error = debug_capture->start();
	if (error != OK) {
		MCPDiscovery::remove_record(discovery_directory, project_identity.project_id, project_identity.instance_id);
		host.stop();
		_unregister_tools();
		project_lease->release();
		r_error = "Unable to start MCP debug event capture.";
		return error;
	}

	main_thread_executor.reset();
	last_heartbeat_usec = OS::get_singleton()->get_ticks_usec();
	print_line(vformat("Godot MCP Host started for project %s at %s", project_identity.project_id, host.get_endpoint()));
	return OK;
}

void MCPEditorPlugin::_stop_mcp() {
	debug_capture->stop();
	main_thread_executor.shutdown();
	if (host.is_running()) {
		host.stop();
	}
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	if (gdscript_session_manager) {
		gdscript_session_manager->clear();
	}
#endif
	_unregister_tools();
	if (!discovery_directory.is_empty() && project_identity.is_valid()) {
		MCPDiscovery::remove_record(discovery_directory, project_identity.project_id, project_identity.instance_id);
	}
	if (project_lease) {
		project_lease->release();
		memdelete(project_lease);
		project_lease = nullptr;
	}
	discovery_record = MCPDiscoveryRecord();
	project_identity = MCPProjectIdentity();
	discovery_directory = String();
	auth_secret = String();
}

void MCPEditorPlugin::_refresh_heartbeat() {
	const uint64_t now_usec = OS::get_singleton()->get_ticks_usec();
	if (now_usec - last_heartbeat_usec < HEARTBEAT_INTERVAL_USEC) {
		return;
	}
	last_heartbeat_usec = now_usec;
	String error;
	if (_publish_discovery(error) != OK) {
		_exit_with_error("Godot MCP Host lost its project lease: " + error);
	}
}

void MCPEditorPlugin::_handle_start_error(Error p_error, const String &p_message) {
	last_start_error = p_message.is_empty() ? error_names[p_error] : p_message;
	if (p_error == ERR_ALREADY_IN_USE) {
		if (_is_headless()) {
			_exit_with_error("Another MCP Host already owns this project. " + last_start_error);
		} else {
			startup_state = STARTUP_WAITING_FOR_CONFLICT;
			_show_conflict_dialog();
		}
		return;
	}
	_exit_with_error("Unable to start Godot MCP Host: " + last_start_error);
}

void MCPEditorPlugin::_show_conflict_dialog() {
	if (!conflict_dialog) {
		conflict_dialog = memnew(ConfirmationDialog);
		EditorNode::get_singleton()->get_gui_base()->add_child(conflict_dialog);
		conflict_dialog->set_title(TTR("MCP Host Already Running"));
		conflict_dialog->set_ok_button_text(TTR("Retry"));
		conflict_dialog->set_cancel_button_text(TTR("Exit"));
		conflict_dialog->add_button(TTR("Continue Without MCP"), true, "continue_without_mcp");
		conflict_dialog->connect(SceneStringName(confirmed), callable_mp(this, &MCPEditorPlugin::_retry_after_conflict));
		conflict_dialog->connect("custom_action", callable_mp(this, &MCPEditorPlugin::_conflict_custom_action));
		conflict_dialog->connect("canceled", callable_mp(this, &MCPEditorPlugin::_exit_after_conflict));
	}
	const String owner_description = conflicting_owner.is_valid(false) ? vformat("PID: %d\nEndpoint: %s", conflicting_owner.pid, conflicting_owner.endpoint) : TTR("The current owner metadata is incomplete.");
	conflict_dialog->set_text(vformat(TTR("This project already has an MCP Host. Close the owning Godot instance, then retry.\n\n%s"), owner_description));
	conflict_dialog->popup_centered();
}

void MCPEditorPlugin::_retry_after_conflict() {
	_stop_mcp();
	startup_state = STARTUP_PENDING;
}

void MCPEditorPlugin::_conflict_custom_action(const String &p_action) {
	if (p_action != "continue_without_mcp") {
		return;
	}
	requested = false;
	startup_state = STARTUP_DISABLED;
	if (conflict_dialog) {
		conflict_dialog->hide();
	}
	_stop_mcp();
	set_process_internal(false);
}

void MCPEditorPlugin::_exit_after_conflict() {
	_exit_with_error("Godot MCP Host startup was canceled because this project is already owned by another instance.");
}

void MCPEditorPlugin::_exit_with_error(const String &p_message) {
	startup_state = STARTUP_FAILED;
	ERR_PRINT(p_message);
	_stop_mcp();
	if (get_tree()) {
		get_tree()->quit(EXIT_FAILURE);
	} else {
		OS::get_singleton()->set_exit_code(EXIT_FAILURE);
	}
}

void MCPEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!requested || startup_state == STARTUP_DISABLED || startup_state == STARTUP_FAILED || startup_state == STARTUP_WAITING_FOR_CONFLICT) {
				return;
			}
			if (startup_state == STARTUP_PENDING) {
				if (!EditorNode::get_singleton()->is_editor_ready()) {
					return;
				}
				String error;
				const Error start_error = _start_mcp(error);
				if (start_error != OK) {
					_handle_start_error(start_error, error);
					return;
				}
				startup_state = STARTUP_RUNNING;
			}
			host.poll();
			main_thread_executor.poll();
			_refresh_heartbeat();
		} break;

		case NOTIFICATION_EXIT_TREE: {
			_stop_mcp();
		} break;
	}
}

MCPEditorPlugin::MCPEditorPlugin() {
	singleton = this;
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	Ref<MCPGDScriptSessionManager> session_manager;
	session_manager.instantiate();
	gdscript_session_manager = session_manager.ptr();
#endif
	class_provider = memnew(MCPClassProvider);
	debug_event_store = memnew(MCPDebugEventStore);
	debug_capture = memnew(MCPDebugCapture(debug_event_store));
	debug_provider = memnew(MCPDebugProvider(debug_event_store));
	runtime_debug_service = memnew(MCPRuntimeDebugService(debug_capture));
	runtime_provider = memnew(MCPRuntimeProvider(runtime_debug_service));
	editor_provider = memnew(MCPEditorProvider);
	editor_ui_provider = memnew(MCPEditorUIProvider);
	file_provider = memnew(MCPFileProvider);
	scene_provider = memnew(MCPSceneProvider);
	node_provider = memnew(MCPNodeProvider);
	node_structure_provider = memnew(MCPNodeStructureProvider);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	script_provider = memnew(MCPScriptProvider(session_manager));
#endif
	resource_provider = memnew(MCPResourceProvider);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	gdscript_provider = memnew(MCPGDScriptProvider(session_manager));
#endif
	set_process_internal(requested);
}

MCPEditorPlugin::~MCPEditorPlugin() {
	_stop_mcp();
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	memdelete(gdscript_provider);
#endif
	memdelete(resource_provider);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	memdelete(script_provider);
#endif
	memdelete(node_structure_provider);
	memdelete(node_provider);
	memdelete(scene_provider);
	memdelete(file_provider);
	memdelete(editor_ui_provider);
	memdelete(editor_provider);
	memdelete(runtime_provider);
	memdelete(runtime_debug_service);
	memdelete(debug_provider);
	memdelete(debug_capture);
	memdelete(debug_event_store);
	memdelete(class_provider);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	gdscript_session_manager = nullptr;
#endif
	if (singleton == this) {
		singleton = nullptr;
	}
}
