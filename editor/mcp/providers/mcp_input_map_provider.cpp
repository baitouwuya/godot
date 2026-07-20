/**************************************************************************/
/*  mcp_input_map_provider.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
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

#include "mcp_input_map_provider.h"

#include "mcp_input_map_event_codec.h"
#include "mcp_project_settings_mutation.h"
#include "mcp_tool_utils.h"

#include "core/config/project_settings.h"
#include "core/input/input_map.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

static Dictionary _property_schema(const String &p_type, const String &p_description) {
	Dictionary property;
	property["type"] = p_type;
	property["description"] = p_description;
	return property;
}

static Dictionary _object_schema(const Dictionary &p_properties = Dictionary(), const PackedStringArray &p_required = PackedStringArray()) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["required"] = p_required;
	schema["additionalProperties"] = false;
	return schema;
}

static Dictionary _get_actions_schema() {
	Dictionary properties;
	properties["prefix"] = _property_schema("string", "Optional action-name prefix.");
	Dictionary include_builtin = _property_schema("boolean", "Include built-in UI actions. Defaults to false.");
	include_builtin["default"] = false;
	properties["includeBuiltin"] = include_builtin;
	Dictionary limit = _property_schema("integer", "Maximum number of actions returned.");
	limit["minimum"] = 1;
	limit["maximum"] = 512;
	limit["default"] = 128;
	properties["limit"] = limit;
	return _object_schema(properties);
}

static Dictionary _set_action_schema() {
	Dictionary properties;
	properties["name"] = _property_schema("string", "Project input action name.");
	Dictionary deadzone = _property_schema("number", "Optional action deadzone between 0 and 1.");
	deadzone["minimum"] = 0.0;
	deadzone["maximum"] = 1.0;
	properties["deadzone"] = deadzone;
	Dictionary events = _property_schema("array", "Optional complete event list using the semantic event objects returned by get_actions.");
	events["maxItems"] = 64;
	Dictionary event_item;
	event_item["type"] = "object";
	event_item["additionalProperties"] = true;
	events["items"] = event_item;
	properties["events"] = events;
	return _object_schema(properties, PackedStringArray{ "name" });
}

static Dictionary _name_schema() {
	Dictionary properties;
	properties["name"] = _property_schema("string", "Project input action name.");
	return _object_schema(properties, PackedStringArray{ "name" });
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

static Dictionary _error(const String &p_code, const String &p_message) {
	return MCPToolUtils::make_error_result(p_code, p_message);
}

static bool _read_name(const Dictionary &p_arguments, String &r_name) {
	const Variant value = p_arguments.get("name", Variant());
	if (value.get_type() != Variant::STRING) {
		return false;
	}
	r_name = String(value).strip_edges();
	return !r_name.is_empty() && !r_name.contains("/") && r_name.length() <= 128;
}

static Dictionary _make_action(const String &p_setting_name, const Dictionary &p_action) {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	Dictionary result;
	result["name"] = p_setting_name.trim_prefix("input/");
	result["builtin"] = settings->is_builtin_setting(p_setting_name);
	result["deadzone"] = p_action.get("deadzone", InputMap::DEFAULT_DEADZONE);

	Array encoded_events;
	const Array events = p_action.get("events", Array());
	for (int i = 0; i < events.size(); i++) {
		const Ref<InputEvent> event = events[i];
		if (event.is_null()) {
			continue;
		}
		bool supported = false;
		const Dictionary encoded = MCPInputMapEventCodec::encode(event, supported);
		Dictionary entry;
		entry["index"] = i;
		entry["class"] = event->get_class();
		entry["display"] = event->as_text();
		entry["supported"] = supported;
		if (supported) {
			for (const KeyValue<Variant, Variant> &field : encoded) {
				entry[field.key] = field.value;
			}
		}
		encoded_events.push_back(entry);
	}
	result["eventCount"] = events.size();
	result["events"] = encoded_events;
	return result;
}

static bool _find_action(const String &p_name, String &r_setting_name, Dictionary &r_action) {
	r_setting_name = "input/" + p_name;
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (!settings->has_setting(r_setting_name)) {
		return false;
	}
	const Variant value = settings->get_setting(r_setting_name);
	if (value.get_type() != Variant::DICTIONARY) {
		return false;
	}
	r_action = value;
	return true;
}

} // namespace

MCPInputMapProvider::~MCPInputMapProvider() {
	unregister_tools();
}

Error MCPInputMapProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Input Map tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	Error error = p_registry->register_tool(
			MCPToolUtils::make_tool_definition("godot.input.get_actions", "List project Input Map actions and semantic event bindings.", _get_actions_schema(), MCPToolUtils::TOOL_READ_ONLY),
			callable_mp(this, &MCPInputMapProvider::get_actions), this, r_error);
	if (error == OK) {
		error = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.input.set_action", "Create or replace an Input Map action through editor undo/redo without saving project.godot.", _set_action_schema(), MCPToolUtils::TOOL_DESTRUCTIVE),
				callable_mp(this, &MCPInputMapProvider::set_action), this, r_error);
	}
	if (error == OK) {
		error = p_registry->register_tool(
				MCPToolUtils::make_tool_definition("godot.input.remove_action", "Remove a custom Input Map action through editor undo/redo.", _name_schema(), MCPToolUtils::TOOL_DESTRUCTIVE),
				callable_mp(this, &MCPInputMapProvider::remove_action), this, r_error);
	}
	if (error != OK) {
		p_registry->unregister_tools_for_owner(this);
		return error;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPInputMapProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPInputMapProvider::get_actions(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed{ "prefix", "includeBuiltin", "limit" };
	String unknown;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown)) {
		return _error("INVALID_ARGUMENTS", "Unknown argument: " + unknown);
	}
	const Variant prefix_value = p_arguments.get("prefix", String());
	const Variant include_builtin_value = p_arguments.get("includeBuiltin", false);
	if (prefix_value.get_type() != Variant::STRING || include_builtin_value.get_type() != Variant::BOOL) {
		return _error("INVALID_ARGUMENTS", "prefix must be a string and includeBuiltin must be boolean.");
	}
	int64_t limit = 128;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("limit", 128), 1, 512, limit)) {
		return _error("INVALID_ARGUMENTS", "limit must be an integer between 1 and 512.");
	}

	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String prefix = prefix_value;
	const bool include_builtin = include_builtin_value;
	Array actions;
	int matched_count = 0;
	List<PropertyInfo> properties;
	settings->get_property_list(&properties, true);
	for (const PropertyInfo &property : properties) {
		const String setting_name = property.name;
		if (!setting_name.begins_with("input/")) {
			continue;
		}
		const String action_name = setting_name.trim_prefix("input/");
		if ((!prefix.is_empty() && !action_name.begins_with(prefix)) || (!include_builtin && settings->is_builtin_setting(setting_name))) {
			continue;
		}
		const Variant value = settings->get_setting(setting_name);
		if (value.get_type() != Variant::DICTIONARY) {
			continue;
		}
		matched_count++;
		if (actions.size() < limit) {
			actions.push_back(_make_action(setting_name, value));
		}
	}

	Dictionary result;
	result["prefix"] = prefix;
	result["includeBuiltin"] = include_builtin;
	result["matchedCount"] = matched_count;
	result["returnedCount"] = actions.size();
	result["truncated"] = matched_count > actions.size();
	result["actions"] = actions;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPInputMapProvider::set_action(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed{ "name", "deadzone", "events" };
	String unknown;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown)) {
		return _error("INVALID_ARGUMENTS", "Unknown argument: " + unknown);
	}
	String name;
	if (!_read_name(p_arguments, name)) {
		return _error("INVALID_ARGUMENTS", "name must be a non-empty action name without '/'.");
	}

	String setting_name;
	Dictionary old_action;
	const bool existed = _find_action(name, setting_name, old_action);
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (existed && settings->is_builtin_setting(setting_name)) {
		return _error("BUILTIN_ACTION", "Built-in Input Map actions are read-only through MCP: " + name);
	}

	Dictionary action = existed ? old_action.duplicate(true) : Dictionary();
	const Variant default_deadzone = existed ? action.get("deadzone", Variant(InputMap::DEFAULT_DEADZONE)) : Variant(InputMap::DEFAULT_DEADZONE);
	const Variant deadzone_value = p_arguments.get("deadzone", default_deadzone);
	if (deadzone_value.get_type() != Variant::INT && deadzone_value.get_type() != Variant::FLOAT) {
		return _error("INVALID_DEADZONE", "deadzone must be numeric.");
	}
	const double deadzone = deadzone_value;
	if (!Math::is_finite(deadzone) || deadzone < 0.0 || deadzone > 1.0) {
		return _error("INVALID_DEADZONE", "deadzone must be between 0 and 1.");
	}
	action["deadzone"] = deadzone;

	if (p_arguments.has("events")) {
		if (p_arguments["events"].get_type() != Variant::ARRAY) {
			return _error("INVALID_EVENTS", "events must be an array.");
		}
		const Array input_events = p_arguments["events"];
		if (input_events.size() > 64) {
			return _error("INVALID_EVENTS", "events cannot contain more than 64 entries.");
		}
		Array events;
		for (int i = 0; i < input_events.size(); i++) {
			if (input_events[i].get_type() != Variant::DICTIONARY) {
				return _error("INVALID_EVENTS", vformat("events[%d] must be an object.", i));
			}
			Ref<InputEvent> event;
			String event_error;
			if (MCPInputMapEventCodec::decode(input_events[i], event, &event_error) != OK) {
				return _error("INVALID_EVENTS", vformat("events[%d]: %s", i, event_error));
			}
			events.push_back(event);
		}
		action["events"] = events;
	} else if (!action.has("events")) {
		action["events"] = Array();
	}

	const int old_order = existed ? settings->get_order(setting_name) : -1;
	MCPProjectSettingsMutation::commit("Set Input Action", setting_name, action, existed, old_action, old_order, MCPProjectSettingsMutation::REFRESH_INPUT_MAP);
	Dictionary result = _make_action(setting_name, settings->get_setting(setting_name));
	result["changed"] = !existed || old_action != action;
	result["saved"] = false;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPInputMapProvider::remove_action(const Dictionary &p_arguments, const Dictionary &) {
	PackedStringArray allowed{ "name" };
	String unknown;
	if (!MCPToolUtils::has_only_arguments(p_arguments, allowed, unknown)) {
		return _error("INVALID_ARGUMENTS", "Unknown argument: " + unknown);
	}
	String name;
	if (!_read_name(p_arguments, name)) {
		return _error("INVALID_ARGUMENTS", "name must be a non-empty action name without '/'.");
	}
	String setting_name;
	Dictionary old_action;
	if (!_find_action(name, setting_name, old_action)) {
		return _error("ACTION_NOT_FOUND", "Input Map action was not found: " + name);
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings->is_builtin_setting(setting_name)) {
		return _error("BUILTIN_ACTION", "Built-in Input Map actions cannot be removed through MCP: " + name);
	}
	const int old_order = settings->get_order(setting_name);
	MCPProjectSettingsMutation::commit("Remove Input Action", setting_name, Variant(), true, old_action, old_order, MCPProjectSettingsMutation::REFRESH_INPUT_MAP);

	Dictionary result;
	result["name"] = name;
	result["removed"] = true;
	result["saved"] = false;
	return MCPToolUtils::make_success_result(result);
}
