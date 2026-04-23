/**************************************************************************/
/*  cli_ai_input_server.cpp                                               */
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

#include "cli_ai_input_server.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/input/input_map.h"
#include "core/io/dir_access.h"
#include "core/io/image.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "scene/3d/node_3d.h"
#include "scene/main/canvas_item.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

static const int MAX_OPS_PER_FRAME = 128;

void CLIAIInputServer::register_project_settings() {
	GLOBAL_DEF_BASIC("editor/ai_agent_control/enabled", false);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/port", PROPERTY_HINT_RANGE, "1,65535,1"), 7010);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/max_batch_ops", PROPERTY_HINT_RANGE, "1,65536,1"), 1024);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/max_line_bytes", PROPERTY_HINT_RANGE, "256,16777216,1"), 1048576);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/default_perf_top_frames", PROPERTY_HINT_RANGE, "0,1024,1"), 10);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/default_scene_tree_max_depth", PROPERTY_HINT_RANGE, "0,128,1"), 32);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "editor/ai_agent_control/screenshot_directory", PROPERTY_HINT_GLOBAL_DIR), "");
}

bool CLIAIInputServer::_consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error) {
	if (!r_next) {
		r_error = "Missing value after " + p_arg + ".";
		return false;
	}

	r_value = r_next->get();
	r_next = r_next->next();
	return true;
}

bool CLIAIInputServer::parse_argument(const String &p_arg, List<String>::Element *&r_next, Options &r_options, String &r_error) {
	String value;
	r_error = String();

	if (p_arg == "--ai-agent-control") {
		r_options.enabled = true;
		r_options.enabled_set = true;
		return true;
	}

	if (p_arg == "--ai-agent-port") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (!value.is_valid_int()) {
			r_error = "--ai-agent-port must be followed by an integer.";
			return true;
		}
		r_options.port = value.to_int();
		r_options.port_set = true;
		return true;
	}

	if (p_arg == "--ai-agent-max-ops") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (!value.is_valid_int()) {
			r_error = "--ai-agent-max-ops must be followed by an integer.";
			return true;
		}
		r_options.max_batch_ops = value.to_int();
		r_options.max_batch_ops_set = true;
		return true;
	}

	if (p_arg == "--ai-agent-max-line-bytes") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (!value.is_valid_int()) {
			r_error = "--ai-agent-max-line-bytes must be followed by an integer.";
			return true;
		}
		r_options.max_line_bytes = value.to_int();
		r_options.max_line_bytes_set = true;
		return true;
	}

	return false;
}

void CLIAIInputServer::apply_project_settings(Options &r_options) {
	if (!r_options.enabled_set) {
		r_options.enabled = GLOBAL_GET("editor/ai_agent_control/enabled");
	}
	if (!r_options.port_set) {
		r_options.port = GLOBAL_GET("editor/ai_agent_control/port");
	}
	if (!r_options.max_batch_ops_set) {
		r_options.max_batch_ops = GLOBAL_GET("editor/ai_agent_control/max_batch_ops");
	}
	if (!r_options.max_line_bytes_set) {
		r_options.max_line_bytes = GLOBAL_GET("editor/ai_agent_control/max_line_bytes");
	}

	r_options.default_perf_top_frames = GLOBAL_GET("editor/ai_agent_control/default_perf_top_frames");
	r_options.default_scene_tree_max_depth = GLOBAL_GET("editor/ai_agent_control/default_scene_tree_max_depth");
	r_options.screenshot_directory = GLOBAL_GET("editor/ai_agent_control/screenshot_directory");
}

Error CLIAIInputServer::validate_options(const Options &p_options, bool p_editor, bool p_project_manager, bool p_cmdline_tool, String &r_error) {
	r_error = String();

	if (p_options.port < 1 || p_options.port > 65535) {
		r_error = "--ai-agent-port must be between 1 and 65535.";
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.max_batch_ops < 1) {
		r_error = "--ai-agent-max-ops must be greater than or equal to 1.";
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.max_line_bytes < 1) {
		r_error = "--ai-agent-max-line-bytes must be greater than or equal to 1.";
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.default_perf_top_frames < 0) {
		r_error = "editor/ai_agent_control/default_perf_top_frames must be greater than or equal to 0.";
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.default_scene_tree_max_depth < 0 || p_options.default_scene_tree_max_depth > 128) {
		r_error = "editor/ai_agent_control/default_scene_tree_max_depth must be between 0 and 128.";
		return ERR_INVALID_PARAMETER;
	}

	if (p_options.enabled && (p_editor || p_project_manager || p_cmdline_tool)) {
		r_error = "--ai-agent-control only supports CLI project runs. It cannot be used with the editor, project manager, or one-shot command line tools.";
		return ERR_INVALID_PARAMETER;
	}

	return OK;
}

bool CLIAIInputServer::_variant_to_int(const Variant &p_value, int &r_value) {
	if (p_value.get_type() != Variant::INT && p_value.get_type() != Variant::FLOAT) {
		return false;
	}
	r_value = (int)p_value;
	return true;
}

bool CLIAIInputServer::_variant_to_double(const Variant &p_value, double &r_value) {
	if (p_value.get_type() != Variant::INT && p_value.get_type() != Variant::FLOAT) {
		return false;
	}
	r_value = (double)p_value;
	return true;
}

bool CLIAIInputServer::_variant_to_bool(const Variant &p_value, bool &r_value) {
	if (p_value.get_type() != Variant::BOOL) {
		return false;
	}
	r_value = (bool)p_value;
	return true;
}

bool CLIAIInputServer::_get_int(const Dictionary &p_dict, const String &p_key, int p_default, int &r_value, String &r_error) {
	if (!p_dict.has(p_key)) {
		r_value = p_default;
		return true;
	}
	if (!_variant_to_int(p_dict[p_key], r_value)) {
		r_error = p_key + " must be a number.";
		return false;
	}
	return true;
}

bool CLIAIInputServer::_get_double(const Dictionary &p_dict, const String &p_key, double p_default, double &r_value, String &r_error) {
	if (!p_dict.has(p_key)) {
		r_value = p_default;
		return true;
	}
	if (!_variant_to_double(p_dict[p_key], r_value)) {
		r_error = p_key + " must be a number.";
		return false;
	}
	return true;
}

bool CLIAIInputServer::_get_bool(const Dictionary &p_dict, const String &p_key, bool p_default, bool &r_value, String &r_error) {
	if (!p_dict.has(p_key)) {
		r_value = p_default;
		return true;
	}
	if (!_variant_to_bool(p_dict[p_key], r_value)) {
		r_error = p_key + " must be a boolean.";
		return false;
	}
	return true;
}

