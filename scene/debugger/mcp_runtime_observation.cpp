/**************************************************************************/
/*  mcp_runtime_observation.cpp                                           */
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

#include "mcp_runtime_observation.h"

#include "mcp_runtime_screenshot_capture.h"

#include "core/config/engine.h"
#include "scene/gui/control.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#ifndef _3D_DISABLED
#include "scene/3d/node_3d.h"
#endif

namespace {

constexpr int DEFAULT_MAX_RESULTS = 64;
constexpr int MAX_RESULTS = 256;
constexpr int MAX_VISITED_NODES = 100000;
constexpr int MAX_ACTION_VISITED_NODES = 20000;

void _set_error(String &r_code, String &r_message, const String &p_code, const String &p_message) {
	r_code = p_code;
	r_message = p_message;
}

Node *_resolve_content_scene_node(SceneTree *p_scene_tree) {
	Node *current_scene = p_scene_tree ? p_scene_tree->get_current_scene() : nullptr;
	if (!current_scene) {
		return nullptr;
	}
	Node *best = nullptr;
	int best_depth = -1;
	int visited = 0;
	Vector<Pair<Node *, int>> stack;
	stack.push_back(Pair<Node *, int>(current_scene, 0));
	while (!stack.is_empty() && visited++ < MAX_VISITED_NODES) {
		const Pair<Node *, int> entry = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		Node *node = entry.first;
		if (node != current_scene && !node->get_scene_file_path().is_empty() && entry.second > best_depth) {
			best = node;
			best_depth = entry.second;
		}
		for (int i = node->get_child_count() - 1; i >= 0; i--) {
			stack.push_back(Pair<Node *, int>(node->get_child(i), entry.second + 1));
		}
	}
	return best;
}

Error _read_max_results(const Dictionary &p_arguments, int &r_max_results, String &r_message) {
	r_max_results = DEFAULT_MAX_RESULTS;
	if (!p_arguments.has("maxResults")) {
		return OK;
	}
	const Variant value = p_arguments["maxResults"];
	if (value.get_type() != Variant::INT || int64_t(value) < 1 || int64_t(value) > MAX_RESULTS) {
		r_message = vformat("maxResults must be an integer between 1 and %d.", MAX_RESULTS);
		return ERR_INVALID_PARAMETER;
	}
	r_max_results = int(value);
	return OK;
}

Error _collect_snapshots(const MCPRuntimeObservation::Selector &p_selector, int p_max_results, bool p_interactables_only,
		Array &r_nodes, int &r_visited, String &r_message, int p_visit_limit = MAX_VISITED_NODES) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	Viewport *root = scene_tree ? scene_tree->get_root() : nullptr;
	if (!root) {
		r_message = "SceneTree root is unavailable.";
		return ERR_UNAVAILABLE;
	}
	if (!p_selector.path.is_empty()) {
		Node *node = root->get_node_or_null(NodePath(p_selector.path));
		r_visited = 1;
		if (node) {
			const Dictionary snapshot = MCPRuntimeObservation::build_node_snapshot(node, root);
			if (MCPRuntimeObservation::matches_snapshot(snapshot, p_selector) &&
					(!p_interactables_only || MCPRuntimeObservation::is_interactable(node, snapshot))) {
				r_nodes.push_back(snapshot);
			}
		}
		return OK;
	}

	Dictionary nearest_snapshot;
	double nearest_distance = 0.0;
	Vector<Node *> stack;
	stack.push_back(root);
	while (!stack.is_empty() && (p_selector.has_nearest_to_screen_point || r_nodes.size() < p_max_results)) {
		Node *node = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		if (++r_visited > p_visit_limit) {
			r_message = vformat("Runtime query exceeded the %d-node traversal limit.", p_visit_limit);
			return ERR_OUT_OF_MEMORY;
		}
		const Dictionary snapshot = MCPRuntimeObservation::build_node_snapshot(node, root);
		if (MCPRuntimeObservation::matches_snapshot(snapshot, p_selector) &&
				(!p_interactables_only || MCPRuntimeObservation::is_interactable(node, snapshot))) {
			if (p_selector.has_nearest_to_screen_point) {
				if (MCPRuntimeObservation::has_screen_position(snapshot)) {
					const double distance = MCPRuntimeObservation::get_screen_position(snapshot).distance_squared_to(p_selector.nearest_to_screen_point);
					if (nearest_snapshot.is_empty() || distance < nearest_distance) {
						nearest_snapshot = snapshot;
						nearest_distance = distance;
					}
				}
			} else {
				r_nodes.push_back(snapshot);
			}
		}
		for (int i = node->get_child_count() - 1; i >= 0; i--) {
			stack.push_back(node->get_child(i));
		}
	}
	if (!nearest_snapshot.is_empty()) {
		r_nodes.push_back(nearest_snapshot);
	}
	return OK;
}

