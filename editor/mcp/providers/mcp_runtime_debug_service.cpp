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

#include "mcp_debug_capture.h"
#include "mcp_path_utils.h"
#include "mcp_runtime_input.h"
#include "mcp_runtime_input_sequence.h"
#include "mcp_runtime_observation_debugger_plugin.h"
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
constexpr int DEFAULT_OBSERVATION_TIMEOUT_MSEC = 500;
constexpr int MAX_OBSERVATION_TIMEOUT_MSEC = 1500;

Dictionary _error(const String &p_code, const String &p_message) {
	return MCPToolUtils::make_error_result(p_code, p_message);
}

int _timeout_from_arguments(const Dictionary &p_arguments) {
	int64_t timeout = DEFAULT_TIMEOUT_MSEC;
	MCPToolUtils::try_get_json_integer(p_arguments.get("timeoutMs", DEFAULT_TIMEOUT_MSEC), 50, 5000, timeout);
	return int(timeout);
}

bool _observation_timeout_from_arguments(const Dictionary &p_arguments, int &r_timeout) {
	int64_t timeout = DEFAULT_OBSERVATION_TIMEOUT_MSEC;
	if (p_arguments.has("timeoutMs") && !MCPToolUtils::try_get_json_integer(p_arguments["timeoutMs"], 50, MAX_OBSERVATION_TIMEOUT_MSEC, timeout)) {
		return false;
	}
	r_timeout = int(timeout);
	return true;
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

} // namespace

MCPRuntimeDebugService::MCPRuntimeDebugService(MCPDebugCapture *p_debug_capture) :
		input_scheduler(p_debug_capture) {
	debug_capture = p_debug_capture;
}

MCPRuntimeDebugService::~MCPRuntimeDebugService() {
	shutdown_input();
}

Dictionary MCPRuntimeDebugService::_make_session_identity(int p_debugger_session, uint64_t p_runtime_generation) const {
	Dictionary identity;
	identity["debuggerSession"] = p_debugger_session;
	identity["runtimeGeneration"] = int64_t(p_runtime_generation);
	return identity;
}

