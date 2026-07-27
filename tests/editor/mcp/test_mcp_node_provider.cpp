/**************************************************************************/
/*  test_mcp_node_provider.cpp                                            */
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
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/script_language.h"
#include "core/variant/callable.h"
#include "editor/mcp/providers/mcp_node_inspection.h"
#include "editor/mcp/providers/mcp_node_provider.h"
#include "editor/mcp/providers/mcp_node_tool_utils.h"
#include "editor/mcp/providers/mcp_undo_redo_action.h"
#include "editor/mcp/providers/mcp_variant_codec.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#include "mcp_test_undo_redo_action.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/gdscript.h"
#endif

TEST_FORCE_LINK(test_mcp_node_provider);

namespace TestMCPNodeProvider {

using TestUndoRedoAction = MCPTestUndoRedoAction;

class NodeProviderFixture {
public:
	NodeProviderFixture() {
		scene_root = memnew(Node);
		scene_root->set_name("SceneRoot");
		SceneTree::get_singleton()->get_root()->add_child(scene_root);
		provider = memnew(MCPNodeProvider(scene_root, &undo_redo));
	}

	~NodeProviderFixture() {
		if (provider) {
			provider->unregister_tools();
			memdelete(provider);
		}
		undo_redo.clear_history();
		memdelete(scene_root);
	}

	Node *scene_root = nullptr;
	TestUndoRedoAction undo_redo;
	MCPToolRegistry registry;
	MCPNodeProvider *provider = nullptr;
};

#ifdef MODULE_GDSCRIPT_ENABLED
class ScopedProjectResourcePath {
public:
	ScopedProjectResourcePath() {
		previous_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_valid()) {
			resource_path = filesystem->get_current_dir();
			TestProjectSettingsInternalsAccessor::resource_path() = resource_path;
		}
	}

	~ScopedProjectResourcePath() {
		TestProjectSettingsInternalsAccessor::resource_path() = previous_resource_path;
	}

	bool is_valid() const {
		return resource_path.is_absolute_path();
	}

private:
	String previous_resource_path;
	String resource_path;
};

class ScopedGDScriptLanguage {
public:
	explicit ScopedGDScriptLanguage(bool p_scripting_enabled = true) {
		language = GDScriptLanguage::get_singleton();
		previous_scripting_enabled = ScriptServer::is_scripting_enabled();
		if (language && !language->has_any_global_constant(SNAME("PI"))) {
			language->init();
			initialized_here = true;
		}
		ScriptServer::set_scripting_enabled(p_scripting_enabled);
	}

	~ScopedGDScriptLanguage() {
		ScriptServer::set_scripting_enabled(previous_scripting_enabled);
		if (initialized_here) {
			language->finish();
		}
	}

	bool is_available() const {
		return language != nullptr;
	}

private:
	GDScriptLanguage *language = nullptr;
	bool initialized_here = false;
	bool previous_scripting_enabled = true;
};
#endif

static MCPToolRegistry::CallResult _call_tool(MCPToolRegistry &p_registry, const StringName &p_name, const Dictionary &p_arguments) {
	MCPToolCallContext context;
	return p_registry.call_tool(p_name, p_arguments, context);
}

static String _error_code(const MCPToolRegistry::CallResult &p_result) {
	const Dictionary structured = p_result.result.get("structuredContent", Dictionary());
	return Dictionary(structured.get("error", Dictionary())).get("code", String());
}

static Dictionary _error_details(const MCPToolRegistry::CallResult &p_result) {
	const Dictionary structured = p_result.result.get("structuredContent", Dictionary());
	return Dictionary(structured.get("error", Dictionary())).get("details", Dictionary());
}

static bool _check_tool_success(const MCPToolRegistry::CallResult &p_result) {
	INFO(p_result.result);
	CHECK(p_result.status == MCPToolRegistry::CALL_OK);
	if (p_result.status != MCPToolRegistry::CALL_OK) {
		return false;
	}
	const bool is_error = p_result.result.get("isError", false);
	CHECK_FALSE(is_error);
	return !is_error;
}

