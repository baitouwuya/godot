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