Dictionary MCPRuntimeDebugService::_resolve_session(const Dictionary &p_arguments, bool p_require_generation, ScriptEditorDebugger *&r_debugger,
		int &r_debugger_session, uint64_t &r_runtime_generation) const {
	r_debugger = nullptr;
	r_debugger_session = -1;
	r_runtime_generation = 0;
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node || !debug_capture) {
		return _error("RUNTIME_UNAVAILABLE", "The editor debugger is not available.");
	}

	if (p_arguments.has("debuggerSession")) {
		int64_t requested_session = 0;
		if (!MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, requested_session)) {
			return _error("INVALID_ARGUMENTS", "debuggerSession must be a non-negative integer.");
		}
		if (requested_session >= debugger_node->get_debugger_count()) {
			return _error("RUNTIME_NOT_FOUND", "The requested debugger session does not exist.");
		}
		ScriptEditorDebugger *candidate = debugger_node->get_debugger(int(requested_session));
		if (!candidate || !candidate->is_session_active()) {
			return _error("RUNTIME_NOT_RUNNING", "The requested debugger session is not active.");
		}
		r_debugger = candidate;
		r_debugger_session = int(requested_session);
	} else {
		for (int i = 0; i < debugger_node->get_debugger_count(); i++) {
			ScriptEditorDebugger *candidate = debugger_node->get_debugger(i);
			if (!candidate || !candidate->is_session_active()) {
				continue;
			}
			if (r_debugger) {
				return _error("AMBIGUOUS_RUNTIME", "Multiple running project sessions are active; provide debuggerSession.");
			}
			r_debugger = candidate;
			r_debugger_session = i;
		}
		if (!r_debugger) {
			return _error("RUNTIME_NOT_RUNNING", "No running project debugger session is active.");
		}
	}

	if (!debug_capture->get_runtime_generation(r_debugger_session, r_runtime_generation)) {
		return _error("RUNTIME_STARTING", "The running project has not completed debugger initialization.");
	}
	if (p_require_generation) {
		int64_t expected_generation = 0;
		if (!p_arguments.has("runtimeGeneration") ||
				!MCPToolUtils::try_get_json_integer(p_arguments["runtimeGeneration"], 1, INT64_MAX, expected_generation)) {
			return _error("INVALID_ARGUMENTS", "A positive runtimeGeneration returned by get_state or get_tree is required.");
		}
		if (uint64_t(expected_generation) != r_runtime_generation) {
			return _error("STALE_RUNTIME", "The running project restarted; refresh runtime state before mutating it.");
		}
	}
	return Dictionary();
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
			if (debug_capture && debug_capture->get_runtime_generation(i, generation)) {
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
		input_scheduler.release_all();
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

Dictionary MCPRuntimeDebugService::_request_observation(const Dictionary &p_arguments, const String &p_operation, const Dictionary &p_payload) const {
	int timeout_msec = DEFAULT_OBSERVATION_TIMEOUT_MSEC;
	if (!_observation_timeout_from_arguments(p_arguments, timeout_msec)) {
		return _error("INVALID_ARGUMENTS", vformat("timeoutMs must be an integer between 50 and %d for runtime observations.", MAX_OBSERVATION_TIMEOUT_MSEC));
	}
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary session_error = _resolve_session(p_arguments, false, debugger, debugger_session, generation);
	if (!session_error.is_empty()) {
		return session_error;
	}
	if (!observation_plugin) {
		return _error("RUNTIME_OBSERVATION_UNAVAILABLE", "The MCP runtime observation debugger plugin is not active.");
	}
	const String request_id = vformat("%d-%d", OS::get_singleton()->get_process_id(), next_observation_request_id++);
	if (!observation_plugin->register_request(debugger_session, request_id, p_operation)) {
		return _error("RUNTIME_OBSERVATION_BUSY", "Unable to reserve a runtime observation request.");
	}
	debugger->send_message("mcp_observation:" + p_operation, Array{ request_id, p_payload });
	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + uint64_t(timeout_msec) * 1000;
	MCPRuntimeObservationDebuggerPlugin::Response response;
	// Observation messages are bounded, short round trips. Long runtime jobs must use asynchronous request state.
	while (debugger->is_session_active() && OS::get_singleton()->get_ticks_usec() < deadline) {
		debugger->poll_peer_messages(2000);
		if (observation_plugin->take_response(debugger_session, request_id, response)) {
			if (!response.ok) {
				return MCPToolUtils::make_error_result(response.code.is_empty() ? "RUNTIME_OBSERVATION_FAILED" : response.code,
						response.message.is_empty() ? "Runtime observation failed." : response.message, response.data);
			}
			Dictionary result = _make_session_identity(debugger_session, generation);
			result.merge(response.data, true);
			return MCPToolUtils::make_success_result(result);
		}
		OS::get_singleton()->delay_usec(500);
	}
	observation_plugin->cancel_request(debugger_session, request_id);
	return debugger->is_session_active() ? _error("RUNTIME_TIMEOUT", "Timed out waiting for the running project's observation response.") : _error("RUNTIME_NOT_RUNNING", "The running project stopped before returning the observation response.");
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
	const Variant events_value = p_arguments.get("events", Variant());
	if (events_value.get_type() != Variant::ARRAY) {
		return _error("INVALID_ARGUMENTS", "events must be an array.");
	}
	const Array events = events_value;
	if (events.is_empty() || events.size() > 128) {
		return _error("INVALID_ARGUMENTS", "events must contain between 1 and 128 entries.");
	}
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!error.is_empty()) {
		return error;
	}
	Vector<MCPRuntimeInput::EncodedEvent> encoded_events;
	for (int i = 0; i < events.size(); i++) {
		if (events[i].get_type() != Variant::DICTIONARY) {
			return _error("INVALID_INPUT_EVENT", vformat("events[%d] must be an object.", i));
		}
		MCPRuntimeInput::EncodedEvent encoded;
		String input_error;
		if (MCPRuntimeInput::encode(events[i], encoded, &input_error, true) != OK) {
			return _error("INVALID_INPUT_EVENT", vformat("events[%d]: %s", i, input_error));
		}
		encoded_events.push_back(encoded);
	}
	int dispatched = 0;
	String dispatch_error;
	if (input_scheduler.dispatch_immediate(p_mcp_session_id, debugger, debugger_session, generation, encoded_events, dispatched, dispatch_error) != OK) {
		return _error("RUNTIME_INPUT_FAILED", dispatch_error);
	}
	Dictionary result = _make_session_identity(debugger_session, generation);
	result["dispatched"] = true;
	result["eventCount"] = dispatched;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::start_input_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant steps_value = p_arguments.get("steps", Variant());
	if (steps_value.get_type() != Variant::ARRAY) {
		return _error("INVALID_ARGUMENTS", "steps must be an array.");
	}
	Vector<MCPRuntimeInputSequence::Step> steps;
	String compile_error;
	if (MCPRuntimeInputSequence::compile(steps_value, steps, &compile_error) != OK) {
		return _error("INVALID_INPUT_SEQUENCE", compile_error);
	}

	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary session_error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!session_error.is_empty()) {
		return session_error;
	}
	Dictionary result;
	String scheduler_error;
	const Error error = input_scheduler.start_sequence(p_mcp_session_id, debugger_session, generation, steps, result, scheduler_error);
	if (error != OK) {
		return _error(error == ERR_BUSY ? "INPUT_SEQUENCE_BUSY" : "INPUT_SEQUENCE_FAILED", scheduler_error);
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::get_input_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant sequence_value = p_arguments.get("sequenceId", Variant());
	if (sequence_value.get_type() != Variant::STRING || String(sequence_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "sequenceId must be a non-empty string.");
	}
	Dictionary result;
	String scheduler_error;
	const Error error = input_scheduler.get_sequence(p_mcp_session_id, sequence_value, result, scheduler_error);
	if (error != OK) {
		return _error(error == ERR_DOES_NOT_EXIST ? "INPUT_SEQUENCE_NOT_FOUND" : "INPUT_SEQUENCE_QUERY_FAILED", scheduler_error);
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::cancel_input_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant sequence_value = p_arguments.get("sequenceId", Variant());
	if (sequence_value.get_type() != Variant::STRING || String(sequence_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "sequenceId must be a non-empty string.");
	}
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary session_error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!session_error.is_empty()) {
		return session_error;
	}
	Dictionary result;
	String scheduler_error;
	const Error error = input_scheduler.cancel_sequence(p_mcp_session_id, sequence_value, debugger_session, generation, result, scheduler_error);
	if (error != OK) {
		return _error(error == ERR_DOES_NOT_EXIST ? "INPUT_SEQUENCE_NOT_FOUND" : "INPUT_SEQUENCE_MISMATCH", scheduler_error);
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::release_input(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary session_error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!session_error.is_empty()) {
		return session_error;
	}
	int released = 0;
	String release_error;
	if (input_scheduler.release_session_inputs(p_mcp_session_id, debugger_session, generation, true, released, release_error) != OK) {
		return _error("RUNTIME_INPUT_RELEASE_FAILED", release_error);
	}
	Dictionary result = _make_session_identity(debugger_session, generation);
	result["releasedCount"] = released;
	result["sequencesCancelled"] = true;
	return MCPToolUtils::make_success_result(result);
}

void MCPRuntimeDebugService::process_input() {
	input_scheduler.process();
}

void MCPRuntimeDebugService::release_mcp_session(const String &p_mcp_session_id) {
	input_scheduler.release_mcp_session(p_mcp_session_id);
}

void MCPRuntimeDebugService::shutdown_input() {
	input_scheduler.release_all();
}
