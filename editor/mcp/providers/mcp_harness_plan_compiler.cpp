/**************************************************************************/
/*  mcp_harness_plan_compiler.cpp                                        */
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
/* "Software"), to deal in the Software without restriction, including   */
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

#include "mcp_harness_plan_compiler.h"

#include "core/math/math_funcs.h"

namespace {

constexpr int MAX_VARIANT_DEPTH = 16;
constexpr double MAX_MOUSE_DELTA = 100000.0;

static Error _fail(const String &p_message, String *r_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return ERR_INVALID_DATA;
}

static bool _has_only(const Dictionary &p_dictionary, const PackedStringArray &p_allowed, String &r_unknown) {
	for (const KeyValue<Variant, Variant> &entry : p_dictionary) {
		if (!entry.key.is_string() || !p_allowed.has(String(entry.key))) {
			r_unknown = String(entry.key);
			return false;
		}
	}
	r_unknown = String();
	return true;
}

static bool _read_bool(const Dictionary &p_dictionary, const StringName &p_name, bool p_default, bool &r_value, String &r_error) {
	const Variant value = p_dictionary.get(p_name, p_default);
	if (value.get_type() != Variant::BOOL) {
		r_error = String(p_name) + " must be a boolean.";
		return false;
	}
	r_value = value;
	return true;
}

static bool _read_string(const Dictionary &p_dictionary, const StringName &p_name, const String &p_default, String &r_value, String &r_error, bool p_allow_empty = true) {
	const Variant value = p_dictionary.get(p_name, p_default);
	if (value.get_type() != Variant::STRING && value.get_type() != Variant::STRING_NAME) {
		r_error = String(p_name) + " must be a string.";
		return false;
	}
	r_value = value;
	if ((!p_allow_empty && r_value.is_empty()) || r_value.length() > MCPHarnessPlanCompiler::MAX_TEXT_LENGTH) {
		r_error = String(p_name) + (r_value.is_empty() ? " must not be empty." : " exceeds the text length limit.");
		return false;
	}
	return true;
}

static bool _read_integer(const Dictionary &p_dictionary, const StringName &p_name, int64_t p_default, int64_t p_minimum, int64_t p_maximum, int64_t &r_value, String &r_error) {
	const Variant value = p_dictionary.get(p_name, p_default);
	if (value.get_type() == Variant::INT) {
		r_value = value;
	} else if (value.get_type() == Variant::FLOAT) {
		const double number = value;
		if (!Math::is_finite(number) || Math::floor(number) != number || number < double(INT64_MIN) || number > double(INT64_MAX)) {
			r_error = String(p_name) + " must be an integer.";
			return false;
		}
		r_value = int64_t(number);
	} else {
		r_error = String(p_name) + " must be an integer.";
		return false;
	}
	if (r_value < p_minimum || r_value > p_maximum) {
		r_error = vformat("%s must be between %d and %d.", String(p_name), p_minimum, p_maximum);
		return false;
	}
	return true;
}

static bool _read_number(const Dictionary &p_dictionary, const StringName &p_name, double p_default, double p_minimum, double p_maximum, double &r_value, String &r_error) {
	const Variant value = p_dictionary.get(p_name, p_default);
	if (value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) {
		r_error = String(p_name) + " must be numeric.";
		return false;
	}
	r_value = value;
	if (!Math::is_finite(r_value) || r_value < p_minimum || r_value > p_maximum) {
		r_error = String(p_name) + " is outside its allowed range.";
		return false;
	}
	return true;
}

static Error _validate_text_budget(const Variant &p_value, int p_depth, int64_t &r_total, String *r_error) {
	if (p_depth > MAX_VARIANT_DEPTH) {
		return _fail("Harness data exceeds the nesting limit.", r_error);
	}
	if (p_value.get_type() == Variant::STRING || p_value.get_type() == Variant::STRING_NAME) {
		const String text = p_value;
		if (text.length() > MCPHarnessPlanCompiler::MAX_TEXT_LENGTH) {
			return _fail("Harness text exceeds the per-value length limit.", r_error);
		}
		r_total += text.length();
		if (r_total > MCPHarnessPlanCompiler::MAX_TOTAL_TEXT_LENGTH) {
			return _fail("Harness text exceeds the total length limit.", r_error);
		}
		return OK;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array array = p_value;
		for (int i = 0; i < array.size(); i++) {
			const Error error = _validate_text_budget(array[i], p_depth + 1, r_total, r_error);
			if (error != OK) {
				return error;
			}
		}
	} else if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_value;
		for (const KeyValue<Variant, Variant> &entry : dictionary) {
			Error error = _validate_text_budget(entry.key, p_depth + 1, r_total, r_error);
			if (error == OK) {
				error = _validate_text_budget(entry.value, p_depth + 1, r_total, r_error);
			}
			if (error != OK) {
				return error;
			}
		}
	}
	return OK;
}

