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
#include "core/object/undo_redo.h"
#include "core/variant/callable.h"
#include "editor/mcp/providers/mcp_node_provider.h"
#include "editor/mcp/providers/mcp_undo_redo_action.h"
#include "editor/mcp/providers/mcp_variant_codec.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/gdscript.h"
#endif

TEST_FORCE_LINK(test_mcp_node_provider);

namespace TestMCPNodeProvider {

class TestUndoRedoAction : public MCPUndoRedoAction {
public:
	TestUndoRedoAction() {
		undo_redo = memnew(UndoRedo);
		saved_version = undo_redo->get_version();
	}

	~TestUndoRedoAction() override {
		memdelete(undo_redo);
	}

	Error begin_action(const String &p_name, String *r_error) override {
		if (r_error) {
			*r_error = String();
		}
		undo_redo->create_action(p_name);
		return OK;
	}

	void add_do_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) override {
		undo_redo->add_do_method(Callable(p_object, p_method).bindp(p_args, p_argcount));
	}

	void add_undo_methodp(Object *p_object, const StringName &p_method, const Variant **p_args, int p_argcount) override {
		undo_redo->add_undo_method(Callable(p_object, p_method).bindp(p_args, p_argcount));
	}

	void add_do_property(Object *p_object, const StringName &p_property, const Variant &p_value) override {
		undo_redo->add_do_property(p_object, p_property, p_value);
	}

	void add_undo_property(Object *p_object, const StringName &p_property, const Variant &p_value) override {
		undo_redo->add_undo_property(p_object, p_property, p_value);
	}

	void add_do_reference(Object *p_object) override {
		undo_redo->add_do_reference(p_object);
	}

	void commit_action() override {
		undo_redo->commit_action();
	}

	bool undo() {
		return undo_redo->undo();
	}

	bool redo() {
		return undo_redo->redo();
	}

	bool is_unsaved() const {
		return undo_redo->get_version() != saved_version;
	}

	void clear_history() {
		undo_redo->clear_history(false);
		saved_version = undo_redo->get_version();
	}

private:
	UndoRedo *undo_redo = nullptr;
	uint64_t saved_version = 0;
};

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
	ScopedGDScriptLanguage() {
		language = GDScriptLanguage::get_singleton();
		previous_scripting_enabled = ScriptServer::is_scripting_enabled();
		if (language && !language->has_any_global_constant(SNAME("PI"))) {
			language->init();
			initialized_here = true;
		}
		ScriptServer::set_scripting_enabled(true);
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
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	return p_registry.call_tool(p_name, p_arguments, context);
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

TEST_CASE("[MCP][Provider] Node tools expose encoded property and undoable mutation schemas") {
	MCPToolRegistry registry;
	MCPNodeProvider *provider = memnew(MCPNodeProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);

	const PackedStringArray names = registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(names.size() == 4);
	CHECK(names[0] == "godot.node.get_properties");
	CHECK(names[1] == "godot.node.create");
	CHECK(names[2] == "godot.node.set_property");
	CHECK(names[3] == "godot.node.attach_script");
	CHECK(registry.get_tool_names(MCPToolRegistry::TOOL_SURFACE_CLI).is_empty());

	const Array definitions = registry.get_tool_definitions(MCPToolRegistry::TOOL_SURFACE_MCP);
	REQUIRE(definitions.size() == 4);
	const Dictionary set_schema = Dictionary(definitions[2]).get("inputSchema", Dictionary());
	const PackedStringArray required = set_schema.get("required", PackedStringArray());
	CHECK(required.has("path"));
	CHECK(required.has("property"));
	CHECK(required.has("value"));

	MCPToolCallContext context;
	context.surface = MCPToolRegistry::TOOL_SURFACE_MCP;
	const MCPToolRegistry::CallResult invalid_call = registry.call_tool("godot.node.create", Dictionary(), context);
	REQUIRE(invalid_call.status == MCPToolRegistry::CALL_OK);
	CHECK(bool(invalid_call.result.get("isError", false)));

	provider->unregister_tools();
	CHECK(registry.get_tool_names().is_empty());
	memdelete(provider);
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
#endif

} // namespace TestMCPNodeProvider
