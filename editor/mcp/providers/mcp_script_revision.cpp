/**************************************************************************/
/*  mcp_script_revision.cpp                                               */
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

#include "mcp_script_revision.h"

#include "mcp_tool_utils.h"

namespace MCPScriptRevision {

static Error _fail(const String &p_message, String *r_error, Error p_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return p_error;
}

Error validate(const String &p_text, uint32_t p_revision, const Variant &p_expected_revision,
		const Variant &p_expected_sha256, Dictionary &r_current_state, String *r_error) {
	if (r_error) {
		*r_error = String();
	}
	const String current_sha256 = p_text.sha256_text();
	r_current_state = Dictionary();
	r_current_state["currentRevision"] = int64_t(p_revision);
	r_current_state["currentSha256"] = current_sha256;

	const bool has_expected_revision = p_expected_revision.get_type() != Variant::NIL;
	const bool has_expected_sha256 = p_expected_sha256.get_type() != Variant::NIL;
	if (!has_expected_revision && !has_expected_sha256) {
		return _fail("expected_revision or expected_sha256 is required.", r_error, ERR_INVALID_PARAMETER);
	}

	bool stale = false;
	if (has_expected_revision) {
		int64_t expected_revision = 0;
		if (!MCPToolUtils::try_get_json_integer(p_expected_revision, INT64_MIN, INT64_MAX, expected_revision)) {
			return _fail("expected_revision must be an integer.", r_error, ERR_INVALID_PARAMETER);
		}
		if (expected_revision < 0 || expected_revision > 0xFFFFFFFFLL) {
			return _fail("expected_revision is outside the uint32 range.", r_error, ERR_PARAMETER_RANGE_ERROR);
		}
		r_current_state["expectedRevision"] = expected_revision;
		stale = uint32_t(expected_revision) != p_revision;
	}

	if (has_expected_sha256) {
		if (p_expected_sha256.get_type() != Variant::STRING) {
			return _fail("expected_sha256 must be a string.", r_error, ERR_INVALID_PARAMETER);
		}
		const String expected_sha256 = String(p_expected_sha256).to_lower();
		if (expected_sha256.length() != 64 || !expected_sha256.is_valid_hex_number(false)) {
			return _fail("expected_sha256 must contain 64 hexadecimal characters.", r_error, ERR_INVALID_PARAMETER);
		}
		r_current_state["expectedSha256"] = expected_sha256;
		stale = stale || expected_sha256 != current_sha256;
	}

	if (stale) {
		return _fail("The script buffer changed after the expected revision was read.", r_error, ERR_BUSY);
	}
	return OK;
}

} // namespace MCPScriptRevision
