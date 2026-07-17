/**************************************************************************/
/*  mcp_runtime_target_action.cpp                                         */
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

#include "mcp_runtime_target_action.h"

#include "core/os/keyboard.h"

namespace {

void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

Array _position(const Vector2 &p_position) {
	return Array{ p_position.x, p_position.y };
}

Dictionary _event_step(const Dictionary &p_event) {
	Dictionary step;
	step["event"] = p_event;
	return step;
}

Dictionary _wait_step(int p_frames) {
	Dictionary step;
	step["waitFrames"] = p_frames;
	return step;
}

Dictionary _mouse_motion(const Vector2 &p_position, const Vector2 &p_relative = Vector2(), int p_button_mask = 0) {
	Dictionary event;
	event["type"] = "mouse_motion";
	event["position"] = _position(p_position);
	event["relative"] = _position(p_relative);
	event["buttonMask"] = p_button_mask;
	return event;
}

Dictionary _mouse_button(const Vector2 &p_position, MouseButton p_button, bool p_pressed, bool p_double_click = false) {
	Dictionary event;
	event["type"] = "mouse_button";
	event["position"] = _position(p_position);
	event["buttonIndex"] = int(p_button);
	event["pressed"] = p_pressed;
	event["doubleClick"] = p_double_click;
	const bool held_button = p_button == MouseButton::LEFT || p_button == MouseButton::RIGHT || p_button == MouseButton::MIDDLE;
	event["buttonMask"] = p_pressed && held_button ? int(mouse_button_to_mask(p_button)) : 0;
	return event;
}

void _append_click(const Vector2 &p_position, MouseButton p_button, int p_press_frames, bool p_double_click, Array &r_steps) {
	r_steps.push_back(_event_step(_mouse_button(p_position, p_button, true, p_double_click)));
	r_steps.push_back(_wait_step(p_press_frames));
	r_steps.push_back(_event_step(_mouse_button(p_position, p_button, false)));
}

Dictionary _key_event(char32_t p_codepoint, bool p_pressed) {
	Key keycode = Key::NONE;
	if (p_codepoint == '\n' || p_codepoint == '\r') {
		keycode = Key::ENTER;
	} else if (p_codepoint == '\t') {
		keycode = Key::TAB;
	} else if (p_codepoint == '\b') {
		keycode = Key::BACKSPACE;
	}
	Dictionary event;
	event["type"] = "key";
	event["keycode"] = int64_t(keycode);
	event["physicalKeycode"] = int64_t(keycode);
	event["unicode"] = int64_t(p_codepoint);
	event["pressed"] = p_pressed;
	return event;
}

} // namespace

