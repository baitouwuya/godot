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
#include "main/custom_feature_tracer.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/gui/control.h"
#include "scene/gui/line_edit.h"
#include "scene/main/canvas_item.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/rendering/rendering_server.h"

static const int MAX_OPS_PER_FRAME = 128;

namespace {

struct AIAgentCommandHelpEntry {
	const char *name;
	const char *aliases;
	const char *category;
	const char *summary;
	const char *key_params;
	bool may_defer;
	const char *example;
};

static const AIAgentCommandHelpEntry AI_AGENT_COMMAND_HELP[] = {
		{ "ping", "", "core", "Health-check request.", "id (optional)", false, "{\"id\":1,\"cmd\":\"ping\"}" },
		{ "get_status", "", "core", "Returns runtime status and active operations.", "id (optional)", false, "{\"id\":2,\"cmd\":\"get_status\"}" },
		{ "batch", "", "batch", "Runs multiple ops with stop/continue policy.", "ops[], onError", true, "{\"id\":3,\"cmd\":\"batch\",\"onError\":\"stop\",\"ops\":[{\"cmd\":\"ping\"}]}" },
		{ "batch_status", "", "batch", "Returns active batch progress.", "id (optional)", false, "{\"id\":4,\"cmd\":\"batch_status\"}" },
		{ "cancel_batch", "", "batch", "Cancels currently active batch.", "id (optional)", false, "{\"id\":5,\"cmd\":\"cancel_batch\"}" },
		{ "list_checkpoints", "", "batch", "Lists checkpoint events emitted by batch ops.", "id (optional)", false, "{\"id\":6,\"cmd\":\"list_checkpoints\"}" },
		{ "checkpoint", "", "batch", "Emits an in-band checkpoint event for current batch step.", "name (optional)", false, "{\"cmd\":\"checkpoint\",\"name\":\"after_menu\"}" },
		{ "action", "", "input", "Press/release mapped Input action.", "action, pressed", false, "{\"cmd\":\"action\",\"action\":\"ui_accept\",\"pressed\":true}" },
		{ "key", "", "input", "Injects key event with modifiers.", "keycode, pressed", false, "{\"cmd\":\"key\",\"keycode\":\"KEY_ENTER\",\"pressed\":true}" },
		{ "mouse_button", "", "input", "Injects mouse button press/release.", "button, pressed, x, y", false, "{\"cmd\":\"mouse_button\",\"button\":\"left\",\"pressed\":true,\"x\":100,\"y\":120}" },
		{ "mouse_motion", "", "input", "Injects mouse motion to screen position.", "x, y", false, "{\"cmd\":\"mouse_motion\",\"x\":100,\"y\":120}" },
		{ "type_text_commit", "", "input", "Commits text directly to focused control.", "text", false, "{\"cmd\":\"type_text_commit\",\"text\":\"player01\"}" },
		{ "wait", "", "wait", "Waits for N frames.", "frames", true, "{\"id\":7,\"cmd\":\"wait\",\"frames\":10}" },
		{ "wait_until", "expect", "wait", "Waits until condition is satisfied.", "condition, timeoutFrames, pollEveryFrames", true, "{\"id\":8,\"cmd\":\"wait_until\",\"condition\":{\"kind\":\"node_exists\",\"selector\":{\"name\":\"PlayButton\"}}}" },
		{ "wait_node_exists", "", "wait", "Waits until selector resolves to a node.", "selector", true, "{\"id\":9,\"cmd\":\"wait_node_exists\",\"selector\":{\"name\":\"PlayButton\"}}" },
		{ "wait_node_gone", "", "wait", "Waits until selector no longer resolves.", "selector", true, "{\"id\":10,\"cmd\":\"wait_node_gone\",\"selector\":{\"name\":\"LoadingPanel\"}}" },
		{ "wait_property", "", "wait", "Waits for node property to equal expected value.", "selector, property, equals", true, "{\"id\":11,\"cmd\":\"wait_property\",\"selector\":{\"name\":\"SpeedBar\"},\"property\":\"value\",\"equals\":10}" },
		{ "wait_scene_changed", "", "wait", "Waits for scene/content scene change.", "timeoutFrames, pollEveryFrames", true, "{\"id\":12,\"cmd\":\"wait_scene_changed\"}" },
		{ "wait_screenshot_diff", "", "wait", "Waits until screenshot differs from baseline.", "selector(optional), threshold", true, "{\"id\":13,\"cmd\":\"wait_screenshot_diff\",\"threshold\":0.02}" },
		{ "get_interactables", "", "observation", "Lists interactable UI/3D targets.", "filter(optional)", false, "{\"id\":14,\"cmd\":\"get_interactables\"}" },
		{ "query_nodes", "", "observation", "Queries snapshots by selector filters.", "filter", false, "{\"id\":15,\"cmd\":\"query_nodes\",\"filter\":{\"group\":\"menu\"}}" },
		{ "get_node_snapshot", "", "observation", "Returns one node snapshot for selector.", "selector", false, "{\"id\":16,\"cmd\":\"get_node_snapshot\",\"selector\":{\"name\":\"PlayButton\"}}" },
		{ "get_viewport_summary", "", "observation", "Returns viewport and camera summary.", "includeCounts(optional)", false, "{\"id\":17,\"cmd\":\"get_viewport_summary\"}" },
		{ "get_scene_tree", "", "observation", "Returns bounded scene tree structure.", "maxDepth(optional)", false, "{\"id\":18,\"cmd\":\"get_scene_tree\",\"maxDepth\":4}" },
		{ "get_screenshot", "", "observation", "Captures screenshot and returns path/metadata.", "path(optional), crop(optional)", false, "{\"id\":19,\"cmd\":\"get_screenshot\"}" },
		{ "get_last_error_bundle", "", "diagnostics", "Returns most recent contextual error bundle.", "id(optional)", false, "{\"id\":20,\"cmd\":\"get_last_error_bundle\"}" },
		{ "perf_start", "", "diagnostics", "Starts runtime perf recording.", "topFrames(optional)", false, "{\"id\":21,\"cmd\":\"perf_start\",\"topFrames\":10}" },
		{ "perf_stop", "", "diagnostics", "Stops runtime perf recording and returns summary.", "id(optional)", true, "{\"id\":22,\"cmd\":\"perf_stop\"}" },
		{ "perf_status", "", "diagnostics", "Returns runtime perf recorder status.", "id(optional)", false, "{\"id\":23,\"cmd\":\"perf_status\"}" },
		{ "click", "", "high_level", "Expands to pointer move/press/wait/release.", "x, y, button(optional)", false, "{\"cmd\":\"click\",\"x\":100,\"y\":120}" },
		{ "double_click", "", "high_level", "Expands to two click sequences.", "x, y, button(optional)", false, "{\"cmd\":\"double_click\",\"x\":100,\"y\":120}" },
		{ "hold", "", "high_level", "Press-hold-release at screen position.", "x, y, frames, button(optional)", false, "{\"cmd\":\"hold\",\"x\":100,\"y\":120,\"frames\":6}" },
		{ "drag", "", "high_level", "Drags pointer along a path.", "path[], frames(optional), button(optional)", false, "{\"cmd\":\"drag\",\"path\":[[100,120],[320,120]],\"frames\":12}" },
		{ "hover_target", "", "high_level", "Moves pointer to selector target.", "selector", false, "{\"cmd\":\"hover_target\",\"selector\":{\"name\":\"PlayButton\"}}" },
		{ "focus_target", "", "high_level", "Focuses Control or moves pointer to target.", "selector", false, "{\"cmd\":\"focus_target\",\"selector\":{\"name\":\"NameInput\"}}" },
		{ "click_target", "", "high_level", "Clicks selector-resolved target.", "selector, button(optional)", false, "{\"cmd\":\"click_target\",\"selector\":{\"name\":\"PlayButton\"}}" },
		{ "double_click_target", "", "high_level", "Double-clicks selector-resolved target.", "selector, button(optional)", false, "{\"cmd\":\"double_click_target\",\"selector\":{\"name\":\"PlayButton\"}}" },
		{ "drag_target_to_target", "", "high_level", "Drags from source selector to destination selector.", "fromSelector, toSelector, frames(optional)", false, "{\"cmd\":\"drag_target_to_target\",\"fromSelector\":{\"name\":\"A\"},\"toSelector\":{\"name\":\"B\"}}" },
		{ "scroll_view", "", "high_level", "Sends wheel input on selector target.", "selector, direction, steps(optional)", false, "{\"cmd\":\"scroll_view\",\"selector\":{\"name\":\"List\"},\"direction\":\"down\",\"steps\":2}" },
		{ "type_text", "", "high_level", "Focuses target then commits text.", "selector, text", false, "{\"cmd\":\"type_text\",\"selector\":{\"name\":\"NameInput\"},\"text\":\"Player\"}" },
		{ "click_target_and_wait", "", "high_level", "Clicks target then waits by condition.", "selector, wait", false, "{\"cmd\":\"click_target_and_wait\",\"selector\":{\"name\":\"PlayButton\"},\"wait\":{\"condition\":{\"kind\":\"scene_changed\"}}}" },
		{ "type_text_and_wait", "", "high_level", "Types text then waits by condition.", "selector, text, wait", false, "{\"cmd\":\"type_text_and_wait\",\"selector\":{\"name\":\"NameInput\"},\"text\":\"Player\",\"wait\":{\"condition\":{\"kind\":\"node_exists\",\"selector\":{\"name\":\"LevelRoot\"}}}}" },
};

static String _command_aliases_or_none(const char *p_aliases) {
	const String aliases = p_aliases;
	return aliases.is_empty() ? "-" : aliases;
}

static bool _is_supported_immediate_command_name(const String &p_cmd) {
	for (const AIAgentCommandHelpEntry &entry : AI_AGENT_COMMAND_HELP) {
		if (p_cmd == String(entry.name)) {
			return true;
		}
		const PackedStringArray aliases = String(entry.aliases).split(",", false);
		for (const String &alias : aliases) {
			if (p_cmd == alias.strip_edges()) {
				return true;
			}
		}
	}
	return false;
}

} // namespace

static String _trace_correlation_from_request(CustomFeatureTracer *p_tracer, const Dictionary &p_request) {
	if (!p_tracer) {
		return String();
	}
	return p_request.has("id") ? p_tracer->correlation_id_from_external_id(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, p_request["id"]) : p_tracer->next_correlation_id(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL);
}

static Dictionary _make_trace_request_data(const Dictionary &p_request) {
	Dictionary data;
	data["cmd"] = p_request.get("cmd", String());
	data["hasId"] = p_request.has("id");
	if (p_request.has("id")) {
		data["id"] = p_request["id"];
	}
	return data;
}

static Dictionary _make_trace_response_data(const Dictionary &p_response) {
	Dictionary data;
	data["ok"] = p_response.get("ok", false);
	data["frame"] = p_response.get("frame", -1);
	data["hasId"] = p_response.has("id");
	if (p_response.has("id")) {
		data["id"] = p_response["id"];
	}
	if (p_response.has("event")) {
		data["event"] = p_response["event"];
	}
	if (p_response.has("error")) {
		Dictionary error = p_response["error"];
		data["errorCode"] = error.get("code", String());
	}
	return data;
}

static Variant _get_object_property_or_nil(Object *p_object, const StringName &p_property, bool &r_valid) {
	r_valid = false;
	if (!p_object) {
		return Variant();
	}
	return p_object->get(p_property, &r_valid);
}

static Dictionary _make_point_rect(const Vector2 &p_point, float p_extent = 4.0f) {
	Dictionary rect;
	rect["x"] = p_point.x - p_extent * 0.5f;
	rect["y"] = p_point.y - p_extent * 0.5f;
	rect["width"] = p_extent;
	rect["height"] = p_extent;
	return rect;
}

static SceneTree *_get_runtime_scene_tree() {
	if (SceneTree::get_singleton()) {
		return SceneTree::get_singleton();
	}
	return OS::get_singleton() ? Object::cast_to<SceneTree>(OS::get_singleton()->get_main_loop()) : nullptr;
}

void CLIAIInputServer::register_project_settings() {
	GLOBAL_DEF_BASIC("editor/ai_agent_control/enabled", false);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/port", PROPERTY_HINT_RANGE, "1,65535,1"), 7010);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/max_batch_ops", PROPERTY_HINT_RANGE, "1,65536,1"), 1024);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/max_line_bytes", PROPERTY_HINT_RANGE, "256,16777216,1"), 1048576);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/default_perf_top_frames", PROPERTY_HINT_RANGE, "0,1024,1"), 10);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/default_scene_tree_max_depth", PROPERTY_HINT_RANGE, "0,128,1"), 32);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/default_wait_timeout_frames", PROPERTY_HINT_RANGE, "1,65536,1"), 240);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/default_poll_every_frames", PROPERTY_HINT_RANGE, "1,1024,1"), 1);
	GLOBAL_DEF_BASIC("editor/ai_agent_control/default_capture_on_error", false);
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "editor/ai_agent_control/default_query_max_results", PROPERTY_HINT_RANGE, "1,4096,1"), 128);
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

	if (p_arg == "--ai-agent-help") {
		r_options.help_enabled = true;
		return true;
	}

	if (p_arg == "--ai-agent-help-format") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (value == "text") {
			r_options.help_format = HELP_FORMAT_TEXT;
		} else if (value == "json") {
			r_options.help_format = HELP_FORMAT_JSON;
		} else if (value == "both") {
			r_options.help_format = HELP_FORMAT_BOTH;
		} else {
			r_error = "--ai-agent-help-format must be text, json or both.";
			return true;
		}
		r_options.help_format_set = true;
		return true;
	}

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

Error CLIAIInputServer::validate_help_options(const Options &p_options, String &r_error) {
	r_error = String();
	if (!p_options.help_enabled && p_options.help_format_set) {
		r_error = "--ai-agent-help-format requires --ai-agent-help.";
		return ERR_INVALID_PARAMETER;
	}
	return OK;
}

String CLIAIInputServer::get_help_format_name(HelpFormat p_format) {
	switch (p_format) {
		case HELP_FORMAT_TEXT:
			return "text";
		case HELP_FORMAT_JSON:
			return "json";
		case HELP_FORMAT_BOTH:
			return "both";
	}
	return "text";
}

