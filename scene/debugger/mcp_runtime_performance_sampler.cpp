/**************************************************************************/
/*  mcp_runtime_performance_sampler.cpp                                   */
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

#include "mcp_runtime_performance_sampler.h"

#include "core/config/engine.h"
#include "core/debugger/engine_debugger.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/variant/typed_array.h"
#include "main/performance.h"
#include "scene/main/scene_tree.h"

namespace {

bool _read_performance_integer(const Variant &p_value, int64_t p_minimum, int64_t p_maximum, uint64_t &r_value) {
	if (p_value.get_type() != Variant::INT) {
		return false;
	}
	const int64_t value = p_value;
	if (value < p_minimum || value > p_maximum) {
		return false;
	}
	r_value = uint64_t(value);
	return true;
}

bool _performance_has_only(const Dictionary &p_dictionary, const PackedStringArray &p_allowed, String &r_error) {
	for (const Variant &key_value : p_dictionary.keys()) {
		if (!key_value.is_string() || !p_allowed.has(String(key_value))) {
			r_error = "Unsupported performance job field: " + String(key_value);
			return false;
		}
	}
	return true;
}

} // namespace

void MCPRuntimePerformanceSampler::_bind_methods() {
}

MCPRuntimePerformanceSampler::MCPRuntimePerformanceSampler() {
	if (!singleton) {
		singleton = this;
		owns_capture = true;
		EngineDebugger::register_message_capture("mcp_performance", EngineDebugger::Capture(this, _parse_message));
	}
}

