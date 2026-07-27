/**************************************************************************/
/*  mcp_autoload_provider.cpp                                             */
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

#include "mcp_autoload_provider.h"

#include "mcp_path_utils.h"
#include "mcp_project_settings_mutation.h"
#include "mcp_project_tool_utils.h"
#include "mcp_tool_utils.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/settings/editor_autoload_settings.h"

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

} // namespace

MCPAutoloadProvider::MCPAutoloadProvider(const String &p_project_root) :
		project_root(p_project_root) {
}

MCPAutoloadProvider::~MCPAutoloadProvider() {
	unregister_tools();
}

Error MCPAutoloadProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Autoload tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.autoload.get_all", "List project Autoload entries in load order.",
				MCPToolUtils::make_object_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPAutoloadProvider::get_autoloads), MCPProjectToolUtils::autoloads_output_schema() },
		{ "godot.autoload.add", "Add a project-local Script or PackedScene Autoload through editor undo/redo.",
				MCPProjectToolUtils::autoload_add_schema(), MCPToolUtils::TOOL_ADDITIVE, callable_mp(this, &MCPAutoloadProvider::add_autoload), MCPProjectToolUtils::autoload_add_output_schema() },
		{ "godot.autoload.remove", "Remove an Autoload through editor undo/redo.",
				MCPProjectToolUtils::autoload_name_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPAutoloadProvider::remove_autoload), MCPProjectToolUtils::named_mutation_output_schema("removed", "Whether the Autoload was removed.") },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPAutoloadProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPAutoloadProvider::get_autoloads(const Dictionary &, const Dictionary &) {
	Array autoloads;
	ProjectSettings *settings = ProjectSettings::get_singleton();
	List<PropertyInfo> properties;
	settings->get_property_list(&properties, true);
	for (const PropertyInfo &property : properties) {
		const String setting_name = property.name;
		const bool prepend = setting_name.begins_with("autoload_prepend/");
		if (!prepend && !setting_name.begins_with("autoload/")) {
			continue;
		}
		String value = settings->get_setting(setting_name);
		const bool singleton = value.begins_with("*");
		if (singleton) {
			value = value.substr(1);
		}
		Dictionary entry;
		entry["name"] = setting_name.get_slicec('/', 1);
		entry["path"] = ResourceUID::ensure_path(value);
		entry["singleton"] = singleton;
		entry["prepend"] = prepend;
		entry["order"] = settings->get_order(setting_name);
		autoloads.push_back(entry);
	}
	Dictionary result;
	result["count"] = autoloads.size();
	result["autoloads"] = autoloads;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPAutoloadProvider::add_autoload(const Dictionary &p_arguments, const Dictionary &) {
	String name;
	String path;
	const Variant singleton_value = p_arguments.get("singleton", true);
	if (!_read_nonempty_string(p_arguments, "name", name) || !_read_nonempty_string(p_arguments, "path", path) || singleton_value.get_type() != Variant::BOOL) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "name and path must be non-empty strings and singleton must be boolean.");
	}
	String name_error;
	if (!EditorAutoloadSettings::is_autoload_name_valid(name, &name_error)) {
		return MCPToolUtils::make_error_result("INVALID_AUTOLOAD_NAME", name_error);
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings->has_autoload(name) || settings->has_setting("autoload/" + name) || settings->has_setting("autoload_prepend/" + name)) {
		return MCPToolUtils::make_error_result("AUTOLOAD_EXISTS", "Autoload already exists: " + name);
	}

	String resource_path;
	String absolute_path;
	String path_error;
	const Error path_result = project_root.is_empty() ?
			MCPPathUtils::resolve_project_file_path(path, resource_path, absolute_path, &path_error) :
			MCPPathUtils::resolve_project_file_path_for_root(path, project_root, resource_path, absolute_path, &path_error);
	if (path_result != OK || !FileAccess::exists(absolute_path)) {
		return MCPToolUtils::make_error_result("INVALID_AUTOLOAD_PATH", path_error.is_empty() ? "Autoload resource does not exist." : path_error);
	}
	const String resource_type = ResourceLoader::get_resource_type(project_root.is_empty() ? resource_path : absolute_path);
	if (resource_type != "PackedScene" && !ClassDB::is_parent_class(resource_type, "Script")) {
		return MCPToolUtils::make_error_result("INVALID_AUTOLOAD_RESOURCE", "Autoload path must reference a Script or PackedScene.");
	}

	const String setting_name = "autoload/" + name;
	String stored_path = ResourceUID::get_singleton()->path_to_uid(resource_path);
	if (bool(singleton_value)) {
		stored_path = "*" + stored_path;
	}
	MCPProjectSettingsMutation::commit("Add Autoload", setting_name, stored_path, false, Variant(), -1, MCPProjectSettingsMutation::REFRESH_AUTOLOAD);

	Dictionary result;
	result["name"] = name;
	result["path"] = resource_path;
	result["singleton"] = bool(singleton_value);
	result["saved"] = false;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPAutoloadProvider::remove_autoload(const Dictionary &p_arguments, const Dictionary &) {
	String name;
	if (!_read_nonempty_string(p_arguments, "name", name)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty Autoload name is required.");
	}
	ProjectSettings *settings = ProjectSettings::get_singleton();
	String setting_name = "autoload/" + name;
	if (!settings->has_setting(setting_name)) {
		setting_name = "autoload_prepend/" + name;
	}
	if (!settings->has_setting(setting_name)) {
		return MCPToolUtils::make_error_result("AUTOLOAD_NOT_FOUND", "Autoload was not found: " + name);
	}
	const Variant old_value = settings->get_setting(setting_name);
	const int old_order = settings->get_order(setting_name);
	MCPProjectSettingsMutation::commit("Remove Autoload", setting_name, Variant(), true, old_value, old_order, MCPProjectSettingsMutation::REFRESH_AUTOLOAD);

	Dictionary result;
	result["name"] = name;
	result["removed"] = true;
	result["saved"] = false;
	return MCPToolUtils::make_success_result(result);
}
