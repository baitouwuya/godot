/**************************************************************************/
/*  mcp_debug_capture.h                                                   */
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

#include "core/error/error_macros.h"
#include "core/object/object.h"
#include "core/string/print_string.h"
#include "core/templates/hash_map.h"

class MCPDebugEventStore;

class MCPDebugCapture : public Object {
	MCPDebugEventStore *event_store = nullptr;
	PrintHandlerList print_handler;
	ErrorHandlerList error_handler;
	HashMap<int, uint64_t> runtime_generations;
	uint64_t next_runtime_generation = 1;
	bool started = false;

	static void _print_handler(void *p_self, const String &p_string, bool p_error, bool p_rich);
	static void _error_handler(void *p_self, const char *p_function, const char *p_file, int p_line,
			const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type);
	void _debug_data_received(const String &p_message, uint64_t p_thread_id, const Array &p_data, int p_debugger);
	void _debug_session_stopped(int p_debugger);

public:
	explicit MCPDebugCapture(MCPDebugEventStore *p_event_store);
	~MCPDebugCapture();

	Error start();
	void stop();
	bool is_started() const { return started; }
};
