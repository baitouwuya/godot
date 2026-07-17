/**************************************************************************/
/*  mcp_project_lease.cpp                                                 */
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

#include "mcp_project_lease.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace {

void set_error(String *r_error, const String &p_error) {
	if (r_error) {
		*r_error = p_error;
	}
}

uint64_t resolve_now(uint64_t p_now_unix_ms) {
	return p_now_unix_ms == 0 ? MCPDiscovery::get_current_unix_time_ms() : p_now_unix_ms;
}

} // namespace

bool MCPOSProjectLeaseProcessProbe::is_process_running(ProcessID p_pid) const {
	return p_pid > 0 && OS::get_singleton()->is_process_running(p_pid);
}

bool MCPProjectLease::_record_belongs_to_identity(const MCPDiscoveryRecord &p_record) const {
	return p_record.project_id == identity.project_id && p_record.project_path == identity.canonical_path;
}

bool MCPProjectLease::_same_owner_revision(const MCPDiscoveryRecord &p_left, const MCPDiscoveryRecord &p_right) const {
	return p_left.project_id == p_right.project_id &&
			p_left.instance_id == p_right.instance_id &&
			p_left.pid == p_right.pid &&
			p_left.started_at_unix_ms == p_right.started_at_unix_ms &&
			p_left.heartbeat_at_unix_ms == p_right.heartbeat_at_unix_ms &&
			p_left.transport == p_right.transport &&
			p_left.endpoint == p_right.endpoint &&
			p_left.protocol_version == p_right.protocol_version;
}

bool MCPProjectLease::_is_in_startup_grace(const MCPDiscoveryRecord &p_record, uint64_t p_now_unix_ms) const {
	if (startup_grace_ms == 0 || p_record.started_at_unix_ms == 0) {
		return false;
	}
	if (p_now_unix_ms < p_record.started_at_unix_ms) {
		return true;
	}
	return p_now_unix_ms - p_record.started_at_unix_ms < startup_grace_ms;
}

bool MCPProjectLease::_is_lock_directory_in_startup_grace(uint64_t p_now_unix_ms) const {
	if (startup_grace_ms == 0) {
		return false;
	}
	const uint64_t modified_at_unix_ms = FileAccess::get_modified_time(lock_directory) * 1000;
	if (modified_at_unix_ms == 0 || p_now_unix_ms < modified_at_unix_ms) {
		return true;
	}
	return p_now_unix_ms - modified_at_unix_ms < startup_grace_ms;
}

void MCPProjectLease::_remove_reclaim_marker() const {
	if (DirAccess::exists(reclaim_directory)) {
		DirAccess::remove_absolute(reclaim_directory);
	}
}

Error MCPProjectLease::_publish_owner(const MCPDiscoveryRecord &p_record, String *r_error) {
	// The lease record intentionally never publishes an authentication secret.
	return MCPDiscovery::write_record_file(owner_file, p_record, false, r_error);
}

Error MCPProjectLease::_try_reclaim(const MCPDiscoveryRecord *p_initial_owner, const MCPProjectLeaseHealthProbe &p_health_probe, const MCPProjectLeaseProcessProbe &p_process_probe, uint64_t p_now_unix_ms, MCPDiscoveryRecord *r_conflicting_owner, String *r_error) {
	const Error marker_error = DirAccess::make_dir_absolute(reclaim_directory);
	if (marker_error == ERR_ALREADY_EXISTS) {
		set_error(r_error, "Another process is already checking the MCP project lease.");
		return ERR_ALREADY_IN_USE;
	}
	if (marker_error != OK) {
		set_error(r_error, vformat("Cannot create MCP lease reclaim marker '%s'.", reclaim_directory));
		return marker_error;
	}

	MCPDiscoveryRecord current_owner;
	const Error read_error = read_owner(current_owner);
	if (read_error == OK) {
		if (!_record_belongs_to_identity(current_owner)) {
			_remove_reclaim_marker();
			if (r_conflicting_owner) {
				*r_conflicting_owner = current_owner;
			}
			set_error(r_error, "MCP lease owner changed to another project identity during stale recovery.");
			return ERR_INVALID_DATA;
		}
		if (p_initial_owner && !_same_owner_revision(*p_initial_owner, current_owner)) {
			_remove_reclaim_marker();
			if (r_conflicting_owner) {
				*r_conflicting_owner = current_owner;
			}
			set_error(r_error, "MCP lease owner changed while stale recovery was being prepared.");
			return ERR_ALREADY_IN_USE;
		}

		const bool process_running = p_process_probe.is_process_running(current_owner.pid);
		const bool endpoint_healthy = !process_running && p_health_probe.is_healthy(current_owner);
		if (process_running || endpoint_healthy || _is_in_startup_grace(current_owner, p_now_unix_ms)) {
			_remove_reclaim_marker();
			if (r_conflicting_owner) {
				*r_conflicting_owner = current_owner;
			}
			set_error(r_error, "MCP project lease is owned by a live or starting instance.");
			return ERR_ALREADY_IN_USE;
		}
	} else {
		_remove_reclaim_marker();
		set_error(r_error, "MCP project lease metadata changed or became unreadable during stale recovery.");
		return ERR_ALREADY_IN_USE;
	}

	if (FileAccess::exists(owner_file)) {
		const Error remove_owner_error = DirAccess::remove_absolute(owner_file);
		if (remove_owner_error != OK) {
			_remove_reclaim_marker();
			set_error(r_error, vformat("Cannot remove stale MCP lease owner file '%s'.", owner_file));
			return remove_owner_error;
		}
	}

	_remove_reclaim_marker();
	const Error remove_lock_error = DirAccess::remove_absolute(lock_directory);
	if (remove_lock_error != OK) {
		// A concurrent owner refresh can recreate owner.json before this remove.
		// Treat that as contention and never erase the directory recursively.
		set_error(r_error, "MCP project lease changed during stale recovery.");
		return ERR_ALREADY_IN_USE;
	}
	return OK;
}

