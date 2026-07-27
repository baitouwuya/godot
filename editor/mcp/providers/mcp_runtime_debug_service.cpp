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
#include "mcp_runtime_input.h"
#include "mcp_runtime_input_sequence.h"
#include "mcp_runtime_target_action.h"
#include "mcp_tool_utils.h"
#include "mcp_variant_codec.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"
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

bool _is_error_result(const Dictionary &p_result) {
	return bool(p_result.get("isError", false));
}

bool _read_bounded_integer(const Dictionary &p_arguments, const StringName &p_name, int p_default, int p_minimum,
		int p_maximum, int &r_value, String &r_error) {
	int64_t value = p_default;
	if (p_arguments.has(p_name) && !MCPToolUtils::try_get_json_integer(p_arguments[p_name], p_minimum, p_maximum, value)) {
		r_error = vformat("%s must be an integer between %d and %d.", String(p_name), p_minimum, p_maximum);
		return false;
	}
	r_value = int(value);
	return true;
}

bool _read_string_argument(const Dictionary &p_arguments, const StringName &p_name, const String &p_default,
		String &r_value, String &r_error) {
	const Variant value = p_arguments.get(p_name, p_default);
	if (value.get_type() != Variant::STRING) {
		r_error = String(p_name) + " must be a string.";
		return false;
	}
	r_value = value;
	return true;
}

bool _read_resolved_target(const Dictionary &p_resolution, int p_index, Dictionary &r_target, Dictionary &r_node,
		Vector2 &r_position, bool &r_has_position) {
	const Dictionary content = p_resolution.get("structuredContent", Dictionary());
	const Variant targets_value = content.get("targets", Variant());
	if (targets_value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array targets = targets_value;
	if (p_index < 0 || p_index >= targets.size() || targets[p_index].get_type() != Variant::DICTIONARY) {
		return false;
	}
	r_target = targets[p_index];
	const Variant node_value = r_target.get("node", Variant());
	if (node_value.get_type() != Variant::DICTIONARY) {
		return false;
	}
	r_node = node_value;
	r_has_position = MCPRuntimeTargetAction::parse_position(r_target.get("position", Variant()), r_position);
	return true;
}

} // namespace

