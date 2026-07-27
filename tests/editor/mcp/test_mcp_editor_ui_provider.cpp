/**************************************************************************/
/*  test_mcp_editor_ui_provider.cpp                                       */
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
/* "Software"), to deal in the Software without restriction, including    */
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

#include "core/mcp/mcp_tool_registry.h"
#include "editor/editor_node.h"
#include "editor/mcp/providers/mcp_editor_ui_provider.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/slider.h"
#include "tests/signal_watcher.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_editor_ui_provider);

namespace TestMCPEditorUIProvider {

static MCPToolRegistry::CallResult _call(MCPToolRegistry &p_registry, const StringName &p_name, const Dictionary &p_arguments, const String &p_session_id = "ui-test-session") {
	MCPToolCallContext context;
	if (!p_session_id.is_empty()) {
		context.session["sessionId"] = p_session_id;
	}
	return p_registry.call_tool(p_name, p_arguments, context);
}

static Dictionary _content(const MCPToolRegistry::CallResult &p_result) {
	INFO(p_result.result);
	REQUIRE(p_result.status == MCPToolRegistry::CALL_OK);
	return p_result.result.get("structuredContent", Dictionary());
}

static String _error_code(const MCPToolRegistry::CallResult &p_result) {
	INFO(p_result.result);
	REQUIRE(p_result.status == MCPToolRegistry::CALL_OK);
	REQUIRE(bool(p_result.result.get("isError", false)));
	return Dictionary(_content(p_result).get("error", Dictionary())).get("code", String());
}

static Dictionary _find_item(const Array &p_items, const String &p_role, const String &p_name) {
	for (const Variant &item_value : p_items) {
		const Dictionary item = item_value;
		if (item.get("role", String()) == p_role && item.get("name", String()) == p_name) {
			return item;
		}
	}
	return Dictionary();
}

static void _add_editor_test_controls(Control *p_root, LineEdit *p_secret, HSlider *p_range, OptionButton *p_option) {
	p_secret->set_accessibility_name("MCP Secret Field");
	p_secret->set_secret(true);
	p_secret->set_editable(true);
	p_secret->set_text("not-for-tools");
	p_secret->set_position(Point2(8, 8));
	p_secret->set_size(Size2(180, 32));
	p_root->add_child(p_secret);

	p_range->set_accessibility_name("MCP Test Range");
	p_range->set_min(0);
	p_range->set_max(10);
	p_range->set_step(2);
	p_range->set_value(2);
	p_range->set_position(Point2(8, 48));
	p_range->set_size(Size2(180, 32));
	p_root->add_child(p_range);

	p_option->set_accessibility_name("MCP Test Option");
	p_option->add_item("First");
	p_option->add_item("Second");
	p_option->set_position(Point2(8, 88));
	p_option->set_size(Size2(180, 32));
	p_root->add_child(p_option);
}

TEST_CASE("[MCP][Provider] Editor UI tools expose strict session-bound schemas") {
	MCPToolRegistry registry;
	MCPEditorUIProvider provider;
	REQUIRE(provider.register_tools(&registry) == OK);
	CHECK(provider.register_tools(&registry) == ERR_ALREADY_IN_USE);
	const Array tool_definitions = registry.get_tool_definitions();
	REQUIRE(tool_definitions.size() == 2);
	const Dictionary snapshot_annotations = Dictionary(tool_definitions[0]).get("annotations", Dictionary());
	CHECK_FALSE(bool(snapshot_annotations.get("readOnlyHint", true)));
	CHECK(bool(snapshot_annotations.get("destructiveHint", false)));
	CHECK_FALSE(bool(snapshot_annotations.get("idempotentHint", true)));

	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 2);
	CHECK(names[0] == "godot.editor.ui.get_actions");
	CHECK(names[1] == "godot.editor.ui.perform");

	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 2);
	for (const Variant &definition_value : definitions) {
		const Dictionary schema = Dictionary(definition_value).get("inputSchema", Dictionary());
		CHECK(schema.get("type", String()) == "object");
		CHECK_FALSE(bool(schema.get("additionalProperties", true)));
	}
	const Dictionary snapshot_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK(Dictionary(snapshot_output.get("properties", Dictionary())).has("items"));
	CHECK(PackedStringArray(snapshot_output.get("required", PackedStringArray())).has("snapshotId"));
	CHECK_FALSE(bool(snapshot_output.get("additionalProperties", true)));
	const Dictionary perform_input = Dictionary(definitions[1]).get("inputSchema", Dictionary());
	const Dictionary perform_snapshot_id = Dictionary(perform_input.get("properties", Dictionary())).get("snapshotId", Dictionary());
	CHECK(int(perform_snapshot_id.get("minLength", 0)) == 1);
	const Dictionary perform_output = Dictionary(definitions[1]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(perform_output.get("required", PackedStringArray())).has("snapshotInvalidated"));
	CHECK_FALSE(bool(perform_output.get("additionalProperties", true)));

	CHECK(_error_code(_call(registry, "godot.editor.ui.get_actions", Dictionary(), String())) == "MCP_SESSION_REQUIRED");
	Dictionary invalid;
	invalid["unknown"] = true;
	CHECK(_error_code(_call(registry, "godot.editor.ui.get_actions", invalid)) == "INVALID_ARGUMENTS");

	provider.unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
}

