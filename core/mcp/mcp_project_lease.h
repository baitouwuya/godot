/**************************************************************************/
/*  mcp_project_lease.h                                                   */
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

#pragma once

#include "core/mcp/mcp_discovery.h"
#include "core/os/process_id.h"

class MCPProjectLeaseHealthProbe {
public:
	virtual bool is_healthy(const MCPDiscoveryRecord &p_owner) const = 0;
	virtual ~MCPProjectLeaseHealthProbe() = default;
};

class MCPProjectLeaseProcessProbe {
public:
	virtual bool is_process_running(ProcessID p_pid) const = 0;
	virtual ~MCPProjectLeaseProcessProbe() = default;
};

class MCPOSProjectLeaseProcessProbe final : public MCPProjectLeaseProcessProbe {
public:
	bool is_process_running(ProcessID p_pid) const override;
};

class MCPProjectLease {
	static constexpr const char *LOCK_DIRECTORY_NAME = "mcp.lock";
	static constexpr const char *OWNER_FILE_NAME = "owner.json";
	static constexpr const char *RECLAIM_DIRECTORY_NAME = "reclaim";

	MCPProjectIdentity identity;
	String project_state_directory;
	String lock_directory;
	String owner_file;
	String reclaim_directory;
	String owned_instance_id;
	uint64_t startup_grace_ms = 5000;
	bool acquired = false;

	bool _record_belongs_to_identity(const MCPDiscoveryRecord &p_record) const;
	bool _same_owner_revision(const MCPDiscoveryRecord &p_left, const MCPDiscoveryRecord &p_right) const;
	bool _is_in_startup_grace(const MCPDiscoveryRecord &p_record, uint64_t p_now_unix_ms) const;
	bool _is_lock_directory_in_startup_grace(uint64_t p_now_unix_ms) const;
	void _remove_reclaim_marker() const;
	Error _publish_owner(const MCPDiscoveryRecord &p_record, String *r_error);
	Error _try_reclaim(const MCPDiscoveryRecord *p_initial_owner, const MCPProjectLeaseHealthProbe &p_health_probe, const MCPProjectLeaseProcessProbe &p_process_probe, uint64_t p_now_unix_ms, MCPDiscoveryRecord *r_conflicting_owner, String *r_error);

public:
	static String get_default_project_state_directory(const MCPProjectIdentity &p_identity);

	explicit MCPProjectLease(const MCPProjectIdentity &p_identity, const String &p_project_state_directory = String(), uint64_t p_startup_grace_ms = 5000);
	~MCPProjectLease();

	MCPProjectLease(const MCPProjectLease &) = delete;
	MCPProjectLease &operator=(const MCPProjectLease &) = delete;

	Error acquire(const MCPDiscoveryRecord &p_owner, const MCPProjectLeaseHealthProbe &p_health_probe, MCPDiscoveryRecord *r_conflicting_owner = nullptr, String *r_error = nullptr, uint64_t p_now_unix_ms = 0, const MCPProjectLeaseProcessProbe *p_process_probe = nullptr);
	Error refresh(const MCPDiscoveryRecord &p_owner, String *r_error = nullptr, uint64_t p_now_unix_ms = 0);
	Error read_owner(MCPDiscoveryRecord &r_owner, String *r_error = nullptr) const;
	Error release(String *r_error = nullptr);

	bool is_acquired() const { return acquired; }
	const String &get_lock_directory() const { return lock_directory; }
	const String &get_owner_file() const { return owner_file; }
};
