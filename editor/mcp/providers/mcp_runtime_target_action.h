/**************************************************************************/
/*  mcp_runtime_target_action.h                                           */
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

#include "core/error/error_list.h"
#include "core/input/input_enums.h"
#include "core/math/vector2.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class MCPRuntimeTargetAction {
public:
	static constexpr int MAX_TEXT_LENGTH = 100;
	static constexpr int MAX_DRAG_FRAMES = 60;
	static constexpr int MAX_SCROLL_STEPS = 20;
	static constexpr int MAX_CLICK_FRAMES = 10;

	static bool parse_position(const Variant &p_value, Vector2 &r_position);
	static bool parse_button(const String &p_value, MouseButton &r_button);
	static Error build_hover(const Vector2 &p_position, Array &r_events);
	static Error build_click(const Vector2 &p_position, MouseButton p_button, int p_press_frames,
			bool p_double_click, int p_gap_frames, Array &r_steps, String *r_error = nullptr);
	static Error build_drag(const Vector2 &p_from, const Vector2 &p_to, MouseButton p_button, int p_frames,
			Array &r_steps, String *r_error = nullptr);
	static Error build_text(const String &p_text, Array &r_steps, String *r_error = nullptr);
	static Error build_scroll(const Vector2 &p_position, const String &p_direction, int p_steps,
			Array &r_events, String *r_error = nullptr);
};
