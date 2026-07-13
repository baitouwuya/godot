/**************************************************************************/
/*  mcp_gdscript_session_manager.cpp                                      */
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

#include "mcp_gdscript_session_manager.h"

static Error _session_manager_fail(const String &p_message, String *r_error, Error p_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

MCPGDScriptSessionManager::MCPGDScriptSessionManager(const Ref<GDScriptAnalysisService> &p_service, const Ref<GDScriptAnalysisSession> &p_fallback_session) {
	analysis_service = p_service;
	fallback_session = p_fallback_session;
}

MCPGDScriptSessionManager::~MCPGDScriptSessionManager() {
	clear();
}

void MCPGDScriptSessionManager::set_analysis_service(const Ref<GDScriptAnalysisService> &p_service) {
	if (analysis_service == p_service) {
		return;
	}
	clear();
	analysis_service = p_service;
}

Ref<GDScriptAnalysisSession> MCPGDScriptSessionManager::get_or_create_session(const String &p_session_id) {
	if (p_session_id.is_empty() || analysis_service.is_null()) {
		return Ref<GDScriptAnalysisSession>();
	}
	if (const Ref<GDScriptAnalysisSession> *session = sessions.getptr(p_session_id)) {
		return *session;
	}

	Ref<GDScriptAnalysisSession> session = analysis_service->create_session();
	if (session.is_valid()) {
		sessions.insert(p_session_id, session);
	}
	return session;
}

Ref<GDScriptAnalysisSession> MCPGDScriptSessionManager::get_session(const String &p_session_id) const {
	const Ref<GDScriptAnalysisSession> *session = sessions.getptr(p_session_id);
	return session ? *session : Ref<GDScriptAnalysisSession>();
}

Error MCPGDScriptSessionManager::resolve_context(const Dictionary &p_context, String &r_session_id, Ref<GDScriptAnalysisSession> &r_session, String *r_error) {
	r_session_id = String();
	r_session.unref();
	if (r_error) {
		*r_error = String();
	}

	const Variant session_value = p_context.get("session", Variant());
	if (session_value.get_type() == Variant::DICTIONARY) {
		const Dictionary session_context = session_value;
		const Variant session_id_value = session_context.get("sessionId", Variant());
		if (session_id_value.get_type() == Variant::STRING || session_id_value.get_type() == Variant::STRING_NAME) {
			r_session_id = session_id_value;
			if (r_session_id.is_empty()) {
				return _session_manager_fail("MCP session.sessionId must not be empty.", r_error, ERR_INVALID_PARAMETER);
			}
			r_session = get_or_create_session(r_session_id);
			return r_session.is_valid() ? OK : _session_manager_fail("The MCP GDScript analysis session could not be created.", r_error, ERR_UNCONFIGURED);
		}
		if (!session_context.is_empty()) {
			return _session_manager_fail("MCP session.sessionId must be a non-empty string.", r_error, ERR_INVALID_PARAMETER);
		}
	} else if (session_value.get_type() != Variant::NIL) {
		return _session_manager_fail("MCP tool context session must be an object.", r_error, ERR_INVALID_PARAMETER);
	}

	r_session = fallback_session;
	return r_session.is_valid() ? OK : _session_manager_fail("MCP session.sessionId is required when no fallback analysis session is injected.", r_error, ERR_UNCONFIGURED);
}

Error MCPGDScriptSessionManager::sync_document(const String &p_session_id, const String &p_path, const String &p_text, int64_t p_client_version, Array &r_diagnostics, String *r_error) {
	r_diagnostics.clear();
	if (r_error) {
		*r_error = String();
	}
	if (p_path.is_empty()) {
		return _session_manager_fail("The GDScript analysis path must not be empty.", r_error, ERR_INVALID_PARAMETER);
	}
	if (p_client_version < 0) {
		return _session_manager_fail("The GDScript analysis client version must be non-negative.", r_error, ERR_PARAMETER_RANGE_ERROR);
	}

	Ref<GDScriptAnalysisSession> session = p_session_id.is_empty() ? fallback_session : get_or_create_session(p_session_id);
	if (session.is_null()) {
		return _session_manager_fail("The MCP GDScript analysis session is unavailable.", r_error, ERR_UNCONFIGURED);
	}

	const GDScriptAnalysisSession::DocumentState *document = session->get_document(p_path);
	Error sync_error = OK;
	if (!document || document->source_state == GDScriptAnalysisSession::SOURCE_STATE_DISK) {
		sync_error = session->open_document(p_path, p_text, LSP::LanguageId::GDSCRIPT, p_client_version);
	} else if (document->text != p_text || document->sha256 != p_text.sha256_text() || document->client_version != p_client_version) {
		sync_error = session->change_document(p_path, p_text, p_client_version);
	}
	if (sync_error != OK) {
		return _session_manager_fail("The authoritative Script buffer could not be synchronized with GDScript analysis.", r_error, sync_error);
	}

	Error diagnostics_error = OK;
	r_diagnostics = session->get_diagnostics(p_path, &diagnostics_error);
	if (diagnostics_error != OK) {
		return _session_manager_fail("GDScript diagnostics could not be read after synchronizing the Script buffer.", r_error, diagnostics_error);
	}
	return OK;
}

Error MCPGDScriptSessionManager::reconcile_open_editor_documents(const String &p_session_id, const HashSet<String> &p_open_paths, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	Ref<GDScriptAnalysisSession> session = p_session_id.is_empty() ? fallback_session : get_session(p_session_id);
	if (session.is_null()) {
		return _session_manager_fail("The MCP GDScript analysis session is unavailable.", r_error, ERR_UNCONFIGURED);
	}

	Vector<String> closed_paths;
	for (const KeyValue<String, GDScriptAnalysisSession::DocumentState> &entry : session->get_documents()) {
		if (entry.value.source_state == GDScriptAnalysisSession::SOURCE_STATE_OPEN_DOCUMENT && !p_open_paths.has(entry.key)) {
			closed_paths.push_back(entry.key);
		}
	}
	for (const String &path : closed_paths) {
		const Error close_error = session->close_document(path);
		if (close_error != OK && close_error != ERR_DOES_NOT_EXIST) {
			return _session_manager_fail("A closed ScriptEditor document could not be released from MCP analysis.", r_error, close_error);
		}
	}
	return OK;
}

bool MCPGDScriptSessionManager::release_session(const String &p_session_id) {
	const Ref<GDScriptAnalysisSession> *session = sessions.getptr(p_session_id);
	if (!session) {
		return false;
	}
	if (analysis_service.is_valid() && (*session).is_valid()) {
		analysis_service->remove_session((*session)->get_session_id());
	}
	sessions.erase(p_session_id);
	return true;
}

void MCPGDScriptSessionManager::clear() {
	while (!sessions.is_empty()) {
		release_session(sessions.begin()->key);
	}
}
