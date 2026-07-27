/**************************************************************************/
/*  test_mcp_gdscript_provider.cpp                                        */
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
/* INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,        */
/* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL */
/* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR    */
/* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,  */
/* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR  */
/* OTHER DEALINGS IN THE SOFTWARE.                                       */
/**************************************************************************/

#include "tests/test_macros.h"

#include "modules/modules_enabled.gen.h"

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/mcp/mcp_tool_registry.h"
#include "editor/mcp/providers/mcp_gdscript_provider.h"
#include "editor/mcp/providers/mcp_gdscript_request_parser.h"
#include "editor/mcp/providers/mcp_gdscript_tool_utils.h"

#endif

TEST_FORCE_LINK(test_mcp_gdscript_provider);

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)

namespace TestMCPGDScriptProvider {

static String _error_code(const Dictionary &p_result) {
	const Dictionary structured = p_result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return error.get("code", String());
}

static Dictionary _error_details(const Dictionary &p_result) {
	const Dictionary structured = p_result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return error.get("details", Dictionary());
}

static Error _write_text(const String &p_path, const String &p_text) {
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return open_error == OK ? ERR_CANT_OPEN : open_error;
	}
	return file->store_string(p_text) ? OK : ERR_FILE_CANT_WRITE;
}

TEST_CASE("[MCP][Provider] GDScript tool utilities preserve position and completion mappings") {
	Dictionary arguments;
	arguments["path"] = "res://tool_utils.gd";
	arguments["line"] = 7.0;
	String error;

	String path;
	CHECK(MCPGDScriptToolUtils::get_string_argument(arguments, "path", path, error));
	CHECK(path == "res://tool_utils.gd");
	int line = 0;
	CHECK(MCPGDScriptToolUtils::get_position_argument(arguments, "line", line, error));
	CHECK(line == 7);
	arguments["line"] = 7.5;
	CHECK_FALSE(MCPGDScriptToolUtils::get_position_argument(arguments, "line", line, error));
	CHECK(error == "line must be an integer.");
	arguments["line"] = double(INT32_MAX) + 1.0;
	CHECK_FALSE(MCPGDScriptToolUtils::get_position_argument(arguments, "line", line, error));
	CHECK(error == "line must be a non-negative 32-bit integer.");

	arguments["line"] = -1;
	CHECK_FALSE(MCPGDScriptToolUtils::get_position_argument(arguments, "line", line, error));
	CHECK(error == "line must be a non-negative 32-bit integer.");

	int completion_limit = 0;
	CHECK(MCPGDScriptToolUtils::get_completion_limit(Dictionary(), completion_limit, error));
	CHECK(completion_limit == 100);
	arguments["limit"] = 500;
	CHECK(MCPGDScriptToolUtils::get_completion_limit(arguments, completion_limit, error));
	CHECK(completion_limit == 500);
	arguments["limit"] = 501;
	CHECK_FALSE(MCPGDScriptToolUtils::get_completion_limit(arguments, completion_limit, error));
	CHECK(error == "limit is outside its allowed range.");

	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_ENUM) == LSP::CompletionItemKind::Enum);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_CLASS) == LSP::CompletionItemKind::Class);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_MEMBER) == LSP::CompletionItemKind::Property);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_FUNCTION) == LSP::CompletionItemKind::Method);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_SIGNAL) == LSP::CompletionItemKind::Event);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_CONSTANT) == LSP::CompletionItemKind::Constant);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_VARIABLE) == LSP::CompletionItemKind::Variable);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_FILE_PATH) == LSP::CompletionItemKind::File);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_NODE_PATH) == LSP::CompletionItemKind::Snippet);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_KEYWORD) == LSP::CompletionItemKind::Keyword);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT) == LSP::CompletionItemKind::Text);
	CHECK(MCPGDScriptToolUtils::completion_kind(ScriptLanguage::CODE_COMPLETION_KIND_MAX) == LSP::CompletionItemKind::Text);
}