MCPRuntimePerformanceSampler::~MCPRuntimePerformanceSampler() {
	_set_process_connected(false);
	if (owns_capture) {
		EngineDebugger::unregister_message_capture("mcp_performance");
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}

void MCPRuntimePerformanceSampler::initialize() {
	if (!singleton) {
		memnew(MCPRuntimePerformanceSampler);
	}
}

void MCPRuntimePerformanceSampler::deinitialize() {
	if (singleton) {
		memdelete(singleton);
	}
}

void MCPRuntimePerformanceSampler::_set_process_connected(bool p_connected) {
	SceneTree *tree = SceneTree::get_singleton();
	if (!tree || process_connected == p_connected) {
		return;
	}
	const Callable callback = callable_mp(this, &MCPRuntimePerformanceSampler::_process_frame);
	if (p_connected) {
		tree->connect("process_frame", callback);
		last_sample_ticks_usec = 0;
	} else if (tree->is_connected("process_frame", callback)) {
		tree->disconnect("process_frame", callback);
	}
	process_connected = p_connected;
}

void MCPRuntimePerformanceSampler::_send_response(const String &p_request_id, const String &p_operation, bool p_ok,
		const String &p_code, const String &p_message, const Dictionary &p_data) const {
	if (!p_request_id.is_empty() && EngineDebugger::get_singleton()) {
		EngineDebugger::get_singleton()->send_message("mcp_performance:response",
				Array{ p_request_id, p_operation, p_ok, p_code, p_message, p_data });
	}
}

bool MCPRuntimePerformanceSampler::_is_terminal(const Job &p_job) const {
	return p_job.state != "running";
}

Dictionary MCPRuntimePerformanceSampler::_job_state(const Job &p_job, bool p_include_summary) const {
	Dictionary result;
	result["jobId"] = p_job.id;
	if (!p_job.name.is_empty()) {
		result["name"] = p_job.name;
	}
	result["state"] = p_job.state;
	result["startedFrame"] = int64_t(p_job.started_frame);
	result["maxFrames"] = p_job.max_frames > 0 ? Variant(int64_t(p_job.max_frames)) : Variant();
	result["capturedFrames"] = int64_t(p_job.aggregator.get_captured_frames());
	if (!p_job.failure.is_empty()) {
		result["failure"] = p_job.failure;
	}
	if (p_include_summary) {
		Dictionary summary = p_job.aggregator.summary(_is_terminal(p_job));
		Array slow_frames = summary.get("slowFrames", Array());
		for (int i = 0; i < slow_frames.size(); i++) {
			Dictionary frame = slow_frames[i];
			frame.erase("builtinMonitors");
			frame.erase("customMonitors");
			slow_frames[i] = frame;
		}
		if (!slow_frames.is_empty()) {
			summary["slowFrames"] = slow_frames;
		}
		Dictionary monitor_types;
		monitor_types["builtin"] = builtin_monitor_types;
		monitor_types["custom"] = custom_monitor_types;
		summary["monitorTypes"] = monitor_types;
		result["summary"] = summary;
	}
	return result;
}

int MCPRuntimePerformanceSampler::_active_job_count(const String &p_mcp_session_id) const {
	int count = 0;
	for (const KeyValue<String, Job> &entry : jobs) {
		if (!_is_terminal(entry.value) && (p_mcp_session_id.is_empty() || entry.value.mcp_session_id == p_mcp_session_id)) {
			count++;
		}
	}
	return count;
}

void MCPRuntimePerformanceSampler::_prune_jobs() {
	while (jobs.size() >= MAX_RETAINED_JOBS) {
		int remove_index = -1;
		for (int i = 0; i < job_order.size(); i++) {
			const Job *job = jobs.getptr(job_order[i]);
			if (!job || _is_terminal(*job)) {
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
}

Error MCPRuntimePerformanceSampler::_create_job(const Dictionary &p_payload, String &r_code, String &r_error) {
	r_code = String();
	r_error = String();
	if (!_performance_has_only(p_payload, PackedStringArray{ "jobId", "mcpSessionId", "name", "topFrames", "maxFrames" }, r_error)) {
		r_code = "INVALID_ARGUMENTS";
		return ERR_INVALID_PARAMETER;
	}
	const Variant job_id_value = p_payload.get("jobId", Variant());
	const Variant session_id_value = p_payload.get("mcpSessionId", Variant());
	const Variant name_value = p_payload.get("name", String());
	if (job_id_value.get_type() != Variant::STRING || String(job_id_value).is_empty() || String(job_id_value).length() > 128 ||
			session_id_value.get_type() != Variant::STRING || String(session_id_value).is_empty() || String(session_id_value).length() > 256 ||
			name_value.get_type() != Variant::STRING) {
		r_code = "INVALID_ARGUMENTS";
		r_error = "Performance jobs require bounded jobId, mcpSessionId, and name strings.";
		return ERR_INVALID_PARAMETER;
	}
	uint64_t top_frames = 0;
	uint64_t max_frames = 0;
	if (!_read_performance_integer(p_payload.get("topFrames", Variant()), 0, PerformanceAggregator::MAX_TOP_FRAMES, top_frames) ||
			!_read_performance_integer(p_payload.get("maxFrames", Variant()), 0, MAX_FRAMES, max_frames)) {
		r_code = "INVALID_ARGUMENTS";
		r_error = "topFrames or maxFrames is outside its allowed range.";
		return ERR_INVALID_PARAMETER;
	}
	_prune_jobs();
	const String job_id = job_id_value;
	const String session_id = session_id_value;
	if (jobs.has(job_id) || jobs.size() >= MAX_RETAINED_JOBS || _active_job_count() >= MAX_ACTIVE_JOBS ||
			_active_job_count(session_id) >= MAX_SESSION_JOBS) {
		r_code = "PERFORMANCE_JOB_LIMIT";
		r_error = "Runtime performance job capacity is full.";
		return ERR_OUT_OF_MEMORY;
	}
	Job job;
	job.id = job_id;
	job.name = name_value;
	job.mcp_session_id = session_id;
	job.started_frame = Engine::get_singleton()->get_process_frames();
	job.max_frames = max_frames;
	if (job.aggregator.start(name_value, int(top_frames), &r_error) != OK) {
		r_code = "PERFORMANCE_START_FAILED";
		return ERR_INVALID_PARAMETER;
	}
	jobs.insert(job.id, job);
	job_order.push_back(job.id);
	_set_process_connected(true);
	return OK;
}

void MCPRuntimePerformanceSampler::_collect_sample(Dictionary &r_frame_metrics,
		Dictionary &r_builtin_monitors, Dictionary &r_custom_monitors) {
	Performance *performance = Performance::get_singleton();
	if (!performance) {
		return;
	}
	const double fps = performance->get_monitor(Performance::TIME_FPS);
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	r_frame_metrics["frame_time_usec"] = last_sample_ticks_usec > 0 ? double(now - last_sample_ticks_usec) : Engine::get_singleton()->get_process_step() * 1000000.0;
	last_sample_ticks_usec = now;
	r_frame_metrics["process_step_usec"] = Engine::get_singleton()->get_process_step() * 1000000.0;
	r_frame_metrics["process_time_seconds"] = performance->get_monitor(Performance::TIME_PROCESS);
	r_frame_metrics["physics_process_time_seconds"] = performance->get_monitor(Performance::TIME_PHYSICS_PROCESS);
	r_frame_metrics["navigation_process_time_seconds"] = performance->get_monitor(Performance::TIME_NAVIGATION_PROCESS);
	r_frame_metrics["fps"] = fps;

	const bool collect_builtin_types = builtin_monitor_types.is_empty();
	for (int monitor = 0; monitor < Performance::MONITOR_MAX; monitor++) {
		const Performance::Monitor monitor_id = Performance::Monitor(monitor);
		const StringName name = performance->get_monitor_name(monitor_id);
		r_builtin_monitors[name] = performance->get_monitor(monitor_id);
		if (collect_builtin_types) {
			builtin_monitor_types[name] = int(performance->get_monitor_type(monitor_id));
		}
	}
	const uint64_t modification_time = performance->get_monitor_modification_time();
	if (custom_monitors_modification_time != modification_time) {
		custom_monitors_modification_time = modification_time;
		custom_monitor_names.clear();
		custom_monitor_types.clear();
		const TypedArray<StringName> names = performance->get_custom_monitor_names();
		const Vector<int> types = performance->get_custom_monitor_types();
		custom_monitor_names.reserve(names.size());
		for (int i = 0; i < names.size(); i++) {
			const StringName name = names[i];
			const String name_string = name;
			if (!name_string.is_empty() && name_string.length() <= PerformanceAggregator::MAX_METRIC_NAME_LENGTH) {
				custom_monitor_names.push_back(name);
				if (i < types.size()) {
					custom_monitor_types[name] = types[i];
				}
			}
		}
		custom_monitor_names.sort();
		if (custom_monitor_names.size() > MAX_CUSTOM_MONITORS) {
			custom_monitor_names.resize(MAX_CUSTOM_MONITORS);
		}
		for (const Variant &name : custom_monitor_types.keys()) {
			if (!custom_monitor_names.has(StringName(String(name)))) {
				custom_monitor_types.erase(name);
			}
		}
	}
	for (const StringName &name : custom_monitor_names) {
		const Variant value = performance->get_custom_monitor(name);
		if ((value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT) && Math::is_finite(double(value))) {
			r_custom_monitors[name] = value;
		}
	}
}

void MCPRuntimePerformanceSampler::_push_sample_to_jobs(uint64_t p_frame, const Dictionary &p_frame_metrics,
		const Dictionary &p_builtin_monitors, const Dictionary &p_custom_monitors) {
	for (KeyValue<String, Job> &entry : jobs) {
		Job &job = entry.value;
		if (_is_terminal(job)) {
			continue;
		}
		String error;
		if (job.aggregator.push_sample(p_frame, p_frame_metrics, p_builtin_monitors, p_custom_monitors, &error) != OK) {
			job.state = "failed";
			job.failure = error;
		} else if (job.max_frames > 0 && job.aggregator.get_captured_frames() >= job.max_frames) {
			job.state = "completed";
		}
	}
}

void MCPRuntimePerformanceSampler::_process_frame() {
	if (_active_job_count() == 0) {
		_set_process_connected(false);
		return;
	}
	const uint64_t frame = Engine::get_singleton()->get_process_frames();
	Dictionary frame_metrics;
	Dictionary builtin_monitors;
	Dictionary custom_monitors;
	_collect_sample(frame_metrics, builtin_monitors, custom_monitors);
	if (frame_metrics.is_empty()) {
		for (KeyValue<String, Job> &entry : jobs) {
			if (!_is_terminal(entry.value)) {
				entry.value.state = "failed";
				entry.value.failure = "The runtime Performance singleton is unavailable.";
			}
		}
	} else {
		_push_sample_to_jobs(frame, frame_metrics, builtin_monitors, custom_monitors);
	}
	if (_active_job_count() == 0) {
		_set_process_connected(false);
	}
}

bool MCPRuntimePerformanceSampler::_handle_start(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 2 || p_arguments[0].get_type() != Variant::STRING || p_arguments[1].get_type() != Variant::DICTIONARY) {
		_send_response(request_id, "start", false, "INVALID_ARGUMENTS", "start requires a request ID and payload.");
		return false;
	}
	const Dictionary payload = p_arguments[1];
	String code;
	String error;
	if (_create_job(payload, code, error) != OK) {
		_send_response(request_id, "start", false, code, error);
		return false;
	}
	const Job *job = jobs.getptr(String(payload["jobId"]));
	_send_response(request_id, "start", true, String(), String(), _job_state(*job, false));
	return true;
}

bool MCPRuntimePerformanceSampler::_handle_status(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 2 || p_arguments[1].get_type() != Variant::DICTIONARY) {
		_send_response(request_id, "status", false, "INVALID_ARGUMENTS", "status requires jobId and mcpSessionId.");
		return false;
	}
	const Dictionary payload = p_arguments[1];
	const String job_id = payload.get("jobId", String());
	const String session_id = payload.get("mcpSessionId", String());
	const Job *job = jobs.getptr(job_id);
	if (!job || job->mcp_session_id != session_id) {
		_send_response(request_id, "status", false, "PERFORMANCE_JOB_NOT_FOUND", "Runtime performance job was not found for this MCP session.");
		return true;
	}
	_send_response(request_id, "status", true, String(), String(), _job_state(*job, false));
	return true;
}

bool MCPRuntimePerformanceSampler::_handle_stop(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 2 || p_arguments[1].get_type() != Variant::DICTIONARY) {
		_send_response(request_id, "stop", false, "INVALID_ARGUMENTS", "stop requires jobId and mcpSessionId.");
		return false;
	}
	const Dictionary payload = p_arguments[1];
	const String job_id = payload.get("jobId", String());
	const String session_id = payload.get("mcpSessionId", String());
	Job *job = jobs.getptr(job_id);
	if (!job || job->mcp_session_id != session_id) {
		_send_response(request_id, "stop", false, "PERFORMANCE_JOB_NOT_FOUND", "Runtime performance job was not found for this MCP session.");
		return true;
	}
	if (!_is_terminal(*job)) {
		job->state = "stopped";
	}
	if (_active_job_count() == 0) {
		_set_process_connected(false);
	}
	_send_response(request_id, "stop", true, String(), String(), _job_state(*job, true));
	return true;
}

bool MCPRuntimePerformanceSampler::_handle_release_session(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 2 || p_arguments[1].get_type() != Variant::DICTIONARY) {
		_send_response(request_id, "release_session", false, "INVALID_ARGUMENTS", "release_session requires mcpSessionId.");
		return false;
	}
	const String session_id = Dictionary(p_arguments[1]).get("mcpSessionId", String());
	if (session_id.is_empty()) {
		_send_response(request_id, "release_session", false, "INVALID_ARGUMENTS", "release_session requires mcpSessionId.");
		return false;
	}
	int cancelled = 0;
	for (KeyValue<String, Job> &entry : jobs) {
		if (entry.value.mcp_session_id == session_id && !_is_terminal(entry.value)) {
			entry.value.state = "cancelled";
			entry.value.failure = "The owning MCP session closed.";
			cancelled++;
		}
	}
	if (_active_job_count() == 0) {
		_set_process_connected(false);
	}
	_send_response(request_id, "release_session", true, String(), String(), Dictionary{ { "cancelledCount", cancelled } });
	return true;
}

