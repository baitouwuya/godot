/**************************************************************************/
/*  mcp_class_provider.cpp                                                */
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

#include "mcp_class_provider.h"

#include "mcp_class_documentation.h"
#include "mcp_tool_utils.h"

#include "core/doc_data.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "editor/doc/doc_tools.h"
#include "editor/doc/editor_help.h"

namespace {

static Dictionary _property_schema(const String &p_type, const String &p_description) {
	Dictionary property;
	property["type"] = p_type;
	property["description"] = p_description;
	return property;
}

static Dictionary _enum_schema(const PackedStringArray &p_values, const String &p_description, const String &p_default) {
	Dictionary property = _property_schema("string", p_description);
	property["enum"] = p_values;
	property["default"] = p_default;
	return property;
}

static Dictionary _object_schema(const Dictionary &p_properties, const PackedStringArray &p_required) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["required"] = p_required;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _search_schema() {
	Dictionary properties;
	properties["query"] = _property_schema("string", "Optional fuzzy class-name query. Empty lists classes alphabetically.");
	PackedStringArray sources;
	sources.push_back("all");
	sources.push_back("native");
	sources.push_back("script");
	properties["source"] = _enum_schema(sources, "Filter native or script documentation.", "all");
	properties["inherits"] = _property_schema("string", "Optional base class; matches descendants at any depth.");
	Dictionary include_deprecated = _property_schema("boolean", "Include deprecated classes.");
	include_deprecated["default"] = false;
	properties["includeDeprecated"] = include_deprecated;
	Dictionary limit = _property_schema("integer", "Maximum result count from 1 to 100.");
	limit["minimum"] = 1;
	limit["maximum"] = 100;
	limit["default"] = 20;
	properties["limit"] = limit;
	return _object_schema(properties, PackedStringArray());
}

static Dictionary _documentation_schema() {
	Dictionary properties;
	properties["name"] = _property_schema("string", "Exact documented class name.");
	PackedStringArray views;
	views.push_back("summary");
	views.push_back("full");
	properties["view"] = _enum_schema(views, "full includes member descriptions; summary keeps compact signatures.", "summary");
	PackedStringArray sections;
	sections.push_back("overview");
	sections.push_back("all");
	sections.push_back("constructors");
	sections.push_back("methods");
	sections.push_back("operators");
	sections.push_back("signals");
	sections.push_back("properties");
	sections.push_back("constants");
	sections.push_back("enums");
	sections.push_back("themeItems");
	sections.push_back("annotations");
	properties["section"] = _enum_schema(sections, "Class documentation section.", "overview");
	properties["member"] = _property_schema("string", "Optional exact member name within the selected section.");
	PackedStringArray required;
	required.push_back("name");
	return _object_schema(properties, required);
}

static bool _get_string(const Dictionary &p_arguments, const StringName &p_name, const String &p_default,
		bool p_allow_empty, String &r_value) {
	const Variant value = p_arguments.get(p_name, p_default);
	if (value.get_type() != Variant::STRING || (!p_allow_empty && String(value).is_empty())) {
		return false;
	}
	r_value = value;
	return true;
}

static Dictionary _unknown_argument_error(const String &p_name) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown argument: " + p_name);
}

