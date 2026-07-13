/**************************************************************************/
/*  mcp_script_member_selector.cpp                                        */
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

#include "mcp_script_member_selector.h"

#include "modules/gdscript/language_server/gdscript_analysis_session.h"
#include "modules/gdscript/language_server/gdscript_extend_parser.h"

namespace {

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
	return p_requested == p_actual || (p_requested == "variable" && p_actual == "property");
}

static const LSP::DocumentSymbol *_find_class_by_path(const LSP::DocumentSymbol &p_root, const String &p_path) {
	if (p_path.is_empty()) {
		return &p_root;
	}
	const PackedStringArray parts = p_path.split(".", false);
	const LSP::DocumentSymbol *current = &p_root;
	for (const String &part : parts) {
		const LSP::DocumentSymbol *next = nullptr;
		for (const LSP::DocumentSymbol &child : current->children) {
			if (child.kind == LSP::SymbolKind::Class && child.name == part) {
				next = &child;
				break;
			}
		}
		if (!next) {
			return nullptr;
		}
		current = next;
	}
	return current;
}

static void _collect_classes_named(const LSP::DocumentSymbol &p_class, const String &p_path, const String &p_name,
		Vector<const LSP::DocumentSymbol *> &r_classes, Vector<String> &r_paths) {
	for (const LSP::DocumentSymbol &child : p_class.children) {
		if (child.kind != LSP::SymbolKind::Class) {
			continue;
		}
		const String child_path = p_path.is_empty() ? child.name : p_path + "." + child.name;
		if (child.name == p_name) {
			r_classes.push_back(&child);
			r_paths.push_back(child_path);
		}
		_collect_classes_named(child, child_path, p_name, r_classes, r_paths);
	}
}

static const LSP::DocumentSymbol *_resolve_owner_class(const LSP::DocumentSymbol &p_root, const String &p_owner,
		String &r_owner_path, bool &r_ambiguous) {
	r_owner_path = String();
	r_ambiguous = false;
	if (p_owner.is_empty() || p_owner == p_root.name) {
		return &p_root;
	}
	if (p_owner.contains(".")) {
		const LSP::DocumentSymbol *resolved = _find_class_by_path(p_root, p_owner);
		if (resolved) {
			r_owner_path = p_owner;
		}
		return resolved;
	}
	Vector<const LSP::DocumentSymbol *> classes;
	Vector<String> paths;
	_collect_classes_named(p_root, String(), p_owner, classes, paths);
	if (classes.size() == 1) {
		r_owner_path = paths[0];
		return classes[0];
	}
	r_ambiguous = classes.size() > 1;
	return nullptr;
}

static const GDScriptParser::ParameterNode *_find_parameter_node(const ExtendGDScriptParser &p_parser,
		const LSP::DocumentSymbol &p_owner, const LSP::DocumentSymbol &p_candidate, bool &r_rest) {
	r_rest = false;
	const GDScriptParser::ClassNode *tree = p_parser.get_tree();
	if (!tree) {
		return nullptr;
	}
	if (p_owner.kind == LSP::SymbolKind::Method || p_owner.kind == LSP::SymbolKind::Function) {
		const GDScriptParser::FunctionNode *function = _find_function_node(tree, p_owner.range.start.line);
		if (!function) {
			return nullptr;
		}
		for (const GDScriptParser::ParameterNode *parameter : function->parameters) {
			if (parameter->identifier->name == p_candidate.name && parameter->start_line - 1 == p_candidate.range.start.line) {
				return parameter;
			}
		}
		if (function->rest_parameter && function->rest_parameter->identifier->name == p_candidate.name &&
				function->rest_parameter->start_line - 1 == p_candidate.range.start.line) {
			r_rest = true;
			return function->rest_parameter;
		}
		return nullptr;
	}
	if (p_owner.kind == LSP::SymbolKind::Event) {
		const GDScriptParser::SignalNode *signal = _find_signal_node(tree, p_owner.range.start.line);
		if (!signal) {
			return nullptr;
		}
		for (const GDScriptParser::ParameterNode *parameter : signal->parameters) {
			if (parameter->identifier->name == p_candidate.name && parameter->start_line - 1 == p_candidate.range.start.line) {
				return parameter;
			}
		}
	}
	return nullptr;
}

