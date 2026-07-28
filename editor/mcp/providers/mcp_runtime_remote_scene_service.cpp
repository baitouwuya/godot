/**************************************************************************/
/*  mcp_runtime_remote_scene_service.cpp                                 */
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

#include "mcp_runtime_remote_scene_service.h"

#include "mcp_runtime_debugger_wait.h"
#include "mcp_tool_utils.h"
#include "mcp_variant_codec.h"

#include "editor/debugger/editor_debugger_inspector.h"
#include "editor/debugger/script_editor_debugger.h"
#include "scene/debugger/scene_debugger_object.h"

namespace {

constexpr int DEFAULT_TIMEOUT_MSEC = 750;

Dictionary _error(const String &p_code, const String &p_message) {
	return MCPToolUtils::make_error_result(p_code, p_message);
}

Dictionary _wait_error(MCPRuntimeDebuggerWait::WaitStatus p_status, const String &p_subject) {
	switch (p_status) {
		case MCPRuntimeDebuggerWait::WAIT_DISCONNECTED:
			return _error("RUNTIME_NOT_RUNNING", "The running project stopped while refreshing " + p_subject + ".");
		case MCPRuntimeDebuggerWait::WAIT_STALE:
			return _error("STALE_RUNTIME", "The running project restarted while refreshing " + p_subject + ".");
		case MCPRuntimeDebuggerWait::WAIT_TIMEOUT:
			return _error("RUNTIME_TIMEOUT", "Timed out waiting for " + p_subject + ".");
		case MCPRuntimeDebuggerWait::WAIT_COMPLETED:
			return Dictionary();
	}
	return _error("RUNTIME_DEBUGGER_REQUEST_FAILED", "The runtime debugger returned an unknown wait status.");
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

bool _find_tree_node(const SceneDebuggerTree *p_tree, const String &p_requested_path, ObjectID &r_object_id,
		Dictionary &r_node) {
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

MCPRuntimeRemoteSceneService::MCPRuntimeRemoteSceneService(MCPRuntimeDebuggerGateway *p_runtime_gateway) {
	runtime_gateway = p_runtime_gateway;
}

Dictionary MCPRuntimeRemoteSceneService::_refresh_tree(const MCPRuntimeDebuggerGateway::Session &p_session,
		uint64_t p_deadline_usec) const {
	const uint64_t previous_revision = p_session.debugger->get_remote_tree_revision();
	p_session.debugger->request_remote_tree();
	const MCPRuntimeDebuggerWait::WaitStatus status = MCPRuntimeDebuggerWait::wait_until(
			p_session.debugger,
			p_deadline_usec,
			[&]() { return p_session.debugger->get_remote_tree_revision() > previous_revision; },
			[&]() { return !runtime_gateway->is_runtime_current(p_session.debugger_session, p_session.runtime_generation); });
	return _wait_error(status, "the running project's scene tree");
}

Dictionary MCPRuntimeRemoteSceneService::_find_node(ScriptEditorDebugger *p_debugger, const String &p_path,
		ObjectID &r_object_id, Dictionary &r_node) const {
	const SceneDebuggerTree *tree = p_debugger->get_remote_tree();
	if (!tree || !_find_tree_node(tree, p_path, r_object_id, r_node)) {
		return _error("RUNTIME_NODE_NOT_FOUND", "Runtime node was not found: " + p_path);
	}
	return Dictionary();
}

Dictionary MCPRuntimeRemoteSceneService::_refresh_object(const MCPRuntimeDebuggerGateway::Session &p_session,
		ObjectID p_object_id, uint64_t p_deadline_usec) const {
	const uint64_t previous_revision = p_session.debugger->get_remote_object_revision(p_object_id);
	TypedArray<uint64_t> ids;
	ids.push_back(uint64_t(p_object_id));
	p_session.debugger->request_remote_objects(ids, false);
	const MCPRuntimeDebuggerWait::WaitStatus status = MCPRuntimeDebuggerWait::wait_until(
			p_session.debugger,
			p_deadline_usec,
			[&]() { return p_session.debugger->get_remote_object_revision(p_object_id) > previous_revision; },
			[&]() { return !runtime_gateway->is_runtime_current(p_session.debugger_session, p_session.runtime_generation); });
	return _wait_error(status, "the running project's node properties");
}

Dictionary MCPRuntimeRemoteSceneService::_validate_current(const MCPRuntimeDebuggerGateway::Session &p_session) const {
	if (!p_session.debugger || !p_session.debugger->is_session_active()) {
		return _error("RUNTIME_NOT_RUNNING", "The running project stopped while resolving the runtime scene.");
	}
	if (!runtime_gateway->is_runtime_current(p_session.debugger_session, p_session.runtime_generation)) {
		return _error("STALE_RUNTIME", "The running project restarted while resolving the runtime scene.");
	}
	return Dictionary();
}

Dictionary MCPRuntimeRemoteSceneService::get_tree(const Dictionary &p_arguments) const {
	MCPRuntimeDebuggerGateway::Session session;
	Dictionary error = runtime_gateway->resolve_session(p_arguments, false, session);
	if (!error.is_empty()) {
		return error;
	}
	const uint64_t deadline = MCPRuntimeDebuggerWait::deadline_from_timeout_msec(_timeout_from_arguments(p_arguments));
	error = _refresh_tree(session, deadline);
	if (!error.is_empty()) {
		return error;
	}
	error = _validate_current(session);
	if (!error.is_empty()) {
		return error;
	}
	Dictionary result = runtime_gateway->make_session_identity(session.debugger_session, session.runtime_generation);
	result["treeRevision"] = int64_t(session.debugger->get_remote_tree_revision());
	result["nodes"] = _make_tree_nodes(session.debugger->get_remote_tree());
	result["nodeCount"] = Array(result["nodes"]).size();
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeRemoteSceneService::get_properties(const Dictionary &p_arguments) const {
	const Variant path_value = p_arguments.get("path", Variant());
	if (path_value.get_type() != Variant::STRING || String(path_value).is_empty()) {
		return _error("INVALID_ARGUMENTS", "path must be a non-empty string.");
	}
	MCPRuntimeDebuggerGateway::Session session;
	Dictionary error = runtime_gateway->resolve_session(p_arguments, false, session);
	if (!error.is_empty()) {
		return error;
	}
	const uint64_t deadline = MCPRuntimeDebuggerWait::deadline_from_timeout_msec(_timeout_from_arguments(p_arguments));
	error = _refresh_tree(session, deadline);
	if (!error.is_empty()) {
		return error;
	}
	ObjectID object_id;
	Dictionary node;
	error = _find_node(session.debugger, path_value, object_id, node);
	if (!error.is_empty()) {
		return error;
	}
	error = _refresh_object(session, object_id, deadline);
	if (!error.is_empty()) {
		return error;
	}
	error = _validate_current(session);
	if (!error.is_empty()) {
		return error;
	}
	EditorDebuggerRemoteObjects *remote_object = session.debugger->get_cached_remote_object(object_id);
	if (!remote_object) {
		return _error("RUNTIME_DATA_UNAVAILABLE", "The running project did not return inspectable node properties.");
	}
	Dictionary result = runtime_gateway->make_session_identity(session.debugger_session, session.runtime_generation);
	result.merge(node, true);
	result.merge(_encode_remote_properties(remote_object, object_id), true);
	result["objectRevision"] = int64_t(session.debugger->get_remote_object_revision(object_id));
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPRuntimeRemoteSceneService::set_property(const Dictionary &p_arguments) const {
	const Variant path_value = p_arguments.get("path", Variant());
	const Variant property_value = p_arguments.get("property", Variant());
	if (path_value.get_type() != Variant::STRING || String(path_value).is_empty() ||
			property_value.get_type() != Variant::STRING || String(property_value).is_empty() || !p_arguments.has("value")) {
		return _error("INVALID_ARGUMENTS", "path, property, and value are required.");
	}
	MCPRuntimeDebuggerGateway::Session session;
	Dictionary error = runtime_gateway->resolve_session(p_arguments, true, session);
	if (!error.is_empty()) {
		return error;
	}
	const uint64_t deadline = MCPRuntimeDebuggerWait::deadline_from_timeout_msec(_timeout_from_arguments(p_arguments));
	error = _refresh_tree(session, deadline);
	if (!error.is_empty()) {
		return error;
	}
	ObjectID object_id;
	Dictionary node;
	error = _find_node(session.debugger, path_value, object_id, node);
	if (!error.is_empty()) {
		return error;
	}
	error = _refresh_object(session, object_id, deadline);
	if (!error.is_empty()) {
		return error;
	}
	error = _validate_current(session);
	if (!error.is_empty()) {
		return error;
	}
	EditorDebuggerRemoteObjects *remote_object = session.debugger->get_cached_remote_object(object_id);
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
	if (MCPRuntimeDebuggerWait::is_expired(deadline)) {
		return _error("RUNTIME_TIMEOUT", "Timed out before the runtime property could be updated.");
	}
	session.debugger->update_remote_object(object_id, property_value, decoded_value);
	error = _refresh_object(session, object_id, deadline);
	if (!error.is_empty()) {
		return error;
	}
	error = _validate_current(session);
	if (!error.is_empty()) {
		return error;
	}
	remote_object = session.debugger->get_cached_remote_object(object_id);
	const Variant effective_value = remote_object ? remote_object->get_variant(property_value) : Variant();
	if (remote_object && effective_value != decoded_value && effective_value == previous_value) {
		Dictionary details;
		details["path"] = node["path"];
		details["property"] = property_value;
		return MCPToolUtils::make_error_result("RUNTIME_PROPERTY_REJECTED", "The runtime object rejected the property value.", details);
	}
	Dictionary result = runtime_gateway->make_session_identity(session.debugger_session, session.runtime_generation);
	result["path"] = node["path"];
	result["objectId"] = String::num_uint64(uint64_t(object_id));
	result["property"] = property_value;
	result["objectRevision"] = int64_t(session.debugger->get_remote_object_revision(object_id));
	Variant encoded_value;
	result["coerced"] = remote_object && effective_value != decoded_value;
	if (remote_object && MCPVariantCodec::encode(effective_value, encoded_value, &codec_error) == OK) {
		result["value"] = encoded_value;
	} else {
		result["valueAvailable"] = false;
	}
	return MCPToolUtils::make_success_result(result);
}
