/**************************************************************************/
/*  mcp_gdscript_tool_utils.cpp                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_gdscript_tool_utils.h"

#include "mcp_tool_utils.h"

#include "modules/gdscript/language_server/godot_lsp.h"

namespace {

static constexpr int DEFAULT_COMPLETION_LIMIT = 100;
static constexpr int MAX_COMPLETION_LIMIT = 500;

static Dictionary _required_schema(const PackedStringArray &p_required) {
	Dictionary schema;
	schema["required"] = p_required;
	return schema;
}

static Array _document_selector_options() {
	PackedStringArray path_required;
	path_required.push_back("path");
	PackedStringArray uri_required;
	uri_required.push_back("uri");

	Array options;
	options.push_back(_required_schema(path_required));
	options.push_back(_required_schema(uri_required));
	return options;
}

static Dictionary _position_integer_schema(const String &p_description) {
	Dictionary property = MCPToolUtils::make_property_schema("integer", p_description);
	property["minimum"] = 0;
	property["maximum"] = INT32_MAX;
	return property;
}

static Dictionary _required_string_schema(const String &p_description) {
	Dictionary property = MCPToolUtils::make_property_schema("string", p_description);
	property["minLength"] = 1;
	return property;
}

static Dictionary _document_properties() {
	Dictionary properties;
	properties["path"] = _required_string_schema("Project res:// path or an absolute .gd path inside the project.");
	properties["uri"] = _required_string_schema("File URI resolving to a .gd file inside the project. Use path or uri, not both.");
	return properties;
}

static Dictionary _position_properties(bool p_include_position = true) {
	Dictionary properties = _document_properties();
	properties["line"] = _position_integer_schema("Zero-based document line.");
	properties["character"] = _position_integer_schema("Zero-based UTF-16 code-unit offset in the line.");
	if (p_include_position) {
		Dictionary position_properties;
		position_properties["line"] = _position_integer_schema("Zero-based document line.");
		position_properties["character"] = _position_integer_schema("Zero-based UTF-16 code-unit offset in the line.");
		PackedStringArray position_required;
		position_required.push_back("line");
		position_required.push_back("character");
		properties["position"] = MCPToolUtils::make_object_schema(position_properties, position_required);
	}
	return properties;
}

static Dictionary _position_schema(const Dictionary &p_properties) {
	PackedStringArray nested_position_required;
	nested_position_required.push_back("position");
	PackedStringArray flat_position_required;
	flat_position_required.push_back("line");
	flat_position_required.push_back("character");
	Array position_options;
	position_options.push_back(_required_schema(nested_position_required));
	position_options.push_back(_required_schema(flat_position_required));

	Dictionary document_constraint;
	document_constraint["oneOf"] = _document_selector_options();
	Dictionary position_constraint;
	position_constraint["oneOf"] = position_options;
	Array constraints;
	constraints.push_back(document_constraint);
	constraints.push_back(position_constraint);

	Dictionary schema = MCPToolUtils::make_object_schema(p_properties);
	schema["allOf"] = constraints;
	return schema;
}

static Dictionary _non_negative_integer_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("integer", p_description);
	schema["minimum"] = 0;
	return schema;
}

static Dictionary _metadata_properties() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Analyzed project GDScript path.");
	properties["uri"] = MCPToolUtils::make_property_schema("string", "Analyzed GDScript file URI.");
	properties["analysisRevision"] = _non_negative_integer_schema("GDScript analysis session revision.");
	properties["revision"] = _non_negative_integer_schema("Open document client revision, when available.");
	Dictionary sha256 = MCPToolUtils::make_property_schema("string", "SHA-256 of the analyzed source text.");
	sha256["pattern"] = "^[0-9a-f]{64}$";
	properties["sha256"] = sha256;
	properties["sourceState"] = MCPToolUtils::make_property_schema("string", "Open or disk analysis source state.");
	Dictionary client_version = MCPToolUtils::make_property_schema("integer", "LSP client version, or -1 for disk documents.");
	client_version["minimum"] = -1;
	properties["clientVersion"] = client_version;
	return properties;
}

static PackedStringArray _metadata_required() {
	return PackedStringArray{ "path", "uri", "analysisRevision", "sha256", "sourceState", "clientVersion" };
}

static Dictionary _object_array_schema(const String &p_description) {
	Dictionary array = MCPToolUtils::make_property_schema("array", p_description);
	array["items"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	return array;
}

static Dictionary _nullable_object_schema(const String &p_description) {
	Dictionary schema;
	schema["type"] = PackedStringArray{ "object", "null" };
	schema["description"] = p_description;
	schema["additionalProperties"] = true;
	return schema;
}

static Dictionary _metadata_output_schema(const Dictionary &p_extra_properties, const PackedStringArray &p_extra_required) {
	Dictionary properties = _metadata_properties();
	for (const KeyValue<Variant, Variant> &entry : p_extra_properties) {
		properties[entry.key] = entry.value;
	}
	PackedStringArray required = _metadata_required();
	for (const String &name : p_extra_required) {
		required.push_back(name);
	}
	return MCPToolUtils::make_object_schema(properties, required);
}

} // namespace

namespace MCPGDScriptToolUtils {

Dictionary diagnostics_schema() {
	Dictionary schema = MCPToolUtils::make_object_schema(_document_properties());
	schema["description"] = "Exactly one document selector is required: path or uri.";
	schema["oneOf"] = _document_selector_options();
	return schema;
}

Dictionary position_schema() {
	return _position_schema(_position_properties());
}

Dictionary references_schema() {
	Dictionary properties = _position_properties();
	properties["includeDeclaration"] = MCPToolUtils::make_property_schema("boolean", "Include the declaration in the returned references.");
	return _position_schema(properties);
}

Dictionary rename_schema() {
	Dictionary properties = _position_properties();
	properties["newName"] = _required_string_schema("The replacement identifier.");
	Dictionary schema = _position_schema(properties);
	PackedStringArray required;
	required.push_back("newName");
	schema["required"] = required;
	return schema;
}

Dictionary workspace_edit_schema() {
	Dictionary document_properties;
	document_properties["path"] = _required_string_schema("Project GDScript path changed by the WorkspaceEdit.");
	Dictionary expected_revision = MCPToolUtils::make_property_schema("integer", "Expected ScriptEditor revision.");
	expected_revision["minimum"] = 0;
	expected_revision["maximum"] = uint64_t(UINT32_MAX);
	document_properties["expected_revision"] = expected_revision;
	Dictionary expected_sha256 = MCPToolUtils::make_property_schema("string", "Expected authoritative SHA-256.");
	expected_sha256["pattern"] = "^[0-9a-fA-F]{64}$";
	document_properties["expected_sha256"] = expected_sha256;
	Dictionary document = MCPToolUtils::make_object_schema(document_properties, PackedStringArray{ "path" });
	Array expectation_options;
	expectation_options.push_back(_required_schema(PackedStringArray{ "expected_revision" }));
	expectation_options.push_back(_required_schema(PackedStringArray{ "expected_sha256" }));
	document["anyOf"] = expectation_options;

	Dictionary properties;
	Dictionary edit = MCPToolUtils::make_property_schema("object", "LSP WorkspaceEdit containing a changes object.");
	edit["additionalProperties"] = true;
	properties["edit"] = edit;
	Dictionary documents = MCPToolUtils::make_property_schema("array", "One optimistic revision expectation for every changed document.");
	documents["minItems"] = 1;
	documents["maxItems"] = 256;
	documents["items"] = document;
	properties["documents"] = documents;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "edit", "documents" });
}

Dictionary completion_schema() {
	Dictionary schema = position_schema();
	Dictionary properties = schema.get("properties", Dictionary());
	Dictionary limit = MCPToolUtils::make_property_schema("integer", "Maximum completion items returned.");
	limit["minimum"] = 1;
	limit["maximum"] = MAX_COMPLETION_LIMIT;
	limit["default"] = DEFAULT_COMPLETION_LIMIT;
	properties["limit"] = limit;
	schema["properties"] = properties;
	return schema;
}

Dictionary diagnostics_output_schema() {
	Dictionary properties;
	properties["diagnostics"] = _object_array_schema("GDScript diagnostics.");
	return _metadata_output_schema(properties, PackedStringArray{ "diagnostics" });
}

Dictionary symbols_output_schema() {
	Dictionary properties;
	properties["symbols"] = _object_array_schema("GDScript document symbols.");
	return _metadata_output_schema(properties, PackedStringArray{ "symbols" });
}

Dictionary completion_output_schema() {
	Dictionary properties;
	Dictionary items = _object_array_schema("Bounded LSP completion items.");
	items["maxItems"] = MAX_COMPLETION_LIMIT;
	properties["items"] = items;
	properties["totalItemCount"] = _non_negative_integer_schema("Total completion item count before truncation.");
	properties["isIncomplete"] = MCPToolUtils::make_property_schema("boolean", "Whether completion items were truncated.");
	return _metadata_output_schema(properties, PackedStringArray{ "items", "totalItemCount", "isIncomplete" });
}

Dictionary hover_output_schema() {
	Dictionary properties;
	properties["hover"] = _nullable_object_schema("LSP hover result, or null when no symbol is found.");
	properties["symbol"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	properties["found"] = MCPToolUtils::make_property_schema("boolean", "Whether hover information was found.");
	return _metadata_output_schema(properties, PackedStringArray{ "hover", "found" });
}

Dictionary locations_output_schema(bool p_include_operation) {
	Dictionary properties;
	properties["locations"] = _object_array_schema("Resolved LSP locations.");
	properties["found"] = MCPToolUtils::make_property_schema("boolean", "Whether locations were found.");
	properties["symbol"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	properties["native"] = MCPToolUtils::make_property_schema("boolean", "Whether the symbol resolves to a native class.");
	PackedStringArray required{ "locations", "found" };
	if (p_include_operation) {
		properties["operation"] = MCPToolUtils::make_property_schema("string", "Definition or declaration operation.");
		required.push_back("operation");
	}
	return _metadata_output_schema(properties, required);
}

Dictionary signature_output_schema() {
	Dictionary properties;
	properties["signatureHelp"] = _nullable_object_schema("LSP signature help, or null when unavailable.");
	properties["found"] = MCPToolUtils::make_property_schema("boolean", "Whether signature help was found.");
	return _metadata_output_schema(properties, PackedStringArray{ "signatureHelp", "found" });
}

Dictionary rename_output_schema() {
	Dictionary properties;
	properties["edit"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	properties["changed"] = MCPToolUtils::make_property_schema("boolean", "Whether the WorkspaceEdit contains changes.");
	return _metadata_output_schema(properties, PackedStringArray{ "edit", "changed" });
}

Dictionary workspace_edit_output_schema() {
	Dictionary properties;
	properties["applied"] = MCPToolUtils::make_property_schema("boolean", "Whether the WorkspaceEdit was applied.");
	properties["analysisSynchronized"] = MCPToolUtils::make_property_schema("boolean", "Whether all changed buffers were synchronized with analysis.");
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether changed buffers were saved.");
	properties["documentCount"] = _non_negative_integer_schema("Changed document count.");
	properties["documents"] = _object_array_schema("Changed authoritative Script editor buffer snapshots.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "applied", "analysisSynchronized", "saved", "documentCount", "documents" });
}

bool get_string_argument(const Dictionary &p_arguments, const String &p_name, String &r_value, String &r_error) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() != Variant::STRING) {
		r_error = p_name + " must be a string.";
		return false;
	}
	r_value = value;
	return true;
}

bool get_position_argument(const Dictionary &p_arguments, const String &p_name, int &r_value, String &r_error) {
	const Variant value = p_arguments.get(p_name, Variant());
	int64_t integer = 0;
	if (!MCPToolUtils::try_get_json_integer(value, INT64_MIN, INT64_MAX, integer)) {
		r_error = p_name + " must be an integer.";
		return false;
	}
	if (integer < 0 || integer > INT32_MAX) {
		r_error = p_name + " must be a non-negative 32-bit integer.";
		return false;
	}
	r_value = int(integer);
	return true;
}

bool get_completion_limit(const Dictionary &p_arguments, int &r_value, String &r_error) {
	int64_t limit = DEFAULT_COMPLETION_LIMIT;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("limit", DEFAULT_COMPLETION_LIMIT), 1, MAX_COMPLETION_LIMIT, limit)) {
		r_error = "limit is outside its allowed range.";
		return false;
	}
	r_value = int(limit);
	return true;
}

int completion_kind(ScriptLanguage::CodeCompletionKind p_kind) {
	switch (p_kind) {
		case ScriptLanguage::CODE_COMPLETION_KIND_ENUM:
			return LSP::CompletionItemKind::Enum;
		case ScriptLanguage::CODE_COMPLETION_KIND_CLASS:
			return LSP::CompletionItemKind::Class;
		case ScriptLanguage::CODE_COMPLETION_KIND_MEMBER:
			return LSP::CompletionItemKind::Property;
		case ScriptLanguage::CODE_COMPLETION_KIND_FUNCTION:
			return LSP::CompletionItemKind::Method;
		case ScriptLanguage::CODE_COMPLETION_KIND_SIGNAL:
			return LSP::CompletionItemKind::Event;
		case ScriptLanguage::CODE_COMPLETION_KIND_CONSTANT:
			return LSP::CompletionItemKind::Constant;
		case ScriptLanguage::CODE_COMPLETION_KIND_VARIABLE:
			return LSP::CompletionItemKind::Variable;
		case ScriptLanguage::CODE_COMPLETION_KIND_FILE_PATH:
			return LSP::CompletionItemKind::File;
		case ScriptLanguage::CODE_COMPLETION_KIND_NODE_PATH:
			return LSP::CompletionItemKind::Snippet;
		case ScriptLanguage::CODE_COMPLETION_KIND_KEYWORD:
			return LSP::CompletionItemKind::Keyword;
		case ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT:
			return LSP::CompletionItemKind::Text;
		case ScriptLanguage::CODE_COMPLETION_KIND_MAX:
			break;
	}
	return LSP::CompletionItemKind::Text;
}

} // namespace MCPGDScriptToolUtils
