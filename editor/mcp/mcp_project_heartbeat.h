/**************************************************************************/
/*  mcp_project_heartbeat.h                                               */
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

#include "core/mcp/mcp_discovery.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"

class MCPProjectLease;

class MCPProjectHeartbeat {
	static constexpr uint64_t DEFAULT_INTERVAL_USEC = 2 * 1000 * 1000;
	static constexpr uint64_t STOP_POLL_INTERVAL_USEC = 20 * 1000;

	Thread thread;
	SafeFlag running;
	mutable Mutex state_mutex;
	MCPProjectLease *lease = nullptr;
	String discovery_directory;
	MCPDiscoveryRecord record;
	String failure;
	uint64_t interval_usec = DEFAULT_INTERVAL_USEC;

	static void _thread_main(void *p_userdata);
	void _run();

public:
	Error start(MCPProjectLease *p_lease, const String &p_discovery_directory, const MCPDiscoveryRecord &p_record, uint64_t p_interval_usec = DEFAULT_INTERVAL_USEC);
	void stop();
	bool poll_failure(String &r_failure);
	bool is_running() const { return running.is_set(); }

	~MCPProjectHeartbeat();
};