static Dictionary _make_step(int p_index, const String &p_kind, const String &p_source, const String &p_tool, const Dictionary &p_arguments, bool p_generation_required) {
	Dictionary step;
	step["index"] = p_index;
	step["kind"] = p_kind;
	step["source"] = p_source;
	step["tool"] = p_tool;
	step["arguments"] = p_arguments;
	PackedStringArray requirements;
	if (p_generation_required) {
		requirements.push_back("runtimeGeneration");
	}
	step["contextRequirements"] = requirements;
	return step;
}

static Dictionary _arguments_without(const Dictionary &p_step, const PackedStringArray &p_removed) {
	Dictionary arguments = p_step.duplicate(true);
	for (const String &name : p_removed) {
		arguments.erase(name);
	}
	return arguments;
}

static Error _validate_selector(const Variant &p_selector, const String &p_label, String *r_error) {
	if (p_selector.get_type() != Variant::DICTIONARY) {
		return _fail(p_label + " must be an object.", r_error);
	}
	String unknown;
	if (!_has_only(p_selector, PackedStringArray{ "path", "name", "type", "group", "text", "is3D", "visible", "screenRect", "nearestToScreenPoint" }, unknown)) {
		return _fail("Unknown " + p_label + " field: " + unknown + ".", r_error);
	}
	return OK;
}

static Error _compile_input_alias(const Dictionary &p_step, int p_index, const String &p_command, Dictionary &r_step, int &r_operations, String *r_error) {
	String unknown;
	if (p_command == "tap_action" || p_command == "hold_action") {
		if (!_has_only(p_step, PackedStringArray{ "cmd", "action", "frames" }, unknown)) {
			return _fail("Unknown " + p_command + " field: " + unknown + ".", r_error);
		}
		String action;
		String error;
		int64_t frames = p_command == "tap_action" ? 1 : 0;
		if (!_read_string(p_step, "action", String(), action, error, false) ||
				!_read_integer(p_step, "frames", frames, 1, MCPHarnessPlanCompiler::MAX_WAIT_FRAMES, frames, error)) {
			return _fail(error, r_error);
		}
		Dictionary press;
		press["type"] = "action";
		press["action"] = action;
		press["pressed"] = true;
		press["strength"] = 1.0;
		Dictionary release = press.duplicate(true);
		release["pressed"] = false;
		release["strength"] = 0.0;
		Array sequence;
		sequence.push_back(Dictionary{ { "event", press } });
		sequence.push_back(Dictionary{ { "waitFrames", frames } });
		sequence.push_back(Dictionary{ { "event", release } });
		Dictionary arguments;
		arguments["steps"] = sequence;
		r_step = _make_step(p_index, "command", p_command, "godot.runtime.input.sequence", arguments, true);
		r_step["expandedOperationCount"] = sequence.size();
		r_operations = sequence.size();
		return OK;
	}

	if (p_command == "mouse_look") {
		if (!_has_only(p_step, PackedStringArray{ "cmd", "relativeX", "relativeY" }, unknown)) {
			return _fail("Unknown mouse_look field: " + unknown + ".", r_error);
		}
		String error;
		double relative_x = 0.0;
		double relative_y = 0.0;
		if (!_read_number(p_step, "relativeX", 0.0, -MAX_MOUSE_DELTA, MAX_MOUSE_DELTA, relative_x, error) ||
				!_read_number(p_step, "relativeY", 0.0, -MAX_MOUSE_DELTA, MAX_MOUSE_DELTA, relative_y, error)) {
			return _fail(error, r_error);
		}
		Dictionary event;
		event["type"] = "mouse_motion";
		event["relative"] = Array{ relative_x, relative_y };
		Dictionary arguments;
		arguments["events"] = Array{ event };
		r_step = _make_step(p_index, "command", p_command, "godot.runtime.input.send", arguments, true);
		r_step["expandedOperationCount"] = 1;
		r_operations = 1;
		return OK;
	}
	return ERR_DOES_NOT_EXIST;
}

