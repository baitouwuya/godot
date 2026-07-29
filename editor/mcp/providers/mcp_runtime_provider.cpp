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
#include "mcp_runtime_output_schemas.h"
#include "mcp_runtime_tool_schemas.h"
#include "mcp_tool_utils.h"

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

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

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.runtime.get_state", "Get editor run state and all active runtime debugger sessions.",
				MCPToolUtils::make_object_schema(Dictionary()), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_state), MCPRuntimeOutputSchemas::state() },
		{ "godot.runtime.play", "Run the main, current, or a specific project scene from the editor.",
				MCPRuntimeToolSchemas::play(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_play), MCPRuntimeOutputSchemas::play() },
		{ "godot.runtime.stop", "Stop the project currently launched by the editor.",
				MCPToolUtils::make_object_schema(Dictionary()), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_stop), MCPRuntimeOutputSchemas::stop() },
		{ "godot.runtime.pause", "Suspend a running project's SceneTree without entering a script breakpoint.",
				MCPRuntimeToolSchemas::session(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_pause), MCPRuntimeOutputSchemas::suspended() },
		{ "godot.runtime.resume", "Resume a SceneTree suspended through the debugger.",
				MCPRuntimeToolSchemas::session(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_resume), MCPRuntimeOutputSchemas::suspended() },
		{ "godot.runtime.next_frame", "Advance one rendered frame while the running SceneTree is suspended.",
				MCPRuntimeToolSchemas::session(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_next_frame), MCPRuntimeOutputSchemas::dispatched() },
		{ "godot.runtime.debug.break", "Pause a running project at the next safe script-debugger point.",
				MCPRuntimeToolSchemas::session(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_break), MCPRuntimeOutputSchemas::debug_control() },
		{ "godot.runtime.debug.continue", "Continue a project paused at a script breakpoint.",
				MCPRuntimeToolSchemas::session(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_continue), MCPRuntimeOutputSchemas::debug_control() },
		{ "godot.runtime.debug.step_into", "Step into one GDScript statement from a paused debugger frame.",
				MCPRuntimeToolSchemas::session(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_step_into), MCPRuntimeOutputSchemas::debug_control() },
		{ "godot.runtime.debug.step_over", "Step over one GDScript statement from a paused debugger frame.",
				MCPRuntimeToolSchemas::session(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_step_over), MCPRuntimeOutputSchemas::debug_control() },
		{ "godot.runtime.debug.step_out", "Step out of the current GDScript frame from a paused debugger frame.",
				MCPRuntimeToolSchemas::session(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_debug_step_out), MCPRuntimeOutputSchemas::debug_control() },
		{ "godot.runtime.get_tree", "Get a fresh running-project scene tree using Godot's remote debugger.",
				MCPRuntimeToolSchemas::tree(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_tree), MCPRuntimeOutputSchemas::tree() },
		{ "godot.runtime.node.get_properties", "Get all inspectable runtime node properties, including script members and exports.",
				MCPRuntimeToolSchemas::properties(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_properties), MCPRuntimeOutputSchemas::properties() },
		{ "godot.runtime.node.set_property", "Set and verify one runtime node property through Godot's remote inspector protocol.",
				MCPRuntimeToolSchemas::set_property(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_set_property), MCPRuntimeOutputSchemas::set_property() },
		{ "godot.runtime.input.send", "Inject validated action, keyboard, mouse, joypad, pan, and magnify input events into a running project.",
				MCPRuntimeToolSchemas::input(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_send_input), MCPRuntimeOutputSchemas::input_dispatch() },
		{ "godot.runtime.input.sequence", "Run an asynchronous input sequence with millisecond or frame waits, tap, and hold operations.",
				MCPRuntimeToolSchemas::input_sequence(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_start_input_sequence), MCPRuntimeOutputSchemas::input_sequence() },
		{ "godot.runtime.input.sequence_status", "Read the current state of an input sequence owned by this MCP session.",
				MCPRuntimeToolSchemas::input_sequence_status(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_input_sequence), MCPRuntimeOutputSchemas::input_sequence() },
		{ "godot.runtime.input.sequence_cancel", "Cancel an input sequence and release inputs held by it.",
				MCPRuntimeToolSchemas::input_sequence_cancel(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_cancel_input_sequence), MCPRuntimeOutputSchemas::input_sequence() },
		{ "godot.runtime.input.release_all", "Cancel this MCP session's sequences and release all of its held runtime inputs.",
				MCPRuntimeToolSchemas::session(true, false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_release_input), MCPRuntimeOutputSchemas::release_input() },
		{ "godot.runtime.get_screenshot", "Capture the running project's root viewport to a temporary PNG.",
				MCPRuntimeToolSchemas::screenshot(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPRuntimeProvider::_get_screenshot), MCPRuntimeOutputSchemas::screenshot() },
		{ "godot.runtime.get_viewport_summary", "Get root viewport, active camera, scene, and target-count information.",
				MCPRuntimeToolSchemas::viewport_summary(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_viewport_summary), MCPRuntimeOutputSchemas::viewport_summary() },
		{ "godot.runtime.query_nodes", "Query compact runtime node snapshots by path, name, type, group, text, visibility, or screen position.",
				MCPRuntimeToolSchemas::runtime_query(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_query_nodes), MCPRuntimeOutputSchemas::query() },
		{ "godot.runtime.get_interactables", "List visible runtime UI and ray-pickable 3D targets matching an optional filter.",
				MCPRuntimeToolSchemas::runtime_query(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_interactables), MCPRuntimeOutputSchemas::query() },
		{ "godot.runtime.get_node_snapshot", "Get one compact runtime node snapshot from an unambiguous selector.",
				MCPRuntimeToolSchemas::runtime_snapshot(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_node_snapshot), MCPRuntimeOutputSchemas::snapshot() },
		{ "godot.runtime.click_target", "Resolve a runtime selector and enqueue one bounded mouse click at the target.",
				MCPRuntimeToolSchemas::click_target(false), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_click_target), MCPRuntimeOutputSchemas::target_action() },
		{ "godot.runtime.double_click_target", "Resolve a runtime selector and enqueue one bounded double-click at the target.",
				MCPRuntimeToolSchemas::click_target(true), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_double_click_target), MCPRuntimeOutputSchemas::target_action() },
		{ "godot.runtime.hover_target", "Resolve a runtime selector and move the pointer to the target.",
				MCPRuntimeToolSchemas::single_target(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_hover_target), MCPRuntimeOutputSchemas::target_action() },
		{ "godot.runtime.focus_target", "Focus a runtime Control, or move the pointer to a non-Control target.",
				MCPRuntimeToolSchemas::single_target(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_focus_target), MCPRuntimeOutputSchemas::target_action() },
		{ "godot.runtime.drag_target_to_target", "Atomically resolve two selectors and enqueue a bounded pointer drag between them.",
				MCPRuntimeToolSchemas::drag_target(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_drag_target_to_target), MCPRuntimeOutputSchemas::target_action() },
		{ "godot.runtime.type_text", "Focus a runtime Control and enqueue bounded Unicode key input.",
				MCPRuntimeToolSchemas::type_text(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_type_text), MCPRuntimeOutputSchemas::target_action() },
		{ "godot.runtime.scroll_view", "Resolve a runtime selector and inject bounded vertical wheel input at the target.",
				MCPRuntimeToolSchemas::scroll_view(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_scroll_view), MCPRuntimeOutputSchemas::target_action() },
		{ "godot.runtime.wait.start", "Register a bounded runtime condition job and return immediately.",
				MCPRuntimeToolSchemas::wait_start(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPRuntimeProvider::_start_wait), MCPRuntimeOutputSchemas::wait_job() },
		{ "godot.runtime.wait.status", "Get the current state of an asynchronous runtime condition job.",
				MCPRuntimeToolSchemas::wait_job(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_wait_status), MCPRuntimeOutputSchemas::wait_job() },
		{ "godot.runtime.wait.cancel", "Cancel an asynchronous runtime condition job owned by this MCP session.",
				MCPRuntimeToolSchemas::wait_job(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_cancel_wait), MCPRuntimeOutputSchemas::wait_job() },
		{ "godot.runtime.performance.start", "Start bounded in-process runtime performance aggregation and return immediately.",
				MCPRuntimeToolSchemas::performance_start(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPRuntimeProvider::_start_performance), MCPRuntimeOutputSchemas::performance_job() },
		{ "godot.runtime.performance.status", "Get the current state and captured-frame count of a runtime performance job.",
				MCPRuntimeToolSchemas::performance_job(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPRuntimeProvider::_get_performance_status), MCPRuntimeOutputSchemas::performance_job() },
		{ "godot.runtime.performance.stop", "Stop a runtime performance job and return its final structured summary.",
				MCPRuntimeToolSchemas::performance_job(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPRuntimeProvider::_stop_performance), MCPRuntimeOutputSchemas::performance_job() },
	};

	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
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

