/**************************************************************************/
/*  mcp_gdscript_semantic_service.h                                      */
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

#pragma once

#include "mcp_gdscript_session_manager.h"

#include "core/error/error_list.h"
#include "core/variant/dictionary.h"

#include "modules/gdscript/language_server/godot_lsp.h"

class GDScriptWorkspace;

class MCPGDScriptSemanticService {
public:
	struct OperationResult {
		Error error = OK;
		String error_code;
		String message;
		Dictionary value;
		Dictionary details;

		bool is_ok() const { return error == OK; }
	};

	explicit MCPGDScriptSemanticService(const Ref<MCPGDScriptSessionManager> &p_session_manager = Ref<MCPGDScriptSessionManager>());

	OperationResult validate_ready() const;
	Ref<GDScriptWorkspace> get_workspace() const;
	String get_project_root() const;

	OperationResult diagnostics(const String &p_session_id, const String &p_path) const;
	OperationResult symbols(const String &p_session_id, const String &p_path) const;
	OperationResult completion(const String &p_session_id, const String &p_path, const LSP::CompletionParams &p_params, int p_limit) const;
	OperationResult hover(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params) const;
	OperationResult definition(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params) const;
	OperationResult declaration(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params) const;
	OperationResult references(const String &p_session_id, const String &p_path, const LSP::ReferenceParams &p_params) const;
	OperationResult signature_help(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params) const;
	OperationResult rename(const String &p_session_id, const String &p_path, const LSP::TextDocumentPositionParams &p_params, const String &p_new_name) const;
	OperationResult apply_workspace_edit(const String &p_session_id, const Dictionary &p_edit, const Array &p_documents) const;

private:
	enum SyncPolicy {
		SYNC_TARGET_ONLY,
		SYNC_OPEN_BUFFERS_AND_TARGET,
	};

	struct PreparedDocument {
		Ref<GDScriptWorkspace> workspace;
		Ref<GDScriptAnalysisSession> session;
		String path;
	};

	Ref<MCPGDScriptSessionManager> session_manager;

	static OperationResult _success(const Dictionary &p_value);
	static OperationResult _failure(Error p_error, const String &p_error_code, const String &p_message, const Dictionary &p_details = Dictionary());
	static OperationResult _script_not_found(const String &p_path);
	bool _prepare_document(const String &p_session_id, const String &p_path, SyncPolicy p_sync_policy, PreparedDocument &r_document, Array *r_diagnostics, OperationResult &r_failure) const;
};
