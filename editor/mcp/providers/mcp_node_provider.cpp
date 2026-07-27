/**************************************************************************/
/*  mcp_node_provider.cpp                                                 */
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

#include "mcp_node_provider.h"

#include "mcp_node_operations.h"
#include "mcp_path_utils.h"
#include "mcp_scene_utils.h"
#include "mcp_tool_utils.h"
#include "mcp_undo_redo_action.h"
#include "mcp_variant_codec.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/main/node.h"

namespace {

static Dictionary _required_string_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("string", p_description);
	schema["minLength"] = 1;
	return schema;
}

static Dictionary _non_negative_integer_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("integer", p_description);
	schema["minimum"] = 0;
	return schema;
}

static Dictionary _path_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root. Use '.' for the root.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path" });
}

static Dictionary _create_schema() {
	Dictionary properties;
	properties["type"] = _required_string_schema("Instantiable Godot Node class name.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Optional node name.");
	properties["parentPath"] = MCPToolUtils::make_property_schema("string", "Optional parent path. Defaults to the edited scene root.");
	PackedStringArray required;
	required.push_back("type");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _set_property_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root.");
	properties["property"] = _required_string_schema("Node property name.");
	Dictionary value;
	value["description"] = "JSON-native Variant value returned by godot.node.get_properties.";
	properties["value"] = value;
	PackedStringArray required;
	required.push_back("path");
	required.push_back("property");
	required.push_back("value");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _attach_script_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root.");
	properties["scriptPath"] = _required_string_schema("Project script path beginning with res://.");
	PackedStringArray required;
	required.push_back("path");
	required.push_back("scriptPath");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _group_schema(bool p_add) {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root.");
	properties["group"] = _required_string_schema("Non-empty scene group name.");
	if (p_add) {
		properties["persistent"] = MCPToolUtils::make_property_schema("boolean", "Whether the group is saved with the scene. Defaults to true.");
	}
	PackedStringArray required;
	required.push_back("path");
	required.push_back("group");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _signal_connections_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Source node path relative to the edited scene root.");
	properties["signal"] = _required_string_schema("Optional source signal name to filter connections.");
	PackedStringArray required;
	required.push_back("path");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _signal_mutation_schema(bool p_connect) {
	Dictionary properties;
	properties["path"] = _required_string_schema("Source node path relative to the edited scene root.");
	properties["signal"] = _required_string_schema("Signal declared by the source node.");
	properties["targetPath"] = _required_string_schema("Target node path relative to the edited scene root.");
	properties["method"] = _required_string_schema("Target method name.");
	if (p_connect) {
		Dictionary flags = MCPToolUtils::make_property_schema("integer", "Optional Object connect flags. Only deferred (1) and one-shot (4) are accepted; persistence is automatic.");
		flags["minimum"] = 0;
		flags["maximum"] = Object::CONNECT_DEFERRED | Object::CONNECT_ONE_SHOT;
		flags["default"] = 0;
		properties["flags"] = flags;
	}
	PackedStringArray required;
	required.push_back("path");
	required.push_back("signal");
	required.push_back("targetPath");
	required.push_back("method");
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _node_summary_properties() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Node path relative to the edited scene root.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Node name.");
	properties["type"] = MCPToolUtils::make_property_schema("string", "Node class name.");
	properties["childCount"] = _non_negative_integer_schema("Number of child nodes.");
	properties["editable"] = MCPToolUtils::make_property_schema("boolean", "Whether the node can be edited.");
	properties["internal"] = MCPToolUtils::make_property_schema("boolean", "Whether the node is internal.");
	properties["owner"] = MCPToolUtils::make_property_schema("string", "Owning scene node path.");
	properties["sceneFilePath"] = MCPToolUtils::make_property_schema("string", "Packed scene path when the node is an instance.");
	return properties;
}

static Dictionary _node_summary_schema() {
	return MCPToolUtils::make_object_schema(_node_summary_properties(), PackedStringArray{ "path", "name", "type", "childCount", "editable", "internal", "owner" }, true);
}

static Dictionary _node_properties_output_schema() {
	Dictionary array = MCPToolUtils::make_property_schema("array", "Inspectable node properties.");
	array["items"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	Dictionary layout = MCPToolUtils::make_property_schema("array", "Inspector property and section layout.");
	layout["items"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	Dictionary properties = _node_summary_properties();
	properties["scriptPath"] = MCPToolUtils::make_property_schema("string", "Attached script path.");
	properties["propertyCount"] = _non_negative_integer_schema("Inspectable property count.");
	properties["scriptPropertyCount"] = _non_negative_integer_schema("Script-exposed property count.");
	properties["properties"] = array;
	properties["propertyLayout"] = layout;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "name", "type", "childCount", "editable", "internal", "owner", "propertyCount", "scriptPropertyCount", "properties", "propertyLayout" }, true);
}

static Dictionary _node_property_output_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Changed node path.");
	properties["property"] = MCPToolUtils::make_property_schema("string", "Changed node property name.");
	properties["value"] = Dictionary();
	properties["valueEncodingError"] = MCPToolUtils::make_property_schema("string", "Value encoding failure.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "property", "value" });
}

static Dictionary _attach_script_output_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Node path.");
	properties["scriptPath"] = MCPToolUtils::make_property_schema("string", "Attached project script path.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "scriptPath" });
}

static Dictionary _groups_output_schema() {
	Dictionary group_properties;
	group_properties["name"] = MCPToolUtils::make_property_schema("string", "Scene group name.");
	group_properties["persistent"] = MCPToolUtils::make_property_schema("boolean", "Whether the group is persistent.");
	Dictionary group = MCPToolUtils::make_object_schema(group_properties, PackedStringArray{ "name", "persistent" });
	Dictionary groups = MCPToolUtils::make_property_schema("array", "Node scene groups.");
	groups["items"] = group;
	Dictionary properties = _node_summary_properties();
	properties["groups"] = groups;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "name", "type", "childCount", "editable", "internal", "owner", "groups" }, true);
}