Error CLIAIInputServer::build_help_outputs(String &r_text_output, String &r_json_output, String &r_error) {
	r_text_output = String();
	r_json_output = String();
	r_error = String();

	String text;
	text += "Runtime AI Agent Control Protocol Help\n";
	text += "=====================================\n\n";
	text += "Transport\n";
	text += "- Localhost TCP JSONL stream.\n";
	text += "- One JSON object per line.\n";
	text += "- Default port: 7010 (override with --ai-agent-port).\n\n";
	text += "Request Envelope\n";
	text += "- Required: cmd (string).\n";
	text += "- Optional: id (any JSON scalar/object), captureOnError (bool), and command-specific fields.\n";
	text += "- High-level commands expand to low-level ops before execution.\n\n";
	text += "Response Envelope\n";
	text += "- ok (bool), frame (int64), result (on success), error { code, message } (on failure).\n";
	text += "- id is echoed when provided.\n";
	text += "- sceneDigest may be attached for top-level request responses.\n\n";
	text += "Commands\n";
	text += "--------\n";
	for (const AIAgentCommandHelpEntry &entry : AI_AGENT_COMMAND_HELP) {
		text += vformat("* %s", entry.name);
		const String aliases = _command_aliases_or_none(entry.aliases);
		text += vformat(" | category=%s | aliases=%s | deferred=%s\n", entry.category, aliases, entry.may_defer ? "yes" : "no");
		text += vformat("  - %s\n", entry.summary);
		text += vformat("  - key params: %s\n", entry.key_params);
		text += vformat("  - example: %s\n", entry.example);
	}
	r_text_output = text;

	Dictionary root;
	Dictionary transport;
	transport["type"] = "tcp_jsonl";
	transport["host"] = "127.0.0.1";
	transport["defaultPort"] = 7010;
	transport["lineDelimitedJson"] = true;
	root["transport"] = transport;

	Dictionary request_envelope;
	PackedStringArray required_fields;
	required_fields.push_back("cmd");
	PackedStringArray optional_fields;
	optional_fields.push_back("id");
	optional_fields.push_back("captureOnError");
	request_envelope["required"] = required_fields;
	request_envelope["optional"] = optional_fields;
	request_envelope["note"] = "High-level commands expand to low-level ops before execution.";
	root["requestEnvelope"] = request_envelope;

	Dictionary response_envelope;
	PackedStringArray success_fields;
	success_fields.push_back("ok");
	success_fields.push_back("frame");
	success_fields.push_back("result");
	PackedStringArray error_fields;
	error_fields.push_back("ok");
	error_fields.push_back("frame");
	error_fields.push_back("error.code");
	error_fields.push_back("error.message");
	PackedStringArray response_optional_fields;
	response_optional_fields.push_back("id");
	response_optional_fields.push_back("sceneDigest");
	response_optional_fields.push_back("lastResolvedTarget");
	response_optional_fields.push_back("screenshotPath");
	response_envelope["successFields"] = success_fields;
	response_envelope["errorFields"] = error_fields;
	response_envelope["optionalFields"] = response_optional_fields;
	root["responseEnvelope"] = response_envelope;

	Array commands;
	for (const AIAgentCommandHelpEntry &entry : AI_AGENT_COMMAND_HELP) {
		Dictionary cmd;
		cmd["name"] = entry.name;
		const PackedStringArray aliases = String(entry.aliases).split(",", false);
		Array alias_array;
		for (const String &alias : aliases) {
			const String clean_alias = alias.strip_edges();
			if (!clean_alias.is_empty()) {
				alias_array.push_back(clean_alias);
			}
		}
		cmd["aliases"] = alias_array;
		cmd["category"] = entry.category;
		cmd["summary"] = entry.summary;
		cmd["keyParams"] = entry.key_params;
		cmd["mayDefer"] = entry.may_defer;
		cmd["example"] = entry.example;
		commands.push_back(cmd);
	}
	root["commands"] = commands;

	r_json_output = JSON::stringify(root);
	return OK;
}

Vector<String> CLIAIInputServer::get_supported_command_names() {
	Vector<String> names;
	for (const AIAgentCommandHelpEntry &entry : AI_AGENT_COMMAND_HELP) {
		names.push_back(entry.name);
	}
	return names;
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
	r_options.default_wait_timeout_frames = GLOBAL_GET("editor/ai_agent_control/default_wait_timeout_frames");
	r_options.default_poll_every_frames = GLOBAL_GET("editor/ai_agent_control/default_poll_every_frames");
	r_options.default_capture_on_error = GLOBAL_GET("editor/ai_agent_control/default_capture_on_error");
	r_options.default_query_max_results = GLOBAL_GET("editor/ai_agent_control/default_query_max_results");
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
	if (p_options.default_wait_timeout_frames < 1) {
		r_error = "editor/ai_agent_control/default_wait_timeout_frames must be greater than or equal to 1.";
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.default_poll_every_frames < 1) {
		r_error = "editor/ai_agent_control/default_poll_every_frames must be greater than or equal to 1.";
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.default_query_max_results < 1) {
		r_error = "editor/ai_agent_control/default_query_max_results must be greater than or equal to 1.";
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

bool CLIAIInputServer::_variant_to_string(const Variant &p_value, String &r_value) {
	if (p_value.get_type() != Variant::STRING && p_value.get_type() != Variant::STRING_NAME) {
		return false;
	}
	r_value = String(p_value);
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

bool CLIAIInputServer::_get_string(const Dictionary &p_dict, const String &p_key, const String &p_default, String &r_value, String &r_error) {
	if (!p_dict.has(p_key)) {
		r_value = p_default;
		return true;
	}
	if (!_variant_to_string(p_dict[p_key], r_value)) {
		r_error = p_key + " must be a string.";
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

Array CLIAIInputServer::_vector2_to_array(const Vector2 &p_value) {
	Array value;
	value.push_back(p_value.x);
	value.push_back(p_value.y);
	return value;
}

Array CLIAIInputServer::_vector3_to_array(const Vector3 &p_value) {
	Array value;
	value.push_back(p_value.x);
	value.push_back(p_value.y);
	value.push_back(p_value.z);
	return value;
}

Dictionary CLIAIInputServer::_rect2_to_dictionary(const Rect2 &p_value) {
	Dictionary rect;
	rect["x"] = p_value.position.x;
	rect["y"] = p_value.position.y;
	rect["width"] = p_value.size.x;
	rect["height"] = p_value.size.y;
	return rect;
}

bool CLIAIInputServer::_dictionary_to_rect2(const Dictionary &p_rect, Rect2 &r_rect, String &r_error) {
	double x = 0.0;
	double y = 0.0;
	double width = 0.0;
	double height = 0.0;
	if (!_get_double(p_rect, "x", 0.0, x, r_error) || !_get_double(p_rect, "y", 0.0, y, r_error) || !_get_double(p_rect, "width", 0.0, width, r_error) || !_get_double(p_rect, "height", 0.0, height, r_error)) {
		return false;
	}
	r_rect = Rect2(x, y, width, height);
	return true;
}

Rect2 CLIAIInputServer::_transform_rect(const Transform2D &p_transform, const Rect2 &p_rect) {
	Rect2 rect(p_transform.xform(p_rect.position), Vector2());
	rect.expand_to(p_transform.xform(p_rect.position + Vector2(p_rect.size.x, 0.0)));
	rect.expand_to(p_transform.xform(p_rect.position + p_rect.size));
	rect.expand_to(p_transform.xform(p_rect.position + Vector2(0.0, p_rect.size.y)));
	rect.size = rect.size.abs();
	return rect;
}

Vector2 CLIAIInputServer::_rect_center(const Dictionary &p_rect) {
	double x = 0.0;
	double y = 0.0;
	double width = 0.0;
	double height = 0.0;
	String unused_error;
	_get_double(p_rect, "x", 0.0, x, unused_error);
	_get_double(p_rect, "y", 0.0, y, unused_error);
	_get_double(p_rect, "width", 0.0, width, unused_error);
	_get_double(p_rect, "height", 0.0, height, unused_error);
	return Vector2(x + width * 0.5, y + height * 0.5);
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
	CustomFeatureTracer *tracer = CustomFeatureTracer::get_singleton();
	CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL);

	if (!options.listen_tcp) {
		if (tracer) {
			Dictionary data;
			data["localOnly"] = true;
			tracer->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "local_executor_started", "info", String(), data);
		}
		return OK;
	}

	server.instantiate();
	const Error err = server->listen((uint16_t)options.port, IPAddress("127.0.0.1"));
	if (err != OK) {
		if (tracer) {
			Dictionary data;
			data["port"] = options.port;
			tracer->record_error_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "server_listen_failed", String(), "listen_failed", vformat("Unable to listen for AI agent control on 127.0.0.1:%d (error code %d).", options.port, (int)err), data);
		}
		r_error = vformat("Unable to listen for AI agent control on 127.0.0.1:%d (error code %d).", options.port, (int)err);
		server.unref();
		return err;
	}
	if (tracer) {
		Dictionary data;
		data["host"] = "127.0.0.1";
		data["port"] = options.port;
		tracer->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "server_started", "info", String(), data);
	}

	OS::get_singleton()->printerr("AI agent control listening on 127.0.0.1:%d\n", options.port);
	return OK;
}

bool CLIAIInputServer::is_listening() const {
	return server.is_valid() && server->is_listening();
}

void CLIAIInputServer::_send_response(const Dictionary &p_response) {
	if (local_response_capture_active) {
		local_response_queue.push_back(p_response);
		return;
	}
#ifdef TESTS_ENABLED
	test_sent_responses.push_back(p_response);
#endif
	if (client.is_null() || client->get_status() != StreamPeerSocket::STATUS_CONNECTED) {
		return;
	}

	const String line = JSON::stringify(p_response) + "\n";
	const CharString utf8 = line.utf8();
	client->put_data((const uint8_t *)utf8.get_data(), utf8.length());
}

Dictionary CLIAIInputServer::execute_local_request(const Dictionary &p_request) {
	CustomFeatureTracer *tracer = CustomFeatureTracer::get_singleton();
	const String correlation_id = _trace_correlation_from_request(tracer, p_request);
	CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, correlation_id);
	if (tracer) {
		Dictionary payloads;
		tracer->add_json_payload(payloads, "request", p_request);
		tracer->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "request_received", "info", correlation_id, _make_trace_request_data(p_request), payloads);
	}

	bool deferred = false;
	local_response_capture_active = true;
	const Dictionary response = _handle_request(p_request, false, deferred);
	if (!deferred) {
		local_response_capture_active = false;
		_record_last_error_bundle(response);
		if (tracer) {
			Dictionary payloads;
			tracer->add_json_payload(payloads, "response", response);
			const Dictionary error = response.has("error") && response["error"].get_type() == Variant::DICTIONARY ? (Dictionary)response["error"] : Dictionary();
			tracer->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "response_sent", error.is_empty() ? "info" : (error.get("code", String()) == "timeout" ? "warning" : "error"), correlation_id, _make_trace_response_data(response), payloads, error);
		}
		return response;
	}

	Dictionary deferred_response;
	deferred_response["ok"] = true;
	deferred_response["frame"] = (int64_t)_current_frame();
	deferred_response["deferred"] = true;
	return deferred_response;
}

bool CLIAIInputServer::pop_local_response(Dictionary &r_response) {
	if (local_response_queue.is_empty()) {
		r_response.clear();
		return false;
	}
	r_response = local_response_queue[0];
	local_response_queue.remove_at(0);
	return true;
}

bool CLIAIInputServer::_dispatch_root_input(const Ref<InputEvent> &p_event, bool p_local_coords) const {
	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root() || p_event.is_null()) {
		return false;
	}

	Window *root = scene_tree->get_root();
	if (InputEventFromWindow *window_event = Object::cast_to<InputEventFromWindow>(*p_event)) {
		window_event->set_window_id(root->get_window_id());
	}
	if (p_local_coords) {
		Input::get_singleton()->parse_input_event(p_event);
	} else {
		root->push_input(p_event, false);
	}
	return true;
}

bool CLIAIInputServer::_dispatch_root_text_input(const String &p_text) const {
	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return false;
	}
	scene_tree->get_root()->push_text_input(p_text);
	return true;
}

bool CLIAIInputServer::_get_root_window_mouse_coordinates(const Vector2 &p_position, Vector2 &r_local_position, Vector2 &r_global_position) const {
	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return false;
	}

	Window *root = scene_tree->get_root();
	r_local_position = p_position;
	r_global_position = p_position;
	if (!root) {
		return false;
	}

	const Transform2D canvas_inverse = root->get_global_canvas_transform().affine_inverse();
	r_global_position = canvas_inverse.xform(p_position);
	return true;
}

void CLIAIInputServer::_send_busy_and_close(Ref<StreamPeerTCP> p_peer) {
	Dictionary response;
	response["ok"] = false;
	response["frame"] = (int64_t)_current_frame();
	response["error"] = _make_error_payload("busy", "Another AI agent client is already connected.");
	CustomFeatureTracer *tracer = CustomFeatureTracer::get_singleton();
	if (tracer) {
		const String correlation_id = tracer->next_correlation_id(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL);
		Dictionary data = _make_trace_response_data(response);
		Dictionary payloads;
		tracer->add_json_payload(payloads, "response", response);
		tracer->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "busy_rejected", "warning", correlation_id, data, payloads);
	}

	const String line = JSON::stringify(response) + "\n";
	const CharString utf8 = line.utf8();
	p_peer->put_data((const uint8_t *)utf8.get_data(), utf8.length());
	p_peer->disconnect_from_host();
}

void CLIAIInputServer::_disconnect_client(bool p_abort_state) {
	CustomFeatureTracer *tracer = CustomFeatureTracer::get_singleton();
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
	if (tracer) {
		Dictionary data;
		data["abortState"] = p_abort_state;
		tracer->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "client_disconnected", "info", String(), data);
	}
	line_buffer.clear();
	pending_client_bytes.clear();
	dropping_oversized_line = false;
	last_resolved_target.clear();
	last_error_bundle.clear();
	checkpoint_history.clear();
	observation_cache = ObservationFrameCache();
	root_image_cache_frame = UINT64_MAX;
	root_image_cache_populated = false;
	root_image_cache_available = false;
	root_image_cache.unref();
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
		if (CustomFeatureTracer::has_singleton()) {
			Dictionary data;
			data["frame"] = (int64_t)_current_frame();
			CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "client_connected", "info", String(), data);
		}
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
			const Dictionary response = _make_error_response(Variant(), false, "line_too_large", "JSONL request exceeded max_line_bytes.");
			_record_last_error_bundle(response);
			_send_response(response);
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

bool CLIAIInputServer::_should_capture_on_error(const Dictionary &p_request) const {
	if (!p_request.has("captureOnError")) {
		return options.default_capture_on_error;
	}
	bool capture = false;
	return _variant_to_bool(p_request["captureOnError"], capture) ? capture : options.default_capture_on_error;
}

void CLIAIInputServer::_decorate_error_response(const Dictionary &p_request, Dictionary &r_response, const Dictionary &p_last_resolved_target) const {
	if (!p_last_resolved_target.is_empty()) {
		r_response["lastResolvedTarget"] = p_last_resolved_target;
	}
	if (!_should_capture_on_error(p_request)) {
		return;
	}

	Dictionary screenshot_result;
	String screenshot_error;
	if (_capture_screenshot_result(p_request, screenshot_result, screenshot_error) == OK && screenshot_result.has("path")) {
		r_response["screenshotPath"] = screenshot_result["path"];
	}
	r_response["sceneDigest"] = _build_scene_digest();
}

Dictionary CLIAIInputServer::_make_contextual_error_response(const Dictionary &p_request, const Variant &p_id, bool p_has_id, const String &p_code, const String &p_message, const Dictionary &p_last_resolved_target) const {
	Dictionary response = _make_error_response(p_id, p_has_id, p_code, p_message);
	_decorate_error_response(p_request, response, p_last_resolved_target);
	return response;
}

