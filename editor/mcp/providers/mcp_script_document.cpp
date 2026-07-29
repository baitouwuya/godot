/**************************************************************************/
/*  mcp_script_document.cpp                                               */
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

#include "mcp_script_document.h"

#include "mcp_script_member_selector.h"

#include "modules/gdscript/language_server/gdscript_extend_parser.h"

namespace {

static Error _fail(const String &p_message, String *r_error, Error p_error = ERR_INVALID_PARAMETER) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

static String _symbol_kind(const LSP::DocumentSymbol &p_symbol) {
	switch (p_symbol.kind) {
		case LSP::SymbolKind::Class:
			return "class";
		case LSP::SymbolKind::Method:
		case LSP::SymbolKind::Function:
			return "method";
		case LSP::SymbolKind::Property:
		case LSP::SymbolKind::Variable:
			return "property";
		case LSP::SymbolKind::Constant:
			return "constant";
		case LSP::SymbolKind::Enum:
			return "enum";
		case LSP::SymbolKind::EnumMember:
			return "enumValue";
		case LSP::SymbolKind::Event:
			return "signal";
		default:
			return String();
	}
}

static Dictionary _make_line(int p_line, const String &p_text, const String &p_role) {
	Dictionary line;
	line["line"] = p_line;
	line["text"] = p_text;
	line["role"] = p_role;
	return line;
}

static String _declaration_text(const ExtendGDScriptParser &p_parser, const LSP::DocumentSymbol &p_symbol,
		const GDScriptParser::ParameterNode *p_parameter, bool p_rest) {
	if (p_parameter) {
		return MCPScriptMemberSelector::parameter_source_slice(p_parser, p_parameter, p_rest).text;
	}
	const Vector<String> &source_lines = p_parser.get_lines();
	const int line = p_symbol.selectionRange.start.line;
	if (line >= 0 && line < source_lines.size()) {
		const String source = source_lines[line];
		const String trimmed = source.strip_edges();
		const bool complete_method = (p_symbol.kind != LSP::SymbolKind::Method && p_symbol.kind != LSP::SymbolKind::Function) || trimmed.ends_with(":");
		if (!trimmed.is_empty() && complete_method) {
			return source;
		}
	}
	String detail = p_symbol.detail;
	if (p_symbol.kind == LSP::SymbolKind::Event) {
		detail = "signal " + p_symbol.name + "(";
		for (int i = 0; i < p_symbol.children.size(); i++) {
			if (i > 0) {
				detail += ", ";
			}
			detail += p_symbol.children[i].detail.trim_prefix("var ");
		}
		detail += ")";
	}
	if ((p_symbol.kind == LSP::SymbolKind::Method || p_symbol.kind == LSP::SymbolKind::Function || p_symbol.kind == LSP::SymbolKind::Class) && !detail.ends_with(":")) {
		detail += ":";
	}
	return detail;
}

static Dictionary _render_document_member(const ExtendGDScriptParser &p_parser, const LSP::DocumentSymbol &p_symbol,
		const String &p_kind, bool p_include_comments, const GDScriptParser::ParameterNode *p_parameter, bool p_rest,
		const String &p_owner, const String &p_class_path = String()) {
	Dictionary member;
	const MCPScriptSourceSlice parameter_slice = MCPScriptMemberSelector::parameter_source_slice(p_parser, p_parameter, p_rest);
	member["text"] = _declaration_text(p_parser, p_symbol, p_parameter, p_rest);
	member["line"] = p_parameter ? parameter_slice.start.line + 1 : p_symbol.selectionRange.start.line + 1;
	if (p_include_comments && !p_symbol.documentation.is_empty()) {
		member["doc"] = p_symbol.documentation;
	}
	if (!p_owner.is_empty()) {
		member["owner"] = p_owner;
	}
	if (!p_class_path.is_empty()) {
		member["classPath"] = p_class_path;
	}
	return member;
}

static StringName _group_name(const String &p_kind) {
	if (p_kind == "class") {
		return "classes";
	}
	if (p_kind == "enum") {
		return "enums";
	}
	if (p_kind == "enumValue") {
		return "enumValues";
	}
	if (p_kind == "constant") {
		return "constants";
	}
	if (p_kind == "property") {
		return "properties";
	}
	if (p_kind == "signal") {
		return "signals";
	}
	if (p_kind == "method") {
		return "methods";
	}
	return "parameters";
}

static void _append_grouped(Dictionary &r_groups, const String &p_kind, const Dictionary &p_member) {
	const StringName group_name = _group_name(p_kind);
	Array group = r_groups.get(group_name, Array());
	group.push_back(p_member);
	r_groups[group_name] = group;
}

static void _render_class_members(const ExtendGDScriptParser &p_parser, const LSP::DocumentSymbol &p_class,
		bool p_include_comments, const String &p_owner, Dictionary &r_groups) {
	for (const LSP::DocumentSymbol &symbol : p_class.children) {
		const String kind = _symbol_kind(symbol);
		if (kind.is_empty()) {
			continue;
		}
		_append_grouped(r_groups, kind, _render_document_member(p_parser, symbol, kind, p_include_comments, nullptr, false, p_owner));
		if (symbol.kind == LSP::SymbolKind::Class) {
			const String child_path = p_owner.is_empty() ? symbol.name : p_owner + "." + symbol.name;
			_render_class_members(p_parser, symbol, p_include_comments, child_path, r_groups);
		}
	}
}

static Array _render_full_lines(const ExtendGDScriptParser &p_parser, int p_start_line, int p_end_line, bool p_include_comments) {
	Array result;
	const Vector<String> &source_lines = p_parser.get_lines();
	if (source_lines.is_empty()) {
		return result;
	}
	p_start_line = MAX(0, p_start_line);
	p_end_line = MIN(p_end_line, source_lines.size() - 1);
	for (int line = p_start_line; line <= p_end_line; line++) {
		String text = source_lines[line];
		if (!p_include_comments) {
			const GDScriptTokenizer::CommentData *comment = p_parser.comment_data.getptr(line + 1);
			if (comment) {
				if (comment->new_line) {
					continue;
				}
				const int comment_start = text.rfind(comment->comment);
				if (comment_start >= 0) {
					text = text.left(comment_start).strip_edges(false, true);
				}
			}
		}
		result.push_back(_make_line(line + 1, text, "source"));
	}
	return result;
}

static String _join_lines(const Array &p_lines) {
	String text;
	for (int i = 0; i < p_lines.size(); i++) {
		if (i > 0) {
			text += "\n";
		}
		const Dictionary line = p_lines[i];
		text += String(line.get("text", String()));
	}
	return text;
}

static Array _render_source_slice_lines(const ExtendGDScriptParser &p_parser, const MCPScriptSourceSlice &p_slice, bool p_include_comments) {
	Array result;
	const PackedStringArray lines = p_slice.text.split("\n", true);
	for (int i = 0; i < lines.size(); i++) {
		String text = lines[i];
		if (!p_include_comments) {
			const GDScriptTokenizer::CommentData *comment = p_parser.comment_data.getptr(p_slice.start.line + i + 1);
			if (comment) {
				if (comment->new_line) {
					continue;
				}
				const int comment_start = text.rfind(comment->comment);
				if (comment_start >= 0) {
					text = text.left(comment_start).strip_edges(false, true);
				}
			}
		}
		result.push_back(_make_line(p_slice.start.line + i + 1, text, "source"));
	}
	return result;
}

} // namespace

