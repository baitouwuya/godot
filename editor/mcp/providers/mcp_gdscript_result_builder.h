/**************************************************************************/
/*  mcp_gdscript_result_builder.h                                        */
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

#include "core/object/script_language.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

#include "modules/gdscript/language_server/gdscript_analysis_session.h"
#include "modules/gdscript/language_server/gdscript_workspace.h"

class MCPGDScriptResultBuilder {
public:
	MCPGDScriptResultBuilder(const Ref<GDScriptWorkspace> &p_workspace, const Ref<GDScriptAnalysisSession> &p_session, const String &p_path);

	Dictionary metadata() const;
	Dictionary diagnostics(const Array &p_diagnostics) const;
	Dictionary document_symbols() const;
	Dictionary completion(const List<ScriptLanguage::CodeCompletionOption> &p_options, int p_limit) const;
	Dictionary hover(const LSP::DocumentSymbol *p_symbol) const;
	Dictionary symbol_resolution(const LSP::DocumentSymbol *p_symbol, bool p_declaration) const;
	Dictionary locations(const Vector<LSP::Location> &p_locations, const LSP::DocumentSymbol *p_symbol = nullptr) const;
	Dictionary signature_help(const LSP::SignatureHelp *p_signature) const;
	Dictionary rename(const Dictionary &p_edit) const;

private:
	Ref<GDScriptWorkspace> workspace;
	Ref<GDScriptAnalysisSession> session;
	String path;
};