void MCPRuntimeProvider::process() {
	runtime_service->process_input();
}

void MCPRuntimeProvider::on_session_removed(const String &p_session_id) {
	runtime_service->release_mcp_session(p_session_id);
}

void MCPRuntimeProvider::shutdown() {
	runtime_service->shutdown_input();
}

Dictionary MCPRuntimeProvider::_get_state(const Dictionary &, const Dictionary &) {
	return runtime_service->get_state();
}

Dictionary MCPRuntimeProvider::_play(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->play(p_arguments);
}

Dictionary MCPRuntimeProvider::_stop(const Dictionary &, const Dictionary &) {
	return runtime_service->stop();
}

Dictionary MCPRuntimeProvider::_pause(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->set_suspended(p_arguments, true);
}

Dictionary MCPRuntimeProvider::_resume(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->set_suspended(p_arguments, false);
}

Dictionary MCPRuntimeProvider::_next_frame(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->next_frame(p_arguments);
}

Dictionary MCPRuntimeProvider::_debug_break(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "break");
}

Dictionary MCPRuntimeProvider::_debug_continue(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "continue");
}

Dictionary MCPRuntimeProvider::_debug_step_into(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "step_into");
}

Dictionary MCPRuntimeProvider::_debug_step_over(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "step_over");
}

