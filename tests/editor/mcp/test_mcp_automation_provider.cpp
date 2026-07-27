/**************************************************************************/
/*  test_mcp_automation_provider.cpp                                      */
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

#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "editor/mcp/providers/mcp_automation_provider.h"
#include "editor/mcp/providers/mcp_automation_tool_utils.h"
#include "editor/mcp/providers/mcp_tool_utils.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_automation_provider);

namespace TestMCPAutomationProvider {

class RecordingProvider : public Object {
public:
	int call_count = 0;
	String session_id;

	Dictionary succeed(const Dictionary &p_arguments, const Dictionary &p_context) {
		call_count++;
		const Dictionary session = p_context.get("session", Dictionary());
		session_id = session.get("sessionId", String());
		Dictionary content;
		content["value"] = p_arguments.get("value", Variant());
		return MCPToolUtils::make_success_result(content);
	}

	Dictionary fail(const Dictionary &, const Dictionary &) {
		call_count++;
		return MCPToolUtils::make_error_result("EXPECTED", "Expected failure.");
	}
};

static Dictionary _definition(const String &p_name) {
	Dictionary definition;
	definition["name"] = p_name;
	return definition;
}

static Dictionary _call(const String &p_name, const Dictionary &p_arguments = Dictionary()) {
	Dictionary call;
	call["name"] = p_name;
	call["arguments"] = p_arguments;
	return call;
}

TEST_CASE("[MCP][Provider] Automation batch composes MCP tools in one session") {
	MCPToolRegistry registry;
	MCPAutomationProvider *automation = memnew(MCPAutomationProvider);
	RecordingProvider *recording = memnew(RecordingProvider);
	REQUIRE(automation->register_tools(&registry) == OK);
	REQUIRE(registry.register_tool(_definition("test.succeed"), callable_mp(recording, &RecordingProvider::succeed), recording) == OK);
	REQUIRE(registry.register_tool(_definition("test.fail"), callable_mp(recording, &RecordingProvider::fail), recording) == OK);

	CHECK(registry.has_tool("godot.automation.batch"));
	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 3);
	const Dictionary input_schema = Dictionary(definitions[0]).get("inputSchema", Dictionary());
	const Dictionary input_properties = input_schema.get("properties", Dictionary());
	CHECK(PackedStringArray(input_schema.get("required", PackedStringArray())).has("calls"));
	CHECK(bool(Dictionary(input_properties.get("stopOnError", Dictionary())).get("default", false)));
	const Dictionary output_schema = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	const Dictionary output_properties = output_schema.get("properties", Dictionary());
	CHECK(output_properties.has("results"));
	CHECK(PackedStringArray(output_schema.get("required", PackedStringArray())).has("failedCount"));
	CHECK_FALSE(bool(output_schema.get("additionalProperties", true)));

	Dictionary first_arguments;
	first_arguments["value"] = 7;
	Array calls;
	calls.push_back(_call("test.succeed", first_arguments));
	calls.push_back(_call("test.succeed"));
	Dictionary arguments;
	arguments["calls"] = calls;

	MCPToolCallContext context;
	context.session["sessionId"] = "session-1";
	const MCPToolRegistry::CallResult result = registry.call_tool("godot.automation.batch", arguments, context);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK_FALSE(bool(result.result.get("isError", false)));
	const Dictionary content = result.result.get("structuredContent", Dictionary());
	CHECK(int(content.get("callCount", 0)) == 2);
	CHECK(int(content.get("completedCount", 0)) == 2);
	CHECK(int(content.get("failedCount", -1)) == 0);
	CHECK(recording->call_count == 2);
	CHECK(recording->session_id == "session-1");

	registry.unregister_tools_for_owner(recording);
	automation->unregister_tools();
	memdelete(recording);
	memdelete(automation);
}

