/**************************************************************************/
/*  test_mcp_script_usages.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "editor/mcp/providers/mcp_script_usages.h"
#include "modules/gdscript/language_server/gdscript_analysis_session.h"
#include "modules/gdscript/language_server/gdscript_extend_parser.h"
#include "modules/gdscript/language_server/gdscript_workspace.h"
#include "tests/test_utils.h"

#endif

TEST_FORCE_LINK(test_mcp_script_usages);

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

namespace TestMCPScriptUsages {

class ScopedProjectResourcePath {
	String previous_path;

public:
	ScopedProjectResourcePath(const String &p_path) {
		previous_path = TestProjectSettingsInternalsAccessor::resource_path();
		TestProjectSettingsInternalsAccessor::resource_path() = p_path;
	}

	~ScopedProjectResourcePath() {
		TestProjectSettingsInternalsAccessor::resource_path() = previous_path;
	}
};

static Dictionary _member(const String &p_kind, const String &p_name, const String &p_owner = String()) {
	Dictionary member;
	member["kind"] = p_kind;
	member["name"] = p_name;
	if (!p_owner.is_empty()) {
		member["owner"] = p_owner;
	}
	return member;
}

static Dictionary _find(const Ref<GDScriptWorkspace> &p_workspace, const Ref<GDScriptAnalysisSession> &p_session,
		const ExtendGDScriptParser &p_parser, const Dictionary &p_member, bool p_include_declaration = false) {
	Dictionary result;
	String error;
	INFO(error);
	REQUIRE(MCPScriptUsages::find(p_workspace, p_session, p_parser, p_member, p_include_declaration, result, &error) == OK);
	return result;
}

TEST_CASE("[MCP][Provider] Script usages resolve named members through LSP symbols") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_script_usages", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());
	ScopedProjectResourcePath scoped_path(temporary_directory->get_current_dir());

	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisSession> session = memnew(GDScriptAnalysisSession(1, workspace));
	const String path = "res://usage_scene.tscn::GDScript_usage";
	const String source =
			"extends RefCounted\n"
			"var speed: int = 0 # speed documentation\n"
			"func move(amount: int) -> int:\n"
			"\tspeed += amount\n"
			"\treturn amount\n"
			"func call_move() -> void:\n"
			"\tvar icon := \"speed 😀\"; move(1)\n";
	REQUIRE(session->open_document(path, source, LSP::LanguageId::GDSCRIPT, 1) == OK);
	ExtendGDScriptParser *parser = session->get_parse_result(path);
	REQUIRE(parser);

	const Dictionary method_result = _find(workspace, session, *parser, _member("method", "move"));
	CHECK(int(method_result.get("count", -1)) == 1);
	const Array method_locations = method_result.get("locations", Array());
	REQUIRE(method_locations.size() == 1);
	const Dictionary method_range = Dictionary(method_locations[0]).get("range", Dictionary());
	const Dictionary method_start = method_range.get("start", Dictionary());
	CHECK(int(method_start.get("line", -1)) == 6);
	const int codepoint_character = source.get_slice("\n", 6).find("move");
	const LSP::Position expected_position = GDScriptAnalysisSession::codepoint_to_lsp_position(source, LSP::Position(6, codepoint_character));
	CHECK(int(method_start.get("character", -1)) == expected_position.character);

	const Dictionary method_with_declaration = _find(workspace, session, *parser, _member("method", "move"), true);
	CHECK(int(method_with_declaration.get("count", -1)) == 2);

	const Dictionary parameter_result = _find(workspace, session, *parser, _member("parameter", "amount", "move"));
	CHECK(int(parameter_result.get("count", -1)) == 2);
	const Dictionary normalized_parameter = parameter_result.get("member", Dictionary());
	CHECK(normalized_parameter.get("kind", String()) == "parameter");
	CHECK(normalized_parameter.get("owner", String()) == "move");
	CHECK(int(_find(workspace, session, *parser, _member("parameter", "amount", "move"), true).get("count", -1)) == 3);

	CHECK(int(_find(workspace, session, *parser, _member("property", "speed")).get("count", -1)) == 1);
	CHECK(int(_find(workspace, session, *parser, _member("property", "speed"), true).get("count", -1)) == 2);

	const String root_name = parser->get_symbols().name;
	const Dictionary root_result = _find(workspace, session, *parser, _member("class", root_name));
	CHECK(Dictionary(root_result.get("member", Dictionary())).get("name", String()) == root_name);

	const String external_path = "res://usage_external.gd";
	Ref<FileAccess> external_file = FileAccess::open(external_path, FileAccess::WRITE);
	REQUIRE(external_file.is_valid());
	REQUIRE(external_file->store_string(source));
	external_file.unref();
	Ref<GDScriptAnalysisSession> external_session = memnew(GDScriptAnalysisSession(3, workspace));
	REQUIRE(external_session->open_document(external_path, source, LSP::LanguageId::GDSCRIPT, 1) == OK);
	ExtendGDScriptParser *external_parser = external_session->get_parse_result(external_path);
	REQUIRE(external_parser);
	CHECK(int(_find(workspace, external_session, *external_parser, _member("property", "speed")).get("count", -1)) == 1);
}

TEST_CASE("[MCP][Provider] Script usages preserve selector errors") {
	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisSession> session = memnew(GDScriptAnalysisSession(2, workspace));
	const String path = "res://usage_errors.gd";
	const String source =
			"class First:\n"
			"\tfunc apply(value: int) -> void:\n"
			"\t\tprint(value)\n"
			"class Second:\n"
			"\tfunc apply(value: int) -> void:\n"
			"\t\tprint(value)\n";
	REQUIRE(session->open_document(path, source, LSP::LanguageId::GDSCRIPT, 1) == OK);
	ExtendGDScriptParser *parser = session->get_parse_result(path);
	REQUIRE(parser);

	Dictionary result;
	String error;
	CHECK(MCPScriptUsages::find(workspace, session, *parser, _member("parameter", "value", "apply"), false, result, &error) == ERR_ALREADY_EXISTS);
	CHECK(error.contains("ambiguous"));
	CHECK(MCPScriptUsages::find(workspace, session, *parser, _member("method", "missing"), false, result, &error) == ERR_DOES_NOT_EXIST);
}

} // namespace TestMCPScriptUsages

#endif // MODULE_GDSCRIPT_ENABLED && !GDSCRIPT_NO_LSP