static void _collect_parameter_matches(const ExtendGDScriptParser &p_parser, const LSP::DocumentSymbol &p_class,
		const String &p_class_path, const String &p_callable_name, const String &p_parameter_name, Vector<MCPScriptMemberMatch> &r_matches) {
	for (const LSP::DocumentSymbol &member : p_class.children) {
		if ((member.kind == LSP::SymbolKind::Method || member.kind == LSP::SymbolKind::Function || member.kind == LSP::SymbolKind::Event) && member.name == p_callable_name) {
			for (const LSP::DocumentSymbol &child : member.children) {
				bool rest = false;
				const GDScriptParser::ParameterNode *parameter = child.name == p_parameter_name && child.kind == LSP::SymbolKind::Variable ?
						_find_parameter_node(p_parser, member, child, rest) : nullptr;
				if (parameter) {
					MCPScriptMemberMatch match;
					match.symbol = &child;
					match.parameter_node = parameter;
					match.kind = "parameter";
					match.owner_path = p_class_path;
					match.callable_name = p_callable_name;
					match.rest_parameter = rest;
					r_matches.push_back(match);
				}
			}
		}
		if (member.kind == LSP::SymbolKind::Class) {
			const String child_path = p_class_path.is_empty() ? member.name : p_class_path + "." + member.name;
			_collect_parameter_matches(p_parser, member, child_path, p_callable_name, p_parameter_name, r_matches);
		}
	}
}

} // namespace

Error MCPScriptMemberSelector::find(const ExtendGDScriptParser &p_parser, const Dictionary &p_query, MCPScriptMemberMatch &r_match, String *r_error) {
	r_match = MCPScriptMemberMatch();
	const LSP::DocumentSymbol &root = p_parser.get_symbols();
	const String requested_kind = p_query.get("kind", String());
	const String requested_name = p_query.get("name", String());
	const String owner = p_query.get("owner", String());
	bool ambiguous = false;

	if (requested_kind == "parameter") {
		Vector<MCPScriptMemberMatch> matches;
		const String class_path = p_query.get("classPath", String());
		if (!class_path.is_empty()) {
			const LSP::DocumentSymbol *owner_class = _find_class_by_path(root, class_path);
			if (owner_class) {
				_collect_parameter_matches(p_parser, *owner_class, class_path, owner, requested_name, matches);
				for (int i = matches.size() - 1; i >= 0; i--) {
					if (matches[i].owner_path != class_path) {
						matches.remove_at(i);
					}
				}
			}
		} else {
			_collect_parameter_matches(p_parser, root, String(), owner, requested_name, matches);
		}
		if (matches.size() == 1) {
			r_match = matches[0];
			return OK;
		}
		ambiguous = matches.size() > 1;
	} else if (requested_kind == "class" && requested_name == root.name && (owner.is_empty() || owner == root.name)) {
		r_match.symbol = &root;
		r_match.kind = "class";
		return OK;
	} else {
		const LSP::DocumentSymbol *owner_class = _resolve_owner_class(root, owner, r_match.owner_path, ambiguous);
		if (owner_class) {
			for (const LSP::DocumentSymbol &member : owner_class->children) {
				const String actual_kind = _symbol_kind(member);
				if (member.name == requested_name && _kind_matches(requested_kind, actual_kind)) {
					r_match.symbol = &member;
					r_match.kind = actual_kind;
					return OK;
				}
			}
		}
	}
	if (r_error) {
		*r_error = ambiguous ? "The requested script member is ambiguous; provide a qualified owner or classPath." : "The requested script member was not found.";
	}
	return ambiguous ? ERR_ALREADY_EXISTS : ERR_DOES_NOT_EXIST;
}

MCPScriptSourceSlice MCPScriptMemberSelector::parameter_source_slice(const ExtendGDScriptParser &p_parser,
		const GDScriptParser::ParameterNode *p_parameter, bool p_rest) {
	MCPScriptSourceSlice slice;
	if (!p_parameter) {
		return slice;
	}
	const String &source = p_parser.get_source_text();
	const LSP::Range range = p_parser.to_lsp_range(p_parameter->start_line, p_parameter->start_column, p_parameter->end_line, p_parameter->end_column);
	int start_offset = GDScriptAnalysisSession::position_to_offset(source, range.start);
	const int end_offset = GDScriptAnalysisSession::position_to_offset(source, range.end);
	if (p_rest) {
		int prefix_end = start_offset;
		while (prefix_end > 0 && (source[prefix_end - 1] == ' ' || source[prefix_end - 1] == '\t' || source[prefix_end - 1] == '\r' || source[prefix_end - 1] == '\n')) {
			prefix_end--;
		}
		if (prefix_end >= 3 && source.substr(prefix_end - 3, 3) == "...") {
			start_offset = prefix_end - 3;
		}
	}
	slice.text = source.substr(start_offset, MAX(0, end_offset - start_offset));
	slice.start = GDScriptAnalysisSession::offset_to_position(source, start_offset);
	return slice;
}
