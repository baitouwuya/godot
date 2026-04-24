/**************************************************************************/
/*  cli_ai_input_server.h                                                 */
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

#include "core/input/input_enums.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/math/rect2.h"
#include "core/math/vector3.h"
#include "core/templates/hash_set.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "main/cli_performance_recorder.h"

class Camera3D;
class Image;
class Node;
class Viewport;

class CLIAIInputServer {
public:
	struct Options {
		bool enabled = false;
		int port = 7010;
		int max_batch_ops = 1024;
		int max_line_bytes = 1048576;
		int default_perf_top_frames = 10;
		int default_scene_tree_max_depth = 32;
		int default_wait_timeout_frames = 240;
		int default_poll_every_frames = 1;
		bool default_capture_on_error = false;
		int default_query_max_results = 128;
		String screenshot_directory;

		bool enabled_set = false;
		bool port_set = false;
		bool max_batch_ops_set = false;
		bool max_line_bytes_set = false;
	};

	static void register_project_settings();
	static bool parse_argument(const String &p_arg, List<String>::Element *&r_next, Options &r_options, String &r_error);
	static void apply_project_settings(Options &r_options);
	static Error validate_options(const Options &p_options, bool p_editor, bool p_project_manager, bool p_cmdline_tool, String &r_error);
	static bool is_enabled(const Options &p_options) { return p_options.enabled; }

	static bool parse_mouse_button(const Variant &p_value, MouseButton &r_button);
	static bool expand_operation_for_tests(const Dictionary &p_operation, Array &r_expanded, String &r_error);

	explicit CLIAIInputServer(const Options &p_options);
	~CLIAIInputServer();

	Error initialize(String &r_error);
	void poll_commands();
	void record_frame(uint64_t p_frame_index, uint64_t p_frame_time_usec, uint64_t p_process_time_usec, uint64_t p_physics_process_time_usec, uint64_t p_navigation_process_time_usec, double p_physics_frame_time_sec);
	void shutdown();

	bool is_listening() const;
	bool is_batch_active() const { return active_batch.active; }
	bool is_runtime_perf_active() const { return runtime_perf_recorder != nullptr; }

#ifdef TESTS_ENABLED
	Dictionary test_cmd_get_interactables(const Dictionary &p_request) { return _cmd_get_interactables(p_request); }
	Dictionary test_cmd_query_nodes(const Dictionary &p_request) { return _cmd_query_nodes(p_request); }
	Dictionary test_cmd_get_node_snapshot(const Dictionary &p_request) { return _cmd_get_node_snapshot(p_request); }
	bool test_prepare_expanded_operation(const Dictionary &p_operation, Array &r_expanded, Dictionary &r_result, String &r_error_code, String &r_error_message) { return _prepare_expanded_operation(p_operation, r_expanded, r_result, r_error_code, r_error_message); }
	Dictionary test_cmd_wait_node_exists(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) { return _cmd_wait_node_exists(p_request, p_from_batch, r_deferred); }
	Dictionary test_cmd_wait_property(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) { return _cmd_wait_property(p_request, p_from_batch, r_deferred); }
	Dictionary test_cmd_wait_scene_changed(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) { return _cmd_wait_scene_changed(p_request, p_from_batch, r_deferred); }
	Dictionary test_cmd_perf_start(const Dictionary &p_request) { return _cmd_perf_start(p_request); }
	Dictionary test_cmd_perf_stop(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) { return _cmd_perf_stop(p_request, p_from_batch, r_deferred); }
	Dictionary test_cmd_perf_status() const { return _cmd_perf_status(); }
	void test_process_line(const String &p_line) { _process_line(p_line); }
	void test_finish_runtime_perf_stop(bool p_completed, bool p_event_response) { _finish_runtime_perf_stop(p_completed, p_event_response); }
	void test_process_pending_waits() { _process_pending_waits(); }
	static uint64_t test_current_frame() { return _current_frame(); }
	void test_setup_active_batch(int p_step, const String &p_on_error) {
		active_batch.active = true;
		active_batch.step = p_step;
		active_batch.on_error = p_on_error;
		active_batch.started_frame = _current_frame();
	}
	int test_batch_results_count() const { return active_batch.results.size(); }
	Dictionary test_batch_result(int p_index) const { return active_batch.results[p_index]; }
	bool test_has_pending_waits() const { return !pending_waits.is_empty(); }
	void test_force_last_pending_wait_to_now() {
		if (!pending_waits.is_empty()) {
			pending_waits.write[pending_waits.size() - 1].timeout_frame = _current_frame();
			pending_waits.write[pending_waits.size() - 1].next_poll_frame = _current_frame();
		}
	}
	Dictionary test_last_error_bundle() const { return last_error_bundle; }
	void test_add_checkpoint(const String &p_name) {
		Dictionary checkpoint;
		checkpoint["event"] = "checkpoint";
		checkpoint["name"] = p_name;
		checkpoint_history.push_back(checkpoint);
		active_batch.checkpoints.push_back(checkpoint);
	}
	Dictionary test_batch_status() const { return _cmd_batch_status(); }
	Dictionary test_cancel_batch(const Dictionary &p_request) { return _cmd_cancel_batch(p_request); }
	Dictionary test_list_checkpoints() const { return _cmd_list_checkpoints(); }
	Dictionary test_get_last_error_bundle() const { return _cmd_get_last_error_bundle(); }
	int test_observation_cache_rebuild_count() const { return test_observation_cache_rebuilds; }
	int test_root_image_fetch_count() const { return test_root_image_fetches; }
	void test_reset_cache_debug_counters() const {
		test_observation_cache_rebuilds = 0;
		test_root_image_fetches = 0;
		observation_cache = ObservationFrameCache();
		root_image_cache_frame = UINT64_MAX;
		root_image_cache_populated = false;
		root_image_cache_available = false;
		root_image_cache.unref();
	}
	bool test_get_root_image(Ref<Image> &r_image) const { return _get_root_image(r_image); }
#endif

private:
	struct SelectorSpec {
		String path;
		String name;
		String type;
		String group;
		String text;
		bool has_is_3d = false;
		bool is_3d = false;
		bool has_visible = false;
		bool visible = false;
		bool has_screen_rect = false;
		Rect2 screen_rect;
		bool has_nearest_to_screen_point = false;
		Vector2 nearest_to_screen_point;

