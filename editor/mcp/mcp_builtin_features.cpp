/**************************************************************************/
/*  mcp_builtin_features.cpp                                              */
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

#include "mcp_builtin_features.h"

#include "providers/mcp_autoload_provider.h"
#include "providers/mcp_automation_provider.h"
#include "providers/mcp_class_provider.h"
#include "providers/mcp_debug_capture.h"
#include "providers/mcp_debug_event_store.h"
#include "providers/mcp_debug_provider.h"
#include "providers/mcp_editor_provider.h"
#include "providers/mcp_editor_ui_provider.h"
#include "providers/mcp_file_provider.h"
#include "providers/mcp_harness_provider.h"
#include "providers/mcp_input_map_provider.h"
#include "providers/mcp_node_provider.h"
#include "providers/mcp_node_structure_provider.h"
#include "providers/mcp_project_provider.h"
#include "providers/mcp_resource_provider.h"
#include "providers/mcp_runtime_debug_service.h"
#include "providers/mcp_runtime_provider.h"
#include "providers/mcp_scene_provider.h"
#include "providers/mcp_trace_service.h"

#include "modules/modules_enabled.gen.h"

#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
#include "providers/mcp_gdscript_provider.h"
#include "providers/mcp_gdscript_session_manager.h"
#include "providers/mcp_script_provider.h"

#include "modules/gdscript/language_server/gdscript_language_protocol.h"
#endif

Error MCPBuiltinFeatures::initialize() {
	ERR_FAIL_COND_V(initialized, ERR_ALREADY_IN_USE);

	automation_provider = memnew(MCPAutomationProvider);
	class_provider = memnew(MCPClassProvider);
	debug_event_store = memnew(MCPDebugEventStore);
	debug_capture = memnew(MCPDebugCapture(debug_event_store));
	debug_provider = memnew(MCPDebugProvider(debug_event_store));
	runtime_debug_service = memnew(MCPRuntimeDebugService(debug_capture));
	runtime_input_debugger_plugin.instantiate();
	runtime_input_debugger_plugin->set_scheduler(runtime_debug_service->get_input_scheduler());
	runtime_observation_debugger_plugin.instantiate();
	runtime_debug_service->set_observation_plugin(runtime_observation_debugger_plugin.ptr());
	runtime_provider = memnew(MCPRuntimeProvider(runtime_debug_service));
	trace_service = memnew(MCPTraceService);
	harness_provider = memnew(MCPHarnessProvider);
	editor_provider = memnew(MCPEditorProvider);
	editor_ui_provider = memnew(MCPEditorUIProvider);
	file_provider = memnew(MCPFileProvider);
	scene_provider = memnew(MCPSceneProvider);
	node_provider = memnew(MCPNodeProvider);
	node_structure_provider = memnew(MCPNodeStructureProvider);
	project_provider = memnew(MCPProjectProvider);
	autoload_provider = memnew(MCPAutoloadProvider);
	input_map_provider = memnew(MCPInputMapProvider);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	gdscript_session_manager.instantiate();
	script_provider = memnew(MCPScriptProvider(gdscript_session_manager));
#endif
	resource_provider = memnew(MCPResourceProvider);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	gdscript_provider = memnew(MCPGDScriptProvider(gdscript_session_manager));
#endif

	ERR_FAIL_COND_V(feature_set.add_feature(automation_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(class_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(debug_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(runtime_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(trace_service) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(harness_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(editor_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(editor_ui_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(file_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(scene_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(node_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(node_structure_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(project_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(autoload_provider) != OK, ERR_CANT_CREATE);
	ERR_FAIL_COND_V(feature_set.add_feature(input_map_provider) != OK, ERR_CANT_CREATE);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	ERR_FAIL_COND_V(feature_set.add_feature(script_provider) != OK, ERR_CANT_CREATE);
#endif
	ERR_FAIL_COND_V(feature_set.add_feature(resource_provider) != OK, ERR_CANT_CREATE);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	ERR_FAIL_COND_V(feature_set.add_feature(gdscript_provider) != OK, ERR_CANT_CREATE);
#endif

	initialized = true;
	return OK;
}

Error MCPBuiltinFeatures::prepare_for_registration(String &r_error) {
	ERR_FAIL_COND_V(!initialized, ERR_UNCONFIGURED);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	GDScriptLanguageProtocol *language_protocol = GDScriptLanguageProtocol::get_singleton();
	if (!language_protocol || language_protocol->get_analysis_service().is_null()) {
		r_error = "The built-in GDScript analysis service is not available.";
		return ERR_UNCONFIGURED;
	}
	gdscript_session_manager->set_analysis_service(language_protocol->get_analysis_service());
#endif
	return OK;
}

Error MCPBuiltinFeatures::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	ERR_FAIL_COND_V(!initialized, ERR_UNCONFIGURED);
	return feature_set.register_tools(p_registry, r_error);
}

void MCPBuiltinFeatures::unregister_tools() {
	feature_set.unregister_tools();
}

void MCPBuiltinFeatures::process() {
	feature_set.process();
}

void MCPBuiltinFeatures::on_session_removed(const String &p_session_id) {
	feature_set.on_session_removed(p_session_id);
}

void MCPBuiltinFeatures::shutdown() {
	feature_set.shutdown();
}

void MCPBuiltinFeatures::configure_host(const Dictionary &p_project_metadata) {
	ERR_FAIL_NULL(harness_provider);
	harness_provider->set_trace_service(trace_service, p_project_metadata);
}

void MCPBuiltinFeatures::clear_debug_events() {
	ERR_FAIL_NULL(debug_event_store);
	debug_event_store->clear();
}

Error MCPBuiltinFeatures::start_debug_capture() {
	ERR_FAIL_NULL_V(debug_capture, ERR_UNCONFIGURED);
	return debug_capture->start();
}

void MCPBuiltinFeatures::stop_debug_capture() {
	if (debug_capture) {
		debug_capture->stop();
	}
}

void MCPBuiltinFeatures::clear_runtime_requests() {
	if (runtime_observation_debugger_plugin.is_valid()) {
		runtime_observation_debugger_plugin->clear_requests();
	}
}

MCPBuiltinFeatures::~MCPBuiltinFeatures() {
	feature_set.shutdown();
	clear_runtime_requests();
	stop_debug_capture();
	runtime_observation_debugger_plugin.unref();
	runtime_input_debugger_plugin.unref();
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	memdelete(gdscript_provider);
#endif
	memdelete(resource_provider);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	memdelete(script_provider);
#endif
	memdelete(input_map_provider);
	memdelete(autoload_provider);
	memdelete(project_provider);
	memdelete(node_structure_provider);
	memdelete(node_provider);
	memdelete(scene_provider);
	memdelete(file_provider);
	memdelete(editor_ui_provider);
	memdelete(editor_provider);
	memdelete(harness_provider);
	memdelete(trace_service);
	memdelete(runtime_provider);
	memdelete(runtime_debug_service);
	memdelete(debug_provider);
	memdelete(debug_capture);
	memdelete(debug_event_store);
	memdelete(class_provider);
	memdelete(automation_provider);
#if defined(MODULE_GDSCRIPT_ENABLED) && !defined(GDSCRIPT_NO_LSP)
	gdscript_session_manager.unref();
#endif
}