Dictionary MCPRuntimeProvider::_debug_step_out(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->debug_control(p_arguments, "step_out");
}

Dictionary MCPRuntimeProvider::_get_tree(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_tree(p_arguments);
}

Dictionary MCPRuntimeProvider::_get_screenshot(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_screenshot(p_arguments);
}

Dictionary MCPRuntimeProvider::_get_viewport_summary(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_viewport_summary(p_arguments);
}

Dictionary MCPRuntimeProvider::_query_nodes(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->query_nodes(p_arguments);
}

Dictionary MCPRuntimeProvider::_get_interactables(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_interactables(p_arguments);
}

Dictionary MCPRuntimeProvider::_get_node_snapshot(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_node_snapshot(p_arguments);
}

Dictionary MCPRuntimeProvider::_click_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->click_target(p_arguments, session_id, false) : error;
}

Dictionary MCPRuntimeProvider::_double_click_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->click_target(p_arguments, session_id, true) : error;
}

Dictionary MCPRuntimeProvider::_hover_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->hover_target(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_focus_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->focus_target(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_drag_target_to_target(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->drag_target_to_target(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_type_text(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->type_text(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_scroll_view(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->scroll_view(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_start_wait(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->start_wait(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_get_wait_status(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->get_wait_status(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_cancel_wait(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->cancel_wait(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_start_performance(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->start_performance(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_get_performance_status(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->get_performance_status(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_stop_performance(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->stop_performance(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_get_properties(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->get_properties(p_arguments);
}

Dictionary MCPRuntimeProvider::_set_property(const Dictionary &p_arguments, const Dictionary &) {
	return runtime_service->set_property(p_arguments);
}

Dictionary MCPRuntimeProvider::_send_input(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->send_input(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_start_input_sequence(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->start_input_sequence(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_get_input_sequence(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->get_input_sequence(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_cancel_input_sequence(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->cancel_input_sequence(p_arguments, session_id) : error;
}

Dictionary MCPRuntimeProvider::_release_input(const Dictionary &p_arguments, const Dictionary &p_context) {
	Dictionary error;
	String session_id;
	return _resolve_mcp_session(p_context, session_id, error) ? runtime_service->release_input(p_arguments, session_id) : error;
}
