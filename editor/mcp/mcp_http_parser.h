/**************************************************************************/
/*  mcp_http_parser.h                                                     */
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
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

class MCPHTTPParser {
public:
	enum ParseResult {
		PARSE_INCOMPLETE,
		PARSE_READY,
		PARSE_HEADER_TOO_LARGE,
		PARSE_PAYLOAD_TOO_LARGE,
		PARSE_BAD_REQUEST,
	};

	struct Request {
		String method;
		String target;
		String path;
		String version;
		Dictionary headers;
		String body;
		int64_t content_length = 0;
		int64_t consumed_bytes = 0;
		bool has_content_length = false;
	};
	struct State {
		Request request;
		int header_scan_offset = 0;
		int header_end = -1;
		int64_t body_start = 0;
		bool headers_parsed = false;

		void reset() { *this = State(); }
	};

	static ParseResult parse(const Vector<uint8_t> &p_buffer, int64_t p_max_header_bytes, int64_t p_max_body_bytes, Request &r_request, String &r_error);
	static ParseResult parse_incremental(const Vector<uint8_t> &p_buffer, int64_t p_max_header_bytes, int64_t p_max_body_bytes, State &r_state, Request &r_request, String &r_error);
};
