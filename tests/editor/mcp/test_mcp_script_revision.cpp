/**************************************************************************/
/*  test_mcp_script_revision.cpp                                         */
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

#include "editor/mcp/providers/mcp_script_revision.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_script_revision);

namespace TestMCPScriptRevision {

TEST_CASE("[MCP][Provider] Script revisions accept exact revision or SHA expectations") {
	const String text = "extends Node\n";
	const uint32_t revision = 17;
	Dictionary state;
	String error;

	CHECK(MCPScriptRevision::validate(text, revision, int64_t(revision), Variant(), state, &error) == OK);
	CHECK(error.is_empty());
	CHECK(int64_t(state.get("currentRevision", -1)) == revision);
	CHECK(state.get("currentSha256", String()) == text.sha256_text());

	CHECK(MCPScriptRevision::validate(text, revision, Variant(), text.sha256_text().to_upper(), state, &error) == OK);
	CHECK(MCPScriptRevision::validate(text, revision, int64_t(revision), text.sha256_text(), state, &error) == OK);
}

TEST_CASE("[MCP][Provider] Script revisions return current state for stale edits") {
	const String text = "var value = 1\n";
	Dictionary state;
	String error;

	CHECK(MCPScriptRevision::validate(text, 9, int64_t(8), Variant(), state, &error) == ERR_BUSY);
	CHECK(error.contains("changed"));
	CHECK(int64_t(state.get("currentRevision", -1)) == 9);
	CHECK(int64_t(state.get("expectedRevision", -1)) == 8);
	CHECK(state.get("currentSha256", String()) == text.sha256_text());

	CHECK(MCPScriptRevision::validate(text, 9, int64_t(9), String("0").repeat(64), state, &error) == ERR_BUSY);
}

TEST_CASE("[MCP][Provider] Script revisions reject missing and malformed expectations") {
	Dictionary state;
	String error;
	CHECK(MCPScriptRevision::validate("text", 1, Variant(), Variant(), state, &error) == ERR_INVALID_PARAMETER);
	CHECK(MCPScriptRevision::validate("text", 1, "1", Variant(), state, &error) == ERR_INVALID_PARAMETER);
	CHECK(MCPScriptRevision::validate("text", 1, -1, Variant(), state, &error) == ERR_PARAMETER_RANGE_ERROR);
	CHECK(MCPScriptRevision::validate("text", 1, Variant(), "not-a-sha", state, &error) == ERR_INVALID_PARAMETER);
}

} // namespace TestMCPScriptRevision
