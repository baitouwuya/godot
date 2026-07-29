/**************************************************************************/
/*  test_mcp_gdscript_semantic_service.cpp                               */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "editor/mcp/providers/mcp_gdscript_request_parser.h"
#include "editor/mcp/providers/mcp_gdscript_semantic_service.h"

#include "modules/gdscript/language_server/gdscript_workspace.h"

#endif

TEST_FORCE_LINK(test_mcp_gdscript_semantic_service);

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

namespace TestMCPGDScriptSemanticService {

static Error _write_text(const String &p_path, const String &p_text) {
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return open_error == OK ? ERR_CANT_OPEN : open_error;
	}
	return file->store_string(p_text) ? OK : ERR_FILE_CANT_WRITE;
}

TEST_CASE("[MCP][Provider] GDScript semantic service composes authoritative analysis per session") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_gdscript_semantic_service", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());
	REQUIRE(temporary_directory->make_dir("scripts") == OK);
	const String project_root = temporary_directory->get_current_dir();
	const String absolute_path = project_root.path_join("scripts/semantic.gd");
	const String resource_path = "res://scripts/semantic.gd";
	const String source = "var value: int = 1\n\nfunc call() -> int:\n\treturn value\n";
	REQUIRE(_write_text(absolute_path, source) == OK);

	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisService> analysis_service;
	analysis_service.instantiate();
	analysis_service->configure(project_root, workspace);
	Ref<MCPGDScriptSessionManager> session_manager = memnew(MCPGDScriptSessionManager(analysis_service));
	MCPGDScriptSemanticService semantic_service(session_manager);
	CHECK(semantic_service.validate_ready().is_ok());

	const MCPGDScriptSemanticService::OperationResult first_diagnostics = semantic_service.diagnostics("semantic-first", resource_path);
	REQUIRE(first_diagnostics.is_ok());
	CHECK(first_diagnostics.value.get("path", String()) == resource_path);
	CHECK(first_diagnostics.value.get("sha256", String()) == source.sha256_text());
	CHECK(first_diagnostics.value.has("diagnostics"));

	const MCPGDScriptSemanticService::OperationResult second_diagnostics = semantic_service.diagnostics("semantic-second", absolute_path);
	REQUIRE(second_diagnostics.is_ok());
	Ref<GDScriptAnalysisSession> first_session = session_manager->get_session("semantic-first");
	Ref<GDScriptAnalysisSession> second_session = session_manager->get_session("semantic-second");
	REQUIRE(first_session.is_valid());
	REQUIRE(second_session.is_valid());
	CHECK(first_session != second_session);
	REQUIRE(first_session->get_document(resource_path));
	REQUIRE(second_session->get_document(resource_path));
	CHECK(first_session->get_document(resource_path)->text == source);
	CHECK(second_session->get_document(resource_path)->text == source);

	MCPGDScriptRequestParser parser(workspace, project_root);
	Dictionary position_arguments;
	position_arguments["path"] = resource_path;
	position_arguments["line"] = 3;
	position_arguments["character"] = 9;
	String parsed_path;
	String parse_error;
	LSP::TextDocumentPositionParams position;
	REQUIRE(parser.parse_position(position_arguments, parsed_path, position, &parse_error) == OK);
	const MCPGDScriptSemanticService::OperationResult hover = semantic_service.hover("semantic-first", parsed_path, position);
	REQUIRE(hover.is_ok());
	CHECK(hover.value.has("found"));

	const MCPGDScriptSemanticService::OperationResult missing = semantic_service.diagnostics("semantic-first", "res://scripts/missing.gd");
	CHECK(missing.error == ERR_FILE_NOT_FOUND);
	CHECK(missing.error_code == "SCRIPT_NOT_FOUND");
	CHECK(missing.details.get("path", String()) == "res://scripts/missing.gd");
}

TEST_CASE("[MCP][Provider] GDScript semantic service reports missing dependencies") {
	MCPGDScriptSemanticService semantic_service;
	const MCPGDScriptSemanticService::OperationResult readiness = semantic_service.validate_ready();
	CHECK(readiness.error == ERR_UNCONFIGURED);
	CHECK(readiness.error_code == "ANALYSIS_UNAVAILABLE");
}

} // namespace TestMCPGDScriptSemanticService

#endif // MODULE_GDSCRIPT_ENABLED && !GDSCRIPT_NO_LSP
