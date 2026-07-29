/**************************************************************************/
/*  mcp_runtime_condition_scheduler.cpp                                   */
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

#include "mcp_runtime_condition_scheduler.h"

#include "mcp_runtime_observation.h"
#include "mcp_runtime_screenshot_capture.h"

#include "core/config/engine.h"
#include "core/debugger/engine_debugger.h"
#include "core/object/callable_mp.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace {

bool _read_condition_integer(const Variant &p_value, int64_t p_minimum, int64_t p_maximum, uint64_t &r_value) {
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

bool _condition_has_only(const Dictionary &p_condition, const PackedStringArray &p_allowed, String &r_error) {
	for (const Variant &key_value : p_condition.keys()) {
		if (!key_value.is_string() || !p_allowed.has(String(key_value))) {
			r_error = "Unsupported condition field: " + String(key_value);
			return false;
		}
	}
	return true;
}

} // namespace

void MCPRuntimeConditionScheduler::_bind_methods() {
}

MCPRuntimeConditionScheduler::MCPRuntimeConditionScheduler() {
	singleton = this;
	EngineDebugger::register_message_capture("mcp_condition", EngineDebugger::Capture(this, _parse_message));
}

MCPRuntimeConditionScheduler::~MCPRuntimeConditionScheduler() {
	if (process_connected && SceneTree::get_singleton()) {
		const Callable callback = callable_mp(this, &MCPRuntimeConditionScheduler::_process_frame);
		if (SceneTree::get_singleton()->is_connected("process_frame", callback)) {
			SceneTree::get_singleton()->disconnect("process_frame", callback);
		}
	}
	for (KeyValue<String, Job> &entry : jobs) {
		_release_baseline(entry.value);
	}
	if (EngineDebugger::get_singleton()) {
		EngineDebugger::unregister_message_capture("mcp_condition");
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}

void MCPRuntimeConditionScheduler::initialize() {
	if (!singleton) {
		memnew(MCPRuntimeConditionScheduler);
	}
}

void MCPRuntimeConditionScheduler::deinitialize() {
	if (singleton) {
		memdelete(singleton);
	}
}

void MCPRuntimeConditionScheduler::_ensure_process_connected() {
	if (process_connected || !SceneTree::get_singleton()) {
		return;
	}
	SceneTree::get_singleton()->connect("process_frame", callable_mp(this, &MCPRuntimeConditionScheduler::_process_frame));
	process_connected = true;
}

void MCPRuntimeConditionScheduler::_send_response(const String &p_request_id, const String &p_operation, bool p_ok,
		const String &p_code, const String &p_message, const Dictionary &p_data) const {
	if (!p_request_id.is_empty() && EngineDebugger::get_singleton()) {
		EngineDebugger::get_singleton()->send_message("mcp_condition:response",
				Array{ p_request_id, p_operation, p_ok, p_code, p_message, p_data });
	}
}

bool MCPRuntimeConditionScheduler::_is_terminal(const Job &p_job) const {
	return p_job.state != "running";
}

Dictionary MCPRuntimeConditionScheduler::_job_state(const Job &p_job) const {
	Dictionary state;
	state["jobId"] = p_job.id;
	state["state"] = p_job.state;
	state["conditionKind"] = p_job.kind;
	state["startedFrame"] = int64_t(p_job.started_frame);
	state["deadlineFrame"] = int64_t(p_job.deadline_frame);
	state["nextPollFrame"] = int64_t(p_job.next_poll_frame);
	state["pollEveryFrames"] = int64_t(p_job.poll_every_frames);
	state["evaluationCount"] = p_job.evaluations;
	if (!p_job.failure.is_empty()) {
		state["failure"] = p_job.failure;
	}
	return state;
}

void MCPRuntimeConditionScheduler::_release_baseline(Job &r_job) {
	if (r_job.baseline_bytes > 0) {
		total_baseline_bytes -= MIN(total_baseline_bytes, r_job.baseline_bytes);
		r_job.baseline_bytes = 0;
	}
	r_job.baseline_image.unref();
}

void MCPRuntimeConditionScheduler::_finish_job(Job &r_job, const String &p_state, const String &p_failure) {
	if (_is_terminal(r_job)) {
		return;
	}
	r_job.state = p_state;
	r_job.failure = p_failure;
	_release_baseline(r_job);
}

int MCPRuntimeConditionScheduler::_active_job_count(const String &p_mcp_session_id) const {
	int count = 0;
	for (const KeyValue<String, Job> &entry : jobs) {
		if (!_is_terminal(entry.value) && (p_mcp_session_id.is_empty() || entry.value.mcp_session_id == p_mcp_session_id)) {
			count++;
		}
	}
	return count;
}

void MCPRuntimeConditionScheduler::_prune_jobs() {
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
	process_cursor = job_order.is_empty() ? 0 : process_cursor % job_order.size();
}

Error MCPRuntimeConditionScheduler::_get_screenshot(Ref<Image> &r_image, String &r_error) {
	const uint64_t frame = Engine::get_singleton()->get_process_frames();
	if (screenshot_cache_frame != frame) {
		screenshot_cache_frame = frame;
		screenshot_cache.unref();
		const Error error = MCPRuntimeScreenshotCapture::capture_image(screenshot_cache, r_error);
		if (error != OK) {
			return error;
		}
		const uint64_t bytes = uint64_t(screenshot_cache->get_width()) * uint64_t(screenshot_cache->get_height()) * 4;
		if (screenshot_cache->get_width() > MAX_SCREENSHOT_DIMENSION || screenshot_cache->get_height() > MAX_SCREENSHOT_DIMENSION ||
				bytes > MAX_SCREENSHOT_BYTES) {
			r_error = "Runtime screenshot exceeds the condition scheduler image limit.";
			screenshot_cache.unref();
			return ERR_OUT_OF_MEMORY;
		}
	}
	r_image = screenshot_cache;
	return r_image.is_valid() ? OK : ERR_UNAVAILABLE;
}

double MCPRuntimeConditionScheduler::_screenshot_diff_ratio(const Ref<Image> &p_a, const Ref<Image> &p_b) const {
	if (p_a.is_null() || p_b.is_null() || p_a->get_size() != p_b->get_size()) {
		return 1.0;
	}
	const int pixel_count = p_a->get_width() * p_a->get_height();
	const int step = MAX(1, pixel_count / 4096);
	double total = 0.0;
	int samples = 0;
	for (int i = 0; i < pixel_count; i += step) {
		const Color a = p_a->get_pixel(i % p_a->get_width(), i / p_a->get_width());
		const Color b = p_b->get_pixel(i % p_b->get_width(), i / p_b->get_width());
		total += Math::abs(a.r - b.r) + Math::abs(a.g - b.g) + Math::abs(a.b - b.b) + Math::abs(a.a - b.a);
		samples++;
	}
	return samples > 0 ? total / (double(samples) * 4.0) : 0.0;
}

Error MCPRuntimeConditionScheduler::_prepare_job(Job &r_job, String &r_code, String &r_error) {
	const Variant kind_value = r_job.condition.get("kind", Variant());
	if (kind_value.get_type() != Variant::STRING) {
		r_code = "INVALID_CONDITION";
		r_error = "condition.kind must be a string.";
		return ERR_INVALID_PARAMETER;
	}
	r_job.kind = kind_value;
	if (r_job.kind == "node_exists" || r_job.kind == "node_gone" || r_job.kind == "property") {
		const PackedStringArray allowed = r_job.kind == "property" ? PackedStringArray{ "kind", "selector", "property", "equals" } : PackedStringArray{ "kind", "selector" };
		if (!_condition_has_only(r_job.condition, allowed, r_error) || r_job.condition.get("selector", Variant()).get_type() != Variant::DICTIONARY) {
			r_code = "INVALID_CONDITION";
			if (r_error.is_empty()) {
				r_error = "The condition requires a selector object.";
			}
			return ERR_INVALID_PARAMETER;
		}
		MCPRuntimeObservation::Selector selector;
		if (MCPRuntimeObservation::parse_selector(r_job.condition["selector"], selector, r_error) != OK || selector.is_empty()) {
			r_code = "INVALID_CONDITION";
			if (r_error.is_empty()) {
				r_error = "condition.selector must not be empty.";
			}
			return ERR_INVALID_PARAMETER;
		}
		if (r_job.kind == "property") {
			const Variant property_value = r_job.condition.get("property", Variant());
			if (property_value.get_type() != Variant::STRING || String(property_value).is_empty() || String(property_value).length() > 128 ||
					!r_job.condition.has("equals")) {
				r_code = "INVALID_CONDITION";
				r_error = "property conditions require a property name up to 128 characters and equals value.";
				return ERR_INVALID_PARAMETER;
			}
		}
		return OK;
	}
	if (r_job.kind == "scene_changed") {
		if (!_condition_has_only(r_job.condition, PackedStringArray{ "kind" }, r_error)) {
			r_code = "INVALID_CONDITION";
			return ERR_INVALID_PARAMETER;
		}
		SceneTree *tree = SceneTree::get_singleton();
		Node *scene = tree ? tree->get_current_scene() : nullptr;
		r_job.baseline_scene_id = scene ? scene->get_instance_id() : ObjectID();
		r_job.baseline_scene_path = scene ? scene->get_scene_file_path() : String();
		return OK;
	}
	if (r_job.kind == "screenshot_diff") {
		if (!_condition_has_only(r_job.condition, PackedStringArray{ "kind", "threshold" }, r_error)) {
			r_code = "INVALID_CONDITION";
			return ERR_INVALID_PARAMETER;
		}
		const Variant threshold_value = r_job.condition.get("threshold", 0.02);
		if (!threshold_value.is_num() || !Math::is_finite(double(threshold_value)) || double(threshold_value) < 0.001 || double(threshold_value) > 1.0) {
			r_code = "INVALID_CONDITION";
			r_error = "screenshot_diff.threshold must be between 0.001 and 1.0.";
			return ERR_INVALID_PARAMETER;
		}
		int active_screenshot_jobs = 0;
		for (const KeyValue<String, Job> &entry : jobs) {
			if (!_is_terminal(entry.value) && entry.value.kind == "screenshot_diff") {
				active_screenshot_jobs++;
			}
		}
		if (active_screenshot_jobs >= MAX_ACTIVE_SCREENSHOT_JOBS) {
			r_code = "WAIT_JOB_LIMIT";
			r_error = "The active screenshot_diff job limit is full.";
			return ERR_BUSY;
		}
		if (_get_screenshot(r_job.baseline_image, r_error) != OK) {
			r_code = "RUNTIME_SCREENSHOT_UNAVAILABLE";
			return ERR_UNAVAILABLE;
		}
		r_job.baseline_image = r_job.baseline_image->duplicate();
		r_job.baseline_bytes = uint64_t(r_job.baseline_image->get_width()) * uint64_t(r_job.baseline_image->get_height()) * 4;
		if (r_job.baseline_bytes > MAX_SCREENSHOT_BYTES) {
			r_job.baseline_image.unref();
			r_job.baseline_bytes = 0;
			r_code = "WAIT_MEMORY_LIMIT";
			r_error = "Runtime screenshot baseline exceeds the per-job memory limit.";
			return ERR_OUT_OF_MEMORY;
		}
		if (r_job.baseline_bytes > MAX_TOTAL_BASELINE_BYTES - MIN(total_baseline_bytes, MAX_TOTAL_BASELINE_BYTES)) {
			r_job.baseline_image.unref();
			r_job.baseline_bytes = 0;
			r_code = "WAIT_MEMORY_LIMIT";
			r_error = "Runtime screenshot baselines exceed the global memory limit.";
			return ERR_OUT_OF_MEMORY;
		}
		total_baseline_bytes += r_job.baseline_bytes;
		return OK;
	}
	r_code = "INVALID_CONDITION";
	r_error = "Unsupported condition kind: " + r_job.kind;
	return ERR_INVALID_PARAMETER;
}

Error MCPRuntimeConditionScheduler::_evaluate_job(Job &r_job, bool &r_satisfied, String &r_error) {
	r_satisfied = false;
	if (r_job.kind == "scene_changed") {
		SceneTree *tree = SceneTree::get_singleton();
		Node *scene = tree ? tree->get_current_scene() : nullptr;
		const ObjectID current_id = scene ? scene->get_instance_id() : ObjectID();
		const String current_path = scene ? scene->get_scene_file_path() : String();
		r_satisfied = current_id != r_job.baseline_scene_id || current_path != r_job.baseline_scene_path;
		return OK;
	}
	if (r_job.kind == "screenshot_diff") {
		Ref<Image> current;
		const Error error = _get_screenshot(current, r_error);
		if (error != OK) {
			return error;
		}
		r_satisfied = _screenshot_diff_ratio(r_job.baseline_image, current) >= double(r_job.condition.get("threshold", 0.02));
		return OK;
	}
	Array nodes;
	int visited = 0;
	const Error resolve_error = MCPRuntimeObservation::resolve_selector(r_job.condition["selector"], 2, MAX_SELECTOR_VISITS, nodes, visited, r_error);
	if (resolve_error != OK) {
		return resolve_error;
	}
	if (r_job.kind == "node_exists") {
		r_satisfied = !nodes.is_empty();
		return OK;
	}
	if (r_job.kind == "node_gone") {
		r_satisfied = nodes.is_empty();
		return OK;
	}
	if (nodes.is_empty()) {
		return OK;
	}
	if (nodes.size() > 1) {
		r_error = "property selector matched multiple runtime nodes.";
		return ERR_ALREADY_IN_USE;
	}
	SceneTree *tree = SceneTree::get_singleton();
	Node *node = tree && tree->get_root() ? tree->get_root()->get_node_or_null(NodePath(String(Dictionary(nodes[0])["nodePath"]))) : nullptr;
	bool valid = false;
	const Variant value = node ? node->get(StringName(r_job.condition["property"]), &valid) : Variant();
	if (!valid) {
		r_error = "Runtime property was not found: " + String(r_job.condition["property"]);
		return ERR_DOES_NOT_EXIST;
	}
	r_satisfied = bool(Variant::evaluate(Variant::OP_EQUAL, value, r_job.condition["equals"]));
	return OK;
}

void MCPRuntimeConditionScheduler::_process_frame() {
	if (job_order.is_empty()) {
		return;
	}
	screenshot_cache_frame = UINT64_MAX;
	screenshot_cache.unref();
	const uint64_t frame = Engine::get_singleton()->get_process_frames();
	int budget = MAX_EVALUATIONS_PER_FRAME;
	int inspected = 0;
	while (budget > 0 && inspected < job_order.size()) {
		if (process_cursor >= job_order.size()) {
			process_cursor = 0;
		}
		Job *job = jobs.getptr(job_order[process_cursor]);
		process_cursor = (process_cursor + 1) % job_order.size();
		inspected++;
		if (!job || _is_terminal(*job)) {
			continue;
		}
		if (frame >= job->deadline_frame && frame < job->next_poll_frame) {
			_finish_job(*job, "timed_out", "The runtime wait condition reached timeoutFrames.");
			continue;
		}
		if (frame < job->next_poll_frame) {
			continue;
		}
		budget--;
		job->evaluations++;
		bool satisfied = false;
		String error;
		const Error evaluation_error = _evaluate_job(*job, satisfied, error);
		job->next_poll_frame = frame + job->poll_every_frames;
		if (evaluation_error != OK) {
			_finish_job(*job, "failed", error);
		} else if (satisfied) {
			_finish_job(*job, "succeeded");
		} else if (frame >= job->deadline_frame) {
			_finish_job(*job, "timed_out", "The runtime wait condition reached timeoutFrames.");
		}
	}
}

bool MCPRuntimeConditionScheduler::_handle_start(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 2 || p_arguments[0].get_type() != Variant::STRING || p_arguments[1].get_type() != Variant::DICTIONARY) {
		_send_response(request_id, "start", false, "INVALID_ARGUMENTS", "start requires a request ID and payload.");
		return false;
	}
	const Dictionary payload = p_arguments[1];
	const Variant job_id = payload.get("jobId", Variant());
	const Variant session_id = payload.get("mcpSessionId", Variant());
	const Variant condition = payload.get("condition", Variant());
	if (job_id.get_type() != Variant::STRING || String(job_id).is_empty() || session_id.get_type() != Variant::STRING ||
			String(session_id).is_empty() || condition.get_type() != Variant::DICTIONARY) {
		_send_response(request_id, "start", false, "INVALID_ARGUMENTS", "start payload requires jobId, mcpSessionId, and condition.");
		return false;
	}
	uint64_t timeout_frames = 0;
	uint64_t poll_frames = 0;
	if (!_read_condition_integer(payload.get("timeoutFrames", Variant()), 1, MAX_TIMEOUT_FRAMES, timeout_frames) ||
			!_read_condition_integer(payload.get("pollEveryFrames", Variant()), 1, MAX_POLL_EVERY_FRAMES, poll_frames)) {
		_send_response(request_id, "start", false, "INVALID_ARGUMENTS", "Runtime wait frame limits are invalid.");
		return false;
	}
	_prune_jobs();
	if (jobs.has(job_id) || jobs.size() >= MAX_RETAINED_JOBS || _active_job_count() >= MAX_ACTIVE_JOBS ||
			_active_job_count(session_id) >= MAX_SESSION_JOBS) {
		_send_response(request_id, "start", false, "WAIT_JOB_LIMIT", "Runtime wait job capacity is full.");
		return true;
	}
	Job job;
	job.id = job_id;
	job.mcp_session_id = session_id;
	job.condition = condition;
	job.started_frame = Engine::get_singleton()->get_process_frames();
	job.deadline_frame = job.started_frame + timeout_frames;
	job.next_poll_frame = job.started_frame + 1;
	job.poll_every_frames = poll_frames;
	String code;
	String error;
	if (_prepare_job(job, code, error) != OK) {
		_send_response(request_id, "start", false, code, error);
		return false;
	}
	jobs.insert(job.id, job);
	job_order.push_back(job.id);
	_ensure_process_connected();
	_send_response(request_id, "start", true, String(), String(), _job_state(*jobs.getptr(job.id)));
	return true;
}

