/**************************************************************************/
/*  cli_harness_runner.cpp                                                */
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

#include "cli_harness_runner.h"

#include "core/io/image.h"
#include "main/cli_ai_input_server.h"
#include "main/cli_latest_log_runner.h"
#include "main/cli_performance_recorder.h"
#include "main/custom_feature_tracer.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/time.h"

bool CLIHarnessRunner::_consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error) {
	if (!r_next) {
		r_error = "Missing value after " + p_arg + ".";
		return false;
	}
	r_value = r_next->get();
	r_next = r_next->next();
	return true;
}

bool CLIHarnessRunner::parse_argument(const String &p_arg, List<String>::Element *&r_next, Options &r_options, String &r_error) {
	String value;
	r_error = String();

	if (p_arg == "--harness-run") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		r_options.enabled = true;
		r_options.run_path = value;
		return true;
	}

	if (p_arg == "--harness-report") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		r_options.report_path = value;
		r_options.report_path_set = true;
		return true;
	}

	if (p_arg == "--harness-timeout-frames") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (!value.is_valid_int()) {
			r_error = "--harness-timeout-frames must be followed by an integer.";
			return true;
		}
		r_options.timeout_frames = value.to_int();
		r_options.timeout_frames_set = true;
		return true;
	}

	if (p_arg == "--harness-format") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (value == "text") {
			r_options.format = FORMAT_TEXT;
		} else if (value == "json") {
			r_options.format = FORMAT_JSON;
		} else if (value == "both") {
			r_options.format = FORMAT_BOTH;
		} else {
			r_error = "--harness-format must be text, json or both.";
			return true;
		}
		r_options.format_set = true;
		return true;
	}

	return false;
}

Error CLIHarnessRunner::validate_options(const Options &p_options, bool p_found_project, bool p_editor, bool p_project_manager, bool p_cmdline_tool, String &r_error) {
	r_error = String();
	if (!p_options.enabled) {
		if (p_options.report_path_set || p_options.timeout_frames_set || p_options.format_set) {
			r_error = "--harness-report, --harness-timeout-frames and --harness-format require --harness-run.";
			return ERR_INVALID_PARAMETER;
		}
		return OK;
	}
	if (p_options.run_path.is_empty()) {
		r_error = "--harness-run requires a script path.";
		return ERR_INVALID_PARAMETER;
	}
	if (p_options.timeout_frames < 1) {
		r_error = "--harness-timeout-frames must be greater than or equal to 1.";
		return ERR_INVALID_PARAMETER;
	}
	if (!p_found_project) {
		r_error = "--harness-run requires a project context.";
		return ERR_INVALID_PARAMETER;
	}
	if (p_editor || p_project_manager || p_cmdline_tool) {
		r_error = "--harness-run only supports CLI project runs.";
		return ERR_INVALID_PARAMETER;
	}
	return OK;
}

String CLIHarnessRunner::get_format_name(Format p_format) {
	switch (p_format) {
		case FORMAT_TEXT:
			return "text";
		case FORMAT_JSON:
			return "json";
		case FORMAT_BOTH:
			return "both";
	}
	return "text";
}

String CLIHarnessRunner::resolve_script_path(const String &p_path) {
	if (p_path.begins_with("res://") || p_path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(p_path).simplify_path();
	}
	if (p_path.is_absolute_path()) {
		return p_path.simplify_path();
	}
	const String base = ProjectSettings::get_singleton() && !ProjectSettings::get_singleton()->get_resource_path().is_empty() ? ProjectSettings::get_singleton()->get_resource_path() : OS::get_singleton()->get_cwd();
	return base.path_join(p_path).simplify_path();
}

Error CLIHarnessRunner::read_script_file(const String &p_path, Dictionary &r_script, String &r_error) {
	r_script.clear();
	r_error = String();
	const String resolved_path = resolve_script_path(p_path);
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(resolved_path, FileAccess::READ, &err);
	if (file.is_null() || err != OK) {
		r_error = vformat("Unable to open harness script \"%s\" (error code %d).", resolved_path, (int)err);
		return err == OK ? ERR_CANT_OPEN : err;
	}

	JSON json;
	err = json.parse(file->get_as_text());
	if (err != OK) {
		r_error = vformat("Invalid harness JSON: %s at line %d.", json.get_error_message(), json.get_error_line());
		return ERR_PARSE_ERROR;
	}
	if (json.get_data().get_type() != Variant::DICTIONARY) {
		r_error = "Harness script must be a JSON object.";
		return ERR_INVALID_DATA;
	}
	r_script = json.get_data();
	return OK;
}

