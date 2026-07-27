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
#include "mcp_script_tool_utils.h"
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

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
			{ "godot.script.create", "Create and open an external GDScript.",
					MCPScriptToolUtils::create_schema(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPScriptProvider::create), MCPScriptToolUtils::create_output_schema() },
			{ "godot.script.open", "Open an external or built-in GDScript in the Script editor.",
					MCPScriptToolUtils::selector_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPScriptProvider::open), MCPScriptToolUtils::open_output_schema() },
			{ "godot.script.get", "Read a GDScript as documentation, a member, or full source.",
					MCPScriptToolUtils::get_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPScriptProvider::get), MCPScriptToolUtils::get_output_schema() },
			{ "godot.script.usages", "Find semantic LSP usages of a named GDScript member.",
					MCPScriptToolUtils::usages_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPScriptProvider::usages), MCPScriptToolUtils::usages_output_schema() },
			{ "godot.script.edit", "Replace a Script editor buffer with optimistic concurrency.",
					MCPScriptToolUtils::edit_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPScriptProvider::edit), MCPScriptToolUtils::edit_output_schema() },
			{ "godot.script.save", "Explicitly save an authoritative Script editor buffer.",
					MCPScriptToolUtils::selector_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPScriptProvider::save), MCPScriptToolUtils::save_output_schema() },
	};
	const Error err = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (err != OK) {
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
	String view;
	bool include_comments = true;
	Dictionary member;
	String argument_error;
	if (!MCPScriptToolUtils::parse_script_view(p_arguments, view, include_comments, member, argument_error)) {
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
	const Variant member_value = p_arguments.get("member", Variant());
	const Variant include_declaration_value = p_arguments.get("includeDeclaration", false);
	if (member_value.get_type() != Variant::DICTIONARY || Dictionary(member_value).is_empty() || include_declaration_value.get_type() != Variant::BOOL) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty member object and optional boolean includeDeclaration are required.");
	}
	const Dictionary member = member_value;
	String argument_error;
	if (!MCPScriptToolUtils::validate_member_query(member, argument_error)) {
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