static Error _compile_runtime_command(const Dictionary &p_step, int p_index, const String &p_command, Dictionary &r_step, int &r_operations, String *r_error) {
	String tool;
	PackedStringArray allowed{ "cmd" };
	bool generation_required = false;
	if (p_command == "get_status") {
		tool = "godot.runtime.get_state";
	} else if (p_command == "get_viewport_summary") {
		tool = "godot.runtime.get_viewport_summary";
		allowed.append_array(PackedStringArray{ "includeCounts", "debuggerSession", "timeoutMs" });
	} else if (p_command == "get_interactables") {
		tool = "godot.runtime.get_interactables";
		allowed.append_array(PackedStringArray{ "filter", "maxResults", "debuggerSession", "timeoutMs" });
	} else if (p_command == "query_nodes") {
		tool = "godot.runtime.query_nodes";
		allowed.append_array(PackedStringArray{ "filter", "maxResults", "debuggerSession", "timeoutMs" });
	} else if (p_command == "get_node_snapshot") {
		tool = "godot.runtime.get_node_snapshot";
		allowed.append_array(PackedStringArray{ "selector", "debuggerSession", "timeoutMs" });
	} else if (p_command == "get_screenshot") {
		tool = "godot.runtime.get_screenshot";
		allowed.append_array(PackedStringArray{ "name", "crop", "debuggerSession", "timeoutMs" });
	} else if (p_command == "get_scene_tree") {
		tool = "godot.runtime.get_tree";
		allowed.append_array(PackedStringArray{ "debuggerSession", "timeoutMs" });
	} else {
		return ERR_DOES_NOT_EXIST;
	}

	String unknown;
	if (!_has_only(p_step, allowed, unknown)) {
		return _fail("Unknown " + p_command + " field: " + unknown + ".", r_error);
	}
	Dictionary arguments = _arguments_without(p_step, PackedStringArray{ "cmd" });
	if ((p_command == "get_node_snapshot" && _validate_selector(arguments.get("selector", Variant()), "selector", r_error) != OK) ||
			((p_command == "query_nodes" || p_command == "get_interactables") && arguments.has("filter") && _validate_selector(arguments["filter"], "filter", r_error) != OK)) {
		return ERR_INVALID_DATA;
	}
	String validation_error;
	int64_t integer = 0;
	bool boolean = false;
	if ((arguments.has("debuggerSession") && !_read_integer(arguments, "debuggerSession", 0, 0, INT32_MAX, integer, validation_error)) ||
			(arguments.has("timeoutMs") && !_read_integer(arguments, "timeoutMs", 500, 50, 1500, integer, validation_error)) ||
			(arguments.has("maxResults") && !_read_integer(arguments, "maxResults", 64, 1, 256, integer, validation_error)) ||
			(arguments.has("includeCounts") && !_read_bool(arguments, "includeCounts", true, boolean, validation_error)) ||
			(arguments.has("crop") && arguments["crop"].get_type() != Variant::DICTIONARY)) {
		return _fail(validation_error.is_empty() ? "crop must be an object." : validation_error, r_error);
	}
	r_step = _make_step(p_index, "command", p_command, tool, arguments, generation_required);
	r_step["expandedOperationCount"] = 1;
	r_operations = 1;
	return OK;
}