MCPRuntimeDebugService::MCPRuntimeDebugService(MCPDebugCapture *p_debug_capture) :
		runtime_gateway(p_debug_capture),
		input_scheduler(p_debug_capture) {
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
		_release_all_condition_jobs("The running project was stopped.");
		_release_all_performance_jobs("The running project was stopped.");
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

Dictionary MCPRuntimeDebugService::_request_condition(const Dictionary &p_arguments, const String &p_operation,
		const Dictionary &p_payload, bool p_require_generation) const {
	return _request_debugger_message(p_arguments, "mcp_condition", p_operation, p_payload, p_require_generation);
}

Dictionary MCPRuntimeDebugService::_request_performance(const Dictionary &p_arguments, const String &p_operation,
		const Dictionary &p_payload, bool p_require_generation) const {
	return _request_debugger_message(p_arguments, "mcp_performance", p_operation, p_payload, p_require_generation);
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

void MCPRuntimeDebugService::_prune_condition_jobs() {
	constexpr int MAX_EDITOR_CONDITION_JOBS = 256;
	while (condition_jobs.size() >= MAX_EDITOR_CONDITION_JOBS) {
		int remove_index = -1;
		for (int i = 0; i < condition_job_order.size(); i++) {
			const ConditionJobRecord *record = condition_jobs.getptr(condition_job_order[i]);
			if (!record || String(record->state.get("state", String())) != "running") {
				remove_index = i;
				break;
			}
		}
		if (remove_index < 0) {
			return;
		}
		condition_jobs.erase(condition_job_order[remove_index]);
		condition_job_order.remove_at(remove_index);
	}
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
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary session_error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!session_error.is_empty()) {
		return session_error;
	}
	_prune_condition_jobs();
	if (condition_jobs.size() >= 256) {
		return _error("WAIT_JOB_LIMIT", "The editor wait job record limit is full.");
	}
	const String job_id = "wait-" + String::num_uint64(next_condition_job_id++);
	Dictionary payload;
	payload["jobId"] = job_id;
	payload["mcpSessionId"] = p_mcp_session_id;
	payload["condition"] = condition_value;
	payload["timeoutFrames"] = timeout_frames;
	payload["pollEveryFrames"] = poll_frames;
	const Dictionary response = _request_condition(p_arguments, "start", payload, true);
	if (_is_error_result(response)) {
		return response;
	}
	ConditionJobRecord record;
	record.mcp_session_id = p_mcp_session_id;
	record.debugger_session = debugger_session;
	record.runtime_generation = generation;
	record.state = response.get("structuredContent", Dictionary());
	condition_jobs.insert(job_id, record);
	condition_job_order.push_back(job_id);
	return response;
}

Dictionary MCPRuntimeDebugService::get_wait_status(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant job_value = p_arguments.get("jobId", Variant());
	if (job_value.get_type() != Variant::STRING || String(job_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "jobId must be a non-empty string.");
	}
	const String job_id = job_value;
	ConditionJobRecord *record = condition_jobs.getptr(job_id);
	if (!record || record->mcp_session_id != p_mcp_session_id) {
		return _error("WAIT_JOB_NOT_FOUND", "Runtime wait job was not found for this MCP session.");
	}
	int64_t requested_generation = 0;
	int64_t requested_debugger = record->debugger_session;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("runtimeGeneration", Variant()), 1, INT64_MAX, requested_generation) ||
			(p_arguments.has("debuggerSession") && !MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, requested_debugger)) ||
			uint64_t(requested_generation) != record->runtime_generation || int(requested_debugger) != record->debugger_session) {
		return _error("STALE_WAIT_JOB", "Runtime wait job belongs to a different runtime generation or debugger session.");
	}
	const String state = record->state.get("state", String());
	if (state != "running") {
		return MCPToolUtils::make_success_result(record->state);
	}
	Dictionary payload;
	payload["jobId"] = job_id;
	payload["mcpSessionId"] = p_mcp_session_id;
	const Dictionary response = _request_condition(p_arguments, "status", payload, true);
	if (!_is_error_result(response)) {
		record->state = response.get("structuredContent", Dictionary());
	}
	return response;
}

Dictionary MCPRuntimeDebugService::cancel_wait(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant job_value = p_arguments.get("jobId", Variant());
	if (job_value.get_type() != Variant::STRING || String(job_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "jobId must be a non-empty string.");
	}
	const String job_id = job_value;
	ConditionJobRecord *record = condition_jobs.getptr(job_id);
	if (!record || record->mcp_session_id != p_mcp_session_id) {
		return _error("WAIT_JOB_NOT_FOUND", "Runtime wait job was not found for this MCP session.");
	}
	int64_t requested_generation = 0;
	int64_t requested_debugger = record->debugger_session;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("runtimeGeneration", Variant()), 1, INT64_MAX, requested_generation) ||
			(p_arguments.has("debuggerSession") && !MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, requested_debugger)) ||
			uint64_t(requested_generation) != record->runtime_generation || int(requested_debugger) != record->debugger_session) {
		return _error("STALE_WAIT_JOB", "Runtime wait job belongs to a different runtime generation or debugger session.");
	}
	if (String(record->state.get("state", String())) != "running") {
		return MCPToolUtils::make_success_result(record->state);
	}
	Dictionary payload;
	payload["jobId"] = job_id;
	payload["mcpSessionId"] = p_mcp_session_id;
	const Dictionary response = _request_condition(p_arguments, "cancel", payload, true);
	if (!_is_error_result(response)) {
		record->state = response.get("structuredContent", Dictionary());
	}
	return response;
}

