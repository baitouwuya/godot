/**************************************************************************/
/*  test_mcp_json_schema_validator.cpp                                    */
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

#include "core/mcp/mcp_json_schema_validator.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_json_schema_validator);

namespace TestMCPJSONSchemaValidator {

static Dictionary _property(const String &p_type) {
	Dictionary schema;
	schema["type"] = p_type;
	return schema;
}

static Dictionary _closed_object(const Dictionary &p_properties, const PackedStringArray &p_required = PackedStringArray()) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["additionalProperties"] = false;
	if (!p_required.is_empty()) {
		schema["required"] = p_required;
	}
	return schema;
}

TEST_CASE("[MCP][JSONSchema] Tool schemas are checked before registration") {
	Dictionary limit = _property("integer");
	limit["minimum"] = 10;
	limit["maximum"] = 1;
	Dictionary properties;
	properties["limit"] = limit;
	const Dictionary schema = _closed_object(properties);

	String error;
	CHECK_FALSE(MCPJSONSchemaValidator::validate_schema(schema, &error));
	CHECK(error.contains("/properties/limit"));
	CHECK(error.contains("minimum cannot be greater than maximum"));

	properties["limit"] = _property("unsupported");
	CHECK_FALSE(MCPJSONSchemaValidator::validate_schema(_closed_object(properties), &error));
	CHECK(error.contains("Unknown JSON type 'unsupported'"));
}

TEST_CASE("[MCP][JSONSchema] Required and closed object constraints return actionable details") {
	Dictionary properties;
	properties["alpha"] = _property("string");
	properties["zeta"] = _property("boolean");
	const Dictionary schema = _closed_object(properties, PackedStringArray{ "alpha" });
	MCPJSONSchemaValidator::ValidationError error;

	CHECK_FALSE(MCPJSONSchemaValidator::validate(Dictionary(), schema, error));
	CHECK(error.keyword == "required");
	CHECK(PackedStringArray(error.details.get("missingArguments", PackedStringArray())) == PackedStringArray{ "alpha" });

	Dictionary arguments;
	arguments["alpah"] = true;
	CHECK_FALSE(MCPJSONSchemaValidator::validate(arguments, schema, error));
	CHECK(error.keyword == "required");

	arguments["alpha"] = "value";
	CHECK_FALSE(MCPJSONSchemaValidator::validate(arguments, schema, error));
	CHECK(error.keyword == "additionalProperties");
	CHECK(error.details.get("unknownArgument", String()) == "alpah");
	CHECK(PackedStringArray(error.details.get("validArguments", PackedStringArray())) == PackedStringArray{ "alpha", "zeta" });
}

TEST_CASE("[MCP][JSONSchema] Nested types, bounds, enums, and array items are validated") {
	Dictionary item = _property("string");
	Dictionary values = _property("array");
	values["items"] = item;
	values["minItems"] = 1;
	values["maxItems"] = 2;

	Dictionary mode = _property("string");
	mode["enum"] = PackedStringArray{ "fast", "safe" };
	Dictionary limit = _property("integer");
	limit["minimum"] = 1;
	limit["maximum"] = 10;

	Dictionary properties;
	properties["mode"] = mode;
	properties["limit"] = limit;
	properties["values"] = values;
	const Dictionary schema = _closed_object(properties, PackedStringArray{ "mode", "limit", "values" });

	Dictionary arguments;
	arguments["mode"] = "safe";
	arguments["limit"] = 5.0;
	arguments["values"] = Array{ "one" };
	MCPJSONSchemaValidator::ValidationError error;
	CHECK(MCPJSONSchemaValidator::validate(arguments, schema, error));

	arguments["limit"] = 5.5;
	CHECK_FALSE(MCPJSONSchemaValidator::validate(arguments, schema, error));
	CHECK(error.keyword == "type");
	CHECK(error.instance_path == "/limit");

	arguments["limit"] = 11;
	CHECK_FALSE(MCPJSONSchemaValidator::validate(arguments, schema, error));
	CHECK(error.keyword == "maximum");

	arguments["limit"] = 5;
	arguments["values"] = Array{ "one", 2 };
	CHECK_FALSE(MCPJSONSchemaValidator::validate(arguments, schema, error));
	CHECK(error.keyword == "type");
	CHECK(error.instance_path == "/values/1");
}

