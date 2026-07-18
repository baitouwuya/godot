/**************************************************************************/
/*  mcp_script_analysis_sync.h                                            */
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

#include "mcp_gdscript_session_manager.h"

class MCPScriptAnalysisSync {
public:
	static Error sync_snapshot(const Ref<MCPGDScriptSessionManager> &p_session_manager, const String &p_session_id, const Dictionary &p_snapshot, Array *r_diagnostics = nullptr, String *r_error = nullptr);
	static Error sync_authoritative_path(const Ref<MCPGDScriptSessionManager> &p_session_manager, const String &p_session_id, const String &p_path, const String &p_project_root, Dictionary &r_snapshot, Array *r_diagnostics = nullptr, String *r_error = nullptr);
	static Error sync_open_buffers(const Ref<MCPGDScriptSessionManager> &p_session_manager, const String &p_session_id, String *r_error = nullptr);
	static Error sync_saved_snapshot(const Ref<MCPGDScriptSessionManager> &p_session_manager, const String &p_session_id, Dictionary &r_snapshot, String *r_error = nullptr);
	static Dictionary make_failure(const Dictionary &p_snapshot, bool p_applied, bool p_changed, bool p_created, const String &p_message);
};
