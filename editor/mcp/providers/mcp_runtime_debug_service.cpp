/**************************************************************************/
/*  mcp_runtime_debug_service.cpp                                         */
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

#include "mcp_runtime_debug_service.h"

#include "mcp_path_utils.h"
#include "mcp_tool_utils.h"
#include "mcp_variant_codec.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "editor/debugger/editor_debugger_inspector.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_node.h"
#include "editor/run/editor_run_bar.h"
#include "scene/debugger/scene_debugger_object.h"
#include "scene/resources/packed_scene.h"

namespace {

constexpr int DEFAULT_TIMEOUT_MSEC = 750;

Dictionary _error(const String &p_code, const String &p_message) {
	return MCPToolUtils::make_error_result(p_code, p_message);
}

int _timeout_from_arguments(const Dictionary &p_arguments) {
	int64_t timeout = DEFAULT_TIMEOUT_MSEC;
	MCPToolUtils::try_get_json_integer(p_arguments.get("timeoutMs", DEFAULT_TIMEOUT_MSEC), 50, 5000, timeout);
	return int(timeout);
}

String _normalize_runtime_path(const String &p_path) {
	String path = p_path.strip_edges().replace("\\", "/");
	while (path.ends_with("/") && path.length() > 1) {
		path = path.left(-1);
	}
	if (!path.begins_with("/")) {
		path = "/" + path;
	}
	return path;
}

struct TreeFrame {
	String path;
	int remaining_children = 0;
};

Array _make_tree_nodes(const SceneDebuggerTree *p_tree) {
	Array nodes;
	Vector<TreeFrame> parents;
	for (const SceneDebuggerTree::RemoteNode &remote_node : p_tree->nodes) {
		while (!parents.is_empty() && parents[parents.size() - 1].remaining_children == 0) {
			parents.resize(parents.size() - 1);
		}
		const String parent_path = parents.is_empty() ? String() : parents[parents.size() - 1].path;
		const String path = parent_path.is_empty() ? "/" + remote_node.name : parent_path.path_join(remote_node.name);
		if (!parents.is_empty()) {
			parents.write[parents.size() - 1].remaining_children--;
		}

		Dictionary node;
		node["path"] = path;
		node["parentPath"] = parent_path.is_empty() ? Variant() : Variant(parent_path);
		node["name"] = remote_node.name;
		node["type"] = remote_node.type_name;
		node["objectId"] = String::num_uint64(uint64_t(remote_node.id));
		node["childCount"] = remote_node.child_count;
		if (!remote_node.scene_file_path.is_empty()) {
			node["sceneFilePath"] = remote_node.scene_file_path;
		}
		if (remote_node.view_flags & SceneDebuggerTree::RemoteNode::VIEW_HAS_VISIBLE_METHOD) {
			node["visible"] = bool(remote_node.view_flags & SceneDebuggerTree::RemoteNode::VIEW_VISIBLE);
			node["visibleInTree"] = bool(remote_node.view_flags & SceneDebuggerTree::RemoteNode::VIEW_VISIBLE_IN_TREE);
		}
		nodes.push_back(node);

		if (remote_node.child_count > 0) {
			TreeFrame frame;
			frame.path = path;
			frame.remaining_children = remote_node.child_count;
			parents.push_back(frame);
		}
	}
	return nodes;
}

bool _find_tree_node(const SceneDebuggerTree *p_tree, const String &p_requested_path, ObjectID &r_object_id, Dictionary &r_node) {
	const String requested_path = _normalize_runtime_path(p_requested_path);
	const Array nodes = _make_tree_nodes(p_tree);
	for (const Dictionary node : nodes) {
		if (node["path"] == requested_path) {
			r_object_id = ObjectID(String(node["objectId"]).to_int());
			r_node = node;
			return true;
		}
	}
	return false;
}

Dictionary _encode_remote_properties(EditorDebuggerRemoteObjects *p_remote_object, ObjectID p_object_id) {
	Array properties;
	Array layout;
	for (const PropertyInfo &property : p_remote_object->prop_list) {
		Dictionary item = MCPToolUtils::make_property_description(property);
		const bool is_layout = property.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP);
		item["readOnly"] = bool(property.usage & PROPERTY_USAGE_READ_ONLY) || is_layout;
		item["source"] = String(property.name).begins_with("Members/") || String(property.name).begins_with("Constants/") ? "script" : "runtime";
		if (is_layout) {
			item["kind"] = property.usage & PROPERTY_USAGE_CATEGORY ? "category" : (property.usage & PROPERTY_USAGE_GROUP ? "group" : "subgroup");
			layout.push_back(item);
			continue;
		}

		const Variant value = p_remote_object->get_variant(property.name);
		if (property.hint == PROPERTY_HINT_OBJECT_ID && value.get_type() == Variant::INT && uint64_t(value) != 0) {
			Dictionary reference;
			reference["kind"] = "remoteObject";
			reference["objectId"] = String::num_uint64(uint64_t(value));
			item["valueReference"] = reference;
			item["encodable"] = false;
		} else {
			Variant encoded;
			String encoding_error;
			if (MCPVariantCodec::encode(value, encoded, &encoding_error) == OK) {
				item["encodable"] = true;
				item["value"] = encoded;
			} else {
				item["encodable"] = false;
				item["encodingError"] = encoding_error;
			}
		}
		properties.push_back(item);
	}
	Dictionary result;
	result["objectId"] = String::num_uint64(uint64_t(p_object_id));
	result["type"] = p_remote_object->type_name;
	result["name"] = p_remote_object->node_name;
	result["propertyCount"] = properties.size();
	result["properties"] = properties;
	result["propertyLayout"] = layout;
	return result;
}

