/**************************************************************************/
/*  mcp_builtin_features.h                                                */
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

#include "mcp_editor_feature.h"
#include "providers/mcp_runtime_input_debugger_plugin.h"
#include "providers/mcp_runtime_observation_debugger_plugin.h"

class MCPAutoloadProvider;
class MCPAutomationProvider;
class MCPClassProvider;
class MCPDebugCapture;
class MCPDebugEventStore;
class MCPDebugProvider;
class MCPEditorProvider;
class MCPEditorUIProvider;
class MCPFileProvider;
class MCPGDScriptProvider;
class MCPGDScriptSessionManager;
class MCPHarnessProvider;
class MCPInputMapProvider;
class MCPNodeProvider;
class MCPNodeStructureProvider;
class MCPProjectProvider;
class MCPResourceProvider;
class MCPRuntimeDebugService;
class MCPRuntimeProvider;
class MCPSceneProvider;
class MCPScriptProvider;
class MCPToolRegistry;
class MCPTraceService;

// Owns and wires the built-in MCP providers and their shared services.
class MCPBuiltinFeatures {
	MCPEditorFeatureSet feature_set;
	MCPAutoloadProvider *autoload_provider = nullptr;
	MCPAutomationProvider *automation_provider = nullptr;
	MCPClassProvider *class_provider = nullptr;
	MCPDebugEventStore *debug_event_store = nullptr;
	MCPDebugCapture *debug_capture = nullptr;
	MCPDebugProvider *debug_provider = nullptr;
	MCPRuntimeDebugService *runtime_debug_service = nullptr;
	MCPRuntimeProvider *runtime_provider = nullptr;
	Ref<MCPRuntimeInputDebuggerPlugin> runtime_input_debugger_plugin;
	Ref<MCPRuntimeObservationDebuggerPlugin> runtime_observation_debugger_plugin;
	MCPEditorProvider *editor_provider = nullptr;
	MCPEditorUIProvider *editor_ui_provider = nullptr;
	MCPFileProvider *file_provider = nullptr;
	MCPSceneProvider *scene_provider = nullptr;
	MCPNodeProvider *node_provider = nullptr;
	MCPNodeStructureProvider *node_structure_provider = nullptr;
	MCPProjectProvider *project_provider = nullptr;
	MCPScriptProvider *script_provider = nullptr;
	MCPResourceProvider *resource_provider = nullptr;
	MCPGDScriptProvider *gdscript_provider = nullptr;
	Ref<MCPGDScriptSessionManager> gdscript_session_manager;
	MCPHarnessProvider *harness_provider = nullptr;
	MCPInputMapProvider *input_map_provider = nullptr;
	MCPTraceService *trace_service = nullptr;
	bool initialized = false;

public:
	Error initialize();
	Error prepare_for_registration(String &r_error);
	Error register_tools(MCPToolRegistry *p_registry, String *r_error = nullptr);
	void unregister_tools();
	void process();
	void on_session_removed(const String &p_session_id);
	void shutdown();

	void configure_host(const Dictionary &p_project_metadata);
	void clear_debug_events();
	Error start_debug_capture();
	void stop_debug_capture();
	void clear_runtime_requests();
	Ref<MCPRuntimeInputDebuggerPlugin> get_runtime_input_debugger_plugin() const { return runtime_input_debugger_plugin; }
	Ref<MCPRuntimeObservationDebuggerPlugin> get_runtime_observation_debugger_plugin() const { return runtime_observation_debugger_plugin; }

	~MCPBuiltinFeatures();
};
