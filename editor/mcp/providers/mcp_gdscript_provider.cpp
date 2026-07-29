/**************************************************************************/
/*  mcp_gdscript_provider.cpp                                             */
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
/* INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,        */
/* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL */
/* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR    */
/* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,  */
/* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR  */
/* OTHER DEALINGS IN THE SOFTWARE.                                       */
/**************************************************************************/

#include "mcp_gdscript_provider.h"

#include "mcp_gdscript_request_parser.h"
#include "mcp_gdscript_tool_utils.h"
#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPGDScriptProvider::MCPGDScriptProvider(const Ref<MCPGDScriptSessionManager> &p_session_manager) :
		semantic_service(p_session_manager) {
}

MCPGDScriptProvider::~MCPGDScriptProvider() {
	unregister_tools();
}

MCPGDScriptRequestParser MCPGDScriptProvider::_request_parser() const {
	return MCPGDScriptRequestParser(semantic_service.get_workspace(), semantic_service.get_project_root());
}

bool MCPGDScriptProvider::_validate_ready(Dictionary &r_error_result) const {
	const MCPGDScriptSemanticService::OperationResult result = semantic_service.validate_ready();
	if (result.is_ok()) {
		return true;
	}
	r_error_result = _tool_result(result);
	return false;
}

bool MCPGDScriptProvider::_parse_session_id(const Dictionary &p_context, String &r_session_id, Dictionary &r_error_result) const {
	String error;
	if (MCPToolUtils::parse_session_id(p_context, r_session_id, &error) == OK) {
		return true;
	}
	r_error_result = _invalid_arguments(error);
	return false;
}

Dictionary MCPGDScriptProvider::_tool_result(const MCPGDScriptSemanticService::OperationResult &p_result) const {
	return p_result.is_ok() ? MCPToolUtils::make_success_result(p_result.value) : MCPToolUtils::make_error_result(p_result.error_code, p_result.message, p_result.details);
}

Dictionary MCPGDScriptProvider::_invalid_arguments(const String &p_message) const {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", p_message);
}

Dictionary MCPGDScriptProvider::diagnostics(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	String path;
	String argument_error;
	if (_request_parser().parse_document_path(p_arguments, path, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.diagnostics(session_id, path));
}

Dictionary MCPGDScriptProvider::symbols(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	String path;
	String argument_error;
	if (_request_parser().parse_document_path(p_arguments, path, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.symbols(session_id, path));
}

Dictionary MCPGDScriptProvider::completion(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	MCPGDScriptRequestParser::CompletionRequest request;
	String argument_error;
	if (_request_parser().parse_completion(p_arguments, request, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.completion(session_id, request.path, request.params, request.limit));
}

Dictionary MCPGDScriptProvider::hover(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	String argument_error;
	if (_request_parser().parse_position(p_arguments, path, params, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.hover(session_id, path, params));
}

Dictionary MCPGDScriptProvider::definition(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	String argument_error;
	if (_request_parser().parse_position(p_arguments, path, params, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.definition(session_id, path, params));
}

Dictionary MCPGDScriptProvider::declaration(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	String argument_error;
	if (_request_parser().parse_position(p_arguments, path, params, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.declaration(session_id, path, params));
}

Dictionary MCPGDScriptProvider::references(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	String path;
	LSP::ReferenceParams params;
	String argument_error;
	if (_request_parser().parse_reference_position(p_arguments, path, params, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.references(session_id, path, params));
}

Dictionary MCPGDScriptProvider::signature_help(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	String argument_error;
	if (_request_parser().parse_position(p_arguments, path, params, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.signature_help(session_id, path, params));
}

Dictionary MCPGDScriptProvider::rename(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	MCPGDScriptRequestParser::RenameRequest request;
	String argument_error;
	if (_request_parser().parse_rename(p_arguments, request, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.rename(session_id, request.path, request.params, request.new_name));
}

Dictionary MCPGDScriptProvider::apply_workspace_edit(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error_result;
	if (!_validate_ready(error_result)) {
		return error_result;
	}
	MCPGDScriptRequestParser::WorkspaceEditRequest request;
	String argument_error;
	if (_request_parser().parse_workspace_edit(p_arguments, request, &argument_error) != OK) {
		return _invalid_arguments(argument_error);
	}
	String session_id;
	if (!_parse_session_id(p_context, session_id, error_result)) {
		return error_result;
	}
	return _tool_result(semantic_service.apply_workspace_edit(session_id, request.edit, request.documents));
}

Error MCPGDScriptProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "GDScript tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.gdscript.diagnostics", "Return diagnostics for a GDScript document.",
				MCPGDScriptToolUtils::diagnostics_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::diagnostics), MCPGDScriptToolUtils::diagnostics_output_schema() },
		{ "godot.gdscript.symbols", "Return document symbols for a GDScript document.",
				MCPGDScriptToolUtils::diagnostics_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::symbols), MCPGDScriptToolUtils::symbols_output_schema() },
		{ "godot.gdscript.completion", "Return bounded UTF-16 positioned GDScript completions.",
				MCPGDScriptToolUtils::completion_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::completion), MCPGDScriptToolUtils::completion_output_schema() },
		{ "godot.gdscript.hover", "Return GDScript hover information at a position.",
				MCPGDScriptToolUtils::position_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::hover), MCPGDScriptToolUtils::hover_output_schema() },
		{ "godot.gdscript.definition", "Resolve the GDScript definition at a position.",
				MCPGDScriptToolUtils::position_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::definition), MCPGDScriptToolUtils::locations_output_schema(true) },
		{ "godot.gdscript.declaration", "Resolve the GDScript declaration at a position.",
				MCPGDScriptToolUtils::position_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::declaration), MCPGDScriptToolUtils::locations_output_schema(true) },
		{ "godot.gdscript.references", "Find GDScript references at a position.",
				MCPGDScriptToolUtils::references_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::references), MCPGDScriptToolUtils::locations_output_schema(false) },
		{ "godot.gdscript.signature_help", "Return GDScript signature help at a call site.",
				MCPGDScriptToolUtils::position_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::signature_help), MCPGDScriptToolUtils::signature_output_schema() },
		{ "godot.gdscript.rename", "Return a UTF-16 GDScript workspace edit for a symbol.",
				MCPGDScriptToolUtils::rename_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::rename), MCPGDScriptToolUtils::rename_output_schema() },
		{ "godot.gdscript.apply_workspace_edit", "Validate a UTF-16 WorkspaceEdit, then apply it to authoritative ScriptEditor buffers without saving.",
				MCPGDScriptToolUtils::workspace_edit_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPGDScriptProvider::apply_workspace_edit), MCPGDScriptToolUtils::workspace_edit_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPGDScriptProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}
