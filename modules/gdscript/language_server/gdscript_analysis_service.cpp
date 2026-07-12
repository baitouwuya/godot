/**************************************************************************/
/*  gdscript_analysis_service.cpp                                         */
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

#include "gdscript_analysis_service.h"

#include "gdscript_workspace.h"

#include "core/os/thread.h"
#include "editor/file_system/editor_file_system.h"

void GDScriptAnalysisService::configure(const String &p_project_root, const Ref<GDScriptWorkspace> &p_workspace) {
	project_root = p_project_root.simplify_path();
	workspace = p_workspace;
}

void GDScriptAnalysisService::poll_scene_cache() {
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "GDScript scene cache polling must run on the main thread.");
	if (EditorFileSystem::get_singleton()) {
		scene_cache.poll();
	}
}

void GDScriptAnalysisService::clear_scene_cache() {
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "GDScript scene cache clearing must run on the main thread.");
	scene_cache.clear();
}

void GDScriptAnalysisService::request_scene_load(const String &p_script_path) {
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "GDScript scene cache requests must run on the main thread.");
	scene_cache.request_load(p_script_path);
}

void GDScriptAnalysisService::unload_scene(const String &p_script_path) {
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "GDScript scene cache unloading must run on the main thread.");
	scene_cache.unload(p_script_path);
}

Ref<GDScriptAnalysisSession> GDScriptAnalysisService::create_session() {
	const uint64_t session_id = next_session_id++;
	Ref<GDScriptAnalysisSession> session = memnew(GDScriptAnalysisSession(session_id, workspace));
	sessions.insert(session_id, session);
	return session;
}

Ref<GDScriptAnalysisSession> GDScriptAnalysisService::get_session(uint64_t p_session_id) const {
	const Ref<GDScriptAnalysisSession> *session = sessions.getptr(p_session_id);
	return session ? *session : Ref<GDScriptAnalysisSession>();
}

bool GDScriptAnalysisService::remove_session(uint64_t p_session_id) {
	return sessions.erase(p_session_id);
}

void GDScriptAnalysisService::clear_sessions() {
	sessions.clear();
}

Error GDScriptAnalysisService::open_document(uint64_t p_session_id, const String &p_path, const String &p_text, LSP::LanguageId p_language_id, int64_t p_client_version, Array &r_diagnostics) {
	r_diagnostics.clear();
	Ref<GDScriptAnalysisSession> session = get_session(p_session_id);
	ERR_FAIL_COND_V(session.is_null(), ERR_INVALID_PARAMETER);
	const Error error = session->open_document(p_path, p_text, p_language_id, p_client_version);
	if (error == OK && p_language_id == LSP::LanguageId::GDSCRIPT) {
		r_diagnostics = session->get_diagnostics(p_path);
	}
	return error;
}

Error GDScriptAnalysisService::change_document(uint64_t p_session_id, const String &p_path, const String &p_text, int64_t p_client_version, Array &r_diagnostics) {
	r_diagnostics.clear();
	Ref<GDScriptAnalysisSession> session = get_session(p_session_id);
	ERR_FAIL_COND_V(session.is_null(), ERR_INVALID_PARAMETER);
	const Error error = session->change_document(p_path, p_text, p_client_version);
	const GDScriptAnalysisSession::DocumentState *document = session->get_document(p_path);
	if (error == OK && document && document->language_id == LSP::LanguageId::GDSCRIPT) {
		r_diagnostics = session->get_diagnostics(p_path);
	}
	return error;
}

Error GDScriptAnalysisService::close_document(uint64_t p_session_id, const String &p_path) {
	Ref<GDScriptAnalysisSession> session = get_session(p_session_id);
	ERR_FAIL_COND_V(session.is_null(), ERR_INVALID_PARAMETER);
	return session->close_document(p_path);
}

ExtendGDScriptParser *GDScriptAnalysisService::get_parse_result(uint64_t p_session_id, const String &p_path) {
	Ref<GDScriptAnalysisSession> session = get_session(p_session_id);
	return session.is_valid() ? session->get_parse_result(p_path) : nullptr;
}

Array GDScriptAnalysisService::get_diagnostics(uint64_t p_session_id, const String &p_path, Error *r_error) {
	Ref<GDScriptAnalysisSession> session = get_session(p_session_id);
	if (session.is_null()) {
		if (r_error) {
			*r_error = ERR_INVALID_PARAMETER;
		}
		return Array();
	}
	return session->get_diagnostics(p_path, r_error);
}
