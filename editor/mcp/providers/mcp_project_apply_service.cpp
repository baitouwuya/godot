/**************************************************************************/
/*  mcp_project_apply_service.cpp                                         */
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

#include "mcp_project_apply_service.h"

#include "mcp_project_revision.h"
#include "mcp_project_settings_mutation.h"
#include "mcp_tool_utils.h"
#include "mcp_variant_codec.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/templates/vector.h"

namespace {

static constexpr int MAX_APPLY_OPERATIONS = 256;

struct ApplyOperation {
	bool set = false;
	String name;
	Variant value;
};

struct SettingSnapshot {
	String name;
	Variant value;
	int order = -1;
	bool existed = false;
};

static bool _is_string(const Variant &p_value) {
	return p_value.get_type() == Variant::STRING || p_value.get_type() == Variant::STRING_NAME;
}

static bool _read_nonempty_string(const Dictionary &p_arguments, const StringName &p_name, String &r_value) {
	const Variant value = p_arguments.get(p_name, Variant());
	if (!_is_string(value) || String(value).is_empty()) {
		return false;
	}
	r_value = value;
	return true;
}

static bool _is_managed_setting(const String &p_name) {
	static const char *managed_prefixes[] = {
		"autoload/",
		"autoload_prepend/",
		"input/",
		"global_group/",
		"global_shader_parameter/",
	};
	for (const char *prefix : managed_prefixes) {
		if (p_name.begins_with(prefix)) {
			return true;
		}
	}
	return false;
}

static int _find_snapshot(const Vector<SettingSnapshot> &p_snapshots, const String &p_name) {
	for (int i = 0; i < p_snapshots.size(); i++) {
		if (p_snapshots[i].name == p_name) {
			return i;
		}
	}
	return -1;
}

static bool _snapshot_matches(ProjectSettings *p_settings, const SettingSnapshot &p_snapshot) {
	if (!p_snapshot.existed) {
		return !p_settings->has_setting(p_snapshot.name);
	}
	return p_settings->has_setting(p_snapshot.name) &&
			p_settings->get_setting(p_snapshot.name) == p_snapshot.value &&
			p_settings->get_order(p_snapshot.name) == p_snapshot.order;
}

static bool _restore_snapshots(ProjectSettings *p_settings, const Vector<SettingSnapshot> &p_snapshots, String &r_error) {
	r_error = String();
	for (int i = p_snapshots.size() - 1; i >= 0; i--) {
		const SettingSnapshot &snapshot = p_snapshots[i];
		if (snapshot.existed) {
			p_settings->set_setting(snapshot.name, snapshot.value);
			if (p_settings->has_setting(snapshot.name)) {
				p_settings->set_order(snapshot.name, snapshot.order);
			}
		} else if (p_settings->has_setting(snapshot.name)) {
			p_settings->clear(snapshot.name);
		}
	}

	for (const SettingSnapshot &snapshot : p_snapshots) {
		if (_snapshot_matches(p_settings, snapshot)) {
			continue;
		}
		r_error = "Project settings could not be fully restored after save failure.";
		return false;
	}
	return true;
}

static bool _verify_saved_project(const String &p_path, String &r_error) {
	r_error = String();
	if (!FileAccess::exists(p_path)) {
		r_error = "Saved project.godot does not exist: " + p_path;
		return false;
	}
	Error open_error = OK;
	const Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &open_error);
	if (file.is_null() || open_error != OK) {
		r_error = "Saved project.godot is not readable: " + p_path;
		return false;
	}
	return true;
}

static Dictionary _save_failure(ProjectSettings *p_settings, const Vector<SettingSnapshot> &p_snapshots,
		const String &p_message) {
	String rollback_error;
	const bool rollback_succeeded = _restore_snapshots(p_settings, p_snapshots, rollback_error);
	Dictionary details;
	details["projectRevision"] = MCPProjectRevision::compute(p_settings);
	details["rollbackSucceeded"] = rollback_succeeded;
	details["rollbackComplete"] = rollback_succeeded;
	if (!rollback_error.is_empty()) {
		details["rollbackError"] = rollback_error;
	}
	return MCPToolUtils::make_error_result("SAVE_FAILED", p_message, details);
}

} // namespace

