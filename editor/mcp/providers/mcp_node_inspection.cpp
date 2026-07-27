/**************************************************************************/
/*  mcp_node_inspection.cpp                                               */
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

#include "mcp_node_inspection.h"

#include "mcp_scene_utils.h"
#include "mcp_tool_utils.h"
#include "mcp_variant_codec.h"

#include "core/object/script_language.h"
#include "scene/main/node.h"

namespace {

static Error _fail(const String &p_message, String *r_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return ERR_INVALID_PARAMETER;
}

static bool _is_script_path(const Ref<Script> &p_script, const String &p_path) {
	if (p_path.is_empty()) {
		return false;
	}
	Ref<Script> script = p_script;
	while (script.is_valid()) {
		if (script->get_path() == p_path) {
			return true;
		}
		script = script->get_base_script();
	}
	return false;
}

static String _get_layout_kind(const PropertyInfo &p_property) {
	if (p_property.usage & PROPERTY_USAGE_CATEGORY) {
		return "category";
	}
	if (p_property.usage & PROPERTY_USAGE_GROUP) {
		return "group";
	}
	if (p_property.usage & PROPERTY_USAGE_SUBGROUP) {
		return "subgroup";
	}
	return String();
}

static bool _is_native_class_category(Node *p_node, const PropertyInfo &p_property) {
	return (p_property.usage & PROPERTY_USAGE_CATEGORY) &&
			p_property.hint_string == String(p_property.name) && p_node->is_class(p_property.name);
}

static Dictionary _make_node_value_reference(Node *p_scene_root, const Variant &p_value) {
	if (p_value.get_type() != Variant::OBJECT) {
		return Dictionary();
	}
	Node *referenced_node = Object::cast_to<Node>(p_value.get_validated_object());
	if (!referenced_node) {
		return Dictionary();
	}

	Dictionary reference;
	if (MCPSceneUtils::is_node_in_scene(p_scene_root, referenced_node)) {
		reference = MCPSceneUtils::make_node_summary(p_scene_root, referenced_node);
		reference["scope"] = "editedScene";
	} else {
		reference["name"] = String(referenced_node->get_name());
		reference["type"] = referenced_node->get_class();
		if (referenced_node->is_inside_tree()) {
			reference["path"] = String(referenced_node->get_path());
			reference["scope"] = "sceneTree";
		} else {
			reference["scope"] = "detached";
		}
	}
	reference["kind"] = "node";
	return reference;
}

} // namespace

namespace MCPNodeInspection {

Error inspect(Node *p_scene_root, Node *p_node, Dictionary &r_result, String *r_error) {
	r_result = Dictionary();
	if (r_error) {
		*r_error = String();
	}
	if (!MCPSceneUtils::is_node_in_scene(p_scene_root, p_node)) {
		return _fail("The inspected node must belong to the edited scene.", r_error);
	}

	const Ref<Script> node_script = p_node->get_script();
	const String attached_script_path = node_script.is_valid() ? node_script->get_path() : String();
	bool in_script_section = p_node->get_script_instance() != nullptr;
	String current_script_path = attached_script_path;
	Array properties;
	Array property_layout;
	int script_property_count = 0;
	List<PropertyInfo> property_list;
	p_node->get_property_list(&property_list, true);
	for (const PropertyInfo &property : property_list) {
		const String layout_kind = _get_layout_kind(property);
		if (!layout_kind.is_empty()) {
			if (layout_kind == "category" && in_script_section) {
				if (_is_native_class_category(p_node, property)) {
					in_script_section = false;
					current_script_path = String();
				} else if (_is_script_path(node_script, property.hint_string) || property.hint_string.begins_with("res://")) {
					current_script_path = property.hint_string;
				}
			}
			Dictionary layout_entry = MCPToolUtils::make_property_description(property);
			layout_entry["kind"] = layout_kind;
			if (in_script_section) {
				layout_entry["source"] = "script";
				if (!current_script_path.is_empty()) {
					layout_entry["scriptPath"] = current_script_path;
				}
			} else {
				layout_entry["source"] = "native";
			}
			property_layout.push_back(layout_entry);
			continue;
		}
		if (!(property.usage & PROPERTY_USAGE_EDITOR)) {
			continue;
		}

		const bool script_exposed = in_script_section;
		const String property_script_path = !current_script_path.is_empty() ? current_script_path : attached_script_path;
		Dictionary item = MCPToolUtils::make_property_description(property);
		item["readOnly"] = bool(property.usage & PROPERTY_USAGE_READ_ONLY);
		item["scriptExposed"] = script_exposed;
		item["source"] = script_exposed ? "script" : "native";
		if (script_exposed && !property_script_path.is_empty()) {
			item["scriptPath"] = property_script_path;
		}

		bool valid = false;
		const Variant value = p_node->get(property.name, &valid);
		item["valueAvailable"] = valid;
		if (!valid) {
			item["encodable"] = false;
			item["encodingError"] = "Property value is unavailable.";
		} else {
			Variant encoded_value;
			String encoding_error;
			if (MCPVariantCodec::encode(value, encoded_value, &encoding_error) == OK) {
				item["encodable"] = true;
				item["value"] = encoded_value;
			} else {
				item["encodable"] = false;
				item["encodingError"] = encoding_error;
				const Dictionary value_reference = _make_node_value_reference(p_scene_root, value);
				if (!value_reference.is_empty()) {
					item["valueReference"] = value_reference;
				}
			}
		}

		Dictionary layout_entry;
		layout_entry["kind"] = "property";
		layout_entry["name"] = String(property.name);
		layout_entry["propertyIndex"] = properties.size();
		layout_entry["source"] = item["source"];
		if (script_exposed && !property_script_path.is_empty()) {
			layout_entry["scriptPath"] = property_script_path;
		}
		property_layout.push_back(layout_entry);
		properties.push_back(item);
		if (script_exposed) {
			script_property_count++;
		}
	}

	r_result = MCPSceneUtils::make_node_summary(p_scene_root, p_node);
	if (!attached_script_path.is_empty()) {
		r_result["scriptPath"] = attached_script_path;
	}
	r_result["propertyCount"] = properties.size();
	r_result["scriptPropertyCount"] = script_property_count;
	r_result["properties"] = properties;
	r_result["propertyLayout"] = property_layout;
	return OK;
}

} // namespace MCPNodeInspection