void CLIAIInputServer::_record_last_error_bundle(const Dictionary &p_response) {
	if (!p_response.has("ok") || (bool)p_response["ok"]) {
		return;
	}
	last_error_bundle = p_response;
}

bool CLIAIInputServer::_extract_selector_spec(const Dictionary &p_request, const String &p_key, const String &p_label, SelectorSpec &r_spec, String &r_error) const {
	r_error = String();
	if (!p_key.is_empty()) {
		if (!p_request.has(p_key)) {
			r_spec = SelectorSpec();
			return true;
		}
		if (p_request[p_key].get_type() != Variant::DICTIONARY) {
			r_error = p_key + " must be an object.";
			return false;
		}
		return _parse_selector_spec(p_request[p_key], p_label, r_spec, r_error);
	}

	Dictionary selector;
	static const char *keys[] = { "path", "name", "type", "group", "text", "is3D", "visible", "screenRect", "nearestToScreenPoint" };
	for (uint32_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		const String key = keys[i];
		if (p_request.has(key)) {
			selector[key] = p_request[key];
		}
	}
	return _parse_selector_spec(selector, p_label, r_spec, r_error);
}

bool CLIAIInputServer::_parse_selector_spec(const Dictionary &p_dict, const String &p_label, SelectorSpec &r_spec, String &r_error) const {
	r_spec = SelectorSpec();
	r_error = String();
	if (p_dict.is_empty()) {
		return true;
	}

	static const char *allowed_keys[] = { "path", "name", "type", "group", "text", "is3D", "visible", "screenRect", "nearestToScreenPoint" };
	HashSet<String> allowed;
	for (uint32_t i = 0; i < sizeof(allowed_keys) / sizeof(allowed_keys[0]); i++) {
		allowed.insert(String(allowed_keys[i]));
	}

	const Array keys = p_dict.keys();
	for (int i = 0; i < keys.size(); i++) {
		if (keys[i].get_type() != Variant::STRING && keys[i].get_type() != Variant::STRING_NAME) {
			r_error = p_label + " keys must be strings.";
			return false;
		}
		const String key = String(keys[i]);
		if (!allowed.has(key)) {
			r_error = vformat("Unsupported %s field: %s.", p_label, key);
			return false;
		}
	}

	if (!_get_string(p_dict, "path", String(), r_spec.path, r_error) ||
			!_get_string(p_dict, "name", String(), r_spec.name, r_error) ||
			!_get_string(p_dict, "type", String(), r_spec.type, r_error) ||
			!_get_string(p_dict, "group", String(), r_spec.group, r_error) ||
			!_get_string(p_dict, "text", String(), r_spec.text, r_error)) {
		return false;
	}

	if (p_dict.has("is3D")) {
		r_spec.has_is_3d = true;
		if (!_variant_to_bool(p_dict["is3D"], r_spec.is_3d)) {
			r_error = "is3D must be a boolean.";
			return false;
		}
	}
	if (p_dict.has("visible")) {
		r_spec.has_visible = true;
		if (!_variant_to_bool(p_dict["visible"], r_spec.visible)) {
			r_error = "visible must be a boolean.";
			return false;
		}
	}
	if (p_dict.has("screenRect")) {
		if (p_dict["screenRect"].get_type() != Variant::DICTIONARY) {
			r_error = "screenRect must be an object.";
			return false;
		}
		r_spec.has_screen_rect = true;
		if (!_dictionary_to_rect2(p_dict["screenRect"], r_spec.screen_rect, r_error)) {
			r_error = "screenRect." + r_error;
			return false;
		}
	}
	if (p_dict.has("nearestToScreenPoint")) {
		r_spec.has_nearest_to_screen_point = true;
		if (!_get_vector2_from_array(p_dict["nearestToScreenPoint"], r_spec.nearest_to_screen_point)) {
			r_error = "nearestToScreenPoint must be a [x, y] array.";
			return false;
		}
	}

	return true;
}

bool CLIAIInputServer::_resolve_query_limit(const Dictionary &p_request, int &r_max_results, String &r_error) const {
	r_max_results = options.default_query_max_results;
	if (!_get_int(p_request, "maxResults", r_max_results, r_max_results, r_error)) {
		return false;
	}
	if (r_max_results < 1) {
		r_error = "maxResults must be greater than or equal to 1.";
		return false;
	}
	return true;
}

bool CLIAIInputServer::_snapshot_has_screen_position(const Dictionary &p_snapshot) const {
	if (p_snapshot.has("hasScreenPosition") && p_snapshot["hasScreenPosition"].get_type() == Variant::BOOL) {
		return (bool)p_snapshot["hasScreenPosition"];
	}
	return (p_snapshot.has("screenRect") && p_snapshot["screenRect"].get_type() == Variant::DICTIONARY) ||
			(p_snapshot.has("screenPoint") && p_snapshot["screenPoint"].get_type() == Variant::ARRAY);
}

Vector2 CLIAIInputServer::_resolve_target_screen_position(const Dictionary &p_snapshot) const {
	if (p_snapshot.has("screenRect") && p_snapshot["screenRect"].get_type() == Variant::DICTIONARY) {
		return _rect_center(p_snapshot["screenRect"]);
	}
	Vector2 point;
	if (p_snapshot.has("screenPoint") && _get_vector2_from_array(p_snapshot["screenPoint"], point)) {
		return point;
	}
	return Vector2();
}

bool CLIAIInputServer::_is_interactable_snapshot(const Dictionary &p_snapshot) const {
	if (!p_snapshot.has("visible") || !(bool)p_snapshot["visible"]) {
		return false;
	}
	if (p_snapshot.has("is3D") && (bool)p_snapshot["is3D"]) {
		return _is_clickable_3d_snapshot(p_snapshot);
	}
	if (p_snapshot.has("disabled") && p_snapshot["disabled"].get_type() == Variant::BOOL && (bool)p_snapshot["disabled"]) {
		return false;
	}
	return _snapshot_has_screen_position(p_snapshot);
}

bool CLIAIInputServer::_is_clickable_3d_snapshot(const Dictionary &p_snapshot) const {
	if (!_snapshot_has_screen_position(p_snapshot)) {
		return false;
	}
	if (!p_snapshot.has("nodePath") || String(p_snapshot["nodePath"]).is_empty()) {
		return false;
	}
	if (p_snapshot.has("name") && String(p_snapshot["name"]).begins_with("@")) {
		return false;
	}

	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return false;
	}
	Node *node = scene_tree->get_root()->get_node_or_null(NodePath(String(p_snapshot["nodePath"])));
	CollisionObject3D *collision = Object::cast_to<CollisionObject3D>(node);
	if (!collision || !collision->can_process() || !collision->is_ray_pickable()) {
		return false;
	}
	return true;
}

Camera3D *CLIAIInputServer::_get_effective_camera(Viewport *p_viewport) {
	if (!p_viewport) {
		return nullptr;
	}
	return p_viewport->is_camera_3d_override_enabled() ? p_viewport->get_overridden_camera_3d() : p_viewport->get_camera_3d();
}

bool CLIAIInputServer::_get_viewport_to_root_transform(Viewport *p_viewport, Viewport *p_root_viewport, Transform2D &r_transform) const {
	if (!p_viewport || !p_root_viewport) {
		return false;
	}
	r_transform = p_root_viewport->get_screen_transform().affine_inverse() * p_viewport->get_screen_transform();
	return true;
}

bool CLIAIInputServer::_matches_selector_spec(const Dictionary &p_snapshot, const SelectorSpec &p_spec) const {
	if (p_spec.is_empty()) {
		return true;
	}
	if (!p_spec.path.is_empty() && (!p_snapshot.has("nodePath") || String(p_snapshot["nodePath"]) != p_spec.path)) {
		return false;
	}
	if (!p_spec.name.is_empty() && (!p_snapshot.has("name") || String(p_snapshot["name"]) != p_spec.name)) {
		return false;
	}
	if (!p_spec.type.is_empty() && (!p_snapshot.has("type") || String(p_snapshot["type"]) != p_spec.type)) {
		return false;
	}
	if (!p_spec.text.is_empty() && (!p_snapshot.has("text") || p_snapshot["text"].get_type() == Variant::NIL || String(p_snapshot["text"]) != p_spec.text)) {
		return false;
	}
	if (!p_spec.group.is_empty()) {
		if (!p_snapshot.has("groups") || p_snapshot["groups"].get_type() != Variant::ARRAY) {
			return false;
		}
		const Array groups = p_snapshot["groups"];
		bool found = false;
		for (int i = 0; i < groups.size(); i++) {
			if (String(groups[i]) == p_spec.group) {
				found = true;
				break;
			}
		}
		if (!found) {
			return false;
		}
	}
	if (p_spec.has_is_3d && (!p_snapshot.has("is3D") || (bool)p_snapshot["is3D"] != p_spec.is_3d)) {
		return false;
	}
	if (p_spec.has_visible && (!p_snapshot.has("visible") || (bool)p_snapshot["visible"] != p_spec.visible)) {
		return false;
	}
	if (p_spec.has_screen_rect) {
		if (!p_snapshot.has("screenRect") || p_snapshot["screenRect"].get_type() != Variant::DICTIONARY) {
			return false;
		}
		Rect2 snapshot_rect;
		String error;
		if (!_dictionary_to_rect2(p_snapshot["screenRect"], snapshot_rect, error)) {
			return false;
		}
		if (!p_spec.screen_rect.intersects(snapshot_rect)) {
			return false;
		}
	}
	return true;
}

Dictionary CLIAIInputServer::_build_target_snapshot(Node *p_node, Viewport *p_root_viewport) const {
	Dictionary snapshot;
	snapshot["nodePath"] = p_node ? String(p_node->get_path()) : String();
	snapshot["name"] = p_node ? String(p_node->get_name()) : String();
	snapshot["type"] = p_node ? String(p_node->get_class()) : String();
	snapshot["groups"] = Array();
	snapshot["visible"] = true;
	snapshot["disabled"] = Variant();
	snapshot["is3D"] = false;
	snapshot["screenRect"] = Variant();
	snapshot["screenPoint"] = Variant();
	snapshot["hasScreenPosition"] = false;
	snapshot["viewportPath"] = String();
	snapshot["worldPosition"] = Variant();
	snapshot["text"] = Variant();
	snapshot["value"] = Variant();
	snapshot["selected"] = Variant();
	snapshot["childCount"] = p_node ? p_node->get_child_count() : 0;

	if (!p_node) {
		return snapshot;
	}

	Viewport *node_viewport = p_node->get_viewport();
	snapshot["viewportPath"] = node_viewport ? String(node_viewport->get_path()) : String();

	Array groups;
	List<Node::GroupInfo> group_list;
	p_node->get_groups(&group_list);
	for (const Node::GroupInfo &group_info : group_list) {
		const String group_name = String(group_info.name);
		if (!group_name.begins_with("_")) {
			groups.push_back(group_name);
		}
	}
	snapshot["groups"] = groups;

	bool property_valid = false;
	Variant value = _get_object_property_or_nil(p_node, "disabled", property_valid);
	if (property_valid) {
		snapshot["disabled"] = value;
	}

	value = _get_object_property_or_nil(p_node, "text", property_valid);
	if (property_valid) {
		snapshot["text"] = value;
	}

	value = _get_object_property_or_nil(p_node, "value", property_valid);
	if (property_valid) {
		snapshot["value"] = value;
	}

	value = _get_object_property_or_nil(p_node, "selected", property_valid);
	if (property_valid) {
		snapshot["selected"] = value;
	} else {
		value = _get_object_property_or_nil(p_node, "button_pressed", property_valid);
		if (property_valid) {
			snapshot["selected"] = value;
		}
	}

	const Transform2D screen_to_root = p_root_viewport ? p_root_viewport->get_screen_transform().affine_inverse() : Transform2D();
	if (Window *window = Object::cast_to<Window>(p_node)) {
		snapshot["visible"] = window->is_visible();
		const Rect2 screen_rect(window->get_screen_transform().get_origin(), window->get_size());
		snapshot["screenRect"] = _rect2_to_dictionary(_transform_rect(screen_to_root, screen_rect));
		snapshot["hasScreenPosition"] = true;
	}

	if (Control *control = Object::cast_to<Control>(p_node)) {
		snapshot["visible"] = control->is_visible_in_tree();
		snapshot["screenRect"] = _rect2_to_dictionary(_transform_rect(screen_to_root, control->get_screen_rect()));
		snapshot["hasScreenPosition"] = true;
	}

	if (CanvasItem *canvas_item = Object::cast_to<CanvasItem>(p_node)) {
		snapshot["visible"] = canvas_item->is_visible_in_tree();
	}

	if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
		snapshot["is3D"] = true;
		snapshot["visible"] = node_3d->is_visible_in_tree();
		snapshot["worldPosition"] = _vector3_to_array(node_3d->get_global_position());

		Transform2D viewport_to_root;
		Camera3D *camera = _get_effective_camera(node_viewport);
		if (camera && _get_viewport_to_root_transform(node_viewport, p_root_viewport, viewport_to_root) && !camera->is_position_behind(node_3d->get_global_position())) {
			const Vector2 viewport_point = camera->unproject_position(node_3d->get_global_position());
			const Vector2 root_point = viewport_to_root.xform(viewport_point);
			const Rect2 root_visible_rect = p_root_viewport ? p_root_viewport->get_visible_rect() : Rect2();

			if (VisualInstance3D *visual_instance = Object::cast_to<VisualInstance3D>(p_node)) {
				const AABB aabb = visual_instance->get_aabb();
				Vector<Vector2> projected;
				projected.reserve(8);
				for (int i = 0; i < 8; i++) {
					const Vector3 world_corner = visual_instance->get_global_transform().xform(aabb.get_endpoint(i));
					if (!camera->is_position_behind(world_corner)) {
						projected.push_back(camera->unproject_position(world_corner));
					}
				}
				if (!projected.is_empty()) {
					Rect2 rect(projected[0], Vector2());
					for (int i = 1; i < projected.size(); i++) {
						rect.expand_to(projected[i]);
					}
					rect.size = rect.size.abs();
					if (rect.size == Vector2()) {
						rect = Rect2(viewport_point - Vector2(2, 2), Vector2(4, 4));
					}
					const Rect2 root_rect = _transform_rect(viewport_to_root, rect);
					if (root_visible_rect.intersects(root_rect, true)) {
						const Rect2 clipped_rect = root_visible_rect.intersection(root_rect);
						snapshot["screenPoint"] = _vector2_to_array(root_point);
						snapshot["screenRect"] = _rect2_to_dictionary(clipped_rect.has_area() ? clipped_rect : root_rect);
						snapshot["hasScreenPosition"] = true;
					}
				} else {
					const Rect2 point_rect(root_point - Vector2(2, 2), Vector2(4, 4));
					if (root_visible_rect.intersects(point_rect, true)) {
						snapshot["screenPoint"] = _vector2_to_array(root_point);
						snapshot["screenRect"] = _make_point_rect(root_point);
						snapshot["hasScreenPosition"] = true;
					}
				}
			} else if (root_visible_rect.has_point(root_point)) {
				snapshot["screenPoint"] = _vector2_to_array(root_point);
				snapshot["screenRect"] = _make_point_rect(root_point);
				snapshot["hasScreenPosition"] = true;
			}
		}
	}

	return snapshot;
}