static Dictionary _find_property(const Array &p_properties, const StringName &p_name) {
	for (int i = 0; i < p_properties.size(); i++) {
		const Dictionary property = p_properties[i];
		if (StringName(property.get("name", StringName())) == p_name) {
			return property;
		}
	}
	return Dictionary();
}

static Dictionary _find_layout_entry(const Array &p_layout, const String &p_kind, const StringName &p_name) {
	for (int i = 0; i < p_layout.size(); i++) {
		const Dictionary entry = p_layout[i];
		if (entry.get("kind", String()) == p_kind && StringName(entry.get("name", StringName())) == p_name) {
			return entry;
		}
	}
	return Dictionary();
}

TEST_CASE("[MCP][Provider] Node tool utilities share signal flag constraints") {
	Dictionary arguments;
	arguments["path"] = "Child";
	String path;
	CHECK(MCPNodeToolUtils::get_required_string(arguments, "path", path));
	CHECK(path == "Child");

	uint32_t flags = UINT32_MAX;
	String error;
	CHECK(MCPNodeToolUtils::get_signal_flags(Dictionary(), flags, error));
	CHECK(flags == 0);
	arguments["flags"] = Object::CONNECT_DEFERRED | Object::CONNECT_ONE_SHOT;
	CHECK(MCPNodeToolUtils::get_signal_flags(arguments, flags, error));
	CHECK(flags == (Object::CONNECT_DEFERRED | Object::CONNECT_ONE_SHOT));
	arguments["flags"] = 2;
	CHECK_FALSE(MCPNodeToolUtils::get_signal_flags(arguments, flags, error));
}

TEST_CASE("[MCP][Provider] Node inspection returns protocol-neutral property data") {
	Node *scene_root = memnew(Node);
	scene_root->set_name("SceneRoot");
	Node *child = memnew(Node);
	child->set_name("Child");
	scene_root->add_child(child);
	child->set_owner(scene_root);

	Dictionary result;
	String error;
	CHECK(MCPNodeInspection::inspect(scene_root, child, result, &error) == OK);
	CHECK(error.is_empty());
	CHECK(result.get("path", String()) == "Child");
	CHECK(result.get("name", String()) == "Child");
	CHECK(int(result.get("propertyCount", 0)) > 0);
	CHECK(result.get("properties", Variant()).get_type() == Variant::ARRAY);
	CHECK(result.get("propertyLayout", Variant()).get_type() == Variant::ARRAY);

	Node *detached = memnew(Node);
	CHECK(MCPNodeInspection::inspect(scene_root, detached, result, &error) == ERR_INVALID_PARAMETER);
	CHECK(result.is_empty());
	CHECK(error.contains("edited scene"));
	memdelete(detached);
	memdelete(scene_root);
}

