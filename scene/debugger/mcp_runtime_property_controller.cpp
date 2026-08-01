/**************************************************************************/
/*  mcp_runtime_property_controller.cpp                                   */
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

#include "mcp_runtime_property_controller.h"

#include "core/debugger/engine_debugger.h"
#include "core/object/class_db.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace {

Node *_resolve_node(const String &p_path) {
	SceneTree *scene_tree = SceneTree::get_singleton();
	Node *root = scene_tree ? scene_tree->get_root() : nullptr;
	if (!root) {
		return nullptr;
	}
	const NodePath path = p_path;
	if (!path.is_absolute() || path.get_subname_count() > 0) {
		return nullptr;
	}
	return root->get_node_or_null(path);
}

bool _find_property(Node *p_node, const StringName &p_name, PropertyInfo &r_property) {
	List<PropertyInfo> properties;
	p_node->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (property.name == p_name) {
			r_property = property;
			return true;
		}
	}
	return false;
}

bool _is_unsafe_value(const Variant &p_value) {
	return p_value.get_type() == Variant::OBJECT || p_value.get_type() == Variant::CALLABLE ||
			p_value.get_type() == Variant::SIGNAL || p_value.get_type() == Variant::RID;
}

Error _fail(const String &p_code, const String &p_message, String &r_error_code, String &r_error_message, Error p_error) {
	r_error_code = p_code;
	r_error_message = p_message;
	return p_error;
}

} // namespace

void MCPRuntimePropertyController::_bind_methods() {
}

MCPRuntimePropertyController::MCPRuntimePropertyController() {
	singleton = this;
	EngineDebugger::register_message_capture("mcp_property", EngineDebugger::Capture(this, _parse_message));
}

MCPRuntimePropertyController::~MCPRuntimePropertyController() {
	if (EngineDebugger::get_singleton()) {
		EngineDebugger::unregister_message_capture("mcp_property");
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}

void MCPRuntimePropertyController::initialize() {
	if (!singleton) {
		memnew(MCPRuntimePropertyController);
	}
}

void MCPRuntimePropertyController::deinitialize() {
	if (singleton) {
		memdelete(singleton);
	}
}

Error MCPRuntimePropertyController::execute(const Dictionary &p_arguments, Dictionary &r_result, String &r_error_code,
		String &r_error_message) {
	r_result.clear();
	r_error_code = String();
	r_error_message = String();
	const Variant path_value = p_arguments.get("path", Variant());
	const Variant property_value = p_arguments.get("property", Variant());
	if (path_value.get_type() != Variant::STRING || String(path_value).is_empty() ||
			property_value.get_type() != Variant::STRING || String(property_value).is_empty() || !p_arguments.has("value")) {
		return _fail("INVALID_ARGUMENTS", "path, property, and value are required.", r_error_code, r_error_message, ERR_INVALID_PARAMETER);
	}

	Node *node = _resolve_node(path_value);
	if (!node) {
		return _fail("RUNTIME_NODE_NOT_FOUND", "Runtime node was not found: " + String(path_value), r_error_code, r_error_message, ERR_DOES_NOT_EXIST);
	}
	const StringName property_name = property_value;
	PropertyInfo property;
	if (!_find_property(node, property_name, property)) {
		return _fail("RUNTIME_PROPERTY_NOT_FOUND", "Runtime property was not found: " + String(property_name), r_error_code, r_error_message, ERR_DOES_NOT_EXIST);
	}
	if (property.usage & PROPERTY_USAGE_READ_ONLY) {
		return _fail("RUNTIME_PROPERTY_READ_ONLY", "Runtime property is read-only: " + String(property_name), r_error_code, r_error_message, ERR_UNAUTHORIZED);
	}
	const bool editor_property = property.usage & PROPERTY_USAGE_EDITOR;
	const bool native_setter = !ClassDB::get_property_setter(node->get_class_name(), property_name).is_empty();
	if (!editor_property && !native_setter) {
		return _fail("RUNTIME_PROPERTY_NOT_EDITABLE",
				"Runtime property has no public native setter: " + String(property_name), r_error_code, r_error_message, ERR_UNAUTHORIZED);
	}
	const Variant requested_value = p_arguments["value"];
	if (_is_unsafe_value(requested_value)) {
		return _fail("INVALID_VALUE", "Object-like runtime property values are not accepted by this setter.", r_error_code, r_error_message, ERR_INVALID_DATA);
	}
	if (property.type != Variant::NIL && requested_value.get_type() != property.type &&
			!Variant::can_convert_strict(requested_value.get_type(), property.type)) {
		return _fail("INVALID_VALUE", "Value type '" + Variant::get_type_name(requested_value.get_type()) +
					"' is incompatible with runtime property type '" + Variant::get_type_name(property.type) + "'.",
				r_error_code, r_error_message, ERR_INVALID_DATA);
	}

	bool valid = false;
	const Variant previous_value = node->get(property_name, &valid);
	if (!valid || _is_unsafe_value(previous_value)) {
		return _fail("RUNTIME_PROPERTY_UNAVAILABLE", "Runtime property cannot be safely read: " + String(property_name), r_error_code, r_error_message, ERR_UNAVAILABLE);
	}
	node->set(property_name, requested_value, &valid);
	if (!valid) {
		return _fail("RUNTIME_PROPERTY_REJECTED", "The runtime object rejected property: " + String(property_name), r_error_code, r_error_message, ERR_INVALID_DATA);
	}
	const Variant effective_value = node->get(property_name, &valid);
	if (!valid || _is_unsafe_value(effective_value)) {
		return _fail("RUNTIME_PROPERTY_UNAVAILABLE", "Updated runtime property cannot be safely read: " + String(property_name), r_error_code, r_error_message, ERR_UNAVAILABLE);
	}

	r_result["path"] = path_value;
	r_result["property"] = property_name;
	r_result["previousValue"] = previous_value;
	r_result["value"] = effective_value;
	r_result["coerced"] = effective_value != requested_value;
	return OK;
}

void MCPRuntimePropertyController::_send_response(const String &p_request_id, const String &p_operation, bool p_ok,
		const String &p_code, const String &p_message, const Dictionary &p_data) const {
	if (EngineDebugger::get_singleton()) {
		EngineDebugger::get_singleton()->send_message("mcp_property:response",
				Array{ p_request_id, p_operation, p_ok, p_code, p_message, p_data });
	}
}

Error MCPRuntimePropertyController::_parse_message(void *p_user, const String &p_message, const Array &p_arguments,
		bool &r_captured) {
	MCPRuntimePropertyController *controller = static_cast<MCPRuntimePropertyController *>(p_user);
	r_captured = true;
	const String operation = p_message.get_slice(":", 1);
	if (!controller || operation != "set_property") {
		return ERR_INVALID_PARAMETER;
	}
	if (p_arguments.size() != 2 || p_arguments[0].get_type() != Variant::STRING || String(p_arguments[0]).is_empty() ||
			p_arguments[1].get_type() != Variant::DICTIONARY) {
		return ERR_INVALID_DATA;
	}
	Dictionary result;
	String error_code;
	String error_message;
	const Error error = execute(p_arguments[1], result, error_code, error_message);
	controller->_send_response(p_arguments[0], operation, error == OK, error_code, error_message, result);
	return OK;
}
