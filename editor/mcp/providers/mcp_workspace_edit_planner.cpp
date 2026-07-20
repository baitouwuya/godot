/**************************************************************************/
/*  mcp_workspace_edit_planner.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
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

#include "mcp_workspace_edit_planner.h"

#include "core/math/math_funcs.h"
#include "core/string/string_builder.h"
#include "modules/gdscript/language_server/gdscript_analysis_session.h"

namespace {

struct OffsetEdit {
	int start = 0;
	int end = 0;
	int order = 0;
	String text;
};

struct OffsetEditComparator {
	bool operator()(const OffsetEdit &p_left, const OffsetEdit &p_right) const {
		if (p_left.start != p_right.start) {
			return p_left.start > p_right.start;
		}
		if (p_left.end != p_right.end) {
			return p_left.end > p_right.end;
		}
		return p_left.order > p_right.order;
	}
};

static Error _fail(const String &p_message, String *r_error) {
	if (r_error) {
		*r_error = p_message;
	}
	return ERR_INVALID_DATA;
}

static bool _read_nonnegative_int(const Variant &p_value, int &r_value) {
	if (p_value.get_type() == Variant::INT) {
		const int64_t value = p_value;
		if (value < 0 || value > INT32_MAX) {
			return false;
		}
		r_value = int(value);
		return true;
	}
	if (p_value.get_type() == Variant::FLOAT) {
		const double value = p_value;
		if (!Math::is_finite(value) || Math::floor(value) != value || value < 0.0 || value > double(INT32_MAX)) {
			return false;
		}
		r_value = int(value);
		return true;
	}
	return false;
}

static bool _read_position(const Variant &p_value, LSP::Position &r_position) {
	if (p_value.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary value = p_value;
	int line = 0;
	int character = 0;
	if (!_read_nonnegative_int(value.get("line", Variant()), line) ||
			!_read_nonnegative_int(value.get("character", Variant()), character)) {
		return false;
	}
	r_position = LSP::Position(line, character);
	return true;
}

} // namespace

Error MCPWorkspaceEditPlanner::apply(const String &p_text, const Array &p_edits, String &r_text, String *r_error) {
	r_text = p_text;
	if (r_error) {
		*r_error = String();
	}
	if (p_edits.size() > 4096) {
		return _fail("A document cannot contain more than 4096 text edits.", r_error);
	}

	Vector<OffsetEdit> edits;
	edits.reserve(p_edits.size());
	for (int i = 0; i < p_edits.size(); i++) {
		if (p_edits[i].get_type() != Variant::DICTIONARY) {
			return _fail(vformat("edits[%d] must be an object.", i), r_error);
		}
		const Dictionary edit = p_edits[i];
		const Variant range_value = edit.get("range", Variant());
		const Variant text_value = edit.get("newText", Variant());
		if (range_value.get_type() != Variant::DICTIONARY || text_value.get_type() != Variant::STRING) {
			return _fail(vformat("edits[%d] requires a range and string newText.", i), r_error);
		}
		const Dictionary range = range_value;
		LSP::Position start_position;
		LSP::Position end_position;
		if (!_read_position(range.get("start", Variant()), start_position) || !_read_position(range.get("end", Variant()), end_position)) {
			return _fail(vformat("edits[%d] contains an invalid UTF-16 range.", i), r_error);
		}
		const int start = GDScriptAnalysisSession::position_to_offset(p_text, start_position);
		const int end = GDScriptAnalysisSession::position_to_offset(p_text, end_position);
		if (start > end || !(GDScriptAnalysisSession::offset_to_position(p_text, start) == start_position) ||
				!(GDScriptAnalysisSession::offset_to_position(p_text, end) == end_position)) {
			return _fail(vformat("edits[%d] range is outside the document or splits a UTF-16 surrogate pair.", i), r_error);
		}
		OffsetEdit offset_edit;
		offset_edit.start = start;
		offset_edit.end = end;
		offset_edit.order = i;
		offset_edit.text = text_value;
		edits.push_back(offset_edit);
	}

	edits.sort_custom<OffsetEditComparator>();
	int next_start = p_text.length();
	const OffsetEdit *previous_edit = nullptr;
	for (const OffsetEdit &edit : edits) {
		const bool same_position_inserts = previous_edit && edit.start == previous_edit->start &&
				edit.start == edit.end && previous_edit->start == previous_edit->end;
		if (edit.end > next_start || (previous_edit && edit.start == previous_edit->start && !same_position_inserts)) {
			return _fail("Workspace text edits overlap.", r_error);
		}
		next_start = edit.start;
		previous_edit = &edit;
	}

	StringBuilder builder;
	int source_offset = 0;
	for (int i = edits.size() - 1; i >= 0; i--) {
		const OffsetEdit &edit = edits[i];
		builder.append(p_text.substr(source_offset, edit.start - source_offset));
		builder.append(edit.text);
		source_offset = edit.end;
	}
	builder.append(p_text.substr(source_offset));
	r_text = builder.as_string();
	return OK;
}
