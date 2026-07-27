/**************************************************************************/
/*  mcp_input_map_tool_utils.cpp                                          */
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
/* "Software"), to deal in the Software without restriction, including    */
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

#include "mcp_input_map_tool_utils.h"

#include "mcp_tool_utils.h"

namespace {

static Dictionary _name_property_schema() {
	Dictionary schema = MCPToolUtils::make_property_schema("string", "Project input action name.");
	schema["minLength"] = 1;
	schema["maxLength"] = 128;
	return schema;
}

} // namespace

namespace MCPInputMapToolUtils {

Dictionary get_actions_schema() {
	Dictionary properties;
	properties["prefix"] = MCPToolUtils::make_property_schema("string", "Optional action-name prefix.");
	Dictionary include_builtin = MCPToolUtils::make_property_schema("boolean", "Include built-in UI actions. Defaults to false.");
	include_builtin["default"] = false;
	properties["includeBuiltin"] = include_builtin;
	Dictionary limit = MCPToolUtils::make_property_schema("integer", "Maximum number of actions returned.");
	limit["minimum"] = 1;
	limit["maximum"] = 512;
	limit["default"] = 128;
	properties["limit"] = limit;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary set_action_schema() {
	Dictionary properties;
	properties["name"] = _name_property_schema();
	Dictionary deadzone = MCPToolUtils::make_property_schema("number", "Optional action deadzone between 0 and 1.");
	deadzone["minimum"] = 0.0;
	deadzone["maximum"] = 1.0;
	properties["deadzone"] = deadzone;
	Dictionary events = MCPToolUtils::make_property_schema("array", "Optional complete event list using the semantic event objects returned by get_actions.");
	events["maxItems"] = 64;
	Dictionary event_item;
	event_item["type"] = "object";
	event_item["additionalProperties"] = true;
	events["items"] = event_item;
	properties["events"] = events;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name" });
}

Dictionary name_schema() {
	Dictionary properties;
	properties["name"] = _name_property_schema();
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name" });
}

Dictionary action_output_schema(bool p_include_mutation_state) {
	Dictionary event;
	event["type"] = "object";
	event["additionalProperties"] = true;
	Dictionary events = MCPToolUtils::make_property_schema("array", "Semantic input events.");
	events["items"] = event;
	Dictionary event_count = MCPToolUtils::make_property_schema("integer", "Number of configured input events.");
	event_count["minimum"] = 0;

	Dictionary properties;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Input action name.");
	properties["builtin"] = MCPToolUtils::make_property_schema("boolean", "Whether the action is built in.");
	properties["deadzone"] = MCPToolUtils::make_property_schema("number", "Input action deadzone.");
	properties["eventCount"] = event_count;
	properties["events"] = events;
	PackedStringArray required{ "name", "builtin", "deadzone", "eventCount", "events" };
	if (p_include_mutation_state) {
		properties["changed"] = MCPToolUtils::make_property_schema("boolean", "Whether the action changed.");
		properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether project.godot was saved.");
		required.push_back("changed");
		required.push_back("saved");
	}
	return MCPToolUtils::make_object_schema(properties, required);
}

Dictionary get_actions_output_schema() {
	Dictionary count = MCPToolUtils::make_property_schema("integer", "Input action count.");
	count["minimum"] = 0;
	Dictionary actions = MCPToolUtils::make_property_schema("array", "Matching Input Map actions.");
	actions["items"] = action_output_schema(false);
	actions["maxItems"] = 512;

	Dictionary properties;
	properties["prefix"] = MCPToolUtils::make_property_schema("string", "Applied action-name prefix.");
	properties["includeBuiltin"] = MCPToolUtils::make_property_schema("boolean", "Whether built-in actions were included.");
	properties["matchedCount"] = count;
	properties["returnedCount"] = count;
	properties["truncated"] = MCPToolUtils::make_property_schema("boolean", "Whether matching actions were omitted by the limit.");
	properties["actions"] = actions;
	return MCPToolUtils::make_object_schema(properties,
			PackedStringArray{ "prefix", "includeBuiltin", "matchedCount", "returnedCount", "truncated", "actions" });
}

Dictionary remove_action_output_schema() {
	Dictionary properties;
	properties["name"] = MCPToolUtils::make_property_schema("string", "Removed input action name.");
	properties["removed"] = MCPToolUtils::make_property_schema("boolean", "Whether the action was removed.");
	properties["saved"] = MCPToolUtils::make_property_schema("boolean", "Whether project.godot was saved.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "name", "removed", "saved" });
}

bool read_name(const Dictionary &p_arguments, String &r_name) {
	const Variant value = p_arguments.get("name", Variant());
	if (value.get_type() != Variant::STRING) {
		return false;
	}
	r_name = String(value).strip_edges();
	return !r_name.is_empty() && !r_name.contains("/") && r_name.length() <= 128;
}

} // namespace MCPInputMapToolUtils
