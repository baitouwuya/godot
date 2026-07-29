/**************************************************************************/
/*  mcp_cli_stdio_io.h                                                    */
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

#include "core/string/ustring.h"
#include "core/templates/vector.h"

class MCPCLIIO {
public:
	enum ReadStatus {
		READ_LINE,
		READ_EOF,
		READ_TOO_LARGE,
		READ_INVALID_ENCODING,
	};

	virtual ReadStatus read_line(String &r_line, uint64_t p_max_line_bytes) = 0;
	virtual void write_stdout_line(const String &p_line) = 0;
	virtual void write_stderr_line(const String &p_line) = 0;
	virtual ~MCPCLIIO() = default;
};

class MCPOSCLIIO final : public MCPCLIIO {
	Vector<uint8_t> pending_input;
	bool input_finished = false;

public:
	ReadStatus read_line(String &r_line, uint64_t p_max_line_bytes) override;
	void write_stdout_line(const String &p_line) override;
	void write_stderr_line(const String &p_line) override;
};
