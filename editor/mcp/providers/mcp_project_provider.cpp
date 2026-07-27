/**************************************************************************/
/*  mcp_project_provider.cpp                                              */
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

#include "mcp_project_provider.h"

#include "mcp_project_settings_mutation.h"
#include "mcp_project_tool_utils.h"
#include "mcp_tool_utils.h"
#include "mcp_variant_codec.h"

#include "core/config/project_settings.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"

namespace {

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

static bool _read_nonempty_string(const Dictionary &p_arguments, const StringName &p_name, String &r_value) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() != Variant::STRING || String(value).is_empty()) {
		return false;
	}
	r_value = value;
	return true;
}

static bool _is_managed_setting(const String &p_name) {
	static const char *managed_prefixes[] = {
		"autoload/",
		"autoload_prepend/",
		"global_group/",
		"global_shader_parameter/",
		"input/",
	};
	for (const char *prefix : managed_prefixes) {
		if (p_name.begins_with(prefix)) {
			return true;
		}
	}
	return false;
}

static Dictionary _make_setting_result(const String &p_name, const Variant &p_value, const PropertyInfo *p_info, bool p_include_value) {
	Dictionary result;
	if (p_info) {
		result = MCPToolUtils::make_property_description(*p_info);
	} else {
		result["name"] = p_name;
		result["type"] = Variant::get_type_name(p_value.get_type());
		result["typeId"] = int(p_value.get_type());
	}
	result["builtin"] = ProjectSettings::get_singleton()->is_builtin_setting(p_name);
	result["order"] = ProjectSettings::get_singleton()->get_order(p_name);
	if (!p_include_value) {
		return result;
	}

	Variant encoded;
	String encoding_error;
	if (MCPVariantCodec::encode(p_value, encoded, &encoding_error) == OK) {
		result["encodable"] = true;
		result["value"] = encoded;
	} else {
		result["encodable"] = false;
		result["encodingError"] = encoding_error;
	}
	return result;
}

static PropertyInfo _find_setting_info(const String &p_name, bool &r_found) {
	r_found = false;
	List<PropertyInfo> properties;
	ProjectSettings::get_singleton()->get_property_list(&properties, true);
	for (const PropertyInfo &property : properties) {
		if (property.name == p_name) {
			r_found = true;
			return property;
		}
	}
	return PropertyInfo();
}

} // namespace

MCPProjectProvider::~MCPProjectProvider() {
	unregister_tools();
}

