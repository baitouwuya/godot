/**************************************************************************/
/*  mcp_runtime_debugger_wait.h                                           */
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

#pragma once

#include "core/typedefs.h"

// Applies one polling policy to bounded editor-debugger waits.
class MCPRuntimeDebuggerWait {
public:
	enum WaitStatus {
		WAIT_COMPLETED,
		WAIT_TIMEOUT,
		WAIT_DISCONNECTED,
		WAIT_STALE,
	};

	static uint64_t deadline_from_timeout_msec(int p_timeout_msec);

	template <typename TDebugger, typename TCompletionPredicate, typename TStalePredicate>
	static WaitStatus wait_until(TDebugger *p_debugger, uint64_t p_deadline_usec,
			const TCompletionPredicate &p_is_completed, const TStalePredicate &p_is_stale) {
		if (!p_debugger || !p_debugger->is_session_active()) {
			return WAIT_DISCONNECTED;
		}

		while (true) {
			if (p_is_stale()) {
				return WAIT_STALE;
			}
			if (p_is_completed()) {
				return WAIT_COMPLETED;
			}
			if (_get_ticks_usec() >= p_deadline_usec) {
				return WAIT_TIMEOUT;
			}

			p_debugger->poll_peer_messages(POLL_BUDGET_USEC);
			if (!p_debugger->is_session_active()) {
				return WAIT_DISCONNECTED;
			}
			if (p_is_stale()) {
				return WAIT_STALE;
			}
			if (p_is_completed()) {
				return WAIT_COMPLETED;
			}
			if (_get_ticks_usec() >= p_deadline_usec) {
				return WAIT_TIMEOUT;
			}
			_delay_between_polls();
		}
	}

	template <typename TDebugger, typename TCompletionPredicate>
	static WaitStatus wait_until(TDebugger *p_debugger, uint64_t p_deadline_usec,
			const TCompletionPredicate &p_is_completed) {
		return wait_until(p_debugger, p_deadline_usec, p_is_completed, []() { return false; });
	}

private:
	static constexpr uint64_t POLL_BUDGET_USEC = 2000;

	static uint64_t _get_ticks_usec();
	static void _delay_between_polls();
};