bool CLIAIInputServer::_get_vector2_from_array(const Variant &p_value, Vector2 &r_value) {
	if (p_value.get_type() != Variant::ARRAY) {
		return false;
	}
	Array array = p_value;
	if (array.size() < 2) {
		return false;
	}
	double x = 0.0;
	double y = 0.0;
	if (!_variant_to_double(array[0], x) || !_variant_to_double(array[1], y)) {
		return false;
	}
	r_value = Vector2(x, y);
	return true;
}

Dictionary CLIAIInputServer::_make_error_payload(const String &p_code, const String &p_message) {
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	return error;
}

uint64_t CLIAIInputServer::_current_frame() {
	return Engine::get_singleton() ? Engine::get_singleton()->get_process_frames() : 0;
}

bool CLIAIInputServer::parse_mouse_button(const Variant &p_value, MouseButton &r_button) {
	if (p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT) {
		const int button = (int)p_value;
		if (button <= (int)MouseButton::NONE) {
			return false;
		}
		r_button = (MouseButton)button;
		return true;
	}

	if (p_value.get_type() != Variant::STRING) {
		return false;
	}

	const String name = String(p_value).to_lower();
	if (name == "left") {
		r_button = MouseButton::LEFT;
	} else if (name == "right") {
		r_button = MouseButton::RIGHT;
	} else if (name == "middle") {
		r_button = MouseButton::MIDDLE;
	} else if (name == "wheel_up") {
		r_button = MouseButton::WHEEL_UP;
	} else if (name == "wheel_down") {
		r_button = MouseButton::WHEEL_DOWN;
	} else {
		return false;
	}
	return true;
}

CLIAIInputServer::CLIAIInputServer(const Options &p_options) {
	options = p_options;
}

CLIAIInputServer::~CLIAIInputServer() {
	shutdown();
}

Error CLIAIInputServer::initialize(String &r_error) {
	r_error = String();

	server.instantiate();
	const Error err = server->listen((uint16_t)options.port, IPAddress("127.0.0.1"));
	if (err != OK) {
		r_error = vformat("Unable to listen for AI agent control on 127.0.0.1:%d (error code %d).", options.port, (int)err);
		server.unref();
		return err;
	}

	OS::get_singleton()->printerr("AI agent control listening on 127.0.0.1:%d\n", options.port);
	return OK;
}

bool CLIAIInputServer::is_listening() const {
	return server.is_valid() && server->is_listening();
}

void CLIAIInputServer::_send_response(const Dictionary &p_response) {
	if (client.is_null() || client->get_status() != StreamPeerSocket::STATUS_CONNECTED) {
		return;
	}

	const String line = JSON::stringify(p_response) + "\n";
	const CharString utf8 = line.utf8();
	client->put_data((const uint8_t *)utf8.get_data(), utf8.length());
}

void CLIAIInputServer::_send_busy_and_close(Ref<StreamPeerTCP> p_peer) {
	Dictionary response;
	response["ok"] = false;
	response["frame"] = (int64_t)_current_frame();
	response["error"] = _make_error_payload("busy", "Another AI agent client is already connected.");

	const String line = JSON::stringify(response) + "\n";
	const CharString utf8 = line.utf8();
	p_peer->put_data((const uint8_t *)utf8.get_data(), utf8.length());
	p_peer->disconnect_from_host();
}

void CLIAIInputServer::_disconnect_client(bool p_abort_state) {
	if (p_abort_state) {
		_release_held_inputs();
		_abort_runtime_perf(false);
		pending_waits.clear();
		active_batch = ActiveBatch();
	}
	if (client.is_valid()) {
		client->disconnect_from_host();
		client.unref();
	}
	line_buffer.clear();
	pending_client_bytes.clear();
	dropping_oversized_line = false;
}

void CLIAIInputServer::_accept_new_clients() {
	if (server.is_null() || !server->is_listening()) {
		return;
	}

	while (server->is_connection_available()) {
		Ref<StreamPeerTCP> incoming = server->take_connection();
		if (incoming.is_null()) {
			continue;
		}
		incoming->set_no_delay(true);

		if (client.is_valid() && client->get_status() == StreamPeerSocket::STATUS_CONNECTED) {
			_send_busy_and_close(incoming);
			continue;
		}

		client = incoming;
		line_buffer.clear();
		dropping_oversized_line = false;
	}
}

void CLIAIInputServer::_poll_client_lines(int &r_budget) {
	if (client.is_null()) {
		return;
	}

	client->poll();
	if (client->get_status() != StreamPeerSocket::STATUS_CONNECTED) {
		_disconnect_client(true);
		return;
	}

	Vector<uint8_t> bytes;
	int received = 0;
	if (!pending_client_bytes.is_empty()) {
		bytes = pending_client_bytes;
		pending_client_bytes.clear();
		received = bytes.size();
	} else {
		const int available = client->get_available_bytes();
		if (available <= 0) {
			return;
		}

		bytes.resize(available);
		const Error err = client->get_partial_data(bytes.ptrw(), available, received);
		if (err != OK || received <= 0) {
			if (err != OK) {
				_disconnect_client(true);
			}
			return;
		}
	}

	for (int i = 0; i < received; i++) {
		if (r_budget <= 0) {
			for (int j = i; j < received; j++) {
				pending_client_bytes.push_back(bytes[j]);
			}
			break;
		}

		const uint8_t byte = bytes[i];
		if (dropping_oversized_line) {
			if (byte == '\n') {
				dropping_oversized_line = false;
				line_buffer.clear();
			}
			continue;
		}

		if (byte == '\n') {
			const String line = line_buffer.is_empty() ? String() : String::utf8((const char *)line_buffer.ptr(), line_buffer.size());
			line_buffer.clear();
			if (!line.is_empty()) {
				_process_line(line);
				r_budget--;
			}
			continue;
		}

		if (byte == '\r') {
			continue;
		}

		if (line_buffer.size() + 1 > options.max_line_bytes) {
			_send_response(_make_error_response(Variant(), false, "line_too_large", "JSONL request exceeded max_line_bytes."));
			line_buffer.clear();
			dropping_oversized_line = true;
			continue;
		}

		line_buffer.push_back(byte);
	}
}

Dictionary CLIAIInputServer::_make_response_base(const Variant &p_id, bool p_has_id, bool p_ok) const {
	Dictionary response;
	if (p_has_id) {
		response["id"] = p_id;
	}
	response["ok"] = p_ok;
	response["frame"] = (int64_t)_current_frame();
	return response;
}

