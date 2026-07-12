/**************************************************************************/
/*  mcp_project_identity.cpp                                              */
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

#include "mcp_project_identity.h"

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

namespace {

constexpr int MAX_LINK_RESOLUTIONS = 64;
constexpr int INSTANCE_ID_BYTES = 16;

void set_error(String *r_error, const String &p_error) {
	if (r_error) {
		*r_error = p_error;
	}
}

String normalize_path(String p_path) {
	p_path = p_path.replace_char('\\', '/').simplify_path();
	while (p_path.length() > 1 && p_path.ends_with("/") && !p_path.ends_with(":/")) {
		p_path = p_path.left(-1);
	}
	return p_path;
}

bool is_lower_hex_text(const String &p_value, int p_expected_length) {
	if (p_value.length() != p_expected_length) {
		return false;
	}
	for (int i = 0; i < p_value.length(); i++) {
		const char32_t character = p_value[i];
		if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
			return false;
		}
	}
	return true;
}

Error resolve_link_components(const String &p_path, String &r_resolved_path, String *r_error) {
	Ref<DirAccess> access = DirAccess::create_for_path(p_path);
	if (access.is_null()) {
		set_error(r_error, vformat("Cannot create filesystem access for project path '%s'.", p_path));
		return ERR_CANT_OPEN;
	}

	String resolved_path = normalize_path(p_path);
	HashSet<String> visited_paths;

	for (int resolution = 0; resolution < MAX_LINK_RESOLUTIONS; resolution++) {
		if (visited_paths.has(resolved_path)) {
			set_error(r_error, vformat("Project path '%s' contains a cyclic symbolic link.", p_path));
			return ERR_CYCLIC_LINK;
		}
		visited_paths.insert(resolved_path);

		String candidate = resolved_path;
		Vector<String> suffix;
		bool link_resolved = false;

		while (!candidate.is_empty()) {
			if (access->is_link(candidate)) {
				String link_target = access->read_link(candidate);
				if (link_target.is_empty()) {
					set_error(r_error, vformat("Cannot resolve symbolic link '%s'.", candidate));
					return ERR_CANT_RESOLVE;
				}
				if (link_target.is_relative_path()) {
					link_target = candidate.get_base_dir().path_join(link_target);
				}
				for (int i = suffix.size() - 1; i >= 0; i--) {
					link_target = link_target.path_join(suffix[i]);
				}
				resolved_path = normalize_path(link_target);
				link_resolved = true;
				break;
			}

			const String file_name = candidate.get_file();
			String parent = candidate.get_base_dir();
			if (parent.ends_with(":")) {
				parent += "/";
			}
			if (parent.is_empty() || parent == candidate) {
				break;
			}
			if (!file_name.is_empty()) {
				suffix.push_back(file_name);
			}
			candidate = parent;
		}

		if (link_resolved) {
			continue;
		}

		// On Windows, read_link() returns the normalized final path for any
		// existing directory, including paths with junctions in a parent.
		const String final_path = normalize_path(access->read_link(resolved_path));
		if (!final_path.is_empty() && final_path != resolved_path && DirAccess::exists(final_path)) {
			resolved_path = final_path;
			continue;
		}

		r_resolved_path = resolved_path;
		return OK;
	}

	set_error(r_error, vformat("Project path '%s' exceeds the symbolic link resolution limit.", p_path));
	return ERR_CYCLIC_LINK;
}

} // namespace

bool MCPProjectIdentity::is_valid() const {
	return canonical_path.is_absolute_path() && project_id == make_project_id(canonical_path) && is_valid_instance_id(instance_id);
}

bool MCPProjectIdentity::is_valid_project_id(const String &p_project_id) {
	return is_lower_hex_text(p_project_id, 64);
}

bool MCPProjectIdentity::is_valid_instance_id(const String &p_instance_id) {
	return is_lower_hex_text(p_instance_id, INSTANCE_ID_BYTES * 2);
}

Error MCPProjectIdentity::canonicalize_project_path(const String &p_project_path, String &r_canonical_path, String *r_error) {
	r_canonical_path = String();
	set_error(r_error, String());

	if (p_project_path.is_empty()) {
		set_error(r_error, "Project path cannot be empty.");
		return ERR_INVALID_PARAMETER;
	}
	if (p_project_path.begins_with("res://") || p_project_path.begins_with("user://") || p_project_path.contains("://")) {
		set_error(r_error, "Project identity requires a filesystem path.");
		return ERR_INVALID_PARAMETER;
	}

	Error open_error = OK;
	Ref<DirAccess> project_dir = DirAccess::open(p_project_path, &open_error);
	if (project_dir.is_null()) {
		set_error(r_error, vformat("Project directory '%s' does not exist or cannot be opened.", p_project_path));
		return open_error == OK ? ERR_DOES_NOT_EXIST : open_error;
	}

	const String absolute_path = normalize_path(project_dir->get_current_dir());
	String resolved_path;
	const Error resolve_error = resolve_link_components(absolute_path, resolved_path, r_error);
	if (resolve_error != OK) {
		return resolve_error;
	}

	Ref<DirAccess> resolved_dir = DirAccess::open(resolved_path, &open_error);
	if (resolved_dir.is_null()) {
		set_error(r_error, vformat("Resolved project directory '%s' cannot be opened.", resolved_path));
		return open_error == OK ? ERR_CANT_OPEN : open_error;
	}

	r_canonical_path = normalize_path(resolved_dir->get_current_dir());
	return OK;
}

String MCPProjectIdentity::make_project_id(const String &p_canonical_path) {
	return normalize_path(p_canonical_path).sha256_text();
}

Error MCPProjectIdentity::generate_instance_id(String &r_instance_id, String *r_error) {
	r_instance_id = String();
	set_error(r_error, String());

	uint8_t random_bytes[INSTANCE_ID_BYTES];
	CryptoCore::RandomGenerator random;
	Error error = random.init();
	if (error == OK) {
		error = random.get_random_bytes(random_bytes, INSTANCE_ID_BYTES);
	}
	if (error != OK) {
		set_error(r_error, "Cannot generate a cryptographically random MCP instance ID.");
		return error;
	}

	r_instance_id = String::hex_encode_buffer(random_bytes, INSTANCE_ID_BYTES);
	return OK;
}

Error MCPProjectIdentity::create(const String &p_project_path, MCPProjectIdentity &r_identity, String *r_error) {
	r_identity = MCPProjectIdentity();

	Error error = canonicalize_project_path(p_project_path, r_identity.canonical_path, r_error);
	if (error != OK) {
		return error;
	}
	r_identity.project_id = make_project_id(r_identity.canonical_path);

	error = generate_instance_id(r_identity.instance_id, r_error);
	if (error != OK) {
		r_identity = MCPProjectIdentity();
		return error;
	}
	return OK;
}
