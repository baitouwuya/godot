/**************************************************************************/
/*  mcp_variant_codec.cpp                                                 */
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

#include "mcp_variant_codec.h"

#include "mcp_path_utils.h"

#include "core/io/json.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/math/math_funcs.h"

namespace {

static constexpr const char *ENCODED_VARIANT_KEY = "__godot_mcp_encoded_variant__";
static constexpr const char *RESOURCE_PATH_KEY = "__godot_mcp_resource_path__";

static Error _fail(const String &p_message, String *r_error, Error p_error = ERR_INVALID_DATA) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

static bool _is_unsafe_type(Variant::Type p_type) {
	return p_type == Variant::OBJECT || p_type == Variant::CALLABLE || p_type == Variant::SIGNAL || p_type == Variant::RID;
}

static bool _has_legacy_string_prefix(const String &p_value) {
	return p_value.begins_with("i:") || p_value.begins_with("f:") || p_value.begins_with("s:") ||
			p_value.begins_with("sn:") || p_value.begins_with("np:");
}

static bool _looks_like_legacy_native_dictionary(const Dictionary &p_dictionary) {
	if (!p_dictionary.has("type")) {
		return false;
	}
	const Variant type_value = p_dictionary["type"];
	if (type_value.get_type() != Variant::STRING && type_value.get_type() != Variant::STRING_NAME) {
		return false;
	}
	return Variant::get_type_by_name(type_value) != Variant::VARIANT_MAX;
}

static Dictionary _wrap_native_encoding(const Variant &p_value) {
	Dictionary wrapped;
	wrapped[ENCODED_VARIANT_KEY] = JSON::from_native(p_value, false);
	return wrapped;
}

static Error _encode_json_value(const Variant &p_value, Variant &r_value, String *r_error, int p_depth) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		return _fail("Variant nesting exceeds the supported depth.", r_error);
	}

	switch (p_value.get_type()) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT: {
			r_value = p_value;
			return OK;
		}
		case Variant::FLOAT: {
			const double value = p_value;
			r_value = Math::is_finite(value) ? p_value : Variant(_wrap_native_encoding(p_value));
			return OK;
		}
		case Variant::STRING: {
			const String value = p_value;
			r_value = _has_legacy_string_prefix(value) ? Variant(_wrap_native_encoding(p_value)) : p_value;
			return OK;
		}
		case Variant::ARRAY: {
			const Array source = p_value;
			if (source.is_typed()) {
				r_value = _wrap_native_encoding(p_value);
				return OK;
			}
			Array encoded;
			encoded.resize(source.size());
			for (int i = 0; i < source.size(); i++) {
				Variant value;
				const Error error = _encode_json_value(source[i], value, r_error, p_depth + 1);
				if (error != OK) {
					return error;
				}
				encoded[i] = value;
			}
			r_value = encoded;
			return OK;
		}
		case Variant::DICTIONARY: {
			const Dictionary source = p_value;
			if (source.is_typed() || source.has(ENCODED_VARIANT_KEY) || source.has(RESOURCE_PATH_KEY) ||
					_looks_like_legacy_native_dictionary(source)) {
				r_value = _wrap_native_encoding(p_value);
				return OK;
			}
			Dictionary encoded;
			for (const KeyValue<Variant, Variant> &entry : source) {
				if (!entry.key.is_string()) {
					r_value = _wrap_native_encoding(p_value);
					return OK;
				}
				Variant value;
				const Error error = _encode_json_value(entry.value, value, r_error, p_depth + 1);
				if (error != OK) {
					return error;
				}
				encoded[entry.key] = value;
			}
			r_value = encoded;
			return OK;
		}
		default: {
			r_value = _wrap_native_encoding(p_value);
			return OK;
		}
	}
}

