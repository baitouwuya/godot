/**************************************************************************/
/*  test_lsp.h                                                            */
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

#ifdef TOOLS_ENABLED

#include "modules/modules_enabled.gen.h" // For jsonrpc.

#ifdef MODULE_JSONRPC_ENABLED

#include <initializer_list>

#include "tests/test_macros.h"

#include "../language_server/gdscript_extend_parser.h"
#include "../language_server/gdscript_lsp_cli_runner.h"
#include "../language_server/gdscript_language_protocol.h"
#include "../language_server/gdscript_workspace.h"
#include "../language_server/godot_lsp.h"

#include "core/io/dir_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/json.h"
#include "main/custom_feature_tracer.h"
#include "core/os/os.h"
#include "editor/doc/editor_help.h"
#include "editor/editor_node.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/regex/regex.h"

#include "tests/main/test_custom_feature_trace_helpers.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

class TestGDScriptLanguageProtocolInitializer {
public:
	static void setup_client() {
		GDScriptLanguageProtocol *proto = GDScriptLanguageProtocol::get_singleton();
		Ref<GDScriptLanguageProtocol::LSPeer> peer = memnew(GDScriptLanguageProtocol::LSPeer);
		proto->clients.insert(proto->next_client_id, peer);
		proto->latest_client_id = proto->next_client_id;
		proto->next_client_id++;
	}
};

template <>
struct doctest::StringMaker<LSP::Position> {
	static doctest::String convert(const LSP::Position &p_val) {
		return p_val.to_string().utf8().get_data();
	}
};

template <>
struct doctest::StringMaker<LSP::Range> {
	static doctest::String convert(const LSP::Range &p_val) {
		return p_val.to_string().utf8().get_data();
	}
};

template <>
struct doctest::StringMaker<GodotPosition> {
	static doctest::String convert(const GodotPosition &p_val) {
		return p_val.to_string().utf8().get_data();
	}
};

namespace GDScriptTests {

// LSP GDScript test scripts are located inside project of other GDScript tests:
// Cannot reset `ProjectSettings` (singleton) -> Cannot load another workspace and resources in there.
// -> Reuse GDScript test project. LSP specific scripts are then placed inside `lsp` folder.
//    Access via `res://lsp/my_script.gd`.
const String root = "modules/gdscript/tests/scripts/";

/*
 * After use:
 * * `memdelete` returned `GDScriptLanguageProtocol`.
 * * Call `GDScriptTests::::finish_language`.
 */
GDScriptLanguageProtocol *initialize(const String &p_root) {
	Error err = OK;
	Ref<DirAccess> dir(DirAccess::open(p_root, &err));
	REQUIRE_MESSAGE(err == OK, "Could not open specified root directory");
	String absolute_root = dir->get_current_dir();
	init_language(absolute_root);

	GDScriptLanguageProtocol *proto = memnew(GDScriptLanguageProtocol);
	TestGDScriptLanguageProtocolInitializer::setup_client();

	Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();
	workspace->root = absolute_root;
	// On windows: `C:/...` -> `C%3A/...`.
	workspace->root_uri = "file:///" + absolute_root.lstrip("/").replace_first(":", "%3A");

	return proto;
}

LSP::Position pos(const int p_line, const int p_character) {
	LSP::Position p;
	p.line = p_line;
	p.character = p_character;
	return p;
}

LSP::Range range(const LSP::Position p_start, const LSP::Position p_end) {
	LSP::Range r;
	r.start = p_start;
	r.end = p_end;
	return r;
}

LSP::TextDocumentPositionParams pos_in(const LSP::DocumentUri &p_uri, const LSP::Position p_pos) {
	LSP::TextDocumentPositionParams params;
	params.textDocument.uri = p_uri;
	params.position = p_pos;
	return params;
}

const LSP::DocumentSymbol *test_resolve_symbol_at(const String &p_uri, const LSP::Position p_pos, const String &p_expected_uri, const String &p_expected_name, const LSP::Range &p_expected_range) {
	Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();

	LSP::TextDocumentPositionParams params = pos_in(p_uri, p_pos);
	const LSP::DocumentSymbol *symbol = workspace->resolve_symbol(params);
	CHECK(symbol);

	if (symbol) {
		CHECK_EQ(symbol->uri, p_expected_uri);
		CHECK_EQ(symbol->name, p_expected_name);
		CHECK_EQ(symbol->selectionRange, p_expected_range);
	}

	return symbol;
}

struct InlineTestData {
	LSP::Range range;
	String text;
	String name;
	String ref;

