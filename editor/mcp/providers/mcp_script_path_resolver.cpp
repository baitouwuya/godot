/**************************************************************************/
/*  mcp_script_path_resolver.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#include "mcp_script_path_resolver.h"

#include "mcp_path_utils.h"

#include "core/config/project_settings.h"

namespace {

Error fail(const String &p_message, String *r_error, Error p_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

Error validate_external_script_path(Error p_resolve_error, String &r_resource_path, String &r_absolute_path, String *r_error) {
	if (p_resolve_error != OK) {
		return p_resolve_error;
	}
	if (r_resource_path.get_extension().to_lower() != "gd") {
		r_resource_path = String();
		r_absolute_path = String();
		return fail("Only external .gd scripts are supported.", r_error, ERR_INVALID_PARAMETER);
	}
	return OK;
}

Error resolve_built_in_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error) {
	const int separator = p_path.find("::");
	if (separator < 0 || p_path.find("::", separator + 2) >= 0) {
		return fail("A built-in script path must contain exactly one :: subresource separator.", r_error, ERR_INVALID_PARAMETER);
	}

	const String container_path = p_path.left(separator);
	const String subresource_id = p_path.substr(separator + 2);
	if (container_path.is_empty() || subresource_id.is_empty() || subresource_id.contains_char('/') ||
			subresource_id.contains_char('\\') || subresource_id.contains_char(':')) {
		return fail("The built-in script subresource identifier is invalid.", r_error, ERR_INVALID_PARAMETER);
	}

	String container_resource_path;
	const Error resolve_error = MCPPathUtils::resolve_project_file_path_for_root(
			container_path, p_project_root, container_resource_path, r_absolute_path, r_error);
	if (resolve_error != OK) {
		return resolve_error;
	}
	r_resource_path = container_resource_path + "::" + subresource_id;
	return OK;
}

} // namespace

Error MCPScriptPathResolver::normalize_external_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	const Error resolve_error = MCPPathUtils::resolve_project_file_path_for_root(
			p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
	return validate_external_script_path(resolve_error, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptPathResolver::normalize_external(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	const Error resolve_error = MCPPathUtils::resolve_project_file_path(p_path, r_resource_path, r_absolute_path, r_error);
	return validate_external_script_path(resolve_error, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptPathResolver::resolve_external_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, String *r_error) {
	if (p_path.begins_with("res://")) {
		return normalize_external_for_root(p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
	}

	const String absolute_path = p_path.replace_char('\\', '/').simplify_path();
	if (!absolute_path.is_absolute_path() || absolute_path.contains("://")) {
		r_resource_path = String();
		r_absolute_path = String();
		return fail("Script path must be a project res:// path or an absolute path inside the project.", r_error, ERR_INVALID_PARAMETER);
	}

	const String project_root = p_project_root.replace_char('\\', '/').simplify_path();
	if (project_root.is_empty() || !project_root.is_absolute_path()) {
		r_resource_path = String();
		r_absolute_path = String();
		return fail("The project resource path is not an absolute path.", r_error, ERR_UNCONFIGURED);
	}

	const String relative_path = project_root.path_to_file(absolute_path).replace_char('\\', '/');
	if (relative_path.is_empty() || relative_path == "." || relative_path.is_absolute_path() || relative_path == ".." || relative_path.begins_with("../")) {
		r_resource_path = String();
		r_absolute_path = String();
		return fail("Script path is outside the project resource directory.", r_error, ERR_INVALID_PARAMETER);
	}
	return normalize_external_for_root("res://" + relative_path, project_root, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptPathResolver::resolve_external(const String &p_path, String &r_resource_path, String &r_absolute_path, String *r_error) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return fail("Project settings are not available.", r_error, ERR_UNCONFIGURED);
	}
	return resolve_external_for_root(p_path, project_settings->get_resource_path(), r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptPathResolver::resolve_script_for_root(const String &p_path, const String &p_project_root, String &r_resource_path, String &r_absolute_path, bool &r_built_in, String *r_error) {
	r_resource_path = String();
	r_absolute_path = String();
	r_built_in = p_path.contains("::");
	if (r_built_in) {
		return resolve_built_in_for_root(p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
	}
	return resolve_external_for_root(p_path, p_project_root, r_resource_path, r_absolute_path, r_error);
}

Error MCPScriptPathResolver::resolve_script(const String &p_path, String &r_resource_path, String &r_absolute_path, bool &r_built_in, String *r_error) {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return fail("Project settings are not available.", r_error, ERR_UNCONFIGURED);
	}
	return resolve_script_for_root(p_path, project_settings->get_resource_path(), r_resource_path, r_absolute_path, r_built_in, r_error);
}
