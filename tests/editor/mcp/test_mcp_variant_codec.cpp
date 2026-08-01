/**************************************************************************/
/*  test_mcp_variant_codec.cpp                                            */
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

#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "editor/mcp/providers/mcp_variant_codec.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_variant_codec);

namespace TestMCPVariantCodec {

TEST_CASE("[MCP][Provider] Safe native Variants round-trip through JSON native encoding") {
	Array values;
	values.push_back(NodePath("Root/Child"));
	values.push_back(PackedInt32Array({ 2, 4, 8 }));

	Dictionary source;
	source["position"] = Vector3(1.25, -2.5, 3.75);
	source[StringName("values")] = values;

	Variant encoded;
	String error;
	REQUIRE(MCPVariantCodec::encode(source, encoded, &error) == OK);
	CHECK(error.is_empty());

	Variant decoded;
	REQUIRE(MCPVariantCodec::decode(encoded, decoded, &error) == OK);
	CHECK(decoded == source);
}

TEST_CASE("[MCP][Provider] JSON-native Variant values stay readable without type prefixes") {
	Dictionary source;
	source["path"] = "res://main.tscn";
	source["count"] = int64_t(7);
	source["values"] = Array{ true, 2.5, "ready" };

	Variant encoded;
	String error;
	REQUIRE(MCPVariantCodec::encode(source, encoded, &error) == OK);
	REQUIRE(encoded.get_type() == Variant::DICTIONARY);
	const Dictionary encoded_dictionary = encoded;
	CHECK(encoded_dictionary.get("path", String()) == "res://main.tscn");
	CHECK(int64_t(encoded_dictionary.get("count", int64_t(0))) == int64_t(7));
	CHECK(Array(encoded_dictionary.get("values", Array())) == Array{ true, 2.5, "ready" });

	Variant decoded;
	REQUIRE(MCPVariantCodec::decode(encoded, decoded, &error) == OK);
	CHECK(decoded == source);
}

TEST_CASE("[MCP][Provider] Reserved legacy prefixes round-trip as literal strings") {
	for (const String &source : PackedStringArray{ "s:value", "i:42", "f:1.0", "sn:name", "np:path" }) {
		Variant encoded;
		String error;
		REQUIRE(MCPVariantCodec::encode(source, encoded, &error) == OK);
		CHECK(encoded.get_type() == Variant::DICTIONARY);
		Variant decoded;
		REQUIRE(MCPVariantCodec::decode(encoded, decoded, &error) == OK);
		CHECK(decoded == Variant(source));
	}

	Variant legacy_decoded;
	String error;
	REQUIRE(MCPVariantCodec::decode("s:legacy", legacy_decoded, &error) == OK);
	CHECK(legacy_decoded == Variant("legacy"));

	Variant legacy_float;
	REQUIRE(MCPVariantCodec::decode("f:1.05", legacy_float, &error) == OK);
	CHECK(legacy_float == Variant(1.05));
}

TEST_CASE("[MCP][Provider] Native Variant envelopes accept JSON numeric constructor arguments") {
	Dictionary native_vector;
	native_vector["type"] = "Vector2";
	native_vector["args"] = Array{ 1.05, -2.25 };
	Dictionary envelope;
	envelope["__godot_mcp_encoded_variant__"] = native_vector;

	Variant decoded;
	String error;
	INFO(error);
	REQUIRE(MCPVariantCodec::decode(envelope, decoded, &error) == OK);
	CHECK(error.is_empty());
	REQUIRE(decoded.get_type() == Variant::VECTOR2);
	CHECK(Vector2(decoded).is_equal_approx(Vector2(1.05, -2.25)));

	native_vector["unexpected"] = true;
	envelope["__godot_mcp_encoded_variant__"] = native_vector;
	CHECK(MCPVariantCodec::decode(envelope, decoded, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("canonical"));
}

TEST_CASE("[MCP][Provider] Unsafe object-like Variant types are rejected") {
	Variant encoded;
	String error;

	Object *object = memnew(Object);
	Variant object_value = object;
	CHECK(MCPVariantCodec::encode(object_value, encoded, &error) == ERR_INVALID_DATA);
	object_value = Variant();
	memdelete(object);

	CHECK(MCPVariantCodec::encode(Callable(), encoded, &error) == ERR_INVALID_DATA);
	CHECK(MCPVariantCodec::encode(Signal(), encoded, &error) == ERR_INVALID_DATA);
	CHECK(MCPVariantCodec::encode(RID(), encoded, &error) == ERR_INVALID_DATA);

	Ref<Resource> unpathed_resource;
	unpathed_resource.instantiate();
	CHECK(MCPVariantCodec::encode(unpathed_resource, encoded, &error) == ERR_INVALID_DATA);
}

TEST_CASE("[MCP][Provider] Null object values use a safe JSON null representation") {
	Object *null_object = nullptr;
	const Variant null_object_value = null_object;
	REQUIRE(null_object_value.get_type() == Variant::OBJECT);

	Variant encoded;
	String error;
	REQUIRE(MCPVariantCodec::encode(null_object_value, encoded, &error) == OK);
	CHECK(error.is_empty());
	CHECK(encoded.get_type() == Variant::NIL);

	Variant decoded;
	REQUIRE(MCPVariantCodec::decode(encoded, decoded, &error) == OK);
	CHECK(decoded.get_type() == Variant::NIL);

	Object *object = memnew(Object);
	Variant freed_object_value = object;
	memdelete(object);
	CHECK(MCPVariantCodec::encode(freed_object_value, encoded, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("no longer valid"));
}

TEST_CASE("[MCP][Provider] Resource references round-trip by safe res path") {
	const String resource_path = "res://tests/editor/mcp/data/mcp_codec_resource.tres";
	REQUIRE(ResourceLoader::exists(resource_path));
	const Ref<Resource> resource = ResourceLoader::load(resource_path);
	REQUIRE(resource.is_valid());

	Variant encoded;
	String error;
	REQUIRE(MCPVariantCodec::encode(resource, encoded, &error) == OK);

	Variant decoded;
	REQUIRE(MCPVariantCodec::decode(encoded, decoded, &error) == OK);
	REQUIRE(decoded.get_type() == Variant::OBJECT);
	const Ref<Resource> decoded_resource = decoded;
	REQUIRE(decoded_resource.is_valid());
	CHECK(decoded_resource->get_path() == resource_path);
}

TEST_CASE("[MCP][Provider] Encoded unsafe type tags are rejected before native decoding") {
	Variant decoded;
	String error;

	Dictionary rid_tag;
	rid_tag["type"] = "RID";
	CHECK(MCPVariantCodec::decode(rid_tag, decoded, &error) == ERR_INVALID_DATA);
	CHECK(error.contains("not allowed"));

	Dictionary typed_array;
	typed_array["type"] = "Array";
	typed_array["elem_type"] = "Callable";
	typed_array["args"] = Array();
	CHECK(MCPVariantCodec::decode(typed_array, decoded, &error) == ERR_INVALID_DATA);

	Dictionary resource_object;
	resource_object["type"] = "Resource";
	resource_object["props"] = Array();
	CHECK(MCPVariantCodec::decode(resource_object, decoded, &error) == ERR_INVALID_DATA);
}

} // namespace TestMCPVariantCodec
