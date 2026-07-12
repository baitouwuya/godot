/**************************************************************************/
/*  test_mcp_resource_provider.cpp                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_loader.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/os/os.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/import/resource_importer_csv_translation.h"
#include "editor/mcp/providers/mcp_resource_provider.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_resource_provider);

namespace TestMCPResourceProvider {

class TestResourceImporter : public ResourceImporter {
public:
	String get_importer_name() const override { return "mcp_test_resource"; }
	String get_visible_name() const override { return "MCP Test Resource"; }
	void get_recognized_extensions(List<String> *p_extensions) const override { p_extensions->push_back("mcpimport"); }
	String get_save_extension() const override { return "res"; }
	String get_resource_type() const override { return "Resource"; }
	float get_priority() const override { return 100.0f; }
	int get_preset_count() const override { return 2; }
	String get_preset_name(int p_idx) const override { return p_idx == 0 ? "Disabled" : "Enabled"; }

	void get_import_options(const String &, List<ImportOption> *r_options, int p_preset) const override {
		r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), p_preset == 1));
		r_options->push_back(ImportOption(PropertyInfo(Variant::INT, "detail", PROPERTY_HINT_RANGE, "0,10,1"), 4));
	}

	bool get_option_visibility(const String &, const String &p_option, const HashMap<StringName, Variant> &p_options) const override {
		if (p_option != "detail") {
			return true;
		}
		const Variant *enabled = p_options.getptr(SNAME("enabled"));
		return enabled && bool(*enabled);
	}

	Error import(ResourceUID::ID, const String &, const String &, const HashMap<StringName, Variant> &, List<String> *, List<String> *, Variant *) override {
		return ERR_UNAVAILABLE;
	}
};

class ScopedImporterRegistration {
	Ref<ResourceImporter> importer;

public:
	ScopedImporterRegistration() {
		importer = memnew(TestResourceImporter);
		ResourceFormatImporter::get_singleton()->add_importer(importer, true);
	}

	~ScopedImporterRegistration() {
		ResourceFormatImporter::get_singleton()->remove_importer(importer);
	}
};

class ScopedCSVImporterRegistration {
	Ref<ResourceImporter> importer;
	bool registered = false;

public:
	ScopedCSVImporterRegistration() {
		if (ResourceFormatImporter::get_singleton()->get_importer_by_name("csv_translation").is_null()) {
			importer = memnew(ResourceImporterCSVTranslation);
			ResourceFormatImporter::get_singleton()->add_importer(importer, true);
			registered = true;
		}
	}

	~ScopedCSVImporterRegistration() {
		if (registered) {
			ResourceFormatImporter::get_singleton()->remove_importer(importer);
		}
	}
};

class ScopedEditorFileSystem {
	ResourceLoaderImport previous_resource_import = nullptr;
	ResourceFormatImporterLoadOnStartup previous_load_on_startup = nullptr;
	EditorFileSystem *owned_file_system = nullptr;

public:
	ScopedEditorFileSystem() {
		if (!EditorFileSystem::get_singleton()) {
			previous_resource_import = ResourceLoader::import;
			previous_load_on_startup = ResourceImporter::load_on_startup;
			owned_file_system = memnew(EditorFileSystem);
		}
	}

	~ScopedEditorFileSystem() {
		if (owned_file_system) {
			memdelete(owned_file_system);
			ResourceLoader::import = previous_resource_import;
			ResourceImporter::load_on_startup = previous_load_on_startup;
		}
	}

	EditorFileSystem *get() const {
		return EditorFileSystem::get_singleton();
	}
};

class ScopedProjectDefault {
	String setting_name = "importer_defaults/mcp_test_resource";
	Variant previous_value;
	bool existed = false;

public:
	ScopedProjectDefault() {
		existed = ProjectSettings::get_singleton()->has_setting(setting_name);
		if (existed) {
			previous_value = GLOBAL_GET(setting_name);
		}
		Dictionary defaults;
		defaults["enabled"] = true;
		ProjectSettings::get_singleton()->set(setting_name, defaults);
	}

	~ScopedProjectDefault() {
		ProjectSettings::get_singleton()->set(setting_name, existed ? previous_value : Variant());
	}
};

class ScopedReadOnlyFile {
	String path;

public:
	explicit ScopedReadOnlyFile(const String &p_path) :
			path(p_path) {
	}

	~ScopedReadOnlyFile() {
		if (!path.is_empty() && FileAccess::exists(path)) {
			FileAccess::set_read_only_attribute(path, false);
		}
	}
};

class ScopedFileRemoval {
	String path;

public:
	explicit ScopedFileRemoval(const String &p_path) :
			path(p_path) {
		remove();
	}

	~ScopedFileRemoval() {
		remove();
	}

	void remove() {
		if (FileAccess::exists(path)) {
			DirAccess::remove_absolute(path);
		}
	}
};

class ScopedImportArtifacts {
	HashSet<String> paths;

	static void _remove(const String &p_path) {
		const ResourceUID::ID uid = ResourceUID::get_singleton()->get_path_id(p_path);
		if (uid != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->get_id_path(uid) == p_path) {
			ResourceUID::get_singleton()->remove_id(uid);
		}
		if (FileAccess::exists(p_path)) {
			FileAccess::set_read_only_attribute(p_path, false);
			DirAccess::remove_absolute(p_path);
		}
	}

public:
	~ScopedImportArtifacts() {
		for (const String &path : paths) {
			_remove(path);
		}
	}

	void add(const String &p_path) {
		paths.insert(p_path);
		_remove(p_path);
	}
};

struct FakeImportState {
	String project_root;
	HashMap<StringName, Variant> options;
	String importer;
	PackedStringArray generated_resource_paths;
	Error result = OK;
	int calls = 0;
};

static FakeImportState fake_import_state;

static Error _write_text(const String &p_path, const String &p_text) {
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return open_error == OK ? ERR_CANT_OPEN : open_error;
	}
	return file->store_string(p_text) ? OK : ERR_FILE_CANT_WRITE;
}

static Error _fake_import(const String &p_target_path, const HashMap<StringName, Variant> &p_options, const String &p_importer) {
	fake_import_state.calls++;
	fake_import_state.options = p_options;
	fake_import_state.importer = p_importer;
	const String target_absolute_path = fake_import_state.project_root.path_join(p_target_path.trim_prefix("res://"));
	Array generated_files;
	for (const String &resource_path : fake_import_state.generated_resource_paths) {
		generated_files.push_back(resource_path);
		if (resource_path.begins_with("res://")) {
			const String generated_absolute_path = fake_import_state.project_root.path_join(resource_path.trim_prefix("res://"));
			const Error directory_error = DirAccess::make_dir_recursive_absolute(generated_absolute_path.get_base_dir());
			if (directory_error != OK) {
				return directory_error;
			}
			const Error write_error = _write_text(generated_absolute_path, "partial generated product");
			if (write_error != OK) {
				return write_error;
			}
		}
	}
	String metadata = "[remap]\n\nimporter=\"mcp_test_resource\"\ntype=\"Resource\"\npath=\"res://.godot/imported/mcp-test.res\"\n\n[deps]\n\nsource_file=" + Variant(p_target_path).get_construct_string() + "\n";
	if (!generated_files.is_empty()) {
		metadata += "dest_files=" + Variant(generated_files).get_construct_string() + "\n";
	}
	Error error = _write_text(target_absolute_path + ".import", metadata);
	if (error != OK) {
		return error;
	}
	if (fake_import_state.result != OK) {
		error = _write_text(target_absolute_path + ".unwrap_cache", "partial product");
		if (error != OK) {
			return error;
		}
	}
	return fake_import_state.result;
}

static void _fake_scan() {
}

static String _error_code(const MCPToolRegistry::CallResult &p_result) {
	const Dictionary structured = p_result.result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return error.get("code", String());
}

static Dictionary _error_details(const MCPToolRegistry::CallResult &p_result) {
	const Dictionary structured = p_result.result.get("structuredContent", Dictionary());
	const Dictionary error = structured.get("error", Dictionary());
	return error.get("details", Dictionary());
}

static Dictionary _find_option(const Array &p_options, const String &p_name) {
	for (int i = 0; i < p_options.size(); i++) {
		const Dictionary option = p_options[i];
		const Dictionary property = option.get("property", Dictionary());
		if (property.get("name", String()) == p_name) {
			return option;
		}
	}
	return Dictionary();
}

static bool _array_has_string(const Array &p_values, const String &p_expected) {
	for (int i = 0; i < p_values.size(); i++) {
		if ((p_values[i].get_type() == Variant::STRING || p_values[i].get_type() == Variant::STRING_NAME) && String(p_values[i]) == p_expected) {
			return true;
		}
	}
	return false;
}

struct TestPaths {
	Ref<DirAccess> temporary_directory;
	String root;
	String project_root;
	String source_path;
};

static TestPaths _make_test_paths() {
	TestPaths paths;
	Error temp_error = OK;
	paths.temporary_directory = DirAccess::create_temp("mcp-resource-provider", false, &temp_error);
	if (temp_error != OK || paths.temporary_directory.is_null()) {
		return paths;
	}
	paths.root = paths.temporary_directory->get_current_dir();
	paths.temporary_directory->make_dir_recursive("external");
	paths.temporary_directory->make_dir_recursive("project");
	paths.project_root = paths.root.path_join("project");
	paths.source_path = paths.root.path_join("external/asset.mcpimport");
	_write_text(paths.source_path, "external source");
	return paths;
}

TEST_CASE("[MCP][Provider] Resource options expose candidates, presets, PropertyInfo, defaults, overrides, and visibility") {
	REQUIRE(ResourceFormatImporter::get_singleton() != nullptr);
	ScopedImporterRegistration importer_registration;
	ScopedProjectDefault project_default;
	TestPaths paths = _make_test_paths();
	REQUIRE(paths.temporary_directory.is_valid());

	MCPToolRegistry registry;
	MCPResourceProvider *provider = memnew(MCPResourceProvider(paths.project_root, _fake_import, _fake_scan));
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);
	const PackedStringArray names = registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(names.size() == 2);
	CHECK(names[0] == "godot.resource.import_options");
	CHECK(names[1] == "godot.resource.import");
	CHECK(registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI).is_empty());

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	Dictionary arguments;
	arguments["source"] = paths.source_path;
	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.resource.import_options", arguments, context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Dictionary default_result = call_result.result.get("structuredContent", Dictionary());
	CHECK(default_result.get("selectedImporter", String()) == "mcp_test_resource");
	CHECK(bool(default_result.get("projectDefaultsApplied", false)));
	REQUIRE(Array(default_result.get("importers", Array())).size() == 1);
	const Array default_options = default_result.get("options", Array());
	const Dictionary enabled_default = _find_option(default_options, "enabled");
	const Dictionary detail_default = _find_option(default_options, "detail");
	CHECK_FALSE(bool(enabled_default.get("defaultValue", true)));
	CHECK(bool(enabled_default.get("value", false)));
	CHECK_FALSE(bool(enabled_default.get("overridden", true)));
	CHECK(bool(detail_default.get("visible", false)));

	Dictionary overrides;
	overrides["enabled"] = false;
	arguments["options"] = overrides;
	call_result = registry.call_tool("godot.resource.import_options", arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Array overridden_options = Dictionary(call_result.result.get("structuredContent", Dictionary())).get("options", Array());
	const Dictionary enabled_override = _find_option(overridden_options, "enabled");
	const Dictionary detail_override = _find_option(overridden_options, "detail");
	CHECK_FALSE(bool(enabled_override.get("value", true)));
	CHECK(bool(enabled_override.get("overridden", false)));
	CHECK_FALSE(bool(detail_override.get("visible", true)));

	arguments.erase("options");
	arguments["preset"] = 1;
	call_result = registry.call_tool("godot.resource.import_options", arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Dictionary preset_result = call_result.result.get("structuredContent", Dictionary());
	CHECK_FALSE(bool(preset_result.get("projectDefaultsApplied", true)));
	CHECK(bool(_find_option(Array(preset_result.get("options", Array())), "enabled").get("value", false)));

	arguments.erase("preset");
	arguments["importer"] = "missing_importer";
	call_result = registry.call_tool("godot.resource.import_options", arguments, context);
	CHECK(_error_code(call_result) == "UNKNOWN_IMPORTER");

	arguments.erase("importer");
	arguments["options"] = Array();
	call_result = registry.call_tool("godot.resource.import_options", arguments, context);
	CHECK(_error_code(call_result) == "INVALID_OPTIONS");

	provider->unregister_tools();
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] EditorFileSystem imports CSV resources with default and custom options") {
	REQUIRE(ResourceFormatImporter::get_singleton() != nullptr);
	ScopedCSVImporterRegistration importer_registration;
	ScopedEditorFileSystem editor_file_system;
	REQUIRE(editor_file_system.get() != nullptr);

	TestPaths paths = _make_test_paths();
	REQUIRE(paths.temporary_directory.is_valid());
	const String default_source = paths.root.path_join("external/default.csv");
	const String custom_source = paths.root.path_join("external/custom.csv");
	REQUIRE(_write_text(default_source, "key,en\nhello,Hello\n") == OK);
	REQUIRE(_write_text(custom_source, "key;en\nhello;Hello\n") == OK);

	const String unique_suffix = String::num_int64(OS::get_singleton()->get_process_id()) + "_" + String::num_uint64(OS::get_singleton()->get_ticks_usec());
	const String default_target = "res://.godot/mcp_tests/csv_default_" + unique_suffix + ".csv";
	const String custom_target = "res://.godot/mcp_tests/csv_custom_" + unique_suffix + ".csv";
	const String default_generated = default_target.get_basename() + ".en.translation";
	const String custom_generated = custom_target.get_basename() + ".en.translation";
	const String default_import_base = ResourceFormatImporter::get_singleton()->get_import_base_path(default_target);
	const String custom_import_base = ResourceFormatImporter::get_singleton()->get_import_base_path(custom_target);

	ScopedImportArtifacts artifacts;
	const String artifact_paths[] = {
		default_target,
		default_target + ".import",
		default_target + ".uid",
		default_generated,
		default_generated + ".uid",
		default_import_base + ".md5",
		custom_target,
		custom_target + ".import",
		custom_target + ".uid",
		custom_generated,
		custom_generated + ".uid",
		custom_import_base + ".md5",
	};
	for (const String &path : artifact_paths) {
		artifacts.add(path);
	}
	REQUIRE(DirAccess::make_dir_recursive_absolute(default_target.get_base_dir()) == OK);
	REQUIRE(DirAccess::copy_absolute(default_source, default_target) == OK);
	REQUIRE(DirAccess::copy_absolute(custom_source, custom_target) == OK);

	const HashMap<StringName, Variant> default_options;
	REQUIRE(editor_file_system.get()->import_file_with_custom_options(default_target, default_options, "csv_translation") == OK);
	CHECK(FileAccess::exists(default_target + ".import"));
	CHECK(FileAccess::exists(default_import_base + ".md5"));
	CHECK(FileAccess::exists(default_generated));
	Ref<ConfigFile> default_config;
	default_config.instantiate();
	REQUIRE(default_config->load(default_target + ".import") == OK);
	CHECK(default_config->get_value("remap", "importer", String()) == "csv_translation");
	CHECK(int(default_config->get_value("params", "delimiter", -1)) == 0);
	CHECK(_array_has_string(default_config->get_value("deps", "dest_files", Array()), default_generated));

	HashMap<StringName, Variant> custom_options;
	custom_options.insert(SNAME("delimiter"), 1);
	REQUIRE(editor_file_system.get()->import_file_with_custom_options(custom_target, custom_options, "csv_translation") == OK);
	CHECK(FileAccess::exists(custom_target + ".import"));
	CHECK(FileAccess::exists(custom_import_base + ".md5"));
	CHECK(FileAccess::exists(custom_generated));
	Ref<ConfigFile> custom_config;
	custom_config.instantiate();
	REQUIRE(custom_config->load(custom_target + ".import") == OK);
	CHECK(custom_config->get_value("remap", "importer", String()) == "csv_translation");
	CHECK(int(custom_config->get_value("params", "delimiter", -1)) == 1);
	CHECK(_array_has_string(custom_config->get_value("deps", "dest_files", Array()), custom_generated));
}

TEST_CASE("[MCP][Provider] Resource import reads an external source, validates targets, and enforces overwrite") {
	REQUIRE(ResourceFormatImporter::get_singleton() != nullptr);
	ScopedImporterRegistration importer_registration;
	TestPaths paths = _make_test_paths();
	REQUIRE(paths.temporary_directory.is_valid());
	ScopedReadOnlyFile read_only_reset(paths.source_path);
	REQUIRE(FileAccess::set_read_only_attribute(paths.source_path, true) == OK);

	fake_import_state = FakeImportState();
	fake_import_state.project_root = paths.project_root;
	MCPToolRegistry registry;
	MCPResourceProvider *provider = memnew(MCPResourceProvider(paths.project_root, _fake_import, _fake_scan));
	REQUIRE(provider->register_tools(&registry) == OK);

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	Dictionary arguments;
	arguments["source"] = paths.source_path;
	const String orphan_target = "res://assets/orphan.mcpimport";
	const String orphan_md5 = ResourceFormatImporter::get_singleton()->get_import_base_path(orphan_target) + ".md5";
	ScopedFileRemoval orphan_cleanup(orphan_md5);
	REQUIRE(DirAccess::make_dir_recursive_absolute(orphan_md5.get_base_dir()) == OK);
	REQUIRE(_write_text(orphan_md5, "orphan import product") == OK);
	arguments["target"] = orphan_target;
	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.resource.import", arguments, context);
	CHECK(_error_code(call_result) == "TARGET_EXISTS");
	CHECK(fake_import_state.calls == 0);

	arguments["target"] = "res://assets/imported.mcpimport";
	call_result = registry.call_tool("godot.resource.import", arguments, context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const String target_absolute_path = paths.project_root.path_join("assets/imported.mcpimport");
	CHECK(FileAccess::get_file_as_string(paths.source_path) == "external source");
	CHECK(FileAccess::get_read_only_attribute(paths.source_path));
	CHECK(FileAccess::get_file_as_string(target_absolute_path) == "external source");
	CHECK(fake_import_state.calls == 1);
	CHECK(fake_import_state.importer == "mcp_test_resource");
	const Variant *enabled_option = fake_import_state.options.getptr(SNAME("enabled"));
	REQUIRE(enabled_option != nullptr);
	CHECK(bool(*enabled_option) == false);

	call_result = registry.call_tool("godot.resource.import", arguments, context);
	CHECK(_error_code(call_result) == "TARGET_EXISTS");
	CHECK(fake_import_state.calls == 1);

	arguments["target"] = "res://assets/imported.other";
	call_result = registry.call_tool("godot.resource.import", arguments, context);
	CHECK(_error_code(call_result) == "TARGET_EXTENSION_MISMATCH");
	arguments["target"] = "res://../escape.mcpimport";
	call_result = registry.call_tool("godot.resource.import", arguments, context);
	CHECK(_error_code(call_result) == "INVALID_TARGET");

	arguments["target"] = "res://assets/imported.mcpimport";
	arguments["overwrite"] = true;
	call_result = registry.call_tool("godot.resource.import", arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	CHECK(bool(Dictionary(call_result.result.get("structuredContent", Dictionary())).get("overwritten", false)));
	CHECK(fake_import_state.calls == 2);

	provider->unregister_tools();
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] Failed resource import restores target metadata and partial products") {
	REQUIRE(ResourceFormatImporter::get_singleton() != nullptr);
	ScopedImporterRegistration importer_registration;
	TestPaths paths = _make_test_paths();
	REQUIRE(paths.temporary_directory.is_valid());
	ScopedReadOnlyFile read_only_reset(paths.source_path);
	REQUIRE(FileAccess::set_read_only_attribute(paths.source_path, true) == OK);

	const String target_absolute_path = paths.project_root.path_join("assets/imported.mcpimport");
	REQUIRE(DirAccess::make_dir_recursive_absolute(target_absolute_path.get_base_dir()) == OK);
	REQUIRE(_write_text(target_absolute_path, "original target") == OK);
	REQUIRE(_write_text(target_absolute_path + ".import", "original metadata") == OK);

	fake_import_state = FakeImportState();
	fake_import_state.project_root = paths.project_root;
	fake_import_state.result = ERR_CANT_CREATE;
	MCPToolRegistry registry;
	MCPResourceProvider *provider = memnew(MCPResourceProvider(paths.project_root, _fake_import, _fake_scan));
	REQUIRE(provider->register_tools(&registry) == OK);

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	Dictionary arguments;
	arguments["source"] = paths.source_path;
	arguments["target"] = "res://assets/imported.mcpimport";
	arguments["overwrite"] = true;
	const MCPToolRegistry::CallResult call_result = registry.call_tool("godot.resource.import", arguments, context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	CHECK(_error_code(call_result) == "IMPORT_FAILED");
	CHECK(FileAccess::get_file_as_string(paths.source_path) == "external source");
	CHECK(FileAccess::get_read_only_attribute(paths.source_path));
	CHECK(FileAccess::get_file_as_string(target_absolute_path) == "original target");
	CHECK(FileAccess::get_file_as_string(target_absolute_path + ".import") == "original metadata");
	CHECK_FALSE(FileAccess::exists(target_absolute_path + ".unwrap_cache"));

	provider->unregister_tools();
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] Failed resource import restores owned products and removes newly reported products") {
	REQUIRE(ResourceFormatImporter::get_singleton() != nullptr);
	ScopedImporterRegistration importer_registration;
	TestPaths paths = _make_test_paths();
	REQUIRE(paths.temporary_directory.is_valid());

	const String target_resource_path = "res://assets/imported.mcpimport";
	const String target_absolute_path = paths.project_root.path_join("assets/imported.mcpimport");
	const String owned_resource_path = "res://generated/owned.translation";
	const String owned_absolute_path = paths.project_root.path_join("generated/owned.translation");
	const String residual_resource_path = "res://generated/first_seen.translation";
	const String residual_absolute_path = paths.project_root.path_join("generated/first_seen.translation");
	REQUIRE(DirAccess::make_dir_recursive_absolute(target_absolute_path.get_base_dir()) == OK);
	REQUIRE(DirAccess::make_dir_recursive_absolute(owned_absolute_path.get_base_dir()) == OK);
	REQUIRE(_write_text(target_absolute_path, "original target") == OK);
	REQUIRE(_write_text(owned_absolute_path, "original generated product") == OK);
	Array old_dest_files;
	old_dest_files.push_back(owned_resource_path);
	const String old_metadata = "[remap]\n\nimporter=\"mcp_test_resource\"\n\n[deps]\n\ndest_files=" + Variant(old_dest_files).get_construct_string() + "\n";
	REQUIRE(_write_text(target_absolute_path + ".import", old_metadata) == OK);

	fake_import_state = FakeImportState();
	fake_import_state.project_root = paths.project_root;
	fake_import_state.generated_resource_paths.push_back(owned_resource_path);
	fake_import_state.generated_resource_paths.push_back(residual_resource_path);
	fake_import_state.result = ERR_CANT_CREATE;
	MCPToolRegistry registry;
	MCPResourceProvider *provider = memnew(MCPResourceProvider(paths.project_root, _fake_import, _fake_scan));
	REQUIRE(provider->register_tools(&registry) == OK);

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	Dictionary arguments;
	arguments["source"] = paths.source_path;
	arguments["target"] = target_resource_path;
	arguments["overwrite"] = true;
	const MCPToolRegistry::CallResult call_result = registry.call_tool("godot.resource.import", arguments, context);
	CHECK(_error_code(call_result) == "IMPORT_FAILED");
	CHECK(FileAccess::get_file_as_string(target_absolute_path) == "original target");
	CHECK(FileAccess::get_file_as_string(target_absolute_path + ".import") == old_metadata);
	CHECK(FileAccess::get_file_as_string(owned_absolute_path) == "original generated product");
	CHECK_FALSE(FileAccess::exists(residual_absolute_path));
	const Dictionary details = _error_details(call_result);
	CHECK(bool(details.get("rollbackSucceeded", false)));
	CHECK(bool(details.get("rollbackComplete", false)));
	CHECK_FALSE(details.has("possibleResidualProducts"));

	provider->unregister_tools();
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] Failed resource import preserves an unknown preexisting reported product") {
	REQUIRE(ResourceFormatImporter::get_singleton() != nullptr);
	ScopedImporterRegistration importer_registration;
	TestPaths paths = _make_test_paths();
	REQUIRE(paths.temporary_directory.is_valid());

	const String target_resource_path = "res://assets/imported.mcpimport";
	const String target_absolute_path = paths.project_root.path_join("assets/imported.mcpimport");
	const String unknown_resource_path = "res://generated/unknown.translation";
	const String unknown_absolute_path = paths.project_root.path_join("generated/unknown.translation");
	REQUIRE(DirAccess::make_dir_recursive_absolute(target_absolute_path.get_base_dir()) == OK);
	REQUIRE(DirAccess::make_dir_recursive_absolute(unknown_absolute_path.get_base_dir()) == OK);
	REQUIRE(_write_text(target_absolute_path, "original target") == OK);
	const String old_metadata = "[remap]\n\nimporter=\"mcp_test_resource\"\n\n[deps]\n";
	REQUIRE(_write_text(target_absolute_path + ".import", old_metadata) == OK);
	REQUIRE(_write_text(unknown_absolute_path, "preexisting unknown product") == OK);

	fake_import_state = FakeImportState();
	fake_import_state.project_root = paths.project_root;
	fake_import_state.generated_resource_paths.push_back(unknown_resource_path);
	fake_import_state.result = ERR_CANT_CREATE;
	MCPToolRegistry registry;
	MCPResourceProvider *provider = memnew(MCPResourceProvider(paths.project_root, _fake_import, _fake_scan));
	REQUIRE(provider->register_tools(&registry) == OK);

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	Dictionary arguments;
	arguments["source"] = paths.source_path;
	arguments["target"] = target_resource_path;
	arguments["overwrite"] = true;
	const MCPToolRegistry::CallResult call_result = registry.call_tool("godot.resource.import", arguments, context);
	CHECK(_error_code(call_result) == "IMPORT_FAILED");
	CHECK(FileAccess::get_file_as_string(target_absolute_path) == "original target");
	CHECK(FileAccess::get_file_as_string(target_absolute_path + ".import") == old_metadata);
	CHECK(FileAccess::get_file_as_string(unknown_absolute_path) == "partial generated product");
	const Dictionary details = _error_details(call_result);
	CHECK(bool(details.get("rollbackSucceeded", false)));
	CHECK_FALSE(bool(details.get("rollbackComplete", true)));
	const PackedStringArray possible_residuals = details.get("possibleResidualProducts", PackedStringArray());
	REQUIRE(possible_residuals.size() == 1);
	CHECK(possible_residuals[0] == unknown_resource_path);

	provider->unregister_tools();
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] Resource import rejects unsafe destination paths from existing metadata") {
	REQUIRE(ResourceFormatImporter::get_singleton() != nullptr);
	ScopedImporterRegistration importer_registration;
	TestPaths paths = _make_test_paths();
	REQUIRE(paths.temporary_directory.is_valid());

	const String target_absolute_path = paths.project_root.path_join("assets/imported.mcpimport");
	REQUIRE(DirAccess::make_dir_recursive_absolute(target_absolute_path.get_base_dir()) == OK);
	Array unsafe_dest_files;
	unsafe_dest_files.push_back(paths.root.path_join("outside.res"));
	const String metadata = "[remap]\n\nimporter=\"mcp_test_resource\"\n\n[deps]\n\ndest_files=" + Variant(unsafe_dest_files).get_construct_string() + "\n";
	REQUIRE(_write_text(target_absolute_path + ".import", metadata) == OK);

	fake_import_state = FakeImportState();
	fake_import_state.project_root = paths.project_root;
	MCPToolRegistry registry;
	MCPResourceProvider *provider = memnew(MCPResourceProvider(paths.project_root, _fake_import, _fake_scan));
	REQUIRE(provider->register_tools(&registry) == OK);

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	Dictionary arguments;
	arguments["source"] = paths.source_path;
	arguments["target"] = "res://assets/imported.mcpimport";
	arguments["overwrite"] = true;
	const MCPToolRegistry::CallResult call_result = registry.call_tool("godot.resource.import", arguments, context);
	CHECK(_error_code(call_result) == "UNSAFE_IMPORT_METADATA");
	CHECK(fake_import_state.calls == 0);

	provider->unregister_tools();
	memdelete(provider);
}

} // namespace TestMCPResourceProvider
