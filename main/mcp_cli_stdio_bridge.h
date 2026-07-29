/**************************************************************************/
/*  mcp_cli_stdio_bridge.h                                                */
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

#include "core/mcp/mcp_discovery.h"
#include "main/mcp_cli_http_transport.h"
#include "main/mcp_cli_stdio_io.h"

class MCPCLIStdioBridge {
public:
	enum Result {
		RESULT_OK,
		RESULT_TRANSPORT_FAILED,
		RESULT_PROTOCOL_FAILED,
	};

	struct Limits {
		uint64_t max_line_bytes = 1024 * 1024;
		uint64_t max_response_bytes = 4 * 1024 * 1024;
		uint64_t request_timeout_ms = 10000;
	};

private:
	MCPCLITransport *transport = nullptr;
	MCPCLIIO *io = nullptr;
	MCPDiscoveryRecord record;
	Limits limits;

	void _diagnose(const String &p_message) const;

public:
	MCPCLIStdioBridge(MCPCLITransport *p_transport, MCPCLIIO *p_io, const MCPDiscoveryRecord &p_record, const Limits &p_limits);
	Result run();
};
