/**************************************************************************/
/*  test_mcp_tool_utils.cpp                                               */
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

#include "core/io/json.h"
#include "core/object/property_info.h"
#include "editor/mcp/providers/mcp_tool_utils.h"
#include "tests/test_macros.h"

#include <limits>

TEST_FORCE_LINK(test_mcp_tool_utils);

namespace TestMCPToolUtils {

TEST_CASE("[MCP][Provider] Shared schema builders preserve closed object contracts") {
	Dictionary properties;
	properties["mode"] = MCPToolUtils::make_enum_schema(PackedStringArray{ "summary", "full" }, "Result detail.", "summary");
	properties["limit"] = MCPToolUtils::make_property_schema("integer", "Maximum results.");

	const Dictionary schema = MCPToolUtils::make_object_schema(properties, PackedStringArray{ "mode" });
	CHECK(schema.get("type", String()) == "object");
	CHECK_FALSE(bool(schema.get("additionalProperties", true)));
	CHECK(PackedStringArray(schema.get("required", PackedStringArray())) == PackedStringArray{ "mode" });
	const Dictionary stored_properties = schema.get("properties", Dictionary());
	const Dictionary mode = stored_properties.get("mode", Dictionary());
	CHECK(mode.get("type", String()) == "string");
	CHECK(PackedStringArray(mode.get("enum", PackedStringArray())) == PackedStringArray{ "summary", "full" });
	CHECK(mode.get("default", String()) == "summary");
	CHECK(mode.get("description", String()) == "Result detail.");

	const Dictionary empty_schema = MCPToolUtils::make_object_schema();
	CHECK_FALSE(empty_schema.has("required"));
	CHECK_FALSE(bool(empty_schema.get("additionalProperties", true)));
	const Dictionary open_schema = MCPToolUtils::make_object_schema(Dictionary(), PackedStringArray(), true);
	CHECK(bool(open_schema.get("additionalProperties", false)));
}

TEST_CASE("[MCP][Provider] Tool definitions expose output schemas and standard behavior annotations") {
	Dictionary input_schema;
	input_schema["type"] = "object";

	Dictionary output_schema;
	output_schema["type"] = "object";
	Dictionary output_properties;
	Dictionary count_schema;
	count_schema["type"] = "integer";
	output_properties["count"] = count_schema;
	output_schema["properties"] = output_properties;

	const Dictionary read_definition = MCPToolUtils::make_tool_definition(
			"test.read", "Read test data.", input_schema, MCPToolUtils::TOOL_READ_ONLY, output_schema);
	CHECK(Dictionary(read_definition.get("outputSchema", Dictionary())) == output_schema);
	const Dictionary read_annotations = read_definition.get("annotations", Dictionary());
	CHECK(bool(read_annotations.get("readOnlyHint", false)));
	CHECK_FALSE(bool(read_annotations.get("destructiveHint", true)));
	CHECK(bool(read_annotations.get("idempotentHint", false)));
	CHECK_FALSE(bool(read_annotations.get("openWorldHint", true)));

	const Dictionary additive_definition = MCPToolUtils::make_tool_definition(
			"test.add", "Add test data.", input_schema, MCPToolUtils::TOOL_ADDITIVE);
	const Dictionary additive_annotations = additive_definition.get("annotations", Dictionary());
	CHECK_FALSE(bool(additive_annotations.get("readOnlyHint", true)));
	CHECK_FALSE(bool(additive_annotations.get("destructiveHint", true)));
	CHECK_FALSE(bool(additive_annotations.get("idempotentHint", true)));
	CHECK_FALSE(bool(additive_annotations.get("openWorldHint", true)));
	const Dictionary default_output = additive_definition.get("outputSchema", Dictionary());
	CHECK(default_output.get("type", String()) == "object");
	CHECK(bool(default_output.get("additionalProperties", false)));

	const Dictionary destructive_definition = MCPToolUtils::make_tool_definition(
			"test.destroy", "Destroy test data.", input_schema, MCPToolUtils::TOOL_DESTRUCTIVE);
	const Dictionary destructive_annotations = destructive_definition.get("annotations", Dictionary());
	CHECK_FALSE(bool(destructive_annotations.get("readOnlyHint", true)));
	CHECK(bool(destructive_annotations.get("destructiveHint", false)));
	CHECK_FALSE(bool(destructive_annotations.get("idempotentHint", true)));
	CHECK_FALSE(bool(destructive_annotations.get("openWorldHint", true)));

	count_schema["type"] = "string";
	output_properties["count"] = count_schema;
	const Dictionary stored_output = read_definition.get("outputSchema", Dictionary());
	const Dictionary stored_properties = stored_output.get("properties", Dictionary());
	const Dictionary stored_count = stored_properties.get("count", Dictionary());
	CHECK(stored_count.get("type", String()) == "integer");
}

TEST_CASE("[MCP][Provider] JSON integers accept exact parser doubles and enforce ranges") {
	int64_t value = 0;
	CHECK(MCPToolUtils::try_get_json_integer(int64_t(-7), -10, 10, value));
	CHECK(value == -7);
	CHECK(MCPToolUtils::try_get_json_integer(3.0, -10, 10, value));
	CHECK(value == 3);
	CHECK_FALSE(MCPToolUtils::try_get_json_integer(3.5, -10, 10, value));
	CHECK_FALSE(MCPToolUtils::try_get_json_integer(std::numeric_limits<double>::infinity(), -10, 10, value));
	CHECK_FALSE(MCPToolUtils::try_get_json_integer(9007199254740992.0, INT64_MIN, INT64_MAX, value));
	CHECK_FALSE(MCPToolUtils::try_get_json_integer("3", -10, 10, value));
	CHECK_FALSE(MCPToolUtils::try_get_json_integer(11.0, -10, 10, value));
	CHECK_FALSE(MCPToolUtils::try_get_json_integer(0, 1, -1, value));
}

TEST_CASE("[MCP][Provider] Property descriptions use a shared stable shape") {
	const PropertyInfo property(Variant::OBJECT, "target", PROPERTY_HINT_NODE_TYPE, "Node", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY, "Node");
	const Dictionary description = MCPToolUtils::make_property_description(property);

	CHECK(description.get("name", String()) == "target");
	CHECK(description.get("type", String()) == "Object");
	CHECK(int(description.get("typeId", -1)) == Variant::OBJECT);
	CHECK(description.get("className", String()) == "Node");
	CHECK(int(description.get("hint", -1)) == PROPERTY_HINT_NODE_TYPE);
	CHECK(description.get("hintString", String()) == "Node");
	CHECK(int64_t(description.get("usage", 0)) == (PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY));
}

TEST_CASE("[MCP][Provider] Success results expose matching text and structured content") {
	Dictionary nested;
	nested["enabled"] = true;

	Dictionary structured_content;
	structured_content["count"] = 3;
	structured_content["nested"] = nested;

	const Dictionary result = MCPToolUtils::make_success_result(structured_content);
	CHECK_FALSE(result.has("isError"));
	REQUIRE(result.get("structuredContent", Variant()).get_type() == Variant::DICTIONARY);
	CHECK(Dictionary(result["structuredContent"]) == structured_content);

	const Array content = result.get("content", Array());
	REQUIRE(content.size() == 1);
	const Dictionary text_content = content[0];
	CHECK(text_content.get("type", String()) == "text");
	const Variant parsed_text = JSON::parse_string(text_content.get("text", String()));
	REQUIRE(parsed_text.get_type() == Variant::DICTIONARY);
	const Dictionary parsed_dictionary = parsed_text;
	CHECK(double(parsed_dictionary.get("count", 0.0)) == 3.0);
	CHECK(bool(Dictionary(parsed_dictionary.get("nested", Dictionary())).get("enabled", false)));

	structured_content["count"] = 9;
	CHECK(int(Dictionary(result["structuredContent"]).get("count", 0)) == 3);
}

TEST_CASE("[MCP][Provider] Large success results avoid duplicating structured payloads as text") {
	Dictionary structured_content;
	structured_content["count"] = 1;
	structured_content["payload"] = String("x").repeat(32 * 1024);

	const Dictionary result = MCPToolUtils::make_success_result(structured_content);
	CHECK(Dictionary(result.get("structuredContent", Dictionary())) == structured_content);
	const Array content = result.get("content", Array());
	REQUIRE(content.size() == 1);
	const String text = Dictionary(content[0]).get("text", String());
	CHECK_LT(text.to_utf8_buffer().size(), 1024);
	const Dictionary summary = JSON::parse_string(text);
	CHECK(bool(summary.get("_truncated", false)));
	CHECK_GT(int64_t(summary.get("structuredContentBytes", 0)), int64_t(32 * 1024));
	CHECK(int64_t(summary.get("count", 0)) == 1);
	CHECK_FALSE(summary.has("payload"));
}

TEST_CASE("[MCP][Provider] Error results use a stable structured error shape") {
	Dictionary details;
	details["path"] = "res://example.txt";

	const Dictionary result = MCPToolUtils::make_error_result("FILE_EXISTS", "The file exists.", details);
	CHECK(bool(result.get("isError", false)));

	const Dictionary structured_content = result.get("structuredContent", Dictionary());
	const Dictionary error = structured_content.get("error", Dictionary());
	CHECK(error.get("code", String()) == "FILE_EXISTS");
	CHECK(error.get("message", String()) == "The file exists.");
	CHECK(Dictionary(error.get("details", Dictionary())).get("path", String()) == "res://example.txt");

	const Array content = result.get("content", Array());
	REQUIRE(content.size() == 1);
	CHECK(Dictionary(content[0]).get("text", String()) == "The file exists.");
}

} // namespace TestMCPToolUtils
