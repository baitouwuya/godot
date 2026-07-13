/**************************************************************************/
/*  mcp_node_structure_operations.cpp                                     */
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

#include "mcp_node_structure_operations.h"

#include "mcp_scene_utils.h"
#include "mcp_undo_redo_action.h"

#include "core/io/resource_loader.h"
#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/container.h"
#include "scene/gui/control.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

namespace MCPNodeStructureOperations {

static Error _fail(const String &p_message, String *r_error, Error p_error = ERR_INVALID_PARAMETER) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

static void _clear_error(String *r_error) {
	if (r_error) {
		*r_error = String();
	}
}

static Error _validate_node(Node *p_scene_root, Node *p_node, bool p_allow_root, String *r_error) {
	if (!MCPSceneUtils::is_node_editable(p_scene_root, p_node)) {
		return _fail("Node is not editable in the current scene.", r_error, ERR_UNAUTHORIZED);
	}
	if (!p_allow_root && p_node == p_scene_root) {
		return _fail("The edited scene root cannot be used for this operation.", r_error, ERR_UNAUTHORIZED);
	}
	if (p_node->is_internal()) {
		return _fail("Internal nodes cannot be structurally edited.", r_error, ERR_UNAUTHORIZED);
	}
	return OK;
}

static Error _validate_parent(Node *p_scene_root, Node *p_parent, String *r_error) {
	if (!MCPSceneUtils::is_node_editable(p_scene_root, p_parent)) {
		return _fail("Parent node is not editable in the current scene.", r_error, ERR_UNAUTHORIZED);
	}
	if (p_parent->is_internal()) {
		return _fail("Internal nodes cannot be used as structural parents.", r_error, ERR_UNAUTHORIZED);
	}
	return OK;
}

static Error _validate_name(const StringName &p_name, String *r_error) {
	const String name = p_name;
	if (name.is_empty() || name.strip_edges() != name || name.validate_node_name() != name || name == "." || name == "..") {
		return _fail("Node name is empty or contains invalid characters.", r_error, ERR_INVALID_DATA);
	}
	return OK;
}

static Node *_find_named_child(Node *p_parent, const StringName &p_name, Node *p_except = nullptr) {
	for (int i = 0; i < p_parent->get_child_count(true); i++) {
		Node *child = p_parent->get_child(i, true);
		if (child != p_except && child->get_name() == p_name) {
			return child;
		}
	}
	return nullptr;
}

static Error _validate_index(Node *p_parent, int p_index, bool p_inserting, String *r_error) {
	const int maximum = p_parent->get_child_count(false) - (p_inserting ? 0 : 1);
	if (p_index < -1 || p_index > maximum) {
		return _fail(vformat("Child index must be between -1 and %d.", maximum), r_error, ERR_INVALID_PARAMETER);
	}
	return OK;
}

static bool _inherits_scene_path(Node *p_node, const String &p_scene_path) {
	Node *candidate = p_node;
	Vector<Node *> inherited_instances;
	while (candidate) {
		if (candidate->get_scene_file_path() == p_scene_path) {
			for (Node *instance : inherited_instances) {
				memdelete(instance);
			}
			return true;
		}
		const Ref<SceneState> inherited_state = candidate->get_scene_inherited_state();
		if (inherited_state.is_null()) {
			break;
		}
		const Ref<PackedScene> inherited_scene = ResourceLoader::load(inherited_state->get_path(), "PackedScene");
		if (inherited_scene.is_null()) {
			break;
		}
		candidate = inherited_scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
		if (!candidate) {
			break;
		}
		inherited_instances.push_back(candidate);
	}
	for (Node *instance : inherited_instances) {
		memdelete(instance);
	}
	return false;
}

static bool _contains_scene_path(Node *p_node, const String &p_scene_path) {
	if (_inherits_scene_path(p_node, p_scene_path)) {
		return true;
	}
	for (int i = 0; i < p_node->get_child_count(true); i++) {
		if (_contains_scene_path(p_node->get_child(i, true), p_scene_path)) {
			return true;
		}
	}
	return false;
}

Error delete_node(Node *p_scene_root, Node *p_node, MCPUndoRedoAction *p_undo_redo, String *r_error) {
	_clear_error(r_error);
	Error err = _validate_node(p_scene_root, p_node, false, r_error);
	if (err != OK) {
		return err;
	}
	if (!p_node->get_parent()) {
		return _fail("Node does not have a parent.", r_error, ERR_INVALID_DATA);
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	if (!p_undo_redo->can_update_node_paths()) {
		return _fail("The scene tree path update service is not available.", r_error, ERR_UNCONFIGURED);
	}

	Node *parent = p_node->get_parent();
	const int old_index = p_node->get_index(false);
	List<Node *> owned_nodes;
	p_node->get_owned_by(p_scene_root, &owned_nodes);
	err = p_undo_redo->begin_action("MCP Delete Node", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_do_method(parent, "remove_child", p_node);
	p_undo_redo->add_undo_method(parent, "add_child", p_node, true);
	p_undo_redo->add_undo_method(parent, "move_child", p_node, old_index);
	for (Node *owned_node : owned_nodes) {
		p_undo_redo->add_undo_method(owned_node, "set_owner", p_scene_root);
	}
	p_undo_redo->add_undo_reference(p_node);
	p_undo_redo->add_node_path_updates(p_node, nullptr);
	p_undo_redo->commit_action();
	return OK;
}

Error rename_node(Node *p_scene_root, Node *p_node, const StringName &p_name,
		MCPUndoRedoAction *p_undo_redo, String *r_error) {
	_clear_error(r_error);
	Error err = _validate_node(p_scene_root, p_node, true, r_error);
	if (err != OK) {
		return err;
	}
	err = _validate_name(p_name, r_error);
	if (err != OK) {
		return err;
	}
	if (p_node->get_name() == p_name) {
		return OK;
	}
	Node *parent = p_node->get_parent();
	if (parent && _find_named_child(parent, p_name, p_node)) {
		return _fail("A sibling node already uses the requested name.", r_error, ERR_ALREADY_EXISTS);
	}
	if (p_node->is_unique_name_in_owner()) {
		Node *unique_node = p_scene_root->get_node_or_null(NodePath("%" + String(p_name)));
		if (unique_node && unique_node != p_node) {
			return _fail("The edited scene already has a node with this unique name.", r_error, ERR_ALREADY_EXISTS);
		}
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	if (!p_undo_redo->can_update_node_paths()) {
		return _fail("The scene tree path update service is not available.", r_error, ERR_UNCONFIGURED);
	}

	const bool was_unique = p_node->is_unique_name_in_owner();
	err = p_undo_redo->begin_action("MCP Rename Node", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_node_path_updates(p_node, parent, p_name);
	p_undo_redo->add_undo_method(p_node, "set_name", p_node->get_name());
	p_undo_redo->add_undo_method(p_node, "set_unique_name_in_owner", was_unique);
	p_undo_redo->add_do_method(p_node, "set_name", p_name);
	p_undo_redo->add_do_method(p_node, "set_unique_name_in_owner", was_unique);
	p_undo_redo->commit_action();
	return OK;
}

Error move_node(Node *p_scene_root, Node *p_node, int p_index,
		MCPUndoRedoAction *p_undo_redo, String *r_error) {
	_clear_error(r_error);
	Error err = _validate_node(p_scene_root, p_node, false, r_error);
	if (err != OK) {
		return err;
	}
	Node *parent = p_node->get_parent();
	if (!parent) {
		return _fail("Node does not have a parent.", r_error, ERR_INVALID_DATA);
	}
	err = _validate_index(parent, p_index, false, r_error);
	if (err != OK) {
		return err;
	}
	const int resolved_index = p_index < 0 ? parent->get_child_count(false) - 1 : p_index;
	const int old_index = p_node->get_index(false);
	if (resolved_index == old_index) {
		return OK;
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}

	err = p_undo_redo->begin_action("MCP Move Node", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_do_method(parent, "move_child", p_node, resolved_index);
	p_undo_redo->add_undo_method(parent, "move_child", p_node, old_index);
	p_undo_redo->commit_action();
	return OK;
}

Error reparent_node(Node *p_scene_root, Node *p_node, Node *p_parent, int p_index, bool p_keep_global_transform,
		MCPUndoRedoAction *p_undo_redo, String *r_error) {
	_clear_error(r_error);
	Error err = _validate_node(p_scene_root, p_node, false, r_error);
	if (err != OK) {
		return err;
	}
	err = _validate_parent(p_scene_root, p_parent, r_error);
	if (err != OK) {
		return err;
	}
	if (p_parent == p_node || p_node->is_ancestor_of(p_parent)) {
		return _fail("A node cannot be reparented to itself or one of its descendants.", r_error, ERR_INVALID_PARAMETER);
	}
	if (p_parent == p_node->get_parent()) {
		return move_node(p_scene_root, p_node, p_index, p_undo_redo, r_error);
	}
	if (_find_named_child(p_parent, p_node->get_name())) {
		return _fail("The destination parent already has a child with this name.", r_error, ERR_ALREADY_EXISTS);
	}
	err = _validate_index(p_parent, p_index, true, r_error);
	if (err != OK) {
		return err;
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}
	if (!p_undo_redo->can_update_node_paths()) {
		return _fail("The scene tree path update service is not available.", r_error, ERR_UNCONFIGURED);
	}

	Node *old_parent = p_node->get_parent();
	const int old_index = p_node->get_index(false);
	Node2D *node_2d = Object::cast_to<Node2D>(p_node);
	Node3D *node_3d = Object::cast_to<Node3D>(p_node);
	Control *control = Object::cast_to<Control>(p_node);
	const bool reparented_to_container = control && Object::cast_to<Container>(p_parent);
	const Dictionary old_control_state = reparented_to_container ? control->_edit_get_state() : Dictionary();
	const Transform2D old_transform_2d = node_2d ? node_2d->get_transform() : Transform2D();
	const Transform2D global_transform_2d = node_2d ? node_2d->get_global_transform() : Transform2D();
	const Transform3D old_transform_3d = node_3d ? node_3d->get_transform() : Transform3D();
	const Transform3D global_transform_3d = node_3d ? node_3d->get_global_transform() : Transform3D();
	const Vector2 old_control_position = control ? control->get_position() : Vector2();
	const Vector2 global_control_position = control ? control->get_global_position() : Vector2();

	err = p_undo_redo->begin_action("MCP Reparent Node", r_error);
	if (err != OK) {
		return err;
	}
	p_undo_redo->add_do_method(p_node, "reparent", p_parent, false);
	if (p_index >= 0) {
		p_undo_redo->add_do_method(p_parent, "move_child", p_node, p_index);
	}
	if (p_keep_global_transform) {
		if (node_2d) {
			p_undo_redo->add_do_method(p_node, "set_global_transform", global_transform_2d);
		} else if (node_3d) {
			p_undo_redo->add_do_method(p_node, "set_global_transform", global_transform_3d);
		} else if (control) {
			p_undo_redo->add_do_method(p_node, "set_global_position", global_control_position);
		}
	}
	p_undo_redo->add_undo_method(p_node, "reparent", old_parent, false);
	p_undo_redo->add_undo_method(old_parent, "move_child", p_node, old_index);
	if (p_keep_global_transform) {
		if (node_2d) {
			p_undo_redo->add_undo_method(p_node, "set_transform", old_transform_2d);
		} else if (node_3d) {
			p_undo_redo->add_undo_method(p_node, "set_transform", old_transform_3d);
		} else if (control && !reparented_to_container) {
			p_undo_redo->add_undo_method(p_node, "set_position", old_control_position);
		}
	}
	if (reparented_to_container) {
		p_undo_redo->add_undo_method(p_node, "_edit_set_state", old_control_state);
	}
	p_undo_redo->add_node_path_updates(p_node, p_parent);
	p_undo_redo->commit_action();
	return OK;
}

Error duplicate_node(Node *p_scene_root, Node *p_node, Node *p_parent, const StringName &p_name, int p_index,
		MCPUndoRedoAction *p_undo_redo, Node *&r_duplicate, String *r_error) {
	r_duplicate = nullptr;
	_clear_error(r_error);
	Error err = _validate_node(p_scene_root, p_node, false, r_error);
	if (err != OK) {
		return err;
	}
	err = _validate_parent(p_scene_root, p_parent, r_error);
	if (err != OK) {
		return err;
	}
	if (!p_name.is_empty() && _validate_name(p_name, r_error) != OK) {
		return ERR_INVALID_DATA;
	}
	if (!p_name.is_empty() && _find_named_child(p_parent, p_name)) {
		return _fail("The destination parent already has a child with this name.", r_error, ERR_ALREADY_EXISTS);
	}
	err = _validate_index(p_parent, p_index, true, r_error);
	if (err != OK) {
		return err;
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}

	List<Node *> owned_nodes;
	Node *owner = p_node;
	while (owner) {
		List<Node *> owned_by_current;
		p_node->get_owned_by(owner, &owned_by_current);
		for (Node *owned_node : owned_by_current) {
			owned_nodes.push_back(owned_node);
		}
		owner = owner->get_owner();
	}

	HashMap<const Node *, Node *> duplicate_map;
	HashMap<Node *, HashMap<Ref<Resource>, Ref<Resource>>> resource_remap;
	Node *duplicate = p_node->duplicate_from_editor(duplicate_map, p_scene_root, resource_remap);
	if (!duplicate) {
		return _fail("Node could not be duplicated.", r_error, ERR_CANT_CREATE);
	}
	if (!p_name.is_empty()) {
		duplicate->set_name(p_name);
	}

	err = p_undo_redo->begin_action("MCP Duplicate Node", r_error);
	if (err != OK) {
		memdelete(duplicate);
		return err;
	}
	p_undo_redo->add_do_method(p_parent, "add_child", duplicate, true);
	if (p_index >= 0) {
		p_undo_redo->add_do_method(p_parent, "move_child", duplicate, p_index);
	}
	for (Node *owned_node : owned_nodes) {
		Node **duplicate_node = duplicate_map.getptr(owned_node);
		if (duplicate_node) {
			p_undo_redo->add_do_method(*duplicate_node, "set_owner", p_scene_root);
		}
	}
	p_undo_redo->add_do_method(duplicate, "set_owner", p_scene_root);
	p_undo_redo->add_undo_method(p_parent, "remove_child", duplicate);
	p_undo_redo->add_do_reference(duplicate);
	p_undo_redo->commit_action();

	for (const KeyValue<Node *, HashMap<Ref<Resource>, Ref<Resource>>> &node_remap : resource_remap) {
		for (const KeyValue<Ref<Resource>, Ref<Resource>> &resource : node_remap.value) {
			if (resource.value->is_local_to_scene()) {
				resource.value->setup_local_to_scene();
			}
		}
	}
	r_duplicate = duplicate;
	return OK;
}

Error instantiate_scene(Node *p_scene_root, Node *p_parent, const Ref<PackedScene> &p_scene, const String &p_scene_path,
		const StringName &p_name, int p_index, MCPUndoRedoAction *p_undo_redo, Node *&r_instance, String *r_error) {
	r_instance = nullptr;
	_clear_error(r_error);
	Error err = _validate_parent(p_scene_root, p_parent, r_error);
	if (err != OK) {
		return err;
	}
	if (p_scene.is_null()) {
		return _fail("Packed scene is invalid.", r_error, ERR_INVALID_DATA);
	}
	if (!p_name.is_empty() && _validate_name(p_name, r_error) != OK) {
		return ERR_INVALID_DATA;
	}
	if (!p_name.is_empty() && _find_named_child(p_parent, p_name)) {
		return _fail("The destination parent already has a child with this name.", r_error, ERR_ALREADY_EXISTS);
	}
	err = _validate_index(p_parent, p_index, true, r_error);
	if (err != OK) {
		return err;
	}
	if (!p_undo_redo) {
		return _fail("The editor undo manager is not available.", r_error, ERR_UNCONFIGURED);
	}

	Node *instance = p_scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (!instance) {
		return _fail("Packed scene could not be instantiated.", r_error, ERR_CANT_CREATE);
	}
	instance->set_scene_file_path(p_scene_path);
	if (!p_name.is_empty()) {
		instance->set_name(p_name);
	}
	const String edited_scene_path = p_scene_root->get_scene_file_path();
	if (!edited_scene_path.is_empty() && _contains_scene_path(instance, edited_scene_path)) {
		memdelete(instance);
		return _fail("Instantiating this scene would create a cyclic scene dependency.", r_error, ERR_CYCLIC_LINK);
	}

	err = p_undo_redo->begin_action("MCP Instantiate Scene", r_error);
	if (err != OK) {
		memdelete(instance);
		return err;
	}
	p_undo_redo->add_do_method(p_parent, "add_child", instance, true);
	if (p_index >= 0) {
		p_undo_redo->add_do_method(p_parent, "move_child", instance, p_index);
	}
	p_undo_redo->add_do_method(instance, "set_owner", p_scene_root);
	p_undo_redo->add_undo_method(p_parent, "remove_child", instance);
	p_undo_redo->add_do_reference(instance);
	p_undo_redo->commit_action();
	r_instance = instance;
	return OK;
}

} // namespace MCPNodeStructureOperations