void MCPRuntimeDebugService::_release_condition_session(const String &p_mcp_session_id) {
	HashSet<int> debugger_sessions;
	for (const KeyValue<String, ConditionJobRecord> &entry : condition_jobs) {
		if (entry.value.mcp_session_id == p_mcp_session_id && String(entry.value.state.get("state", String())) == "running") {
			debugger_sessions.insert(entry.value.debugger_session);
		}
	}
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	for (const int debugger_session : debugger_sessions) {
		ScriptEditorDebugger *debugger = debugger_node ? debugger_node->get_debugger(debugger_session) : nullptr;
		if (debugger && debugger->is_session_active()) {
			Dictionary payload;
			payload["mcpSessionId"] = p_mcp_session_id;
			debugger->send_message("mcp_condition:release_session", Array{ String(), payload });
		}
	}
	for (int i = condition_job_order.size() - 1; i >= 0; i--) {
		const ConditionJobRecord *record = condition_jobs.getptr(condition_job_order[i]);
		if (record && record->mcp_session_id == p_mcp_session_id) {
			condition_jobs.erase(condition_job_order[i]);
			condition_job_order.remove_at(i);
		}
	}
}

void MCPRuntimeDebugService::_release_all_condition_jobs(const String &p_reason) {
	HashSet<int> debugger_sessions;
	for (KeyValue<String, ConditionJobRecord> &entry : condition_jobs) {
		if (String(entry.value.state.get("state", String())) == "running") {
			debugger_sessions.insert(entry.value.debugger_session);
			entry.value.state["state"] = "cancelled";
			entry.value.state["failure"] = p_reason;
		}
	}
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	for (const int debugger_session : debugger_sessions) {
		ScriptEditorDebugger *debugger = debugger_node ? debugger_node->get_debugger(debugger_session) : nullptr;
		if (debugger && debugger->is_session_active()) {
			debugger->send_message("mcp_condition:release_all", Array{ String(), Dictionary() });
		}
	}
}

void MCPRuntimeDebugService::_prune_performance_jobs() {
	constexpr int MAX_EDITOR_PERFORMANCE_JOBS = 32;
	while (performance_jobs.size() >= MAX_EDITOR_PERFORMANCE_JOBS) {
		int remove_index = -1;
		for (int i = 0; i < performance_job_order.size(); i++) {
			const PerformanceJobRecord *record = performance_jobs.getptr(performance_job_order[i]);
			if (!record || String(record->state.get("state", String())) != "running") {
				remove_index = i;
				break;
			}
		}
		if (remove_index < 0) {
			return;
		}
		performance_jobs.erase(performance_job_order[remove_index]);
		performance_job_order.remove_at(remove_index);
	}
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
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary session_error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!session_error.is_empty()) {
		return session_error;
	}
	_prune_performance_jobs();
	if (performance_jobs.size() >= 32) {
		return _error("PERFORMANCE_JOB_LIMIT", "The editor performance job record limit is full.");
	}
	const String job_id = "performance-" + String::num_uint64(next_performance_job_id++);
	Dictionary payload;
	payload["jobId"] = job_id;
	payload["mcpSessionId"] = p_mcp_session_id;
	payload["name"] = name_value;
	payload["topFrames"] = top_frames;
	payload["maxFrames"] = max_frames;
	const Dictionary response = _request_performance(p_arguments, "start", payload, true);
	if (_is_error_result(response)) {
		Dictionary stop_payload;
		stop_payload["jobId"] = job_id;
		stop_payload["mcpSessionId"] = p_mcp_session_id;
		debugger->send_message("mcp_performance:stop", Array{ String(), stop_payload });
		return response;
	}
	PerformanceJobRecord record;
	record.mcp_session_id = p_mcp_session_id;
	record.debugger_session = debugger_session;
	record.runtime_generation = generation;
	record.state = response.get("structuredContent", Dictionary());
	performance_jobs.insert(job_id, record);
	performance_job_order.push_back(job_id);
	return response;
}