	static bool try_parse(const Vector<String> &p_lines, const int p_line_number, InlineTestData &r_data) {
		String line = p_lines[p_line_number];

		RegEx regex = RegEx("^\\t*#[ |]*(?<range>(?<left><)?\\^+)(\\s+(?<name>(?!->)\\S+))?(\\s+->\\s+(?<ref>\\S+))?");
		Ref<RegExMatch> match = regex.search(line);
		if (match.is_null()) {
			return false;
		}

		// Find first line without leading comment above current line.
		int target_line = p_line_number;
		while (target_line >= 0) {
			String dedented = p_lines[target_line].lstrip("\t");
			if (!dedented.begins_with("#")) {
				break;
			}
			target_line--;
		}
		if (target_line < 0) {
			return false;
		}
		r_data.range.start.line = r_data.range.end.line = target_line;

		String marker = match->get_string("range");
		int i = line.find(marker);
		REQUIRE(i >= 0);
		r_data.range.start.character = i;
		if (!match->get_string("left").is_empty()) {
			// Include `#` (comment char) in range.
			r_data.range.start.character--;
		}
		r_data.range.end.character = i + marker.length();

		String target = p_lines[target_line];
		r_data.text = target.substr(r_data.range.start.character, r_data.range.end.character - r_data.range.start.character);

		r_data.name = match->get_string("name");
		r_data.ref = match->get_string("ref");

		return true;
	}
};

Vector<InlineTestData> read_tests(const String &p_path) {
	Error err;
	String source = FileAccess::get_file_as_string(p_path, &err);
	REQUIRE_MESSAGE(err == OK, vformat("Cannot read '%s'", p_path));

	// Format:
	// ```gdscript
	// var foo = bar + baz
	// #   | |   | |   ^^^ name -> ref
	// #   | |   ^^^ -> ref
	// #   ^^^ name
	//
	// func my_func():
	// #    ^^^^^^^ name
	//     var value = foo + 42
	//     #   ^^^^^ name
	//     print(value)
	//     #     ^^^^^ -> ref
	// ```
	//
	// * `^`: Range marker.
	// * `name`: Unique name. Can contain any characters except whitespace chars.
	// * `ref`: Reference to unique name.
	//
	// Notes:
	// * If range should include first content-char (which is occupied by `#`): use `<` for next marker.
	//   -> Range expands 1 to left (-> includes `#`).
	//   * Note: Means: Range cannot be single char directly marked by `#`, but must be at least two chars (marked with `#<`).
	// * Comment must start at same ident as line its marked (-> because of tab alignment...).
	// * Use spaces to align after `#`! -> for correct alignment
	// * Between `#` and `^` can be spaces or `|` (to better visualize what's marked below).
	PackedStringArray lines = source.split("\n");

	PackedStringArray names;
	Vector<InlineTestData> data;
	for (int i = 0; i < lines.size(); i++) {
		InlineTestData d;
		if (InlineTestData::try_parse(lines, i, d)) {
			if (!d.name.is_empty()) {
				// Safety check: names must be unique.
				if (names.has(d.name)) {
					FAIL(vformat("Duplicated name '%s' in '%s'. Names must be unique!", d.name, p_path));
				}
				names.append(d.name);
			}

			data.append(d);
		}
	}

	return data;
}

void test_resolve_symbol(const String &p_uri, const InlineTestData &p_test_data, const Vector<InlineTestData> &p_all_data) {
	if (p_test_data.ref.is_empty()) {
		return;
	}

	SUBCASE(vformat("Can resolve symbol '%s' at %s to '%s'", p_test_data.text, p_test_data.range.to_string(), p_test_data.ref).utf8().get_data()) {
		const InlineTestData *target = nullptr;
		for (int i = 0; i < p_all_data.size(); i++) {
			if (p_all_data[i].name == p_test_data.ref) {
				target = &p_all_data[i];
				break;
			}
		}
		REQUIRE_MESSAGE(target, vformat("No target for ref '%s'", p_test_data.ref));

		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();
		LSP::Position pos = p_test_data.range.start;

		SUBCASE("start of identifier") {
			pos.character = p_test_data.range.start.character;
			test_resolve_symbol_at(p_uri, pos, p_uri, target->text, target->range);
		}

		SUBCASE("inside identifier") {
			pos.character = (p_test_data.range.end.character + p_test_data.range.start.character) / 2;
			test_resolve_symbol_at(p_uri, pos, p_uri, target->text, target->range);
		}

		SUBCASE("end of identifier") {
			pos.character = p_test_data.range.end.character;
			test_resolve_symbol_at(p_uri, pos, p_uri, target->text, target->range);
		}
	}
}

Vector<InlineTestData> filter_ref_towards(const Vector<InlineTestData> &p_data, const String &p_name) {
	Vector<InlineTestData> res;

	for (const InlineTestData &d : p_data) {
		if (d.ref == p_name) {
			res.append(d);
		}
	}

	return res;
}

void test_resolve_symbols(const String &p_uri, const Vector<InlineTestData> &p_test_data, const Vector<InlineTestData> &p_all_data) {
	for (const InlineTestData &d : p_test_data) {
		test_resolve_symbol(p_uri, d, p_all_data);
	}
}

void assert_no_errors_in(const String &p_path) {
	Error err;
	String source = FileAccess::get_file_as_string(p_path, &err);
	REQUIRE_MESSAGE(err == OK, vformat("Cannot read '%s'", p_path));

	GDScriptParser parser;
	err = parser.parse(source, p_path, true);
	REQUIRE_MESSAGE(err == OK, vformat("Errors while parsing '%s'", p_path));

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE_MESSAGE(err == OK, vformat("Errors while analyzing '%s'", p_path));
}

inline LSP::Position lsp_pos(int line, int character) {
	LSP::Position p;
	p.line = line;
	p.character = character;
	return p;
}

void test_position_roundtrip(LSP::Position p_lsp, GodotPosition p_gd, const PackedStringArray &p_lines) {
	GodotPosition actual_gd = GodotPosition::from_lsp(p_lsp, p_lines);
	CHECK_EQ(p_gd, actual_gd);
	LSP::Position actual_lsp = p_gd.to_lsp(p_lines);
	CHECK_EQ(p_lsp, actual_lsp);
}

struct ParsedCLIOptionsResult {
	GDScriptLSPCLIRunner::Options options;
	String error;
};

ParsedCLIOptionsResult parse_cli_options(const std::initializer_list<const char *> &p_args) {
	List<String> args;
	for (const char *arg : p_args) {
		args.push_back(String(arg));
	}

	ParsedCLIOptionsResult result;
	const bool has_entrypoint_argument = GDScriptLSPCLIRunner::has_entrypoint_argument(args);
	for (List<String>::Element *E = args.front(); E;) {
		List<String>::Element *N = E->next();
		String error;
		GDScriptLSPCLIRunner::parse_argument(E->get(), N, has_entrypoint_argument, result.options, error);
		if (!error.is_empty()) {
			result.error = error;
			break;
		}
		E = N;
	}

	return result;
}

// Note:
// * Cursor is BETWEEN chars
//	 * `va|r` -> cursor between `a`&`r`
//   * `var`
//        ^
//      -> Character on `r` -> cursor between `a`&`r`s for tests:
// * Line & Char:
//   * LSP: both 0-based
//   * Godot: both 1-based
TEST_SUITE("[Modules][GDScript][LSP][Editor]") {
	TEST_CASE("Can convert positions to and from Godot") {
		String code = R"(extends Node

var member := 42

func f():
		var value := 42
		return value + member)";
		PackedStringArray lines = code.split("\n");

		SUBCASE("line after end") {
			LSP::Position lsp = lsp_pos(7, 0);
			GodotPosition gd(8, 1);
			test_position_roundtrip(lsp, gd, lines);
		}
		SUBCASE("first char in first line") {
			LSP::Position lsp = lsp_pos(0, 0);
			GodotPosition gd(1, 1);
			test_position_roundtrip(lsp, gd, lines);
		}

		SUBCASE("with tabs") {
			// On `v` in `value` in `var value := ...`.
			LSP::Position lsp = lsp_pos(5, 6);
			GodotPosition gd(6, 13);
			test_position_roundtrip(lsp, gd, lines);
		}

		SUBCASE("doesn't fail with column outside of character length") {
			LSP::Position lsp = lsp_pos(2, 100);
			GodotPosition::from_lsp(lsp, lines);

			GodotPosition gd(3, 100);
			gd.to_lsp(lines);
		}

		SUBCASE("doesn't fail with line outside of line length") {
			LSP::Position lsp = lsp_pos(200, 100);
			GodotPosition::from_lsp(lsp, lines);

			GodotPosition gd(300, 100);
			gd.to_lsp(lines);
		}

		SUBCASE("special case: zero column for root class") {
			GodotPosition gd(1, 0);
			LSP::Position expected = lsp_pos(0, 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
		SUBCASE("special case: zero line and column for root class") {
			GodotPosition gd(0, 0);
			LSP::Position expected = lsp_pos(0, 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
		SUBCASE("special case: negative line for root class") {
			GodotPosition gd(-1, 0);
			LSP::Position expected = lsp_pos(0, 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
		SUBCASE("special case: lines.length() + 1 for root class") {
			GodotPosition gd(lines.size() + 1, 0);
			LSP::Position expected = lsp_pos(lines.size(), 0);
			LSP::Position actual = gd.to_lsp(lines);
			CHECK_EQ(actual, expected);
		}
	}
	TEST_CASE("[workspace][resolve_symbol]") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();

		{
			String path = "res://lsp/local_variables.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			SUBCASE("Can get correct ranges for public variables") {
				Vector<InlineTestData> test_data = filter_ref_towards(all_test_data, "member");
				test_resolve_symbols(uri, test_data, all_test_data);
			}
			SUBCASE("Can get correct ranges for local variables") {
				Vector<InlineTestData> test_data = filter_ref_towards(all_test_data, "test");
				test_resolve_symbols(uri, test_data, all_test_data);
			}
			SUBCASE("Can get correct ranges for local parameters") {
				Vector<InlineTestData> test_data = filter_ref_towards(all_test_data, "arg");
				test_resolve_symbols(uri, test_data, all_test_data);
			}
		}

		SUBCASE("Can get correct ranges for indented variables") {
			String path = "res://lsp/indentation.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for scopes") {
			String path = "res://lsp/scopes.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for lambda") {
			String path = "res://lsp/lambdas.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for inner class") {
			String path = "res://lsp/class.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for inner class") {
			String path = "res://lsp/enums.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for shadowing & shadowed variables") {
			String path = "res://lsp/shadowing_initializer.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		SUBCASE("Can get correct ranges for properties and getter/setter") {
			String path = "res://lsp/properties.gd";
			assert_no_errors_in(path);
			String uri = workspace->get_file_uri(path);
			Vector<InlineTestData> all_test_data = read_tests(path);
			test_resolve_symbols(uri, all_test_data, all_test_data);
		}

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}
	TEST_CASE("[workspace][document_symbol]") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);

		SUBCASE("selectionRange of root class must be inside range") {
			LocalVector<String> paths = {
				"res://lsp/first_line_comment.gd", // Comment on first line
				"res://lsp/first_line_class_name.gd", // class_name (and thus selection range) before extends
			};

			for (const String &path : paths) {
				assert_no_errors_in(path);
				ExtendGDScriptParser *parser = GDScriptLanguageProtocol::get_singleton()->get_parse_result(path);
				REQUIRE(parser);
				LSP::DocumentSymbol cls = parser->get_symbols();

				REQUIRE(((cls.range.start.line == cls.selectionRange.start.line && cls.range.start.character <= cls.selectionRange.start.character) || (cls.range.start.line < cls.selectionRange.start.line)));
				REQUIRE(((cls.range.end.line == cls.selectionRange.end.line && cls.range.end.character >= cls.selectionRange.end.character) || (cls.range.end.line > cls.selectionRange.end.line)));
			}
		}

		SUBCASE("Documentation is correctly set") {
			String path = "res://lsp/doc_comments.gd";
			assert_no_errors_in(path);
			ExtendGDScriptParser *parser = GDScriptLanguageProtocol::get_singleton()->get_parse_result(path);
			REQUIRE(parser);
			LSP::DocumentSymbol cls = parser->get_symbols();
			REQUIRE(cls.documentation.contains("brief"));
			REQUIRE(cls.documentation.contains("description"));
			REQUIRE(cls.documentation.contains("t1"));
			REQUIRE(cls.documentation.contains("t2"));
			REQUIRE(cls.documentation.contains("t3"));
		}

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("[cli_runner][options]") {
		SUBCASE("Parses diagnostics summary and fail-on options") {
			ParsedCLIOptionsResult parsed = parse_cli_options({
					"--lsp-diagnostics",
					"--diagnostics-format",
					"summary",
					"--diagnostics-severity",
					"all",
					"--diagnostics-fail-on",
					"warning",
			});
			REQUIRE(parsed.error.is_empty());
			CHECK(parsed.options.diagnostics);
			CHECK_EQ(parsed.options.diagnostics_format, GDScriptLSPCLIRunner::DIAGNOSTICS_FORMAT_SUMMARY);
			CHECK_EQ(parsed.options.diagnostics_severity, GDScriptLSPCLIRunner::DIAGNOSTICS_SEVERITY_ALL);
			CHECK_EQ(parsed.options.diagnostics_fail_on, GDScriptLSPCLIRunner::DIAGNOSTICS_FAIL_ON_WARNING);

			String validation_error;
			CHECK_EQ(GDScriptLSPCLIRunner::validate_options(parsed.options, validation_error), OK);
			CHECK(validation_error.is_empty());
		}

		SUBCASE("Startup options keep query runs headless by default") {
			GDScriptLSPCLIRunner::Options options;
			options.query = "document-symbol";
			options.file = "res://lsp/local_variables.gd";

			bool editor = false;
			bool cmdline_tool = false;
			bool wait_for_import = false;
			bool quiet_stdout = false;
			bool recovery_mode = false;

			GDScriptLSPCLIRunner::apply_startup_options(options, editor, cmdline_tool, wait_for_import, quiet_stdout, recovery_mode);

			CHECK_FALSE(editor);
			CHECK(cmdline_tool);
			CHECK_FALSE(wait_for_import);
			CHECK(quiet_stdout);
			CHECK_FALSE(recovery_mode);
		}

		SUBCASE("Startup options keep diagnostics runs headless by default") {
			GDScriptLSPCLIRunner::Options options;
			options.diagnostics = true;

			bool editor = false;
			bool cmdline_tool = false;
			bool wait_for_import = false;
			bool quiet_stdout = false;
			bool recovery_mode = false;

			GDScriptLSPCLIRunner::apply_startup_options(options, editor, cmdline_tool, wait_for_import, quiet_stdout, recovery_mode);

			CHECK_FALSE(editor);
			CHECK(cmdline_tool);
			CHECK_FALSE(wait_for_import);
			CHECK(quiet_stdout);
			CHECK_FALSE(recovery_mode);
		}

		SUBCASE("Startup options do not change recovery mode") {
			GDScriptLSPCLIRunner::Options options;
			options.diagnostics = true;

			bool editor = false;
			bool cmdline_tool = false;
			bool wait_for_import = false;
			bool quiet_stdout = false;
			bool recovery_mode = true;

			GDScriptLSPCLIRunner::apply_startup_options(options, editor, cmdline_tool, wait_for_import, quiet_stdout, recovery_mode);

			CHECK_FALSE(editor);
			CHECK(cmdline_tool);
			CHECK_FALSE(wait_for_import);
			CHECK(quiet_stdout);
			CHECK(recovery_mode);
		}

		SUBCASE("Startup options leave unrelated launches untouched") {
			GDScriptLSPCLIRunner::Options options;

			bool editor = false;
			bool cmdline_tool = false;
			bool wait_for_import = false;
			bool quiet_stdout = false;
			bool recovery_mode = false;

			GDScriptLSPCLIRunner::apply_startup_options(options, editor, cmdline_tool, wait_for_import, quiet_stdout, recovery_mode);

			CHECK_FALSE(editor);
			CHECK_FALSE(cmdline_tool);
			CHECK_FALSE(wait_for_import);
			CHECK_FALSE(quiet_stdout);
			CHECK_FALSE(recovery_mode);
		}

		SUBCASE("Rejects diagnostics format outside diagnostics mode") {
			ParsedCLIOptionsResult parsed = parse_cli_options({
					"--lsp-query",
					"hover",
					"--file",
					"res://lsp/local_variables.gd",
					"--line",
					"2",
					"--column",
					"1",
					"--diagnostics-format",
					"summary",
			});
			REQUIRE(parsed.error.is_empty());

			String validation_error;
			CHECK_EQ(GDScriptLSPCLIRunner::validate_options(parsed.options, validation_error), ERR_INVALID_PARAMETER);
			CHECK_EQ(validation_error, String("--diagnostics-format is only valid with --lsp-diagnostics."));
		}

		SUBCASE("Rejects include declaration outside references queries") {
			ParsedCLIOptionsResult parsed = parse_cli_options({
					"--lsp-query",
					"hover",
					"--file",
					"res://lsp/local_variables.gd",
					"--line",
					"2",
					"--column",
					"1",
					"--include-declaration",
					"false",
			});
			REQUIRE(parsed.error.is_empty());

			String validation_error;
			CHECK_EQ(GDScriptLSPCLIRunner::validate_options(parsed.options, validation_error), ERR_INVALID_PARAMETER);
			CHECK_EQ(validation_error, String("--include-declaration is only valid with --lsp-query references."));
		}
	}

	TEST_CASE("[cli_runner][query_params]") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		Ref<GDScriptWorkspace> workspace = GDScriptLanguageProtocol::get_singleton()->get_workspace();

		SUBCASE("Normalizes project relative files and injects references context") {
			GDScriptLSPCLIRunner::Options options;
			options.query = "references";
			options.file = "lsp/local_variables.gd";
			options.line = 2;
			options.column = 1;
			options.include_declaration = false;

			CHECK_EQ(GDScriptLSPCLIRunner::normalize_file_path(options.file), String("res://lsp/local_variables.gd"));

			Dictionary params;
			String error;
			CHECK_EQ(GDScriptLSPCLIRunner::build_query_params(options, workspace, params, error), OK);
			CHECK(error.is_empty());

			Dictionary text_document = params["textDocument"];
			CHECK_EQ(String(text_document["uri"]), workspace->get_file_uri("res://lsp/local_variables.gd"));

			Dictionary context = params["context"];
			CHECK_EQ(bool(context["includeDeclaration"]), false);
		}

		SUBCASE("Preserves explicit context from params json") {
			GDScriptLSPCLIRunner::Options options;
			options.query = "references";
			options.params_json = "{\"textDocument\":{\"uri\":\"res://lsp/local_variables.gd\"},\"position\":{\"line\":1,\"character\":0},\"context\":{\"includeDeclaration\":true}}";
			options.include_declaration = false;

			Dictionary params;
			String error;
			CHECK_EQ(GDScriptLSPCLIRunner::build_query_params(options, workspace, params, error), OK);
			CHECK(error.is_empty());

			Dictionary context = params["context"];
			CHECK_EQ(bool(context["includeDeclaration"]), true);
		}

		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("[cli_runner][no_editor_protocol]") {
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		CHECK(GDScriptLanguageProtocol::get_singleton());
		CHECK_FALSE(EditorNode::get_singleton());

		String error;

		GDScriptLSPCLIRunner::Options document_symbol_options;
		document_symbol_options.query = "document-symbol";
		document_symbol_options.file = "res://lsp/local_variables.gd";
		Array document_symbols = GDScriptLSPCLIRunner::run_query_for_tests(document_symbol_options, error);
		CHECK(error.is_empty());
		CHECK_FALSE(document_symbols.is_empty());

		GDScriptLSPCLIRunner::Options hover_options;
		hover_options.query = "hover";
		hover_options.file = "res://lsp/local_variables.gd";
		hover_options.line = 7;
		hover_options.column = 14;
		Dictionary hover = GDScriptLSPCLIRunner::run_query_for_tests(hover_options, error);
		CHECK(error.is_empty());
		CHECK(hover.has("contents"));

		GDScriptLSPCLIRunner::Options completion_options;
		completion_options.query = "completion";
		completion_options.file = "res://lsp/local_variables.gd";
		completion_options.line = 7;
		completion_options.column = 12;
		Array completion = GDScriptLSPCLIRunner::run_query_for_tests(completion_options, error);
		CHECK(error.is_empty());
		CHECK(completion.is_empty());

		GDScriptLSPCLIRunner::Options diagnostics_options;
		diagnostics_options.diagnostics = true;
		diagnostics_options.diagnostics_format = GDScriptLSPCLIRunner::DIAGNOSTICS_FORMAT_SUMMARY;
		diagnostics_options.diagnostics_severity = GDScriptLSPCLIRunner::DIAGNOSTICS_SEVERITY_ALL;
		diagnostics_options.diagnostics_fail_on = GDScriptLSPCLIRunner::DIAGNOSTICS_FAIL_ON_NEVER;
		CHECK_EQ(GDScriptLSPCLIRunner::run_diagnostics_for_tests(diagnostics_options), GDScriptLSPCLIRunner::EXIT_OK);

		memdelete(proto);
		finish_language();
	}

	TEST_CASE("[cli_runner][diagnostics_summary]") {
		Array diagnostics;
		{
			Dictionary diagnostic;
			diagnostic["severityName"] = "warning";
			diagnostic["path"] = "res://main.gd";
			diagnostics.push_back(diagnostic);
		}
		{
			Dictionary diagnostic;
			diagnostic["severityName"] = "warning";
			diagnostic["path"] = "res://main.gd";
			diagnostics.push_back(diagnostic);
		}
		{
			Dictionary diagnostic;
			diagnostic["severityName"] = "error";
			diagnostic["path"] = "res://tests/run_all_tests.gd";
			diagnostics.push_back(diagnostic);
		}

		Dictionary summary = GDScriptLSPCLIRunner::summarize_diagnostics(diagnostics);
		CHECK_EQ(int(summary["total"]), 3);

		Dictionary by_severity = summary["bySeverity"];
		CHECK_EQ(int(by_severity["warning"]), 2);
		CHECK_EQ(int(by_severity["error"]), 1);

		Dictionary by_file = summary["byFile"];
		CHECK_EQ(int(by_file["res://main.gd"]), 2);
		CHECK_EQ(int(by_file["res://tests/run_all_tests.gd"]), 1);

		CHECK(GDScriptLSPCLIRunner::should_fail_for_severity(LSP::DiagnosticSeverity::Error, GDScriptLSPCLIRunner::DIAGNOSTICS_FAIL_ON_ERROR));
		CHECK(!GDScriptLSPCLIRunner::should_fail_for_severity(LSP::DiagnosticSeverity::Warning, GDScriptLSPCLIRunner::DIAGNOSTICS_FAIL_ON_ERROR));
		CHECK(GDScriptLSPCLIRunner::should_fail_for_severity(LSP::DiagnosticSeverity::Warning, GDScriptLSPCLIRunner::DIAGNOSTICS_FAIL_ON_WARNING));
		CHECK(GDScriptLSPCLIRunner::should_fail_for_severity(LSP::DiagnosticSeverity::Hint, GDScriptLSPCLIRunner::DIAGNOSTICS_FAIL_ON_ANY));
		CHECK(!GDScriptLSPCLIRunner::should_fail_for_severity(LSP::DiagnosticSeverity::Error, GDScriptLSPCLIRunner::DIAGNOSTICS_FAIL_ON_NEVER));
	}

	TEST_CASE("[cli_runner][tracing]") {
		EditorFileSystem *efs = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *proto = initialize(root);
		REQUIRE(proto);
		const String trace_root = TestUtils::get_temp_path("gdscript_lsp_cli_trace_root");
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(trace_root);

		CustomFeatureTracer::StartupOptions trace_options;
		trace_options.base_dir_override = trace_root;
		trace_options.binary_path = "godot-dev";
		trace_options.cwd = "E:/GitHub/godot";
		trace_options.project_path = "E:/GitHub/godot";
		trace_options.process_mode = "cmdline_tool";
		trace_options.pid = 8642;

		String trace_error;
		REQUIRE_EQ(CustomFeatureTracer::initialize_singleton(trace_options, trace_error), OK);
		REQUIRE(trace_error.is_empty());
		const String session_dir = CustomFeatureTracer::get_singleton()->get_session_dir_for_tests();

		SUBCASE("query runs emit params and result trace events") {
			GDScriptLSPCLIRunner::Options options;
			options.query = "document-symbol";
			options.file = "res://lsp/local_variables.gd";

			String query_error;
			{
				CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_GDSCRIPT_LSP_CLI, "lsp-query-trace");
				const Variant query_result = GDScriptLSPCLIRunner::run_query_for_tests(options, query_error);
				CHECK(query_error.is_empty());
				CHECK(query_result.get_type() != Variant::NIL);
			}
			CustomFeatureTracer::shutdown_singleton();

			const Vector<Dictionary> events = TestCustomFeatureTraceHelpers::read_events(session_dir);
			const Dictionary params_event = TestCustomFeatureTraceHelpers::find_event(events, CustomFeatureTracer::FEATURE_GDSCRIPT_LSP_CLI, "query_params");
			const Dictionary result_event = TestCustomFeatureTraceHelpers::find_event(events, CustomFeatureTracer::FEATURE_GDSCRIPT_LSP_CLI, "query_result");
			REQUIRE_FALSE(params_event.is_empty());
			REQUIRE_FALSE(result_event.is_empty());
			TestCustomFeatureTraceHelpers::check_common_event_fields(params_event);
			TestCustomFeatureTraceHelpers::check_common_event_fields(result_event);
		}

		SUBCASE("diagnostics runs emit diagnostics summary trace events") {
			GDScriptLSPCLIRunner::Options options;
			options.diagnostics = true;
			options.diagnostics_format = GDScriptLSPCLIRunner::DIAGNOSTICS_FORMAT_SUMMARY;
			options.diagnostics_severity = GDScriptLSPCLIRunner::DIAGNOSTICS_SEVERITY_ALL;
			options.diagnostics_fail_on = GDScriptLSPCLIRunner::DIAGNOSTICS_FAIL_ON_NEVER;

			{
				CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_GDSCRIPT_LSP_CLI, "lsp-diagnostics-trace");
				CHECK_EQ(GDScriptLSPCLIRunner::run_diagnostics_for_tests(options), GDScriptLSPCLIRunner::EXIT_OK);
			}
			CustomFeatureTracer::shutdown_singleton();

			const Vector<Dictionary> events = TestCustomFeatureTraceHelpers::read_events(session_dir);
			const Dictionary diagnostics_event = TestCustomFeatureTraceHelpers::find_event(events, CustomFeatureTracer::FEATURE_GDSCRIPT_LSP_CLI, "diagnostics_summary");
			REQUIRE_FALSE(diagnostics_event.is_empty());
			TestCustomFeatureTraceHelpers::check_common_event_fields(diagnostics_event);
			const Dictionary payloads = diagnostics_event["payloads"];
			REQUIRE(payloads.has("diagnostics"));
			const Variant diagnostics_payload = JSON::parse_string(TestCustomFeatureTraceHelpers::read_payload_text(session_dir, payloads["diagnostics"]));
			REQUIRE(diagnostics_payload.get_type() == Variant::ARRAY);
		}

		if (CustomFeatureTracer::has_singleton()) {
			CustomFeatureTracer::shutdown_singleton();
		}
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(trace_root);
		memdelete(proto);
		memdelete(efs);
		finish_language();
	}

	TEST_CASE("BBCode to markdown conversion") {
		// This tests the conversion from BBCode docstrings to the markdown markup sent to
		// the LSP client on documentation requests

		// Basic formatting
		CHECK_EQ(LSP::marked_documentation("[b]bold[/b]"), "**bold**");
		CHECK_EQ(LSP::marked_documentation("[i]italic[/i]"), "*italic*");
		CHECK_EQ(LSP::marked_documentation("[u]underline[/u]"), "__underline__");
		CHECK_EQ(LSP::marked_documentation("[s]strikethrough[/s]"), "~~strikethrough~~");
		CHECK_EQ(LSP::marked_documentation("[code]code[/code]"), "`code`");
		CHECK_EQ(LSP::marked_documentation("[kbd]Ctrl + S[/kbd]"), "`Ctrl + S`");

		// Line breaks. We insert paragraphs for [br] because the BBCode to
		// markdown conversion function simply makes the conversion line-wise and
		// we don't distinguish markdown inline elements and blocks.
		CHECK_EQ(LSP::marked_documentation("Line1[br]Line2"), "Line1\n\nLine2");

		// These tags (center, color, font) aren't supported in markdown and should be stripped.
		CHECK_EQ(LSP::marked_documentation("[center]Centered text[/center]"), "Centered text");
		CHECK_EQ(LSP::marked_documentation("[color=red]red text[/color]"), "red text");
		CHECK_EQ(LSP::marked_documentation("[font=Arial]Arial text[/font]"), "Arial text");

		// The following tests are for all the link patterns specific to Godot's built-in docs that we render as inline code.
		CHECK_EQ(LSP::marked_documentation("Class link: [Node2D], [Sprite2D]"), "Class link: `Node2D`, `Sprite2D`");
		CHECK_EQ(LSP::marked_documentation("Single class [RigidBody2D]"), "Single class `RigidBody2D`");
		CHECK_EQ(LSP::marked_documentation("[method Node2D.set_position]"), "`Node2D.set_position`");
		CHECK_EQ(LSP::marked_documentation("[member Node2D.position]"), "`Node2D.position`");
		CHECK_EQ(LSP::marked_documentation("[signal Node.ready]"), "`Node.ready`");
		CHECK_EQ(LSP::marked_documentation("[constant Color.RED]"), "`Color.RED`");
		CHECK_EQ(LSP::marked_documentation("[enum Node.ProcessMode]"), "`Node.ProcessMode`");
		CHECK_EQ(LSP::marked_documentation("[annotation @GDScript.@export]"), "`@GDScript.@export`");
		CHECK_EQ(LSP::marked_documentation("[constructor Vector2.Vector2]"), "`Vector2.Vector2`");
		CHECK_EQ(LSP::marked_documentation("[operator Vector2.operator +]"), "`Vector2.operator +`");
		CHECK_EQ(LSP::marked_documentation("[theme_item Button.font]"), "`Button.font`");
		CHECK_EQ(LSP::marked_documentation("[param delta]"), "`delta`");

		// Markdown links
		CHECK_EQ(LSP::marked_documentation("[url=https://godotengine.org]link to Godot Engine[/url]"),
				"[link to Godot Engine](https://godotengine.org)");
		CHECK_EQ(LSP::marked_documentation("[url]https://godotengine.org/[/url]"),
				"[https://godotengine.org/](https://godotengine.org/)");

		// Code listings
		CHECK_EQ(LSP::marked_documentation("[codeblock]\nfunc test():\n    print(\"Hello, Godot!\")\n[/codeblock]"),
				"```gdscript\nfunc test():\n    print(\"Hello, Godot!\")\n```");
		CHECK_EQ(LSP::marked_documentation("[codeblock lang=csharp]\npublic void Test()\n{\n    GD.Print(\"Hello, Godot!\");\n}\n[/codeblock]"),
				"```csharp\npublic void Test()\n{\n    GD.Print(\"Hello, Godot!\");\n}\n```");
		// Code listings with multiple languages (the codeblocks tag is used in the built-in reference)
		// When [codeblocks] is used, we only convert the [gdscript] tag to a code block like the built-in editor.
		// NOTE: There is always a GDScript code listing in the built-in class reference.
		CHECK_EQ(LSP::marked_documentation("[codeblocks]\n[gdscript]\nprint(hash(\"a\")) # Prints 177670\n[/gdscript]\n[csharp]\nGD.Print(GD.Hash(\"a\")); // Prints 177670\n[/csharp]\n[/codeblocks]"),
				"```gdscript\nprint(hash(\"a\")) # Prints 177670\n```\n");

		// lb and rb are used to insert literal square brackets in markdown.
		CHECK_EQ(LSP::marked_documentation("[lb]literal brackets[rb]"), "\\[literal brackets\\]");
		CHECK_EQ(LSP::marked_documentation("[lb]literal[rb] with [ClassName]"), "\\[literal\\] with `ClassName`");

		// We have to be careful that different patterns don't conflict with each
		// other, especially with urls that use brackets in markdown.
		CHECK_EQ(LSP::marked_documentation("Class [Sprite2D] with [url=https://godotengine.org]link[/url]"),
				"Class `Sprite2D` with [link](https://godotengine.org)");
	}
}

} // namespace GDScriptTests

#endif // MODULE_JSONRPC_ENABLED

#endif // TOOLS_ENABLED