Dictionary CLIAIInputServer::_make_success_response(const Variant &p_id, bool p_has_id, const Variant &p_result) const {
	Dictionary response = _make_response_base(p_id, p_has_id, true);
	response["result"] = p_result;
	return response;
}

Dictionary CLIAIInputServer::_make_error_response(const Variant &p_id, bool p_has_id, const String &p_code, const String &p_message) const {
	Dictionary response = _make_response_base(p_id, p_has_id, false);
	response["error"] = _make_error_payload(p_code, p_message);
	return response;
}

void CLIAIInputServer::_process_line(const String &p_line) {
	JSON json;
	const Error err = json.parse(p_line);
	if (err != OK) {
		_send_response(_make_error_response(Variant(), false, "invalid_json", json.get_error_message()));
		return;
	}

	const Variant parsed = json.get_data();
	if (parsed.get_type() != Variant::DICTIONARY) {
		_send_response(_make_error_response(Variant(), false, "invalid_json", "Request must be a JSON object."));
		return;
	}

	bool deferred = false;
	const Dictionary response = _handle_request(parsed, false, deferred);
	if (!deferred) {
		_send_response(response);
	}
}

void CLIAIInputServer::poll_commands() {
	if (!is_listening()) {
		return;
	}

	int budget = MAX_OPS_PER_FRAME;
	_accept_new_clients();
	_poll_client_lines(budget);
	_process_pending_waits();
	_process_batch(budget);
}

void CLIAIInputServer::_process_pending_waits() {
	const uint64_t frame = _current_frame();
	for (int i = pending_waits.size() - 1; i >= 0; i--) {
		const PendingWait wait = pending_waits[i];
		if (frame < wait.target_frame) {
			continue;
		}
		Dictionary result;
		result["startedFrame"] = (int64_t)wait.started_frame;
		result["endedFrame"] = (int64_t)frame;
		result["frames"] = wait.frames;
		_send_response(_make_success_response(wait.id, wait.has_id, result));
		pending_waits.remove_at(i);
	}
}

Dictionary CLIAIInputServer::_handle_request(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");

	if (!p_request.has("cmd") || p_request["cmd"].get_type() != Variant::STRING) {
		return _make_error_response(id, has_id, "missing_cmd", "Request must include a string cmd field.");
	}

	const String cmd = p_request["cmd"];
	if (cmd == "batch") {
		Dictionary response;
		if (!_start_batch(p_request, false, response)) {
			return response;
		}
		r_deferred = true;
		return Dictionary();
	}

	if (cmd == "click" || cmd == "double_click" || cmd == "hold" || cmd == "drag") {
		Dictionary batch_request;
		batch_request["id"] = id;
		batch_request["cmd"] = "batch";
		batch_request["onError"] = "stop";
		Array ops;
		ops.push_back(p_request);
		batch_request["ops"] = ops;

		Dictionary response;
		if (!_start_batch(batch_request, true, response)) {
			return response;
		}
		r_deferred = true;
		return Dictionary();
	}

	return _execute_operation(p_request, p_from_batch, r_deferred);
}

Dictionary CLIAIInputServer::_execute_operation(const Dictionary &p_operation, bool p_from_batch, bool &r_deferred) {
	String error;
	Array expanded;
	if (_expand_high_level_operation(p_operation, expanded, error)) {
		Dictionary result;
		result["expandedSteps"] = expanded.size();
		return _make_success_response(p_operation.has("id") ? p_operation["id"] : Variant(), p_operation.has("id"), result);
	}
	if (!error.is_empty()) {
		return _make_error_response(p_operation.has("id") ? p_operation["id"] : Variant(), p_operation.has("id"), "invalid_operation", error);
	}

	return _execute_immediate_operation(p_operation, p_from_batch, r_deferred);
}

Dictionary CLIAIInputServer::_execute_immediate_operation(const Dictionary &p_operation, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_operation.has("id") ? p_operation["id"] : Variant();
	const bool has_id = p_operation.has("id");
	const String cmd = p_operation["cmd"];

	if (cmd == "ping") {
		return _make_success_response(id, has_id, _cmd_ping());
	}
	if (cmd == "get_status") {
		return _make_success_response(id, has_id, _cmd_get_status());
	}
	if (cmd == "action") {
		return _cmd_action(p_operation);
	}
	if (cmd == "key") {
		return _cmd_key(p_operation);
	}
	if (cmd == "mouse_button") {
		return _cmd_mouse_button(p_operation);
	}
	if (cmd == "mouse_motion") {
		return _cmd_mouse_motion(p_operation);
	}
	if (cmd == "wait") {
		return _cmd_wait(p_operation, p_from_batch, r_deferred);
	}
	if (cmd == "get_screenshot") {
		return _cmd_get_screenshot(p_operation);
	}
	if (cmd == "get_scene_tree") {
		return _cmd_get_scene_tree(p_operation);
	}
	if (cmd == "perf_start") {
		return _cmd_perf_start(p_operation);
	}
	if (cmd == "perf_stop") {
		return _cmd_perf_stop(p_operation, p_from_batch, r_deferred);
	}
	if (cmd == "perf_status") {
		return _make_success_response(id, has_id, _cmd_perf_status());
	}
	if (cmd == "checkpoint") {
		Dictionary event = _make_response_base(id, has_id, true);
		event["event"] = "checkpoint";
		event["name"] = p_operation.has("name") ? String(p_operation["name"]) : String();
		event["step"] = active_batch.step;
		_send_response(event);

		Dictionary result;
		result["checkpoint"] = event["name"];
		return _make_success_response(id, has_id, result);
	}

	return _make_error_response(id, has_id, "unknown_command", "Unknown AI agent command: " + cmd + ".");
}

bool CLIAIInputServer::_start_batch(const Dictionary &p_request, bool p_direct_operation, Dictionary &r_response) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");

	if (active_batch.active) {
		r_response = _make_error_response(id, has_id, "busy", "A batch is already active.");
		return false;
	}
	if (!p_request.has("ops") || p_request["ops"].get_type() != Variant::ARRAY) {
		r_response = _make_error_response(id, has_id, "invalid_batch", "batch.ops must be an array.");
		return false;
	}

	Array ops = p_request["ops"];
	if (ops.size() > options.max_batch_ops) {
		r_response = _make_error_response(id, has_id, "too_many_ops", vformat("batch.ops exceeds max_batch_ops (%d).", options.max_batch_ops));
		return false;
	}

	String on_error = "stop";
	if (p_request.has("onError")) {
		if (p_request["onError"].get_type() != Variant::STRING) {
			r_response = _make_error_response(id, has_id, "invalid_batch", "batch.onError must be stop or continue.");
			return false;
		}
		on_error = p_request["onError"];
		if (on_error != "stop" && on_error != "continue") {
			r_response = _make_error_response(id, has_id, "invalid_batch", "batch.onError must be stop or continue.");
			return false;
		}
	}

	active_batch = ActiveBatch();
	active_batch.active = true;
	active_batch.id = id;
	active_batch.has_id = has_id;
	active_batch.ops = ops;
	active_batch.on_error = on_error;
	active_batch.started_frame = _current_frame();
	active_batch.direct_operation = p_direct_operation;
	return true;
}

