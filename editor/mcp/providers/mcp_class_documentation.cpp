/**************************************************************************/
/*  mcp_class_documentation.cpp                                           */
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

#include "mcp_class_documentation.h"

#include "core/doc_data.h"
#include "core/object/script_language.h"
#include "core/string/fuzzy_search.h"
#include "editor/doc/doc_tools.h"

namespace MCPClassDocumentation {

static Error _fail(const String &p_message, String *r_error, Error p_code = ERR_INVALID_PARAMETER) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_code;
}

static String _documentation_text(const String &p_bbcode) {
	return DocData::bbcode_to_markdown(p_bbcode);
}

static void _add_status(Dictionary &r_item, bool p_deprecated, const String &p_deprecated_message,
		bool p_experimental, const String &p_experimental_message) {
	if (p_deprecated) {
		r_item["deprecated"] = true;
		if (!p_deprecated_message.is_empty()) {
			r_item["deprecatedMessage"] = _documentation_text(p_deprecated_message);
		}
	}
	if (p_experimental) {
		r_item["experimental"] = true;
		if (!p_experimental_message.is_empty()) {
			r_item["experimentalMessage"] = _documentation_text(p_experimental_message);
		}
	}
}

static bool _source_matches(const DocData::ClassDoc &p_doc, const String &p_source) {
	return p_source == "all" || (p_source == "script") == p_doc.is_script_doc;
}

static const DocData::ClassDoc *_find_doc(DocTools *p_docs, const HashMap<String, DocData::ClassDoc> &p_supplemental_docs, const String &p_name) {
	const DocData::ClassDoc *doc = p_docs->class_list.getptr(p_name);
	return doc ? doc : p_supplemental_docs.getptr(p_name);
}

static bool _inherits_from(DocTools *p_docs, const HashMap<String, DocData::ClassDoc> &p_supplemental_docs,
		const DocData::ClassDoc &p_doc, const String &p_base) {
	String current = p_doc.inherits;
	for (int depth = 0; !current.is_empty() && depth < 256; depth++) {
		if (current == p_base) {
			return true;
		}
		const DocData::ClassDoc *base_doc = _find_doc(p_docs, p_supplemental_docs, current);
		if (!base_doc) {
			break;
		}
		current = base_doc->inherits;
	}
	return false;
}

static Dictionary _make_search_result(const DocData::ClassDoc &p_doc) {
	Dictionary item;
	item["name"] = p_doc.name;
	item["inherits"] = p_doc.inherits;
	item["source"] = p_doc.is_script_doc ? "script" : "native";
	if (p_doc.is_script_doc && !p_doc.script_path.is_empty()) {
		item["scriptPath"] = p_doc.script_path;
	}
	if (p_doc.is_script_doc) {
		const bool global = ScriptServer::is_global_class(p_doc.name);
		item["kind"] = global ? "global" : "documented";
		if (global) {
			item["language"] = String(ScriptServer::get_global_class_language(p_doc.name));
			item["nativeBase"] = String(ScriptServer::get_global_class_native_base(p_doc.name));
			item["abstract"] = ScriptServer::is_global_class_abstract(p_doc.name);
			item["tool"] = ScriptServer::is_global_class_tool(p_doc.name);
		}
	}
	if (!p_doc.brief_description.is_empty()) {
		item["brief"] = _documentation_text(p_doc.brief_description);
	}
	_add_status(item, p_doc.is_deprecated, p_doc.deprecated_message,
			p_doc.is_experimental, p_doc.experimental_message);
	return item;
}

static Dictionary _make_argument(const DocData::ArgumentDoc &p_argument) {
	Dictionary argument;
	argument["name"] = p_argument.name;
	argument["type"] = p_argument.type;
	if (!p_argument.enumeration.is_empty()) {
		argument["enum"] = p_argument.enumeration;
		argument["bitfield"] = p_argument.is_bitfield;
	}
	if (!p_argument.default_value.is_empty()) {
		argument["default"] = p_argument.default_value;
	}
	return argument;
}

