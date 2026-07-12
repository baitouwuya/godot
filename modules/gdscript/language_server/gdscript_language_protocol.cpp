/**************************************************************************/
/*  gdscript_language_protocol.cpp                                        */
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

#include "gdscript_language_protocol.h"

#include "godot_lsp.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/doc/doc_tools.h"
#include "editor/doc/editor_help.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/settings/editor_settings.h"

#define LSP_CLIENT_V(m_ret_val) \
	ERR_FAIL_COND_V(latest_client_id == LSP_NO_CLIENT, m_ret_val); \
	ERR_FAIL_COND_V(!clients.has(latest_client_id), m_ret_val); \
	Ref<LSPeer> client = clients.get(latest_client_id); \
	ERR_FAIL_COND_V(!client.is_valid(), m_ret_val);

#define LSP_CLIENT \
	ERR_FAIL_COND(latest_client_id == LSP_NO_CLIENT); \
	ERR_FAIL_COND(!clients.has(latest_client_id)); \
	Ref<LSPeer> client = clients.get(latest_client_id); \
	ERR_FAIL_COND(!client.is_valid());

GDScriptLanguageProtocol *GDScriptLanguageProtocol::singleton = nullptr;

static void _log_connection_message(const String &p_message) {
	EditorNode *editor_node = EditorNode::get_singleton();
	if (!editor_node || !editor_node->is_editor_ready()) {
		return;
	}
	EditorLog *editor_log = EditorNode::get_log();
	if (editor_log) {
		editor_log->add_message(p_message, EditorLog::MSG_TYPE_EDITOR);
	}
}

Error GDScriptLanguageProtocol::LSPeer::handle_data() {
	int read = 0;
	// Read headers
	if (!has_header) {
		while (true) {
			if (req_pos >= LSP_MAX_BUFFER_SIZE) {
				req_pos = 0;
				ERR_FAIL_V_MSG(ERR_OUT_OF_MEMORY, "Response header too big");
			}
			Error err = connection->get_partial_data(&req_buf[req_pos], 1, read);
			if (err != OK) {
				return FAILED;
			} else if (read != 1) { // Busy, wait until next poll
				return ERR_BUSY;
			}
			char *r = (char *)req_buf;
			int l = req_pos;

			// End of headers
			if (l > 3 && r[l] == '\n' && r[l - 1] == '\r' && r[l - 2] == '\n' && r[l - 3] == '\r') {
				r[l - 3] = '\0'; // Null terminate to read string
				String header = String::utf8(r);
				content_length = header.substr(16).to_int();
				has_header = true;
				req_pos = 0;
				break;
			}
			req_pos++;
		}
	}
	if (has_header) {
		while (req_pos < content_length) {
			if (req_pos >= LSP_MAX_BUFFER_SIZE) {
				req_pos = 0;
				has_header = false;
				ERR_FAIL_COND_V_MSG(req_pos >= LSP_MAX_BUFFER_SIZE, ERR_OUT_OF_MEMORY, "Response content too big");
			}
			Error err = connection->get_partial_data(&req_buf[req_pos], 1, read);
			if (err != OK) {
				return FAILED;
			} else if (read != 1) {
				return ERR_BUSY;
			}
			req_pos++;
		}

		// Parse data
		String msg = String::utf8((const char *)req_buf, req_pos);

		// Reset to read again
		req_pos = 0;
		has_header = false;

		// Response
		String output = GDScriptLanguageProtocol::get_singleton()->process_message(msg);
		if (analysis_session.is_valid()) {
			analysis_session->clear_transient_parsers();
		}
		if (!output.is_empty()) {
			res_queue.push_back(output.utf8());
		}
	}
	return OK;
}

