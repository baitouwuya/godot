/**************************************************************************/
/*  gdscript_lsp_cli_runner.cpp                                           */
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

#include "gdscript_lsp_cli_runner.h"

#include "gdscript_extend_parser.h"
#include "gdscript_language_protocol.h"
#include "gdscript_workspace.h"
#include "godot_lsp.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/os/os.h"

static bool _consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error) {
	if (!r_next) {
		r_error = "Missing value after " + p_arg + ".";
		return false;
	}

	r_value = r_next->get();
	r_next = r_next->next();
	return true;
}

static bool _is_position_query(const String &p_query) {
	return p_query == "hover" ||
			p_query == "definition" ||
			p_query == "declaration" ||
			p_query == "references" ||
			p_query == "completion" ||
			p_query == "signature-help";
}

static bool _is_supported_query(const String &p_query) {
	return _is_position_query(p_query) || p_query == "document-symbol";
}

static String _severity_name(int p_severity) {
	switch (p_severity) {
		case LSP::DiagnosticSeverity::Error:
			return "error";
		case LSP::DiagnosticSeverity::Warning:
			return "warning";
		case LSP::DiagnosticSeverity::Information:
			return "information";
		case LSP::DiagnosticSeverity::Hint:
			return "hint";
		default:
			return "unknown";
	}
}

static void _print_json_stdout(const Variant &p_value) {
	const bool stdout_was_enabled = OS::get_singleton()->is_stdout_enabled();
	const bool print_line_was_enabled = Engine::get_singleton()->is_printing_to_stdout();
	OS::get_singleton()->set_stdout_enabled(true);
	Engine::get_singleton()->set_print_to_stdout(true);
	OS::get_singleton()->print("%s\n", JSON::stringify(p_value).utf8().get_data());
	Engine::get_singleton()->set_print_to_stdout(print_line_was_enabled);
	OS::get_singleton()->set_stdout_enabled(stdout_was_enabled);
}

static bool _severity_matches(int p_severity, GDScriptLSPCLIRunner::DiagnosticsSeverity p_filter) {
	switch (p_filter) {
		case GDScriptLSPCLIRunner::DIAGNOSTICS_SEVERITY_ERROR:
			return p_severity == LSP::DiagnosticSeverity::Error;
		case GDScriptLSPCLIRunner::DIAGNOSTICS_SEVERITY_WARNING:
			return p_severity == LSP::DiagnosticSeverity::Warning;
		case GDScriptLSPCLIRunner::DIAGNOSTICS_SEVERITY_ALL:
			return true;
	}

	return true;
}

static bool _parse_bool_string(const String &p_value, bool &r_value) {
	String normalized = p_value.to_lower();
	if (normalized == "true" || normalized == "1" || normalized == "yes") {
		r_value = true;
		return true;
	}
	if (normalized == "false" || normalized == "0" || normalized == "no") {
		r_value = false;
		return true;
	}

	return false;
}

static bool _parse_json_dictionary(const String &p_json, Dictionary &r_params, String &r_error) {
	Ref<JSON> json;
	json.instantiate();
	const Error err = json->parse(p_json);
	if (err != OK) {
		r_error = vformat("Error parsing --params-json at line %d: %s", json->get_error_line(), json->get_error_message());
		return false;
	}

	Variant data = json->get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		r_error = "--params-json must parse to a JSON object.";
		return false;
	}

	r_params = data;
	return true;
}

static Dictionary _make_text_document_params(const String &p_uri, int p_line, int p_column) {
	Dictionary text_document;
	text_document["uri"] = p_uri;

	Dictionary position;
	position["line"] = p_line - 1;
	position["character"] = p_column - 1;

	Dictionary params;
	params["textDocument"] = text_document;
	params["position"] = position;
	return params;
}

static Dictionary _make_document_symbol_params(const String &p_uri) {
	Dictionary text_document;
	text_document["uri"] = p_uri;

	Dictionary params;
	params["textDocument"] = text_document;
	return params;
}

static String _file_to_uri(const Ref<GDScriptWorkspace> &p_workspace, const String &p_file) {
	if (p_file.begins_with("file://")) {
		return p_file;
	}

	return p_workspace->get_file_uri(GDScriptLSPCLIRunner::normalize_file_path(p_file));
}

