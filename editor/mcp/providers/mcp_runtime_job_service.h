/**************************************************************************/
/*  mcp_runtime_job_service.h                                            */
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

#pragma once

#include "mcp_runtime_debugger_gateway.h"

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class MCPRuntimeJobService {
public:
	struct Config {
		String id_prefix;
		String capture;
		String rollback_operation;
		String not_found_code;
		String not_found_message;
		String stale_code;
		String stale_message;
		String limit_code;
		String limit_message;
		int max_records = 0;
	};

private:
	struct JobRecord {
		String mcp_session_id;
		int debugger_session = -1;
		uint64_t runtime_generation = 0;
		Dictionary state;
	};

	MCPRuntimeDebuggerGateway *runtime_gateway = nullptr;
	Config config;
	uint64_t next_job_id = 1;
	HashMap<String, JobRecord> jobs;
	Vector<String> job_order;

	void _prune_records();
	Dictionary _resolve_record(const Dictionary &p_arguments, const String &p_mcp_session_id,
			String &r_job_id, JobRecord *&r_record);

public:
	MCPRuntimeJobService(MCPRuntimeDebuggerGateway *p_runtime_gateway, const Config &p_config);

	Dictionary start(const Dictionary &p_arguments, const String &p_mcp_session_id, Dictionary p_payload);
	Dictionary get_status(const Dictionary &p_arguments, const String &p_mcp_session_id);
	Dictionary finish(const Dictionary &p_arguments, const String &p_mcp_session_id, const String &p_operation,
			const StringName &p_terminal_result_field = StringName());
	void process();
	void release_session(const String &p_mcp_session_id);
	void release_all(const String &p_reason);
};