bool CLIHarnessRunner::_dictionary_has_only_keys(const Dictionary &p_dict, const Vector<String> &p_keys, String &r_error) {
	Array keys = p_dict.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String key = String(keys[i]);
		bool found = false;
		for (const String &allowed : p_keys) {
			if (key == allowed) {
				found = true;
				break;
			}
		}
		if (!found) {
			r_error = "Unknown field: " + key + ".";
			return false;
		}
	}
	return true;
}

bool CLIHarnessRunner::_get_bool(const Dictionary &p_dict, const String &p_key, bool p_default, bool &r_value, String &r_error) {
	if (!p_dict.has(p_key)) {
		r_value = p_default;
		return true;
	}
	if (p_dict[p_key].get_type() != Variant::BOOL) {
		r_error = p_key + " must be a boolean.";
		return false;
	}
	r_value = (bool)p_dict[p_key];
	return true;
}

bool CLIHarnessRunner::_get_int(const Dictionary &p_dict, const String &p_key, int p_default, int &r_value, String &r_error) {
	if (!p_dict.has(p_key)) {
		r_value = p_default;
		return true;
	}
	if (p_dict[p_key].get_type() != Variant::INT && p_dict[p_key].get_type() != Variant::FLOAT) {
		r_error = p_key + " must be a number.";
		return false;
	}
	r_value = (int)p_dict[p_key];
	return true;
}

bool CLIHarnessRunner::_get_double(const Dictionary &p_dict, const String &p_key, double p_default, double &r_value, String &r_error) {
	if (!p_dict.has(p_key)) {
		r_value = p_default;
		return true;
	}
	if (p_dict[p_key].get_type() != Variant::INT && p_dict[p_key].get_type() != Variant::FLOAT) {
		r_error = p_key + " must be a number.";
		return false;
	}
	r_value = (double)p_dict[p_key];
	return true;
}

bool CLIHarnessRunner::_get_string(const Dictionary &p_dict, const String &p_key, const String &p_default, String &r_value, String &r_error) {
	if (!p_dict.has(p_key)) {
		r_value = p_default;
		return true;
	}
	if (p_dict[p_key].get_type() != Variant::STRING && p_dict[p_key].get_type() != Variant::STRING_NAME) {
		r_error = p_key + " must be a string.";
		return false;
	}
	r_value = String(p_dict[p_key]);
	return true;
}

Error CLIHarnessRunner::extract_startup_overrides(const Dictionary &p_script, StartupOverrides &r_overrides, String &r_error) {
	r_overrides = StartupOverrides();
	if (!p_script.has("startup")) {
		return OK;
	}
	if (p_script["startup"].get_type() != Variant::DICTIONARY) {
		r_error = "startup must be an object.";
		return ERR_INVALID_DATA;
	}
	Dictionary startup = p_script["startup"];
	if (!_dictionary_has_only_keys(startup, { "windowed", "aiAgentPort", "perf" }, r_error)) {
		return ERR_INVALID_DATA;
	}
	if (!_get_bool(startup, "windowed", false, r_overrides.windowed, r_error) ||
			!_get_bool(startup, "perf", false, r_overrides.perf, r_error)) {
		return ERR_INVALID_DATA;
	}
	if (startup.has("aiAgentPort")) {
		int port = 0;
		if (!_get_int(startup, "aiAgentPort", 7010, port, r_error)) {
			return ERR_INVALID_DATA;
		}
		if (port < 1 || port > 65535) {
			r_error = "startup.aiAgentPort must be between 1 and 65535.";
			return ERR_INVALID_DATA;
		}
		r_overrides.has_ai_agent_port = true;
		r_overrides.ai_agent_port = port;
	}
	return OK;
}