const PropertyInfo *_find_property(EditorDebuggerRemoteObjects *p_remote_object, const StringName &p_name) {
	for (const PropertyInfo &property : p_remote_object->prop_list) {
		if (property.name == p_name) {
			return &property;
		}
	}
	return nullptr;
}

MCPRuntimeJobService::Config _condition_job_config() {
	MCPRuntimeJobService::Config config;
	config.id_prefix = "wait";
	config.capture = "mcp_condition";
	config.not_found_code = "WAIT_JOB_NOT_FOUND";
	config.not_found_message = "Runtime wait job was not found for this MCP session.";
	config.stale_code = "STALE_WAIT_JOB";
	config.stale_message = "Runtime wait job belongs to a different runtime generation or debugger session.";
	config.limit_code = "WAIT_JOB_LIMIT";
	config.limit_message = "The editor wait job record limit is full.";
	config.max_records = 256;
	return config;
}

MCPRuntimeJobService::Config _performance_job_config() {
	MCPRuntimeJobService::Config config;
	config.id_prefix = "performance";
	config.capture = "mcp_performance";
	config.rollback_operation = "stop";
	config.not_found_code = "PERFORMANCE_JOB_NOT_FOUND";
	config.not_found_message = "Runtime performance job was not found for this MCP session.";
	config.stale_code = "STALE_PERFORMANCE_JOB";
	config.stale_message = "Runtime performance job belongs to a different runtime generation or debugger session.";
	config.limit_code = "PERFORMANCE_JOB_LIMIT";
	config.limit_message = "The editor performance job record limit is full.";
	config.max_records = 32;
	return config;
}

} // namespace

MCPRuntimeDebugService::MCPRuntimeDebugService(MCPDebugCapture *p_debug_capture) :
		runtime_gateway(p_debug_capture),
		condition_jobs(&runtime_gateway, _condition_job_config()),
		performance_jobs(&runtime_gateway, _performance_job_config()),
		input_service(&runtime_gateway, p_debug_capture),
		target_action_service(&runtime_gateway, &input_service) {
}

MCPRuntimeDebugService::~MCPRuntimeDebugService() {
	shutdown_input();
}

Dictionary MCPRuntimeDebugService::_make_session_identity(int p_debugger_session, uint64_t p_runtime_generation) const {
	return runtime_gateway.make_session_identity(p_debugger_session, p_runtime_generation);
}

