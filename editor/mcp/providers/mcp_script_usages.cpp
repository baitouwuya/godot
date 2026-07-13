/**************************************************************************/
/*  mcp_script_usages.cpp                                                 */
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

#include "mcp_script_usages.h"

#include "mcp_script_member_selector.h"

#include "modules/gdscript/language_server/gdscript_analysis_session.h"
#include "modules/gdscript/language_server/gdscript_extend_parser.h"
#include "modules/gdscript/language_server/gdscript_workspace.h"

Error MCPScriptUsages::find(const Ref<GDScriptWorkspace> &p_workspace, const Ref<GDScriptAnalysisSession> &p_session,
		const ExtendGDScriptParser &p_parser, const Dictionary &p_member, bool p_include_declaration,
		Dictionary &r_result, String *r_error) {
	r_result = Dictionary();
	if (r_error) {
		*r_error = String();
	}
	if (p_workspace.is_null() || p_session.is_null()) {
		if (r_error) {
			*r_error = "A GDScript workspace and analysis session are required.";
		}
		return ERR_UNCONFIGURED;
	}

	MCPScriptMemberMatch match;
	const Error selection_error = MCPScriptMemberSelector::find(p_parser, p_member, match, r_error);
	if (selection_error != OK) {
		return selection_error;
	}
	if (!match.symbol) {
		if (r_error) {
			*r_error = "The selected script member does not have an LSP symbol.";
		}
		return ERR_BUG;
	}

	const Vector<LSP::Location> usages = p_workspace->find_all_usages(p_session, *match.symbol);
	Array locations;
	for (const LSP::Location &usage : usages) {
		if (!p_include_declaration && p_workspace->is_declaration_location(*match.symbol, usage)) {
			continue;
		}
		locations.push_back(usage.to_json());
	}

	Dictionary member;
	member["kind"] = match.kind;
	member["name"] = match.symbol->name;
	if (match.kind == "parameter") {
		member["owner"] = match.callable_name;
		if (!match.owner_path.is_empty()) {
			member["classPath"] = match.owner_path;
		}
	} else if (!match.owner_path.is_empty()) {
		member["owner"] = match.owner_path;
	}
	r_result["member"] = member;
	r_result["locations"] = locations;
	r_result["count"] = locations.size();
	return OK;
}