bool MCPRuntimePerformanceSampler::_handle_release_all(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 2 || p_arguments[1].get_type() != Variant::DICTIONARY) {
		_send_response(request_id, "release_all", false, "INVALID_ARGUMENTS", "release_all accepts only requestId.");
		return false;
	}
	int cancelled = 0;
	for (KeyValue<String, Job> &entry : jobs) {
		if (!_is_terminal(entry.value)) {
			entry.value.state = "cancelled";
			entry.value.failure = "The MCP Host stopped.";
			cancelled++;
		}
	}
	_set_process_connected(false);
	_send_response(request_id, "release_all", true, String(), String(), Dictionary{ { "cancelledCount", cancelled } });
	return true;
}

Error MCPRuntimePerformanceSampler::_parse_message(void *p_user, const String &p_message, const Array &p_arguments, bool &r_captured) {
	MCPRuntimePerformanceSampler *sampler = static_cast<MCPRuntimePerformanceSampler *>(p_user);
	ERR_FAIL_NULL_V(sampler, ERR_UNCONFIGURED);
	r_captured = true;
	if (p_message == "start") {
		sampler->_handle_start(p_arguments);
	} else if (p_message == "status") {
		sampler->_handle_status(p_arguments);
	} else if (p_message == "stop") {
		sampler->_handle_stop(p_arguments);
	} else if (p_message == "release_session") {
		sampler->_handle_release_session(p_arguments);
	} else if (p_message == "release_all") {
		sampler->_handle_release_all(p_arguments);
	} else {
		r_captured = false;
	}
	return OK;
}
