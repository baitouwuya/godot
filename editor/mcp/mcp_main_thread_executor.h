/**************************************************************************/
/*  mcp_main_thread_executor.h                                            */
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

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/templates/list.h"
#include "core/variant/array.h"
#include "core/variant/callable.h"
#include "core/variant/variant.h"

class MCPMainThreadTask : public RefCounted {
	GDCLASS(MCPMainThreadTask, RefCounted);

public:
	enum Status {
		STATUS_PENDING,
		STATUS_RUNNING,
		STATUS_COMPLETED,
		STATUS_CALL_FAILED,
		STATUS_CANCELED,
	};

private:
	friend class MCPMainThreadExecutor;

	mutable Mutex mutex;
	Callable callable;
	Array arguments;
	Variant result;
	Callable::CallError call_error;
	Status status = STATUS_PENDING;

	bool _mark_running();
	void _complete(const Variant &p_result, const Callable::CallError &p_call_error);

public:
	bool cancel();
	Status get_status() const;
	bool is_finished() const;
	Error wait(uint64_t p_timeout_usec) const;
	Variant get_result() const;
	Callable::CallError get_call_error() const;
};

class MCPMainThreadExecutor {
	mutable Mutex mutex;
	List<Ref<MCPMainThreadTask>> pending_tasks;
	int max_pending_tasks = 128;
	bool accepting = true;

public:
	explicit MCPMainThreadExecutor(int p_max_pending_tasks = 128);

	Error submit(const Callable &p_callable, const Array &p_arguments, Ref<MCPMainThreadTask> &r_task);
	int poll(int p_max_tasks = 8);
	void shutdown();
	Error reset();

	int get_pending_count() const;
	bool is_accepting() const;
};
