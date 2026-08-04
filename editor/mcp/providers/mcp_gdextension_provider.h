/**************************************************************************/
/*  mcp_gdextension_provider.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#pragma once

#include "../mcp_editor_feature.h"
#include "mcp_gdextension_build_service.h"

#include "core/object/object.h"

class MCPGDExtensionProvider : public Object, public MCPEditorFeature {
	MCPToolRegistry *tool_registry = nullptr;
	MCPGDExtensionBuildService build_service;

public:
	~MCPGDExtensionProvider();

	Error register_tools(MCPToolRegistry *p_registry, String *r_error = nullptr) override;
	void unregister_tools() override;
	void process() override { build_service.process(); }
	void on_session_removed(const String &p_session_id) override { build_service.release_session(p_session_id); }
	void shutdown() override { build_service.shutdown(); }

	Dictionary build(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary build_status(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary build_cancel(const Dictionary &p_arguments, const Dictionary &p_context);
};
