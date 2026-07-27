/**************************************************************************/
/*  mcp_resource_tool_utils.cpp                                           */
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

#include "mcp_resource_tool_utils.h"

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

static Dictionary _property_description_schema() {
	Dictionary properties;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Resource property name.");
	properties["type"] = MCPToolUtils::make_property_schema("string", "Godot Variant type name.");
	properties["typeId"] = _non_negative_integer_schema("Godot Variant type ID.");
	properties["className"] = MCPToolUtils::make_property_schema("string", "Resource class name, when applicable.");
	properties["hint"] = _non_negative_integer_schema("Godot property hint ID.");
	properties["hintString"] = MCPToolUtils::make_property_schema("string", "Godot property hint data.");
	properties["usage"] = _non_negative_integer_schema("Godot property usage flags.");
	properties["readOnly"] = MCPToolUtils::make_property_schema("boolean", "Whether the property is read-only.");
	properties["editable"] = MCPToolUtils::make_property_schema("boolean", "Whether the property can be edited.");
	properties["valueAvailable"] = MCPToolUtils::make_property_schema("boolean", "Whether the property value is available.");
	properties["encodable"] = MCPToolUtils::make_property_schema("boolean", "Whether the property value is MCP encodable.");
	properties["value"] = Dictionary();
	properties["encodingError"] = MCPToolUtils::make_property_schema("string", "Value encoding failure.");
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "name", "type", "typeId", "className", "hint", "hintString", "usage", "readOnly", "editable", "valueAvailable", "encodable" });
}

} // namespace

namespace MCPResourceToolUtils {

Dictionary import_options_schema(bool p_include_target) {
	Dictionary properties;
	properties["source"] = _required_string_schema("Absolute read-only filesystem path to the source asset.");
	properties["importer"] = _required_string_schema("Optional ResourceImporter name.");

	Dictionary preset = MCPToolUtils::make_property_schema("integer", "Optional zero-based importer preset index.");
	preset["minimum"] = 0;
	properties["preset"] = preset;

	Dictionary options = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	options["description"] = "Optional JSON-native import option overrides keyed by PropertyInfo name.";
	properties["options"] = options;

	PackedStringArray required{ "source" };
	if (p_include_target) {
		properties["target"] = _required_string_schema("Destination source asset path beginning with res://.");
		Dictionary overwrite = MCPToolUtils::make_property_schema("boolean", "Allow replacing an existing target and its import metadata.");
		overwrite["default"] = false;
		properties["overwrite"] = overwrite;
		required.push_back("target");
	}
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary path_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Project resource path beginning with res://.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path" });
}

Dictionary set_property_schema() {
	Dictionary schema = path_schema();
	Dictionary properties = schema["properties"];
	properties["property"] = _required_string_schema("Editable Resource property name.");
	Dictionary value;
	value["description"] = "JSON-native Variant value returned by godot.resource.get_properties.";
	properties["value"] = value;
	schema["properties"] = properties;
	PackedStringArray required = schema["required"];
	required.push_back("property");
	required.push_back("value");
	schema["required"] = required;
	return schema;
}

Dictionary get_properties_output_schema() {
	Dictionary properties_array = MCPToolUtils::make_property_schema("array", "Inspectable Resource property descriptions.");
	properties_array["items"] = _property_description_schema();
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Project resource path.");
	properties["type"] = MCPToolUtils::make_property_schema("string", "Resource class name.");
	properties["propertyCount"] = _non_negative_integer_schema("Inspectable Resource property count.");
	properties["properties"] = properties_array;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "type", "propertyCount", "properties" });
}

Dictionary set_property_output_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Project resource path.");
	properties["property"] = MCPToolUtils::make_property_schema("string", "Changed Resource property name.");
	properties["changed"] = MCPToolUtils::make_property_schema("boolean", "Whether the effective property value changed.");
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether the Resource was saved.");
	properties["oldValue"] = Dictionary();
	properties["value"] = Dictionary();
	properties["valueEncodingError"] = MCPToolUtils::make_property_schema("string", "Value encoding failure.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "property", "changed", "saved", "value" });
}

Dictionary save_output_schema() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Saved project resource path.");
	properties["type"] = MCPToolUtils::make_property_schema("string", "Saved Resource class name.");
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether the Resource was saved.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "type", "saved" });
}

Dictionary import_options_output_schema() {
	Dictionary importer = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	Dictionary importers = MCPToolUtils::make_property_schema("array", "ResourceImporter candidates.");
	importers["items"] = importer;
	Dictionary options = MCPToolUtils::make_property_schema("array", "Effective import option descriptions.");
	options["items"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	Dictionary properties;
	properties["source"] = MCPToolUtils::make_property_schema("string", "Validated source asset path.");
	properties["extension"] = MCPToolUtils::make_property_schema("string", "Normalized source extension.");
	properties["defaultImporter"] = MCPToolUtils::make_property_schema("string", "Default ResourceImporter name.");
	properties["selectedImporter"] = MCPToolUtils::make_property_schema("string", "Selected ResourceImporter name.");
	properties["selectedPreset"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	properties["projectDefaultsApplied"] = MCPToolUtils::make_property_schema("boolean", "Whether project defaults were applied.");
	properties["importers"] = importers;
	properties["options"] = options;
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "source", "extension", "defaultImporter", "selectedImporter", "selectedPreset", "projectDefaultsApplied", "importers", "options" });
}

Dictionary import_output_schema() {
	Dictionary generated_files = MCPToolUtils::make_property_schema("array", "Generated imported Resource paths.");
	generated_files["items"] = MCPToolUtils::make_property_schema("string", "Project resource path.");
	Dictionary properties;
	properties["source"] = MCPToolUtils::make_property_schema("string", "Source asset path.");
	properties["target"] = MCPToolUtils::make_property_schema("string", "Imported project resource path.");
	properties["importer"] = MCPToolUtils::make_property_schema("string", "Selected ResourceImporter name.");
	properties["preset"] = _non_negative_integer_schema("Selected importer preset index.");
	properties["projectDefaultsApplied"] = MCPToolUtils::make_property_schema("boolean", "Whether project defaults were applied.");
	properties["overwritten"] = MCPToolUtils::make_property_schema("boolean", "Whether existing target-owned files were replaced.");
	properties["internalPath"] = MCPToolUtils::make_property_schema("string", "Godot internal imported Resource path.");
	properties["generatedFiles"] = generated_files;
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "source", "target", "importer", "preset", "projectDefaultsApplied", "overwritten", "internalPath", "generatedFiles" });
}

} // namespace MCPResourceToolUtils
