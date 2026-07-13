/**************************************************************************/
/*  test_mcp_script_document.cpp                                         */
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

#include "tests/test_macros.h"

#include "modules/modules_enabled.gen.h"

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

#include "editor/mcp/providers/mcp_script_document.h"
#include "modules/gdscript/language_server/gdscript_extend_parser.h"

#endif

TEST_FORCE_LINK(test_mcp_script_document);

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

namespace TestMCPScriptDocument {

static String _line_text(const Array &p_lines) {
	String text;
	for (int i = 0; i < p_lines.size(); i++) {
		text += String(Dictionary(p_lines[i]).get("text", String())) + "\n";
	}
	return text;
}

TEST_CASE("[MCP][Provider] Script documentation uses LSP symbols without implementation text") {
	const String source =
			"## Actor documentation.\n"
			"extends Node\n"
			"\n"
			"## Configured movement speed.\n"
			"@export_range(0.0, 20.0) var speed: float = 4.0\n"
			"\n"
			"## Emitted after movement.\n"
			"signal moved(distance: float)\n"
			"\n"
			"## Moves by the requested amount.\n"
			"func move(amount: float) -> void:\n"
			"\tvar implementation_sentinel := amount * speed\n"
			"\tprint(implementation_sentinel)\n";

	ExtendGDScriptParser parser;
	parser.parse(source, "res://actor.gd");
	REQUIRE(parser.parse_result == OK);

	Dictionary document;
	String error;
	REQUIRE(MCPScriptDocument::render(parser, "documentation", true, Dictionary(), document, &error) == OK);
	CHECK(error.is_empty());
	CHECK(document.get("view", String()) == "documentation");
	CHECK_FALSE(document.has("lines"));
	CHECK_FALSE(document.has("lineBase"));
	CHECK(String(document.get("documentation", String())).contains("Actor documentation"));
	const Array members = document.get("members", Array());
	REQUIRE(members.size() == 3);
	const Dictionary speed = members[0];
	CHECK(speed.get("kind", String()) == "property");
	CHECK(String(speed.get("documentation", String())).contains("Configured movement speed"));
	CHECK(speed.get("declaration", String()) == "@export_range(0.0, 20.0) var speed: float = 4.0");
	CHECK(int(speed.get("line", 0)) == 5);
	CHECK(Dictionary(speed.get("range", Dictionary())).has("start"));
	const Dictionary moved = members[1];
	CHECK(moved.get("kind", String()) == "signal");
	CHECK(moved.get("declaration", String()) == "signal moved(distance: float)");
	const Array signal_parameters = moved.get("parameters", Array());
	REQUIRE(signal_parameters.size() == 1);
	CHECK(String(Dictionary(signal_parameters[0]).get("name", String())) == "distance");
	CHECK(Dictionary(signal_parameters[0]).get("type", String()) == "float");
	const Dictionary move = members[2];
	CHECK(move.get("kind", String()) == "method");
	CHECK(String(move.get("documentation", String())).contains("Moves by the requested amount"));
	CHECK(move.get("declaration", String()) == "func move(amount: float) -> void:");
	CHECK(int(move.get("line", 0)) == 11);
	const Array method_parameters = move.get("parameters", Array());
	REQUIRE(method_parameters.size() == 1);
	CHECK(String(Dictionary(method_parameters[0]).get("name", String())) == "amount");
	CHECK(int(Dictionary(method_parameters[0]).get("line", 0)) == 11);

	Dictionary without_comments;
	REQUIRE(MCPScriptDocument::render(parser, "documentation", false, Dictionary(), without_comments, &error) == OK);
	CHECK_FALSE(without_comments.has("documentation"));
	const Array uncommented_members = without_comments.get("members", Array());
	REQUIRE(uncommented_members.size() == 3);
	CHECK_FALSE(Dictionary(uncommented_members[0]).has("documentation"));
	CHECK(String(Dictionary(uncommented_members[2]).get("declaration", String())).contains("func move"));

	Dictionary full;
	REQUIRE(MCPScriptDocument::render(parser, "full", true, Dictionary(), full, &error) == OK);
	CHECK(full.get("text", String()) == source);
	const Array full_lines = full.get("lines", Array());
	REQUIRE_FALSE(full_lines.is_empty());
	CHECK(int(Dictionary(full_lines[0]).get("line", 0)) == 1);
	CHECK_FALSE(full.has("lineBase"));
	CHECK_FALSE(full.has("members"));
	CHECK(_line_text(full_lines).contains("implementation_sentinel"));
}

TEST_CASE("[MCP][Provider] Script documentation can select individual members and parameters") {
	const String source =
			"extends Node\n"
			"## Speed docs.\n"
			"@export var speed: float = 1.0\n"
			"signal moved(distance: float)\n"
			"func move(amount: float, ...rest) -> void:\n"
			"\tvar local_only: float = amount\n"
			"\tprint(local_only, rest)\n";
	ExtendGDScriptParser parser;
	parser.parse(source, "res://selection.gd");
	REQUIRE(parser.parse_result == OK);

	Dictionary query;
	query["kind"] = "property";
	query["name"] = "speed";
	Dictionary result;
	String error;
	REQUIRE(MCPScriptDocument::render(parser, "documentation", true, query, result, &error) == OK);
	CHECK(int(result.get("memberCount", 0)) == 1);
	CHECK_FALSE(result.has("lines"));
	const Array selected_members = result.get("members", Array());
	REQUIRE(selected_members.size() == 1);
	CHECK(Dictionary(selected_members[0]).get("name", String()) == "speed");

	query["kind"] = "method";
	query["name"] = "move";
	REQUIRE(MCPScriptDocument::render(parser, "full", true, query, result, &error) == OK);
	CHECK(String(result.get("text", String())).contains("print(local_only, rest)"));

	query["kind"] = "signal";
	query["name"] = "moved";
	REQUIRE(MCPScriptDocument::render(parser, "documentation", true, query, result, &error) == OK);
	CHECK(Dictionary(Array(result.get("members", Array()))[0]).get("declaration", String()) == "signal moved(distance: float)");

	query["kind"] = "parameter";
	query["name"] = "amount";
	query["owner"] = "move";
	REQUIRE(MCPScriptDocument::render(parser, "documentation", true, query, result, &error) == OK);
	CHECK(String(Dictionary(Array(result.get("members", Array()))[0]).get("declaration", String())).contains("amount: float"));

	query["kind"] = "method";
	query["name"] = "move";
	query.erase("owner");
	REQUIRE(MCPScriptDocument::render(parser, "documentation", true, query, result, &error) == OK);
	const Array parameters = Dictionary(Array(result.get("members", Array()))[0]).get("parameters", Array());
	REQUIRE(parameters.size() == 2);
	CHECK(bool(Dictionary(parameters[1]).get("variadic", false)));

	query["kind"] = "parameter";
	query["name"] = "local_only";
	query["owner"] = "move";
	CHECK(MCPScriptDocument::render(parser, "documentation", true, query, result, &error) == ERR_DOES_NOT_EXIST);

	query["kind"] = "method";
	query["name"] = "missing";
	query.erase("owner");
	CHECK(MCPScriptDocument::render(parser, "documentation", true, query, result, &error) == ERR_DOES_NOT_EXIST);
	CHECK_FALSE(error.is_empty());
}

} // namespace TestMCPScriptDocument

#endif // MODULE_GDSCRIPT_ENABLED && !GDSCRIPT_NO_LSP
