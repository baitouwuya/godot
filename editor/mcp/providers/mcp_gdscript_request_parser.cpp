/**************************************************************************/
/*  mcp_gdscript_request_parser.cpp                                       */
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

#include "mcp_gdscript_request_parser.h"

#include "mcp_gdscript_tool_utils.h"
#include "mcp_script_path_resolver.h"

#include "modules/gdscript/language_server/gdscript_workspace.h"

namespace {

static Error _fail(const String &p_message, String *r_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return ERR_INVALID_PARAMETER;
}

} // namespace

MCPGDScriptRequestParser::MCPGDScriptRequestParser(const Ref<GDScriptWorkspace> &p_workspace, const String &p_project_root) :
		workspace(p_workspace),
		project_root(p_project_root) {
}

Error MCPGDScriptRequestParser::parse_document_path(const Dictionary &p_arguments, String &r_path, String *r_error) const {
	r_path = String();
	if (r_error) {
		*r_error = String();
	}

	const bool has_path = p_arguments.has("path");
	const bool has_uri = p_arguments.has("uri");
	if (has_path == has_uri) {
		return _fail("Exactly one of path or uri is required.", r_error);
	}

	String raw_path;
	String argument_error;
	const String argument_name = has_path ? "path" : "uri";
	if (!MCPGDScriptToolUtils::get_string_argument(p_arguments, argument_name, raw_path, argument_error)) {
		return _fail(argument_error, r_error);
	}
	if (raw_path.is_empty()) {
		return _fail("path or uri must not be empty.", r_error);
	}

	if (has_uri || raw_path.begins_with("file:")) {
		if (workspace.is_null()) {
			return _fail("A configured GDScript workspace is required to resolve a file URI.", r_error);
		}
		raw_path = workspace->get_file_path(raw_path);
	}
	if (raw_path.is_empty()) {
		return _fail("The script path could not be resolved.", r_error);
	}

	String absolute_path;
	bool built_in = false;
	const Error path_error = MCPScriptPathResolver::resolve_script_for_root(raw_path, project_root, r_path, absolute_path, built_in, r_error);
	if (path_error != OK) {
		r_path = String();
	}
	return path_error;
}

Error MCPGDScriptRequestParser::parse_position(const Dictionary &p_arguments, String &r_path, LSP::TextDocumentPositionParams &r_params, String *r_error) const {
	const Error path_error = parse_document_path(p_arguments, r_path, r_error);
	if (path_error != OK) {
		return path_error;
	}

	int line = 0;
	int character = 0;
	const bool has_position = p_arguments.has("position");
	const bool has_line = p_arguments.has("line");
	const bool has_character = p_arguments.has("character");
	if (has_position && (has_line || has_character)) {
		return _fail("Use either position or top-level line and character, not both.", r_error);
	}
	if (!has_position && (!has_line || !has_character)) {
		return _fail("Either position or both line and character are required.", r_error);
	}

	const Variant position_value = p_arguments.get("position", Variant());
	if (has_position && position_value.get_type() != Variant::DICTIONARY) {
		return _fail("position must be an object.", r_error);
	}
	String argument_error;
	const Dictionary position = has_position ? Dictionary(position_value) : p_arguments;
	if (!MCPGDScriptToolUtils::get_position_argument(position, "line", line, argument_error) ||
			!MCPGDScriptToolUtils::get_position_argument(position, "character", character, argument_error)) {
		return _fail(argument_error, r_error);
	}
	if (workspace.is_null()) {
		return _fail("A configured GDScript workspace is required to create an LSP position.", r_error);
	}

	Dictionary text_document;
	text_document["uri"] = workspace->get_file_uri(r_path);
	Dictionary request_position;
	request_position["line"] = line;
	request_position["character"] = character;
	Dictionary request;
	request["textDocument"] = text_document;
	request["position"] = request_position;
	r_params.load(request);
	return OK;
}

Error MCPGDScriptRequestParser::parse_reference_position(const Dictionary &p_arguments, String &r_path, LSP::ReferenceParams &r_params, String *r_error) const {
	LSP::TextDocumentPositionParams position_params;
	const Error position_error = parse_position(p_arguments, r_path, position_params, r_error);
	if (position_error != OK) {
		return position_error;
	}
	r_params.textDocument = position_params.textDocument;
	r_params.position = position_params.position;
	const Variant include_declaration = p_arguments.get("includeDeclaration", false);
	if (include_declaration.get_type() != Variant::BOOL) {
		return _fail("includeDeclaration must be a boolean.", r_error);
	}
	r_params.context.includeDeclaration = include_declaration;
	return OK;
}