TEST_CASE("[MCP][JSONSchema] JSON-serializable packed arrays satisfy array schemas") {
	Dictionary item_schema;
	item_schema["type"] = "string";
	Dictionary schema;
	schema["type"] = "array";
	schema["items"] = item_schema;
	schema["minItems"] = 1;

	MCPJSONSchemaValidator::ValidationError error;
	CHECK(MCPJSONSchemaValidator::validate(PackedStringArray{ "one", "two" }, schema, error));
	CHECK_FALSE(MCPJSONSchemaValidator::validate(PackedStringArray(), schema, error));
	CHECK(error.keyword == "minItems");
	CHECK_FALSE(MCPJSONSchemaValidator::validate(PackedInt32Array{ 1, 2 }, schema, error));
	CHECK(error.keyword == "type");
}

TEST_CASE("[MCP][JSONSchema] JSON Schema type unions are supported") {
	Dictionary value;
	value["type"] = Array{ "string", "number", "boolean" };
	Dictionary properties;
	properties["value"] = value;
	const Dictionary schema = _closed_object(properties, PackedStringArray{ "value" });

	String schema_error;
	CHECK(MCPJSONSchemaValidator::validate_schema(schema, &schema_error));

	Dictionary arguments;
	arguments["value"] = true;
	MCPJSONSchemaValidator::ValidationError error;
	CHECK(MCPJSONSchemaValidator::validate(arguments, schema, error));

	arguments["value"] = Dictionary();
	CHECK_FALSE(MCPJSONSchemaValidator::validate(arguments, schema, error));
	CHECK(error.keyword == "type");
	CHECK(Array(error.details.get("expectedTypes", Array())).size() == 3);
}

TEST_CASE("[MCP][JSONSchema] Composition constraints share the recursive validator") {
	Dictionary properties;
	properties["path"] = _property("string");
	properties["uri"] = _property("string");
	properties["revision"] = _property("integer");
	Dictionary schema = _closed_object(properties);

	Dictionary path_required;
	path_required["required"] = PackedStringArray{ "path" };
	Dictionary uri_required;
	uri_required["required"] = PackedStringArray{ "uri" };
	schema["oneOf"] = Array{ path_required, uri_required };

	MCPJSONSchemaValidator::ValidationError error;
	CHECK_FALSE(MCPJSONSchemaValidator::validate(Dictionary(), schema, error));
	CHECK(error.keyword == "oneOf");
	CHECK(int(error.details.get("matchedSchemas", -1)) == 0);

	Dictionary arguments;
	arguments["path"] = "res://main.gd";
	CHECK(MCPJSONSchemaValidator::validate(arguments, schema, error));

	arguments["uri"] = "file:///main.gd";
	CHECK_FALSE(MCPJSONSchemaValidator::validate(arguments, schema, error));
	CHECK(error.keyword == "oneOf");
	CHECK(int(error.details.get("matchedSchemas", -1)) == 2);

	Dictionary revision_required;
	revision_required["required"] = PackedStringArray{ "revision" };
	Dictionary hash_required;
	hash_required["required"] = PackedStringArray{ "sha256" };
	Dictionary any_schema;
	any_schema["anyOf"] = Array{ revision_required, hash_required };
	Dictionary both;
	both["revision"] = 1;
	both["sha256"] = "abc";
	CHECK(MCPJSONSchemaValidator::validate(both, any_schema, error));

	Dictionary all_schema;
	all_schema["allOf"] = Array{ schema, any_schema };
	arguments.erase("uri");
	arguments["revision"] = 1;
	CHECK(MCPJSONSchemaValidator::validate(arguments, all_schema, error));
}

} // namespace TestMCPJSONSchemaValidator
