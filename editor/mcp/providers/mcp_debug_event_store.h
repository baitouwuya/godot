/**************************************************************************/
/*  mcp_debug_event_store.h                                               */
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

#pragma once

#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/string/ustring.h"

class MCPDebugEventStore {
public:
	enum Source {
		SOURCE_EDITOR,
		SOURCE_RUNTIME,
	};

	enum Severity {
		SEVERITY_DEBUG,
		SEVERITY_INFO,
		SEVERITY_WARNING,
		SEVERITY_ERROR,
	};

	struct Frame {
		String file;
		String function;
		int line = -1;
	};

	struct Event {
		uint64_t sequence = 0;
		uint64_t timestamp_usec = 0;
		Source source = SOURCE_EDITOR;
		Severity severity = SEVERITY_INFO;
		int debugger_session = -1;
		uint64_t runtime_generation = 0;
		int64_t thread_id = -1;
		bool rich = false;
		bool truncated = false;
		String category;
		String message;
		String expression;
		String description;
		String file;
		String function;
		int line = -1;
		Vector<Frame> stack;
	};

	struct Snapshot {
		uint64_t generation = 0;
		uint64_t next_sequence = 0;
		uint64_t dropped = 0;
		uint64_t stored_bytes = 0;
		Vector<Event> events;
	};

	struct PausedStack {
		int debugger_session = -1;
		uint64_t runtime_generation = 0;
		int64_t thread_id = -1;
		uint64_t timestamp_usec = 0;
		bool truncated = false;
		Vector<Frame> frames;
	};

	struct Limits {
		int max_events = 4096;
		uint64_t max_bytes = 4 * 1024 * 1024;
		int max_field_characters = 16 * 1024;
		int max_stack_frames = 256;
		int max_stack_field_characters = 1024;
		int max_paused_stacks = 16;
	};

private:
	mutable Mutex mutex;
	Limits limits;
	List<Event> events;
	HashMap<String, PausedStack> paused_stacks;
	uint64_t generation = 1;
	uint64_t next_sequence = 1;
	uint64_t dropped = 0;
	uint64_t stored_bytes = 0;

	String _truncate_field(const String &p_value, bool &r_truncated) const;
	Frame _sanitize_frame(const Frame &p_frame, bool &r_truncated) const;
	uint64_t _estimate_event_bytes(const Event &p_event) const;
	String _paused_stack_key(int p_debugger_session, int64_t p_thread_id) const;

public:
	MCPDebugEventStore();
	explicit MCPDebugEventStore(const Limits &p_limits);
	MCPDebugEventStore(int p_max_events, uint64_t p_max_bytes, int p_max_field_characters);

	uint64_t record(const Event &p_event);
	Snapshot snapshot() const;
	void clear();

	void set_paused_stack(int p_debugger_session, uint64_t p_runtime_generation, int64_t p_thread_id, const Vector<Frame> &p_frames);
	void set_paused_stack(int p_debugger_session, const Vector<Frame> &p_frames);
	bool get_paused_stack(int p_debugger_session, PausedStack &r_stack) const;
	bool get_paused_stack(int p_debugger_session, int64_t p_thread_id, PausedStack &r_stack) const;
	bool get_paused_stack(int p_debugger_session, Vector<Frame> &r_frames) const;
	void clear_paused_stack(int p_debugger_session, int64_t p_thread_id = -1);
	void clear_paused_stacks();
};