static Error _sanitize_native(const Variant &p_value, Variant &r_value, String *r_error, int p_depth) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		return _fail("Variant nesting exceeds the supported depth.", r_error);
	}

	switch (p_value.get_type()) {
		case Variant::OBJECT: {
			bool previously_freed = false;
			Object *object = p_value.get_validated_object_with_check(previously_freed);
			if (previously_freed) {
				return _fail("Object reference is no longer valid.", r_error);
			}
			if (!object) {
				r_value = Variant();
				return OK;
			}
			Resource *resource = Object::cast_to<Resource>(object);
			if (!resource) {
				return _fail("Only project Resource references can be encoded as objects.", r_error);
			}

			String resource_path;
			String path_error;
			const Error err = MCPPathUtils::normalize_resource_file_path(resource->get_path(), resource_path, &path_error);
			if (err != OK) {
				return _fail("Resource path is not safe: " + path_error, r_error);
			}

			Dictionary resource_reference;
			resource_reference[RESOURCE_PATH_KEY] = resource_path;
			r_value = resource_reference;
			return OK;
		}
		case Variant::CALLABLE:
		case Variant::SIGNAL:
		case Variant::RID: {
			return _fail("Variant type '" + Variant::get_type_name(p_value.get_type()) + "' cannot be encoded.", r_error);
		}
		case Variant::DICTIONARY: {
			const Dictionary source = p_value;
			Dictionary sanitized;
			for (const KeyValue<Variant, Variant> &entry : source) {
				Variant key;
				Variant value;
				Error err = _sanitize_native(entry.key, key, r_error, p_depth + 1);
				if (err != OK) {
					return err;
				}
				err = _sanitize_native(entry.value, value, r_error, p_depth + 1);
				if (err != OK) {
					return err;
				}
				sanitized[key] = value;
			}
			r_value = sanitized;
			return OK;
		}
		case Variant::ARRAY: {
			const Array source = p_value;
			Array sanitized;
			sanitized.resize(source.size());
			for (int i = 0; i < source.size(); i++) {
				Variant value;
				const Error err = _sanitize_native(source[i], value, r_error, p_depth + 1);
				if (err != OK) {
					return err;
				}
				sanitized[i] = value;
			}
			r_value = sanitized;
			return OK;
		}
		default: {
			r_value = p_value;
			return OK;
		}
	}
}

static Error _validate_type_tag(const Dictionary &p_dictionary, const StringName &p_key, String *r_error) {
	if (!p_dictionary.has(p_key)) {
		return OK;
	}
	const Variant type_value = p_dictionary[p_key];
	if (type_value.get_type() != Variant::STRING && type_value.get_type() != Variant::STRING_NAME) {
		return _fail("Encoded Variant type tags must be strings.", r_error);
	}

	const String type_name = type_value;
	const Variant::Type type = Variant::get_type_by_name(type_name);
	if (type == Variant::VARIANT_MAX || _is_unsafe_type(type)) {
		return _fail("Encoded Variant type '" + type_name + "' is not allowed.", r_error);
	}
	return OK;
}

static Error _validate_encoded_tags(const Variant &p_value, String *r_error, int p_depth) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		return _fail("Encoded Variant nesting exceeds the supported depth.", r_error);
	}

	if (_is_unsafe_type(p_value.get_type())) {
		return _fail("Encoded data contains an unsafe native Variant.", r_error);
	}
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_value;
		static const StringName type_keys[] = { "type", "elem_type", "key_type", "value_type" };
		for (const StringName &key : type_keys) {
			const Error err = _validate_type_tag(dictionary, key, r_error);
			if (err != OK) {
				return err;
			}
		}
		for (const KeyValue<Variant, Variant> &entry : dictionary) {
			Error err = _validate_encoded_tags(entry.key, r_error, p_depth + 1);
			if (err != OK) {
				return err;
			}
			err = _validate_encoded_tags(entry.value, r_error, p_depth + 1);
			if (err != OK) {
				return err;
			}
		}
	} else if (p_value.get_type() == Variant::ARRAY) {
		const Array array = p_value;
		for (int i = 0; i < array.size(); i++) {
			const Error err = _validate_encoded_tags(array[i], r_error, p_depth + 1);
			if (err != OK) {
				return err;
			}
		}
	}
	return OK;
}

