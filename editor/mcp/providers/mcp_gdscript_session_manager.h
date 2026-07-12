/**************************************************************************/
/*  mcp_gdscript_session_manager.h                                        */
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

#pragma once

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"

#include "modules/gdscript/language_server/gdscript_analysis_service.h"

class MCPGDScriptSessionManager : public RefCounted {
	GDSOFTCLASS(MCPGDScriptSessionManager, RefCounted);

	Ref<GDScriptAnalysisService> analysis_service;
	Ref<GDScriptAnalysisSession> fallback_session;
	HashMap<String, Ref<GDScriptAnalysisSession>> sessions;

public:
	MCPGDScriptSessionManager(const Ref<GDScriptAnalysisService> &p_service = Ref<GDScriptAnalysisService>(), const Ref<GDScriptAnalysisSession> &p_fallback_session = Ref<GDScriptAnalysisSession>());
	~MCPGDScriptSessionManager();

	void set_analysis_service(const Ref<GDScriptAnalysisService> &p_service);
	const Ref<GDScriptAnalysisService> &get_analysis_service() const { return analysis_service; }

	void set_fallback_session(const Ref<GDScriptAnalysisSession> &p_session) { fallback_session = p_session; }
	const Ref<GDScriptAnalysisSession> &get_fallback_session() const { return fallback_session; }

	Ref<GDScriptAnalysisSession> get_or_create_session(const String &p_session_id);
	Ref<GDScriptAnalysisSession> get_session(const String &p_session_id) const;
	Error resolve_context(const Dictionary &p_context, String &r_session_id, Ref<GDScriptAnalysisSession> &r_session, String *r_error = nullptr);
	Error sync_document(const String &p_session_id, const String &p_path, const String &p_text, int64_t p_client_version, Array &r_diagnostics, String *r_error = nullptr);
	bool release_session(const String &p_session_id);
	void clear();
	int get_session_count() const { return sessions.size(); }
};
