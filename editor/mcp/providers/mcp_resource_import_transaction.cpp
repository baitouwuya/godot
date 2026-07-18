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

#include "core/io/file_access.h"

namespace {

static String _normalize_path(const String &p_path) {
	return p_path.replace_char('\\', '/').simplify_path();
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
	active = true;
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
	created_paths.insert(normalized_path);
}

bool MCPResourceImportTransaction::is_path_protected(const String &p_path) const {
	return protected_paths.has(_normalize_path(p_path));
}

bool MCPResourceImportTransaction::is_path_tracked_created(const String &p_path) const {
	return created_paths.has(_normalize_path(p_path));
}

void MCPResourceImportTransaction::commit() {
	active = false;
	created_paths.clear();
	protected_paths.clear();
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
	backup_directory.unref();
	_set_error(r_error, first_error_message);
	return first_error;
}
