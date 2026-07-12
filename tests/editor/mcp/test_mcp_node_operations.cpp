/**************************************************************************/
/*  test_mcp_node_operations.cpp                                          */
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

#include "editor/mcp/providers/mcp_node_operations.h"
#include "scene/main/node.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_node_operations);

namespace TestMCPNodeOperations {

TEST_CASE("[MCP][Provider] Node operations do not mutate without an editor undo manager") {
	Node *root = memnew(Node);
	root->set_name("SceneRoot");

	Node *created = nullptr;
	String error;
	CHECK(MCPNodeOperations::create_node(root, root, "Node", "Created", nullptr, created, &error) == ERR_UNCONFIGURED);
	CHECK(created == nullptr);
	CHECK(root->get_child_count(false) == 0);

	PropertyInfo property_info;
	REQUIRE(MCPNodeOperations::find_property_info(root, "process_mode", property_info));
	const Node::ProcessMode old_process_mode = root->get_process_mode();
	CHECK(MCPNodeOperations::set_property(root, root, "process_mode", int(Node::PROCESS_MODE_ALWAYS), nullptr, &error) == ERR_UNCONFIGURED);
	CHECK(root->get_process_mode() == old_process_mode);

	Node *foreign_child = memnew(Node);
	root->add_child(foreign_child);
	CHECK(MCPNodeOperations::set_property(root, foreign_child, "process_mode", int(Node::PROCESS_MODE_ALWAYS), nullptr, &error) == ERR_UNAUTHORIZED);
	CHECK(foreign_child->get_process_mode() == Node::PROCESS_MODE_INHERIT);

	memdelete(root);
}

TEST_CASE("[MCP][Provider] Node creation rejects non-Node classes before allocating") {
	Node *root = memnew(Node);
	Node *created = nullptr;
	String error;
	CHECK(MCPNodeOperations::create_node(root, root, "Resource", "Invalid", nullptr, created, &error) == ERR_INVALID_DATA);
	CHECK(created == nullptr);
	CHECK(root->get_child_count(false) == 0);
	memdelete(root);
}

} // namespace TestMCPNodeOperations
