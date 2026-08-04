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
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/os/os.h"
#include "editor/mcp/mcp_editor_feature.h"
#include "editor/mcp/providers/mcp_autoload_provider.h"
#include "editor/mcp/providers/mcp_project_provider.h"
#include "editor/mcp/providers/mcp_variant_codec.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

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

class ScopedProjectResourcePath {
	String previous_path;
	String resource_path;

public:
	ScopedProjectResourcePath() {
		previous_path = TestProjectSettingsInternalsAccessor::resource_path();
		resource_path = TestUtils::get_temp_path("mcp_project_apply_" + String::num_int64(OS::get_singleton()->get_process_id()) + "_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
		DirAccess::make_dir_recursive_absolute(resource_path);
		TestProjectSettingsInternalsAccessor::resource_path() = resource_path;
	}

	~ScopedProjectResourcePath() {
		const String project_file = resource_path.path_join("project.godot");
		if (FileAccess::exists(project_file)) {
			DirAccess::remove_absolute(project_file);
		}
		TestProjectSettingsInternalsAccessor::resource_path() = previous_path;
	}

	bool is_valid() const {
		return !resource_path.is_empty();
	}

	String project_file() const {
		return resource_path.path_join("project.godot");
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
	REQUIRE(names.size() == 10);
	CHECK(names[0] == "godot.project.inspect");
	CHECK(names[1] == "godot.project.get_settings");
	CHECK(names[5] == "godot.project.apply");
	CHECK(names[6] == "godot.project.save");
	CHECK(names[7] == "godot.autoload.get_all");
	CHECK(names[9] == "godot.autoload.remove");

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
	REQUIRE(names.size() == 7);
	CHECK(names[0] == "godot.project.inspect");
	CHECK(names[1] == "godot.project.get_settings");
	CHECK(names[5] == "godot.project.apply");
	CHECK(names[6] == "godot.project.save");
	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 7);
	const Dictionary inspect_input = Dictionary(definitions[0]).get("inputSchema", Dictionary());
	CHECK_FALSE(bool(inspect_input.get("additionalProperties", true)));
	const Dictionary inspect_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK_FALSE(bool(inspect_output.get("additionalProperties", true)));
	CHECK(PackedStringArray(inspect_output.get("required", PackedStringArray())).has("projectRevision"));
	CHECK(PackedStringArray(inspect_output.get("required", PackedStringArray())).has("dedicatedTools"));
	const Dictionary settings_output = Dictionary(definitions[1]).get("outputSchema", Dictionary());
	CHECK(Dictionary(settings_output.get("properties", Dictionary())).has("settings"));
	CHECK(PackedStringArray(settings_output.get("required", PackedStringArray())).has("matchedCount"));
	CHECK_FALSE(bool(settings_output.get("additionalProperties", true)));
	const Dictionary setting_input = Dictionary(definitions[2]).get("inputSchema", Dictionary());
	const Dictionary setting_name_schema = Dictionary(setting_input.get("properties", Dictionary())).get("name", Dictionary());
	CHECK(int(setting_name_schema.get("minLength", 0)) == 1);
	const Dictionary setting_output = Dictionary(definitions[2]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(setting_output.get("required", PackedStringArray())).has("encodable"));
	const Dictionary set_output = Dictionary(definitions[3]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(set_output.get("required", PackedStringArray())).has("changed"));
	const Dictionary apply_input = Dictionary(definitions[5]).get("inputSchema", Dictionary());
	CHECK_FALSE(bool(apply_input.get("additionalProperties", true)));
	CHECK(Dictionary(apply_input.get("properties", Dictionary())).has("settings"));
	const Dictionary apply_output = Dictionary(definitions[5]).get("outputSchema", Dictionary());
	CHECK_FALSE(bool(apply_output.get("additionalProperties", true)));
	CHECK(PackedStringArray(apply_output.get("required", PackedStringArray())).has("projectRevision"));
	const Dictionary apply_output_revision = Dictionary(apply_output.get("properties", Dictionary())).get("projectRevision", Dictionary());
	CHECK(apply_output_revision.get("type", String()) == "string");
	CHECK(apply_output_revision.get("pattern", String()) == "^[0-9a-f]{64}$");
	const Dictionary apply_input_revision = Dictionary(apply_input.get("properties", Dictionary())).get("expectedProjectRevision", Dictionary());
	CHECK(apply_input_revision.get("type", String()) == "string");
	CHECK(apply_input_revision.get("pattern", String()) == "^[0-9a-fA-F]{64}$");
	const Dictionary save_output = Dictionary(definitions[6]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(save_output.get("required", PackedStringArray())).has("path"));

	const String suffix = String::num_int64(OS::get_singleton()->get_process_id()) + "_" + String::num_uint64(OS::get_singleton()->get_ticks_usec());
	const String setting_name = "mcp_test/settings_" + suffix;
	ScopedSettingRestore restore(setting_name);
	MCPToolCallContext context;
	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.project.inspect", Dictionary(), context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Dictionary inspect_result = call_result.result.get("structuredContent", Dictionary());
	CHECK(inspect_result.get("projectPath", String()) == "res://project.godot");
	CHECK(String(inspect_result.get("projectRevision", String())).length() == 64);
	CHECK(PackedStringArray(inspect_result.get("dedicatedTools", PackedStringArray())).has("godot.input.set_action"));
	Dictionary unexpected_arguments;
	unexpected_arguments["unexpected"] = true;
	CHECK(_error_code(registry.call_tool("godot.project.save", unexpected_arguments, context)) == "INVALID_ARGUMENTS");

	Dictionary set_arguments;
	set_arguments["name"] = setting_name;
	Variant encoded_value;
	REQUIRE(MCPVariantCodec::encode(Variant("res://configured.gd"), encoded_value) == OK);
	CHECK(encoded_value == Variant("res://configured.gd"));
	set_arguments["value"] = encoded_value;
	call_result = registry.call_tool("godot.project.set_setting", set_arguments, context);
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

TEST_CASE("[MCP][Provider] Project apply batches ordinary settings and rejects partial managed mutations") {
	MCPToolRegistry registry;
	MCPProjectProvider *provider = memnew(MCPProjectProvider);
	REQUIRE(provider->register_tools(&registry) == OK);

	const String suffix = String::num_int64(OS::get_singleton()->get_process_id()) + "_" + String::num_uint64(OS::get_singleton()->get_ticks_usec());
	const String first_name = "mcp_test/apply_first_" + suffix;
	const String second_name = "mcp_test/apply_second_" + suffix;
	ScopedSettingRestore restore_first(first_name);
	ScopedSettingRestore restore_second(second_name);
	MCPToolCallContext context;

	Variant encoded_first;
	Variant encoded_second;
	REQUIRE(MCPVariantCodec::encode(Variant(17), encoded_first) == OK);
	REQUIRE(MCPVariantCodec::encode(Variant("batch"), encoded_second) == OK);
	Dictionary set_first;
	set_first["operation"] = "set";
	set_first["name"] = first_name;
	set_first["value"] = encoded_first;
	Dictionary set_second;
	set_second["operation"] = "set";
	set_second["name"] = second_name;
	set_second["value"] = encoded_second;
	Array operations;
	operations.push_back(set_first);
	operations.push_back(set_second);
	Dictionary arguments;
	arguments["settings"] = operations;
	arguments["save"] = false;

	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.project.apply", arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Dictionary result = call_result.result.get("structuredContent", Dictionary());
	CHECK(bool(result.get("changed", false)));
	CHECK_FALSE(bool(result.get("saved", true)));
	CHECK_FALSE(bool(result.get("verified", true)));
	CHECK(ProjectSettings::get_singleton()->get_setting(first_name) == Variant(17));
	CHECK(ProjectSettings::get_singleton()->get_setting(second_name) == Variant("batch"));
	CHECK(Array(result.get("operations", Array())).size() == 2);

	Dictionary erase_first;
	erase_first["operation"] = "erase";
	erase_first["name"] = first_name;
	Array erase_operations;
	erase_operations.push_back(erase_first);
	Dictionary erase_arguments;
	erase_arguments["settings"] = erase_operations;
	erase_arguments["save"] = false;
	erase_arguments["expectedProjectRevision"] = result.get("projectRevision", Variant());
	call_result = registry.call_tool("godot.project.apply", erase_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Dictionary erase_result = call_result.result.get("structuredContent", Dictionary());
	CHECK(erase_result.get("projectRevision", Variant()) != result.get("projectRevision", Variant()));
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting(first_name));
	Dictionary stale_arguments = erase_arguments.duplicate(true);
	stale_arguments["expectedProjectRevision"] = result.get("projectRevision", Variant());
	call_result = registry.call_tool("godot.project.apply", stale_arguments, context);
	CHECK(_error_code(call_result) == "PROJECT_REVISION_CONFLICT");

	Dictionary invalid_set;
	invalid_set["operation"] = "set";
	invalid_set["name"] = "mcp_test/should_not_apply_" + suffix;
	invalid_set["value"] = encoded_first;
	Dictionary managed_set;
	managed_set["operation"] = "set";
	managed_set["name"] = "input/mcp_apply_managed_" + suffix;
	managed_set["value"] = encoded_first;
	Array invalid_operations;
	invalid_operations.push_back(invalid_set);
	invalid_operations.push_back(managed_set);
	Dictionary invalid_arguments;
	invalid_arguments["settings"] = invalid_operations;
	invalid_arguments["save"] = false;
	call_result = registry.call_tool("godot.project.apply", invalid_arguments, context);
	CHECK(_error_code(call_result) == "MANAGED_SETTING");
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting(invalid_set["name"]));

	provider->unregister_tools();
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] Project apply detects revision conflicts and verifies saves") {
	MCPToolRegistry registry;
	MCPProjectProvider *provider = memnew(MCPProjectProvider);
	REQUIRE(provider->register_tools(&registry) == OK);