static Dictionary _group_mutation_output_schema(bool p_include_persistent) {
	Dictionary properties = _node_summary_properties();
	properties["group"] = MCPToolUtils::make_property_schema("string", "Affected scene group.");
	PackedStringArray required{ "path", "name", "type", "childCount", "editable", "internal", "owner", "group" };
	if (p_include_persistent) {
		properties["persistent"] = MCPToolUtils::make_property_schema("boolean", "Whether the group is persistent.");
		required.push_back("persistent");
	}
	return MCPToolUtils::make_object_schema(properties, required, true);
}

static Dictionary _connections_output_schema() {
	Dictionary connection_properties;
	connection_properties["signal"] = MCPToolUtils::make_property_schema("string", "Signal name.");
	connection_properties["targetPath"] = MCPToolUtils::make_property_schema("string", "Target node path.");
	connection_properties["method"] = MCPToolUtils::make_property_schema("string", "Target method name.");
	connection_properties["flags"] = _non_negative_integer_schema("Signal connection flags.");
	Dictionary connection = MCPToolUtils::make_object_schema(connection_properties, PackedStringArray{ "signal", "targetPath", "method", "flags" });
	Dictionary connections = MCPToolUtils::make_property_schema("array", "Persistent signal connections.");
	connections["items"] = connection;
	Dictionary properties = _node_summary_properties();
	properties["connections"] = connections;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "name", "type", "childCount", "editable", "internal", "owner", "connections" }, true);
}

