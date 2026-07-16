/**************************************************************************/
/*  mcp_debug_capture.cpp                                                 */
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

#include "mcp_debug_capture.h"

#include "mcp_debug_event_store.h"

#include "core/debugger/debugger_marshalls.h"
#include "core/debugger/remote_debugger.h"
#include "core/object/callable_mp.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"

namespace {

constexpr int MAX_REMOTE_STACK_FRAMES = 256;

static MCPDebugEventStore::Severity _error_severity(ErrorHandlerType p_type) {
	return p_type == ERR_HANDLER_WARNING ? MCPDebugEventStore::SEVERITY_WARNING : MCPDebugEventStore::SEVERITY_ERROR;
}

static String _error_category(ErrorHandlerType p_type) {
	switch (p_type) {
		case ERR_HANDLER_WARNING:
			return "warning";
		case ERR_HANDLER_SCRIPT:
			return "script";
		case ERR_HANDLER_SHADER:
			return "shader";
		default:
			return "error";
	}
}

static bool _is_non_negative_integer(const Variant &p_value, int64_t &r_value) {
	if (p_value.get_type() != Variant::INT) {
		return false;
	}
	r_value = p_value;
	return r_value >= 0;
}

} // namespace

MCPDebugCapture::MCPDebugCapture(MCPDebugEventStore *p_event_store) :
		event_store(p_event_store) {
	print_handler.printfunc = _print_handler;
	print_handler.userdata = this;
	error_handler.errfunc = _error_handler;
	error_handler.userdata = this;
}

MCPDebugCapture::~MCPDebugCapture() {
	stop();
}

Error MCPDebugCapture::start() {
	ERR_FAIL_NULL_V(event_store, ERR_UNCONFIGURED);
	if (started) {
		return ERR_ALREADY_IN_USE;
	}

	EditorDebuggerNode *debugger = EditorDebuggerNode::get_singleton();
	ERR_FAIL_NULL_V(debugger, ERR_UNCONFIGURED);
	debugger->connect("debug_data_received", callable_mp(this, &MCPDebugCapture::_debug_data_received));
	debugger->connect("debug_session_stopped", callable_mp(this, &MCPDebugCapture::_debug_session_stopped));
	for (int i = 0; i < debugger->get_debugger_count(); i++) {
		ScriptEditorDebugger *session = debugger->get_debugger(i);
		if (session && session->is_session_active()) {
			runtime_generations[i] = next_runtime_generation++;
		}
	}
	add_print_handler(&print_handler);
	add_error_handler(&error_handler);
	started = true;
	return OK;
}

void MCPDebugCapture::stop() {
	if (!started) {
		return;
	}
	remove_print_handler(&print_handler);
	remove_error_handler(&error_handler);
	if (EditorDebuggerNode *debugger = EditorDebuggerNode::get_singleton()) {
		const Callable callback = callable_mp(this, &MCPDebugCapture::_debug_data_received);
		if (debugger->is_connected("debug_data_received", callback)) {
			debugger->disconnect("debug_data_received", callback);
		}
		const Callable stopped_callback = callable_mp(this, &MCPDebugCapture::_debug_session_stopped);
		if (debugger->is_connected("debug_session_stopped", stopped_callback)) {
			debugger->disconnect("debug_session_stopped", stopped_callback);
		}
	}
	runtime_generations.clear();
	started = false;
}

void MCPDebugCapture::_print_handler(void *p_self, const String &p_string, bool p_error, bool p_rich) {
	MCPDebugCapture *self = static_cast<MCPDebugCapture *>(p_self);
	if (!self || !self->event_store) {
		return;
	}
	MCPDebugEventStore::Event event;
	event.source = MCPDebugEventStore::SOURCE_EDITOR;
	event.severity = p_error ? MCPDebugEventStore::SEVERITY_ERROR : MCPDebugEventStore::SEVERITY_INFO;
	event.category = p_error ? "stderr" : "stdout";
	event.message = p_string;
	event.rich = p_rich;
	self->event_store->record(event);
}

void MCPDebugCapture::_error_handler(void *p_self, const char *p_function, const char *p_file, int p_line,
		const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
	MCPDebugCapture *self = static_cast<MCPDebugCapture *>(p_self);
	if (!self || !self->event_store) {
		return;
	}
	MCPDebugEventStore::Event event;
	event.source = MCPDebugEventStore::SOURCE_EDITOR;
	event.severity = _error_severity(p_type);
	event.category = _error_category(p_type);
	event.expression = String::utf8(p_error ? p_error : "");
	event.description = String::utf8(p_explanation ? p_explanation : "");
	event.message = event.description.is_empty() ? event.expression : event.description;
	event.file = String::utf8(p_file ? p_file : "");
	event.function = String::utf8(p_function ? p_function : "");
	event.line = p_line;
	self->event_store->record(event);
}

