/**************************************************************************/
/*  mcp_tool_utils.cpp                                                    */
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

#include "mcp_tool_utils.h"

#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/property_info.h"

namespace MCPToolUtils {

static constexpr uint64_t MAX_INLINE_STRUCTURED_TEXT_BYTES = 16 * 1024;
static constexpr int MAX_SUMMARY_FIELDS = 24;
static constexpr int MAX_SUMMARY_STRING_CHARACTERS = 512;

static Array _make_text_content(const String &p_text) {
	Dictionary text_content;
	text_content["type"] = "text";
	text_content["text"] = p_text;

	Array content;
	content.push_back(text_content);
	return content;
}

Dictionary make_tool_definition(const String &p_name, const String &p_description, const Dictionary &p_input_schema, ToolBehavior p_behavior, const Dictionary &p_output_schema) {
	Dictionary output_schema = p_output_schema;
	if (output_schema.is_empty()) {
		output_schema["type"] = "object";
		output_schema["additionalProperties"] = true;
	}

	const bool read_only = p_behavior == TOOL_READ_ONLY;
	Dictionary annotations;
	annotations["readOnlyHint"] = read_only;
	annotations["destructiveHint"] = p_behavior == TOOL_DESTRUCTIVE;
	annotations["idempotentHint"] = read_only;
	annotations["openWorldHint"] = false;

	Dictionary definition;
	definition["name"] = p_name;
	definition["description"] = p_description;
	definition["inputSchema"] = p_input_schema.duplicate(true);
	definition["outputSchema"] = output_schema.duplicate(true);
	definition["annotations"] = annotations;
	return definition;
}

Error register_tools(MCPToolRegistry *p_registry, Object *p_owner, const LocalVector<ToolDescriptor> &p_tools, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	ERR_FAIL_NULL_V(p_registry, ERR_INVALID_PARAMETER);
	ERR_FAIL_NULL_V(p_owner, ERR_INVALID_PARAMETER);

	LocalVector<StringName> registered_names;
	for (const ToolDescriptor &tool : p_tools) {
		const Dictionary definition = make_tool_definition(tool.name, tool.description, tool.input_schema, tool.behavior, tool.output_schema);
		const Error error = p_registry->register_tool(definition, tool.handler, p_owner, r_error);
		if (error != OK) {
			for (int i = int(registered_names.size()) - 1; i >= 0; i--) {
				p_registry->unregister_tool(registered_names[i]);
			}
			return error;
		}
		registered_names.push_back(tool.name);
	}
	return OK;
}

Dictionary make_property_schema(const String &p_type, const String &p_description) {
	Dictionary property;
	property["type"] = p_type;
	property["description"] = p_description;
	return property;
}

Dictionary make_enum_schema(const PackedStringArray &p_values, const String &p_description, const String &p_default) {
	Dictionary property = make_property_schema("string", p_description);
	property["enum"] = p_values;
	property["default"] = p_default;
	return property;
}

Dictionary make_object_schema(const Dictionary &p_properties, const PackedStringArray &p_required, bool p_allow_additional_properties) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	if (!p_required.is_empty()) {
		schema["required"] = p_required;
	}
	schema["additionalProperties"] = p_allow_additional_properties;
	return schema;
}

Dictionary make_property_description(const PropertyInfo &p_property) {
	Dictionary description;
	description["name"] = p_property.name;
	description["type"] = Variant::get_type_name(p_property.type);
	description["typeId"] = int(p_property.type);
	description["className"] = String(p_property.class_name);
	description["hint"] = int(p_property.hint);
	description["hintString"] = p_property.hint_string;
	description["usage"] = int64_t(p_property.usage);
	return description;
}

Dictionary make_success_result(const Dictionary &p_structured_content) {
	const Dictionary structured_content = p_structured_content.duplicate(true);
	const String serialized = JSON::stringify(structured_content);
	String text = serialized;
	const uint64_t serialized_bytes = serialized.to_utf8_buffer().size();
	if (serialized_bytes > MAX_INLINE_STRUCTURED_TEXT_BYTES) {
		Dictionary summary;
		summary["_truncated"] = true;
		summary["structuredContentBytes"] = int64_t(serialized_bytes);
		int fields = 0;
		for (const KeyValue<Variant, Variant> &entry : structured_content) {
			if (fields >= MAX_SUMMARY_FIELDS) {
				break;
			}
			if (!entry.key.is_string()) {
				continue;
			}
			const Variant::Type type = entry.value.get_type();
			if (type == Variant::NIL || type == Variant::BOOL || type == Variant::INT || type == Variant::FLOAT) {
				summary[entry.key] = entry.value;
				fields++;
			} else if (type == Variant::STRING || type == Variant::STRING_NAME) {
				const String value = entry.value;
				if (value.length() <= MAX_SUMMARY_STRING_CHARACTERS) {
					summary[entry.key] = value;
					fields++;
				}
			}
		}
		text = JSON::stringify(summary);
	}

	Dictionary result;
	result["content"] = _make_text_content(text);
	result["structuredContent"] = structured_content;
	return result;
}

Dictionary make_error_result(const String &p_code, const String &p_message, const Dictionary &p_details) {
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	if (!p_details.is_empty()) {
		error["details"] = p_details.duplicate(true);
	}

	Dictionary structured_content;
	structured_content["error"] = error;

	Dictionary result;
	result["content"] = _make_text_content(p_message);
	result["structuredContent"] = structured_content;
	result["isError"] = true;
	return result;
}

MCPToolCallContext make_tool_call_context(const Dictionary &p_context) {
	MCPToolCallContext context;
	context.request_id = p_context.get("requestId", Variant());
	context.protocol_version = p_context.get("protocolVersion", String());
	const Variant session = p_context.get("session", Dictionary());
	const Variant project = p_context.get("project", Dictionary());
	if (session.get_type() == Variant::DICTIONARY) {
		context.session = session;
	}
	if (project.get_type() == Variant::DICTIONARY) {
		context.project = project;
	}
	return context;
}

bool has_only_arguments(const Dictionary &p_arguments, const PackedStringArray &p_allowed_names, String &r_unknown_name) {
	r_unknown_name = String();
	for (const KeyValue<Variant, Variant> &entry : p_arguments) {
		if (!entry.key.is_string()) {
			r_unknown_name = String(entry.key);
			return false;
		}
		const String name = entry.key;
		if (!p_allowed_names.has(name)) {
			r_unknown_name = name;
			return false;
		}
	}
	return true;
}

bool try_get_json_integer(const Variant &p_value, int64_t p_minimum, int64_t p_maximum, int64_t &r_value) {
	if (p_minimum > p_maximum) {
		return false;
	}

	int64_t value = 0;
	if (p_value.get_type() == Variant::INT) {
		value = p_value;
	} else if (p_value.get_type() == Variant::FLOAT) {
		constexpr double MAX_SAFE_JSON_INTEGER = 9007199254740991.0;
		const double json_number = p_value;
		if (!Math::is_finite(json_number) || json_number < -MAX_SAFE_JSON_INTEGER || json_number > MAX_SAFE_JSON_INTEGER ||
				Math::floor(json_number) != json_number) {
			return false;
		}
		value = int64_t(json_number);
	} else {
		return false;
	}

	if (value < p_minimum || value > p_maximum) {
		return false;
	}
	r_value = value;
	return true;
}

} // namespace MCPToolUtils
