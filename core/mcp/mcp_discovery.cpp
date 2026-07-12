/**************************************************************************/
/*  mcp_discovery.cpp                                                     */
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

#include "mcp_discovery.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"

namespace {

void set_error(String *r_error, const String &p_error) {
	if (r_error) {
		*r_error = p_error;
	}
}

Error get_required_string(const Dictionary &p_dictionary, const StringName &p_key, String &r_value, String *r_error) {
	if (!p_dictionary.has(p_key) || p_dictionary[p_key].get_type() != Variant::STRING) {
		set_error(r_error, vformat("MCP discovery field '%s' must be a string.", p_key));
		return ERR_INVALID_DATA;
	}
	r_value = p_dictionary[p_key];
	if (r_value.is_empty()) {
		set_error(r_error, vformat("MCP discovery field '%s' cannot be empty.", p_key));
		return ERR_INVALID_DATA;
	}
	return OK;
}

Error get_required_uint64(const Dictionary &p_dictionary, const StringName &p_key, uint64_t &r_value, String *r_error) {
	if (!p_dictionary.has(p_key)) {
		set_error(r_error, vformat("MCP discovery field '%s' must be an integer.", p_key));
		return ERR_INVALID_DATA;
	}
	const Variant value_variant = p_dictionary[p_key];
	if (value_variant.get_type() == Variant::INT) {
		const int64_t value = value_variant;
		if (value < 0) {
			set_error(r_error, vformat("MCP discovery field '%s' cannot be negative.", p_key));
			return ERR_INVALID_DATA;
		}
		r_value = (uint64_t)value;
		return OK;
	}
	if (value_variant.get_type() != Variant::FLOAT) {
		set_error(r_error, vformat("MCP discovery field '%s' must be an integer.", p_key));
		return ERR_INVALID_DATA;
	}

	constexpr double MAX_SAFE_JSON_INTEGER = 9007199254740991.0;
	const double value = value_variant;
	if (!Math::is_finite(value) || value < 0.0 || value > MAX_SAFE_JSON_INTEGER || Math::floor(value) != value) {
		set_error(r_error, vformat("MCP discovery field '%s' must be a non-negative JSON-safe integer.", p_key));
		return ERR_INVALID_DATA;
	}
	r_value = (uint64_t)value;
	return OK;
}

Error get_required_int64(const Dictionary &p_dictionary, const StringName &p_key, int64_t &r_value, String *r_error) {
	uint64_t value = 0;
	const Error error = get_required_uint64(p_dictionary, p_key, value, r_error);
	if (error != OK) {
		return error;
	}
	if (value > (uint64_t)INT64_MAX) {
		set_error(r_error, vformat("MCP discovery field '%s' exceeds the supported integer range.", p_key));
		return ERR_INVALID_DATA;
	}
	r_value = (int64_t)value;
	return OK;
}

} // namespace

bool MCPDiscoveryRecord::is_valid(bool p_require_endpoint) const {
	return schema_version == SCHEMA_VERSION && MCPProjectIdentity::is_valid_project_id(project_id) && project_path.is_absolute_path() && project_id == MCPProjectIdentity::make_project_id(project_path) && MCPProjectIdentity::is_valid_instance_id(instance_id) && pid > 0 && started_at_unix_ms > 0 && heartbeat_at_unix_ms > 0 && !transport.is_empty() && !protocol_version.is_empty() && (!p_require_endpoint || !endpoint.is_empty());
}

Dictionary MCPDiscoveryRecord::to_dictionary(bool p_include_secret) const {
	Dictionary dictionary;
	dictionary["schemaVersion"] = schema_version;
	dictionary["projectId"] = project_id;
	dictionary["projectPath"] = project_path;
	dictionary["instanceId"] = instance_id;
	dictionary["pid"] = pid;
	dictionary["startedAtUnixMs"] = started_at_unix_ms;
	dictionary["heartbeatAtUnixMs"] = heartbeat_at_unix_ms;
	dictionary["transport"] = transport;
	dictionary["endpoint"] = endpoint;
	dictionary["protocolVersion"] = protocol_version;
	if (p_include_secret && !auth_secret.is_empty()) {
		dictionary["authSecret"] = auth_secret;
	}
	return dictionary;
}

