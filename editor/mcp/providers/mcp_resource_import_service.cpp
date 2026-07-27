/**************************************************************************/
/*  mcp_resource_import_service.cpp                                       */
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

#include "mcp_resource_import_service.h"

#include "mcp_path_utils.h"
#include "mcp_resource_import_options.h"
#include "mcp_resource_import_transaction.h"
#include "mcp_tool_utils.h"

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_importer.h"
#include "core/templates/hash_set.h"
#include "editor/file_system/editor_file_system.h"

namespace {

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

MCPResourceImportService::MCPResourceImportService(const String &p_project_root, ImportFunction p_import_function, ScanFunction p_scan_function) :
		project_root(p_project_root),
		import_function(p_import_function),
		scan_function(p_scan_function) {
}

Error MCPResourceImportService::_execute_import(const String &p_target_path, const HashMap<StringName, Variant> &p_options, const String &p_importer) const {
	if (import_function) {
		return import_function(p_target_path, p_options, p_importer);
	}
	EditorFileSystem *editor_file_system = EditorFileSystem::get_singleton();
	return editor_file_system ? editor_file_system->import_file_with_custom_options(p_target_path, p_options, p_importer) : ERR_UNCONFIGURED;
}

void MCPResourceImportService::_scan_changes() const {
	if (scan_function) {
		scan_function();
		return;
	}
	if (EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->scan_changes();
	}
}

Dictionary MCPResourceImportService::get_options(const Dictionary &p_arguments) const {
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

Dictionary MCPResourceImportService::import_resource(const Dictionary &p_arguments) const {
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
