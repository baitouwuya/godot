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

#include "modules/gdscript/language_server/gdscript_extend_parser.h"

namespace {

struct ScriptMemberMatch {
	const LSP::DocumentSymbol *symbol = nullptr;
	String kind;
	bool parameter = false;
};

static const GDScriptParser::FunctionNode *_find_function_node(const GDScriptParser::ClassNode *p_class, int p_line);
static const GDScriptParser::SignalNode *_find_signal_node(const GDScriptParser::ClassNode *p_class, int p_line);

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

static bool _kind_matches(const String &p_requested, const String &p_actual) {
	if (p_requested == p_actual) {
		return true;
	}
	return (p_requested == "variable" || p_requested == "parameter") && p_actual == "property";
}

static const LSP::DocumentSymbol *_find_class(const LSP::DocumentSymbol &p_class, const String &p_name) {
	if (p_name.is_empty() || p_class.name == p_name) {
		return &p_class;
	}
	for (const LSP::DocumentSymbol &child : p_class.children) {
		if (child.kind == LSP::SymbolKind::Class) {
			if (child.name == p_name) {
				return &child;
			}
			if (const LSP::DocumentSymbol *nested = _find_class(child, p_name)) {
				return nested;
			}
		}
	}
	return nullptr;
}

static bool _is_parameter(const ExtendGDScriptParser &p_parser, const LSP::DocumentSymbol &p_owner, const LSP::DocumentSymbol &p_candidate) {
	const GDScriptParser::ClassNode *tree = p_parser.get_tree();
	if (!tree) {
		return false;
	}
	if (p_owner.kind == LSP::SymbolKind::Method || p_owner.kind == LSP::SymbolKind::Function) {
		const GDScriptParser::FunctionNode *function = _find_function_node(tree, p_owner.range.start.line);
		if (!function) {
			return false;
		}
		for (const GDScriptParser::ParameterNode *parameter : function->parameters) {
			if (parameter->identifier->name == p_candidate.name && parameter->start_line - 1 == p_candidate.range.start.line) {
				return true;
			}
		}
		return function->rest_parameter && function->rest_parameter->identifier->name == p_candidate.name &&
				function->rest_parameter->start_line - 1 == p_candidate.range.start.line;
	}
	if (p_owner.kind == LSP::SymbolKind::Event) {
		const GDScriptParser::SignalNode *signal = _find_signal_node(tree, p_owner.range.start.line);
		if (!signal) {
			return false;
		}
		for (const GDScriptParser::ParameterNode *parameter : signal->parameters) {
			if (parameter->identifier->name == p_candidate.name && parameter->start_line - 1 == p_candidate.range.start.line) {
				return true;
			}
		}
	}
	return false;
}

static ScriptMemberMatch _find_member(const ExtendGDScriptParser &p_parser, const LSP::DocumentSymbol &p_root, const Dictionary &p_query) {
	ScriptMemberMatch match;
	const String requested_kind = p_query.get("kind", String());
	const String requested_name = p_query.get("name", String());
	const String owner = p_query.get("owner", String());

	if (requested_kind == "parameter") {
		for (const LSP::DocumentSymbol &member : p_root.children) {
			if ((member.kind == LSP::SymbolKind::Method || member.kind == LSP::SymbolKind::Function || member.kind == LSP::SymbolKind::Event) &&
					member.name == owner) {
				for (const LSP::DocumentSymbol &child : member.children) {
					if (child.name == requested_name && child.kind == LSP::SymbolKind::Variable && _is_parameter(p_parser, member, child)) {
						match.symbol = &child;
						match.kind = "parameter";
						match.parameter = true;
						return match;
					}
				}
			}
			if (member.kind == LSP::SymbolKind::Class) {
				match = _find_member(p_parser, member, p_query);
				if (match.symbol) {
					return match;
				}
			}
		}
		return match;
	}

	const LSP::DocumentSymbol *owner_class = _find_class(p_root, owner);
	if (!owner_class) {
		return match;
	}
	for (const LSP::DocumentSymbol &member : owner_class->children) {
		const String actual_kind = _symbol_kind(member);
		if (member.name == requested_name && _kind_matches(requested_kind, actual_kind)) {
			match.symbol = &member;
			match.kind = actual_kind;
			return match;
		}
	}
	return match;
}

static Dictionary _make_line(int p_line, const String &p_text, const String &p_role) {
	Dictionary line;
	line["line"] = p_line;
	line["text"] = p_text;
	line["role"] = p_role;
	return line;
}

static const GDScriptParser::FunctionNode *_find_function_node(const GDScriptParser::ClassNode *p_class, int p_line) {
	if (!p_class) {
		return nullptr;
	}
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::FUNCTION && member.function->start_line - 1 == p_line) {
			return member.function;
		}
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			if (const GDScriptParser::FunctionNode *nested = _find_function_node(member.m_class, p_line)) {
				return nested;
			}
		}
	}
	return nullptr;
}

static const GDScriptParser::SignalNode *_find_signal_node(const GDScriptParser::ClassNode *p_class, int p_line) {
	if (!p_class) {
		return nullptr;
	}
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::SIGNAL && member.signal->start_line - 1 == p_line) {
			return member.signal;
		}
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			if (const GDScriptParser::SignalNode *nested = _find_signal_node(member.m_class, p_line)) {
				return nested;
			}
		}
	}
	return nullptr;
}

