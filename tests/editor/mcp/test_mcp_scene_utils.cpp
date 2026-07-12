/**************************************************************************/
/*  test_mcp_scene_utils.cpp                                              */
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

#include "editor/mcp/providers/mcp_scene_utils.h"
#include "scene/main/node.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_scene_utils);

namespace TestMCPSceneUtils {

TEST_CASE("[MCP][Provider] Scene helpers keep node paths relative to the edited root") {
	Node *container = memnew(Node);
	container->set_name("Container");
	Node *root = memnew(Node);
	root->set_name("SceneRoot");
	container->add_child(root);
	Node *sibling = memnew(Node);
	sibling->set_name("Sibling");
	container->add_child(sibling);

	Node *child = memnew(Node);
	child->set_name("Child");
	root->add_child(child);
	child->set_owner(root);
	Node *grandchild = memnew(Node);
	grandchild->set_name("Grandchild");
	child->add_child(grandchild);
	grandchild->set_owner(root);

	CHECK(MCPSceneUtils::find_node(root, ".") == root);
	CHECK(MCPSceneUtils::find_node(root, "SceneRoot") == root);
	CHECK(MCPSceneUtils::find_node(root, "Child/Grandchild") == grandchild);
	CHECK(MCPSceneUtils::find_node(root, "SceneRoot/Child") == child);
	CHECK(MCPSceneUtils::find_node(root, "../Sibling") == nullptr);
	CHECK(MCPSceneUtils::get_relative_path(root, root) == ".");
	CHECK(MCPSceneUtils::get_relative_path(root, grandchild) == "Child/Grandchild");
	CHECK(MCPSceneUtils::is_node_editable(root, child));
	CHECK_FALSE(MCPSceneUtils::is_node_in_scene(root, sibling));

	const Dictionary summary = MCPSceneUtils::make_node_summary(root, child);
	CHECK(summary.get("path", String()) == "Child");
	CHECK(summary.get("type", String()) == "Node");
	CHECK(summary.get("owner", String()) == ".");
	CHECK(bool(summary.get("editable", false)));

	memdelete(container);
}

TEST_CASE("[MCP][Provider] Scene trees respect depth and internal-node controls") {
	Node *root = memnew(Node);
	root->set_name("SceneRoot");
	Node *child = memnew(Node);
	child->set_name("Child");
	root->add_child(child);
	child->set_owner(root);
	Node *grandchild = memnew(Node);
	grandchild->set_name("Grandchild");
	child->add_child(grandchild);
	grandchild->set_owner(root);
	Node *internal = memnew(Node);
	internal->set_name("Internal");
	root->add_child(internal, false, Node::INTERNAL_MODE_FRONT);

	Dictionary tree;
	String error;
	REQUIRE(MCPSceneUtils::make_tree(root, root, 1, false, tree, &error) == OK);
	CHECK(int(tree.get("childCount", -1)) == 1);
	const Array children = tree.get("children", Array());
	REQUIRE(children.size() == 1);
	const Dictionary child_entry = children[0];
	CHECK(bool(child_entry.get("truncated", false)));
	CHECK(Array(child_entry.get("children", Array())).is_empty());

	REQUIRE(MCPSceneUtils::make_tree(root, root, 0, true, tree, &error) == OK);
	CHECK(int(tree.get("childCount", -1)) == 2);
	CHECK(bool(tree.get("truncated", false)));
	CHECK(Array(tree.get("children", Array())).is_empty());
	CHECK(MCPSceneUtils::make_tree(root, root, 65, false, tree, &error) == ERR_INVALID_PARAMETER);

	memdelete(root);
}

} // namespace TestMCPSceneUtils