static String _method_signature(const DocData::MethodDoc &p_method, const String &p_kind) {
	String signature;
	if (p_kind == "signal") {
		signature = "signal ";
	} else if (!p_method.return_type.is_empty() && p_kind != "constructor") {
		signature = p_method.return_type + " ";
	}
	signature += p_method.name + "(";
	for (int i = 0; i < p_method.arguments.size(); i++) {
		if (i > 0) {
			signature += ", ";
		}
		const DocData::ArgumentDoc &argument = p_method.arguments[i];
		signature += argument.name + ": " + argument.type;
		if (!argument.default_value.is_empty()) {
			signature += " = " + argument.default_value;
		}
	}
	const bool has_vararg = p_method.qualifiers.contains("vararg");
	if (!p_method.rest_argument.name.is_empty() || has_vararg) {
		if (!p_method.arguments.is_empty()) {
			signature += ", ";
		}
		signature += "..." + p_method.rest_argument.name;
		if (!p_method.rest_argument.type.is_empty()) {
			signature += ": " + p_method.rest_argument.type;
		}
	}
	signature += ")";
	const String displayed_qualifiers = p_method.qualifiers.replace("vararg", "").strip_edges();
	if (!displayed_qualifiers.is_empty()) {
		signature += " " + displayed_qualifiers;
	}
	return signature;
}

static Dictionary _make_method(const DocData::MethodDoc &p_method, const String &p_kind, bool p_full) {
	Dictionary item;
	item["name"] = p_method.name;
	item["signature"] = _method_signature(p_method, p_kind);
	if (p_full) {
		if (!p_method.return_type.is_empty()) {
			item["returnType"] = p_method.return_type;
		}
		if (!p_method.return_enum.is_empty()) {
			item["returnEnum"] = p_method.return_enum;
			item["returnBitfield"] = p_method.return_is_bitfield;
		}
		if (!p_method.qualifiers.is_empty()) {
			item["qualifiers"] = p_method.qualifiers;
		}
		Array arguments;
		for (const DocData::ArgumentDoc &argument : p_method.arguments) {
			arguments.push_back(_make_argument(argument));
		}
		if (!arguments.is_empty()) {
			item["arguments"] = arguments;
		}
		if (!p_method.rest_argument.name.is_empty() || p_method.qualifiers.contains("vararg")) {
			item["restArgument"] = _make_argument(p_method.rest_argument);
		}
		if (!p_method.description.is_empty()) {
			item["description"] = _documentation_text(p_method.description);
		}
	}
	_add_status(item, p_method.is_deprecated, p_method.deprecated_message,
			p_method.is_experimental, p_method.experimental_message);
	return item;
}

static Dictionary _make_property(const DocData::PropertyDoc &p_property, bool p_full) {
	Dictionary item;
	item["name"] = p_property.name;
	item["type"] = p_property.type;
	if (!p_property.enumeration.is_empty()) {
		item["enum"] = p_property.enumeration;
		item["bitfield"] = p_property.is_bitfield;
	}
	if (!p_property.default_value.is_empty()) {
		item["default"] = p_property.default_value;
	}
	if (!p_property.setter.is_empty()) {
		item["setter"] = p_property.setter;
	}
	if (!p_property.getter.is_empty()) {
		item["getter"] = p_property.getter;
	}
	if (p_property.overridden) {
		item["overridden"] = true;
		item["overrides"] = p_property.overrides;
	}
	if (p_full && !p_property.description.is_empty()) {
		item["description"] = _documentation_text(p_property.description);
	}
	_add_status(item, p_property.is_deprecated, p_property.deprecated_message,
			p_property.is_experimental, p_property.experimental_message);
	return item;
}

static Dictionary _make_constant(const DocData::ConstantDoc &p_constant, bool p_full) {
	Dictionary item;
	item["name"] = p_constant.name;
	item["type"] = p_constant.type;
	if (p_constant.is_value_valid) {
		item["value"] = p_constant.value;
	}
	if (!p_constant.enumeration.is_empty()) {
		item["enum"] = p_constant.enumeration;
		item["bitfield"] = p_constant.is_bitfield;
	}
	if (p_full && !p_constant.description.is_empty()) {
		item["description"] = _documentation_text(p_constant.description);
	}
	_add_status(item, p_constant.is_deprecated, p_constant.deprecated_message,
			p_constant.is_experimental, p_constant.experimental_message);
	return item;
}