TEST_CASE("[MCP][Provider] Node tools expose encoded property and undoable mutation schemas") {
	MCPToolRegistry registry;
	MCPNodeProvider *provider = memnew(MCPNodeProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 10);
	CHECK(names[0] == "godot.node.get_properties");
	CHECK(names[1] == "godot.node.create");
	CHECK(names[2] == "godot.node.set_property");
	CHECK(names[3] == "godot.node.attach_script");
	CHECK(names[4] == "godot.node.get_groups");
	CHECK(names[5] == "godot.node.add_to_group");
	CHECK(names[6] == "godot.node.remove_from_group");
	CHECK(names[7] == "godot.node.get_signal_connections");
	CHECK(names[8] == "godot.node.connect_signal");
	CHECK(names[9] == "godot.node.disconnect_signal");

	const Array definitions = registry.get_tool_definitions();
	REQUIRE(definitions.size() == 10);
	const Dictionary set_schema = Dictionary(definitions[2]).get("inputSchema", Dictionary());
	const PackedStringArray required = set_schema.get("required", PackedStringArray());
	CHECK(required.has("path"));
	CHECK(required.has("property"));
	CHECK(required.has("value"));
	const Dictionary property_input = Dictionary(definitions[0]).get("inputSchema", Dictionary());
	CHECK(int(Dictionary(Dictionary(property_input.get("properties", Dictionary())).get("path", Dictionary())).get("minLength", 0)) == 1);
	const Dictionary properties_output = Dictionary(definitions[0]).get("outputSchema", Dictionary());
	CHECK(Dictionary(properties_output.get("properties", Dictionary())).has("propertyLayout"));
	CHECK(PackedStringArray(properties_output.get("required", PackedStringArray())).has("scriptPropertyCount"));
	const Dictionary create_input = Dictionary(definitions[1]).get("inputSchema", Dictionary());
	CHECK(int(Dictionary(Dictionary(create_input.get("properties", Dictionary())).get("type", Dictionary())).get("minLength", 0)) == 1);
	const Dictionary create_output = Dictionary(definitions[1]).get("outputSchema", Dictionary());
	CHECK(PackedStringArray(create_output.get("required", PackedStringArray())).has("childCount"));
	const Dictionary connect_schema = Dictionary(definitions[8]).get("inputSchema", Dictionary());
	CHECK(Dictionary(connect_schema.get("properties", Dictionary())).has("flags"));
	const Dictionary flags_schema = Dictionary(connect_schema.get("properties", Dictionary())).get("flags", Dictionary());
	const Array allowed_flags = flags_schema.get("enum", Array());
	CHECK(allowed_flags.size() == 4);
	CHECK(allowed_flags.has(0));
	CHECK(allowed_flags.has(Object::CONNECT_DEFERRED | Object::CONNECT_ONE_SHOT));
	const Dictionary groups_output = Dictionary(definitions[4]).get("outputSchema", Dictionary());
	CHECK(Dictionary(groups_output.get("properties", Dictionary())).has("groups"));
	const Dictionary connections_output = Dictionary(definitions[7]).get("outputSchema", Dictionary());
	CHECK(Dictionary(connections_output.get("properties", Dictionary())).has("connections"));

	MCPToolCallContext context;
	Dictionary unexpected_arguments;
	unexpected_arguments["type"] = "Node";
	unexpected_arguments["unexpected"] = true;
	const MCPToolRegistry::CallResult unexpected_result = registry.call_tool("godot.node.create", unexpected_arguments, context);
	CHECK(_error_code(unexpected_result) == "INVALID_ARGUMENTS");
	CHECK(_error_details(unexpected_result).get("keyword", String()) == "additionalProperties");
	Dictionary invalid_flags;
	invalid_flags["path"] = ".";
	invalid_flags["signal"] = "ready";
	invalid_flags["targetPath"] = ".";
	invalid_flags["method"] = "queue_free";
	invalid_flags["flags"] = 2;
	const MCPToolRegistry::CallResult invalid_flags_result = registry.call_tool("godot.node.connect_signal", invalid_flags, context);
	CHECK(_error_code(invalid_flags_result) == "INVALID_ARGUMENTS");
	CHECK(_error_details(invalid_flags_result).get("keyword", String()) == "enum");
	const MCPToolRegistry::CallResult invalid_call = registry.call_tool("godot.node.create", Dictionary(), context);
	REQUIRE(invalid_call.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(invalid_call.result.get("isError", false)));

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
}