Error CLIHarnessRunner::validate_script(const Dictionary &p_script, String &r_error) {
	r_error = String();
	if (!_dictionary_has_only_keys(p_script, { "name", "startup", "steps", "evidence" }, r_error)) {
		return ERR_INVALID_DATA;
	}
	String name;
	if (!_get_string(p_script, "name", "harness", name, r_error)) {
		return ERR_INVALID_DATA;
	}
	if (!p_script.has("steps") || p_script["steps"].get_type() != Variant::ARRAY) {
		r_error = "steps must be an array.";
		return ERR_INVALID_DATA;
	}
	Array steps = p_script["steps"];
	if (steps.is_empty()) {
		r_error = "steps must not be empty.";
		return ERR_INVALID_DATA;
	}
	StartupOverrides startup;
	if (extract_startup_overrides(p_script, startup, r_error) != OK) {
		return ERR_INVALID_DATA;
	}
	if (p_script.has("evidence")) {
		if (p_script["evidence"].get_type() != Variant::DICTIONARY) {
			r_error = "evidence must be an object.";
			return ERR_INVALID_DATA;
		}
		Dictionary evidence = p_script["evidence"];
		if (!_dictionary_has_only_keys(evidence, { "screenshots", "latestLog", "trace", "perfSummary" }, r_error)) {
			return ERR_INVALID_DATA;
		}
		String screenshots;
		if (!_get_string(evidence, "screenshots", "on_failure", screenshots, r_error)) {
			return ERR_INVALID_DATA;
		}
		if (screenshots != "off" && screenshots != "on_failure" && screenshots != "always") {
			r_error = "evidence.screenshots must be off, on_failure or always.";
			return ERR_INVALID_DATA;
		}
		bool ignored = false;
		if (!_get_bool(evidence, "latestLog", false, ignored, r_error) ||
				!_get_bool(evidence, "trace", true, ignored, r_error) ||
				!_get_bool(evidence, "perfSummary", false, ignored, r_error)) {
			return ERR_INVALID_DATA;
		}
	}

	for (int i = 0; i < steps.size(); i++) {
		if (steps[i].get_type() != Variant::DICTIONARY) {
			r_error = vformat("steps[%d] must be an object.", i);
			return ERR_INVALID_DATA;
		}
		Dictionary request;
		Dictionary meta;
		if (!build_ai_request_for_step(steps[i], i + 1, request, meta, r_error)) {
			r_error = vformat("steps[%d]: %s", i, r_error);
			return ERR_INVALID_DATA;
		}
	}
	return OK;
}

Dictionary CLIHarnessRunner::_make_error(const String &p_code, const String &p_message) {
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	return error;
}

bool CLIHarnessRunner::_step_has_command(const Dictionary &p_step) {
	return p_step.has("cmd");
}

bool CLIHarnessRunner::_step_has_expectation(const Dictionary &p_step) {
	return p_step.has("expect");
}

Error CLIHarnessRunner::_build_expect_request(const Dictionary &p_step, int p_id, Dictionary &r_request, String &r_error) {
	String expect;
	if (!_get_string(p_step, "expect", String(), expect, r_error) || expect.is_empty()) {
		r_error = r_error.is_empty() ? "expect must be a non-empty string." : r_error;
		return ERR_INVALID_DATA;
	}

	r_request.clear();
	r_request["id"] = p_id;
	if (p_step.has("timeoutFrames")) {
		r_request["timeoutFrames"] = p_step["timeoutFrames"];
	}
	if (p_step.has("pollEveryFrames")) {
		r_request["pollEveryFrames"] = p_step["pollEveryFrames"];
	}
	if (p_step.has("captureOnError")) {
		r_request["captureOnError"] = p_step["captureOnError"];
	}

	if (expect == "node_exists") {
		r_request["cmd"] = "wait_node_exists";
	} else if (expect == "node_gone") {
		r_request["cmd"] = "wait_node_gone";
	} else if (expect == "node_visible" || expect == "node_hidden") {
		r_request["cmd"] = "wait_property";
		r_request["property"] = "visible";
		r_request["equals"] = expect == "node_visible";
	} else if (expect == "property_equals") {
		r_request["cmd"] = "wait_property";
		if (!p_step.has("property") || !p_step.has("equals")) {
			r_error = "property_equals requires property and equals.";
			return ERR_INVALID_DATA;
		}
		r_request["property"] = p_step["property"];
		r_request["equals"] = p_step["equals"];
	} else if (expect == "scene_changed") {
		r_request["cmd"] = "wait_scene_changed";
		if (p_step.has("fromScenePath")) {
			r_request["fromScenePath"] = p_step["fromScenePath"];
		}
	} else if (expect == "screenshot_diff") {
		r_request["cmd"] = "wait_screenshot_diff";
		if (p_step.has("threshold")) {
			r_request["threshold"] = p_step["threshold"];
		}
	} else {
		r_error = "Unsupported expect alias: " + expect + ".";
		return ERR_INVALID_DATA;
	}

	if (expect != "scene_changed" && expect != "screenshot_diff") {
		if (!p_step.has("selector")) {
			r_error = expect + " requires selector.";
			return ERR_INVALID_DATA;
		}
		if (p_step["selector"].get_type() != Variant::DICTIONARY) {
			r_error = expect + ".selector must be an object.";
			return ERR_INVALID_DATA;
		}
		r_request["selector"] = p_step["selector"];
	}
	return OK;
}

void CLIHarnessRunner::_append_key_tap(Array &r_ops, const String &p_keycode) {
	Dictionary press;
	press["cmd"] = "key";
	press["keycode"] = p_keycode;
	press["pressed"] = true;
	Dictionary release;
	release["cmd"] = "key";
	release["keycode"] = p_keycode;
	release["pressed"] = false;
	r_ops.push_back(press);
	r_ops.push_back(release);
}

