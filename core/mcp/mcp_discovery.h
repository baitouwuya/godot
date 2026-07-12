/**************************************************************************/
/*  mcp_discovery.h                                                       */
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

#pragma once

#include "core/error/error_list.h"
#include "core/mcp/mcp_project_identity.h"
#include "core/variant/dictionary.h"

struct MCPDiscoveryRecord {
	static constexpr int SCHEMA_VERSION = 1;

	int schema_version = SCHEMA_VERSION;
	String project_id;
	String project_path;
	String instance_id;
	int64_t pid = 0;
	uint64_t started_at_unix_ms = 0;
	uint64_t heartbeat_at_unix_ms = 0;
	String transport;
	String endpoint;
	String protocol_version;
	String auth_secret;

	bool is_valid(bool p_require_endpoint = false) const;
	Dictionary to_dictionary(bool p_include_secret = false) const;

	static Error from_dictionary(const Dictionary &p_dictionary, MCPDiscoveryRecord &r_record, String *r_error = nullptr);
	static MCPDiscoveryRecord create(const MCPProjectIdentity &p_identity, const String &p_transport, const String &p_endpoint, const String &p_protocol_version, const String &p_auth_secret = String(), uint64_t p_now_unix_ms = 0);
};

class MCPDiscovery {
public:
	static constexpr uint64_t MAX_RECORD_BYTES = 64 * 1024;

	static uint64_t get_current_unix_time_ms();
	static String get_record_path(const String &p_discovery_directory, const String &p_project_id);

	static Error write_record(const String &p_discovery_directory, const MCPDiscoveryRecord &p_record, bool p_include_secret = false, String *r_error = nullptr);
	static Error read_record(const String &p_discovery_directory, const String &p_project_id, MCPDiscoveryRecord &r_record, String *r_error = nullptr);
	static Error remove_record(const String &p_discovery_directory, const String &p_project_id, const String &p_expected_instance_id = String(), String *r_error = nullptr);

	static Error write_record_file(const String &p_path, const MCPDiscoveryRecord &p_record, bool p_include_secret = false, String *r_error = nullptr);
	static Error read_record_file(const String &p_path, MCPDiscoveryRecord &r_record, String *r_error = nullptr);
};
