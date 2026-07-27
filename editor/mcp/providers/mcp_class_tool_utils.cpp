/**************************************************************************/
/*  mcp_class_tool_utils.cpp                                              */
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

#include "mcp_class_tool_utils.h"

#include "mcp_tool_utils.h"

namespace {

static constexpr int MIN_SEARCH_RESULTS = 1;
static constexpr int MAX_SEARCH_RESULTS = 100;

static PackedStringArray _source_names() {
	return PackedStringArray{ "all", "native", "script" };
}

static PackedStringArray _view_names() {
	return PackedStringArray{ "summary", "full" };
}

static PackedStringArray _section_names() {
	return PackedStringArray{ "overview", "all", "constructors", "methods", "operators", "signals", "properties", "constants", "enums", "themeItems", "annotations" };
}

static bool _get_string(const Dictionary &p_arguments, const StringName &p_name, const String &p_default,
		bool p_allow_empty, String &r_value) {
	const Variant value = p_arguments.get(p_name, p_default);
	if (value.get_type() != Variant::STRING || (!p_allow_empty && String(value).is_empty())) {
		return false;
	}
	r_value = String(value);
	return true;
}

} // namespace

namespace MCPClassToolUtils {

Dictionary search_schema() {
	const MCPClassDocumentation::SearchOptions defaults;
	Dictionary properties;
	properties["query"] = MCPToolUtils::make_property_schema("string", "Optional fuzzy class-name query. Empty lists classes alphabetically.");
	properties["source"] = MCPToolUtils::make_enum_schema(_source_names(), "Filter native or script documentation.", defaults.source);
	properties["inherits"] = MCPToolUtils::make_property_schema("string", "Optional base class; matches descendants at any depth.");
	Dictionary include_deprecated = MCPToolUtils::make_property_schema("boolean", "Include deprecated classes.");
	include_deprecated["default"] = defaults.include_deprecated;
	properties["includeDeprecated"] = include_deprecated;
	Dictionary limit = MCPToolUtils::make_property_schema("integer", vformat("Maximum result count from %d to %d.", MIN_SEARCH_RESULTS, MAX_SEARCH_RESULTS));
	limit["minimum"] = MIN_SEARCH_RESULTS;
	limit["maximum"] = MAX_SEARCH_RESULTS;
	limit["default"] = defaults.limit;
	properties["limit"] = limit;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary documentation_schema() {
	const MCPClassDocumentation::RenderOptions defaults;
	Dictionary properties;
	Dictionary name = MCPToolUtils::make_property_schema("string", "Exact documented class name.");
	name["minLength"] = 1;
	properties["name"] = name;
	properties["view"] = MCPToolUtils::make_enum_schema(_view_names(), "full includes member descriptions; summary keeps compact signatures.", defaults.view);
	properties["section"] = MCPToolUtils::make_enum_schema(_section_names(), "Class documentation section.", defaults.section);
	properties["member"] = MCPToolUtils::make_property_schema("string", "Optional exact member name within the selected section.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name" });
}

Dictionary search_output_schema() {
	Dictionary source = MCPToolUtils::make_property_schema("string", "Applied documentation source filter.");
	source["enum"] = _source_names();
	Dictionary count = MCPToolUtils::make_property_schema("integer", "Number of returned class matches.");
	count["minimum"] = 0;
	Dictionary match;
	match["type"] = "object";
	match["additionalProperties"] = true;
	Dictionary matches = MCPToolUtils::make_property_schema("array", "Matching class summaries.");
	matches["items"] = match;
	matches["maxItems"] = MAX_SEARCH_RESULTS;

	Dictionary properties;
	properties["query"] = MCPToolUtils::make_property_schema("string", "Normalized class search query.");
	properties["source"] = source;
	properties["inherits"] = MCPToolUtils::make_property_schema("string", "Applied inheritance filter when provided.");
	properties["count"] = count;
	properties["matches"] = matches;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "query", "source", "count", "matches" });
}

Dictionary documentation_output_schema() {
	Dictionary string_array = MCPToolUtils::make_property_schema("array", "Ordered class names.");
	string_array["items"] = MCPToolUtils::make_property_schema("string", "Class name.");
	Dictionary counts;
	counts["type"] = "object";
	counts["additionalProperties"] = true;

	Dictionary properties;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Documented class name.");
	properties["source"] = MCPToolUtils::make_property_schema("string", "Documentation source.");
	properties["inherits"] = MCPToolUtils::make_property_schema("string", "Immediate base class name.");
	properties["inheritance"] = string_array;
	properties["counts"] = counts;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name", "source", "inherits", "inheritance", "counts" }, true);
}

bool parse_search_options(const Dictionary &p_arguments, MCPClassDocumentation::SearchOptions &r_options, String &r_error_message) {
	const MCPClassDocumentation::SearchOptions defaults;
	r_options = defaults;
	r_error_message = String();
	const Variant include_deprecated = p_arguments.get("includeDeprecated", defaults.include_deprecated);
	int64_t limit = defaults.limit;
	if (!_get_string(p_arguments, "query", defaults.query, true, r_options.query) ||
			!_get_string(p_arguments, "source", defaults.source, false, r_options.source) ||
			!_get_string(p_arguments, "inherits", defaults.inherits, true, r_options.inherits) ||
			include_deprecated.get_type() != Variant::BOOL ||
			!MCPToolUtils::try_get_json_integer(p_arguments.get("limit", limit), MIN_SEARCH_RESULTS, MAX_SEARCH_RESULTS, limit) ||
			!_source_names().has(r_options.source)) {
		r_error_message = "Class search arguments are invalid.";
		return false;
	}
	r_options.include_deprecated = bool(include_deprecated);
	r_options.limit = int(limit);
	return true;
}

bool parse_render_options(const Dictionary &p_arguments, MCPClassDocumentation::RenderOptions &r_options, String &r_error_message) {
	const MCPClassDocumentation::RenderOptions defaults;
	r_options = defaults;
	r_error_message = String();
	if (!_get_string(p_arguments, "name", String(), false, r_options.class_name) ||
			!_get_string(p_arguments, "view", defaults.view, false, r_options.view) ||
			!_get_string(p_arguments, "section", defaults.section, false, r_options.section) ||
			!_get_string(p_arguments, "member", defaults.member, true, r_options.member) ||
			!_view_names().has(r_options.view)) {
		r_error_message = "Class documentation arguments are invalid.";
		return false;
	}
	if (!_section_names().has(r_options.section)) {
		r_error_message = "Unknown class documentation section: " + r_options.section;
		return false;
	}
	return true;
}

Dictionary make_documentation_error_result(Error p_error, const String &p_message) {
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

} // namespace MCPClassToolUtils
