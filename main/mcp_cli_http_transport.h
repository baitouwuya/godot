/**************************************************************************/
/*  mcp_cli_http_transport.h                                              */
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

#include "core/error/error_list.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

struct MCPCLIHTTPResponse {
	int status_code = 0;
	Dictionary headers;
	PackedByteArray body;
};

class MCPCLITransport {
public:
	enum RequestMethod {
		REQUEST_POST,
		REQUEST_DELETE,
	};

	virtual Error request(
			RequestMethod p_method,
			const String &p_endpoint,
			const Vector<String> &p_headers,
			const String &p_body,
			uint64_t p_timeout_ms,
			uint64_t p_max_response_bytes,
			MCPCLIHTTPResponse &r_response,
			String &r_error) = 0;
	virtual ~MCPCLITransport() = default;
};

class MCPHTTPCLITransport final : public MCPCLITransport {
public:
	static Error parse_endpoint(
			const String &p_endpoint,
			String &r_scheme,
			String &r_host,
			int &r_port,
			String &r_path,
			String &r_fragment,
			String &r_error);
	static bool is_visible_ascii_header_value(const String &p_value);

	Error request(
			RequestMethod p_method,
			const String &p_endpoint,
			const Vector<String> &p_headers,
			const String &p_body,
			uint64_t p_timeout_ms,
			uint64_t p_max_response_bytes,
			MCPCLIHTTPResponse &r_response,
			String &r_error) override;
};