void CLIAIInputServer::_finish_batch(bool p_completed) {
	Dictionary result;
	result["startedFrame"] = (int64_t)active_batch.started_frame;
	result["endedFrame"] = (int64_t)_current_frame();
	result["completed"] = p_completed;
	result["failedStep"] = active_batch.failed_step;
	result["results"] = active_batch.results;
	result["errors"] = active_batch.errors;

	const bool ok = p_completed && active_batch.errors.is_empty();
	Dictionary response = _make_response_base(active_batch.id, active_batch.has_id, ok);
	response["result"] = result;
	if (!ok) {
		response["error"] = _make_error_payload("batch_failed", "Batch did not complete successfully.");
	}
	_send_response(response);
	active_batch = ActiveBatch();
}

void CLIAIInputServer::_fail_batch_step(int p_step, const Dictionary &p_error_response) {
	Dictionary error;
	error["step"] = p_step;
	if (p_error_response.has("error")) {
		error["error"] = p_error_response["error"];
	} else {
		error["error"] = _make_error_payload("operation_failed", "Operation failed.");
	}
	active_batch.errors.push_back(error);

	if (active_batch.failed_step < 0) {
		active_batch.failed_step = p_step;
	}

	if (active_batch.on_error == "stop") {
		_release_held_inputs();
		_abort_runtime_perf(true);
		_finish_batch(false);
	}
}

void CLIAIInputServer::_process_batch(int &r_budget) {
	while (active_batch.active && r_budget > 0) {
		if (active_batch.waiting) {
			if (_current_frame() < active_batch.wait_target_frame) {
				return;
			}
			active_batch.waiting = false;
		}

		Dictionary op;
		bool internal_op = false;
		int result_step = active_batch.step;

		if (!active_batch.micro_ops.is_empty()) {
			op = active_batch.micro_ops[0];
			active_batch.micro_ops.remove_at(0);
			internal_op = true;
			result_step = MAX(0, active_batch.step - 1);
		} else {
			if (active_batch.step >= active_batch.ops.size()) {
				_finish_batch(true);
				return;
			}
			if (active_batch.ops[active_batch.step].get_type() != Variant::DICTIONARY) {
				Dictionary error_response = _make_error_response(active_batch.id, active_batch.has_id, "invalid_operation", "Batch operation must be an object.");
				_fail_batch_step(active_batch.step, error_response);
				if (!active_batch.active) {
					return;
				}
				active_batch.step++;
				continue;
			}

			op = active_batch.ops[active_batch.step];
			String expansion_error;
			Array expanded;
			if (_expand_high_level_operation(op, expanded, expansion_error)) {
				Dictionary result;
				result["step"] = active_batch.step;
				result["cmd"] = op["cmd"];
				result["expandedSteps"] = expanded.size();
				active_batch.results.push_back(result);
				active_batch.micro_ops = expanded;
				active_batch.step++;
				continue;
			}
			if (!expansion_error.is_empty()) {
				Dictionary error_response = _make_error_response(active_batch.id, active_batch.has_id, "invalid_operation", expansion_error);
				_fail_batch_step(active_batch.step, error_response);
				if (!active_batch.active) {
					return;
				}
				active_batch.step++;
				continue;
			}
			active_batch.step++;
		}

		bool deferred = false;
		Dictionary response = _execute_immediate_operation(op, true, deferred);
		r_budget--;

		if (deferred) {
			return;
		}

		const bool ok = response.has("ok") ? (bool)response["ok"] : false;
		if (!ok) {
			_fail_batch_step(result_step, response);
			if (!active_batch.active) {
				return;
			}
			continue;
		}

		if (!internal_op) {
			Dictionary result;
			result["step"] = result_step;
			result["cmd"] = op["cmd"];
			result["result"] = response["result"];
			active_batch.results.push_back(result);
		}
	}
}

Dictionary CLIAIInputServer::_make_mouse_motion_op(const Vector2 &p_position) {
	Dictionary op;
	op["cmd"] = "mouse_motion";
	op["x"] = p_position.x;
	op["y"] = p_position.y;
	return op;
}

Dictionary CLIAIInputServer::_make_mouse_button_op(MouseButton p_button, const Vector2 &p_position, bool p_pressed, bool p_double_click) {
	Dictionary op;
	op["cmd"] = "mouse_button";
	op["button"] = (int)p_button;
	op["pressed"] = p_pressed;
	op["x"] = p_position.x;
	op["y"] = p_position.y;
	if (p_double_click) {
		op["doubleClick"] = true;
	}
	return op;
}

Dictionary CLIAIInputServer::_make_wait_op(int p_frames) {
	Dictionary op;
	op["cmd"] = "wait";
	op["frames"] = p_frames;
	return op;
}