Error MCPProjectProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Project tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.project.get_settings", "List visible project settings with bounded optional values.",
				MCPProjectToolUtils::settings_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPProjectProvider::get_settings), MCPProjectToolUtils::settings_output_schema() },
		{ "godot.project.get_setting", "Get one project setting and its editor metadata.",
				MCPProjectToolUtils::setting_name_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPProjectProvider::get_setting), MCPProjectToolUtils::setting_output_schema(true) },
		{ "godot.project.set_setting", "Set a project setting through editor undo/redo without saving project.godot.",
				MCPProjectToolUtils::set_setting_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPProjectProvider::set_setting), MCPProjectToolUtils::named_mutation_output_schema("changed", "Whether the project setting changed.", true) },
		{ "godot.project.erase_setting", "Erase a project setting through editor undo/redo without saving project.godot.",
				MCPProjectToolUtils::setting_name_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPProjectProvider::erase_setting), MCPProjectToolUtils::named_mutation_output_schema("erased", "Whether the project setting was erased.") },
		{ "godot.project.save", "Explicitly save current project settings to project.godot.",
				MCPToolUtils::make_object_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPProjectProvider::save), MCPProjectToolUtils::save_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPProjectProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPProjectProvider::get_settings(const Dictionary &p_arguments, const Dictionary &) {
	const Variant prefix_value = p_arguments.get("prefix", String());
	const Variant include_values_value = p_arguments.get("includeValues", false);
	if (prefix_value.get_type() != Variant::STRING || include_values_value.get_type() != Variant::BOOL) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "prefix must be a string and includeValues must be boolean.");
	}
	int64_t limit = 256;
	if (!MCPToolUtils::try_get_json_integer(p_arguments.get("limit", 256), 1, 1024, limit)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "limit must be an integer between 1 and 1024.");
	}

	const String prefix = prefix_value;
	const bool include_values = include_values_value;
	Array settings;
	int matched_count = 0;
	List<PropertyInfo> properties;
	ProjectSettings::get_singleton()->get_property_list(&properties, true);
	for (const PropertyInfo &property : properties) {
		const String name = property.name;
		if (!prefix.is_empty() && !name.begins_with(prefix)) {
			continue;
		}
		matched_count++;
		if (settings.size() >= limit) {
			continue;
		}
		settings.push_back(_make_setting_result(name, ProjectSettings::get_singleton()->get_setting(name), &property, include_values));
	}

	Dictionary result;
	result["prefix"] = prefix;
	result["matchedCount"] = matched_count;
	result["returnedCount"] = settings.size();
	result["truncated"] = matched_count > settings.size();
	result["settings"] = settings;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPProjectProvider::get_setting(const Dictionary &p_arguments, const Dictionary &) {
	String name;
	if (!_read_nonempty_string(p_arguments, "name", name)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty setting name is required.");
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (!settings->has_setting(name)) {
		return MCPToolUtils::make_error_result("SETTING_NOT_FOUND", "Project setting was not found: " + name);
	}
	bool found = false;
	const PropertyInfo info = _find_setting_info(name, found);
	return MCPToolUtils::make_success_result(_make_setting_result(name, settings->get_setting(name), found ? &info : nullptr, true));
}

Dictionary MCPProjectProvider::set_setting(const Dictionary &p_arguments, const Dictionary &) {
	String name;
	if (!_read_nonempty_string(p_arguments, "name", name) || !p_arguments.has("value")) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty setting name and value are required.");
	}
	if (_is_managed_setting(name)) {
		return MCPToolUtils::make_error_result("MANAGED_SETTING", "Use the dedicated MCP tools for this managed project-setting namespace: " + name);
	}

	Variant decoded;
	String decode_error;
	if (MCPVariantCodec::decode(p_arguments["value"], decoded, &decode_error) != OK || decoded.get_type() == Variant::NIL) {
		return MCPToolUtils::make_error_result("INVALID_VALUE", decode_error.is_empty() ? "Use erase_setting to remove a setting." : decode_error);
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const bool old_existed = settings->has_setting(name);
	const Variant old_value = old_existed ? settings->get_setting(name) : Variant();
	const int old_order = old_existed ? settings->get_order(name) : -1;
	MCPProjectSettingsMutation::commit("Set Project Setting", name, decoded, old_existed, old_value, old_order, MCPProjectSettingsMutation::REFRESH_NONE);

	Dictionary result;
	result["name"] = name;
	result["changed"] = !old_existed || old_value != decoded;
	result["saved"] = false;
	Variant encoded;
	if (MCPVariantCodec::encode(settings->get_setting(name), encoded, &decode_error) == OK) {
		result["value"] = encoded;
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPProjectProvider::erase_setting(const Dictionary &p_arguments, const Dictionary &) {
	String name;
	if (!_read_nonempty_string(p_arguments, "name", name)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty setting name is required.");
	}
	if (_is_managed_setting(name)) {
		return MCPToolUtils::make_error_result("MANAGED_SETTING", "Use the dedicated MCP tools for this managed project-setting namespace: " + name);
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (!settings->has_setting(name)) {
		return MCPToolUtils::make_error_result("SETTING_NOT_FOUND", "Project setting was not found: " + name);
	}
	const Variant old_value = settings->get_setting(name);
	const int old_order = settings->get_order(name);
	MCPProjectSettingsMutation::commit("Erase Project Setting", name, Variant(), true, old_value, old_order, MCPProjectSettingsMutation::REFRESH_NONE);

	Dictionary result;
	result["name"] = name;
	result["erased"] = true;
	result["saved"] = false;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPProjectProvider::save(const Dictionary &, const Dictionary &) {
	const Error error = ProjectSettings::get_singleton()->save();
	if (error != OK) {
		return MCPToolUtils::make_error_result("SAVE_FAILED", error_names[error]);
	}
	Dictionary result;
	result["path"] = "res://project.godot";
	result["saved"] = true;
	return MCPToolUtils::make_success_result(result);
}
