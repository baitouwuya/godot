/**************************************************************************/
/*  mcp_script_provider.cpp                                               */
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

#include "mcp_script_provider.h"

#include "mcp_script_analysis_sync.h"
#include "mcp_script_buffer.h"
#include "mcp_script_document.h"
#include "mcp_script_usages.h"
#include "mcp_scene_utils.h"
#include "mcp_tool_utils.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "editor/file_system/editor_file_system.h"
#include "modules/gdscript/language_server/gdscript_analysis_session.h"
#include "modules/gdscript/language_server/gdscript_extend_parser.h"
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

static Dictionary _script_selector_schema(const Dictionary &p_extra_properties = Dictionary()) {
	Dictionary properties;
	properties["path"] = _property_schema("string", "External GDScript or built-in res:// resource path.");
	properties["nodePath"] = _property_schema("string", "Node path in the edited scene whose attached GDScript should be used.");
	for (const KeyValue<Variant, Variant> &property : p_extra_properties) {
		properties[property.key] = property.value;
	}
	Dictionary schema = _object_schema(properties, PackedStringArray());
	Dictionary path_required;
	PackedStringArray path_names;
	path_names.push_back("path");
	path_required["required"] = path_names;
	Dictionary node_required;
	PackedStringArray node_names;
	node_names.push_back("nodePath");
	node_required["required"] = node_names;
	Array one_of;
	one_of.push_back(path_required);
	one_of.push_back(node_required);
	schema["oneOf"] = one_of;
	return schema;
}

static Dictionary _create_schema() {
	Dictionary properties;
	properties["path"] = _property_schema("string", "New external .gd path beginning with res://.");
	properties["text"] = _property_schema("string", "Initial UTF-8 GDScript source text.");
	PackedStringArray required;
	required.push_back("path");
	required.push_back("text");
	return _object_schema(properties, required);
}

static Dictionary _member_schema() {
	Dictionary properties;
	Dictionary kind = _property_schema("string", "Member kind to read.");
	PackedStringArray kinds;
	kinds.push_back("class");
	kinds.push_back("property");
	kinds.push_back("variable");
	kinds.push_back("constant");
	kinds.push_back("enum");
	kinds.push_back("enumValue");
	kinds.push_back("signal");
	kinds.push_back("method");
	kinds.push_back("parameter");
	kind["enum"] = kinds;
	properties["kind"] = kind;
	properties["name"] = _property_schema("string", "Exact member name.");
	properties["owner"] = _property_schema("string", "Optional qualified inner class path, or required method/signal name for a parameter.");
	properties["classPath"] = _property_schema("string", "Optional qualified inner class path for a parameter, such as Outer.Inner.");
	PackedStringArray required;
	required.push_back("kind");
	required.push_back("name");
	return _object_schema(properties, required);
}

static Dictionary _get_schema() {
	Dictionary properties;
	Dictionary view = _property_schema("string", "documentation omits implementations; full returns authoritative source lines.");
	PackedStringArray views;
	views.push_back("documentation");
	views.push_back("full");
	view["enum"] = views;
	view["default"] = "documentation";
	properties["view"] = view;
	Dictionary include_comments = _property_schema("boolean", "Include documentation or source comments.");
	include_comments["default"] = true;
	properties["includeComments"] = include_comments;
	properties["member"] = _member_schema();
	return _script_selector_schema(properties);
}

static Dictionary _usages_schema() {
	Dictionary properties;
	properties["member"] = _member_schema();
	Dictionary include_declaration = _property_schema("boolean", "Include the selected member declaration in the returned LSP locations.");
	include_declaration["default"] = false;
	properties["includeDeclaration"] = include_declaration;
	Dictionary schema = _script_selector_schema(properties);
	PackedStringArray required;
	required.push_back("member");
	schema["required"] = required;
	return schema;
}

static Dictionary _edit_schema() {
	Dictionary properties;
	properties["text"] = _property_schema("string", "Complete replacement source text.");

	Dictionary expected_revision = _property_schema("integer", "TextEdit revision returned by godot.script.get.");
	expected_revision["minimum"] = 0;
	expected_revision["maximum"] = int64_t(0xFFFFFFFFLL);
	properties["expected_revision"] = expected_revision;

	Dictionary expected_sha256 = _property_schema("string", "SHA-256 returned by godot.script.get.");
	expected_sha256["pattern"] = "^[0-9a-fA-F]{64}$";
	properties["expected_sha256"] = expected_sha256;

	Dictionary schema = _script_selector_schema(properties);
	PackedStringArray required;
	required.push_back("text");
	Dictionary text_required;
	text_required["required"] = required;

	PackedStringArray revision_required;
	revision_required.push_back("expected_revision");
	Dictionary revision_option;
	revision_option["required"] = revision_required;
	PackedStringArray sha_required;
	sha_required.push_back("expected_sha256");
	Dictionary sha_option;
	sha_option["required"] = sha_required;
	Array any_of;
	any_of.push_back(revision_option);
	any_of.push_back(sha_option);
	Array all_of;
	all_of.push_back(text_required);
	Dictionary revision_choice;
	revision_choice["anyOf"] = any_of;
	all_of.push_back(revision_choice);
	schema["allOf"] = all_of;
	return schema;
}

