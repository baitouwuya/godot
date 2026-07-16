/**************************************************************************/
/*  mcp_runtime_input_sequence.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_runtime_input_sequence.h"

#include "mcp_tool_utils.h"

namespace {

void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

bool _has_only(const Dictionary &p_step, const PackedStringArray &p_allowed, String &r_error) {
	String unknown;
	if (MCPToolUtils::has_only_arguments(p_step, p_allowed, unknown)) {
		return true;
	}
	r_error = "Unknown step property: " + unknown;
	return false;
}

bool _read_wait(const Dictionary &p_step, const StringName &p_name, int64_t p_maximum, uint64_t &r_value, String &r_error) {
	int64_t value = 0;
	if (!MCPToolUtils::try_get_json_integer(p_step.get(p_name, Variant()), 1, p_maximum, value)) {
		r_error = String(p_name) + " must be a positive integer inside its allowed range.";
		return false;
	}
	r_value = uint64_t(value);
	return true;
}

} // namespace

Error MCPRuntimeInputSequence::compile(const Array &p_steps, Vector<Step> &r_steps, String *r_error) {
	r_steps.clear();
	if (r_error) {
		*r_error = String();
	}
	if (p_steps.is_empty() || p_steps.size() > MAX_SOURCE_STEPS) {
		_set_error(r_error, vformat("steps must contain between 1 and %d entries.", MAX_SOURCE_STEPS));
		return ERR_INVALID_DATA;
	}

	uint64_t total_wait_msec = 0;
	uint64_t total_wait_frames = 0;
	for (int i = 0; i < p_steps.size(); i++) {
		if (p_steps[i].get_type() != Variant::DICTIONARY) {
			_set_error(r_error, vformat("steps[%d] must be an object.", i));
			return ERR_INVALID_DATA;
		}
		const Dictionary source = p_steps[i];
		const bool has_event = source.has("event");
		const bool has_wait_msec = source.has("waitMs");
		const bool has_wait_frames = source.has("waitFrames");
		const bool has_tap = source.has("tap");
		const bool has_hold = source.has("hold");
		const int operation_count = int(has_event) + int(has_wait_msec) + int(has_wait_frames) + int(has_tap) + int(has_hold);
		if (operation_count != 1) {
			_set_error(r_error, vformat("steps[%d] must specify exactly one of event, waitMs, waitFrames, tap, or hold.", i));
			return ERR_INVALID_DATA;
		}

		String error;
		if (has_event) {
			if (!_has_only(source, PackedStringArray{ "event" }, error) || source["event"].get_type() != Variant::DICTIONARY) {
				_set_error(r_error, vformat("steps[%d]: %s", i, error.is_empty() ? "event must be an object." : error));
				return ERR_INVALID_DATA;
			}
			Step step;
			String input_error;
			if (MCPRuntimeInput::encode(source["event"], step.event, &input_error, true) != OK) {
				_set_error(r_error, vformat("steps[%d].event: %s", i, input_error));
				return ERR_INVALID_DATA;
			}
			r_steps.push_back(step);
		} else if (has_wait_msec || has_wait_frames) {
			const StringName property = has_wait_msec ? StringName("waitMs") : StringName("waitFrames");
			if (!_has_only(source, PackedStringArray{ property }, error)) {
				_set_error(r_error, vformat("steps[%d]: %s", i, error));
				return ERR_INVALID_DATA;
			}
			Step step;
			step.type = has_wait_msec ? STEP_WAIT_MSEC : STEP_WAIT_FRAMES;
			const int64_t maximum = has_wait_msec ? int64_t(MAX_TOTAL_WAIT_MSEC) : int64_t(MAX_TOTAL_WAIT_FRAMES);
			if (!_read_wait(source, property, maximum, step.amount, error)) {
				_set_error(r_error, vformat("steps[%d]: %s", i, error));
				return ERR_INVALID_DATA;
			}
			if (has_wait_msec) {
				total_wait_msec += step.amount;
			} else {
				total_wait_frames += step.amount;
			}
			r_steps.push_back(step);
		} else {
			const StringName operation = has_tap ? StringName("tap") : StringName("hold");
			if (!_has_only(source, PackedStringArray{ operation, "durationMs", "durationFrames" }, error) || source[operation].get_type() != Variant::DICTIONARY) {
				_set_error(r_error, vformat("steps[%d]: %s", i, error.is_empty() ? String(operation) + " must be an object." : error));
				return ERR_INVALID_DATA;
			}
			const bool duration_msec = source.has("durationMs");
			const bool duration_frames = source.has("durationFrames");
			if ((duration_msec && duration_frames) || (has_hold && !duration_msec && !duration_frames)) {
				_set_error(r_error, vformat("steps[%d] must specify exactly one durationMs or durationFrames for hold, and at most one for tap.", i));
				return ERR_INVALID_DATA;
			}

			Step press;
			Step release;
			String input_error;
			const Dictionary input = source[operation];
			if (input.has("pressed")) {
				_set_error(r_error, vformat("steps[%d].%s must omit pressed; the sequence controls both press and release.", i, String(operation)));
				return ERR_INVALID_DATA;
			}
			if (MCPRuntimeInput::encode_binary_state(input, true, press.event, &input_error) != OK ||
					MCPRuntimeInput::encode_binary_state(input, false, release.event, &input_error) != OK) {
				_set_error(r_error, vformat("steps[%d].%s: %s", i, String(operation), input_error));
				return ERR_INVALID_DATA;
			}

			Step wait;
			if (!duration_msec && !duration_frames) {
				wait.type = STEP_WAIT_MSEC;
				wait.amount = 50;
				total_wait_msec += wait.amount;
			} else {
				const StringName duration = duration_msec ? StringName("durationMs") : StringName("durationFrames");
				wait.type = duration_msec ? STEP_WAIT_MSEC : STEP_WAIT_FRAMES;
				const int64_t maximum = duration_msec ? int64_t(MAX_TOTAL_WAIT_MSEC) : int64_t(MAX_TOTAL_WAIT_FRAMES);
				if (!_read_wait(source, duration, maximum, wait.amount, error)) {
					_set_error(r_error, vformat("steps[%d]: %s", i, error));
					return ERR_INVALID_DATA;
				}
				if (duration_msec) {
					total_wait_msec += wait.amount;
				} else {
					total_wait_frames += wait.amount;
				}
			}
			r_steps.push_back(press);
			r_steps.push_back(wait);
			r_steps.push_back(release);
		}

		if (r_steps.size() > MAX_COMPILED_STEPS || total_wait_msec > MAX_TOTAL_WAIT_MSEC || total_wait_frames > MAX_TOTAL_WAIT_FRAMES) {
			_set_error(r_error, "The compiled sequence exceeds its step or total wait limit.");
			r_steps.clear();
			return ERR_OUT_OF_MEMORY;
		}
	}
	return OK;
}
