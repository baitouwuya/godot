/**************************************************************************/
/*  test_mcp_file_provider.cpp                                            */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/os/os.h"
#include "editor/mcp/providers/mcp_file_provider.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_file_provider);

namespace TestMCPFileProvider {

class ProjectFileCleanup {
	String absolute_path;

public:
	explicit ProjectFileCleanup(const String &p_absolute_path) :
			absolute_path(p_absolute_path) {
		remove();
	}

	~ProjectFileCleanup() {
		remove();
	}

	void remove() {
		if (!absolute_path.is_empty() && FileAccess::exists(absolute_path)) {
			DirAccess::remove_absolute(absolute_path);
		}
	}
};

TEST_CASE("[MCP][Provider] File create is UTF-8 safe and overwrite-aware") {
	REQUIRE(OS::get_singleton() != nullptr);
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_file_provider", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory->make_dir("project") == OK);
	const String project_root = temporary_directory->get_current_dir().path_join("project");

	const String resource_path = "res://.godot/mcp_tests/file_provider_" +
			String::num_int64(OS::get_singleton()->get_process_id()) + ".txt";
	const String absolute_path = project_root.path_join(resource_path.trim_prefix("res://"));
	ProjectFileCleanup cleanup(absolute_path);

	MCPToolRegistry registry;
	MCPFileProvider *provider = memnew(MCPFileProvider(project_root));
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(registry.has_tool("godot.file.create"));
	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 1);
	const Dictionary output_schema = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK(Dictionary(output_schema.get("properties", Dictionary())).has("bytesWritten"));
	CHECK(PackedStringArray(output_schema.get("required", PackedStringArray())).has("overwritten"));
	CHECK_FALSE(bool(output_schema.get("additionalProperties", true)));

	const String first_content = "hello " + String::chr(0x4E16) + String::chr(0x754C);
	Dictionary arguments;
	arguments["path"] = resource_path;
	arguments["content"] = first_content;

	MCPToolCallContext context;
	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.file.create", arguments, context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	CHECK_FALSE(bool(call_result.result.get("isError", false)));

	const Dictionary created = call_result.result.get("structuredContent", Dictionary());
	CHECK(created.get("path", String()) == resource_path);
	CHECK(int(created.get("bytesWritten", 0)) == first_content.utf8().length());
	CHECK_FALSE(bool(created.get("overwritten", true)));

	Error read_error = OK;
	CHECK(FileAccess::get_file_as_string(absolute_path, &read_error) == first_content);
	CHECK(read_error == OK);

	call_result = registry.call_tool("godot.file.create", arguments, context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(call_result.result.get("isError", false)));
	const Dictionary exists_error =
			Dictionary(call_result.result.get("structuredContent", Dictionary())).get("error", Dictionary());
	CHECK(exists_error.get("code", String()) == "FILE_EXISTS");

	const String replacement_content = "replacement";
	arguments["content"] = replacement_content;
	arguments["overwrite"] = true;
	call_result = registry.call_tool("godot.file.create", arguments, context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	CHECK_FALSE(bool(call_result.result.get("isError", false)));
	CHECK(bool(Dictionary(call_result.result.get("structuredContent", Dictionary())).get("overwritten", false)));
	CHECK(FileAccess::get_file_as_string(absolute_path) == replacement_content);
	provider->unregister_tools();
	CHECK(registry.call_tool("godot.file.create", arguments, context).status == MCPToolRegistry::CALL_TOOL_NOT_FOUND);
	memdelete(provider);
}

TEST_CASE("[MCP][Provider] File create rejects traversal and malformed arguments before writing") {
	MCPToolRegistry registry;
	MCPFileProvider *provider = memnew(MCPFileProvider);
	REQUIRE(provider->register_tools(&registry) == OK);

	MCPToolCallContext context;

	Dictionary arguments;
	arguments["path"] = "res://../outside.txt";
	arguments["content"] = "blocked";
	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.file.create", arguments, context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(call_result.result.get("isError", false)));

	arguments["path"] = "res://safe.txt";
	arguments["extra"] = true;
	call_result = registry.call_tool("godot.file.create", arguments, context);
	REQUIRE(call_result.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(call_result.result.get("isError", false)));
	const Dictionary malformed_error = Dictionary(call_result.result.get("structuredContent", Dictionary())).get("error", Dictionary());
	CHECK(malformed_error.get("code", String()) == "INVALID_ARGUMENTS");
	CHECK(Dictionary(malformed_error.get("details", Dictionary())).get("keyword", String()) == "additionalProperties");

	provider->unregister_tools();
	memdelete(provider);
}

} // namespace TestMCPFileProvider