static Dictionary _make_theme_item(const DocData::ThemeItemDoc &p_theme_item, bool p_full) {
	Dictionary item;
	item["name"] = p_theme_item.name;
	item["type"] = p_theme_item.type;
	item["dataType"] = p_theme_item.data_type;
	if (!p_theme_item.default_value.is_empty()) {
		item["default"] = p_theme_item.default_value;
	}
	if (p_full && !p_theme_item.description.is_empty()) {
		item["description"] = _documentation_text(p_theme_item.description);
	}
	_add_status(item, p_theme_item.is_deprecated, p_theme_item.deprecated_message,
			p_theme_item.is_experimental, p_theme_item.experimental_message);
	return item;
}

template <typename T, typename F>
static Array _select_members(const Vector<T> &p_members, const String &p_member, F p_renderer, bool &r_matched) {
	Array items;
	for (const T &member : p_members) {
		if (!p_member.is_empty() && member.name != p_member) {
			continue;
		}
		items.push_back(p_renderer(member));
		r_matched = true;
	}
	return items;
}

static bool _enum_name_matches(const String &p_enum_name, const String &p_member) {
	return p_member.is_empty() || p_enum_name == p_member || p_enum_name.get_slice(".", p_enum_name.get_slice_count(".") - 1) == p_member;
}

static PackedStringArray _get_enum_names(const DocData::ClassDoc &p_doc) {
	PackedStringArray enum_names;
	for (const KeyValue<String, DocData::EnumDoc> &entry : p_doc.enums) {
		enum_names.push_back(entry.key);
	}
	for (const DocData::ConstantDoc &constant : p_doc.constants) {
		if (!constant.enumeration.is_empty() && !enum_names.has(constant.enumeration)) {
			enum_names.push_back(constant.enumeration);
		}
	}
	enum_names.sort();
	return enum_names;
}

static Array _make_enums(const DocData::ClassDoc &p_doc, const String &p_member, bool p_full, bool &r_matched) {
	const PackedStringArray enum_names = _get_enum_names(p_doc);
	Array enums;
	for (const String &enum_name : enum_names) {
		if (!_enum_name_matches(enum_name, p_member)) {
			continue;
		}
		Dictionary item;
		item["name"] = enum_name;
		const DocData::EnumDoc *enum_doc = p_doc.enums.getptr(enum_name);
		if (enum_doc) {
			if (p_full && !enum_doc->description.is_empty()) {
				item["description"] = _documentation_text(enum_doc->description);
			}
			_add_status(item, enum_doc->is_deprecated, enum_doc->deprecated_message,
					enum_doc->is_experimental, enum_doc->experimental_message);
		}
		Array values;
		for (const DocData::ConstantDoc &constant : p_doc.constants) {
			if (constant.enumeration == enum_name) {
				values.push_back(_make_constant(constant, p_full));
			}
		}
		item["values"] = values;
		enums.push_back(item);
		r_matched = true;
	}
	return enums;
}

static Array _make_constants(const DocData::ClassDoc &p_doc, const String &p_member, bool p_full, bool &r_matched) {
	Array constants;
	for (const DocData::ConstantDoc &constant : p_doc.constants) {
		if (!constant.enumeration.is_empty() || (!p_member.is_empty() && constant.name != p_member)) {
			continue;
		}
		constants.push_back(_make_constant(constant, p_full));
		r_matched = true;
	}
	return constants;
}

