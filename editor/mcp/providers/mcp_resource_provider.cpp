/**************************************************************************/
/*  mcp_resource_provider.cpp                                             */
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

#include "mcp_resource_provider.h"

#include "mcp_path_utils.h"
#include "mcp_resource_import_options.h"
#include "mcp_resource_import_transaction.h"
#include "mcp_resource_tool_utils.h"
#include "mcp_tool_utils.h"
#include "mcp_variant_codec.h"

#include "core/error/error_list.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "core/templates/hash_set.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"

namespace {

struct ResourceSelection {
	String resource_path;
	String absolute_path;
	Ref<Resource> resource;
};

static Dictionary _load_project_resource(const Dictionary &p_arguments, const String &p_project_root,
		const HashMap<String, Ref<Resource>> &p_resource_cache, ResourceSelection &r_selection) {
	r_selection = ResourceSelection();
	const Variant path_value = p_arguments.get("path", Variant());
	if (path_value.get_type() != Variant::STRING || String(path_value).is_empty()) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string path is required.");
	}

	String path_error;
	const Error path_result = p_project_root.is_empty() ?
			MCPPathUtils::resolve_project_file_path(path_value, r_selection.resource_path, r_selection.absolute_path, &path_error) :
			MCPPathUtils::resolve_project_file_path_for_root(path_value, p_project_root, r_selection.resource_path, r_selection.absolute_path, &path_error);
	if (path_result != OK) {
		return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
	}
	if (!FileAccess::exists(r_selection.absolute_path)) {
		return MCPToolUtils::make_error_result("RESOURCE_NOT_FOUND", "Resource does not exist: " + r_selection.resource_path);
	}
	if (const Ref<Resource> *cached = p_resource_cache.getptr(r_selection.resource_path)) {
		r_selection.resource = *cached;
		if (r_selection.resource.is_valid()) {
			return Dictionary();
		}
	}

	const String load_path = p_project_root.is_empty() ? r_selection.resource_path : r_selection.absolute_path;
	r_selection.resource = ResourceLoader::load(load_path, String(), ResourceFormatLoader::CACHE_MODE_REUSE);
	if (r_selection.resource.is_null()) {
		return MCPToolUtils::make_error_result("RESOURCE_LOAD_FAILED", "Resource could not be loaded: " + r_selection.resource_path);
	}
	return Dictionary();
}

static bool _find_property(const Ref<Resource> &p_resource, const StringName &p_name, PropertyInfo &r_property) {
	List<PropertyInfo> properties;
	p_resource->get_property_list(&properties, true);
	for (const PropertyInfo &property : properties) {
		if (property.name == p_name) {
			r_property = property;
			return true;
		}
	}
	return false;
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

static Dictionary _validate_source_path(const Dictionary &p_arguments, String &r_source_path) {
	const Variant source_value = p_arguments.get("source", Variant());
	if (source_value.get_type() != Variant::STRING || String(source_value).is_empty()) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string source path is required.");
	}

	r_source_path = String(source_value).replace_char('\\', '/').simplify_path();
	if (!r_source_path.is_absolute_path() || r_source_path.contains("://")) {
		return MCPToolUtils::make_error_result("INVALID_SOURCE", "Source must be an absolute filesystem path.");
	}
	if (!FileAccess::exists(r_source_path)) {
		return MCPToolUtils::make_error_result("SOURCE_NOT_FOUND", "Source file does not exist: " + r_source_path);
	}

	Error open_error = OK;
	Ref<FileAccess> source_file = FileAccess::open(r_source_path, FileAccess::READ, &open_error);
	if (source_file.is_null() || open_error != OK) {
		return MCPToolUtils::make_error_result("SOURCE_NOT_READABLE", "Source file cannot be opened for reading: " + r_source_path);
	}
	return Dictionary();
}