TEST_CASE("[SceneTree][MCP][Provider] Node group and signal tools are persistent and undoable") {
	NodeProviderFixture fixture;
	Node *source = memnew(Node);
	source->set_name("Source");
	fixture.scene_root->add_child(source);
	source->set_owner(fixture.scene_root);
	Node *target = memnew(Node);
	target->set_name("Target");
	fixture.scene_root->add_child(target);
	target->set_owner(fixture.scene_root);
	REQUIRE(fixture.provider->register_tools(&fixture.registry) == OK);

	Dictionary add_group;
	add_group["path"] = "Source";
	add_group["group"] = "mcp_test_group";
	const MCPToolRegistry::CallResult group_result = _call_tool(fixture.registry, "godot.node.add_to_group", add_group);
	REQUIRE(_check_tool_success(group_result));
	CHECK(source->is_in_group("mcp_test_group"));
	const bool group_undone = fixture.undo_redo.undo();
	REQUIRE(group_undone);
	CHECK_FALSE(source->is_in_group("mcp_test_group"));
	const bool group_redone = fixture.undo_redo.redo();
	REQUIRE(group_redone);
	CHECK(source->is_in_group("mcp_test_group"));

	Dictionary get_groups;
	get_groups["path"] = "Source";
	const MCPToolRegistry::CallResult groups_result = _call_tool(fixture.registry, "godot.node.get_groups", get_groups);
	REQUIRE(_check_tool_success(groups_result));
	const Array groups = Dictionary(groups_result.result.get("structuredContent", Dictionary())).get("groups", Array());
	REQUIRE(groups.size() == 1);
	CHECK(Dictionary(groups[0]).get("name", String()) == "mcp_test_group");
	CHECK(bool(Dictionary(groups[0]).get("persistent", false)));

	Dictionary connect;
	connect["path"] = "Source";
	connect["signal"] = "ready";
	connect["targetPath"] = "Target";
	connect["method"] = "queue_free";
	connect["flags"] = Object::CONNECT_DEFERRED;
	const MCPToolRegistry::CallResult connect_result = _call_tool(fixture.registry, "godot.node.connect_signal", connect);
	REQUIRE(_check_tool_success(connect_result));
	const Callable callable(target, "queue_free");
	CHECK(source->is_connected("ready", callable));
	const bool connect_undone = fixture.undo_redo.undo();
	REQUIRE(connect_undone);
	CHECK_FALSE(source->is_connected("ready", callable));
	const bool connect_redone = fixture.undo_redo.redo();
	REQUIRE(connect_redone);
	CHECK(source->is_connected("ready", callable));

	Dictionary connections;
	connections["path"] = "Source";
	const MCPToolRegistry::CallResult connections_result = _call_tool(fixture.registry, "godot.node.get_signal_connections", connections);
	REQUIRE(_check_tool_success(connections_result));
	const Array items = Dictionary(connections_result.result.get("structuredContent", Dictionary())).get("connections", Array());
	REQUIRE(items.size() == 1);
	CHECK(Dictionary(items[0]).get("signal", String()) == "ready");
	CHECK(Dictionary(items[0]).get("targetPath", String()) == "Target");
	CHECK(Dictionary(items[0]).get("method", String()) == "queue_free");

	Dictionary disconnect = connect.duplicate();
	disconnect.erase("flags");
	const MCPToolRegistry::CallResult disconnect_result = _call_tool(fixture.registry, "godot.node.disconnect_signal", disconnect);
	REQUIRE(_check_tool_success(disconnect_result));
	CHECK_FALSE(source->is_connected("ready", callable));
	const bool disconnect_undone = fixture.undo_redo.undo();
	REQUIRE(disconnect_undone);
	CHECK(source->is_connected("ready", callable));
}

