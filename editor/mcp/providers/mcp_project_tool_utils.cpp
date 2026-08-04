/**************************************************************************/
/*  mcp_project_tool_utils.cpp                                            */
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

#include "mcp_project_tool_utils.h"

#include "mcp_tool_utils.h"

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

static Dictionary _operation_schema(const String &p_operation) {
	Dictionary schema = MCPToolUtils::make_property_schema("string", "Project-setting mutation operation.");
	schema["enum"] = PackedStringArray{ p_operation };
	return schema;
}

static Dictionary _json_variant_schema() {
	Dictionary schema;
	schema["description"] = "JSON-native Variant value.";
	return schema;
}

static Dictionary _apply_operation_output_schema() {
	Dictionary properties;
	Dictionary operation = MCPToolUtils::make_property_schema("string", "Applied project-setting mutation operation.");
	operation["enum"] = PackedStringArray{ "set", "erase" };
	properties["operation"] = operation;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Affected project-setting name.");
	properties["changed"] = MCPToolUtils::make_property_schema("boolean", "Whether this operation changed the in-memory setting.");
	properties["value"] = _json_variant_schema();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "operation", "name", "changed" });
}

static Dictionary _autoload_output_schema() {
	Dictionary properties;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Autoload name.");
	properties["path"] = MCPToolUtils::make_property_schema("string", "Project-local Autoload resource path.");
	properties["singleton"] = MCPToolUtils::make_property_schema("boolean", "Whether the Autoload is a global singleton.");
	properties["prepend"] = MCPToolUtils::make_property_schema("boolean", "Whether the Autoload uses prepend ordering.");
	properties["order"] = MCPToolUtils::make_property_schema("integer", "Project setting serialization order.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name", "path", "singleton", "prepend", "order" });
}

} // namespace

namespace MCPProjectToolUtils {

Dictionary inspect_schema() {
	return MCPToolUtils::make_object_schema();
}

Dictionary settings_schema() {
	Dictionary properties;
	properties["prefix"] = MCPToolUtils::make_property_schema("string", "Optional project-setting name prefix.");
	Dictionary include_values = MCPToolUtils::make_property_schema("boolean", "Include safely encodable current values. Defaults to false.");
	include_values["default"] = false;
	properties["includeValues"] = include_values;
	Dictionary limit = MCPToolUtils::make_property_schema("integer", "Maximum matching settings returned.");
	limit["minimum"] = 1;
	limit["maximum"] = 1024;
	limit["default"] = 256;
	properties["limit"] = limit;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary setting_name_schema() {
	Dictionary properties;
	properties["name"] = _required_string_schema("Exact project-setting name.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name" });
}

Dictionary set_setting_schema() {
	Dictionary properties;
	properties["name"] = _required_string_schema("Exact project-setting name outside special managed namespaces.");
	properties["value"] = _json_variant_schema();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name", "value" });
}

Dictionary apply_schema() {
	Dictionary set_properties;
	set_properties["operation"] = _operation_schema("set");
	set_properties["name"] = _required_string_schema("Exact project-setting name outside special managed namespaces.");
	set_properties["value"] = _json_variant_schema();
	const Dictionary set_operation = MCPToolUtils::make_object_schema(set_properties, PackedStringArray{ "operation", "name", "value" });

	Dictionary erase_properties;
	erase_properties["operation"] = _operation_schema("erase");
	erase_properties["name"] = _required_string_schema("Exact project-setting name outside special managed namespaces.");
	const Dictionary erase_operation = MCPToolUtils::make_object_schema(erase_properties, PackedStringArray{ "operation", "name" });

	Dictionary operation_item_properties;
	operation_item_properties["operation"] = MCPToolUtils::make_property_schema("string", "Project-setting mutation operation.");
	operation_item_properties["name"] = _required_string_schema("Exact project-setting name outside special managed namespaces.");
	operation_item_properties["value"] = _json_variant_schema();
	Dictionary operation_item = MCPToolUtils::make_object_schema(operation_item_properties, PackedStringArray(), false);
	operation_item["oneOf"] = Array{ set_operation, erase_operation };
	Dictionary settings = MCPToolUtils::make_property_schema("array", "Ordered non-managed project-setting mutations.");
	settings["items"] = operation_item;
	settings["minItems"] = 1;
	settings["maxItems"] = 256;

	Dictionary save = MCPToolUtils::make_property_schema("boolean", "Save project.godot after applying all mutations. Defaults to true.");
	save["default"] = true;
	Dictionary expected_revision = MCPToolUtils::make_property_schema("string", "Expected SHA-256 revision for optimistic concurrency.");
	expected_revision["pattern"] = "^[0-9a-fA-F]{64}$";

	Dictionary properties;
	properties["settings"] = settings;
	properties["save"] = save;
	properties["expectedProjectRevision"] = expected_revision;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "settings" });
}

Dictionary autoload_add_schema() {
	Dictionary properties;
	properties["name"] = _required_string_schema("Valid autoload identifier.");
	properties["path"] = _required_string_schema("Existing project-local Script or PackedScene path.");
	Dictionary singleton = MCPToolUtils::make_property_schema("boolean", "Expose the autoload as a global singleton. Defaults to true.");
	singleton["default"] = true;
	properties["singleton"] = singleton;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name", "path" });
}

Dictionary autoload_name_schema() {
	Dictionary properties;
	properties["name"] = _required_string_schema("Exact Autoload name.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name" });
}