	const String suffix = String::num_int64(OS::get_singleton()->get_process_id()) + "_" + String::num_uint64(OS::get_singleton()->get_ticks_usec());
	const String setting_name = "mcp_test/apply_save_" + suffix;
	ScopedSettingRestore restore(setting_name);
	MCPToolCallContext context;
	Variant encoded_value;
	REQUIRE(MCPVariantCodec::encode(Variant("saved"), encoded_value) == OK);
	Dictionary operation;
	operation["operation"] = "set";
	operation["name"] = setting_name;
	operation["value"] = encoded_value;
	Array operations;
	operations.push_back(operation);

	Dictionary conflict_arguments;
	conflict_arguments["settings"] = operations;
	conflict_arguments["save"] = false;
	conflict_arguments["expectedProjectRevision"] = String("0").repeat(64);
	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.project.apply", conflict_arguments, context);
	CHECK(_error_code(call_result) == "PROJECT_REVISION_CONFLICT");
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting(setting_name));

	ScopedProjectResourcePath temporary_project;
	REQUIRE(temporary_project.is_valid());
	Dictionary save_arguments;
	save_arguments["settings"] = operations;
	call_result = registry.call_tool("godot.project.apply", save_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Dictionary save_result = call_result.result.get("structuredContent", Dictionary());
	CHECK(bool(save_result.get("changed", false)));
	CHECK(bool(save_result.get("saved", false)));
	CHECK(bool(save_result.get("verified", false)));
	CHECK(FileAccess::exists(temporary_project.project_file()));
	CHECK(FileAccess::open(temporary_project.project_file(), FileAccess::READ).is_valid());

	provider->unregister_tools();
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
