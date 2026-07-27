/**************************************************************************/
/*  mcp_runtime_target_action_service.cpp                                */
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

#include "mcp_runtime_target_action_service.h"

#include "mcp_runtime_debugger_gateway.h"
#include "mcp_runtime_input_service.h"
#include "mcp_runtime_target_action.h"
#include "mcp_tool_utils.h"

namespace {

Dictionary _error(const String &p_code, const String &p_message) {
	return MCPToolUtils::make_error_result(p_code, p_message);
}

bool _is_error_result(const Dictionary &p_result) {
	return bool(p_result.get("isError", false));
}

bool _read_bounded_integer(const Dictionary &p_arguments, const StringName &p_name, int p_default, int p_minimum,
		int p_maximum, int &r_value, String &r_error) {
	int64_t value = p_default;
	if (p_arguments.has(p_name) && !MCPToolUtils::try_get_json_integer(p_arguments[p_name], p_minimum, p_maximum, value)) {
		r_error = vformat("%s must be an integer between %d and %d.", String(p_name), p_minimum, p_maximum);
		return false;
	}
	r_value = int(value);
	return true;
}

bool _read_string_argument(const Dictionary &p_arguments, const StringName &p_name, const String &p_default,
		String &r_value, String &r_error) {
	const Variant value = p_arguments.get(p_name, p_default);
	if (value.get_type() != Variant::STRING) {
		r_error = String(p_name) + " must be a string.";
		return false;
	}
	r_value = value;
	return true;
}

bool _read_resolved_target(const Dictionary &p_resolution, int p_index, Dictionary &r_target, Dictionary &r_node,
		Vector2 &r_position, bool &r_has_position) {
	const Dictionary content = p_resolution.get("structuredContent", Dictionary());
	const Variant targets_value = content.get("targets", Variant());
	if (targets_value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array targets = targets_value;
	if (p_index < 0 || p_index >= targets.size() || targets[p_index].get_type() != Variant::DICTIONARY) {
		return false;
	}
	r_target = targets[p_index];
	const Variant node_value = r_target.get("node", Variant());
	if (node_value.get_type() != Variant::DICTIONARY) {
		return false;
	}
	r_node = node_value;
	r_has_position = MCPRuntimeTargetAction::parse_position(r_target.get("position", Variant()), r_position);
	return true;
}

} // namespace

MCPRuntimeTargetActionService::MCPRuntimeTargetActionService(MCPRuntimeDebuggerGateway *p_runtime_gateway,
		MCPRuntimeInputService *p_input_service) {
	runtime_gateway = p_runtime_gateway;
	input_service = p_input_service;
}

Dictionary MCPRuntimeTargetActionService::_resolve_targets(const Dictionary &p_arguments, const String &p_kind,
		const Array &p_selectors) const {
	Dictionary payload;
	payload["kind"] = p_kind;
	payload["selectors"] = p_selectors;
	return runtime_gateway->request(p_arguments, "mcp_observation", "resolve_action_targets", payload, true);
}

Dictionary MCPRuntimeTargetActionService::click(const Dictionary &p_arguments, const String &p_mcp_session_id, bool p_double_click) {
	const Dictionary resolution = _resolve_targets(p_arguments, "pointer", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position) || !has_position) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid target coordinates.");
	}
	String button_name;
	String argument_error;
	int press_frames = 1;
	int gap_frames = 2;
	MouseButton button = MouseButton::LEFT;
	if (!_read_string_argument(p_arguments, "button", "left", button_name, argument_error) ||
			!MCPRuntimeTargetAction::parse_button(button_name, button) ||
			!_read_bounded_integer(p_arguments, "pressFrames", 1, 1, MCPRuntimeTargetAction::MAX_CLICK_FRAMES, press_frames, argument_error) ||
			(p_double_click && !_read_bounded_integer(p_arguments, "gapFrames", 2, 1, MCPRuntimeTargetAction::MAX_CLICK_FRAMES, gap_frames, argument_error))) {
		return _error("INVALID_ARGUMENTS", argument_error.is_empty() ? "button must be left, right, or middle." : argument_error);
	}
	Array steps;
	String action_error;
	if (MCPRuntimeTargetAction::build_click(position, button, press_frames, p_double_click, gap_frames, steps, &action_error) != OK) {
		return _error("INVALID_TARGET_ACTION", action_error);
	}
	Dictionary metadata;
	metadata["action"] = p_double_click ? "double_click_target" : "click_target";
	metadata["resolvedTarget"] = node;
	metadata["position"] = Array{ position.x, position.y };
	metadata["button"] = button_name;
	return input_service->start_target_sequence(p_arguments, p_mcp_session_id, steps, metadata);
}

Dictionary MCPRuntimeTargetActionService::hover(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Dictionary resolution = _resolve_targets(p_arguments, "pointer", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position) || !has_position) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid target coordinates.");
	}
	Array events;
	MCPRuntimeTargetAction::build_hover(position, events);
	Dictionary metadata;
	metadata["action"] = "hover_target";
	metadata["resolvedTarget"] = node;
	metadata["position"] = Array{ position.x, position.y };
	return input_service->dispatch_target_events(p_arguments, p_mcp_session_id, events, metadata);
}