TEST_CASE("[MCP][Provider] GDScript request parser adapts document selectors to LSP positions") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_gdscript_request_parser", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());

	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	const String project_root = temporary_directory->get_current_dir();
	MCPGDScriptRequestParser parser(workspace, project_root);

	Dictionary arguments;
	arguments["path"] = "res://request_parser.gd";
	arguments["position"] = Dictionary{ { "line", 7 }, { "character", 11 } };
	String path;
	String error;
	LSP::TextDocumentPositionParams position;
	CHECK(parser.parse_position(arguments, path, position, &error) == OK);
	CHECK(path == "res://request_parser.gd");
	CHECK(position.textDocument.uri == workspace->get_file_uri(path));
	CHECK(position.position.line == 7);
	CHECK(position.position.character == 11);

	arguments.erase("position");
	arguments["line"] = 3;
	arguments["character"] = 5;
	arguments["includeDeclaration"] = true;
	LSP::ReferenceParams references;
	CHECK(parser.parse_reference_position(arguments, path, references, &error) == OK);
	CHECK(references.position.line == 3);
	CHECK(references.position.character == 5);
	CHECK(references.context.includeDeclaration);

	arguments["uri"] = "file:///request_parser.gd";
	CHECK(parser.parse_document_path(arguments, path, &error) == ERR_INVALID_PARAMETER);
	CHECK(error == "Exactly one of path or uri is required.");
	arguments.erase("uri");
	arguments.erase("line");
	CHECK(parser.parse_position(arguments, path, position, &error) == ERR_INVALID_PARAMETER);
	CHECK(error == "Either position or both line and character are required.");
}

