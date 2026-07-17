/**************************************************************************/
/*  test_mcp_project_log_reader.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "editor/mcp/providers/mcp_project_log_reader.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_project_log_reader);

namespace TestMCPProjectLogReader {

static void _write(const String &p_path, const String &p_text) {
	Error error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &error);
	REQUIRE(error == OK);
	REQUIRE(file.is_valid());
	file->store_string(p_text);
}

TEST_CASE("[MCP] Project log reader accepts only logger rotations and returns a bounded tail") {
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_project_log_reader", false, &error);
	REQUIRE(error == OK);
	REQUIRE(temporary_directory.is_valid());
	const String directory = temporary_directory->get_current_dir();
	const String base = directory.path_join("godot.log");
	const String rotated = directory.path_join("godot2026-07-17T18.00.00.log");
	_write(rotated, "one\ntwo\nthree\nfour\n");
	_write(directory.path_join("godot-secret.log"), "must not match\n");
	_write(directory.path_join("godot2026-07-17T18.00.00.txt"), "wrong extension\n");

	String latest;
	CHECK(MCPProjectLogReader::find_latest_log(base, latest) == OK);
	CHECK(latest == rotated);
	MCPProjectLogReader::Result result;
	CHECK(MCPProjectLogReader::read_latest_from_base(base, 2, result) == OK);
	CHECK(result.path == rotated);
	CHECK(result.total_lines == 4);
	CHECK(result.shown_lines == 2);
	CHECK(result.truncated);
	CHECK(result.text == "three\nfour");
}

TEST_CASE("[MCP] Project log reader rejects unbounded reads and missing logs") {
	MCPProjectLogReader::Result result;
	CHECK(MCPProjectLogReader::read_latest_from_base(String(), 20, result) == ERR_INVALID_PARAMETER);
	Error error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp_project_log_missing", false, &error);
	REQUIRE(error == OK);
	CHECK(MCPProjectLogReader::read_latest_from_base(temporary_directory->get_current_dir().path_join("godot.log"), 20, result) == ERR_FILE_NOT_FOUND);
}

} // namespace TestMCPProjectLogReader
