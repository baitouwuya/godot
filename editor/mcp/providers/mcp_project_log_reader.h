/**************************************************************************/
/*  mcp_project_log_reader.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"

class MCPProjectLogReader {
public:
	struct Result {
		String path;
		String text;
		int total_lines = 0;
		int shown_lines = 0;
		bool truncated = false;
	};

	static String resolve_configured_base_path();
	static Error find_latest_log(const String &p_base_path, String &r_path, String *r_error = nullptr);
	static Error read_latest(int p_max_lines, Result &r_result, String *r_error = nullptr);
	static Error read_latest_from_base(const String &p_base_path, int p_max_lines, Result &r_result, String *r_error = nullptr);
};
