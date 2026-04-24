/**************************************************************************/
/*  cli_latest_log_runner.cpp                                             */
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

#include "cli_latest_log_runner.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

bool CLILatestLogRunner::_consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error) {
	if (!r_next) {
		r_error = "Missing value after " + p_arg + ".";
		return false;
	}

	r_value = r_next->get();
	r_next = r_next->next();
	return true;
}

void CLILatestLogRunner::_print_stdout(const String &p_text) {
	const bool stdout_was_enabled = OS::get_singleton()->is_stdout_enabled();
	const bool print_line_was_enabled = Engine::get_singleton()->is_printing_to_stdout();
	OS::get_singleton()->set_stdout_enabled(true);
	Engine::get_singleton()->set_print_to_stdout(true);
	OS::get_singleton()->print("%s", p_text.utf8().get_data());
	Engine::get_singleton()->set_print_to_stdout(print_line_was_enabled);
	OS::get_singleton()->set_stdout_enabled(stdout_was_enabled);
}

bool CLILatestLogRunner::parse_argument(const String &p_arg, List<String>::Element *&r_next, Options &r_options, String &r_error) {
	r_error = String();
	String value;

	if (p_arg == "--latest-log") {
		r_options.enabled = true;
		return true;
	}

	if (p_arg == "--latest-log-lines") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (!value.is_valid_int()) {
			r_error = "--latest-log-lines must be followed by an integer.";
			return true;
		}
		r_options.lines = value.to_int();
		r_options.lines_set = true;
		return true;
	}

	return false;
}

Error CLILatestLogRunner::validate_options(const Options &p_options, bool p_found_project, bool p_editor, bool p_project_manager, String &r_error) {
	r_error = String();

	if (!p_options.enabled && p_options.lines_set) {
		r_error = "--latest-log-lines can only be used together with --latest-log.";
		return ERR_INVALID_PARAMETER;
	}

	if (p_options.lines < 0) {
		r_error = "--latest-log-lines must be greater than or equal to 0.";
		return ERR_INVALID_PARAMETER;
	}

	if (!p_options.enabled) {
		return OK;
	}

	if (p_editor || p_project_manager) {
		r_error = "--latest-log only supports CLI project contexts. It cannot be used with the editor or project manager.";
		return ERR_INVALID_PARAMETER;
	}

	if (!p_found_project) {
		r_error = "--latest-log requires a project context. Use --path <project> or run the command from a project directory.";
		return ERR_INVALID_PARAMETER;
	}

	return OK;
}

String CLILatestLogRunner::resolve_base_log_path(const String &p_log_file_override) {
	String configured_path = p_log_file_override;
	if (configured_path.is_empty()) {
		configured_path = GLOBAL_GET("debug/file_logging/log_path");
	}
	if (configured_path.is_empty()) {
		return String();
	}

	if (configured_path.begins_with("res://") || configured_path.begins_with("user://") || configured_path.begins_with("uid://")) {
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

Error CLILatestLogRunner::find_latest_log_path(const String &p_base_log_path, String &r_latest_log_path) {
	r_latest_log_path = String();
	ERR_FAIL_COND_V_MSG(p_base_log_path.is_empty(), ERR_INVALID_PARAMETER, "Base log path must not be empty.");

	String base_dir = p_base_log_path.get_base_dir();
	if (base_dir.is_empty()) {
		base_dir = ".";
	}

	Ref<DirAccess> dir = DirAccess::open(base_dir);
	if (dir.is_null()) {
		if (FileAccess::exists(p_base_log_path)) {
			r_latest_log_path = p_base_log_path;
			return OK;
		}
		return ERR_FILE_NOT_FOUND;
	}

	const String basename = p_base_log_path.get_file().get_basename();
	const String extension = p_base_log_path.get_extension();
	uint64_t latest_modified_time = 0;
	bool found_match = false;

	dir->list_dir_begin();
	String file_name = dir->get_next();
	while (!file_name.is_empty()) {
		if (!dir->current_is_dir() && file_name.begins_with(basename) && file_name.get_extension() == extension) {
			const String full_path = base_dir.path_join(file_name).simplify_path();
			const uint64_t modified_time = FileAccess::get_modified_time(full_path);
			if (!found_match || modified_time > latest_modified_time || (modified_time == latest_modified_time && full_path == p_base_log_path)) {
				r_latest_log_path = full_path;
				latest_modified_time = modified_time;
				found_match = true;
			}
		}
		file_name = dir->get_next();
	}
	dir->list_dir_end();

	return found_match ? OK : ERR_FILE_NOT_FOUND;
}

Error CLILatestLogRunner::read_log(const String &p_log_path, int p_lines, ReadResult &r_result, String &r_error) {
	r_result = ReadResult();
	r_error = String();

	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_log_path, FileAccess::READ, &err);
	if (file.is_null() || err != OK) {
		r_error = vformat("Unable to open log file \"%s\" for reading (error code %d).", p_log_path, (int)err);
		return err == OK ? ERR_CANT_OPEN : err;
	}

	const uint64_t file_length = file->get_length();
	if (p_lines == 0) {
		while (file->get_position() < file_length) {
			if (!r_result.text.is_empty()) {
				r_result.text += "\n";
			}
			r_result.text += file->get_line();
			r_result.total_lines++;
		}
		r_result.shown_lines = r_result.total_lines;
		return OK;
	}

	Vector<String> tail_lines;
	tail_lines.resize(p_lines);
	int next_slot = 0;
	while (file->get_position() < file_length) {
		tail_lines.write[next_slot] = file->get_line();
		next_slot = (next_slot + 1) % p_lines;
		r_result.total_lines++;
	}

	r_result.shown_lines = MIN(r_result.total_lines, p_lines);
	r_result.truncated = r_result.total_lines > r_result.shown_lines;
	for (int i = 0; i < r_result.shown_lines; i++) {
		const int index = (r_result.total_lines - r_result.shown_lines + i) % p_lines;
		if (!r_result.text.is_empty()) {
			r_result.text += "\n";
		}
		r_result.text += tail_lines[index];
	}

	return OK;
}

int CLILatestLogRunner::run(const Options &p_options, const String &p_log_file_override) {
	const String base_log_path = resolve_base_log_path(p_log_file_override);
	if (base_log_path.is_empty()) {
		OS::get_singleton()->printerr("Error: Unable to resolve the configured project log path.\n");
		return EXIT_INVALID_ARGUMENTS;
	}

	String latest_log_path;
	if (find_latest_log_path(base_log_path, latest_log_path) != OK) {
		OS::get_singleton()->printerr("Error: No log files were found for \"%s\".\n", base_log_path.utf8().get_data());
		return EXIT_NOT_FOUND;
	}

	ReadResult read_result;
	String read_error;
	if (read_log(latest_log_path, p_options.lines, read_result, read_error) != OK) {
		OS::get_singleton()->printerr("Error: %s\n", read_error.utf8().get_data());
		return EXIT_READ_FAILED;
	}

	String output;
	output += "[latest-log] path=" + latest_log_path + "\n";
	output += vformat("[latest-log] lines=%d/%d", read_result.shown_lines, read_result.total_lines);
	if (read_result.truncated) {
		output += " truncated=true";
	}
	output += "\n";
	if (!read_result.text.is_empty()) {
		output += read_result.text;
		if (!output.ends_with("\n")) {
			output += "\n";
		}
	}

	_print_stdout(output);
	return EXIT_OK;
}
