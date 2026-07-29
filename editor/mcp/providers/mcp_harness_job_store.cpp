/**************************************************************************/
/*  mcp_harness_job_store.cpp                                            */
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

#include "mcp_harness_job_store.h"

#include "mcp_harness_report_builder.h"

void MCPHarnessJobStore::_prune() {
	while (job_order.size() >= MAX_RETAINED_JOBS) {
		int remove_index = -1;
		for (int i = 0; i < job_order.size(); i++) {
			const MCPHarnessJob *job = jobs.getptr(job_order[i]);
			if (!job || MCPHarnessReportBuilder::is_terminal(*job)) {
				remove_index = i;
				break;
			}
		}
		if (remove_index < 0) {
			return;
		}
		jobs.erase(job_order[remove_index]);
		job_order.remove_at(remove_index);
	}
	poll_cursor = job_order.is_empty() ? 0 : poll_cursor % job_order.size();
}

MCPHarnessJob *MCPHarnessJobStore::insert(MCPHarnessJob p_job) {
	_prune();
	p_job.id = "harness-" + String::num_uint64(next_job_id++);
	const String job_id = p_job.id;
	jobs.insert(job_id, p_job);
	job_order.push_back(job_id);
	return jobs.getptr(job_id);
}

MCPHarnessJob *MCPHarnessJobStore::find(const String &p_job_id) {
	return jobs.getptr(p_job_id);
}

const MCPHarnessJob *MCPHarnessJobStore::find(const String &p_job_id) const {
	return jobs.getptr(p_job_id);
}

int MCPHarnessJobStore::active_job_count(const String &p_session_id) const {
	int count = 0;
	for (const KeyValue<String, MCPHarnessJob> &entry : jobs) {
		if (!MCPHarnessReportBuilder::is_terminal(entry.value) && (p_session_id.is_empty() || entry.value.session_id == p_session_id)) {
			count++;
		}
	}
	return count;
}

MCPHarnessJob *MCPHarnessJobStore::poll_job_at(int p_offset) {
	if (p_offset < 0 || p_offset >= job_order.size()) {
		return nullptr;
	}
	return jobs.getptr(job_order[(poll_cursor + p_offset) % job_order.size()]);
}

void MCPHarnessJobStore::advance_poll_cursor(int p_visited) {
	poll_cursor = job_order.is_empty() ? 0 : (poll_cursor + MAX(1, p_visited)) % job_order.size();
}

void MCPHarnessJobStore::clear() {
	jobs.clear();
	job_order.clear();
	poll_cursor = 0;
}
