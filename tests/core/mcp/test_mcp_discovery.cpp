/**************************************************************************/
/*  test_mcp_discovery.cpp                                                */
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

TEST_FORCE_LINK(test_mcp_discovery);

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/mcp/mcp_discovery.h"

namespace TestMCPDiscovery {

TEST_CASE("[MCP][Discovery] Hides authentication secrets by default") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_discovery", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory->make_dir("project") == OK);

	MCPProjectIdentity identity;
	REQUIRE(MCPProjectIdentity::create(temporary_directory->get_current_dir().path_join("project"), identity) == OK);
	MCPDiscoveryRecord record = MCPDiscoveryRecord::create(identity, "streamable-http", "http://127.0.0.1:32123/mcp", "2025-06-18", "private-token", 1000);

	const Dictionary public_dictionary = record.to_dictionary();
	CHECK_FALSE(public_dictionary.has("authSecret"));
	CHECK(record.to_dictionary(true).has("authSecret"));
	Dictionary invalid_dictionary = public_dictionary.duplicate();
	invalid_dictionary["instanceId"] = "../unsafe";
	MCPDiscoveryRecord invalid_record;
	CHECK(MCPDiscoveryRecord::from_dictionary(invalid_dictionary, invalid_record) == ERR_INVALID_DATA);

	const String discovery_directory = temporary_directory->get_current_dir().path_join("records");
	REQUIRE(MCPDiscovery::write_record(discovery_directory, record) == OK);
	MCPDiscoveryRecord public_record;
	REQUIRE(MCPDiscovery::read_record(discovery_directory, identity.project_id, public_record) == OK);
	CHECK(public_record.endpoint == record.endpoint);
	CHECK(public_record.auth_secret.is_empty());

	REQUIRE(MCPDiscovery::write_record(discovery_directory, record, true) == OK);
	MCPDiscoveryRecord private_record;
	REQUIRE(MCPDiscovery::read_record(discovery_directory, identity.project_id, private_record) == OK);
	CHECK(private_record.auth_secret == "private-token");
}

TEST_CASE("[MCP][Discovery] Keeps different projects in independent records") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_discovery_projects", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory->make_dir("project_a") == OK);
	REQUIRE(temporary_directory->make_dir("project_b") == OK);

	MCPProjectIdentity identity_a;
	MCPProjectIdentity identity_b;
	REQUIRE(MCPProjectIdentity::create(temporary_directory->get_current_dir().path_join("project_a"), identity_a) == OK);
	REQUIRE(MCPProjectIdentity::create(temporary_directory->get_current_dir().path_join("project_b"), identity_b) == OK);
	REQUIRE(identity_a.project_id != identity_b.project_id);

	const String discovery_directory = temporary_directory->get_current_dir().path_join("records");
	MCPDiscoveryRecord record_a = MCPDiscoveryRecord::create(identity_a, "streamable-http", "http://127.0.0.1:31001/mcp", "2025-06-18", String(), 1000);
	MCPDiscoveryRecord record_b = MCPDiscoveryRecord::create(identity_b, "streamable-http", "http://127.0.0.1:31002/mcp", "2025-06-18", String(), 1000);
	REQUIRE(MCPDiscovery::write_record(discovery_directory, record_a) == OK);
	REQUIRE(MCPDiscovery::write_record(discovery_directory, record_b) == OK);

	MCPDiscoveryRecord loaded_a;
	MCPDiscoveryRecord loaded_b;
	REQUIRE(MCPDiscovery::read_record(discovery_directory, identity_a.project_id, loaded_a) == OK);
	REQUIRE(MCPDiscovery::read_record(discovery_directory, identity_b.project_id, loaded_b) == OK);
	CHECK(loaded_a.endpoint == record_a.endpoint);
	CHECK(loaded_b.endpoint == record_b.endpoint);
}

TEST_CASE("[MCP][Discovery] Removes records only for the expected instance") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_discovery_remove", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory->make_dir("project") == OK);

	MCPProjectIdentity identity;
	REQUIRE(MCPProjectIdentity::create(temporary_directory->get_current_dir().path_join("project"), identity) == OK);
	MCPDiscoveryRecord record = MCPDiscoveryRecord::create(identity, "streamable-http", "http://127.0.0.1:32001/mcp", "2025-06-18", String(), 1000);
	const String discovery_directory = temporary_directory->get_current_dir().path_join("records");
	REQUIRE(MCPDiscovery::write_record(discovery_directory, record) == OK);

	CHECK(MCPDiscovery::remove_record(discovery_directory, identity.project_id, "another-instance") == ERR_UNAUTHORIZED);
	CHECK(FileAccess::exists(MCPDiscovery::get_record_path(discovery_directory, identity.project_id)));
	CHECK(MCPDiscovery::remove_record(discovery_directory, identity.project_id, identity.instance_id) == OK);
	CHECK_FALSE(FileAccess::exists(MCPDiscovery::get_record_path(discovery_directory, identity.project_id)));
}

} // namespace TestMCPDiscovery