static Dictionary _signal_mutation_output_schema(bool p_include_flags) {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Source node path.");
	properties["signal"] = MCPToolUtils::make_property_schema("string", "Signal name.");
	properties["targetPath"] = MCPToolUtils::make_property_schema("string", "Target node path.");
	properties["method"] = MCPToolUtils::make_property_schema("string", "Target method name.");
	if (p_include_flags) {
		properties["flags"] = _non_negative_integer_schema("Applied signal connection flags.");
	}
	PackedStringArray required{ "path", "signal", "targetPath", "method" };
	if (p_include_flags) {
		required.push_back("flags");
	}
	return MCPToolUtils::make_object_schema(properties, required);
}

static bool _validate_string_argument(const Dictionary &p_arguments, const StringName &p_name, String &r_value) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() != Variant::STRING || String(value).is_empty()) {
		return false;
	}
	r_value = value;
	return true;
}

static bool _is_script_path(const Ref<Script> &p_script, const String &p_path) {
	if (p_path.is_empty()) {
		return false;
	}
	Ref<Script> script = p_script;
	while (script.is_valid()) {
		if (script->get_path() == p_path) {
			return true;
		}
		script = script->get_base_script();
	}
	return false;
}

static String _get_layout_kind(const PropertyInfo &p_property) {
	if (p_property.usage & PROPERTY_USAGE_CATEGORY) {
		return "category";
	}
	if (p_property.usage & PROPERTY_USAGE_GROUP) {
		return "group";
	}
	if (p_property.usage & PROPERTY_USAGE_SUBGROUP) {
		return "subgroup";
	}
	return String();
}

static bool _is_native_class_category(Node *p_node, const PropertyInfo &p_property) {
	return (p_property.usage & PROPERTY_USAGE_CATEGORY) &&
			p_property.hint_string == String(p_property.name) && p_node->is_class(p_property.name);
}

