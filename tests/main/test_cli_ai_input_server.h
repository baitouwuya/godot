/**************************************************************************/
/*  test_cli_ai_input_server.h                                            */
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

#pragma once

#include "main/cli_ai_input_server.h"
#include "main/custom_feature_tracer.h"

#ifdef TOOLS_ENABLED
#include "editor/run/editor_run_bar.h"
#endif

#include "core/object/class_db.h"
#include "core/object/message_queue.h"
#include "core/object/object.h"
#include "core/io/json.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#include "tests/main/test_custom_feature_trace_helpers.h"
#include "tests/test_utils.h"

#include "thirdparty/doctest/doctest.h"

class CLIAIInputSignalProbe : public Object {
	GDCLASS(CLIAIInputSignalProbe, Object);

protected:
	static void _bind_methods() {}

public:
	int button_pressed_count = 0;
	int line_edit_focus_entered_count = 0;
	int button_gui_input_count = 0;
	int panel_visibility_changed_count = 0;
	Control *menu_root = nullptr;
	Control *loading_panel = nullptr;

	void on_button_pressed() {
		button_pressed_count++;
	}

	void on_line_edit_focus_entered() {
		line_edit_focus_entered_count++;
	}

	void on_button_gui_input(const Ref<InputEvent> &p_event) {
		if (p_event.is_valid()) {
			button_gui_input_count++;
		}
	}

	void on_panel_visibility_changed() {
		panel_visibility_changed_count++;
	}

	void on_play_button_pressed() {
		button_pressed_count++;
		if (menu_root) {
			menu_root->hide();
		}
		if (loading_panel) {
			loading_panel->show();
		}
	}
};

class CLIAIInputGameplayProbe : public Node {
	GDCLASS(CLIAIInputGameplayProbe, Node);

protected:
	static void _bind_methods() {}

public:
	int mouse_motion_event_count = 0;
	Vector2 last_mouse_screen_relative;
	float move_right_strength = 0.0f;
	bool saw_aim_pressed = false;
	bool saw_aim_just_pressed = false;
	bool saw_aim_just_released = false;
	bool saw_shoot_pressed = false;
	bool saw_move_right_pressed = false;
	bool saw_jump_just_pressed = false;

	virtual void input(const Ref<InputEvent> &p_event) override {
		Ref<InputEventMouseMotion> mm = p_event;
		if (mm.is_valid()) {
			mouse_motion_event_count++;
			last_mouse_screen_relative = mm->get_relative_screen_position();
		}
	}

	void sample_actions() {
		move_right_strength = Input::get_singleton()->get_action_strength("move_right");
		saw_aim_pressed = Input::get_singleton()->is_action_pressed("aim");
		saw_aim_just_pressed = Input::get_singleton()->is_action_just_pressed("aim");
		saw_aim_just_released = Input::get_singleton()->is_action_just_released("aim");
		saw_shoot_pressed = Input::get_singleton()->is_action_pressed("shoot");
		saw_move_right_pressed = Input::get_singleton()->is_action_pressed("move_right");
		saw_jump_just_pressed = Input::get_singleton()->is_action_just_pressed("jump");
	}

	void clear_samples() {
		mouse_motion_event_count = 0;
		last_mouse_screen_relative = Vector2();
		move_right_strength = 0.0f;
		saw_aim_pressed = false;
		saw_aim_just_pressed = false;
		saw_aim_just_released = false;
		saw_shoot_pressed = false;
		saw_move_right_pressed = false;
		saw_jump_just_pressed = false;
	}
};

namespace TestCLIAIInputServer {

struct ParsedOptionsResult {
	CLIAIInputServer::Options options;
	String error;
};

static ParsedOptionsResult parse_cli_options(const std::initializer_list<const char *> &p_args) {
	List<String> args;
	for (const char *arg : p_args) {
		args.push_back(String(arg));
	}

	ParsedOptionsResult result;
	for (List<String>::Element *E = args.front(); E;) {
		List<String>::Element *N = E->next();
		String error;
		CLIAIInputServer::parse_argument(E->get(), N, result.options, error);
		if (!error.is_empty()) {
			result.error = error;
			break;
		}
		E = N;
	}

	return result;
}

static Dictionary find_snapshot_by_name(const Array &p_nodes, const String &p_name) {
	for (int i = 0; i < p_nodes.size(); i++) {
		if (p_nodes[i].get_type() == Variant::DICTIONARY) {
			Dictionary node = p_nodes[i];
			if (node.has("name") && String(node["name"]) == p_name) {
				return node;
			}
		}
	}
	return Dictionary();
}

static Vector2 rect_center_from_snapshot(const Dictionary &p_snapshot) {
	Dictionary rect = p_snapshot["screenRect"];
	return Vector2((double)rect["x"] + ((double)rect["width"] * 0.5), (double)rect["y"] + ((double)rect["height"] * 0.5));
}

static Dictionary find_event_response_payload(const Vector<Dictionary> &p_events, const String &p_session_dir, const String &p_event_name) {
	const Dictionary event = TestCustomFeatureTraceHelpers::find_event(p_events, CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, p_event_name);
	if (event.is_empty()) {
		return Dictionary();
	}
	const Dictionary payloads = event["payloads"];
	if (!payloads.has("response")) {
		return Dictionary();
	}
	const Variant parsed = JSON::parse_string(TestCustomFeatureTraceHelpers::read_payload_text(p_session_dir, payloads["response"]));
	return parsed.get_type() == Variant::DICTIONARY ? (Dictionary)parsed : Dictionary();
}

static void drain_server_batch(CLIAIInputServer &p_server) {
	int guard = 0;
	while ((p_server.is_batch_active() || p_server.test_has_pending_waits()) && guard++ < 32) {
		p_server.test_process_batch();
		if (p_server.test_has_pending_waits()) {
			p_server.test_force_all_pending_waits_to_now();
			p_server.test_process_pending_waits();
		}
	}
}

struct TestSceneContext {
	Node *fixture_root = nullptr;
	Button *button = nullptr;
	LineEdit *line_edit = nullptr;
	ProgressBar *progress_bar = nullptr;
	Control *menu_root = nullptr;
	Control *loading_panel = nullptr;
	CLIAIInputGameplayProbe *gameplay_probe = nullptr;
	StaticBody3D *node_3d = nullptr;
	Node3D *helper_node_3d = nullptr;
	StaticBody3D *offscreen_node_3d = nullptr;
	Camera3D *camera = nullptr;
	Node *plain_node = nullptr;
	SubViewportContainer *subviewport_container = nullptr;
	SubViewport *subviewport = nullptr;
	Camera3D *subviewport_camera = nullptr;
	StaticBody3D *subviewport_node_3d = nullptr;
	Node3D *subviewport_helper_node_3d = nullptr;
	StaticBody3D *subviewport_offscreen_node_3d = nullptr;
	CLIAIInputSignalProbe *signal_probe = nullptr;
	Window::ContentScaleMode saved_content_scale_mode = Window::CONTENT_SCALE_MODE_DISABLED;
	Window::ContentScaleAspect saved_content_scale_aspect = Window::CONTENT_SCALE_ASPECT_IGNORE;
	real_t saved_content_scale_factor = 1.0;
	Size2i saved_root_size;