Error _query_nodes(const Dictionary &p_arguments, bool p_interactables_only, Dictionary &r_result, String &r_code, String &r_message) {
	const Variant filter_value = p_arguments.get("filter", Dictionary());
	if (filter_value.get_type() != Variant::DICTIONARY) {
		_set_error(r_code, r_message, "INVALID_QUERY", "filter must be an object.");
		return ERR_INVALID_PARAMETER;
	}
	MCPRuntimeObservation::Selector selector;
	if (MCPRuntimeObservation::parse_selector(filter_value, selector, r_message) != OK) {
		r_code = "INVALID_QUERY";
		return ERR_INVALID_PARAMETER;
	}
	int max_results = DEFAULT_MAX_RESULTS;
	if (_read_max_results(p_arguments, max_results, r_message) != OK) {
		r_code = "INVALID_QUERY";
		return ERR_INVALID_PARAMETER;
	}
	Array nodes;
	int visited = 0;
	const Error error = _collect_snapshots(selector, max_results, p_interactables_only, nodes, visited, r_message);
	if (error != OK) {
		r_code = error == ERR_UNAVAILABLE ? "RUNTIME_SCENE_UNAVAILABLE" : "RUNTIME_QUERY_LIMIT";
		return error;
	}
	r_result["maxResults"] = max_results;
	r_result["count"] = nodes.size();
	r_result["visitedCount"] = visited;
	r_result["nodes"] = nodes;
	return OK;
}

Error _get_node_snapshot(const Dictionary &p_arguments, Dictionary &r_result, String &r_code, String &r_message) {
	const Variant selector_value = p_arguments.get("selector", Variant());
	if (selector_value.get_type() != Variant::DICTIONARY) {
		_set_error(r_code, r_message, "INVALID_QUERY", "selector must be a non-empty object.");
		return ERR_INVALID_PARAMETER;
	}
	MCPRuntimeObservation::Selector selector;
	if (MCPRuntimeObservation::parse_selector(selector_value, selector, r_message) != OK || selector.is_empty()) {
		_set_error(r_code, r_message, "INVALID_QUERY", r_message.is_empty() ? "selector must not be empty." : r_message);
		return ERR_INVALID_PARAMETER;
	}
	Array nodes;
	int visited = 0;
	const Error error = _collect_snapshots(selector, 2, false, nodes, visited, r_message);
	if (error != OK) {
		r_code = error == ERR_UNAVAILABLE ? "RUNTIME_SCENE_UNAVAILABLE" : "RUNTIME_QUERY_LIMIT";
		return error;
	}
	if (nodes.is_empty()) {
		_set_error(r_code, r_message, "RUNTIME_NODE_NOT_FOUND", "No runtime node matched selector.");
		return ERR_DOES_NOT_EXIST;
	}
	if (nodes.size() > 1 && !selector.has_nearest_to_screen_point) {
		r_result["candidates"] = nodes;
		_set_error(r_code, r_message, "AMBIGUOUS_RUNTIME_NODE", "selector matched multiple runtime nodes.");
		return ERR_ALREADY_IN_USE;
	}
	r_result["node"] = nodes[0];
	return OK;
}

