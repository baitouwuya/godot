/**************************************************************************/
/*  mcp_gdscript_semantic_service.cpp                                    */
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

#include "mcp_gdscript_semantic_service.h"

#include "mcp_gdscript_result_builder.h"
#include "mcp_script_analysis_sync.h"
#include "mcp_workspace_edit_transaction.h"

#include "core/config/project_settings.h"
#include "core/object/script_language.h"

#include "modules/gdscript/language_server/gdscript_extend_parser.h"
#include "modules/gdscript/language_server/gdscript_workspace.h"

MCPGDScriptSemanticService::MCPGDScriptSemanticService(const Ref<MCPGDScriptSessionManager> &p_session_manager) :
		session_manager(p_session_manager) {
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::_success(const Dictionary &p_value) {
	OperationResult result;
	result.value = p_value;
	return result;
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::_failure(Error p_error, const String &p_error_code, const String &p_message, const Dictionary &p_details) {
	OperationResult result;
	result.error = p_error;
	result.error_code = p_error_code;
	result.message = p_message;
	result.details = p_details;
	return result;
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::_script_not_found(const String &p_path) {
	Dictionary details;
	details["path"] = p_path;
	return _failure(ERR_FILE_NOT_FOUND, "SCRIPT_NOT_FOUND", "GDScript could not be loaded: " + p_path, details);
}

Ref<GDScriptWorkspace> MCPGDScriptSemanticService::get_workspace() const {
	if (session_manager.is_null()) {
		return Ref<GDScriptWorkspace>();
	}
	const Ref<GDScriptAnalysisService> &service = session_manager->get_analysis_service();
	if (service.is_valid() && service->get_workspace().is_valid()) {
		return service->get_workspace();
	}
	const Ref<GDScriptAnalysisSession> &fallback_session = session_manager->get_fallback_session();
	return fallback_session.is_valid() ? fallback_session->get_workspace() : Ref<GDScriptWorkspace>();
}

String MCPGDScriptSemanticService::get_project_root() const {
	if (session_manager.is_valid()) {
		const Ref<GDScriptAnalysisService> &service = session_manager->get_analysis_service();
		if (service.is_valid() && service->get_project_root().is_absolute_path() && !service->get_project_root().contains("://")) {
			return service->get_project_root();
		}
	}
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	return project_settings ? project_settings->get_resource_path() : String();
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::validate_ready() const {
	if (session_manager.is_null() || session_manager->get_analysis_service().is_null()) {
		return _failure(ERR_UNCONFIGURED, "ANALYSIS_UNAVAILABLE", "A GDScript analysis service is required.");
	}
	if (get_workspace().is_null()) {
		return _failure(ERR_UNCONFIGURED, "ANALYSIS_UNAVAILABLE", "A configured GDScript workspace is required.");
	}
	return OperationResult();
}

bool MCPGDScriptSemanticService::_prepare_document(const String &p_session_id, const String &p_path, SyncPolicy p_sync_policy, PreparedDocument &r_document, Array *r_diagnostics, OperationResult &r_failure) const {
	r_document = PreparedDocument();
	if (r_diagnostics) {
		r_diagnostics->clear();
	}
	r_failure = validate_ready();
	if (!r_failure.is_ok()) {
		return false;
	}

	String error;
	Ref<GDScriptAnalysisSession> session;
	const Error session_error = session_manager->resolve_session(p_session_id, session, &error);
	if (session_error != OK) {
		r_failure = _failure(session_error, "ANALYSIS_UNAVAILABLE", error);
		return false;
	}
	if (p_sync_policy == SYNC_OPEN_BUFFERS_AND_TARGET) {
		const Error open_buffers_error = MCPScriptAnalysisSync::sync_open_buffers(session_manager, p_session_id, &error);
		if (open_buffers_error != OK) {
			r_failure = _failure(open_buffers_error, "ANALYSIS_UNAVAILABLE", error);
			return false;
		}
	}

	Dictionary snapshot;
	const Error sync_error = MCPScriptAnalysisSync::sync_authoritative_path(
			session_manager, p_session_id, p_path, get_project_root(), snapshot, r_diagnostics, &error);
	if (sync_error == ERR_FILE_NOT_FOUND) {
		r_failure = _script_not_found(p_path);
		return false;
	}
	if (sync_error == ERR_INVALID_PARAMETER || sync_error == ERR_PARAMETER_RANGE_ERROR) {
		r_failure = _failure(sync_error, "INVALID_ARGUMENTS", error);
		return false;
	}
	if (sync_error != OK) {
		r_failure = _failure(sync_error, "ANALYSIS_UNAVAILABLE", error);
		return false;
	}

	r_document.workspace = get_workspace();
	r_document.session = session;
	r_document.path = snapshot.get("path", p_path);
	return true;
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::diagnostics(const String &p_session_id, const String &p_path) const {
	PreparedDocument document;
	Array diagnostics;
	OperationResult failure;
	if (!_prepare_document(p_session_id, p_path, SYNC_TARGET_ONLY, document, &diagnostics, failure)) {
		return failure;
	}
	MCPGDScriptTransientParserCleanup cleanup(document.session);
	const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
	return _success(result_builder.diagnostics(diagnostics));
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::symbols(const String &p_session_id, const String &p_path) const {
	PreparedDocument document;
	OperationResult failure;
	if (!_prepare_document(p_session_id, p_path, SYNC_TARGET_ONLY, document, nullptr, failure)) {
		return failure;
	}
	MCPGDScriptTransientParserCleanup cleanup(document.session);
	if (!document.session->get_parse_result(document.path)) {
		return _script_not_found(document.path);
	}
	const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
	return _success(result_builder.document_symbols());
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::completion(const String &p_session_id, const String &p_path, const LSP::CompletionParams &p_params, int p_limit) const {
	PreparedDocument document;
	OperationResult failure;
	if (!_prepare_document(p_session_id, p_path, SYNC_OPEN_BUFFERS_AND_TARGET, document, nullptr, failure)) {
		return failure;
	}
	MCPGDScriptTransientParserCleanup cleanup(document.session);
	if (!document.session->get_parse_result(document.path)) {
		return _script_not_found(document.path);
	}
	List<ScriptLanguage::CodeCompletionOption> options;
	document.workspace->completion(document.session, p_params, &options);
	const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
	return _success(result_builder.completion(options, p_limit));
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::hover(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params) const {
	PreparedDocument document;
	OperationResult failure;
	if (!_prepare_document(p_session_id, p_path, SYNC_OPEN_BUFFERS_AND_TARGET, document, nullptr, failure)) {
		return failure;
	}
	MCPGDScriptTransientParserCleanup cleanup(document.session);
	if (!document.session->get_parse_result(document.path)) {
		return _script_not_found(document.path);
	}
	const LSP::DocumentSymbol *symbol = document.workspace->resolve_symbol(document.session, p_params);
	const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
	return _success(result_builder.hover(symbol));
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::definition(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params) const {
	PreparedDocument document;
	OperationResult failure;
	if (!_prepare_document(p_session_id, p_path, SYNC_OPEN_BUFFERS_AND_TARGET, document, nullptr, failure)) {
		return failure;
	}
	MCPGDScriptTransientParserCleanup cleanup(document.session);
	if (!document.session->get_parse_result(document.path)) {
		return _script_not_found(document.path);
	}
	const LSP::DocumentSymbol *symbol = document.workspace->resolve_symbol(document.session, p_params);
	const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
	return _success(result_builder.symbol_resolution(symbol, false));
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::declaration(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params) const {
	PreparedDocument document;
	OperationResult failure;
	if (!_prepare_document(p_session_id, p_path, SYNC_OPEN_BUFFERS_AND_TARGET, document, nullptr, failure)) {
		return failure;
	}
	MCPGDScriptTransientParserCleanup cleanup(document.session);
	if (!document.session->get_parse_result(document.path)) {
		return _script_not_found(document.path);
	}
	const LSP::DocumentSymbol *symbol = document.workspace->resolve_symbol(document.session, p_params);
	const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
	return _success(result_builder.symbol_resolution(symbol, true));
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::references(const String &p_session_id, const String &p_path, const LSP::ReferenceParams &p_params) const {
	PreparedDocument document;
	OperationResult failure;
	if (!_prepare_document(p_session_id, p_path, SYNC_OPEN_BUFFERS_AND_TARGET, document, nullptr, failure)) {
		return failure;
	}
	MCPGDScriptTransientParserCleanup cleanup(document.session);
	if (!document.session->get_parse_result(document.path)) {
		return _script_not_found(document.path);
	}

	const LSP::DocumentSymbol *symbol = document.workspace->resolve_symbol(document.session, p_params);
	if (!symbol) {
		const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
		return _success(result_builder.locations(Vector<LSP::Location>()));
	}
	Vector<LSP::Location> usages = document.workspace->find_all_usages(document.session, *symbol);
	if (!p_params.context.includeDeclaration) {
		Vector<LSP::Location> filtered;
		for (const LSP::Location &usage : usages) {
			if (!document.workspace->is_declaration_location(*symbol, usage)) {
				filtered.push_back(usage);
			}
		}
		usages = filtered;
	}
	const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
	return _success(result_builder.locations(usages, symbol));
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::signature_help(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params) const {
	PreparedDocument document;
	OperationResult failure;
	if (!_prepare_document(p_session_id, p_path, SYNC_OPEN_BUFFERS_AND_TARGET, document, nullptr, failure)) {
		return failure;
	}
	MCPGDScriptTransientParserCleanup cleanup(document.session);
	if (!document.session->get_parse_result(document.path)) {
		return _script_not_found(document.path);
	}
	LSP::SignatureHelp signature;
	const Error error = document.workspace->resolve_signature(document.session, p_params, signature);
	const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
	return _success(result_builder.signature_help(error == OK ? &signature : nullptr));
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::rename(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params, const String &p_new_name) const {
	PreparedDocument document;
	OperationResult failure;
	if (!_prepare_document(p_session_id, p_path, SYNC_OPEN_BUFFERS_AND_TARGET, document, nullptr, failure)) {
		return failure;
	}
	MCPGDScriptTransientParserCleanup cleanup(document.session);
	if (!document.session->get_parse_result(document.path)) {
		return _script_not_found(document.path);
	}
	const Dictionary edit = document.workspace->rename(document.session, p_params, p_new_name);
	const MCPGDScriptResultBuilder result_builder(document.workspace, document.session, document.path);
	return _success(result_builder.rename(edit));
}

MCPGDScriptSemanticService::OperationResult MCPGDScriptSemanticService::apply_workspace_edit(const String &p_session_id, const Dictionary &p_edit, const Array &p_documents) const {
	OperationResult readiness = validate_ready();
	if (!readiness.is_ok()) {
		return readiness;
	}

	String error;
	Ref<GDScriptAnalysisSession> session;
	const Error session_error = session_manager->resolve_session(p_session_id, session, &error);
	if (session_error != OK) {
		return _failure(session_error, "ANALYSIS_UNAVAILABLE", error);
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	const Error open_buffers_error = MCPScriptAnalysisSync::sync_open_buffers(session_manager, p_session_id, &error);
	if (open_buffers_error != OK) {
		return _failure(open_buffers_error, "ANALYSIS_UNAVAILABLE", error);
	}

	Dictionary result;
	String error_code;
	const Error apply_error = MCPWorkspaceEditTransaction::apply(
			p_edit, p_documents, get_workspace(), session_manager, p_session_id, result, error_code, error);
	if (apply_error != OK) {
		return _failure(apply_error, error_code.is_empty() ? "WORKSPACE_EDIT_FAILED" : error_code, error, result);
	}
	return _success(result);
}