Error MCPScriptDocument::render(const ExtendGDScriptParser &p_parser, const String &p_view, bool p_include_comments,
		const Dictionary &p_member, Dictionary &r_document, String *r_error) {
	r_document = Dictionary();
	if (r_error) {
		*r_error = String();
	}
	if (p_view != "documentation" && p_view != "full") {
		return _fail("Script view must be documentation or full.", r_error);
	}

	const LSP::DocumentSymbol &root = p_parser.get_symbols();
	MCPScriptMemberMatch selected;
	if (!p_member.is_empty()) {
		const Error select_error = MCPScriptMemberSelector::find(p_parser, p_member, selected, r_error);
		if (select_error != OK) {
			return select_error;
		}
	}

	Dictionary groups;
	if (p_view == "full") {
		Array lines;
		if (selected.parameter_node) {
			const MCPScriptSourceSlice slice = MCPScriptMemberSelector::parameter_source_slice(p_parser, selected.parameter_node, selected.rest_parameter);
			lines = _render_source_slice_lines(p_parser, slice, p_include_comments);
			r_document["text"] = _join_lines(lines);
		} else {
			int start_line = 0;
			int end_line = p_parser.get_lines().size() - 1;
			if (selected.symbol) {
				start_line = selected.symbol->range.start.line;
				end_line = selected.symbol->range.end.line;
			}
			lines = _render_full_lines(p_parser, start_line, end_line, p_include_comments);
			r_document["text"] = _join_lines(lines);
		}
		r_document["lines"] = lines;
	} else if (selected.symbol) {
		_append_grouped(groups, selected.kind, _render_document_member(p_parser, *selected.symbol, selected.kind, p_include_comments, selected.parameter_node, selected.rest_parameter, selected.callable_name.is_empty() ? selected.owner_path : selected.callable_name, selected.parameter_node ? selected.owner_path : String()));
	} else {
		if (p_include_comments && !root.documentation.is_empty()) {
			r_document["documentation"] = root.documentation;
		}
		_render_class_members(p_parser, root, p_include_comments, String(), groups);
	}

	r_document["view"] = p_view;
	r_document["includeComments"] = p_include_comments;
	if (p_view == "documentation") {
		for (const KeyValue<Variant, Variant> &group : groups) {
			r_document[group.key] = group.value;
		}
	}
	return OK;
}
