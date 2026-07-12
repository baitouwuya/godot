/**************************************************************************/
/*  gdscript_analysis_session.cpp                                         */
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
/* "Software"), to deal in the Software without restriction, including    */
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

#include "gdscript_analysis_session.h"

#include "gdscript_extend_parser.h"
#include "gdscript_workspace.h"

#include "core/io/file_access.h"

static int _utf16_code_units(char32_t p_character) {
	return p_character > 0xffff ? 2 : 1;
}

uint64_t GDScriptAnalysisSession::_advance_revision() {
	revision++;
	return revision;
}

void GDScriptAnalysisSession::_remove_cached_parser(const String &p_path) {
	HashMap<String, ExtendGDScriptParser *>::Iterator cached = parser_cache.find(p_path);
	if (cached) {
		memdelete(cached->value);
		parser_cache.remove(cached);
	}
	transient_sources.erase(p_path);
}

bool GDScriptAnalysisSession::_get_line_bounds(const String &p_text, int p_line, int &r_start, int &r_end) {
	const int target_line = MAX(0, p_line);
	int current_line = 0;
	int line_start = 0;

	for (int i = 0; i < p_text.length(); i++) {
		if (p_text[i] != '\n') {
			continue;
		}
		if (current_line == target_line) {
			r_start = line_start;
			r_end = i > line_start && p_text[i - 1] == '\r' ? i - 1 : i;
			return true;
		}
		current_line++;
		line_start = i + 1;
	}

	if (current_line == target_line) {
		r_start = line_start;
		r_end = p_text.length();
		return true;
	}

	r_start = p_text.length();
	r_end = p_text.length();
	return false;
}

Error GDScriptAnalysisSession::open_document(const String &p_path, const String &p_text, LSP::LanguageId p_language_id, int64_t p_client_version) {
	const String path = p_path.simplify_path();
	ERR_FAIL_COND_V(path.is_empty(), ERR_INVALID_PARAMETER);

	const DocumentState *existing = documents.getptr(path);
	ERR_FAIL_COND_V_MSG(existing && existing->source_state == SOURCE_STATE_OPEN_DOCUMENT, ERR_ALREADY_IN_USE, "GDScript analysis session is opening an already open document.");

	_remove_cached_parser(path);
	DocumentState document;
	document.path = path;
	document.text = p_language_id == LSP::LanguageId::GDSCRIPT ? p_text : String();
	document.sha256 = document.text.sha256_text();
	document.revision = _advance_revision();
	document.client_version = p_client_version;
	document.language_id = p_language_id;
	document.source_state = SOURCE_STATE_OPEN_DOCUMENT;
	documents[path] = document;

	if (p_language_id == LSP::LanguageId::GDSCRIPT) {
		get_parse_result(path);
	}
	return OK;
}

Error GDScriptAnalysisSession::change_document(const String &p_path, const String &p_text, int64_t p_client_version) {
	const String path = p_path.simplify_path();
	DocumentState *document = documents.getptr(path);
	ERR_FAIL_COND_V_MSG(!document || document->source_state != SOURCE_STATE_OPEN_DOCUMENT, ERR_DOES_NOT_EXIST, "GDScript analysis session is changing a document that is not open.");

	if (document->language_id != LSP::LanguageId::GDSCRIPT) {
		document->revision = _advance_revision();
		document->client_version = p_client_version;
		return OK;
	}

	_remove_cached_parser(path);
	document->text = p_text;
	document->sha256 = p_text.sha256_text();
	document->revision = _advance_revision();
	document->client_version = p_client_version;
	get_parse_result(path);
	return OK;
}

Error GDScriptAnalysisSession::close_document(const String &p_path) {
	const String path = p_path.simplify_path();
	const DocumentState *document = documents.getptr(path);
	ERR_FAIL_COND_V_MSG(!document || document->source_state != SOURCE_STATE_OPEN_DOCUMENT, ERR_DOES_NOT_EXIST, "GDScript analysis session is closing a document that is not open.");

	_remove_cached_parser(path);
	documents.erase(path);
	_advance_revision();
	return OK;
}

bool GDScriptAnalysisSession::has_document(const String &p_path) const {
	return documents.has(p_path.simplify_path());
}

const GDScriptAnalysisSession::DocumentState *GDScriptAnalysisSession::get_document(const String &p_path) const {
	return documents.getptr(p_path.simplify_path());
}

