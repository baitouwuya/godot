/**************************************************************************/
/*  mcp_gdscript_result_builder.cpp                                      */
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

#include "mcp_gdscript_result_builder.h"

#include "mcp_gdscript_tool_utils.h"

#include "modules/gdscript/language_server/gdscript_extend_parser.h"

MCPGDScriptResultBuilder::MCPGDScriptResultBuilder(const Ref<GDScriptWorkspace> &p_workspace, const Ref<GDScriptAnalysisSession> &p_session, const String &p_path) :
		workspace(p_workspace),
		session(p_session),
		path(p_path) {
}

Dictionary MCPGDScriptResultBuilder::metadata() const {
	Dictionary result;
	result["path"] = path;
	if (workspace.is_valid()) {
		result["uri"] = workspace->get_file_uri(path);
	}
	const GDScriptAnalysisSession::DocumentState *document = session->get_document(path);
	result["analysisRevision"] = document ? document->revision : session->get_revision();
	if (document) {
		if (document->source_state == GDScriptAnalysisSession::SOURCE_STATE_OPEN_DOCUMENT && document->client_version >= 0) {
			result["revision"] = document->client_version;
		}
		result["sha256"] = document->sha256;
		result["sourceState"] = document->source_state == GDScriptAnalysisSession::SOURCE_STATE_OPEN_DOCUMENT ? "open" : "disk";
		result["clientVersion"] = document->client_version;
	}
	return result;
}

Dictionary MCPGDScriptResultBuilder::diagnostics(const Array &p_diagnostics) const {
	Dictionary result = metadata();
	result["diagnostics"] = p_diagnostics;
	return result;
}

Dictionary MCPGDScriptResultBuilder::document_symbols() const {
	Dictionary result = metadata();
	Array symbols;
	ExtendGDScriptParser *parser = session->get_parse_result(path);
	if (parser) {
		symbols.push_back(parser->get_symbols().to_json(true));
	}
	result["symbols"] = symbols;
	return result;
}

Dictionary MCPGDScriptResultBuilder::completion(const List<ScriptLanguage::CodeCompletionOption> &p_options, int p_limit) const {
	Array items;
	items.resize(MIN(p_options.size(), p_limit));
	ExtendGDScriptParser *parser = session->get_parse_result(path);
	int index = 0;
	for (const ScriptLanguage::CodeCompletionOption &option : p_options) {
		if (index >= p_limit) {
			break;
		}
		LSP::CompletionItem item;
		item.label = option.display;
		item.insertText = option.insert_text;
		item.kind = MCPGDScriptToolUtils::completion_kind(option.kind);
		if (option.text_edit.is_set()) {
			item.textEdit.newText = option.text_edit.new_text;
			if (parser) {
				item.textEdit.range = parser->to_lsp_range(option.text_edit.start_line, option.text_edit.start_column, option.text_edit.end_line, option.text_edit.end_column);
			} else {
				item.textEdit.range = LSP::Range(option.text_edit.start_line, option.text_edit.start_column, option.text_edit.end_line, option.text_edit.end_column);
			}
		}
		items[index++] = item.to_json();
	}

	Dictionary result = metadata();
	result["items"] = items;
	result["totalItemCount"] = p_options.size();
	result["isIncomplete"] = p_options.size() > p_limit;
	return result;
}

Dictionary MCPGDScriptResultBuilder::hover(const LSP::DocumentSymbol *p_symbol) const {
	Dictionary result = metadata();
	if (p_symbol) {
		LSP::Hover hover_result;
		hover_result.contents = p_symbol->render();
		hover_result.range = p_symbol->selectionRange;
		result["hover"] = hover_result.to_json();
		result["symbol"] = p_symbol->to_json(true);
		result["found"] = true;
	} else {
		result["hover"] = Variant();
		result["found"] = false;
	}
	return result;
}

Dictionary MCPGDScriptResultBuilder::symbol_resolution(const LSP::DocumentSymbol *p_symbol, bool p_declaration) const {
	Dictionary result = metadata();
	Array resolved_locations;
	if (p_symbol && !p_symbol->uri.is_empty()) {
		LSP::Location location;
		location.uri = p_symbol->uri;
		location.range = p_symbol->selectionRange;
		resolved_locations.push_back(location.to_json());
	}
	result["locations"] = resolved_locations;
	result["found"] = !resolved_locations.is_empty();
	if (p_symbol) {
		result["symbol"] = p_symbol->to_json(true);
		result["native"] = !p_symbol->native_class.is_empty();
	}
	result["operation"] = p_declaration ? "declaration" : "definition";
	return result;
}

Dictionary MCPGDScriptResultBuilder::locations(const Vector<LSP::Location> &p_locations, const LSP::DocumentSymbol *p_symbol) const {
	Dictionary result = metadata();
	Array serialized_locations;
	for (const LSP::Location &location : p_locations) {
		serialized_locations.push_back(location.to_json());
	}
	result["locations"] = serialized_locations;
	result["found"] = !serialized_locations.is_empty();
	if (p_symbol) {
		result["symbol"] = p_symbol->to_json(true);
	}
	return result;
}

Dictionary MCPGDScriptResultBuilder::signature_help(const LSP::SignatureHelp *p_signature) const {
	Dictionary result = metadata();
	result["signatureHelp"] = p_signature ? Variant(p_signature->to_json()) : Variant();
	result["found"] = p_signature != nullptr;
	return result;
}

Dictionary MCPGDScriptResultBuilder::rename(const Dictionary &p_edit) const {
	Dictionary result = metadata();
	result["edit"] = p_edit;
	const Dictionary changes = p_edit.get("changes", Dictionary());
	result["changed"] = !changes.is_empty();
	return result;
}