static Error _compile_expectation(const Dictionary &p_step, int p_index, Dictionary &r_step, String *r_error) {
	String expect;
	String error;
	if (!_read_string(p_step, "expect", String(), expect, error, false)) {
		return _fail(error, r_error);
	}
	PackedStringArray allowed{ "expect", "timeoutFrames", "pollEveryFrames", "captureOnError" };
	Dictionary arguments;
	Dictionary assertion;
	String tool;
	if (expect == "node_exists" || expect == "node_gone") {
		allowed.push_back("selector");
		if (!p_step.has("selector") || _validate_selector(p_step["selector"], expect + ".selector", r_error) != OK) {
			return _fail(expect + " requires a selector object.", r_error);
		}
		tool = "godot.runtime.wait.start";
		Dictionary condition;
		condition["kind"] = expect;
		condition["selector"] = p_step["selector"];
		arguments["condition"] = condition;
		assertion["kind"] = "match_count";
		assertion["operator"] = expect == "node_exists" ? "greater_than" : "equals";
		assertion["value"] = 0;
	} else if (expect == "node_visible" || expect == "node_hidden" || expect == "property_equals") {
		allowed.push_back("selector");
		if (!p_step.has("selector") || _validate_selector(p_step["selector"], expect + ".selector", r_error) != OK) {
			return _fail(expect + " requires a selector object.", r_error);
		}
		tool = "godot.runtime.wait.start";
		Dictionary condition;
		condition["kind"] = "property";
		condition["selector"] = p_step["selector"];
		assertion["kind"] = "property_equals";
		if (expect == "property_equals") {
			allowed.append_array(PackedStringArray{ "property", "equals" });
			String property;
			if (!_read_string(p_step, "property", String(), property, error, false) || !p_step.has("equals")) {
				return _fail(error.is_empty() ? "property_equals requires property and equals." : error, r_error);
			}
			assertion["property"] = property;
			assertion["value"] = p_step["equals"];
			condition["property"] = property;
			condition["equals"] = p_step["equals"];
		} else {
			assertion["property"] = "visible";
			assertion["value"] = expect == "node_visible";
			condition["property"] = "visible";
			condition["equals"] = expect == "node_visible";
		}
		arguments["condition"] = condition;
	} else if (expect == "scene_changed") {
		tool = "godot.runtime.wait.start";
		arguments["condition"] = Dictionary{ { "kind", "scene_changed" } };
		assertion["kind"] = "scene_changed";
	} else if (expect == "screenshot_diff") {
		allowed.push_back("threshold");
		tool = "godot.runtime.wait.start";
		assertion["kind"] = "screenshot_diff";
		double threshold = 0.05;
		if (!_read_number(p_step, "threshold", threshold, 0.001, 1.0, threshold, error)) {
			return _fail(error, r_error);
		}
		assertion["threshold"] = threshold;
		arguments["condition"] = Dictionary{ { "kind", "screenshot_diff" }, { "threshold", threshold } };
	} else {
		return _fail("Unsupported expect alias: " + expect + ".", r_error);
	}

	String unknown;
	if (!_has_only(p_step, allowed, unknown)) {
		return _fail("Unknown " + expect + " field: " + unknown + ".", r_error);
	}
	int64_t timeout_frames = 300;
	int64_t poll_every_frames = 1;
	bool capture_on_error = false;
	if (!_read_integer(p_step, "timeoutFrames", timeout_frames, 1, MCPHarnessPlanCompiler::MAX_TIMEOUT_FRAMES, timeout_frames, error) ||
			!_read_integer(p_step, "pollEveryFrames", poll_every_frames, 1, MCPHarnessPlanCompiler::MAX_TIMEOUT_FRAMES, poll_every_frames, error) ||
			!_read_bool(p_step, "captureOnError", false, capture_on_error, error)) {
		return _fail(error, r_error);
	}
	Dictionary polling;
	polling["timeoutFrames"] = timeout_frames;
	polling["pollEveryFrames"] = poll_every_frames;
	polling["captureOnError"] = capture_on_error;
	arguments["timeoutFrames"] = timeout_frames;
	arguments["pollEveryFrames"] = poll_every_frames;
	r_step = _make_step(p_index, "expect", expect, tool, arguments, true);
	r_step["assertion"] = assertion;
	r_step["poll"] = polling;
	r_step["expandedOperationCount"] = 1;
	return OK;
}

static Error _validate_startup(const Variant &p_value, bool &r_perf, String *r_error) {
	r_perf = false;
	if (p_value.get_type() == Variant::NIL) {
		return OK;
	}
	if (p_value.get_type() != Variant::DICTIONARY) {
		return _fail("startup must be an object.", r_error);
	}
	const Dictionary startup = p_value;
	String unknown;
	if (!_has_only(startup, PackedStringArray{ "perf" }, unknown)) {
		return _fail("startup." + unknown + " is CLI process startup state and is not supported by MCP harness plans.", r_error);
	}
	String error;
	if (!_read_bool(startup, "perf", false, r_perf, error)) {
		return _fail(error, r_error);
	}
	return OK;
}

static Error _validate_evidence(const Variant &p_value, Dictionary &r_evidence, String *r_error) {
	r_evidence = Dictionary{
		{ "screenshots", "on_failure" },
		{ "latestLog", false },
		{ "trace", true },
		{ "perfSummary", false },
	};
	if (p_value.get_type() == Variant::NIL) {
		return OK;
	}
	if (p_value.get_type() != Variant::DICTIONARY) {
		return _fail("evidence must be an object.", r_error);
	}
	const Dictionary evidence = p_value;
	String unknown;
	if (!_has_only(evidence, PackedStringArray{ "screenshots", "latestLog", "trace", "perfSummary" }, unknown)) {
		return _fail("Unknown evidence field: " + unknown + ".", r_error);
	}
	String screenshots;
	String error;
	bool latest_log = false;
	bool trace = true;
	bool perf_summary = false;
	if (!_read_string(evidence, "screenshots", "on_failure", screenshots, error) ||
			(screenshots != "off" && screenshots != "on_failure" && screenshots != "always") ||
			!_read_bool(evidence, "latestLog", false, latest_log, error) ||
			!_read_bool(evidence, "trace", true, trace, error) ||
			!_read_bool(evidence, "perfSummary", false, perf_summary, error)) {
		return _fail(error.is_empty() ? "evidence.screenshots must be off, on_failure, or always." : error, r_error);
	}
	r_evidence["screenshots"] = screenshots;
	r_evidence["latestLog"] = latest_log;
	r_evidence["trace"] = trace;
	r_evidence["perfSummary"] = perf_summary;
	return OK;
}

} // namespace

