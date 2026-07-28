/**************************************************************************/
/*  test_mcp_editor_feature.cpp                                           */
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

#include "core/mcp/mcp_tool_registry.h"
#include "editor/mcp/mcp_editor_feature.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_mcp_editor_feature);

namespace TestMCPEditorFeature {

class RecordingFeature : public MCPEditorFeature {
	String name;
	Vector<String> *events = nullptr;
	Error registration_error = OK;

	void _record(const String &p_event) {
		events->push_back(p_event + ":" + name);
	}

public:
	RecordingFeature(const String &p_name, Vector<String> *p_events, Error p_registration_error = OK) :
			name(p_name), events(p_events), registration_error(p_registration_error) {}

	void set_registration_error(Error p_error) { registration_error = p_error; }

	Error register_tools(MCPToolRegistry *, String *r_error = nullptr) override {
		_record("register");
		if (registration_error != OK && r_error) {
			*r_error = "Registration failed for " + name;
		}
		return registration_error;
	}

	void unregister_tools() override { _record("unregister"); }
	void process() override { _record("process"); }
	void on_session_removed(const String &p_session_id) override { _record("session-" + p_session_id); }
	void shutdown() override { _record("shutdown"); }
};

TEST_CASE("[MCP][EditorFeature] Feature set preserves forward and reverse lifecycle order") {
	Vector<String> events;
	RecordingFeature first("first", &events);
	RecordingFeature second("second", &events);
	RecordingFeature third("third", &events);
	MCPEditorFeatureSet feature_set;
	MCPToolRegistry registry;

	REQUIRE(feature_set.add_feature(&first) == OK);
	REQUIRE(feature_set.add_feature(&second) == OK);
	REQUIRE(feature_set.add_feature(&third) == OK);
	REQUIRE(feature_set.register_tools(&registry) == OK);
	CHECK(feature_set.get_feature_count() == 3);
	CHECK(feature_set.get_registered_count() == 3);
	ERR_PRINT_OFF;
	CHECK(feature_set.register_tools(&registry) == ERR_ALREADY_IN_USE);
	ERR_PRINT_ON;
	CHECK(feature_set.get_registered_count() == 3);

	feature_set.process();
	feature_set.on_session_removed("session-a");
	feature_set.unregister_tools();
	feature_set.shutdown();

	const Vector<String> expected{
		"register:first", "register:second", "register:third",
		"process:first", "process:second", "process:third",
		"session-session-a:third", "session-session-a:second", "session-session-a:first",
		"unregister:third", "unregister:second", "unregister:first",
		"shutdown:third", "shutdown:second", "shutdown:first"
	};
	CHECK(events == expected);
	CHECK(feature_set.get_registered_count() == 0);
}

TEST_CASE("[MCP][EditorFeature] Feature set rolls back partial registration and supports retry") {
	Vector<String> events;
	RecordingFeature first("first", &events);
	RecordingFeature failing("failing", &events, ERR_UNAVAILABLE);
	RecordingFeature last("last", &events);
	MCPEditorFeatureSet feature_set;
	MCPToolRegistry registry;
	String error;

	REQUIRE(feature_set.add_feature(&first) == OK);
	REQUIRE(feature_set.add_feature(&failing) == OK);
	REQUIRE(feature_set.add_feature(&last) == OK);
	CHECK(feature_set.register_tools(&registry, &error) == ERR_UNAVAILABLE);
	CHECK(error == "Registration failed for failing");
	CHECK(feature_set.get_registered_count() == 0);
	CHECK(events == Vector<String>{ "register:first", "register:failing", "unregister:failing", "unregister:first" });
	feature_set.shutdown();
	CHECK(events == Vector<String>{ "register:first", "register:failing", "unregister:failing", "unregister:first" });

	events.clear();
	error = String();
	failing.set_registration_error(OK);
	CHECK(feature_set.register_tools(&registry, &error) == OK);
	CHECK(feature_set.get_registered_count() == 3);
	feature_set.unregister_tools();
	feature_set.shutdown();
	CHECK(events == Vector<String>{ "register:first", "register:failing", "register:last", "unregister:last", "unregister:failing", "unregister:first", "shutdown:last", "shutdown:failing", "shutdown:first" });
}

TEST_CASE("[MCP][EditorFeature] Feature set shutdown is idempotent per registration epoch") {
	Vector<String> events;
	RecordingFeature first("first", &events);
	RecordingFeature second("second", &events);
	MCPEditorFeatureSet feature_set;
	MCPToolRegistry registry;

	REQUIRE(feature_set.add_feature(&first) == OK);
	REQUIRE(feature_set.add_feature(&second) == OK);
	REQUIRE(feature_set.register_tools(&registry) == OK);
	feature_set.unregister_tools();
	feature_set.process();
	feature_set.on_session_removed("closed");
	ERR_PRINT_OFF;
	CHECK(feature_set.register_tools(&registry) == ERR_ALREADY_IN_USE);
	ERR_PRINT_ON;
	feature_set.shutdown();
	feature_set.shutdown();

	CHECK(events == Vector<String>{ "register:first", "register:second", "unregister:second", "unregister:first", "shutdown:second", "shutdown:first" });

	events.clear();
	REQUIRE(feature_set.register_tools(&registry) == OK);
	feature_set.shutdown();
	feature_set.process();
	feature_set.on_session_removed("closed");
	feature_set.shutdown();
	feature_set.unregister_tools();

	CHECK(events == Vector<String>{ "register:first", "register:second", "shutdown:second", "shutdown:first", "unregister:second", "unregister:first" });
}

TEST_CASE("[MCP][EditorFeature] Feature set clear completes lifecycle without owning features") {
	Vector<String> events;
	RecordingFeature first("first", &events);
	RecordingFeature second("second", &events);
	MCPEditorFeatureSet feature_set;
	MCPToolRegistry registry;

	REQUIRE(feature_set.add_feature(&first) == OK);
	REQUIRE(feature_set.add_feature(&second) == OK);
	REQUIRE(feature_set.register_tools(&registry) == OK);
	feature_set.clear();
	feature_set.clear();

	CHECK(feature_set.get_feature_count() == 0);
	CHECK(feature_set.get_registered_count() == 0);
	CHECK(events == Vector<String>{ "register:first", "register:second", "unregister:second", "unregister:first", "shutdown:second", "shutdown:first" });

	REQUIRE(feature_set.add_feature(&first) == OK);
	REQUIRE(feature_set.register_tools(&registry) == OK);
	feature_set.clear();
	CHECK(events == Vector<String>{ "register:first", "register:second", "unregister:second", "unregister:first", "shutdown:second", "shutdown:first", "register:first", "unregister:first", "shutdown:first" });
}

} // namespace TestMCPEditorFeature