TEST_CASE("[MCP][Provider] Automation batch contract normalizes calls before dispatch") {
	MCPAutomationToolUtils::BatchOptions options;
	String error_message;
	Dictionary call;
	call["name"] = "test.succeed";
	Dictionary arguments;
	arguments["calls"] = Array{ call };
	CHECK(MCPAutomationToolUtils::parse_batch_options(arguments, options, error_message));
	CHECK(options.calls.size() == 1);
	CHECK(options.stop_on_error);
	CHECK(error_message.is_empty());

	arguments["stopOnError"] = false;
	CHECK(MCPAutomationToolUtils::parse_batch_options(arguments, options, error_message));
	CHECK_FALSE(options.stop_on_error);
	arguments["stopOnError"] = "false";
	CHECK_FALSE(MCPAutomationToolUtils::parse_batch_options(arguments, options, error_message));
	CHECK(error_message.contains("stopOnError"));

	call["name"] = MCPAutomationToolUtils::batch_tool_name();
	arguments["calls"] = Array{ call };
	arguments.erase("stopOnError");
	CHECK_FALSE(MCPAutomationToolUtils::parse_batch_options(arguments, options, error_message));
	CHECK(error_message.contains("Recursive"));
	arguments["calls"] = Array();
	CHECK_FALSE(MCPAutomationToolUtils::parse_batch_options(arguments, options, error_message));
}

TEST_CASE("[MCP][Provider] Automation batch validates before mutation and stops on tool errors") {
	MCPToolRegistry registry;
	MCPAutomationProvider *automation = memnew(MCPAutomationProvider);
	RecordingProvider *recording = memnew(RecordingProvider);
	REQUIRE(automation->register_tools(&registry) == OK);
	REQUIRE(registry.register_tool(_definition("test.succeed"), callable_mp(recording, &RecordingProvider::succeed), recording) == OK);
	REQUIRE(registry.register_tool(_definition("test.fail"), callable_mp(recording, &RecordingProvider::fail), recording) == OK);

	MCPToolCallContext context;
	Dictionary malformed_arguments;
	malformed_arguments["calls"] = Array{ _call("test.succeed") };
	malformed_arguments["unexpected"] = true;
	MCPToolRegistry::CallResult result = registry.call_tool("godot.automation.batch", malformed_arguments, context);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(result.result.get("isError", false)));
	Dictionary error = Dictionary(result.result.get("structuredContent", Dictionary())).get("error", Dictionary());
	CHECK(error.get("code", String()) == "INVALID_ARGUMENTS");
	CHECK(Dictionary(error.get("details", Dictionary())).get("keyword", String()) == "additionalProperties");
	CHECK(recording->call_count == 0);

	Array invalid_calls;
	Dictionary malformed_call = _call("test.succeed");
	malformed_call["unexpected"] = true;
	invalid_calls.push_back(malformed_call);
	invalid_calls.push_back(_call("test.missing"));
	Dictionary invalid_arguments;
	invalid_arguments["calls"] = invalid_calls;
	result = registry.call_tool("godot.automation.batch", invalid_arguments, context);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(result.result.get("isError", false)));
	error = Dictionary(result.result.get("structuredContent", Dictionary())).get("error", Dictionary());
	CHECK(error.get("code", String()) == "INVALID_ARGUMENTS");
	CHECK(Dictionary(error.get("details", Dictionary())).get("keyword", String()) == "additionalProperties");
	CHECK(recording->call_count == 0);

	Array calls;
	calls.push_back(_call("test.succeed"));
	calls.push_back(_call("test.fail"));
	calls.push_back(_call("test.succeed"));
	Dictionary arguments;
	arguments["calls"] = calls;
	result = registry.call_tool("godot.automation.batch", arguments, context);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(result.result.get("isError", false)));
	error = Dictionary(result.result.get("structuredContent", Dictionary())).get("error", Dictionary());
	const Dictionary details = error.get("details", Dictionary());
	CHECK(int(details.get("failedIndex", -1)) == 1);
	CHECK(String(details.get("failedTool", String())) == "test.fail");
	CHECK(recording->call_count == 2);

	arguments["stopOnError"] = false;
	result = registry.call_tool("godot.automation.batch", arguments, context);
	REQUIRE(result.status == MCPToolRegistry::CALL_OK);
	CHECK_FALSE(bool(result.result.get("isError", false)));
	const Dictionary content = result.result.get("structuredContent", Dictionary());
	CHECK(int(content.get("failedCount", 0)) == 1);
	CHECK(recording->call_count == 5);

	registry.unregister_tools_for_owner(recording);
	automation->unregister_tools();
	memdelete(recording);
	memdelete(automation);
}

} // namespace TestMCPAutomationProvider
