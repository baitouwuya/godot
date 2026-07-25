/**************************************************************************/
/*  mcp_log_analyzer.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

class MCPLogAnalyzer {
public:
	struct Options {
		int first_line = 1;
		int total_lines = 0;
		int requested_lines = 0;
		bool truncated = false;
	};

	static Dictionary analyze(const String &p_text);
	static Dictionary analyze(const String &p_text, const Options &p_options);
};