TEST_CASE("[MCP][Provider] GDScript semantic tools use injected analysis sessions") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_gdscript_provider", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());

	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisService> service;
	service.instantiate();
	service->configure(temporary_directory->get_current_dir(), workspace);
	Ref<GDScriptAnalysisSession> session = service->create_session();

	MCPToolRegistry registry;
	MCPGDScriptProvider *provider = memnew(MCPGDScriptProvider(service, session));
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);
	CHECK(provider->get_analysis_service() == service);
	CHECK(provider->get_analysis_session() == session);

	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 10);
	CHECK(names[0] == "godot.gdscript.diagnostics");
	CHECK(names[1] == "godot.gdscript.symbols");
	CHECK(names[2] == "godot.gdscript.completion");
	CHECK(names[3] == "godot.gdscript.hover");
	CHECK(names[4] == "godot.gdscript.definition");
	CHECK(names[5] == "godot.gdscript.declaration");
	CHECK(names[6] == "godot.gdscript.references");
	CHECK(names[7] == "godot.gdscript.signature_help");
	CHECK(names[8] == "godot.gdscript.rename");
	CHECK(names[9] == "godot.gdscript.apply_workspace_edit");

	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 10);
	const Dictionary diagnostics_schema = Dictionary(definitions[0]).get("inputSchema", Dictionary());
	CHECK(String(diagnostics_schema.get("description", String())).contains("Exactly one"));
	CHECK(Array(diagnostics_schema.get("oneOf", Array())).size() == 2);
	const Dictionary diagnostics_properties = diagnostics_schema.get("properties", Dictionary());
	CHECK(int(Dictionary(diagnostics_properties.get("path", Dictionary())).get("minLength", 0)) == 1);
	const Array document_options = diagnostics_schema.get("oneOf", Array());
	CHECK(PackedStringArray(Dictionary(document_options[0]).get("required", PackedStringArray())) == PackedStringArray{ "path" });
	CHECK(PackedStringArray(Dictionary(document_options[1]).get("required", PackedStringArray())) == PackedStringArray{ "uri" });
	const Dictionary completion_schema = Dictionary(definitions[2]).get("inputSchema", Dictionary());
	const Dictionary completion_properties = completion_schema.get("properties", Dictionary());
	CHECK(completion_properties.has("path"));
	CHECK(completion_properties.has("position"));
	CHECK(completion_properties.has("line"));
	CHECK(completion_properties.has("character"));
	CHECK(completion_properties.has("limit"));
	const Array completion_constraints = completion_schema.get("allOf", Array());
	REQUIRE(completion_constraints.size() == 2);
	CHECK(Array(Dictionary(completion_constraints[0]).get("oneOf", Array())).size() == 2);
	CHECK(Array(Dictionary(completion_constraints[1]).get("oneOf", Array())).size() == 2);
	const Dictionary nested_position_schema = completion_properties.get("position", Dictionary());
	const PackedStringArray nested_position_required = nested_position_schema.get("required", PackedStringArray());
	CHECK(nested_position_required.has("line"));
	CHECK(nested_position_required.has("character"));
	CHECK(int(Dictionary(completion_properties.get("line", Dictionary())).get("minimum", -1)) == 0);
	CHECK(int(Dictionary(completion_properties.get("character", Dictionary())).get("minimum", -1)) == 0);
	const Dictionary diagnostics_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(diagnostics_output.get("required", PackedStringArray())).has("diagnostics"));
	const Dictionary completion_output = Dictionary(definitions[2]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(completion_output.get("required", PackedStringArray())).has("items"));
	const Dictionary hover_output = Dictionary(definitions[3]).get("outputSchema", Dictionary());
	CHECK(Dictionary(hover_output.get("properties", Dictionary())).has("hover"));
	const Dictionary definition_output = Dictionary(definitions[4]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(definition_output.get("required", PackedStringArray())).has("operation"));
	const Dictionary references_output = Dictionary(definitions[6]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(references_output.get("required", PackedStringArray())).has("locations"));
	const Dictionary signature_output = Dictionary(definitions[7]).get("outputSchema", Dictionary());
	CHECK(Dictionary(signature_output.get("properties", Dictionary())).has("signatureHelp"));
	const Dictionary rename_schema = Dictionary(definitions[8]).get("inputSchema", Dictionary());
	CHECK(Dictionary(rename_schema.get("properties", Dictionary())).has("newName"));
	CHECK(int(Dictionary(Dictionary(rename_schema.get("properties", Dictionary())).get("newName", Dictionary())).get("minLength", 0)) == 1);
	const PackedStringArray rename_required = rename_schema.get("required", PackedStringArray());
	CHECK(rename_required.has("newName"));
	const Dictionary rename_output = Dictionary(definitions[8]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(rename_output.get("required", PackedStringArray())).has("edit"));
	const Dictionary workspace_edit_schema = Dictionary(definitions[9]).get("inputSchema", Dictionary());
	const Dictionary workspace_edit_properties = workspace_edit_schema.get("properties", Dictionary());
	CHECK(workspace_edit_properties.has("edit"));
	CHECK(workspace_edit_properties.has("documents"));
	const Dictionary workspace_edit_annotations = Dictionary(definitions[9]).get("annotations", Dictionary());
	CHECK(bool(workspace_edit_annotations.get("destructiveHint", false)));
	const Dictionary workspace_edit_output = Dictionary(definitions[9]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(workspace_edit_output.get("required", PackedStringArray())).has("documents"));

	MCPToolCallContext context;
	Dictionary missing_script;
	missing_script["path"] = "res://mcp_semantic_missing.gd";
	Dictionary unexpected_arguments = missing_script.duplicate(true);
	unexpected_arguments["unexpected"] = true;
	const Dictionary unexpected_result = registry.call_tool("godot.gdscript.diagnostics", unexpected_arguments, context).result;
	CHECK(_error_code(unexpected_result) == "INVALID_ARGUMENTS");
	CHECK(_error_details(unexpected_result).get("keyword", String()) == "additionalProperties");
	const MCPToolRegistry::CallResult missing_call = registry.call_tool("godot.gdscript.diagnostics", missing_script, context);
	REQUIRE(missing_call.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(missing_call.result.get("isError", false)));
	CHECK(_error_code(missing_call.result) == "SCRIPT_NOT_FOUND");

	Dictionary missing_position = missing_script.duplicate();
	missing_position["line"] = 0;
	missing_position["character"] = 0;
	const MCPToolRegistry::CallResult missing_hover = registry.call_tool("godot.gdscript.hover", missing_position, context);
	REQUIRE(missing_hover.status == MCPToolRegistry::CALL_OK);
	CHECK(_error_code(missing_hover.result) == "SCRIPT_NOT_FOUND");
	const MCPToolRegistry::CallResult missing_references = registry.call_tool("godot.gdscript.references", missing_position, context);
	REQUIRE(missing_references.status == MCPToolRegistry::CALL_OK);
	CHECK(_error_code(missing_references.result) == "SCRIPT_NOT_FOUND");
	Dictionary invalid_nested_position;
	invalid_nested_position["line"] = 0;
	invalid_nested_position["character"] = 0;
	invalid_nested_position["unexpected"] = true;
	Dictionary invalid_nested_arguments;
	invalid_nested_arguments["path"] = "res://mcp_semantic_missing.gd";
	invalid_nested_arguments["position"] = invalid_nested_position;
	const Dictionary invalid_nested_result = registry.call_tool("godot.gdscript.hover", invalid_nested_arguments, context).result;
	CHECK(_error_code(invalid_nested_result) == "INVALID_ARGUMENTS");
	CHECK(_error_details(invalid_nested_result).get("keyword", String()) == "additionalProperties");

	Dictionary negative_position = missing_position.duplicate();
	negative_position["line"] = -1;
	const MCPToolRegistry::CallResult negative_call = registry.call_tool("godot.gdscript.hover", negative_position, context);
	REQUIRE(negative_call.status == MCPToolRegistry::CALL_OK);
	CHECK(_error_code(negative_call.result) == "INVALID_ARGUMENTS");

	Dictionary mixed_position = missing_position.duplicate();
	Dictionary nested_position;
	nested_position["line"] = 0;
	nested_position["character"] = 0;
	mixed_position["position"] = nested_position;
	const MCPToolRegistry::CallResult mixed_call = registry.call_tool("godot.gdscript.hover", mixed_position, context);
	REQUIRE(mixed_call.status == MCPToolRegistry::CALL_OK);
	CHECK(_error_code(mixed_call.result) == "INVALID_ARGUMENTS");

	const MCPToolRegistry::CallResult missing_selector = registry.call_tool("godot.gdscript.diagnostics", Dictionary(), context);
	REQUIRE(missing_selector.status == MCPToolRegistry::CALL_OK);
	CHECK(_error_code(missing_selector.result) == "INVALID_ARGUMENTS");
	const Dictionary selector_error = _error_details(missing_selector.result);
	CHECK(selector_error.get("keyword", String()) == "oneOf");
	CHECK(int(selector_error.get("matchedSchemas", -1)) == 0);

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
	CHECK(service->remove_session(session->get_session_id()));
}

