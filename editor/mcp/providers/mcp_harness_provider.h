/**************************************************************************/
/*  mcp_harness_provider.h                                               */
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

#include "../mcp_editor_feature.h"
#include "mcp_harness_service.h"

#include "core/object/object.h"

class MCPHarnessProvider : public Object, public MCPEditorFeature {
public:
	MCPHarnessProvider() = default;
	~MCPHarnessProvider();

	Error register_tools(MCPToolRegistry *p_registry, String *r_error = nullptr) override;
	void unregister_tools() override;
	void set_trace_service(MCPTraceService *p_service, const Dictionary &p_project_metadata = Dictionary(), const String &p_root_directory_override = String()) { service.set_trace_service(p_service, p_project_metadata, p_root_directory_override); }

	Dictionary start(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary get_status(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary cancel(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary get_report(const Dictionary &p_arguments, const Dictionary &p_context);

	int poll() { return service.poll(); }
	void release_session(const String &p_session_id) { service.release_session(p_session_id); }
	void process() override { service.poll(); }
	void on_session_removed(const String &p_session_id) override { service.release_session(p_session_id); }
	void shutdown() override { service.shutdown(); }
	MCPHarnessService &get_service() { return service; }

private:
	MCPToolRegistry *tool_registry = nullptr;
	MCPHarnessService service;
};
