/**************************************************************************/
/*  mcp_editor_ui_tool_utils.cpp                                          */
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

#include "mcp_editor_ui_tool_utils.h"

#include "mcp_tool_utils.h"

namespace {

static constexpr int MIN_SNAPSHOT_DEPTH = 1;
static constexpr int MAX_SNAPSHOT_DEPTH = 64;
static constexpr int MIN_SNAPSHOT_ITEMS = 1;
static constexpr int MAX_SNAPSHOT_ITEMS = 2048;

static Dictionary _required_string_schema(const String &p_description) {
	Dictionary schema = MCPToolUtils::make_property_schema("string", p_description);
	schema["minLength"] = 1;
	return schema;
}

static PackedStringArray _action_names() {
	return PackedStringArray{ "focus", "click", "activate", "select", "set_value", "expand", "collapse", "increment", "decrement" };
}

} // namespace

namespace MCPEditorUIToolUtils {

Dictionary get_actions_schema() {
	const MCPEditorUIService::SnapshotOptions defaults;
	Dictionary properties;
	Dictionary include_disabled = MCPToolUtils::make_property_schema("boolean", "Include visible controls that cannot currently be operated.");
	include_disabled["default"] = defaults.include_disabled;
	properties["includeDisabled"] = include_disabled;
	Dictionary include_values = MCPToolUtils::make_property_schema("boolean", "Include non-secret current control values.");
	include_values["default"] = defaults.include_values;
	properties["includeValues"] = include_values;
	Dictionary max_depth = MCPToolUtils::make_property_schema("integer", "Maximum UI tree traversal depth.");
	max_depth["minimum"] = MIN_SNAPSHOT_DEPTH;
	max_depth["maximum"] = MAX_SNAPSHOT_DEPTH;
	max_depth["default"] = defaults.max_depth;
	properties["maxDepth"] = max_depth;
	Dictionary limit = MCPToolUtils::make_property_schema("integer", "Maximum visible action targets returned.");
	limit["minimum"] = MIN_SNAPSHOT_ITEMS;
	limit["maximum"] = MAX_SNAPSHOT_ITEMS;
	limit["default"] = defaults.limit;
	properties["limit"] = limit;
	return MCPToolUtils::make_object_schema(properties);
}

Dictionary perform_schema() {
	Dictionary properties;
	properties["snapshotId"] = _required_string_schema("Opaque session-bound UI snapshot ID.");
	properties["targetId"] = _required_string_schema("Opaque target ID from the referenced UI snapshot.");
	Dictionary action = MCPToolUtils::make_property_schema("string", "Semantic editor UI action to perform.");
	action["enum"] = _action_names();
	properties["action"] = action;
	Dictionary value;
	value["description"] = "Value used by set_value actions.";
	value["type"] = Array{ "string", "number", "integer", "boolean" };
	properties["value"] = value;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "snapshotId", "targetId", "action" });
}

Dictionary snapshot_output_schema() {
	Dictionary item;
	item["type"] = "object";
	item["additionalProperties"] = true;
	Dictionary items = MCPToolUtils::make_property_schema("array", "Visible editor UI action targets.");
	items["items"] = item;
	items["maxItems"] = MAX_SNAPSHOT_ITEMS;
	Dictionary count = MCPToolUtils::make_property_schema("integer", "Returned UI target count.");
	count["minimum"] = 0;
	count["maximum"] = MAX_SNAPSHOT_ITEMS;

	Dictionary properties;
	properties["snapshotId"] = MCPToolUtils::make_property_schema("string", "Opaque session-bound UI snapshot ID.");
	properties["windowTitle"] = MCPToolUtils::make_property_schema("string", "Captured editor window title.");
	properties["items"] = items;
	properties["truncated"] = MCPToolUtils::make_property_schema("boolean", "Whether additional UI targets were omitted.");
	properties["count"] = count;
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "snapshotId", "windowTitle", "items", "truncated", "count" });
}

Dictionary perform_output_schema() {
	Dictionary action = MCPToolUtils::make_property_schema("string", "Performed semantic UI action.");
	action["enum"] = _action_names();
	Dictionary properties;
	properties["performed"] = MCPToolUtils::make_property_schema("boolean", "Whether the UI action completed.");
	properties["action"] = action;
	properties["targetId"] = MCPToolUtils::make_property_schema("string", "Opaque acted-on target ID.");
	properties["snapshotInvalidated"] = MCPToolUtils::make_property_schema("boolean", "Whether the source snapshot was invalidated.");
	return MCPToolUtils::make_object_schema(properties, PackedStringArray{ "performed", "action", "targetId", "snapshotInvalidated" });
}

Error parse_snapshot_options(const Dictionary &p_arguments, MCPEditorUIService::SnapshotOptions &r_options, String &r_error_message) {
	r_options = MCPEditorUIService::SnapshotOptions();
	r_error_message = String();
	if (p_arguments.has("includeDisabled")) {
		if (p_arguments["includeDisabled"].get_type() != Variant::BOOL) {
			r_error_message = "includeDisabled must be a boolean.";
			return ERR_INVALID_PARAMETER;
		}
		r_options.include_disabled = p_arguments["includeDisabled"];
	}
	if (p_arguments.has("includeValues")) {
		if (p_arguments["includeValues"].get_type() != Variant::BOOL) {
			r_error_message = "includeValues must be a boolean.";
			return ERR_INVALID_PARAMETER;
		}
		r_options.include_values = p_arguments["includeValues"];
	}
	int64_t integer = 0;
	if (p_arguments.has("maxDepth")) {
		if (!MCPToolUtils::try_get_json_integer(p_arguments["maxDepth"], MIN_SNAPSHOT_DEPTH, MAX_SNAPSHOT_DEPTH, integer)) {
			r_error_message = vformat("maxDepth must be an integer from %d to %d.", MIN_SNAPSHOT_DEPTH, MAX_SNAPSHOT_DEPTH);
			return ERR_INVALID_PARAMETER;
		}
		r_options.max_depth = integer;
	}
	if (p_arguments.has("limit")) {
		if (!MCPToolUtils::try_get_json_integer(p_arguments["limit"], MIN_SNAPSHOT_ITEMS, MAX_SNAPSHOT_ITEMS, integer)) {
			r_error_message = vformat("limit must be an integer from %d to %d.", MIN_SNAPSHOT_ITEMS, MAX_SNAPSHOT_ITEMS);
			return ERR_INVALID_PARAMETER;
		}
		r_options.limit = integer;
	}
	return OK;
}

Error parse_perform_arguments(const Dictionary &p_arguments, PerformArguments &r_arguments, String &r_error_message) {
	r_arguments = PerformArguments();
	r_error_message = String();
	const Variant snapshot = p_arguments.get("snapshotId", Variant());
	const Variant target = p_arguments.get("targetId", Variant());
	const Variant action = p_arguments.get("action", Variant());
	if (snapshot.get_type() != Variant::STRING || String(snapshot).is_empty() || target.get_type() != Variant::STRING || String(target).is_empty() || action.get_type() != Variant::STRING || String(action).is_empty()) {
		r_error_message = "snapshotId, targetId, and action must be non-empty strings.";
		return ERR_INVALID_PARAMETER;
	}
	r_arguments.snapshot_id = String(snapshot);
	r_arguments.target_id = String(target);
	r_arguments.action = String(action);
	return OK;
}

} // namespace MCPEditorUIToolUtils
