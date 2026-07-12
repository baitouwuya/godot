/**************************************************************************/
/*  mcp_resource_import_transaction.cpp                                   */
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

#include "mcp_resource_import_transaction.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"

namespace {

static String _normalize_path(const String &p_path) {
	return p_path.replace_char('\\', '/').simplify_path();
}

static bool _is_path_within_root(const String &p_path, const String &p_root) {
	const String root_prefix = p_root.ends_with("/") ? p_root : p_root + "/";
	return p_path == p_root || p_path.begins_with(root_prefix);
}

} // namespace

void MCPResourceImportTransaction::_set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

MCPResourceImportTransaction::~MCPResourceImportTransaction() {
	if (active) {
		rollback();
	}
}

Error MCPResourceImportTransaction::begin(String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (active) {
		_set_error(r_error, "Resource import transaction is already active.");
		return ERR_ALREADY_IN_USE;
	}

	Error error = OK;
	backup_directory = DirAccess::create_temp("godot-mcp-resource-import", false, &error);
	if (backup_directory.is_null() || error != OK) {
		_set_error(r_error, "Could not create a temporary resource import backup directory.");
		return error == OK ? ERR_CANT_CREATE : error;
	}

	protected_paths.clear();
	created_paths.clear();
	existing_paths.clear();
	excluded_snapshot_roots.clear();
	snapshot_root = String();
	snapshot_captured = false;
	active = true;
	return OK;
}

Error MCPResourceImportTransaction::capture_existing_files(const String &p_project_root, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!active) {
		_set_error(r_error, "Resource import transaction is not active.");
		return ERR_UNCONFIGURED;
	}
	if (snapshot_captured) {
		_set_error(r_error, "Resource import transaction already captured the project files.");
		return ERR_ALREADY_IN_USE;
	}

	const String normalized_root = _normalize_path(p_project_root);
	if (normalized_root.is_empty()) {
		_set_error(r_error, "Cannot capture files for an empty project root.");
		return ERR_INVALID_PARAMETER;
	}

	String project_data_directory = ProjectSettings::get_singleton()->get_project_data_dir_name();
	if (project_data_directory.is_empty()) {
		project_data_directory = ".godot";
	}
	excluded_snapshot_roots.insert(_normalize_path(normalized_root.path_join(project_data_directory)));

	Vector<String> pending_directories;
	HashSet<String> visited_directories;
	pending_directories.push_back(normalized_root);
	while (!pending_directories.is_empty()) {
		const String directory_path = pending_directories[pending_directories.size() - 1];
		pending_directories.resize(pending_directories.size() - 1);
		if (visited_directories.has(directory_path)) {
			continue;
		}
		visited_directories.insert(directory_path);

		Error open_error = OK;
		Ref<DirAccess> directory = DirAccess::open(directory_path, &open_error);
		if (directory.is_null() || open_error != OK) {
			existing_paths.clear();
			_set_error(r_error, "Could not inspect project directory before resource import: " + directory_path);
			return open_error == OK ? ERR_CANT_OPEN : open_error;
		}

		for (const String &file_name : directory->get_files()) {
			const String file_path = _normalize_path(directory_path.path_join(file_name));
			if (_is_path_within_root(file_path, normalized_root)) {
				existing_paths.insert(file_path);
			}
		}
		for (const String &directory_name : directory->get_directories()) {
			const String child_path = _normalize_path(directory_path.path_join(directory_name));
			if (!_is_path_within_root(child_path, normalized_root) || excluded_snapshot_roots.has(child_path)) {
				continue;
			}
			if (directory->is_link(directory_name)) {
				excluded_snapshot_roots.insert(child_path);
				continue;
			}
			pending_directories.push_back(child_path);
		}
	}

	snapshot_root = normalized_root;
	snapshot_captured = true;
	return OK;
}

