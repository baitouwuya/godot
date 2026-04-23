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
#include "core/templates/hash_set.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "main/cli_performance_recorder.h"

class CLIAIInputServer {
public:
	struct Options {
		bool enabled = false;
		int port = 7010;
		int max_batch_ops = 1024;
		int max_line_bytes = 1048576;
		int default_perf_top_frames = 10;
		int default_scene_tree_max_depth = 32;
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

private:
	struct PendingWait {
		Variant id;
		bool has_id = false;
		uint64_t started_frame = 0;
		uint64_t target_frame = 0;
		int frames = 0;
	};

	struct ActiveBatch {
		bool active = false;
		Variant id;
		bool has_id = false;
		Array ops;
		Array micro_ops;
		Array results;
		Array errors;
		String on_error = "stop";
		int step = 0;
		int failed_step = -1;
		uint64_t started_frame = 0;
		uint64_t wait_target_frame = 0;
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
	static bool _get_int(const Dictionary &p_dict, const String &p_key, int p_default, int &r_value, String &r_error);
	static bool _get_double(const Dictionary &p_dict, const String &p_key, double p_default, double &r_value, String &r_error);
	static bool _get_bool(const Dictionary &p_dict, const String &p_key, bool p_default, bool &r_value, String &r_error);
	static bool _get_vector2_from_array(const Variant &p_value, Vector2 &r_value);
	static Dictionary _make_error_payload(const String &p_code, const String &p_message);
	static uint64_t _current_frame();

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
	Dictionary _handle_request(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _execute_operation(const Dictionary &p_operation, bool p_from_batch, bool &r_deferred);
	Dictionary _execute_immediate_operation(const Dictionary &p_operation, bool p_from_batch, bool &r_deferred);

	bool _start_batch(const Dictionary &p_request, bool p_direct_operation, Dictionary &r_response);
	void _finish_batch(bool p_completed);
	void _fail_batch_step(int p_step, const Dictionary &p_error_response);
	static bool _expand_high_level_operation(const Dictionary &p_operation, Array &r_expanded, String &r_error);
	static Dictionary _make_mouse_motion_op(const Vector2 &p_position);
	static Dictionary _make_mouse_button_op(MouseButton p_button, const Vector2 &p_position, bool p_pressed, bool p_double_click = false);
	static Dictionary _make_wait_op(int p_frames);

	Dictionary _cmd_ping() const;
	Dictionary _cmd_get_status() const;
	Dictionary _cmd_action(const Dictionary &p_request);
	Dictionary _cmd_key(const Dictionary &p_request);
	Dictionary _cmd_mouse_button(const Dictionary &p_request);
	Dictionary _cmd_mouse_motion(const Dictionary &p_request);
	Dictionary _cmd_wait(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_get_screenshot(const Dictionary &p_request);
	Dictionary _cmd_get_scene_tree(const Dictionary &p_request);
	Dictionary _cmd_perf_start(const Dictionary &p_request);
	Dictionary _cmd_perf_stop(const Dictionary &p_request, bool p_from_batch, bool &r_deferred);
	Dictionary _cmd_perf_status() const;

	void _inject_mouse_motion(const Vector2 &p_position, const Vector2 &p_relative);
	void _inject_mouse_button(MouseButton p_button, const Vector2 &p_position, bool p_pressed, bool p_double_click);
	void _release_held_inputs();
	void _abort_runtime_perf(bool p_send_event);
	static int64_t _key_hold_id(Key p_key, bool p_shift, bool p_ctrl, bool p_alt, bool p_meta);
	static bool _mouse_button_has_hold_mask(MouseButton p_button);
	static MouseButtonMask _mouse_button_to_hold_mask(MouseButton p_button);

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

	CLIPerformanceRecorder *runtime_perf_recorder = nullptr;
	Variant pending_perf_stop_id;
	bool pending_perf_stop_has_id = false;
	bool pending_perf_stop = false;
};
