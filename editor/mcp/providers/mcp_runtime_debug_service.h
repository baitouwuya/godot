/**************************************************************************/
/*  mcp_runtime_debug_service.h                                           */
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

#include "mcp_runtime_debugger_gateway.h"
#include "mcp_runtime_input_scheduler.h"

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

class MCPDebugCapture;
class ScriptEditorDebugger;

class MCPRuntimeDebugService {
	struct ConditionJobRecord {
		String mcp_session_id;
		int debugger_session = -1;
		uint64_t runtime_generation = 0;
		Dictionary state;
	};
	struct PerformanceJobRecord {
		String mcp_session_id;
		int debugger_session = -1;
		uint64_t runtime_generation = 0;
		Dictionary state;
	};

	MCPRuntimeDebuggerGateway runtime_gateway;
	MCPRuntimeInputScheduler input_scheduler;
	uint64_t next_condition_job_id = 1;
	HashMap<String, ConditionJobRecord> condition_jobs;
	Vector<String> condition_job_order;
	uint64_t next_performance_job_id = 1;
	HashMap<String, PerformanceJobRecord> performance_jobs;
	Vector<String> performance_job_order;

	Dictionary _resolve_session(const Dictionary &p_arguments, bool p_require_generation, ScriptEditorDebugger *&r_debugger,
			int &r_debugger_session, uint64_t &r_runtime_generation) const;
	Dictionary _refresh_tree(ScriptEditorDebugger *p_debugger, int p_timeout_msec) const;
	Dictionary _find_node(ScriptEditorDebugger *p_debugger, const String &p_path, ObjectID &r_object_id, Dictionary &r_node) const;
	Dictionary _refresh_object(ScriptEditorDebugger *p_debugger, ObjectID p_object_id, int p_timeout_msec) const;
	Dictionary _make_session_identity(int p_debugger_session, uint64_t p_runtime_generation) const;
	Dictionary _request_debugger_message(const Dictionary &p_arguments, const String &p_capture, const String &p_operation,
			const Dictionary &p_payload, bool p_require_generation) const;
	Dictionary _request_observation(const Dictionary &p_arguments, const String &p_operation, const Dictionary &p_payload,
			bool p_require_generation = false) const;
	Dictionary _request_condition(const Dictionary &p_arguments, const String &p_operation, const Dictionary &p_payload,
			bool p_require_generation = true) const;
	Dictionary _request_performance(const Dictionary &p_arguments, const String &p_operation, const Dictionary &p_payload,
			bool p_require_generation = true) const;
	void _prune_condition_jobs();
	void _release_condition_session(const String &p_mcp_session_id);
	void _release_all_condition_jobs(const String &p_reason);
	void _prune_performance_jobs();
	void _release_performance_session(const String &p_mcp_session_id);
	void _release_all_performance_jobs(const String &p_reason);
	Dictionary _dispatch_action_events(const Dictionary &p_arguments, const String &p_mcp_session_id, const Array &p_events,
			const Dictionary &p_metadata);
	Dictionary _start_action_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id, const Array &p_steps,
			const Dictionary &p_metadata);
	Dictionary _resolve_action_targets(const Dictionary &p_arguments, const String &p_kind, const Array &p_selectors) const;

public:
	explicit MCPRuntimeDebugService(MCPDebugCapture *p_debug_capture);
	~MCPRuntimeDebugService();

	Dictionary get_state() const;
	Dictionary play(const Dictionary &p_arguments) const;
	Dictionary stop();
	Dictionary set_suspended(const Dictionary &p_arguments, bool p_suspended) const;
	Dictionary next_frame(const Dictionary &p_arguments) const;
	Dictionary debug_control(const Dictionary &p_arguments, const String &p_operation) const;
	Dictionary get_tree(const Dictionary &p_arguments) const;
	Dictionary get_screenshot(const Dictionary &p_arguments) const;
	Dictionary get_viewport_summary(const Dictionary &p_arguments) const;
	Dictionary query_nodes(const Dictionary &p_arguments) const;
	Dictionary get_interactables(const Dictionary &p_arguments) const;
	Dictionary get_node_snapshot(const Dictionary &p_arguments) const;
	Dictionary click_target(const Dictionary &p_arguments, const String &p_mcp_session_id, bool p_double_click);
	Dictionary hover_target(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary focus_target(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary drag_target_to_target(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary type_text(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary scroll_view(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary start_wait(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary get_wait_status(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary cancel_wait(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary start_performance(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary get_performance_status(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary stop_performance(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary get_properties(const Dictionary &p_arguments) const;
	Dictionary set_property(const Dictionary &p_arguments) const;
	Dictionary send_input(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary start_input_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary get_input_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary cancel_input_sequence(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary release_input(const Dictionary &p_arguments, const String &p_mcp_session_id);
	void process_input();
	void release_mcp_session(const String &p_mcp_session_id);
	void shutdown_input();
	void set_observation_plugin(MCPRuntimeObservationDebuggerPlugin *p_plugin) { runtime_gateway.set_response_plugin(p_plugin); }
	MCPRuntimeInputScheduler *get_input_scheduler() { return &input_scheduler; }
};
