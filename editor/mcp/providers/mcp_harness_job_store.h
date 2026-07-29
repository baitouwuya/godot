/**************************************************************************/
/*  mcp_harness_job_store.h                                              */
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

#include "mcp_harness_job.h"

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class MCPHarnessJobStore {
public:
	MCPHarnessJob *insert(MCPHarnessJob p_job);
	MCPHarnessJob *find(const String &p_job_id);
	const MCPHarnessJob *find(const String &p_job_id) const;
	int active_job_count(const String &p_session_id = String()) const;

	int poll_job_count() const { return job_order.size(); }
	MCPHarnessJob *poll_job_at(int p_offset);
	void advance_poll_cursor(int p_visited);
	Vector<String> get_job_ids() const { return job_order; }
	void clear();

private:
	static constexpr int MAX_RETAINED_JOBS = 32;

	HashMap<String, MCPHarnessJob> jobs;
	Vector<String> job_order;
	uint64_t next_job_id = 1;
	int poll_cursor = 0;

	void _prune();
};
