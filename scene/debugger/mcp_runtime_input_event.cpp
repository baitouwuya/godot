/**************************************************************************/
/*  mcp_runtime_input_event.cpp                                           */
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

#include "mcp_runtime_input_event.h"

#include "core/input/input.h"
#include "core/input/input_event_codec.h"
#include "core/input/input_map.h"

namespace {

bool _has_only(const Dictionary &p_dictionary, const PackedStringArray &p_allowed, String &r_error) {
	const Array keys = p_dictionary.keys();
	for (const Variant &key_value : keys) {
		if (key_value.get_type() != Variant::STRING && key_value.get_type() != Variant::STRING_NAME) {
			r_error = "Input payload keys must be strings.";
			return false;
		}
		if (!p_allowed.has(String(key_value))) {
			r_error = "Unknown input payload property: " + String(key_value);
			return false;
		}
	}
	return true;
}

bool _is_number(const Variant &p_value) {
	return p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT;
}

void _dispatch_action(const StringName &p_action, bool p_pressed, float p_strength) {
	Ref<InputEventAction> event;
	event.instantiate();
	event->set_action(p_action);
	event->set_pressed(p_pressed);
	event->set_strength(p_strength);
	Input::get_singleton()->parse_input_event(event);
}

} // namespace

Error MCPRuntimeInputEvent::decode(const Dictionary &p_payload, MCPRuntimeInputEvent &r_event, String &r_error) {
	r_event = MCPRuntimeInputEvent();
	r_error = String();
	if (!_has_only(p_payload, PackedStringArray{ "kind", "message", "arguments" }, r_error) ||
			p_payload.get("kind", Variant()).get_type() != Variant::STRING || String(p_payload["kind"]) != "event" ||
			p_payload.get("message", Variant()).get_type() != Variant::STRING || p_payload.get("arguments", Variant()).get_type() != Variant::ARRAY) {
		if (r_error.is_empty()) {
			r_error = "An event payload requires kind=event, message, and arguments.";
		}
		return ERR_INVALID_DATA;
	}

	const String message = p_payload["message"];
	const Array arguments = p_payload["arguments"];
	if (message == "scene:inject_input_action") {
		if (arguments.size() != 3 || (arguments[0].get_type() != Variant::STRING && arguments[0].get_type() != Variant::STRING_NAME) ||
				arguments[1].get_type() != Variant::BOOL || !_is_number(arguments[2])) {
			r_error = "An input action requires action, pressed, and strength arguments.";
			return ERR_INVALID_DATA;
		}
		const String action_text = arguments[0];
		if (action_text.utf8().length() + 16 > MAX_ENCODED_EVENT_BYTES) {
			r_error = "The encoded input action exceeds the per-event size limit.";
			return ERR_OUT_OF_MEMORY;
		}
		const StringName action = action_text;
		const double strength_value = arguments[2];
		if (!InputMap::get_singleton() || !InputMap::get_singleton()->has_action(action) || !Math::is_finite(strength_value) || strength_value < 0.0 || strength_value > 1.0) {
			r_error = "The input action is unknown or has an invalid strength.";
			return ERR_INVALID_DATA;
		}
		r_event.action_event = true;
		r_event.action = action;
		r_event.pressed = arguments[1];
		r_event.strength = float(strength_value);
		r_event.stateful = true;
		r_event.active = r_event.pressed;
		r_event.state_key = "action:" + String(action);
		r_event.encoded_bytes = String(action).utf8().length() + 16;
		return OK;
	}

	if (message != "scene:inject_input_event" || arguments.size() != 1 || arguments[0].get_type() != Variant::PACKED_BYTE_ARRAY) {
		r_error = "Only Godot debugger input event or input action payloads are accepted.";
		return ERR_INVALID_DATA;
	}
	const PackedByteArray bytes = arguments[0];
	if (bytes.is_empty() || bytes.size() > MAX_ENCODED_EVENT_BYTES) {
		r_error = "The encoded input event is empty or exceeds the per-event size limit.";
		return ERR_OUT_OF_MEMORY;
	}
	if (decode_input_event(bytes, r_event.event) != OK || r_event.event.is_null()) {
		r_error = "The encoded input event is invalid.";
		return ERR_INVALID_DATA;
	}
	r_event.encoded_bytes = bytes.size();

	Ref<InputEventKey> key = r_event.event;
	if (key.is_valid()) {
		Ref<InputEventKey> release = key->duplicate();
		release->set_pressed(false);
		release->set_echo(false);
		r_event.release_event = release;
		r_event.stateful = true;
		r_event.active = key->is_pressed();
		const uint32_t identity = key->get_physical_keycode() != Key::NONE ? uint32_t(key->get_physical_keycode()) : uint32_t(key->get_keycode());
		if (identity == 0) {
			r_error = "A stateful key event requires keycode or physicalKeycode.";
			return ERR_INVALID_DATA;
		}
		r_event.state_key = vformat("key:%d:%d:%d", key->get_device(), int(key->get_location()), identity);
		return OK;
	}
	Ref<InputEventMouseButton> mouse_button = r_event.event;
	if (mouse_button.is_valid()) {
		Ref<InputEventMouseButton> release = mouse_button->duplicate();
		release->set_pressed(false);
		release->set_double_click(false);
		r_event.release_event = release;
		r_event.stateful = true;
		r_event.active = mouse_button->is_pressed();
		r_event.state_key = vformat("mouse_button:%d:%d", mouse_button->get_device(), int(mouse_button->get_button_index()));
		return OK;
	}
	Ref<InputEventJoypadButton> joypad_button = r_event.event;
	if (joypad_button.is_valid()) {
		Ref<InputEventJoypadButton> release = joypad_button->duplicate();
		release->set_pressed(false);
		r_event.release_event = release;
		r_event.stateful = true;
		r_event.active = joypad_button->is_pressed();
		r_event.state_key = vformat("joypad_button:%d:%d", joypad_button->get_device(), int(joypad_button->get_button_index()));
		return OK;
	}
	Ref<InputEventJoypadMotion> motion = r_event.event;
	if (motion.is_valid()) {
		Ref<InputEventJoypadMotion> release = motion->duplicate();
		release->set_axis_value(0.0f);
		r_event.release_event = release;
		r_event.stateful = true;
		r_event.active = !Math::is_zero_approx(motion->get_axis_value());
		r_event.analog = true;
		r_event.state_key = vformat("joypad_motion:%d:%d", motion->get_device(), int(motion->get_axis()));
		return OK;
	}
	if (Object::cast_to<InputEventMouseMotion>(*r_event.event) || Object::cast_to<InputEventPanGesture>(*r_event.event) || Object::cast_to<InputEventMagnifyGesture>(*r_event.event)) {
		return OK;
	}

	r_error = "The encoded input event type is not supported by MCP runtime control.";
	return ERR_INVALID_DATA;
}

void MCPRuntimeInputEvent::dispatch() const {
	if (action_event) {
		_dispatch_action(action, pressed, strength);
	} else if (event.is_valid()) {
		Input::get_singleton()->parse_input_event(event);
	}
}

void MCPRuntimeInputEvent::dispatch_release() const {
	if (action_event) {
		_dispatch_action(action, false, 0.0f);
	} else if (release_event.is_valid()) {
		Input::get_singleton()->parse_input_event(release_event);
	}
}
