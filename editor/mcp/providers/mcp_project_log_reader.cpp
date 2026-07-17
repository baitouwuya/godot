/**************************************************************************/
/*  mcp_project_log_reader.cpp                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "mcp_project_log_reader.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace {

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

static bool _is_digit(char32_t p_character) {
	return p_character >= '0' && p_character <= '9';
}

static bool _is_rotated_name(const String &p_file_name, const String &p_base_file_name) {
	if (p_file_name == p_base_file_name) {
		return true;
	}
	const String extension = p_base_file_name.get_extension();
	const String stem = p_base_file_name.get_basename();
	if (p_file_name.get_extension() != extension || !p_file_name.begins_with(stem)) {
		return false;
	}
	const int suffix_start = stem.length();
	const int suffix_end = p_file_name.length() - (extension.is_empty() ? 0 : extension.length() + 1);
	const String suffix = p_file_name.substr(suffix_start, suffix_end - suffix_start);
	// RotatedFileLogger appends YYYY-MM-DDTHH.MM.SS directly to the basename.
	if (suffix.length() != 19 || suffix[4] != '-' || suffix[7] != '-' || suffix[10] != 'T' || suffix[13] != '.' || suffix[16] != '.') {
		return false;
	}
	for (int i = 0; i < suffix.length(); i++) {
		if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) {
			continue;
		}
		if (!_is_digit(suffix[i])) {
			return false;
		}
	}
	return true;
}

} // namespace

String MCPProjectLogReader::resolve_configured_base_path() {
	String configured_path = GLOBAL_GET("debug/file_logging/log_path");
	if (configured_path.is_empty()) {
		return String();
	}
	if (configured_path.begins_with("res://") || configured_path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(configured_path).simplify_path();
	}
	if (configured_path.is_absolute_path()) {
		return configured_path.simplify_path();
	}
	String project_path = ProjectSettings::get_singleton()->get_resource_path();
	if (project_path.is_empty()) {
		project_path = OS::get_singleton()->get_cwd();
	}
	return project_path.path_join(configured_path).simplify_path();
}

Error MCPProjectLogReader::find_latest_log(const String &p_base_path, String &r_path, String *r_error) {
	r_path = String();
	_set_error(r_error, String());
	if (p_base_path.is_empty() || !p_base_path.is_absolute_path()) {
		_set_error(r_error, "The configured project log path is empty or not absolute.");
		return ERR_INVALID_PARAMETER;
	}

	const String base_path = p_base_path.simplify_path();
	const String base_directory = base_path.get_base_dir();
	Ref<DirAccess> directory = DirAccess::open(base_directory);
	if (directory.is_null()) {
		_set_error(r_error, "The configured project log directory does not exist.");
		return ERR_FILE_NOT_FOUND;
	}

	const String base_file_name = base_path.get_file();
	uint64_t latest_modified = 0;
	bool found = false;
	directory->list_dir_begin();
	for (String file_name = directory->get_next(); !file_name.is_empty(); file_name = directory->get_next()) {
		if (directory->current_is_dir() || directory->current_is_hidden() || directory->is_link(file_name) || !_is_rotated_name(file_name, base_file_name)) {
			continue;
		}
		const String candidate = base_directory.path_join(file_name).simplify_path();
		const uint64_t modified = FileAccess::get_modified_time(candidate);
		const bool prefer_base_on_tie = modified == latest_modified && candidate == base_path;
		const bool deterministic_tie = modified == latest_modified && !prefer_base_on_tie && candidate < r_path && r_path != base_path;
		if (!found || modified > latest_modified || prefer_base_on_tie || deterministic_tie) {
			r_path = candidate;
			latest_modified = modified;
			found = true;
		}
	}
	directory->list_dir_end();
	if (!found) {
		_set_error(r_error, "No configured project log or rotated log was found.");
		return ERR_FILE_NOT_FOUND;
	}
	return OK;
}

Error MCPProjectLogReader::read_latest(int p_max_lines, Result &r_result, String *r_error) {
	return read_latest_from_base(resolve_configured_base_path(), p_max_lines, r_result, r_error);
}

Error MCPProjectLogReader::read_latest_from_base(const String &p_base_path, int p_max_lines, Result &r_result, String *r_error) {
	r_result = Result();
	_set_error(r_error, String());
	if (p_max_lines <= 0) {
		_set_error(r_error, "maxLines must be positive.");
		return ERR_INVALID_PARAMETER;
	}
	String latest_path;
	Error error = find_latest_log(p_base_path, latest_path, r_error);
	if (error != OK) {
		return error;
	}

	Ref<FileAccess> file = FileAccess::open(latest_path, FileAccess::READ, &error);
	if (file.is_null() || error != OK) {
		_set_error(r_error, "The latest project log could not be opened for reading.");
		return error == OK ? ERR_CANT_OPEN : error;
	}
	Vector<String> lines;
	lines.resize(p_max_lines);
	int next_slot = 0;
	const uint64_t length = file->get_length();
	while (file->get_position() < length) {
		lines.write[next_slot] = file->get_line();
		next_slot = (next_slot + 1) % p_max_lines;
		r_result.total_lines++;
	}
	r_result.path = latest_path;
	r_result.shown_lines = MIN(r_result.total_lines, p_max_lines);
	r_result.truncated = r_result.total_lines > r_result.shown_lines;
	for (int i = 0; i < r_result.shown_lines; i++) {
		const int index = (r_result.total_lines - r_result.shown_lines + i) % p_max_lines;
		if (i > 0) {
			r_result.text += "\n";
		}
		r_result.text += lines[index];
	}
	return OK;
}
