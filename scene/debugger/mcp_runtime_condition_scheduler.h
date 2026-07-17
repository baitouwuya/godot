/**************************************************************************/
/*  mcp_runtime_condition_scheduler.h                                     */
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

#include "core/io/image.h"
#include "core/object/object.h"
#include "core/templates/hash_map.h"

class MCPRuntimeConditionScheduler : public Object {
	GDCLASS(MCPRuntimeConditionScheduler, Object);

	struct Job {
		String id;
		String mcp_session_id;
		String kind;
		Dictionary condition;
		String state = "running";
		String failure;
		uint64_t started_frame = 0;
		uint64_t deadline_frame = 0;
		uint64_t next_poll_frame = 0;
		uint64_t poll_every_frames = 1;
		int evaluations = 0;
		ObjectID baseline_scene_id;
		String baseline_scene_path;
		Ref<Image> baseline_image;
		uint64_t baseline_bytes = 0;
	};

	static constexpr int MAX_ACTIVE_JOBS = 64;
	static constexpr int MAX_SESSION_JOBS = 16;
	static constexpr int MAX_RETAINED_JOBS = 128;
	static constexpr int MAX_EVALUATIONS_PER_FRAME = 16;
	static constexpr int MAX_SELECTOR_VISITS = 20000;
	static constexpr int MAX_TIMEOUT_FRAMES = 36000;
	static constexpr int MAX_POLL_EVERY_FRAMES = 600;
	static constexpr int MAX_ACTIVE_SCREENSHOT_JOBS = 4;
	static constexpr int MAX_SCREENSHOT_DIMENSION = 4096;
	static constexpr uint64_t MAX_SCREENSHOT_BYTES = 16 * 1024 * 1024;
	static constexpr uint64_t MAX_TOTAL_BASELINE_BYTES = 64 * 1024 * 1024;

	static inline MCPRuntimeConditionScheduler *singleton = nullptr;

	HashMap<String, Job> jobs;
	Vector<String> job_order;
	int process_cursor = 0;
	uint64_t total_baseline_bytes = 0;
	uint64_t screenshot_cache_frame = UINT64_MAX;
	Ref<Image> screenshot_cache;
	bool process_connected = false;

	static Error _parse_message(void *p_user, const String &p_message, const Array &p_arguments, bool &r_captured);
	void _ensure_process_connected();
	void _process_frame();
	void _send_response(const String &p_request_id, const String &p_operation, bool p_ok, const String &p_code,
			const String &p_message, const Dictionary &p_data = Dictionary()) const;
	Dictionary _job_state(const Job &p_job) const;
	bool _is_terminal(const Job &p_job) const;
	void _finish_job(Job &r_job, const String &p_state, const String &p_failure = String());
	void _release_baseline(Job &r_job);
	void _prune_jobs();
	int _active_job_count(const String &p_mcp_session_id = String()) const;
	Error _prepare_job(Job &r_job, String &r_code, String &r_error);
	Error _evaluate_job(Job &r_job, bool &r_satisfied, String &r_error);
	Error _get_screenshot(Ref<Image> &r_image, String &r_error);
	double _screenshot_diff_ratio(const Ref<Image> &p_a, const Ref<Image> &p_b) const;
	bool _handle_start(const Array &p_arguments);
	bool _handle_status(const Array &p_arguments);
	bool _handle_cancel(const Array &p_arguments);
	bool _handle_release_session(const Array &p_arguments);
	bool _handle_release_all(const Array &p_arguments);

	friend struct MCPRuntimeConditionSchedulerTestAccess;

protected:
	static void _bind_methods();

public:
	static void initialize();
	static void deinitialize();
	static MCPRuntimeConditionScheduler *get_singleton() { return singleton; }

	MCPRuntimeConditionScheduler();
	~MCPRuntimeConditionScheduler();
};