Error _get_viewport_summary(const Dictionary &p_arguments, Dictionary &r_result, String &r_code, String &r_message) {
	const Variant include_value = p_arguments.get("includeCounts", true);
	if (include_value.get_type() != Variant::BOOL) {
		_set_error(r_code, r_message, "INVALID_ARGUMENTS", "includeCounts must be boolean.");
		return ERR_INVALID_PARAMETER;
	}
	SceneTree *scene_tree = SceneTree::get_singleton();
	Viewport *root = scene_tree ? scene_tree->get_root() : nullptr;
	if (!root) {
		_set_error(r_code, r_message, "RUNTIME_SCENE_UNAVAILABLE", "SceneTree root viewport is unavailable.");
		return ERR_UNAVAILABLE;
	}
	Node *current_scene = scene_tree->get_current_scene();
	Node *content_scene = _resolve_content_scene_node(scene_tree);
	r_result["windowSize"] = Array{ root->get_visible_rect().size.x, root->get_visible_rect().size.y };
	r_result["rootPath"] = String(root->get_path());
	r_result["rootScene"] = current_scene ? String(current_scene->get_path()) : String();
	r_result["contentScene"] = content_scene ? String(content_scene->get_path()) : String(r_result["rootScene"]);
	r_result["activeCamera"] = MCPRuntimeObservation::get_active_camera_path(root);
	if (!bool(include_value)) {
		return OK;
	}
	int ui_targets = 0;
	int interactable_ui_targets = 0;
	int targets_3d = 0;
	int interactable_targets_3d = 0;
	int visited = 0;
	Vector<Node *> stack;
	stack.push_back(root);
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		if (++visited > MAX_VISITED_NODES) {
			_set_error(r_code, r_message, "RUNTIME_QUERY_LIMIT", vformat("Runtime summary exceeded the %d-node traversal limit.", MAX_VISITED_NODES));
			return ERR_OUT_OF_MEMORY;
		}
		const Dictionary snapshot = MCPRuntimeObservation::build_node_snapshot(node, root);
		const bool interactable = MCPRuntimeObservation::is_interactable(node, snapshot);
#ifndef _3D_DISABLED
		if (Object::cast_to<Node3D>(node)) {
			targets_3d++;
			interactable_targets_3d += interactable ? 1 : 0;
		} else
#endif
				if (Object::cast_to<Control>(node)) {
			ui_targets++;
			interactable_ui_targets += interactable ? 1 : 0;
		}
		for (int i = node->get_child_count() - 1; i >= 0; i--) {
			stack.push_back(node->get_child(i));
		}
	}
	r_result["uiTargets"] = ui_targets;
	r_result["interactableUiTargets"] = interactable_ui_targets;
	r_result["targets3D"] = targets_3d;
	r_result["interactableTargets3D"] = interactable_targets_3d;
	return OK;
}

Error _resolve_action_targets(const Dictionary &p_arguments, Dictionary &r_result, String &r_code, String &r_message) {
	const Variant kind_value = p_arguments.get("kind", Variant());
	const Variant selectors_value = p_arguments.get("selectors", Variant());
	if (kind_value.get_type() != Variant::STRING || selectors_value.get_type() != Variant::ARRAY) {
		_set_error(r_code, r_message, "INVALID_ARGUMENTS", "kind and selectors are required for target resolution.");
		return ERR_INVALID_PARAMETER;
	}
	const String kind = kind_value;
	const Array selectors = selectors_value;
	if ((kind != "pointer" && kind != "focus" && kind != "focus_text") || selectors.is_empty() || selectors.size() > 2 ||
			(kind != "pointer" && selectors.size() != 1)) {
		_set_error(r_code, r_message, "INVALID_ARGUMENTS", "Target resolution kind or selector count is invalid.");
		return ERR_INVALID_PARAMETER;
	}
	SceneTree *scene_tree = SceneTree::get_singleton();
	Viewport *root = scene_tree ? scene_tree->get_root() : nullptr;
	if (!root) {
		_set_error(r_code, r_message, "RUNTIME_SCENE_UNAVAILABLE", "SceneTree root viewport is unavailable.");
		return ERR_UNAVAILABLE;
	}
	Array targets;
	int total_visited = 0;
	for (int i = 0; i < selectors.size(); i++) {
		if (selectors[i].get_type() != Variant::DICTIONARY) {
			_set_error(r_code, r_message, "INVALID_QUERY", vformat("selectors[%d] must be an object.", i));
			return ERR_INVALID_PARAMETER;
		}
		MCPRuntimeObservation::Selector selector;
		if (MCPRuntimeObservation::parse_selector(selectors[i], selector, r_message) != OK || selector.is_empty()) {
			_set_error(r_code, r_message, "INVALID_QUERY", r_message.is_empty() ? vformat("selectors[%d] must not be empty.", i) : r_message);
			return ERR_INVALID_PARAMETER;
		}
		Array nodes;
		int visited = 0;
		const int remaining_budget = MAX_ACTION_VISITED_NODES - total_visited;
		if (remaining_budget <= 0) {
			_set_error(r_code, r_message, "RUNTIME_QUERY_LIMIT", "Target selectors exceeded the shared traversal budget.");
			return ERR_OUT_OF_MEMORY;
		}
		const Error query_error = _collect_snapshots(selector, 2, false, nodes, visited, r_message, remaining_budget);
		total_visited += visited;
		if (query_error != OK) {
			r_code = query_error == ERR_UNAVAILABLE ? "RUNTIME_SCENE_UNAVAILABLE" : "RUNTIME_QUERY_LIMIT";
			return query_error;
		}
		if (nodes.is_empty()) {
			_set_error(r_code, r_message, "RUNTIME_TARGET_NOT_FOUND", vformat("No runtime target matched selectors[%d].", i));
			return ERR_DOES_NOT_EXIST;
		}
		if (nodes.size() > 1 && !selector.has_nearest_to_screen_point) {
			r_result["candidates"] = nodes;
			_set_error(r_code, r_message, "AMBIGUOUS_RUNTIME_TARGET", vformat("selectors[%d] matched multiple runtime targets.", i));
			return ERR_ALREADY_IN_USE;
		}
		const Dictionary snapshot = nodes[0];
		Node *node = root->get_node_or_null(NodePath(String(snapshot["nodePath"])));
		Dictionary target;
		target["node"] = snapshot;
		if (kind == "focus" || kind == "focus_text") {
			Control *control = Object::cast_to<Control>(node);
			if (control) {
				control->grab_focus();
				if (root->gui_get_focus_owner() != control) {
					_set_error(r_code, r_message, "RUNTIME_TARGET_NOT_FOCUSABLE", "Resolved Control did not accept focus.");
					return ERR_UNAVAILABLE;
				}
				target["focused"] = true;
			} else if (kind == "focus_text") {
				_set_error(r_code, r_message, "RUNTIME_TARGET_NOT_FOCUSABLE", "Text input requires a focusable Control target.");
				return ERR_INVALID_PARAMETER;
			}
		}
		if (!bool(target.get("focused", false))) {
			if (!MCPRuntimeObservation::has_screen_position(snapshot)) {
				_set_error(r_code, r_message, "RUNTIME_TARGET_NOT_CLICKABLE", "Resolved target does not expose a usable screen position.");
				return ERR_UNAVAILABLE;
			}
			if (bool(snapshot.get("is3D", false)) && !MCPRuntimeObservation::is_interactable(node, snapshot)) {
				_set_error(r_code, r_message, "RUNTIME_TARGET_NOT_INTERACTABLE", "Resolved 3D target is not ray-pickable.");
				return ERR_UNAVAILABLE;
			}
			target["position"] = Array{ MCPRuntimeObservation::get_screen_position(snapshot).x,
				MCPRuntimeObservation::get_screen_position(snapshot).y };
		}
		targets.push_back(target);
	}
	r_result["kind"] = kind;
	r_result["targets"] = targets;
	return OK;
}

} // namespace

