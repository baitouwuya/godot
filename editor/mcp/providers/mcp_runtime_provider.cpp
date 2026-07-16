/**************************************************************************/
/*  mcp_runtime_provider.cpp                                              */
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

#include "mcp_runtime_provider.h"

#include "mcp_runtime_debug_service.h"
#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

Dictionary _property_schema(const String &p_type, const String &p_description) {
	Dictionary property;
	property["type"] = p_type;
	property["description"] = p_description;
	return property;
}

Dictionary _object_schema(const Dictionary &p_properties, const PackedStringArray &p_required = PackedStringArray()) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["required"] = p_required;
	schema["additionalProperties"] = false;
	return schema;
}

void _add_session_properties(Dictionary &r_properties, bool p_generation, bool p_timeout) {
	Dictionary debugger = _property_schema("integer", "Optional debugger session index. Required when multiple project instances are running.");
	debugger["minimum"] = 0;
	r_properties["debuggerSession"] = debugger;
	if (p_generation) {
		Dictionary generation = _property_schema("integer", "Runtime generation returned by godot.runtime.get_state or get_tree.");
		generation["minimum"] = 1;
		r_properties["runtimeGeneration"] = generation;
	}
	if (p_timeout) {
		Dictionary timeout = _property_schema("integer", "Maximum time to wait for a remote debugger response.");
		timeout["minimum"] = 50;
		timeout["maximum"] = 5000;
		timeout["default"] = 750;
		r_properties["timeoutMs"] = timeout;
	}
}

Dictionary _session_schema(bool p_require_generation, bool p_timeout) {
	Dictionary properties;
	_add_session_properties(properties, p_require_generation, p_timeout);
	PackedStringArray required;
	if (p_require_generation) {
		required.push_back("runtimeGeneration");
	}
	return _object_schema(properties, required);
}

Dictionary _play_schema() {
	Dictionary properties;
	Dictionary mode = _property_schema("string", "Run the project main scene, current edited scene, or a specific scene path.");
	mode["enum"] = PackedStringArray{ "main", "current", "scene" };
	mode["default"] = "main";
	properties["mode"] = mode;
	properties["path"] = _property_schema("string", "Project scene path required when mode is scene.");
	Dictionary restart = _property_schema("boolean", "Stop the current run before starting the requested scene.");
	restart["default"] = false;
	properties["restart"] = restart;
	return _object_schema(properties);
}

Dictionary _tree_schema() {
	Dictionary properties;
	_add_session_properties(properties, false, true);
	return _object_schema(properties);
}

Dictionary _properties_schema() {
	Dictionary properties;
	_add_session_properties(properties, false, true);
	properties["path"] = _property_schema("string", "Absolute runtime node path returned by godot.runtime.get_tree.");
	PackedStringArray required;
	required.push_back("path");
	return _object_schema(properties, required);
}

Dictionary _set_property_schema() {
	Dictionary properties;
	_add_session_properties(properties, true, true);
	properties["path"] = _property_schema("string", "Absolute runtime node path returned by godot.runtime.get_tree.");
	properties["property"] = _property_schema("string", "Exact runtime property name returned by get_properties.");
	Dictionary value;
	value["description"] = "JSON-native Variant value compatible with the property's type.";
	properties["value"] = value;
	PackedStringArray required;
	required.push_back("path");
	required.push_back("property");
	required.push_back("value");
	required.push_back("runtimeGeneration");
	return _object_schema(properties, required);
}

Dictionary _input_schema() {
	Dictionary properties;
	_add_session_properties(properties, true, false);
	Dictionary event;
	event["type"] = "object";
	event["description"] = "A validated action, key, mouse_button, mouse_motion, joypad_button, joypad_motion, pan, or magnify event.";
	Dictionary event_properties;
	event_properties["type"] = _property_schema("string", "Input event type.");
	event["properties"] = event_properties;
	event["required"] = PackedStringArray{ "type" };
	event["additionalProperties"] = true;
	Dictionary events = _property_schema("array", "Ordered input events to inject into the running project.");
	events["items"] = event;
	events["minItems"] = 1;
	events["maxItems"] = 128;
	properties["events"] = events;
	PackedStringArray required;
	required.push_back("events");
	required.push_back("runtimeGeneration");
	return _object_schema(properties, required);
}

