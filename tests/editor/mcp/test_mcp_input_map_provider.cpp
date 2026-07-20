/**************************************************************************/
/*  test_mcp_input_map_provider.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
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
#include "core/input/input_map.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/os/os.h"
#include "editor/mcp/providers/mcp_input_map_provider.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_input_map_provider);

namespace TestMCPInputMapProvider {

class ScopedActionRestore {
	String setting_name;
	Variant value;
	int order = -1;
	bool existed = false;

public:
	explicit ScopedActionRestore(const String &p_action) :
			setting_name("input/" + p_action) {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		existed = settings->has_setting(setting_name);
		if (existed) {
			value = settings->get_setting(setting_name);
			order = settings->get_order(setting_name);
		}
	}

	~ScopedActionRestore() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		if (settings->has_setting(setting_name)) {
			settings->clear(setting_name);
		}
		if (existed) {
			settings->set_setting(setting_name, value);
			settings->set_order(setting_name, order);
		}
		InputMap::get_singleton()->load_from_project_settings();
	}
};

TEST_CASE("[MCP][Provider][SceneTree] Input Map actions use semantic events and update the live InputMap") {
	MCPToolRegistry registry;
	MCPInputMapProvider *provider = memnew(MCPInputMapProvider);
	REQUIRE(provider->register_tools(&registry) == OK);
	CHECK(provider->register_tools(&registry) == ERR_ALREADY_IN_USE);
	const PackedStringArray names = registry.get_tool_names();
	REQUIRE(names.size() == 3);
	CHECK(names[0] == "godot.input.get_actions");
	CHECK(names[1] == "godot.input.set_action");
	CHECK(names[2] == "godot.input.remove_action");

	const String action_name = "mcp_action_" + String::num_int64(OS::get_singleton()->get_process_id()) + "_" + String::num_uint64(OS::get_singleton()->get_ticks_usec());
	ScopedActionRestore restore(action_name);
	MCPToolCallContext context;

	Dictionary key_event;
	key_event["type"] = "key";
	key_event["physicalKeycode"] = int64_t(Key::A);
	key_event["keyLabel"] = int64_t(Key::A);
	key_event["ctrl"] = true;
	Array events;
	events.push_back(key_event);
	Dictionary set_arguments;
	set_arguments["name"] = action_name;
	set_arguments["deadzone"] = 0.25;
	set_arguments["events"] = events;
	MCPToolRegistry::CallResult call_result = registry.call_tool("godot.input.set_action", set_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	CHECK(ProjectSettings::get_singleton()->has_setting("input/" + action_name));
	REQUIRE(InputMap::get_singleton()->has_action(action_name));
	CHECK(InputMap::get_singleton()->action_get_deadzone(action_name) == doctest::Approx(0.25));
	CHECK(InputMap::get_singleton()->action_get_events(action_name)->size() == 1);
	const Dictionary set_result = call_result.result.get("structuredContent", Dictionary());
	CHECK(int(set_result.get("eventCount", 0)) == 1);
	CHECK_FALSE(bool(set_result.get("saved", true)));

	Dictionary get_arguments;
	get_arguments["prefix"] = action_name;
	call_result = registry.call_tool("godot.input.get_actions", get_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	const Array actions = Dictionary(call_result.result.get("structuredContent", Dictionary())).get("actions", Array());
	REQUIRE(actions.size() == 1);
	const Array returned_events = Dictionary(actions[0]).get("events", Array());
	REQUIRE(returned_events.size() == 1);
	const Dictionary returned_event = returned_events[0];
	CHECK(returned_event.get("type", String()) == "key");
	CHECK(int64_t(returned_event.get("physicalKeycode", 0)) == int64_t(Key::A));
	CHECK(int64_t(returned_event.get("keyLabel", 0)) == int64_t(Key::A));
	CHECK(bool(returned_event.get("ctrl", false)));

	set_arguments["events"] = returned_events;
	call_result = registry.call_tool("godot.input.set_action", set_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));

	Dictionary remove_arguments;
	remove_arguments["name"] = action_name;
	call_result = registry.call_tool("godot.input.remove_action", remove_arguments, context);
	REQUIRE_FALSE(bool(call_result.result.get("isError", false)));
	CHECK_FALSE(ProjectSettings::get_singleton()->has_setting("input/" + action_name));
	CHECK_FALSE(InputMap::get_singleton()->has_action(action_name));

	provider->unregister_tools();
	memdelete(provider);
}

} // namespace TestMCPInputMapProvider
