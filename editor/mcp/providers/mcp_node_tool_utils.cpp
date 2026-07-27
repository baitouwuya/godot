/**************************************************************************/
/*  mcp_node_tool_utils.cpp                                               */
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

#include "mcp_node_tool_utils.h"

#include "mcp_scene_utils.h"
#include "mcp_tool_utils.h"

#include "core/object/object.h"

namespace {

static constexpr uint32_t SIGNAL_FLAGS_MASK = Object::CONNECT_DEFERRED | Object::CONNECT_ONE_SHOT;

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

} // namespace

namespace MCPNodeToolUtils {

Dictionary path_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root. Use '.' for the root.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path" });
}

Dictionary create_schema() {
	Dictionary properties;
	properties["type"] = _required_string_schema("Instantiable Godot Node class name.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Optional node name.");
	properties["parentPath"] = MCPToolUtils::make_property_schema("string", "Optional parent path. Defaults to the edited scene root.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "type" });
}

Dictionary rename_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root.");
	properties["name"] = _required_string_schema("New node name.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "name" });
}

Dictionary reparent_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root.");
	properties["parentPath"] = _required_string_schema("New parent path relative to the edited scene root.");
	Dictionary index = MCPToolUtils::make_property_schema("integer", "Optional 0-based child index. -1 appends the node.");
	index["minimum"] = -1;
	index["default"] = -1;
	properties["index"] = index;
	Dictionary keep_global_transform = MCPToolUtils::make_property_schema("boolean", "Preserve the global transform when supported. Defaults to true.");
	keep_global_transform["default"] = true;
	properties["keepGlobalTransform"] = keep_global_transform;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "parentPath" });
}

Dictionary move_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root.");
	Dictionary index = MCPToolUtils::make_property_schema("integer", "0-based child index. -1 moves the node to the end.");
	index["minimum"] = -1;
	properties["index"] = index;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "index" });
}

Dictionary duplicate_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path to duplicate.");
	properties["parentPath"] = MCPToolUtils::make_property_schema("string", "Optional destination parent. Defaults to the source parent.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Optional name for the duplicate.");
	Dictionary index = MCPToolUtils::make_property_schema("integer", "Optional 0-based child index. -1 appends the duplicate.");
	index["minimum"] = -1;
	index["default"] = -1;
	properties["index"] = index;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path" });
}

Dictionary instantiate_scene_schema() {
	Dictionary properties;
	properties["scenePath"] = _required_string_schema("PackedScene path beginning with res://.");
	properties["parentPath"] = MCPToolUtils::make_property_schema("string", "Optional destination parent. Defaults to the edited scene root.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Optional name for the instantiated scene root.");
	Dictionary index = MCPToolUtils::make_property_schema("integer", "Optional 0-based child index. -1 appends the instance.");
	index["minimum"] = -1;
	index["default"] = -1;
	properties["index"] = index;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "scenePath" });
}

Dictionary set_property_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root.");
	properties["property"] = _required_string_schema("Node property name.");
	Dictionary value;
	value["description"] = "JSON-native Variant value returned by godot.node.get_properties.";
	properties["value"] = value;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "property", "value" });
}

Dictionary attach_script_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root.");
	properties["scriptPath"] = _required_string_schema("Project script path beginning with res://.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "scriptPath" });
}

Dictionary group_schema(bool p_add) {
	Dictionary properties;
	properties["path"] = _required_string_schema("Node path relative to the edited scene root.");
	properties["group"] = _required_string_schema("Non-empty scene group name.");
	if (p_add) {
		properties["persistent"] = MCPToolUtils::make_property_schema("boolean", "Whether the group is saved with the scene. Defaults to true.");
	}
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "group" });
}

Dictionary signal_connections_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Source node path relative to the edited scene root.");
	properties["signal"] = _required_string_schema("Optional source signal name to filter connections.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path" });
}

Dictionary signal_mutation_schema(bool p_connect) {
	Dictionary properties;
	properties["path"] = _required_string_schema("Source node path relative to the edited scene root.");
	properties["signal"] = _required_string_schema("Signal declared by the source node.");
	properties["targetPath"] = _required_string_schema("Target node path relative to the edited scene root.");
	properties["method"] = _required_string_schema("Target method name.");
	if (p_connect) {
		Dictionary flags = MCPToolUtils::make_property_schema("integer", "Optional Object connect flags. Only deferred (1) and one-shot (4) are accepted; persistence is automatic.");
		flags["enum"] = Array{ 0, Object::CONNECT_DEFERRED, Object::CONNECT_ONE_SHOT, SIGNAL_FLAGS_MASK };
		flags["default"] = 0;
		properties["flags"] = flags;
	}
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "signal", "targetPath", "method" });
}

Dictionary node_properties_output_schema() {
	Dictionary array = MCPToolUtils::make_property_schema("array", "Inspectable node properties.");
	array["items"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	Dictionary layout = MCPToolUtils::make_property_schema("array", "Inspector property and section layout.");
	layout["items"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	Dictionary properties = MCPSceneUtils::make_node_summary_schema_properties();
	properties["scriptPath"] = MCPToolUtils::make_property_schema("string", "Attached script path.");
	properties["propertyCount"] = _non_negative_integer_schema("Inspectable property count.");
	properties["scriptPropertyCount"] = _non_negative_integer_schema("Script-exposed property count.");
	properties["properties"] = array;
	properties["propertyLayout"] = layout;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "name", "type", "childCount", "editable", "internal", "owner", "propertyCount", "scriptPropertyCount", "properties", "propertyLayout" }, true);
}

Dictionary node_property_output_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Changed node path.");
	properties["property"] = MCPToolUtils::make_property_schema("string", "Changed node property name.");
	properties["value"] = Dictionary();
	properties["valueEncodingError"] = MCPToolUtils::make_property_schema("string", "Value encoding failure.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "property", "value" });
}

