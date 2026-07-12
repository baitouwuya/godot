/**************************************************************************/
/*  test_mcp_http_parser.cpp                                              */
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

#include "editor/mcp/mcp_http_parser.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_http_parser)

namespace TestMCPHTTPParser {

Vector<uint8_t> bytes(const String &p_text) {
	const CharString utf8 = p_text.utf8();
	Vector<uint8_t> result;
	result.resize(utf8.length());
	if (!result.is_empty()) {
		memcpy(result.ptrw(), utf8.get_data(), utf8.length());
	}
	return result;
}

TEST_CASE("[MCP] HTTP parser accepts a complete request") {
	const String body = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":1}";
	const String wire = "POST /mcp?project=test HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: " + itos(body.utf8().length()) + "\r\n\r\n" + body;

	MCPHTTPParser::Request request;
	String error;
	CHECK(MCPHTTPParser::parse(bytes(wire), 8192, 1024, request, error) == MCPHTTPParser::PARSE_READY);
	CHECK(error.is_empty());
	CHECK(request.method == "POST");
	CHECK(request.path == "/mcp");
	CHECK(request.body == body);
	CHECK(request.consumed_bytes == wire.utf8().length());
}

TEST_CASE("[MCP] HTTP parser waits for a partial body") {
	const String wire = "POST /mcp HTTP/1.1\r\nContent-Length: 5\r\n\r\nabc";
	MCPHTTPParser::Request request;
	String error;
	CHECK(MCPHTTPParser::parse(bytes(wire), 8192, 1024, request, error) == MCPHTTPParser::PARSE_INCOMPLETE);
}

TEST_CASE("[MCP] HTTP parser reports request limits") {
	MCPHTTPParser::Request request;
	String error;
	CHECK(MCPHTTPParser::parse(bytes("POST /mcp HTTP/1.1\r\nLong: value"), 8, 1024, request, error) == MCPHTTPParser::PARSE_HEADER_TOO_LARGE);
	CHECK(MCPHTTPParser::parse(bytes("POST /mcp HTTP/1.1\r\nContent-Length: 1025\r\n\r\n"), 8192, 1024, request, error) == MCPHTTPParser::PARSE_PAYLOAD_TOO_LARGE);
}

TEST_CASE("[MCP] HTTP parser rejects ambiguous framing") {
	MCPHTTPParser::Request request;
	String error;
	CHECK(MCPHTTPParser::parse(bytes("POST /mcp HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n"), 8192, 1024, request, error) == MCPHTTPParser::PARSE_BAD_REQUEST);
	CHECK(MCPHTTPParser::parse(bytes("POST /mcp HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"), 8192, 1024, request, error) == MCPHTTPParser::PARSE_BAD_REQUEST);
}

TEST_CASE("[MCP] HTTP parser exposes one request boundary") {
	const String first = "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n";
	const String second = "GET /next HTTP/1.1\r\nHost: localhost\r\n\r\n";
	MCPHTTPParser::Request request;
	String error;
	CHECK(MCPHTTPParser::parse(bytes(first + second), 8192, 1024, request, error) == MCPHTTPParser::PARSE_READY);
	CHECK(request.path == "/health");
	CHECK(request.consumed_bytes == first.utf8().length());
}

} // namespace TestMCPHTTPParser