		bool is_empty() const {
			return path.is_empty() && name.is_empty() && type.is_empty() && group.is_empty() && text.is_empty() &&
					!has_is_3d && !has_visible && !has_screen_rect && !has_nearest_to_screen_point;
		}
	};

	struct WaitSpec {
		String condition_kind;
		SelectorSpec selector;
		int timeout_frames = 0;
		int poll_every_frames = 1;
		int frames = 0;
		String property_name;
		Variant expected_value;
		String baseline_scene_path;
		Ref<Image> baseline_image;
		double screenshot_diff_threshold = 0.0;
	};

	struct ObservationEntry {
		Dictionary snapshot;
		bool interactable = false;
	};

	struct ObservationFrameCache {
		uint64_t frame = UINT64_MAX;
		bool populated = false;
		Vector<ObservationEntry> entries;
		HashMap<String, int> path_to_index;
		int ui_count = 0;
		int ui_interactable_count = 0;
		int count_3d = 0;
		int interactable_count_3d = 0;
	};

	struct TargetResolution {
		bool ok = false;
		bool ambiguous = false;
		Dictionary snapshot;
		Array candidates;
		Vector2 screen_position;
	};

	struct PendingWait {
		Variant id;
		bool has_id = false;
		String trace_correlation_id;
		bool from_batch = false;
		int batch_step = -1;
		String command;
		WaitSpec spec;
		uint64_t started_frame = 0;
		uint64_t timeout_frame = 0;
		uint64_t next_poll_frame = 0;
		Dictionary request;
		Dictionary last_resolved_target;
		bool capture_on_error = false;
	};

	struct ActiveBatch {
		bool active = false;
		Variant id;
		bool has_id = false;
		String trace_correlation_id;
		Array ops;
		Array micro_ops;
		Array results;
		Array errors;
		Array checkpoints;
		String on_error = "stop";
		int step = 0;
		int failed_step = -1;
		uint64_t started_frame = 0;
		bool waiting = false;
		bool direct_operation = false;
	};

	struct HeldKey {
		Key keycode = Key::NONE;
		bool shift = false;
		bool ctrl = false;
		bool alt = false;
		bool meta = false;
	};

