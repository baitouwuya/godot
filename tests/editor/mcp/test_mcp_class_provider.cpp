/**************************************************************************/
/*  test_mcp_class_provider.cpp                                           */
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

#include "core/doc_data.h"
#include "core/mcp/mcp_tool_registry.h"
#include "editor/doc/doc_tools.h"
#include "editor/mcp/providers/mcp_class_provider.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_class_provider);

namespace TestMCPClassProvider {

static MCPToolRegistry::CallResult _call(MCPToolRegistry &p_registry, const StringName &p_name, const Dictionary &p_arguments) {
	MCPToolCallContext context;
	return p_registry.call_tool(p_name, p_arguments, context);
}

static Dictionary _success(const MCPToolRegistry::CallResult &p_result) {
	INFO(p_result.result);
	REQUIRE(p_result.status == MCPToolRegistry::CALL_OK);
	REQUIRE_FALSE(bool(p_result.result.get("isError", false)));
	return p_result.result.get("structuredContent", Dictionary());
}

static DocTools _make_docs() {
	DocTools docs;

	DocData::ClassDoc node;
	node.name = "Node";
	node.brief_description = "Base node.";
	docs.add_doc(node);

	DocData::ClassDoc fancy;
	fancy.name = "FancyNode";
	fancy.inherits = "Node";
	fancy.brief_description = "A [b]fancy[/b] test node.";
	fancy.description = "Used to test [method FancyNode.jump].";

	DocData::ArgumentDoc force;
	force.name = "force";
	force.type = "float";
	force.default_value = "1.0";
	DocData::MethodDoc jump;
	jump.name = "jump";
	jump.return_type = "bool";
	jump.arguments.push_back(force);
	jump.description = "Jumps with the requested [param force].";
	fancy.methods.push_back(jump);

	DocData::MethodDoc landed;
	landed.name = "landed";
	landed.description = "Emitted after landing.";
	fancy.signals.push_back(landed);

	DocData::PropertyDoc speed;
	speed.name = "speed";
	speed.type = "float";
	speed.default_value = "4.0";
	speed.description = "Movement speed.";
	fancy.properties.push_back(speed);

	DocData::ConstantDoc fast;
	fast.name = "MODE_FAST";
	fast.type = "int";
	fast.value = "1";
	fast.is_value_valid = true;
	fast.enumeration = "FancyNode.Mode";
	fast.description = "Fast mode.";
	fancy.constants.push_back(fast);
	DocData::MethodDoc call_many;
	call_many.name = "call_many";
	call_many.qualifiers = "vararg";
	fancy.methods.push_back(call_many);
	docs.add_doc(fancy);

	DocData::ClassDoc actor;
	actor.name = "ProjectActor";
	actor.inherits = "FancyNode";
	actor.is_script_doc = true;
	actor.script_path = "res://actor.gd";
	actor.brief_description = "Project actor.";
	docs.add_doc(actor);

	DocData::ClassDoc old;
	old.name = "OldNode";
	old.inherits = "Node";
	old.is_deprecated = true;
	docs.add_doc(old);

	return docs;
}

TEST_CASE("[MCP][Provider] Class documentation tools expose strict schemas") {
	DocTools docs = _make_docs();
	MCPToolRegistry registry;
	MCPClassProvider *provider = memnew(MCPClassProvider(&docs));
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);
	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 2);
	CHECK(names[0] == "godot.class.search");
	CHECK(names[1] == "godot.class.get_documentation");

	Dictionary invalid;
	invalid["unknown"] = true;
	const MCPToolRegistry::CallResult result = _call(registry, "godot.class.search", invalid);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(result.result.get("isError", false)));

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] Class search supports fuzzy source and inheritance filters") {
	DocTools docs = _make_docs();
	MCPToolRegistry registry;
	MCPClassProvider provider(&docs);
	REQUIRE(provider.register_tools(&registry) == OK);

	Dictionary fuzzy_arguments;
	fuzzy_arguments["query"] = "  fancy  ";
	Dictionary content = _success(_call(registry, "godot.class.search", fuzzy_arguments));
	const Array fuzzy_matches = content.get("matches", Array());
	REQUIRE(fuzzy_matches.size() == 1);
	CHECK(Dictionary(fuzzy_matches[0]).get("name", String()) == "FancyNode");
	CHECK(String(Dictionary(fuzzy_matches[0]).get("brief", String())).contains("**fancy**"));

	Dictionary script_arguments;
	script_arguments["source"] = "script";
	script_arguments["inherits"] = "Node";
	content = _success(_call(registry, "godot.class.search", script_arguments));
	const Array script_matches = content.get("matches", Array());
	REQUIRE(script_matches.size() == 1);
	const Dictionary script_match = script_matches[0];
	CHECK(script_match.get("name", String()) == "ProjectActor");
	CHECK(script_match.get("scriptPath", String()) == "res://actor.gd");

	Dictionary native_arguments;
	native_arguments["source"] = "native";
	native_arguments["inherits"] = "Node";
	content = _success(_call(registry, "godot.class.search", native_arguments));
	const Array native_matches = content.get("matches", Array());
	REQUIRE(native_matches.size() == 1);
	CHECK(Dictionary(native_matches[0]).get("name", String()) == "FancyNode");
}

