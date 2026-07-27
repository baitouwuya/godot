/**************************************************************************/
/*  mcp_gdscript_provider.cpp                                             */
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

#include "mcp_gdscript_provider.h"

#include "mcp_gdscript_tool_utils.h"
#include "mcp_script_analysis_sync.h"
#include "mcp_script_buffer.h"
#include "mcp_tool_utils.h"
#include "mcp_workspace_edit_transaction.h"

#include "core/config/project_settings.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "core/object/script_language.h"

#include "modules/gdscript/language_server/gdscript_extend_parser.h"

namespace {

static constexpr int DEFAULT_COMPLETION_LIMIT = 100;
static constexpr int MAX_COMPLETION_LIMIT = 500;

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

static Dictionary _completion_schema() {
	Dictionary schema = MCPGDScriptToolUtils::position_schema();
	Dictionary properties = schema.get("properties", Dictionary());
	Dictionary limit;
	limit["type"] = "integer";
	limit["description"] = "Maximum completion items returned.";
	limit["minimum"] = 1;
	limit["maximum"] = MAX_COMPLETION_LIMIT;
	limit["default"] = DEFAULT_COMPLETION_LIMIT;
	properties["limit"] = limit;
	schema["properties"] = properties;
	return schema;
}

static Dictionary _non_negative_integer_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("integer", p_description);
	schema["minimum"] = 0;
	return schema;
}

static Dictionary _metadata_properties() {
	Dictionary properties;
	properties["path"] = MCPToolUtils::make_property_schema("string", "Analyzed project GDScript path.");
	properties["uri"] = MCPToolUtils::make_property_schema("string", "Analyzed GDScript file URI.");
	properties["analysisRevision"] = _non_negative_integer_schema("GDScript analysis session revision.");
	properties["revision"] = _non_negative_integer_schema("Open document client revision, when available.");
	Dictionary sha256 = MCPToolUtils::make_property_schema("string", "SHA-256 of the analyzed source text.");
	sha256["pattern"] = "^[0-9a-f]{64}$";
	properties["sha256"] = sha256;
	properties["sourceState"] = MCPToolUtils::make_property_schema("string", "Open or disk analysis source state.");
	Dictionary client_version = MCPToolUtils::make_property_schema("integer", "LSP client version, or -1 for disk documents.");
	client_version["minimum"] = -1;
	properties["clientVersion"] = client_version;
	return properties;
}

static PackedStringArray _metadata_required() {
	return PackedStringArray{ "path", "uri", "analysisRevision", "sha256", "sourceState", "clientVersion" };
}