TEST_CASE("[MCP][Provider] GDScript semantic tools reject missing analysis injection") {
	MCPGDScriptProvider provider;
	Dictionary arguments;
	arguments["path"] = "res://missing.gd";
	const Dictionary result = provider.diagnostics(arguments, Dictionary());
	CHECK(bool(result.get("isError", false)));
	CHECK(_error_code(result) == "ANALYSIS_UNAVAILABLE");
}

TEST_CASE("[MCP][Provider] GDScript semantic tools isolate MCP sessions") {
	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisService> service;
	service.instantiate();
	service->configure("res://", workspace);
	Ref<GDScriptAnalysisSession> fallback_session = service->create_session();

	MCPToolRegistry registry;
	MCPGDScriptProvider *provider = memnew(MCPGDScriptProvider(service, fallback_session));
	REQUIRE(provider->register_tools(&registry) == OK);
	MCPGDScriptSessionManager *session_manager = provider->get_session_manager();
	REQUIRE(session_manager);
	CHECK(session_manager->get_session_count() == 0);

	const String first_context_id = "mcp-client-first";
	const String second_context_id = "mcp-client-second";
	Ref<GDScriptAnalysisSession> first_session = session_manager->get_or_create_session(first_context_id);
	Ref<GDScriptAnalysisSession> second_session = session_manager->get_or_create_session(second_context_id);
	REQUIRE(first_session.is_valid());
	REQUIRE(second_session.is_valid());
	CHECK(first_session != second_session);
	CHECK(first_session != fallback_session);
	CHECK(second_session != fallback_session);
	CHECK(session_manager->get_or_create_session(first_context_id) == first_session);
	CHECK(session_manager->get_session_count() == 2);

	const String path = "res://mcp_context_session_isolation.gd";
	const String valid_source = "var value: int = 1\n";
	const String changed_source = valid_source + "var other: int = 2\n";
	const String invalid_source = "var value: int =\n";
	REQUIRE(first_session->open_document(path, valid_source, LSP::LanguageId::GDSCRIPT, 1) == OK);
	REQUIRE(second_session->open_document(path, invalid_source, LSP::LanguageId::GDSCRIPT, 7) == OK);
	CHECK(first_session->get_diagnostics(path).is_empty());
	CHECK_FALSE(second_session->get_diagnostics(path).is_empty());
	const uint64_t second_revision = second_session->get_revision();
	REQUIRE(first_session->change_document(path, changed_source, 2) == OK);
	CHECK_GT(first_session->get_revision(), second_revision);
	CHECK_EQ(second_session->get_revision(), second_revision);
	CHECK_FALSE(fallback_session->has_document(path));

	const uint64_t first_analysis_id = first_session->get_session_id();
	const uint64_t second_analysis_id = second_session->get_session_id();
	CHECK(provider->release_session(first_context_id));
	CHECK(service->get_session(first_analysis_id).is_null());
	CHECK(session_manager->get_session_count() == 1);
	provider->clear_sessions();
	CHECK(service->get_session(second_analysis_id).is_null());
	CHECK(session_manager->get_session_count() == 0);
	CHECK(service->get_session(fallback_session->get_session_id()) == fallback_session);

	provider->unregister_tools();
	memdelete(provider);
	first_session.unref();
	second_session.unref();
	CHECK(service->remove_session(fallback_session->get_session_id()));
}

