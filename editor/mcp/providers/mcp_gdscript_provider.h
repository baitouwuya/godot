/**************************************************************************/
/*  mcp_gdscript_provider.h                                               */
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
/* INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,        */
/* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL */
/* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR    */
/* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,  */
/* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR  */
/* OTHER DEALINGS IN THE SOFTWARE.                                       */
/**************************************************************************/

#pragma once

#include "../mcp_editor_feature.h"
#include "mcp_gdscript_semantic_service.h"

#include "core/object/object.h"
#include "core/variant/dictionary.h"

class MCPGDScriptRequestParser;
class MCPToolRegistry;

class MCPGDScriptProvider : public Object, public MCPEditorFeature {
public:
	explicit MCPGDScriptProvider(const Ref<MCPGDScriptSessionManager> &p_session_manager = Ref<MCPGDScriptSessionManager>());
	~MCPGDScriptProvider();

	Error register_tools(MCPToolRegistry *p_registry, String *r_error = nullptr) override;
	void unregister_tools() override;

	Dictionary diagnostics(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary symbols(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary completion(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary hover(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary definition(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary declaration(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary references(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary signature_help(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary rename(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary apply_workspace_edit(const Dictionary &p_arguments, const Dictionary &p_context);

private:
	MCPToolRegistry *tool_registry = nullptr;
	MCPGDScriptSemanticService semantic_service;

	MCPGDScriptRequestParser _request_parser() const;
	bool _validate_ready(Dictionary &r_error_result) const;
	bool _parse_session_id(const Dictionary &p_context, String &r_session_id, Dictionary &r_error_result) const;
	Dictionary _tool_result(const MCPGDScriptSemanticService::OperationResult &p_result) const;
	Dictionary _invalid_arguments(const String &p_message) const;
};