Dictionary structure_output_schema(bool p_include_previous_path) {
	Dictionary properties = MCPSceneUtils::make_node_summary_schema_properties();
	properties["previousPath"] = MCPToolUtils::make_property_schema("string", "Node path before the operation.");
	properties["parentPath"] = MCPToolUtils::make_property_schema("string", "Parent node path after the operation.");
	Dictionary index = MCPToolUtils::make_property_schema("integer", "0-based child index after the operation.");
	index["minimum"] = 0;
	properties["index"] = index;
	PackedStringArray required{ "path", "name", "type", "childCount", "editable", "internal", "owner" };
	if (p_include_previous_path) {
		required.push_back("previousPath");
	}
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary delete_output_schema() {
	Dictionary deleted_properties = MCPSceneUtils::make_node_summary_schema_properties();
	deleted_properties["parentPath"] = MCPToolUtils::make_property_schema("string", "Parent node path before deletion.");
	Dictionary index = MCPToolUtils::make_property_schema("integer", "0-based child index before deletion.");
	index["minimum"] = 0;
	deleted_properties["index"] = index;
	const Dictionary deleted = MCPToolUtils::make_object_schema(deleted_properties,
			PackedStringArray{ "path", "name", "type", "childCount", "editable", "internal", "owner", "parentPath", "index" });
	Dictionary properties;
	properties["deleted"] = deleted;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "deleted" });
}

Dictionary attach_script_output_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Node path.");
	properties["scriptPath"] = MCPToolUtils::make_property_schema("string", "Attached project script path.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "scriptPath" });
}

Dictionary groups_output_schema() {
	Dictionary group_properties;
	group_properties["name"] = MCPToolUtils::make_property_schema("string", "Scene group name.");
	group_properties["persistent"] = MCPToolUtils::make_property_schema("boolean", "Whether the group is persistent.");
	Dictionary group = MCPToolUtils::make_object_schema(group_properties, PackedStringArray{ "name", "persistent" });
	Dictionary groups = MCPToolUtils::make_property_schema("array", "Node scene groups.");
	groups["items"] = group;
	Dictionary properties = MCPSceneUtils::make_node_summary_schema_properties();
	properties["groups"] = groups;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "name", "type", "childCount", "editable", "internal", "owner", "groups" }, true);
}

Dictionary group_mutation_output_schema(bool p_include_persistent) {
	Dictionary properties = MCPSceneUtils::make_node_summary_schema_properties();
	properties["group"] = MCPToolUtils::make_property_schema("string", "Affected scene group.");
	PackedStringArray required{ "path", "name", "type", "childCount", "editable", "internal", "owner", "group" };
	if (p_include_persistent) {
		properties["persistent"] = MCPToolUtils::make_property_schema("boolean", "Whether the group is persistent.");
		required.push_back("persistent");
	}
	return MCPToolUtils::make_object_schema(properties, required, true);
}

Dictionary connections_output_schema() {
	Dictionary connection_properties;
	connection_properties["signal"] = MCPToolUtils::make_property_schema("string", "Signal name.");
	connection_properties["targetPath"] = MCPToolUtils::make_property_schema("string", "Target node path.");
	connection_properties["method"] = MCPToolUtils::make_property_schema("string", "Target method name.");
	connection_properties["flags"] = _non_negative_integer_schema("Signal connection flags.");
	Dictionary connection = MCPToolUtils::make_object_schema(connection_properties, PackedStringArray{ "signal", "targetPath", "method", "flags" });
	Dictionary connections = MCPToolUtils::make_property_schema("array", "Persistent signal connections.");
	connections["items"] = connection;
	Dictionary properties = MCPSceneUtils::make_node_summary_schema_properties();
	properties["connections"] = connections;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "name", "type", "childCount", "editable", "internal", "owner", "connections" }, true);
}

Dictionary signal_mutation_output_schema(bool p_include_flags) {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Source node path.");
	properties["signal"] = MCPToolUtils::make_property_schema("string", "Signal name.");
	properties["targetPath"] = MCPToolUtils::make_property_schema("string", "Target node path.");
	properties["method"] = MCPToolUtils::make_property_schema("string", "Target method name.");
	PackedStringArray required{ "path", "signal", "targetPath", "method" };
	if (p_include_flags) {
		properties["flags"] = _non_negative_integer_schema("Applied signal connection flags.");
		required.push_back("flags");
	}
	return MCPToolUtils::make_object_schema(properties, required);
}

bool get_required_string(const Dictionary &p_arguments, const StringName &p_name, String &r_value) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() != Variant::STRING || String(value).is_empty()) {
		return false;
	}
	r_value = value;
	return true;
}

bool get_optional_string(const Dictionary &p_arguments, const StringName &p_name, const String &p_default, String &r_value) {
	const Variant value = p_arguments.get(p_name, p_default);
	if (value.get_type() != Variant::STRING) {
		return false;
	}
	r_value = value;
	return true;
}

bool get_index(const Dictionary &p_arguments, int &r_index) {
	int64_t index = -1;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("index", -1), -1, INT32_MAX, index)) {
		return false;
	}
	r_index = int(index);
	return true;
}

bool get_signal_flags(const Dictionary &p_arguments, uint32_t &r_flags, String &r_error) {
	int64_t flags = 0;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("flags", 0), 0, SIGNAL_FLAGS_MASK, flags) || (uint32_t(flags) & ~SIGNAL_FLAGS_MASK) != 0) {
		r_error = "flags must contain only deferred (1) and one-shot (4) bits.";
		return false;
	}
	r_flags = uint32_t(flags);
	return true;
}

} // namespace MCPNodeToolUtils
