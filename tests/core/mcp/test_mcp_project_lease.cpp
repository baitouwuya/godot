/**************************************************************************/
/*  test_mcp_project_lease.cpp                                            */
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

TEST_FORCE_LINK(test_mcp_project_lease);

#include "core/io/dir_access.h"
#include "core/mcp/mcp_project_lease.h"

namespace TestMCPProjectLease {

class FixedHealthProbe final : public MCPProjectLeaseHealthProbe {
	bool healthy = false;

public:
	mutable int calls = 0;

	explicit FixedHealthProbe(bool p_healthy) :
			healthy(p_healthy) {}

	bool is_healthy(const MCPDiscoveryRecord &p_owner) const override {
		calls++;
		return healthy && !p_owner.instance_id.is_empty();
	}
};

class FixedProcessProbe final : public MCPProjectLeaseProcessProbe {
	bool running = false;

public:
	mutable int calls = 0;

	explicit FixedProcessProbe(bool p_running) :
			running(p_running) {}

	bool is_process_running(ProcessID p_pid) const override {
		calls++;
		return running && p_pid > 0;
	}
};

void seed_owner(const String &p_state_directory, const MCPDiscoveryRecord &p_owner) {
	const String lock_directory = p_state_directory.path_join("mcp.lock");
	const Error state_directory_error = DirAccess::make_dir_recursive_absolute(p_state_directory);
	const bool state_directory_ready = state_directory_error == OK || DirAccess::exists(p_state_directory);
	REQUIRE(state_directory_ready);
	REQUIRE(DirAccess::make_dir_absolute(lock_directory) == OK);
	REQUIRE(MCPDiscovery::write_record_file(lock_directory.path_join("owner.json"), p_owner) == OK);
}

TEST_CASE("[MCP][ProjectLease] Atomic directory acquisition exposes the current owner") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_lease_atomic", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory->make_dir("project") == OK);

	const String project_path = temporary_directory->get_current_dir().path_join("project");
	MCPProjectIdentity first_identity;
	MCPProjectIdentity second_identity;
	REQUIRE(MCPProjectIdentity::create(project_path, first_identity) == OK);
	REQUIRE(MCPProjectIdentity::create(project_path, second_identity) == OK);
	MCPDiscoveryRecord first_owner = MCPDiscoveryRecord::create(first_identity, "streamable-http", "http://127.0.0.1:29001/mcp", "2025-06-18", String(), 1000);
	MCPDiscoveryRecord second_owner = MCPDiscoveryRecord::create(second_identity, "streamable-http", "http://127.0.0.1:29002/mcp", "2025-06-18", String(), 1000);
	FixedHealthProbe unhealthy_probe(false);
	FixedHealthProbe healthy_probe(true);
	FixedProcessProbe process_probe(false);
	const String state_directory = temporary_directory->get_current_dir().path_join("state");

	MCPProjectLease first_lease(first_identity, state_directory);
	MCPProjectLease second_lease(second_identity, state_directory);
	REQUIRE(first_lease.acquire(first_owner, unhealthy_probe, nullptr, nullptr, 1000, &process_probe) == OK);
	MCPDiscoveryRecord conflict;
	CHECK(second_lease.acquire(second_owner, healthy_probe, &conflict, nullptr, 10000, &process_probe) == ERR_ALREADY_IN_USE);
	CHECK(conflict.instance_id == first_identity.instance_id);
	CHECK(first_lease.release() == OK);
}

