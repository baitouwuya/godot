/**************************************************************************/
/*  mcp_harness_service.h                                                */
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

#include "core/mcp/mcp_tool_registry.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class MCPHarnessService {
public:
	static constexpr int MAX_ACTIVE_JOBS = 8;
	static constexpr int MAX_SESSION_JOBS = 2;
	static constexpr int MAX_TOOL_CALLS_PER_POLL = 4;

	void set_tool_registry(MCPToolRegistry *p_registry) { tool_registry = p_registry; }

	Dictionary start(const Dictionary &p_arguments, const MCPToolCallContext &p_context);
	Dictionary get_status(const Dictionary &p_arguments, const MCPToolCallContext &p_context) const;
	Dictionary cancel(const Dictionary &p_arguments, const MCPToolCallContext &p_context);
	Dictionary get_report(const Dictionary &p_arguments, const MCPToolCallContext &p_context) const;

	int poll(int p_max_tool_calls = MAX_TOOL_CALLS_PER_POLL);
	void release_session(const String &p_session_id);
	void shutdown();

private:
	enum ChildKind {
		CHILD_NONE,
		CHILD_INPUT_SEQUENCE,
		CHILD_RUNTIME_WAIT,
	};

	struct Job {
		String id;
		String session_id;
		int debugger_session = -1;
		uint64_t runtime_generation = 0;
		MCPToolCallContext context;
		Dictionary plan;
		Array steps;
		Array report_steps;
		Array assertions;
		Dictionary failure;
		Dictionary evidence;
		String state = "running";
		int current_step = 0;
		ChildKind child_kind = CHILD_NONE;
		String child_id;
		bool cancel_requested = false;
		bool evidence_screenshot = false;
		int evidence_phase = 0;
	};

	MCPToolRegistry *tool_registry = nullptr;
	HashMap<String, Job> jobs;
	Vector<String> job_order;
	uint64_t next_job_id = 1;
	int poll_cursor = 0;

	static String _session_id(const MCPToolCallContext &p_context);
	static bool _is_terminal(const Job &p_job);
	static bool _is_error_result(const MCPToolRegistry::CallResult &p_result);
	static Dictionary _result_content(const MCPToolRegistry::CallResult &p_result);
	static Dictionary _call_error(const MCPToolRegistry::CallResult &p_result, const String &p_tool);
	static Dictionary _summary(const Job &p_job);
	static Dictionary _report(const Job &p_job);
	static Dictionary _evidence_reference(const String &p_tool, const MCPToolRegistry::CallResult &p_result);

	Job *_find_owned_job(const Dictionary &p_arguments, const MCPToolCallContext &p_context, Dictionary &r_error);
	const Job *_find_owned_job(const Dictionary &p_arguments, const MCPToolCallContext &p_context, Dictionary &r_error) const;
	int _active_job_count(const String &p_session_id = String()) const;
	void _prune_jobs();
	Dictionary _prepare_arguments(const Job &p_job, const Dictionary &p_step) const;
	MCPToolRegistry::CallResult _call_tool(const Job &p_job, const String &p_tool, const Dictionary &p_arguments) const;
	void _record_step_start(Job &r_job, const Dictionary &p_step, const Dictionary &p_arguments);
	void _record_step_end(Job &r_job, const MCPToolRegistry::CallResult &p_result, bool p_passed);
	void _begin_failure(Job &r_job, const Dictionary &p_failure);
	void _complete_job(Job &r_job);
	int _advance_job(Job &r_job);
	int _advance_child(Job &r_job);
	int _advance_cancellation(Job &r_job);
	int _advance_evidence(Job &r_job);
};