Dictionary MCPRuntimeDebugService::get_performance_status(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant job_value = p_arguments.get("jobId", Variant());
	if (job_value.get_type() != Variant::STRING || String(job_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "jobId must be a non-empty string.");
	}
	const String job_id = job_value;
	PerformanceJobRecord *record = performance_jobs.getptr(job_id);
	if (!record || record->mcp_session_id != p_mcp_session_id) {
		return _error("PERFORMANCE_JOB_NOT_FOUND", "Runtime performance job was not found for this MCP session.");
	}
	int64_t requested_generation = 0;
	int64_t requested_debugger = record->debugger_session;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("runtimeGeneration", Variant()), 1, INT64_MAX, requested_generation) ||
			(p_arguments.has("debuggerSession") && !MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, requested_debugger)) ||
			uint64_t(requested_generation) != record->runtime_generation || int(requested_debugger) != record->debugger_session) {
		return _error("STALE_PERFORMANCE_JOB", "Runtime performance job belongs to a different runtime generation or debugger session.");
	}
	if (String(record->state.get("state", String())) != "running") {
		return MCPToolUtils::make_success_result(record->state);
	}
	Dictionary payload;
	payload["jobId"] = job_id;
	payload["mcpSessionId"] = p_mcp_session_id;
	const Dictionary response = _request_performance(p_arguments, "status", payload, true);
	if (!_is_error_result(response)) {
		record->state = response.get("structuredContent", Dictionary());
	}
	return response;
}

Dictionary MCPRuntimeDebugService::stop_performance(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Variant job_value = p_arguments.get("jobId", Variant());
	if (job_value.get_type() != Variant::STRING || String(job_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "jobId must be a non-empty string.");
	}
	const String job_id = job_value;
	PerformanceJobRecord *record = performance_jobs.getptr(job_id);
	if (!record || record->mcp_session_id != p_mcp_session_id) {
		return _error("PERFORMANCE_JOB_NOT_FOUND", "Runtime performance job was not found for this MCP session.");
	}
	int64_t requested_generation = 0;
	int64_t requested_debugger = record->debugger_session;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("runtimeGeneration", Variant()), 1, INT64_MAX, requested_generation) ||
			(p_arguments.has("debuggerSession") && !MCPToolUtils::try_get_json_integer(p_arguments["debuggerSession"], 0, INT32_MAX, requested_debugger)) ||
			uint64_t(requested_generation) != record->runtime_generation || int(requested_debugger) != record->debugger_session) {
		return _error("STALE_PERFORMANCE_JOB", "Runtime performance job belongs to a different runtime generation or debugger session.");
	}
	if (String(record->state.get("state", String())) != "running" && record->state.has("summary")) {
		return MCPToolUtils::make_success_result(record->state);
	}
	Dictionary payload;
	payload["jobId"] = job_id;
	payload["mcpSessionId"] = p_mcp_session_id;
	const Dictionary response = _request_performance(p_arguments, "stop", payload, true);
	if (!_is_error_result(response)) {
		record->state = response.get("structuredContent", Dictionary());
	}
	return response;
}

void MCPRuntimeDebugService::_release_performance_session(const String &p_mcp_session_id) {
	HashSet<int> debugger_sessions;
	for (const KeyValue<String, PerformanceJobRecord> &entry : performance_jobs) {
		if (entry.value.mcp_session_id == p_mcp_session_id && String(entry.value.state.get("state", String())) == "running") {
			debugger_sessions.insert(entry.value.debugger_session);
		}
	}
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	for (const int debugger_session : debugger_sessions) {
		ScriptEditorDebugger *debugger = debugger_node ? debugger_node->get_debugger(debugger_session) : nullptr;
		if (debugger && debugger->is_session_active()) {
			Dictionary payload;
			payload["mcpSessionId"] = p_mcp_session_id;
			debugger->send_message("mcp_performance:release_session", Array{ String(), payload });
		}
	}
	for (int i = performance_job_order.size() - 1; i >= 0; i--) {
		const PerformanceJobRecord *record = performance_jobs.getptr(performance_job_order[i]);
		if (record && record->mcp_session_id == p_mcp_session_id) {
			performance_jobs.erase(performance_job_order[i]);
			performance_job_order.remove_at(i);
		}
	}
}

void MCPRuntimeDebugService::_release_all_performance_jobs(const String &p_reason) {
	HashSet<int> debugger_sessions;
	for (KeyValue<String, PerformanceJobRecord> &entry : performance_jobs) {
		if (String(entry.value.state.get("state", String())) == "running") {
			debugger_sessions.insert(entry.value.debugger_session);
			entry.value.state["state"] = "cancelled";
			entry.value.state["failure"] = p_reason;
		}
	}
	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	for (const int debugger_session : debugger_sessions) {
		ScriptEditorDebugger *debugger = debugger_node ? debugger_node->get_debugger(debugger_session) : nullptr;
		if (debugger && debugger->is_session_active()) {
			debugger->send_message("mcp_performance:release_all", Array{ String(), Dictionary() });
		}
	}
}

