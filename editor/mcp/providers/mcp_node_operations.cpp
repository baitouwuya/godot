/**************************************************************************/
/*  mcp_node_operations.cpp                                               */
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

#include "mcp_node_operations.h"

#include "mcp_scene_utils.h"
#include "mcp_undo_redo_action.h"

#include "core/object/class_db.h"
#include "core/variant/callable.h"
#include "core/object/script_language.h"
#include "scene/main/node.h"

namespace MCPNodeOperations {

static Error _fail(const String &p_message, String *r_error, Error p_error = ERR_INVALID_PARAMETER) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

bool find_property_info(Node *p_node, const StringName &p_property, PropertyInfo &r_info) {
	if (!p_node) {
		return false;
	}
	List<PropertyInfo> properties;
	p_node->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (property.name == p_property) {
			r_info = property;
			return true;
		}
	}
	return false;
}

Error create_node(Node *p_scene_root, Node *p_parent, const StringName &p_type, const String &p_name,
		MCPUndoRedoAction *p_undo_redo, Node *&r_node, String *r_error) {
	r_node = nullptr;
	if (r_error) {
		*r_error = String();
	}
	if (!MCPSceneUtils::is_node_editable(p_scene_root, p_parent)) {
		return _fail("Parent node is not editable in the current scene.", r_error, ERR_UNAUTHORIZED);
	}
	if (p_type.is_empty() || !ClassDB::class_exists(p_type) || !ClassDB::can_instantiate(p_type) ||
			!ClassDB::is_parent_class(p_type, "Node")) {
		return _fail("Class is not an instantiable Node type: " + String(p_type), r_error, ERR_INVALID_DATA);
	}

	Node *node = Object::cast_to<Node>(ClassDB::instantiate(p_type));
	if (!node) {
		return _fail("Node type could not be instantiated: " + String(p_type), r_error, ERR_CANT_CREATE);
	}
	if (!p_name.is_empty()) {
		node->set_name(p_name);
	}

	if (!p_undo_redo) {
		memdelete(node);
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	Error err = p_undo_redo->begin_action("MCP Create Node", r_error);
	if (err != OK) {
		memdelete(node);
		return err;
	}
	p_undo_redo->add_do_method(p_parent, "add_child", node, true);
	p_undo_redo->add_do_method(node, "set_owner", p_scene_root);
	p_undo_redo->add_do_reference(node);
	p_undo_redo->add_undo_method(p_parent, "remove_child", node);
	p_undo_redo->commit_action();

	r_node = node;
	return OK;
}

Error set_property(Node *p_scene_root, Node *p_node, const StringName &p_property, const Variant &p_value,
		MCPUndoRedoAction *p_undo_redo, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!MCPSceneUtils::is_node_editable(p_scene_root, p_node)) {
		return _fail("Node is not editable in the current scene.", r_error, ERR_UNAUTHORIZED);
	}
	if (p_property == CoreStringName(script)) {
		return _fail("Use godot.node.attach_script to change a node script.", r_error, ERR_INVALID_PARAMETER);
	}

	PropertyInfo property_info;
	if (!find_property_info(p_node, p_property, property_info) || !(property_info.usage & PROPERTY_USAGE_EDITOR)) {
		return _fail("Node property was not found: " + String(p_property), r_error, ERR_DOES_NOT_EXIST);
	}
	if (property_info.usage & PROPERTY_USAGE_READ_ONLY) {
		return _fail("Node property is read-only: " + String(p_property), r_error, ERR_UNAUTHORIZED);
	}
	if (property_info.type != Variant::NIL && p_value.get_type() != property_info.type &&
			!(p_value.get_type() == Variant::NIL && property_info.type == Variant::OBJECT) &&
			!Variant::can_convert_strict(p_value.get_type(), property_info.type)) {
		return _fail("Value type '" + Variant::get_type_name(p_value.get_type()) + "' is incompatible with property type '" +
						Variant::get_type_name(property_info.type) + "'.",
				r_error, ERR_INVALID_DATA);
	}

	bool valid = false;
	const Variant old_value = p_node->get(p_property, &valid);
	if (!valid) {
		return _fail("Node property could not be read: " + String(p_property), r_error, ERR_INVALID_DATA);
	}

	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	Error err = p_undo_redo->begin_action("MCP Set Node Property", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_do_property(p_node, p_property, p_value);
	p_undo_redo->add_undo_property(p_node, p_property, old_value);
	p_undo_redo->commit_action();
	return OK;
}

Error attach_script(Node *p_scene_root, Node *p_node, const Ref<Script> &p_script,
		MCPUndoRedoAction *p_undo_redo, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!MCPSceneUtils::is_node_editable(p_scene_root, p_node)) {
		return _fail("Node is not editable in the current scene.", r_error, ERR_UNAUTHORIZED);
	}
	if (p_script.is_null() || !p_script->is_valid() || p_script->is_abstract()) {
		return _fail("Script is invalid or abstract.", r_error, ERR_INVALID_DATA);
	}
	const StringName base_type = p_script->get_instance_base_type();
	if (base_type.is_empty() || !p_node->is_class(base_type)) {
		return _fail("Script base type is incompatible with node type '" + p_node->get_class() + "'.", r_error, ERR_INVALID_DATA);
	}

	const Ref<Script> old_script = p_node->get_script();
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	Error err = p_undo_redo->begin_action("MCP Attach Script", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_do_method(p_node, "set_script", p_script);
	p_undo_redo->add_undo_method(p_node, "set_script", old_script);
	p_undo_redo->commit_action();
	return OK;
}

Error add_to_group(Node *p_scene_root, Node *p_node, const StringName &p_group, bool p_persistent,
		MCPUndoRedoAction *p_undo_redo, String *r_error) {
	if (!MCPSceneUtils::is_node_editable(p_scene_root, p_node)) {
		return _fail("Node is not editable in the current scene.", r_error, ERR_UNAUTHORIZED);
	}
	if (p_group.is_empty()) {
		return _fail("A non-empty group name is required.", r_error);
	}
	if (p_node->is_in_group(p_group)) {
		return _fail("Node is already in group: " + String(p_group), r_error, ERR_ALREADY_EXISTS);
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	const Error err = p_undo_redo->begin_action("MCP Add Node To Group", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_do_method(p_node, "add_to_group", p_group, p_persistent);
	p_undo_redo->add_undo_method(p_node, "remove_from_group", p_group);
	p_undo_redo->commit_action();
	return OK;
}

Error remove_from_group(Node *p_scene_root, Node *p_node, const StringName &p_group,
		MCPUndoRedoAction *p_undo_redo, String *r_error) {
	if (!MCPSceneUtils::is_node_editable(p_scene_root, p_node)) {
		return _fail("Node is not editable in the current scene.", r_error, ERR_UNAUTHORIZED);
	}
	if (p_group.is_empty()) {
		return _fail("A non-empty group name is required.", r_error);
	}
	List<Node::GroupInfo> groups;
	p_node->get_groups(&groups);
	bool persistent = false;
	bool found = false;
	for (const Node::GroupInfo &group : groups) {
		if (group.name == p_group) {
			persistent = group.persistent;
			found = true;
			break;
		}
	}
	if (!found) {
		return _fail("Node is not in group: " + String(p_group), r_error, ERR_DOES_NOT_EXIST);
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	const Error err = p_undo_redo->begin_action("MCP Remove Node From Group", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_do_method(p_node, "remove_from_group", p_group);
	p_undo_redo->add_undo_method(p_node, "add_to_group", p_group, persistent);
	p_undo_redo->commit_action();
	return OK;
}

static Error _validate_signal_endpoints(Node *p_scene_root, Node *p_source, const StringName &p_signal, Node *p_target,
		const StringName &p_method, String *r_error) {
	if (!MCPSceneUtils::is_node_editable(p_scene_root, p_source) || !MCPSceneUtils::is_node_editable(p_scene_root, p_target)) {
		return _fail("Signal source and target must be editable nodes in the current scene.", r_error, ERR_UNAUTHORIZED);
	}
	if (p_signal.is_empty() || !p_source->has_signal(p_signal)) {
		return _fail("Signal was not found on source node: " + String(p_signal), r_error, ERR_DOES_NOT_EXIST);
	}
	if (p_method.is_empty() || !p_target->has_method(p_method)) {
		return _fail("Method was not found on target node: " + String(p_method), r_error, ERR_DOES_NOT_EXIST);
	}
	return OK;
}

Error connect_signal(Node *p_scene_root, Node *p_source, const StringName &p_signal, Node *p_target,
		const StringName &p_method, uint32_t p_flags, MCPUndoRedoAction *p_undo_redo, String *r_error) {
	const Error validation = _validate_signal_endpoints(p_scene_root, p_source, p_signal, p_target, p_method, r_error);
	if (validation != OK) {
		return validation;
	}
	const Callable callable(p_target, p_method);
	if (p_source->is_connected(p_signal, callable)) {
		return _fail("Signal is already connected to the target method.", r_error, ERR_ALREADY_EXISTS);
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	const Error err = p_undo_redo->begin_action("MCP Connect Signal", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_do_method(p_source, "connect", p_signal, callable, p_flags | Object::CONNECT_PERSIST);
	p_undo_redo->add_undo_method(p_source, "disconnect", p_signal, callable);
	p_undo_redo->commit_action();
	return OK;
}

Error disconnect_signal(Node *p_scene_root, Node *p_source, const StringName &p_signal, Node *p_target,
		const StringName &p_method, MCPUndoRedoAction *p_undo_redo, String *r_error) {
	const Error validation = _validate_signal_endpoints(p_scene_root, p_source, p_signal, p_target, p_method, r_error);
	if (validation != OK) {
		return validation;
	}
	const Callable callable(p_target, p_method);
	List<Object::Connection> connections;
	p_source->get_signal_connection_list(p_signal, &connections);
	uint32_t flags = 0;
	bool found = false;
	for (const Object::Connection &connection : connections) {
		if (connection.callable == callable) {
			flags = connection.flags;
			found = true;
			break;
		}
	}
	if (!found) {
		return _fail("Signal is not connected to the target method.", r_error, ERR_DOES_NOT_EXIST);
	}
	if (!(flags & Object::CONNECT_PERSIST)) {
		return _fail("Only persistent scene signal connections can be modified.", r_error, ERR_UNAUTHORIZED);
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	const Error err = p_undo_redo->begin_action("MCP Disconnect Signal", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_do_method(p_source, "disconnect", p_signal, callable);
	p_undo_redo->add_undo_method(p_source, "connect", p_signal, callable, flags);
	p_undo_redo->commit_action();
	return OK;
}

} // namespace MCPNodeOperations