static Dictionary _make_counts(const DocData::ClassDoc &p_doc) {
	Dictionary counts;
	counts["constructors"] = p_doc.constructors.size();
	counts["methods"] = p_doc.methods.size();
	counts["operators"] = p_doc.operators.size();
	counts["signals"] = p_doc.signals.size();
	counts["properties"] = p_doc.properties.size();
	int constant_count = 0;
	for (const DocData::ConstantDoc &constant : p_doc.constants) {
		if (constant.enumeration.is_empty()) {
			constant_count++;
		}
	}
	counts["constants"] = constant_count;
	counts["enums"] = _get_enum_names(p_doc).size();
	counts["themeItems"] = p_doc.theme_properties.size();
	counts["annotations"] = p_doc.annotations.size();
	return counts;
}

Error search(DocTools *p_docs, const String &p_query, const String &p_source, const String &p_inherits,
		bool p_include_deprecated, int p_limit, const HashMap<String, DocData::ClassDoc> &p_supplemental_docs,
		Dictionary &r_result, String *r_error) {
	r_result = Dictionary();
	if (r_error) {
		*r_error = String();
	}
	if (!p_docs) {
		return _fail("Editor class documentation is not available.", r_error, ERR_UNCONFIGURED);
	}
	if (!p_inherits.is_empty() && !_find_doc(p_docs, p_supplemental_docs, p_inherits)) {
		return _fail("Base class documentation was not found: " + p_inherits, r_error, ERR_DOES_NOT_EXIST);
	}

	const String query = p_query.strip_edges();
	PackedStringArray candidates;
	for (const KeyValue<String, DocData::ClassDoc> &entry : p_docs->class_list) {
		const DocData::ClassDoc &doc = entry.value;
		if (doc.name.is_empty() || !_source_matches(doc, p_source) ||
				(!p_include_deprecated && doc.is_deprecated) ||
				(!p_inherits.is_empty() && !_inherits_from(p_docs, p_supplemental_docs, doc, p_inherits))) {
			continue;
		}
		candidates.push_back(doc.name);
	}
	for (const KeyValue<String, DocData::ClassDoc> &entry : p_supplemental_docs) {
		const DocData::ClassDoc &doc = entry.value;
		if (doc.name.is_empty() || p_docs->class_list.has(doc.name) || !_source_matches(doc, p_source) ||
				(!p_include_deprecated && doc.is_deprecated) ||
				(!p_inherits.is_empty() && !_inherits_from(p_docs, p_supplemental_docs, doc, p_inherits))) {
			continue;
		}
		candidates.push_back(doc.name);
	}
	candidates.sort();

	PackedStringArray names;
	if (query.is_empty()) {
		for (int i = 0; i < MIN(p_limit, candidates.size()); i++) {
			names.push_back(candidates[i]);
		}
	} else {
		FuzzySearch fuzzy_search;
		fuzzy_search.set_query(query);
		fuzzy_search.max_results = p_limit;
		fuzzy_search.allow_subsequences = true;
		fuzzy_search.max_misses = 2;
		Vector<FuzzySearchResult> matches;
		fuzzy_search.search_all(candidates, matches);
		for (const FuzzySearchResult &match : matches) {
			names.push_back(match.target);
		}
	}

	Array matches;
	for (const String &name : names) {
		const DocData::ClassDoc *doc = _find_doc(p_docs, p_supplemental_docs, name);
		if (doc) {
			matches.push_back(_make_search_result(*doc));
		}
	}
	r_result["query"] = query;
	r_result["source"] = p_source;
	if (!p_inherits.is_empty()) {
		r_result["inherits"] = p_inherits;
	}
	r_result["count"] = matches.size();
	r_result["matches"] = matches;
	return OK;
}