Error GDScriptLanguageProtocol::LSPeer::send_data() {
	int sent = 0;
	while (!res_queue.is_empty()) {
		CharString c_res = res_queue[0];
		if (res_sent < c_res.size()) {
			Error err = connection->put_partial_data((const uint8_t *)c_res.get_data() + res_sent, c_res.size() - res_sent - 1, sent);
			if (err != OK) {
				return err;
			}
			res_sent += sent;
		}
		// Response sent
		if (res_sent >= c_res.size() - 1) {
			res_sent = 0;
			res_queue.remove_at(0);
		}
	}
	return OK;
}

Error GDScriptLanguageProtocol::on_client_connected() {
	Ref<StreamPeerTCP> tcp_peer = server->take_connection();
	ERR_FAIL_COND_V_MSG(clients.size() >= LSP_MAX_CLIENTS, FAILED, "Max client limits reached");
	Ref<LSPeer> peer = memnew(LSPeer);
	peer->connection = tcp_peer;
	peer->analysis_session = analysis_service->create_session();
	peer->analysis_session_id = peer->analysis_session->get_session_id();
	clients.insert(next_client_id, peer);
	next_client_id++;
	_log_connection_message("[LSP] Connection Taken");
	return OK;
}

bool GDScriptLanguageProtocol::_remove_client(int p_client_id) {
	const Ref<LSPeer> *peer = clients.getptr(p_client_id);
	if (!peer) {
		return false;
	}
	if ((*peer).is_valid() && analysis_service.is_valid()) {
		analysis_service->remove_session((*peer)->analysis_session_id);
	}
	clients.erase(p_client_id);
	if (latest_client_id == p_client_id) {
		latest_client_id = clients.is_empty() ? LSP_NO_CLIENT : clients.begin()->key;
	}
	if (clients.is_empty()) {
		scene_cache.clear();
	}
	return true;
}

void GDScriptLanguageProtocol::on_client_disconnected(const int &p_client_id) {
	if (_remove_client(p_client_id)) {
		_log_connection_message("[LSP] Disconnected");
	}
}

String GDScriptLanguageProtocol::process_message(const String &p_text) {
	String ret = process_string(p_text);
	if (ret.is_empty()) {
		return ret;
	} else {
		return format_output(ret);
	}
}

String GDScriptLanguageProtocol::format_output(const String &p_text) {
	String header = "Content-Length: ";
	CharString charstr = p_text.utf8();
	size_t len = charstr.length();
	header += itos(len);
	header += "\r\n\r\n";

	return header + p_text;
}

void GDScriptLanguageProtocol::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_text_document"), &GDScriptLanguageProtocol::get_text_document);
	ClassDB::bind_method(D_METHOD("get_workspace"), &GDScriptLanguageProtocol::get_workspace);
	ClassDB::bind_method(D_METHOD("is_smart_resolve_enabled"), &GDScriptLanguageProtocol::is_smart_resolve_enabled);
	ClassDB::bind_method(D_METHOD("is_initialized"), &GDScriptLanguageProtocol::is_initialized);

#ifndef DISABLE_DEPRECATED
	ClassDB::bind_method(D_METHOD("initialize", "params"), &GDScriptLanguageProtocol::initialize);
	ClassDB::bind_method(D_METHOD("initialized", "params"), &GDScriptLanguageProtocol::initialized);
	ClassDB::bind_method(D_METHOD("on_client_connected"), &GDScriptLanguageProtocol::on_client_connected);
	ClassDB::bind_method(D_METHOD("on_client_disconnected", "client_id"), &GDScriptLanguageProtocol::on_client_disconnected);
	ClassDB::bind_method(D_METHOD("notify_client", "method", "params", "client_id"), &GDScriptLanguageProtocol::notify_client, DEFVAL(Variant()), DEFVAL(-1));
#endif // !DISABLE_DEPRECATED
}

template <typename T>
Variant get_deep(Variant p_dict, Variant p_default, T p_key) {
	if (p_dict.get_type() != Variant::DICTIONARY) {
		return p_default;
	}
	return p_dict.operator Dictionary().get(p_key, p_default);
}