Node *CLIAIInputServer::_resolve_content_scene_node(SceneTree *p_scene_tree) const {
	if (!p_scene_tree) {
		return nullptr;
	}
	Node *current_scene = p_scene_tree->get_current_scene();
	if (!current_scene) {
		return nullptr;
	}
	Node *best = nullptr;
	int best_depth = -1;

	List<Node *> stack;
	stack.push_back(current_scene);
	while (!stack.is_empty()) {
		Node *node = stack.back()->get();
		stack.pop_back();
		if (!node) {
			continue;
		}

		const bool is_scene_instance = !node->get_scene_file_path().is_empty();
		if (node != current_scene && is_scene_instance) {
			int depth = 0;
			for (Node *cursor = node; cursor && cursor != current_scene; cursor = cursor->get_parent()) {
				depth++;
			}
			if (depth > best_depth) {
				best = node;
				best_depth = depth;
			}
		}

		for (int i = node->get_child_count() - 1; i >= 0; i--) {
			stack.push_back(node->get_child(i));
		}
	}

	return best;
}

void CLIAIInputServer::_resolve_scene_paths(SceneTree *p_scene_tree, String &r_root_scene_path, String &r_content_scene_path, ObjectID *r_root_scene_id, ObjectID *r_content_scene_id) const {
	r_root_scene_path = String();
	r_content_scene_path = String();
	if (r_root_scene_id) {
		*r_root_scene_id = ObjectID();
	}
	if (r_content_scene_id) {
		*r_content_scene_id = ObjectID();
	}
	if (!p_scene_tree) {
		return;
	}

	Node *current_scene = p_scene_tree->get_current_scene();
	if (current_scene) {
		r_root_scene_path = String(current_scene->get_path());
		if (r_root_scene_id) {
			*r_root_scene_id = current_scene->get_instance_id();
		}
	}
	Node *content_scene = _resolve_content_scene_node(p_scene_tree);
	if (content_scene) {
		r_content_scene_path = String(content_scene->get_path());
		if (r_content_scene_id) {
			*r_content_scene_id = content_scene->get_instance_id();
		}
	}
}

void CLIAIInputServer::_append_observation_entries(Node *p_node, Viewport *p_root_viewport) const {
	if (!p_node) {
		return;
	}

	const Dictionary snapshot = _build_target_snapshot(p_node, p_root_viewport);
	ObservationEntry entry;
	entry.snapshot = snapshot;
	entry.interactable = _is_interactable_snapshot(snapshot);
	observation_cache.entries.push_back(entry);
	observation_cache.path_to_index[String(snapshot["nodePath"])] = observation_cache.entries.size() - 1;

	const bool is_3d = snapshot.has("is3D") && (bool)snapshot["is3D"];
	if (is_3d) {
		observation_cache.count_3d++;
		if (entry.interactable) {
			observation_cache.interactable_count_3d++;
		}
	} else if (Object::cast_to<Control>(p_node)) {
		observation_cache.ui_count++;
		if (entry.interactable) {
			observation_cache.ui_interactable_count++;
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_append_observation_entries(p_node->get_child(i), p_root_viewport);
	}
}

bool CLIAIInputServer::_ensure_observation_frame_cache() const {
	const uint64_t frame = _current_frame();
	if (observation_cache.populated && observation_cache.frame == frame) {
		return true;
	}
	_rebuild_observation_frame_cache();
	return observation_cache.populated && observation_cache.frame == frame;
}

void CLIAIInputServer::_rebuild_observation_frame_cache() const {
	observation_cache = ObservationFrameCache();
	observation_cache.frame = _current_frame();
	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return;
	}
	observation_cache.populated = true;
#ifdef TESTS_ENABLED
	test_observation_cache_rebuilds++;
#endif
	_append_observation_entries(scene_tree->get_root(), scene_tree->get_root());
}

bool CLIAIInputServer::_build_snapshot_for_node(Node *p_node, Dictionary &r_snapshot) const {
	r_snapshot.clear();
	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root() || !p_node) {
		return false;
	}
	r_snapshot = _build_target_snapshot(p_node, scene_tree->get_root());
	return !r_snapshot.is_empty();
}

bool CLIAIInputServer::_get_snapshot_for_selector_path(const SelectorSpec &p_selector, Dictionary &r_snapshot) const {
	r_snapshot.clear();
	if (p_selector.path.is_empty()) {
		return false;
	}
	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return false;
	}
	Node *node = scene_tree->get_root()->get_node_or_null(NodePath(p_selector.path));
	return _build_snapshot_for_node(node, r_snapshot);
}

CLIAIInputServer::TargetResolution CLIAIInputServer::_resolve_target(const SelectorSpec &p_selector, bool p_require_screen_position) const {
	TargetResolution resolution;

	Array matches;
	if (!p_selector.path.is_empty()) {
		Dictionary snapshot;
		if (_get_snapshot_for_selector_path(p_selector, snapshot) && _matches_selector_spec(snapshot, p_selector)) {
			matches.push_back(snapshot);
		}
	} else {
		if (!_ensure_observation_frame_cache()) {
			return resolution;
		}
		for (int i = 0; i < observation_cache.entries.size(); i++) {
			const Dictionary &snapshot = observation_cache.entries[i].snapshot;
			if (_matches_selector_spec(snapshot, p_selector)) {
				matches.push_back(snapshot);
			}
		}
	}

	if (matches.is_empty()) {
		return resolution;
	}

	if (p_selector.has_nearest_to_screen_point) {
		int best_index = -1;
		double best_distance = 0.0;
		for (int i = 0; i < matches.size(); i++) {
			const Dictionary candidate = matches[i];
			if (!_snapshot_has_screen_position(candidate)) {
				continue;
			}
			if (p_require_screen_position && candidate.has("is3D") && (bool)candidate["is3D"] && !_is_clickable_3d_snapshot(candidate)) {
				continue;
			}
			const double distance = _resolve_target_screen_position(candidate).distance_squared_to(p_selector.nearest_to_screen_point);
			if (best_index < 0 || distance < best_distance) {
				best_index = i;
				best_distance = distance;
			}
		}
		if (best_index < 0) {
			return resolution;
		}
		resolution.ok = true;
		resolution.snapshot = matches[best_index];
		resolution.screen_position = _resolve_target_screen_position(resolution.snapshot);
		return resolution;
	}

	if (matches.size() > 1) {
		resolution.ambiguous = true;
		resolution.candidates = matches;
		return resolution;
	}

	resolution.ok = true;
	resolution.snapshot = matches[0];
	if (p_require_screen_position && !_snapshot_has_screen_position(resolution.snapshot)) {
		resolution.ok = true;
		return resolution;
	}
	resolution.screen_position = _resolve_target_screen_position(resolution.snapshot);
	return resolution;
}

Dictionary CLIAIInputServer::_build_viewport_summary(bool p_include_counts) const {
	Dictionary summary;
	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return summary;
	}

	Viewport *root = scene_tree->get_root();
	Camera3D *camera = _get_effective_camera(root);
	String root_scene_path;
	String content_scene_path;
	_resolve_scene_paths(scene_tree, root_scene_path, content_scene_path);
	summary["windowSize"] = _vector2_to_array(root->get_visible_rect().size);
	summary["rootPath"] = String(root->get_path());
	summary["rootScene"] = root_scene_path;
	summary["contentScene"] = !content_scene_path.is_empty() ? content_scene_path : root_scene_path;
	summary["activeCamera"] = camera ? String(camera->get_path()) : String();

	if (p_include_counts && _ensure_observation_frame_cache()) {
		summary["uiTargets"] = observation_cache.ui_count;
		summary["interactableUiTargets"] = observation_cache.ui_interactable_count;
		summary["targets3D"] = observation_cache.count_3d;
		summary["interactableTargets3D"] = observation_cache.interactable_count_3d;
	}

	return summary;
}

Dictionary CLIAIInputServer::_build_scene_digest() const {
	return _build_viewport_summary(true);
}

void CLIAIInputServer::_process_line(const String &p_line) {
	CustomFeatureTracer *tracer = CustomFeatureTracer::get_singleton();
	JSON json;
	const Error err = json.parse(p_line);
	if (err != OK) {
		const String correlation_id = tracer ? tracer->next_correlation_id(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL) : String();
		const Dictionary response = _make_error_response(Variant(), false, "invalid_json", json.get_error_message());
		_record_last_error_bundle(response);
		if (tracer) {
			Dictionary data = _make_trace_response_data(response);
			Dictionary payloads;
			tracer->add_text_payload(payloads, "requestLine", "text/plain", p_line);
			tracer->add_json_payload(payloads, "response", response);
			tracer->record_error_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "request_invalid_json", correlation_id, "invalid_json", json.get_error_message(), data, payloads);
		}
		_send_response(response);
		return;
	}

	const Variant parsed = json.get_data();
	if (parsed.get_type() != Variant::DICTIONARY) {
		const String correlation_id = tracer ? tracer->next_correlation_id(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL) : String();
		const Dictionary response = _make_error_response(Variant(), false, "invalid_json", "Request must be a JSON object.");
		_record_last_error_bundle(response);
		if (tracer) {
			Dictionary data = _make_trace_response_data(response);
			Dictionary payloads;
			tracer->add_text_payload(payloads, "requestLine", "text/plain", p_line);
			tracer->add_json_payload(payloads, "response", response);
			tracer->record_error_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "request_invalid_json", correlation_id, "invalid_json", "Request must be a JSON object.", data, payloads);
		}
		_send_response(response);
		return;
	}

	const Dictionary request = parsed;
	const String correlation_id = _trace_correlation_from_request(tracer, request);
	CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, correlation_id);
	if (tracer) {
		Dictionary payloads;
		tracer->add_json_payload(payloads, "request", request);
		tracer->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "request_received", "info", correlation_id, _make_trace_request_data(request), payloads);
	}
	bool deferred = false;
	const Dictionary response = _handle_request(request, false, deferred);
	if (!deferred) {
		_record_last_error_bundle(response);
		if (tracer) {
			Dictionary payloads;
			tracer->add_json_payload(payloads, "response", response);
			const Dictionary error = response.has("error") && response["error"].get_type() == Variant::DICTIONARY ? (Dictionary)response["error"] : Dictionary();
			if (!error.is_empty()) {
				tracer->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "response_sent", error.get("code", String()) == "timeout" ? "warning" : "error", correlation_id, _make_trace_response_data(response), payloads, error);
			} else {
				tracer->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "response_sent", "info", correlation_id, _make_trace_response_data(response), payloads);
			}
		}
		_send_response(response);
	}
}

bool CLIAIInputServer::_get_root_image(Ref<Image> &r_image) const {
	r_image.unref();
	const uint64_t frame = _current_frame();
	if (root_image_cache_populated && root_image_cache_frame == frame) {
		if (!root_image_cache_available) {
			return false;
		}
		r_image = root_image_cache;
		return r_image.is_valid() && !r_image->is_empty();
	}

	root_image_cache_frame = frame;
	root_image_cache_populated = true;
	root_image_cache_available = false;
	root_image_cache.unref();
#ifdef TESTS_ENABLED
	test_root_image_fetches++;
#endif

	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return false;
	}
	if (!RenderingServer::get_singleton()) {
		return false;
	}
	const RID viewport_rid = scene_tree->get_root()->get_viewport_rid();
	if (!viewport_rid.is_valid()) {
		return false;
	}
	const RID texture_rid = RenderingServer::get_singleton()->viewport_get_texture(viewport_rid);
	if (!texture_rid.is_valid()) {
		return false;
	}

	root_image_cache = RenderingServer::get_singleton()->texture_2d_get(texture_rid);
	root_image_cache_available = root_image_cache.is_valid() && !root_image_cache->is_empty();
	if (!root_image_cache_available) {
		root_image_cache.unref();
		return false;
	}

	r_image = root_image_cache;
	return true;
}

Error CLIAIInputServer::_capture_screenshot_result(const Dictionary &p_request, Dictionary &r_result, String &r_error_message) const {
	r_result.clear();
	r_error_message = String();

	Ref<Image> image;
	if (!_get_root_image(image)) {
		r_error_message = "Root viewport image is unavailable.";
		return ERR_UNAVAILABLE;
	}

	const String directory = _resolve_screenshot_directory();
	const Error dir_err = DirAccess::make_dir_recursive_absolute(directory);
	if (dir_err != OK) {
		r_error_message = vformat("Unable to create screenshot directory \"%s\" (error code %d).", directory, (int)dir_err);
		return dir_err;
	}

	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	const String name = p_request.has("name") ? String(p_request["name"]) : String();
	const String filename = vformat("godot-ai-agent-%d-%d-%s.png", OS::get_singleton()->get_process_id(), (int)_current_frame(), has_id ? id.stringify().validate_filename() : "request");
	const String path = directory.path_join(filename);
	const Error save_err = image->save_png(path);
	if (save_err != OK) {
		r_error_message = vformat("Unable to save screenshot \"%s\" (error code %d).", path, (int)save_err);
		return save_err;
	}

	r_result["name"] = name;
	r_result["path"] = path;
	r_result["width"] = image->get_width();
	r_result["height"] = image->get_height();
	return OK;
}

double CLIAIInputServer::_compute_screenshot_diff_ratio(const Ref<Image> &p_a, const Ref<Image> &p_b) const {
	if (p_a.is_null() || p_b.is_null()) {
		return 1.0;
	}
	if (p_a->get_width() != p_b->get_width() || p_a->get_height() != p_b->get_height()) {
		return 1.0;
	}

	Ref<Image> image_a = p_a->duplicate();
	Ref<Image> image_b = p_b->duplicate();
	if (image_a.is_null() || image_b.is_null()) {
		return 1.0;
	}
	image_a->convert(Image::FORMAT_RGBA8);
	image_b->convert(Image::FORMAT_RGBA8);

	const int width = image_a->get_width();
	const int height = image_a->get_height();
	if (width <= 0 || height <= 0) {
		return 0.0;
	}

	const int pixel_count = width * height;
	const int step = MAX(1, pixel_count / 4096);
	double total = 0.0;
	int samples = 0;
	for (int i = 0; i < pixel_count; i += step) {
		const int x = i % width;
		const int y = i / width;
		const Color color_a = image_a->get_pixel(x, y);
		const Color color_b = image_b->get_pixel(x, y);
		total += Math::abs(color_a.r - color_b.r);
		total += Math::abs(color_a.g - color_b.g);
		total += Math::abs(color_a.b - color_b.b);
		total += Math::abs(color_a.a - color_b.a);
		samples++;
	}

	if (samples <= 0) {
		return 0.0;
	}
	return total / ((double)samples * 4.0);
}

