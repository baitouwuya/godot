/**************************************************************************/
/*  mcp_runtime_observation.h                                             */
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
#include "core/math/rect2.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class Node;
class Viewport;

class MCPRuntimeObservation {
public:
	struct Selector {
		String path;
		String name;
		String type;
		String group;
		String text;
		bool has_is_3d = false;
		bool is_3d = false;
		bool has_visible = false;
		bool visible = false;
		bool has_screen_rect = false;
		Rect2 screen_rect;
		bool has_nearest_to_screen_point = false;
		Vector2 nearest_to_screen_point;

		bool is_empty() const;
	};

	static Error parse_selector(const Dictionary &p_value, Selector &r_selector, String &r_error);
	static Error resolve_selector(const Dictionary &p_value, int p_max_results, int p_max_visited,
			Array &r_nodes, int &r_visited, String &r_error);
	static bool matches_snapshot(const Dictionary &p_snapshot, const Selector &p_selector);
	static Dictionary build_node_snapshot(Node *p_node, Viewport *p_root_viewport);
	static bool has_screen_position(const Dictionary &p_snapshot);
	static Vector2 get_screen_position(const Dictionary &p_snapshot);
	static bool is_interactable(Node *p_node, const Dictionary &p_snapshot);
	static String get_active_camera_path(Viewport *p_viewport);
	static Error execute(const String &p_operation, const Dictionary &p_arguments, Dictionary &r_result,
			String &r_error_code, String &r_error_message);
};
