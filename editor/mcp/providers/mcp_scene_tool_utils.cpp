/**************************************************************************/
/*  mcp_scene_tool_utils.cpp                                              */
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

#include "mcp_scene_tool_utils.h"

#include "mcp_path_utils.h"
#include "mcp_tool_utils.h"

namespace {

static constexpr int MIN_TREE_DEPTH = -1;
static constexpr int MAX_TREE_DEPTH = 64;

static Error _fail(const String &p_message, String *r_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return ERR_INVALID_PARAMETER;
}

} // namespace

namespace MCPSceneToolUtils {

Dictionary node_summary_schema_properties() {
	Dictionary child_count = MCPToolUtils::make_property_schema("integer", "Number of child nodes.");
	child_count["minimum"] = 0;
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Node path relative to the edited scene root.");
	properties["name"] = MCPToolUtils::make_property_schema("string", "Node name.");
	properties["type"] = MCPToolUtils::make_property_schema("string", "Node class name.");
	properties["childCount"] = child_count;
	properties["editable"] = MCPToolUtils::make_property_schema("boolean", "Whether the node can be edited.");
	properties["internal"] = MCPToolUtils::make_property_schema("boolean", "Whether the node is internal.");
	properties["owner"] = MCPToolUtils::make_property_schema("string", "Owning scene node path.");
	properties["sceneFilePath"] = MCPToolUtils::make_property_schema("string", "Packed scene path when the node is an instance.");
	return properties;
}

Dictionary node_summary_schema(bool p_allow_additional_properties) {
	return MCPToolUtils::make_object_schema(node_summary_schema_properties(),
			PackedStringArray{ "path", "name", "type", "childCount", "editable", "internal", "owner" }, p_allow_additional_properties);
}

Dictionary open_schema() {
	Dictionary path = MCPToolUtils::make_property_schema("string", "Project scene path beginning with res://.");
	path["minLength"] = 1;
	Dictionary properties;
	properties["path"] = path;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path" });
}

Dictionary tree_schema() {
	const TreeOptions defaults;
	Dictionary root_path = MCPToolUtils::make_property_schema("string", "Optional node path used as the returned tree root.");
	root_path["default"] = defaults.root_path;
	Dictionary max_depth = MCPToolUtils::make_property_schema("integer", "Maximum descendant depth. -1 uses the safety limit of 64.");
	max_depth["minimum"] = MIN_TREE_DEPTH;
	max_depth["maximum"] = MAX_TREE_DEPTH;
	max_depth["default"] = defaults.max_depth;
	Dictionary include_internal = MCPToolUtils::make_property_schema("boolean", "Include editor-internal child nodes.");
	include_internal["default"] = defaults.include_internal;

	Dictionary properties;
	properties["rootPath"] = root_path;
	properties["maxDepth"] = max_depth;
	properties["includeInternal"] = include_internal;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary save_schema() {
	Dictionary path = MCPToolUtils::make_property_schema("string", "Optional res:// path for Save As. Existing scene path is used when omitted.");
	Dictionary properties;
	properties["path"] = path;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary path_result_schema(const String &p_boolean_name, const String &p_boolean_description) {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Normalized project scene path.");
	properties[p_boolean_name] = MCPToolUtils::make_property_schema("boolean", p_boolean_description);
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", p_boolean_name });
}

Dictionary tree_output_schema() {
	Dictionary root = node_summary_schema(true);
	Dictionary root_properties = root.get("properties", Dictionary());
	Dictionary child;
	child["type"] = "object";
	child["additionalProperties"] = true;
	Dictionary children = MCPToolUtils::make_property_schema("array", "Child scene tree entries.");
	children["items"] = child;
	root_properties["children"] = children;
	root_properties["truncated"] = MCPToolUtils::make_property_schema("boolean", "Whether deeper descendants were omitted.");
	root["properties"] = root_properties;
	PackedStringArray root_required = root.get("required", PackedStringArray());
	root_required.push_back("children");
	root["required"] = root_required;

	Dictionary properties;
	properties["scenePath"] = MCPToolUtils::make_property_schema("string", "Current edited scene path.");
	properties["root"] = root;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "scenePath", "root" });
}

Dictionary selection_output_schema() {
	Dictionary count = MCPToolUtils::make_property_schema("integer", "Selected scene node count.");
	count["minimum"] = 0;
	Dictionary nodes = MCPToolUtils::make_property_schema("array", "Selected scene node summaries.");
	nodes["items"] = node_summary_schema(true);

	Dictionary properties;
	properties["count"] = count;
	properties["nodes"] = nodes;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "count", "nodes" });
}

bool parse_open_path(const Dictionary &p_arguments, String &r_path, String &r_error_message) {
	r_path = String();
	r_error_message = String();
	const Variant path = p_arguments.get("path", Variant());
	if (path.get_type() != Variant::STRING || String(path).is_empty()) {
		r_error_message = "A non-empty string path is required.";
		return false;
	}
	r_path = String(path);
	return true;
}

bool parse_save_path(const Dictionary &p_arguments, String &r_path, String &r_error_message) {
	r_path = String();
	r_error_message = String();
	const Variant path = p_arguments.get("path", String());
	if (path.get_type() != Variant::STRING) {
		r_error_message = "path must be a string when provided.";
		return false;
	}
	r_path = String(path);
	return true;
}

bool parse_tree_options(const Dictionary &p_arguments, TreeOptions &r_options, String &r_error_message) {
	const TreeOptions defaults;
	r_options = defaults;
	r_error_message = String();
	const Variant root_path = p_arguments.get("rootPath", defaults.root_path);
	const Variant max_depth = p_arguments.get("maxDepth", defaults.max_depth);
	const Variant include_internal = p_arguments.get("includeInternal", defaults.include_internal);
	int64_t parsed_max_depth = 0;
	if (root_path.get_type() != Variant::STRING ||
			!MCPToolUtils::try_get_json_integer(max_depth, MIN_TREE_DEPTH, MAX_TREE_DEPTH, parsed_max_depth) ||
			include_internal.get_type() != Variant::BOOL) {
		r_error_message = vformat("rootPath must be a string, maxDepth an integer from %d to %d, and includeInternal a boolean.", MIN_TREE_DEPTH, MAX_TREE_DEPTH);
		return false;
	}
	r_options.root_path = String(root_path);
	r_options.max_depth = int(parsed_max_depth);
	r_options.include_internal = bool(include_internal);
	return true;
}

Error resolve_scene_file_path(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error) {
	const Error err = MCPPathUtils::resolve_project_file_path(p_path, r_resource_path, r_absolute_path, r_error);
	if (err != OK) {
		return err;
	}
	const String extension = r_resource_path.get_extension().to_lower();
	if (extension != "tscn" && extension != "scn") {
		return _fail("Scene path must use the .tscn or .scn extension.", r_error);
	}
	return OK;
}

} // namespace MCPSceneToolUtils
