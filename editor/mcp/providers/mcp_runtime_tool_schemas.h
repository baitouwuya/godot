/**************************************************************************/
/*  mcp_runtime_tool_schemas.h                                            */
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
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

namespace MCPRuntimeToolSchemas {

Dictionary session(bool p_require_generation, bool p_timeout);
Dictionary play();
Dictionary tree();
Dictionary runtime_query();
Dictionary runtime_snapshot();
Dictionary viewport_summary();
Dictionary screenshot();
Dictionary click_target(bool p_double_click);
Dictionary single_target();
Dictionary drag_target();
Dictionary type_text();
Dictionary scroll_view();
Dictionary wait_start();
Dictionary wait_job();
Dictionary performance_start();
Dictionary performance_job();
Dictionary properties();
Dictionary set_property();
Dictionary input();
Dictionary input_sequence();
Dictionary input_sequence_status();
Dictionary input_sequence_cancel();

} // namespace MCPRuntimeToolSchemas