void CLIHarnessRunner::_append_action_tap(Array &r_ops, const String &p_action, int p_frames) {
	Dictionary press;
	press["cmd"] = "action";
	press["action"] = p_action;
	press["pressed"] = true;
	Dictionary wait;
	wait["cmd"] = "wait";
	wait["frames"] = p_frames;
	Dictionary release;
	release["cmd"] = "action";
	release["action"] = p_action;
	release["pressed"] = false;
	r_ops.push_back(press);
	r_ops.push_back(wait);
	r_ops.push_back(release);
}

Error CLIHarnessRunner::_build_alias_request(const Dictionary &p_step, int p_id, Dictionary &r_request, Dictionary &r_step_meta, String &r_error) {
	String cmd;
	if (!_get_string(p_step, "cmd", String(), cmd, r_error) || cmd.is_empty()) {
		r_error = r_error.is_empty() ? "cmd must be a non-empty string." : r_error;
		return ERR_INVALID_DATA;
	}

	r_request = p_step.duplicate(true);
	r_request["id"] = p_id;
	r_step_meta["kind"] = "cmd";
	r_step_meta["sourceCommand"] = cmd;

	if (cmd == "tap_action") {
		String action;
		int frames = 1;
		if (!_get_string(p_step, "action", String(), action, r_error) || action.is_empty() ||
				!_get_int(p_step, "frames", 1, frames, r_error)) {
			r_error = r_error.is_empty() ? "tap_action requires action." : r_error;
			return ERR_INVALID_DATA;
		}
		if (frames < 1) {
			r_error = "tap_action.frames must be greater than or equal to 1.";
			return ERR_INVALID_DATA;
		}
		Array ops;
		Dictionary press;
		press["cmd"] = "action";
		press["action"] = action;
		press["pressed"] = true;
		Dictionary wait;
		wait["cmd"] = "wait";
		wait["frames"] = frames;
		Dictionary release;
		release["cmd"] = "action";
		release["action"] = action;
		release["pressed"] = false;
		ops.push_back(press);
		ops.push_back(wait);
		ops.push_back(release);
		r_request.clear();
		r_request["id"] = p_id;
		r_request["cmd"] = "batch";
		r_request["onError"] = "stop";
		r_request["ops"] = ops;
		r_step_meta["expandedSteps"] = ops.size();
		return OK;
	}

	if (cmd == "hold_action") {
		String action;
		int frames = 0;
		if (!_get_string(p_step, "action", String(), action, r_error) || action.is_empty() ||
				!_get_int(p_step, "frames", 0, frames, r_error)) {
			r_error = r_error.is_empty() ? "hold_action requires action and frames." : r_error;
			return ERR_INVALID_DATA;
		}
		if (frames < 1) {
			r_error = "hold_action.frames must be greater than or equal to 1.";
			return ERR_INVALID_DATA;
		}
		Array ops;
		Dictionary press;
		press["cmd"] = "action";
		press["action"] = action;
		press["pressed"] = true;
		Dictionary wait;
		wait["cmd"] = "wait";
		wait["frames"] = frames;
		Dictionary release;
		release["cmd"] = "action";
		release["action"] = action;
		release["pressed"] = false;
		ops.push_back(press);
		ops.push_back(wait);
		ops.push_back(release);
		r_request.clear();
		r_request["id"] = p_id;
		r_request["cmd"] = "batch";
		r_request["onError"] = "stop";
		r_request["ops"] = ops;
		r_step_meta["expandedSteps"] = ops.size();
		return OK;
	}

	if (cmd == "mouse_look") {
		double relative_x = 0.0;
		double relative_y = 0.0;
		if (!_get_double(p_step, "relativeX", 0.0, relative_x, r_error) ||
				!_get_double(p_step, "relativeY", 0.0, relative_y, r_error)) {
			return ERR_INVALID_DATA;
		}
		r_request.clear();
		r_request["id"] = p_id;
		r_request["cmd"] = "mouse_motion";
		r_request["relativeX"] = relative_x;
		r_request["relativeY"] = relative_y;
		r_step_meta["expandedSteps"] = 1;
		return OK;
	}

	if (cmd == "input_self_test") {
		r_request.clear();
		r_request["id"] = p_id;
		r_request["cmd"] = "batch";
		r_request["onError"] = "stop";
		Array ops;
		Dictionary status;
		status["cmd"] = "get_status";
		Dictionary viewport;
		viewport["cmd"] = "get_viewport_summary";
		ops.push_back(status);
		ops.push_back(viewport);
		if (p_step.has("selector")) {
			if (p_step["selector"].get_type() != Variant::DICTIONARY) {
				r_error = "input_self_test.selector must be an object.";
				return ERR_INVALID_DATA;
			}
			Dictionary focus;
			focus["cmd"] = "focus_target";
			focus["selector"] = p_step["selector"];
			ops.push_back(focus);
			_append_key_tap(ops, "Enter");
			_append_key_tap(ops, "Space");
			bool click = true;
			if (!_get_bool(p_step, "click", true, click, r_error)) {
				return ERR_INVALID_DATA;
			}
			if (click) {
				Dictionary click_target;
				click_target["cmd"] = "click_target";
				click_target["selector"] = p_step["selector"];
				ops.push_back(click_target);
			}
		}
		String action;
		if (p_step.has("action")) {
			if (!_get_string(p_step, "action", String(), action, r_error) || action.is_empty()) {
				r_error = r_error.is_empty() ? "input_self_test.action must be a non-empty string." : r_error;
				return ERR_INVALID_DATA;
			}
			_append_action_tap(ops, action, 1);
		}
		if (p_step.has("mouseRelative")) {
			if (p_step["mouseRelative"].get_type() != Variant::ARRAY) {
				r_error = "input_self_test.mouseRelative must be an array.";
				return ERR_INVALID_DATA;
			}
			Array relative = p_step["mouseRelative"];
			if (relative.size() != 2 || (relative[0].get_type() != Variant::INT && relative[0].get_type() != Variant::FLOAT) ||
					(relative[1].get_type() != Variant::INT && relative[1].get_type() != Variant::FLOAT)) {
				r_error = "input_self_test.mouseRelative must contain two numbers.";
				return ERR_INVALID_DATA;
			}
			Dictionary motion;
			motion["cmd"] = "mouse_motion";
			motion["relativeX"] = (double)relative[0];
			motion["relativeY"] = (double)relative[1];
			ops.push_back(motion);
		}
		r_request["ops"] = ops;
		r_step_meta["expandedSteps"] = ops.size();
		r_step_meta["selfTest"] = true;
		if (p_step.has("selector")) {
			r_step_meta["checksFocusAndGuiKeys"] = true;
			r_step_meta["checksClickTarget"] = p_step.get("click", true);
		}
		if (!action.is_empty()) {
			r_step_meta["checksAction"] = action;
		}
		if (p_step.has("mouseRelative")) {
			r_step_meta["checksMouseMotion"] = true;
		}
		return OK;
	}

	return OK;
}