TEST_CASE("[MCP][Provider] GDScript queries synchronize project authority into every MCP session") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp-gdscript-authority", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());
	REQUIRE(temporary_directory->make_dir_recursive("project/scripts") == OK);
	REQUIRE(temporary_directory->make_dir("outside") == OK);
	const String root = temporary_directory->get_current_dir();
	const String project_root = root.path_join("project");
	const String absolute_path = project_root.path_join("scripts/authoritative.gd");
	const String outside_path = root.path_join("outside/outside.gd");
	const String resource_path = "res://scripts/authoritative.gd";
	const String authoritative_source = "var authoritative: int =\n";
	const String stale_source = "var stale: int = 1\n";
	REQUIRE(_write_text(absolute_path, authoritative_source) == OK);
	REQUIRE(_write_text(outside_path, "var outside: int = 1\n") == OK);

	Ref<GDScriptWorkspace> workspace;
	workspace.instantiate();
	Ref<GDScriptAnalysisService> service;
	service.instantiate();
	service->configure(project_root, workspace);
	MCPGDScriptProvider *provider = memnew(MCPGDScriptProvider(service));
	MCPToolRegistry registry;
	REQUIRE(provider->register_tools(&registry) == OK);
	MCPGDScriptSessionManager *session_manager = provider->get_session_manager();
	REQUIRE(session_manager);

	const String first_id = "authoritative-first";
	const String second_id = "authoritative-second";
	Ref<GDScriptAnalysisSession> first_session = session_manager->get_or_create_session(first_id);
	Ref<GDScriptAnalysisSession> second_session = session_manager->get_or_create_session(second_id);
	REQUIRE(first_session.is_valid());
	REQUIRE(second_session.is_valid());
	REQUIRE(first_session->open_document(resource_path, stale_source, LSP::LanguageId::GDSCRIPT, 1) == OK);
	REQUIRE(second_session->open_document(resource_path, stale_source, LSP::LanguageId::GDSCRIPT, 2) == OK);

	Dictionary arguments;
	arguments["path"] = absolute_path;
	MCPToolCallContext first_context;
	first_context.session["sessionId"] = first_id;
	MCPToolCallContext second_context;
	second_context.session["sessionId"] = second_id;

	const MCPToolRegistry::CallResult first_call = registry.call_tool("godot.gdscript.diagnostics", arguments, first_context);
	const MCPToolRegistry::CallResult second_call = registry.call_tool("godot.gdscript.diagnostics", arguments, second_context);
	REQUIRE(first_call.status == MCPToolRegistry::CALL_OK);
	REQUIRE(second_call.status == MCPToolRegistry::CALL_OK);
	REQUIRE_FALSE(bool(first_call.result.get("isError", false)));
	REQUIRE_FALSE(bool(second_call.result.get("isError", false)));
	const Dictionary first_result = first_call.result.get("structuredContent", Dictionary());
	const Dictionary second_result = second_call.result.get("structuredContent", Dictionary());
	CHECK(first_result.get("sha256", String()) == authoritative_source.sha256_text());
	CHECK(second_result.get("sha256", String()) == authoritative_source.sha256_text());
	CHECK_FALSE(Array(first_result.get("diagnostics", Array())).is_empty());
	CHECK(Array(first_result.get("diagnostics", Array())) == Array(second_result.get("diagnostics", Array())));
	REQUIRE(first_session->get_document(resource_path));
	REQUIRE(second_session->get_document(resource_path));
	CHECK(first_session->get_document(resource_path)->text == authoritative_source);
	CHECK(second_session->get_document(resource_path)->text == authoritative_source);

	arguments["path"] = outside_path;
	CHECK(_error_code(registry.call_tool("godot.gdscript.diagnostics", arguments, first_context).result) == "INVALID_ARGUMENTS");
	arguments.erase("path");
	arguments["uri"] = workspace->get_file_uri(outside_path);
	CHECK(_error_code(registry.call_tool("godot.gdscript.diagnostics", arguments, first_context).result) == "INVALID_ARGUMENTS");
	arguments.erase("uri");
	arguments["path"] = "res://../outside.gd";
	CHECK(_error_code(registry.call_tool("godot.gdscript.diagnostics", arguments, first_context).result) == "INVALID_ARGUMENTS");

	const String link_path = project_root.path_join("scripts/external_link");
	if (temporary_directory->create_link(root.path_join("outside"), link_path) == OK) {
		arguments["path"] = "res://scripts/external_link/outside.gd";
		CHECK(_error_code(registry.call_tool("godot.gdscript.diagnostics", arguments, first_context).result) == "INVALID_ARGUMENTS");
		arguments.erase("path");
		arguments["uri"] = workspace->get_file_uri(link_path.path_join("outside.gd"));
		CHECK(_error_code(registry.call_tool("godot.gdscript.diagnostics", arguments, first_context).result) == "INVALID_ARGUMENTS");
		CHECK(DirAccess::remove_absolute(link_path) == OK);
	}

	provider->clear_sessions();
	provider->unregister_tools();
	memdelete(provider);
}

} // namespace TestMCPGDScriptProvider

#endif // MODULE_GDSCRIPT_ENABLED && !GDSCRIPT_NO_LSP