TEST_CASE("[MCP][Provider] Editor UI snapshots are monotonic, latest-only, and isolated by session") {
	MCPToolRegistry registry;
	MCPEditorUIProvider provider;
	REQUIRE(provider.register_tools(&registry) == OK);

	Dictionary first = _content(_call(registry, "godot.editor.ui.get_actions", Dictionary(), "first"));
	Dictionary second = _content(_call(registry, "godot.editor.ui.get_actions", Dictionary(), "first"));
	Dictionary other = _content(_call(registry, "godot.editor.ui.get_actions", Dictionary(), "other"));
	const String first_id = first.get("snapshotId", String());
	const String second_id = second.get("snapshotId", String());
	const String other_id = other.get("snapshotId", String());
	CHECK_FALSE(first_id.is_empty());
	CHECK_FALSE(second_id.is_empty());
	CHECK_FALSE(other_id.is_empty());
	CHECK(first_id != second_id);
	CHECK(second_id != other_id);
	CHECK(first.get("items", Variant()).get_type() == Variant::ARRAY);
	CHECK(first.get("count", Variant()).get_type() == Variant::INT);

	Dictionary perform;
	perform["snapshotId"] = first_id;
	perform["targetId"] = "missing";
	perform["action"] = "click";
	CHECK(_error_code(_call(registry, "godot.editor.ui.perform", perform, "first")) == "STALE_UI_SNAPSHOT");

	perform["snapshotId"] = second_id;
	CHECK(_error_code(_call(registry, "godot.editor.ui.perform", perform, "other")) == "STALE_UI_SNAPSHOT");
	CHECK(_error_code(_call(registry, "godot.editor.ui.perform", perform, "first")) == "UI_TARGET_NOT_FOUND");

	provider.release_session("first");
	CHECK(_error_code(_call(registry, "godot.editor.ui.perform", perform, "first")) == "STALE_UI_SNAPSHOT");
}

TEST_CASE("[MCP][Provider] Editor UI arguments enforce bounded traversal and action values") {
	MCPToolRegistry registry;
	MCPEditorUIProvider provider;
	REQUIRE(provider.register_tools(&registry) == OK);

	Dictionary arguments;
	arguments["maxDepth"] = 0;
	CHECK(_error_code(_call(registry, "godot.editor.ui.get_actions", arguments)) == "INVALID_ARGUMENTS");
	arguments.clear();
	arguments["limit"] = 2049;
	CHECK(_error_code(_call(registry, "godot.editor.ui.get_actions", arguments)) == "INVALID_ARGUMENTS");
	arguments.clear();
	arguments["includeValues"] = "yes";
	CHECK(_error_code(_call(registry, "godot.editor.ui.get_actions", arguments)) == "INVALID_ARGUMENTS");

	Dictionary perform;
	perform["snapshotId"] = "snapshot";
	perform["targetId"] = "target";
	CHECK(_error_code(_call(registry, "godot.editor.ui.perform", perform)) == "INVALID_ARGUMENTS");
}