Error MCPResourceImportTransaction::protect_path(const String &p_path, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!active) {
		_set_error(r_error, "Resource import transaction is not active.");
		return ERR_UNCONFIGURED;
	}
	const String normalized_path = _normalize_path(p_path);
	if (normalized_path.is_empty()) {
		_set_error(r_error, "Cannot protect an empty resource import path.");
		return ERR_INVALID_PARAMETER;
	}
	if (protected_paths.has(normalized_path)) {
		return OK;
	}

	ProtectedPath protected_path;
	protected_path.existed = FileAccess::exists(normalized_path);
	if (protected_path.existed) {
		protected_path.backup_path = backup_directory->get_current_dir().path_join(itos(protected_paths.size()) + ".bak");
		const Error copy_error = DirAccess::copy_absolute(normalized_path, protected_path.backup_path);
		if (copy_error != OK) {
			_set_error(r_error, "Could not back up resource import path: " + normalized_path);
			return copy_error;
		}
	}

	protected_paths.insert(normalized_path, protected_path);
	created_paths.erase(normalized_path);
	return OK;
}

void MCPResourceImportTransaction::track_created_path(const String &p_path) {
	const String normalized_path = _normalize_path(p_path);
	if (!active || normalized_path.is_empty() || protected_paths.has(normalized_path)) {
		return;
	}
	if (snapshot_captured && !_is_path_within_root(normalized_path, snapshot_root)) {
		return;
	}
	created_paths.insert(normalized_path);
}

bool MCPResourceImportTransaction::is_path_protected(const String &p_path) const {
	return protected_paths.has(_normalize_path(p_path));
}

bool MCPResourceImportTransaction::has_preimport_path_state(const String &p_path) const {
	const String normalized_path = _normalize_path(p_path);
	if (!snapshot_captured || !_is_path_within_root(normalized_path, snapshot_root)) {
		return false;
	}
	for (const String &excluded_root : excluded_snapshot_roots) {
		if (_is_path_within_root(normalized_path, excluded_root)) {
			return false;
		}
	}
	return true;
}

bool MCPResourceImportTransaction::existed_before_import(const String &p_path) const {
	const String normalized_path = _normalize_path(p_path);
	return has_preimport_path_state(normalized_path) && existing_paths.has(normalized_path);
}

void MCPResourceImportTransaction::commit() {
	active = false;
	created_paths.clear();
	protected_paths.clear();
	existing_paths.clear();
	excluded_snapshot_roots.clear();
	snapshot_root = String();
	snapshot_captured = false;
	backup_directory.unref();
}

Error MCPResourceImportTransaction::rollback(String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!active) {
		return OK;
	}

	Error first_error = OK;
	String first_error_message;
	for (const String &path : created_paths) {
		if (!FileAccess::exists(path)) {
			continue;
		}
		const Error remove_error = DirAccess::remove_absolute(path);
		if (remove_error != OK && first_error == OK) {
			first_error = remove_error;
			first_error_message = "Could not remove newly created resource import path: " + path;
		}
	}

	for (const KeyValue<String, ProtectedPath> &entry : protected_paths) {
		if (FileAccess::exists(entry.key)) {
			const Error remove_error = DirAccess::remove_absolute(entry.key);
			if (remove_error != OK && first_error == OK) {
				first_error = remove_error;
				first_error_message = "Could not remove changed resource import path: " + entry.key;
			}
		}
		if (!entry.value.existed) {
			continue;
		}

		const String parent_directory = entry.key.get_base_dir();
		if (!DirAccess::dir_exists_absolute(parent_directory)) {
			const Error directory_error = DirAccess::make_dir_recursive_absolute(parent_directory);
			if (directory_error != OK) {
				if (first_error == OK) {
					first_error = directory_error;
					first_error_message = "Could not restore the parent directory for: " + entry.key;
				}
				continue;
			}
		}

		const Error restore_error = DirAccess::copy_absolute(entry.value.backup_path, entry.key);
		if (restore_error != OK && first_error == OK) {
			first_error = restore_error;
			first_error_message = "Could not restore resource import path: " + entry.key;
		}
	}

	active = false;
	created_paths.clear();
	protected_paths.clear();
	existing_paths.clear();
	excluded_snapshot_roots.clear();
	snapshot_root = String();
	snapshot_captured = false;
	backup_directory.unref();
	_set_error(r_error, first_error_message);
	return first_error;
}
