/**************************************************************************/
/*  test_mcp_workspace_edit_planner.cpp                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
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

#include "tests/test_macros.h"

#include "modules/modules_enabled.gen.h"

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
#include "editor/mcp/providers/mcp_workspace_edit_planner.h"
#endif

TEST_FORCE_LINK(test_mcp_workspace_edit_planner);

namespace TestMCPWorkspaceEditPlanner {

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

static Dictionary _edit(int p_start_line, int p_start_character, int p_end_line, int p_end_character, const String &p_text) {
	Dictionary start;
	start["line"] = p_start_line;
	start["character"] = p_start_character;
	Dictionary end;
	end["line"] = p_end_line;
	end["character"] = p_end_character;
	Dictionary range;
	range["start"] = start;
	range["end"] = end;
	Dictionary edit;
	edit["range"] = range;
	edit["newText"] = p_text;
	return edit;
}

TEST_CASE("[MCP][Provider] Workspace edits apply descending UTF-16 ranges") {
	const String source = U"var face = \U0001f600\nprint(face)\n";
	Array edits;
	Dictionary json_round_trip_edit = _edit(1, 6, 1, 10, "expression");
	Dictionary json_range = json_round_trip_edit["range"];
	Dictionary json_start = json_range["start"];
	Dictionary json_end = json_range["end"];
	json_start["line"] = 1.0;
	json_start["character"] = 6.0;
	json_end["line"] = 1.0;
	json_end["character"] = 10.0;
	edits.push_back(json_round_trip_edit);
	edits.push_back(_edit(0, 4, 0, 8, "expression"));
	String result;
	String error;
	REQUIRE(MCPWorkspaceEditPlanner::apply(source, edits, result, &error) == OK);
	CHECK(result == U"var expression = \U0001f600\nprint(expression)\n");
	CHECK(error.is_empty());
}

TEST_CASE("[MCP][Provider] Workspace edits reject overlap and invalid UTF-16 positions") {
	const String source = U"var icon = \U0001f600\n";
	String result;
	String error;
	Array overlapping;
	overlapping.push_back(_edit(0, 4, 0, 8, "a"));
	overlapping.push_back(_edit(0, 6, 0, 9, "b"));
	CHECK(MCPWorkspaceEditPlanner::apply(source, overlapping, result, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("overlap"));
	Array same_position;
	same_position.push_back(_edit(0, 4, 0, 4, "a"));
	same_position.push_back(_edit(0, 4, 0, 4, "b"));
	REQUIRE(MCPWorkspaceEditPlanner::apply(source, same_position, result, &error) == OK);
	CHECK(result == U"var abicon = \U0001f600\n");

	Array insert_and_replace;
	insert_and_replace.push_back(_edit(0, 4, 0, 4, "a"));
	insert_and_replace.push_back(_edit(0, 4, 0, 8, "b"));
	CHECK(MCPWorkspaceEditPlanner::apply(source, insert_and_replace, result, &error) == ERR_INVALID_DATA);

	Array surrogate_split;
	surrogate_split.push_back(_edit(0, 12, 0, 12, "x"));
	CHECK(MCPWorkspaceEditPlanner::apply(source, surrogate_split, result, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("surrogate"));

	Array out_of_bounds;
	out_of_bounds.push_back(_edit(10, 0, 10, 0, "x"));
	CHECK(MCPWorkspaceEditPlanner::apply(source, out_of_bounds, result, &error) == ERR_INVALID_DATA);

	Dictionary fractional = _edit(0, 4, 0, 8, "x");
	Dictionary fractional_range = fractional["range"];
	Dictionary fractional_start = fractional_range["start"];
	fractional_start["character"] = 4.5;
	Array fractional_edits;
	fractional_edits.push_back(fractional);
	CHECK(MCPWorkspaceEditPlanner::apply(source, fractional_edits, result, &error) == ERR_INVALID_DATA);
}

#endif

} // namespace TestMCPWorkspaceEditPlanner