TEST_CASE("[SceneTree][MCP][Provider] Node tools share undoable actions and leave the scene unsaved") {
	NodeProviderFixture fixture;
	const Error register_error = fixture.provider->register_tools(&fixture.registry);
	REQUIRE(register_error == OK);
	if (register_error != OK) {
		return;
	}

	Dictionary create_arguments;
	create_arguments["type"] = "Node";
	create_arguments["name"] = "Created";
	MCPToolRegistry::CallResult call_result = _call_tool(fixture.registry, "godot.node.create", create_arguments);
	if (!_check_tool_success(call_result)) {
		return;
	}
	Dictionary content = call_result.result.get("structuredContent", Dictionary());
	CHECK(content.get("path", String()) == "Created");
	Node *created = fixture.scene_root->get_node_or_null(NodePath("Created"));
	REQUIRE(created != nullptr);
	if (!created) {
		return;
	}
	CHECK(created->get_owner() == fixture.scene_root);
	CHECK(fixture.undo_redo.is_unsaved());

	const bool create_undone = fixture.undo_redo.undo();
	REQUIRE(create_undone);
	if (!create_undone) {
		return;
	}
	CHECK(fixture.scene_root->get_node_or_null(NodePath("Created")) == nullptr);
	CHECK_FALSE(fixture.undo_redo.is_unsaved());
	const bool create_redone = fixture.undo_redo.redo();
	REQUIRE(create_redone);
	if (!create_redone) {
		return;
	}
	created = fixture.scene_root->get_node_or_null(NodePath("Created"));
	REQUIRE(created != nullptr);
	if (!created) {
		return;
	}
	CHECK(fixture.undo_redo.is_unsaved());

	Dictionary property_arguments;
	property_arguments["path"] = "Created";
	property_arguments["property"] = "process_mode";
	Variant encoded_process_mode;
	REQUIRE(MCPVariantCodec::encode(int(Node::PROCESS_MODE_ALWAYS), encoded_process_mode) == OK);
	property_arguments["value"] = encoded_process_mode;
	call_result = _call_tool(fixture.registry, "godot.node.set_property", property_arguments);
	if (!_check_tool_success(call_result)) {
		return;
	}
	content = call_result.result.get("structuredContent", Dictionary());
	CHECK(content.get("property", String()) == "process_mode");
	CHECK(created->get_process_mode() == Node::PROCESS_MODE_ALWAYS);
	const bool property_undone = fixture.undo_redo.undo();
	REQUIRE(property_undone);
	if (!property_undone) {
		return;
	}
	CHECK(created->get_process_mode() == Node::PROCESS_MODE_INHERIT);
	const bool property_redone = fixture.undo_redo.redo();
	REQUIRE(property_redone);
	if (!property_redone) {
		return;
	}
	CHECK(created->get_process_mode() == Node::PROCESS_MODE_ALWAYS);
	CHECK(fixture.undo_redo.is_unsaved());
}

TEST_CASE("[SceneTree][MCP][Provider] Native properties carrying script usage remain native") {
	NodeProviderFixture fixture;
	RichTextLabel *rich_text_label = memnew(RichTextLabel);
	rich_text_label->set_name("RichTextLabel");
	fixture.scene_root->add_child(rich_text_label);
	rich_text_label->set_owner(fixture.scene_root);
	REQUIRE(fixture.provider->register_tools(&fixture.registry) == OK);

	Dictionary arguments;
	arguments["path"] = "RichTextLabel";
	const MCPToolRegistry::CallResult result = _call_tool(fixture.registry, "godot.node.get_properties", arguments);
	if (!_check_tool_success(result)) {
		return;
	}

	const Dictionary content = result.result.get("structuredContent", Dictionary());
	CHECK(int(content.get("scriptPropertyCount", -1)) == 0);
	const Dictionary effects_property = _find_property(content.get("properties", Array()), "custom_effects");
	REQUIRE_FALSE(effects_property.is_empty());
	CHECK((int64_t(effects_property.get("usage", 0)) & PROPERTY_USAGE_SCRIPT_VARIABLE) != 0);
	CHECK(effects_property.get("source", String()) == "native");
	CHECK_FALSE(bool(effects_property.get("scriptExposed", true)));

	const Dictionary markup_group = _find_layout_entry(content.get("propertyLayout", Array()), "group", "Markup");
	REQUIRE_FALSE(markup_group.is_empty());
	CHECK(markup_group.get("source", String()) == "native");
}

