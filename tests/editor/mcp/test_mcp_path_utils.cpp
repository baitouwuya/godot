/**************************************************************************/
/*  test_mcp_path_utils.cpp                                               */
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

#include "core/io/dir_access.h"
#include "editor/mcp/providers/mcp_path_utils.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_path_utils);

namespace TestMCPPathUtils {

TEST_CASE("[MCP][Provider] Resource file paths require canonical res paths") {
	String normalized_path;
	String error;
	CHECK(MCPPathUtils::normalize_resource_file_path("res://folder/file.txt", normalized_path, &error) == OK);
	CHECK(normalized_path == "res://folder/file.txt");
	CHECK(error.is_empty());

	const char *invalid_paths[] = {
		"file.txt",
		"user://file.txt",
		"C:/project/file.txt",
		"res://",
		"res://../file.txt",
		"res://folder/./file.txt",
		"res://folder//file.txt",
		"res://folder/file.txt/",
		"res://folder\\file.txt",
		"res://folder/file:name.txt",
	};
	for (const char *path : invalid_paths) {
		CHECK(MCPPathUtils::normalize_resource_file_path(path, normalized_path, &error) != OK);
		CHECK(normalized_path.is_empty());
		CHECK_FALSE(error.is_empty());
	}
}

TEST_CASE("[MCP][Provider] Resolved resource paths stay below the project root") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_path_root", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory->make_dir_recursive("project/tests/editor/mcp") == OK);
	const String project_root = temporary_directory->get_current_dir().path_join("project");

	String resource_path;
	String absolute_path;
	String error;
	REQUIRE(MCPPathUtils::resolve_project_file_path_for_root(
					"res://tests/editor/mcp/generated.txt", project_root, resource_path, absolute_path, &error) == OK);
	CHECK(resource_path == "res://tests/editor/mcp/generated.txt");

	const String project_prefix = project_root.ends_with("/") ? project_root : project_root + "/";
	CHECK(absolute_path.begins_with(project_prefix));
	CHECK(absolute_path.ends_with("/tests/editor/mcp/generated.txt"));
}

TEST_CASE("[MCP][Provider] Resolved resource paths reject links outside the project") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_path_outside", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory.is_valid());
	REQUIRE(temporary_directory->make_dir_recursive("project/.godot/mcp_tests") == OK);
	REQUIRE(temporary_directory->make_dir("outside") == OK);

	const String project_root = temporary_directory->get_current_dir().path_join("project");
	const String outside_directory = temporary_directory->get_current_dir().path_join("outside");
	const String link_name = "path_link";
	const String link_directory = project_root.path_join(".godot/mcp_tests").path_join(link_name);
	if (temporary_directory->create_link(outside_directory, link_directory) != OK) {
		return;
	}

	String resource_path;
	String absolute_path;
	String path_error;
	CHECK(MCPPathUtils::resolve_project_file_path_for_root(
				  "res://.godot/mcp_tests/" + link_name + "/outside.txt",
				  project_root,
				  resource_path,
				  absolute_path,
				  &path_error) != OK);
	CHECK_FALSE(path_error.is_empty());
	CHECK(DirAccess::remove_absolute(link_directory) == OK);
}

} // namespace TestMCPPathUtils