bool MCPRuntimeTargetAction::parse_position(const Variant &p_value, Vector2 &r_position) {
	if (p_value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array values = p_value;
	if (values.size() != 2 || !values[0].is_num() || !values[1].is_num()) {
		return false;
	}
	const double x = values[0];
	const double y = values[1];
	if (!Math::is_finite(x) || !Math::is_finite(y)) {
		return false;
	}
	r_position = Vector2(x, y);
	return true;
}

bool MCPRuntimeTargetAction::parse_button(const String &p_value, MouseButton &r_button) {
	if (p_value == "left") {
		r_button = MouseButton::LEFT;
	} else if (p_value == "right") {
		r_button = MouseButton::RIGHT;
	} else if (p_value == "middle") {
		r_button = MouseButton::MIDDLE;
	} else {
		return false;
	}
	return true;
}

Error MCPRuntimeTargetAction::build_hover(const Vector2 &p_position, Array &r_events) {
	r_events.clear();
	r_events.push_back(_mouse_motion(p_position));
	return OK;
}

Error MCPRuntimeTargetAction::build_click(const Vector2 &p_position, MouseButton p_button, int p_press_frames,
		bool p_double_click, int p_gap_frames, Array &r_steps, String *r_error) {
	r_steps.clear();
	if (p_button != MouseButton::LEFT && p_button != MouseButton::RIGHT && p_button != MouseButton::MIDDLE) {
		_set_error(r_error, "button must be left, right, or middle.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_press_frames < 1 || p_press_frames > MAX_CLICK_FRAMES ||
			(p_double_click && (p_gap_frames < 1 || p_gap_frames > MAX_CLICK_FRAMES))) {
		_set_error(r_error, vformat("click frame counts must be between 1 and %d.", MAX_CLICK_FRAMES));
		return ERR_INVALID_PARAMETER;
	}
	r_steps.push_back(_event_step(_mouse_motion(p_position)));
	_append_click(p_position, p_button, p_press_frames, false, r_steps);
	if (p_double_click) {
		r_steps.push_back(_wait_step(p_gap_frames));
		_append_click(p_position, p_button, p_press_frames, true, r_steps);
	}
	return OK;
}

Error MCPRuntimeTargetAction::build_drag(const Vector2 &p_from, const Vector2 &p_to, MouseButton p_button, int p_frames,
		Array &r_steps, String *r_error) {
	r_steps.clear();
	if (p_button != MouseButton::LEFT && p_button != MouseButton::RIGHT && p_button != MouseButton::MIDDLE) {
		_set_error(r_error, "button must be left, right, or middle.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_frames < 1 || p_frames > MAX_DRAG_FRAMES) {
		_set_error(r_error, vformat("frames must be between 1 and %d.", MAX_DRAG_FRAMES));
		return ERR_INVALID_PARAMETER;
	}
	const int button_mask = int(mouse_button_to_mask(p_button));
	r_steps.push_back(_event_step(_mouse_motion(p_from)));
	r_steps.push_back(_event_step(_mouse_button(p_from, p_button, true)));
	Vector2 previous = p_from;
	for (int i = 1; i <= p_frames; i++) {
		const Vector2 position = p_from.lerp(p_to, double(i) / double(p_frames));
		r_steps.push_back(_event_step(_mouse_motion(position, position - previous, button_mask)));
		r_steps.push_back(_wait_step(1));
		previous = position;
	}
	r_steps.push_back(_event_step(_mouse_button(p_to, p_button, false)));
	return OK;
}

Error MCPRuntimeTargetAction::build_text(const String &p_text, Array &r_steps, String *r_error) {
	r_steps.clear();
	if (p_text.is_empty() || p_text.length() > MAX_TEXT_LENGTH) {
		_set_error(r_error, vformat("text must contain between 1 and %d Unicode code points.", MAX_TEXT_LENGTH));
		return ERR_INVALID_PARAMETER;
	}
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t codepoint = p_text.unicode_at(i);
		if (codepoint < 0x20 && codepoint != '\n' && codepoint != '\r' && codepoint != '\t' && codepoint != '\b') {
			_set_error(r_error, "text contains an unsupported control character.");
			r_steps.clear();
			return ERR_INVALID_PARAMETER;
		}
		r_steps.push_back(_event_step(_key_event(codepoint, true)));
		r_steps.push_back(_event_step(_key_event(codepoint, false)));
	}
	return OK;
}

Error MCPRuntimeTargetAction::build_scroll(const Vector2 &p_position, const String &p_direction, int p_steps,
		Array &r_events, String *r_error) {
	r_events.clear();
	if (p_direction != "up" && p_direction != "down") {
		_set_error(r_error, "direction must be up or down.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_steps < 1 || p_steps > MAX_SCROLL_STEPS) {
		_set_error(r_error, vformat("steps must be between 1 and %d.", MAX_SCROLL_STEPS));
		return ERR_INVALID_PARAMETER;
	}
	const MouseButton button = p_direction == "up" ? MouseButton::WHEEL_UP : MouseButton::WHEEL_DOWN;
	r_events.push_back(_mouse_motion(p_position));
	for (int i = 0; i < p_steps; i++) {
		r_events.push_back(_mouse_button(p_position, button, true));
		r_events.push_back(_mouse_button(p_position, button, false));
	}
	return OK;
}