#ifdef MODULE_GDSCRIPT_ENABLED
TEST_CASE("[SceneTree][MCP][Provider][GDScript] Node script attachment is undoable") {
	ScopedProjectResourcePath project_resource_path;
	REQUIRE(project_resource_path.is_valid());
	if (!project_resource_path.is_valid()) {
		return;
	}
	ScopedGDScriptLanguage gdscript_language;
	REQUIRE(gdscript_language.is_available());
	if (!gdscript_language.is_available()) {
		return;
	}

	NodeProviderFixture fixture;
	Node *created = memnew(Node);
	created->set_name("Created");
	fixture.scene_root->add_child(created);
	created->set_owner(fixture.scene_root);
	const Error register_error = fixture.provider->register_tools(&fixture.registry);
	REQUIRE(register_error == OK);
	if (register_error != OK) {
		return;
	}

	const String script_path = "res://tests/editor/mcp/data/mcp_node_script.gd";
	Dictionary script_arguments;
	script_arguments["path"] = "Created";
	script_arguments["scriptPath"] = script_path;
	const MCPToolRegistry::CallResult call_result = _call_tool(fixture.registry, "godot.node.attach_script", script_arguments);
	if (!_check_tool_success(call_result)) {
		return;
	}
	const Dictionary content = call_result.result.get("structuredContent", Dictionary());
	CHECK(content.get("scriptPath", String()) == script_path);
	Ref<Script> attached_script = created->get_script();
	REQUIRE(attached_script.is_valid());
	if (attached_script.is_null()) {
		return;
	}
	CHECK(attached_script->get_path() == script_path);

	Node *target = memnew(Node);
	target->set_name("Target");
	created->add_child(target);
	target->set_owner(fixture.scene_root);
	created->set("exported_node", target);

	Dictionary get_arguments;
	get_arguments["path"] = "Created";
	MCPToolRegistry::CallResult get_result = _call_tool(fixture.registry, "godot.node.get_properties", get_arguments);
	if (!_check_tool_success(get_result)) {
		return;
	}
	Dictionary properties_content = get_result.result.get("structuredContent", Dictionary());
	CHECK(properties_content.get("scriptPath", String()) == script_path);
	CHECK(int(properties_content.get("propertyCount", 0)) > 3);
	CHECK(int(properties_content.get("scriptPropertyCount", 0)) == 4);

	const Array properties = properties_content.get("properties", Array());
	const Dictionary speed_property = _find_property(properties, "exported_speed");
	REQUIRE_FALSE(speed_property.is_empty());
	CHECK(speed_property.get("source", String()) == "script");
	CHECK(bool(speed_property.get("scriptExposed", false)));
	CHECK(speed_property.get("scriptPath", String()) == script_path);
	CHECK(speed_property.get("type", String()) == "float");
	CHECK(int(speed_property.get("typeId", -1)) == Variant::FLOAT);
	CHECK(int(speed_property.get("hint", -1)) == PROPERTY_HINT_RANGE);
	CHECK(speed_property.get("hintString", String()) == "0.0,10.0,0.5");
	CHECK(bool(speed_property.get("valueAvailable", false)));
	CHECK(bool(speed_property.get("encodable", false)));
	CHECK(speed_property.get("value", Variant()) == Variant(2.5));

	const Dictionary path_property = _find_property(properties, "exported_path");
	REQUIRE_FALSE(path_property.is_empty());
	CHECK(path_property.get("source", String()) == "script");
	Variant decoded_path;
	REQUIRE(MCPVariantCodec::decode(path_property.get("value", Variant()), decoded_path) == OK);
	CHECK(decoded_path == Variant(NodePath(".")));
	const String base_script_path = "res://tests/editor/mcp/data/mcp_node_base_script.gd";
	const Dictionary base_property = _find_property(properties, "exported_base");
	REQUIRE_FALSE(base_property.is_empty());
	CHECK(base_property.get("source", String()) == "script");
	CHECK(base_property.get("scriptPath", String()) == base_script_path);
	CHECK(base_property.get("value", Variant()) == Variant(11));

	const Dictionary node_property = _find_property(properties, "exported_node");
	REQUIRE_FALSE(node_property.is_empty());
	CHECK(node_property.get("source", String()) == "script");
	CHECK(bool(node_property.get("valueAvailable", false)));
	CHECK_FALSE(bool(node_property.get("encodable", true)));
	CHECK_FALSE(String(node_property.get("encodingError", String())).is_empty());
	const Dictionary node_reference = node_property.get("valueReference", Dictionary());
	REQUIRE_FALSE(node_reference.is_empty());
	CHECK(node_reference.get("kind", String()) == "node");
	CHECK(node_reference.get("scope", String()) == "editedScene");
	CHECK(node_reference.get("path", String()) == "Created/Target");
	CHECK(node_reference.get("type", String()) == "Node");

	CHECK(_find_property(properties, "runtime_only").is_empty());
	CHECK(_find_property(properties, "stored_only").is_empty());
	const Dictionary native_property = _find_property(properties, "process_mode");
	REQUIRE_FALSE(native_property.is_empty());
	CHECK(native_property.get("source", String()) == "native");
	CHECK_FALSE(bool(native_property.get("scriptExposed", true)));

	const Array property_layout = properties_content.get("propertyLayout", Array());
	const Dictionary custom_category = _find_layout_entry(property_layout, "category", "MCP Test");
	REQUIRE_FALSE(custom_category.is_empty());
	CHECK(custom_category.get("source", String()) == "script");
	CHECK(custom_category.get("scriptPath", String()) == script_path);
	CHECK_FALSE(_find_layout_entry(property_layout, "group", "Values").is_empty());
	CHECK_FALSE(_find_layout_entry(property_layout, "subgroup", "References").is_empty());
	const Dictionary speed_layout = _find_layout_entry(property_layout, "property", "exported_speed");
	REQUIRE_FALSE(speed_layout.is_empty());
	CHECK(speed_layout.get("source", String()) == "script");
	const int speed_index = speed_layout.get("propertyIndex", -1);
	REQUIRE(speed_index >= 0);
	REQUIRE(speed_index < properties.size());
	CHECK(StringName(Dictionary(properties[speed_index]).get("name", StringName())) == "exported_speed");
	const Dictionary base_category = _find_layout_entry(property_layout, "category", "mcp_node_base_script.gd");
	REQUIRE_FALSE(base_category.is_empty());
	CHECK(base_category.get("scriptPath", String()) == base_script_path);

	Dictionary property_arguments;
	property_arguments["path"] = "Created";
	property_arguments["property"] = "exported_speed";
	property_arguments["value"] = "f:4.5";
	const MCPToolRegistry::CallResult property_result = _call_tool(fixture.registry, "godot.node.set_property", property_arguments);
	if (!_check_tool_success(property_result)) {
		return;
	}
	CHECK(double(created->get("exported_speed")) == doctest::Approx(4.5));
	const bool property_undone = fixture.undo_redo.undo();
	REQUIRE(property_undone);
	CHECK(double(created->get("exported_speed")) == doctest::Approx(2.5));
	const bool property_redone = fixture.undo_redo.redo();
	REQUIRE(property_redone);
	CHECK(double(created->get("exported_speed")) == doctest::Approx(4.5));
	REQUIRE(fixture.undo_redo.undo());
	CHECK(double(created->get("exported_speed")) == doctest::Approx(2.5));

	const bool attachment_undone = fixture.undo_redo.undo();
	REQUIRE(attachment_undone);
	if (!attachment_undone) {
		return;
	}
	attached_script = created->get_script();
	CHECK(attached_script.is_null());
	const bool attachment_redone = fixture.undo_redo.redo();
	REQUIRE(attachment_redone);
	if (!attachment_redone) {
		return;
	}
	attached_script = created->get_script();
	REQUIRE(attached_script.is_valid());
	if (attached_script.is_null()) {
		return;
	}
	CHECK(attached_script->get_path() == script_path);
	CHECK(fixture.undo_redo.is_unsaved());
}