Dictionary setting_output_schema(bool p_require_encoding_state) {
	Dictionary properties;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Project setting name.");
	properties["type"] = MCPToolUtils::make_property_schema("string", "Godot Variant type name.");
	properties["typeId"] = _non_negative_integer_schema("Godot Variant type ID.");
	properties["builtin"] = MCPToolUtils::make_property_schema("boolean", "Whether this is a built-in project setting.");
	properties["order"] = MCPToolUtils::make_property_schema("integer", "Project setting serialization order.");
	properties["encodable"] = MCPToolUtils::make_property_schema("boolean", "Whether the current value is MCP encodable.");
	properties["value"] = Dictionary();
	properties["encodingError"] = MCPToolUtils::make_property_schema("string", "Value encoding failure.");
	PackedStringArray required{ "name", "type", "typeId", "builtin", "order" };
	if (p_require_encoding_state) {
		required.push_back("encodable");
	}
	return MCPToolUtils::make_object_schema(properties, required, true);
}

Dictionary inspect_output_schema() {
	Dictionary project_revision = MCPToolUtils::make_property_schema("string", "Current SHA-256 project configuration revision for optimistic concurrency.");
	project_revision["pattern"] = "^[0-9a-f]{64}$";
	Dictionary ordinary_setting_count = _non_negative_integer_schema("Stored settings managed by godot.project.apply.");
	Dictionary managed_setting_count = _non_negative_integer_schema("Stored settings owned by dedicated Autoload or Input Map tools.");
	Dictionary dedicated_tools = MCPToolUtils::make_property_schema("array", "Tools that must be used for managed configuration namespaces.");
	dedicated_tools["items"] = MCPToolUtils::make_property_schema("string", "Canonical MCP tool name.");
	dedicated_tools["minItems"] = 1;

	Dictionary properties;
	properties["projectPath"] = MCPToolUtils::make_property_schema("string", "Canonical project settings file path.");
	properties["projectRevision"] = project_revision;
	properties["projectFileExists"] = MCPToolUtils::make_property_schema("boolean", "Whether project.godot currently exists on disk.");
	properties["ordinarySettingCount"] = ordinary_setting_count;
	properties["managedSettingCount"] = managed_setting_count;
	properties["dedicatedTools"] = dedicated_tools;
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "projectPath", "projectRevision", "projectFileExists", "ordinarySettingCount", "managedSettingCount", "dedicatedTools" });
}

Dictionary settings_output_schema() {
	Dictionary settings = MCPToolUtils::make_property_schema("array", "Matching project setting descriptions.");
	settings["items"] = setting_output_schema(false);
	settings["maxItems"] = 1024;
	Dictionary properties;
	properties["prefix"] = MCPToolUtils::make_property_schema("string", "Applied project-setting prefix.");
	properties["matchedCount"] = _non_negative_integer_schema("Matching project setting count.");
	properties["returnedCount"] = _non_negative_integer_schema("Returned project setting count.");
	properties["truncated"] = MCPToolUtils::make_property_schema("boolean", "Whether matching settings were omitted by the limit.");
	properties["settings"] = settings;
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "prefix", "matchedCount", "returnedCount", "truncated", "settings" });
}

Dictionary named_mutation_output_schema(const String &p_state_name, const String &p_state_description, bool p_include_value) {
	Dictionary properties;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Affected project entry name.");
	properties[p_state_name] = MCPToolUtils::make_property_schema("boolean", p_state_description);
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether project.godot was saved.");
	if (p_include_value) {
		properties["value"] = Dictionary();
	}
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name", p_state_name, "saved" });
}

Dictionary save_output_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Saved project settings path.");
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether project settings were saved.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "saved" });
}

Dictionary apply_output_schema() {
	Dictionary operations = MCPToolUtils::make_property_schema("array", "Applied project-setting mutations in input order.");
	operations["items"] = _apply_operation_output_schema();
	operations["minItems"] = 1;
	operations["maxItems"] = 256;
	Dictionary project_revision = MCPToolUtils::make_property_schema("string", "SHA-256 revision after the operation.");
	project_revision["pattern"] = "^[0-9a-f]{64}$";

	Dictionary properties;
	properties["changed"] = MCPToolUtils::make_property_schema("boolean", "Whether any project setting changed.");
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether project.godot was saved.");
	properties["projectRevision"] = project_revision;
	properties["verified"] = MCPToolUtils::make_property_schema("boolean", "Whether the saved project.godot was verified readable.");
	properties["operations"] = operations;
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "changed", "saved", "projectRevision", "verified", "operations" });
}

Dictionary autoloads_output_schema() {
	Dictionary autoloads = MCPToolUtils::make_property_schema("array", "Autoload entries in load order.");
	autoloads["items"] = _autoload_output_schema();
	Dictionary properties;
	properties["count"] = _non_negative_integer_schema("Autoload entry count.");
	properties["autoloads"] = autoloads;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "count", "autoloads" });
}

Dictionary autoload_add_output_schema() {
	Dictionary properties;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Added Autoload name.");
	properties["path"] = MCPToolUtils::make_property_schema("string", "Project-local Autoload resource path.");
	properties["singleton"] = MCPToolUtils::make_property_schema("boolean", "Whether the Autoload is a global singleton.");
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether project.godot was saved.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name", "path", "singleton", "saved" });
}

} // namespace MCPProjectToolUtils