Error MCPDiscoveryRecord::from_dictionary(const Dictionary &p_dictionary, MCPDiscoveryRecord &r_record, String *r_error) {
	r_record = MCPDiscoveryRecord();
	set_error(r_error, String());

	int64_t schema_version = 0;
	Error error = get_required_int64(p_dictionary, "schemaVersion", schema_version, r_error);
	if (error != OK) {
		return error;
	}
	if (schema_version != SCHEMA_VERSION) {
		set_error(r_error, vformat("Unsupported MCP discovery schema version %d.", schema_version));
		return ERR_UNAVAILABLE;
	}
	r_record.schema_version = (int)schema_version;

	error = get_required_string(p_dictionary, "projectId", r_record.project_id, r_error);
	if (error != OK) {
		return error;
	}
	if (!MCPProjectIdentity::is_valid_project_id(r_record.project_id)) {
		set_error(r_error, "MCP discovery field 'projectId' must be a lowercase SHA-256 digest.");
		return ERR_INVALID_DATA;
	}
	if ((error = get_required_string(p_dictionary, "projectPath", r_record.project_path, r_error)) != OK ||
			(error = get_required_string(p_dictionary, "instanceId", r_record.instance_id, r_error)) != OK ||
			(error = get_required_string(p_dictionary, "transport", r_record.transport, r_error)) != OK ||
			(error = get_required_string(p_dictionary, "protocolVersion", r_record.protocol_version, r_error)) != OK) {
		return error;
	}
	r_record.project_path = r_record.project_path.replace_char('\\', '/').simplify_path();
	if (!r_record.project_path.is_absolute_path() || r_record.project_id != MCPProjectIdentity::make_project_id(r_record.project_path)) {
		set_error(r_error, "MCP discovery fields 'projectId' and 'projectPath' do not identify the same project.");
		return ERR_INVALID_DATA;
	}
	if (!MCPProjectIdentity::is_valid_instance_id(r_record.instance_id)) {
		set_error(r_error, "MCP discovery field 'instanceId' must be a lowercase 128-bit hexadecimal ID.");
		return ERR_INVALID_DATA;
	}

	if ((error = get_required_int64(p_dictionary, "pid", r_record.pid, r_error)) != OK) {
		return error;
	}
	if (r_record.pid <= 0) {
		set_error(r_error, "MCP discovery field 'pid' must be positive.");
		return ERR_INVALID_DATA;
	}

	if ((error = get_required_uint64(p_dictionary, "startedAtUnixMs", r_record.started_at_unix_ms, r_error)) != OK ||
			(error = get_required_uint64(p_dictionary, "heartbeatAtUnixMs", r_record.heartbeat_at_unix_ms, r_error)) != OK) {
		return error;
	}
	if (r_record.started_at_unix_ms == 0 || r_record.heartbeat_at_unix_ms == 0) {
		set_error(r_error, "MCP discovery timestamps must be positive.");
		return ERR_INVALID_DATA;
	}

	if (p_dictionary.has("endpoint")) {
		if (p_dictionary["endpoint"].get_type() != Variant::STRING) {
			set_error(r_error, "MCP discovery field 'endpoint' must be a string.");
			return ERR_INVALID_DATA;
		}
		r_record.endpoint = p_dictionary["endpoint"];
	}
	if (p_dictionary.has("authSecret")) {
		if (p_dictionary["authSecret"].get_type() != Variant::STRING) {
			set_error(r_error, "MCP discovery field 'authSecret' must be a string.");
			return ERR_INVALID_DATA;
		}
		r_record.auth_secret = p_dictionary["authSecret"];
	}
	return OK;
}

MCPDiscoveryRecord MCPDiscoveryRecord::create(const MCPProjectIdentity &p_identity, const String &p_transport, const String &p_endpoint, const String &p_protocol_version, const String &p_auth_secret, uint64_t p_now_unix_ms) {
	MCPDiscoveryRecord record;
	record.project_id = p_identity.project_id;
	record.project_path = p_identity.canonical_path;
	record.instance_id = p_identity.instance_id;
	record.pid = OS::get_singleton()->get_process_id();
	const uint64_t now_unix_ms = p_now_unix_ms == 0 ? MCPDiscovery::get_current_unix_time_ms() : p_now_unix_ms;
	record.started_at_unix_ms = now_unix_ms;
	record.heartbeat_at_unix_ms = now_unix_ms;
	record.transport = p_transport;
	record.endpoint = p_endpoint;
	record.protocol_version = p_protocol_version;
	record.auth_secret = p_auth_secret;
	return record;
}

uint64_t MCPDiscovery::get_current_unix_time_ms() {
	return (uint64_t)(OS::get_singleton()->get_unix_time() * 1000.0);
}

String MCPDiscovery::get_record_path(const String &p_discovery_directory, const String &p_project_id) {
	if (!MCPProjectIdentity::is_valid_project_id(p_project_id)) {
		return String();
	}
	return p_discovery_directory.path_join(p_project_id + ".json");
}

Error MCPDiscovery::write_record(const String &p_discovery_directory, const MCPDiscoveryRecord &p_record, bool p_include_secret, String *r_error) {
	if (!p_record.is_valid(true)) {
		set_error(r_error, "Cannot publish an incomplete MCP discovery record.");
		return ERR_INVALID_DATA;
	}
	const String record_path = get_record_path(p_discovery_directory, p_record.project_id);
	if (record_path.is_empty()) {
		set_error(r_error, "Cannot build an MCP discovery path from an invalid project ID.");
		return ERR_INVALID_PARAMETER;
	}
	return write_record_file(record_path, p_record, p_include_secret, r_error);
}