bool CLIAIInputServer::_normalize_wait_property_expected(const String &p_property_name, const Variant &p_value, Variant &r_value, String &r_error) const {
	r_error = String();
	if (p_property_name == "visible" || p_property_name == "disabled" || p_property_name == "selected") {
		bool value = false;
		if (!_variant_to_bool(p_value, value)) {
			r_error = p_property_name + " expects a boolean equals value.";
			return false;
		}
		r_value = value;
		return true;
	}
	if (p_property_name == "text") {
		String value;
		if (!_variant_to_string(p_value, value)) {
			r_error = "text expects a string equals value.";
			return false;
		}
		r_value = value;
		return true;
	}
	if (p_property_name == "value") {
		double value = 0.0;
		if (!_variant_to_double(p_value, value)) {
			r_error = "value expects a numeric equals value.";
			return false;
		}
		r_value = value;
		return true;
	}

	r_error = "Unsupported wait property: " + p_property_name + ".";
	return false;
}

bool CLIAIInputServer::_wait_property_matches(const Dictionary &p_snapshot, const String &p_property_name, const Variant &p_expected_value, Variant &r_current_value) const {
	r_current_value = Variant();
	if (!p_snapshot.has(p_property_name)) {
		return false;
	}

	const Variant current_value = p_snapshot[p_property_name];
	if (current_value.get_type() == Variant::NIL) {
		return false;
	}

	if (p_property_name == "visible" || p_property_name == "disabled" || p_property_name == "selected") {
		bool current_bool = false;
		if (!_variant_to_bool(current_value, current_bool)) {
			return false;
		}
		r_current_value = current_bool;
		return current_bool == (bool)p_expected_value;
	}
	if (p_property_name == "text") {
		const String current_text = String(current_value);
		r_current_value = current_text;
		return current_text == String(p_expected_value);
	}
	if (p_property_name == "value") {
		double current_number = 0.0;
		if (!_variant_to_double(current_value, current_number)) {
			return false;
		}
		r_current_value = current_number;
		return Math::is_equal_approx(current_number, (double)p_expected_value);
	}
	return false;
}

bool CLIAIInputServer::_extract_wait_request(const Dictionary &p_request, const String &p_condition_kind, const Variant &p_id, bool p_has_id, bool p_from_batch, PendingWait &r_wait, String &r_error_code, String &r_error_message) const {
	r_error_code = String();
	r_error_message = String();
	r_wait = PendingWait();
	r_wait.id = p_id;
	r_wait.has_id = p_has_id;
	r_wait.trace_correlation_id = p_from_batch ? active_batch.trace_correlation_id : CustomFeatureTracer::get_current_correlation_id();
	r_wait.from_batch = p_from_batch;
	r_wait.command = p_request.has("cmd") ? String(p_request["cmd"]) : p_condition_kind;
	r_wait.spec.condition_kind = p_condition_kind;
	r_wait.request = p_request;
	r_wait.started_frame = _current_frame();
	r_wait.capture_on_error = _should_capture_on_error(p_request);

	int timeout_frames = options.default_wait_timeout_frames;
	int poll_every_frames = options.default_poll_every_frames;
	if (!_get_int(p_request, "timeoutFrames", timeout_frames, timeout_frames, r_error_message)) {
		r_error_code = "invalid_wait";
		return false;
	}
	if (!_get_int(p_request, "pollEveryFrames", poll_every_frames, poll_every_frames, r_error_message)) {
		r_error_code = "invalid_wait";
		return false;
	}
	if (timeout_frames < 1) {
		r_error_code = "invalid_wait";
		r_error_message = "timeoutFrames must be greater than or equal to 1.";
		return false;
	}
	if (poll_every_frames < 1) {
		r_error_code = "invalid_wait";
		r_error_message = "pollEveryFrames must be greater than or equal to 1.";
		return false;
	}
	r_wait.spec.timeout_frames = timeout_frames;
	r_wait.spec.poll_every_frames = poll_every_frames;
	r_wait.timeout_frame = r_wait.started_frame + (uint64_t)timeout_frames;
	r_wait.next_poll_frame = r_wait.started_frame;

	if (p_condition_kind == "node_exists" || p_condition_kind == "node_gone") {
		SelectorSpec selector;
		if (!_extract_selector_spec(p_request, "selector", "selector", selector, r_error_message) || selector.is_empty()) {
			r_error_code = "invalid_wait";
			if (r_error_message.is_empty()) {
				r_error_message = "selector must not be empty.";
			}
			return false;
		}
		r_wait.spec.selector = selector;
		return true;
	}

	if (p_condition_kind == "property") {
		SelectorSpec selector;
		if (!_extract_selector_spec(p_request, "selector", "selector", selector, r_error_message) || selector.is_empty()) {
			r_error_code = "invalid_wait";
			if (r_error_message.is_empty()) {
				r_error_message = "selector must not be empty.";
			}
			return false;
		}
		String property_name;
		if (!_get_string(p_request, "property", String(), property_name, r_error_message) || property_name.is_empty()) {
			r_error_code = "invalid_wait";
			if (r_error_message.is_empty()) {
				r_error_message = "property must be a non-empty string.";
			}
			return false;
		}
		static const char *allowed_properties[] = { "visible", "disabled", "text", "value", "selected" };
		bool allowed = false;
		for (uint32_t i = 0; i < sizeof(allowed_properties) / sizeof(allowed_properties[0]); i++) {
			if (property_name == allowed_properties[i]) {
				allowed = true;
				break;
			}
		}
		if (!allowed) {
			r_error_code = "invalid_wait";
			r_error_message = "wait_property.property must be one of visible, disabled, text, value, selected.";
			return false;
		}
		if (!p_request.has("equals")) {
			r_error_code = "invalid_wait";
			r_error_message = "wait_property requires an equals field.";
			return false;
		}
		Variant normalized_value;
		if (!_normalize_wait_property_expected(property_name, p_request["equals"], normalized_value, r_error_message)) {
			r_error_code = "invalid_wait";
			return false;
		}
		r_wait.spec.selector = selector;
		r_wait.spec.property_name = property_name;
		r_wait.spec.expected_value = normalized_value;
		return true;
	}

	if (p_condition_kind == "scene_changed") {
		SceneTree *scene_tree = _get_runtime_scene_tree();
		String root_scene_path;
		String content_scene_path;
		ObjectID root_scene_id;
		ObjectID content_scene_id;
		_resolve_scene_paths(scene_tree, root_scene_path, content_scene_path, &root_scene_id, &content_scene_id);
		r_wait.spec.baseline_scene_path = root_scene_path;
		r_wait.spec.baseline_scene_id = root_scene_id;
		r_wait.spec.baseline_content_scene_path = !content_scene_path.is_empty() ? content_scene_path : root_scene_path;
		r_wait.spec.baseline_content_scene_id = content_scene_id != ObjectID() ? content_scene_id : root_scene_id;
		r_wait.spec.baseline_target_scene_path = p_request.has("fromScenePath") ? String(p_request["fromScenePath"]) : String();
		if (r_wait.spec.baseline_target_scene_path.is_empty()) {
			r_wait.spec.baseline_target_scene_path = r_wait.spec.baseline_content_scene_path;
			r_wait.spec.baseline_target_scene_id = r_wait.spec.baseline_content_scene_id;
			r_wait.spec.baseline_target_existed = r_wait.spec.baseline_target_scene_id != ObjectID();
			return true;
		}

		Node *baseline_target = nullptr;
		if (scene_tree && scene_tree->get_root()) {
			baseline_target = scene_tree->get_root()->get_node_or_null(NodePath(r_wait.spec.baseline_target_scene_path));
		}
		if (!baseline_target) {
			r_error_code = "invalid_wait";
			r_error_message = "wait_scene_changed.fromScenePath must resolve to an existing node when the wait is armed.";
			return false;
		}
		r_wait.spec.baseline_target_scene_id = baseline_target->get_instance_id();
		r_wait.spec.baseline_target_existed = true;
		return true;
	}

	if (p_condition_kind == "screenshot_diff") {
		double threshold = 0.05;
		if (!_get_double(p_request, "threshold", threshold, threshold, r_error_message)) {
			r_error_code = "invalid_wait";
			return false;
		}
		if (threshold < 0.0 || threshold > 1.0) {
			r_error_code = "invalid_wait";
			r_error_message = "threshold must be between 0.0 and 1.0.";
			return false;
		}
		Ref<Image> baseline_image;
		if (!_get_root_image(baseline_image)) {
			r_error_code = "screenshot_unavailable";
			r_error_message = "Root viewport image is unavailable.";
			return false;
		}
		r_wait.spec.baseline_image = baseline_image;
		r_wait.spec.screenshot_diff_threshold = threshold;
		return true;
	}

	r_error_code = "invalid_wait";
	r_error_message = "Unsupported wait condition: " + p_condition_kind + ".";
	return false;
}

bool CLIAIInputServer::_extract_wait_until_request(const Dictionary &p_request, const Variant &p_id, bool p_has_id, bool p_from_batch, PendingWait &r_wait, String &r_error_code, String &r_error_message) const {
	if (!p_request.has("condition") || p_request["condition"].get_type() != Variant::DICTIONARY) {
		r_error_code = "invalid_wait";
		r_error_message = "wait_until requires a condition object.";
		return false;
	}

	Dictionary merged = p_request.duplicate(true);
	Dictionary condition = p_request["condition"];
	if (!condition.has("kind") || condition["kind"].get_type() != Variant::STRING) {
		r_error_code = "invalid_wait";
		r_error_message = "wait_until.condition.kind must be a string.";
		return false;
	}
	const String kind = condition["kind"];
	condition.erase("kind");
	Array keys = condition.keys();
	for (int i = 0; i < keys.size(); i++) {
		merged[keys[i]] = condition[keys[i]];
	}
	merged["cmd"] = "wait_until";
	return _extract_wait_request(merged, kind, p_id, p_has_id, p_from_batch, r_wait, r_error_code, r_error_message);
}

bool CLIAIInputServer::_evaluate_wait_condition(PendingWait &r_wait, bool &r_finished, Dictionary &r_result, String &r_error_code, String &r_error_message) {
	r_finished = false;
	r_result.clear();
	r_error_code = String();
	r_error_message = String();

	const uint64_t frame = _current_frame();
	if (frame < r_wait.next_poll_frame) {
		return true;
	}

	r_result["condition"] = r_wait.spec.condition_kind;
	r_result["startedFrame"] = (int64_t)r_wait.started_frame;
	r_result["endedFrame"] = (int64_t)frame;

	if (r_wait.spec.condition_kind == "frames") {
		if ((int)(frame - r_wait.started_frame) >= r_wait.spec.frames) {
			r_result["frames"] = r_wait.spec.frames;
			r_finished = true;
		}
		return true;
	}

	if (r_wait.spec.condition_kind == "node_exists") {
		const TargetResolution resolution = _resolve_target(r_wait.spec.selector, false);
		if (resolution.ambiguous) {
			r_error_code = "ambiguous_target";
			r_error_message = "selector resolved to multiple targets.";
			return false;
		}
		if (resolution.ok) {
			r_wait.last_resolved_target = resolution.snapshot;
			r_result["node"] = resolution.snapshot;
			r_finished = true;
		}
		return true;
	}

	if (r_wait.spec.condition_kind == "node_gone") {
		const TargetResolution resolution = _resolve_target(r_wait.spec.selector, false);
		if (!resolution.ok && !resolution.ambiguous) {
			r_result["selector"] = r_wait.request["selector"];
			r_finished = true;
		}
		return true;
	}

	if (r_wait.spec.condition_kind == "property") {
		const TargetResolution resolution = _resolve_target(r_wait.spec.selector, false);
		if (resolution.ambiguous) {
			r_error_code = "ambiguous_target";
			r_error_message = "selector resolved to multiple targets.";
			return false;
		}
		if (!resolution.ok) {
			return true;
		}

		r_wait.last_resolved_target = resolution.snapshot;
		const Dictionary snapshot = resolution.snapshot;
		Variant current_value;
		if (_wait_property_matches(snapshot, r_wait.spec.property_name, r_wait.spec.expected_value, current_value)) {
			r_result["node"] = snapshot;
			r_result["property"] = r_wait.spec.property_name;
			r_result["value"] = current_value;
			r_finished = true;
		}
		return true;
	}

	if (r_wait.spec.condition_kind == "scene_changed") {
		SceneTree *scene_tree = _get_runtime_scene_tree();
		String current_root_scene_path;
		String current_content_scene_path;
		ObjectID current_root_scene_id;
		ObjectID current_content_scene_id;
		_resolve_scene_paths(scene_tree, current_root_scene_path, current_content_scene_path, &current_root_scene_id, &current_content_scene_id);
		const String current_effective_scene_path = !current_content_scene_path.is_empty() ? current_content_scene_path : current_root_scene_path;
		const String baseline_effective_scene_path = !r_wait.spec.baseline_target_scene_path.is_empty() ? r_wait.spec.baseline_target_scene_path :
				(!r_wait.spec.baseline_content_scene_path.is_empty() ? r_wait.spec.baseline_content_scene_path : r_wait.spec.baseline_scene_path);
		bool baseline_target_exists = false;
		ObjectID current_target_scene_id;
		if (scene_tree && scene_tree->get_root() && !baseline_effective_scene_path.is_empty()) {
			if (Node *target_node = scene_tree->get_root()->get_node_or_null(NodePath(baseline_effective_scene_path))) {
				baseline_target_exists = true;
				current_target_scene_id = target_node->get_instance_id();
			}
		}
		const ObjectID current_effective_scene_id = current_content_scene_id != ObjectID() ? current_content_scene_id : current_root_scene_id;
		const bool target_disappeared = r_wait.spec.baseline_target_existed && !baseline_target_exists;
		const bool target_replaced = r_wait.spec.baseline_target_scene_id != ObjectID() && current_target_scene_id != ObjectID() && current_target_scene_id != r_wait.spec.baseline_target_scene_id;
		const bool content_path_changed = !r_wait.spec.baseline_content_scene_path.is_empty() && current_content_scene_path != r_wait.spec.baseline_content_scene_path;
		const bool content_instance_changed = r_wait.spec.baseline_content_scene_id != ObjectID() && current_effective_scene_id != ObjectID() && current_effective_scene_id != r_wait.spec.baseline_content_scene_id;
		const bool root_path_changed = r_wait.spec.baseline_content_scene_path.is_empty() && current_root_scene_path != r_wait.spec.baseline_scene_path;
		const bool root_instance_changed = r_wait.spec.baseline_content_scene_path.is_empty() && r_wait.spec.baseline_scene_id != ObjectID() && current_root_scene_id != ObjectID() && current_root_scene_id != r_wait.spec.baseline_scene_id;
		if (target_disappeared || target_replaced || content_path_changed || content_instance_changed || root_path_changed || root_instance_changed) {
			r_result["fromScenePath"] = baseline_effective_scene_path;
			r_result["toScenePath"] = current_effective_scene_path;
			r_result["fromRootScenePath"] = r_wait.spec.baseline_scene_path;
			r_result["toRootScenePath"] = current_root_scene_path;
			r_result["fromContentScenePath"] = r_wait.spec.baseline_content_scene_path;
			r_result["toContentScenePath"] = current_content_scene_path;
			r_finished = true;
		}
		return true;
	}

	if (r_wait.spec.condition_kind == "screenshot_diff") {
		Ref<Image> current_image;
		if (!_get_root_image(current_image)) {
			r_error_code = "screenshot_unavailable";
			r_error_message = "Root viewport image is unavailable.";
			return false;
		}
		const double diff_ratio = _compute_screenshot_diff_ratio(r_wait.spec.baseline_image, current_image);
		if (diff_ratio >= r_wait.spec.screenshot_diff_threshold) {
			r_result["threshold"] = r_wait.spec.screenshot_diff_threshold;
			r_result["diffRatio"] = diff_ratio;
			r_finished = true;
		}
		return true;
	}

	r_error_code = "invalid_wait";
	r_error_message = "Unsupported wait condition: " + r_wait.spec.condition_kind + ".";
	return false;
}

