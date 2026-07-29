/**************************************************************************/
/*  mcp_gdscript_request_parser.h                                         */
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

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

#include "modules/gdscript/language_server/godot_lsp.h"

class GDScriptWorkspace;

class MCPGDScriptRequestParser {
public:
	struct CompletionRequest {
		String path;
		LSP::CompletionParams params;
		int limit = 0;
	};

	struct RenameRequest {
		String path;
		LSP::TextDocumentPositionParams params;
		String new_name;
	};

	struct WorkspaceEditRequest {
		Dictionary edit;
		Array documents;
	};

	MCPGDScriptRequestParser(const Ref<GDScriptWorkspace> &p_workspace, const String &p_project_root);

	Error parse_document_path(const Dictionary &p_arguments, String &r_path, String *r_error = nullptr) const;
	Error parse_position(const Dictionary &p_arguments, String &r_path, LSP::TextDocumentPositionParams &r_params, String *r_error = nullptr) const;
	Error parse_reference_position(const Dictionary &p_arguments, String &r_path, LSP::ReferenceParams &r_params, String *r_error = nullptr) const;
	Error parse_completion(const Dictionary &p_arguments, CompletionRequest &r_request, String *r_error = nullptr) const;
	Error parse_rename(const Dictionary &p_arguments, RenameRequest &r_request, String *r_error = nullptr) const;
	Error parse_workspace_edit(const Dictionary &p_arguments, WorkspaceEditRequest &r_request, String *r_error = nullptr) const;

private:
	Ref<GDScriptWorkspace> workspace;
	String project_root;
};