static Variant _run_query(const GDScriptLSPCLIRunner::Options &p_options, String &r_error) {
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	Ref<GDScriptWorkspace> workspace = protocol->get_workspace();
	Ref<GDScriptTextDocument> text_document = protocol->get_text_document();

	Dictionary params;
	if (GDScriptLSPCLIRunner::build_query_params(p_options, workspace, params, r_error) != OK) {
		return Variant();
	}

	if (p_options.query == "hover") {
		return text_document->hover(params);
	} else if (p_options.query == "definition") {
		return text_document->definition(params);
	} else if (p_options.query == "declaration") {
		return text_document->declaration(params);
	} else if (p_options.query == "references") {
		if (!params.has("context")) {
			Dictionary context;
			context["includeDeclaration"] = p_options.include_declaration;
			params["context"] = context;
		}
		return text_document->references(params);
	} else if (p_options.query == "document-symbol") {
		return text_document->documentSymbol(params);
	} else if (p_options.query == "completion") {
		return text_document->completion(params);
	} else if (p_options.query == "signature-help") {
		return text_document->signatureHelp(params);
	}

	r_error = "Unsupported --lsp-query operation: " + p_options.query;
	return Variant();
}

static Dictionary _make_diagnostic_json(const Ref<GDScriptWorkspace> &p_workspace, const String &p_path, const LSP::Diagnostic &p_diagnostic) {
	Dictionary diagnostic = p_diagnostic.to_json();
	diagnostic["path"] = p_path;
	diagnostic["uri"] = p_workspace->get_file_uri(p_path);
	diagnostic["severityName"] = _severity_name(p_diagnostic.severity);
	return diagnostic;
}

static int _run_diagnostics(const GDScriptLSPCLIRunner::Options &p_options) {
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	Ref<GDScriptWorkspace> workspace = protocol->get_workspace();

	List<String> paths;
	workspace->list_script_files("res://", paths);
	paths.sort();

	Array diagnostics;
	bool should_fail = false;

	for (const String &path : paths) {
		ExtendGDScriptParser *parser = protocol->get_parse_result(path);
		if (!parser) {
			continue;
		}

		for (const LSP::Diagnostic &diagnostic : parser->get_diagnostics()) {
			if (!_severity_matches(diagnostic.severity, p_options.diagnostics_severity)) {
				continue;
			}

			should_fail = should_fail || GDScriptLSPCLIRunner::should_fail_for_severity(diagnostic.severity, p_options.diagnostics_fail_on);

			Dictionary diagnostic_json = _make_diagnostic_json(workspace, path, diagnostic);
			if (p_options.diagnostics_format == GDScriptLSPCLIRunner::DIAGNOSTICS_FORMAT_JSONL) {
				_print_json_stdout(diagnostic_json);
			} else {
				diagnostics.push_back(diagnostic_json);
			}
		}
	}

	protocol->clear_stale_parsers();

	if (p_options.diagnostics_format == GDScriptLSPCLIRunner::DIAGNOSTICS_FORMAT_JSON) {
		_print_json_stdout(diagnostics);
	} else if (p_options.diagnostics_format == GDScriptLSPCLIRunner::DIAGNOSTICS_FORMAT_SUMMARY) {
		_print_json_stdout(GDScriptLSPCLIRunner::summarize_diagnostics(diagnostics));
	}

	return should_fail ? GDScriptLSPCLIRunner::EXIT_DIAGNOSTICS_FOUND : GDScriptLSPCLIRunner::EXIT_OK;
}

bool GDScriptLSPCLIRunner::has_entrypoint_argument(const List<String> &p_args) {
	for (const String &arg : p_args) {
		if (arg == "--lsp-query" || arg == "--lsp-diagnostics") {
			return true;
		}
	}

	return false;
}

