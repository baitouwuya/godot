/**************************************************************************/
/*  mcp_editor_tool_utils.cpp                                             */
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

#include "mcp_editor_tool_utils.h"

#include "mcp_tool_utils.h"

namespace MCPEditorToolUtils {

Dictionary empty_input_schema() {
	return MCPToolUtils::make_object_schema();
}

Dictionary state_output_schema() {
	Dictionary string_property = MCPToolUtils::make_property_schema("string", "Editor resource path.");
	Dictionary string_array_property = MCPToolUtils::make_property_schema("array", "Editor resource paths.");
	string_array_property["items"] = string_property;

	Dictionary properties;
	properties["currentScene"] = string_property;
	properties["unsavedScenes"] = string_array_property;
	properties["openScripts"] = string_array_property;
	properties["unsavedScripts"] = string_array_property;
	properties["canUndo"] = MCPToolUtils::make_property_schema("boolean", "Whether an editor undo action is available.");
	properties["canRedo"] = MCPToolUtils::make_property_schema("boolean", "Whether an editor redo action is available.");
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "currentScene", "unsavedScenes", "openScripts", "unsavedScripts", "canUndo", "canRedo" });
}

Dictionary history_output_schema() {
	Dictionary action = MCPToolUtils::make_property_schema("string", "History action that was performed.");
	action["enum"] = PackedStringArray{ "undo", "redo" };
	Dictionary properties;
	properties["action"] = action;
	properties["performed"] = MCPToolUtils::make_property_schema("boolean", "Whether the history action completed.");
	properties["canUndo"] = MCPToolUtils::make_property_schema("boolean", "Whether another undo is available.");
	properties["canRedo"] = MCPToolUtils::make_property_schema("boolean", "Whether another redo is available.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "action", "performed", "canUndo", "canRedo" });
}

} // namespace MCPEditorToolUtils