Dictionary MCPRuntimeDebugService::_resolve_session(const Dictionary &p_arguments, bool p_require_generation, ScriptEditorDebugger *&r_debugger,
		int &r_debugger_session, uint64_t &r_runtime_generation) const {
	MCPRuntimeDebuggerGateway::Session session;
	const Dictionary error = runtime_gateway.resolve_session(p_arguments, p_require_generation, session);
	r_debugger = session.debugger;
	r_debugger_session = session.debugger_session;
	r_runtime_generation = session.runtime_generation;
	return error;
}

Dictionary MCPRuntimeDebugService::get_state() const {
	Dictionary result;
	EditorRunBar *run_bar = EditorRunBar::get_singleton();
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	result["playing"] = run_bar && run_bar->is_playing();
	result["playingScene"] = run_bar ? run_bar->get_playing_scene() : String();
	result["processId"] = run_bar ? int64_t(run_bar->get_current_process()) : int64_t(0);
	Array sessions;
	if (debugger_node) {
		for (int i = 0; i < debugger_node->get_debugger_count(); i++) {
			ScriptEditorDebugger *debugger = debugger_node->get_debugger(i);
			if (!debugger || !debugger->is_session_active()) {
				continue;
			}
			Dictionary session;
			session["debuggerSession"] = i;
			session["current"] = i == debugger_node->get_current_debugger_id();
			session["processId"] = int64_t(debugger->get_remote_pid());
			session["breaked"] = debugger->is_breaked();
			session["debuggable"] = debugger->is_debuggable();
			uint64_t generation = 0;
			if (runtime_gateway.get_runtime_generation(i, generation)) {
				session["runtimeGeneration"] = int64_t(generation);
			}
			sessions.push_back(session);
		}
	}
	result["sessionCount"] = sessions.size();
	result["sessions"] = sessions;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::play(const Dictionary &p_arguments) const {
	EditorRunBar *run_bar = EditorRunBar::get_singleton();
	if (!run_bar) {
		return _error("RUNTIME_UNAVAILABLE", "The editor run bar is not available.");
	}
	const Variant mode_value = p_arguments.get("mode", "main");
	const Variant restart_value = p_arguments.get("restart", false);
	if (mode_value.get_type() != Variant::STRING || restart_value.get_type() != Variant::BOOL) {
		return _error("INVALID_ARGUMENTS", "mode must be a string and restart must be boolean.");
	}
	const String mode = mode_value;
	const bool restart = restart_value;
	String scene_path;
	if (mode == "main") {
		scene_path = GLOBAL_GET("application/run/main_scene");
		if (scene_path.is_empty()) {
			return _error("NO_MAIN_SCENE", "The project does not define a main scene.");
		}
	} else if (mode == "current") {
		if (!EditorNode::get_singleton()->get_edited_scene()) {
			return _error("NO_SCENE", "No edited scene is open.");
		}
	} else if (mode == "scene") {
		const Variant path_value = p_arguments.get("path", Variant());
		if (path_value.get_type() != Variant::STRING || String(path_value).is_empty()) {
			return _error("INVALID_ARGUMENTS", "path is required when mode is scene.");
		}
		String absolute_path;
		String path_error;
		if (MCPPathUtils::resolve_project_file_path(path_value, scene_path, absolute_path, &path_error) != OK ||
				!ResourceLoader::exists(scene_path, "PackedScene")) {
			return _error("INVALID_SCENE", path_error.is_empty() ? "The requested PackedScene does not exist." : path_error);
		}
	} else {
		return _error("INVALID_ARGUMENTS", "mode must be main, current, or scene.");
	}

	if (run_bar->is_playing()) {
		if (!restart) {
			return _error("RUNTIME_ALREADY_RUNNING", "A project is already running; pass restart=true to replace it.");
		}
		run_bar->stop_playing();
	}

	if (mode == "main") {
		run_bar->play_main_scene();
	} else if (mode == "current") {
		run_bar->play_current_scene();
	} else {
		run_bar->play_custom_scene(scene_path);
	}

	Dictionary result;
	result["accepted"] = true;
	result["mode"] = mode;
	result["state"] = "starting";
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::stop() {
	EditorRunBar *run_bar = EditorRunBar::get_singleton();
	if (!run_bar) {
		return _error("RUNTIME_UNAVAILABLE", "The editor run bar is not available.");
	}
	const bool was_playing = run_bar->is_playing();
	if (was_playing) {
		condition_jobs.release_all("The running project was stopped.");
		performance_jobs.release_all("The running project was stopped.");
		input_service.shutdown();
		run_bar->stop_playing();
	}
	Dictionary result;
	result["stopped"] = was_playing;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::set_suspended(const Dictionary &p_arguments, bool p_suspended) const {
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!error.is_empty()) {
		return error;
	}
	debugger->send_message("scene:suspend_changed", Array{ p_suspended });
	Dictionary result = _make_session_identity(debugger_session, generation);
	result["suspended"] = p_suspended;
	result["dispatched"] = true;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::next_frame(const Dictionary &p_arguments) const {
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!error.is_empty()) {
		return error;
	}
	debugger->send_message("scene:next_frame", Array());
	Dictionary result = _make_session_identity(debugger_session, generation);
	result["dispatched"] = true;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::debug_control(const Dictionary &p_arguments, const String &p_operation) const {
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!error.is_empty()) {
		return error;
	}

	const bool breaked = debugger->is_breaked();
	if (p_operation == "break") {
		if (breaked) {
			return _error("RUNTIME_ALREADY_BREAKED", "The requested debugger session is already paused at a breakpoint.");
		}
		debugger->debug_break();
	} else {
		if (!breaked || !debugger->is_debuggable()) {
			return _error("RUNTIME_NOT_BREAKED", "The requested debugger session is not paused at a debuggable breakpoint.");
		}
		if (p_operation == "continue") {
			debugger->debug_continue();
		} else if (p_operation == "step_into") {
			debugger->debug_step();
		} else if (p_operation == "step_over") {
			debugger->debug_next();
		} else if (p_operation == "step_out") {
			debugger->debug_out();
		} else {
			return _error("INVALID_ARGUMENTS", "Unknown debugger control operation.");
		}
	}

	Dictionary result = _make_session_identity(debugger_session, generation);
	result["operation"] = p_operation;
	result["dispatched"] = true;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::_refresh_tree(ScriptEditorDebugger *p_debugger, int p_timeout_msec) const {
	const uint64_t previous_revision = p_debugger->get_remote_tree_revision();
	p_debugger->request_remote_tree();
	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + uint64_t(p_timeout_msec) * 1000;
	while (p_debugger->is_session_active() && OS::get_singleton()->get_ticks_usec() < deadline) {
		p_debugger->poll_peer_messages(2000);
		if (p_debugger->get_remote_tree_revision() > previous_revision) {
			return Dictionary();
		}
		OS::get_singleton()->delay_usec(500);
	}
	return _error("RUNTIME_TIMEOUT", "Timed out waiting for the running project's scene tree.");
}

Dictionary MCPRuntimeDebugService::_find_node(ScriptEditorDebugger *p_debugger, const String &p_path, ObjectID &r_object_id, Dictionary &r_node) const {
	const SceneDebuggerTree *tree = p_debugger->get_remote_tree();
	if (!tree || !_find_tree_node(tree, p_path, r_object_id, r_node)) {
		return _error("RUNTIME_NODE_NOT_FOUND", "Runtime node was not found: " + p_path);
	}
	return Dictionary();
}

Dictionary MCPRuntimeDebugService::get_tree(const Dictionary &p_arguments) const {
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	Dictionary error = _resolve_session(p_arguments, false, debugger, debugger_session, generation);
	if (!error.is_empty()) {
		return error;
	}
	error = _refresh_tree(debugger, _timeout_from_arguments(p_arguments));
	if (!error.is_empty()) {
		return error;
	}
	Dictionary result = _make_session_identity(debugger_session, generation);
	result["treeRevision"] = int64_t(debugger->get_remote_tree_revision());
	result["nodes"] = _make_tree_nodes(debugger->get_remote_tree());
	result["nodeCount"] = Array(result["nodes"]).size();
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::_request_debugger_message(const Dictionary &p_arguments, const String &p_capture,
		const String &p_operation, const Dictionary &p_payload, bool p_require_generation) const {
	return runtime_gateway.request(p_arguments, p_capture, p_operation, p_payload, p_require_generation);
}

Dictionary MCPRuntimeDebugService::_request_observation(const Dictionary &p_arguments, const String &p_operation,
		const Dictionary &p_payload, bool p_require_generation) const {
	return _request_debugger_message(p_arguments, "mcp_observation", p_operation, p_payload, p_require_generation);
}

Dictionary MCPRuntimeDebugService::get_screenshot(const Dictionary &p_arguments) const {
	Dictionary payload;
	if (p_arguments.has("name")) {
		payload["name"] = p_arguments["name"];
	}
	if (p_arguments.has("crop")) {
		payload["crop"] = p_arguments["crop"];
	}
	return _request_observation(p_arguments, "get_screenshot", payload);
}

Dictionary MCPRuntimeDebugService::get_viewport_summary(const Dictionary &p_arguments) const {
	Dictionary payload;
	if (p_arguments.has("includeCounts")) {
		payload["includeCounts"] = p_arguments["includeCounts"];
	}
	return _request_observation(p_arguments, "get_viewport_summary", payload);
}

Dictionary MCPRuntimeDebugService::query_nodes(const Dictionary &p_arguments) const {
	Dictionary payload;
	if (p_arguments.has("filter")) {
		payload["filter"] = p_arguments["filter"];
	}
	if (p_arguments.has("maxResults")) {
		payload["maxResults"] = p_arguments["maxResults"];
	}
	return _request_observation(p_arguments, "query_nodes", payload);
}

Dictionary MCPRuntimeDebugService::get_interactables(const Dictionary &p_arguments) const {
	Dictionary payload;
	if (p_arguments.has("filter")) {
		payload["filter"] = p_arguments["filter"];
	}
	if (p_arguments.has("maxResults")) {
		payload["maxResults"] = p_arguments["maxResults"];
	}
	return _request_observation(p_arguments, "get_interactables", payload);
}

Dictionary MCPRuntimeDebugService::get_node_snapshot(const Dictionary &p_arguments) const {
	Dictionary payload;
	payload["selector"] = p_arguments.get("selector", Dictionary());
	return _request_observation(p_arguments, "get_node_snapshot", payload);
}

Dictionary MCPRuntimeDebugService::start_wait(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant condition_value = p_arguments.get("condition", Variant());
	if (condition_value.get_type() != Variant::DICTIONARY) {
		return _error("INVALID_ARGUMENTS", "condition must be an object.");
	}
	int64_t timeout_frames = 300;
	int64_t poll_frames = 1;
	if ((p_arguments.has("timeoutFrames") && !MCPToolUtils::try_get_json_integer(p_arguments["timeoutFrames"], 1, 36000, timeout_frames)) ||
			(p_arguments.has("pollEveryFrames") && !MCPToolUtils::try_get_json_integer(p_arguments["pollEveryFrames"], 1, 600, poll_frames))) {
		return _error("INVALID_ARGUMENTS", "timeoutFrames or pollEveryFrames is outside its allowed range.");
	}
	Dictionary payload;
	payload["condition"] = condition_value;
	payload["timeoutFrames"] = timeout_frames;
	payload["pollEveryFrames"] = poll_frames;
	return condition_jobs.start(p_arguments, p_mcp_session_id, payload);
}

Dictionary MCPRuntimeDebugService::get_wait_status(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return condition_jobs.get_status(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::cancel_wait(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return condition_jobs.finish(p_arguments, p_mcp_session_id, "cancel");
}

Dictionary MCPRuntimeDebugService::start_performance(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant name_value = p_arguments.get("name", String());
	if (name_value.get_type() != Variant::STRING || String(name_value).length() > 256) {
		return _error("INVALID_ARGUMENTS", "name must be a string no longer than 256 characters.");
	}
	int64_t top_frames = 10;
	int64_t max_frames = 0;
	if ((p_arguments.has("topFrames") && !MCPToolUtils::try_get_json_integer(p_arguments["topFrames"], 0, 100, top_frames)) ||
			(p_arguments.has("maxFrames") && !MCPToolUtils::try_get_json_integer(p_arguments["maxFrames"], 1, 36000, max_frames))) {
		return _error("INVALID_ARGUMENTS", "topFrames or maxFrames is outside its allowed range.");
	}
	Dictionary payload;
	payload["name"] = name_value;
	payload["topFrames"] = top_frames;
	payload["maxFrames"] = max_frames;
	return performance_jobs.start(p_arguments, p_mcp_session_id, payload);
}

Dictionary MCPRuntimeDebugService::get_performance_status(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return performance_jobs.get_status(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::stop_performance(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return performance_jobs.finish(p_arguments, p_mcp_session_id, "stop", "summary");
}

Dictionary MCPRuntimeDebugService::click_target(const Dictionary &p_arguments, const String &p_mcp_session_id, bool p_double_click) {
	return target_action_service.click(p_arguments, p_mcp_session_id, p_double_click);
}

Dictionary MCPRuntimeDebugService::hover_target(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return target_action_service.hover(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::focus_target(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return target_action_service.focus(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::drag_target_to_target(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return target_action_service.drag(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::type_text(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return target_action_service.type_text(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::scroll_view(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return target_action_service.scroll(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::_refresh_object(ScriptEditorDebugger *p_debugger, ObjectID p_object_id, int p_timeout_msec) const {
	const uint64_t previous_revision = p_debugger->get_remote_object_revision(p_object_id);
	TypedArray<uint64_t> ids;
	ids.push_back(uint64_t(p_object_id));
	p_debugger->request_remote_objects(ids, false);
	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + uint64_t(p_timeout_msec) * 1000;
	while (p_debugger->is_session_active() && OS::get_singleton()->get_ticks_usec() < deadline) {
		p_debugger->poll_peer_messages(2000);
		if (p_debugger->get_remote_object_revision(p_object_id) > previous_revision) {
			return Dictionary();
		}
		OS::get_singleton()->delay_usec(500);
	}
	return _error("RUNTIME_TIMEOUT", "Timed out waiting for the running project's node properties.");
}

Dictionary MCPRuntimeDebugService::get_properties(const Dictionary &p_arguments) const {
	const Variant path_value = p_arguments.get("path", Variant());
	if (path_value.get_type() != Variant::STRING || String(path_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "path must be a non-empty string.");
	}
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	Dictionary error = _resolve_session(p_arguments, false, debugger, debugger_session, generation);
	if (!error.is_empty()) {
		return error;
	}
	const int timeout = _timeout_from_arguments(p_arguments);
	error = _refresh_tree(debugger, timeout);
	if (!error.is_empty()) {
		return error;
	}
	ObjectID object_id;
	Dictionary node;
	error = _find_node(debugger, path_value, object_id, node);
	if (!error.is_empty()) {
		return error;
	}
	error = _refresh_object(debugger, object_id, timeout);
	if (!error.is_empty()) {
		return error;
	}
	EditorDebuggerRemoteObjects *remote_object = debugger->get_cached_remote_object(object_id);
	if (!remote_object) {
		return _error("RUNTIME_DATA_UNAVAILABLE", "The running project did not return inspectable node properties.");
	}
	Dictionary result = _make_session_identity(debugger_session, generation);
	result.merge(node, true);
	result.merge(_encode_remote_properties(remote_object, object_id), true);
	result["objectRevision"] = int64_t(debugger->get_remote_object_revision(object_id));
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::set_property(const Dictionary &p_arguments) const {
	const Variant path_value = p_arguments.get("path", Variant());
	const Variant property_value = p_arguments.get("property", Variant());
	if (path_value.get_type() != Variant::STRING || String(path_value).is_empty() ||
			property_value.get_type() != Variant::STRING || String(property_value).is_empty() || !p_arguments.has("value")) {
		return _error("INVALID_ARGUMENTS", "path, property, and value are required.");
	}
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	Dictionary error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!error.is_empty()) {
		return error;
	}
	const int timeout = _timeout_from_arguments(p_arguments);
	error = _refresh_tree(debugger, timeout);
	if (!error.is_empty()) {
		return error;
	}
	ObjectID object_id;
	Dictionary node;
	error = _find_node(debugger, path_value, object_id, node);
	if (!error.is_empty()) {
		return error;
	}
	error = _refresh_object(debugger, object_id, timeout);
	if (!error.is_empty()) {
		return error;
	}
	EditorDebuggerRemoteObjects *remote_object = debugger->get_cached_remote_object(object_id);
	const PropertyInfo *property = remote_object ? _find_property(remote_object, StringName(property_value)) : nullptr;
	if (!property) {
		return _error("RUNTIME_PROPERTY_NOT_FOUND", "Runtime property was not found: " + String(property_value));
	}
	if (property->usage & (PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP)) {
		return _error("RUNTIME_PROPERTY_READ_ONLY", "Runtime property is not editable: " + String(property_value));
	}
	Variant decoded_value;
	String codec_error;
	if (MCPVariantCodec::decode(p_arguments["value"], decoded_value, &codec_error) != OK) {
		return _error("INVALID_VALUE", codec_error);
	}
	const Variant previous_value = remote_object->get_variant(property_value);
	debugger->update_remote_object(object_id, property_value, decoded_value);
	error = _refresh_object(debugger, object_id, timeout);
	if (!error.is_empty()) {
		return error;
	}
	remote_object = debugger->get_cached_remote_object(object_id);
	const Variant effective_value = remote_object ? remote_object->get_variant(property_value) : Variant();
	if (remote_object && effective_value != decoded_value && effective_value == previous_value) {
		Dictionary details;
		details["path"] = node["path"];
		details["property"] = property_value;
		return MCPToolUtils::make_error_result("RUNTIME_PROPERTY_REJECTED", "The runtime object rejected the property value.", details);
	}
	Dictionary result = _make_session_identity(debugger_session, generation);
	result["path"] = node["path"];
	result["objectId"] = String::num_uint64(uint64_t(object_id));
	result["property"] = property_value;
	result["objectRevision"] = int64_t(debugger->get_remote_object_revision(object_id));
	Variant encoded_value;
	result["coerced"] = remote_object && effective_value != decoded_value;
	if (remote_object && MCPVariantCodec::encode(effective_value, encoded_value, &codec_error) == OK) {
		result["value"] = encoded_value;
	} else {
		result["valueAvailable"] = false;
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::send_input(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return input_service.send_input(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::start_input_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return input_service.start_sequence(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::get_input_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return input_service.get_sequence(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::cancel_input_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return input_service.cancel_sequence(p_arguments, p_mcp_session_id);
}

Dictionary MCPRuntimeDebugService::release_input(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	return input_service.release_input(p_arguments, p_mcp_session_id);
}

void MCPRuntimeDebugService::process_input() {
	input_service.process();
	condition_jobs.process();
	performance_jobs.process();
}

void MCPRuntimeDebugService::release_mcp_session(const String &p_mcp_session_id) {
	condition_jobs.release_session(p_mcp_session_id);
	performance_jobs.release_session(p_mcp_session_id);
	input_service.release_session(p_mcp_session_id);
}

void MCPRuntimeDebugService::shutdown_input() {
	condition_jobs.release_all("The MCP Host shut down.");
	performance_jobs.release_all("The MCP Host shut down.");
	input_service.shutdown();
}