bool CLIAIInputServer::_expand_high_level_operation(const Dictionary &p_operation, Array &r_expanded, String &r_error) {
	r_error = String();
	if (!p_operation.has("cmd") || p_operation["cmd"].get_type() != Variant::STRING) {
		return false;
	}

	const String cmd = p_operation["cmd"];
	if (cmd != "click" && cmd != "double_click" && cmd != "hold" && cmd != "drag") {
		return false;
	}

	MouseButton button = MouseButton::LEFT;
	if (p_operation.has("button") && !parse_mouse_button(p_operation["button"], button)) {
		r_error = "button must be left, right, middle, wheel_up, wheel_down, or a positive integer.";
		return false;
	}

	String error;
	if (cmd == "click") {
		double x = 0.0;
		double y = 0.0;
		int press_frames = 1;
		if (!_get_double(p_operation, "x", 0.0, x, error) || !_get_double(p_operation, "y", 0.0, y, error) || !_get_int(p_operation, "pressFrames", 1, press_frames, error)) {
			r_error = error;
			return false;
		}
		if (press_frames < 1) {
			r_error = "click.pressFrames must be greater than or equal to 1.";
			return false;
		}
		const Vector2 pos(x, y);
		r_expanded.push_back(_make_mouse_motion_op(pos));
		r_expanded.push_back(_make_mouse_button_op(button, pos, true));
		r_expanded.push_back(_make_wait_op(press_frames));
		r_expanded.push_back(_make_mouse_button_op(button, pos, false));
		return true;
	}

	if (cmd == "double_click") {
		double x = 0.0;
		double y = 0.0;
		int press_frames = 1;
		int gap_frames = 2;
		if (!_get_double(p_operation, "x", 0.0, x, error) || !_get_double(p_operation, "y", 0.0, y, error) || !_get_int(p_operation, "pressFrames", 1, press_frames, error) || !_get_int(p_operation, "gapFrames", 2, gap_frames, error)) {
			r_error = error;
			return false;
		}
		if (press_frames < 1 || gap_frames < 1) {
			r_error = "double_click.pressFrames and gapFrames must be greater than or equal to 1.";
			return false;
		}
		const Vector2 pos(x, y);
		r_expanded.push_back(_make_mouse_motion_op(pos));
		r_expanded.push_back(_make_mouse_button_op(button, pos, true));
		r_expanded.push_back(_make_wait_op(press_frames));
		r_expanded.push_back(_make_mouse_button_op(button, pos, false));
		r_expanded.push_back(_make_wait_op(gap_frames));
		r_expanded.push_back(_make_mouse_button_op(button, pos, true, true));
		r_expanded.push_back(_make_wait_op(press_frames));
		r_expanded.push_back(_make_mouse_button_op(button, pos, false, true));
		return true;
	}

	if (cmd == "hold") {
		double x = 0.0;
		double y = 0.0;
		int frames = 0;
		if (!_get_double(p_operation, "x", 0.0, x, error) || !_get_double(p_operation, "y", 0.0, y, error) || !_get_int(p_operation, "frames", 0, frames, error)) {
			r_error = error;
			return false;
		}
		if (frames < 1) {
			r_error = "hold.frames must be greater than or equal to 1.";
			return false;
		}
		const Vector2 pos(x, y);
		r_expanded.push_back(_make_mouse_motion_op(pos));
		r_expanded.push_back(_make_mouse_button_op(button, pos, true));
		r_expanded.push_back(_make_wait_op(frames));
		r_expanded.push_back(_make_mouse_button_op(button, pos, false));
		return true;
	}

	int frames = 0;
	if (!_get_int(p_operation, "frames", 0, frames, error)) {
		r_error = error;
		return false;
	}
	if (frames < 1) {
		r_error = "drag.frames must be greater than or equal to 1.";
		return false;
	}

	Vector<Vector2> path;
	if (p_operation.has("path")) {
		if (p_operation["path"].get_type() != Variant::ARRAY) {
			r_error = "drag.path must be an array of [x, y] points.";
			return false;
		}
		Array points = p_operation["path"];
		if (points.size() < 2) {
			r_error = "drag.path must contain at least two points.";
			return false;
		}
		for (int i = 0; i < points.size(); i++) {
			Vector2 point;
			if (!_get_vector2_from_array(points[i], point)) {
				r_error = "drag.path entries must be [x, y] arrays.";
				return false;
			}
			path.push_back(point);
		}
	} else {
		Vector2 from;
		Vector2 to;
		if (!_get_vector2_from_array(p_operation.get("from", Variant()), from) || !_get_vector2_from_array(p_operation.get("to", Variant()), to)) {
			r_error = "drag requires from and to [x, y] arrays when path is not provided.";
			return false;
		}
		path.push_back(from);
		path.push_back(to);
	}

	r_expanded.push_back(_make_mouse_motion_op(path[0]));
	r_expanded.push_back(_make_mouse_button_op(button, path[0], true));
	for (int i = 1; i <= frames; i++) {
		const double t = (double)i / (double)frames;
		const double scaled = t * (double)(path.size() - 1);
		const int segment = MIN((int)path.size() - 2, (int)Math::floor(scaled));
		const double local_t = scaled - (double)segment;
		const Vector2 pos = path[segment].lerp(path[segment + 1], local_t);
		r_expanded.push_back(_make_mouse_motion_op(pos));
	}
	r_expanded.push_back(_make_mouse_button_op(button, path[path.size() - 1], false));
	return true;
}

bool CLIAIInputServer::expand_operation_for_tests(const Dictionary &p_operation, Array &r_expanded, String &r_error) {
	return _expand_high_level_operation(p_operation, r_expanded, r_error);
}

Dictionary CLIAIInputServer::_cmd_ping() const {
	Dictionary result;
	result["pong"] = true;
	return result;
}

Dictionary CLIAIInputServer::_cmd_get_status() const {
	Dictionary result;
	result["listening"] = is_listening();
	result["port"] = options.port;
	result["batchActive"] = active_batch.active;
	result["runtimePerfActive"] = runtime_perf_recorder != nullptr;
	result["pendingWaits"] = pending_waits.size();
	result["heldActions"] = held_actions.size();
	result["heldKeys"] = held_keys.size();
	result["heldMouseButtons"] = held_mouse_buttons.size();
	return result;
}

Dictionary CLIAIInputServer::_cmd_action(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	if (!p_request.has("action") || p_request["action"].get_type() != Variant::STRING) {
		return _make_error_response(id, has_id, "invalid_action", "action command requires a string action field.");
	}

	const StringName action = String(p_request["action"]);
	if (!InputMap::get_singleton() || !InputMap::get_singleton()->has_action(action)) {
		return _make_error_response(id, has_id, "unknown_action", "InputMap does not contain action: " + String(action) + ".");
	}

	bool pressed = false;
	String error;
	if (!_get_bool(p_request, "pressed", false, pressed, error)) {
		return _make_error_response(id, has_id, "invalid_action", error);
	}
	double strength = 1.0;
	if (!_get_double(p_request, "strength", 1.0, strength, error)) {
		return _make_error_response(id, has_id, "invalid_action", error);
	}
	strength = CLAMP(strength, 0.0, 1.0);

	if (pressed) {
		Input::get_singleton()->action_press(action, strength);
		held_actions.insert(action);
	} else {
		Input::get_singleton()->action_release(action);
		held_actions.erase(action);
	}

	Dictionary result;
	result["action"] = String(action);
	result["pressed"] = pressed;
	result["strength"] = strength;
	return _make_success_response(id, has_id, result);
}

