/**************************************************************************/
/*  mcp_path_utils.cpp                                                    */
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

#include "mcp_path_utils.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/mcp/mcp_project_identity.h"

namespace MCPPathUtils {

static Error _fail(const String &p_message, String *r_error, Error p_error = ERR_INVALID_PARAMETER) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

static bool _has_control_character(const String &p_segment) {
	for (int i = 0; i < p_segment.length(); i++) {
		if (p_segment[i] < 32) {
			return true;
		}
	}
	return false;
}

static String _path_for_comparison(String p_path) {
	p_path = p_path.replace_char('\\', '/').simplify_path();
#ifdef WINDOWS_ENABLED
	p_path = p_path.to_lower();
#endif
	return p_path;
}

static bool _is_path_within(const String &p_root, const String &p_path) {
	const String root = _path_for_comparison(p_root);
	const String path = _path_for_comparison(p_path);
	return path == root || path.begins_with(root.ends_with("/") ? root : root + "/");
}

static Error _validate_existing_ancestor(const String &p_project_root, const String &p_absolute_path, String *r_error) {
	String canonical_root;
	String canonical_error;
	Error err = MCPProjectIdentity::canonicalize_project_path(p_project_root, canonical_root, &canonical_error);
	if (err != OK) {
		return _fail("Cannot canonicalize the project resource directory: " + canonical_error, r_error, err);
	}

	String existing_directory = p_absolute_path.get_base_dir();
	while (!DirAccess::dir_exists_absolute(existing_directory)) {
		const String parent = existing_directory.get_base_dir();
		if (parent.is_empty() || parent == existing_directory) {
			return _fail("Cannot find an existing parent directory for the resolved path.", r_error, ERR_CANT_RESOLVE);
		}
		existing_directory = parent;
	}

	String canonical_existing_directory;
	err = MCPProjectIdentity::canonicalize_project_path(existing_directory, canonical_existing_directory, &canonical_error);
	if (err != OK) {
		return _fail("Cannot canonicalize the resolved parent directory: " + canonical_error, r_error, err);
	}
	if (!_is_path_within(canonical_root, canonical_existing_directory)) {
		return _fail("Resolved path crosses a symbolic link outside the project resource directory.", r_error, ERR_INVALID_DATA);
	}

	Ref<DirAccess> filesystem = DirAccess::create_for_path(p_absolute_path);
	if (filesystem.is_valid() && filesystem->is_link(p_absolute_path)) {
		return _fail("The target path cannot be a symbolic link.", r_error, ERR_INVALID_DATA);
	}
	return OK;
}

Error normalize_resource_file_path(const String &p_path, String &r_normalized_path, String *r_error) {
	r_normalized_path = String();
	if (r_error) {
		*r_error = String();
	}

	if (!p_path.begins_with("res://")) {
		return _fail("Path must begin with 'res://'.", r_error);
	}
	if (p_path.contains_char('\\')) {
		return _fail("Path must use forward slashes.", r_error);
	}

	const String relative_path = p_path.trim_prefix("res://");
	if (relative_path.is_empty()) {
		return _fail("Path must identify a file inside the project.", r_error);
	}

	const PackedStringArray segments = relative_path.split("/", true);
	for (const String &segment : segments) {
		if (segment == "." || segment == "..") {
			return _fail("Path cannot contain '.' or '..' segments.", r_error);
		}
		if (!segment.is_valid_filename() || _has_control_character(segment)) {
			return _fail("Path contains an invalid file name segment.", r_error);
		}
	}

	r_normalized_path = ("res://" + relative_path).simplify_path();
	if (r_normalized_path != p_path) {
		return _fail("Path must already be in canonical 'res://' form.", r_error);
	}
	return OK;
}

Error resolve_project_file_path_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error) {
	r_resource_path = String();
	r_absolute_path = String();
	if (r_error) {
		*r_error = String();
	}

	Error err = normalize_resource_file_path(p_path, r_resource_path, r_error);
	if (err != OK) {
		return err;
	}

	const String project_root = p_project_root.replace_char('\\', '/').simplify_path();
	if (project_root.is_empty() || !project_root.is_absolute_path()) {
		return _fail("The project resource path is not an absolute path.", r_error, ERR_UNCONFIGURED);
	}

	const String absolute_path = project_root.path_join(r_resource_path.trim_prefix("res://")).simplify_path();
	if (!absolute_path.is_absolute_path() || !_is_path_within(project_root, absolute_path) || absolute_path == project_root) {
		return _fail("Resolved path is outside the project resource directory.", r_error, ERR_INVALID_DATA);
	}
	const Error ancestor_error = _validate_existing_ancestor(project_root, absolute_path, r_error);
	if (ancestor_error != OK) {
		return ancestor_error;
	}

	r_absolute_path = absolute_path;
	return OK;
}

Error resolve_project_file_path(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return _fail("Project settings are not available.", r_error, ERR_UNCONFIGURED);
	}
	return resolve_project_file_path_for_root(
			p_path, project_settings->get_resource_path(), r_resource_path, r_absolute_path, r_error);
}

} // namespace MCPPathUtils