static Dictionary _documentation_error(Error p_error, const String &p_message) {
	String code = "CLASS_DOCUMENTATION_FAILED";
	if (p_error == ERR_DOES_NOT_EXIST) {
		code = "DOCUMENTATION_NOT_FOUND";
	} else if (p_error == ERR_INVALID_PARAMETER) {
		code = "INVALID_ARGUMENTS";
	} else if (p_error == ERR_UNCONFIGURED) {
		code = "DOCUMENTATION_UNAVAILABLE";
	}
	return MCPToolUtils::make_error_result(code, p_message);
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPClassProvider::MCPClassProvider(DocTools *p_doc_tools) :
		doc_tools_override(p_doc_tools) {
}

MCPClassProvider::~MCPClassProvider() {
	unregister_tools();
}

DocTools *MCPClassProvider::_get_doc_tools() const {
	return doc_tools_override ? doc_tools_override : EditorHelp::get_doc_data();
}

Error MCPClassProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Class documentation tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	Error err = p_registry->register_tool(
			MCPToolUtils::make_tool_definition("godot.class.search", "Search native and project script classes.", _search_schema(), MCPToolUtils::TOOL_READ_ONLY),
			callable_mp(this, &MCPClassProvider::search), this, r_error);
	if (err == OK) {
		err = p_registry->register_tool(
			MCPToolUtils::make_tool_definition("godot.class.get_documentation",
					"Read structured Godot class documentation.", _documentation_schema(), MCPToolUtils::TOOL_READ_ONLY),
				callable_mp(this, &MCPClassProvider::get_documentation), this, r_error);
	}
	if (err != OK) {
		p_registry->unregister_tools_for_owner(this);
		return err;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPClassProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPClassProvider::search(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed;
	allowed.push_back("query");
	allowed.push_back("source");
	allowed.push_back("inherits");
	allowed.push_back("includeDeprecated");
	allowed.push_back("limit");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

	String query;
	String source;
	String inherits;
	const Variant include_deprecated = p_arguments.get("includeDeprecated", false);
	int64_t limit = 20;
	if (!_get_string(p_arguments, "query", String(), true, query) ||
			!_get_string(p_arguments, "source", "all", false, source) ||
			!_get_string(p_arguments, "inherits", String(), true, inherits) ||
			include_deprecated.get_type() != Variant::BOOL ||
			!MCPToolUtils::try_get_json_integer(p_arguments.get("limit", 20), 1, 100, limit) ||
			(source != "all" && source != "native" && source != "script")) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Class search arguments are invalid.");
	}

	Dictionary result;
	String documentation_error;
	const Error err = MCPClassDocumentation::search(_get_doc_tools(), query, source, inherits,
			include_deprecated, int(limit), result, &documentation_error);
	return err == OK ? MCPToolUtils::make_success_result(result) : _documentation_error(err, documentation_error);
}

Dictionary MCPClassProvider::get_documentation(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed;
	allowed.push_back("name");
	allowed.push_back("view");
	allowed.push_back("section");
	allowed.push_back("member");
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown_argument)) {
		return _unknown_argument_error(unknown_argument);
	}

	String name;
	String view;
	String section;
	String member;
	if (!_get_string(p_arguments, "name", String(), false, name) ||
			!_get_string(p_arguments, "view", "summary", false, view) ||
			!_get_string(p_arguments, "section", "overview", false, section) ||
			!_get_string(p_arguments, "member", String(), true, member) ||
			(view != "summary" && view != "full")) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Class documentation arguments are invalid.");
	}
	const PackedStringArray valid_sections = {
		"overview", "all", "constructors", "methods", "operators", "signals",
		"properties", "constants", "enums", "themeItems", "annotations"
	};
	if (!valid_sections.has(section)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown class documentation section: " + section);
	}

	DocTools *docs = _get_doc_tools();
	const DocData::ClassDoc *class_doc = docs ? docs->class_list.getptr(name) : nullptr;
	if (class_doc && class_doc->is_script_doc) {
		Dictionary details;
		details["className"] = class_doc->name;
		if (!class_doc->script_path.is_empty()) {
			details["path"] = class_doc->script_path;
		}
		details["tool"] = "godot.script.get";
		return MCPToolUtils::make_error_result("USE_SCRIPT_DOCUMENTATION_TOOL",
				"Use godot.script.get so unsaved ScriptEditor text remains authoritative.", details);
	}

	Dictionary result;
	String documentation_error;
	const Error err = MCPClassDocumentation::render(docs, name, view, section, member, result, &documentation_error);
	return err == OK ? MCPToolUtils::make_success_result(result) : _documentation_error(err, documentation_error);
}
