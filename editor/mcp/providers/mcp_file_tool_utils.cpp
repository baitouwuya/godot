/**************************************************************************/
/*  mcp_file_tool_utils.cpp                                               */
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

#include "mcp_file_tool_utils.h"

#include "mcp_tool_utils.h"

namespace MCPFileToolUtils {

Dictionary create_schema() {
	const CreateOptions defaults;
	Dictionary overwrite = MCPToolUtils::make_property_schema("boolean", "Replace an existing project file.");
	overwrite["default"] = defaults.overwrite;

	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Project-relative file path beginning with res://.");
	properties["content"] = MCPToolUtils::make_property_schema("string", "UTF-8 text to write.");
	properties["overwrite"] = overwrite;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "content" });
}

Dictionary create_output_schema() {
	Dictionary bytes_written = MCPToolUtils::make_property_schema("integer", "Number of UTF-8 bytes written.");
	bytes_written["minimum"] = 0;
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Created project resource path.");
	properties["bytesWritten"] = bytes_written;
	properties["overwritten"] = MCPToolUtils::make_property_schema("boolean", "Whether an existing file was replaced.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "bytesWritten", "overwritten" });
}

bool parse_create_options(const Dictionary &p_arguments, CreateOptions &r_options, String &r_error_message) {
	r_options = CreateOptions();
	r_error_message = String();
	const Variant path = p_arguments.get("path", Variant());
	const Variant content = p_arguments.get("content", Variant());
	const Variant overwrite = p_arguments.get("overwrite", r_options.overwrite);
	if (path.get_type() != Variant::STRING || content.get_type() != Variant::STRING || overwrite.get_type() != Variant::BOOL) {
		r_error_message = "Arguments must contain string 'path', string 'content', and optional boolean 'overwrite'.";
		return false;
	}
	r_options.path = String(path);
	r_options.content = String(content);
	r_options.overwrite = bool(overwrite);
	return true;
}

} // namespace MCPFileToolUtils
