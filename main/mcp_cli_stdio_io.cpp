/**************************************************************************/
/*  mcp_cli_stdio_io.cpp                                                  */
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

#include "mcp_cli_stdio_io.h"

#include "core/os/os.h"

#include <cstdio>

namespace {

constexpr uint64_t STDIN_READ_CHUNK_BYTES = 8192;

} // namespace

MCPCLIIO::ReadStatus MCPOSCLIIO::read_line(String &r_line, uint64_t p_max_line_bytes) {
	r_line = String();
	if (p_max_line_bytes == 0) {
		return READ_TOO_LARGE;
	}

	while (true) {
		int line_end = -1;
		for (int i = 0; i < pending_input.size(); i++) {
			if (pending_input[i] == '\n') {
				line_end = i;
				break;
			}
		}

		if (line_end >= 0 || (input_finished && !pending_input.is_empty())) {
			const int consumed_bytes = line_end >= 0 ? line_end + 1 : pending_input.size();
			int line_bytes = line_end >= 0 ? line_end : pending_input.size();
			if (line_bytes > 0 && pending_input[line_bytes - 1] == '\r') {
				line_bytes--;
			}
			const bool too_large = (uint64_t)line_bytes > p_max_line_bytes;
			Error decode_error = OK;
			if (!too_large && line_bytes > 0) {
				decode_error = r_line.append_utf8((const char *)pending_input.ptr(), line_bytes);
			}
			pending_input = pending_input.slice(consumed_bytes, pending_input.size());
			if (too_large) {
				return READ_TOO_LARGE;
			}
			return decode_error == OK ? READ_LINE : READ_INVALID_ENCODING;
		}

		if ((uint64_t)pending_input.size() > p_max_line_bytes) {
			pending_input.clear();
			return READ_TOO_LARGE;
		}
		if (input_finished) {
			return READ_EOF;
		}

		PackedByteArray chunk = OS::get_singleton()->get_stdin_buffer(STDIN_READ_CHUNK_BYTES);
		int chunk_size = chunk.size();
		// Windows keeps the requested buffer size and zero-fills bytes beyond the read count.
		for (int i = 0; i < chunk_size; i++) {
			if (chunk[i] == 0) {
				chunk_size = i;
				break;
			}
		}
		if (chunk_size == 0) {
			input_finished = true;
			continue;
		}
		const int old_size = pending_input.size();
		pending_input.resize(old_size + chunk_size);
		memcpy(pending_input.ptrw() + old_size, chunk.ptr(), chunk_size);
	}
}

void MCPOSCLIIO::write_stdout_line(const String &p_line) {
	const CharString line = p_line.utf8();
	fwrite(line.get_data(), 1, line.length(), stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

void MCPOSCLIIO::write_stderr_line(const String &p_line) {
	const CharString line = p_line.utf8();
	fwrite(line.get_data(), 1, line.length(), stderr);
	fputc('\n', stderr);
	fflush(stderr);
}