template <typename T1, typename... T2>
Variant get_deep(Variant p_dict, Variant p_default, T1 p_key1, T2... p_key2) {
	if (p_dict.get_type() != Variant::DICTIONARY || !p_dict.operator Dictionary().has(p_key1)) {
		return p_default;
	}

	return get_deep(p_dict.operator Dictionary()[p_key1], p_default, p_key2...);
}

Variant GDScriptLanguageProtocol::initialize(const Dictionary &p_params) {
	LSP_CLIENT_V(Variant());

	LSP::InitializeResult ret;

	{
		// Warn if the workspace root does not match with the project that is currently open in Godot,
		// since it might lead to unexpected behavior, like wrong warnings about duplicate class names.

		String root;
		Variant root_uri_var = p_params["rootUri"];
		Variant root_var = p_params.get("rootPath", Variant());
		if (root_uri_var.is_string()) {
			root = get_workspace()->get_file_path(root_uri_var);
		} else if (root_var.is_string()) {
			root = root_var;
		}

		if (ProjectSettings::get_singleton()->localize_path(root) != "res://") {
			LSP::ShowMessageParams params{
				LSP::MessageType::Warning,
				"The GDScript Language Server might not work correctly with other projects than the one opened in Godot."
			};
			notify_client("window/showMessage", params.to_json());
		}
	}

	String root_uri = p_params["rootUri"];
	String root = p_params.get("rootPath", "");
	bool is_same_workspace;
#ifndef WINDOWS_ENABLED
	is_same_workspace = root.to_lower() == workspace->root.to_lower();
#else
	is_same_workspace = root.replace_char('\\', '/').to_lower() == workspace->root.to_lower();
#endif

	if (root_uri.length() && is_same_workspace) {
		workspace->root_uri = root_uri;
	} else {
		String r_root = workspace->root;
		r_root = r_root.lstrip("/");
		workspace->root_uri = "file:///" + r_root;

		Dictionary params;
		params["path"] = workspace->root;
		Dictionary request = make_notification("gdscript_client/changeWorkspace", params);

		ERR_FAIL_COND_V_MSG(!clients.has(latest_client_id), ret.to_json(),
				vformat("GDScriptLanguageProtocol: Can't initialize invalid peer '%d'.", latest_client_id));
		Ref<LSPeer> peer = clients.get(latest_client_id);
		if (peer.is_valid()) {
			String msg = Variant(request).to_json_string();
			msg = format_output(msg);
			(*peer)->res_queue.push_back(msg.utf8());
		}
	}

	if (!_initialized) {
		workspace->initialize();
		_initialized = true;
	}

	// Handle client capabilities.
	Dictionary capabilities = p_params["capabilities"];
	client->behavior.use_snippets_for_brace_completion = get_deep(capabilities, false,
			"textDocument", "completion", "completionItem", "snippetSupport");

	return ret.to_json();
}

void GDScriptLanguageProtocol::initialized(const Variant &p_params) {
	LSP::GodotCapabilities capabilities;

	DocTools *doc = EditorHelp::get_doc_data();
	for (const KeyValue<String, DocData::ClassDoc> &E : doc->class_list) {
		LSP::GodotNativeClassInfo gdclass;
		gdclass.name = E.value.name;
		gdclass.class_doc = &(E.value);
		if (ClassDB::ClassInfo *ptr = ClassDB::classes.getptr(StringName(E.value.name))) {
			gdclass.class_info = ptr;
		}
		capabilities.native_classes.push_back(gdclass);
	}

	notify_client("gdscript/capabilities", capabilities.to_json());
}