static Dictionary _make_node_value_reference(Node *p_scene_root, const Variant &p_value) {
	if (p_value.get_type() != Variant::OBJECT) {
		return Dictionary();
	}
	Node *referenced_node = Object::cast_to<Node>(p_value.get_validated_object());
	if (!referenced_node) {
		return Dictionary();
	}

	Dictionary reference;
	if (MCPSceneUtils::is_node_in_scene(p_scene_root, referenced_node)) {
		reference = MCPSceneUtils::make_node_summary(p_scene_root, referenced_node);
		reference["scope"] = "editedScene";
	} else {
		reference["name"] = String(referenced_node->get_name());
		reference["type"] = referenced_node->get_class();
		if (referenced_node->is_inside_tree()) {
			reference["path"] = String(referenced_node->get_path());
			reference["scope"] = "sceneTree";
		} else {
			reference["scope"] = "detached";
		}
	}
	reference["kind"] = "node";
	return reference;
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPNodeProvider::MCPNodeProvider(Node *p_scene_root, MCPUndoRedoAction *p_undo_redo) :
		scene_root_override(p_scene_root),
		undo_redo_override(p_undo_redo) {
}

MCPNodeProvider::~MCPNodeProvider() {
	unregister_tools();
}

Node *MCPNodeProvider::_get_scene_root() const {
	return scene_root_override ? scene_root_override : MCPSceneUtils::get_edited_scene_root();
}

Error MCPNodeProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Node tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
			{ "godot.node.get_properties", "Get all native and script-exposed node properties marked for editor use.",
					_path_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPNodeProvider::get_properties), _node_properties_output_schema() },
			{ "godot.node.create", "Create a node through editor undo/redo.",
					_create_schema(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPNodeProvider::create), _node_summary_schema() },
			{ "godot.node.set_property", "Set a node property through editor undo/redo.",
					_set_property_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPNodeProvider::set_property), _node_property_output_schema() },
			{ "godot.node.attach_script", "Attach a project script through editor undo/redo.",
					_attach_script_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPNodeProvider::attach_script), _attach_script_output_schema() },
			{ "godot.node.get_groups", "List editable scene groups on a node.",
					_path_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPNodeProvider::get_groups), _groups_output_schema() },
			{ "godot.node.add_to_group", "Add a node to a persistent scene group through editor undo/redo.",
					_group_schema(true), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPNodeProvider::add_to_group), _group_mutation_output_schema(true) },
			{ "godot.node.remove_from_group", "Remove a node from a scene group through editor undo/redo.",
					_group_schema(false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPNodeProvider::remove_from_group), _group_mutation_output_schema(false) },
			{ "godot.node.get_signal_connections", "List persistent signal connections from an editable scene node.",
					_signal_connections_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPNodeProvider::get_signal_connections), _connections_output_schema() },
			{ "godot.node.connect_signal", "Create a persistent scene signal connection through editor undo/redo.",
					_signal_mutation_schema(true), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPNodeProvider::connect_signal), _signal_mutation_output_schema(true) },
			{ "godot.node.disconnect_signal", "Remove a persistent scene signal connection through editor undo/redo.",
					_signal_mutation_schema(false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPNodeProvider::disconnect_signal), _signal_mutation_output_schema(false) },
	};
	const Error err = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (err != OK) {
		return err;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPNodeProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPNodeProvider::get_properties(const Dictionary &p_arguments, const Dictionary &) {
	String path;
	if (!_validate_string_argument(p_arguments, "path", path)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string path is required.");
	}

	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = MCPSceneUtils::find_node(scene_root, path);
	if (!node) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + path);
	}

	const Ref<Script> node_script = node->get_script();
	const String attached_script_path = node_script.is_valid() ? node_script->get_path() : String();
	bool in_script_section = node->get_script_instance() != nullptr;
	String current_script_path = attached_script_path;
	Array properties;
	Array property_layout;
	int script_property_count = 0;
	List<PropertyInfo> property_list;
	node->get_property_list(&property_list, true);
	for (const PropertyInfo &property : property_list) {
		const String layout_kind = _get_layout_kind(property);
		if (!layout_kind.is_empty()) {
			if (layout_kind == "category" && in_script_section) {
				if (_is_native_class_category(node, property)) {
					in_script_section = false;
					current_script_path = String();
				} else if (_is_script_path(node_script, property.hint_string) ||
						property.hint_string.begins_with("res://")) {
					current_script_path = property.hint_string;
				}
			}
			Dictionary layout_entry = MCPToolUtils::make_property_description(property);
			layout_entry["kind"] = layout_kind;
			if (in_script_section) {
				layout_entry["source"] = "script";
				if (!current_script_path.is_empty()) {
					layout_entry["scriptPath"] = current_script_path;
				}
			} else {
				layout_entry["source"] = "native";
			}
			property_layout.push_back(layout_entry);
			continue;
		}
		if (!(property.usage & PROPERTY_USAGE_EDITOR)) {
			continue;
		}

		const bool script_exposed = in_script_section;
		const String property_script_path = !current_script_path.is_empty() ? current_script_path : attached_script_path;
		Dictionary item = MCPToolUtils::make_property_description(property);
		item["readOnly"] = bool(property.usage & PROPERTY_USAGE_READ_ONLY);
		item["scriptExposed"] = script_exposed;
		item["source"] = script_exposed ? "script" : "native";
		if (script_exposed && !property_script_path.is_empty()) {
			item["scriptPath"] = property_script_path;
		}

		bool valid = false;
		const Variant value = node->get(property.name, &valid);
		item["valueAvailable"] = valid;
		if (!valid) {
			item["encodable"] = false;
			item["encodingError"] = "Property value is unavailable.";
		} else {
			Variant encoded_value;
			String encoding_error;
			if (MCPVariantCodec::encode(value, encoded_value, &encoding_error) == OK) {
				item["encodable"] = true;
				item["value"] = encoded_value;
			} else {
				item["encodable"] = false;
				item["encodingError"] = encoding_error;
				const Dictionary value_reference = _make_node_value_reference(scene_root, value);
				if (!value_reference.is_empty()) {
					item["valueReference"] = value_reference;
				}
			}
		}

		Dictionary layout_entry;
		layout_entry["kind"] = "property";
		layout_entry["name"] = String(property.name);
		layout_entry["propertyIndex"] = properties.size();
		layout_entry["source"] = item["source"];
		if (script_exposed && !property_script_path.is_empty()) {
			layout_entry["scriptPath"] = property_script_path;
		}
		property_layout.push_back(layout_entry);
		properties.push_back(item);
		if (script_exposed) {
			script_property_count++;
		}
	}

	Dictionary result = MCPSceneUtils::make_node_summary(scene_root, node);
	if (!attached_script_path.is_empty()) {
		result["scriptPath"] = attached_script_path;
	}
	result["propertyCount"] = properties.size();
	result["scriptPropertyCount"] = script_property_count;
	result["properties"] = properties;
	result["propertyLayout"] = property_layout;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeProvider::create(const Dictionary &p_arguments, const Dictionary &) {
	String type;
	if (!_validate_string_argument(p_arguments, "type", type)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string type is required.");
	}
	const Variant name_value = p_arguments.get("name", String());
	const Variant parent_path_value = p_arguments.get("parentPath", ".");
	if (name_value.get_type() != Variant::STRING || parent_path_value.get_type() != Variant::STRING) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "name and parentPath must be strings when provided.");
	}

	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *parent = MCPSceneUtils::find_node(scene_root, parent_path_value);
	if (!parent) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Parent node was not found: " + String(parent_path_value));
	}

	Node *node = nullptr;
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeOperations::create_node(scene_root, parent, type, name_value,
			undo_redo_override ? undo_redo_override : &editor_undo_redo, node, &operation_error);
	if (err != OK) {
		return MCPToolUtils::make_error_result(err == ERR_INVALID_DATA ? "INVALID_NODE_TYPE" : "CREATE_FAILED", operation_error);
	}
	return MCPToolUtils::make_success_result(MCPSceneUtils::make_node_summary(scene_root, node));
}

