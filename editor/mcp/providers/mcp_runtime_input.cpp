/**************************************************************************/
/*  mcp_runtime_input.cpp                                                 */
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

#include "mcp_runtime_input.h"

#include "mcp_tool_utils.h"

#include "core/input/input_event.h"
#include "core/input/input_event_codec.h"
#include "core/input/input_map.h"

namespace {

void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

bool _read_bool(const Dictionary &p_input, const StringName &p_name, bool p_default, bool &r_value, String *r_error) {
	const Variant value = p_input.get(p_name, p_default);
	if (value.get_type() != Variant::BOOL) {
		_set_error(r_error, String(p_name) + " must be boolean.");
		return false;
	}
	r_value = value;
	return true;
}

bool _read_integer(const Dictionary &p_input, const StringName &p_name, int64_t p_default, int64_t p_minimum,
		int64_t p_maximum, int64_t &r_value, String *r_error) {
	if (!MCPToolUtils::try_get_json_integer(p_input.get(p_name, p_default), p_minimum, p_maximum, r_value)) {
		_set_error(r_error, String(p_name) + " must be an integer inside its allowed range.");
		return false;
	}
	return true;
}

bool _read_number(const Dictionary &p_input, const StringName &p_name, double p_default, double p_minimum, double p_maximum, double &r_value, String *r_error) {
	const Variant value = p_input.get(p_name, p_default);
	if (value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) {
		_set_error(r_error, String(p_name) + " must be numeric.");
		return false;
	}
	r_value = value;
	if (!Math::is_finite(r_value) || r_value < p_minimum || r_value > p_maximum) {
		_set_error(r_error, String(p_name) + " is outside its allowed range.");
		return false;
	}
	return true;
}

bool _read_vector2(const Dictionary &p_input, const StringName &p_name, const Vector2 &p_default, Vector2 &r_value, String *r_error) {
	if (!p_input.has(p_name)) {
		r_value = p_default;
		return true;
	}
	const Variant value = p_input[p_name];
	if (value.get_type() != Variant::ARRAY) {
		_set_error(r_error, String(p_name) + " must be a two-number array.");
		return false;
	}
	const Array values = value;
	if (values.size() != 2 || (values[0].get_type() != Variant::INT && values[0].get_type() != Variant::FLOAT) ||
			(values[1].get_type() != Variant::INT && values[1].get_type() != Variant::FLOAT)) {
		_set_error(r_error, String(p_name) + " must be a two-number array.");
		return false;
	}
	r_value = Vector2(values[0], values[1]);
	if (!r_value.is_finite()) {
		_set_error(r_error, String(p_name) + " must contain finite numbers.");
		return false;
	}
	return true;
}

bool _apply_modifiers(const Dictionary &p_input, const Ref<InputEventWithModifiers> &p_event, String *r_error) {
	bool shift = false;
	bool alt = false;
	bool ctrl = false;
	bool meta = false;
	if (!_read_bool(p_input, "shift", false, shift, r_error) || !_read_bool(p_input, "alt", false, alt, r_error) ||
			!_read_bool(p_input, "ctrl", false, ctrl, r_error) || !_read_bool(p_input, "meta", false, meta, r_error)) {
		return false;
	}
	p_event->set_shift_pressed(shift);
	p_event->set_alt_pressed(alt);
	p_event->set_ctrl_pressed(ctrl);
	p_event->set_meta_pressed(meta);
	return true;
}

bool _apply_mouse(const Dictionary &p_input, const Ref<InputEventMouse> &p_event, String *r_error) {
	Vector2 position;
	int64_t button_mask = 0;
	if (!_read_vector2(p_input, "position", Vector2(), position, r_error) ||
			!_read_integer(p_input, "buttonMask", 0, 0, UINT8_MAX, button_mask, r_error) ||
			!_apply_modifiers(p_input, p_event, r_error)) {
		return false;
	}
	p_event->set_position(position);
	p_event->set_global_position(position);
	p_event->set_button_mask(BitField<MouseButtonMask>(uint32_t(button_mask)));
	return true;
}

bool _set_device(const Dictionary &p_input, const Ref<InputEvent> &p_event, String *r_error) {
	int64_t device = 0;
	if (!_read_integer(p_input, "device", 0, -1, INT32_MAX, device, r_error)) {
		return false;
	}
	p_event->set_device(int(device));
	return true;
}

Error _encode_event(const Ref<InputEvent> &p_event, const String &p_type, MCPRuntimeInput::EncodedEvent &r_event, String *r_error) {
	PackedByteArray data;
	if (!encode_input_event(p_event, data)) {
		_set_error(r_error, "The input event type cannot be encoded by Godot's debugger codec.");
		return ERR_UNAVAILABLE;
	}
	r_event.message = "scene:inject_input_event";
	r_event.arguments = Array{ data };
	r_event.type = p_type;
	return OK;
}

Error _encode_release_event(const Ref<InputEvent> &p_event, MCPRuntimeInput::EncodedEvent &r_event, String *r_error) {
	PackedByteArray data;
	if (!encode_input_event(p_event, data)) {
		_set_error(r_error, "The input release event cannot be encoded by Godot's debugger codec.");
		return ERR_UNAVAILABLE;
	}
	r_event.release_message = "scene:inject_input_event";
	r_event.release_arguments = Array{ data };
	return OK;
}

} // namespace

