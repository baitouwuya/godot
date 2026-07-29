/**************************************************************************/
/*  test_mcp_gdscript_session_manager.cpp                                 */
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

#include "core/io/file_access.h"
#include "core/os/os.h"
#include "editor/mcp/providers/mcp_gdscript_session_manager.h"

#include "modules/gdscript/language_server/gdscript_workspace.h"

#endif

TEST_FORCE_LINK(test_mcp_gdscript_session_manager);

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

namespace TestMCPGDScriptSessionManager {

TEST_CASE("[MCP][Provider] GDScript session manager synchronizes authoritative snapshots") {
	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisService> service;
	service.instantiate();
	service->configure("res://", workspace);
	Ref<MCPGDScriptSessionManager> manager = memnew(MCPGDScriptSessionManager(service));

	const String session_id = "manager-sync-client";
	const String path = "res://mcp_manager_sync.gd";
	const String valid_source = "var value: int = 1\n";
	const String invalid_source = "var value: int =\n";
	Array diagnostics;
	String error;
	REQUIRE(manager->sync_document(session_id, path, valid_source, 10, diagnostics, &error) == OK);
	CHECK(error.is_empty());
	CHECK(diagnostics.is_empty());

	Ref<GDScriptAnalysisSession> session = manager->get_session(session_id);
	REQUIRE(session.is_valid());
	const GDScriptAnalysisSession::DocumentState *document = session->get_document(path);
	REQUIRE(document);
	CHECK(document->source_state == GDScriptAnalysisSession::SOURCE_STATE_OPEN_DOCUMENT);
	CHECK(document->client_version == 10);
	CHECK(document->sha256 == valid_source.sha256_text());
	const uint64_t idempotent_revision = session->get_revision();
	ExtendGDScriptParser *idempotent_parser = session->get_parse_result(path);
	REQUIRE(idempotent_parser);

	REQUIRE(manager->sync_document(session_id, path, valid_source, 10, diagnostics, &error) == OK);
	CHECK(session->get_revision() == idempotent_revision);
	CHECK(session->get_parse_result(path) == idempotent_parser);
	CHECK(diagnostics.is_empty());

	REQUIRE(manager->sync_document(session_id, path, invalid_source, 11, diagnostics, &error) == OK);
	CHECK_GT(session->get_revision(), idempotent_revision);
	document = session->get_document(path);
	REQUIRE(document);
	CHECK(document->client_version == 11);
	CHECK(document->sha256 == invalid_source.sha256_text());
	CHECK_FALSE(diagnostics.is_empty());

	const int session_count = manager->get_session_count();
	CHECK(manager->sync_document("invalid-version", path, valid_source, -1, diagnostics, &error) == ERR_PARAMETER_RANGE_ERROR);
	CHECK(manager->get_session_count() == session_count);

	const String disk_path = OS::get_singleton()->get_executable_path().get_base_dir().get_base_dir().path_join("modules/gdscript/tests/scripts/utils.notest.gd");
	Error read_error = OK;
	const String disk_source = FileAccess::get_file_as_string(disk_path, &read_error);
	REQUIRE(read_error == OK);
	Ref<GDScriptAnalysisSession> disk_session = manager->get_or_create_session("manager-disk-client");
	REQUIRE(disk_session.is_valid());
	REQUIRE(disk_session->get_parse_result(disk_path));
	document = disk_session->get_document(disk_path);
	REQUIRE(document);
	CHECK(document->source_state == GDScriptAnalysisSession::SOURCE_STATE_DISK);
	const uint64_t disk_revision = disk_session->get_revision();
	REQUIRE(manager->sync_document("manager-disk-client", disk_path, disk_source, 12, diagnostics, &error) == OK);
	document = disk_session->get_document(disk_path);
	REQUIRE(document);
	CHECK(document->source_state == GDScriptAnalysisSession::SOURCE_STATE_OPEN_DOCUMENT);
	CHECK(document->client_version == 12);
	CHECK_GT(disk_session->get_revision(), disk_revision);

	const uint64_t session_analysis_id = session->get_session_id();
	const uint64_t disk_analysis_id = disk_session->get_session_id();
	manager->clear();
	CHECK(service->get_session(session_analysis_id).is_null());
	CHECK(service->get_session(disk_analysis_id).is_null());
	CHECK(manager->get_session_count() == 0);
}

TEST_CASE("[MCP][Provider] GDScript session manager releases closed editor documents per session") {
	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisService> service;
	service.instantiate();
	service->configure("res://", workspace);
	Ref<MCPGDScriptSessionManager> manager = memnew(MCPGDScriptSessionManager(service));

	const String first_id = "editor-documents-first";
	const String second_id = "editor-documents-second";
	const String kept_path = "res://scene.tscn::GDScript_kept";
	const String closed_path = "res://scene.tscn::GDScript_closed";
	Array diagnostics;
	String error;
	REQUIRE(manager->sync_document(first_id, kept_path, "var kept := true\n", 1, diagnostics, &error) == OK);
	REQUIRE(manager->sync_document(first_id, closed_path, "var stale := true\n", 2, diagnostics, &error) == OK);
	REQUIRE(manager->sync_document(second_id, closed_path, "var isolated := true\n", 3, diagnostics, &error) == OK);

	HashSet<String> initially_open_paths;
	initially_open_paths.insert(kept_path);
	initially_open_paths.insert(closed_path);
	manager->track_open_editor_document(first_id, kept_path);
	manager->track_open_editor_document(first_id, closed_path);
	REQUIRE(manager->reconcile_open_editor_documents(first_id, initially_open_paths, &error) == OK);
	HashSet<String> open_paths;
	open_paths.insert(kept_path);
	REQUIRE(manager->reconcile_open_editor_documents(first_id, open_paths, &error) == OK);
	Ref<GDScriptAnalysisSession> first_session = manager->get_session(first_id);
	Ref<GDScriptAnalysisSession> second_session = manager->get_session(second_id);
	REQUIRE(first_session.is_valid());
	REQUIRE(second_session.is_valid());
	CHECK(first_session->has_document(kept_path));
	CHECK_FALSE(first_session->has_document(closed_path));
	CHECK(first_session->get_parse_result(closed_path) == nullptr);
	CHECK(second_session->has_document(closed_path));
	CHECK(second_session->get_parse_result(closed_path) != nullptr);
}

TEST_CASE("[MCP][Provider] GDScript session manager owns MCP lifecycle cleanup") {
	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisService> service;
	service.instantiate();
	service->configure("res://", workspace);
	Ref<MCPGDScriptSessionManager> manager = memnew(MCPGDScriptSessionManager(service));

	Ref<GDScriptAnalysisSession> first_session = manager->get_or_create_session("lifecycle-first");
	Ref<GDScriptAnalysisSession> second_session = manager->get_or_create_session("lifecycle-second");
	REQUIRE(first_session.is_valid());
	REQUIRE(second_session.is_valid());
	const uint64_t first_analysis_id = first_session->get_session_id();
	const uint64_t second_analysis_id = second_session->get_session_id();

	manager->on_session_removed("lifecycle-first");
	CHECK(manager->get_session_count() == 1);
	CHECK(service->get_session(first_analysis_id).is_null());
	CHECK(service->get_session(second_analysis_id) == second_session);

	manager->shutdown();
	CHECK(manager->get_session_count() == 0);
	CHECK(service->get_session(second_analysis_id).is_null());
	manager->shutdown();
	CHECK(manager->get_session_count() == 0);
}

} // namespace TestMCPGDScriptSessionManager

#endif // MODULE_GDSCRIPT_ENABLED && !GDSCRIPT_NO_LSP