static Dictionary _unknown_argument_error(const String &p_name) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown argument: " + p_name);
}

static bool _get_required_string(const Dictionary &p_arguments, const StringName &p_name, String &r_value) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() != Variant::STRING) {
		return false;
	}
	r_value = value;
	return !r_value.is_empty();
}

static bool _is_valid_class_path(const String &p_path) {
	return !p_path.is_empty() && !p_path.begins_with(".") && !p_path.ends_with(".") && !p_path.contains("..");
}

static bool _is_valid_member_kind(const String &p_kind) {
	return p_kind == "class" || p_kind == "property" || p_kind == "variable" || p_kind == "constant" ||
			p_kind == "enum" || p_kind == "enumValue" || p_kind == "signal" || p_kind == "method" || p_kind == "parameter";
}

static String _buffer_error_code(Error p_error) {
	switch (p_error) {
		case ERR_FILE_NOT_FOUND:
			return "SCRIPT_NOT_FOUND";
		case ERR_INVALID_PARAMETER:
		case ERR_PARAMETER_RANGE_ERROR:
			return "INVALID_PATH";
		case ERR_UNCONFIGURED:
			return "EDITOR_UNAVAILABLE";
		default:
			return "BUFFER_UNAVAILABLE";
	}
}

static Dictionary _open_buffer(const String &p_path, MCPScriptBuffer &r_buffer) {
	String error;
	const Error err = MCPScriptBuffer::open(p_path, r_buffer, &error);
	return err == OK ? Dictionary() : MCPToolUtils::make_error_result(_buffer_error_code(err), error);
}

static Dictionary _open_selected_buffer(const Dictionary &p_arguments, MCPScriptBuffer &r_buffer) {
	const Variant path_value = p_arguments.get("path", Variant());
	const Variant node_path_value = p_arguments.get("nodePath", Variant());
	const bool has_path = path_value.get_type() == Variant::STRING && !String(path_value).is_empty();
	const bool has_node_path = node_path_value.get_type() == Variant::STRING && !String(node_path_value).is_empty();
	if (has_path == has_node_path) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Exactly one non-empty path or nodePath is required.");
	}
	if (has_path) {
		return _open_buffer(path_value, r_buffer);
	}

	Node *scene_root = MCPSceneUtils::get_edited_scene_root();
	if (!scene_root) {
		return MCPToolUtils::make_error_result("NO_SCENE", "No edited scene is open.");
	}
	Node *node = MCPSceneUtils::find_node(scene_root, node_path_value);
	if (!node) {
		return MCPToolUtils::make_error_result("NODE_NOT_FOUND", "Node was not found: " + String(node_path_value));
	}
	const Ref<Script> script = node->get_script();
	if (script.is_null()) {
		return MCPToolUtils::make_error_result("SCRIPT_NOT_FOUND", "The selected node does not have an attached script.");
	}
	String error;
	const Error err = MCPScriptBuffer::open(script, r_buffer, &error);
	return err == OK ? Dictionary() : MCPToolUtils::make_error_result(_buffer_error_code(err), error);
}