Dictionary MCPNodeProvider::set_property(const Dictionary &p_arguments, const Dictionary &) {
	String path;
	String property;
	if (!_validate_string_argument(p_arguments, "path", path) || !_validate_string_argument(p_arguments, "property", property) ||
			!p_arguments.has("value")) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Non-empty path, property, and a value are required.");
	}

	Variant decoded_value;
	String codec_error;
	if (MCPVariantCodec::decode(p_arguments["value"], decoded_value, &codec_error) != OK) {
		return MCPToolUtils::make_error_result("INVALID_VALUE", codec_error);
	}

	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = MCPSceneUtils::find_node(scene_root, path);
	if (!node) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + path);
	}

	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeOperations::set_property(
			scene_root, node, property, decoded_value, undo_redo_override ? undo_redo_override : &editor_undo_redo, &operation_error);
	if (err != OK) {
		return MCPToolUtils::make_error_result("PROPERTY_SET_FAILED", operation_error);
	}

	Dictionary result;
	result["path"] = MCPSceneUtils::get_relative_path(scene_root, node);
	result["property"] = property;
	Variant encoded_value;
	if (MCPVariantCodec::encode(node->get(property), encoded_value, &codec_error) == OK) {
		result["value"] = encoded_value;
	} else {
		result["value"] = p_arguments["value"];
		result["valueEncodingError"] = codec_error;
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeProvider::attach_script(const Dictionary &p_arguments, const Dictionary &) {
	String path;
	String script_path;
	if (!_validate_string_argument(p_arguments, "path", path) || !_validate_string_argument(p_arguments, "scriptPath", script_path)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Non-empty path and scriptPath strings are required.");
	}

	String normalized_script_path;
	String absolute_script_path;
	String path_error;
	const Error path_result = MCPPathUtils::resolve_project_file_path(
			script_path, normalized_script_path, absolute_script_path, &path_error);
	if (path_result != OK) {
		return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
	}
	script_path = normalized_script_path;
	if (!FileAccess::exists(absolute_script_path) || !ResourceLoader::exists(script_path, "Script")) {
		return MCPToolUtils::make_error_result("SCRIPT_NOT_FOUND", "Script was not found: " + script_path);
	}
	const Ref<Script> script = ResourceLoader::load(script_path, "Script");
	if (script.is_null()) {
		return MCPToolUtils::make_error_result("SCRIPT_LOAD_FAILED", "Script could not be loaded: " + script_path);
	}

	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = MCPSceneUtils::find_node(scene_root, path);
	if (!node) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + path);
	}

	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeOperations::attach_script(
			scene_root, node, script, undo_redo_override ? undo_redo_override : &editor_undo_redo, &operation_error);
	if (err != OK) {
		return MCPToolUtils::make_error_result("SCRIPT_ATTACH_FAILED", operation_error);
	}

	Dictionary result;
	result["path"] = MCPSceneUtils::get_relative_path(scene_root, node);
	result["scriptPath"] = script_path;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeProvider::get_groups(const Dictionary &p_arguments, const Dictionary &) {
	String path;
	if (!_validate_string_argument(p_arguments, "path", path)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string path is required.");
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = MCPSceneUtils::find_node(scene_root, path);
	if (!node) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + path);
	}
	List<Node::GroupInfo> groups;
	node->get_groups(&groups);
	Array result_groups;
	for (const Node::GroupInfo &group : groups) {
		Dictionary item;
		item["name"] = String(group.name);
		item["persistent"] = group.persistent;
		result_groups.push_back(item);
	}
	Dictionary result = MCPSceneUtils::make_node_summary(scene_root, node);
	result["groups"] = result_groups;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeProvider::add_to_group(const Dictionary &p_arguments, const Dictionary &) {
	String path;
	String group;
	if (!_validate_string_argument(p_arguments, "path", path) || !_validate_string_argument(p_arguments, "group", group)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Non-empty path and group strings are required.");
	}
	const Variant persistent_value = p_arguments.get("persistent", true);
	if (persistent_value.get_type() != Variant::BOOL) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "persistent must be a boolean when provided.");
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = MCPSceneUtils::find_node(scene_root, path);
	if (!node) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + path);
	}
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeOperations::add_to_group(scene_root, node, group, persistent_value,
			undo_redo_override ? undo_redo_override : &editor_undo_redo, &operation_error);
	if (err != OK) {
		return MCPToolUtils::make_error_result("GROUP_ADD_FAILED", operation_error);
	}
	Dictionary result = MCPSceneUtils::make_node_summary(scene_root, node);
	result["group"] = group;
	result["persistent"] = persistent_value;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeProvider::remove_from_group(const Dictionary &p_arguments, const Dictionary &) {
	String path;
	String group;
	if (!_validate_string_argument(p_arguments, "path", path) || !_validate_string_argument(p_arguments, "group", group)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Non-empty path and group strings are required.");
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = MCPSceneUtils::find_node(scene_root, path);
	if (!node) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + path);
	}
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = MCPNodeOperations::remove_from_group(scene_root, node, group,
			undo_redo_override ? undo_redo_override : &editor_undo_redo, &operation_error);
	if (err != OK) {
		return MCPToolUtils::make_error_result("GROUP_REMOVE_FAILED", operation_error);
	}
	Dictionary result = MCPSceneUtils::make_node_summary(scene_root, node);
	result["group"] = group;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeProvider::get_signal_connections(const Dictionary &p_arguments, const Dictionary &) {
	String path;
	if (!_validate_string_argument(p_arguments, "path", path)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string path is required.");
	}
	String signal;
	if (p_arguments.has("signal") && !_validate_string_argument(p_arguments, "signal", signal)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "signal must be a non-empty string when provided.");
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = MCPSceneUtils::find_node(scene_root, path);
	if (!node) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + path);
	}
	List<Object::Connection> signal_connections;
	if (signal.is_empty()) {
		node->get_all_signal_connections(&signal_connections);
	} else if (!node->has_signal(signal)) {
		return MCPToolUtils::make_error_result("SIGNAL_NOT_FOUND", "Signal was not found on source node: " + signal);
	} else {
		node->get_signal_connection_list(signal, &signal_connections);
	}
	Array items;
	for (const Object::Connection &connection : signal_connections) {
		if (!(connection.flags & Object::CONNECT_PERSIST) || !connection.callable.is_standard()) {
			continue;
		}
		Node *target = Object::cast_to<Node>(connection.callable.get_object());
		if (!MCPSceneUtils::is_node_in_scene(scene_root, target)) {
			continue;
		}
		Dictionary item;
		item["signal"] = String(connection.signal.get_name());
		item["targetPath"] = MCPSceneUtils::get_relative_path(scene_root, target);
		item["method"] = String(connection.callable.get_method());
		item["flags"] = int64_t(connection.flags & (Object::CONNECT_DEFERRED | Object::CONNECT_ONE_SHOT));
		items.push_back(item);
	}
	Dictionary result = MCPSceneUtils::make_node_summary(scene_root, node);
	result["connections"] = items;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeProvider::_mutate_signal_connection(const Dictionary &p_arguments, bool p_connect) {
	String path;
	String signal;
	String target_path;
	String method;
	if (!_validate_string_argument(p_arguments, "path", path) || !_validate_string_argument(p_arguments, "signal", signal) ||
			!_validate_string_argument(p_arguments, "targetPath", target_path) || !_validate_string_argument(p_arguments, "method", method)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Non-empty path, signal, targetPath, and method strings are required.");
	}
	uint32_t flags = 0;
	if (p_connect && p_arguments.has("flags")) {
		int64_t parsed_flags = 0;
		if (!MCPToolUtils::try_get_json_integer(p_arguments["flags"], 0, Object::CONNECT_DEFERRED | Object::CONNECT_ONE_SHOT, parsed_flags)) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "flags must contain only deferred (1) and one-shot (4) bits.");
		}
		flags = uint32_t(parsed_flags);
	}
	Node *scene_root = _get_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *source = MCPSceneUtils::find_node(scene_root, path);
	Node *target = MCPSceneUtils::find_node(scene_root, target_path);
	if (!source || !target) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", !source ? "Source node was not found: " + path : "Target node was not found: " + target_path);
	}
	String operation_error;
	MCPEditorUndoRedoAction editor_undo_redo(EditorUndoRedoManager::get_singleton());
	const Error err = p_connect ? MCPNodeOperations::connect_signal(scene_root, source, signal, target, method, flags,
			undo_redo_override ? undo_redo_override : &editor_undo_redo, &operation_error) :
			MCPNodeOperations::disconnect_signal(scene_root, source, signal, target, method,
					undo_redo_override ? undo_redo_override : &editor_undo_redo, &operation_error);
	if (err != OK) {
		return MCPToolUtils::make_error_result(p_connect ? "SIGNAL_CONNECT_FAILED" : "SIGNAL_DISCONNECT_FAILED", operation_error);
	}
	Dictionary result;
	result["path"] = MCPSceneUtils::get_relative_path(scene_root, source);
	result["signal"] = signal;
	result["targetPath"] = MCPSceneUtils::get_relative_path(scene_root, target);
	result["method"] = method;
	if (p_connect) {
		result["flags"] = int64_t(flags);
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeProvider::connect_signal(const Dictionary &p_arguments, const Dictionary &) {
	return _mutate_signal_connection(p_arguments, true);
}

Dictionary MCPNodeProvider::disconnect_signal(const Dictionary &p_arguments, const Dictionary &) {
	return _mutate_signal_connection(p_arguments, false);
}
