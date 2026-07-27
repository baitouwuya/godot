/**************************************************************************/
/*  mcp_resource_provider.cpp                                             */
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

#include "mcp_resource_provider.h"

#include "mcp_path_utils.h"
#include "mcp_resource_tool_utils.h"
#include "mcp_tool_utils.h"
#include "mcp_variant_codec.h"

#include "core/error/error_list.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/mcp/mcp_tool_registry.h"
#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"

namespace {

struct ResourceSelection {
	String resource_path;
	String absolute_path;
	Ref<Resource> resource;
};

static Dictionary _load_project_resource(const Dictionary &p_arguments, const String &p_project_root,
		const HashMap<String, Ref<Resource>> &p_resource_cache, ResourceSelection &r_selection) {
	r_selection = ResourceSelection();
	const Variant path_value = p_arguments.get("path", Variant());
	if (path_value.get_type() != Variant::STRING || String(path_value).is_empty()) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty string path is required.");
	}

	String path_error;
	const Error path_result = p_project_root.is_empty() ?
			MCPPathUtils::resolve_project_file_path(path_value, r_selection.resource_path, r_selection.absolute_path, &path_error) :
			MCPPathUtils::resolve_project_file_path_for_root(path_value, p_project_root, r_selection.resource_path, r_selection.absolute_path, &path_error);
	if (path_result != OK) {
		return MCPToolUtils::make_error_result("INVALID_PATH", path_error);
	}
	if (!FileAccess::exists(r_selection.absolute_path)) {
		return MCPToolUtils::make_error_result("RESOURCE_NOT_FOUND", "Resource does not exist: " + r_selection.resource_path);
	}
	if (const Ref<Resource> *cached = p_resource_cache.getptr(r_selection.resource_path)) {
		r_selection.resource = *cached;
		if (r_selection.resource.is_valid()) {
			return Dictionary();
		}
	}

	const String load_path = p_project_root.is_empty() ? r_selection.resource_path : r_selection.absolute_path;
	r_selection.resource = ResourceLoader::load(load_path, String(), ResourceFormatLoader::CACHE_MODE_REUSE);
	if (r_selection.resource.is_null()) {
		return MCPToolUtils::make_error_result("RESOURCE_LOAD_FAILED", "Resource could not be loaded: " + r_selection.resource_path);
	}
	return Dictionary();
}

static bool _find_property(const Ref<Resource> &p_resource, const StringName &p_name, PropertyInfo &r_property) {
	List<PropertyInfo> properties;
	p_resource->get_property_list(&properties, true);
	for (const PropertyInfo &property : properties) {
		if (property.name == p_name) {
			r_property = property;
			return true;
		}
	}
	return false;
}

static void _set_error(String *r_error, const String &p_message) {
	if (r_error) {
		*r_error = p_message;
	}
}

} // namespace

MCPResourceProvider::MCPResourceProvider(const String &p_project_root, ImportFunction p_import_function, ScanFunction p_scan_function) :
		project_root(p_project_root),
		import_service(p_project_root, p_import_function, p_scan_function) {
}

MCPResourceProvider::~MCPResourceProvider() {
	unregister_tools();
}