Dictionary MCPRuntimeDebugService::_resolve_action_targets(const Dictionary &p_arguments, const String &p_kind, const Array &p_selectors) const {
	Dictionary payload;
	payload["kind"] = p_kind;
	payload["selectors"] = p_selectors;
	return _request_observation(p_arguments, "resolve_action_targets", payload, true);
}

Dictionary MCPRuntimeDebugService::_dispatch_action_events(const Dictionary &p_arguments, const String &p_mcp_session_id,
		const Array &p_events, const Dictionary &p_metadata) {
	if (p_events.is_empty() || p_events.size() > 128) {
		return _error("INVALID_TARGET_ACTION", "The target action generated an invalid number of input events.");
	}
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary session_error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!session_error.is_empty()) {
		return session_error;
	}
	int timeout_msec = DEFAULT_OBSERVATION_TIMEOUT_MSEC;
	if (!_observation_timeout_from_arguments(p_arguments, timeout_msec)) {
		return _error("INVALID_ARGUMENTS", "timeoutMs is outside the target action range.");
	}
	Vector<MCPRuntimeInput::EncodedEvent> encoded_events;
	for (int i = 0; i < p_events.size(); i++) {
		if (p_events[i].get_type() != Variant::DICTIONARY) {
			return _error("INVALID_TARGET_ACTION", vformat("Generated input event %d is invalid.", i));
		}
		MCPRuntimeInput::EncodedEvent encoded;
		String input_error;
		if (MCPRuntimeInput::encode(p_events[i], encoded, &input_error, true) != OK) {
			return _error("INVALID_TARGET_ACTION", input_error);
		}
		encoded_events.push_back(encoded);
	}
	int dispatched = 0;
	String dispatch_error;
	if (input_scheduler.dispatch_immediate(p_mcp_session_id, debugger, debugger_session, generation,
				encoded_events, dispatched, dispatch_error, timeout_msec) != OK) {
		return _error("RUNTIME_INPUT_FAILED", dispatch_error);
	}
	Dictionary result = _make_session_identity(debugger_session, generation);
	result.merge(p_metadata, true);
	result["dispatched"] = true;
	result["eventCount"] = dispatched;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::_start_action_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id,
		const Array &p_steps, const Dictionary &p_metadata) {
	Vector<MCPRuntimeInputSequence::Step> steps;
	String compile_error;
	if (MCPRuntimeInputSequence::compile(p_steps, steps, &compile_error) != OK) {
		return _error("INVALID_TARGET_ACTION", compile_error);
	}
	ScriptEditorDebugger *debugger = nullptr;
	int debugger_session = -1;
	uint64_t generation = 0;
	const Dictionary session_error = _resolve_session(p_arguments, true, debugger, debugger_session, generation);
	if (!session_error.is_empty()) {
		return session_error;
	}
	int timeout_msec = DEFAULT_OBSERVATION_TIMEOUT_MSEC;
	if (!_observation_timeout_from_arguments(p_arguments, timeout_msec)) {
		return _error("INVALID_ARGUMENTS", "timeoutMs is outside the target action range.");
	}
	Dictionary result;
	String scheduler_error;
	const Error error = input_scheduler.start_sequence(p_mcp_session_id, debugger_session, generation, steps, result,
			scheduler_error, timeout_msec);
	if (error != OK) {
		return _error(error == ERR_BUSY ? "INPUT_SEQUENCE_BUSY" : "INPUT_SEQUENCE_FAILED", scheduler_error);
	}
	result.merge(p_metadata, true);
	result["asynchronous"] = true;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeDebugService::click_target(const Dictionary &p_arguments, const String &p_mcp_session_id, bool p_double_click) {
	const Dictionary resolution = _resolve_action_targets(p_arguments, "pointer", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position) || !has_position) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid target coordinates.");
	}
	String button_name;
	String argument_error;
	int press_frames = 1;
	int gap_frames = 2;
	MouseButton button = MouseButton::LEFT;
	if (!_read_string_argument(p_arguments, "button", "left", button_name, argument_error) ||
			!MCPRuntimeTargetAction::parse_button(button_name, button) ||
			!_read_bounded_integer(p_arguments, "pressFrames", 1, 1, MCPRuntimeTargetAction::MAX_CLICK_FRAMES, press_frames, argument_error) ||
			(p_double_click && !_read_bounded_integer(p_arguments, "gapFrames", 2, 1, MCPRuntimeTargetAction::MAX_CLICK_FRAMES, gap_frames, argument_error))) {
		return _error("INVALID_ARGUMENTS", argument_error.is_empty() ? "button must be left, right, or middle." : argument_error);
	}
	Array steps;
	String action_error;
	if (MCPRuntimeTargetAction::build_click(position, button, press_frames, p_double_click, gap_frames, steps, &action_error) != OK) {
		return _error("INVALID_TARGET_ACTION", action_error);
	}
	Dictionary metadata;
	metadata["action"] = p_double_click ? "double_click_target" : "click_target";
	metadata["resolvedTarget"] = node;
	metadata["position"] = Array{ position.x, position.y };
	metadata["button"] = button_name;
	return _start_action_sequence(p_arguments, p_mcp_session_id, steps, metadata);
}