	static bool _consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error);
	static bool _variant_to_int(const Variant &p_value, int &r_value);
	static bool _variant_to_double(const Variant &p_value, double &r_value);
	static bool _variant_to_bool(const Variant &p_value, bool &r_value);
	static bool _variant_to_string(const Variant &p_value, String &r_value);
	static bool _get_int(const Dictionary &p_dict, const String &p_key, int p_default, int &r_value, String &r_error);
	static bool _get_double(const Dictionary &p_dict, const String &p_key, double p_default, double &r_value, String &r_error);
	static bool _get_bool(const Dictionary &p_dict, const String &p_key, bool p_default, bool &r_value, String &r_error);
	static bool _get_string(const Dictionary &p_dict, const String &p_key, const String &p_default, String &r_value, String &r_error);
	static bool _get_vector2_from_array(const Variant &p_value, Vector2 &r_value);
	static Dictionary _make_error_payload(const String &p_code, const String &p_message);
	static uint64_t _current_frame();
	static Array _vector2_to_array(const Vector2 &p_value);
	static Array _vector3_to_array(const Vector3 &p_value);
	static Dictionary _rect2_to_dictionary(const Rect2 &p_value);
	static bool _dictionary_to_rect2(const Dictionary &p_rect, Rect2 &r_rect, String &r_error);
	static Rect2 _transform_rect(const Transform2D &p_transform, const Rect2 &p_rect);
	static Vector2 _rect_center(const Dictionary &p_rect);

	void _accept_new_clients();
	void _poll_client_lines(int &r_budget);
	void _process_line(const String &p_line);
	void _process_pending_waits();
	void _process_batch(int &r_budget);
	void _send_response(const Dictionary &p_response);
	void _send_busy_and_close(Ref<StreamPeerTCP> p_peer);
	void _disconnect_client(bool p_abort_state);

	Dictionary _make_response_base(const Variant &p_id, bool p_has_id, bool p_ok) const;
	Dictionary _make_success_response(const Variant &p_id, bool p_has_id, const Variant &p_result) const;
	Dictionary _make_error_response(const Variant &p_id, bool p_has_id, const String &p_code, const String &p_message) const;
	Dictionary _make_contextual_error_response(const Dictionary &p_request, const Variant &p_id, bool p_has_id, const String &p_code, const String &p_message, const Dictionary &p_last_resolved_target = Dictionary()) const;
	void _decorate_error_response(const Dictionary &p_request, Dictionary &r_response, const Dictionary &p_last_resolved_target = Dictionary()) const;
	Dictionary _handle_request(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _execute_operation(const Dictionary &p_operation, bool p_from_batch, bool &r_deferred);
	Dictionary _execute_immediate_operation(const Dictionary &p_operation, bool p_from_batch, bool &r_deferred);
	bool _prepare_expanded_operation(const Dictionary &p_operation, Array &r_expanded, Dictionary &r_result, String &r_error_code, String &r_error_message);

	bool _start_batch(const Dictionary &p_request, bool p_direct_operation, Dictionary &r_response);
	void _finish_batch(bool p_completed);
	void _fail_batch_step(int p_step, const Dictionary &p_error_response);
	void _cancel_active_batch(const String &p_error_code, const String &p_error_message, bool p_send_response);
	static bool _expand_high_level_operation(const Dictionary &p_operation, Array &r_expanded, String &r_error);
	static Dictionary _make_mouse_motion_op(const Vector2 &p_position);
	static Dictionary _make_mouse_button_op(MouseButton p_button, const Vector2 &p_position, bool p_pressed, bool p_double_click = false);
	static Dictionary _make_wait_op(int p_frames);
	static Dictionary _make_type_text_commit_op(const String &p_text);

	Dictionary _cmd_ping() const;
	Dictionary _cmd_get_status() const;
	Dictionary _cmd_action(const Dictionary &p_request);
	Dictionary _cmd_key(const Dictionary &p_request);
	Dictionary _cmd_mouse_button(const Dictionary &p_request);
	Dictionary _cmd_mouse_motion(const Dictionary &p_request);
	Dictionary _cmd_wait(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_wait_until(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_wait_node_exists(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_wait_node_gone(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_wait_property(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_wait_scene_changed(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_wait_screenshot_diff(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_get_interactables(const Dictionary &p_request);
	Dictionary _cmd_query_nodes(const Dictionary &p_request);
	Dictionary _cmd_get_node_snapshot(const Dictionary &p_request);
	Dictionary _cmd_get_viewport_summary(const Dictionary &p_request);
	Dictionary _cmd_get_screenshot(const Dictionary &p_request);
	Dictionary _cmd_get_scene_tree(const Dictionary &p_request);
	Dictionary _cmd_perf_start(const Dictionary &p_request);
	Dictionary _cmd_perf_stop(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_perf_status() const;
	Dictionary _cmd_type_text_commit(const Dictionary &p_request);
	Dictionary _cmd_batch_status() const;
	Dictionary _cmd_cancel_batch(const Dictionary &p_request);
	Dictionary _cmd_list_checkpoints() const;
	Dictionary _cmd_get_last_error_bundle() const;

	void _inject_mouse_motion(const Vector2 &p_position, const Vector2 &p_relative);
	void _inject_mouse_button(MouseButton p_button, const Vector2 &p_position, bool p_pressed, bool p_double_click);
	void _inject_text_input(const String &p_text);
	void _release_held_inputs();
	void _abort_runtime_perf(bool p_send_event);
	static int64_t _key_hold_id(Key p_key, bool p_shift, bool p_ctrl, bool p_alt, bool p_meta);
	static bool _mouse_button_has_hold_mask(MouseButton p_button);
	static MouseButtonMask _mouse_button_to_hold_mask(MouseButton p_button);

	bool _get_root_image(Ref<Image> &r_image) const;
	bool _ensure_observation_frame_cache() const;
	void _rebuild_observation_frame_cache() const;
	void _append_observation_entries(Node *p_node, Viewport *p_root_viewport) const;
	bool _build_snapshot_for_node(Node *p_node, Dictionary &r_snapshot) const;
	static Camera3D *_get_effective_camera(Viewport *p_viewport);
	bool _get_viewport_to_root_transform(Viewport *p_viewport, Viewport *p_root_viewport, Transform2D &r_transform) const;
	bool _extract_selector_spec(const Dictionary &p_request, const String &p_key, const String &p_label, SelectorSpec &r_spec, String &r_error) const;
	bool _parse_selector_spec(const Dictionary &p_dict, const String &p_label, SelectorSpec &r_spec, String &r_error) const;
	bool _matches_selector_spec(const Dictionary &p_snapshot, const SelectorSpec &p_spec) const;
	bool _resolve_query_limit(const Dictionary &p_request, int &r_max_results, String &r_error) const;
	bool _normalize_wait_property_expected(const String &p_property_name, const Variant &p_value, Variant &r_value, String &r_error) const;
	bool _wait_property_matches(const Dictionary &p_snapshot, const String &p_property_name, const Variant &p_expected_value, Variant &r_current_value) const;
	bool _get_snapshot_for_selector_path(const SelectorSpec &p_selector, Dictionary &r_snapshot) const;
	Error _capture_screenshot_result(const Dictionary &p_request, Dictionary &r_result, String &r_error_message) const;
	Dictionary _build_scene_digest() const;
	Dictionary _build_viewport_summary(bool p_include_counts) const;
	Dictionary _build_target_snapshot(Node *p_node, Viewport *p_root_viewport) const;
	TargetResolution _resolve_target(const SelectorSpec &p_selector, bool p_require_screen_position) const;
	bool _is_interactable_snapshot(const Dictionary &p_snapshot) const;
	Vector2 _resolve_target_screen_position(const Dictionary &p_snapshot) const;
	bool _snapshot_has_screen_position(const Dictionary &p_snapshot) const;
	bool _extract_wait_request(const Dictionary &p_request, const String &p_condition_kind, const Variant &p_id, bool p_has_id, bool p_from_batch, PendingWait &r_wait, String &r_error_code, String &r_error_message) const;
	bool _extract_wait_until_request(const Dictionary &p_request, const Variant &p_id, bool p_has_id, bool p_from_batch, PendingWait &r_wait, String &r_error_code, String &r_error_message) const;
	bool _evaluate_wait_condition(PendingWait &r_wait, bool &r_finished, Dictionary &r_result, String &r_error_code, String &r_error_message);
	double _compute_screenshot_diff_ratio(const Ref<Image> &p_a, const Ref<Image> &p_b) const;
	void _queue_wait(const PendingWait &p_wait, bool p_from_batch);
	void _complete_batch_wait(const PendingWait &p_wait, const Dictionary &p_result);
	void _fail_wait(const PendingWait &p_wait, const Dictionary &p_error_response);
	void _record_last_error_bundle(const Dictionary &p_response);
	bool _should_capture_on_error(const Dictionary &p_request) const;

	void _append_scene_tree_node(class Node *p_node, int p_depth, int p_max_depth, Array &r_nodes) const;
	String _resolve_screenshot_directory() const;
	void _mark_runtime_perf_stop_requested(const Variant &p_id, bool p_has_id);
	void _finish_runtime_perf_stop(bool p_completed, bool p_event_response);

	Options options;
	Ref<TCPServer> server;
	Ref<StreamPeerTCP> client;
	Vector<uint8_t> line_buffer;
	Vector<uint8_t> pending_client_bytes;
	bool dropping_oversized_line = false;
	Vector<PendingWait> pending_waits;
	ActiveBatch active_batch;
	HashSet<StringName> held_actions;
	HashSet<int> held_mouse_buttons;
	HashMap<int64_t, HeldKey> held_keys;
	BitField<MouseButtonMask> ai_mouse_button_mask;
	Vector2 last_mouse_position;
	Dictionary last_resolved_target;
	Dictionary last_error_bundle;
	Array checkpoint_history;
	mutable ObservationFrameCache observation_cache;
	mutable uint64_t root_image_cache_frame = UINT64_MAX;
	mutable bool root_image_cache_populated = false;
	mutable bool root_image_cache_available = false;
	mutable Ref<Image> root_image_cache;

	CLIPerformanceRecorder *runtime_perf_recorder = nullptr;
	Variant pending_perf_stop_id;
	bool pending_perf_stop_has_id = false;
	String pending_perf_stop_trace_correlation_id;
	bool pending_perf_stop = false;

#ifdef TESTS_ENABLED
	mutable int test_observation_cache_rebuilds = 0;
	mutable int test_root_image_fetches = 0;
#endif
};
