/**************************************************************************/
/*  mcp_tool_registry.h                                                   */
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

#pragma once

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/type_info.h"

struct MCPToolCallContext {
	Variant request_id;
	String protocol_version;
	Dictionary session;
	Dictionary project;

	Dictionary to_dictionary() const;
};

class MCPToolRegistry {
public:
	enum CallStatus {
		CALL_OK,
		CALL_TOOL_NOT_FOUND,
		CALL_TOOL_UNAVAILABLE,
		CALL_HANDLER_FAILED,
		CALL_INVALID_RESULT,
	};

	struct CallResult {
		CallStatus status = CALL_OK;
		Dictionary result;
		String message;
	};

	Error register_tool(const Dictionary &p_definition, const Callable &p_handler, Object *p_owner = nullptr, String *r_error = nullptr);
	bool unregister_tool(const StringName &p_name);
	int unregister_tools_for_owner(Object *p_owner);
	int unregister_tools_for_owner(ObjectID p_owner_id);

	bool has_tool(const StringName &p_name) const;
	Array get_tool_definitions() const;
	PackedStringArray get_tool_names() const;
	CallResult call_tool(const StringName &p_name, const Dictionary &p_arguments, const MCPToolCallContext &p_context) const;

private:
	struct ToolEntry {
		Dictionary definition;
		Callable handler;
		ObjectID owner_id;
	};

	HashMap<StringName, ToolEntry> tools;
	Vector<StringName> registration_order;
	mutable Array definition_cache;
	mutable bool definition_cache_valid = false;

	void _invalidate_definition_cache();
	const Array &_get_cached_tool_definitions() const;
};

VARIANT_ENUM_CAST(MCPToolRegistry::CallStatus);
