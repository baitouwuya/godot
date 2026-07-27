/**************************************************************************/
/*  test_mcp_project_provider.cpp                                         */
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

#include "core/config/project_settings.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/os/os.h"
#include "editor/mcp/mcp_editor_feature.h"
#include "editor/mcp/providers/mcp_autoload_provider.h"
#include "editor/mcp/providers/mcp_project_provider.h"
#include "editor/mcp/providers/mcp_variant_codec.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_project_provider);

namespace TestMCPProjectProvider {

static String _error_code(const MCPToolRegistry::CallResult &p_result) {
	const Dictionary structured = p_result.result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return error.get("code", String());
}

class ScopedSettingRestore {
	String name;
	Variant value;
	int order = -1;
	bool existed = false;

public:
	explicit ScopedSettingRestore(const String &p_name) :
			name(p_name) {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		existed = settings->has_setting(name);
		if (existed) {
			value = settings->get_setting(name);
			order = settings->get_order(name);
		}
	}

	~ScopedSettingRestore() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		if (settings->has_setting(name)) {
			settings->clear(name);
		}
		if (existed) {
			settings->set_setting(name, value);
			settings->set_order(name, order);
		}
	}
};

TEST_CASE("[MCP][Provider] Project and Autoload features preserve tool order") {
	MCPToolRegistry registry;
	MCPEditorFeatureSet feature_set;
	MCPProjectProvider *project_provider = memnew(MCPProjectProvider);
	MCPAutoloadProvider *autoload_provider = memnew(MCPAutoloadProvider);
	REQUIRE(feature_set.add_feature(project_provider) == OK);
	REQUIRE(feature_set.add_feature(autoload_provider) == OK);
	REQUIRE(feature_set.register_tools(&registry) == OK);

	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 8);
	CHECK(names[0] == "godot.project.get_settings");
	CHECK(names[4] == "godot.project.save");
	CHECK(names[5] == "godot.autoload.get_all");
	CHECK(names[7] == "godot.autoload.remove");

	feature_set.unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(autoload_provider);
	memdelete(project_provider);
}