static bool _validate_member_query(const Dictionary &p_member, String &r_error) {
	if (!p_member.is_empty()) {
		String unknown;
		PackedStringArray allowed;
		allowed.push_back("kind");
		allowed.push_back("name");
		allowed.push_back("owner");
		allowed.push_back("classPath");
		if (!MCPToolUtils::has_only_arguments(p_member, allowed, unknown) ||
				p_member.get("kind", Variant()).get_type() != Variant::STRING || String(p_member.get("kind", String())).is_empty() ||
				p_member.get("name", Variant()).get_type() != Variant::STRING || String(p_member.get("name", String())).is_empty() ||
				(p_member.has("owner") && p_member["owner"].get_type() != Variant::STRING) ||
				(p_member.has("classPath") && (p_member["classPath"].get_type() != Variant::STRING || String(p_member["classPath"]).is_empty()))) {
			r_error = "member requires non-empty string kind and name fields, with optional string owner and classPath fields.";
			return false;
		}
		const bool parameter = String(p_member.get("kind", String())) == "parameter";
		if (!_is_valid_member_kind(p_member.get("kind", String()))) {
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
	}
	return true;
}

static bool _get_script_view(const Dictionary &p_arguments, String &r_view, bool &r_include_comments, Dictionary &r_member, String &r_error) {
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
	return _validate_member_query(r_member, r_error);
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPScriptProvider::MCPScriptProvider(const Ref<MCPGDScriptSessionManager> &p_session_manager) {
	session_manager = p_session_manager;
}

MCPScriptProvider::~MCPScriptProvider() {
	unregister_tools();
}

bool MCPScriptProvider::_resolve_analysis_context(const Dictionary &p_context, String &r_session_id, Ref<GDScriptAnalysisSession> &r_session, String &r_error) {
	if (session_manager.is_null()) {
		r_error = "A GDScript analysis session manager is required.";
		return false;
	}
	return session_manager->resolve_context(p_context, r_session_id, r_session, &r_error) == OK;
}

Error MCPScriptProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Script tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	Error err = p_registry->register_tool(
			MCPToolUtils::make_tool_definition("godot.script.create", "Create and open an external GDScript.", _create_schema()),
			callable_mp(this, &MCPScriptProvider::create), this, r_error);
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.script.open", "Open an external or built-in GDScript in the Script editor.", _script_selector_schema()),
				callable_mp(this, &MCPScriptProvider::open), this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.script.get", "Read a GDScript as documentation, a member, or full source.", _get_schema()),
				callable_mp(this, &MCPScriptProvider::get), this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.script.usages", "Find semantic LSP usages of a named GDScript member.", _usages_schema()),
				callable_mp(this, &MCPScriptProvider::usages), this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.script.edit", "Replace a Script editor buffer with optimistic concurrency.", _edit_schema()),
				callable_mp(this, &MCPScriptProvider::edit), this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.script.save", "Explicitly save an authoritative Script editor buffer.", _script_selector_schema()),
				callable_mp(this, &MCPScriptProvider::save), this, r_error);
	}
	if (err != OK) {
		p_registry->unregister_tools_for_owner(this);
		return err;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPScriptProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPScriptProvider::open(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	allowed_arguments.push_back("nodePath");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

	MCPScriptBuffer buffer;
	const Dictionary buffer_error = _open_selected_buffer(p_arguments, buffer);
	if (!buffer_error.is_empty()) {
		return buffer_error;
	}
	Dictionary result = buffer.get_snapshot();
	result.erase("text");
	result["opened"] = true;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPScriptProvider::create(const Dictionary &p_arguments, const Dictionary &p_context) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	allowed_arguments.push_back("text");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

	String path;
	String text;
	if (!_get_required_string(p_arguments, "path", path) || p_arguments.get("text", Variant()).get_type() != Variant::STRING) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty path and string text are required.");
	}
	text = p_arguments["text"];
	String session_id;
	Ref<GDScriptAnalysisSession> session;
	String analysis_error;
	if (!_resolve_analysis_context(p_context, session_id, session, analysis_error)) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", analysis_error);
	}
	MCPGDScriptTransientParserCleanup cleanup(session);

	String resource_path;
	String absolute_path;
	String path_error;
	const Error path_result = MCPScriptBuffer::normalize_path(path, resource_path, absolute_path, &path_error);
	if (path_result != OK) {
		return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
	}
	if (FileAccess::exists(absolute_path)) {
		return MCPToolUtils::make_error_result("SCRIPT_EXISTS", "Script already exists: " + resource_path);
	}

	const String parent_directory = absolute_path.get_base_dir();
	if (!DirAccess::dir_exists_absolute(parent_directory)) {
		const Error directory_error = DirAccess::make_dir_recursive_absolute(parent_directory);
		if (directory_error != OK) {
			return MCPToolUtils::make_error_result("DIRECTORY_CREATE_FAILED", "Could not create the script parent directory.");
		}
	}

	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return MCPToolUtils::make_error_result("FILE_OPEN_FAILED", "Could not create script: " + resource_path);
	}
	if (!file->store_string(text)) {
		file.unref();
		DirAccess::remove_absolute(absolute_path);
		return MCPToolUtils::make_error_result("FILE_WRITE_FAILED", "Could not write script: " + resource_path);
	}
	file->flush();
	file.unref();
	if (EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->update_file(resource_path);
	}

	MCPScriptBuffer buffer;
	const Dictionary buffer_error = _open_buffer(resource_path, buffer);
	if (!buffer_error.is_empty()) {
		Dictionary details;
		details["path"] = resource_path;
		details["created"] = true;
		const Dictionary structured = buffer_error.get("structuredContent", Dictionary());
		const Dictionary error = structured.get("error", Dictionary());
		return MCPToolUtils::make_error_result(error.get("code", "BUFFER_UNAVAILABLE"),
				error.get("message", "The script was created but its editor buffer is unavailable."), details);
	}

	Dictionary result = buffer.get_snapshot();
	Array diagnostics;
	if (MCPScriptAnalysisSync::sync_snapshot(session_manager, session_id, result, &diagnostics, &analysis_error) != OK) {
		return MCPScriptAnalysisSync::make_failure(result, true, true, true, analysis_error);
	}
	result["created"] = true;
	result["diagnostics"] = diagnostics;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPScriptProvider::get(const Dictionary &p_arguments, const Dictionary &p_context) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	allowed_arguments.push_back("nodePath");
	allowed_arguments.push_back("view");
	allowed_arguments.push_back("includeComments");
	allowed_arguments.push_back("member");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}
	String view;
	bool include_comments = true;
	Dictionary member;
	String argument_error;
	if (!_get_script_view(p_arguments, view, include_comments, member, argument_error)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}
	String session_id;
	Ref<GDScriptAnalysisSession> session;
	String analysis_error;
	if (!_resolve_analysis_context(p_context, session_id, session, analysis_error)) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", analysis_error);
	}
	MCPGDScriptTransientParserCleanup cleanup(session);

	MCPScriptBuffer buffer;
	const Dictionary buffer_error = _open_selected_buffer(p_arguments, buffer);
	if (!buffer_error.is_empty()) {
		return buffer_error;
	}
	Dictionary result = buffer.get_snapshot();
	if (MCPScriptAnalysisSync::sync_snapshot(session_manager, session_id, result, nullptr, &analysis_error) != OK) {
		return MCPScriptAnalysisSync::make_failure(result, false, false, false, analysis_error);
	}
	ExtendGDScriptParser *parser = session->get_parse_result(result.get("path", String()));
	if (!parser) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", "The synchronized GDScript parser result is unavailable.");
	}
	Dictionary document;
	String document_error;
	const Error render_error = MCPScriptDocument::render(*parser, view, include_comments, member, document, &document_error);
	if (render_error != OK) {
		const String error_code = render_error == ERR_DOES_NOT_EXIST ? "MEMBER_NOT_FOUND" : render_error == ERR_ALREADY_EXISTS ? "MEMBER_AMBIGUOUS"
																													 : "INVALID_ARGUMENTS";
		return MCPToolUtils::make_error_result(error_code, document_error);
	}
	result.erase("text");
	for (const KeyValue<Variant, Variant> &entry : document) {
		result[entry.key] = entry.value;
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPScriptProvider::usages(const Dictionary &p_arguments, const Dictionary &p_context) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	allowed_arguments.push_back("nodePath");
	allowed_arguments.push_back("member");
	allowed_arguments.push_back("includeDeclaration");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}
	const Variant member_value = p_arguments.get("member", Variant());
	const Variant include_declaration_value = p_arguments.get("includeDeclaration", false);
	if (member_value.get_type() != Variant::DICTIONARY || Dictionary(member_value).is_empty() || include_declaration_value.get_type() != Variant::BOOL) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty member object and optional boolean includeDeclaration are required.");
	}
	const Dictionary member = member_value;
	String argument_error;
	if (!_validate_member_query(member, argument_error)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}

	String session_id;
	Ref<GDScriptAnalysisSession> session;
	String analysis_error;
	if (!_resolve_analysis_context(p_context, session_id, session, analysis_error)) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", analysis_error);
	}
	MCPGDScriptTransientParserCleanup cleanup(session);

	MCPScriptBuffer buffer;
	const Dictionary buffer_error = _open_selected_buffer(p_arguments, buffer);
	if (!buffer_error.is_empty()) {
		return buffer_error;
	}
	Dictionary result = buffer.get_snapshot();
	if (MCPScriptAnalysisSync::sync_snapshot(session_manager, session_id, result, nullptr, &analysis_error) != OK) {
		return MCPScriptAnalysisSync::make_failure(result, false, false, false, analysis_error);
	}
	if (MCPScriptAnalysisSync::sync_open_buffers(session_manager, session_id, &analysis_error) != OK) {
		return MCPScriptAnalysisSync::make_failure(result, false, false, false, analysis_error);
	}
	const String path = result.get("path", String());
	ExtendGDScriptParser *parser = session->get_parse_result(path);
	if (!parser || session->get_workspace().is_null()) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", "The synchronized GDScript parser or workspace is unavailable.");
	}

	Dictionary usage_result;
	String usage_error;
	const Error error = MCPScriptUsages::find(session->get_workspace(), session, *parser, member, include_declaration_value, usage_result, &usage_error);
	if (error != OK) {
		String error_code = "USAGE_QUERY_FAILED";
		if (error == ERR_DOES_NOT_EXIST) {
			error_code = "MEMBER_NOT_FOUND";
		} else if (error == ERR_ALREADY_EXISTS) {
			error_code = "MEMBER_AMBIGUOUS";
		} else if (error == ERR_UNCONFIGURED) {
			error_code = "ANALYSIS_UNAVAILABLE";
		}
		return MCPToolUtils::make_error_result(error_code, usage_error);
	}
	result.erase("text");
	for (const KeyValue<Variant, Variant> &entry : usage_result) {
		result[entry.key] = entry.value;
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPScriptProvider::edit(const Dictionary &p_arguments, const Dictionary &p_context) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	allowed_arguments.push_back("nodePath");
	allowed_arguments.push_back("text");
	allowed_arguments.push_back("expected_revision");
	allowed_arguments.push_back("expected_sha256");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

	if (p_arguments.get("text", Variant()).get_type() != Variant::STRING) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A string text replacement is required.");
	}
	String session_id;
	Ref<GDScriptAnalysisSession> session;
	String analysis_error;
	if (!_resolve_analysis_context(p_context, session_id, session, analysis_error)) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", analysis_error);
	}
	MCPGDScriptTransientParserCleanup cleanup(session);

	MCPScriptBuffer buffer;
	const Dictionary buffer_error = _open_selected_buffer(p_arguments, buffer);
	if (!buffer_error.is_empty()) {
		return buffer_error;
	}

	bool changed = false;
	Dictionary current_state;
	String edit_error;
	const Error err = buffer.replace_text(p_arguments["text"], p_arguments.get("expected_revision", Variant()),
			p_arguments.get("expected_sha256", Variant()), changed, current_state, &edit_error);
	if (err == ERR_BUSY) {
		const Dictionary snapshot = buffer.get_snapshot();
		current_state["path"] = snapshot.get("path", p_arguments.get("path", String()));
		current_state["unsaved"] = snapshot.get("unsaved", false);
		return MCPToolUtils::make_error_result(STALE_REVISION_ERROR_CODE, edit_error, current_state);
	}
	if (err != OK) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", edit_error);
	}

	Dictionary result = buffer.get_snapshot();
	Array diagnostics;
	if (MCPScriptAnalysisSync::sync_snapshot(session_manager, session_id, result, &diagnostics, &analysis_error) != OK) {
		return MCPScriptAnalysisSync::make_failure(result, true, changed, false, analysis_error);
	}
	result["changed"] = changed;
	result["diagnostics"] = diagnostics;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPScriptProvider::save(const Dictionary &p_arguments, const Dictionary &p_context) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	allowed_arguments.push_back("nodePath");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}
	String session_id;
	Ref<GDScriptAnalysisSession> session;
	String analysis_error;
	if (!_resolve_analysis_context(p_context, session_id, session, analysis_error)) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", analysis_error);
	}
	MCPGDScriptTransientParserCleanup cleanup(session);

	MCPScriptBuffer buffer;
	const Dictionary buffer_error = _open_selected_buffer(p_arguments, buffer);
	if (!buffer_error.is_empty()) {
		return buffer_error;
	}
	const Dictionary before_save = buffer.get_snapshot();
	String save_error;
	const Error err = buffer.save(&save_error);
	if (err != OK) {
		return MCPToolUtils::make_error_result("SAVE_FAILED", save_error);
	}

	Dictionary result = buffer.get_snapshot();
	const bool changed = before_save.get("sha256", String()) != result.get("sha256", String()) ||
			int64_t(before_save.get("revision", -1)) != int64_t(result.get("revision", -1));
	if (MCPScriptAnalysisSync::sync_saved_snapshot(session_manager, session_id, result, &analysis_error) != OK) {
		return MCPScriptAnalysisSync::make_failure(result, true, changed, false, analysis_error);
	}
	return MCPToolUtils::make_success_result(result);
}
