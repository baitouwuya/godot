/**************************************************************************/
/*  mcp_input_map_event_codec.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
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

#include "mcp_input_map_event_codec.h"

#include "mcp_runtime_input.h"

#include "core/input/input_event_codec.h"

namespace {

static void _add_modifiers(Dictionary &r_result, const Ref<InputEventWithModifiers> &p_event) {
	r_result["shift"] = p_event->is_shift_pressed();
	r_result["ctrl"] = p_event->is_ctrl_pressed();
	r_result["alt"] = p_event->is_alt_pressed();
	r_result["meta"] = p_event->is_meta_pressed();
}

static void _normalize_mapping_event(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_valid()) {
		key->set_pressed(false);
		key->set_echo(false);
		return;
	}
	Ref<InputEventMouseButton> mouse_button = p_event;
	if (mouse_button.is_valid()) {
		mouse_button->set_pressed(false);
		mouse_button->set_double_click(false);
		return;
	}
	Ref<InputEventJoypadButton> joypad_button = p_event;
	if (joypad_button.is_valid()) {
		joypad_button->set_pressed(false);
	}
}

} // namespace

Error MCPInputMapEventCodec::decode(const Dictionary &p_event, Ref<InputEvent> &r_event, String *r_error) {
	r_event.unref();
	MCPRuntimeInput::EncodedEvent encoded;
	Error error = MCPRuntimeInput::encode(p_event, encoded, r_error);
	if (error != OK) {
		return error;
	}
	if (encoded.message != "scene:inject_input_event" || encoded.arguments.size() != 1 || encoded.arguments[0].get_type() != Variant::PACKED_BYTE_ARRAY) {
		if (r_error) {
			*r_error = "Input Map events must be key, mouse_button, joypad_button, or joypad_motion objects.";
		}
		return ERR_INVALID_DATA;
	}
	error = decode_input_event(encoded.arguments[0], r_event);
	if (error != OK || r_event.is_null() || !r_event->is_action_type()) {
		r_event.unref();
		if (r_error) {
			*r_error = "Input event is not supported by the project Input Map.";
		}
		return ERR_INVALID_DATA;
	}
	_normalize_mapping_event(r_event);
	return OK;
}

Dictionary MCPInputMapEventCodec::encode(const Ref<InputEvent> &p_event, bool &r_supported) {
	r_supported = true;
	Dictionary result;
	if (p_event.is_null()) {
		r_supported = false;
		return result;
	}
	result["device"] = p_event->get_device();

	Ref<InputEventKey> key = p_event;
	if (key.is_valid()) {
		result["type"] = "key";
		result["keycode"] = int64_t(key->get_keycode());
		result["physicalKeycode"] = int64_t(key->get_physical_keycode());
		result["keyLabel"] = int64_t(key->get_key_label());
		result["unicode"] = int64_t(key->get_unicode());
		result["location"] = int(key->get_location());
		_add_modifiers(result, key);
		return result;
	}
	Ref<InputEventMouseButton> mouse_button = p_event;
	if (mouse_button.is_valid()) {
		result["type"] = "mouse_button";
		result["buttonIndex"] = int(mouse_button->get_button_index());
		_add_modifiers(result, mouse_button);
		return result;
	}
	Ref<InputEventJoypadButton> joypad_button = p_event;
	if (joypad_button.is_valid()) {
		result["type"] = "joypad_button";
		result["buttonIndex"] = int(joypad_button->get_button_index());
		return result;
	}
	Ref<InputEventJoypadMotion> motion = p_event;
	if (motion.is_valid()) {
		result["type"] = "joypad_motion";
		result["axis"] = int(motion->get_axis());
		result["axisValue"] = motion->get_axis_value();
		return result;
	}
	result.clear();
	r_supported = false;
	return result;
}