void GDScriptLanguageProtocol::poll(int p_limit_usec) {
	uint64_t target_ticks = OS::get_singleton()->get_ticks_usec() + p_limit_usec;

	if (server->is_connection_available()) {
		on_client_connected();
	}

	scene_cache.poll();

	HashMap<int, Ref<LSPeer>>::Iterator E = clients.begin();
	while (E != clients.end()) {
		Ref<LSPeer> peer = E->value;
		peer->connection->poll();
		StreamPeerTCP::Status status = peer->connection->get_status();
		if (status == StreamPeerTCP::STATUS_NONE || status == StreamPeerTCP::STATUS_ERROR) {
			on_client_disconnected(E->key);
			E = clients.begin();
			continue;
		} else {
			Error err = OK;
			while (peer->connection->get_available_bytes() > 0) {
				latest_client_id = E->key;
				err = peer->handle_data();
				if (err != OK || OS::get_singleton()->get_ticks_usec() >= target_ticks) {
					break;
				}
			}

			if (err != OK && err != ERR_BUSY) {
				on_client_disconnected(E->key);
				E = clients.begin();
				continue;
			}

			err = peer->send_data();
			if (err != OK && err != ERR_BUSY) {
				on_client_disconnected(E->key);
				E = clients.begin();
				continue;
			}
		}
		++E;
	}
}

Error GDScriptLanguageProtocol::start(int p_port, const IPAddress &p_bind_ip) {
	return server->listen(p_port, p_bind_ip);
}

void GDScriptLanguageProtocol::stop() {
	for (const KeyValue<int, Ref<LSPeer>> &E : clients) {
		Ref<LSPeer> peer = clients.get(E.key);
		peer->connection->disconnect_from_host();
	}

	scene_cache.clear();
	server->stop();
}

void GDScriptLanguageProtocol::notify_client(const String &p_method, const Variant &p_params, int p_client_id) {
#ifdef TESTS_ENABLED
	if (clients.is_empty()) {
		return;
	}
#endif
	if (p_client_id == -1) {
		ERR_FAIL_COND_MSG(latest_client_id == LSP_NO_CLIENT, "GDScript LSP: Can't notify client as none was connected.");
		p_client_id = latest_client_id;
	}
	ERR_FAIL_COND(!clients.has(p_client_id));
	Ref<LSPeer> peer = clients.get(p_client_id);
	ERR_FAIL_COND(peer.is_null());

	Dictionary message = make_notification(p_method, p_params);
	String msg = Variant(message).to_json_string();
	msg = format_output(msg);
	peer->res_queue.push_back(msg.utf8());
}

void GDScriptLanguageProtocol::request_client(const String &p_method, const Variant &p_params, int p_client_id) {
#ifdef TESTS_ENABLED
	if (clients.is_empty()) {
		return;
	}
#endif
	if (p_client_id == -1) {
		ERR_FAIL_COND_MSG(latest_client_id == LSP_NO_CLIENT, "GDScript LSP: Can't notify client as none was connected.");
		p_client_id = latest_client_id;
	}
	ERR_FAIL_COND(!clients.has(p_client_id));
	Ref<LSPeer> peer = clients.get(p_client_id);
	ERR_FAIL_COND(peer.is_null());

	Dictionary message = make_request(p_method, p_params, next_server_id);
	next_server_id++;
	String msg = Variant(message).to_json_string();
	msg = format_output(msg);
	peer->res_queue.push_back(msg.utf8());
}

bool GDScriptLanguageProtocol::is_smart_resolve_enabled() const {
	return bool(_EDITOR_GET("network/language_server/enable_smart_resolve"));
}

bool GDScriptLanguageProtocol::is_goto_native_symbols_enabled() const {
	return bool(_EDITOR_GET("network/language_server/show_native_symbols_in_editor"));
}

Ref<GDScriptAnalysisSession> GDScriptLanguageProtocol::get_analysis_session(int p_client_id) const {
	const int client_id = p_client_id == LSP_NO_CLIENT ? latest_client_id : p_client_id;
	const Ref<LSPeer> *peer = clients.getptr(client_id);
	return peer ? (*peer)->analysis_session : Ref<GDScriptAnalysisSession>();
}

ExtendGDScriptParser *GDScriptLanguageProtocol::get_parse_result(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path) {
	return p_session.is_valid() ? p_session->get_parse_result(p_path) : nullptr;
}