Error MCPDiscovery::read_record(const String &p_discovery_directory, const String &p_project_id, MCPDiscoveryRecord &r_record, String *r_error) {
	const String record_path = get_record_path(p_discovery_directory, p_project_id);
	if (record_path.is_empty()) {
		set_error(r_error, "Cannot build an MCP discovery path from an invalid project ID.");
		return ERR_INVALID_PARAMETER;
	}
	return read_record_file(record_path, r_record, r_error);
}

Error MCPDiscovery::remove_record(const String &p_discovery_directory, const String &p_project_id, const String &p_expected_instance_id, String *r_error) {
	set_error(r_error, String());
	const String record_path = get_record_path(p_discovery_directory, p_project_id);
	if (record_path.is_empty()) {
		set_error(r_error, "Cannot build an MCP discovery path from an invalid project ID.");
		return ERR_INVALID_PARAMETER;
	}
	if (!FileAccess::exists(record_path)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (!p_expected_instance_id.is_empty()) {
		MCPDiscoveryRecord existing_record;
		const Error read_error = read_record_file(record_path, existing_record, r_error);
		if (read_error != OK) {
			return read_error;
		}
		if (existing_record.instance_id != p_expected_instance_id) {
			set_error(r_error, "MCP discovery record belongs to another instance.");
			return ERR_UNAUTHORIZED;
		}
	}
	const Error remove_error = DirAccess::remove_absolute(record_path);
	if (remove_error != OK) {
		set_error(r_error, vformat("Cannot remove MCP discovery record '%s'.", record_path));
	}
	return remove_error;
}

Error MCPDiscovery::write_record_file(const String &p_path, const MCPDiscoveryRecord &p_record, bool p_include_secret, String *r_error) {
	set_error(r_error, String());
	if (!p_record.is_valid(false)) {
		set_error(r_error, "Cannot write an invalid MCP record.");
		return ERR_INVALID_DATA;
	}

	const Error directory_error = DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	if (directory_error != OK && directory_error != ERR_ALREADY_EXISTS) {
		set_error(r_error, vformat("Cannot create MCP record directory '%s'.", p_path.get_base_dir()));
		return directory_error;
	}

	const String temporary_path = p_path + "." + p_record.instance_id + "." + String::num_uint64(OS::get_singleton()->get_ticks_usec()) + ".tmp";
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(temporary_path, FileAccess::WRITE, &open_error);
	if (file.is_null()) {
		set_error(r_error, vformat("Cannot open temporary MCP record '%s' for writing.", temporary_path));
		return open_error == OK ? ERR_CANT_OPEN : open_error;
	}
	file->store_string(JSON::stringify(p_record.to_dictionary(p_include_secret), "\t"));
	file->flush();
	const Error write_error = file->get_error();
	file->close();
	if (write_error != OK) {
		DirAccess::remove_absolute(temporary_path);
		set_error(r_error, vformat("Cannot write temporary MCP record '%s'.", temporary_path));
		return write_error;
	}

	const Error rename_error = DirAccess::rename_absolute(temporary_path, p_path);
	if (rename_error != OK) {
		DirAccess::remove_absolute(temporary_path);
		set_error(r_error, vformat("Cannot publish MCP record '%s'.", p_path));
	}
	return rename_error;
}

Error MCPDiscovery::read_record_file(const String &p_path, MCPDiscoveryRecord &r_record, String *r_error) {
	r_record = MCPDiscoveryRecord();
	set_error(r_error, String());

	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ, &open_error);
	if (file.is_null()) {
		set_error(r_error, vformat("Cannot open MCP record '%s'.", p_path));
		return open_error == OK ? ERR_CANT_OPEN : open_error;
	}
	if (file->get_length() > MAX_RECORD_BYTES) {
		set_error(r_error, vformat("MCP record '%s' exceeds the size limit.", p_path));
		return ERR_FILE_CORRUPT;
	}

	JSON json;
	const Error parse_error = json.parse(file->get_as_utf8_string());
	if (parse_error != OK) {
		set_error(r_error, vformat("Cannot parse MCP record '%s': %s", p_path, json.get_error_message()));
		return ERR_PARSE_ERROR;
	}
	if (json.get_data().get_type() != Variant::DICTIONARY) {
		set_error(r_error, vformat("MCP record '%s' must contain a JSON object.", p_path));
		return ERR_INVALID_DATA;
	}
	return MCPDiscoveryRecord::from_dictionary(json.get_data(), r_record, r_error);
}
