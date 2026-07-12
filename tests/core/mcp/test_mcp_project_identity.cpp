/**************************************************************************/
/*  test_mcp_project_identity.cpp                                         */
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
/* "Software"), to deal in the Software without restriction, including    */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_project_identity);

#include "core/io/dir_access.h"
#include "core/mcp/mcp_project_identity.h"

namespace TestMCPProjectIdentity {

TEST_CASE("[MCP][ProjectIdentity] Canonical aliases produce one project ID") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_identity", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory.is_valid());
	REQUIRE(temporary_directory->make_dir("project") == OK);
	REQUIRE(temporary_directory->make_dir("project/nested") == OK);

	const String project_path = temporary_directory->get_current_dir().path_join("project");
	MCPProjectIdentity direct_identity;
	MCPProjectIdentity aliased_identity;
	String identity_error;
	REQUIRE(MCPProjectIdentity::create(project_path, direct_identity, &identity_error) == OK);
	REQUIRE(MCPProjectIdentity::create(project_path.path_join("nested").path_join(".."), aliased_identity, &identity_error) == OK);

	CHECK(direct_identity.is_valid());
	CHECK(aliased_identity.is_valid());
	CHECK(direct_identity.canonical_path == aliased_identity.canonical_path);
	CHECK(direct_identity.project_id == aliased_identity.project_id);
	CHECK(direct_identity.project_id == direct_identity.canonical_path.sha256_text());
	CHECK(direct_identity.instance_id != aliased_identity.instance_id);
}

TEST_CASE("[MCP][ProjectIdentity] Resolves a project directory symbolic link when supported") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_identity_link", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory.is_valid());
	REQUIRE(temporary_directory->make_dir("project") == OK);

	const String root_path = temporary_directory->get_current_dir();
	const String project_path = root_path.path_join("project");
	const String link_path = root_path.path_join("project_link");
	if (temporary_directory->create_link(project_path, link_path) != OK) {
		return;
	}

	MCPProjectIdentity project_identity;
	MCPProjectIdentity link_identity;
	REQUIRE(MCPProjectIdentity::create(project_path, project_identity) == OK);
	REQUIRE(MCPProjectIdentity::create(link_path, link_identity) == OK);
	CHECK(project_identity.canonical_path == link_identity.canonical_path);
	CHECK(project_identity.project_id == link_identity.project_id);
}

TEST_CASE("[MCP][ProjectIdentity] Rejects missing project directories") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_identity_missing", false, &error);
	REQUIRE(error == OK);

	MCPProjectIdentity identity;
	String identity_error;
	CHECK(MCPProjectIdentity::create(temporary_directory->get_current_dir().path_join("missing"), identity, &identity_error) != OK);
	CHECK_FALSE(identity_error.is_empty());
	CHECK_FALSE(identity.is_valid());
}

} // namespace TestMCPProjectIdentity