String MCPProjectLease::get_default_project_state_directory(const MCPProjectIdentity &p_identity) {
	return p_identity.canonical_path.path_join(".godot").path_join("editor");
}

MCPProjectLease::MCPProjectLease(const MCPProjectIdentity &p_identity, const String &p_project_state_directory, uint64_t p_startup_grace_ms) :
		identity(p_identity),
		project_state_directory(p_project_state_directory.is_empty() ? get_default_project_state_directory(p_identity) : p_project_state_directory.simplify_path()),
		startup_grace_ms(p_startup_grace_ms) {
	lock_directory = project_state_directory.path_join(LOCK_DIRECTORY_NAME);
	owner_file = lock_directory.path_join(OWNER_FILE_NAME);
	reclaim_directory = lock_directory.path_join(RECLAIM_DIRECTORY_NAME);
}

MCPProjectLease::~MCPProjectLease() {
	if (acquired) {
		release();
	}
}

Error MCPProjectLease::acquire(const MCPDiscoveryRecord &p_owner, const MCPProjectLeaseHealthProbe &p_health_probe, MCPDiscoveryRecord *r_conflicting_owner, String *r_error, uint64_t p_now_unix_ms, const MCPProjectLeaseProcessProbe *p_process_probe) {
	set_error(r_error, String());
	if (r_conflicting_owner) {
		*r_conflicting_owner = MCPDiscoveryRecord();
	}
	if (acquired) {
		set_error(r_error, "This MCPProjectLease has already acquired its lock.");
		return ERR_ALREADY_IN_USE;
	}
	if (!identity.is_valid() || !_record_belongs_to_identity(p_owner) || p_owner.instance_id != identity.instance_id) {
		set_error(r_error, "MCP lease owner does not match the project identity.");
		return ERR_INVALID_PARAMETER;
	}

	MCPDiscoveryRecord owner = p_owner;
	const uint64_t now_unix_ms = resolve_now(p_now_unix_ms);
	if (owner.pid <= 0) {
		owner.pid = OS::get_singleton()->get_process_id();
	}
	if (owner.started_at_unix_ms == 0) {
		owner.started_at_unix_ms = now_unix_ms;
	}
	owner.heartbeat_at_unix_ms = now_unix_ms;
	if (!owner.is_valid(false)) {
		set_error(r_error, "MCP lease owner record is incomplete.");
		return ERR_INVALID_DATA;
	}

	const Error state_directory_error = DirAccess::make_dir_recursive_absolute(project_state_directory);
	if (state_directory_error != OK && state_directory_error != ERR_ALREADY_EXISTS) {
		set_error(r_error, vformat("Cannot create MCP project state directory '%s'.", project_state_directory));
		return state_directory_error;
	}

	MCPOSProjectLeaseProcessProbe os_process_probe;
	const MCPProjectLeaseProcessProbe &process_probe = p_process_probe ? *p_process_probe : os_process_probe;

	for (int attempt = 0; attempt < 2; attempt++) {
		const Error lock_error = DirAccess::make_dir_absolute(lock_directory);
		if (lock_error == OK) {
			const Error publish_error = _publish_owner(owner, r_error);
			if (publish_error != OK) {
				DirAccess::remove_absolute(lock_directory);
				return publish_error;
			}
			acquired = true;
			owned_instance_id = owner.instance_id;
			return OK;
		}
		if (lock_error != ERR_ALREADY_EXISTS) {
			set_error(r_error, vformat("Cannot acquire MCP project lease directory '%s'.", lock_directory));
			return lock_error;
		}

		MCPDiscoveryRecord existing_owner;
		const Error read_error = read_owner(existing_owner);
		const MCPDiscoveryRecord *initial_owner = read_error == OK ? &existing_owner : nullptr;
		if (initial_owner) {
			if (!_record_belongs_to_identity(existing_owner)) {
				if (r_conflicting_owner) {
					*r_conflicting_owner = existing_owner;
				}
				set_error(r_error, "MCP lease directory contains an owner for another project identity.");
				return ERR_INVALID_DATA;
			}
			const bool process_running = process_probe.is_process_running(existing_owner.pid);
			const bool endpoint_healthy = !process_running && p_health_probe.is_healthy(existing_owner);
			if (process_running || endpoint_healthy || _is_in_startup_grace(existing_owner, now_unix_ms)) {
				if (r_conflicting_owner) {
					*r_conflicting_owner = existing_owner;
				}
				set_error(r_error, "MCP project lease is owned by a live or starting instance.");
				return ERR_ALREADY_IN_USE;
			}
		} else {
			set_error(
					r_error,
					_is_lock_directory_in_startup_grace(now_unix_ms) ? "MCP project lease metadata is incomplete and still within its startup grace period." : "MCP project lease metadata is unreadable, so owner process and endpoint health cannot be verified.");
			return ERR_ALREADY_IN_USE;
		}

		const Error reclaim_error = _try_reclaim(initial_owner, p_health_probe, process_probe, now_unix_ms, r_conflicting_owner, r_error);
		if (reclaim_error != OK) {
			return reclaim_error;
		}
	}

	set_error(r_error, "MCP project lease remained contested after stale recovery.");
	return ERR_ALREADY_IN_USE;
}