Dictionary MCPRuntimeDebugService::hover_target(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Dictionary resolution = _resolve_action_targets(p_arguments, "pointer", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position) || !has_position) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid target coordinates.");
	}
	Array events;
	MCPRuntimeTargetAction::build_hover(position, events);
	Dictionary metadata;
	metadata["action"] = "hover_target";
	metadata["resolvedTarget"] = node;
	metadata["position"] = Array{ position.x, position.y };
	return _dispatch_action_events(p_arguments, p_mcp_session_id, events, metadata);
}

Dictionary MCPRuntimeDebugService::focus_target(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Dictionary resolution = _resolve_action_targets(p_arguments, "focus", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position)) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid focus target data.");
	}
	Dictionary metadata;
	metadata["action"] = "focus_target";
	metadata["resolvedTarget"] = node;
	metadata["focused"] = bool(target.get("focused", false));
	if (bool(metadata["focused"])) {
		const Dictionary content = resolution.get("structuredContent", Dictionary());
		metadata["debuggerSession"] = content.get("debuggerSession", Variant());
		metadata["runtimeGeneration"] = content.get("runtimeGeneration", Variant());
		return MCPToolUtils::make_success_result(metadata);
	}
	if (!has_position) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The non-Control focus target has no pointer position.");
	}
	Array events;
	MCPRuntimeTargetAction::build_hover(position, events);
	metadata["position"] = Array{ position.x, position.y };
	return _dispatch_action_events(p_arguments, p_mcp_session_id, events, metadata);
}

Dictionary MCPRuntimeDebugService::drag_target_to_target(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Array selectors{ p_arguments.get("fromSelector", Dictionary()), p_arguments.get("toSelector", Dictionary()) };
	const Dictionary resolution = _resolve_action_targets(p_arguments, "pointer", selectors);
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary from_target;
	Dictionary from_node;
	Dictionary to_target;
	Dictionary to_node;
	Vector2 from;
	Vector2 to;
	bool has_from = false;
	bool has_to = false;
	if (!_read_resolved_target(resolution, 0, from_target, from_node, from, has_from) || !has_from ||
			!_read_resolved_target(resolution, 1, to_target, to_node, to, has_to) || !has_to) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid drag target coordinates.");
	}
	String button_name;
	String argument_error;
	int frames = 20;
	MouseButton button = MouseButton::LEFT;
	if (!_read_string_argument(p_arguments, "button", "left", button_name, argument_error) ||
			!MCPRuntimeTargetAction::parse_button(button_name, button) ||
			!_read_bounded_integer(p_arguments, "frames", 20, 1, MCPRuntimeTargetAction::MAX_DRAG_FRAMES, frames, argument_error)) {
		return _error("INVALID_ARGUMENTS", argument_error.is_empty() ? "button must be left, right, or middle." : argument_error);
	}
	Array steps;
	String action_error;
	if (MCPRuntimeTargetAction::build_drag(from, to, button, frames, steps, &action_error) != OK) {
		return _error("INVALID_TARGET_ACTION", action_error);
	}
	Dictionary metadata;
	metadata["action"] = "drag_target_to_target";
	metadata["resolvedFromTarget"] = from_node;
	metadata["resolvedToTarget"] = to_node;
	metadata["from"] = Array{ from.x, from.y };
	metadata["to"] = Array{ to.x, to.y };
	metadata["frames"] = frames;
	metadata["button"] = button_name;
	return _start_action_sequence(p_arguments, p_mcp_session_id, steps, metadata);
}

