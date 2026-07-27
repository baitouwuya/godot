/**************************************************************************/
/*  mcp_json_schema_validator.cpp                                         */
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

#include "mcp_json_schema_validator.h"

#include "core/math/math_funcs.h"

namespace {

constexpr int MAX_SCHEMA_DEPTH = 64;

static String _append_pointer(const String &p_path, const String &p_token) {
	return p_path + "/" + p_token.replace("~", "~0").replace("/", "~1");
}

static String _display_path(const String &p_path) {
	return p_path.is_empty() ? "tool arguments" : "argument '" + p_path + "'";
}

static bool _to_schema_array(const Variant &p_value, Array &r_array) {
	if (p_value.get_type() == Variant::ARRAY) {
		r_array = p_value;
		return true;
	}
	if (p_value.get_type() == Variant::PACKED_STRING_ARRAY) {
		const PackedStringArray values = p_value;
		for (const String &value : values) {
			r_array.push_back(value);
		}
		return true;
	}
	return false;
}

static bool _is_number(const Variant &p_value) {
	return p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT;
}

static bool _is_finite_number(const Variant &p_value) {
	return _is_number(p_value) && Math::is_finite(double(p_value));
}

static bool _is_known_type(const String &p_type) {
	return p_type == "null" || p_type == "boolean" || p_type == "number" || p_type == "integer" ||
			p_type == "string" || p_type == "array" || p_type == "object";
}

static bool _read_non_negative_integer(const Variant &p_value, int64_t &r_value) {
	if (p_value.get_type() == Variant::INT) {
		r_value = p_value;
		return r_value >= 0;
	}
	if (p_value.get_type() != Variant::FLOAT) {
		return false;
	}
	const double value = p_value;
	if (!Math::is_finite(value) || value < 0.0 || Math::floor(value) != value || value > double(INT64_MAX)) {
		return false;
	}
	r_value = int64_t(value);
	return true;
}

static bool _matches_type(const Variant &p_value, const String &p_type) {
	const Variant::Type type = p_value.get_type();
	if (p_type == "null") {
		return type == Variant::NIL;
	}
	if (p_type == "boolean") {
		return type == Variant::BOOL;
	}
	if (p_type == "number") {
		return _is_finite_number(p_value);
	}
	if (p_type == "integer") {
		if (type == Variant::INT) {
			return true;
		}
		if (type != Variant::FLOAT) {
			return false;
		}
		const double value = p_value;
		return Math::is_finite(value) && Math::floor(value) == value;
	}
	if (p_type == "string") {
		return type == Variant::STRING || type == Variant::STRING_NAME;
	}
	if (p_type == "array") {
		return type == Variant::ARRAY;
	}
	if (p_type == "object") {
		return type == Variant::DICTIONARY;
	}
	return false;
}

static String _expected_type_name(const String &p_type) {
	if (p_type == "object") {
		return "an object";
	}
	if (p_type == "array") {
		return "an array";
	}
	if (p_type == "integer") {
		return "an integer";
	}
	return "a " + p_type;
}

static String _expected_types_description(const Array &p_types) {
	if (p_types.size() == 1) {
		return _expected_type_name(p_types[0]);
	}
	PackedStringArray types;
	for (const Variant &type : p_types) {
		types.push_back(type);
	}
	return "one of these types: " + String(", ").join(types);
}

static bool _values_equal(const Variant &p_left, const Variant &p_right) {
	if (_is_number(p_left) && _is_number(p_right)) {
		return double(p_left) == double(p_right);
	}
	return p_left.hash_compare(p_right);
}

static bool _schema_error(const String &p_path, const String &p_message, String *r_error) {
	if (r_error) {
		*r_error = (p_path.is_empty() ? "$" : p_path) + ": " + p_message;
	}
	return false;
}

static bool _validate_schema(const Dictionary &p_schema, const String &p_path, int p_depth, String *r_error) {
	if (p_depth > MAX_SCHEMA_DEPTH) {
		return _schema_error(p_path, "Schema nesting exceeds the supported depth.", r_error);
	}

	if (p_schema.has("type")) {
		const Variant type_value = p_schema["type"];
		Array types;
		if (type_value.is_string()) {
			types.push_back(type_value);
		} else if (!_to_schema_array(type_value, types) || types.is_empty()) {
			return _schema_error(_append_pointer(p_path, "type"), "Expected a type string or a non-empty array of type strings.", r_error);
		}
		for (const Variant &type_value_item : types) {
			if (!type_value_item.is_string()) {
				return _schema_error(_append_pointer(p_path, "type"), "Expected only type strings.", r_error);
			}
			const String type = type_value_item;
			if (!_is_known_type(type)) {
				return _schema_error(_append_pointer(p_path, "type"), "Unknown JSON type '" + type + "'.", r_error);
			}
		}
	}

	if (p_schema.has("properties")) {
		if (p_schema["properties"].get_type() != Variant::DICTIONARY) {
			return _schema_error(_append_pointer(p_path, "properties"), "Expected an object.", r_error);
		}
		const Dictionary properties = p_schema["properties"];
		for (const KeyValue<Variant, Variant> &entry : properties) {
			if (!entry.key.is_string() || entry.value.get_type() != Variant::DICTIONARY) {
				return _schema_error(_append_pointer(p_path, "properties"), "Property names must map to schema objects.", r_error);
			}
			if (!_validate_schema(entry.value, _append_pointer(_append_pointer(p_path, "properties"), entry.key), p_depth + 1, r_error)) {
				return false;
			}
		}
	}

	if (p_schema.has("required")) {
		Array required;
		if (!_to_schema_array(p_schema["required"], required)) {
			return _schema_error(_append_pointer(p_path, "required"), "Expected an array of strings.", r_error);
		}
		for (const Variant &name : required) {
			if (!name.is_string()) {
				return _schema_error(_append_pointer(p_path, "required"), "Expected an array of strings.", r_error);
			}
		}
	}

	if (p_schema.has("additionalProperties")) {
		const Variant additional = p_schema["additionalProperties"];
		if (additional.get_type() != Variant::BOOL && additional.get_type() != Variant::DICTIONARY) {
			return _schema_error(_append_pointer(p_path, "additionalProperties"), "Expected a boolean or schema object.", r_error);
		}
		if (additional.get_type() == Variant::DICTIONARY && !_validate_schema(additional, _append_pointer(p_path, "additionalProperties"), p_depth + 1, r_error)) {
			return false;
		}
	}

	if (p_schema.has("items")) {
		if (p_schema["items"].get_type() != Variant::DICTIONARY) {
			return _schema_error(_append_pointer(p_path, "items"), "Expected a schema object.", r_error);
		}
		if (!_validate_schema(p_schema["items"], _append_pointer(p_path, "items"), p_depth + 1, r_error)) {
			return false;
		}
	}

	if (p_schema.has("enum")) {
		Array values;
		if (!_to_schema_array(p_schema["enum"], values) || values.is_empty()) {
			return _schema_error(_append_pointer(p_path, "enum"), "Expected a non-empty array.", r_error);
		}
	}

	const char *numeric_keywords[] = { "minimum", "maximum" };
	for (const char *keyword : numeric_keywords) {
		if (p_schema.has(keyword) && !_is_finite_number(p_schema[keyword])) {
			return _schema_error(_append_pointer(p_path, keyword), "Expected a finite number.", r_error);
		}
	}
	if (p_schema.has("minimum") && p_schema.has("maximum") && double(p_schema["minimum"]) > double(p_schema["maximum"])) {
		return _schema_error(p_path, "minimum cannot be greater than maximum.", r_error);
	}

	const char *count_pairs[][2] = {
		{ "minLength", "maxLength" },
		{ "minItems", "maxItems" },
	};
	for (const auto &pair : count_pairs) {
		int64_t minimum = 0;
		int64_t maximum = INT64_MAX;
		if (p_schema.has(pair[0]) && !_read_non_negative_integer(p_schema[pair[0]], minimum)) {
			return _schema_error(_append_pointer(p_path, pair[0]), "Expected a non-negative integer.", r_error);
		}
		if (p_schema.has(pair[1]) && !_read_non_negative_integer(p_schema[pair[1]], maximum)) {
			return _schema_error(_append_pointer(p_path, pair[1]), "Expected a non-negative integer.", r_error);
		}
		if (minimum > maximum) {
			return _schema_error(p_path, String(pair[0]) + " cannot be greater than " + pair[1] + ".", r_error);
		}
	}

	if (p_schema.has("pattern") && !p_schema["pattern"].is_string()) {
		return _schema_error(_append_pointer(p_path, "pattern"), "Expected a string.", r_error);
	}

	const char *composition_keywords[] = { "allOf", "anyOf", "oneOf" };
	for (const char *keyword : composition_keywords) {
		if (!p_schema.has(keyword)) {
			continue;
		}
		Array schemas;
		if (!_to_schema_array(p_schema[keyword], schemas) || schemas.is_empty()) {
			return _schema_error(_append_pointer(p_path, keyword), "Expected a non-empty array of schema objects.", r_error);
		}
		for (int i = 0; i < schemas.size(); i++) {
			if (schemas[i].get_type() != Variant::DICTIONARY) {
				return _schema_error(_append_pointer(_append_pointer(p_path, keyword), itos(i)), "Expected a schema object.", r_error);
			}
			if (!_validate_schema(schemas[i], _append_pointer(_append_pointer(p_path, keyword), itos(i)), p_depth + 1, r_error)) {
				return false;
			}
		}
	}
	return true;
}

static bool _set_error(MCPJSONSchemaValidator::ValidationError &r_error, const String &p_keyword,
		const String &p_path, const String &p_message, const Dictionary &p_details = Dictionary()) {
	r_error.keyword = p_keyword;
	r_error.instance_path = p_path;
	r_error.message = p_message;
	r_error.details = p_details;
	return false;
}

static bool _validate_value(const Variant &p_value, const Dictionary &p_schema, const String &p_path,
		int p_depth, MCPJSONSchemaValidator::ValidationError &r_error) {
	if (p_depth > MAX_SCHEMA_DEPTH) {
		return _set_error(r_error, "depth", p_path, "Tool arguments exceed the supported nesting depth.");
	}

	if (p_schema.has("type")) {
		const Variant type_value = p_schema["type"];
		Array expected_types;
		if (type_value.is_string()) {
			expected_types.push_back(type_value);
		} else {
			_to_schema_array(type_value, expected_types);
		}
		bool type_matches = false;
		for (const Variant &expected_type : expected_types) {
			if (_matches_type(p_value, expected_type)) {
				type_matches = true;
				break;
			}
		}
		if (!type_matches) {
			Dictionary details;
			if (expected_types.size() == 1) {
				details["expectedType"] = expected_types[0];
			} else {
				details["expectedTypes"] = expected_types;
			}
			details["actualType"] = Variant::get_type_name(p_value.get_type());
			return _set_error(r_error, "type", p_path,
					_display_path(p_path).capitalize() + " must be " + _expected_types_description(expected_types) + "; received " + String(details["actualType"]) + ".", details);
		}
	}

	if (p_schema.has("enum")) {
		Array allowed;
		_to_schema_array(p_schema["enum"], allowed);
		bool matched = false;
		for (const Variant &candidate : allowed) {
			if (_values_equal(p_value, candidate)) {
				matched = true;
				break;
			}
		}
		if (!matched) {
			Dictionary details;
			details["allowedValues"] = allowed;
			return _set_error(r_error, "enum", p_path, _display_path(p_path).capitalize() + " has an unsupported value.", details);
		}
	}

	if (_is_number(p_value)) {
		const double value = p_value;
		if (p_schema.has("minimum") && value < double(p_schema["minimum"])) {
			Dictionary details;
			details["minimum"] = p_schema["minimum"];
			return _set_error(r_error, "minimum", p_path, _display_path(p_path).capitalize() + " is below the minimum.", details);
		}
		if (p_schema.has("maximum") && value > double(p_schema["maximum"])) {
			Dictionary details;
			details["maximum"] = p_schema["maximum"];
			return _set_error(r_error, "maximum", p_path, _display_path(p_path).capitalize() + " exceeds the maximum.", details);
		}
	}

	if (p_value.is_string()) {
		const int64_t length = String(p_value).length();
		if (p_schema.has("minLength") && length < int64_t(p_schema["minLength"])) {
			Dictionary details;
			details["minLength"] = p_schema["minLength"];
			return _set_error(r_error, "minLength", p_path, _display_path(p_path).capitalize() + " is too short.", details);
		}
		if (p_schema.has("maxLength") && length > int64_t(p_schema["maxLength"])) {
			Dictionary details;
			details["maxLength"] = p_schema["maxLength"];
			return _set_error(r_error, "maxLength", p_path, _display_path(p_path).capitalize() + " is too long.", details);
		}
	}

	if (p_value.get_type() == Variant::ARRAY) {
		const Array values = p_value;
		if (p_schema.has("minItems") && values.size() < int64_t(p_schema["minItems"])) {
			Dictionary details;
			details["minItems"] = p_schema["minItems"];
			return _set_error(r_error, "minItems", p_path, _display_path(p_path).capitalize() + " has too few items.", details);
		}
		if (p_schema.has("maxItems") && values.size() > int64_t(p_schema["maxItems"])) {
			Dictionary details;
			details["maxItems"] = p_schema["maxItems"];
			return _set_error(r_error, "maxItems", p_path, _display_path(p_path).capitalize() + " has too many items.", details);
		}
		if (p_schema.has("items")) {
			const Dictionary item_schema = p_schema["items"];
			for (int i = 0; i < values.size(); i++) {
				if (!_validate_value(values[i], item_schema, _append_pointer(p_path, itos(i)), p_depth + 1, r_error)) {
					return false;
				}
			}
		}
	}

	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary value = p_value;
		const Dictionary properties = p_schema.get("properties", Dictionary());
		if (p_schema.has("required")) {
			Array required;
			_to_schema_array(p_schema["required"], required);
			PackedStringArray missing;
			for (const Variant &name : required) {
				if (!value.has(name)) {
					missing.push_back(name);
				}
			}
			if (!missing.is_empty()) {
				missing.sort();
				Dictionary details;
				details["missingArguments"] = missing;
				const String noun = missing.size() == 1 ? "argument" : "arguments";
				return _set_error(r_error, "required", p_path, "Missing required " + noun + ": " + String(", ").join(missing) + ".", details);
			}
		}

		PackedStringArray unknown;
		for (const KeyValue<Variant, Variant> &entry : value) {
			if (!entry.key.is_string() || !properties.has(entry.key)) {
				unknown.push_back(entry.key);
			}
		}
		const Variant additional = p_schema.get("additionalProperties", true);
		if (!unknown.is_empty() && additional.get_type() == Variant::BOOL && !bool(additional)) {
			unknown.sort();
			PackedStringArray valid;
			for (const Variant &name : properties.keys()) {
				if (name.is_string()) {
					valid.push_back(name);
				}
			}
			valid.sort();
			String message = "Unknown argument: " + unknown[0] + ".";
			if (p_path.is_empty()) {
				message += valid.is_empty() ? " This tool accepts no arguments." : " Valid arguments: " + String(", ").join(valid) + ".";
			} else {
				message = "Unknown property at '" + p_path + "': " + unknown[0] + ".";
				if (!valid.is_empty()) {
					message += " Valid properties: " + String(", ").join(valid) + ".";
				}
			}
			Dictionary details;
			details["unknownArgument"] = unknown[0];
			details["unknownArguments"] = unknown;
			details["validArguments"] = valid;
			return _set_error(r_error, "additionalProperties", p_path, message, details);
		}

		for (const KeyValue<Variant, Variant> &entry : value) {
			if (entry.key.is_string() && properties.has(entry.key)) {
				if (!_validate_value(entry.value, properties[entry.key], _append_pointer(p_path, entry.key), p_depth + 1, r_error)) {
					return false;
				}
			} else if (additional.get_type() == Variant::DICTIONARY) {
				if (!_validate_value(entry.value, additional, _append_pointer(p_path, entry.key), p_depth + 1, r_error)) {
					return false;
				}
			}
		}
	}