Error MCPHarnessPlanCompiler::compile(const Dictionary &p_script, Dictionary &r_plan, String *r_error) {
	r_plan = Dictionary();
	if (r_error) {
		*r_error = String();
	}
	String unknown;
	if (!_has_only(p_script, PackedStringArray{ "name", "startup", "steps", "evidence" }, unknown)) {
		return _fail("Unknown harness field: " + unknown + ".", r_error);
	}
	int64_t total_text = 0;
	if (_validate_text_budget(p_script, 0, total_text, r_error) != OK) {
		return ERR_INVALID_DATA;
	}
	String name;
	String error;
	if (!_read_string(p_script, "name", "harness", name, error, false)) {
		return _fail(error, r_error);
	}
	const Variant steps_value = p_script.get("steps", Variant());
	if (steps_value.get_type() != Variant::ARRAY) {
		return _fail("steps must be an array.", r_error);
	}
	const Array source_steps = steps_value;
	if (source_steps.is_empty() || source_steps.size() > MAX_SCRIPT_STEPS) {
		return _fail(vformat("steps must contain between 1 and %d entries.", MAX_SCRIPT_STEPS), r_error);
	}
	bool perf = false;
	Dictionary evidence;
	if (_validate_startup(p_script.get("startup", Variant()), perf, r_error) != OK ||
			_validate_evidence(p_script.get("evidence", Variant()), evidence, r_error) != OK) {
		return ERR_INVALID_DATA;
	}

	Array compiled_steps;
	if (perf) {
		evidence["perfSummary"] = true;
	}
	int total_operations = 0;
	if (perf) {
		total_operations++;
	}
	for (int i = 0; i < source_steps.size(); i++) {
		if (source_steps[i].get_type() != Variant::DICTIONARY) {
			return _fail(vformat("steps[%d] must be an object.", i), r_error);
		}
		const Dictionary source = source_steps[i];
		const bool has_command = source.has("cmd");
		const bool has_expectation = source.has("expect");
		if (has_command == has_expectation) {
			return _fail(vformat("steps[%d] must contain exactly one of cmd or expect.", i), r_error);
		}

		Dictionary compiled;
		int operations = 1;
		Error compile_error = OK;
		if (has_expectation) {
			compile_error = _compile_expectation(source, compiled_steps.size(), compiled, r_error);
		} else {
			String command;
			if (!_read_string(source, "cmd", String(), command, error, false)) {
				return _fail(vformat("steps[%d]: %s", i, error), r_error);
			}
			compile_error = _compile_input_alias(source, compiled_steps.size(), command, compiled, operations, r_error);
			if (compile_error == ERR_DOES_NOT_EXIST) {
				compile_error = _compile_runtime_command(source, compiled_steps.size(), command, compiled, operations, r_error);
			}
			if (compile_error == ERR_DOES_NOT_EXIST) {
				return _fail(vformat("steps[%d]: Unsupported command: %s.", i, command), r_error);
			}
		}
		if (compile_error != OK) {
			if (r_error && !r_error->begins_with("steps[")) {
				*r_error = vformat("steps[%d]: %s", i, *r_error);
			}
			return ERR_INVALID_DATA;
		}
		total_operations += operations;
		if (total_operations > MAX_SEQUENCE_STEPS) {
			return _fail("The compiled harness exceeds the operation limit.", r_error);
		}
		compiled_steps.push_back(compiled);
	}

	r_plan["schemaVersion"] = 1;
	r_plan["name"] = name;
	r_plan["evidencePolicy"] = evidence;
	r_plan["steps"] = compiled_steps;
	r_plan["stepCount"] = compiled_steps.size();
	r_plan["operationCount"] = total_operations;
	r_plan["runtimeContextRequired"] = true;
	return OK;
}