Dictionary _input_sequence_schema() {
	Dictionary properties;
	_add_session_properties(properties, true, false);
	Dictionary step;
	step["type"] = "object";
	step["description"] = "Exactly one event, waitMs, waitFrames, tap, or hold operation. tap and hold accept durationMs or durationFrames.";
	step["additionalProperties"] = true;
	Dictionary steps = _property_schema("array", "Ordered asynchronous input operations.");
	steps["items"] = step;
	steps["minItems"] = 1;
	steps["maxItems"] = 256;
	properties["steps"] = steps;
	return _object_schema(properties, PackedStringArray{ "steps", "runtimeGeneration" });
}

Dictionary _input_sequence_status_schema() {
	Dictionary properties;
	properties["sequenceId"] = _property_schema("string", "Opaque sequence ID returned by godot.runtime.input.sequence.");
	return _object_schema(properties, PackedStringArray{ "sequenceId" });
}

Dictionary _input_sequence_cancel_schema() {
	Dictionary properties;
	_add_session_properties(properties, true, false);
	properties["sequenceId"] = _property_schema("string", "Opaque sequence ID returned by godot.runtime.input.sequence.");
	return _object_schema(properties, PackedStringArray{ "sequenceId", "runtimeGeneration" });
}

bool _resolve_mcp_session(const Dictionary &p_context, String &r_session_id, Dictionary &r_error) {
	r_session_id = String();
	const Variant session_value = p_context.get("session", Variant());
	if (session_value.get_type() == Variant::DICTIONARY) {
		const Variant id_value = Dictionary(session_value).get("sessionId", Variant());
		if (id_value.get_type() == Variant::STRING) {
			r_session_id = id_value;
		}
	}
	if (!r_session_id.is_empty()) {
		return true;
	}
	r_error = MCPToolUtils::make_error_result("MCP_SESSION_REQUIRED", "A valid MCP session is required for runtime input ownership.");
	return false;
}

Dictionary _unknown_argument(const String &p_name) {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown argument: " + p_name);
}

bool _has_only(const Dictionary &p_arguments, const PackedStringArray &p_allowed, Dictionary &r_error) {
	String unknown;
	if (MCPToolUtils::has_only_arguments(p_arguments, p_allowed, unknown)) {
		return true;
	}
	r_error = _unknown_argument(unknown);
	return false;
}

void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPRuntimeProvider::MCPRuntimeProvider(MCPRuntimeDebugService *p_runtime_service) {
	runtime_service = p_runtime_service;
}

MCPRuntimeProvider::~MCPRuntimeProvider() {
	unregister_tools();
}

Error MCPRuntimeProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry || !runtime_service) {
		_set_error(r_error, "A tool registry and runtime debug service are required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Runtime tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	struct ToolRegistration {
		const char *name;
		const char *description;
		Dictionary schema;
		Callable callable;
	};
	const ToolRegistration tools[] = {
		{ "godot.runtime.get_state", "Get editor run state and all active runtime debugger sessions.",
				_object_schema(Dictionary()), callable_mp(this, &MCPRuntimeProvider::_get_state) },
		{ "godot.runtime.play", "Run the main, current, or a specific project scene from the editor.",
				_play_schema(), callable_mp(this, &MCPRuntimeProvider::_play) },
		{ "godot.runtime.stop", "Stop the project currently launched by the editor.",
				_object_schema(Dictionary()), callable_mp(this, &MCPRuntimeProvider::_stop) },
		{ "godot.runtime.pause", "Suspend a running project's SceneTree without entering a script breakpoint.",
				_session_schema(true, false), callable_mp(this, &MCPRuntimeProvider::_pause) },
		{ "godot.runtime.resume", "Resume a SceneTree suspended through the debugger.",
				_session_schema(true, false), callable_mp(this, &MCPRuntimeProvider::_resume) },
		{ "godot.runtime.next_frame", "Advance one rendered frame while the running SceneTree is suspended.",
				_session_schema(true, false), callable_mp(this, &MCPRuntimeProvider::_next_frame) },
		{ "godot.runtime.get_tree", "Get a fresh running-project scene tree using Godot's remote debugger.",
				_tree_schema(), callable_mp(this, &MCPRuntimeProvider::_get_tree) },
		{ "godot.runtime.node.get_properties", "Get all inspectable runtime node properties, including script members and exports.",
				_properties_schema(), callable_mp(this, &MCPRuntimeProvider::_get_properties) },
		{ "godot.runtime.node.set_property", "Set and verify one runtime node property through Godot's remote inspector protocol.",
				_set_property_schema(), callable_mp(this, &MCPRuntimeProvider::_set_property) },
		{ "godot.runtime.input.send", "Inject validated action, keyboard, mouse, joypad, pan, and magnify input events into a running project.",
				_input_schema(), callable_mp(this, &MCPRuntimeProvider::_send_input) },
		{ "godot.runtime.input.sequence", "Run an asynchronous input sequence with millisecond or frame waits, tap, and hold operations.",
				_input_sequence_schema(), callable_mp(this, &MCPRuntimeProvider::_start_input_sequence) },
		{ "godot.runtime.input.sequence_status", "Read the current state of an input sequence owned by this MCP session.",
				_input_sequence_status_schema(), callable_mp(this, &MCPRuntimeProvider::_get_input_sequence) },
		{ "godot.runtime.input.sequence_cancel", "Cancel an input sequence and release inputs held by it.",
				_input_sequence_cancel_schema(), callable_mp(this, &MCPRuntimeProvider::_cancel_input_sequence) },
		{ "godot.runtime.input.release_all", "Cancel this MCP session's sequences and release all of its held runtime inputs.",
				_session_schema(true, false), callable_mp(this, &MCPRuntimeProvider::_release_input) },
	};

	for (const ToolRegistration &tool : tools) {
		const Error error = p_registry->register_tool(
				MCPToolUtils::make_tool_definition(tool.name, tool.description, tool.schema), tool.callable,
				MCPToolRegistry::TOOL_SURFACE_MCP, this, r_error);
		if (error != OK) {
			p_registry->unregister_tools_for_owner(this);
			return error;
		}
	}
	tool_registry = p_registry;
	return OK;
}

void MCPRuntimeProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPRuntimeProvider::_get_state(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray(), error) ? runtime_service->get_state() : error;
}

Dictionary MCPRuntimeProvider::_play(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "mode", "path", "restart" }, error) ? runtime_service->play(p_arguments) : error;
}

Dictionary MCPRuntimeProvider::_stop(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray(), error) ? runtime_service->stop() : error;
}

Dictionary MCPRuntimeProvider::_pause(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "debuggerSession", "runtimeGeneration" }, error) ? runtime_service->set_suspended(p_arguments, true) : error;
}

Dictionary MCPRuntimeProvider::_resume(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "debuggerSession", "runtimeGeneration" }, error) ? runtime_service->set_suspended(p_arguments, false) : error;
}

Dictionary MCPRuntimeProvider::_next_frame(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "debuggerSession", "runtimeGeneration" }, error) ? runtime_service->next_frame(p_arguments) : error;
}

Dictionary MCPRuntimeProvider::_get_tree(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "debuggerSession", "timeoutMs" }, error) ? runtime_service->get_tree(p_arguments) : error;
}

Dictionary MCPRuntimeProvider::_get_properties(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary error;
	return _has_only(p_arguments, PackedStringArray{ "path", "debuggerSession", "timeoutMs" }, error) ? runtime_service->get_properties(p_arguments) : error;
}

Dictionary MCPRuntimeProvider::_set_property(const Dictionary &p_arguments, const Dictionary &) {
	Dictionary error;
	const PackedStringArray allowed{ "path", "property", "value", "debuggerSession", "runtimeGeneration", "timeoutMs" };
	return _has_only(p_arguments, allowed, error) ? runtime_service->set_property(p_arguments) : error;
}

Dictionary MCPRuntimeProvider::_send_input(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	if (!_has_only(p_arguments, PackedStringArray{ "events", "debuggerSession", "runtimeGeneration" }, error)) {
		return error;
	}
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->send_input(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_start_input_sequence(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	if (!_has_only(p_arguments, PackedStringArray{ "steps", "debuggerSession", "runtimeGeneration" }, error)) {
		return error;
	}
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->start_input_sequence(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_get_input_sequence(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	if (!_has_only(p_arguments, PackedStringArray{ "sequenceId" }, error)) {
		return error;
	}
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->get_input_sequence(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_cancel_input_sequence(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	if (!_has_only(p_arguments, PackedStringArray{ "sequenceId", "debuggerSession", "runtimeGeneration" }, error)) {
		return error;
	}
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->cancel_input_sequence(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_release_input(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	if (!_has_only(p_arguments, PackedStringArray{ "debuggerSession", "runtimeGeneration" }, error)) {
		return error;
	}
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->release_input(p_arguments, session_id) : error;
}