Dictionary CLIAIInputServer::_cmd_key(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	if (!p_request.has("keycode")) {
		return _make_error_response(id, has_id, "invalid_key", "key command requires keycode.");
	}

	Key key = Key::NONE;
	if (p_request["keycode"].get_type() == Variant::STRING) {
		key = find_keycode(String(p_request["keycode"]));
	} else if (p_request["keycode"].get_type() == Variant::INT || p_request["keycode"].get_type() == Variant::FLOAT) {
		key = (Key)(int)p_request["keycode"];
	} else {
		return _make_error_response(id, has_id, "invalid_key", "keycode must be a string or integer.");
	}

	if (key == Key::NONE) {
		return _make_error_response(id, has_id, "invalid_key", "Unknown keycode.");
	}

	bool pressed = false;
	String error;
	if (!_get_bool(p_request, "pressed", false, pressed, error)) {
		return _make_error_response(id, has_id, "invalid_key", error);
	}
	bool shift = (int)(key & KeyModifierMask::SHIFT) != 0;
	bool ctrl = (int)(key & KeyModifierMask::CTRL) != 0;
	bool alt = (int)(key & KeyModifierMask::ALT) != 0;
	bool meta = (int)(key & KeyModifierMask::META) != 0;
	bool echo = false;
	if (!_get_bool(p_request, "shift", shift, shift, error) ||
			!_get_bool(p_request, "ctrl", ctrl, ctrl, error) ||
			!_get_bool(p_request, "alt", alt, alt, error) ||
			!_get_bool(p_request, "meta", meta, meta, error) ||
			!_get_bool(p_request, "echo", false, echo, error)) {
		return _make_error_response(id, has_id, "invalid_key", error);
	}
	key &= KeyModifierMask::CODE_MASK;

	Ref<InputEventKey> event;
	event.instantiate();
	event->set_keycode(key);
	event->set_physical_keycode(key);
	event->set_pressed(pressed);
	event->set_shift_pressed(shift);
	event->set_ctrl_pressed(ctrl);
	event->set_alt_pressed(alt);
	event->set_meta_pressed(meta);
	event->set_echo(echo);
	if (p_request.has("unicode")) {
		int unicode = 0;
		if (!_variant_to_int(p_request["unicode"], unicode)) {
			return _make_error_response(id, has_id, "invalid_key", "unicode must be an integer.");
		}
		event->set_unicode((char32_t)unicode);
	}
	Input::get_singleton()->parse_input_event(event);

	const int64_t hold_id = _key_hold_id(key, shift, ctrl, alt, meta);
	if (pressed) {
		HeldKey held_key;
		held_key.keycode = key;
		held_key.shift = shift;
		held_key.ctrl = ctrl;
		held_key.alt = alt;
		held_key.meta = meta;
		held_keys[hold_id] = held_key;
	} else {
		held_keys.erase(hold_id);
	}

	Dictionary result;
	result["keycode"] = (int64_t)key;
	result["pressed"] = pressed;
	return _make_success_response(id, has_id, result);
}

bool CLIAIInputServer::_mouse_button_has_hold_mask(MouseButton p_button) {
	return p_button == MouseButton::LEFT || p_button == MouseButton::RIGHT || p_button == MouseButton::MIDDLE || p_button == MouseButton::MB_XBUTTON1 || p_button == MouseButton::MB_XBUTTON2;
}

int64_t CLIAIInputServer::_key_hold_id(Key p_key, bool p_shift, bool p_ctrl, bool p_alt, bool p_meta) {
	int64_t id = (int64_t)(int)p_key & 0xFFFFFFFF;
	if (p_shift) {
		id |= 1LL << 32;
	}
	if (p_ctrl) {
		id |= 1LL << 33;
	}
	if (p_alt) {
		id |= 1LL << 34;
	}
	if (p_meta) {
		id |= 1LL << 35;
	}
	return id;
}

MouseButtonMask CLIAIInputServer::_mouse_button_to_hold_mask(MouseButton p_button) {
	return mouse_button_to_mask(p_button);
}

void CLIAIInputServer::_inject_mouse_button(MouseButton p_button, const Vector2 &p_position, bool p_pressed, bool p_double_click) {
	if (_mouse_button_has_hold_mask(p_button)) {
		const MouseButtonMask mask = _mouse_button_to_hold_mask(p_button);
		if (p_pressed) {
			ai_mouse_button_mask.set_flag(mask);
			held_mouse_buttons.insert((int)p_button);
		} else {
			ai_mouse_button_mask.clear_flag(mask);
			held_mouse_buttons.erase((int)p_button);
		}
	}

	last_mouse_position = p_position;
	Input::get_singleton()->set_mouse_position(p_position);

	Ref<InputEventMouseButton> event;
	event.instantiate();
	event->set_button_index(p_button);
	event->set_pressed(p_pressed);
	event->set_position(p_position);
	event->set_global_position(p_position);
	event->set_button_mask(ai_mouse_button_mask);
	event->set_double_click(p_double_click);
	Input::get_singleton()->parse_input_event(event);
}

void CLIAIInputServer::_inject_mouse_motion(const Vector2 &p_position, const Vector2 &p_relative) {
	last_mouse_position = p_position;
	Input::get_singleton()->set_mouse_position(p_position);

	Ref<InputEventMouseMotion> event;
	event.instantiate();
	event->set_position(p_position);
	event->set_global_position(p_position);
	event->set_relative(p_relative);
	event->set_relative_screen_position(p_relative);
	event->set_button_mask(ai_mouse_button_mask);
	Input::get_singleton()->parse_input_event(event);
}

Dictionary CLIAIInputServer::_cmd_mouse_button(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	MouseButton button = MouseButton::NONE;
	if (!p_request.has("button") || !parse_mouse_button(p_request["button"], button)) {
		return _make_error_response(id, has_id, "invalid_mouse_button", "mouse_button requires a valid button.");
	}

	bool pressed = false;
	bool double_click = false;
	String error;
	if (!_get_bool(p_request, "pressed", false, pressed, error)) {
		return _make_error_response(id, has_id, "invalid_mouse_button", error);
	}
	_get_bool(p_request, "doubleClick", false, double_click, error);

	double x = last_mouse_position.x;
	double y = last_mouse_position.y;
	if (!_get_double(p_request, "x", x, x, error) || !_get_double(p_request, "y", y, y, error)) {
		return _make_error_response(id, has_id, "invalid_mouse_button", error);
	}

	_inject_mouse_button(button, Vector2(x, y), pressed, double_click);

	Dictionary result;
	result["button"] = (int)button;
	result["pressed"] = pressed;
	result["x"] = x;
	result["y"] = y;
	return _make_success_response(id, has_id, result);
}