static Error _restore_resource_references(const Variant &p_value, Variant &r_value, String *r_error, int p_depth) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		return _fail("Decoded Variant nesting exceeds the supported depth.", r_error);
	}

	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary source = p_value;
		if (source.size() == 1 && source.has(RESOURCE_PATH_KEY)) {
			const Variant path_value = source[RESOURCE_PATH_KEY];
			if (path_value.get_type() != Variant::STRING) {
				return _fail("Resource reference path must be a string.", r_error);
			}

			String resource_path;
			String path_error;
			Error err = MCPPathUtils::normalize_resource_file_path(path_value, resource_path, &path_error);
			if (err != OK) {
				return _fail("Resource reference path is not safe: " + path_error, r_error);
			}
			if (!ResourceLoader::exists(resource_path)) {
				return _fail("Resource does not exist: " + resource_path, r_error, ERR_FILE_NOT_FOUND);
			}

			const Ref<Resource> resource = ResourceLoader::load(resource_path);
			if (resource.is_null()) {
				return _fail("Resource could not be loaded: " + resource_path, r_error, ERR_CANT_OPEN);
			}
			r_value = resource;
			return OK;
		}

		Dictionary restored;
		for (const KeyValue<Variant, Variant> &entry : source) {
			Variant key;
			Variant value;
			Error err = _restore_resource_references(entry.key, key, r_error, p_depth + 1);
			if (err != OK) {
				return err;
			}
			err = _restore_resource_references(entry.value, value, r_error, p_depth + 1);
			if (err != OK) {
				return err;
			}
			restored[key] = value;
		}
		r_value = restored;
		return OK;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array source = p_value;
		Array restored;
		restored.resize(source.size());
		for (int i = 0; i < source.size(); i++) {
			Variant value;
			const Error err = _restore_resource_references(source[i], value, r_error, p_depth + 1);
			if (err != OK) {
				return err;
			}
			restored[i] = value;
		}
		r_value = restored;
		return OK;
	}

	r_value = p_value;
	return OK;
}

static Error _decode_native_encoding(const Variant &p_encoded_value, Variant &r_value, String *r_error) {
	Error error = _validate_encoded_tags(p_encoded_value, r_error, 0);
	if (error != OK) {
		return error;
	}

	const Variant native_value = JSON::to_native(p_encoded_value, false);
	if (JSON::from_native(native_value, false) != p_encoded_value) {
		return _fail("Encoded Variant is malformed or not in canonical form.", r_error);
	}
	return _restore_resource_references(native_value, r_value, r_error, 0);
}

static Error _decode_json_value(const Variant &p_encoded_value, Variant &r_value, String *r_error, int p_depth) {
	if (p_depth > Variant::MAX_RECURSION_DEPTH) {
		return _fail("Encoded Variant nesting exceeds the supported depth.", r_error);
	}

	if (p_encoded_value.get_type() == Variant::STRING && _has_legacy_string_prefix(p_encoded_value)) {
		return _decode_native_encoding(p_encoded_value, r_value, r_error);
	}
	if (p_encoded_value.get_type() == Variant::DICTIONARY) {
		const Dictionary source = p_encoded_value;
		if (source.size() == 1 && source.has(ENCODED_VARIANT_KEY)) {
			return _decode_native_encoding(source[ENCODED_VARIANT_KEY], r_value, r_error);
		}
		if (_looks_like_legacy_native_dictionary(source)) {
			return _decode_native_encoding(source, r_value, r_error);
		}
		Dictionary decoded;
		for (const KeyValue<Variant, Variant> &entry : source) {
			Variant value;
			const Error error = _decode_json_value(entry.value, value, r_error, p_depth + 1);
			if (error != OK) {
				return error;
			}
			decoded[entry.key] = value;
		}
		r_value = decoded;
		return OK;
	}
	if (p_encoded_value.get_type() == Variant::ARRAY) {
		const Array source = p_encoded_value;
		Array decoded;
		decoded.resize(source.size());
		for (int i = 0; i < source.size(); i++) {
			Variant value;
			const Error error = _decode_json_value(source[i], value, r_error, p_depth + 1);
			if (error != OK) {
				return error;
			}
			decoded[i] = value;
		}
		r_value = decoded;
		return OK;
	}
	if (p_encoded_value.get_type() == Variant::NIL || p_encoded_value.get_type() == Variant::BOOL ||
			p_encoded_value.get_type() == Variant::INT || p_encoded_value.get_type() == Variant::FLOAT ||
			p_encoded_value.get_type() == Variant::STRING) {
		r_value = p_encoded_value;
		return OK;
	}
	return _fail("Encoded Variant contains a non-JSON native value.", r_error);
}

} // namespace

Error MCPVariantCodec::encode(const Variant &p_value, Variant &r_encoded_value, String *r_error) {
	r_encoded_value = Variant();
	if (r_error) {
		*r_error = String();
	}

	Variant sanitized;
	const Error err = _sanitize_native(p_value, sanitized, r_error, 0);
	if (err != OK) {
		return err;
	}
	return _encode_json_value(sanitized, r_encoded_value, r_error, 0);
}

Error MCPVariantCodec::decode(const Variant &p_encoded_value, Variant &r_value, String *r_error) {
	r_value = Variant();
	if (r_error) {
		*r_error = String();
	}

	return _decode_json_value(p_encoded_value, r_value, r_error, 0);
}
