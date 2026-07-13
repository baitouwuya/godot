/**************************************************************************/
/*  test_mcp_script_buffer.cpp                                           */
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
#include "core/io/file_access.h"
#include "editor/mcp/providers/mcp_script_buffer.h"
#include "scene/gui/code_edit.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_script_buffer);

namespace TestMCPScriptBuffer {

static Error _write_text(const String &p_path, const String &p_text) {
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return open_error == OK ? ERR_CANT_OPEN : open_error;
	}
	return file->store_string(p_text) ? OK : ERR_FILE_CANT_WRITE;
}

TEST_CASE("[SceneTree][MCP][Provider] Script replacement is one native TextEdit undo group") {
	CodeEdit *text_edit = memnew(CodeEdit);
	text_edit->set_editable(true);
	text_edit->set_text("extends Node\nvar value = 1\n");
	text_edit->clear_undo_history();
	text_edit->tag_saved_version();
	const uint32_t saved_revision = text_edit->get_saved_version();

	MCPScriptBuffer::apply_text_edit(text_edit, "extends Node\nvar value = 2\n");
	CHECK(text_edit->get_text() == "extends Node\nvar value = 2\n");
	CHECK(text_edit->get_version() != saved_revision);
	CHECK(text_edit->has_undo());

	text_edit->undo();
	CHECK(text_edit->get_text() == "extends Node\nvar value = 1\n");
	memdelete(text_edit);
}

TEST_CASE("[MCP][Provider] Script paths are limited to external gd files") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_script_root", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());
	REQUIRE(temporary_directory->make_dir_recursive("project/scripts") == OK);
	const String project_root = temporary_directory->get_current_dir().path_join("project");

	String resource_path;
	String absolute_path;
	String error;
	REQUIRE(MCPScriptBuffer::normalize_path_for_root(
					"res://scripts/example.gd", project_root, resource_path, absolute_path, &error) == OK);
	CHECK(resource_path == "res://scripts/example.gd");
	CHECK(absolute_path == project_root.path_join("scripts/example.gd").simplify_path());
	CHECK(error.is_empty());

	CHECK(MCPScriptBuffer::normalize_path_for_root(
				  "res://scripts/example.txt", project_root, resource_path, absolute_path, &error) == ERR_INVALID_PARAMETER);
	CHECK(resource_path.is_empty());
	CHECK(absolute_path.is_empty());
	CHECK(MCPScriptBuffer::normalize_path_for_root(
				  "res://../escape.gd", project_root, resource_path, absolute_path, &error) != OK);
	CHECK(resource_path.is_empty());
	CHECK(absolute_path.is_empty());
}

TEST_CASE("[MCP][Provider] Script paths accept project built-in GDScript subresources") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_script_builtin_root", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());
	REQUIRE(temporary_directory->make_dir("project") == OK);
	const String project_root = temporary_directory->get_current_dir().path_join("project");

	String resource_path;
	String absolute_path;
	String error;
	bool built_in = false;
	REQUIRE(MCPScriptBuffer::resolve_script_path_for_root(
				"res://main.tscn::GDScript_actor", project_root, resource_path, absolute_path, built_in, &error) == OK);
	CHECK(built_in);
	CHECK(resource_path == "res://main.tscn::GDScript_actor");
	CHECK(absolute_path == project_root.path_join("main.tscn").simplify_path());
	CHECK(error.is_empty());

	CHECK(MCPScriptBuffer::resolve_script_path_for_root(
			  "res://main.tscn::", project_root, resource_path, absolute_path, built_in, &error) == ERR_INVALID_PARAMETER);
	CHECK(MCPScriptBuffer::resolve_script_path_for_root(
			  "res://main.tscn::nested/id", project_root, resource_path, absolute_path, built_in, &error) == ERR_INVALID_PARAMETER);
	CHECK(MCPScriptBuffer::resolve_script_path_for_root(
			  "res://../outside.tscn::GDScript_actor", project_root, resource_path, absolute_path, built_in, &error) != OK);
}

TEST_CASE("[MCP][Provider] Authoritative script paths accept project absolutes and reject escape links") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_script_authoritative", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());
	REQUIRE(temporary_directory->make_dir_recursive("project/scripts") == OK);
	REQUIRE(temporary_directory->make_dir("outside") == OK);
	const String root = temporary_directory->get_current_dir();
	const String project_root = root.path_join("project");
	const String script_path = project_root.path_join("scripts/authoritative.gd");
	const String outside_path = root.path_join("outside/outside.gd");
	const String source = "var authoritative: int = 1\n";
	REQUIRE(_write_text(script_path, source) == OK);
	REQUIRE(_write_text(outside_path, "var outside: int = 2\n") == OK);

	String resource_path;
	String absolute_path;
	String error;
	REQUIRE(MCPScriptBuffer::resolve_path_for_root(
				script_path, project_root, resource_path, absolute_path, &error) == OK);
	CHECK(resource_path == "res://scripts/authoritative.gd");
	CHECK(absolute_path == script_path.simplify_path());
	CHECK(MCPScriptBuffer::resolve_path_for_root(
				outside_path, project_root, resource_path, absolute_path, &error) != OK);
	CHECK(MCPScriptBuffer::resolve_path_for_root(
				"res://../outside.gd", project_root, resource_path, absolute_path, &error) != OK);

	Dictionary snapshot;
	REQUIRE(MCPScriptBuffer::read_authoritative_snapshot_for_root(
				script_path, project_root, snapshot, &error) == OK);
	CHECK(snapshot.get("path", String()) == "res://scripts/authoritative.gd");
	CHECK(snapshot.get("text", String()) == source);
	CHECK(snapshot.get("sha256", String()) == source.sha256_text());
	CHECK(snapshot.get("sourceState", String()) == "disk");
	CHECK_FALSE(bool(snapshot.get("unsaved", true)));

	const String link_path = project_root.path_join("scripts/external_link");
	if (temporary_directory->create_link(root.path_join("outside"), link_path) == OK) {
		CHECK(MCPScriptBuffer::resolve_path_for_root(
					"res://scripts/external_link/outside.gd", project_root, resource_path, absolute_path, &error) != OK);
		CHECK(DirAccess::remove_absolute(link_path) == OK);
	}
}

} // namespace TestMCPScriptBuffer