static void _read_import_dest_paths(const String &p_target_path, const String &p_project_root, HashSet<String> &r_resource_paths, HashSet<String> &r_transaction_paths, PackedStringArray *r_unsafe_paths = nullptr, HashMap<String, String> *r_transaction_paths_by_resource = nullptr) {
	r_resource_paths.clear();
	r_transaction_paths.clear();
	if (r_transaction_paths_by_resource) {
		r_transaction_paths_by_resource->clear();
	}
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(p_target_path + ".import") != OK) {
		return;
	}

	const Variant dest_files_value = config->get_value("deps", "dest_files", Array());
	if (dest_files_value.get_type() != Variant::ARRAY) {
		return;
	}
	const Array dest_files = dest_files_value;
	for (int i = 0; i < dest_files.size(); i++) {
		if (dest_files[i].get_type() != Variant::STRING && dest_files[i].get_type() != Variant::STRING_NAME) {
			if (r_unsafe_paths) {
				r_unsafe_paths->push_back("<non-string dest_files entry>");
			}
			continue;
		}
		String normalized_path;
		String absolute_path;
		String path_error;
		const Error path_result = p_project_root.is_empty() ? MCPPathUtils::resolve_project_file_path(dest_files[i], normalized_path, absolute_path, &path_error) : MCPPathUtils::resolve_project_file_path_for_root(dest_files[i], p_project_root, normalized_path, absolute_path, &path_error);
		if (path_result != OK) {
			if (r_unsafe_paths) {
				r_unsafe_paths->push_back(String(dest_files[i]));
			}
			continue;
		}
		r_resource_paths.insert(normalized_path);
		const String transaction_path = p_project_root.is_empty() ? normalized_path : absolute_path;
		r_transaction_paths.insert(transaction_path);
		if (r_transaction_paths_by_resource) {
			r_transaction_paths_by_resource->insert(normalized_path, transaction_path);
		}
	}
}

static HashSet<String> _collect_import_base_files(const String &p_import_base_path) {
	HashSet<String> paths;
	Error open_error = OK;
	Ref<DirAccess> directory = DirAccess::open(p_import_base_path.get_base_dir(), &open_error);
	if (directory.is_null() || open_error != OK) {
		return paths;
	}

	const String prefix = p_import_base_path.get_file();
	for (const String &file : directory->get_files()) {
		if (file.begins_with(prefix)) {
			paths.insert(p_import_base_path.get_base_dir().path_join(file));
		}
	}
	return paths;
}

static Error _protect_paths(MCPResourceImportTransaction &p_transaction, const HashSet<String> &p_paths, String *r_error) {
	for (const String &path : p_paths) {
		const Error error = p_transaction.protect_path(path, r_error);
		if (error != OK) {
			return error;
		}
	}
	return OK;
}

static void _track_new_paths(MCPResourceImportTransaction &p_transaction, const HashSet<String> &p_paths) {
	for (const String &path : p_paths) {
		if (!p_transaction.is_path_protected(path)) {
			p_transaction.track_created_path(path);
		}
	}
}

static HashSet<String> _track_reported_dest_paths(MCPResourceImportTransaction &p_transaction, const HashMap<String, String> &p_transaction_paths_by_resource) {
	HashSet<String> preexisting_unprotected_paths;
	for (const KeyValue<String, String> &entry : p_transaction_paths_by_resource) {
		if (p_transaction.is_path_protected(entry.value) || p_transaction.is_path_tracked_created(entry.value)) {
			continue;
		}
		preexisting_unprotected_paths.insert(entry.key);
	}
	return preexisting_unprotected_paths;
}

static PackedStringArray _paths_to_sorted_array(const HashSet<String> &p_paths) {
	PackedStringArray result;
	for (const String &path : p_paths) {
		result.push_back(path);
	}
	result.sort();
	return result;
}

} // namespace

MCPResourceProvider::MCPResourceProvider(const String &p_project_root, ImportFunction p_import_function, ScanFunction p_scan_function) :
		project_root(p_project_root),
		import_function(p_import_function),
		scan_function(p_scan_function) {
}

MCPResourceProvider::~MCPResourceProvider() {
	unregister_tools();
}