Error render(DocTools *p_docs, const String &p_class_name, const String &p_view, const String &p_section,
		const String &p_member, Dictionary &r_result, String *r_error) {
	r_result = Dictionary();
	if (r_error) {
		*r_error = String();
	}
	if (!p_docs) {
		return _fail("Editor class documentation is not available.", r_error, ERR_UNCONFIGURED);
	}
	const DocData::ClassDoc *doc = p_docs->class_list.getptr(p_class_name);
	if (!doc) {
		return _fail("Class documentation was not found: " + p_class_name, r_error, ERR_DOES_NOT_EXIST);
	}
	if (doc->is_script_doc) {
		return _fail("Use godot.script.get for authoritative project script documentation.", r_error, ERR_UNAVAILABLE);
	}
	if (p_section == "overview" && !p_member.is_empty()) {
		return _fail("member cannot be used with the overview section.", r_error);
	}
	if (p_section == "all" && !p_member.is_empty()) {
		return _fail("Select a specific section when requesting one member.", r_error);
	}

	r_result["name"] = doc->name;
	r_result["source"] = doc->is_script_doc ? "script" : "native";
	r_result["inherits"] = doc->inherits;
	if (doc->is_script_doc && !doc->script_path.is_empty()) {
		r_result["scriptPath"] = doc->script_path;
	}
	if (!doc->api_type.is_empty()) {
		r_result["apiType"] = doc->api_type;
	}
	if (!doc->brief_description.is_empty()) {
		r_result["brief"] = _documentation_text(doc->brief_description);
	}
	if (!doc->description.is_empty()) {
		r_result["description"] = _documentation_text(doc->description);
	}
	_add_status(r_result, doc->is_deprecated, doc->deprecated_message,
			doc->is_experimental, doc->experimental_message);

	Array inheritance;
	String base_name = doc->inherits;
	for (int depth = 0; !base_name.is_empty() && depth < 256; depth++) {
		inheritance.push_back(base_name);
		const DocData::ClassDoc *base_doc = p_docs->class_list.getptr(base_name);
		if (!base_doc) {
			break;
		}
		base_name = base_doc->inherits;
	}
	r_result["inheritance"] = inheritance;
	r_result["counts"] = _make_counts(*doc);
	if (!doc->tutorials.is_empty()) {
		Array tutorials;
		for (const DocData::TutorialDoc &tutorial : doc->tutorials) {
			Dictionary item;
			item["title"] = tutorial.title;
			item["url"] = tutorial.link;
			tutorials.push_back(item);
		}
		r_result["tutorials"] = tutorials;
	}
	if (p_section == "overview") {
		return OK;
	}

	const bool full = p_view == "full";
	bool matched = p_member.is_empty();
	if (p_section == "all" || p_section == "constructors") {
		r_result["constructors"] = _select_members(doc->constructors, p_member,
				[full](const DocData::MethodDoc &method) { return _make_method(method, "constructor", full); }, matched);
	}
	if (p_section == "all" || p_section == "methods") {
		r_result["methods"] = _select_members(doc->methods, p_member,
				[full](const DocData::MethodDoc &method) { return _make_method(method, "method", full); }, matched);
	}
	if (p_section == "all" || p_section == "operators") {
		r_result["operators"] = _select_members(doc->operators, p_member,
				[full](const DocData::MethodDoc &method) { return _make_method(method, "operator", full); }, matched);
	}
	if (p_section == "all" || p_section == "signals") {
		r_result["signals"] = _select_members(doc->signals, p_member,
				[full](const DocData::MethodDoc &method) { return _make_method(method, "signal", full); }, matched);
	}
	if (p_section == "all" || p_section == "properties") {
		r_result["properties"] = _select_members(doc->properties, p_member,
				[full](const DocData::PropertyDoc &property) { return _make_property(property, full); }, matched);
	}
	if (p_section == "all" || p_section == "constants") {
		r_result["constants"] = _make_constants(*doc, p_member, full, matched);
	}
	if (p_section == "all" || p_section == "enums") {
		r_result["enums"] = _make_enums(*doc, p_member, full, matched);
	}
	if (p_section == "all" || p_section == "themeItems") {
		r_result["themeItems"] = _select_members(doc->theme_properties, p_member,
				[full](const DocData::ThemeItemDoc &item) { return _make_theme_item(item, full); }, matched);
	}
	if (p_section == "all" || p_section == "annotations") {
		r_result["annotations"] = _select_members(doc->annotations, p_member,
				[full](const DocData::MethodDoc &method) { return _make_method(method, "annotation", full); }, matched);
	}
	if (!matched) {
		return _fail("Class member documentation was not found: " + p_member, r_error, ERR_DOES_NOT_EXIST);
	}
	return OK;
}

} // namespace MCPClassDocumentation
