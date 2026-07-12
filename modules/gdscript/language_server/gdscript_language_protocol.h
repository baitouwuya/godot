/**************************************************************************/
/*  gdscript_language_protocol.h                                          */
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

#include "gdscript_analysis_service.h"
#include "gdscript_language_transport.h"
#include "gdscript_text_document.h"
#include "gdscript_workspace.h"

#include "modules/jsonrpc/jsonrpc.h"

#define LSP_NO_CLIENT -1

class GDScriptLanguageProtocol : public JSONRPC {
	GDCLASS(GDScriptLanguageProtocol, JSONRPC)

	friend class TestGDScriptLanguageProtocolInitializer;

public:
	struct ClientBehavior {
		/** If `true` use snippet insert mode to position the cursor between braces of completion options. If `false` strip braces from completion options since we can't provide good UX for them. */
		bool use_snippets_for_brace_completion = false;
	};

private:
	struct LSPeer : RefCounted {
		/**
		 * Represents how the server should behave towards this client in certain situations.
		 * This gets derived from client capabilities so the configured behavior is guaranteed to be supported by the client.
		 */
		ClientBehavior behavior;
		uint64_t analysis_session_id = 0;
		Ref<GDScriptAnalysisSession> analysis_session;
	};

	enum LSPErrorCode {
		RequestCancelled = -32800,
		ContentModified = -32801,
	};

	static GDScriptLanguageProtocol *singleton;

	HashMap<int, Ref<LSPeer>> clients;
	GDScriptLanguageTransport transport;
	int active_client_id = LSP_NO_CLIENT;
	int next_client_id = 0;

	int next_server_id = 0;

	Ref<GDScriptTextDocument> text_document;
	Ref<GDScriptWorkspace> workspace;
	Ref<GDScriptAnalysisService> analysis_service;

	Error _add_client(int p_client_id);
	Error on_client_connected();
	bool _remove_client(int p_client_id);
	void on_client_disconnected(const int &p_client_id);
	void _clear_clients();
	void _process_transport_event(const GDScriptLanguageTransport::Event &p_event);
	bool _queue_output(int p_client_id, const String &p_output);

	String process_message(const String &p_text);
	String format_output(const String &p_text);

	bool _initialized = false;

protected:
	static void _bind_methods();

	Variant initialize(const Dictionary &p_params);
	void initialized(const Variant &p_params);

public:
	_FORCE_INLINE_ static GDScriptLanguageProtocol *get_singleton() { return singleton; }
	_FORCE_INLINE_ Ref<GDScriptWorkspace> get_workspace() { return workspace; }
	_FORCE_INLINE_ Ref<GDScriptTextDocument> get_text_document() { return text_document; }
	_FORCE_INLINE_ Ref<GDScriptAnalysisService> get_analysis_service() { return analysis_service; }
	_FORCE_INLINE_ SceneCache *get_scene_cache() { return analysis_service->get_scene_cache(); }

	_FORCE_INLINE_ bool is_initialized() const { return _initialized; }

	void poll(int p_limit_usec);
	void poll_network(int p_limit_usec);
	void poll_main_thread(int p_limit_usec);
	Error start(int p_port, const IPAddress &p_bind_ip);
	void begin_shutdown();
	void stop();

	void notify_client(const String &p_method, const Variant &p_params = Variant(), int p_client_id = -1);
	void request_client(const String &p_method, const Variant &p_params = Variant(), int p_client_id = -1);

	bool is_smart_resolve_enabled() const;
	bool is_goto_native_symbols_enabled() const;
	Ref<GDScriptAnalysisSession> get_analysis_session(int p_client_id = LSP_NO_CLIENT) const;

	// Text Document Synchronization
	void lsp_did_open(const Dictionary &p_params);
	void lsp_did_change(const Dictionary &p_params);
	void lsp_did_close(const Dictionary &p_params);
	Error lsp_did_open(const Ref<GDScriptAnalysisSession> &p_session, const Dictionary &p_params, int p_client_id = LSP_NO_CLIENT);
	Error lsp_did_change(const Ref<GDScriptAnalysisSession> &p_session, const Dictionary &p_params, int p_client_id = LSP_NO_CLIENT);
	Error lsp_did_close(const Ref<GDScriptAnalysisSession> &p_session, const Dictionary &p_params);

	// Completion
	Array lsp_completion(const Dictionary &p_params);
	Array lsp_completion(const Ref<GDScriptAnalysisSession> &p_session, const ClientBehavior &p_behavior, const Dictionary &p_params);

	/**
	 * Returns a list of symbols that might be related to the document position.
	 *
	 * The result fulfills no semantic guarantees, nor is it guaranteed to be complete.
	 * Should only be used for "smart resolve".
	 */
	void resolve_related_symbols(const LSP::TextDocumentPositionParams &p_doc_pos, List<const LSP::DocumentSymbol *> &r_list);
	void resolve_related_symbols(const Ref<GDScriptAnalysisSession> &p_session, const LSP::TextDocumentPositionParams &p_doc_pos, List<const LSP::DocumentSymbol *> &r_list);

	/**
	 * Returns parse results for the given path, using the cache if available.
	 * If no such file exists, or the file is not a GDScript file a `nullptr` is returned.
	 */
	ExtendGDScriptParser *get_parse_result(const String &p_path);
	ExtendGDScriptParser *get_parse_result(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path);

	GDScriptLanguageProtocol();
	~GDScriptLanguageProtocol();
};