Error MCPResourceProvider::_execute_import(const String &p_target_path, const HashMap<StringName, Variant> &p_options, const String &p_importer) const {
	if (import_function) {
		return import_function(p_target_path, p_options, p_importer);
	}
	EditorFileSystem *editor_file_system = EditorFileSystem::get_singleton();
	return editor_file_system ? editor_file_system->import_file_with_custom_options(p_target_path, p_options, p_importer) : ERR_UNCONFIGURED;
}

void MCPResourceProvider::_scan_changes() const {
	if (scan_function) {
		scan_function();
		return;
	}
	if (EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->scan_changes();
	}
}

Error MCPResourceProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Resource tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
			{ "godot.resource.get_properties", "Get editable Inspector properties from a cached project Resource.",
					MCPResourceToolUtils::path_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPResourceProvider::get_properties), MCPResourceToolUtils::get_properties_output_schema() },
			{ "godot.resource.set_property", "Set a cached project Resource property without saving it.",
					MCPResourceToolUtils::set_property_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPResourceProvider::set_property), MCPResourceToolUtils::set_property_output_schema() },
			{ "godot.resource.save", "Explicitly save a cached project Resource.",
					MCPResourceToolUtils::path_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPResourceProvider::save), MCPResourceToolUtils::save_output_schema() },
			{ "godot.resource.import_options", "List ResourceImporter candidates and effective import options for an external asset.",
					MCPResourceToolUtils::import_options_schema(false), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPResourceProvider::import_options), MCPResourceToolUtils::import_options_output_schema() },
			{ "godot.resource.import", "Copy an external asset into the project, roll back known target-owned artifacts, and report unowned residuals.",
					MCPResourceToolUtils::import_options_schema(true), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPResourceProvider::import_resource), MCPResourceToolUtils::import_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPResourceProvider::unregister_tools() {
	if (!tool_registry) {
		resource_cache.clear();
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
	resource_cache.clear();
}

Dictionary MCPResourceProvider::get_properties(const Dictionary &p_arguments, const Dictionary &) {
	ResourceSelection selection;
	const Dictionary load_error = _load_project_resource(p_arguments, project_root, resource_cache, selection);
	if (!load_error.is_empty()) {
		return load_error;
	}
	resource_cache[selection.resource_path] = selection.resource;

	Array properties;
	List<PropertyInfo> property_list;
	selection.resource->get_property_list(&property_list, true);
	for (const PropertyInfo &property : property_list) {
		if (!(property.usage & PROPERTY_USAGE_EDITOR) || property.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP)) {
			continue;
		}
		Dictionary item = MCPToolUtils::make_property_description(property);
		item["readOnly"] = bool(property.usage & PROPERTY_USAGE_READ_ONLY);
		item["editable"] = !(property.usage & PROPERTY_USAGE_READ_ONLY);
		bool valid = false;
		const Variant value = selection.resource->get(property.name, &valid);
		item["valueAvailable"] = valid;
		Variant encoded_value;
		String encoding_error;
		if (valid && MCPVariantCodec::encode(value, encoded_value, &encoding_error) == OK) {
			item["encodable"] = true;
			item["value"] = encoded_value;
		} else {
			item["encodable"] = false;
			item["encodingError"] = valid ? encoding_error : "Property value is unavailable.";
		}
		properties.push_back(item);
	}

	Dictionary result;
	result["path"] = selection.resource_path;
	result["type"] = selection.resource->get_class();
	result["propertyCount"] = properties.size();
	result["properties"] = properties;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPResourceProvider::set_property(const Dictionary &p_arguments, const Dictionary &) {
	const Variant property_value = p_arguments.get("property", Variant());
	if (property_value.get_type() != Variant::STRING || String(property_value).is_empty() || !p_arguments.has("value")) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty property string and a value are required.");
	}

	ResourceSelection selection;
	const Dictionary load_error = _load_project_resource(p_arguments, project_root, resource_cache, selection);
	if (!load_error.is_empty()) {
		return load_error;
	}
	resource_cache[selection.resource_path] = selection.resource;
	const StringName property_name = String(property_value);
	PropertyInfo property;
	if (!_find_property(selection.resource, property_name, property) || !(property.usage & PROPERTY_USAGE_EDITOR)) {
		return MCPToolUtils::make_error_result("PROPERTY_NOT_FOUND", "Editable Resource property was not found: " + String(property_name));
	}
	if (property.usage & PROPERTY_USAGE_READ_ONLY) {
		return MCPToolUtils::make_error_result("PROPERTY_READ_ONLY", "Resource property is read-only: " + String(property_name));
	}

	Variant decoded_value;
	String codec_error;
	if (MCPVariantCodec::decode(p_arguments["value"], decoded_value, &codec_error) != OK) {
		return MCPToolUtils::make_error_result("INVALID_VALUE", codec_error);
	}
	bool value_valid = false;
	const Variant old_value = selection.resource->get(property_name, &value_valid);
	if (!value_valid) {
		return MCPToolUtils::make_error_result("PROPERTY_GET_FAILED", "Resource property value is unavailable: " + String(property_name));
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	if (undo_redo) {
		undo_redo->create_action("Set Resource Property", UndoRedo::MERGE_DISABLE, selection.resource.ptr());
		undo_redo->add_do_property(selection.resource.ptr(), property_name, decoded_value);
		undo_redo->add_undo_property(selection.resource.ptr(), property_name, old_value);
		undo_redo->commit_action();
	} else {
		selection.resource->set(property_name, decoded_value, &value_valid);
		if (!value_valid) {
			return MCPToolUtils::make_error_result("PROPERTY_SET_FAILED", "Resource property could not be set: " + String(property_name));
		}
	}
	if (EditorNode::get_singleton() && EditorInterface::get_singleton()) {
		EditorInterface::get_singleton()->inspect_object(selection.resource.ptr());
	}

	const Variant effective_value = selection.resource->get(property_name, &value_valid);
	if (!value_valid) {
		return MCPToolUtils::make_error_result("PROPERTY_SET_FAILED", "Resource property could not be read after modification: " + String(property_name));
	}
	Dictionary result;
	result["path"] = selection.resource_path;
	result["property"] = String(property_name);
	result["changed"] = old_value != effective_value;
	result["saved"] = false;
	Variant encoded_old_value;
	if (MCPVariantCodec::encode(old_value, encoded_old_value, &codec_error) == OK) {
		result["oldValue"] = encoded_old_value;
	}
	Variant encoded_value;
	if (MCPVariantCodec::encode(effective_value, encoded_value, &codec_error) == OK) {
		result["value"] = encoded_value;
	} else {
		result["value"] = p_arguments["value"];
		result["valueEncodingError"] = codec_error;
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPResourceProvider::save(const Dictionary &p_arguments, const Dictionary &) {
	ResourceSelection selection;
	const Dictionary load_error = _load_project_resource(p_arguments, project_root, resource_cache, selection);
	if (!load_error.is_empty()) {
		return load_error;
	}
	resource_cache[selection.resource_path] = selection.resource;
	const String save_path = project_root.is_empty() ? selection.resource_path : selection.absolute_path;
	const Error error = ResourceSaver::save(selection.resource, save_path);
	if (error != OK) {
		return MCPToolUtils::make_error_result("SAVE_FAILED", error_names[error]);
	}
	if (project_root.is_empty() && EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->update_file(selection.resource_path);
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	if (undo_redo) {
		const int history_id = undo_redo->get_history_id_for_object(selection.resource.ptr());
		if (history_id != EditorUndoRedoManager::INVALID_HISTORY) {
			undo_redo->set_history_as_saved(history_id);
		}
	}

	Dictionary result;
	result["path"] = selection.resource_path;
	result["type"] = selection.resource->get_class();
	result["saved"] = true;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPResourceProvider::import_options(const Dictionary &p_arguments, const Dictionary &) {
	String source_path;
	const Dictionary source_error = _validate_source_path(p_arguments, source_path);
	if (!source_error.is_empty()) {
		return source_error;
	}

	MCPResourceImportResolution resolution;
	Dictionary resolution_error;
	if (!MCPResourceImportOptions::resolve(p_arguments, source_path, resolution, resolution_error)) {
		return resolution_error;
	}

	Dictionary encoding_error;
	const Dictionary result = MCPResourceImportOptions::make_result(source_path, resolution, encoding_error);
	return encoding_error.is_empty() ? MCPToolUtils::make_success_result(result) : encoding_error;
}

Dictionary MCPResourceProvider::import_resource(const Dictionary &p_arguments, const Dictionary &) {
	String source_path;
	const Dictionary source_error = _validate_source_path(p_arguments, source_path);
	if (!source_error.is_empty()) {
		return source_error;
	}
	const Variant target_value = p_arguments.get("target", Variant());
	const Variant overwrite_value = p_arguments.get("overwrite", false);
	if (target_value.get_type() != Variant::STRING || String(target_value).is_empty() || overwrite_value.get_type() != Variant::BOOL) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string target and optional boolean overwrite are required.");
	}

	String target_path;
	String target_absolute_path;
	String target_error;
	const Error target_result = project_root.is_empty() ? MCPPathUtils::resolve_project_file_path(target_value, target_path, target_absolute_path, &target_error) : MCPPathUtils::resolve_project_file_path_for_root(target_value, project_root, target_path, target_absolute_path, &target_error);
	if (target_result != OK) {
		return MCPToolUtils::make_error_result("INVALID_TARGET", target_error);
	}
	if (source_path.get_extension().nocasecmp_to(target_path.get_extension()) != 0) {
		return MCPToolUtils::make_error_result("TARGET_EXTENSION_MISMATCH", "Source and target must use the same file extension.");
	}
	if (source_path.nocasecmp_to(target_absolute_path.replace_char('\\', '/').simplify_path()) == 0) {
		return MCPToolUtils::make_error_result("INVALID_TARGET", "Source and target must be different files.");
	}

	MCPResourceImportResolution resolution;
	Dictionary resolution_error;
	if (!MCPResourceImportOptions::resolve(p_arguments, source_path, resolution, resolution_error)) {
		return resolution_error;
	}

	if (!import_function && !EditorFileSystem::get_singleton()) {
		return MCPToolUtils::make_error_result("EDITOR_FILESYSTEM_UNAVAILABLE", "EditorFileSystem is not initialized.");
	}

	const String import_base_path = ResourceFormatImporter::get_singleton()->get_import_base_path(target_path);
	const HashSet<String> previous_import_base_files = _collect_import_base_files(import_base_path);
	PackedStringArray unsafe_previous_dest_paths;
	HashSet<String> previous_dest_resource_paths;
	HashSet<String> previous_dest_transaction_paths;
	_read_import_dest_paths(target_absolute_path, project_root, previous_dest_resource_paths, previous_dest_transaction_paths, &unsafe_previous_dest_paths);
	if (!unsafe_previous_dest_paths.is_empty()) {
		unsafe_previous_dest_paths.sort();
		Dictionary details;
		details["destFiles"] = unsafe_previous_dest_paths;
		return MCPToolUtils::make_error_result("UNSAFE_IMPORT_METADATA", "Existing import metadata contains destination paths outside the safe project scope.", details);
	}
	const bool target_existed = FileAccess::exists(target_absolute_path);
	const bool metadata_existed = FileAccess::exists(target_absolute_path + ".import") || FileAccess::exists(target_absolute_path + ".uid");
	const bool source_sidecar_existed = FileAccess::exists(target_absolute_path + ".unwrap_cache");
	const bool import_products_existed = !previous_import_base_files.is_empty() || !previous_dest_resource_paths.is_empty();
	if ((target_existed || metadata_existed || source_sidecar_existed || import_products_existed) && !bool(overwrite_value)) {
		Dictionary details;
		details["targetExists"] = target_existed;
		details["metadataExists"] = metadata_existed;
		details["sourceSidecarExists"] = source_sidecar_existed;
		details["importProducts"] = _paths_to_sorted_array(previous_import_base_files);
		details["destFiles"] = _paths_to_sorted_array(previous_dest_resource_paths);
		return MCPToolUtils::make_error_result("TARGET_EXISTS", "Target or its import products already exist: " + target_path, details);
	}

	const String parent_directory = target_absolute_path.get_base_dir();
	if (!DirAccess::dir_exists_absolute(parent_directory)) {
		const Error directory_error = DirAccess::make_dir_recursive_absolute(parent_directory);
		if (directory_error != OK) {
			return MCPToolUtils::make_error_result("DIRECTORY_CREATE_FAILED", "Could not create target parent directory: " + target_path);
		}
	}

	MCPResourceImportTransaction transaction;
	String transaction_error;
	Error error = transaction.begin(&transaction_error);
	if (error != OK) {
		return MCPToolUtils::make_error_result("TRANSACTION_FAILED", transaction_error);
	}
	const String fixed_paths[] = {
		target_absolute_path,
		target_absolute_path + ".import",
		target_absolute_path + ".uid",
		target_absolute_path + ".unwrap_cache",
	};
	for (const String &path : fixed_paths) {
		error = transaction.protect_path(path, &transaction_error);
		if (error != OK) {
			return MCPToolUtils::make_error_result("TRANSACTION_FAILED", transaction_error);
		}
	}
	error = _protect_paths(transaction, previous_import_base_files, &transaction_error);
	if (error == OK) {
		error = _protect_paths(transaction, previous_dest_transaction_paths, &transaction_error);
	}
	if (error != OK) {
		return MCPToolUtils::make_error_result("TRANSACTION_FAILED", transaction_error);
	}

	error = DirAccess::copy_absolute(source_path, target_absolute_path);
	if (error == OK) {
		error = _execute_import(target_path, resolution.values, resolution.importer->get_importer_name());
	}

	const HashSet<String> current_import_base_files = _collect_import_base_files(import_base_path);
	PackedStringArray unsafe_current_dest_paths;
	HashSet<String> current_dest_resource_paths;
	HashSet<String> current_dest_transaction_paths;
	HashMap<String, String> current_dest_transactions_by_resource;
	_read_import_dest_paths(target_absolute_path, project_root, current_dest_resource_paths, current_dest_transaction_paths, &unsafe_current_dest_paths, &current_dest_transactions_by_resource);
	_track_new_paths(transaction, current_import_base_files);
	const HashSet<String> possible_residual_paths = _track_reported_dest_paths(transaction, current_dest_transactions_by_resource);
	if (error == OK && !unsafe_current_dest_paths.is_empty()) {
		error = ERR_INVALID_DATA;
	}

	if (error != OK || !FileAccess::exists(target_absolute_path + ".import")) {
		if (error == OK) {
			error = ERR_FILE_CORRUPT;
		}
		String rollback_error;
		const Error rollback_result = transaction.rollback(&rollback_error);
		_scan_changes();

		Dictionary details;
		details["source"] = source_path;
		details["target"] = target_path;
		details["godotError"] = error_names[error];
		details["rollbackSucceeded"] = rollback_result == OK;
		details["rollbackComplete"] = rollback_result == OK && possible_residual_paths.is_empty() && unsafe_current_dest_paths.is_empty();
		if (!possible_residual_paths.is_empty()) {
			details["possibleResidualProducts"] = _paths_to_sorted_array(possible_residual_paths);
		}
		if (!unsafe_current_dest_paths.is_empty()) {
			unsafe_current_dest_paths.sort();
			details["unsafeReportedProducts"] = unsafe_current_dest_paths;
		}
		if (rollback_result != OK) {
			details["rollbackError"] = rollback_error;
		}
		return MCPToolUtils::make_error_result("IMPORT_FAILED", "Resource import failed for target: " + target_path, details);
	}

	transaction.commit();
	_scan_changes();

	Dictionary result;
	result["source"] = source_path;
	result["target"] = target_path;
	result["importer"] = resolution.importer->get_importer_name();
	result["preset"] = resolution.preset;
	result["projectDefaultsApplied"] = resolution.project_defaults_applied;
	result["overwritten"] = target_existed || metadata_existed || source_sidecar_existed || import_products_existed;
	result["internalPath"] = ResourceFormatImporter::get_singleton()->get_internal_resource_path(target_path);
	result["generatedFiles"] = _paths_to_sorted_array(current_dest_resource_paths);
	return MCPToolUtils::make_success_result(result);
}