bool CLIHarnessRunner::build_ai_request_for_step(const Dictionary &p_step, int p_id, Dictionary &r_request, Dictionary &r_step_meta, String &r_error) {
	r_error = String();
	if (_step_has_command(p_step) == _step_has_expectation(p_step)) {
		r_error = "step must contain exactly one of cmd or expect.";
		return false;
	}
	if (_step_has_expectation(p_step)) {
		if (_build_expect_request(p_step, p_id, r_request, r_error) != OK) {
			return false;
		}
		r_step_meta["kind"] = "expect";
		r_step_meta["sourceExpect"] = p_step["expect"];
		return true;
	}
	return _build_alias_request(p_step, p_id, r_request, r_step_meta, r_error) == OK;
}

String CLIHarnessRunner::_utc_now_string() {
	return Time::get_singleton()->get_datetime_string_from_system(true, true);
}

CLIHarnessRunner::CLIHarnessRunner(const Options &p_options, const String &p_project_path, CLIAIInputServer *p_ai_server, CLIPerformanceRecorder *p_perf_recorder) {
	options = p_options;
	project_path = p_project_path;
	ai_server = p_ai_server;
	perf_recorder = p_perf_recorder;
}

Error CLIHarnessRunner::initialize(String &r_error) {
	if (!ai_server) {
		r_error = "Harness requires an initialized Runtime AI Agent Control server.";
		return ERR_UNCONFIGURED;
	}
	Error err = read_script_file(options.run_path, script, r_error);
	if (err != OK) {
		return err;
	}
	err = validate_script(script, r_error);
	if (err != OK) {
		return err;
	}

	steps = script["steps"];
	if (script.has("evidence")) {
		evidence_options = script["evidence"];
	}
	ai_server->set_local_execution_owner(true);
	harness_name = script.get("name", "harness");
	correlation_id = CustomFeatureTracer::has_singleton() ? CustomFeatureTracer::get_singleton()->next_correlation_id(CustomFeatureTracer::FEATURE_PROJECT_HARNESS) : String();
	started_frame = Engine::get_singleton() ? Engine::get_singleton()->get_process_frames() : 0;
	started_usec = OS::get_singleton()->get_ticks_usec();
	effective_report_path = _resolve_report_path();

	report["name"] = harness_name;
	report["projectPath"] = project_path;
	report["scriptPath"] = resolve_script_path(options.run_path);
	report["startedAt"] = _utc_now_string();
	report["startedFrame"] = (int64_t)started_frame;
	report["format"] = get_format_name(options.format);

	Dictionary data;
	data["name"] = harness_name;
	data["steps"] = steps.size();
	data["scriptPath"] = report["scriptPath"];
	_record_trace_event("harness_started", "info", data);
	return OK;
}

