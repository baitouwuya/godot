/**************************************************************************/
/*  mcp_debug_event_store.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_debug_event_store.h"

#include "core/os/os.h"

namespace {

uint64_t _string_storage_bytes(const String &p_value) {
	return uint64_t(p_value.length()) * sizeof(char32_t);
}

} // namespace

String MCPDebugEventStore::_truncate_field(const String &p_value, bool &r_truncated) const {
	if (p_value.length() <= limits.max_field_characters) {
		return p_value;
	}
	r_truncated = true;
	return p_value.substr(0, limits.max_field_characters);
}

MCPDebugEventStore::Frame MCPDebugEventStore::_sanitize_frame(const Frame &p_frame, bool &r_truncated) const {
	Frame frame = p_frame;
	const int max_characters = MIN(limits.max_field_characters, limits.max_stack_field_characters);
	if (frame.file.length() > max_characters) {
		frame.file = frame.file.substr(0, max_characters);
		r_truncated = true;
	}
	if (frame.function.length() > max_characters) {
		frame.function = frame.function.substr(0, max_characters);
		r_truncated = true;
	}
	return frame;
}

uint64_t MCPDebugEventStore::_estimate_event_bytes(const Event &p_event) const {
	uint64_t bytes = sizeof(Event);
	bytes += _string_storage_bytes(p_event.category);
	bytes += _string_storage_bytes(p_event.message);
	bytes += _string_storage_bytes(p_event.expression);
	bytes += _string_storage_bytes(p_event.description);
	bytes += _string_storage_bytes(p_event.file);
	bytes += _string_storage_bytes(p_event.function);
	bytes += uint64_t(p_event.stack.size()) * sizeof(Frame);
	for (const Frame &frame : p_event.stack) {
		bytes += _string_storage_bytes(frame.file);
		bytes += _string_storage_bytes(frame.function);
	}
	return bytes;
}

MCPDebugEventStore::MCPDebugEventStore() : MCPDebugEventStore(Limits()) {
}

MCPDebugEventStore::MCPDebugEventStore(const Limits &p_limits) {
	limits.max_events = MAX(p_limits.max_events, 0);
	limits.max_bytes = p_limits.max_bytes;
	limits.max_field_characters = MAX(p_limits.max_field_characters, 0);
	limits.max_stack_frames = MAX(p_limits.max_stack_frames, 0);
	limits.max_stack_field_characters = MAX(p_limits.max_stack_field_characters, 0);
	limits.max_paused_stacks = MAX(p_limits.max_paused_stacks, 0);
}

MCPDebugEventStore::MCPDebugEventStore(int p_max_events, uint64_t p_max_bytes, int p_max_field_characters) :
		MCPDebugEventStore(Limits{ p_max_events, p_max_bytes, p_max_field_characters, 256, 1024, 16 }) {
}

String MCPDebugEventStore::_paused_stack_key(int p_debugger_session, int64_t p_thread_id) const {
	return itos(p_debugger_session) + ":" + String::num_int64(p_thread_id);
}

uint64_t MCPDebugEventStore::record(const Event &p_event) {
	Event event = p_event;
	event.sequence = 0;
	event.timestamp_usec = OS::get_singleton()->get_ticks_usec();
	event.category = _truncate_field(event.category, event.truncated);
	event.message = _truncate_field(event.message, event.truncated);
	event.expression = _truncate_field(event.expression, event.truncated);
	event.description = _truncate_field(event.description, event.truncated);
	event.file = _truncate_field(event.file, event.truncated);
	event.function = _truncate_field(event.function, event.truncated);
	if (event.stack.size() > limits.max_stack_frames) {
		event.stack.resize(limits.max_stack_frames);
		event.truncated = true;
	}
	for (Frame &frame : event.stack) {
		frame = _sanitize_frame(frame, event.truncated);
	}

	MutexLock lock(mutex);
	event.sequence = next_sequence++;
	const uint64_t event_bytes = _estimate_event_bytes(event);
	events.push_back(event);
	stored_bytes += event_bytes;

	while (!events.is_empty() && (events.size() > limits.max_events || stored_bytes > limits.max_bytes)) {
		const uint64_t removed_bytes = _estimate_event_bytes(events.front()->get());
		stored_bytes = removed_bytes <= stored_bytes ? stored_bytes - removed_bytes : 0;
		events.pop_front();
		dropped++;
	}
	return event.sequence;
}

MCPDebugEventStore::Snapshot MCPDebugEventStore::snapshot() const {
	MutexLock lock(mutex);
	Snapshot result;
	result.generation = generation;
	result.next_sequence = next_sequence;
	result.dropped = dropped;
	result.stored_bytes = stored_bytes;
	result.events.resize(events.size());
	int index = 0;
	for (const Event &event : events) {
		result.events.write[index++] = event;
	}
	return result;
}

void MCPDebugEventStore::clear() {
	MutexLock lock(mutex);
	events.clear();
	paused_stacks.clear();
	stored_bytes = 0;
	dropped = 0;
	generation++;
}

void MCPDebugEventStore::set_paused_stack(int p_debugger_session, uint64_t p_runtime_generation, int64_t p_thread_id, const Vector<Frame> &p_frames) {
	PausedStack paused_stack;
	paused_stack.debugger_session = p_debugger_session;
	paused_stack.runtime_generation = p_runtime_generation;
	paused_stack.thread_id = p_thread_id;
	paused_stack.timestamp_usec = OS::get_singleton()->get_ticks_usec();
	const int frame_count = MIN(p_frames.size(), limits.max_stack_frames);
	paused_stack.truncated = p_frames.size() > frame_count;
	paused_stack.frames.resize(frame_count);
	for (int i = 0; i < frame_count; i++) {
		paused_stack.frames.write[i] = _sanitize_frame(p_frames[i], paused_stack.truncated);
	}

	MutexLock lock(mutex);
	if (!paused_stacks.has(_paused_stack_key(p_debugger_session, p_thread_id)) && int(paused_stacks.size()) >= limits.max_paused_stacks) {
		paused_stacks.clear();
	}
	paused_stacks[_paused_stack_key(p_debugger_session, p_thread_id)] = paused_stack;
}

void MCPDebugEventStore::set_paused_stack(int p_debugger_session, const Vector<Frame> &p_frames) {
	set_paused_stack(p_debugger_session, 0, -1, p_frames);
}

bool MCPDebugEventStore::get_paused_stack(int p_debugger_session, PausedStack &r_stack) const {
	MutexLock lock(mutex);
	const PausedStack *latest = nullptr;
	for (const KeyValue<String, PausedStack> &entry : paused_stacks) {
		if (entry.value.debugger_session == p_debugger_session && (!latest || entry.value.timestamp_usec > latest->timestamp_usec)) {
			latest = &entry.value;
		}
	}
	if (!latest) {
		return false;
	}
	r_stack = *latest;
	return true;
}

bool MCPDebugEventStore::get_paused_stack(int p_debugger_session, int64_t p_thread_id, PausedStack &r_stack) const {
	MutexLock lock(mutex);
	const PausedStack *paused_stack = paused_stacks.getptr(_paused_stack_key(p_debugger_session, p_thread_id));
	if (!paused_stack) {
		return false;
	}
	r_stack = *paused_stack;
	return true;
}

bool MCPDebugEventStore::get_paused_stack(int p_debugger_session, Vector<Frame> &r_frames) const {
	MutexLock lock(mutex);
	const PausedStack *latest = nullptr;
	for (const KeyValue<String, PausedStack> &entry : paused_stacks) {
		if (entry.value.debugger_session == p_debugger_session && (!latest || entry.value.timestamp_usec > latest->timestamp_usec)) {
			latest = &entry.value;
		}
	}
	if (!latest) {
		return false;
	}
	r_frames = latest->frames;
	return true;
}

void MCPDebugEventStore::clear_paused_stack(int p_debugger_session, int64_t p_thread_id) {
	MutexLock lock(mutex);
	if (p_thread_id >= 0) {
		paused_stacks.erase(_paused_stack_key(p_debugger_session, p_thread_id));
		return;
	}
	Vector<String> keys;
	for (const KeyValue<String, PausedStack> &entry : paused_stacks) {
		if (entry.value.debugger_session == p_debugger_session) {
			keys.push_back(entry.key);
		}
	}
	for (const String &key : keys) {
		paused_stacks.erase(key);
	}
}

void MCPDebugEventStore::clear_paused_stacks() {
	MutexLock lock(mutex);
	paused_stacks.clear();
}