void CLIAIInputServer::_queue_wait(const PendingWait &p_wait, bool p_from_batch) {
	PendingWait wait = p_wait;
	if (p_from_batch) {
		wait.from_batch = true;
		wait.batch_step = MAX(0, active_batch.step - 1);
		active_batch.waiting = true;
	}
	pending_waits.push_back(wait);
	if (CustomFeatureTracer::has_singleton()) {
		Dictionary data;
		data["cmd"] = wait.command;
		data["condition"] = wait.spec.condition_kind;
		data["timeoutFrames"] = wait.spec.timeout_frames;
		data["pollEveryFrames"] = wait.spec.poll_every_frames;
		data["fromBatch"] = wait.from_batch;
		CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "wait_queued", "info", wait.trace_correlation_id, data);
	}
}

void CLIAIInputServer::_complete_batch_wait(const PendingWait &p_wait, const Dictionary &p_result) {
	active_batch.waiting = false;
	if (p_wait.request.has("internal") && p_wait.request["internal"].get_type() == Variant::BOOL && (bool)p_wait.request["internal"]) {
		return;
	}
	Dictionary result;
	result["step"] = p_wait.batch_step;
	result["cmd"] = p_wait.command;
	result["result"] = p_result;
	active_batch.results.push_back(result);
}

void CLIAIInputServer::_fail_wait(const PendingWait &p_wait, const Dictionary &p_error_response) {
	if (CustomFeatureTracer::has_singleton()) {
		Dictionary data = _make_trace_response_data(p_error_response);
		data["cmd"] = p_wait.command;
		data["condition"] = p_wait.spec.condition_kind;
		Dictionary payloads;
		CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "response", p_error_response);
		CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "wait_failed", "warning", p_wait.trace_correlation_id, data, payloads, p_error_response.has("error") && p_error_response["error"].get_type() == Variant::DICTIONARY ? (Dictionary)p_error_response["error"] : Dictionary());
	}
	if (p_wait.from_batch) {
		active_batch.waiting = false;
		_fail_batch_step(p_wait.batch_step, p_error_response);
		return;
	}
	_send_response(p_error_response);
}

void CLIAIInputServer::poll_commands() {
	if (options.listen_tcp && !is_listening()) {
		return;
	}

	int budget = MAX_OPS_PER_FRAME;
	if (options.listen_tcp && !local_execution_owner) {
		_accept_new_clients();
		_poll_client_lines(budget);
	}
	_process_pending_waits();
	_process_batch(budget);
}

void CLIAIInputServer::_process_pending_waits() {
	for (int i = pending_waits.size() - 1; i >= 0; i--) {
		PendingWait wait = pending_waits[i];
		CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, wait.trace_correlation_id);
		const uint64_t frame = _current_frame();
		if (frame < wait.next_poll_frame) {
			continue;
		}

		bool finished = false;
		Dictionary result;
		String error_code;
		String error_message;
		const bool ok = _evaluate_wait_condition(wait, finished, result, error_code, error_message);
		if (!ok) {
			Dictionary error_response = _make_contextual_error_response(wait.request, wait.id, wait.has_id, error_code, error_message, wait.last_resolved_target);
			_record_last_error_bundle(error_response);
			_fail_wait(wait, error_response);
			pending_waits.remove_at(i);
			continue;
		}
		if (finished) {
			if (!wait.last_resolved_target.is_empty()) {
				last_resolved_target = wait.last_resolved_target;
			}
			if (CustomFeatureTracer::has_singleton()) {
				Dictionary data;
				data["cmd"] = wait.command;
				data["condition"] = wait.spec.condition_kind;
				data["fromBatch"] = wait.from_batch;
				Dictionary payloads;
				CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "result", result);
				CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "wait_finished", "info", wait.trace_correlation_id, data, payloads);
			}
			if (wait.from_batch) {
				_complete_batch_wait(wait, result);
			} else {
				Dictionary response = _make_success_response(wait.id, wait.has_id, result);
				if (!active_batch.active) {
					response["sceneDigest"] = _build_scene_digest();
				}
				if (CustomFeatureTracer::has_singleton()) {
					Dictionary payloads;
					CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "response", response);
					CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "response_sent", "info", wait.trace_correlation_id, _make_trace_response_data(response), payloads);
				}
				_send_response(response);
			}
			pending_waits.remove_at(i);
			continue;
		}
		if (frame >= wait.timeout_frame) {
			const String timeout_message = vformat("%s timed out after %d frames.", wait.command, wait.spec.timeout_frames);
			Dictionary error_response = _make_contextual_error_response(wait.request, wait.id, wait.has_id, "timeout", timeout_message, wait.last_resolved_target);
			_record_last_error_bundle(error_response);
			_fail_wait(wait, error_response);
			pending_waits.remove_at(i);
			continue;
		}

		wait.next_poll_frame = frame + (uint64_t)wait.spec.poll_every_frames;
		pending_waits.write[i] = wait;
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

	if (cmd == "click" || cmd == "double_click" || cmd == "hold" || cmd == "drag" ||
			cmd == "click_target" || cmd == "double_click_target" || cmd == "focus_target" || cmd == "hover_target" ||
			cmd == "drag_target_to_target" || cmd == "type_text" || cmd == "scroll_view" ||
			cmd == "click_target_and_wait" || cmd == "type_text_and_wait") {
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
	String error_code;
	String error_message;
	Dictionary result;
	Array expanded;
	if (_prepare_expanded_operation(p_operation, expanded, result, error_code, error_message)) {
		if (!error_code.is_empty()) {
			return _make_contextual_error_response(p_operation, p_operation.has("id") ? p_operation["id"] : Variant(), p_operation.has("id"), error_code, error_message, last_resolved_target);
		}
		result["expandedSteps"] = expanded.size();
		Dictionary response = _make_success_response(p_operation.has("id") ? p_operation["id"] : Variant(), p_operation.has("id"), result);
		if (!p_from_batch) {
			response["sceneDigest"] = _build_scene_digest();
		}
		return response;
	}

	return _execute_immediate_operation(p_operation, p_from_batch, r_deferred);
}

Dictionary CLIAIInputServer::_execute_immediate_operation(const Dictionary &p_operation, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_operation.has("id") ? p_operation["id"] : Variant();
	const bool has_id = p_operation.has("id");
	const String cmd = p_operation["cmd"];

	ERR_FAIL_COND_V_MSG(!_is_supported_immediate_command_name(cmd), _make_error_response(id, has_id, "unknown_command", "Unknown AI agent command: " + cmd + "."), "Unknown AI agent command: " + cmd + ".");

	if (cmd == "batch") {
		Dictionary response;
		if (!_start_batch(p_operation, false, response)) {
			return response;
		}
		r_deferred = true;
		return Dictionary();
	}
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
	if (cmd == "wait_until" || cmd == "expect") {
		return _cmd_wait_until(p_operation, p_from_batch, r_deferred);
	}
	if (cmd == "wait_node_exists") {
		return _cmd_wait_node_exists(p_operation, p_from_batch, r_deferred);
	}
	if (cmd == "wait_node_gone") {
		return _cmd_wait_node_gone(p_operation, p_from_batch, r_deferred);
	}
	if (cmd == "wait_property") {
		return _cmd_wait_property(p_operation, p_from_batch, r_deferred);
	}
	if (cmd == "wait_scene_changed") {
		return _cmd_wait_scene_changed(p_operation, p_from_batch, r_deferred);
	}
	if (cmd == "wait_screenshot_diff") {
		return _cmd_wait_screenshot_diff(p_operation, p_from_batch, r_deferred);
	}
	if (cmd == "get_interactables") {
		return _cmd_get_interactables(p_operation);
	}
	if (cmd == "query_nodes") {
		return _cmd_query_nodes(p_operation);
	}
	if (cmd == "get_node_snapshot") {
		return _cmd_get_node_snapshot(p_operation);
	}
	if (cmd == "get_viewport_summary") {
		return _cmd_get_viewport_summary(p_operation);
	}
	if (cmd == "get_screenshot") {
		return _cmd_get_screenshot(p_operation);
	}
	if (cmd == "get_scene_tree") {
		return _cmd_get_scene_tree(p_operation);
	}
	if (cmd == "focus_target") {
		String selector_error;
		SelectorSpec selector;
		if (!_extract_selector_spec(p_operation, "selector", "selector", selector, selector_error) || selector.is_empty()) {
			return _make_contextual_error_response(p_operation, id, has_id, "invalid_operation", selector_error.is_empty() ? "selector must not be empty." : selector_error, last_resolved_target);
		}
		const TargetResolution resolution = _resolve_target(selector, false);
		if (resolution.ambiguous) {
			return _make_contextual_error_response(p_operation, id, has_id, "ambiguous_target", "selector resolved to multiple targets.", last_resolved_target);
		}
		if (!resolution.ok) {
			return _make_contextual_error_response(p_operation, id, has_id, "target_not_found", "No target matched selector.", last_resolved_target);
		}
		last_resolved_target = resolution.snapshot;
		SceneTree *scene_tree = _get_runtime_scene_tree();
		Node *node = nullptr;
		if (scene_tree && scene_tree->get_root() && resolution.snapshot.has("nodePath")) {
			node = scene_tree->get_root()->get_node_or_null(NodePath(String(resolution.snapshot["nodePath"])));
		}
		if (Control *control = Object::cast_to<Control>(node)) {
			control->grab_focus();
			const bool focused = scene_tree && scene_tree->get_root() && scene_tree->get_root()->gui_get_focus_owner() == control;
			if (!focused) {
				return _make_contextual_error_response(p_operation, id, has_id, "target_not_focusable", "Resolved target did not become the focused control.", resolution.snapshot);
			}
			Dictionary result;
			result["resolvedTarget"] = resolution.snapshot;
			result["focused"] = true;
			return _make_success_response(id, has_id, result);
		}
		if (!_snapshot_has_screen_position(resolution.snapshot)) {
			return _make_contextual_error_response(p_operation, id, has_id, "target_not_clickable", "Resolved target does not expose a usable screen position.", resolution.snapshot);
		}
		const Vector2 position = _resolve_target_screen_position(resolution.snapshot);
		_inject_mouse_motion(position, position - last_mouse_position);
		Dictionary result;
		result["resolvedTarget"] = resolution.snapshot;
		result["position"] = _vector2_to_array(position);
		return _make_success_response(id, has_id, result);
	}
	if (cmd == "type_text_commit") {
		return _cmd_type_text_commit(p_operation);
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
	if (cmd == "batch_status") {
		return _make_success_response(id, has_id, _cmd_batch_status());
	}
	if (cmd == "cancel_batch") {
		return _cmd_cancel_batch(p_operation);
	}
	if (cmd == "list_checkpoints") {
		return _make_success_response(id, has_id, _cmd_list_checkpoints());
	}
	if (cmd == "get_last_error_bundle") {
		return _make_success_response(id, has_id, _cmd_get_last_error_bundle());
	}
	if (cmd == "checkpoint") {
		Dictionary event = _make_response_base(id, has_id, true);
		event["event"] = "checkpoint";
		event["name"] = p_operation.has("name") ? String(p_operation["name"]) : String();
		event["step"] = active_batch.step;
		if (CustomFeatureTracer::has_singleton()) {
			Dictionary payloads;
			CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "response", event);
			CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "batch_checkpoint", "info", active_batch.trace_correlation_id, _make_trace_response_data(event), payloads);
		}
		_send_response(event);
		checkpoint_history.push_back(event);
		active_batch.checkpoints.push_back(event);

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
	active_batch.trace_correlation_id = CustomFeatureTracer::get_current_correlation_id();
	active_batch.ops = ops;
	active_batch.on_error = on_error;
	active_batch.started_frame = _current_frame();
	active_batch.direct_operation = p_direct_operation;
	active_batch.checkpoints.clear();
	if (CustomFeatureTracer::has_singleton()) {
		Dictionary data;
		data["opsCount"] = ops.size();
		data["onError"] = on_error;
		data["directOperation"] = p_direct_operation;
		CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "batch_started", "info", active_batch.trace_correlation_id, data);
	}
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
	result["checkpoints"] = active_batch.checkpoints;

	const bool ok = p_completed && active_batch.errors.is_empty();
	Dictionary response = _make_response_base(active_batch.id, active_batch.has_id, ok);
	response["result"] = result;
	if (active_batch.direct_operation) {
		response["sceneDigest"] = _build_scene_digest();
	}
	if (!ok) {
		Dictionary top_error = _make_error_payload("batch_failed", "Batch did not complete successfully.");
		if (active_batch.direct_operation && !last_error_bundle.is_empty()) {
			if (last_error_bundle.has("error") && last_error_bundle["error"].get_type() == Variant::DICTIONARY) {
				top_error = last_error_bundle["error"];
			}
			if (last_error_bundle.has("lastResolvedTarget")) {
				response["lastResolvedTarget"] = last_error_bundle["lastResolvedTarget"];
			}
			if (last_error_bundle.has("screenshotPath")) {
				response["screenshotPath"] = last_error_bundle["screenshotPath"];
			}
		} else if (active_batch.direct_operation && !active_batch.errors.is_empty() && active_batch.errors[0].get_type() == Variant::DICTIONARY) {
			const Dictionary error_entry = active_batch.errors[0];
			if (error_entry.has("error") && error_entry["error"].get_type() == Variant::DICTIONARY) {
				top_error = error_entry["error"];
			}
		}
		response["error"] = top_error;
	}
	_record_last_error_bundle(response);
	if (CustomFeatureTracer::has_singleton()) {
		Dictionary payloads;
		CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "response", response);
		CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "batch_finished", ok ? "info" : "warning", active_batch.trace_correlation_id, _make_trace_response_data(response), payloads, response.has("error") && response["error"].get_type() == Variant::DICTIONARY ? (Dictionary)response["error"] : Dictionary());
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
	_record_last_error_bundle(p_error_response);
	if (CustomFeatureTracer::has_singleton()) {
		Dictionary data = _make_trace_response_data(p_error_response);
		data["step"] = p_step;
		Dictionary payloads;
		CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "response", p_error_response);
		CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "batch_step_failed", "warning", active_batch.trace_correlation_id, data, payloads, p_error_response.has("error") && p_error_response["error"].get_type() == Variant::DICTIONARY ? (Dictionary)p_error_response["error"] : Dictionary());
	}

	if (active_batch.on_error == "stop") {
		_release_held_inputs();
		_abort_runtime_perf(true);
		_finish_batch(false);
	}
}