ExtendGDScriptParser *GDScriptAnalysisSession::get_parse_result(const String &p_path) {
	const String path = p_path.simplify_path();
	if (ExtendGDScriptParser **cached = parser_cache.getptr(path)) {
		return *cached;
	}

	DocumentState *document = documents.getptr(path);
	if (!document) {
		if (!path.has_extension("gd")) {
			return nullptr;
		}
		Error error;
		const String content = FileAccess::get_file_as_string(path, &error);
		if (error != OK) {
			return nullptr;
		}

		DocumentState disk_document;
		disk_document.path = path;
		disk_document.text = content;
		disk_document.sha256 = content.sha256_text();
		// Loading a transient disk parser does not change the session's editable
		// document state. Keep it out of the revision sequence used by diagnostics.
		disk_document.revision = revision;
		disk_document.language_id = LSP::LanguageId::GDSCRIPT;
		disk_document.source_state = SOURCE_STATE_DISK;
		documents[path] = disk_document;
		document = documents.getptr(path);
		transient_sources.insert(path);
	}

	if (document->language_id != LSP::LanguageId::GDSCRIPT) {
		return nullptr;
	}

	// Parsing can load preload dependencies into this session, which may rehash
	// the documents map. Keep the parser input independent of that map storage.
	const String source_text = document->text;
	ExtendGDScriptParser *parser = memnew(ExtendGDScriptParser);
	parser_cache[path] = parser;
	parser->parse(source_text, path, this, workspace);
	return parser;
}

Array GDScriptAnalysisSession::get_diagnostics(const String &p_path, Error *r_error) {
	if (r_error) {
		*r_error = OK;
	}
	Array diagnostics;
	ExtendGDScriptParser *parser = get_parse_result(p_path);
	if (!parser) {
		if (r_error) {
			*r_error = ERR_CANT_OPEN;
		}
		return diagnostics;
	}

	const Vector<LSP::Diagnostic> &parser_diagnostics = parser->get_diagnostics();
	diagnostics.resize(parser_diagnostics.size());
	for (int i = 0; i < parser_diagnostics.size(); i++) {
		diagnostics[i] = parser_diagnostics[i].to_json();
	}
	return diagnostics;
}

void GDScriptAnalysisSession::clear_transient_parsers() {
	while (!transient_sources.is_empty()) {
		const String path = *transient_sources.begin();
		_remove_cached_parser(path);
		documents.erase(path);
	}
}

int GDScriptAnalysisSession::document_position_to_offset(const String &p_path, const LSP::Position &p_position) const {
	const DocumentState *document = get_document(p_path);
	ERR_FAIL_NULL_V(document, 0);
	return position_to_offset(document->text, p_position);
}

LSP::Position GDScriptAnalysisSession::document_offset_to_position(const String &p_path, int p_offset) const {
	const DocumentState *document = get_document(p_path);
	ERR_FAIL_NULL_V(document, LSP::Position());
	return offset_to_position(document->text, p_offset);
}

int GDScriptAnalysisSession::position_to_offset(const String &p_text, const LSP::Position &p_position) {
	int line_start = 0;
	int line_end = 0;
	if (!_get_line_bounds(p_text, p_position.line, line_start, line_end)) {
		return p_text.length();
	}

	const int target_character = MAX(0, p_position.character);
	int utf16_character = 0;
	for (int offset = line_start; offset < line_end; offset++) {
		if (utf16_character >= target_character) {
			return offset;
		}
		const int units = _utf16_code_units(p_text[offset]);
		if (utf16_character + units > target_character) {
			return offset;
		}
		utf16_character += units;
	}
	return line_end;
}

LSP::Position GDScriptAnalysisSession::offset_to_position(const String &p_text, int p_offset) {
	const int offset = CLAMP(p_offset, 0, p_text.length());
	LSP::Position position;
	for (int i = 0; i < offset; i++) {
		const char32_t character = p_text[i];
		if (character == '\n') {
			position.line++;
			position.character = 0;
		} else if (character != '\r' || i + 1 >= p_text.length() || p_text[i + 1] != '\n') {
			position.character += _utf16_code_units(character);
		}
	}
	return position;
}

LSP::Position GDScriptAnalysisSession::lsp_to_codepoint_position(const String &p_text, const LSP::Position &p_position) {
	const int offset = position_to_offset(p_text, p_position);
	const LSP::Position normalized = offset_to_position(p_text, offset);
	int line_start = 0;
	int line_end = 0;
	_get_line_bounds(p_text, normalized.line, line_start, line_end);
	return LSP::Position(normalized.line, CLAMP(offset - line_start, 0, line_end - line_start));
}

LSP::Position GDScriptAnalysisSession::codepoint_to_lsp_position(const String &p_text, const LSP::Position &p_position) {
	int line_start = 0;
	int line_end = 0;
	if (!_get_line_bounds(p_text, p_position.line, line_start, line_end)) {
		return offset_to_position(p_text, p_text.length());
	}
	const int offset = line_start + CLAMP(p_position.character, 0, line_end - line_start);
	return offset_to_position(p_text, offset);
}

LSP::Position GDScriptAnalysisSession::parser_to_lsp_position(const String &p_text, int p_line, int p_column) {
	return codepoint_to_lsp_position(p_text, LSP::Position(MAX(0, p_line - 1), MAX(0, p_column - 1)));
}

GDScriptAnalysisSession::GDScriptAnalysisSession(uint64_t p_session_id, const Ref<GDScriptWorkspace> &p_workspace) {
	session_id = p_session_id;
	workspace = p_workspace;
}

GDScriptAnalysisSession::~GDScriptAnalysisSession() {
	while (!parser_cache.is_empty()) {
		_remove_cached_parser(parser_cache.begin()->key);
	}
	documents.clear();
}