TEST_CASE("[MCP][ProjectLease] Requires both process and health probes to fail") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_lease_probes", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory->make_dir("project") == OK);

	const String project_path = temporary_directory->get_current_dir().path_join("project");
	MCPProjectIdentity owner_identity;
	MCPProjectIdentity contender_identity;
	REQUIRE(MCPProjectIdentity::create(project_path, owner_identity) == OK);
	REQUIRE(MCPProjectIdentity::create(project_path, contender_identity) == OK);
	MCPDiscoveryRecord stale_owner = MCPDiscoveryRecord::create(owner_identity, "streamable-http", "http://127.0.0.1:30001/mcp", "2025-06-18", String(), 1000);
	MCPDiscoveryRecord contender = MCPDiscoveryRecord::create(contender_identity, "streamable-http", "http://127.0.0.1:30002/mcp", "2025-06-18", String(), 20000);

	SUBCASE("A running PID keeps the lease") {
		const String state_directory = temporary_directory->get_current_dir().path_join("state_pid");
		seed_owner(state_directory, stale_owner);
		FixedHealthProbe health_probe(false);
		FixedProcessProbe process_probe(true);
		MCPProjectLease lease(contender_identity, state_directory);
		MCPDiscoveryRecord conflict;
		CHECK(lease.acquire(contender, health_probe, &conflict, nullptr, 20000, &process_probe) == ERR_ALREADY_IN_USE);
		CHECK(conflict.instance_id == owner_identity.instance_id);
		CHECK(process_probe.calls == 1);
		CHECK(health_probe.calls == 0);
	}

	SUBCASE("A healthy endpoint keeps the lease") {
		const String state_directory = temporary_directory->get_current_dir().path_join("state_health");
		seed_owner(state_directory, stale_owner);
		FixedHealthProbe health_probe(true);
		FixedProcessProbe process_probe(false);
		MCPProjectLease lease(contender_identity, state_directory);
		CHECK(lease.acquire(contender, health_probe, nullptr, nullptr, 20000, &process_probe) == ERR_ALREADY_IN_USE);
		CHECK(process_probe.calls == 1);
		CHECK(health_probe.calls == 1);
	}

	SUBCASE("A dead PID and unhealthy endpoint allow stale recovery") {
		const String state_directory = temporary_directory->get_current_dir().path_join("state_stale");
		seed_owner(state_directory, stale_owner);
		FixedHealthProbe health_probe(false);
		FixedProcessProbe process_probe(false);
		MCPProjectLease lease(contender_identity, state_directory);
		REQUIRE(lease.acquire(contender, health_probe, nullptr, nullptr, 20000, &process_probe) == OK);
		MCPDiscoveryRecord acquired_owner;
		REQUIRE(lease.read_owner(acquired_owner) == OK);
		CHECK(acquired_owner.instance_id == contender_identity.instance_id);
		CHECK(process_probe.calls == 2);
		CHECK(health_probe.calls == 2);
		CHECK(lease.release() == OK);
	}
}

TEST_CASE("[MCP][ProjectLease] Does not reclaim a lock without verifiable owner probes") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_lease_incomplete", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory->make_dir("project") == OK);

	const String project_path = temporary_directory->get_current_dir().path_join("project");
	MCPProjectIdentity contender_identity;
	REQUIRE(MCPProjectIdentity::create(project_path, contender_identity) == OK);
	const MCPDiscoveryRecord contender = MCPDiscoveryRecord::create(
			contender_identity,
			"streamable-http",
			"http://127.0.0.1:30501/mcp",
			"2025-11-25",
			String(),
			20000);
	REQUIRE(contender.is_valid(false));

	const String state_directory = temporary_directory->get_current_dir().path_join("state_incomplete");
	const String lock_directory = state_directory.path_join("mcp.lock");
	REQUIRE(DirAccess::make_dir_recursive_absolute(state_directory) == OK);
	REQUIRE(DirAccess::make_dir_absolute(lock_directory) == OK);

	FixedHealthProbe health_probe(false);
	FixedProcessProbe process_probe(false);
	MCPProjectLease lease(contender_identity, state_directory, 0);
	String lease_error;
	const Error acquire_error = lease.acquire(contender, health_probe, nullptr, &lease_error, 20000, &process_probe);
	INFO(lease_error);
	CHECK(acquire_error == ERR_ALREADY_IN_USE);
	CHECK(health_probe.calls == 0);
	CHECK(process_probe.calls == 0);
	CHECK_FALSE(lease_error.is_empty());
	CHECK(DirAccess::exists(lock_directory));
	CHECK(DirAccess::remove_absolute(lock_directory) == OK);
}

