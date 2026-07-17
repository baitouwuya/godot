/**************************************************************************/
/*  mcp_runtime_observation_snapshot.cpp                                  */
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

#include "mcp_runtime_observation.h"

#include "scene/gui/control.h"
#include "scene/main/canvas_item.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#ifndef _3D_DISABLED
#include "scene/3d/camera_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/visual_instance_3d.h"
#endif

namespace {

bool _get_vector2(const Variant &p_value, Vector2 &r_value) {
	if (p_value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array values = p_value;
	if (values.size() != 2 || !values[0].is_num() || !values[1].is_num()) {
		return false;
	}
	const double x = values[0];
	const double y = values[1];
	if (!Math::is_finite(x) || !Math::is_finite(y)) {
		return false;
	}
	r_value = Vector2(x, y);
	return true;
}

Array _vector2_to_array(const Vector2 &p_value) {
	return Array{ p_value.x, p_value.y };
}

#ifndef _3D_DISABLED
Array _vector3_to_array(const Vector3 &p_value) {
	return Array{ p_value.x, p_value.y, p_value.z };
}
#endif

Dictionary _rect_to_dictionary(const Rect2 &p_rect) {
	Dictionary result;
	result["x"] = p_rect.position.x;
	result["y"] = p_rect.position.y;
	result["width"] = p_rect.size.x;
	result["height"] = p_rect.size.y;
	return result;
}

bool _dictionary_to_rect(const Dictionary &p_value, Rect2 &r_rect) {
	static const char *fields[] = { "x", "y", "width", "height" };
	for (const char *field : fields) {
		if (!p_value.has(field) || !p_value[field].is_num()) {
			return false;
		}
		const double value = p_value[field];
		if (!Math::is_finite(value)) {
			return false;
		}
	}
	r_rect = Rect2(double(p_value["x"]), double(p_value["y"]), double(p_value["width"]), double(p_value["height"]));
	return r_rect.size.x >= 0.0 && r_rect.size.y >= 0.0;
}

Rect2 _transform_rect(const Transform2D &p_transform, const Rect2 &p_rect) {
	const Vector2 p0 = p_transform.xform(p_rect.position);
	const Vector2 p1 = p_transform.xform(p_rect.position + Vector2(p_rect.size.x, 0));
	const Vector2 p2 = p_transform.xform(p_rect.position + p_rect.size);
	const Vector2 p3 = p_transform.xform(p_rect.position + Vector2(0, p_rect.size.y));
	Rect2 result(p0, Vector2());
	result.expand_to(p1);
	result.expand_to(p2);
	result.expand_to(p3);
	return result;
}

Variant _get_property_or_nil(Object *p_object, const StringName &p_property, bool &r_valid) {
	r_valid = false;
	return p_object ? p_object->get(p_property, &r_valid) : Variant();
}

#ifndef _3D_DISABLED
Camera3D *_get_effective_camera(Viewport *p_viewport) {
	if (!p_viewport) {
		return nullptr;
	}
	return p_viewport->is_camera_3d_override_enabled() ? p_viewport->get_overridden_camera_3d() : p_viewport->get_camera_3d();
}
#endif

} // namespace

bool MCPRuntimeObservation::Selector::is_empty() const {
	return path.is_empty() && name.is_empty() && type.is_empty() && group.is_empty() && text.is_empty() &&
			!has_is_3d && !has_visible && !has_screen_rect && !has_nearest_to_screen_point;
}

Error MCPRuntimeObservation::parse_selector(const Dictionary &p_value, Selector &r_selector, String &r_error) {
	r_selector = Selector();
	r_error = String();
	const PackedStringArray allowed{ "path", "name", "type", "group", "text", "is3D", "visible", "screenRect", "nearestToScreenPoint" };
	for (const Variant &key_value : p_value.keys()) {
		if (!key_value.is_string() || !allowed.has(String(key_value))) {
			r_error = "Unsupported selector field: " + String(key_value);
			return ERR_INVALID_PARAMETER;
		}
	}
	const char *string_fields[] = { "path", "name", "type", "group", "text" };
	String *targets[] = { &r_selector.path, &r_selector.name, &r_selector.type, &r_selector.group, &r_selector.text };
	for (int i = 0; i < 5; i++) {
		const Variant value = p_value.get(string_fields[i], String());
		if (value.get_type() != Variant::STRING) {
			r_error = String(string_fields[i]) + " must be a string.";
			return ERR_INVALID_PARAMETER;
		}
		*targets[i] = value;
	}
	if (p_value.has("is3D")) {
		if (p_value["is3D"].get_type() != Variant::BOOL) {
			r_error = "is3D must be boolean.";
			return ERR_INVALID_PARAMETER;
		}
		r_selector.has_is_3d = true;
		r_selector.is_3d = p_value["is3D"];
	}
	if (p_value.has("visible")) {
		if (p_value["visible"].get_type() != Variant::BOOL) {
			r_error = "visible must be boolean.";
			return ERR_INVALID_PARAMETER;
		}
		r_selector.has_visible = true;
		r_selector.visible = p_value["visible"];
	}
	if (p_value.has("screenRect")) {
		if (p_value["screenRect"].get_type() != Variant::DICTIONARY || !_dictionary_to_rect(p_value["screenRect"], r_selector.screen_rect)) {
			r_error = "screenRect must contain finite x, y, width, and height values with non-negative size.";
			return ERR_INVALID_PARAMETER;
		}
		r_selector.has_screen_rect = true;
	}
	if (p_value.has("nearestToScreenPoint")) {
		if (!_get_vector2(p_value["nearestToScreenPoint"], r_selector.nearest_to_screen_point)) {
			r_error = "nearestToScreenPoint must be a finite [x, y] array.";
			return ERR_INVALID_PARAMETER;
		}
		r_selector.has_nearest_to_screen_point = true;
	}
	return OK;
}

bool MCPRuntimeObservation::matches_snapshot(const Dictionary &p_snapshot, const Selector &p_selector) {
	if (!p_selector.path.is_empty() && String(p_snapshot.get("nodePath", String())) != p_selector.path) {
		return false;
	}
	if (!p_selector.name.is_empty() && String(p_snapshot.get("name", String())) != p_selector.name) {
		return false;
	}
	if (!p_selector.type.is_empty() && String(p_snapshot.get("type", String())) != p_selector.type) {
		return false;
	}
	if (!p_selector.text.is_empty() && String(p_snapshot.get("text", String())) != p_selector.text) {
		return false;
	}
	if (!p_selector.group.is_empty()) {
		const Variant groups_value = p_snapshot.get("groups", Variant());
		if (groups_value.get_type() != Variant::ARRAY || !Array(groups_value).has(p_selector.group)) {
			return false;
		}
	}
	if (p_selector.has_is_3d && bool(p_snapshot.get("is3D", false)) != p_selector.is_3d) {
		return false;
	}
	if (p_selector.has_visible && bool(p_snapshot.get("visible", false)) != p_selector.visible) {
		return false;
	}
	if (p_selector.has_screen_rect) {
		const Variant rect_value = p_snapshot.get("screenRect", Variant());
		Rect2 rect;
		if (rect_value.get_type() != Variant::DICTIONARY || !_dictionary_to_rect(rect_value, rect) || !p_selector.screen_rect.intersects(rect)) {
			return false;
		}
	}
	return true;
}

bool MCPRuntimeObservation::has_screen_position(const Dictionary &p_snapshot) {
	return bool(p_snapshot.get("hasScreenPosition", false));
}

Vector2 MCPRuntimeObservation::get_screen_position(const Dictionary &p_snapshot) {
	const Variant rect_value = p_snapshot.get("screenRect", Variant());
	if (rect_value.get_type() == Variant::DICTIONARY) {
		Rect2 rect;
		if (_dictionary_to_rect(rect_value, rect)) {
			return rect.get_center();
		}
	}
	Vector2 point;
	return _get_vector2(p_snapshot.get("screenPoint", Variant()), point) ? point : Vector2();
}

bool MCPRuntimeObservation::is_interactable(Node *p_node, const Dictionary &p_snapshot) {
	if (!bool(p_snapshot.get("visible", false)) || !has_screen_position(p_snapshot)) {
		return false;
	}
#ifndef _3D_DISABLED
	if (bool(p_snapshot.get("is3D", false))) {
		CollisionObject3D *collision = Object::cast_to<CollisionObject3D>(p_node);
		return collision && collision->can_process() && collision->is_ray_pickable() &&
				!String(p_snapshot.get("name", String())).begins_with("@");
	}
#endif
	return !bool(p_snapshot.get("disabled", false));
}

String MCPRuntimeObservation::get_active_camera_path(Viewport *p_viewport) {
#ifndef _3D_DISABLED
	Camera3D *camera = _get_effective_camera(p_viewport);
	return camera ? String(camera->get_path()) : String();
#else
	return String();
#endif
}

Dictionary MCPRuntimeObservation::build_node_snapshot(Node *p_node, Viewport *p_root_viewport) {
	Dictionary snapshot;
	snapshot["nodePath"] = p_node ? String(p_node->get_path()) : String();
	snapshot["name"] = p_node ? String(p_node->get_name()) : String();
	snapshot["type"] = p_node ? String(p_node->get_class()) : String();
	snapshot["groups"] = Array();
	snapshot["visible"] = true;
	snapshot["disabled"] = Variant();
	snapshot["is3D"] = false;
	snapshot["screenRect"] = Variant();
	snapshot["screenPoint"] = Variant();
	snapshot["hasScreenPosition"] = false;
	snapshot["viewportPath"] = String();
	snapshot["worldPosition"] = Variant();
	snapshot["text"] = Variant();
	snapshot["value"] = Variant();
	snapshot["selected"] = Variant();
	snapshot["childCount"] = p_node ? p_node->get_child_count() : 0;
	if (!p_node) {
		return snapshot;
	}
	Viewport *node_viewport = p_node->get_viewport();
	snapshot["viewportPath"] = node_viewport ? String(node_viewport->get_path()) : String();
	Array groups;
	List<Node::GroupInfo> group_list;
	p_node->get_groups(&group_list);
	for (const Node::GroupInfo &group : group_list) {
		if (!String(group.name).begins_with("_")) {
			groups.push_back(String(group.name));
		}
	}
	snapshot["groups"] = groups;
	bool valid = false;
	Variant value = _get_property_or_nil(p_node, "disabled", valid);
	if (valid) {
		snapshot["disabled"] = value;
	}
	value = _get_property_or_nil(p_node, "text", valid);
	if (valid) {
		snapshot["text"] = value;
	}
	value = _get_property_or_nil(p_node, "value", valid);
	if (valid) {
		snapshot["value"] = value;
	}
	value = _get_property_or_nil(p_node, "selected", valid);
	if (!valid) {
		value = _get_property_or_nil(p_node, "button_pressed", valid);
	}
	if (valid) {
		snapshot["selected"] = value;
	}
	const Transform2D screen_to_root = p_root_viewport ? p_root_viewport->get_screen_transform().affine_inverse() : Transform2D();
	if (Window *window = Object::cast_to<Window>(p_node)) {
		snapshot["visible"] = window->is_visible();
		const Rect2 screen_rect(window->get_screen_transform().get_origin(), window->get_size());
		snapshot["screenRect"] = _rect_to_dictionary(_transform_rect(screen_to_root, screen_rect));
		snapshot["hasScreenPosition"] = true;
	}
	if (Control *control = Object::cast_to<Control>(p_node)) {
		snapshot["visible"] = control->is_visible_in_tree();
		snapshot["screenRect"] = _rect_to_dictionary(_transform_rect(screen_to_root, control->get_screen_rect()));
		snapshot["hasScreenPosition"] = true;
	} else if (CanvasItem *canvas_item = Object::cast_to<CanvasItem>(p_node)) {
		snapshot["visible"] = canvas_item->is_visible_in_tree();
	}
#ifndef _3D_DISABLED
	if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
		snapshot["is3D"] = true;
		snapshot["visible"] = node_3d->is_visible_in_tree();
		snapshot["worldPosition"] = _vector3_to_array(node_3d->get_global_position());
		Camera3D *camera = _get_effective_camera(node_viewport);
		if (camera && node_viewport && p_root_viewport && !camera->is_position_behind(node_3d->get_global_position())) {
			const Transform2D viewport_to_root = p_root_viewport->get_screen_transform().affine_inverse() * node_viewport->get_screen_transform();
			const Vector2 viewport_point = camera->unproject_position(node_3d->get_global_position());
			const Vector2 root_point = viewport_to_root.xform(viewport_point);
			const Rect2 visible_rect = p_root_viewport->get_visible_rect();
			Rect2 root_rect(root_point - Vector2(2, 2), Vector2(4, 4));
			if (VisualInstance3D *visual = Object::cast_to<VisualInstance3D>(p_node)) {
				const AABB aabb = visual->get_aabb();
				bool initialized = false;
				Rect2 projected;
				for (int i = 0; i < 8; i++) {
					const Vector3 corner = visual->get_global_transform().xform(aabb.get_endpoint(i));
					if (camera->is_position_behind(corner)) {
						continue;
					}
					const Vector2 point = camera->unproject_position(corner);
					if (!initialized) {
						projected = Rect2(point, Vector2());
						initialized = true;
					} else {
						projected.expand_to(point);
					}
				}
				if (initialized) {
					if (projected.size == Vector2()) {
						projected = Rect2(viewport_point - Vector2(2, 2), Vector2(4, 4));
					}
					root_rect = _transform_rect(viewport_to_root, projected);
				}
			}
			if (visible_rect.intersects(root_rect, true)) {
				const Rect2 clipped = visible_rect.intersection(root_rect);
				snapshot["screenPoint"] = _vector2_to_array(root_point);
				snapshot["screenRect"] = _rect_to_dictionary(clipped.has_area() ? clipped : root_rect);
				snapshot["hasScreenPosition"] = true;
			}
		}
	}
#endif
	return snapshot;
}