Error MCPResourceProvider::register_tools(MCPToolRegistry *p_registry, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	if (!p_registry) {
		_set_error(r_error, "An MCP tool registry is required.");
		return ERR_INVALID_PARAMETER;
	}
	if (tool_registry) {
		_set_error(r_error, "Resource tools are already registered.");
		return ERR_ALREADY_IN_USE;
	}

	const LocalVector<MCPToolUtils::ToolDescriptor> tools{
			{ "godot.resource.get_properties", "Get editable Inspector properties from a cached project Resource.",
					MCPResourceToolUtils::path_schema(), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPResourceProvider::get_properties), MCPResourceToolUtils::get_properties_output_schema() },
			{ "godot.resource.set_property", "Set a cached project Resource property without saving it.",
					MCPResourceToolUtils::set_property_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPResourceProvider::set_property), MCPResourceToolUtils::set_property_output_schema() },
			{ "godot.resource.save", "Explicitly save a cached project Resource.",
					MCPResourceToolUtils::path_schema(), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPResourceProvider::save), MCPResourceToolUtils::save_output_schema() },
			{ "godot.resource.import_options", "List ResourceImporter candidates and effective import options for an external asset.",
					MCPResourceToolUtils::import_options_schema(false), MCPToolUtils::TOOL_READ_ONLY, callable_mp(this, &MCPResourceProvider::import_options), MCPResourceToolUtils::import_options_output_schema() },
			{ "godot.resource.import", "Copy an external asset into the project, roll back known target-owned artifacts, and report unowned residuals.",
					MCPResourceToolUtils::import_options_schema(true), MCPToolUtils::TOOL_DESTRUCTIVE, callable_mp(this, &MCPResourceProvider::import_resource), MCPResourceToolUtils::import_output_schema() },
	};
	const Error error = MCPToolUtils::register_tools(p_registry, this, tools, r_error);
	if (error != OK) {
		return error;
	}

	tool_registry = p_registry;
	return OK;
}

void MCPResourceProvider::unregister_tools() {
	if (!tool_registry) {
		resource_cache.clear();
		return;
	}
	tool_registry->unregister_tools_for_owner(this);
	tool_registry = nullptr;
	resource_cache.clear();
}