void MCPDebugCapture::_debug_data_received(const String &p_message, uint64_t p_thread_id, const Array &p_data, int p_debugger) {
	if (!event_store) {
		return;
	}

	if (p_message == "set_pid") {
		runtime_generations[p_debugger] = next_runtime_generation++;
		event_store->clear_paused_stack(p_debugger);
		return;
	}
	const uint64_t *runtime_generation_ptr = runtime_generations.getptr(p_debugger);
	const uint64_t runtime_generation = runtime_generation_ptr ? *runtime_generation_ptr : 0;

	if (p_message == "output") {
		if (p_data.size() != 2 || p_data[0].get_type() != Variant::PACKED_STRING_ARRAY || p_data[1].get_type() != Variant::PACKED_INT32_ARRAY) {
			return;
		}
		const PackedStringArray messages = p_data[0];
		const PackedInt32Array types = p_data[1];
		if (messages.size() != types.size()) {
			return;
		}
		for (int i = 0; i < messages.size(); i++) {
			const int type = types[i];
			if (type < RemoteDebugger::MESSAGE_TYPE_LOG || type > RemoteDebugger::MESSAGE_TYPE_LOG_RICH) {
				continue;
			}
			const PackedStringArray lines = messages[i].replace("\r\n", "\n").replace("\r", "\n").split("\n", true);
			for (const String &line : lines) {
				MCPDebugEventStore::Event event;
				event.source = MCPDebugEventStore::SOURCE_RUNTIME;
				event.debugger_session = p_debugger;
				event.runtime_generation = runtime_generation;
				event.thread_id = int64_t(p_thread_id);
				event.severity = type == RemoteDebugger::MESSAGE_TYPE_ERROR ? MCPDebugEventStore::SEVERITY_ERROR : MCPDebugEventStore::SEVERITY_INFO;
				event.category = type == RemoteDebugger::MESSAGE_TYPE_ERROR ? "stderr" : "stdout";
				event.message = line;
				event.rich = type == RemoteDebugger::MESSAGE_TYPE_LOG_RICH;
				event_store->record(event);
			}
		}
		return;
	}

	if (p_message == "error") {
		if (p_data.size() < 11) {
			return;
		}
		int64_t stack_size = 0;
		if (!_is_non_negative_integer(p_data[10], stack_size) || stack_size % 3 != 0 ||
				stack_size > MAX_REMOTE_STACK_FRAMES * 3 || stack_size != p_data.size() - 11) {
			return;
		}
		DebuggerMarshalls::OutputError output_error;
		if (!output_error.deserialize(p_data)) {
			return;
		}
		MCPDebugEventStore::Event event;
		event.source = MCPDebugEventStore::SOURCE_RUNTIME;
		event.debugger_session = p_debugger;
		event.runtime_generation = runtime_generation;
		event.thread_id = int64_t(p_thread_id);
		event.severity = output_error.warning ? MCPDebugEventStore::SEVERITY_WARNING : MCPDebugEventStore::SEVERITY_ERROR;
		event.category = output_error.warning ? "warning" : "error";
		event.expression = output_error.error;
		event.description = output_error.error_descr;
		event.message = event.description.is_empty() ? event.expression : event.description;
		event.file = output_error.source_file;
		event.function = output_error.source_func;
		event.line = output_error.source_line;
		for (const ScriptLanguage::StackInfo &frame : output_error.callstack) {
			MCPDebugEventStore::Frame stored_frame;
			stored_frame.file = frame.file;
			stored_frame.function = frame.func;
			stored_frame.line = frame.line;
			event.stack.push_back(stored_frame);
		}
		event_store->record(event);
		return;
	}

	if (p_message == "stack_dump") {
		if (p_data.is_empty()) {
			return;
		}
		int64_t stack_size = 0;
		if (!_is_non_negative_integer(p_data[0], stack_size) || stack_size % 3 != 0 ||
				stack_size > MAX_REMOTE_STACK_FRAMES * 3 || stack_size != p_data.size() - 1) {
			return;
		}
		DebuggerMarshalls::ScriptStackDump stack_dump;
		if (!stack_dump.deserialize(p_data)) {
			return;
		}
		Vector<MCPDebugEventStore::Frame> frames;
		for (const ScriptLanguage::StackInfo &frame : stack_dump.frames) {
			MCPDebugEventStore::Frame stored_frame;
			stored_frame.file = frame.file;
			stored_frame.function = frame.func;
			stored_frame.line = frame.line;
			frames.push_back(stored_frame);
		}
		event_store->set_paused_stack(p_debugger, runtime_generation, int64_t(p_thread_id), frames);
		return;
	}

	if (p_message == "debug_exit") {
		event_store->clear_paused_stack(p_debugger, int64_t(p_thread_id));
	}
}

void MCPDebugCapture::_debug_session_stopped(int p_debugger) {
	if (event_store) {
		event_store->clear_paused_stack(p_debugger);
	}
	runtime_generations.erase(p_debugger);
}

bool MCPDebugCapture::get_runtime_generation(int p_debugger_session, uint64_t &r_generation) const {
	const uint64_t *generation = runtime_generations.getptr(p_debugger_session);
	if (!generation) {
		return false;
	}
	r_generation = *generation;
	return true;
}
