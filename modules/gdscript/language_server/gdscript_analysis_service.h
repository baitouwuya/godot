/**************************************************************************/
/*  gdscript_analysis_service.h                                           */
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

#include "gdscript_analysis_session.h"
#include "scene_cache.h"

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"

class GDScriptWorkspace;

class GDScriptAnalysisService : public RefCounted {
	String project_root;
	Ref<GDScriptWorkspace> workspace;
	HashMap<uint64_t, Ref<GDScriptAnalysisSession>> sessions;
	SceneCache scene_cache;
	uint64_t next_session_id = 1;

public:
	void configure(const String &p_project_root, const Ref<GDScriptWorkspace> &p_workspace);
	const String &get_project_root() const { return project_root; }
	const Ref<GDScriptWorkspace> &get_workspace() const { return workspace; }
	SceneCache *get_scene_cache() { return &scene_cache; }

	void poll_scene_cache();
	void clear_scene_cache();
	void request_scene_load(const String &p_script_path);
	void unload_scene(const String &p_script_path);

	Ref<GDScriptAnalysisSession> create_session();
	Ref<GDScriptAnalysisSession> get_session(uint64_t p_session_id) const;
	bool remove_session(uint64_t p_session_id);
	void clear_sessions();

	Error open_document(uint64_t p_session_id, const String &p_path, const String &p_text, LSP::LanguageId p_language_id, int64_t p_client_version, Array &r_diagnostics);
	Error change_document(uint64_t p_session_id, const String &p_path, const String &p_text, int64_t p_client_version, Array &r_diagnostics);
	Error close_document(uint64_t p_session_id, const String &p_path);
	ExtendGDScriptParser *get_parse_result(uint64_t p_session_id, const String &p_path);
	Array get_diagnostics(uint64_t p_session_id, const String &p_path, Error *r_error = nullptr);
};
