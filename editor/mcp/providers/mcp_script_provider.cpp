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
#include "mcp_tool_utils.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "editor/file_system/editor_file_system.h"

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
	properties["path"] = _property_schema("string", "External GDScript path beginning with res://.");
	PackedStringArray required;
	required.push_back("path");
	return _object_schema(properties, required);
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

static Dictionary _edit_schema() {
	Dictionary properties;
	properties["path"] = _property_schema("string", "External GDScript path beginning with res://.");
	properties["text"] = _property_schema("string", "Complete replacement source text.");

	Dictionary expected_revision = _property_schema("integer", "TextEdit revision returned by godot.script.get.");
	expected_revision["minimum"] = 0;
	expected_revision["maximum"] = int64_t(0xFFFFFFFFLL);
	properties["expected_revision"] = expected_revision;

	Dictionary expected_sha256 = _property_schema("string", "SHA-256 returned by godot.script.get.");
	expected_sha256["pattern"] = "^[0-9a-fA-F]{64}$";
	properties["expected_sha256"] = expected_sha256;

	PackedStringArray required;
	required.push_back("path");
	required.push_back("text");
	Dictionary schema = _object_schema(properties, required);

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
	schema["anyOf"] = any_of;
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

bool MCPScriptProvider::_resolve_analysis_context(const Dictionary &p_context, String &r_session_id, String &r_error) {
	if (session_manager.is_null()) {
		r_error = "A GDScript analysis session manager is required.";
		return false;
	}
	Ref<GDScriptAnalysisSession> session;
	return session_manager->resolve_context(p_context, r_session_id, session, &r_error) == OK;
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
			callable_mp(this, &MCPScriptProvider::create), MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.script.get", "Read the authoritative Script editor buffer.", _path_schema()),
				callable_mp(this, &MCPScriptProvider::get), MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.script.edit", "Replace a Script editor buffer with optimistic concurrency.", _edit_schema()),
				callable_mp(this, &MCPScriptProvider::edit), MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
	}
	if (err == OK) {
		err = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.script.save", "Explicitly save an authoritative Script editor buffer.", _path_schema()),
				callable_mp(this, &MCPScriptProvider::save), MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
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
	String analysis_error;
	if (!_resolve_analysis_context(p_context, session_id, analysis_error)) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", analysis_error);
	}

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
	if (MCPScriptAnalysisSync::sync_snapshot(session_manager, session_id, result, diagnostics, &analysis_error) != OK) {
		return MCPScriptAnalysisSync::make_failure(result, true, true, true, analysis_error);
	}
	result["created"] = true;
	result["diagnostics"] = diagnostics;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPScriptProvider::get(const Dictionary &p_arguments, const Dictionary &p_context) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}
	String path;
	if (!_get_required_string(p_arguments, "path", path)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string path is required.");
	}
	String session_id;
	String analysis_error;
	if (!_resolve_analysis_context(p_context, session_id, analysis_error)) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", analysis_error);
	}

	MCPScriptBuffer buffer;
	const Dictionary buffer_error = _open_buffer(path, buffer);
	if (!buffer_error.is_empty()) {
		return buffer_error;
	}
	Dictionary result = buffer.get_snapshot();
	Array diagnostics;
	if (MCPScriptAnalysisSync::sync_snapshot(session_manager, session_id, result, diagnostics, &analysis_error) != OK) {
		return MCPScriptAnalysisSync::make_failure(result, false, false, false, analysis_error);
	}
	result["diagnostics"] = diagnostics;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPScriptProvider::edit(const Dictionary &p_arguments, const Dictionary &p_context) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	allowed_arguments.push_back("text");
	allowed_arguments.push_back("expected_revision");
	allowed_arguments.push_back("expected_sha256");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

	String path;
	if (!_get_required_string(p_arguments, "path", path) || p_arguments.get("text", Variant()).get_type() != Variant::STRING) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty path and string text are required.");
	}
	String session_id;
	String analysis_error;
	if (!_resolve_analysis_context(p_context, session_id, analysis_error)) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", analysis_error);
	}

	MCPScriptBuffer buffer;
	const Dictionary buffer_error = _open_buffer(path, buffer);
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
		current_state["path"] = snapshot.get("path", path);
		current_state["unsaved"] = snapshot.get("unsaved", false);
		return MCPToolUtils::make_error_result(STALE_REVISION_ERROR_CODE, edit_error, current_state);
	}
	if (err != OK) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", edit_error);
	}

	Dictionary result = buffer.get_snapshot();
	Array diagnostics;
	if (MCPScriptAnalysisSync::sync_snapshot(session_manager, session_id, result, diagnostics, &analysis_error) != OK) {
		return MCPScriptAnalysisSync::make_failure(result, true, changed, false, analysis_error);
	}
	result["changed"] = changed;
	result["diagnostics"] = diagnostics;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPScriptProvider::save(const Dictionary &p_arguments, const Dictionary &p_context) {
	PackedStringArray allowed_arguments;
	allowed_arguments.push_back("path");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed_arguments, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}
	String path;
	if (!_get_required_string(p_arguments, "path", path)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string path is required.");
	}
	String session_id;
	String analysis_error;
	if (!_resolve_analysis_context(p_context, session_id, analysis_error)) {
		return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", analysis_error);
	}

	MCPScriptBuffer buffer;
	const Dictionary buffer_error = _open_buffer(path, buffer);
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