bool MCPRuntimeConditionScheduler::_handle_status(const Array &p_arguments) {
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
		_send_response(request_id, "status", false, "WAIT_JOB_NOT_FOUND", "Runtime wait job was not found for this MCP session.");
		return true;
	}
	_send_response(request_id, "status", true, String(), String(), _job_state(*job));
	return true;
}

bool MCPRuntimeConditionScheduler::_handle_cancel(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 2 || p_arguments[1].get_type() != Variant::DICTIONARY) {
		_send_response(request_id, "cancel", false, "INVALID_ARGUMENTS", "cancel requires jobId and mcpSessionId.");
		return false;
	}
	const Dictionary payload = p_arguments[1];
	const String job_id = payload.get("jobId", String());
	const String session_id = payload.get("mcpSessionId", String());
	Job *job = jobs.getptr(job_id);
	if (!job || job->mcp_session_id != session_id) {
		_send_response(request_id, "cancel", false, "WAIT_JOB_NOT_FOUND", "Runtime wait job was not found for this MCP session.");
		return true;
	}
	_finish_job(*job, "cancelled", "Cancelled by the MCP client.");
	_send_response(request_id, "cancel", true, String(), String(), _job_state(*job));
	return true;
}

bool MCPRuntimeConditionScheduler::_handle_release_session(const Array &p_arguments) {
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
			_finish_job(entry.value, "cancelled", "The owning MCP session closed.");
			cancelled++;
		}
	}
	_send_response(request_id, "release_session", true, String(), String(), Dictionary{ { "cancelledCount", cancelled } });
	return true;
}