	TestSceneContext() {
		Window *root = SceneTree::get_singleton()->get_root();
		saved_content_scale_mode = root->get_content_scale_mode();
		saved_content_scale_aspect = root->get_content_scale_aspect();
		saved_content_scale_factor = root->get_content_scale_factor();
		saved_root_size = root->get_size();
		root->set_content_scale_mode(Window::CONTENT_SCALE_MODE_CANVAS_ITEMS);
		root->set_content_scale_aspect(Window::CONTENT_SCALE_ASPECT_EXPAND);
		root->set_content_scale_factor(1.0);
		root->set_size(Size2i(1920, 1080));

		signal_probe = memnew(CLIAIInputSignalProbe);
		fixture_root = memnew(Node);
		fixture_root->set_name("AITestFixtureRoot");
		root->add_child(fixture_root);

		button = memnew(Button);
		button->set_name("PlayButton");
		button->set_text("Play");
		button->set_position(Vector2(32, 40));
		button->set_size(Vector2(140, 36));
		button->add_to_group("menu");
		button->add_to_group("pick");
		menu_root = memnew(Control);
		menu_root->set_name("MenuRoot");
		menu_root->set_size(Vector2(320, 240));
		fixture_root->add_child(menu_root);
		menu_root->add_child(button);
		loading_panel = memnew(Control);
		loading_panel->set_name("LoadingPanel");
		loading_panel->set_position(Vector2(32, 200));
		loading_panel->set_size(Vector2(180, 32));
		loading_panel->set_visible(false);
		fixture_root->add_child(loading_panel);
		loading_panel->connect("visibility_changed", callable_mp(signal_probe, &CLIAIInputSignalProbe::on_panel_visibility_changed));
		signal_probe->menu_root = menu_root;
		signal_probe->loading_panel = loading_panel;
		button->connect("pressed", callable_mp(signal_probe, &CLIAIInputSignalProbe::on_play_button_pressed));
		button->connect("gui_input", callable_mp(signal_probe, &CLIAIInputSignalProbe::on_button_gui_input));

		line_edit = memnew(LineEdit);
		line_edit->set_name("NameInput");
		line_edit->set_position(Vector2(32, 144));
		line_edit->set_size(Vector2(180, 36));
		line_edit->add_to_group("pick");
		menu_root->add_child(line_edit);
		line_edit->connect("focus_entered", callable_mp(signal_probe, &CLIAIInputSignalProbe::on_line_edit_focus_entered));

		progress_bar = memnew(ProgressBar);
		progress_bar->set_name("SpeedBar");
		progress_bar->set_position(Vector2(32, 96));
		progress_bar->set_size(Vector2(160, 20));
		progress_bar->set_value(5.0);
		menu_root->add_child(progress_bar);

		if (!InputMap::get_singleton()->has_action("move_right")) {
			InputMap::get_singleton()->add_action("move_right");
		}
		Ref<InputEventKey> move_right_key = InputEventKey::create_reference(Key::D, true);
		InputMap::get_singleton()->action_add_event("move_right", move_right_key);

		if (!InputMap::get_singleton()->has_action("jump")) {
			InputMap::get_singleton()->add_action("jump");
		}
		Ref<InputEventKey> jump_key = InputEventKey::create_reference(Key::SPACE, true);
		InputMap::get_singleton()->action_add_event("jump", jump_key);

		if (!InputMap::get_singleton()->has_action("aim")) {
			InputMap::get_singleton()->add_action("aim");
		}
		Ref<InputEventMouseButton> aim_button;
		aim_button.instantiate();
		aim_button->set_button_index(MouseButton::RIGHT);
		InputMap::get_singleton()->action_add_event("aim", aim_button);

		if (!InputMap::get_singleton()->has_action("shoot")) {
			InputMap::get_singleton()->add_action("shoot");
		}
		Ref<InputEventMouseButton> shoot_button;
		shoot_button.instantiate();
		shoot_button->set_button_index(MouseButton::LEFT);
		InputMap::get_singleton()->action_add_event("shoot", shoot_button);

		gameplay_probe = memnew(CLIAIInputGameplayProbe);
		gameplay_probe->set_name("GameplayProbe");
		fixture_root->add_child(gameplay_probe);
		gameplay_probe->set_process_input(true);

		camera = memnew(Camera3D);
		camera->set_name("TestCamera");
		fixture_root->add_child(camera);
		camera->make_current();

		node_3d = memnew(StaticBody3D);
		node_3d->set_name("CubeTarget");
		node_3d->set_position(Vector3(0, 0, -5));
		node_3d->add_to_group("world");
		fixture_root->add_child(node_3d);

		helper_node_3d = memnew(Node3D);
		helper_node_3d->set_name("@Helper3D");
		helper_node_3d->set_position(Vector3(0.1f, 0.0f, -5.0f));
		helper_node_3d->add_to_group("world");
		fixture_root->add_child(helper_node_3d);

		offscreen_node_3d = memnew(StaticBody3D);
		offscreen_node_3d->set_name("OffscreenCube");
		offscreen_node_3d->set_position(Vector3(200.0f, 0.0f, -5.0f));
		offscreen_node_3d->add_to_group("world");
		fixture_root->add_child(offscreen_node_3d);

		plain_node = memnew(Node);
		plain_node->set_name("PlainNode");
		plain_node->add_to_group("pick");
		fixture_root->add_child(plain_node);

		subviewport_container = memnew(SubViewportContainer);
		subviewport_container->set_name("TargetViewportContainer");
		subviewport_container->set_position(Vector2(200, 120));
		subviewport_container->set_size(Vector2(240, 160));
		fixture_root->add_child(subviewport_container);

		subviewport = memnew(SubViewport);
		subviewport->set_name("TargetSubViewport");
		subviewport->set_size(Size2i(240, 160));
		subviewport_container->add_child(subviewport);

		subviewport_camera = memnew(Camera3D);
		subviewport_camera->set_name("SubViewportCamera");
		subviewport->add_child(subviewport_camera);
		subviewport_camera->make_current();

		subviewport_node_3d = memnew(StaticBody3D);
		subviewport_node_3d->set_name("SubViewportCube");
		subviewport_node_3d->set_position(Vector3(0, 0, -4));
		subviewport_node_3d->add_to_group("subview");
		subviewport->add_child(subviewport_node_3d);

		subviewport_helper_node_3d = memnew(Node3D);
		subviewport_helper_node_3d->set_name("@SubViewportHelper");
		subviewport_helper_node_3d->set_position(Vector3(0.1f, 0.0f, -4.0f));
		subviewport_helper_node_3d->add_to_group("subview");
		subviewport->add_child(subviewport_helper_node_3d);

		subviewport_offscreen_node_3d = memnew(StaticBody3D);
		subviewport_offscreen_node_3d->set_name("SubViewportOffscreen");
		subviewport_offscreen_node_3d->set_position(Vector3(200.0f, 0.0f, -4.0f));
		subviewport_offscreen_node_3d->add_to_group("subview");
		subviewport->add_child(subviewport_offscreen_node_3d);
	}

	~TestSceneContext() {
		Window *root = SceneTree::get_singleton() ? SceneTree::get_singleton()->get_root() : nullptr;
		if (root) {
			root->set_content_scale_mode(saved_content_scale_mode);
			root->set_content_scale_aspect(saved_content_scale_aspect);
			root->set_content_scale_factor(saved_content_scale_factor);
			root->set_size(saved_root_size);
		}
		if (fixture_root && fixture_root->get_parent()) {
			fixture_root->get_parent()->remove_child(fixture_root);
		}
		memdelete(fixture_root);
		memdelete(signal_probe);
	}

};

struct RouterSceneContext {
	Node *router_root = nullptr;
	Node *menu_scene = nullptr;
	Node *level_scene = nullptr;