TEST_CASE("[MCP][ProjectLease] Refreshes a dynamic endpoint without publishing its secret") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_lease_refresh", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory->make_dir("project") == OK);

	MCPProjectIdentity identity;
	REQUIRE(MCPProjectIdentity::create(temporary_directory->get_current_dir().path_join("project"), identity) == OK);
	MCPDiscoveryRecord owner = MCPDiscoveryRecord::create(identity, "streamable-http", String(), "2025-06-18", "private-token", 1000);
	FixedHealthProbe health_probe(false);
	FixedProcessProbe process_probe(false);
	const String state_directory = temporary_directory->get_current_dir().path_join("state");
	MCPProjectLease lease(identity, state_directory);
	REQUIRE(lease.acquire(owner, health_probe, nullptr, nullptr, 1000, &process_probe) == OK);

	owner.endpoint = "http://127.0.0.1:34567/mcp";
	REQUIRE(lease.refresh(owner, nullptr, 2000) == OK);
	MCPDiscoveryRecord refreshed_owner;
	REQUIRE(lease.read_owner(refreshed_owner) == OK);
	CHECK(refreshed_owner.endpoint == owner.endpoint);
	CHECK(refreshed_owner.heartbeat_at_unix_ms == 2000);
	CHECK(refreshed_owner.started_at_unix_ms == 1000);
	CHECK(refreshed_owner.auth_secret.is_empty());
	CHECK(lease.release() == OK);
}

TEST_CASE("[MCP][ProjectLease] Different projects acquire independent leases") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_lease_projects", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory->make_dir("project_a") == OK);
	REQUIRE(temporary_directory->make_dir("project_b") == OK);

	MCPProjectIdentity identity_a;
	MCPProjectIdentity identity_b;
	REQUIRE(MCPProjectIdentity::create(temporary_directory->get_current_dir().path_join("project_a"), identity_a) == OK);
	REQUIRE(MCPProjectIdentity::create(temporary_directory->get_current_dir().path_join("project_b"), identity_b) == OK);
	MCPDiscoveryRecord owner_a = MCPDiscoveryRecord::create(identity_a, "streamable-http", "http://127.0.0.1:33001/mcp", "2025-06-18", String(), 1000);
	MCPDiscoveryRecord owner_b = MCPDiscoveryRecord::create(identity_b, "streamable-http", "http://127.0.0.1:33002/mcp", "2025-06-18", String(), 1000);
	FixedHealthProbe health_probe(false);
	FixedProcessProbe process_probe(false);

	MCPProjectLease lease_a(identity_a);
	MCPProjectLease lease_b(identity_b);
	REQUIRE(lease_a.acquire(owner_a, health_probe, nullptr, nullptr, 1000, &process_probe) == OK);
	REQUIRE(lease_b.acquire(owner_b, health_probe, nullptr, nullptr, 1000, &process_probe) == OK);
	CHECK(lease_a.get_lock_directory() != lease_b.get_lock_directory());
	CHECK(lease_a.release() == OK);
	CHECK(lease_b.release() == OK);
}

TEST_CASE("[MCP][ProjectLease] Release verifies the instance nonce") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_lease_release", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory->make_dir("project") == OK);

	const String project_path = temporary_directory->get_current_dir().path_join("project");
	MCPProjectIdentity identity;
	MCPProjectIdentity replacement_identity;
	REQUIRE(MCPProjectIdentity::create(project_path, identity) == OK);
	REQUIRE(MCPProjectIdentity::create(project_path, replacement_identity) == OK);
	MCPDiscoveryRecord owner = MCPDiscoveryRecord::create(identity, "streamable-http", "http://127.0.0.1:34001/mcp", "2025-06-18", String(), 1000);
	MCPDiscoveryRecord replacement = MCPDiscoveryRecord::create(replacement_identity, "streamable-http", "http://127.0.0.1:34002/mcp", "2025-06-18", String(), 2000);
	FixedHealthProbe health_probe(false);
	FixedProcessProbe process_probe(false);
	const String state_directory = temporary_directory->get_current_dir().path_join("state");
	MCPProjectLease lease(identity, state_directory);
	REQUIRE(lease.acquire(owner, health_probe, nullptr, nullptr, 1000, &process_probe) == OK);
	REQUIRE(MCPDiscovery::write_record_file(lease.get_owner_file(), replacement) == OK);

	CHECK(lease.release() == ERR_UNAUTHORIZED);
	CHECK(DirAccess::exists(lease.get_lock_directory()));
	CHECK(DirAccess::remove_absolute(lease.get_owner_file()) == OK);
	CHECK(DirAccess::remove_absolute(lease.get_lock_directory()) == OK);
}

} // namespace TestMCPProjectLease