bool MCPRuntimeConditionScheduler::_handle_release_all(const Array &p_arguments) {
	const String request_id = p_arguments.is_empty() ? String() : String(p_arguments[0]);
	if (p_arguments.size() != 2 || p_arguments[1].get_type() != Variant::DICTIONARY) {
		_send_response(request_id, "release_all", false, "INVALID_ARGUMENTS", "release_all accepts only requestId.");
		return false;
	}
	int cancelled = 0;
	for (KeyValue<String, Job> &entry : jobs) {
		if (!_is_terminal(entry.value)) {
			_finish_job(entry.value, "cancelled", "The MCP Host stopped.");
			cancelled++;
		}
	}
	_send_response(request_id, "release_all", true, String(), String(), Dictionary{ { "cancelledCount", cancelled } });
	return true;
}

Error MCPRuntimeConditionScheduler::_parse_message(void *p_user, const String &p_message, const Array &p_arguments, bool &r_captured) {
	MCPRuntimeConditionScheduler *scheduler = static_cast<MCPRuntimeConditionScheduler *>(p_user);
	ERR_FAIL_NULL_V(scheduler, ERR_UNCONFIGURED);
	r_captured = true;
	if (p_message == "start") {
		scheduler->_handle_start(p_arguments);
	} else if (p_message == "status") {
		scheduler->_handle_status(p_arguments);
	} else if (p_message == "cancel") {
		scheduler->_handle_cancel(p_arguments);
	} else if (p_message == "release_session") {
		scheduler->_handle_release_session(p_arguments);
	} else if (p_message == "release_all") {
		scheduler->_handle_release_all(p_arguments);
	} else {
		r_captured = false;
	}
	return OK;
}