	RouterSceneContext() {
		SceneTree *tree = SceneTree::get_singleton();
		Window *root = tree->get_root();
		router_root = memnew(Node);
		router_root->set_name("RouterRoot");
		router_root->set_scene_file_path("res://router_root.tscn");
		root->add_child(router_root);
		tree->set_current_scene(router_root);

		menu_scene = memnew(Node);
		menu_scene->set_name("Menu");
		menu_scene->set_scene_file_path("res://menu.tscn");
		router_root->add_child(menu_scene);
	}

	~RouterSceneContext() {
		SceneTree *tree = SceneTree::get_singleton();
		if (tree && tree->get_current_scene() == router_root) {
			tree->set_current_scene(nullptr);
		}
		if (router_root && router_root->get_parent()) {
			router_root->get_parent()->remove_child(router_root);
		}
		memdelete(router_root);
	}

	void switch_to_level() {
		if (menu_scene && menu_scene->get_parent()) {
			router_root->remove_child(menu_scene);
			memdelete(menu_scene);
			menu_scene = nullptr;
		}
		if (!level_scene) {
			level_scene = memnew(Node3D);
			level_scene->set_name("Level");
			level_scene->set_scene_file_path("res://level.tscn");
			router_root->add_child(level_scene);
		}
	}
};

TEST_SUITE("[Main][CLIAIInputServer][SceneTree]") {
	TEST_CASE("Parses AI agent CLI arguments") {
		const ParsedOptionsResult parsed = parse_cli_options({ "--ai-agent-control", "--ai-agent-port", "7011", "--ai-agent-max-ops", "32", "--ai-agent-max-line-bytes", "4096" });
		CHECK(parsed.error.is_empty());
		CHECK(parsed.options.enabled);
		CHECK(parsed.options.enabled_set);
		CHECK_EQ(parsed.options.port, 7011);
		CHECK(parsed.options.port_set);
		CHECK_EQ(parsed.options.max_batch_ops, 32);
		CHECK(parsed.options.max_batch_ops_set);
		CHECK_EQ(parsed.options.max_line_bytes, 4096);
		CHECK(parsed.options.max_line_bytes_set);
	}

	TEST_CASE("Validates AI agent options") {
		String error;

		SUBCASE("rejects invalid port") {
			CLIAIInputServer::Options options;
			options.enabled = true;
			options.port = 70000;
			CHECK_EQ(CLIAIInputServer::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--ai-agent-port"));
		}

		SUBCASE("rejects invalid max ops") {
			CLIAIInputServer::Options options;
			options.enabled = true;
			options.max_batch_ops = 0;
			CHECK_EQ(CLIAIInputServer::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("--ai-agent-max-ops"));
		}

		SUBCASE("rejects editor mode when enabled") {
			CLIAIInputServer::Options options;
			options.enabled = true;
			CHECK_EQ(CLIAIInputServer::validate_options(options, true, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("CLI project runs"));
		}

		SUBCASE("allows argument overrides without enabling the server") {
			CLIAIInputServer::Options options;
			options.port = 7012;
			options.port_set = true;
			CHECK_EQ(CLIAIInputServer::validate_options(options, false, false, false, error), OK);
		}

		SUBCASE("rejects invalid wait and query defaults") {
			CLIAIInputServer::Options options;
			options.default_wait_timeout_frames = 0;
			CHECK_EQ(CLIAIInputServer::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("default_wait_timeout_frames"));

			options.default_wait_timeout_frames = 10;
			options.default_poll_every_frames = 0;
			CHECK_EQ(CLIAIInputServer::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("default_poll_every_frames"));

			options.default_poll_every_frames = 1;
			options.default_query_max_results = 0;
			CHECK_EQ(CLIAIInputServer::validate_options(options, false, false, false, error), ERR_INVALID_PARAMETER);
			CHECK(error.contains("default_query_max_results"));
		}
	}

	TEST_CASE("Parses mouse buttons") {
		MouseButton button = MouseButton::NONE;
		CHECK(CLIAIInputServer::parse_mouse_button("left", button));
		CHECK(button == MouseButton::LEFT);
		CHECK(CLIAIInputServer::parse_mouse_button("wheel_down", button));
		CHECK(button == MouseButton::WHEEL_DOWN);
		CHECK(CLIAIInputServer::parse_mouse_button(8, button));
		CHECK(button == MouseButton::MB_XBUTTON1);
		CHECK_FALSE(CLIAIInputServer::parse_mouse_button("invalid", button));
	}

	TEST_CASE("Expands high-level input operations") {
		String error;
		Array expanded;

		SUBCASE("click expands to move, press, wait, release") {
			Dictionary click;
			click["cmd"] = "click";
			click["button"] = "left";
			click["x"] = 10;
			click["y"] = 20;
			CHECK(CLIAIInputServer::expand_operation_for_tests(click, expanded, error));
			CHECK(error.is_empty());
			CHECK_EQ(expanded.size(), 4);
			CHECK_EQ(String(((Dictionary)expanded[0])["cmd"]), String("mouse_motion"));
			CHECK_EQ(String(((Dictionary)expanded[1])["cmd"]), String("mouse_button"));
			CHECK_EQ(String(((Dictionary)expanded[2])["cmd"]), String("wait"));
			CHECK_EQ(String(((Dictionary)expanded[3])["cmd"]), String("mouse_button"));
		}

		SUBCASE("double click marks second press as double click") {
			Dictionary double_click;
			double_click["cmd"] = "double_click";
			double_click["x"] = 10;
			double_click["y"] = 20;
			CHECK(CLIAIInputServer::expand_operation_for_tests(double_click, expanded, error));
			CHECK(error.is_empty());
			CHECK_EQ(expanded.size(), 8);
			CHECK((bool)((Dictionary)expanded[5])["doubleClick"]);
		}

		SUBCASE("drag path expands across requested frame count") {
			Dictionary drag;
			drag["cmd"] = "drag";
			drag["button"] = "left";
			Array path;
			Array p0;
			p0.push_back(0);
			p0.push_back(0);
			Array p1;
			p1.push_back(10);
			p1.push_back(10);
			path.push_back(p0);
			path.push_back(p1);
			drag["path"] = path;
			drag["frames"] = 3;
			CHECK(CLIAIInputServer::expand_operation_for_tests(drag, expanded, error));
			CHECK(error.is_empty());
			CHECK_EQ(expanded.size(), 6);
			CHECK_EQ(String(((Dictionary)expanded[5])["cmd"]), String("mouse_button"));
			CHECK_FALSE((bool)((Dictionary)expanded[5])["pressed"]);
		}

		SUBCASE("invalid hold frame count is rejected") {
			Dictionary hold;
			hold["cmd"] = "hold";
			hold["x"] = 1;
			hold["y"] = 1;
			hold["frames"] = 0;
			CHECK_FALSE(CLIAIInputServer::expand_operation_for_tests(hold, expanded, error));
			CHECK(error.contains("hold.frames"));
		}
	}

	TEST_CASE("Reuses performance recorder for open-ended runtime sessions") {
		CLIPerformanceRecorder::Options options;
		options.enabled = true;
		options.start_frame = 5;
		options.recording_name = "probe";
		options.top_frames = 1;

		CLIPerformanceRecorder recorder(options);
		recorder.record_frame(4, 1000, 100, 50, 25, 0.016);
		recorder.record_frame(5, 1000, 100, 50, 25, 0.016);
		recorder.record_frame(6, 2000, 100, 50, 25, 0.016);
		recorder.set_summary_end_frame(6);

		const Dictionary summary = recorder.build_summary(true);
		const Dictionary recording = summary["recording"];
		CHECK_EQ((int64_t)recording["startFrame"], 5);
		CHECK_EQ((int64_t)recording["endFrame"], 6);
		CHECK_EQ((int64_t)recording["capturedFrames"], 2);
		CHECK_EQ(String(recording["name"]), String("probe"));
		CHECK((bool)recording["completed"]);
		CHECK(summary.has("slowFrames"));
	}

	TEST_CASE("[SceneTree] Observation queries cover UI and 3D targets") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		Dictionary interactables_request;
		interactables_request["cmd"] = "get_interactables";
		const Dictionary interactables_response = server.test_cmd_get_interactables(interactables_request);
		REQUIRE((bool)interactables_response["ok"]);
		const Dictionary interactables_result = interactables_response["result"];
		const Array interactables = interactables_result["nodes"];
		const Dictionary button_snapshot = find_snapshot_by_name(interactables, "PlayButton");
		const Dictionary node3d_snapshot = find_snapshot_by_name(interactables, "CubeTarget");
		const Dictionary subviewport_node_snapshot = find_snapshot_by_name(interactables, "SubViewportCube");

		CHECK_FALSE(button_snapshot.is_empty());
		CHECK_FALSE(node3d_snapshot.is_empty());
		CHECK_FALSE(subviewport_node_snapshot.is_empty());
		CHECK_FALSE((bool)button_snapshot["is3D"]);
		CHECK(button_snapshot["screenRect"].get_type() == Variant::DICTIONARY);
		CHECK_EQ(String(button_snapshot["text"]), String("Play"));
		CHECK((bool)node3d_snapshot["is3D"]);
		CHECK(node3d_snapshot["worldPosition"].get_type() == Variant::ARRAY);
		const bool has_3d_screen_shape = node3d_snapshot["screenRect"].get_type() == Variant::DICTIONARY || node3d_snapshot["screenPoint"].get_type() == Variant::ARRAY;
		CHECK(has_3d_screen_shape);
		CHECK((bool)subviewport_node_snapshot["hasScreenPosition"]);
		CHECK_EQ(String(subviewport_node_snapshot["viewportPath"]), String(scene.subviewport->get_path()));
		CHECK(subviewport_node_snapshot["screenRect"].get_type() == Variant::DICTIONARY);

		Dictionary query_request;
		query_request["cmd"] = "query_nodes";
		Dictionary filter;
		filter["group"] = "menu";
		query_request["filter"] = filter;
		const Dictionary query_response = server.test_cmd_query_nodes(query_request);
		REQUIRE((bool)query_response["ok"]);
		const Dictionary query_result = query_response["result"];
		CHECK_EQ((int)query_result["count"], 1);
		CHECK_EQ(String(((Dictionary)((Array)query_result["nodes"])[0])["name"]), String("PlayButton"));

		filter.clear();
		filter["path"] = button_snapshot["nodePath"];
		query_request["filter"] = filter;
		const Dictionary query_path_response = server.test_cmd_query_nodes(query_request);
		REQUIRE((bool)query_path_response["ok"]);
		CHECK_EQ((int)((Dictionary)query_path_response["result"])["count"], 1);

		filter.clear();
		filter["is3D"] = true;
		query_request["filter"] = filter;
		const Dictionary query_3d_response = server.test_cmd_query_nodes(query_request);
		REQUIRE((bool)query_3d_response["ok"]);
		const Array query_3d_nodes = ((Dictionary)query_3d_response["result"])["nodes"];
		CHECK_FALSE(find_snapshot_by_name(query_3d_nodes, "CubeTarget").is_empty());

		filter.clear();
		filter["type"] = "Button";
		filter["text"] = "Play";
		query_request["filter"] = filter;
		const Dictionary query_text_response = server.test_cmd_query_nodes(query_request);
		REQUIRE((bool)query_text_response["ok"]);
		CHECK_EQ((int)((Dictionary)query_text_response["result"])["count"], 1);

		filter.clear();
		filter["group"] = "subview";
		query_request["filter"] = filter;
		const Dictionary query_subviewport_response = server.test_cmd_query_nodes(query_request);
		REQUIRE((bool)query_subviewport_response["ok"]);
		const Array query_subviewport_nodes = ((Dictionary)query_subviewport_response["result"])["nodes"];
		const Dictionary query_subviewport_node = find_snapshot_by_name(query_subviewport_nodes, "SubViewportCube");
		CHECK_FALSE(query_subviewport_node.is_empty());
		CHECK_EQ(String(query_subviewport_node["viewportPath"]), String(scene.subviewport->get_path()));

		Dictionary snapshot_request;
		snapshot_request["cmd"] = "get_node_snapshot";
		Dictionary selector;
		selector["name"] = "PlainNode";
		snapshot_request["selector"] = selector;
		const Dictionary snapshot_response = server.test_cmd_get_node_snapshot(snapshot_request);
		REQUIRE((bool)snapshot_response["ok"]);
		const Dictionary snapshot = ((Dictionary)snapshot_response["result"])["node"];
		CHECK(snapshot["text"].get_type() == Variant::NIL);
		CHECK(snapshot["value"].get_type() == Variant::NIL);
		CHECK(snapshot["selected"].get_type() == Variant::NIL);
	}

	TEST_CASE("[SceneTree] Selector validation and nearest target resolution are stable") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		Dictionary invalid_query_request;
		invalid_query_request["cmd"] = "query_nodes";
		Dictionary invalid_filter;
		invalid_filter["is3D"] = "yes";
		invalid_query_request["filter"] = invalid_filter;
		const Dictionary invalid_query_response = server.test_cmd_query_nodes(invalid_query_request);
		CHECK_FALSE((bool)invalid_query_response["ok"]);
		CHECK_EQ(String(((Dictionary)invalid_query_response["error"])["code"]), String("invalid_query"));

		Dictionary nearest_snapshot_request;
		nearest_snapshot_request["cmd"] = "get_node_snapshot";
		Dictionary nearest_selector;
		nearest_selector["group"] = "pick";
		Array nearest_point;
		nearest_point.push_back(100);
		nearest_point.push_back(60);
		nearest_selector["nearestToScreenPoint"] = nearest_point;
		nearest_snapshot_request["selector"] = nearest_selector;
		const Dictionary nearest_snapshot_response = server.test_cmd_get_node_snapshot(nearest_snapshot_request);
		REQUIRE((bool)nearest_snapshot_response["ok"]);
		const Dictionary nearest_node = ((Dictionary)nearest_snapshot_response["result"])["node"];
		CHECK_EQ(String(nearest_node["name"]), String("PlayButton"));
		CHECK((bool)nearest_node["hasScreenPosition"]);

		Dictionary path_snapshot_request;
		path_snapshot_request["cmd"] = "get_node_snapshot";
		Dictionary path_selector;
		path_selector["path"] = String(scene.subviewport_node_3d->get_path());
		path_snapshot_request["selector"] = path_selector;
		const Dictionary path_snapshot_response = server.test_cmd_get_node_snapshot(path_snapshot_request);
		REQUIRE((bool)path_snapshot_response["ok"]);
		const Dictionary path_snapshot = ((Dictionary)path_snapshot_response["result"])["node"];
		CHECK_EQ(String(path_snapshot["name"]), String("SubViewportCube"));
	}

	TEST_CASE("[SceneTree] Selector-based actions expand into concrete operations") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		Dictionary selector;
		selector["name"] = "PlayButton";

		Dictionary click_target;
		click_target["cmd"] = "click_target";
		click_target["selector"] = selector;
		Array expanded;
		Dictionary result;
		String error_code;
		String error_message;
		CHECK(server.test_prepare_expanded_operation(click_target, expanded, result, error_code, error_message));
		CHECK(error_code.is_empty());
		CHECK_EQ(String(((Dictionary)result["resolvedTarget"])["name"]), String("PlayButton"));
		CHECK_EQ(String(((Dictionary)expanded[0])["cmd"]), String("mouse_motion"));
		CHECK_EQ(String(((Dictionary)expanded[expanded.size() - 1])["cmd"]), String("mouse_button"));

		Dictionary type_text;
		type_text["cmd"] = "type_text";
		type_text["selector"] = selector;
		type_text["text"] = "hello";
		expanded.clear();
		result.clear();
		CHECK(server.test_prepare_expanded_operation(type_text, expanded, result, error_code, error_message));
		CHECK(error_code.is_empty());
		CHECK_EQ((int)result["textLength"], 5);
		CHECK_EQ(String(((Dictionary)expanded[expanded.size() - 1])["cmd"]), String("type_text_commit"));

		Dictionary drag_target;
		drag_target["cmd"] = "drag_target_to_target";
		Dictionary from_selector;
		from_selector["name"] = "PlayButton";
		Dictionary to_selector;
		to_selector["name"] = "CubeTarget";
		drag_target["fromSelector"] = from_selector;
		drag_target["toSelector"] = to_selector;
		drag_target["frames"] = 5;
		expanded.clear();
		result.clear();
		CHECK(server.test_prepare_expanded_operation(drag_target, expanded, result, error_code, error_message));
		CHECK(error_code.is_empty());
		CHECK_EQ(String(((Dictionary)result["resolvedFromTarget"])["name"]), String("PlayButton"));
		CHECK_EQ(String(((Dictionary)result["resolvedToTarget"])["name"]), String("CubeTarget"));
		CHECK_EQ(String(((Dictionary)expanded[0])["cmd"]), String("mouse_motion"));
	}

