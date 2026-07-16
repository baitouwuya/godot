/**************************************************************************/
/*  mcp_runtime_input_sequence.h                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_runtime_input.h"

#include "core/templates/vector.h"

class MCPRuntimeInputSequence {
public:
	enum StepType {
		STEP_EVENT,
		STEP_WAIT_MSEC,
		STEP_WAIT_FRAMES,
	};

	struct Step {
		StepType type = STEP_EVENT;
		MCPRuntimeInput::EncodedEvent event;
		uint64_t amount = 0;
	};

	static constexpr int MAX_SOURCE_STEPS = 256;
	static constexpr int MAX_COMPILED_STEPS = 768;
	static constexpr uint64_t MAX_TOTAL_WAIT_MSEC = 10 * 60 * 1000;
	static constexpr uint64_t MAX_TOTAL_WAIT_FRAMES = 60 * 60 * 10;

	static Error compile(const Array &p_steps, Vector<Step> &r_steps, String *r_error = nullptr);
};