void CLIAIInputServer::_cancel_active_batch(const String &p_error_code, const String &p_error_message, bool p_send_response) {
	if (!active_batch.active) {
		return;
	}
	_release_held_inputs();
	_abort_runtime_perf(true);
	active_batch.waiting = false;
	pending_waits.clear();

	if (p_send_response) {
		Dictionary response = _make_error_response(active_batch.id, active_batch.has_id, p_error_code, p_error_message);
		_record_last_error_bundle(response);
		_send_response(response);
	}
	active_batch = ActiveBatch();
}

void CLIAIInputServer::_process_batch(int &r_budget) {
	while (active_batch.active && r_budget > 0) {
		CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, active_batch.trace_correlation_id);
		if (active_batch.waiting) {
			return;
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
			String expansion_error_code;
			String expansion_error_message;
			Array expanded;
			Dictionary expanded_result;
			if (_prepare_expanded_operation(op, expanded, expanded_result, expansion_error_code, expansion_error_message)) {
				if (!expansion_error_code.is_empty()) {
					Dictionary error_response = _make_contextual_error_response(op, active_batch.id, active_batch.has_id, expansion_error_code, expansion_error_message, last_resolved_target);
					_fail_batch_step(active_batch.step, error_response);
					if (!active_batch.active) {
						return;
					}
					active_batch.step++;
					continue;
				}
				Dictionary result;
				result["step"] = active_batch.step;
				result["cmd"] = op["cmd"];
				Dictionary expanded_result_with_meta = expanded_result.duplicate(true);
				expanded_result_with_meta["expandedSteps"] = expanded.size();
				result["result"] = expanded_result_with_meta;
				active_batch.results.push_back(result);
				active_batch.micro_ops = expanded;
				active_batch.step++;
				continue;
			}
			if (!expansion_error_message.is_empty()) {
				Dictionary error_response = _make_contextual_error_response(op, active_batch.id, active_batch.has_id, "invalid_operation", expansion_error_message, last_resolved_target);
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

bool CLIAIInputServer::_prepare_expanded_operation(const Dictionary &p_operation, Array &r_expanded, Dictionary &r_result, String &r_error_code, String &r_error_message) {
	r_expanded.clear();
	r_result.clear();
	r_error_code = String();
	r_error_message = String();

	String expansion_error;
	if (_expand_high_level_operation(p_operation, r_expanded, expansion_error)) {
		return true;
	}
	if (!expansion_error.is_empty()) {
		r_error_code = "invalid_operation";
		r_error_message = expansion_error;
		return true;
	}

	if (!p_operation.has("cmd") || p_operation["cmd"].get_type() != Variant::STRING) {
		return false;
	}

	const String cmd = p_operation["cmd"];
	auto resolve_action_target = [&](const SelectorSpec &selector, const String &not_found_message, bool require_screen = true) -> TargetResolution {
		const TargetResolution resolution = _resolve_target(selector, require_screen);
		if (resolution.ambiguous) {
			r_error_code = "ambiguous_target";
			r_error_message = "selector resolved to multiple targets.";
			return resolution;
		}
		if (!resolution.ok) {
			r_error_code = "target_not_found";
			r_error_message = not_found_message;
			return resolution;
		}
		if (require_screen && !_snapshot_has_screen_position(resolution.snapshot)) {
			r_error_code = "target_not_clickable";
			r_error_message = "Resolved target does not expose a usable screen position.";
			return resolution;
		}
		if (require_screen && resolution.snapshot.has("is3D") && (bool)resolution.snapshot["is3D"] && !_is_clickable_3d_snapshot(resolution.snapshot)) {
			r_error_code = "target_not_clickable";
			r_error_message = "Resolved target is not a clickable 3D target.";
			return resolution;
		}
		last_resolved_target = resolution.snapshot;
		return resolution;
	};

	auto resolve_selector = [&](const String &key, const String &fallback_error) -> SelectorSpec {
		String selector_error;
		SelectorSpec selector;
		if (!_extract_selector_spec(p_operation, key, key, selector, selector_error)) {
			r_error_code = "invalid_operation";
			r_error_message = selector_error;
			return SelectorSpec();
		}
		if (selector.is_empty()) {
			r_error_code = "invalid_operation";
			r_error_message = fallback_error;
			return SelectorSpec();
		}
		return selector;
	};

	auto expand_from_dict = [&](const Dictionary &op) -> bool {
		String nested_error;
		Array expanded;
		if (_expand_high_level_operation(op, expanded, nested_error)) {
			for (int i = 0; i < expanded.size(); i++) {
				r_expanded.push_back(expanded[i]);
			}
			return true;
		}
		r_error_code = "invalid_operation";
		r_error_message = nested_error;
		return false;
	};

	auto append_wait_condition = [&](const Dictionary &wait_request) -> bool {
		if (wait_request.is_empty()) {
			r_error_code = "invalid_operation";
			r_error_message = "wait must be an object.";
			return false;
		}
		Dictionary expect_op = wait_request.duplicate(true);
		if (p_operation.has("captureOnError") && !expect_op.has("captureOnError")) {
			expect_op["captureOnError"] = p_operation["captureOnError"];
		}
		expect_op["cmd"] = "expect";
		r_expanded.push_back(expect_op);
		return true;
	};

	if (cmd == "hover_target" || cmd == "click_target" || cmd == "double_click_target" || cmd == "focus_target" || cmd == "scroll_view" || cmd == "type_text" || cmd == "click_target_and_wait" || cmd == "type_text_and_wait") {
		const SelectorSpec selector = resolve_selector("selector", "selector must not be empty.");
		if (selector.is_empty()) {
			return true;
		}
		const bool focus_like_cmd = cmd == "focus_target";
		const bool type_like_cmd = cmd == "type_text" || cmd == "type_text_and_wait";
		const bool require_screen = !focus_like_cmd;
		const TargetResolution resolution = resolve_action_target(selector, "No target matched selector.", require_screen);
		if (!r_error_code.is_empty()) {
			return true;
		}
		r_result["resolvedTarget"] = resolution.snapshot;
		if (_snapshot_has_screen_position(resolution.snapshot)) {
			const Vector2 position = _resolve_target_screen_position(resolution.snapshot);
			r_result["position"] = _vector2_to_array(position);
		}

		if (cmd == "hover_target") {
			const Vector2 position = _resolve_target_screen_position(resolution.snapshot);
			r_expanded.push_back(_make_mouse_motion_op(position));
			return true;
		}

		if (cmd == "scroll_view") {
			const Vector2 position = _resolve_target_screen_position(resolution.snapshot);
			String direction = "down";
			int steps = 1;
			if (!_get_string(p_operation, "direction", "down", direction, r_error_message) || !_get_int(p_operation, "steps", 1, steps, r_error_message)) {
				r_error_code = "invalid_operation";
				return true;
			}
			if (steps < 1) {
				r_error_code = "invalid_operation";
				r_error_message = "scroll_view.steps must be greater than or equal to 1.";
				return true;
			}
			MouseButton button = MouseButton::WHEEL_DOWN;
			if (direction == "up") {
				button = MouseButton::WHEEL_UP;
			} else if (direction != "down") {
				r_error_code = "invalid_operation";
				r_error_message = "scroll_view.direction must be up or down.";
				return true;
			}
			r_expanded.push_back(_make_mouse_motion_op(position));
			for (int i = 0; i < steps; i++) {
				r_expanded.push_back(_make_mouse_button_op(button, position, true));
				r_expanded.push_back(_make_mouse_button_op(button, position, false));
			}
			r_result["steps"] = steps;
			r_result["direction"] = direction;
			return true;
		}

		if (cmd == "type_text" || cmd == "type_text_and_wait") {
			String text;
			if (!_get_string(p_operation, "text", String(), text, r_error_message)) {
				r_error_code = "invalid_operation";
				return true;
			}
			Dictionary focus_op;
			focus_op["cmd"] = "focus_target";
			focus_op["selector"] = p_operation["selector"];
			r_expanded.push_back(focus_op);
			r_expanded.push_back(_make_type_text_commit_op(text));
			r_result["textLength"] = text.length();

			if (cmd == "type_text_and_wait") {
				if (!p_operation.has("wait") || p_operation["wait"].get_type() != Variant::DICTIONARY) {
					r_error_code = "invalid_operation";
					r_error_message = "type_text_and_wait.wait must be an object.";
					return true;
				}
				if (!append_wait_condition(p_operation["wait"])) {
					return true;
				}
			}
			return true;
		}

		if (cmd == "focus_target") {
			r_expanded.push_back(p_operation);
			return true;
		}

		const Vector2 position = _resolve_target_screen_position(resolution.snapshot);
		String base_cmd = cmd;
		Dictionary high_level;
		high_level["x"] = position.x;
		high_level["y"] = position.y;

		if (base_cmd == "click_target" || base_cmd == "click_target_and_wait") {
			high_level["cmd"] = "click";
			if (p_operation.has("button")) {
				high_level["button"] = p_operation["button"];
			}
			if (p_operation.has("pressFrames")) {
				high_level["pressFrames"] = p_operation["pressFrames"];
			}
		} else if (base_cmd == "double_click_target") {
			high_level["cmd"] = "double_click";
			if (p_operation.has("button")) {
				high_level["button"] = p_operation["button"];
			}
			if (p_operation.has("pressFrames")) {
				high_level["pressFrames"] = p_operation["pressFrames"];
			}
			if (p_operation.has("gapFrames")) {
				high_level["gapFrames"] = p_operation["gapFrames"];
			}
		}

		if (!expand_from_dict(high_level)) {
			return true;
		}

		if (cmd == "click_target_and_wait") {
			if (!p_operation.has("wait") || p_operation["wait"].get_type() != Variant::DICTIONARY) {
				r_error_code = "invalid_operation";
				r_error_message = "click_target_and_wait.wait must be an object.";
				return true;
			}
			if (!append_wait_condition(p_operation["wait"])) {
				return true;
			}
		}
		return true;
	}

	if (cmd == "drag_target_to_target") {
		const SelectorSpec from_selector = resolve_selector("fromSelector", "fromSelector must not be empty.");
		if (from_selector.is_empty()) {
			return true;
		}
		const SelectorSpec to_selector = resolve_selector("toSelector", "toSelector must not be empty.");
		if (to_selector.is_empty()) {
			return true;
		}
		const TargetResolution from_resolution = resolve_action_target(from_selector, "No source target matched selector.");
		if (!r_error_code.is_empty()) {
			return true;
		}
		const TargetResolution to_resolution = resolve_action_target(to_selector, "No destination target matched selector.");
		if (!r_error_code.is_empty()) {
			return true;
		}

		Dictionary drag_op;
		drag_op["cmd"] = "drag";
		drag_op["frames"] = p_operation.has("frames") ? Variant(p_operation["frames"]) : Variant(20);
		if (p_operation.has("button")) {
			drag_op["button"] = p_operation["button"];
		}
		Array from = _vector2_to_array(_resolve_target_screen_position(from_resolution.snapshot));
		Array to = _vector2_to_array(_resolve_target_screen_position(to_resolution.snapshot));
		drag_op["from"] = from;
		drag_op["to"] = to;
		if (!expand_from_dict(drag_op)) {
			return true;
		}
		r_result["resolvedFromTarget"] = from_resolution.snapshot;
		r_result["resolvedToTarget"] = to_resolution.snapshot;
		r_result["from"] = from;
		r_result["to"] = to;
		return true;
	}

	return false;
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
	op["internal"] = true;
	return op;
}

Dictionary CLIAIInputServer::_make_type_text_commit_op(const String &p_text) {
	Dictionary op;
	op["cmd"] = "type_text_commit";
	op["text"] = p_text;
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
	result["localOnly"] = !options.listen_tcp;
	result["port"] = options.port;
	result["batchActive"] = active_batch.active;
	result["runtimePerfActive"] = runtime_perf_recorder != nullptr;
	result["pendingWaits"] = pending_waits.size();
	result["heldActions"] = held_actions.size();
	result["heldKeys"] = held_keys.size();
	result["heldMouseButtons"] = held_mouse_buttons.size();
	result["checkpointCount"] = checkpoint_history.size();
	result["lastError"] = last_error_bundle;
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

	Ref<InputEventAction> event;
	event.instantiate();
	event->set_action(action);
	event->set_pressed(pressed);
	event->set_strength((float)strength);
	event->set_device(InputEvent::DEVICE_ID_EMULATION);
	Ref<InputEvent> base_event = event;
	if (!_dispatch_root_input(base_event, true)) {
		return _make_error_response(id, has_id, "scene_tree_unavailable", "SceneTree root viewport is unavailable.");
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
	Ref<InputEvent> base_event = event;
	if (!_dispatch_root_input(base_event, true)) {
		return _make_error_response(id, has_id, "scene_tree_unavailable", "SceneTree root viewport is unavailable.");
	}

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

	Vector2 local_position = p_position;
	Vector2 global_position = p_position;
	if (!_get_root_window_mouse_coordinates(p_position, local_position, global_position)) {
		return;
	}

	last_mouse_position = local_position;
	Input::get_singleton()->set_mouse_position(local_position);

	Ref<InputEventMouseButton> event;
	event.instantiate();
	event->set_button_index(p_button);
	event->set_pressed(p_pressed);
	event->set_position(local_position);
	event->set_global_position(global_position);
	event->set_button_mask(ai_mouse_button_mask);
	event->set_double_click(p_double_click);
	Ref<InputEvent> base_event = event;
	_dispatch_root_input(base_event, true);
}

void CLIAIInputServer::_inject_mouse_motion(const Vector2 &p_position, const Vector2 &p_relative) {
	Vector2 local_position = p_position;
	Vector2 global_position = p_position;
	if (!_get_root_window_mouse_coordinates(p_position, local_position, global_position)) {
		return;
	}

	last_mouse_position = local_position;
	Input::get_singleton()->set_mouse_position(local_position);

	Ref<InputEventMouseMotion> event;
	event.instantiate();
	event->set_position(local_position);
	event->set_global_position(global_position);
	event->set_relative(p_relative);
	event->set_relative_screen_position(p_relative);
	event->set_velocity(p_relative);
	event->set_screen_velocity(p_relative);
	event->set_button_mask(ai_mouse_button_mask);
	Ref<InputEvent> base_event = event;
	_dispatch_root_input(base_event, true);
}

void CLIAIInputServer::_inject_text_input(const String &p_text) {
	String text_buffer;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t codepoint = p_text.unicode_at(i);
		const bool is_control = codepoint == '\n' || codepoint == '\r' || codepoint == '\t' || codepoint == '\b';
		if (!is_control) {
			text_buffer += String::chr(codepoint);
			continue;
		}

		if (!text_buffer.is_empty()) {
			_dispatch_root_text_input(text_buffer);
			text_buffer = String();
		}

		Key keycode = Key::NONE;
		if (codepoint == '\n' || codepoint == '\r') {
			keycode = Key::ENTER;
		} else if (codepoint == '\t') {
			keycode = Key::TAB;
		} else if (codepoint == '\b') {
			keycode = Key::BACKSPACE;
		}

		Ref<InputEventKey> press_event;
		press_event.instantiate();
		press_event->set_keycode(keycode);
		press_event->set_physical_keycode(keycode);
		press_event->set_unicode(codepoint);
		press_event->set_pressed(true);
		Ref<InputEvent> press_base = press_event;
		_dispatch_root_input(press_base, true);

		Ref<InputEventKey> release_event;
		release_event.instantiate();
		release_event->set_keycode(keycode);
		release_event->set_physical_keycode(keycode);
		release_event->set_unicode(codepoint);
		release_event->set_pressed(false);
		Ref<InputEvent> release_base = release_event;
		_dispatch_root_input(release_base, true);
	}

	if (!text_buffer.is_empty()) {
		_dispatch_root_text_input(text_buffer);
	}
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

	PendingWait wait;
	wait.id = id;
	wait.has_id = has_id;
	wait.command = "wait";
	wait.spec.condition_kind = "frames";
	wait.request = p_request;
	wait.started_frame = _current_frame();
	wait.timeout_frame = wait.started_frame + (uint64_t)frames;
	wait.next_poll_frame = wait.started_frame + 1;
	wait.spec.frames = frames;
	wait.spec.timeout_frames = frames;
	wait.spec.poll_every_frames = 1;
	_queue_wait(wait, p_from_batch);
	r_deferred = true;
	return Dictionary();
}

Dictionary CLIAIInputServer::_cmd_wait_until(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	PendingWait wait;
	String error_code;
	String error_message;
	if (!_extract_wait_until_request(p_request, id, has_id, p_from_batch, wait, error_code, error_message)) {
		return _make_contextual_error_response(p_request, id, has_id, error_code, error_message, last_resolved_target);
	}
	_queue_wait(wait, p_from_batch);
	r_deferred = true;
	return Dictionary();
}

Dictionary CLIAIInputServer::_cmd_wait_node_exists(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	PendingWait wait;
	String error_code;
	String error_message;
	if (!_extract_wait_request(p_request, "node_exists", id, has_id, p_from_batch, wait, error_code, error_message)) {
		return _make_contextual_error_response(p_request, id, has_id, error_code, error_message, last_resolved_target);
	}
	_queue_wait(wait, p_from_batch);
	r_deferred = true;
	return Dictionary();
}

Dictionary CLIAIInputServer::_cmd_wait_node_gone(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	PendingWait wait;
	String error_code;
	String error_message;
	if (!_extract_wait_request(p_request, "node_gone", id, has_id, p_from_batch, wait, error_code, error_message)) {
		return _make_contextual_error_response(p_request, id, has_id, error_code, error_message, last_resolved_target);
	}
	_queue_wait(wait, p_from_batch);
	r_deferred = true;
	return Dictionary();
}

Dictionary CLIAIInputServer::_cmd_wait_property(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	PendingWait wait;
	String error_code;
	String error_message;
	if (!_extract_wait_request(p_request, "property", id, has_id, p_from_batch, wait, error_code, error_message)) {
		return _make_contextual_error_response(p_request, id, has_id, error_code, error_message, last_resolved_target);
	}
	_queue_wait(wait, p_from_batch);
	r_deferred = true;
	return Dictionary();
}

Dictionary CLIAIInputServer::_cmd_wait_scene_changed(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	PendingWait wait;
	String error_code;
	String error_message;
	if (!_extract_wait_request(p_request, "scene_changed", id, has_id, p_from_batch, wait, error_code, error_message)) {
		return _make_contextual_error_response(p_request, id, has_id, error_code, error_message, last_resolved_target);
	}
	_queue_wait(wait, p_from_batch);
	r_deferred = true;
	return Dictionary();
}

Dictionary CLIAIInputServer::_cmd_wait_screenshot_diff(const Dictionary &p_request, bool p_from_batch, bool &r_deferred) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	PendingWait wait;
	String error_code;
	String error_message;
	if (!_extract_wait_request(p_request, "screenshot_diff", id, has_id, p_from_batch, wait, error_code, error_message)) {
		return _make_contextual_error_response(p_request, id, has_id, error_code, error_message, last_resolved_target);
	}
	_queue_wait(wait, p_from_batch);
	r_deferred = true;
	return Dictionary();
}

Dictionary CLIAIInputServer::_cmd_get_interactables(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	SelectorSpec filter;
	String selector_error;
	if (!_extract_selector_spec(p_request, "filter", "filter", filter, selector_error)) {
		return _make_error_response(id, has_id, "invalid_query", selector_error);
	}
	int max_results = 0;
	String limit_error;
	if (!_resolve_query_limit(p_request, max_results, limit_error)) {
		return _make_error_response(id, has_id, "invalid_query", limit_error);
	}

	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return _make_error_response(id, has_id, "scene_tree_unavailable", "SceneTree root is unavailable.");
	}

	Array nodes;
	if (!filter.path.is_empty()) {
		Dictionary snapshot;
		if (_get_snapshot_for_selector_path(filter, snapshot) && _matches_selector_spec(snapshot, filter) && _is_interactable_snapshot(snapshot)) {
			nodes.push_back(snapshot);
		}
	} else if (_ensure_observation_frame_cache()) {
		for (int i = 0; i < observation_cache.entries.size() && nodes.size() < max_results; i++) {
			const ObservationEntry &entry = observation_cache.entries[i];
			if (entry.interactable && _matches_selector_spec(entry.snapshot, filter)) {
				nodes.push_back(entry.snapshot);
			}
		}
	}

	Dictionary result;
	result["maxResults"] = max_results;
	result["count"] = nodes.size();
	result["nodes"] = nodes;
	return _make_success_response(id, has_id, result);
}

Dictionary CLIAIInputServer::_cmd_query_nodes(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	SelectorSpec filter;
	String selector_error;
	if (!_extract_selector_spec(p_request, "filter", "filter", filter, selector_error)) {
		return _make_error_response(id, has_id, "invalid_query", selector_error);
	}
	int max_results = 0;
	String limit_error;
	if (!_resolve_query_limit(p_request, max_results, limit_error)) {
		return _make_error_response(id, has_id, "invalid_query", limit_error);
	}

	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return _make_error_response(id, has_id, "scene_tree_unavailable", "SceneTree root is unavailable.");
	}

	Array nodes;
	if (!filter.path.is_empty()) {
		Dictionary snapshot;
		if (_get_snapshot_for_selector_path(filter, snapshot) && _matches_selector_spec(snapshot, filter)) {
			nodes.push_back(snapshot);
		}
	} else if (_ensure_observation_frame_cache()) {
		for (int i = 0; i < observation_cache.entries.size() && nodes.size() < max_results; i++) {
			const Dictionary &snapshot = observation_cache.entries[i].snapshot;
			if (_matches_selector_spec(snapshot, filter)) {
				nodes.push_back(snapshot);
			}
		}
	}

	Dictionary result;
	result["maxResults"] = max_results;
	result["count"] = nodes.size();
	result["nodes"] = nodes;
	return _make_success_response(id, has_id, result);
}