Dictionary MCPRuntimeDebugService::type_text(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Dictionary resolution = _resolve_action_targets(p_arguments, "focus_text", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position) || !bool(target.get("focused", false))) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project did not focus the text target.");
	}
	const Variant text_value = p_arguments.get("text", Variant());
	if (text_value.get_type() != Variant::STRING) {
		return _error("INVALID_ARGUMENTS", "text must be a string.");
	}
	Array steps;
	String action_error;
	if (MCPRuntimeTargetAction::build_text(text_value, steps, &action_error) != OK) {
		return _error("INVALID_ARGUMENTS", action_error);
	}
	Dictionary metadata;
	metadata["action"] = "type_text";
	metadata["resolvedTarget"] = node;
	metadata["textLength"] = String(text_value).length();
	return _start_action_sequence(p_arguments, p_mcp_session_id, steps, metadata);
}

Dictionary MCPRuntimeDebugService::scroll_view(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Dictionary resolution = _resolve_action_targets(p_arguments, "pointer", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position) || !has_position) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid scroll target coordinates.");
	}
	String direction;
	String argument_error;
	int steps = 1;
	if (!_read_string_argument(p_arguments, "direction", "down", direction, argument_error) ||
			!_read_bounded_integer(p_arguments, "steps", 1, 1, MCPRuntimeTargetAction::MAX_SCROLL_STEPS, steps, argument_error)) {
		return _error("INVALID_ARGUMENTS", argument_error);
	}
	Array events;
	String action_error;
	if (MCPRuntimeTargetAction::build_scroll(position, direction, steps, events, &action_error) != OK) {
		return _error("INVALID_ARGUMENTS", action_error);
	}
	Dictionary metadata;
	metadata["action"] = "scroll_view";
	metadata["resolvedTarget"] = node;
	metadata["position"] = Array{ position.x, position.y };
	metadata["direction"] = direction;
	metadata["steps"] = steps;
	return _dispatch_action_events(p_arguments, p_mcp_session_id, events, metadata);
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
	for (KeyValue<String, ConditionJobRecord> &entry : condition_jobs) {
		if (String(entry.value.state.get("state", String())) != "running") {
			continue;
		}
		if (!runtime_gateway.is_runtime_current(entry.value.debugger_session, entry.value.runtime_generation)) {
			entry.value.state["state"] = "cancelled";
			entry.value.state["failure"] = "The running project disconnected or restarted.";
		}
	}
	for (KeyValue<String, PerformanceJobRecord> &entry : performance_jobs) {
		if (String(entry.value.state.get("state", String())) != "running") {
			continue;
		}
		if (!runtime_gateway.is_runtime_current(entry.value.debugger_session, entry.value.runtime_generation)) {
			entry.value.state["state"] = "cancelled";
			entry.value.state["failure"] = "The running project disconnected or restarted.";
		}
	}
}

void MCPRuntimeDebugService::release_mcp_session(const String &p_mcp_session_id) {
	_release_condition_session(p_mcp_session_id);
	_release_performance_session(p_mcp_session_id);
	input_scheduler.release_mcp_session(p_mcp_session_id);
}

void MCPRuntimeDebugService::shutdown_input() {
	_release_all_condition_jobs("The MCP Host shut down.");
	_release_all_performance_jobs("The MCP Host shut down.");
	input_scheduler.release_all();
}