static Dictionary _make_parameter(const ExtendGDScriptParser &p_parser, const GDScriptParser::ParameterNode *p_parameter, bool p_rest) {
	Dictionary parameter;
	parameter["name"] = p_parameter->identifier->name;
	parameter["line"] = p_parameter->start_line;
	parameter["range"] = p_parser.to_lsp_range(p_parameter->start_line, p_parameter->start_column, p_parameter->end_line, p_parameter->end_column).to_json();
	const GDScriptParser::DataType type = p_parameter->get_datatype();
	if (type.is_hard_type()) {
		parameter["type"] = type.to_string();
	}
	if (p_rest) {
		parameter["variadic"] = true;
	}
	return parameter;
}

static Array _get_parameters(const ExtendGDScriptParser &p_parser, const LSP::DocumentSymbol &p_symbol) {
	Array parameters;
	const GDScriptParser::ClassNode *tree = p_parser.get_tree();
	if (!tree) {
		return parameters;
	}
	if (p_symbol.kind == LSP::SymbolKind::Method || p_symbol.kind == LSP::SymbolKind::Function) {
		const GDScriptParser::FunctionNode *function = _find_function_node(tree, p_symbol.range.start.line);
		if (function) {
			for (const GDScriptParser::ParameterNode *parameter : function->parameters) {
				parameters.push_back(_make_parameter(p_parser, parameter, false));
			}
			if (function->rest_parameter) {
				parameters.push_back(_make_parameter(p_parser, function->rest_parameter, true));
			}
		}
	} else if (p_symbol.kind == LSP::SymbolKind::Event) {
		const GDScriptParser::SignalNode *signal = _find_signal_node(tree, p_symbol.range.start.line);
		if (signal) {
			for (const GDScriptParser::ParameterNode *parameter : signal->parameters) {
				parameters.push_back(_make_parameter(p_parser, parameter, false));
			}
		}
	}
	return parameters;
}

static String _declaration_text(const ExtendGDScriptParser &p_parser, const LSP::DocumentSymbol &p_symbol, bool p_parameter) {
	const Vector<String> &source_lines = p_parser.get_lines();
	const int line = p_symbol.selectionRange.start.line;
	if (!p_parameter && line >= 0 && line < source_lines.size()) {
		const String source = source_lines[line];
		const String trimmed = source.strip_edges();
		const bool complete_method = p_symbol.kind != LSP::SymbolKind::Method && p_symbol.kind != LSP::SymbolKind::Function || trimmed.ends_with(":");
		if (!trimmed.is_empty() && complete_method) {
			return source;
		}
	}
	String detail = p_symbol.detail;
	if (p_parameter) {
		return detail.trim_prefix("var ");
	}
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
		const String &p_kind, bool p_include_comments, bool p_parameter, const String &p_owner) {
	Dictionary member;
	member["kind"] = p_kind;
	member["name"] = p_symbol.name;
	member["declaration"] = _declaration_text(p_parser, p_symbol, p_parameter);
	member["line"] = p_symbol.selectionRange.start.line + 1;
	member["range"] = p_symbol.range.to_json();
	if (p_include_comments && !p_symbol.documentation.is_empty()) {
		member["documentation"] = p_symbol.documentation;
	}
	if (!p_owner.is_empty()) {
		member["owner"] = p_owner;
	}
	const Array parameters = _get_parameters(p_parser, p_symbol);
	if (!parameters.is_empty()) {
		member["parameters"] = parameters;
	}
	return member;
}

static void _render_class_members(const ExtendGDScriptParser &p_parser, const LSP::DocumentSymbol &p_class,
		bool p_include_comments, const String &p_owner, Array &r_members) {
	for (const LSP::DocumentSymbol &symbol : p_class.children) {
		const String kind = _symbol_kind(symbol);
		if (kind.is_empty()) {
			continue;
		}
		r_members.push_back(_render_document_member(p_parser, symbol, kind, p_include_comments, false, p_owner));
		if (symbol.kind == LSP::SymbolKind::Class) {
			_render_class_members(p_parser, symbol, p_include_comments, symbol.name, r_members);
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
	ScriptMemberMatch selected;
	if (!p_member.is_empty()) {
		selected = _find_member(p_parser, root, p_member);
		if (!selected.symbol) {
			return _fail("The requested script member was not found.", r_error, ERR_DOES_NOT_EXIST);
		}
	}

	Array members;
	if (p_view == "full") {
		int start_line = 0;
		int end_line = p_parser.get_lines().size() - 1;
		if (selected.symbol) {
			start_line = selected.symbol->range.start.line;
			end_line = selected.symbol->range.end.line;
		}
		const Array lines = _render_full_lines(p_parser, start_line, end_line, p_include_comments);
		r_document["lines"] = lines;
		r_document["text"] = _join_lines(lines);
	} else if (selected.symbol) {
		members.push_back(_render_document_member(p_parser, *selected.symbol, selected.kind, p_include_comments, selected.parameter, p_member.get("owner", String())));
	} else {
		if (p_include_comments && !root.documentation.is_empty()) {
			r_document["documentation"] = root.documentation;
		}
		_render_class_members(p_parser, root, p_include_comments, String(), members);
	}

	r_document["view"] = p_view;
	r_document["includeComments"] = p_include_comments;
	if (p_view == "documentation") {
		r_document["memberCount"] = members.size();
		r_document["members"] = members;
	}
	return OK;
}