Error MCPRuntimeObservation::resolve_selector(const Dictionary &p_value, int p_max_results, int p_max_visited,
		Array &r_nodes, int &r_visited, String &r_error) {
	r_nodes.clear();
	r_visited = 0;
	if (p_max_results < 1 || p_max_results > MAX_RESULTS || p_max_visited < 1 || p_max_visited > MAX_VISITED_NODES) {
		r_error = "Runtime selector limits are invalid.";
		return ERR_INVALID_PARAMETER;
	}
	Selector selector;
	const Error parse_error = parse_selector(p_value, selector, r_error);
	if (parse_error != OK) {
		return parse_error;
	}
	return _collect_snapshots(selector, p_max_results, false, r_nodes, r_visited, r_error, p_max_visited);
}

Error MCPRuntimeObservation::execute(const String &p_operation, const Dictionary &p_arguments, Dictionary &r_result,
		String &r_error_code, String &r_error_message) {
	r_result.clear();
	r_error_code = String();
	r_error_message = String();
	Error error = ERR_INVALID_PARAMETER;
	if (p_operation == "query_nodes") {
		error = _query_nodes(p_arguments, false, r_result, r_error_code, r_error_message);
	} else if (p_operation == "get_interactables") {
		error = _query_nodes(p_arguments, true, r_result, r_error_code, r_error_message);
	} else if (p_operation == "get_node_snapshot") {
		error = _get_node_snapshot(p_arguments, r_result, r_error_code, r_error_message);
	} else if (p_operation == "get_viewport_summary") {
		error = _get_viewport_summary(p_arguments, r_result, r_error_code, r_error_message);
	} else if (p_operation == "get_screenshot") {
		error = MCPRuntimeScreenshotCapture::capture(p_arguments, r_result, r_error_code, r_error_message);
	} else if (p_operation == "resolve_action_targets") {
		error = _resolve_action_targets(p_arguments, r_result, r_error_code, r_error_message);
	} else {
		_set_error(r_error_code, r_error_message, "RUNTIME_OBSERVATION_UNSUPPORTED", "Unsupported runtime observation operation: " + p_operation);
	}
	if (error == OK) {
		r_result["frame"] = int64_t(Engine::get_singleton()->get_process_frames());
	}
	return error;
}