TEST_CASE("[MCP][Provider] Class documentation renders compact sections and exact full members") {
	DocTools docs = _make_docs();
	MCPToolRegistry registry;
	MCPClassProvider provider(&docs);
	REQUIRE(provider.register_tools(&registry) == OK);

	Dictionary overview_arguments;
	overview_arguments["name"] = "FancyNode";
	Dictionary content = _success(_call(registry, "godot.class.get_documentation", overview_arguments));
	CHECK(content.get("name", String()) == "FancyNode");
	CHECK(content.get("source", String()) == "native");
	CHECK(String(content.get("description", String())).contains("`FancyNode.jump`"));
	CHECK(Array(content.get("inheritance", Array())).size() == 1);
	const Dictionary counts = content.get("counts", Dictionary());
	CHECK(int(counts.get("methods", 0)) == 2);
	CHECK_FALSE(content.has("methods"));

	Dictionary method_arguments;
	method_arguments["name"] = "FancyNode";
	method_arguments["section"] = "methods";
	method_arguments["member"] = "jump";
	method_arguments["view"] = "full";
	content = _success(_call(registry, "godot.class.get_documentation", method_arguments));
	const Array methods = content.get("methods", Array());
	REQUIRE(methods.size() == 1);
	const Dictionary method = methods[0];
	CHECK(method.get("signature", String()) == "bool jump(force: float = 1.0)");
	CHECK(String(method.get("description", String())).contains("`force`"));

	Dictionary enum_arguments;
	enum_arguments["name"] = "FancyNode";
	enum_arguments["section"] = "enums";
	enum_arguments["member"] = "Mode";
	enum_arguments["view"] = "full";
	content = _success(_call(registry, "godot.class.get_documentation", enum_arguments));
	const Array enums = content.get("enums", Array());
	REQUIRE(enums.size() == 1);
	const Array values = Dictionary(enums[0]).get("values", Array());
	REQUIRE(values.size() == 1);
	CHECK(Dictionary(values[0]).get("name", String()) == "MODE_FAST");

	Dictionary vararg_arguments;
	vararg_arguments["name"] = "FancyNode";
	vararg_arguments["section"] = "methods";
	vararg_arguments["member"] = "call_many";
	content = _success(_call(registry, "godot.class.get_documentation", vararg_arguments));
	const Array vararg_methods = content.get("methods", Array());
	REQUIRE(vararg_methods.size() == 1);
	CHECK(Dictionary(vararg_methods[0]).get("signature", String()) == "call_many(...)");
}

TEST_CASE("[MCP][Provider] Class documentation reports missing classes and members") {
	DocTools docs = _make_docs();
	MCPToolRegistry registry;
	MCPClassProvider provider(&docs);
	REQUIRE(provider.register_tools(&registry) == OK);

	Dictionary missing_class;
	missing_class["name"] = "Missing";
	MCPToolRegistry::CallResult result = _call(registry, "godot.class.get_documentation", missing_class);
	CHECK(bool(result.result.get("isError", false)));

	Dictionary missing_member;
	missing_member["name"] = "FancyNode";
	missing_member["section"] = "methods";
	missing_member["member"] = "missing";
	result = _call(registry, "godot.class.get_documentation", missing_member);
	CHECK(bool(result.result.get("isError", false)));

	Dictionary script_doc;
	script_doc["name"] = "ProjectActor";
	result = _call(registry, "godot.class.get_documentation", script_doc);
	CHECK(bool(result.result.get("isError", false)));
	const Dictionary script_error = result.result.get("structuredContent", Dictionary());
	CHECK(Dictionary(script_error.get("error", Dictionary())).get("code", String()) == "USE_SCRIPT_DOCUMENTATION_TOOL");
}

} // namespace TestMCPClassProvider