Dictionary MCPRuntimeTargetActionService::focus(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Dictionary resolution = _resolve_targets(p_arguments, "focus", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position)) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid focus target data.");
	}
	Dictionary metadata;
	metadata["action"] = "focus_target";
	metadata["resolvedTarget"] = node;
	metadata["focused"] = bool(target.get("focused", false));
	if (bool(metadata["focused"])) {
		const Dictionary content = resolution.get("structuredContent", Dictionary());
		metadata["debuggerSession"] = content.get("debuggerSession", Variant());
		metadata["runtimeGeneration"] = content.get("runtimeGeneration", Variant());
		return MCPToolUtils::make_success_result(metadata);
	}
	if (!has_position) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The non-Control focus target has no pointer position.");
	}
	Array events;
	MCPRuntimeTargetAction::build_hover(position, events);
	metadata["position"] = Array{ position.x, position.y };
	return input_service->dispatch_target_events(p_arguments, p_mcp_session_id, events, metadata);
}

Dictionary MCPRuntimeTargetActionService::drag(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Array selectors{ p_arguments.get("fromSelector", Dictionary()), p_arguments.get("toSelector", Dictionary()) };
	const Dictionary resolution = _resolve_targets(p_arguments, "pointer", selectors);
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary from_target;
	Dictionary from_node;
	Dictionary to_target;
	Dictionary to_node;
	Vector2 from;
	Vector2 to;
	bool has_from = false;
	bool has_to = false;
	if (!_read_resolved_target(resolution, 0, from_target, from_node, from, has_from) || !has_from ||
			!_read_resolved_target(resolution, 1, to_target, to_node, to, has_to) || !has_to) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid drag target coordinates.");
	}
	String button_name;
	String argument_error;
	int frames = 20;
	MouseButton button = MouseButton::LEFT;
	if (!_read_string_argument(p_arguments, "button", "left", button_name, argument_error) ||
			!MCPRuntimeTargetAction::parse_button(button_name, button) ||
			!_read_bounded_integer(p_arguments, "frames", 20, 1, MCPRuntimeTargetAction::MAX_DRAG_FRAMES, frames, argument_error)) {
		return _error("INVALID_ARGUMENTS", argument_error.is_empty() ? "button must be left, right, or middle." : argument_error);
	}
	Array steps;
	String action_error;
	if (MCPRuntimeTargetAction::build_drag(from, to, button, frames, steps, &action_error) != OK) {
		return _error("INVALID_TARGET_ACTION", action_error);
	}
	Dictionary metadata;
	metadata["action"] = "drag_target_to_target";
	metadata["resolvedFromTarget"] = from_node;
	metadata["resolvedToTarget"] = to_node;
	metadata["from"] = Array{ from.x, from.y };
	metadata["to"] = Array{ to.x, to.y };
	metadata["frames"] = frames;
	metadata["button"] = button_name;
	return input_service->start_target_sequence(p_arguments, p_mcp_session_id, steps, metadata);
}

Dictionary MCPRuntimeTargetActionService::type_text(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Dictionary resolution = _resolve_targets(p_arguments, "focus_text", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position) || !bool(target.get("focused", false))) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project did not focus the text target.");
	}
	const Variant text_value = p_arguments.get("text", Variant());
	if (text_value.get_type() != Variant::STRING) {
		return _error("INVALID_ARGUMENTS", "text must be a string.");
	}
	Array steps;
	String action_error;
	if (MCPRuntimeTargetAction::build_text(text_value, steps, &action_error) != OK) {
		return _error("INVALID_ARGUMENTS", action_error);
	}
	Dictionary metadata;
	metadata["action"] = "type_text";
	metadata["resolvedTarget"] = node;
	metadata["textLength"] = String(text_value).length();
	return input_service->start_target_sequence(p_arguments, p_mcp_session_id, steps, metadata);
}

Dictionary MCPRuntimeTargetActionService::scroll(const Dictionary &p_arguments, const String &p_mcp_session_id) {
	const Dictionary resolution = _resolve_targets(p_arguments, "pointer", Array{ p_arguments.get("selector", Dictionary()) });
	if (_is_error_result(resolution)) {
		return resolution;
	}
	Dictionary target;
	Dictionary node;
	Vector2 position;
	bool has_position = false;
	if (!_read_resolved_target(resolution, 0, target, node, position, has_position) || !has_position) {
		return _error("RUNTIME_TARGET_DATA_INVALID", "The running project returned invalid scroll target coordinates.");
	}
	String direction;
	String argument_error;
	int steps = 1;
	if (!_read_string_argument(p_arguments, "direction", "down", direction, argument_error) ||
			!_read_bounded_integer(p_arguments, "steps", 1, 1, MCPRuntimeTargetAction::MAX_SCROLL_STEPS, steps, argument_error)) {
		return _error("INVALID_ARGUMENTS", argument_error);
	}
	Array events;
	String action_error;
	if (MCPRuntimeTargetAction::build_scroll(position, direction, steps, events, &action_error) != OK) {
		return _error("INVALID_ARGUMENTS", action_error);
	}
	Dictionary metadata;
	metadata["action"] = "scroll_view";
	metadata["resolvedTarget"] = node;
	metadata["position"] = Array{ position.x, position.y };
	metadata["direction"] = direction;
	metadata["steps"] = steps;
	return input_service->dispatch_target_events(p_arguments, p_mcp_session_id, events, metadata);
}
