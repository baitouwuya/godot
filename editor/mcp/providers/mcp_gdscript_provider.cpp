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
#include "mcp_gdscript_result_builder.h"
#include "mcp_gdscript_tool_utils.h"
#include "mcp_script_analysis_sync.h"
#include "mcp_tool_utils.h"
#include "mcp_workspace_edit_transaction.h"

#include "core/config/project_settings.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "core/object/script_language.h"

namespace {

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPGDScriptProvider::MCPGDScriptProvider(const Ref<GDScriptAnalysisService> &p_service, const Ref<GDScriptAnalysisSession> &p_fallback_session) {
	session_manager = memnew(MCPGDScriptSessionManager(p_service, p_fallback_session));
}

MCPGDScriptProvider::MCPGDScriptProvider(const Ref<MCPGDScriptSessionManager> &p_session_manager) {
	set_session_manager(p_session_manager);
}

MCPGDScriptProvider::~MCPGDScriptProvider() {
	unregister_tools();
}

void MCPGDScriptProvider::set_analysis_service(const Ref<GDScriptAnalysisService> &p_service) {
	if (session_manager.is_null()) {
		session_manager = memnew(MCPGDScriptSessionManager);
	}
	session_manager->set_analysis_service(p_service);
}

void MCPGDScriptProvider::set_analysis_session(const Ref<GDScriptAnalysisSession> &p_session) {
	if (session_manager.is_null()) {
		session_manager = memnew(MCPGDScriptSessionManager);
	}
	session_manager->set_fallback_session(p_session);
}

void MCPGDScriptProvider::set_session_manager(const Ref<MCPGDScriptSessionManager> &p_session_manager) {
	session_manager = p_session_manager;
}

Ref<GDScriptWorkspace> MCPGDScriptProvider::_get_workspace() const {
	if (session_manager.is_null()) {
		return Ref<GDScriptWorkspace>();
	}
	const Ref<GDScriptAnalysisService> &service = session_manager->get_analysis_service();
	if (service.is_valid() && service->get_workspace().is_valid()) {
		return service->get_workspace();
	}
	const Ref<GDScriptAnalysisSession> &fallback_session = session_manager->get_fallback_session();
	if (fallback_session.is_valid()) {
		return fallback_session->get_workspace();
	}
	return Ref<GDScriptWorkspace>();
}

String MCPGDScriptProvider::_get_project_root() const {
	if (session_manager.is_valid()) {
		const Ref<GDScriptAnalysisService> &service = session_manager->get_analysis_service();
		if (service.is_valid() && service->get_project_root().is_absolute_path() && !service->get_project_root().contains("://")) {
			return service->get_project_root();
		}
	}
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	return project_settings ? project_settings->get_resource_path() : String();
}

bool MCPGDScriptProvider::_is_ready(String &r_error) const {
	if (session_manager.is_null() || session_manager->get_analysis_service().is_null()) {
		r_error = "A GDScript analysis service is required.";
		return false;
	}
	if (_get_workspace().is_null()) {
		r_error = "A configured GDScript workspace is required.";
		return false;
	}
	return true;
}

bool MCPGDScriptProvider::_validate_ready(String &r_error, Dictionary &r_error_result) const {
	if (!_is_ready(r_error)) {
		r_error_result = _unavailable(r_error);
		return false;
	}
	return true;
}

bool MCPGDScriptProvider::_resolve_session(const Dictionary &p_context, String &r_session_id, Ref<GDScriptAnalysisSession> &r_session, String &r_error) {
	if (!_is_ready(r_error)) {
		return false;
	}
	return session_manager->resolve_context(p_context, r_session_id, r_session, &r_error) == OK;
}

bool MCPGDScriptProvider::_prepare_document(const Dictionary &p_context, const String &p_path, bool p_sync_open_buffers, Ref<GDScriptAnalysisSession> &r_session, Array *r_diagnostics, Dictionary &r_error_result) {
	String session_id;
	String error;
	if (!_resolve_session(p_context, session_id, r_session, error)) {
		r_error_result = _unavailable(error);
		return false;
	}
	if (p_sync_open_buffers && MCPScriptAnalysisSync::sync_open_buffers(session_manager, session_id, &error) != OK) {
		r_error_result = _unavailable(error);
		return false;
	}

	Dictionary snapshot;
	const Error sync_error = MCPScriptAnalysisSync::sync_authoritative_path(
			session_manager, session_id, p_path, _get_project_root(), snapshot, r_diagnostics, &error);
	if (sync_error == ERR_FILE_NOT_FOUND) {
		r_error_result = _script_not_found(p_path);
		return false;
	}
	if (sync_error != OK) {
		r_error_result = _unavailable(error);
		return false;
	}
	return true;
}

bool MCPGDScriptProvider::_parse_path(const Dictionary &p_arguments, String &r_path, String &r_error) const {
	const MCPGDScriptRequestParser parser(_get_workspace(), _get_project_root());
	return parser.parse_document_path(p_arguments, r_path, &r_error) == OK;
}

bool MCPGDScriptProvider::_parse_position(const Dictionary &p_arguments, String &r_path, LSP::TextDocumentPositionParams &r_params, String &r_error) const {
	const MCPGDScriptRequestParser parser(_get_workspace(), _get_project_root());
	return parser.parse_position(p_arguments, r_path, r_params, &r_error) == OK;
}

bool MCPGDScriptProvider::_parse_reference_position(const Dictionary &p_arguments, String &r_path, LSP::ReferenceParams &r_params, String &r_error) const {
	const MCPGDScriptRequestParser parser(_get_workspace(), _get_project_root());
	return parser.parse_reference_position(p_arguments, r_path, r_params, &r_error) == OK;
}

Dictionary MCPGDScriptProvider::_invalid_arguments(const String &p_message) const {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", p_message);
}

Dictionary MCPGDScriptProvider::_unavailable(const String &p_message) const {
	return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", p_message);
}

Dictionary MCPGDScriptProvider::_script_not_found(const String &p_path) const {
	Dictionary details;
	details["path"] = p_path;
	return MCPToolUtils::make_error_result("SCRIPT_NOT_FOUND", "GDScript could not be loaded: " + p_path, details);
}

Dictionary MCPGDScriptProvider::diagnostics(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	if (!_parse_path(p_arguments, path, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Array diagnostic_list;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, false, session, &diagnostic_list, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
	return MCPToolUtils::make_success_result(result_builder.diagnostics(diagnostic_list));
}

Dictionary MCPGDScriptProvider::symbols(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	if (!_parse_path(p_arguments, path, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, false, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	ExtendGDScriptParser *parser = session->get_parse_result(path);
	if (!parser) {
		return _script_not_found(path);
	}
	const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
	return MCPToolUtils::make_success_result(result_builder.document_symbols());
}

Dictionary MCPGDScriptProvider::completion(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams position;
	if (!_parse_position(p_arguments, path, position, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	int limit = 0;
	if (!MCPGDScriptToolUtils::get_completion_limit(p_arguments, limit, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	LSP::CompletionParams params;
	params.textDocument = position.textDocument;
	params.position = position.position;
	List<ScriptLanguage::CodeCompletionOption> options;
	_get_workspace()->completion(session, params, &options);
	const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
	return MCPToolUtils::make_success_result(result_builder.completion(options, limit));
}

Dictionary MCPGDScriptProvider::hover(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	const LSP::DocumentSymbol *symbol = _get_workspace()->resolve_symbol(session, params);
	const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
	return MCPToolUtils::make_success_result(result_builder.hover(symbol));
}

Dictionary MCPGDScriptProvider::definition(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	const LSP::DocumentSymbol *symbol = _get_workspace()->resolve_symbol(session, params);
	const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
	return MCPToolUtils::make_success_result(result_builder.symbol_resolution(symbol, false));
}

Dictionary MCPGDScriptProvider::declaration(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	const LSP::DocumentSymbol *symbol = _get_workspace()->resolve_symbol(session, params);
	const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
	return MCPToolUtils::make_success_result(result_builder.symbol_resolution(symbol, true));
}

Dictionary MCPGDScriptProvider::references(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::ReferenceParams params;
	if (!_parse_reference_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	const LSP::DocumentSymbol *symbol = _get_workspace()->resolve_symbol(session, params);
	if (!symbol) {
		const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
		return MCPToolUtils::make_success_result(result_builder.locations(Vector<LSP::Location>()));
	}
	Vector<LSP::Location> usages = _get_workspace()->find_all_usages(session, *symbol);
	if (!params.context.includeDeclaration) {
		Vector<LSP::Location> filtered;
		for (const LSP::Location &usage : usages) {
			if (_get_workspace()->is_declaration_location(*symbol, usage)) {
				continue;
			}
			filtered.push_back(usage);
		}
		usages = filtered;
	}
	const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
	return MCPToolUtils::make_success_result(result_builder.locations(usages, symbol));
}

Dictionary MCPGDScriptProvider::signature_help(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	LSP::SignatureHelp signature;
	const Error error = _get_workspace()->resolve_signature(session, params, signature);
	const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
	return MCPToolUtils::make_success_result(result_builder.signature_help(error == OK ? &signature : nullptr));
}

Dictionary MCPGDScriptProvider::rename(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	String new_name;
	if (!MCPGDScriptToolUtils::get_string_argument(p_arguments, "newName", new_name, argument_error) || new_name.is_empty()) {
		return _invalid_arguments(new_name.is_empty() ? "newName must not be empty." : argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	const Dictionary edit = _get_workspace()->rename(session, params, new_name);
	const MCPGDScriptResultBuilder result_builder(_get_workspace(), session, path);
	return MCPToolUtils::make_success_result(result_builder.rename(edit));
}

Dictionary MCPGDScriptProvider::apply_workspace_edit(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	const Variant edit_value = p_arguments.get("edit", Variant());
	const Variant documents_value = p_arguments.get("documents", Variant());
	if (edit_value.get_type() != Variant::DICTIONARY || documents_value.get_type() != Variant::ARRAY) {
		return _invalid_arguments("edit must be a WorkspaceEdit object and documents must be an array.");
	}

	String session_id;
	Ref<GDScriptAnalysisSession> session;
	if (!_resolve_session(p_context, session_id, session, argument_error)) {
		return _unavailable(argument_error);
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (MCPScriptAnalysisSync::sync_open_buffers(session_manager, session_id, &argument_error) != OK) {
		return _unavailable(argument_error);
	}

	Dictionary result;
	String error_code;
	const Error error = MCPWorkspaceEditTransaction::apply(edit_value, documents_value, _get_workspace(), session_manager, session_id, result, error_code, argument_error);
	if (error != OK) {
		return MCPToolUtils::make_error_result(error_code.is_empty() ? "WORKSPACE_EDIT_FAILED" : error_code, argument_error, result);
	}
	return MCPToolUtils::make_success_result(result);
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

void MCPGDScriptProvider::on_session_removed(const String &p_session_id) {
	release_session(p_session_id);
}

void MCPGDScriptProvider::shutdown() {
	clear_sessions();
}
