/**************************************************************************/
/*  mcp_main_thread_executor.cpp                                          */
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

#include "mcp_main_thread_executor.h"

#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/local_vector.h"

bool MCPMainThreadTask::_mark_running() {
	MutexLock lock(mutex);
	if (status != STATUS_PENDING) {
		return false;
	}
	status = STATUS_RUNNING;
	return true;
}

void MCPMainThreadTask::_complete(const Variant &p_result, const Callable::CallError &p_call_error) {
	MutexLock lock(mutex);
	result = p_result;
	call_error = p_call_error;
	status = call_error.error == Callable::CallError::CALL_OK ? STATUS_COMPLETED : STATUS_CALL_FAILED;
}

bool MCPMainThreadTask::cancel() {
	MutexLock lock(mutex);
	if (status != STATUS_PENDING) {
		return false;
	}
	status = STATUS_CANCELED;
	callable = Callable();
	arguments.clear();
	return true;
}

MCPMainThreadTask::Status MCPMainThreadTask::get_status() const {
	MutexLock lock(mutex);
	return status;
}

bool MCPMainThreadTask::is_finished() const {
	const Status current_status = get_status();
	return current_status == STATUS_COMPLETED || current_status == STATUS_CALL_FAILED || current_status == STATUS_CANCELED;
}

Error MCPMainThreadTask::wait(uint64_t p_timeout_usec) const {
	ERR_FAIL_COND_V_MSG(Thread::is_main_thread(), ERR_BUSY, "The main thread cannot wait for an MCP main-thread task.");
	const uint64_t started_at = OS::get_singleton()->get_ticks_usec();
	while (!is_finished()) {
		if (OS::get_singleton()->get_ticks_usec() - started_at >= p_timeout_usec) {
			return ERR_TIMEOUT;
		}
		OS::get_singleton()->delay_usec(100);
	}
	const Status current_status = get_status();
	if (current_status == STATUS_CANCELED) {
		return ERR_SKIP;
	}
	return current_status == STATUS_COMPLETED ? OK : FAILED;
}

Variant MCPMainThreadTask::get_result() const {
	MutexLock lock(mutex);
	return status == STATUS_COMPLETED ? result : Variant();
}

Callable::CallError MCPMainThreadTask::get_call_error() const {
	MutexLock lock(mutex);
	return call_error;
}

MCPMainThreadExecutor::MCPMainThreadExecutor(int p_max_pending_tasks) {
	ERR_FAIL_COND(p_max_pending_tasks <= 0);
	max_pending_tasks = p_max_pending_tasks;
}

Error MCPMainThreadExecutor::submit(const Callable &p_callable, const Array &p_arguments, Ref<MCPMainThreadTask> &r_task) {
	ERR_FAIL_COND_V(!p_callable.is_valid(), ERR_INVALID_PARAMETER);
	MutexLock lock(mutex);
	if (!accepting) {
		return ERR_UNCONFIGURED;
	}
	if (pending_tasks.size() >= max_pending_tasks) {
		return ERR_BUSY;
	}

	r_task.instantiate();
	r_task->callable = p_callable;
	r_task->arguments = p_arguments.duplicate(true);
	pending_tasks.push_back(r_task);
	return OK;
}

int MCPMainThreadExecutor::poll(int p_max_tasks) {
	ERR_FAIL_COND_V_MSG(!Thread::is_main_thread(), 0, "MCP main-thread tasks must be polled on the main thread.");
	ERR_FAIL_COND_V(p_max_tasks <= 0, 0);

	int processed = 0;
	while (processed < p_max_tasks) {
		Ref<MCPMainThreadTask> task;
		{
			MutexLock lock(mutex);
			if (pending_tasks.is_empty()) {
				break;
			}
			task = pending_tasks.front()->get();
			pending_tasks.pop_front();
		}

		if (!task->_mark_running()) {
			processed++;
			continue;
		}

		LocalVector<const Variant *> argument_pointers;
		argument_pointers.resize(task->arguments.size());
		for (int i = 0; i < task->arguments.size(); i++) {
			argument_pointers[i] = &task->arguments[i];
		}
		Variant result;
		Callable::CallError call_error;
		task->callable.callp(argument_pointers.ptr(), argument_pointers.size(), result, call_error);
		task->_complete(result, call_error);
		processed++;
	}
	return processed;
}

void MCPMainThreadExecutor::shutdown() {
	MutexLock lock(mutex);
	accepting = false;
	for (const Ref<MCPMainThreadTask> &task : pending_tasks) {
		task->cancel();
	}
	pending_tasks.clear();
}

Error MCPMainThreadExecutor::reset() {
	MutexLock lock(mutex);
	ERR_FAIL_COND_V(!pending_tasks.is_empty(), ERR_BUSY);
	accepting = true;
	return OK;
}

int MCPMainThreadExecutor::get_pending_count() const {
	MutexLock lock(mutex);
	return pending_tasks.size();
}

bool MCPMainThreadExecutor::is_accepting() const {
	MutexLock lock(mutex);
	return accepting;
}
