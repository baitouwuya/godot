/**************************************************************************/
/*  mcp_script_member_selector.h                                          */
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

#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/language_server/godot_lsp.h"

class ExtendGDScriptParser;

struct MCPScriptMemberMatch {
	const LSP::DocumentSymbol *symbol = nullptr;
	const GDScriptParser::ParameterNode *parameter_node = nullptr;
	String kind;
	String owner_path;
	String callable_name;
	bool rest_parameter = false;
};

struct MCPScriptSourceSlice {
	String text;
	LSP::Position start;
};

class MCPScriptMemberSelector {
public:
	static Error find(const ExtendGDScriptParser &p_parser, const Dictionary &p_query, MCPScriptMemberMatch &r_match, String *r_error = nullptr);
	static MCPScriptSourceSlice parameter_source_slice(const ExtendGDScriptParser &p_parser,
			const GDScriptParser::ParameterNode *p_parameter, bool p_rest);
};
