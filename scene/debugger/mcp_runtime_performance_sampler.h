/**************************************************************************/
/*  mcp_runtime_performance_sampler.h                                     */
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

#include "core/debugger/performance_aggregator.h"
#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class MCPRuntimePerformanceSampler : public Object {
	GDCLASS(MCPRuntimePerformanceSampler, Object);

	struct Job {
		String id;
		String name;
		String mcp_session_id;
		String state = "running";
		String failure;
		uint64_t started_frame = 0;
		uint64_t max_frames = 0;
		PerformanceAggregator aggregator;
	};

	static constexpr int MAX_ACTIVE_JOBS = 8;
	static constexpr int MAX_SESSION_JOBS = 2;
	static constexpr int MAX_RETAINED_JOBS = 32;
	static constexpr int MAX_FRAMES = 36000;
	static constexpr int MAX_CUSTOM_MONITORS = PerformanceAggregator::MAX_METRICS_PER_GROUP;

	static inline MCPRuntimePerformanceSampler *singleton = nullptr;

	HashMap<String, Job> jobs;
	Vector<String> job_order;
	uint64_t last_sample_ticks_usec = 0;
	uint64_t custom_monitors_modification_time = UINT64_MAX;
	Vector<StringName> custom_monitor_names;
	Dictionary builtin_monitor_types;
	Dictionary custom_monitor_types;
	bool process_connected = false;
	bool owns_capture = false;

	static Error _parse_message(void *p_user, const String &p_message, const Array &p_arguments, bool &r_captured);
	void _set_process_connected(bool p_connected);
	void _process_frame();
	void _send_response(const String &p_request_id, const String &p_operation, bool p_ok, const String &p_code,
			const String &p_message, const Dictionary &p_data = Dictionary()) const;
	Dictionary _job_state(const Job &p_job, bool p_include_summary) const;
	bool _is_terminal(const Job &p_job) const;
	int _active_job_count(const String &p_mcp_session_id = String()) const;
	void _prune_jobs();
	Error _create_job(const Dictionary &p_payload, String &r_code, String &r_error);
	void _collect_sample(Dictionary &r_frame_metrics, Dictionary &r_builtin_monitors,
			Dictionary &r_custom_monitors);
	void _push_sample_to_jobs(uint64_t p_frame, const Dictionary &p_frame_metrics,
			const Dictionary &p_builtin_monitors, const Dictionary &p_custom_monitors);
	bool _handle_start(const Array &p_arguments);
	bool _handle_status(const Array &p_arguments);
	bool _handle_stop(const Array &p_arguments);
	bool _handle_release_session(const Array &p_arguments);
	bool _handle_release_all(const Array &p_arguments);

	friend struct MCPRuntimePerformanceSamplerTestAccess;

protected:
	static void _bind_methods();

public:
	static void initialize();
	static void deinitialize();
	static MCPRuntimePerformanceSampler *get_singleton() { return singleton; }

	MCPRuntimePerformanceSampler();
	~MCPRuntimePerformanceSampler();
};