void CLIHarnessRunner::_record_trace_event(const String &p_event, const String &p_severity, const Dictionary &p_data, const Dictionary &p_payloads, const Dictionary &p_error) {
	if (!CustomFeatureTracer::has_singleton()) {
		return;
	}
	bool trace_enabled = true;
	String ignored_error;
	if (!_get_bool(evidence_options, "trace", true, trace_enabled, ignored_error) || !trace_enabled) {
		return;
	}
	CustomFeatureTracer::get_singleton()->record_event(CustomFeatureTracer::FEATURE_PROJECT_HARNESS, p_event, p_severity, correlation_id, p_data, p_payloads, p_error);
}

void CLIHarnessRunner::poll() {
	if (finished || !ai_server) {
		return;
	}
	CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_PROJECT_HARNESS, correlation_id);

	Dictionary response;
	while (ai_server->pop_local_response(response)) {
		_finish_step(response);
		if (finished) {
			return;
		}
	}

	if (waiting_for_response) {
		if ((int64_t)((Engine::get_singleton() ? Engine::get_singleton()->get_process_frames() : 0) - started_frame) >= options.timeout_frames) {
			_finish_with_error(EXIT_TIMEOUT, "timeout", vformat("Harness timed out after %d frames.", options.timeout_frames));
		}
		return;
	}

	if (current_step >= steps.size()) {
		_finish_success();
		return;
	}
	_start_step();
}

void CLIHarnessRunner::_start_step() {
	Dictionary step = steps[current_step];
	Dictionary request;
	Dictionary meta;
	String error;
	if (!build_ai_request_for_step(step, current_step + 1, request, meta, error)) {
		_finish_with_error(EXIT_INVALID_ARGUMENTS, "invalid_step", error);
		return;
	}

	Dictionary trace_data;
	trace_data["index"] = current_step;
	trace_data["kind"] = meta.get("kind", String());
	trace_data["command"] = request.get("cmd", String());
	trace_data["sourceCommand"] = meta.get("sourceCommand", String());
	trace_data["sourceExpect"] = meta.get("sourceExpect", String());
	_record_trace_event("step_started", "info", trace_data);

	Dictionary report_step;
	report_step["index"] = current_step;
	report_step["startedFrame"] = (int64_t)(Engine::get_singleton() ? Engine::get_singleton()->get_process_frames() : 0);
	report_step["request"] = request;
	report_step["meta"] = meta;
	report_steps.push_back(report_step);

	const Dictionary immediate = ai_server->execute_local_request(request);
	if (immediate.get("deferred", false)) {
		Dictionary data;
		data["index"] = current_step;
		data["command"] = request.get("cmd", String());
		_record_trace_event("step_deferred", "info", data);
		waiting_for_response = true;
		return;
	}
	_finish_step(immediate);
}