	TEST_CASE("[SceneTree] Direct target actions drive real GUI focus and text input") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":1,\"cmd\":\"click_target\",\"selector\":{\"name\":\"PlayButton\"}}");
		drain_server_batch(server);
		CHECK_EQ(scene.signal_probe->button_pressed_count, 1);
		REQUIRE_EQ(server.test_sent_response_count(), 1);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK(server.test_sent_response(0).has("sceneDigest"));

		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":2,\"cmd\":\"focus_target\",\"selector\":{\"name\":\"NameInput\"}}");
		drain_server_batch(server);
		REQUIRE_EQ(server.test_sent_response_count(), 1);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK(scene.line_edit->has_focus());
		CHECK(scene.signal_probe->line_edit_focus_entered_count >= 1);

		scene.line_edit->release_focus();
		scene.line_edit->set_focus_mode(Control::FOCUS_NONE);
		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":22,\"cmd\":\"focus_target\",\"selector\":{\"name\":\"NameInput\"}}");
		drain_server_batch(server);
		REQUIRE_EQ(server.test_sent_response_count(), 1);
		CHECK_FALSE((bool)server.test_sent_response(0)["ok"]);
		CHECK_EQ(String(((Dictionary)server.test_sent_response(0)["error"])["code"]), String("target_not_focusable"));
		CHECK_FALSE(scene.line_edit->has_focus());
		scene.line_edit->set_focus_mode(Control::FOCUS_ALL);

		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":3,\"cmd\":\"type_text\",\"selector\":{\"name\":\"NameInput\"},\"text\":\"hello\"}");
		drain_server_batch(server);
		CHECK_EQ(scene.line_edit->get_text(), String("hello"));
		REQUIRE_EQ(server.test_sent_response_count(), 1);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK(server.test_sent_response(0).has("sceneDigest"));
	}

	TEST_CASE("[SceneTree] Fullscreen-style GUI inputs trigger focused buttons through real events") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		Dictionary snapshot_request;
		snapshot_request["cmd"] = "get_node_snapshot";
		Dictionary selector;
		selector["name"] = "PlayButton";
		snapshot_request["selector"] = selector;
		const Dictionary snapshot_response = server.test_cmd_get_node_snapshot(snapshot_request);
		REQUIRE((bool)snapshot_response["ok"]);
		const Dictionary button_snapshot = ((Dictionary)snapshot_response["result"])["node"];
		const Vector2 click_position = rect_center_from_snapshot(button_snapshot);

		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":29,\"cmd\":\"focus_target\",\"selector\":{\"name\":\"PlayButton\"}}");
		drain_server_batch(server);
		REQUIRE_EQ(server.test_sent_response_count(), 1);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK(scene.button->has_focus());

		scene.signal_probe->button_pressed_count = 0;
		scene.signal_probe->button_gui_input_count = 0;
		scene.signal_probe->panel_visibility_changed_count = 0;
		scene.menu_root->show();
		scene.loading_panel->hide();
		server.test_clear_sent_responses();
		server.test_process_line(vformat("{\"id\":30,\"cmd\":\"mouse_motion\",\"x\":%.6f,\"y\":%.6f}", click_position.x, click_position.y));
		server.test_process_line(vformat("{\"id\":31,\"cmd\":\"mouse_button\",\"button\":\"left\",\"pressed\":true,\"x\":%.6f,\"y\":%.6f}", click_position.x, click_position.y));
		server.test_process_line(vformat("{\"id\":32,\"cmd\":\"mouse_button\",\"button\":\"left\",\"pressed\":false,\"x\":%.6f,\"y\":%.6f}", click_position.x, click_position.y));
		CHECK_EQ(scene.signal_probe->button_pressed_count, 1);
		CHECK_FALSE(scene.menu_root->is_visible_in_tree());
		CHECK(scene.loading_panel->is_visible_in_tree());
		CHECK(scene.signal_probe->panel_visibility_changed_count >= 1);
		REQUIRE_EQ(server.test_sent_response_count(), 3);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK((bool)server.test_sent_response(1)["ok"]);
		CHECK((bool)server.test_sent_response(2)["ok"]);

		server.test_clear_sent_responses();
		scene.signal_probe->button_pressed_count = 0;
		scene.signal_probe->button_gui_input_count = 0;
		scene.signal_probe->panel_visibility_changed_count = 0;
		scene.menu_root->show();
		scene.loading_panel->hide();
		server.test_process_line("{\"id\":330,\"cmd\":\"focus_target\",\"selector\":{\"name\":\"PlayButton\"}}");
		drain_server_batch(server);
		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":33,\"cmd\":\"key\",\"keycode\":\"Enter\",\"pressed\":true}");
		server.test_process_line("{\"id\":34,\"cmd\":\"key\",\"keycode\":\"Enter\",\"pressed\":false}");
		REQUIRE_EQ(server.test_sent_response_count(), 2);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK((bool)server.test_sent_response(1)["ok"]);
		CHECK_EQ(scene.signal_probe->button_pressed_count, 1);
		CHECK(scene.signal_probe->button_gui_input_count >= 1);
		CHECK_FALSE(scene.menu_root->is_visible_in_tree());
		CHECK(scene.loading_panel->is_visible_in_tree());
		CHECK(scene.signal_probe->panel_visibility_changed_count >= 1);

		server.test_clear_sent_responses();
		scene.signal_probe->button_pressed_count = 0;
		scene.signal_probe->button_gui_input_count = 0;
		scene.signal_probe->panel_visibility_changed_count = 0;
		scene.menu_root->show();
		scene.loading_panel->hide();
		server.test_process_line("{\"id\":350,\"cmd\":\"focus_target\",\"selector\":{\"name\":\"PlayButton\"}}");
		drain_server_batch(server);
		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":35,\"cmd\":\"key\",\"keycode\":\"Space\",\"pressed\":true}");
		server.test_process_line("{\"id\":36,\"cmd\":\"key\",\"keycode\":\"Space\",\"pressed\":false}");
		REQUIRE_EQ(server.test_sent_response_count(), 2);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK((bool)server.test_sent_response(1)["ok"]);
		CHECK_EQ(scene.signal_probe->button_pressed_count, 1);
		CHECK(scene.signal_probe->button_gui_input_count >= 1);
		CHECK_FALSE(scene.menu_root->is_visible_in_tree());
		CHECK(scene.loading_panel->is_visible_in_tree());
		CHECK(scene.signal_probe->panel_visibility_changed_count >= 1);

		scene.signal_probe->button_pressed_count = 0;
		scene.signal_probe->button_gui_input_count = 0;
		scene.signal_probe->panel_visibility_changed_count = 0;
		scene.menu_root->show();
		scene.loading_panel->hide();
		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":370,\"cmd\":\"focus_target\",\"selector\":{\"name\":\"PlayButton\"}}");
		drain_server_batch(server);
		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":37,\"cmd\":\"action\",\"action\":\"ui_accept\",\"pressed\":true}");
		server.test_process_line("{\"id\":38,\"cmd\":\"action\",\"action\":\"ui_accept\",\"pressed\":false}");
		CHECK_EQ(scene.signal_probe->button_pressed_count, 1);
		CHECK(scene.signal_probe->button_gui_input_count >= 1);
		CHECK_FALSE(scene.menu_root->is_visible_in_tree());
		CHECK(scene.loading_panel->is_visible_in_tree());
		CHECK(scene.signal_probe->panel_visibility_changed_count >= 1);
		CHECK_FALSE(Input::get_singleton()->is_action_pressed("ui_accept"));
		REQUIRE_EQ(server.test_sent_response_count(), 2);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK((bool)server.test_sent_response(1)["ok"]);
	}

	TEST_CASE("[SceneTree] Action commands preserve gameplay-style Input state while dispatching GUI events") {
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":39,\"cmd\":\"action\",\"action\":\"ui_right\",\"pressed\":true,\"strength\":1.0}");
		REQUIRE_EQ(server.test_sent_response_count(), 1);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK(Input::get_singleton()->is_action_pressed("ui_right"));
		CHECK_EQ(Input::get_singleton()->get_action_strength("ui_right"), doctest::Approx(1.0f));

		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":40,\"cmd\":\"action\",\"action\":\"ui_right\",\"pressed\":false,\"strength\":0.0}");
		REQUIRE_EQ(server.test_sent_response_count(), 1);
		CHECK((bool)server.test_sent_response(0)["ok"]);
		CHECK_FALSE(Input::get_singleton()->is_action_pressed("ui_right"));
		CHECK_EQ(Input::get_singleton()->get_action_strength("ui_right"), doctest::Approx(0.0f));
	}

	TEST_CASE("[SceneTree] Key and mouse commands preserve gameplay input semantics") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		scene.gameplay_probe->clear_samples();
		server.test_clear_sent_responses();

		server.test_process_line("{\"id\":41,\"cmd\":\"key\",\"keycode\":\"D\",\"pressed\":true}");
		scene.gameplay_probe->sample_actions();
		CHECK(scene.gameplay_probe->saw_move_right_pressed);
		CHECK_EQ(scene.gameplay_probe->move_right_strength, doctest::Approx(1.0f));

		server.test_process_line("{\"id\":42,\"cmd\":\"key\",\"keycode\":\"D\",\"pressed\":false}");
		scene.gameplay_probe->sample_actions();
		CHECK_FALSE(scene.gameplay_probe->saw_move_right_pressed);
		CHECK_EQ(scene.gameplay_probe->move_right_strength, doctest::Approx(0.0f));

		scene.gameplay_probe->clear_samples();
		server.test_process_line("{\"id\":43,\"cmd\":\"key\",\"keycode\":\"Space\",\"pressed\":true}");
		scene.gameplay_probe->sample_actions();
		CHECK(scene.gameplay_probe->saw_jump_just_pressed);
		server.test_process_line("{\"id\":44,\"cmd\":\"key\",\"keycode\":\"Space\",\"pressed\":false}");

		scene.gameplay_probe->clear_samples();
		server.test_process_line("{\"id\":45,\"cmd\":\"mouse_button\",\"button\":\"right\",\"pressed\":true,\"x\":100,\"y\":100}");
		scene.gameplay_probe->sample_actions();
		CHECK(scene.gameplay_probe->saw_aim_pressed);
		CHECK(scene.gameplay_probe->saw_aim_just_pressed);
		CHECK_FALSE(scene.gameplay_probe->saw_shoot_pressed);

		server.test_process_line("{\"id\":46,\"cmd\":\"mouse_button\",\"button\":\"right\",\"pressed\":false,\"x\":100,\"y\":100}");
		scene.gameplay_probe->sample_actions();
		CHECK_FALSE(scene.gameplay_probe->saw_aim_pressed);
		CHECK(scene.gameplay_probe->saw_aim_just_released);

		server.test_process_line("{\"id\":47,\"cmd\":\"mouse_button\",\"button\":\"left\",\"pressed\":true,\"x\":100,\"y\":100}");
		scene.gameplay_probe->sample_actions();
		CHECK(scene.gameplay_probe->saw_shoot_pressed);

		server.test_process_line("{\"id\":48,\"cmd\":\"mouse_button\",\"button\":\"left\",\"pressed\":false,\"x\":100,\"y\":100}");
		scene.gameplay_probe->sample_actions();
		CHECK_FALSE(scene.gameplay_probe->saw_shoot_pressed);

		scene.gameplay_probe->clear_samples();
		server.test_process_line("{\"id\":49,\"cmd\":\"mouse_motion\",\"x\":140,\"y\":160}");
		CHECK_EQ(scene.gameplay_probe->mouse_motion_event_count, 1);
		CHECK_EQ(scene.gameplay_probe->last_mouse_screen_relative, Vector2(40, 60));

		server.test_process_line("{\"id\":50,\"cmd\":\"mouse_motion\",\"relativeX\":5,\"relativeY\":-3}");
		CHECK_EQ(scene.gameplay_probe->mouse_motion_event_count, 2);
		CHECK_EQ(scene.gameplay_probe->last_mouse_screen_relative, Vector2(5, -3));

		REQUIRE_EQ(server.test_sent_response_count(), 10);
		for (int i = 0; i < server.test_sent_response_count(); i++) {
			CHECK((bool)server.test_sent_response(i)["ok"]);
		}
	}

	TEST_CASE("[SceneTree] Wait primitives and batch debug helpers expose state") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		server.test_setup_active_batch(1, "continue");

		Dictionary wait_request;
		wait_request["cmd"] = "wait_node_exists";
		Dictionary selector;
		selector["name"] = "PlayButton";
		wait_request["selector"] = selector;
		bool deferred = false;
		Dictionary wait_response = server.test_cmd_wait_node_exists(wait_request, true, deferred);
		CHECK(wait_response.is_empty());
		CHECK(deferred);
		server.test_process_pending_waits();
		CHECK_EQ(server.test_batch_results_count(), 1);
		const Dictionary wait_result_entry = server.test_batch_result(0);
		const Dictionary wait_result_payload = wait_result_entry["result"];
		const Dictionary wait_node = wait_result_payload["node"];
		CHECK_EQ(String(wait_node["name"]), String("PlayButton"));

		server.test_setup_active_batch(2, "continue");
		Dictionary timeout_request;
		timeout_request["cmd"] = "wait_property";
		timeout_request["selector"] = selector;
		timeout_request["property"] = "text";
		timeout_request["equals"] = "Missing";
		timeout_request["captureOnError"] = true;
		deferred = false;
		wait_response = server.test_cmd_wait_property(timeout_request, true, deferred);
		CHECK(wait_response.is_empty());
		CHECK(deferred);
		REQUIRE(server.test_has_pending_waits());
		server.test_force_last_pending_wait_to_now();
		server.test_process_pending_waits();
		const Dictionary timeout_error_bundle = server.test_last_error_bundle();
		CHECK_FALSE(timeout_error_bundle.is_empty());
		CHECK_EQ(String(((Dictionary)timeout_error_bundle["error"])["code"]), String("timeout"));
		CHECK(timeout_error_bundle.has("sceneDigest"));

		server.test_add_checkpoint("after_click");
		const Dictionary batch_status = server.test_batch_status();
		CHECK((bool)batch_status["active"]);
		CHECK_EQ(((Array)batch_status["checkpoints"]).size(), 1);

		Dictionary list_checkpoints = server.test_list_checkpoints();
		CHECK_EQ((int)list_checkpoints["count"], 1);

		Dictionary cancel_request;
		cancel_request["cmd"] = "cancel_batch";
		cancel_request["id"] = 99;
		const Dictionary cancel_response = server.test_cancel_batch(cancel_request);
		CHECK((bool)cancel_response["ok"]);
		CHECK_FALSE((bool)server.test_batch_status()["active"]);

		const Dictionary last_error = server.test_get_last_error_bundle();
		CHECK(last_error.has("errorBundle"));
	}

	TEST_CASE("[SceneTree] Wait validation and frame caches reuse work") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		Dictionary invalid_wait_request;
		invalid_wait_request["cmd"] = "wait_property";
		Dictionary selector;
		selector["name"] = "PlayButton";
		invalid_wait_request["selector"] = selector;
		invalid_wait_request["property"] = "visible";
		invalid_wait_request["equals"] = "true";
		bool deferred = false;
		const Dictionary invalid_wait_response = server.test_cmd_wait_property(invalid_wait_request, false, deferred);
		CHECK_FALSE((bool)invalid_wait_response["ok"]);
		CHECK_EQ(String(((Dictionary)invalid_wait_response["error"])["code"]), String("invalid_wait"));

		Dictionary invalid_scene_wait_request;
		invalid_scene_wait_request["cmd"] = "wait_scene_changed";
		invalid_scene_wait_request["fromScenePath"] = "/root/DoesNotExist";
		deferred = false;
		const Dictionary invalid_scene_wait_response = server.test_cmd_wait_scene_changed(invalid_scene_wait_request, false, deferred);
		CHECK_FALSE((bool)invalid_scene_wait_response["ok"]);
		CHECK_EQ(String(((Dictionary)invalid_scene_wait_response["error"])["code"]), String("invalid_wait"));

		Dictionary numeric_wait_request;
		numeric_wait_request["cmd"] = "wait_property";
		Dictionary numeric_selector;
		numeric_selector["name"] = "SpeedBar";
		numeric_wait_request["selector"] = numeric_selector;
		numeric_wait_request["property"] = "value";
		numeric_wait_request["equals"] = 5.0;
		deferred = false;
		Dictionary numeric_wait_response = server.test_cmd_wait_property(numeric_wait_request, false, deferred);
		CHECK(numeric_wait_response.is_empty());
		CHECK(deferred);
		server.test_process_pending_waits();
		CHECK_FALSE(server.test_has_pending_waits());

		server.test_reset_cache_debug_counters();
		Dictionary query_request;
		query_request["cmd"] = "query_nodes";
		const Dictionary first_query_response = server.test_cmd_query_nodes(query_request);
		const Dictionary second_query_response = server.test_cmd_get_interactables(query_request);
		REQUIRE((bool)first_query_response["ok"]);
		REQUIRE((bool)second_query_response["ok"]);
		CHECK_EQ(server.test_observation_cache_rebuild_count(), 1);

		server.test_reset_cache_debug_counters();
		Ref<Image> first_image;
		Ref<Image> second_image;
		const bool first_image_ok = server.test_get_root_image(first_image);
		const bool second_image_ok = server.test_get_root_image(second_image);
		CHECK_EQ(first_image_ok, second_image_ok);
		CHECK_EQ(server.test_root_image_fetch_count(), 1);
	}

	TEST_CASE("[SceneTree] get_interactables and nearest target ignore helper or offscreen 3D nodes") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		Dictionary interactables_request;
		interactables_request["cmd"] = "get_interactables";
		Dictionary interactables_response = server.test_cmd_get_interactables(interactables_request);
		REQUIRE((bool)interactables_response["ok"]);
		const Array interactables = ((Dictionary)interactables_response["result"])["nodes"];
		CHECK_FALSE(find_snapshot_by_name(interactables, "CubeTarget").is_empty());
		CHECK_FALSE(find_snapshot_by_name(interactables, "SubViewportCube").is_empty());
		CHECK(find_snapshot_by_name(interactables, "@Helper3D").is_empty());
		CHECK(find_snapshot_by_name(interactables, "OffscreenCube").is_empty());
		CHECK(find_snapshot_by_name(interactables, "@SubViewportHelper").is_empty());
		CHECK(find_snapshot_by_name(interactables, "SubViewportOffscreen").is_empty());

		Dictionary snapshot_request;
		snapshot_request["cmd"] = "get_node_snapshot";
		Dictionary selector;
		selector["group"] = "world";
		Array nearest_point;
		nearest_point.push_back(0);
		nearest_point.push_back(0);
		selector["nearestToScreenPoint"] = nearest_point;
		snapshot_request["selector"] = selector;
		Dictionary snapshot_response = server.test_cmd_get_node_snapshot(snapshot_request);
		REQUIRE((bool)snapshot_response["ok"]);
		const Dictionary nearest_node = ((Dictionary)snapshot_response["result"])["node"];
		CHECK_EQ(String(nearest_node["name"]), String("CubeTarget"));
	}

	TEST_CASE("[SceneTree] wait_scene_changed tracks content scene instead of current_scene only") {
		RouterSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };

		Dictionary wait_request;
		wait_request["cmd"] = "wait_scene_changed";
		wait_request["fromScenePath"] = "/root/RouterRoot/Menu";
		bool deferred = false;
		Dictionary wait_response = server.test_cmd_wait_scene_changed(wait_request, false, deferred);
		CHECK(wait_response.is_empty());
		CHECK(deferred);
		REQUIRE(server.test_has_pending_waits());

		server.test_process_pending_waits();
		CHECK(server.test_has_pending_waits());
		CHECK_EQ(server.test_sent_response_count(), 0);

		scene.switch_to_level();
		server.test_process_pending_waits();
		if (server.test_has_pending_waits()) {
			server.test_force_all_pending_waits_to_now();
			server.test_process_pending_waits();
		}
		CHECK_FALSE(server.test_has_pending_waits());
		REQUIRE_EQ(server.test_sent_response_count(), 1);
		const Dictionary response = server.test_sent_response(0);
		CHECK((bool)response["ok"]);
		const Dictionary result = response["result"];
		CHECK_EQ(String(result["fromScenePath"]), String("/root/RouterRoot/Menu"));
		CHECK_EQ(String(result["toScenePath"]), String("/root/RouterRoot/Level"));
		CHECK_EQ(String(result["fromRootScenePath"]), String("/root/RouterRoot"));
		CHECK_EQ(String(result["toRootScenePath"]), String("/root/RouterRoot"));
	}

	TEST_CASE("[SceneTree] Emits trace events for request, wait and runtime perf flows") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };
		const String trace_root = TestUtils::get_temp_path("cli_ai_input_server_trace_root");
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(trace_root);

		CustomFeatureTracer::StartupOptions trace_options;
		trace_options.base_dir_override = trace_root;
		trace_options.binary_path = "godot-dev";
		trace_options.cwd = "E:/GitHub/godot";
		trace_options.project_path = "E:/GitHub/godot";
		trace_options.process_mode = "cli_project_run";
		trace_options.pid = 1357;

		String trace_error;
		REQUIRE_EQ(CustomFeatureTracer::initialize_singleton(trace_options, trace_error), OK);
		REQUIRE(trace_error.is_empty());
		const String session_dir = CustomFeatureTracer::get_singleton()->get_session_dir_for_tests();

		server.test_process_line("{\"id\":1,\"cmd\":\"get_status\"}");

		{
			CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "wait-trace");
			Dictionary wait_request;
			wait_request["cmd"] = "wait_property";
			Dictionary selector;
			selector["name"] = "PlayButton";
			wait_request["selector"] = selector;
			wait_request["property"] = "text";
			wait_request["equals"] = "Play";
			bool deferred = false;
			Dictionary wait_response = server.test_cmd_wait_property(wait_request, false, deferred);
			CHECK(wait_response.is_empty());
			CHECK(deferred);
			server.test_process_pending_waits();
		}

		{
			CustomFeatureTracer::ScopedEventContext trace_context(CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "perf-trace");
			Dictionary perf_start_request;
			perf_start_request["cmd"] = "perf_start";
			perf_start_request["name"] = "trace-session";
			const Dictionary perf_start_response = server.test_cmd_perf_start(perf_start_request);
			REQUIRE((bool)perf_start_response["ok"]);

			bool deferred = false;
			Dictionary perf_stop_request;
			perf_stop_request["cmd"] = "perf_stop";
			perf_stop_request["id"] = 5;
			const Dictionary perf_stop_response = server.test_cmd_perf_stop(perf_stop_request, false, deferred);
			CHECK(perf_stop_response.is_empty());
			CHECK(deferred);
			server.record_frame(0, 16000, 4000, 3000, 2000, 0.016);
		}

		server.shutdown();
		CustomFeatureTracer::shutdown_singleton();

		const Vector<Dictionary> events = TestCustomFeatureTraceHelpers::read_events(session_dir);
		REQUIRE_FALSE(events.is_empty());
		for (int i = 0; i < events.size(); i++) {
			TestCustomFeatureTraceHelpers::check_common_event_fields(events[i]);
		}

		const Dictionary request_event = TestCustomFeatureTraceHelpers::find_event(events, CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "request_received");
		REQUIRE_FALSE(request_event.is_empty());
		CHECK_EQ(String(((Dictionary)request_event["data"])["cmd"]), String("get_status"));

		const Dictionary wait_event = TestCustomFeatureTraceHelpers::find_event(events, CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "wait_finished");
		REQUIRE_FALSE(wait_event.is_empty());
		CHECK_EQ(String(((Dictionary)wait_event["data"])["cmd"]), String("wait_property"));

		const Dictionary perf_start_event = TestCustomFeatureTraceHelpers::find_event(events, CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "runtime_perf_started");
		REQUIRE_FALSE(perf_start_event.is_empty());

		const Dictionary perf_summary_event = TestCustomFeatureTraceHelpers::find_event(events, CustomFeatureTracer::FEATURE_RUNTIME_AI_AGENT_CONTROL, "runtime_perf_summary");
		REQUIRE_FALSE(perf_summary_event.is_empty());
		const Dictionary perf_payloads = perf_summary_event["payloads"];
		REQUIRE(perf_payloads.has("response"));
		const Variant perf_response_payload = JSON::parse_string(TestCustomFeatureTraceHelpers::read_payload_text(session_dir, perf_payloads["response"]));
		REQUIRE(perf_response_payload.get_type() == Variant::DICTIONARY);
		const Dictionary perf_response = perf_response_payload;
		REQUIRE((bool)perf_response["ok"]);
		const Dictionary perf_result = perf_response["result"];
		const Dictionary recording = perf_result["recording"];
		CHECK((bool)recording["completed"]);
		CHECK_EQ((int64_t)recording["capturedFrames"], 1);

		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(trace_root);
	}

	TEST_CASE("[SceneTree] Direct operation traces include sceneDigest on success and failure") {
		TestSceneContext scene;
		CLIAIInputServer server{ CLIAIInputServer::Options() };
		const String trace_root = TestUtils::get_temp_path("cli_ai_input_server_direct_trace_root");
		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(trace_root);

		CustomFeatureTracer::StartupOptions trace_options;
		trace_options.base_dir_override = trace_root;
		trace_options.binary_path = "godot-dev";
		trace_options.cwd = "E:/GitHub/godot";
		trace_options.project_path = "E:/GitHub/godot";
		trace_options.process_mode = "cli_project_run";
		trace_options.pid = 2468;

		String trace_error;
		REQUIRE_EQ(CustomFeatureTracer::initialize_singleton(trace_options, trace_error), OK);
		REQUIRE(trace_error.is_empty());
		const String session_dir = CustomFeatureTracer::get_singleton()->get_session_dir_for_tests();

		server.test_clear_sent_responses();
		server.test_process_line("{\"id\":11,\"cmd\":\"click_target\",\"selector\":{\"name\":\"PlayButton\"}}");
		drain_server_batch(server);
		server.test_process_line("{\"id\":12,\"cmd\":\"click_target\",\"selector\":{\"name\":\"MissingButton\"},\"captureOnError\":true}");
		drain_server_batch(server);

		server.shutdown();
		CustomFeatureTracer::shutdown_singleton();

		const Vector<Dictionary> events = TestCustomFeatureTraceHelpers::read_events(session_dir);
		REQUIRE_FALSE(events.is_empty());

		const Dictionary batch_finished_response = find_event_response_payload(events, session_dir, "batch_finished");
		REQUIRE_FALSE(batch_finished_response.is_empty());
		CHECK(batch_finished_response.has("sceneDigest"));

		const Dictionary batch_step_failed_response = find_event_response_payload(events, session_dir, "batch_step_failed");
		REQUIRE_FALSE(batch_step_failed_response.is_empty());
		CHECK(batch_step_failed_response.has("sceneDigest"));

		TestCustomFeatureTraceHelpers::cleanup_directory_recursive(trace_root);
	}

#ifdef TOOLS_ENABLED
	TEST_CASE("[SceneTree] Editor run args append AI agent flags before plugin hooks") {
		Vector<String> args;
		args.push_back("--foo");
		const Vector<String> appended = EditorRunBar::test_append_ai_agent_control_run_args(args, true, 7011, 32, 4096);
		REQUIRE_EQ(appended.size(), 8);
		CHECK_EQ(appended[0], String("--foo"));
		CHECK_EQ(appended[1], String("--ai-agent-control"));
		CHECK_EQ(appended[2], String("--ai-agent-port"));
		CHECK_EQ(appended[3], String("7011"));
		CHECK_EQ(appended[4], String("--ai-agent-max-ops"));
		CHECK_EQ(appended[5], String("32"));
		CHECK_EQ(appended[6], String("--ai-agent-max-line-bytes"));
		CHECK_EQ(appended[7], String("4096"));
	}
#endif
}

} // namespace TestCLIAIInputServer
