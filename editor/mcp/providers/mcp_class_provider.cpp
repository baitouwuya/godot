/**************************************************************************/
/*  mcp_class_provider.cpp                                                */
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

#include "mcp_class_provider.h"

#include "mcp_class_documentation.h"
#include "mcp_class_tool_utils.h"
#include "mcp_tool_utils.h"

#include "core/doc_data.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "editor/doc/doc_tools.h"
#include "editor/doc/editor_help.h"

namespace {

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

static HashMap<String, DocData::ClassDoc> _get_undocumented_global_classes(DocTools *p_docs) {
	HashMap<String, DocData::ClassDoc> classes;
	LocalVector<StringName> names;
	ScriptServer::get_global_class_list(names);
	for (const StringName &name : names) {
		if (p_docs && p_docs->class_list.has(name)) {
			continue;
		}
		DocData::ClassDoc doc;
		doc.name = name;
		doc.inherits = ScriptServer::get_global_class_base(name);
		doc.is_script_doc = true;
		doc.script_path = ScriptServer::get_global_class_path(name);
		classes.insert(doc.name, doc);
	}
	return classes;
}

} // namespace

MCPClassProvider::MCPClassProvider(DocTools *p_doc_tools) :
		doc_tools_override(p_doc_tools) {
}

MCPClassProvider::~MCPClassProvider() {
	unregister_tools();
}

DocTools *MCPClassProvider::_get_doc_tools() const {
	return doc_tools_override ? doc_tools_override : EditorHelp::get_doc_data();
}

Error MCPClassProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Class documentation tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
		{ "godot.class.search", "Search native and project script classes.",
				MCPClassToolUtils::search_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPClassProvider::search), MCPClassToolUtils::search_output_schema() },
		{ "godot.class.get_documentation", "Read structured Godot class documentation.",
				MCPClassToolUtils::documentation_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPClassProvider::get_documentation), MCPClassToolUtils::documentation_output_schema() },
	};
	const Error err = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (err != OK) {
		return err;
	}
	tool_registry = p_registry;
	return OK;
}

void MCPClassProvider::unregister_tools() {
	if (!tool_registry) {
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
}

Dictionary MCPClassProvider::search(const Dictionary &p_arguments, const Dictionary &) {
	MCPClassDocumentation::SearchOptions options;
	String argument_error;
	if (!MCPClassToolUtils::parse_search_options(p_arguments, options, argument_error)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}

	Dictionary result;
	String documentation_error;
	DocTools *docs = _get_doc_tools();
	const HashMap<String, DocData::ClassDoc> supplemental_docs = _get_undocumented_global_classes(docs);
	const Error err = MCPClassDocumentation::search(docs, options, supplemental_docs, result, &documentation_error);
	return err == OK ? MCPToolUtils::make_success_result(result) : MCPClassToolUtils::make_documentation_error_result(err, documentation_error);
}

Dictionary MCPClassProvider::get_documentation(const Dictionary &p_arguments, const Dictionary &) {
	MCPClassDocumentation::RenderOptions options;
	String argument_error;
	if (!MCPClassToolUtils::parse_render_options(p_arguments, options, argument_error)) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", argument_error);
	}

	DocTools *docs = _get_doc_tools();
	const DocData::ClassDoc *class_doc = docs ? docs->class_list.getptr(options.class_name) : nullptr;
	const bool undocumented_global_class = !class_doc && ScriptServer::is_global_class(options.class_name);
	if ((class_doc && class_doc->is_script_doc) || undocumented_global_class) {
		Dictionary details;
		details["className"] = options.class_name;
		const String script_path = class_doc ? class_doc->script_path : ScriptServer::get_global_class_path(options.class_name);
		if (!script_path.is_empty()) {
			details["path"] = script_path;
		}
		details["tool"] = "godot.script.get";
		return MCPToolUtils::make_error_result("USE_SCRIPT_DOCUMENTATION_TOOL",
				"Use godot.script.get so unsaved ScriptEditor text remains authoritative.", details);
	}

	Dictionary result;
	String documentation_error;
	const Error err = MCPClassDocumentation::render(docs, options, result, &documentation_error);
	return err == OK ? MCPToolUtils::make_success_result(result) : MCPClassToolUtils::make_documentation_error_result(err, documentation_error);
}
