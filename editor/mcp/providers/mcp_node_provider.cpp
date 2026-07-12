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

static Dictionary _property_schema(const String &p_type, const String &p_description) {
	Dictionary property;
	property["type"] = p_type;
	property["description"] = p_description;
	return property;
}

static Dictionary _object_schema(const Dictionary &p_properties, const PackedStringArray &p_required) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["required"] = p_required;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _path_schema() {
	Dictionary properties;
	properties["path"] = _property_schema("string", "Node path relative to the edited scene root. Use '.' for the root.");
	PackedStringArray required;
	required.push_back("path");
	return _object_schema(properties, required);
}

static Dictionary _create_schema() {
	Dictionary properties;
	properties["type"] = _property_schema("string", "Instantiable Godot Node class name.");
	properties["name"] = _property_schema("string", "Optional node name.");
	properties["parentPath"] = _property_schema("string", "Optional parent path. Defaults to the edited scene root.");
	PackedStringArray required;
	required.push_back("type");
	return _object_schema(properties, required);
}

static Dictionary _set_property_schema() {
	Dictionary properties;
	properties["path"] = _property_schema("string", "Node path relative to the edited scene root.");
	properties["property"] = _property_schema("string", "Node property name.");
	Dictionary value;
	value["description"] = "JSON-native Variant value returned by godot.node.get_properties.";
	properties["value"] = value;
	PackedStringArray required;
	required.push_back("path");
	required.push_back("property");
	required.push_back("value");
	return _object_schema(properties, required);
}

static Dictionary _attach_script_schema() {
	Dictionary properties;
	properties["path"] = _property_schema("string", "Node path relative to the edited scene root.");
	properties["scriptPath"] = _property_schema("string", "Project script path beginning with res://.");
	PackedStringArray required;
	required.push_back("path");
	required.push_back("scriptPath");
	return _object_schema(properties, required);
}

static Dictionary _unknown_argument_error(const String &p_name) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown argument: " + p_name);
}

static bool _validate_string_argument(const Dictionary &p_arguments, const StringName &p_name, String &r_value) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() != Variant::STRING || String(value).is_empty()) {
		return false;
	}
	r_value = value;
	return true;
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

	Error err = p_registry->register_tool(
			MCPToolUtils::make_tool_definition("godot.node.get_properties", "Get editor-visible properties for a node.", _path_schema()),
			callable_mp(this, &MCPNodeProvider::get_properties), MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.node.create", "Create a node through editor undo/redo.", _create_schema()),
				callable_mp(this, &MCPNodeProvider::create), MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.node.set_property", "Set a node property through editor undo/redo.", _set_property_schema()),
				callable_mp(this, &MCPNodeProvider::set_property), MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.node.attach_script", "Attach a project script through editor undo/redo.", _attach_script_schema()),
				callable_mp(this, &MCPNodeProvider::attach_script), MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
	}
	if (err != OK) {
		p_registry->unregister_tools_for_owner(this);
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
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}
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

	Array properties;
	List<PropertyInfo> property_list;
	node->get_property_list(&property_list);
	for (const PropertyInfo &property : property_list) {
		if (!(property.usage & PROPERTY_USAGE_EDITOR) ||
				(property.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP))) {
			continue;
		}

		bool valid = false;
		const Variant value = node->get(property.name, &valid);
		if (!valid) {
			continue;
		}
		Dictionary item;
		item["name"] = String(property.name);
		item["type"] = Variant::get_type_name(property.type);
		item["hint"] = int(property.hint);
		item["hintString"] = property.hint_string;
		item["usage"] = int64_t(property.usage);
		item["readOnly"] = bool(property.usage & PROPERTY_USAGE_READ_ONLY);

		Variant encoded_value;
		String encoding_error;
		if (MCPVariantCodec::encode(value, encoded_value, &encoding_error) == OK) {
			item["encodable"] = true;
			item["value"] = encoded_value;
		} else {
			item["encodable"] = false;
			item["encodingError"] = encoding_error;
		}
		properties.push_back(item);
	}

	Dictionary result = MCPSceneUtils::make_node_summary(scene_root, node);
	result["properties"] = properties;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPNodeProvider::create(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("type");
	allowed_arguments.push_back("name");
	allowed_arguments.push_back("parentPath");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

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
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	allowed_arguments.push_back("property");
	allowed_arguments.push_back("value");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

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
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	allowed_arguments.push_back("scriptPath");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

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