Dictionary MCPResourceProvider::get_properties(const Dictionary &p_arguments, const Dictionary &) {
	ResourceSelection selection;
	const Dictionary load_error = _load_project_resource(p_arguments, project_root, resource_cache, selection);
	if (!load_error.is_empty()) {
		return load_error;
	}
	resource_cache[selection.resource_path] = selection.resource;

	Array properties;
	List<PropertyInfo> property_list;
	selection.resource->get_property_list(&property_list, true);
	for (const PropertyInfo &property : property_list) {
		if (!(property.usage & PROPERTY_USAGE_EDITOR) || property.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP)) {
			continue;
		}
		Dictionary item = MCPToolUtils::make_property_description(property);
		item["readOnly"] = bool(property.usage & PROPERTY_USAGE_READ_ONLY);
		item["editable"] = !(property.usage & PROPERTY_USAGE_READ_ONLY);
		bool valid = false;
		const Variant value = selection.resource->get(property.name, &valid);
		item["valueAvailable"] = valid;
		Variant encoded_value;
		String encoding_error;
		if (valid && MCPVariantCodec::encode(value, encoded_value, &encoding_error) == OK) {
			item["encodable"] = true;
			item["value"] = encoded_value;
		} else {
			item["encodable"] = false;
			item["encodingError"] = valid ? encoding_error : "Property value is unavailable.";
		}
		properties.push_back(item);
	}

	Dictionary result;
	result["path"] = selection.resource_path;
	result["type"] = selection.resource->get_class();
	result["propertyCount"] = properties.size();
	result["properties"] = properties;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPResourceProvider::set_property(const Dictionary &p_arguments, const Dictionary &) {
	const Variant property_value = p_arguments.get("property", Variant());
	if (property_value.get_type() != Variant::STRING || String(property_value).is_empty() || !p_arguments.has("value")) {
		return MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "A non-empty property string and a value are required.");
	}

	ResourceSelection selection;
	const Dictionary load_error = _load_project_resource(p_arguments, project_root, resource_cache, selection);
	if (!load_error.is_empty()) {
		return load_error;
	}
	resource_cache[selection.resource_path] = selection.resource;
	const StringName property_name = String(property_value);
	PropertyInfo property;
	if (!_find_property(selection.resource, property_name, property) || !(property.usage & PROPERTY_USAGE_EDITOR)) {
		return MCPToolUtils::make_error_result("PROPERTY_NOT_FOUND", "Editable Resource property was not found: " + String(property_name));
	}
	if (property.usage & PROPERTY_USAGE_READ_ONLY) {
		return MCPToolUtils::make_error_result("PROPERTY_READ_ONLY", "Resource property is read-only: " + String(property_name));
	}

	Variant decoded_value;
	String codec_error;
	if (MCPVariantCodec::decode(p_arguments["value"], decoded_value, &codec_error) != OK) {
		return MCPToolUtils::make_error_result("INVALID_VALUE", codec_error);
	}
	bool value_valid = false;
	const Variant old_value = selection.resource->get(property_name, &value_valid);
	if (!value_valid) {
		return MCPToolUtils::make_error_result("PROPERTY_GET_FAILED", "Resource property value is unavailable: " + String(property_name));
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	if (undo_redo) {
		undo_redo->create_action("Set Resource Property", UndoRedo::MERGE_DISABLE, selection.resource.ptr());
		undo_redo->add_do_property(selection.resource.ptr(), property_name, decoded_value);
		undo_redo->add_undo_property(selection.resource.ptr(), property_name, old_value);
		undo_redo->commit_action();
	} else {
		selection.resource->set(property_name, decoded_value, &value_valid);
		if (!value_valid) {
			return MCPToolUtils::make_error_result("PROPERTY_SET_FAILED", "Resource property could not be set: " + String(property_name));
		}
	}
	if (EditorNode::get_singleton() && EditorInterface::get_singleton()) {
		EditorInterface::get_singleton()->inspect_object(selection.resource.ptr());
	}

	const Variant effective_value = selection.resource->get(property_name, &value_valid);
	if (!value_valid) {
		return MCPToolUtils::make_error_result("PROPERTY_SET_FAILED", "Resource property could not be read after modification: " + String(property_name));
	}
	Dictionary result;
	result["path"] = selection.resource_path;
	result["property"] = String(property_name);
	result["changed"] = old_value != effective_value;
	result["saved"] = false;
	Variant encoded_old_value;
	if (MCPVariantCodec::encode(old_value, encoded_old_value, &codec_error) == OK) {
		result["oldValue"] = encoded_old_value;
	}
	Variant encoded_value;
	if (MCPVariantCodec::encode(effective_value, encoded_value, &codec_error) == OK) {
		result["value"] = encoded_value;
	} else {
		result["value"] = p_arguments["value"];
		result["valueEncodingError"] = codec_error;
	}
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPResourceProvider::save(const Dictionary &p_arguments, const Dictionary &) {
	ResourceSelection selection;
	const Dictionary load_error = _load_project_resource(p_arguments, project_root, resource_cache, selection);
	if (!load_error.is_empty()) {
		return load_error;
	}
	resource_cache[selection.resource_path] = selection.resource;
	const String save_path = project_root.is_empty() ? selection.resource_path : selection.absolute_path;
	const Error error = ResourceSaver::save(selection.resource, save_path);
	if (error != OK) {
		return MCPToolUtils::make_error_result("SAVE_FAILED", error_names[error]);
	}
	if (project_root.is_empty() && EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->update_file(selection.resource_path);
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	if (undo_redo) {
		const int history_id = undo_redo->get_history_id_for_object(selection.resource.ptr());
		if (history_id != EditorUndoRedoManager::INVALID_HISTORY) {
			undo_redo->set_history_as_saved(history_id);
		}
	}

	Dictionary result;
	result["path"] = selection.resource_path;
	result["type"] = selection.resource->get_class();
	result["saved"] = true;
	return MCPToolUtils::make_success_result(result);
}

Dictionary MCPResourceProvider::import_options(const Dictionary &p_arguments, const Dictionary &) {
	return import_service.get_options(p_arguments);
}

Dictionary MCPResourceProvider::import_resource(const Dictionary &p_arguments, const Dictionary &) {
	return import_service.import_resource(p_arguments);
}