void CLIHarnessRunner::_finish_step(const Dictionary &p_response) {
	if (report_steps.is_empty()) {
		return;
	}
	Dictionary report_step = report_steps[report_steps.size() - 1];
	if (p_response.has("event")) {
		Array events = report_step.has("events") && report_step["events"].get_type() == Variant::ARRAY ? (Array)report_step["events"] : Array();
		events.push_back(p_response);
		report_step["events"] = events;
		report_steps[report_steps.size() - 1] = report_step;
		Dictionary data;
		data["index"] = current_step;
		data["event"] = p_response.get("event", String());
		Dictionary payloads;
		if (CustomFeatureTracer::has_singleton()) {
			CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "response", p_response);
		}
		_record_trace_event("step_event", "info", data, payloads);
		waiting_for_response = true;
		return;
	}
	report_step["endedFrame"] = (int64_t)(Engine::get_singleton() ? Engine::get_singleton()->get_process_frames() : 0);
	report_step["response"] = p_response;
	report_step["ok"] = p_response.get("ok", false);
	report_steps[report_steps.size() - 1] = report_step;

	Dictionary data;
	data["index"] = current_step;
	data["ok"] = p_response.get("ok", false);
	data["command"] = ((Dictionary)report_step["request"]).get("cmd", String());
	const Dictionary meta = report_step.has("meta") && report_step["meta"].get_type() == Variant::DICTIONARY ? (Dictionary)report_step["meta"] : Dictionary();
	const bool is_expect = String(meta.get("kind", String())) == "expect";
	Dictionary payloads;
	if (CustomFeatureTracer::has_singleton()) {
		CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "response", p_response);
	}
	_record_trace_event("step_finished", (bool)p_response.get("ok", false) ? "info" : "warning", data, payloads, p_response.has("error") && p_response["error"].get_type() == Variant::DICTIONARY ? (Dictionary)p_response["error"] : Dictionary());
	if (is_expect) {
		Dictionary assertion;
		assertion["index"] = current_step;
		assertion["expect"] = meta.get("sourceExpect", String());
		assertion["ok"] = p_response.get("ok", false);
		assertion["startedFrame"] = report_step.get("startedFrame", 0);
		assertion["endedFrame"] = report_step.get("endedFrame", 0);
		assertion["request"] = report_step.get("request", Dictionary());
		assertion["response"] = p_response;
		assertions.push_back(assertion);
		if (!(bool)p_response.get("ok", false)) {
			_record_trace_event("assertion_failed", "warning", data, payloads, p_response.has("error") && p_response["error"].get_type() == Variant::DICTIONARY ? (Dictionary)p_response["error"] : _make_error("assertion_failed", "Harness assertion failed."));
		}
	}

	waiting_for_response = false;
	if (!(bool)p_response.get("ok", false)) {
		Dictionary error = p_response.has("error") && p_response["error"].get_type() == Variant::DICTIONARY ? (Dictionary)p_response["error"] : _make_error("operation_failed", "Harness step failed.");
		if (!is_expect) {
			_record_trace_event("step_failed", "warning", data, payloads, error);
		}
		_finish_with_error(EXIT_FAILED, error.get("code", "operation_failed"), error.get("message", "Harness step failed."), p_response);
		return;
	}
	current_step++;
}

void CLIHarnessRunner::_capture_screenshot_evidence(const String &p_name, Dictionary *r_bundle) {
	if (!ai_server) {
		return;
	}
	Dictionary screenshot_request;
	screenshot_request["id"] = "harness-screenshot";
	screenshot_request["cmd"] = "get_screenshot";
	screenshot_request["name"] = p_name;
	const Dictionary screenshot_response = ai_server->execute_local_request(screenshot_request);
	if ((bool)screenshot_response.get("ok", false)) {
		Dictionary result = screenshot_response["result"];
		if (r_bundle) {
			(*r_bundle)["screenshotPath"] = result.get("path", String());
		}
		evidence_items.push_back(result);
	} else if (r_bundle) {
		(*r_bundle)["screenshotError"] = screenshot_response;
	}
}

void CLIHarnessRunner::_capture_failure_evidence() {
	String screenshots;
	String error;
	_get_string(evidence_options, "screenshots", "on_failure", screenshots, error);
	if (screenshots == "off") {
		return;
	}
	_capture_screenshot_evidence(harness_name + "_failure", &failure_bundle);
}

void CLIHarnessRunner::_capture_success_evidence() {
	String screenshots;
	String error;
	_get_string(evidence_options, "screenshots", "on_failure", screenshots, error);
	if (screenshots != "always") {
		return;
	}
	_capture_screenshot_evidence(harness_name + "_success");
}

void CLIHarnessRunner::_collect_latest_log_summary() {
	bool latest_log = false;
	String error;
	if (!_get_bool(evidence_options, "latestLog", false, latest_log, error) || !latest_log) {
		return;
	}

	CLILatestLogRunner::Options latest_options;
	latest_options.enabled = true;
	latest_options.lines = 200;
	latest_options.format = CLILatestLogRunner::FORMAT_JSON;
	String output;
	Dictionary summary;
	String build_error;
	const String base_path = CLILatestLogRunner::resolve_base_log_path(String());
	String latest_path;
	if (CLILatestLogRunner::find_latest_log_path(base_path, latest_path) == OK &&
			CLILatestLogRunner::build_output(latest_path, base_path, latest_options, output, summary, build_error) == OK) {
		report["latestLogSummary"] = summary;
	}
}

void CLIHarnessRunner::_finish_with_error(int p_exit_code, const String &p_code, const String &p_message, const Dictionary &p_response) {
	if (finished) {
		return;
	}
	if (ai_server) {
		ai_server->set_local_execution_owner(false);
	}
	exit_code = p_exit_code;
	failure_bundle["error"] = _make_error(p_code, p_message);
	if (!p_response.is_empty()) {
		failure_bundle["response"] = p_response;
	}
	_capture_failure_evidence();
	_finalize_report();
	_write_report();
	_print_output();
	Dictionary data;
	data["exitCode"] = exit_code;
	data["errorCode"] = p_code;
	Dictionary error = _make_error(p_code, p_message);
	Dictionary payloads;
	if (CustomFeatureTracer::has_singleton()) {
		CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "report", report);
	}
	_record_trace_event("harness_finished", "warning", data, payloads, error);
	finished = true;
}