Error MCPProjectLease::refresh(const MCPDiscoveryRecord &p_owner, String *r_error, uint64_t p_now_unix_ms) {
	set_error(r_error, String());
	if (!acquired || owned_instance_id.is_empty()) {
		set_error(r_error, "Cannot refresh an MCP project lease that is not acquired.");
		return ERR_UNCONFIGURED;
	}
	if (DirAccess::exists(reclaim_directory)) {
		set_error(r_error, "Cannot refresh MCP project lease while stale recovery is in progress.");
		return ERR_BUSY;
	}

	MCPDiscoveryRecord existing_owner;
	Error error = read_owner(existing_owner, r_error);
	if (error != OK) {
		return error;
	}
	if (existing_owner.instance_id != owned_instance_id || !_record_belongs_to_identity(existing_owner)) {
		acquired = false;
		owned_instance_id = String();
		set_error(r_error, "MCP project lease ownership changed before refresh.");
		return ERR_UNAUTHORIZED;
	}
	if (!_record_belongs_to_identity(p_owner) || p_owner.instance_id != owned_instance_id) {
		set_error(r_error, "MCP lease refresh record does not match the acquired identity.");
		return ERR_INVALID_PARAMETER;
	}

	MCPDiscoveryRecord refreshed_owner = p_owner;
	refreshed_owner.pid = existing_owner.pid;
	refreshed_owner.started_at_unix_ms = existing_owner.started_at_unix_ms;
	refreshed_owner.heartbeat_at_unix_ms = resolve_now(p_now_unix_ms);
	if (!refreshed_owner.is_valid(false)) {
		set_error(r_error, "MCP lease refresh record is incomplete.");
		return ERR_INVALID_DATA;
	}
	return _publish_owner(refreshed_owner, r_error);
}

Error MCPProjectLease::read_owner(MCPDiscoveryRecord &r_owner, String *r_error) const {
	return MCPDiscovery::read_record_file(owner_file, r_owner, r_error);
}

Error MCPProjectLease::release(String *r_error) {
	set_error(r_error, String());
	if (!acquired) {
		return OK;
	}
	if (DirAccess::exists(reclaim_directory)) {
		set_error(r_error, "Cannot release MCP project lease while stale recovery is in progress.");
		return ERR_BUSY;
	}

	MCPDiscoveryRecord existing_owner;
	const Error read_error = read_owner(existing_owner, r_error);
	if (read_error == OK && (existing_owner.instance_id != owned_instance_id || !_record_belongs_to_identity(existing_owner))) {
		acquired = false;
		owned_instance_id = String();
		set_error(r_error, "MCP project lease belongs to another instance.");
		return ERR_UNAUTHORIZED;
	}
	if (read_error != OK && FileAccess::exists(owner_file)) {
		return read_error;
	}

	if (FileAccess::exists(owner_file)) {
		const Error remove_owner_error = DirAccess::remove_absolute(owner_file);
		if (remove_owner_error != OK) {
			set_error(r_error, vformat("Cannot remove MCP lease owner file '%s'.", owner_file));
			return remove_owner_error;
		}
	}
	const Error remove_lock_error = DirAccess::remove_absolute(lock_directory);
	if (remove_lock_error != OK && DirAccess::exists(lock_directory)) {
		set_error(r_error, vformat("Cannot remove MCP lease directory '%s'.", lock_directory));
		return remove_lock_error;
	}

	acquired = false;
	owned_instance_id = String();
	return OK;
}