TEST_CASE("[MCP][Provider] Project settings are bounded, typed, undo-aware, and explicitly saved") {
	MCPToolRegistry registry;
	MCPProjectProvider *provider = memnew(MCPProjectProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);
	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 5);
	CHECK(names[0] == "godot.project.get_settings");
	CHECK(names[4] == "godot.project.save");
	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 5);
	const Dictionary settings_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK(Dictionary(settings_output.get("properties", Dictionary())).has("settings"));
	CHECK(PackedStringArray(settings_output.get("required", PackedStringArray())).has("matchedCount"));
	CHECK_FALSE(bool(settings_output.get("additionalProperties", true)));
	const Dictionary setting_input = Dictionary(definitions[1]).get("inputSchema", Dictionary());
	const Dictionary setting_name_schema = Dictionary(setting_input.get("properties", Dictionary())).get("name", Dictionary());
	CHECK(int(setting_name_schema.get("minLength", 0)) == 1);
	const Dictionary setting_output = Dictionary(definitions[1]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(setting_output.get("required", PackedStringArray())).has("encodable"));
	const Dictionary set_output = Dictionary(definitions[2]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(set_output.get("required", PackedStringArray())).has("changed"));
	const Dictionary save_output = Dictionary(definitions[4]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(save_output.get("required", PackedStringArray())).has("path"));

	const String suffix = String::num_int64(OS::get_singleton()->get_process_id()) + "_" + String::num_uint64(OS::get_singleton()->get_ticks_usec());
	const String setting_name = "mcp_test/settings_" + suffix;
	ScopedSettingRestore restore(setting_name);
	MCPToolCallContext context;
	Dictionary unexpected_arguments;
	unexpected_arguments["unexpected"] = true;
	CHECK(_error_code(registry.call_tool("godot.project.save", unexpected_arguments, context)) == "INVALID_ARGUMENTS");

	Dictionary set_arguments;
	set_arguments["name"] = setting_name;
	Variant encoded_value;
	REQUIRE(MCPVariantCodec::encode(Variant("res://configured.gd"), encoded_value) == OK);
	CHECK(encoded_value == Variant("res://configured.gd"));
	set_arguments["value"] = encoded_value;
	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.project.set_setting", set_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	CHECK(ProjectSettings::get_singleton()->get_setting(setting_name) == Variant("res://configured.gd"));
	const Dictionary set_result = call_result.result.get("structuredContent", Dictionary());
	CHECK_FALSE(bool(set_result.get("saved", true)));

	Dictionary get_arguments;
	get_arguments["name"] = setting_name;
	call_result = registry.call_tool("godot.project.get_setting", get_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Dictionary get_result = call_result.result.get("structuredContent", Dictionary());
	CHECK(get_result.get("name", String()) == setting_name);
	CHECK(get_result.get("value", Variant()) == encoded_value);

	Dictionary list_arguments;
	list_arguments["prefix"] = "mcp_test/";
	list_arguments["includeValues"] = true;
	list_arguments["limit"] = 1;
	call_result = registry.call_tool("godot.project.get_settings", list_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Dictionary list_result = call_result.result.get("structuredContent", Dictionary());
	CHECK(int(list_result.get("returnedCount", 0)) == 1);
	CHECK(Array(list_result.get("settings", Array())).size() == 1);

	call_result = registry.call_tool("godot.project.erase_setting", get_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting(setting_name));

	set_arguments["name"] = "input/mcp_managed";
	call_result = registry.call_tool("godot.project.set_setting", set_arguments, context);
	CHECK(_error_code(call_result) == "MANAGED_SETTING");

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] Autoload tools expose and mutate project-local entries without saving") {
	MCPToolRegistry registry;
	const String repository_root = OS::get_singleton()->get_executable_path().get_base_dir().get_base_dir();
	MCPAutoloadProvider *provider = memnew(MCPAutoloadProvider(repository_root));
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);
	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 3);
	CHECK(names[0] == "godot.autoload.get_all");
	CHECK(names[1] == "godot.autoload.add");
	CHECK(names[2] == "godot.autoload.remove");
	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 3);
	const Dictionary autoloads_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK(Dictionary(autoloads_output.get("properties", Dictionary())).has("autoloads"));
	const Dictionary add_input = Dictionary(definitions[1]).get("inputSchema", Dictionary());
	CHECK(int(Dictionary(add_input.get("properties", Dictionary())).get("path", Dictionary()).get("minLength", 0)) == 1);
	const Dictionary remove_output = Dictionary(definitions[2]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(remove_output.get("required", PackedStringArray())).has("removed"));

	const String suffix = String::num_int64(OS::get_singleton()->get_process_id()) + "_" + String::num_uint64(OS::get_singleton()->get_ticks_usec());
	const String autoload_name = "McpAutoload_" + suffix;
	const String setting_name = "autoload/" + autoload_name;
	ScopedSettingRestore restore(setting_name);
	MCPToolCallContext context;

	Dictionary add_arguments;
	add_arguments["name"] = autoload_name;
	add_arguments["path"] = "res://tests/editor/mcp/data/mcp_codec_resource.tres";
	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.autoload.add", add_arguments, context);
	CHECK(_error_code(call_result) == "INVALID_AUTOLOAD_RESOURCE");

	add_arguments["path"] = "res://tests/editor/mcp/data/mcp_autoload.gd";
	call_result = registry.call_tool("godot.autoload.add", add_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	CHECK(ProjectSettings::get_singleton()->has_setting(setting_name));
	CHECK_FALSE(bool(Dictionary(call_result.result.get("structuredContent", Dictionary())).get("saved", true)));

	call_result = registry.call_tool("godot.autoload.get_all", Dictionary(), context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	bool found = false;
	for (const Variant &value : Array(Dictionary(call_result.result.get("structuredContent", Dictionary())).get("autoloads", Array()))) {
		const Dictionary autoload = value;
		if (autoload.get("name", String()) == autoload_name) {
			found = true;
			CHECK(bool(autoload.get("singleton", false)));
		}
	}
	CHECK(found);

	Dictionary remove_arguments;
	remove_arguments["name"] = autoload_name;
	call_result = registry.call_tool("godot.autoload.remove", remove_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting(setting_name));

	provider->unregister_tools();
	memdelete(provider);
}

} // namespace TestMCPProjectProvider
