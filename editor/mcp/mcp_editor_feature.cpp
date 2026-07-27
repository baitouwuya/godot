/**************************************************************************/
/*  mcp_editor_feature.cpp                                                */
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

#include "mcp_editor_feature.h"

#include "core/mcp/mcp_tool_registry.h"

Error MCPEditorFeatureSet::add_feature(MCPEditorFeature *p_feature) {
	ERR_FAIL_NULL_V(p_feature, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(registered_count != 0, ERR_ALREADY_IN_USE);
	ERR_FAIL_COND_V(features.has(p_feature), ERR_ALREADY_EXISTS);
	features.push_back(p_feature);
	return OK;
}

Error MCPEditorFeatureSet::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	ERR_FAIL_NULL_V(p_registry, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(registered_count != 0, ERR_ALREADY_IN_USE);

	for (MCPEditorFeature *feature : features) {
		const Error error = feature->register_tools(p_registry, r_error);
		if (error != OK) {
			feature->unregister_tools();
			unregister_tools();
			return error;
		}
		registered_count++;
	}
	return OK;
}

void MCPEditorFeatureSet::unregister_tools() {
	while (registered_count > 0) {
		features[--registered_count]->unregister_tools();
	}
}

void MCPEditorFeatureSet::process() {
	for (int i = 0; i < registered_count; i++) {
		features[i]->process();
	}
}

void MCPEditorFeatureSet::on_session_removed(const String &p_session_id) {
	for (int i = registered_count - 1; i >= 0; i--) {
		features[i]->on_session_removed(p_session_id);
	}
}

void MCPEditorFeatureSet::shutdown() {
	for (int i = int(features.size()) - 1; i >= 0; i--) {
		features[i]->shutdown();
	}
}