void CLIHarnessRunner::_finish_success() {
	if (ai_server) {
		ai_server->set_local_execution_owner(false);
	}
	exit_code = EXIT_OK;
	_capture_success_evidence();
	_finalize_report();
	_write_report();
	_print_output();
	Dictionary data;
	data["exitCode"] = exit_code;
	data["steps"] = report_steps.size();
	Dictionary payloads;
	if (CustomFeatureTracer::has_singleton()) {
		CustomFeatureTracer::get_singleton()->add_json_payload(payloads, "report", report);
	}
	_record_trace_event("harness_finished", "info", data, payloads);
	finished = true;
}

void CLIHarnessRunner::_finalize_report() {
	_collect_latest_log_summary();
	report["endedAt"] = _utc_now_string();
	report["endedFrame"] = (int64_t)(Engine::get_singleton() ? Engine::get_singleton()->get_process_frames() : 0);
	report["durationUsec"] = (int64_t)(OS::get_singleton()->get_ticks_usec() - started_usec);
	report["exitCode"] = exit_code;
	report["steps"] = report_steps;
	report["assertions"] = assertions;
	report["evidence"] = evidence_items;
	report["failureBundle"] = failure_bundle;
	report["reportPath"] = effective_report_path;
	if (CustomFeatureTracer::has_singleton()) {
		bool trace_enabled = true;
		String ignored_error;
		if (_get_bool(evidence_options, "trace", true, trace_enabled, ignored_error) && trace_enabled) {
			report["traceSessionDir"] = CustomFeatureTracer::get_singleton()->get_session_dir();
		} else {
			report["traceSessionDir"] = String();
		}
	} else {
		report["traceSessionDir"] = String();
	}
	bool perf_summary = false;
	String ignored_error;
	if (_get_bool(evidence_options, "perfSummary", false, perf_summary, ignored_error) && perf_summary && perf_recorder) {
		report["perfSummary"] = perf_recorder->build_summary(perf_recorder->is_completed());
	}
}

String CLIHarnessRunner::_resolve_report_path() const {
	if (!options.report_path.is_empty()) {
		if (options.report_path.begins_with("res://") || options.report_path.begins_with("user://")) {
			return ProjectSettings::get_singleton()->globalize_path(options.report_path).simplify_path();
		}
		if (options.report_path.is_absolute_path()) {
			return options.report_path.simplify_path();
		}
		return ProjectSettings::get_singleton()->get_resource_path().path_join(options.report_path).simplify_path();
	}
	if (CustomFeatureTracer::has_singleton()) {
		return CustomFeatureTracer::get_singleton()->get_session_dir().path_join("harness-report.json");
	}
	return OS::get_singleton()->get_temp_path().path_join("godot-harness-report.json");
}

void CLIHarnessRunner::_write_report() {
	if (effective_report_path.is_empty()) {
		return;
	}
	DirAccess::make_dir_recursive_absolute(effective_report_path.get_base_dir());
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(effective_report_path, FileAccess::WRITE, &err);
	if (file.is_valid() && err == OK) {
		file->store_string(JSON::stringify(report, "\t"));
		file->close();
		return;
	}
	report["reportWriteError"] = vformat("Unable to write harness report \"%s\" (error code %d).", effective_report_path, (int)err);
	OS::get_singleton()->printerr("%s\n", String(report["reportWriteError"]).utf8().get_data());
}

String CLIHarnessRunner::_build_text_output() const {
	String output;
	output += vformat("[harness] name=%s exitCode=%d steps=%d report=%s\n", harness_name, exit_code, report_steps.size(), effective_report_path);
	if (!failure_bundle.is_empty() && failure_bundle.has("error")) {
		const Dictionary error = failure_bundle["error"];
		output += vformat("[harness] error code=%s message=%s\n", String(error.get("code", String())), String(error.get("message", String())));
	}
	return output;
}

void CLIHarnessRunner::_print_output() const {
	if (options.format == FORMAT_TEXT) {
		OS::get_singleton()->print("%s", _build_text_output().utf8().get_data());
		return;
	}
	if (options.format == FORMAT_BOTH) {
		OS::get_singleton()->print("%s", _build_text_output().utf8().get_data());
	}
	OS::get_singleton()->print("%s\n", JSON::stringify(report).utf8().get_data());
}