ExtendGDScriptParser *GDScriptLanguageProtocol::get_parse_result(const String &p_path) {
	return get_parse_result(get_analysis_session(), p_path);
}

Error GDScriptLanguageProtocol::lsp_did_open(const Ref<GDScriptAnalysisSession> &p_session, const Dictionary &p_params, int p_client_id) {
	ERR_FAIL_COND_V(p_session.is_null(), ERR_INVALID_PARAMETER);
	LSP::TextDocumentItem document;
	document.load(p_params["textDocument"]);
	const String path = get_workspace()->get_file_path(document.uri);

	Array diagnostics;
	const Error error = p_session->open_document(path, document.text, document.languageId, document.version);
	if (error == OK && document.languageId == LSP::LanguageId::GDSCRIPT) {
		diagnostics = p_session->get_diagnostics(path);
	}
	if (error != OK) {
		return error;
	}
	if (document.languageId == LSP::LanguageId::GDSCRIPT) {
		workspace->publish_diagnostics(path, diagnostics, p_client_id);
	}
	scene_cache.request_load(path);
	return OK;
}

void GDScriptLanguageProtocol::lsp_did_open(const Dictionary &p_params) {
	const Error error = lsp_did_open(get_analysis_session(), p_params, latest_client_id);
	ERR_FAIL_COND_MSG(error != OK, "LSP: Client is opening an already open or invalid document.");
}

Error GDScriptLanguageProtocol::lsp_did_change(const Ref<GDScriptAnalysisSession> &p_session, const Dictionary &p_params, int p_client_id) {
	ERR_FAIL_COND_V(p_session.is_null(), ERR_INVALID_PARAMETER);
	LSP::TextDocumentIdentifier identifier;
	identifier.load(p_params["textDocument"]);
	const String path = get_workspace()->get_file_path(identifier.uri);
	const GDScriptAnalysisSession::DocumentState *document = p_session->get_document(path);
	ERR_FAIL_NULL_V(document, ERR_DOES_NOT_EXIST);
	const LSP::LanguageId language_id = document->language_id;

	const Array content_changes = p_params["contentChanges"];
	if (content_changes.is_empty()) {
		return OK;
	}

	LSP::TextDocumentContentChangeEvent event;
	event.load(content_changes.back());
	const Dictionary text_document_params = p_params["textDocument"];
	const int64_t client_version = text_document_params.get("version", -1);
	Array diagnostics;
	const Error error = p_session->change_document(path, event.text, client_version);
	if (error == OK && language_id == LSP::LanguageId::GDSCRIPT) {
		diagnostics = p_session->get_diagnostics(path);
		workspace->publish_diagnostics(path, diagnostics, p_client_id);
	}
	return error;
}

void GDScriptLanguageProtocol::lsp_did_change(const Dictionary &p_params) {
	const Error error = lsp_did_change(get_analysis_session(), p_params, latest_client_id);
	ERR_FAIL_COND_MSG(error != OK, "LSP: Client is changing a document that is not open.");
}

Error GDScriptLanguageProtocol::lsp_did_close(const Ref<GDScriptAnalysisSession> &p_session, const Dictionary &p_params) {
	ERR_FAIL_COND_V(p_session.is_null(), ERR_INVALID_PARAMETER);
	LSP::TextDocumentIdentifier identifier;
	identifier.load(p_params["textDocument"]);
	const String path = get_workspace()->get_file_path(identifier.uri);
	const Error error = p_session->close_document(path);
	if (error == OK) {
		scene_cache.unload(path);
	}
	return error;
}

void GDScriptLanguageProtocol::lsp_did_close(const Dictionary &p_params) {
	const Error error = lsp_did_close(get_analysis_session(), p_params);
	ERR_FAIL_COND_MSG(error != OK, "LSP: Client is closing a document that is not open.");
}