static Dictionary _object_array_schema(const String &p_description) {
	Dictionary array = MCPToolUtils::make_property_schema("array", p_description);
	array["items"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	return array;
}

static Dictionary _nullable_object_schema(const String &p_description) {
	Dictionary schema;
	schema["type"] = PackedStringArray{ "object", "null" };
	schema["description"] = p_description;
	schema["additionalProperties"] = true;
	return schema;
}

static Dictionary _metadata_output_schema(const Dictionary &p_extra_properties, const PackedStringArray &p_extra_required) {
	Dictionary properties = _metadata_properties();
	for (const KeyValue<Variant, Variant> &entry : p_extra_properties) {
		properties[entry.key] = entry.value;
	}
	PackedStringArray required = _metadata_required();
	for (const String &name : p_extra_required) {
		required.push_back(name);
	}
	return MCPToolUtils::make_object_schema(properties, required);
}

static Dictionary _diagnostics_output_schema() {
	Dictionary properties;
	properties["diagnostics"] = _object_array_schema("GDScript diagnostics.");
	return _metadata_output_schema(properties, PackedStringArray{ "diagnostics" });
}

static Dictionary _symbols_output_schema() {
	Dictionary properties;
	properties["symbols"] = _object_array_schema("GDScript document symbols.");
	return _metadata_output_schema(properties, PackedStringArray{ "symbols" });
}

static Dictionary _completion_output_schema() {
	Dictionary properties;
	Dictionary items = _object_array_schema("Bounded LSP completion items.");
	items["maxItems"] = MAX_COMPLETION_LIMIT;
	properties["items"] = items;
	properties["totalItemCount"] = _non_negative_integer_schema("Total completion item count before truncation.");
	properties["isIncomplete"] = MCPToolUtils::make_property_schema("boolean", "Whether completion items were truncated.");
	return _metadata_output_schema(properties, PackedStringArray{ "items", "totalItemCount", "isIncomplete" });
}

static Dictionary _hover_output_schema() {
	Dictionary properties;
	properties["hover"] = _nullable_object_schema("LSP hover result, or null when no symbol is found.");
	properties["symbol"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	properties["found"] = MCPToolUtils::make_property_schema("boolean", "Whether hover information was found.");
	return _metadata_output_schema(properties, PackedStringArray{ "hover", "found" });
}

static Dictionary _locations_output_schema(bool p_include_operation) {
	Dictionary properties;
	properties["locations"] = _object_array_schema("Resolved LSP locations.");
	properties["found"] = MCPToolUtils::make_property_schema("boolean", "Whether locations were found.");
	properties["symbol"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	properties["native"] = MCPToolUtils::make_property_schema("boolean", "Whether the symbol resolves to a native class.");
	PackedStringArray required{ "locations", "found" };
	if (p_include_operation) {
		properties["operation"] = MCPToolUtils::make_property_schema("string", "Definition or declaration operation.");
		required.push_back("operation");
	}
	return _metadata_output_schema(properties, required);
}

static Dictionary _signature_output_schema() {
	Dictionary properties;
	properties["signatureHelp"] = _nullable_object_schema("LSP signature help, or null when unavailable.");
	properties["found"] = MCPToolUtils::make_property_schema("boolean", "Whether signature help was found.");
	return _metadata_output_schema(properties, PackedStringArray{ "signatureHelp", "found" });
}

static Dictionary _rename_output_schema() {
	Dictionary properties;
	properties["edit"] = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	properties["changed"] = MCPToolUtils::make_property_schema("boolean", "Whether the WorkspaceEdit contains changes.");
	return _metadata_output_schema(properties, PackedStringArray{ "edit", "changed" });
}

static Dictionary _workspace_edit_output_schema() {
	Dictionary properties;
	properties["applied"] = MCPToolUtils::make_property_schema("boolean", "Whether the WorkspaceEdit was applied.");
	properties["analysisSynchronized"] = MCPToolUtils::make_property_schema("boolean", "Whether all changed buffers were synchronized with analysis.");
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether changed buffers were saved.");
	properties["documentCount"] = _non_negative_integer_schema("Changed document count.");
	properties["documents"] = _object_array_schema("Changed authoritative Script editor buffer snapshots.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "applied", "analysisSynchronized", "saved", "documentCount", "documents" });
}

} // namespace

MCPGDScriptProvider::MCPGDScriptProvider(const Ref<GDScriptAnalysisService> &p_service, const Ref<GDScriptAnalysisSession> &p_fallback_session) {
	session_manager = memnew(MCPGDScriptSessionManager(p_service, p_fallback_session));
}

MCPGDScriptProvider::MCPGDScriptProvider(const Ref<MCPGDScriptSessionManager> &p_session_manager) {
	set_session_manager(p_session_manager);
}

MCPGDScriptProvider::~MCPGDScriptProvider() {
	unregister_tools();
}

void MCPGDScriptProvider::set_analysis_service(const Ref<GDScriptAnalysisService> &p_service) {
	if (session_manager.is_null()) {
		session_manager = memnew(MCPGDScriptSessionManager);
	}
	session_manager->set_analysis_service(p_service);
}

void MCPGDScriptProvider::set_analysis_session(const Ref<GDScriptAnalysisSession> &p_session) {
	if (session_manager.is_null()) {
		session_manager = memnew(MCPGDScriptSessionManager);
	}
	session_manager->set_fallback_session(p_session);
}

void MCPGDScriptProvider::set_session_manager(const Ref<MCPGDScriptSessionManager> &p_session_manager) {
	session_manager = p_session_manager;
}

Ref<GDScriptWorkspace> MCPGDScriptProvider::_get_workspace() const {
	if (session_manager.is_null()) {
		return Ref<GDScriptWorkspace>();
	}
	const Ref<GDScriptAnalysisService> &service = session_manager->get_analysis_service();
	if (service.is_valid() && service->get_workspace().is_valid()) {
		return service->get_workspace();
	}
	const Ref<GDScriptAnalysisSession> &fallback_session = session_manager->get_fallback_session();
	if (fallback_session.is_valid()) {
		return fallback_session->get_workspace();
	}
	return Ref<GDScriptWorkspace>();
}

String MCPGDScriptProvider::_get_project_root() const {
	if (session_manager.is_valid()) {
		const Ref<GDScriptAnalysisService> &service = session_manager->get_analysis_service();
		if (service.is_valid() && service->get_project_root().is_absolute_path() && !service->get_project_root().contains("://")) {
			return service->get_project_root();
		}
	}
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	return project_settings ? project_settings->get_resource_path() : String();
}

bool MCPGDScriptProvider::_is_ready(String &r_error) const {
	if (session_manager.is_null() || session_manager->get_analysis_service().is_null()) {
		r_error = "A GDScript analysis service is required.";
		return false;
	}
	if (_get_workspace().is_null()) {
		r_error = "A configured GDScript workspace is required.";
		return false;
	}
	return true;
}

bool MCPGDScriptProvider::_validate_ready(String &r_error, Dictionary &r_error_result) const {
	if (!_is_ready(r_error)) {
		r_error_result = _unavailable(r_error);
		return false;
	}
	return true;
}

bool MCPGDScriptProvider::_resolve_session(const Dictionary &p_context, String &r_session_id, Ref<GDScriptAnalysisSession> &r_session, String &r_error) {
	if (!_is_ready(r_error)) {
		return false;
	}
	return session_manager->resolve_context(p_context, r_session_id, r_session, &r_error) == OK;
}

bool MCPGDScriptProvider::_prepare_document(const Dictionary &p_context, const String &p_path, bool p_sync_open_buffers, Ref<GDScriptAnalysisSession> &r_session, Array *r_diagnostics, Dictionary &r_error_result) {
	String session_id;
	String error;
	if (!_resolve_session(p_context, session_id, r_session, error)) {
		r_error_result = _unavailable(error);
		return false;
	}
	if (p_sync_open_buffers && MCPScriptAnalysisSync::sync_open_buffers(session_manager, session_id, &error) != OK) {
		r_error_result = _unavailable(error);
		return false;
	}

	Dictionary snapshot;
	const Error sync_error = MCPScriptAnalysisSync::sync_authoritative_path(
			session_manager, session_id, p_path, _get_project_root(), snapshot, r_diagnostics, &error);
	if (sync_error == ERR_FILE_NOT_FOUND) {
		r_error_result = _script_not_found(p_path);
		return false;
	}
	if (sync_error != OK) {
		r_error_result = _unavailable(error);
		return false;
	}
	return true;
}

bool MCPGDScriptProvider::_parse_path(const Dictionary &p_arguments, String &r_path, String &r_error) const {
	const bool has_path = p_arguments.has("path");
	const bool has_uri = p_arguments.has("uri");
	if (has_path == has_uri) {
		r_error = "Exactly one of path or uri is required.";
		return false;
	}

	String raw_path;
	if (has_path) {
		if (!MCPGDScriptToolUtils::get_string_argument(p_arguments, "path", raw_path, r_error)) {
			return false;
		}
	} else if (!MCPGDScriptToolUtils::get_string_argument(p_arguments, "uri", raw_path, r_error)) {
		return false;
	}
	if (raw_path.is_empty()) {
		r_error = "path or uri must not be empty.";
		return false;
	}

	Ref<GDScriptWorkspace> workspace = _get_workspace();
	if (has_uri || raw_path.begins_with("file:")) {
		if (workspace.is_null()) {
			r_error = "A configured GDScript workspace is required to resolve a file URI.";
			return false;
		}
		r_path = workspace->get_file_path(raw_path);
	} else {
		r_path = raw_path;
	}
	if (r_path.is_empty()) {
		r_error = "The script path could not be resolved.";
		return false;
	}

	String resource_path;
	String absolute_path;
	bool built_in = false;
	const Error path_error = MCPScriptBuffer::resolve_script_path_for_root(r_path, _get_project_root(), resource_path, absolute_path, built_in, &r_error);
	if (path_error != OK) {
		r_path = String();
		return false;
	}
	r_path = resource_path;
	return true;
}

bool MCPGDScriptProvider::_parse_position(const Dictionary &p_arguments, String &r_path, LSP::TextDocumentPositionParams &r_params, String &r_error) const {
	if (!_parse_path(p_arguments, r_path, r_error)) {
		return false;
	}

	int line = 0;
	int character = 0;
	const bool has_position = p_arguments.has("position");
	const bool has_line = p_arguments.has("line");
	const bool has_character = p_arguments.has("character");
	if (has_position && (has_line || has_character)) {
		r_error = "Use either position or top-level line and character, not both.";
		return false;
	}
	if (!has_position && (!has_line || !has_character)) {
		r_error = "Either position or both line and character are required.";
		return false;
	}

	const Variant position_value = p_arguments.get("position", Variant());
	if (has_position && position_value.get_type() != Variant::DICTIONARY) {
		r_error = "position must be an object.";
		return false;
	}
	if (has_position) {
		const Dictionary position = position_value;
		if (!MCPGDScriptToolUtils::get_position_argument(position, "line", line, r_error) || !MCPGDScriptToolUtils::get_position_argument(position, "character", character, r_error)) {
			return false;
		}
	} else {
		if (!MCPGDScriptToolUtils::get_position_argument(p_arguments, "line", line, r_error) || !MCPGDScriptToolUtils::get_position_argument(p_arguments, "character", character, r_error)) {
			return false;
		}
	}

	Ref<GDScriptWorkspace> workspace = _get_workspace();
	Dictionary text_document;
	text_document["uri"] = workspace->get_file_uri(r_path);
	Dictionary position;
	position["line"] = line;
	position["character"] = character;
	Dictionary request;
	request["textDocument"] = text_document;
	request["position"] = position;
	r_params.load(request);
	return true;
}

bool MCPGDScriptProvider::_parse_reference_position(const Dictionary &p_arguments, String &r_path, LSP::ReferenceParams &r_params, String &r_error) const {
	LSP::TextDocumentPositionParams position_params;
	if (!_parse_position(p_arguments, r_path, position_params, r_error)) {
		return false;
	}
	r_params.textDocument = position_params.textDocument;
	r_params.position = position_params.position;
	const Variant include_declaration = p_arguments.get("includeDeclaration", false);
	if (include_declaration.get_type() != Variant::BOOL) {
		r_error = "includeDeclaration must be a boolean.";
		return false;
	}
	r_params.context.includeDeclaration = include_declaration;
	return true;
}

Dictionary MCPGDScriptProvider::_metadata(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path) const {
	Dictionary result;
	result["path"] = p_path;
	Ref<GDScriptWorkspace> workspace = _get_workspace();
	if (workspace.is_valid()) {
		result["uri"] = workspace->get_file_uri(p_path);
	}
	const GDScriptAnalysisSession::DocumentState *document = p_session->get_document(p_path);
	result["analysisRevision"] = document ? document->revision : p_session->get_revision();
	if (document) {
		if (document->source_state == GDScriptAnalysisSession::SOURCE_STATE_OPEN_DOCUMENT && document->client_version >= 0) {
			result["revision"] = document->client_version;
		}
		result["sha256"] = document->sha256;
		result["sourceState"] = document->source_state == GDScriptAnalysisSession::SOURCE_STATE_OPEN_DOCUMENT ? "open" : "disk";
		result["clientVersion"] = document->client_version;
	}
	return result;
}

Dictionary MCPGDScriptProvider::_invalid_arguments(const String &p_message) const {
	return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", p_message);
}

Dictionary MCPGDScriptProvider::_unavailable(const String &p_message) const {
	return MCPToolUtils::make_error_result("ANALYSIS_UNAVAILABLE", p_message);
}

Dictionary MCPGDScriptProvider::_script_not_found(const String &p_path) const {
	Dictionary details;
	details["path"] = p_path;
	return MCPToolUtils::make_error_result("SCRIPT_NOT_FOUND", "GDScript could not be loaded: " + p_path, details);
}

Dictionary MCPGDScriptProvider::_locations_result(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path, const Vector<LSP::Location> &p_locations, const LSP::DocumentSymbol *p_symbol) const {
	Dictionary result = _metadata(p_session, p_path);
	Array locations;
	for (const LSP::Location &location : p_locations) {
		locations.push_back(location.to_json());
	}
	result["locations"] = locations;
	result["found"] = !locations.is_empty();
	if (p_symbol) {
		result["symbol"] = p_symbol->to_json(true);
	}
	return MCPToolUtils::make_success_result(result);
}

Array MCPGDScriptProvider::_completion_items(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path, const List<ScriptLanguage::CodeCompletionOption> &p_options, int p_limit) const {
	Array items;
	items.resize(MIN(p_options.size(), p_limit));
	ExtendGDScriptParser *parser = p_session->get_parse_result(p_path);
	int index = 0;
	for (const ScriptLanguage::CodeCompletionOption &option : p_options) {
		if (index >= p_limit) {
			break;
		}
		LSP::CompletionItem item;
		item.label = option.display;
		item.insertText = option.insert_text;
		item.kind = MCPGDScriptToolUtils::completion_kind(option.kind);
		if (option.text_edit.is_set()) {
			item.textEdit.newText = option.text_edit.new_text;
			if (parser) {
				item.textEdit.range = parser->to_lsp_range(option.text_edit.start_line, option.text_edit.start_column, option.text_edit.end_line, option.text_edit.end_column);
			} else {
				item.textEdit.range = LSP::Range(option.text_edit.start_line, option.text_edit.start_column, option.text_edit.end_line, option.text_edit.end_column);
			}
		}
		items[index++] = item.to_json();
	}
	return items;
}

Dictionary MCPGDScriptProvider::_symbol_result(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path, const LSP::TextDocumentPositionParams &p_params, bool p_declaration) const {
	Dictionary result = _metadata(p_session, p_path);
	const LSP::DocumentSymbol *symbol = _get_workspace()->resolve_symbol(p_session, p_params);
	Array locations;
	if (symbol && !symbol->uri.is_empty()) {
		LSP::Location location;
		location.uri = symbol->uri;
		location.range = symbol->selectionRange;
		locations.push_back(location.to_json());
	}
	result["locations"] = locations;
	result["found"] = !locations.is_empty();
	if (symbol) {
		result["symbol"] = symbol->to_json(true);
		result["native"] = symbol->native_class.is_empty() ? false : true;
	}
	result["operation"] = p_declaration ? "declaration" : "definition";
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPGDScriptProvider::diagnostics(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	if (!_parse_path(p_arguments, path, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Array diagnostic_list;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, false, session, &diagnostic_list, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	Dictionary result = _metadata(session, path);
	result["diagnostics"] = diagnostic_list;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPGDScriptProvider::symbols(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	if (!_parse_path(p_arguments, path, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, false, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	ExtendGDScriptParser *parser = session->get_parse_result(path);
	if (!parser) {
		return _script_not_found(path);
	}
	Dictionary result = _metadata(session, path);
	Array symbol_list;
	symbol_list.push_back(parser->get_symbols().to_json(true));
	result["symbols"] = symbol_list;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPGDScriptProvider::completion(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams position;
	if (!_parse_position(p_arguments, path, position, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	int64_t limit = DEFAULT_COMPLETION_LIMIT;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("limit", DEFAULT_COMPLETION_LIMIT), 1, MAX_COMPLETION_LIMIT, limit)) {
		return _invalid_arguments("limit is outside its allowed range.");
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	LSP::CompletionParams params;
	params.textDocument = position.textDocument;
	params.position = position.position;
	List<ScriptLanguage::CodeCompletionOption> options;
	_get_workspace()->completion(session, params, &options);
	Dictionary result = _metadata(session, path);
	result["items"] = _completion_items(session, path, options, int(limit));
	result["totalItemCount"] = options.size();
	result["isIncomplete"] = options.size() > limit;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPGDScriptProvider::hover(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	Dictionary result = _metadata(session, path);
	const LSP::DocumentSymbol *symbol = _get_workspace()->resolve_symbol(session, params);
	if (symbol) {
		LSP::Hover hover_result;
		hover_result.contents = symbol->render();
		hover_result.range = symbol->selectionRange;
		result["hover"] = hover_result.to_json();
		result["symbol"] = symbol->to_json(true);
		result["found"] = true;
	} else {
		result["hover"] = Variant();
		result["found"] = false;
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPGDScriptProvider::definition(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	return _symbol_result(session, path, params, false);
}

Dictionary MCPGDScriptProvider::declaration(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	return _symbol_result(session, path, params, true);
}

Dictionary MCPGDScriptProvider::references(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::ReferenceParams params;
	if (!_parse_reference_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	const LSP::DocumentSymbol *symbol = _get_workspace()->resolve_symbol(session, params);
	if (!symbol) {
		Dictionary result = _metadata(session, path);
		result["locations"] = Array();
		result["found"] = false;
		return MCPToolUtils::make_success_result(result);
	}
	Vector<LSP::Location> usages = _get_workspace()->find_all_usages(session, *symbol);
	if (!params.context.includeDeclaration) {
		Vector<LSP::Location> filtered;
		for (const LSP::Location &usage : usages) {
			if (_get_workspace()->is_declaration_location(*symbol, usage)) {
				continue;
			}
			filtered.push_back(usage);
		}
		usages = filtered;
	}
	return _locations_result(session, path, usages, symbol);
}

Dictionary MCPGDScriptProvider::signature_help(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	LSP::SignatureHelp signature;
	const Error error = _get_workspace()->resolve_signature(session, params, signature);
	Dictionary result = _metadata(session, path);
	if (error == OK) {
		result["signatureHelp"] = signature.to_json();
		result["found"] = true;
	} else {
		result["signatureHelp"] = Variant();
		result["found"] = false;
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPGDScriptProvider::rename(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	String path;
	LSP::TextDocumentPositionParams params;
	if (!_parse_position(p_arguments, path, params, argument_error)) {
		return _invalid_arguments(argument_error);
	}
	String new_name;
	if (!MCPGDScriptToolUtils::get_string_argument(p_arguments, "newName", new_name, argument_error) || new_name.is_empty()) {
		return _invalid_arguments(new_name.is_empty() ? "newName must not be empty." : argument_error);
	}
	Ref<GDScriptAnalysisSession> session;
	Dictionary preparation_error;
	if (!_prepare_document(p_context, path, true, session, nullptr, preparation_error)) {
		return preparation_error;
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (!session->get_parse_result(path)) {
		return _script_not_found(path);
	}
	const Dictionary edit = _get_workspace()->rename(session, params, new_name);
	Dictionary result = _metadata(session, path);
	result["edit"] = edit;
	const Dictionary changes = edit.get("changes", Dictionary());
	result["changed"] = !changes.is_empty();
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPGDScriptProvider::apply_workspace_edit(const Dictionary &p_arguments, const Dictionary &p_context) {
	String argument_error;
	Dictionary request_error;
	if (!_validate_ready(argument_error, request_error)) {
		return request_error;
	}
	const Variant edit_value = p_arguments.get("edit", Variant());
	const Variant documents_value = p_arguments.get("documents", Variant());
	if (edit_value.get_type() != Variant::DICTIONARY || documents_value.get_type() != Variant::ARRAY) {
		return _invalid_arguments("edit must be a WorkspaceEdit object and documents must be an array.");
	}

	String session_id;
	Ref<GDScriptAnalysisSession> session;
	if (!_resolve_session(p_context, session_id, session, argument_error)) {
		return _unavailable(argument_error);
	}
	MCPGDScriptTransientParserCleanup cleanup(session);
	if (MCPScriptAnalysisSync::sync_open_buffers(session_manager, session_id, &argument_error) != OK) {
		return _unavailable(argument_error);
	}

	Dictionary result;
	String error_code;
	const Error error = MCPWorkspaceEditTransaction::apply(edit_value, documents_value, _get_workspace(), session_manager, session_id, result, error_code, argument_error);
	if (error != OK) {
		return MCPToolUtils::make_error_result(error_code.is_empty() ? "WORKSPACE_EDIT_FAILED" : error_code, argument_error, result);
	}
	return MCPToolUtils::make_success_result(result);
}

Error MCPGDScriptProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "GDScript tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
			{ "godot.gdscript.diagnostics", "Return diagnostics for a GDScript document.",
					MCPGDScriptToolUtils::diagnostics_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::diagnostics), _diagnostics_output_schema() },
			{ "godot.gdscript.symbols", "Return document symbols for a GDScript document.",
					MCPGDScriptToolUtils::diagnostics_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::symbols), _symbols_output_schema() },
			{ "godot.gdscript.completion", "Return bounded UTF-16 positioned GDScript completions.",
					_completion_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::completion), _completion_output_schema() },
			{ "godot.gdscript.hover", "Return GDScript hover information at a position.",
					MCPGDScriptToolUtils::position_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::hover), _hover_output_schema() },
			{ "godot.gdscript.definition", "Resolve the GDScript definition at a position.",
					MCPGDScriptToolUtils::position_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::definition), _locations_output_schema(true) },
			{ "godot.gdscript.declaration", "Resolve the GDScript declaration at a position.",
					MCPGDScriptToolUtils::position_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::declaration), _locations_output_schema(true) },
			{ "godot.gdscript.references", "Find GDScript references at a position.",
					MCPGDScriptToolUtils::references_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::references), _locations_output_schema(false) },
			{ "godot.gdscript.signature_help", "Return GDScript signature help at a call site.",
					MCPGDScriptToolUtils::position_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::signature_help), _signature_output_schema() },
			{ "godot.gdscript.rename", "Return a UTF-16 GDScript workspace edit for a symbol.",
					MCPGDScriptToolUtils::rename_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPGDScriptProvider::rename), _rename_output_schema() },
			{ "godot.gdscript.apply_workspace_edit", "Validate a UTF-16 WorkspaceEdit, then apply it to authoritative ScriptEditor buffers without saving.",
					MCPGDScriptToolUtils::workspace_edit_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPGDScriptProvider::apply_workspace_edit), _workspace_edit_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPGDScriptProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

void MCPGDScriptProvider::on_session_removed(const String &p_session_id) {
	release_session(p_session_id);
}

void MCPGDScriptProvider::shutdown() {
	clear_sessions();
}