Dictionary CLIAIInputServer::_cmd_mouse_motion(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	String error;

	double x = last_mouse_position.x;
	double y = last_mouse_position.y;
	double relative_x = 0.0;
	double relative_y = 0.0;
	const bool has_absolute = p_request.has("x") || p_request.has("y");
	const bool has_relative = p_request.has("relativeX") || p_request.has("relativeY");

	if (has_absolute) {
		if (!_get_double(p_request, "x", x, x, error) || !_get_double(p_request, "y", y, y, error)) {
			return _make_error_response(id, has_id, "invalid_mouse_motion", error);
		}
		relative_x = x - last_mouse_position.x;
		relative_y = y - last_mouse_position.y;
	} else if (has_relative) {
		if (!_get_double(p_request, "relativeX", 0.0, relative_x, error) || !_get_double(p_request, "relativeY", 0.0, relative_y, error)) {
			return _make_error_response(id, has_id, "invalid_mouse_motion", error);
		}
		x = last_mouse_position.x + relative_x;
		y = last_mouse_position.y + relative_y;
	}

	_inject_mouse_motion(Vector2(x, y), Vector2(relative_x, relative_y));

	Dictionary result;
	result["x"] = x;
	result["y"] = y;
	result["relativeX"] = relative_x;
	result["relativeY"] = relative_y;
	return _make_success_response(id, has_id, result);
}

Dictionary CLIAIInputServer::_cmd_wait(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	int frames = 0;
	String error;
	if (!_get_int(p_request, "frames", 0, frames, error)) {
		return _make_error_response(id, has_id, "invalid_wait", error);
	}
	if (frames < 1) {
		return _make_error_response(id, has_id, "invalid_wait", "wait.frames must be greater than or equal to 1.");
	}

	const uint64_t frame = _current_frame();
	if (p_from_batch) {
		active_batch.waiting = true;
		active_batch.wait_target_frame = frame + (uint64_t)frames;
		r_deferred = true;
		return Dictionary();
	}

	PendingWait wait;
	wait.id = id;
	wait.has_id = has_id;
	wait.started_frame = frame;
	wait.target_frame = frame + (uint64_t)frames;
	wait.frames = frames;
	pending_waits.push_back(wait);
	r_deferred = true;
	return Dictionary();
}

String CLIAIInputServer::_resolve_screenshot_directory() const {
	String directory = options.screenshot_directory;
	if (directory.is_empty()) {
		directory = OS::get_singleton()->get_temp_path();
	}
	if (directory.begins_with("res://") || directory.begins_with("user://")) {
		directory = ProjectSettings::get_singleton()->globalize_path(directory);
	}
	return directory;
}

Dictionary CLIAIInputServer::_cmd_get_screenshot(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	SceneTree *scene_tree = Object::cast_to<SceneTree>(OS::get_singleton()->get_main_loop());
	if (!scene_tree || !scene_tree->get_root()) {
		return _make_error_response(id, has_id, "screenshot_unavailable", "SceneTree root viewport is unavailable.");
	}

	Ref<ViewportTexture> texture = scene_tree->get_root()->get_texture();
	if (texture.is_null()) {
		return _make_error_response(id, has_id, "screenshot_unavailable", "Root viewport texture is unavailable.");
	}

	Ref<Image> image = texture->get_image();
	if (image.is_null() || image->is_empty()) {
		return _make_error_response(id, has_id, "screenshot_unavailable", "Root viewport image is unavailable.");
	}

	const String directory = _resolve_screenshot_directory();
	const Error dir_err = DirAccess::make_dir_recursive_absolute(directory);
	if (dir_err != OK) {
		return _make_error_response(id, has_id, "screenshot_failed", vformat("Unable to create screenshot directory \"%s\" (error code %d).", directory, (int)dir_err));
	}

	const String name = p_request.has("name") ? String(p_request["name"]) : String();
	const String filename = vformat("godot-ai-agent-%d-%d-%s.png", OS::get_singleton()->get_process_id(), (int)_current_frame(), has_id ? id.stringify().validate_filename() : "request");
	const String path = directory.path_join(filename);
	const Error save_err = image->save_png(path);
	if (save_err != OK) {
		return _make_error_response(id, has_id, "screenshot_failed", vformat("Unable to save screenshot \"%s\" (error code %d).", path, (int)save_err));
	}

	Dictionary result;
	result["name"] = name;
	result["path"] = path;
	result["width"] = image->get_width();
	result["height"] = image->get_height();
	return _make_success_response(id, has_id, result);
}

void CLIAIInputServer::_append_scene_tree_node(Node *p_node, int p_depth, int p_max_depth, Array &r_nodes) const {
	if (!p_node || p_depth > p_max_depth) {
		return;
	}

	Dictionary item;
	item["depth"] = p_depth;
	item["path"] = String(p_node->get_path());
	item["name"] = String(p_node->get_name());
	item["type"] = p_node->get_class();
	item["instanceId"] = (int64_t)p_node->get_instance_id();
	item["childCount"] = p_node->get_child_count();

	bool visible = true;
	if (CanvasItem *canvas_item = Object::cast_to<CanvasItem>(p_node)) {
		visible = canvas_item->is_visible();
	} else if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
		visible = node_3d->is_visible();
	} else if (Window *window = Object::cast_to<Window>(p_node)) {
		visible = window->is_visible();
	}
	item["visible"] = visible;
	r_nodes.push_back(item);

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_append_scene_tree_node(p_node->get_child(i), p_depth + 1, p_max_depth, r_nodes);
	}
}

Dictionary CLIAIInputServer::_cmd_get_scene_tree(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	int max_depth = options.default_scene_tree_max_depth;
	String error;
	if (!_get_int(p_request, "maxDepth", max_depth, max_depth, error)) {
		return _make_error_response(id, has_id, "invalid_scene_tree", error);
	}
	max_depth = CLAMP(max_depth, 0, 128);

	SceneTree *scene_tree = Object::cast_to<SceneTree>(OS::get_singleton()->get_main_loop());
	if (!scene_tree || !scene_tree->get_root()) {
		return _make_error_response(id, has_id, "scene_tree_unavailable", "SceneTree root is unavailable.");
	}

	Array nodes;
	_append_scene_tree_node(scene_tree->get_root(), 0, max_depth, nodes);

	Dictionary result;
	result["maxDepth"] = max_depth;
	result["nodes"] = nodes;
	return _make_success_response(id, has_id, result);
}

