/**************************************************************************/
/*  gdscript_analysis_session.h                                           */
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

#pragma once

#include "godot_lsp.h"

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class ExtendGDScriptParser;
class GDScriptWorkspace;

class GDScriptAnalysisSession : public RefCounted {
public:
	enum SourceState {
		SOURCE_STATE_DISK,
		SOURCE_STATE_OPEN_DOCUMENT,
	};

	struct DocumentState {
		String path;
		String text;
		String sha256;
		uint64_t revision = 0;
		int64_t client_version = -1;
		LSP::LanguageId language_id = LSP::LanguageId::OTHER;
		SourceState source_state = SOURCE_STATE_DISK;
	};

private:
	uint64_t session_id = 0;
	uint64_t revision = 0;
	Ref<GDScriptWorkspace> workspace;
	HashMap<String, DocumentState> documents;
	HashMap<String, ExtendGDScriptParser *> parser_cache;
	HashSet<String> transient_sources;

	uint64_t _advance_revision();
	void _remove_cached_parser(const String &p_path);
	static bool _get_line_bounds(const String &p_text, int p_line, int &r_start, int &r_end);

public:
	uint64_t get_session_id() const { return session_id; }
	uint64_t get_revision() const { return revision; }
	const Ref<GDScriptWorkspace> &get_workspace() const { return workspace; }

	Error open_document(const String &p_path, const String &p_text, LSP::LanguageId p_language_id = LSP::LanguageId::GDSCRIPT, int64_t p_client_version = -1);
	Error change_document(const String &p_path, const String &p_text, int64_t p_client_version = -1);
	Error close_document(const String &p_path);

	bool has_document(const String &p_path) const;
	const DocumentState *get_document(const String &p_path) const;
	const HashMap<String, DocumentState> &get_documents() const { return documents; }
	ExtendGDScriptParser *get_parse_result(const String &p_path);
	Array get_diagnostics(const String &p_path, Error *r_error = nullptr);
	void clear_transient_parsers();

	const HashMap<String, ExtendGDScriptParser *> &get_cached_parsers() const { return parser_cache; }
	int get_cached_parser_count() const { return parser_cache.size(); }

	int document_position_to_offset(const String &p_path, const LSP::Position &p_position) const;
	LSP::Position document_offset_to_position(const String &p_path, int p_offset) const;

	static int position_to_offset(const String &p_text, const LSP::Position &p_position);
	static LSP::Position offset_to_position(const String &p_text, int p_offset);
	static LSP::Position lsp_to_codepoint_position(const String &p_text, const LSP::Position &p_position);
	static LSP::Position codepoint_to_lsp_position(const String &p_text, const LSP::Position &p_position);
	static LSP::Position parser_to_lsp_position(const String &p_text, int p_line, int p_column);

	GDScriptAnalysisSession(uint64_t p_session_id, const Ref<GDScriptWorkspace> &p_workspace);
	~GDScriptAnalysisSession();
};
