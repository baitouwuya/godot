/**************************************************************************/
/*  mcp_runtime_observation_service.cpp                                  */
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

#include "mcp_runtime_observation_service.h"

#include "core/variant/variant.h"

MCPRuntimeObservationService::MCPRuntimeObservationService(MCPRuntimeDebuggerGateway *p_runtime_gateway) {
	runtime_gateway = p_runtime_gateway;
}

Dictionary MCPRuntimeObservationService::_request(const Dictionary &p_arguments, const String &p_operation,
		const Dictionary &p_payload) const {
	return runtime_gateway->request(p_arguments, "mcp_observation", p_operation, p_payload);
}

Dictionary MCPRuntimeObservationService::get_screenshot(const Dictionary &p_arguments) const {
	Dictionary payload;
	if (p_arguments.has("name")) {
		payload["name"] = p_arguments["name"];
	}
	if (p_arguments.has("crop")) {
		payload["crop"] = p_arguments["crop"];
	}
	return _request(p_arguments, "get_screenshot", payload);
}

Dictionary MCPRuntimeObservationService::get_viewport_summary(const Dictionary &p_arguments) const {
	Dictionary payload;
	if (p_arguments.has("includeCounts")) {
		payload["includeCounts"] = p_arguments["includeCounts"];
	}
	return _request(p_arguments, "get_viewport_summary", payload);
}

Dictionary MCPRuntimeObservationService::query_nodes(const Dictionary &p_arguments) const {
	Dictionary payload;
	if (p_arguments.has("filter")) {
		payload["filter"] = p_arguments["filter"];
	}
	if (p_arguments.has("maxResults")) {
		payload["maxResults"] = p_arguments["maxResults"];
	}
	return _request(p_arguments, "query_nodes", payload);
}

Dictionary MCPRuntimeObservationService::get_interactables(const Dictionary &p_arguments) const {
	Dictionary payload;
	if (p_arguments.has("filter")) {
		payload["filter"] = p_arguments["filter"];
	}
	if (p_arguments.has("maxResults")) {
		payload["maxResults"] = p_arguments["maxResults"];
	}
	return _request(p_arguments, "get_interactables", payload);
}

Dictionary MCPRuntimeObservationService::get_node_snapshot(const Dictionary &p_arguments) const {
	Dictionary payload;
	payload["selector"] = p_arguments.get("selector", Dictionary());
	return _request(p_arguments, "get_node_snapshot", payload);
}