Array GDScriptLanguageProtocol::lsp_completion(const Dictionary &p_params) {
	Array arr;
	LSP_CLIENT_V(arr);
	return lsp_completion(client->analysis_session, client->behavior, p_params);
}

Array GDScriptLanguageProtocol::lsp_completion(const Ref<GDScriptAnalysisSession> &p_session, const ClientBehavior &p_behavior, const Dictionary &p_params) {
	Array arr;
	ERR_FAIL_COND_V(p_session.is_null(), arr);

	LSP::CompletionParams params;
	params.load(p_params);
	Dictionary request_data = params.to_json();

	List<ScriptLanguage::CodeCompletionOption> options;
	get_workspace()->completion(p_session, params, &options);
	const String path = get_workspace()->get_file_path(params.textDocument.uri);
	const ExtendGDScriptParser *parser = get_parse_result(p_session, path);

	if (!options.is_empty()) {
		int i = 0;
		arr.resize(options.size());

		for (const ScriptLanguage::CodeCompletionOption &option : options) {
			LSP::CompletionItem item;
			item.label = option.display;
			item.data = request_data;
			item.insertText = option.insert_text;

			// LSP clients won't autoclose brackets.
			if (p_behavior.use_snippets_for_brace_completion) {
				// Use snippet insert mode to insert closing brace as well.
				if (item.insertText.ends_with("(")) {
					item.insertText += "$1)";
					item.insertTextFormat = LSP::InsertTextFormat::Snippet;
				}
			} else {
				// Trim braces.
				item.insertText = item.insertText.trim_suffix("(");
			}

			if (option.text_edit.is_set()) {
				item.textEdit.newText = option.text_edit.new_text;
				if (parser) {
					item.textEdit.range = parser->to_lsp_range(option.text_edit.start_line, option.text_edit.start_column, option.text_edit.end_line, option.text_edit.end_column);
				} else {
					GodotRange range(GodotPosition(option.text_edit.start_line, option.text_edit.start_column), GodotPosition(option.text_edit.end_line, option.text_edit.end_column));
					item.textEdit.range = range.to_lsp();
				}
			}

			switch (option.kind) {
				case ScriptLanguage::CODE_COMPLETION_KIND_ENUM:
					item.kind = LSP::CompletionItemKind::Enum;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_CLASS:
					item.kind = LSP::CompletionItemKind::Class;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_MEMBER:
					item.kind = LSP::CompletionItemKind::Property;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_FUNCTION:
					item.kind = LSP::CompletionItemKind::Method;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_SIGNAL:
					item.kind = LSP::CompletionItemKind::Event;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_CONSTANT:
					item.kind = LSP::CompletionItemKind::Constant;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_VARIABLE:
					item.kind = LSP::CompletionItemKind::Variable;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_FILE_PATH:
					item.kind = LSP::CompletionItemKind::File;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_NODE_PATH:
					item.kind = LSP::CompletionItemKind::Snippet;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_PLAIN_TEXT:
					item.kind = LSP::CompletionItemKind::Text;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_KEYWORD:
					item.kind = LSP::CompletionItemKind::Keyword;
					break;
				case ScriptLanguage::CODE_COMPLETION_KIND_MAX: {
				}
			}

			arr[i] = item.to_json();
			i++;
		}
	}
	return arr;
}

void GDScriptLanguageProtocol::resolve_related_symbols(const LSP::TextDocumentPositionParams &p_doc_pos, List<const LSP::DocumentSymbol *> &r_list) {
	resolve_related_symbols(get_analysis_session(), p_doc_pos, r_list);
}

