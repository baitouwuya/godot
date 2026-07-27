/**************************************************************************/
/*  mcp_node_provider.h                                                   */
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

#include "core/object/object.h"
#include "core/variant/dictionary.h"

class MCPToolRegistry;
class MCPUndoRedoAction;
class Node;

class MCPNodeProvider : public Object, public MCPEditorFeature {
public:
	MCPNodeProvider(Node *p_scene_root = nullptr, MCPUndoRedoAction *p_undo_redo = nullptr);
	~MCPNodeProvider();

	Error register_tools(MCPToolRegistry *p_registry, String *r_error = nullptr) override;
	void unregister_tools() override;

	Dictionary get_properties(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary create(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary set_property(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary attach_script(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary get_groups(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary add_to_group(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary remove_from_group(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary get_signal_connections(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary connect_signal(const Dictionary &p_arguments, const Dictionary &p_context);
	Dictionary disconnect_signal(const Dictionary &p_arguments, const Dictionary &p_context);

private:
	Node *_get_scene_root() const;
	Dictionary _mutate_signal_connection(const Dictionary &p_arguments, bool p_connect);

	MCPToolRegistry *tool_registry = nullptr;
	Node *scene_root_override = nullptr;
	MCPUndoRedoAction *undo_redo_override = nullptr;
};