Dictionary MCPProjectApplyService::apply(const Dictionary &p_arguments) const {
	String unknown_argument;
	if (!MCPToolUtils::has_only_arguments(p_arguments,
			PackedStringArray{ "settings", "save", "expectedProjectRevision" }, unknown_argument)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "Unknown godot.project.apply argument: " + unknown_argument);
	}

	const Variant settings_value = p_arguments.get("settings", Variant());
	if (settings_value.get_type() != Variant::ARRAY) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "settings must be an array.");
	}
	const Array settings_array = settings_value;
	if (settings_array.is_empty() || settings_array.size() > MAX_APPLY_OPERATIONS) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", vformat("settings must contain between 1 and %d entries.", MAX_APPLY_OPERATIONS));
	}

	const Variant save_value = p_arguments.get("save", true);
	if (save_value.get_type() != Variant::BOOL) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "save must be boolean.");
	}
	const bool save = save_value;

	bool has_expected_revision = false;
	String expected_revision;
	if (p_arguments.has("expectedProjectRevision")) {
		has_expected_revision = true;
		const Variant expected_value = p_arguments["expectedProjectRevision"];
		if (expected_value.get_type() != Variant::STRING) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "expectedProjectRevision must be a SHA-256 string.");
		}
		expected_revision = String(expected_value).to_lower();
		if (expected_revision.length() != 64 || !expected_revision.is_valid_hex_number(false)) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "expectedProjectRevision must contain 64 hexadecimal characters.");
		}
	}

	// Parse and decode every operation before changing ProjectSettings.
	Vector<ApplyOperation> operations;
	operations.resize(settings_array.size());
	for (int i = 0; i < settings_array.size(); i++) {
		if (settings_array[i].get_type() != Variant::DICTIONARY) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", vformat("settings[%d] must be an object.", i));
		}
		const Dictionary entry = settings_array[i];
		String operation_unknown;
		if (!MCPToolUtils::has_only_arguments(entry, PackedStringArray{ "operation", "name", "value" }, operation_unknown)) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", vformat("settings[%d] has unknown argument: %s.", i, operation_unknown));
		}

		const Variant operation_value = entry.get("operation", Variant());
		if (!_is_string(operation_value)) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", vformat("settings[%d].operation must be set or erase.", i));
		}
		const String operation_name = operation_value;
		if (operation_name != "set" && operation_name != "erase") {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", vformat("settings[%d].operation must be set or erase.", i));
		}

		ApplyOperation &operation = operations.write[i];
		operation.set = operation_name == "set";
		if (!_read_nonempty_string(entry, "name", operation.name)) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", vformat("settings[%d].name must be a non-empty string.", i));
		}
		if (_is_managed_setting(operation.name)) {
			return MCPToolUtils::make_error_result("MANAGED_SETTING", "Use the dedicated MCP tools for this managed project-setting namespace: " + operation.name);
		}

		if (operation.set) {
			if (!entry.has("value")) {
				return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", vformat("settings[%d].value is required for set operations.", i));
			}
			String decode_error;
			if (MCPVariantCodec::decode(entry["value"], operation.value, &decode_error) != OK || operation.value.get_type() == Variant::NIL) {
				return MCPToolUtils::make_error_result("INVALID_VALUE", vformat("settings[%d]: %s", i,
						decode_error.is_empty() ? "set values must not be null." : decode_error));
			}
		} else if (entry.has("value")) {
			return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", vformat("settings[%d].value is only valid for set operations.", i));
		}
	}

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return MCPToolUtils::make_error_result("PROJECT_SETTINGS_UNAVAILABLE", "Project settings are unavailable.");
	}
	const String current_revision = MCPProjectRevision::compute(project_settings);
	if (has_expected_revision && expected_revision != current_revision) {
		Dictionary details;
		details["expectedProjectRevision"] = expected_revision;
		details["currentProjectRevision"] = current_revision;
		return MCPToolUtils::make_error_result("PROJECT_REVISION_CONFLICT",
				"Project settings changed after the expected revision was read.", details);
	}

	Vector<SettingSnapshot> snapshots;
	for (const ApplyOperation &operation : operations) {
		if (_find_snapshot(snapshots, operation.name) >= 0) {
			continue;
		}
		SettingSnapshot snapshot;
		snapshot.name = operation.name;
		snapshot.existed = project_settings->has_setting(operation.name);
		if (snapshot.existed) {
			snapshot.value = project_settings->get_setting(operation.name).duplicate(true);
			snapshot.order = project_settings->get_order(operation.name);
		}
		snapshots.push_back(snapshot);
	}

	Array operation_results;
	bool changed = false;
	for (const ApplyOperation &operation : operations) {
		const bool old_existed = project_settings->has_setting(operation.name);
		const Variant old_value = old_existed ? project_settings->get_setting(operation.name) : Variant();
		const int old_order = old_existed ? project_settings->get_order(operation.name) : -1;
		bool operation_changed = false;

		if (operation.set) {
			operation_changed = !old_existed || old_value != operation.value;
			if (operation_changed) {
				MCPProjectSettingsMutation::commit("Apply Project Setting", operation.name, operation.value,
						old_existed, old_value, old_order, MCPProjectSettingsMutation::REFRESH_NONE);
			}
			if (!project_settings->has_setting(operation.name)) {
				return _save_failure(project_settings, snapshots, "Project setting could not be set: " + operation.name);
			}
		} else if (old_existed) {
			operation_changed = true;
			MCPProjectSettingsMutation::commit("Apply Project Setting", operation.name, Variant(), true, old_value,
					old_order, MCPProjectSettingsMutation::REFRESH_NONE);
			if (project_settings->has_setting(operation.name)) {
				return _save_failure(project_settings, snapshots, "Project setting could not be erased: " + operation.name);
			}
		}

		changed = changed || operation_changed;
		Dictionary operation_result;
		operation_result["operation"] = operation.set ? "set" : "erase";
		operation_result["name"] = operation.name;
		operation_result["changed"] = operation_changed;
		if (operation.set && project_settings->has_setting(operation.name)) {
			Variant encoded_value;
			String encoding_error;
			if (MCPVariantCodec::encode(project_settings->get_setting(operation.name), encoded_value, &encoding_error) == OK) {
				operation_result["value"] = encoded_value;
			}
		}
		operation_results.push_back(operation_result);
	}

	bool verified = false;
	if (save) {
		const Error save_error = project_settings->save();
		if (save_error != OK) {
			return _save_failure(project_settings, snapshots, "Could not save project.godot: " + String(error_names[save_error]));
		}
		String verify_error;
		const String project_path = project_settings->get_resource_path().path_join("project.godot");
		if (!_verify_saved_project(project_path, verify_error)) {
			return _save_failure(project_settings, snapshots, verify_error);
		}
		verified = true;
	}

	Dictionary result;
	result["changed"] = changed;
	result["saved"] = save;
	result["projectRevision"] = MCPProjectRevision::compute(project_settings);
	result["verified"] = verified;
	result["operations"] = operation_results;
	return MCPToolUtils::make_success_result(result);
}