	if (p_schema.has("allOf")) {
		const Array schemas = p_schema["allOf"];
		for (const Variant &schema : schemas) {
			if (!_validate_value(p_value, schema, p_path, p_depth + 1, r_error)) {
				return false;
			}
		}
	}

	const char *choice_keywords[] = { "anyOf", "oneOf" };
	for (const char *keyword : choice_keywords) {
		if (!p_schema.has(keyword)) {
			continue;
		}
		const Array schemas = p_schema[keyword];
		int matches = 0;
		for (const Variant &schema : schemas) {
			MCPJSONSchemaValidator::ValidationError ignored;
			if (_validate_value(p_value, schema, p_path, p_depth + 1, ignored)) {
				matches++;
			}
		}
		const bool valid = String(keyword) == "anyOf" ? matches > 0 : matches == 1;
		if (!valid) {
			Dictionary details;
			details["matchedSchemas"] = matches;
			details["schemaCount"] = schemas.size();
			const String requirement = String(keyword) == "anyOf" ? "at least one allowed schema" : "exactly one allowed schema";
			return _set_error(r_error, keyword, p_path, _display_path(p_path).capitalize() + " must match " + requirement + ".", details);
		}
	}
	return true;
}

} // namespace

bool MCPJSONSchemaValidator::validate_schema(const Dictionary &p_schema, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	return _validate_schema(p_schema, String(), 0, r_error);
}

bool MCPJSONSchemaValidator::validate(const Variant &p_value, const Dictionary &p_schema, ValidationError &r_error) {
	r_error = ValidationError();
	return _validate_value(p_value, p_schema, String(), 0, r_error);
}
