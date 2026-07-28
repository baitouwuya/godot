/**************************************************************************/
/*  mcp_editor_feature.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/local_vector.h"

class MCPToolRegistry;

class MCPEditorFeature {
public:
	virtual ~MCPEditorFeature() = default;

	virtual Error register_tools(MCPToolRegistry *p_registry, String *r_error = nullptr) { return OK; }
	virtual void unregister_tools() {}
	virtual void process() {}
	virtual void on_session_removed(const String &p_session_id) {}
	virtual void shutdown() {}
};

// Non-owning lifecycle composition; the caller controls feature allocation.
class MCPEditorFeatureSet {
	LocalVector<MCPEditorFeature *> features;
	int registered_count = 0;
	bool registration_active = false;
	bool shutdown_pending = false;

public:
	MCPEditorFeatureSet() = default;
	MCPEditorFeatureSet(const MCPEditorFeatureSet &) = delete;
	MCPEditorFeatureSet &operator=(const MCPEditorFeatureSet &) = delete;

	Error add_feature(MCPEditorFeature *p_feature);
	Error register_tools(MCPToolRegistry *p_registry, String *r_error = nullptr);
	void unregister_tools();
	void process();
	void on_session_removed(const String &p_session_id);
	void shutdown();
	void clear();

	int get_feature_count() const { return features.size(); }
	int get_registered_count() const { return registered_count; }
};
