/**************************************************************************/
/*  test_mcp_script_analysis_sync.cpp                                     */
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

#include "editor/mcp/providers/mcp_script_analysis_sync.h"

#include "modules/gdscript/language_server/gdscript_workspace.h"

#endif

TEST_FORCE_LINK(test_mcp_script_analysis_sync);

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

namespace TestMCPScriptAnalysisSync {

TEST_CASE("[MCP][Provider] Script snapshots and GDScript diagnostics share one session manager") {
	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisService> service;
	service.instantiate();
	service->configure("res://", workspace);
	Ref<MCPGDScriptSessionManager> manager = memnew(MCPGDScriptSessionManager(service));
	const String session_id = "script-analysis-contract";
	const String path = "res://mcp_script_analysis_contract.gd";
	const String invalid_source = "var value: int =\n";
	Dictionary snapshot;
	snapshot["path"] = path;
	snapshot["text"] = invalid_source;
	snapshot["revision"] = int64_t(21);
	snapshot["savedRevision"] = int64_t(20);
	snapshot["sha256"] = invalid_source.sha256_text();
	snapshot["unsaved"] = true;

	Array edit_diagnostics;
	String sync_error;
	REQUIRE(MCPScriptAnalysisSync::sync_snapshot(manager, session_id, snapshot, edit_diagnostics, &sync_error) == OK);
	CHECK(sync_error.is_empty());
	CHECK_FALSE(edit_diagnostics.is_empty());
	Ref<GDScriptAnalysisSession> session = manager->get_session(session_id);
	REQUIRE(session.is_valid());
	const uint64_t analysis_revision = session->get_revision();
	ExtendGDScriptParser *parser = session->get_parse_result(path);
	REQUIRE(parser);

	const GDScriptAnalysisSession::DocumentState *document = session->get_document(path);
	REQUIRE(document);
	CHECK_EQ(document->client_version, int64_t(snapshot["revision"]));
	CHECK_EQ(document->sha256, String(snapshot["sha256"]));
	CHECK(session->get_diagnostics(path) == edit_diagnostics);

	Array repeated_diagnostics;
	REQUIRE(MCPScriptAnalysisSync::sync_snapshot(manager, session_id, snapshot, repeated_diagnostics, &sync_error) == OK);
	CHECK(session->get_revision() == analysis_revision);
	CHECK(session->get_parse_result(path) == parser);
	CHECK(repeated_diagnostics == edit_diagnostics);

	const String saved_source = "var saved_value: int =\n";
	Dictionary saved_snapshot;
	saved_snapshot["path"] = path;
	saved_snapshot["text"] = saved_source;
	saved_snapshot["revision"] = int64_t(22);
	saved_snapshot["savedRevision"] = int64_t(22);
	saved_snapshot["sha256"] = saved_source.sha256_text();
	saved_snapshot["unsaved"] = false;
	REQUIRE(MCPScriptAnalysisSync::sync_saved_snapshot(manager, session_id, saved_snapshot, &sync_error) == OK);
	CHECK(bool(saved_snapshot.get("saved", false)));
	CHECK_FALSE(Array(saved_snapshot.get("diagnostics", Array())).is_empty());
	const GDScriptAnalysisSession::DocumentState *saved_document = session->get_document(path);
	REQUIRE(saved_document);
	CHECK_EQ(saved_document->client_version, int64_t(saved_snapshot["revision"]));
	CHECK_EQ(saved_document->sha256, String(saved_snapshot["sha256"]));

	CHECK(session->get_diagnostics(path) == Array(saved_snapshot["diagnostics"]));

	Dictionary unsaved_snapshot = saved_snapshot.duplicate(true);
	unsaved_snapshot["unsaved"] = true;
	CHECK(MCPScriptAnalysisSync::sync_saved_snapshot(manager, session_id, unsaved_snapshot, &sync_error) == ERR_INVALID_DATA);

	const Dictionary failure = MCPScriptAnalysisSync::make_failure(snapshot, true, true, false, "sync failed");
	const Dictionary failure_error = Dictionary(failure.get("structuredContent", Dictionary())).get("error", Dictionary());
	CHECK(String(failure_error.get("code", String())) == "ANALYSIS_SYNC_FAILED");
	const Dictionary details = failure_error.get("details", Dictionary());
	CHECK(bool(details.get("applied", false)));
	CHECK(bool(details.get("changed", false)));
	const Dictionary current = details.get("current", Dictionary());
	CHECK_EQ(int64_t(current.get("revision", -1)), int64_t(snapshot["revision"]));
	CHECK_EQ(String(current.get("sha256", String())), String(snapshot["sha256"]));

	manager->clear();
}

} // namespace TestMCPScriptAnalysisSync

#endif // MODULE_GDSCRIPT_ENABLED && !GDSCRIPT_NO_LSP
