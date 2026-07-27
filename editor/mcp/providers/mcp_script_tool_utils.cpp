/**************************************************************************/
/*  mcp_script_tool_utils.cpp                                             */
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

#include "mcp_script_tool_utils.h"

#include "mcp_tool_utils.h"

namespace {

static constexpr const char *MEMBER_KINDS[] = {
	"class",
	"property",
	"variable",
	"constant",
	"enum",
	"enumValue",
	"signal",
	"method",
	"parameter",
};

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

static Dictionary _member_schema() {
	Dictionary properties;
	Dictionary kind = MCPToolUtils::make_property_schema("string", "Member kind to read.");
	PackedStringArray kinds;
	for (const char *member_kind : MEMBER_KINDS) {
		kinds.push_back(member_kind);
	}
	kind["enum"] = kinds;
	properties["kind"] = kind;
	properties["name"] = _required_string_schema("Exact member name.");
	properties["owner"] = MCPToolUtils::make_property_schema("string", "Optional qualified inner class path, or required method/signal name for a parameter.");
	properties["classPath"] = _required_string_schema("Optional qualified inner class path for a parameter, such as Outer.Inner.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "kind", "name" });
}

static Dictionary _diagnostics_schema() {
	Dictionary diagnostics = MCPToolUtils::make_property_schema("array", "GDScript diagnostics for the authoritative buffer revision.");
	diagnostics["items"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	return diagnostics;
}

static Dictionary _snapshot_properties() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Authoritative Script editor resource path.");
	properties["text"] = MCPToolUtils::make_property_schema("string", "Authoritative UTF-8 script text.");
	properties["revision"] = _non_negative_integer_schema("Current Script editor revision.");
	properties["savedRevision"] = _non_negative_integer_schema("Last saved Script editor revision.");
	Dictionary sha256 = MCPToolUtils::make_property_schema("string", "SHA-256 of the authoritative script text.");
	sha256["pattern"] = "^[0-9a-f]{64}$";
	properties["sha256"] = sha256;
	properties["unsaved"] = MCPToolUtils::make_property_schema("boolean", "Whether the Script editor buffer has unsaved changes.");
	properties["sourceState"] = MCPToolUtils::make_property_schema("string", "Authoritative source state.");
	properties["sourceType"] = MCPToolUtils::make_property_schema("string", "External or built-in script source type.");
	properties["scenePath"] = MCPToolUtils::make_property_schema("string", "Owning scene path for a built-in script.");
	return properties;
}

static PackedStringArray _snapshot_required(bool p_include_text) {
	PackedStringArray required{ "path", "revision", "savedRevision", "sha256", "unsaved", "sourceState", "sourceType" };
	if (p_include_text) {
		required.push_back("text");
	}
	return required;
}

static bool _is_valid_class_path(const String &p_path) {
	return !p_path.is_empty() && !p_path.begins_with(".") && !p_path.ends_with(".") && !p_path.contains("..");
}

static bool _is_valid_member_kind(const String &p_kind) {
	for (const char *member_kind : MEMBER_KINDS) {
		if (p_kind == member_kind) {
			return true;
		}
	}
	return false;
}

} // namespace

namespace MCPScriptToolUtils {

Dictionary selector_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("External GDScript or built-in res:// resource path.");
	properties["nodePath"] = _required_string_schema("Node path in the edited scene whose attached GDScript should be used.");
	Dictionary schema = MCPToolUtils::make_object_schema(properties, PackedStringArray());
	Array one_of;
	Dictionary path_required;
	path_required["required"] = PackedStringArray{ "path" };
	one_of.push_back(path_required);
	Dictionary node_required;
	node_required["required"] = PackedStringArray{ "nodePath" };
	one_of.push_back(node_required);
	schema["oneOf"] = one_of;
	return schema;
}

Dictionary create_schema() {
	Dictionary properties;
	properties["path"] = _required_string_schema("New external .gd path beginning with res://.");
	properties["text"] = MCPToolUtils::make_property_schema("string", "Initial UTF-8 GDScript source text.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "text" });
}

Dictionary get_schema() {
	Dictionary schema = selector_schema();
	Dictionary properties = schema.get("properties", Dictionary());
	Dictionary view = MCPToolUtils::make_property_schema("string", "documentation omits implementations; full returns authoritative source lines.");
	view["enum"] = PackedStringArray{ "documentation", "full" };
	view["default"] = "documentation";
	properties["view"] = view;
	Dictionary include_comments = MCPToolUtils::make_property_schema("boolean", "Include documentation or source comments.");
	include_comments["default"] = true;
	properties["includeComments"] = include_comments;
	properties["member"] = _member_schema();
	schema["properties"] = properties;
	return schema;
}

Dictionary usages_schema() {
	Dictionary schema = selector_schema();
	Dictionary properties = schema.get("properties", Dictionary());
	properties["member"] = _member_schema();
	Dictionary include_declaration = MCPToolUtils::make_property_schema("boolean", "Include the selected member declaration in the returned LSP locations.");
	include_declaration["default"] = false;
	properties["includeDeclaration"] = include_declaration;
	schema["properties"] = properties;
	schema["required"] = PackedStringArray{ "member" };
	return schema;
}

Dictionary edit_schema() {
	Dictionary schema = selector_schema();
	Dictionary properties = schema.get("properties", Dictionary());
	properties["text"] = MCPToolUtils::make_property_schema("string", "Complete replacement source text.");

	Dictionary expected_revision = MCPToolUtils::make_property_schema("integer", "TextEdit revision returned by godot.script.get.");
	expected_revision["minimum"] = 0;
	expected_revision["maximum"] = int64_t(0xFFFFFFFFLL);
	properties["expected_revision"] = expected_revision;

	Dictionary expected_sha256 = MCPToolUtils::make_property_schema("string", "SHA-256 returned by godot.script.get.");
	expected_sha256["pattern"] = "^[0-9a-fA-F]{64}$";
	properties["expected_sha256"] = expected_sha256;
	schema["properties"] = properties;

	Dictionary text_required;
	text_required["required"] = PackedStringArray{ "text" };
	Dictionary revision_required;
	revision_required["required"] = PackedStringArray{ "expected_revision" };
	Dictionary sha_required;
	sha_required["required"] = PackedStringArray{ "expected_sha256" };
	Dictionary revision_choice;
	revision_choice["anyOf"] = Array{ revision_required, sha_required };
	schema["allOf"] = Array{ text_required, revision_choice };
	return schema;
}

Dictionary open_output_schema() {
	Dictionary properties = _snapshot_properties();
	properties["opened"] = MCPToolUtils::make_property_schema("boolean", "Whether the script was opened.");
	PackedStringArray required = _snapshot_required(false);
	required.push_back("opened");
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary create_output_schema() {
	Dictionary properties = _snapshot_properties();
	properties["created"] = MCPToolUtils::make_property_schema("boolean", "Whether the external script was created.");
	properties["diagnostics"] = _diagnostics_schema();
	PackedStringArray required = _snapshot_required(true);
	required.push_back("created");
	required.push_back("diagnostics");
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary get_output_schema() {
	Dictionary properties = _snapshot_properties();
	properties["view"] = MCPToolUtils::make_property_schema("string", "Applied script view.");
	properties["includeComments"] = MCPToolUtils::make_property_schema("boolean", "Whether comments were included.");
	PackedStringArray required = _snapshot_required(false);
	required.push_back("view");
	required.push_back("includeComments");
	return MCPToolUtils::make_object_schema(properties, required, true);
}

Dictionary usages_output_schema() {
	Dictionary properties = _snapshot_properties();
	properties["member"] = _member_schema();
	Dictionary locations = MCPToolUtils::make_property_schema("array", "Semantic LSP usage locations.");
	locations["items"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	properties["locations"] = locations;
	properties["count"] = _non_negative_integer_schema("Returned usage location count.");
	PackedStringArray required = _snapshot_required(false);
	required.push_back("member");
	required.push_back("locations");
	required.push_back("count");
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary edit_output_schema() {
	Dictionary properties = _snapshot_properties();
	properties["changed"] = MCPToolUtils::make_property_schema("boolean", "Whether the authoritative buffer changed.");
	properties["diagnostics"] = _diagnostics_schema();
	PackedStringArray required = _snapshot_required(true);
	required.push_back("changed");
	required.push_back("diagnostics");
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary save_output_schema() {
	Dictionary properties = _snapshot_properties();
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether the script was saved.");
	properties["diagnostics"] = _diagnostics_schema();
	PackedStringArray required = _snapshot_required(true);
	required.push_back("saved");
	required.push_back("diagnostics");
	return MCPToolUtils::make_object_schema(properties, required);
}

bool validate_member_query(const Dictionary &p_member, String &r_error) {
	if (p_member.is_empty()) {
		return true;
	}
	if (p_member.get("kind", Variant()).get_type() != Variant::STRING || String(p_member.get("kind", String())).is_empty() ||
			p_member.get("name", Variant()).get_type() != Variant::STRING || String(p_member.get("name", String())).is_empty() ||
			(p_member.has("owner") && p_member["owner"].get_type() != Variant::STRING) ||
			(p_member.has("classPath") && (p_member["classPath"].get_type() != Variant::STRING || String(p_member["classPath"]).is_empty()))) {
		r_error = "member requires non-empty string kind and name fields, with optional string owner and classPath fields.";
		return false;
	}
	const String kind = p_member.get("kind", String());
	const bool parameter = kind == "parameter";
	if (!_is_valid_member_kind(kind)) {
		r_error = "member kind is not supported.";
		return false;
	}
	if (parameter && String(p_member.get("owner", String())).is_empty()) {
		r_error = "A parameter member query requires its method or signal owner.";
		return false;
	}
	if (!parameter && p_member.has("classPath")) {
		r_error = "classPath is only valid for parameter member queries.";
		return false;
	}
	const String class_path = p_member.get("classPath", String());
	const String owner = p_member.get("owner", String());
	if (parameter && owner.contains(".")) {
		r_error = "A parameter member owner must be an unqualified method or signal name.";
		return false;
	}
	if ((!class_path.is_empty() && !_is_valid_class_path(class_path)) || (!parameter && owner.contains(".") && !_is_valid_class_path(owner))) {
		r_error = "Qualified class paths cannot start or end with a period or contain empty segments.";
		return false;
	}
	return true;
}

bool parse_script_view(const Dictionary &p_arguments, String &r_view, bool &r_include_comments, Dictionary &r_member, String &r_error) {
	r_view = p_arguments.get("view", "documentation");
	r_include_comments = p_arguments.get("includeComments", true);
	const Variant member_value = p_arguments.get("member", Dictionary());
	if ((r_view != "documentation" && r_view != "full") ||
			(p_arguments.has("view") && p_arguments["view"].get_type() != Variant::STRING) ||
			(p_arguments.has("includeComments") && p_arguments["includeComments"].get_type() != Variant::BOOL) ||
			member_value.get_type() != Variant::DICTIONARY) {
		r_error = "view, includeComments, or member has an invalid type or value.";
		return false;
	}
	r_member = member_value;
	return validate_member_query(r_member, r_error);
}

} // namespace MCPScriptToolUtils
