/**************************************************************************/
/*  test_mcp_runtime_property_controller.cpp                              */
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

#include "scene/2d/node_2d.h"
#include "scene/debugger/mcp_runtime_property_controller.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_runtime_property_controller);

namespace TestMCPRuntimePropertyController {

TEST_CASE("[MCP][Runtime Property][SceneTree] Hidden native properties are set and read back in the running project") {
	Window *root = SceneTree::get_singleton()->get_root();
	Node2D *parent = memnew(Node2D);
	parent->set_name("MCPRuntimePropertyParent");
	root->add_child(parent);
	parent->set_position(Vector2(20.0, 10.0));
	Node2D *child = memnew(Node2D);
	child->set_name("Child");
	parent->add_child(child);

	Dictionary arguments;
	arguments["path"] = String(child->get_path());
	arguments["property"] = "global_position";
	arguments["value"] = Vector2(1.05, -2.25);
	Dictionary result;
	String error_code;
	String error_message;
	CHECK(MCPRuntimePropertyController::execute(arguments, result, error_code, error_message) == OK);
	CHECK(error_code.is_empty());
	CHECK(error_message.is_empty());
	CHECK(child->get_global_position().is_equal_approx(Vector2(1.05, -2.25)));
	CHECK_FALSE(child->get_position().is_equal_approx(child->get_global_position()));
	CHECK(Vector2(result.get("value", Vector2())).is_equal_approx(child->get_global_position()));

	arguments["property"] = "missing_property";
	CHECK(MCPRuntimePropertyController::execute(arguments, result, error_code, error_message) == ERR_DOES_NOT_EXIST);
	CHECK(error_code == "RUNTIME_PROPERTY_NOT_FOUND");
	CHECK(error_message.contains("missing_property"));

	memdelete(parent);
}

} // namespace TestMCPRuntimePropertyController