bool GDScriptLSPCLIRunner::parse_argument(const String &p_arg, List<String>::Element *&r_next, bool p_has_entrypoint_argument, Options &r_options, String &r_error) {
	String value;
	r_error = String();

	if (p_arg == "--lsp-query") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		r_options.query = value;
		return true;
	}

	if (p_arg == "--lsp-diagnostics") {
		r_options.diagnostics = true;
		return true;
	}

	if (!p_has_entrypoint_argument) {
		return false;
	}

	if (p_arg == "--file") {
		if (!_consume_argument_value(p_arg, r_next, r_options.file, r_error)) {
			return true;
		}
		return true;
	}

	if (p_arg == "--line") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		r_options.line = value.to_int();
		return true;
	}

	if (p_arg == "--column") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		r_options.column = value.to_int();
		return true;
	}

	if (p_arg == "--params-json") {
		if (!_consume_argument_value(p_arg, r_next, r_options.params_json, r_error)) {
			return true;
		}
		return true;
	}

	if (p_arg == "--include-declaration") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}

		if (!_parse_bool_string(value, r_options.include_declaration)) {
			r_error = "--include-declaration must be true or false.";
		}
		return true;
	}

	if (p_arg == "--diagnostics-format") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}

		value = value.to_lower();
		if (value == "jsonl") {
			r_options.diagnostics_format = DIAGNOSTICS_FORMAT_JSONL;
		} else if (value == "json") {
			r_options.diagnostics_format = DIAGNOSTICS_FORMAT_JSON;
		} else if (value == "summary") {
			r_options.diagnostics_format = DIAGNOSTICS_FORMAT_SUMMARY;
		} else {
			r_error = "--diagnostics-format must be jsonl, json or summary.";
		}
		return true;
	}

	if (p_arg == "--diagnostics-severity") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}

		value = value.to_lower();
		if (value == "error") {
			r_options.diagnostics_severity = DIAGNOSTICS_SEVERITY_ERROR;
		} else if (value == "warning") {
			r_options.diagnostics_severity = DIAGNOSTICS_SEVERITY_WARNING;
		} else if (value == "all") {
			r_options.diagnostics_severity = DIAGNOSTICS_SEVERITY_ALL;
		} else {
			r_error = "--diagnostics-severity must be error, warning or all.";
		}
		return true;
	}

	if (p_arg == "--diagnostics-fail-on") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}

		value = value.to_lower();
		if (value == "error") {
			r_options.diagnostics_fail_on = DIAGNOSTICS_FAIL_ON_ERROR;
		} else if (value == "warning") {
			r_options.diagnostics_fail_on = DIAGNOSTICS_FAIL_ON_WARNING;
		} else if (value == "any") {
			r_options.diagnostics_fail_on = DIAGNOSTICS_FAIL_ON_ANY;
		} else if (value == "never") {
			r_options.diagnostics_fail_on = DIAGNOSTICS_FAIL_ON_NEVER;
		} else {
			r_error = "--diagnostics-fail-on must be error, warning, any or never.";
		}
		return true;
	}

	return false;
}

void GDScriptLSPCLIRunner::apply_startup_options(const Options &p_options, bool &r_editor, bool &r_cmdline_tool, bool &r_wait_for_import, bool &r_quiet_stdout, bool &r_recovery_mode) {
	if (!is_enabled(p_options)) {
		return;
	}

	r_editor = true;
	r_cmdline_tool = true;
	r_wait_for_import = true;
	r_quiet_stdout = true;
	r_recovery_mode = true;
}

String GDScriptLSPCLIRunner::normalize_file_path(const String &p_file) {
	if (p_file.begins_with("file://")) {
		return p_file;
	}

	if (p_file.is_relative_path() && !p_file.is_resource_file()) {
		return "res://" + p_file;
	}

	return p_file;
}

Error GDScriptLSPCLIRunner::build_query_params(const Options &p_options, const Ref<GDScriptWorkspace> &p_workspace, Dictionary &r_params, String &r_error) {
	if (!p_options.params_json.is_empty()) {
		if (!_parse_json_dictionary(p_options.params_json, r_params, r_error)) {
			return ERR_INVALID_PARAMETER;
		}
	} else {
		const String uri = _file_to_uri(p_workspace, p_options.file);
		if (p_options.query == "document-symbol") {
			r_params = _make_document_symbol_params(uri);
		} else {
			r_params = _make_text_document_params(uri, p_options.line, p_options.column);
		}
	}

	if (p_options.query == "references" && !r_params.has("context")) {
		Dictionary context;
		context["includeDeclaration"] = p_options.include_declaration;
		r_params["context"] = context;
	}

	return OK;
}

Dictionary GDScriptLSPCLIRunner::summarize_diagnostics(const Array &p_diagnostics) {
	Dictionary by_severity;
	Dictionary by_file;

	for (const Variant &item : p_diagnostics) {
		Dictionary diagnostic = item;
		String severity_name = diagnostic.get("severityName", "unknown");
		by_severity[severity_name] = int(by_severity.get(severity_name, 0)) + 1;

		String path = diagnostic.get("path", diagnostic.get("uri", ""));
		by_file[path] = int(by_file.get(path, 0)) + 1;
	}

	Dictionary summary;
	summary["total"] = p_diagnostics.size();
	summary["bySeverity"] = by_severity;
	summary["byFile"] = by_file;
	return summary;
}

