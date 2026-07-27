/**************************************************************************/
/*  mcp_gdscript_provider.h                                               */
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

#pragma once

#include "../mcp_editor_feature.h"
#include "mcp_gdscript_session_manager.h"

#include "core/object/object.h"
#include "core/object/script_language.h"
#include "core/variant/dictionary.h"

#include "modules/gdscript/language_server/gdscript_workspace.h"

class MCPToolRegistry;

class MCPGDScriptProvider : public Object, public MCPEditorFeature {
public:
	MCPGDScriptProvider(const Ref<GDScriptAnalysisService> &p_service = Ref<GDScriptAnalysisService>(), const Ref<GDScriptAnalysisSession> &p_fallback_session = Ref<GDScriptAnalysisSession>());
	explicit MCPGDScriptProvider(const Ref<MCPGDScriptSessionManager> &p_session_manager);
	~MCPGDScriptProvider();

	Error register_tools(MCPToolRegistry *p_registry, String *r_error = nullptr) override;
	void unregister_tools() override;
	void on_session_removed(const String &p_session_id) override;
	void shutdown() override;

	void set_analysis_service(const Ref<GDScriptAnalysisService> &p_service);
	void set_analysis_session(const Ref<GDScriptAnalysisSession> &p_session);
	void set_session_manager(const Ref<MCPGDScriptSessionManager> &p_session_manager);
	Ref<GDScriptAnalysisService> get_analysis_service() const { return session_manager.is_valid() ? session_manager->get_analysis_service() : Ref<GDScriptAnalysisService>(); }
	Ref<GDScriptAnalysisSession> get_analysis_session() const { return session_manager.is_valid() ? session_manager->get_fallback_session() : Ref<GDScriptAnalysisSession>(); }
	MCPGDScriptSessionManager *get_session_manager() { return session_manager.ptr(); }
	const MCPGDScriptSessionManager *get_session_manager() const { return session_manager.ptr(); }
	const Ref<MCPGDScriptSessionManager> &get_session_manager_ref() const { return session_manager; }
	bool release_session(const String &p_session_id) { return session_manager.is_valid() && session_manager->release_session(p_session_id); }
	void clear_sessions() {
		if (session_manager.is_valid()) {
			session_manager->clear();
		}
	}

	Dictionary diagnostics(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary symbols(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary completion(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary hover(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary definition(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary declaration(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary references(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary signature_help(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary rename(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary apply_workspace_edit(const Dictionary &p_arguments, const Dictionary &p_context);

private:
	MCPToolRegistry *tool_registry = nullptr;
	Ref<MCPGDScriptSessionManager> session_manager;

	Ref<GDScriptWorkspace> _get_workspace() const;
	String _get_project_root() const;
	bool _is_ready(String &r_error) const;
	bool _validate_ready_arguments(const Dictionary &p_arguments, const PackedStringArray &p_allowed, String &r_error, Dictionary &r_error_result) const;
	bool _resolve_session(const Dictionary &p_context, String &r_session_id, Ref<GDScriptAnalysisSession> &r_session, String &r_error);
	bool _prepare_document(const Dictionary &p_context, const String &p_path, bool p_sync_open_buffers, Ref<GDScriptAnalysisSession> &r_session, Array *r_diagnostics, Dictionary &r_error_result);
	bool _parse_path(const Dictionary &p_arguments, String &r_path, String &r_error) const;
	bool _parse_position(const Dictionary &p_arguments, String &r_path, LSP::TextDocumentPositionParams &r_params, String &r_error) const;
	bool _parse_reference_position(const Dictionary &p_arguments, String &r_path, LSP::ReferenceParams &r_params, String &r_error) const;

	Dictionary _metadata(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path) const;
	Dictionary _invalid_arguments(const String &p_message) const;
	Dictionary _unavailable(const String &p_message) const;
	Dictionary _script_not_found(const String &p_path) const;
	Dictionary _locations_result(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path, const Vector<LSP::Location> &p_locations, const LSP::DocumentSymbol *p_symbol = nullptr) const;

	Array _completion_items(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path, const List<ScriptLanguage::CodeCompletionOption> &p_options, int p_limit) const;
	Dictionary _symbol_result(const Ref<GDScriptAnalysisSession> &p_session, const String &p_path, const LSP::TextDocumentPositionParams &p_params, bool p_declaration) const;
};
