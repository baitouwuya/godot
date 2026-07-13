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