TEST_CASE("[SceneTree][Editor][MCP][Provider][GDScript] Placeholder node properties retain script source and layout") {
	ScopedProjectResourcePath project_resource_path;
	REQUIRE(project_resource_path.is_valid());
	if (!project_resource_path.is_valid()) {
		return;
	}
	ScopedGDScriptLanguage gdscript_language(false);
	REQUIRE(gdscript_language.is_available());
	if (!gdscript_language.is_available()) {
		return;
	}

	NodeProviderFixture fixture;
	Node *created = memnew(Node);
	created->set_name("Created");
	fixture.scene_root->add_child(created);
	created->set_owner(fixture.scene_root);
	REQUIRE(fixture.provider->register_tools(&fixture.registry) == OK);

	const String script_path = "res://tests/editor/mcp/data/mcp_node_script.gd";
	Dictionary script_arguments;
	script_arguments["path"] = "Created";
	script_arguments["scriptPath"] = script_path;
	const MCPToolRegistry::CallResult attach_result = _call_tool(fixture.registry, "godot.node.attach_script", script_arguments);
	if (!_check_tool_success(attach_result)) {
		return;
	}

	Dictionary get_arguments;
	get_arguments["path"] = "Created";
	const MCPToolRegistry::CallResult get_result = _call_tool(fixture.registry, "godot.node.get_properties", get_arguments);
	if (!_check_tool_success(get_result)) {
		return;
	}
	const Dictionary content = get_result.result.get("structuredContent", Dictionary());
	CHECK(content.get("scriptPath", String()) == script_path);
	CHECK(int(content.get("scriptPropertyCount", 0)) == 4);

	const Array properties = content.get("properties", Array());
	const Dictionary speed_property = _find_property(properties, "exported_speed");
	REQUIRE_FALSE(speed_property.is_empty());
	CHECK((int64_t(speed_property.get("usage", 0)) & PROPERTY_USAGE_SCRIPT_VARIABLE) == 0);
	CHECK(speed_property.get("source", String()) == "script");
	CHECK(bool(speed_property.get("scriptExposed", false)));
	CHECK(speed_property.get("scriptPath", String()) == script_path);
	CHECK(_find_property(properties, "runtime_only").is_empty());
	CHECK(_find_property(properties, "stored_only").is_empty());
	const String base_script_path = "res://tests/editor/mcp/data/mcp_node_base_script.gd";
	const Dictionary base_property = _find_property(properties, "exported_base");
	REQUIRE_FALSE(base_property.is_empty());
	CHECK(base_property.get("source", String()) == "script");
	CHECK(base_property.get("scriptPath", String()) == base_script_path);

	const Array property_layout = content.get("propertyLayout", Array());
	const Dictionary custom_category = _find_layout_entry(property_layout, "category", "MCP Test");
	REQUIRE_FALSE(custom_category.is_empty());
	CHECK(custom_category.get("source", String()) == "script");
	CHECK_FALSE(_find_layout_entry(property_layout, "group", "Values").is_empty());
	CHECK_FALSE(_find_layout_entry(property_layout, "subgroup", "References").is_empty());
	const Dictionary base_category = _find_layout_entry(property_layout, "category", "mcp_node_base_script.gd");
	REQUIRE_FALSE(base_category.is_empty());
	CHECK(base_category.get("scriptPath", String()) == base_script_path);

	Node *in_memory_node = memnew(Node);
	in_memory_node->set_name("InMemory");
	fixture.scene_root->add_child(in_memory_node);
	in_memory_node->set_owner(fixture.scene_root);
	Ref<GDScript> in_memory_script;
	in_memory_script.instantiate();
	in_memory_script->set_source_code("extends Node\n@export_group(\"Memory\")\n@export var in_memory_value: int = 7\n");
	REQUIRE(in_memory_script->reload() == OK);
	in_memory_node->set_script(in_memory_script);
	REQUIRE(in_memory_node->get_script_instance() != nullptr);

	Dictionary in_memory_arguments;
	in_memory_arguments["path"] = "InMemory";
	const MCPToolRegistry::CallResult in_memory_result = _call_tool(fixture.registry, "godot.node.get_properties", in_memory_arguments);
	if (!_check_tool_success(in_memory_result)) {
		return;
	}
	const Dictionary in_memory_content = in_memory_result.result.get("structuredContent", Dictionary());
	CHECK_FALSE(in_memory_content.has("scriptPath"));
	CHECK(int(in_memory_content.get("scriptPropertyCount", 0)) == 1);
	const Dictionary in_memory_property = _find_property(in_memory_content.get("properties", Array()), "in_memory_value");
	REQUIRE_FALSE(in_memory_property.is_empty());
	CHECK(in_memory_property.get("source", String()) == "script");
	CHECK(bool(in_memory_property.get("scriptExposed", false)));
	CHECK_FALSE(in_memory_property.has("scriptPath"));
	CHECK(in_memory_property.get("value", Variant()) == Variant(7));
	const Dictionary memory_group = _find_layout_entry(in_memory_content.get("propertyLayout", Array()), "group", "Memory");
	REQUIRE_FALSE(memory_group.is_empty());
	CHECK(memory_group.get("source", String()) == "script");
	CHECK_FALSE(memory_group.has("scriptPath"));
}
#endif

} // namespace TestMCPNodeProvider
