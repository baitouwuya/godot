/**************************************************************************/
/*  mcp_file_provider.cpp                                                 */
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

#include "mcp_file_provider.h"

#include "mcp_path_utils.h"
#include "mcp_tool_utils.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "editor/file_system/editor_file_system.h"

namespace {

static Dictionary _create_file_schema() {
	Dictionary overwrite_property = MCPToolUtils::make_property_schema("boolean", "Replace an existing project file.");
	overwrite_property["default"] = false;

	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Project-relative file path beginning with res://.");
	properties["content"] = MCPToolUtils::make_property_schema("string", "UTF-8 text to write.");
	properties["overwrite"] = overwrite_property;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "content" });
}

static Dictionary _create_file_output_schema() {
	Dictionary bytes_written = MCPToolUtils::make_property_schema("integer", "Number of UTF-8 bytes written.");
	bytes_written["minimum"] = 0;
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Created project resource path.");
	properties["bytesWritten"] = bytes_written;
	properties["overwritten"] = MCPToolUtils::make_property_schema("boolean", "Whether an existing file was replaced.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "path", "bytesWritten", "overwritten" });
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPFileProvider::MCPFileProvider(const String &p_project_root) :
		project_root(p_project_root) {
}

MCPFileProvider::~MCPFileProvider() {
	unregister_tools();
}

Error MCPFileProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "File tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.file.create", "Create a UTF-8 text file inside the current Godot project.",
				_create_file_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPFileProvider::create_file), _create_file_output_schema() },
	};
	const Error err = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (err != OK) {
		return err;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPFileProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPFileProvider::create_file(const Dictionary &p_arguments, const Dictionary &) {
	const Variant path_value = p_arguments.get("path", Variant());
	const Variant content_value = p_arguments.get("content", Variant());
	const Variant overwrite_value = p_arguments.get("overwrite", false);
	if (path_value.get_type() != Variant::STRING || content_value.get_type() != Variant::STRING ||
			overwrite_value.get_type() != Variant::BOOL) {
		return MCPToolUtils::make_error_result(
				"INVALID_ARGUMENTS", "Arguments must contain string 'path', string 'content', and optional boolean 'overwrite'.");
	}

	String resource_path;
	String absolute_path;
	String path_error;
	const Error path_result = project_root.is_empty() ? MCPPathUtils::resolve_project_file_path(path_value, resource_path, absolute_path, &path_error) : MCPPathUtils::resolve_project_file_path_for_root(path_value, project_root, resource_path, absolute_path, &path_error);
	if (path_result != OK) {
		return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
	}

	const bool overwrite = overwrite_value;
	const bool existed = FileAccess::exists(absolute_path);
	if (existed && !overwrite) {
		return MCPToolUtils::make_error_result("FILE_EXISTS", "File already exists: " + resource_path);
	}

	const String parent_directory = absolute_path.get_base_dir();
	if (!DirAccess::dir_exists_absolute(parent_directory)) {
		const Error directory_error = DirAccess::make_dir_recursive_absolute(parent_directory);
		if (directory_error != OK) {
			return MCPToolUtils::make_error_result(
					"DIRECTORY_CREATE_FAILED", "Could not create parent directory for: " + resource_path);
		}
	}

	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return MCPToolUtils::make_error_result("FILE_OPEN_FAILED", "Could not open file for writing: " + resource_path);
	}

	const String content = content_value;
	if (!file->store_string(content)) {
		return MCPToolUtils::make_error_result("FILE_WRITE_FAILED", "Could not write file: " + resource_path);
	}
	file->flush();
	file.unref();
	if (project_root.is_empty() && EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->scan_changes();
	}

	Dictionary result;
	result["path"] = resource_path;
	result["bytesWritten"] = content.utf8().length();
	result["overwritten"] = existed;
	return MCPToolUtils::make_success_result(result);
}