Dictionary CLIAIInputServer::_cmd_get_node_snapshot(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	String selector_error;
	SelectorSpec selector;
	if (!_extract_selector_spec(p_request, "selector", "selector", selector, selector_error) || selector.is_empty()) {
		return _make_contextual_error_response(p_request, id, has_id, "invalid_query", selector_error.is_empty() ? "selector must not be empty." : selector_error);
	}

	const TargetResolution resolution = _resolve_target(selector, false);
	if (resolution.ambiguous) {
		Dictionary response = _make_contextual_error_response(p_request, id, has_id, "ambiguous_target", "selector resolved to multiple targets.");
		response["candidates"] = resolution.candidates;
		return response;
	}
	if (!resolution.ok) {
		return _make_contextual_error_response(p_request, id, has_id, "target_not_found", "No target matched selector.");
	}

	last_resolved_target = resolution.snapshot;
	Dictionary result;
	result["node"] = resolution.snapshot;
	return _make_success_response(id, has_id, result);
}

Dictionary CLIAIInputServer::_cmd_get_viewport_summary(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	const Dictionary result = _build_viewport_summary(true);
	if (result.is_empty()) {
		return _make_error_response(id, has_id, "scene_tree_unavailable", "SceneTree root viewport is unavailable.");
	}
	return _make_success_response(id, has_id, result);
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
	Dictionary result;
	String error_message;
	const Error err = _capture_screenshot_result(p_request, result, error_message);
	if (err != OK) {
		return _make_error_response(id, has_id, err == ERR_UNAVAILABLE ? "screenshot_unavailable" : "screenshot_failed", error_message);
	}
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

	SceneTree *scene_tree = _get_runtime_scene_tree();
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
	if (CustomFeatureTracer::has_singleton()) {
		Dictionary data;
		data["name"] = perf_options.recording_name;
		data["startFrame"] = perf_options.start_frame;
		data["topFrames"] = top_frames;
		if (perf_options.samples_file_set) {
			data["samplesFile"] = perf_options.samples_file;
		}
		const String trace_correlation_id = active_batch.active ? active_batch.trace_correlation_id : CustomFeatureTracer::get_current_correlation_id();
		CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "runtime_perf_started", "info", trace_correlation_id, data);
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
	pending_perf_stop_trace_correlation_id = CustomFeatureTracer::get_current_correlation_id();
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
		if (CustomFeatureTracer::has_singleton()) {
			Dictionary payloads;
			CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "summary", summary);
			CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "runtime_perf_summary", "info", active_batch.trace_correlation_id, Dictionary(), payloads);
		}
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

Dictionary CLIAIInputServer::_cmd_type_text_commit(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	String text;
	String error;
	if (!_get_string(p_request, "text", String(), text, error)) {
		return _make_error_response(id, has_id, "invalid_text", error);
	}
	SceneTree *scene_tree = _get_runtime_scene_tree();
	if (!scene_tree || !scene_tree->get_root()) {
		return _make_error_response(id, has_id, "scene_tree_unavailable", "SceneTree root viewport is unavailable.");
	}
	_inject_text_input(text);

	Dictionary result;
	result["textLength"] = text.length();
	return _make_success_response(id, has_id, result);
}

Dictionary CLIAIInputServer::_cmd_batch_status() const {
	Dictionary result;
	result["active"] = active_batch.active;
	result["step"] = active_batch.step;
	result["waiting"] = active_batch.waiting;
	result["startedFrame"] = (int64_t)active_batch.started_frame;
	result["resultsCount"] = active_batch.results.size();
	result["errorsCount"] = active_batch.errors.size();
	result["checkpoints"] = active_batch.checkpoints;
	result["recentError"] = last_error_bundle;
	return result;
}

Dictionary CLIAIInputServer::_cmd_cancel_batch(const Dictionary &p_request) {
	const Variant id = p_request.has("id") ? p_request["id"] : Variant();
	const bool has_id = p_request.has("id");
	if (!active_batch.active) {
		return _make_error_response(id, has_id, "batch_not_active", "No batch is currently active.");
	}

	Dictionary batch_result;
	batch_result["startedFrame"] = (int64_t)active_batch.started_frame;
	batch_result["endedFrame"] = (int64_t)_current_frame();
	batch_result["completed"] = false;
	batch_result["failedStep"] = active_batch.step;
	batch_result["results"] = active_batch.results;
	Array errors = active_batch.errors;
	Dictionary cancel_error;
	cancel_error["step"] = active_batch.step;
	cancel_error["error"] = _make_error_payload("batch_cancelled", "Batch was cancelled by cancel_batch.");
	errors.push_back(cancel_error);
	batch_result["errors"] = errors;
	batch_result["checkpoints"] = active_batch.checkpoints;

	Dictionary batch_response = _make_error_response(active_batch.id, active_batch.has_id, "batch_cancelled", "Batch was cancelled by cancel_batch.");
	batch_response["result"] = batch_result;
	_record_last_error_bundle(batch_response);
	if (CustomFeatureTracer::has_singleton()) {
		Dictionary payloads;
		CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "response", batch_response);
		CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "batch_cancelled", "warning", active_batch.trace_correlation_id, _make_trace_response_data(batch_response), payloads, batch_response.has("error") && batch_response["error"].get_type() == Variant::DICTIONARY ? (Dictionary)batch_response["error"] : Dictionary());
	}
	_send_response(batch_response);

	_release_held_inputs();
	_abort_runtime_perf(true);
	for (int i = pending_waits.size() - 1; i >= 0; i--) {
		if (pending_waits[i].from_batch) {
			pending_waits.remove_at(i);
		}
	}
	active_batch = ActiveBatch();

	Dictionary result;
	result["cancelled"] = true;
	return _make_success_response(id, has_id, result);
}

Dictionary CLIAIInputServer::_cmd_list_checkpoints() const {
	Dictionary result;
	result["count"] = checkpoint_history.size();
	result["checkpoints"] = checkpoint_history;
	return result;
}

Dictionary CLIAIInputServer::_cmd_get_last_error_bundle() const {
	Dictionary result;
	result["errorBundle"] = last_error_bundle;
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
	CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, pending_perf_stop_trace_correlation_id);

	if (runtime_perf_recorder->get_captured_frames() > 0) {
		runtime_perf_recorder->set_summary_end_frame((int64_t)runtime_perf_recorder->get_last_sampled_frame());
	} else {
		runtime_perf_recorder->set_summary_end_frame((int64_t)_current_frame());
	}

	Dictionary response = _make_success_response(pending_perf_stop_id, pending_perf_stop_has_id, runtime_perf_recorder->build_summary(p_completed));
	if (p_event_response) {
		response["event"] = "perf_summary";
	}
	if (CustomFeatureTracer::has_singleton()) {
		Dictionary payloads;
		CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "response", response);
		CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "runtime_perf_summary", p_completed ? "info" : "warning", pending_perf_stop_trace_correlation_id, _make_trace_response_data(response), payloads);
	}
	_send_response(response);

	runtime_perf_recorder->close();
	memdelete(runtime_perf_recorder);
	runtime_perf_recorder = nullptr;
	pending_perf_stop = false;
	pending_perf_stop_id = Variant();
	pending_perf_stop_has_id = false;
	pending_perf_stop_trace_correlation_id = String();
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
		Ref<InputEvent> base_event = event;
		_dispatch_root_input(base_event, true);
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
	pending_perf_stop_trace_correlation_id = String();
}

void CLIAIInputServer::shutdown() {
	if (CustomFeatureTracer::has_singleton()) {
		Dictionary data;
		data["listening"] = is_listening();
		data["batchActive"] = active_batch.active;
		data["runtimePerfActive"] = runtime_perf_recorder != nullptr;
		CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "server_shutdown", "info", String(), data);
	}
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
	local_response_queue.clear();
	local_response_capture_active = false;
	local_execution_owner = false;
	observation_cache = ObservationFrameCache();
	root_image_cache_frame = UINT64_MAX;
	root_image_cache_populated = false;
	root_image_cache_available = false;
	root_image_cache.unref();
}