TEST_CASE("[MCP][Provider] Editor UI snapshots inspect and operate native editor controls") {
	EditorNode *editor = EditorNode::get_singleton();
	if (!editor) {
		return; // EditorNode is not constructed by the headless unit-test runner.
	}
	Control *root = editor->get_gui_base();
	REQUIRE(root != nullptr);

	LineEdit *secret = memnew(LineEdit);
	HSlider *range = memnew(HSlider);
	OptionButton *option = memnew(OptionButton);
	_add_editor_test_controls(root, secret, range, option);

	MCPToolRegistry registry;
	MCPEditorUIProvider provider;
	REQUIRE(provider.register_tools(&registry) == OK);
	Dictionary content = _content(_call(registry, "godot.editor.ui.get_actions", Dictionary()));
	const String snapshot_id = content.get("snapshotId", String());
	const Array items = content.get("items", Array());
	const Dictionary other_session = _content(_call(registry, "godot.editor.ui.get_actions", Dictionary(), "other-ui-session"));
	const Dictionary secret_item = _find_item(items, "text_field", "MCP Secret Field");
	const Dictionary range_item = _find_item(items, "range", "MCP Test Range");
	const Dictionary second_option = _find_item(items, "option", "Second");
	REQUIRE_FALSE(secret_item.is_empty());
	REQUIRE_FALSE(range_item.is_empty());
	REQUIRE_FALSE(second_option.is_empty());
	CHECK_FALSE(secret_item.has("value"));
	CHECK_FALSE(String(secret_item.get("targetId", String())).contains(String::num_uint64(uint64_t(secret->get_instance_id()))));

	Dictionary perform;
	perform["snapshotId"] = snapshot_id;
	perform["targetId"] = range_item.get("targetId", String());
	perform["action"] = "set_value";
	perform["value"] = 8;
	content = _content(_call(registry, "godot.editor.ui.perform", perform));
	CHECK(bool(content.get("performed", false)));
	CHECK(bool(content.get("snapshotInvalidated", false)));
	CHECK(range->get_value() == doctest::Approx(8));
	CHECK(_error_code(_call(registry, "godot.editor.ui.perform", perform)) == "STALE_UI_SNAPSHOT");
	Dictionary other_perform;
	other_perform["snapshotId"] = other_session.get("snapshotId", String());
	other_perform["targetId"] = "missing";
	other_perform["action"] = "click";
	CHECK(_error_code(_call(registry, "godot.editor.ui.perform", other_perform, "other-ui-session")) == "UI_TARGET_NOT_FOUND");

	content = _content(_call(registry, "godot.editor.ui.get_actions", Dictionary()));
	perform["snapshotId"] = content.get("snapshotId", String());
	perform["targetId"] = _find_item(content.get("items", Array()), "option", "Second").get("targetId", String());
	perform["action"] = "select";
	perform.erase("value");
	CHECK_FALSE(bool(_call(registry, "godot.editor.ui.perform", perform).result.get("isError", false)));
	CHECK(option->get_selected() == 1);

	memdelete(option);
	memdelete(range);
	memdelete(secret);
}

TEST_CASE("[MCP][UI] BaseButton semantic activation preserves toggle state and signals") {
	Button button;
	button.set_toggle_mode(true);
	SIGNAL_WATCH(&button, "pressed");
	SIGNAL_WATCH(&button, "toggled");

	button.activate();
	CHECK(button.is_pressed());
	Array empty_signal_args = { {} };
	SIGNAL_CHECK("pressed", empty_signal_args);
	Array toggled_args = { { true } };
	SIGNAL_CHECK("toggled", toggled_args);

	button.activate();
	CHECK_FALSE(button.is_pressed());
	SIGNAL_CHECK("pressed", empty_signal_args);
	toggled_args = { { false } };
	SIGNAL_CHECK("toggled", toggled_args);

	button.set_disabled(true);
	ERR_PRINT_OFF;
	button.activate();
	ERR_PRINT_ON;
	CHECK_FALSE(button.is_pressed());

	Button radio;
	radio.set_toggle_mode(true);
	Ref<ButtonGroup> group;
	group.instantiate();
	radio.set_button_group(group);
	radio.set_pressed(true);
	radio.activate();
	CHECK(radio.is_pressed());
}

} // namespace TestMCPEditorUIProvider