Error MCPRuntimeInput::encode(const Dictionary &p_input, EncodedEvent &r_event, String *r_error, bool p_require_explicit_state) {
	r_event = EncodedEvent();
	if (r_error) {
		*r_error = String();
	}
	const Variant type_value = p_input.get("type", Variant());
	if (type_value.get_type() != Variant::STRING || String(type_value).is_empty()) {
		_set_error(r_error, "type must be a non-empty string.");
		return ERR_INVALID_DATA;
	}
	const String type = type_value;

	if (type == "action") {
		const Variant action_value = p_input.get("action", Variant());
		bool pressed = true;
		double strength = 1.0;
		if (p_require_explicit_state && !p_input.has("pressed")) {
			_set_error(r_error, "pressed is required for action events.");
			return ERR_INVALID_DATA;
		}
		if (action_value.get_type() != Variant::STRING || String(action_value).is_empty() ||
				!_read_bool(p_input, "pressed", true, pressed, r_error) ||
				!_read_number(p_input, "strength", pressed ? 1.0 : 0.0, 0.0, 1.0, strength, r_error)) {
			if (action_value.get_type() != Variant::STRING || String(action_value).is_empty()) {
				_set_error(r_error, "action must be a non-empty string.");
			}
			return ERR_INVALID_DATA;
		}
		if (!InputMap::get_singleton() || !InputMap::get_singleton()->has_action(StringName(action_value))) {
			_set_error(r_error, "Unknown project input action: " + String(action_value));
			return ERR_DOES_NOT_EXIST;
		}
		r_event.message = "scene:inject_input_action";
		r_event.arguments = Array{ StringName(action_value), pressed, strength };
		r_event.type = type;
		r_event.state_key = "action:" + String(action_value);
		r_event.release_message = r_event.message;
		r_event.release_arguments = Array{ StringName(action_value), false, 0.0 };
		r_event.stateful = true;
		r_event.active = pressed;
		return OK;
	}

	if (type == "key") {
		int64_t keycode = 0;
		int64_t physical_keycode = 0;
		int64_t unicode = 0;
		int64_t location = 0;
		bool pressed = true;
		bool echo = false;
		if (p_require_explicit_state && !p_input.has("pressed")) {
			_set_error(r_error, "pressed is required for key events.");
			return ERR_INVALID_DATA;
		}
		if (!_read_integer(p_input, "keycode", 0, 0, UINT32_MAX, keycode, r_error) ||
				!_read_integer(p_input, "physicalKeycode", 0, 0, UINT32_MAX, physical_keycode, r_error) ||
				!_read_integer(p_input, "unicode", 0, 0, UINT32_MAX, unicode, r_error) ||
				!_read_integer(p_input, "location", 0, 0, int64_t(KeyLocation::RIGHT), location, r_error) ||
				!_read_bool(p_input, "pressed", true, pressed, r_error) || !_read_bool(p_input, "echo", false, echo, r_error)) {
			return ERR_INVALID_DATA;
		}
		Ref<InputEventKey> event;
		event.instantiate();
		event->set_keycode(Key(keycode));
		event->set_physical_keycode(Key(physical_keycode));
		event->set_unicode(char32_t(unicode));
		event->set_location(KeyLocation(location));
		event->set_pressed(pressed);
		event->set_echo(echo);
		if (!_set_device(p_input, event, r_error) || !_apply_modifiers(p_input, event, r_error)) {
			return ERR_INVALID_DATA;
		}
		Error error = _encode_event(event, type, r_event, r_error);
		if (error != OK) {
			return error;
		}
		Ref<InputEventKey> release = event->duplicate();
		release->set_pressed(false);
		release->set_echo(false);
		error = _encode_release_event(release, r_event, r_error);
		const int64_t identity = physical_keycode != 0 ? physical_keycode : keycode;
		r_event.state_key = vformat("key:%d:%d:%d", event->get_device(), location, identity);
		r_event.stateful = true;
		r_event.active = pressed;
		return error;
	}

	if (type == "mouse_button") {
		int64_t button_index = 0;
		bool pressed = true;
		bool double_click = false;
		if (p_require_explicit_state && !p_input.has("pressed")) {
			_set_error(r_error, "pressed is required for mouse_button events.");
			return ERR_INVALID_DATA;
		}
		if (!_read_integer(p_input, "buttonIndex", 0, 1, 9, button_index, r_error) ||
				!_read_bool(p_input, "pressed", true, pressed, r_error) ||
				!_read_bool(p_input, "doubleClick", false, double_click, r_error)) {
			return ERR_INVALID_DATA;
		}
		Ref<InputEventMouseButton> event;
		event.instantiate();
		event->set_button_index(MouseButton(button_index));
		event->set_pressed(pressed);
		event->set_double_click(double_click);
		if (!_set_device(p_input, event, r_error) || !_apply_mouse(p_input, event, r_error)) {
			return ERR_INVALID_DATA;
		}
		Error error = _encode_event(event, type, r_event, r_error);
		if (error != OK) {
			return error;
		}
		Ref<InputEventMouseButton> release = event->duplicate();
		release->set_pressed(false);
		release->set_double_click(false);
		error = _encode_release_event(release, r_event, r_error);
		r_event.state_key = vformat("mouse_button:%d:%d", event->get_device(), button_index);
		r_event.stateful = true;
		r_event.active = pressed;
		return error;
	}

	if (type == "mouse_motion") {
		Vector2 relative;
		double pressure = 0.0;
		if (!_read_vector2(p_input, "relative", Vector2(), relative, r_error) ||
				!_read_number(p_input, "pressure", 0.0, 0.0, 1.0, pressure, r_error)) {
			return ERR_INVALID_DATA;
		}
		Ref<InputEventMouseMotion> event;
		event.instantiate();
		event->set_relative(relative);
		event->set_pressure(float(pressure));
		if (!_set_device(p_input, event, r_error) || !_apply_mouse(p_input, event, r_error)) {
			return ERR_INVALID_DATA;
		}
		return _encode_event(event, type, r_event, r_error);
	}

	if (type == "joypad_button") {
		int64_t button_index = 0;
		bool pressed = true;
		if (p_require_explicit_state && !p_input.has("pressed")) {
			_set_error(r_error, "pressed is required for joypad_button events.");
			return ERR_INVALID_DATA;
		}
		if (!_read_integer(p_input, "buttonIndex", 0, 0, int64_t(JoyButton::MAX) - 1, button_index, r_error) ||
				!_read_bool(p_input, "pressed", true, pressed, r_error)) {
			return ERR_INVALID_DATA;
		}
		Ref<InputEventJoypadButton> event;
		event.instantiate();
		event->set_button_index(JoyButton(button_index));
		event->set_pressed(pressed);
		if (!_set_device(p_input, event, r_error)) {
			return ERR_INVALID_DATA;
		}
		Error error = _encode_event(event, type, r_event, r_error);
		if (error != OK) {
			return error;
		}
		Ref<InputEventJoypadButton> release = event->duplicate();
		release->set_pressed(false);
		error = _encode_release_event(release, r_event, r_error);
		r_event.state_key = vformat("joypad_button:%d:%d", event->get_device(), button_index);
		r_event.stateful = true;
		r_event.active = pressed;
		return error;
	}

	if (type == "joypad_motion") {
		int64_t axis = 0;
		double axis_value = 0.0;
		if (p_require_explicit_state && !p_input.has("axisValue")) {
			_set_error(r_error, "axisValue is required for joypad_motion events.");
			return ERR_INVALID_DATA;
		}
		if (!_read_integer(p_input, "axis", 0, 0, int64_t(JoyAxis::MAX) - 1, axis, r_error) ||
				!_read_number(p_input, "axisValue", 0.0, -1.0, 1.0, axis_value, r_error)) {
			return ERR_INVALID_DATA;
		}
		Ref<InputEventJoypadMotion> event;
		event.instantiate();
		event->set_axis(JoyAxis(axis));
		event->set_axis_value(float(axis_value));
		if (!_set_device(p_input, event, r_error)) {
			return ERR_INVALID_DATA;
		}
		Error error = _encode_event(event, type, r_event, r_error);
		if (error != OK) {
			return error;
		}
		Ref<InputEventJoypadMotion> release = event->duplicate();
		release->set_axis_value(0.0f);
		error = _encode_release_event(release, r_event, r_error);
		r_event.state_key = vformat("joypad_motion:%d:%d", event->get_device(), axis);
		r_event.stateful = true;
		r_event.active = !Math::is_zero_approx(axis_value);
		return error;
	}

	if (type == "pan" || type == "magnify") {
		Vector2 position;
		if (!_read_vector2(p_input, "position", Vector2(), position, r_error)) {
			return ERR_INVALID_DATA;
		}
		if (type == "pan") {
			Vector2 delta;
			if (!_read_vector2(p_input, "delta", Vector2(), delta, r_error)) {
				return ERR_INVALID_DATA;
			}
			Ref<InputEventPanGesture> event;
			event.instantiate();
			event->set_position(position);
			event->set_delta(delta);
			if (!_set_device(p_input, event, r_error) || !_apply_modifiers(p_input, event, r_error)) {
				return ERR_INVALID_DATA;
			}
			return _encode_event(event, type, r_event, r_error);
		}
		double factor = 1.0;
		if (!_read_number(p_input, "factor", 1.0, 0.001, 1000.0, factor, r_error)) {
			return ERR_INVALID_DATA;
		}
		Ref<InputEventMagnifyGesture> event;
		event.instantiate();
		event->set_position(position);
		event->set_factor(float(factor));
		if (!_set_device(p_input, event, r_error) || !_apply_modifiers(p_input, event, r_error)) {
			return ERR_INVALID_DATA;
		}
		return _encode_event(event, type, r_event, r_error);
	}

	_set_error(r_error, "Unsupported input event type: " + type);
	return ERR_INVALID_DATA;
}

Error MCPRuntimeInput::encode_binary_state(const Dictionary &p_input, bool p_pressed, EncodedEvent &r_event, String *r_error) {
	const Variant type_value = p_input.get("type", Variant());
	if (type_value.get_type() != Variant::STRING) {
		_set_error(r_error, "type must be a non-empty string.");
		return ERR_INVALID_DATA;
	}
	const String type = type_value;
	if (type != "action" && type != "key" && type != "mouse_button" && type != "joypad_button") {
		_set_error(r_error, "tap and hold support action, key, mouse_button, or joypad_button events.");
		return ERR_INVALID_DATA;
	}
	Dictionary input = p_input.duplicate();
	input["pressed"] = p_pressed;
	if (type == "action" && !p_pressed) {
		input["strength"] = 0.0;
	}
	return encode(input, r_event, r_error, true);
}
