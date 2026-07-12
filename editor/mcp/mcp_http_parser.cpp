/**************************************************************************/
/*  mcp_http_parser.cpp                                                   */
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

#include "mcp_http_parser.h"

namespace {

int find_header_end(const Vector<uint8_t> &p_buffer) {
	for (int i = 0; i + 3 < p_buffer.size(); i++) {
		if (p_buffer[i] == '\r' && p_buffer[i + 1] == '\n' && p_buffer[i + 2] == '\r' && p_buffer[i + 3] == '\n') {
			return i;
		}
	}
	return -1;
}

bool is_http_token(const String &p_value) {
	if (p_value.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_value.length(); i++) {
		const char32_t c = p_value[i];
		if (c <= 0x20 || c >= 0x7f || String("()<>@,;:\\\"/[]?={} ").contains_char(c)) {
			return false;
		}
	}
	return true;
}

bool is_valid_header_value(const String &p_value) {
	for (int i = 0; i < p_value.length(); i++) {
		const char32_t c = p_value[i];
		if ((c < 0x20 && c != '\t') || c == 0x7f) {
			return false;
		}
	}
	return true;
}

MCPHTTPParser::ParseResult parse_content_length(const String &p_value, int64_t p_max_body_bytes, int64_t &r_content_length, String &r_error) {
	if (p_value.is_empty()) {
		r_error = "Invalid Content-Length header.";
		return MCPHTTPParser::PARSE_BAD_REQUEST;
	}

	int64_t content_length = 0;
	for (int i = 0; i < p_value.length(); i++) {
		const char32_t c = p_value[i];
		if (c < '0' || c > '9') {
			r_error = "Invalid Content-Length header.";
			return MCPHTTPParser::PARSE_BAD_REQUEST;
		}
		const int digit = c - '0';
		if (content_length > p_max_body_bytes / 10 || (content_length == p_max_body_bytes / 10 && digit > p_max_body_bytes % 10)) {
			return MCPHTTPParser::PARSE_PAYLOAD_TOO_LARGE;
		}
		content_length = content_length * 10 + digit;
	}

	r_content_length = content_length;
	return MCPHTTPParser::PARSE_READY;
}

} // namespace

MCPHTTPParser::ParseResult MCPHTTPParser::parse(const Vector<uint8_t> &p_buffer, int64_t p_max_header_bytes, int64_t p_max_body_bytes, Request &r_request, String &r_error) {
	r_request = Request();
	r_error = String();

	if (p_max_header_bytes <= 0 || p_max_body_bytes < 0) {
		r_error = "Invalid HTTP parser limits.";
		return PARSE_BAD_REQUEST;
	}

	const int header_end = find_header_end(p_buffer);
	if (header_end < 0) {
		return p_buffer.size() > p_max_header_bytes ? PARSE_HEADER_TOO_LARGE : PARSE_INCOMPLETE;
	}
	const int64_t body_start = (int64_t)header_end + 4;
	if (body_start > p_max_header_bytes) {
		return PARSE_HEADER_TOO_LARGE;
	}

	for (int i = 0; i < header_end; i++) {
		if (p_buffer[i] > 0x7f) {
			r_error = "HTTP request headers must be ASCII.";
			return PARSE_BAD_REQUEST;
		}
	}

	const String header_text = String::utf8((const char *)p_buffer.ptr(), header_end);
	const PackedStringArray lines = header_text.split("\r\n");
	if (lines.is_empty()) {
		r_error = "HTTP request line is missing.";
		return PARSE_BAD_REQUEST;
	}

	const PackedStringArray request_line = lines[0].split(" ", false);
	if (request_line.size() != 3 || !is_http_token(request_line[0]) || request_line[1].is_empty() || (request_line[2] != "HTTP/1.1" && request_line[2] != "HTTP/1.0")) {
		r_error = "Invalid HTTP request line.";
		return PARSE_BAD_REQUEST;
	}

	r_request.method = request_line[0].to_upper();
	r_request.target = request_line[1];
	r_request.version = request_line[2];
	if (!r_request.target.begins_with("/")) {
		r_error = "HTTP request target must use origin-form.";
		return PARSE_BAD_REQUEST;
	}
	const int query_position = r_request.target.find("?");
	r_request.path = query_position < 0 ? r_request.target : r_request.target.substr(0, query_position);

	for (int i = 1; i < lines.size(); i++) {
		const int separator = lines[i].find(":");
		if (separator <= 0) {
			r_error = "Malformed HTTP header.";
			return PARSE_BAD_REQUEST;
		}
		const String name = lines[i].substr(0, separator).to_lower();
		const String value = lines[i].substr(separator + 1).strip_edges();
		if (!is_http_token(name)) {
			r_error = "Invalid HTTP header name.";
			return PARSE_BAD_REQUEST;
		}
		if (!is_valid_header_value(value)) {
			r_error = "Invalid HTTP header value.";
			return PARSE_BAD_REQUEST;
		}
		if (r_request.headers.has(name)) {
			r_error = name == "content-length" ? "Invalid or duplicate Content-Length header." : "Duplicate HTTP header: " + name;
			return PARSE_BAD_REQUEST;
		}
		if (name == "transfer-encoding") {
			r_error = "Transfer-Encoding is not supported.";
			return PARSE_BAD_REQUEST;
		}
		if (name == "content-length") {
			r_request.has_content_length = true;
			const ParseResult length_result = parse_content_length(value, p_max_body_bytes, r_request.content_length, r_error);
			if (length_result != PARSE_READY) {
				return length_result;
			}
		}
		r_request.headers[name] = value;
	}

	if ((int64_t)p_buffer.size() - body_start < r_request.content_length) {
		return PARSE_INCOMPLETE;
	}
	if (r_request.content_length > 0) {
		const Error utf8_error = r_request.body.append_utf8((const char *)p_buffer.ptr() + body_start, (int)r_request.content_length);
		if (utf8_error != OK) {
			r_error = "HTTP request body is not valid UTF-8.";
			return PARSE_BAD_REQUEST;
		}
	}
	r_request.consumed_bytes = body_start + r_request.content_length;
	return PARSE_READY;
}
