/**************************************************************************/
/*  mcp_cli_runtime.h                                                     */
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

#include "core/variant/typed_array.h"
#include "main/mcp_cli_command_registry.h"

class MCPCLI;

class MCPCLIRuntime {
	MCPCLICommandRegistry command_registry;
	MCPCLI *adapter = nullptr;
	StringName selected_command;
	PackedStringArray selected_raw_arguments;
	uint64_t selected_raw_argument_bytes = 0;

	void _clear_selection();

public:
	static constexpr int MAX_RAW_ARGUMENT_COUNT = 256;
	static constexpr uint64_t MAX_RAW_ARGUMENT_BYTES = 1024 * 1024;

	MCPCLIRuntime() = default;
	explicit MCPCLIRuntime(MCPCLI *p_adapter) :
			adapter(p_adapter) {}
	MCPCLIRuntime(const MCPCLIRuntime &) = delete;
	MCPCLIRuntime &operator=(const MCPCLIRuntime &) = delete;

	void clear();

	const MCPCLICommandRegistry &get_command_registry() const { return command_registry; }

	Error select_command(const StringName &p_name, String *r_error = nullptr);
	bool has_selected_command() const { return selected_command != StringName(); }
	const MCPCLICommandDefinition *get_selected_definition() const;
	Error append_raw_argument(const String &p_argument, String *r_error = nullptr);
	const PackedStringArray &get_raw_arguments() const { return selected_raw_arguments; }
	Error invoke_selected(const PackedStringArray &p_arguments, int &r_exit_code, String *r_error = nullptr) const;
};
