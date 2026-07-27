/**************************************************************************/
/*  mcp_tool_utils.h                                                      */
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

#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

struct PropertyInfo;

namespace MCPToolUtils {

enum ToolBehavior {
	TOOL_READ_ONLY,
	TOOL_ADDITIVE,
	TOOL_DESTRUCTIVE,
};

Dictionary make_tool_definition(const String &p_name, const String &p_description, const Dictionary &p_input_schema, ToolBehavior p_behavior, const Dictionary &p_output_schema = Dictionary());
Dictionary make_property_schema(const String &p_type, const String &p_description);
Dictionary make_enum_schema(const PackedStringArray &p_values, const String &p_description, const String &p_default);
Dictionary make_object_schema(const Dictionary &p_properties = Dictionary(), const PackedStringArray &p_required = PackedStringArray(), bool p_allow_additional_properties = false);
Dictionary make_property_description(const PropertyInfo &p_property);
Dictionary make_success_result(const Dictionary &p_structured_content);
Dictionary make_error_result(const String &p_code, const String &p_message, const Dictionary &p_details = Dictionary());
bool has_only_arguments(const Dictionary &p_arguments, const PackedStringArray &p_allowed_names, String &r_unknown_name);
bool try_get_json_integer(const Variant &p_value, int64_t p_minimum, int64_t p_maximum, int64_t &r_value);

} // namespace MCPToolUtils