void GDScriptLanguageProtocol::resolve_related_symbols(const Ref<GDScriptAnalysisSession> &p_session, const LSP::TextDocumentPositionParams &p_doc_pos, List<const LSP::DocumentSymbol *> &r_list) {
	ERR_FAIL_COND(p_session.is_null());

	String path = workspace->get_file_path(p_doc_pos.textDocument.uri);

	const ExtendGDScriptParser *parser = get_parse_result(p_session, path);
	if (!parser) {
		return;
	}

	String symbol_name;
	LSP::Range range;
	symbol_name = parser->get_symbol_name_under_position(p_doc_pos.position, range);

	for (const KeyValue<StringName, ClassMembers> &E : workspace->native_members) {
		if (const LSP::DocumentSymbol *const *symbol = E.value.getptr(symbol_name)) {
			r_list.push_back(*symbol);
		}
	}

	for (const KeyValue<String, ExtendGDScriptParser *> &E : p_session->get_cached_parsers()) {
		const ExtendGDScriptParser *scr = E.value;
		const ClassMembers &members = scr->get_members();
		if (const LSP::DocumentSymbol *const *symbol = members.getptr(symbol_name)) {
			r_list.push_back(*symbol);
		}

		for (const KeyValue<String, ClassMembers> &F : scr->get_inner_classes()) {
			const ClassMembers *inner_class = &F.value;
			if (const LSP::DocumentSymbol *const *symbol = inner_class->getptr(symbol_name)) {
				r_list.push_back(*symbol);
			}
		}
	}
}

// clang-format off
#define SET_DOCUMENT_METHOD(m_method) set_method(_STR(textDocument/m_method), callable_mp(text_document.ptr(), &GDScriptTextDocument::m_method))
#define SET_COMPLETION_METHOD(m_method) set_method(_STR(completionItem/m_method), callable_mp(text_document.ptr(), &GDScriptTextDocument::m_method))
#define SET_WORKSPACE_METHOD(m_method) set_method(_STR(workspace/m_method), callable_mp(workspace.ptr(), &GDScriptWorkspace::m_method))
// clang-format on

GDScriptLanguageProtocol::GDScriptLanguageProtocol() {
	server.instantiate();
	singleton = this;
	workspace.instantiate();
	text_document.instantiate();
	analysis_service.instantiate();
	analysis_service->configure(ProjectSettings::get_singleton()->get_resource_path(), workspace);

	SET_DOCUMENT_METHOD(didOpen);
	SET_DOCUMENT_METHOD(didClose);
	SET_DOCUMENT_METHOD(didChange);
	SET_DOCUMENT_METHOD(willSaveWaitUntil);
	SET_DOCUMENT_METHOD(didSave);

	SET_DOCUMENT_METHOD(documentSymbol);
	SET_DOCUMENT_METHOD(documentHighlight);
	SET_DOCUMENT_METHOD(completion);
	SET_DOCUMENT_METHOD(rename);
	SET_DOCUMENT_METHOD(prepareRename);
	SET_DOCUMENT_METHOD(references);
	SET_DOCUMENT_METHOD(foldingRange);
	SET_DOCUMENT_METHOD(codeLens);
	SET_DOCUMENT_METHOD(documentLink);
	SET_DOCUMENT_METHOD(colorPresentation);
	SET_DOCUMENT_METHOD(hover);
	SET_DOCUMENT_METHOD(definition);
	SET_DOCUMENT_METHOD(declaration);
	SET_DOCUMENT_METHOD(signatureHelp);

	SET_DOCUMENT_METHOD(nativeSymbol); // Custom method.

	SET_COMPLETION_METHOD(resolve);

	set_method("initialize", callable_mp(this, &GDScriptLanguageProtocol::initialize));
	set_method("initialized", callable_mp(this, &GDScriptLanguageProtocol::initialized));

	workspace->root = ProjectSettings::get_singleton()->get_resource_path();
}

GDScriptLanguageProtocol::~GDScriptLanguageProtocol() {
	stop();
	if (singleton == this) {
		singleton = nullptr;
	}
}

#undef SET_DOCUMENT_METHOD
#undef SET_COMPLETION_METHOD
#undef SET_WORKSPACE_METHOD

#undef LSP_CLIENT
#undef LSP_CLIENT_V
