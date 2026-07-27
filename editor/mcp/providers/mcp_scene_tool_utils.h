/**************************************************************************/
/*  mcp_scene_tool_utils.h                                                */
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
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

namespace MCPSceneToolUtils {

struct TreeOptions {
	String root_path = ".";
	int max_depth = -1;
	bool include_internal = false;
};

Dictionary node_summary_schema_properties();
Dictionary node_summary_schema(bool p_allow_additional_properties = true);
Dictionary open_schema();
Dictionary tree_schema();
Dictionary save_schema();
Dictionary path_result_schema(const String &p_boolean_name, const String &p_boolean_description);
Dictionary tree_output_schema();
Dictionary selection_output_schema();

bool parse_open_path(const Dictionary &p_arguments, String &r_path, String &r_error_message);
bool parse_save_path(const Dictionary &p_arguments, String &r_path, String &r_error_message);
bool parse_tree_options(const Dictionary &p_arguments, TreeOptions &r_options, String &r_error_message);
Error resolve_scene_file_path(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error = nullptr);

} // namespace MCPSceneToolUtils
