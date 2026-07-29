/**************************************************************************/
/*  mcp_script_buffer_service.cpp                                        */
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

#include "mcp_script_buffer_service.h"

#include "mcp_scene_utils.h"
#include "mcp_script_buffer.h"
#include "mcp_script_path_resolver.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/main/node.h"

namespace {

static MCPScriptBufferService::OperationResult _failure(Error p_error, const String &p_error_code, const String &p_message) {
	MCPScriptBufferService::OperationResult result;
	result.error = p_error;
	result.error_code = p_error_code;
	result.message = p_message;
	return result;
}

static String _buffer_error_code(Error p_error) {
	switch (p_error) {
		case ERR_FILE_NOT_FOUND:
			return "SCRIPT_NOT_FOUND";
		case ERR_INVALID_PARAMETER:
		case ERR_PARAMETER_RANGE_ERROR:
			return "INVALID_PATH";
		case ERR_UNCONFIGURED:
			return "EDITOR_UNAVAILABLE";
		default:
			return "BUFFER_UNAVAILABLE";
	}
}

} // namespace

MCPScriptBufferService::MCPScriptBufferService(const String &p_project_root) :
		project_root(p_project_root) {
}

MCPScriptBufferService::OperationResult MCPScriptBufferService::create_external(const String &p_path, const String &p_text, String &r_resource_path) const {
	r_resource_path = String();
	String absolute_path;
	String path_error;
	Error path_result = OK;
	if (project_root.is_empty()) {
		path_result = MCPScriptPathResolver::normalize_external(p_path, r_resource_path, absolute_path, &path_error);
	} else {
		path_result = MCPScriptPathResolver::normalize_external_for_root(p_path, project_root, r_resource_path, absolute_path, &path_error);
	}
	if (path_result != OK) {
		return _failure(path_result, "INVALID_PATH", path_error);
	}
	if (FileAccess::exists(absolute_path)) {
		return _failure(ERR_ALREADY_EXISTS, "SCRIPT_EXISTS", "Script already exists: " + r_resource_path);
	}

	const String parent_directory = absolute_path.get_base_dir();
	if (!DirAccess::dir_exists_absolute(parent_directory)) {
		const Error directory_error = DirAccess::make_dir_recursive_absolute(parent_directory);
		if (directory_error != OK) {
			return _failure(directory_error, "DIRECTORY_CREATE_FAILED", "Could not create the script parent directory.");
		}
	}

	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE, &open_error);
	if (file.is_null() || open_error != OK) {
		return _failure(open_error == OK ? ERR_CANT_OPEN : open_error, "FILE_OPEN_FAILED", "Could not create script: " + r_resource_path);
	}
	if (!file->store_string(p_text)) {
		file.unref();
		DirAccess::remove_absolute(absolute_path);
		return _failure(ERR_FILE_CANT_WRITE, "FILE_WRITE_FAILED", "Could not write script: " + r_resource_path);
	}
	file->flush();
	file.unref();
	if (project_root.is_empty() && EditorFileSystem::get_singleton()) {
		EditorFileSystem::get_singleton()->update_file(r_resource_path);
	}
	return OperationResult();
}

MCPScriptBufferService::OperationResult MCPScriptBufferService::open_path(const String &p_path, MCPScriptBuffer &r_buffer) const {
	String error;
	const Error open_error = MCPScriptBuffer::open(p_path, r_buffer, &error);
	return open_error == OK ? OperationResult() : _failure(open_error, _buffer_error_code(open_error), error);
}

MCPScriptBufferService::OperationResult MCPScriptBufferService::open_selected(const Dictionary &p_arguments, MCPScriptBuffer &r_buffer) const {
	const Variant path_value = p_arguments.get("path", Variant());
	const Variant node_path_value = p_arguments.get("nodePath", Variant());
	const bool has_path = path_value.get_type() == Variant::STRING && !String(path_value).is_empty();
	const bool has_node_path = node_path_value.get_type() == Variant::STRING && !String(node_path_value).is_empty();
	if (has_path == has_node_path) {
		return _failure(ERR_INVALID_PARAMETER, "INVALID_ARGUMENTS", "Exactly one non-empty path or nodePath is required.");
	}
	if (has_path) {
		return open_path(path_value, r_buffer);
	}

	Node *scene_root = MCPSceneUtils::get_edited_scene_root();
	if (!scene_root) {
		return _failure(ERR_UNCONFIGURED, "NO_SCENE", "No edited scene is open.");
	}
	Node *node = MCPSceneUtils::find_node(scene_root, node_path_value);
	if (!node) {
		return _failure(ERR_DOES_NOT_EXIST, "NODE_NOT_FOUND", "Node was not found: " + String(node_path_value));
	}
	const Ref<Script> script = node->get_script();
	if (script.is_null()) {
		return _failure(ERR_FILE_NOT_FOUND, "SCRIPT_NOT_FOUND", "The selected node does not have an attached script.");
	}
	String error;
	const Error open_error = MCPScriptBuffer::open(script, r_buffer, &error);
	return open_error == OK ? OperationResult() : _failure(open_error, _buffer_error_code(open_error), error);
}