Dictionary CLIAIInputServer::_cmd_perf_start(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	if (runtime_perf_recorder) {
		return _make_error_response(id, has_id, "busy", "Runtime performance recording is already active.");
	}

	int top_frames = options.default_perf_top_frames;
	String error;
	if (!_get_int(p_request, "topFrames", top_frames, top_frames, error)) {
		return _make_error_response(id, has_id, "invalid_perf", error);
	}
	if (top_frames < 0) {
		return _make_error_response(id, has_id, "invalid_perf", "perf_start.topFrames must be greater than or equal to 0.");
	}

	CLIPerformanceRecorder::Options perf_options;
	perf_options.enabled = true;
	perf_options.start_frame = (int)_current_frame();
	perf_options.top_frames = top_frames;
	if (p_request.has("name")) {
		perf_options.recording_name = String(p_request["name"]);
	}
	if (p_request.has("samplesFile")) {
		perf_options.samples_file = String(p_request["samplesFile"]);
		perf_options.samples_file_set = true;
	}

	runtime_perf_recorder = memnew(CLIPerformanceRecorder(perf_options));
	String initialize_error;
	if (runtime_perf_recorder->initialize(initialize_error) != OK) {
		memdelete(runtime_perf_recorder);
		runtime_perf_recorder = nullptr;
		return _make_error_response(id, has_id, "perf_samples_file_failed", initialize_error);
	}

	Dictionary result;
	result["name"] = perf_options.recording_name;
	result["startFrame"] = perf_options.start_frame;
	result["topFrames"] = top_frames;
	return _make_success_response(id, has_id, result);
}

void CLIAIInputServer::_mark_runtime_perf_stop_requested(const Variant &p_id, bool p_has_id) {
	pending_perf_stop = true;
	pending_perf_stop_id = p_id;
	pending_perf_stop_has_id = p_has_id;
}

Dictionary CLIAIInputServer::_cmd_perf_stop(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	if (!runtime_perf_recorder) {
		return _make_error_response(id, has_id, "perf_not_active", "Runtime performance recording is not active.");
	}
	if (p_from_batch) {
		if (runtime_perf_recorder->get_captured_frames() > 0) {
			runtime_perf_recorder->set_summary_end_frame((int64_t)runtime_perf_recorder->get_last_sampled_frame());
		} else {
			runtime_perf_recorder->set_summary_end_frame((int64_t)_current_frame());
		}
		Dictionary summary = runtime_perf_recorder->build_summary(true);
		runtime_perf_recorder->close();
		memdelete(runtime_perf_recorder);
		runtime_perf_recorder = nullptr;
		return _make_success_response(id, has_id, summary);
	}
	_mark_runtime_perf_stop_requested(id, has_id);
	r_deferred = true;
	return Dictionary();
}

Dictionary CLIAIInputServer::_cmd_perf_status() const {
	Dictionary result;
	result["active"] = runtime_perf_recorder != nullptr;
	result["capturedFrames"] = runtime_perf_recorder ? (int64_t)runtime_perf_recorder->get_captured_frames() : 0;
	result["lastSampledFrame"] = runtime_perf_recorder && runtime_perf_recorder->get_captured_frames() > 0 ? (int64_t)runtime_perf_recorder->get_last_sampled_frame() : -1;
	result["pendingStop"] = pending_perf_stop;
	return result;
}

void CLIAIInputServer::record_frame(uint64_t p_frame_index, uint64_t p_frame_time_usec, uint64_t p_process_time_usec, uint64_t p_physics_process_time_usec, uint64_t p_navigation_process_time_usec, double p_physics_frame_time_sec) {
	if (!runtime_perf_recorder) {
		return;
	}

	runtime_perf_recorder->record_frame(p_frame_index, p_frame_time_usec, p_process_time_usec, p_physics_process_time_usec, p_navigation_process_time_usec, p_physics_frame_time_sec);
	if (pending_perf_stop) {
		_finish_runtime_perf_stop(true, false);
	}
}

void CLIAIInputServer::_finish_runtime_perf_stop(bool p_completed, bool p_event_response) {
	if (!runtime_perf_recorder) {
		pending_perf_stop = false;
		return;
	}

	if (runtime_perf_recorder->get_captured_frames() > 0) {
		runtime_perf_recorder->set_summary_end_frame((int64_t)runtime_perf_recorder->get_last_sampled_frame());
	} else {
		runtime_perf_recorder->set_summary_end_frame((int64_t)_current_frame());
	}

	Dictionary response = _make_success_response(pending_perf_stop_id, pending_perf_stop_has_id, runtime_perf_recorder->build_summary(p_completed));
	if (p_event_response) {
		response["event"] = "perf_summary";
	}
	_send_response(response);

	runtime_perf_recorder->close();
	memdelete(runtime_perf_recorder);
	runtime_perf_recorder = nullptr;
	pending_perf_stop = false;
	pending_perf_stop_id = Variant();
	pending_perf_stop_has_id = false;
}

void CLIAIInputServer::_release_held_inputs() {
	for (const StringName &action : held_actions) {
		Input::get_singleton()->action_release(action);
	}
	held_actions.clear();

	Vector<HeldKey> keys;
	for (const KeyValue<int64_t, HeldKey> &E : held_keys) {
		keys.push_back(E.value);
	}
	for (int i = 0; i < keys.size(); i++) {
		const HeldKey &held_key = keys[i];
		Ref<InputEventKey> event;
		event.instantiate();
		event->set_keycode(held_key.keycode);
		event->set_physical_keycode(held_key.keycode);
		event->set_pressed(false);
		event->set_shift_pressed(held_key.shift);
		event->set_ctrl_pressed(held_key.ctrl);
		event->set_alt_pressed(held_key.alt);
		event->set_meta_pressed(held_key.meta);
		Input::get_singleton()->parse_input_event(event);
	}
	held_keys.clear();

	Vector<int> buttons;
	for (const int &button : held_mouse_buttons) {
		buttons.push_back(button);
	}
	for (int i = 0; i < buttons.size(); i++) {
		_inject_mouse_button((MouseButton)buttons[i], last_mouse_position, false, false);
	}
	held_mouse_buttons.clear();
	ai_mouse_button_mask.clear();
}

void CLIAIInputServer::_abort_runtime_perf(bool p_send_event) {
	if (!runtime_perf_recorder) {
		pending_perf_stop = false;
		return;
	}

	if (p_send_event) {
		_finish_runtime_perf_stop(false, true);
		return;
	}

	runtime_perf_recorder->close();
	memdelete(runtime_perf_recorder);
	runtime_perf_recorder = nullptr;
	pending_perf_stop = false;
	pending_perf_stop_id = Variant();
	pending_perf_stop_has_id = false;
}

void CLIAIInputServer::shutdown() {
	if (runtime_perf_recorder) {
		_finish_runtime_perf_stop(false, true);
	}
	_release_held_inputs();

	if (client.is_valid()) {
		client->disconnect_from_host();
		client.unref();
	}
	if (server.is_valid()) {
		server->stop();
		server.unref();
	}
	line_buffer.clear();
	pending_client_bytes.clear();
	pending_waits.clear();
	active_batch = ActiveBatch();
}
