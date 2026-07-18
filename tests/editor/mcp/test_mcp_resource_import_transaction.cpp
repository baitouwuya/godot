/**************************************************************************/
/*  test_mcp_resource_import_transaction.cpp                              */
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

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "editor/mcp/providers/mcp_resource_import_transaction.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_resource_import_transaction);

namespace TestMCPResourceImportTransaction {

class ScopedFileRemoval {
	String path;

public:
	explicit ScopedFileRemoval(const String &p_path) :
			path(p_path) {
		remove();
	}

	~ScopedFileRemoval() {
		remove();
	}

	void remove() {
		if (FileAccess::exists(path)) {
			DirAccess::remove_absolute(path);
		}
	}
};

static Error _write_text(const String &p_path, const String &p_text) {
	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return open_error == OK ? ERR_CANT_OPEN : open_error;
	}
	return file->store_string(p_text) ? OK : ERR_FILE_CANT_WRITE;
}

TEST_CASE("[MCP][Provider] Resource import transaction restores protected files and removes new products") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp-resource-transaction", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());

	const String root = temporary_directory->get_current_dir();
	const String target = root.path_join("asset.bin");
	const String metadata = target + ".import";
	const String uid = target + ".uid";
	const String generated = root.path_join("generated.res");
	REQUIRE(_write_text(target, "original target") == OK);
	REQUIRE(_write_text(metadata, "original metadata") == OK);

	MCPResourceImportTransaction transaction;
	String transaction_error;
	REQUIRE(transaction.begin(&transaction_error) == OK);
	REQUIRE(transaction.protect_path(target, &transaction_error) == OK);
	REQUIRE(transaction.protect_path(metadata, &transaction_error) == OK);
	REQUIRE(transaction.protect_path(uid, &transaction_error) == OK);

	REQUIRE(_write_text(target, "changed target") == OK);
	REQUIRE(_write_text(metadata, "changed metadata") == OK);
	REQUIRE(_write_text(uid, "new uid") == OK);
	REQUIRE(_write_text(generated, "new generated product") == OK);
	transaction.track_created_path(generated);

	CHECK(transaction.rollback(&transaction_error) == OK);
	CHECK(transaction_error.is_empty());
	CHECK(FileAccess::get_file_as_string(target) == "original target");
	CHECK(FileAccess::get_file_as_string(metadata) == "original metadata");
	CHECK_FALSE(FileAccess::exists(uid));
	CHECK_FALSE(FileAccess::exists(generated));
}

TEST_CASE("[MCP][Provider] Resource import transaction removes only explicitly tracked paths") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp-resource-transaction-snapshot", false, &temp_error);
	REQUIRE(temp_error == OK);
	REQUIRE(temporary_directory.is_valid());

	const String root = temporary_directory->get_current_dir();
	const String project_root = root.path_join("project");
	const String existing = project_root.path_join("generated/existing.res");
	const String created = project_root.path_join("generated/created.res");
	const String outside = root.path_join("outside.res");
	REQUIRE(DirAccess::make_dir_recursive_absolute(existing.get_base_dir()) == OK);
	REQUIRE(_write_text(existing, "before") == OK);

	MCPResourceImportTransaction transaction;
	String transaction_error;
	REQUIRE(transaction.begin(&transaction_error) == OK);

	REQUIRE(_write_text(existing, "changed but unprotected") == OK);
	REQUIRE(_write_text(created, "new project product") == OK);
	REQUIRE(_write_text(outside, "new outside product") == OK);
	transaction.track_created_path(created);

	CHECK(transaction.rollback(&transaction_error) == OK);
	CHECK(transaction_error.is_empty());
	CHECK(FileAccess::get_file_as_string(existing) == "changed but unprotected");
	CHECK_FALSE(FileAccess::exists(created));
	CHECK(FileAccess::get_file_as_string(outside) == "new outside product");
}

TEST_CASE("[MCP][Provider] Committed resource import transaction keeps changes") {
	Error temp_error = OK;
	Ref<DirAccess> temporary_directory = DirAccess::create_temp("mcp-resource-transaction-commit", false, &temp_error);
	REQUIRE(temp_error == OK);

	const String target = temporary_directory->get_current_dir().path_join("asset.bin");
	REQUIRE(_write_text(target, "before") == OK);

	MCPResourceImportTransaction transaction;
	REQUIRE(transaction.begin() == OK);
	REQUIRE(transaction.protect_path(target) == OK);
	REQUIRE(_write_text(target, "after") == OK);
	transaction.commit();

	CHECK(FileAccess::get_file_as_string(target) == "after");
}

TEST_CASE("[MCP][Provider] Resource import transaction restores res paths") {
	const String resource_path = "res://.godot/mcp_tests/resource_transaction_" + String::num_int64(OS::get_singleton()->get_process_id()) + ".tmp";
	ScopedFileRemoval cleanup(resource_path);
	REQUIRE(DirAccess::make_dir_recursive_absolute(resource_path.get_base_dir()) == OK);
	REQUIRE(_write_text(resource_path, "before") == OK);

	MCPResourceImportTransaction transaction;
	REQUIRE(transaction.begin() == OK);
	REQUIRE(transaction.protect_path(resource_path) == OK);
	REQUIRE(_write_text(resource_path, "after") == OK);
	CHECK(transaction.rollback() == OK);

	CHECK(FileAccess::get_file_as_string(resource_path) == "before");
}

} // namespace TestMCPResourceImportTransaction
