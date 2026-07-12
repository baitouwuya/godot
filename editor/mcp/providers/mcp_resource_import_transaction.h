/**************************************************************************/
/*  mcp_resource_import_transaction.h                                     */
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

#pragma once

#include "core/io/dir_access.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class MCPResourceImportTransaction {
	struct ProtectedPath {
		String backup_path;
		bool existed = false;
	};

	Ref<DirAccess> backup_directory;
	HashMap<String, ProtectedPath> protected_paths;
	HashSet<String> created_paths;
	HashSet<String> existing_paths;
	HashSet<String> excluded_snapshot_roots;
	String snapshot_root;
	bool snapshot_captured = false;
	bool active = false;

	static void _set_error(String *r_error, const String &p_message);

public:
	~MCPResourceImportTransaction();

	Error begin(String *r_error = nullptr);
	Error capture_existing_files(const String &p_project_root, String *r_error = nullptr);
	Error protect_path(const String &p_path, String *r_error = nullptr);
	void track_created_path(const String &p_path);
	bool is_path_protected(const String &p_path) const;
	bool has_preimport_path_state(const String &p_path) const;
	bool existed_before_import(const String &p_path) const;

	void commit();
	Error rollback(String *r_error = nullptr);
};