bool GDScriptLSPCLIRunner::should_fail_for_severity(int p_severity, DiagnosticsFailOn p_fail_on) {
	switch (p_fail_on) {
		case DIAGNOSTICS_FAIL_ON_ERROR:
			return p_severity == LSP::DiagnosticSeverity::Error;
		case DIAGNOSTICS_FAIL_ON_WARNING:
			return p_severity == LSP::DiagnosticSeverity::Error || p_severity == LSP::DiagnosticSeverity::Warning;
		case DIAGNOSTICS_FAIL_ON_ANY:
			return true;
		case DIAGNOSTICS_FAIL_ON_NEVER:
			return false;
	}

	return false;
}

bool GDScriptLSPCLIRunner::is_enabled(const Options &p_options) {
	return !p_options.query.is_empty() || p_options.diagnostics;
}

Error GDScriptLSPCLIRunner::validate_options(const Options &p_options, String &r_error) {
	if (!is_enabled(p_options)) {
		return OK;
	}

	if (!p_options.query.is_empty() && p_options.diagnostics) {
		r_error = "--lsp-query and --lsp-diagnostics are mutually exclusive.";
		return ERR_INVALID_PARAMETER;
	}

	if (!p_options.diagnostics) {
		if (p_options.diagnostics_format != DIAGNOSTICS_FORMAT_JSONL) {
			r_error = "--diagnostics-format is only valid with --lsp-diagnostics.";
			return ERR_INVALID_PARAMETER;
		}
		if (p_options.diagnostics_severity != DIAGNOSTICS_SEVERITY_ALL) {
			r_error = "--diagnostics-severity is only valid with --lsp-diagnostics.";
			return ERR_INVALID_PARAMETER;
		}
		if (p_options.diagnostics_fail_on != DIAGNOSTICS_FAIL_ON_ANY) {
			r_error = "--diagnostics-fail-on is only valid with --lsp-diagnostics.";
			return ERR_INVALID_PARAMETER;
		}
	}

	if (!p_options.query.is_empty()) {
		if (!_is_supported_query(p_options.query)) {
			r_error = "Unsupported --lsp-query operation: " + p_options.query;
			return ERR_INVALID_PARAMETER;
		}

		const bool explicit_location = !p_options.file.is_empty() || p_options.line != -1 || p_options.column != -1;
		if (!p_options.params_json.is_empty() && explicit_location) {
			r_error = "--params-json is mutually exclusive with --file, --line and --column.";
			return ERR_INVALID_PARAMETER;
		}

		if (p_options.params_json.is_empty()) {
			if (p_options.file.is_empty()) {
				r_error = "--lsp-query requires --file unless --params-json is used.";
				return ERR_INVALID_PARAMETER;
			}
			if (_is_position_query(p_options.query)) {
				if (p_options.line < 1 || p_options.column < 1) {
					r_error = "--lsp-query " + p_options.query + " requires 1-based --line and --column values.";
					return ERR_INVALID_PARAMETER;
				}
			} else if (p_options.line != -1 || p_options.column != -1) {
				r_error = "--line and --column are only valid for position-based --lsp-query operations.";
				return ERR_INVALID_PARAMETER;
			}
		}

		if (p_options.query != "references" && !p_options.include_declaration) {
			r_error = "--include-declaration is only valid with --lsp-query references.";
			return ERR_INVALID_PARAMETER;
		}
	}

	return OK;
}

int GDScriptLSPCLIRunner::run(const Options &p_options) {
	ERR_FAIL_COND_V(!is_enabled(p_options), EXIT_OK);

	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	if (!protocol) {
		ERR_PRINT("GDScript LSP CLI failed: GDScriptLanguageProtocol is not initialized.");
		return EXIT_INITIALIZATION_FAILED;
	}

	const int previous_client_id = protocol->get_current_client();
	const int client_id = protocol->create_internal_client();
	if (client_id == LSP_NO_CLIENT) {
		ERR_PRINT("GDScript LSP CLI failed: Could not create an internal LSP client.");
		return EXIT_INITIALIZATION_FAILED;
	}

	int exit_code = EXIT_OK;
	if (protocol->initialize_for_current_client() != OK) {
		ERR_PRINT("GDScript LSP CLI failed: Could not initialize the GDScript language protocol.");
		protocol->remove_internal_client(client_id, previous_client_id);
		return EXIT_INITIALIZATION_FAILED;
	}

	if (p_options.diagnostics) {
		exit_code = _run_diagnostics(p_options);
	} else {
		String error;
		Variant result = _run_query(p_options, error);
		if (!error.is_empty()) {
			ERR_PRINT("GDScript LSP CLI failed: " + error);
			exit_code = EXIT_QUERY_FAILED;
		} else {
			_print_json_stdout(result);
		}
	}

	protocol->remove_internal_client(client_id, previous_client_id);
	return exit_code;
}
