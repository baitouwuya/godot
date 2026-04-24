/**************************************************************************/
/*  cli_latest_log_runner.cpp                                             */
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

#include "cli_latest_log_runner.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/hashfuncs.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_REGEX_ENABLED
#include "modules/regex/regex.h"
#endif

namespace {

struct NumericSlotSummary {
	int count = 0;
	double min = 0.0;
	double max = 0.0;
	double last = 0.0;

	void push(double p_value) {
		if (count == 0) {
			min = p_value;
			max = p_value;
		} else {
			min = MIN(min, p_value);
			max = MAX(max, p_value);
		}
		last = p_value;
		count++;
	}

	Dictionary to_dictionary(int p_index) const {
		Dictionary result;
		result["slot"] = vformat("slot%d", p_index);
		result["count"] = count;
		result["min"] = min;
		result["max"] = max;
		result["last"] = last;
		return result;
	}
};

struct CallsiteInfo {
	bool valid = false;
	String function;
	String file;
	int line = 0;

	String to_anchor() const {
		if (!valid) {
			return "<none>";
		}
		return function + "|" + file + "|" + String::num_int64(line);
	}

	Dictionary to_dictionary() const {
		Dictionary result;
		result["function"] = function;
		result["file"] = file;
		result["line"] = line;
		return result;
	}
};

struct StackFrameInfo {
	int index = 0;
	String function;
	String file;
	int line = 0;

	String to_anchor() const {
		return String::num_int64(index) + "|" + function + "|" + file + "|" + String::num_int64(line);
	}

	Dictionary to_dictionary() const {
		Dictionary result;
		result["index"] = index;
		result["function"] = function;
		result["file"] = file;
		result["line"] = line;
		return result;
	}
};

struct PlainLineUnit {
	int line = 0;
	String raw_text;
	String normalized_text;
	String template_id;
	Vector<double> numeric_values;
};

struct IssueBlockUnit {
	int start_line = 0;
	int end_line = 0;
	String severity_key;
	String severity_label;
	String headline;
	String continuation_text;
	String template_text;
	String template_id;
	Vector<double> numeric_values;
	CallsiteInfo callsite;
	String stack_header;
	Vector<StackFrameInfo> frames;
	String stack_fingerprint;
	String fold_signature;
};

struct UnitOccurrence {
	bool is_issue = false;
	PlainLineUnit plain;
	IssueBlockUnit issue;

	String get_kind() const {
		return is_issue ? "issueBlock" : "line";
	}

	String get_template_id() const {
		return is_issue ? issue.template_id : plain.template_id;
	}

	String get_template_text() const {
		return is_issue ? issue.template_text : plain.normalized_text;
	}

	int get_occurrence_line() const {
		return is_issue ? issue.start_line : plain.line;
	}

	String get_fold_signature() const {
		return is_issue ? issue.fold_signature : plain.template_id;
	}

	const Vector<double> &get_numeric_values() const {
		return is_issue ? issue.numeric_values : plain.numeric_values;
	}
};

struct FoldedUnit {
	UnitOccurrence unit;
	int repeat = 0;
	int first_line = 0;
	int last_line = 0;
	Vector<NumericSlotSummary> numeric_slots;

	void _push_numeric_values(const Vector<double> &p_values) {
		if (p_values.size() > numeric_slots.size()) {
			numeric_slots.resize(p_values.size());
		}
		for (int i = 0; i < p_values.size(); i++) {
			numeric_slots.write[i].push(p_values[i]);
		}
	}

	void initialize(const UnitOccurrence &p_occurrence) {
		unit = p_occurrence;
		repeat = 1;
		first_line = p_occurrence.get_occurrence_line();
		last_line = first_line;
		_push_numeric_values(p_occurrence.get_numeric_values());
	}

	void append(const UnitOccurrence &p_occurrence) {
		repeat++;
		last_line = p_occurrence.get_occurrence_line();
		_push_numeric_values(p_occurrence.get_numeric_values());
	}

	Array numeric_slots_to_array() const {
		Array result;
		for (int i = 0; i < numeric_slots.size(); i++) {
			if (numeric_slots[i].count == 0) {
				continue;
			}
			result.push_back(numeric_slots[i].to_dictionary(i));
		}
		return result;
	}

	Dictionary to_fallback_dictionary() const {
		Dictionary result;
		result["templateId"] = unit.get_template_id();
		result["text"] = unit.is_issue ? unit.issue.headline : unit.plain.normalized_text;
		result["repeat"] = repeat;
		result["firstLine"] = first_line;
		result["lastLine"] = last_line;
		result["numericSlots"] = numeric_slots_to_array();
		return result;
	}
};

struct TemplateAggregate {
	String template_id;
	String kind;
	String template_text;
	int count = 0;
	int first_occurrence_line = 0;
	int last_occurrence_line = 0;
	Vector<NumericSlotSummary> numeric_slots;

	void _push_numeric_values(const Vector<double> &p_values) {
		if (p_values.size() > numeric_slots.size()) {
			numeric_slots.resize(p_values.size());
		}
		for (int i = 0; i < p_values.size(); i++) {
			numeric_slots.write[i].push(p_values[i]);
		}
	}

	void append(const UnitOccurrence &p_occurrence) {
		if (count == 0) {
			template_id = p_occurrence.get_template_id();
			kind = p_occurrence.get_kind();
			template_text = p_occurrence.get_template_text();
			first_occurrence_line = p_occurrence.get_occurrence_line();
		}
		count++;
		last_occurrence_line = p_occurrence.get_occurrence_line();
		_push_numeric_values(p_occurrence.get_numeric_values());
	}

	Dictionary to_dictionary() const {
		Dictionary result;
		result["templateId"] = template_id;
		result["kind"] = kind;
		result["template"] = template_text;
		result["count"] = count;
		result["firstOccurrenceLine"] = first_occurrence_line;
		result["lastOccurrenceLine"] = last_occurrence_line;

		Array numeric_slots_array;
		for (int i = 0; i < numeric_slots.size(); i++) {
			if (numeric_slots[i].count == 0) {
				continue;
			}
			numeric_slots_array.push_back(numeric_slots[i].to_dictionary(i));
		}
		result["numericSlots"] = numeric_slots_array;
		return result;
	}
};

struct KeywordHit {
	String token;
	int count = 0;

	Dictionary to_dictionary() const {
		Dictionary result;
		result["token"] = token;
		result["count"] = count;
		return result;
	}
};

struct AnalysisSummary {
	String path;
	String base_path;
	CLILatestLogRunner::Format format = CLILatestLogRunner::FORMAT_TEXT;
	int window_lines_requested = 0;
	int window_lines_analyzed = 0;
	int total_lines = 0;
	bool used_full_file_fallback = false;
	int folded_repeat_count = 0;
	int issue_block_count = 0;
	int error_count = 0;
	int warning_count = 0;
	int script_error_count = 0;
	int shader_error_count = 0;
	Vector<FoldedUnit> folded_units;
	Vector<TemplateAggregate> top_templates;
	Vector<KeywordHit> keyword_hits;
};

struct Replacement {
	int start = 0;
	int end = 0;
	String replacement;
	bool numeric = false;
	double numeric_value = 0.0;
};

struct NormalizedText {
	String text;
	Vector<double> numeric_values;
};

struct ReplacementStartComparator {
	bool operator()(const Replacement &p_a, const Replacement &p_b) const {
		return p_a.start < p_b.start;
	}
};

struct TemplateSortComparator {
	bool operator()(const TemplateAggregate &p_a, const TemplateAggregate &p_b) const {
		if (p_a.count == p_b.count) {
			if (p_a.last_occurrence_line == p_b.last_occurrence_line) {
				return p_a.template_id < p_b.template_id;
			}
			return p_a.last_occurrence_line > p_b.last_occurrence_line;
		}
		return p_a.count > p_b.count;
	}
};

struct KeywordSortComparator {
	bool operator()(const KeywordHit &p_a, const KeywordHit &p_b) const {
		if (p_a.count == p_b.count) {
			return p_a.token < p_b.token;
		}
		return p_a.count > p_b.count;
	}
};

static String _format_repeat_count(int p_repeat) {
	return p_repeat > 1 ? vformat(" x%d", p_repeat) : String();
}

static Vector<String> _split_lines(const String &p_text) {
	Vector<String> lines;
	if (p_text.is_empty()) {
		return lines;
	}

	int from = 0;
	while (from <= p_text.length()) {
		const int next_break = p_text.find("\n", from);
		if (next_break == -1) {
			lines.push_back(p_text.substr(from));
			break;
		}
		lines.push_back(p_text.substr(from, next_break - from));
		from = next_break + 1;
	}

	return lines;
}

static String _join_lines(const Vector<String> &p_lines, const String &p_separator) {
	String result;
	for (int i = 0; i < p_lines.size(); i++) {
		if (i > 0) {
			result += p_separator;
		}
		result += p_lines[i];
	}
	return result;
}

static String _compact_multiline(const String &p_text) {
	return p_text.replace("\n", " | ");
}

static String _to_short_hash(const String &p_text) {
	const CharString utf8 = p_text.utf8();
	const char *data = utf8.get_data();
	const int length = utf8.length();
	const uint32_t hash = length > 0 ? hash_murmur3_buffer(data, length) : hash_murmur3_one_32(0);
	return String::num_uint64(hash, 16).pad_zeros(8);
}

static bool _is_ascii_alnum(char32_t p_char) {
	return (p_char >= 'a' && p_char <= 'z') || (p_char >= '0' && p_char <= '9');
}

static bool _is_pathish_numeric_neighbor(char32_t p_char) {
	return _is_ascii_alnum(p_char) || p_char == '_' || p_char == '.' || p_char == '/' || p_char == '\\' || p_char == ':';
}

static bool _is_ascii_digit_only(const String &p_token) {
	if (p_token.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_token.length(); i++) {
		const char32_t c = p_token[i];
		if (c < '0' || c > '9') {
			return false;
		}
	}
	return true;
}

static bool _is_stopword(const String &p_token) {
	static const char *stopwords[] = {
		"and",
		"are",
		"at",
		"backtrace",
		"call",
		"file",
		"first",
		"for",
		"frame",
		"frames",
		"from",
		"function",
		"godot",
		"hex",
		"int",
		"ip4",
		"into",
		"line",
		"lines",
		"log",
		"logs",
		"most",
		"num",
		"path",
		"recent",
		"shader",
		"script",
		"stack",
		"ts",
		"the",
		"this",
		"uuid",
		"warning",
		"with",
		"error",
	};

	for (uint32_t i = 0; i < sizeof(stopwords) / sizeof(stopwords[0]); i++) {
		if (p_token == stopwords[i]) {
			return true;
		}
	}
	return false;
}

static void _append_keyword_hits(HashMap<String, int> &r_hits, const String &p_text, int p_weight = 1) {
	const String lowered = p_text.to_lower();
	String current;

	for (int i = 0; i < lowered.length(); i++) {
		const char32_t c = lowered[i];
		if (_is_ascii_alnum(c)) {
			current += String::chr(c);
			continue;
		}

		if (current.length() >= 3 && !_is_ascii_digit_only(current) && !_is_stopword(current)) {
			int current_count = 0;
			if (HashMap<String, int>::ConstIterator E = r_hits.find(current)) {
				current_count = E->value;
			}
			r_hits[current] = current_count + p_weight;
		}
		current = String();
	}

	if (current.length() >= 3 && !_is_ascii_digit_only(current) && !_is_stopword(current)) {
		int current_count = 0;
		if (HashMap<String, int>::ConstIterator E = r_hits.find(current)) {
			current_count = E->value;
		}
		r_hits[current] = current_count + p_weight;
	}
}

#ifdef MODULE_REGEX_ENABLED
static Ref<RegEx> _make_detached_regex(const String &p_pattern) {
	Ref<RegEx> regex = RegEx::create_from_string(p_pattern, false);
	if (regex.is_valid()) {
		regex->detach_from_objectdb();
	}
	return regex;
}

static Ref<RegEx> _get_timestamp_regex() {
	static Ref<RegEx> regex = _make_detached_regex("\\b\\d{4}-\\d{2}-\\d{2}[T ]\\d{2}:\\d{2}:\\d{2}(?:[\\.,]\\d+)?\\b|\\b\\d{2}:\\d{2}:\\d{2}(?:[\\.,]\\d+)?\\b");
	return regex;
}

static Ref<RegEx> _get_uuid_regex() {
	static Ref<RegEx> regex = _make_detached_regex("\\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\b");
	return regex;
}

static Ref<RegEx> _get_ipv4_regex() {
	static Ref<RegEx> regex = _make_detached_regex("\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b");
	return regex;
}

static Ref<RegEx> _get_hex_regex() {
	static Ref<RegEx> regex = _make_detached_regex("\\b0x[0-9a-fA-F]{6,}\\b");
	return regex;
}

static Ref<RegEx> _get_long_int_regex() {
	static Ref<RegEx> regex = _make_detached_regex("[-+]?\\d{6,}");
	return regex;
}

static Ref<RegEx> _get_float_regex() {
	static Ref<RegEx> regex = _make_detached_regex("[-+]?(?:\\d+\\.\\d+(?:[eE][-+]?\\d+)?|\\d+[eE][-+]?\\d+)");
	return regex;
}

static Ref<RegEx> _get_int_regex() {
	static Ref<RegEx> regex = _make_detached_regex("[-+]?\\d+");
	return regex;
}

static Ref<RegEx> _get_callsite_regex() {
	static Ref<RegEx> regex = _make_detached_regex("^\\s+at:\\s+(.+) \\((.+):(\\d+)\\)\\s*$");
	return regex;
}

static Ref<RegEx> _get_stack_frame_regex() {
	static Ref<RegEx> regex = _make_detached_regex("^\\s*\\[(\\d+)\\]\\s+(.+) \\((.+):(\\d+)\\)\\s*$");
	return regex;
}
#endif

static bool _range_is_free(const Vector<uint8_t> &p_occupied, int p_start, int p_end) {
	for (int i = p_start; i < p_end; i++) {
		if (p_occupied[i] != 0) {
			return false;
		}
	}
	return true;
}

static void _mark_range(Vector<uint8_t> &r_occupied, int p_start, int p_end) {
	for (int i = p_start; i < p_end; i++) {
		r_occupied.write[i] = 1;
	}
}

static double _parse_numeric_value(const String &p_text) {
	return (p_text.contains(".") || p_text.contains("e") || p_text.contains("E")) ? p_text.to_float() : (double)p_text.to_int();
}

static bool _is_numeric_match_safe(const String &p_line, int p_start, int p_end) {
	if (p_start > 0 && _is_pathish_numeric_neighbor(p_line[p_start - 1])) {
		return false;
	}
	if (p_end < p_line.length() && _is_pathish_numeric_neighbor(p_line[p_end])) {
		return false;
	}
	return true;
}

#ifdef MODULE_REGEX_ENABLED
static void _collect_replacements(const Ref<RegEx> &p_regex, const String &p_line, const String &p_placeholder, bool p_numeric, bool p_require_numeric_boundaries, Vector<Replacement> &r_replacements, Vector<uint8_t> &r_occupied) {
	if (p_regex.is_null()) {
		return;
	}

	const TypedArray<RegExMatch> matches = p_regex->search_all(p_line);
	for (int i = 0; i < matches.size(); i++) {
		Ref<RegExMatch> match = matches[i];
		if (match.is_null()) {
			continue;
		}

		const int start = match->get_start(0);
		const int end = match->get_end(0);
		if (start < 0 || end <= start) {
			continue;
		}
		if (p_require_numeric_boundaries && !_is_numeric_match_safe(p_line, start, end)) {
			continue;
		}
		if (!_range_is_free(r_occupied, start, end)) {
			continue;
		}

		Replacement replacement;
		replacement.start = start;
		replacement.end = end;
		replacement.replacement = p_placeholder;
		replacement.numeric = p_numeric;
		if (p_numeric) {
			replacement.numeric_value = _parse_numeric_value(match->get_string(0));
		}

		_mark_range(r_occupied, start, end);
		r_replacements.push_back(replacement);
	}
}
#endif

static NormalizedText _normalize_text(const String &p_text) {
	NormalizedText result;
	result.text = p_text;

#ifdef MODULE_REGEX_ENABLED
	if (p_text.is_empty()) {
		return result;
	}

	Vector<uint8_t> occupied;
	occupied.resize(p_text.length());
	for (int i = 0; i < occupied.size(); i++) {
		occupied.write[i] = 0;
	}

	Vector<Replacement> replacements;
	_collect_replacements(_get_timestamp_regex(), p_text, "<ts>", false, false, replacements, occupied);
	_collect_replacements(_get_uuid_regex(), p_text, "<uuid>", false, false, replacements, occupied);
	_collect_replacements(_get_ipv4_regex(), p_text, "<ip4>", false, false, replacements, occupied);
	_collect_replacements(_get_hex_regex(), p_text, "<hex>", false, false, replacements, occupied);
	_collect_replacements(_get_long_int_regex(), p_text, "<int>", true, true, replacements, occupied);
	_collect_replacements(_get_float_regex(), p_text, "<num>", true, true, replacements, occupied);
	_collect_replacements(_get_int_regex(), p_text, "<int>", true, true, replacements, occupied);

	if (replacements.is_empty()) {
		return result;
	}

	replacements.sort_custom<ReplacementStartComparator>();

	String normalized;
	int cursor = 0;
	for (int i = 0; i < replacements.size(); i++) {
		const Replacement &replacement = replacements[i];
		if (replacement.start > cursor) {
			normalized += p_text.substr(cursor, replacement.start - cursor);
		}
		normalized += replacement.replacement;
		cursor = replacement.end;
		if (replacement.numeric) {
			result.numeric_values.push_back(replacement.numeric_value);
		}
	}
	if (cursor < p_text.length()) {
		normalized += p_text.substr(cursor);
	}

	result.text = normalized;
#endif

	return result;
}

static bool _parse_severity_line(const String &p_line, String &r_severity_key, String &r_severity_label, String &r_headline) {
	if (p_line.begins_with("ERROR:")) {
		r_severity_key = "error";
		r_severity_label = "ERROR";
		r_headline = p_line.substr(String("ERROR:").length()).strip_edges();
		return true;
	}
	if (p_line.begins_with("WARNING:")) {
		r_severity_key = "warning";
		r_severity_label = "WARNING";
		r_headline = p_line.substr(String("WARNING:").length()).strip_edges();
		return true;
	}
	if (p_line.begins_with("SCRIPT ERROR:")) {
		r_severity_key = "scriptError";
		r_severity_label = "SCRIPT ERROR";
		r_headline = p_line.substr(String("SCRIPT ERROR:").length()).strip_edges();
		return true;
	}
	if (p_line.begins_with("SHADER ERROR:")) {
		r_severity_key = "shaderError";
		r_severity_label = "SHADER ERROR";
		r_headline = p_line.substr(String("SHADER ERROR:").length()).strip_edges();
		return true;
	}
	return false;
}

static bool _is_indented_line(const String &p_line) {
	return !p_line.is_empty() && (p_line[0] == ' ' || p_line[0] == '\t');
}

static bool _parse_callsite(const String &p_line, CallsiteInfo &r_callsite) {
#ifdef MODULE_REGEX_ENABLED
	Ref<RegExMatch> match = _get_callsite_regex()->search(p_line);
	if (match.is_null()) {
		return false;
	}
	r_callsite.valid = true;
	r_callsite.function = match->get_string(1);
	r_callsite.file = match->get_string(2);
	r_callsite.line = match->get_string(3).to_int();
	return true;
#else
	return false;
#endif
}

static bool _is_backtrace_header(const String &p_line) {
	return p_line.strip_edges().ends_with("backtrace (most recent call first):");
}

static bool _parse_stack_frame(const String &p_line, StackFrameInfo &r_frame) {
#ifdef MODULE_REGEX_ENABLED
	Ref<RegExMatch> match = _get_stack_frame_regex()->search(p_line);
	if (match.is_null()) {
		return false;
	}
	r_frame.index = match->get_string(1).to_int();
	r_frame.function = match->get_string(2);
	r_frame.file = match->get_string(3);
	r_frame.line = match->get_string(4).to_int();
	return true;
#else
	return false;
#endif
}

static void _append_numeric_values(Vector<double> &r_to, const Vector<double> &p_from) {
	for (int i = 0; i < p_from.size(); i++) {
		r_to.push_back(p_from[i]);
	}
}

static String _build_issue_template_text(const String &p_severity_label, const String &p_headline, const Vector<String> &p_continuations) {
	String result = p_severity_label + ": " + p_headline;
	for (int i = 0; i < p_continuations.size(); i++) {
		result += "\n" + p_continuations[i];
	}
	return result;
}

static String _build_stack_anchor(const Vector<StackFrameInfo> &p_frames, int p_limit) {
	Vector<String> anchors;
	for (int i = 0; i < MIN(p_frames.size(), p_limit); i++) {
		anchors.push_back(p_frames[i].to_anchor());
	}
	return _join_lines(anchors, ";");
}

static void _accumulate_issue_count(AnalysisSummary &r_summary, const String &p_severity_key) {
	r_summary.issue_block_count++;
	if (p_severity_key == "error") {
		r_summary.error_count++;
	} else if (p_severity_key == "warning") {
		r_summary.warning_count++;
	} else if (p_severity_key == "scriptError") {
		r_summary.script_error_count++;
	} else if (p_severity_key == "shaderError") {
		r_summary.shader_error_count++;
	}
}

static void _parse_units(const Vector<String> &p_lines, int p_first_line_number, Vector<UnitOccurrence> &r_units, AnalysisSummary &r_summary) {
	for (int i = 0; i < p_lines.size();) {
		String severity_key;
		String severity_label;
		String headline;
		if (_parse_severity_line(p_lines[i], severity_key, severity_label, headline)) {
			IssueBlockUnit issue_unit;
			issue_unit.start_line = p_first_line_number + i;
			issue_unit.severity_key = severity_key;
			issue_unit.severity_label = severity_label;

			const NormalizedText normalized_headline = _normalize_text(headline);
			issue_unit.headline = normalized_headline.text;
			_append_numeric_values(issue_unit.numeric_values, normalized_headline.numeric_values);

			Vector<String> normalized_continuations;
			int j = i + 1;
			while (j < p_lines.size()) {
				String next_severity_key;
				String next_severity_label;
				String next_headline;
				if (_parse_severity_line(p_lines[j], next_severity_key, next_severity_label, next_headline)) {
					break;
				}

				CallsiteInfo callsite;
				if (_parse_callsite(p_lines[j], callsite)) {
					issue_unit.callsite = callsite;
					j++;
					continue;
				}

				if (_is_backtrace_header(p_lines[j])) {
					issue_unit.stack_header = p_lines[j].strip_edges();
					j++;
					continue;
				}

				StackFrameInfo frame;
				if (_parse_stack_frame(p_lines[j], frame)) {
					issue_unit.frames.push_back(frame);
					j++;
					continue;
				}

				if (_is_indented_line(p_lines[j])) {
					const NormalizedText normalized_continuation = _normalize_text(p_lines[j].strip_edges());
					if (!normalized_continuation.text.is_empty()) {
						normalized_continuations.push_back(normalized_continuation.text);
					}
					_append_numeric_values(issue_unit.numeric_values, normalized_continuation.numeric_values);
					j++;
					continue;
				}

				break;
			}

			issue_unit.end_line = p_first_line_number + j - 1;
			issue_unit.continuation_text = _join_lines(normalized_continuations, "\n");
			issue_unit.template_text = _build_issue_template_text(severity_label, issue_unit.headline, normalized_continuations);
			issue_unit.template_id = _to_short_hash(severity_key + "|" + issue_unit.template_text);
			const String stack_anchor = _build_stack_anchor(issue_unit.frames, 3);
			issue_unit.stack_fingerprint = _to_short_hash(severity_key + "|" + issue_unit.template_text + "|" + issue_unit.callsite.to_anchor() + "|" + stack_anchor);
			issue_unit.fold_signature = issue_unit.template_id + "|" + issue_unit.callsite.to_anchor() + "|" + stack_anchor;

			UnitOccurrence occurrence;
			occurrence.is_issue = true;
			occurrence.issue = issue_unit;
			r_units.push_back(occurrence);
			_accumulate_issue_count(r_summary, severity_key);
			i = j;
			continue;
		}

		if (p_lines[i].strip_edges().is_empty()) {
			i++;
			continue;
		}

		PlainLineUnit line_unit;
		line_unit.line = p_first_line_number + i;
		line_unit.raw_text = p_lines[i];
		const NormalizedText normalized_line = _normalize_text(line_unit.raw_text);
		line_unit.normalized_text = normalized_line.text;
		line_unit.numeric_values = normalized_line.numeric_values;
		line_unit.template_id = _to_short_hash("line|" + line_unit.normalized_text);

		UnitOccurrence occurrence;
		occurrence.is_issue = false;
		occurrence.plain = line_unit;
		r_units.push_back(occurrence);
		i++;
	}
}

static void _fold_units(const Vector<UnitOccurrence> &p_units, AnalysisSummary &r_summary) {
	for (int i = 0; i < p_units.size(); i++) {
		if (!r_summary.folded_units.is_empty() && r_summary.folded_units[r_summary.folded_units.size() - 1].unit.get_fold_signature() == p_units[i].get_fold_signature()) {
			r_summary.folded_units.write[r_summary.folded_units.size() - 1].append(p_units[i]);
			r_summary.folded_repeat_count++;
			continue;
		}

		FoldedUnit folded;
		folded.initialize(p_units[i]);
		r_summary.folded_units.push_back(folded);
	}
}

static void _build_template_aggregates(const Vector<UnitOccurrence> &p_units, AnalysisSummary &r_summary) {
	HashMap<String, int> index_by_key;

	for (int i = 0; i < p_units.size(); i++) {
		const String key = p_units[i].get_kind() + "|" + p_units[i].get_template_id();
		int index = -1;
		if (HashMap<String, int>::ConstIterator E = index_by_key.find(key)) {
			index = E->value;
		}
		if (index == -1) {
			index = r_summary.top_templates.size();
			index_by_key.insert(key, index);
			r_summary.top_templates.push_back(TemplateAggregate());
		}
		r_summary.top_templates.write[index].append(p_units[i]);
	}

	r_summary.top_templates.sort_custom<TemplateSortComparator>();
	if (r_summary.top_templates.size() > 5) {
		r_summary.top_templates.resize(5);
	}
}

static void _build_keywords(AnalysisSummary &r_summary) {
	HashMap<String, int> keyword_counts;

	if (r_summary.issue_block_count > 0) {
		for (int i = 0; i < r_summary.folded_units.size(); i++) {
			const FoldedUnit &folded = r_summary.folded_units[i];
			if (!folded.unit.is_issue) {
				continue;
			}

			String keyword_source = folded.unit.issue.template_text;
			if (folded.unit.issue.callsite.valid) {
				keyword_source += " " + folded.unit.issue.callsite.function + " " + folded.unit.issue.callsite.file;
			}
			for (int j = 0; j < folded.unit.issue.frames.size(); j++) {
				keyword_source += " " + folded.unit.issue.frames[j].function + " " + folded.unit.issue.frames[j].file;
			}
			_append_keyword_hits(keyword_counts, keyword_source, folded.repeat);
		}
	} else {
		const int preview_count = MIN(10, r_summary.folded_units.size());
		const int first_index = MAX(0, r_summary.folded_units.size() - preview_count);
		for (int i = first_index; i < r_summary.folded_units.size(); i++) {
			const FoldedUnit &folded = r_summary.folded_units[i];
			if (folded.unit.is_issue) {
				continue;
			}
			_append_keyword_hits(keyword_counts, folded.unit.plain.normalized_text, folded.repeat);
		}
	}

	for (const KeyValue<String, int> &E : keyword_counts) {
		KeywordHit hit;
		hit.token = E.key;
		hit.count = E.value;
		r_summary.keyword_hits.push_back(hit);
	}
	r_summary.keyword_hits.sort_custom<KeywordSortComparator>();
	if (r_summary.keyword_hits.size() > 10) {
		r_summary.keyword_hits.resize(10);
	}
}

static AnalysisSummary _analyze_lines(const Vector<String> &p_lines, int p_first_line_number, const String &p_log_path, const String &p_base_log_path, const CLILatestLogRunner::Options &p_options, int p_total_lines, bool p_used_full_file_fallback) {
	AnalysisSummary summary;
	summary.path = p_log_path;
	summary.base_path = p_base_log_path;
	summary.format = p_options.format;
	summary.window_lines_requested = p_options.lines;
	summary.window_lines_analyzed = p_lines.size();
	summary.total_lines = p_total_lines;
	summary.used_full_file_fallback = p_used_full_file_fallback;

	Vector<UnitOccurrence> units;
	_parse_units(p_lines, p_first_line_number, units, summary);
	_fold_units(units, summary);
	_build_template_aggregates(units, summary);
	_build_keywords(summary);
	return summary;
}

static Array _numeric_slots_to_array(const Vector<NumericSlotSummary> &p_numeric_slots) {
	Array result;
	for (int i = 0; i < p_numeric_slots.size(); i++) {
		if (p_numeric_slots[i].count == 0) {
			continue;
		}
		result.push_back(p_numeric_slots[i].to_dictionary(i));
	}
	return result;
}

static Dictionary _issue_block_to_dictionary(const FoldedUnit &p_folded) {
	Dictionary result;
	result["templateId"] = p_folded.unit.issue.template_id;
	result["severity"] = p_folded.unit.issue.severity_key;
	result["headline"] = p_folded.unit.issue.headline;
	result["repeat"] = p_folded.repeat;
	result["callsite"] = p_folded.unit.issue.callsite.to_dictionary();
	result["stackFingerprint"] = p_folded.unit.issue.stack_fingerprint;

	Dictionary stack_summary;
	stack_summary["header"] = p_folded.unit.issue.stack_header;
	Array frames;
	const int frame_count = MIN(10, p_folded.unit.issue.frames.size());
	for (int i = 0; i < frame_count; i++) {
		frames.push_back(p_folded.unit.issue.frames[i].to_dictionary());
	}
	stack_summary["frames"] = frames;
	result["stackSummary"] = stack_summary;
	return result;
}

static Dictionary _build_summary_dictionary(const AnalysisSummary &p_summary) {
	Dictionary summary;
	summary["path"] = p_summary.path;
	summary["basePath"] = p_summary.base_path;
	summary["format"] = CLILatestLogRunner::get_format_name(p_summary.format);

	Dictionary analysis;
	analysis["windowLinesRequested"] = p_summary.window_lines_requested;
	analysis["windowLinesAnalyzed"] = p_summary.window_lines_analyzed;
	analysis["totalLines"] = p_summary.total_lines;
	analysis["usedFullFileFallback"] = p_summary.used_full_file_fallback;
	analysis["foldedRepeatCount"] = p_summary.folded_repeat_count;
	analysis["issueBlockCount"] = p_summary.issue_block_count;
	summary["analysis"] = analysis;

	Dictionary counts;
	counts["error"] = p_summary.error_count;
	counts["warning"] = p_summary.warning_count;
	counts["scriptError"] = p_summary.script_error_count;
	counts["shaderError"] = p_summary.shader_error_count;
	summary["counts"] = counts;

	Array keyword_hits;
	for (int i = 0; i < p_summary.keyword_hits.size(); i++) {
		keyword_hits.push_back(p_summary.keyword_hits[i].to_dictionary());
	}
	summary["keywordHits"] = keyword_hits;

	Array top_templates;
	for (int i = 0; i < p_summary.top_templates.size(); i++) {
		top_templates.push_back(p_summary.top_templates[i].to_dictionary());
	}
	summary["topTemplates"] = top_templates;

	Array recent_issue_blocks;
	int emitted_issues = 0;
	for (int i = p_summary.folded_units.size() - 1; i >= 0 && emitted_issues < 3; i--) {
		if (!p_summary.folded_units[i].unit.is_issue) {
			continue;
		}
		recent_issue_blocks.push_back(_issue_block_to_dictionary(p_summary.folded_units[i]));
		emitted_issues++;
	}
	summary["recentIssueBlocks"] = recent_issue_blocks;

	if (p_summary.issue_block_count == 0) {
		Array fallback_tail;
		const int preview_count = MIN(10, p_summary.folded_units.size());
		const int first_index = MAX(0, p_summary.folded_units.size() - preview_count);
		for (int i = first_index; i < p_summary.folded_units.size(); i++) {
			fallback_tail.push_back(p_summary.folded_units[i].to_fallback_dictionary());
		}
		summary["fallbackTail"] = fallback_tail;
	}

	return summary;
}

static String _build_keywords_line(const AnalysisSummary &p_summary) {
	if (p_summary.keyword_hits.is_empty()) {
		return "[latest-log] keywords -";
	}

	Vector<String> parts;
	for (int i = 0; i < p_summary.keyword_hits.size(); i++) {
		parts.push_back(vformat("%s(%d)", p_summary.keyword_hits[i].token, p_summary.keyword_hits[i].count));
	}
	return "[latest-log] keywords " + _join_lines(parts, ", ");
}

static String _format_numeric_slots_text(const Vector<NumericSlotSummary> &p_numeric_slots) {
	Vector<String> parts;
	for (int i = 0; i < p_numeric_slots.size(); i++) {
		if (p_numeric_slots[i].count == 0) {
			continue;
		}
		parts.push_back(vformat("slot%d[count=%d min=%s max=%s last=%s]",
				i,
				p_numeric_slots[i].count,
				String::num(p_numeric_slots[i].min),
				String::num(p_numeric_slots[i].max),
				String::num(p_numeric_slots[i].last)));
	}
	return parts.is_empty() ? String() : _join_lines(parts, ", ");
}

static String _render_text_summary(const AnalysisSummary &p_summary) {
	String output;
	output += "[latest-log] path=" + p_summary.path + "\n";
	output += vformat("[latest-log] format=%s analyzed=%d/%d fallback=%s\n",
			CLILatestLogRunner::get_format_name(p_summary.format),
			p_summary.window_lines_analyzed,
			p_summary.total_lines,
			p_summary.used_full_file_fallback ? "true" : "false");
	output += vformat("[latest-log] counts error=%d warning=%d scriptError=%d shaderError=%d repeats=%d\n",
			p_summary.error_count,
			p_summary.warning_count,
			p_summary.script_error_count,
			p_summary.shader_error_count,
			p_summary.folded_repeat_count);
	output += _build_keywords_line(p_summary) + "\n";
	output += vformat("[latest-log] top-templates %d\n", p_summary.top_templates.size());

	for (int i = 0; i < p_summary.top_templates.size(); i++) {
		const TemplateAggregate &templ = p_summary.top_templates[i];
		output += vformat("  - [%s x%d] id=%s %s\n",
				templ.kind,
				templ.count,
				templ.template_id,
				_compact_multiline(templ.template_text));
		const String numeric_slots_text = _format_numeric_slots_text(templ.numeric_slots);
		if (!numeric_slots_text.is_empty()) {
			output += "    slots: " + numeric_slots_text + "\n";
		}
	}

	if (p_summary.issue_block_count > 0) {
		output += "Recent issue blocks:\n";
		int emitted_issues = 0;
		for (int i = p_summary.folded_units.size() - 1; i >= 0 && emitted_issues < 3; i--) {
			const FoldedUnit &folded = p_summary.folded_units[i];
			if (!folded.unit.is_issue) {
				continue;
			}

			output += vformat("  - [%s%s] %s\n",
					folded.unit.issue.severity_label,
					_format_repeat_count(folded.repeat),
					folded.unit.issue.headline);
			if (!folded.unit.issue.continuation_text.is_empty()) {
				output += "    details: " + _compact_multiline(folded.unit.issue.continuation_text) + "\n";
			}
			if (folded.unit.issue.callsite.valid) {
				output += vformat("    callsite: %s (%s:%d)\n",
						folded.unit.issue.callsite.function,
						folded.unit.issue.callsite.file,
						folded.unit.issue.callsite.line);
			} else {
				output += "    callsite: <none>\n";
			}
			output += "    stack: " + folded.unit.issue.stack_fingerprint + "\n";
			const int visible_frames = MIN(5, folded.unit.issue.frames.size());
			for (int frame_index = 0; frame_index < visible_frames; frame_index++) {
				const StackFrameInfo &frame = folded.unit.issue.frames[frame_index];
				output += vformat("      [%d] %s (%s:%d)\n", frame.index, frame.function, frame.file, frame.line);
			}
			if (folded.unit.issue.frames.size() > visible_frames) {
				output += vformat("      (+%d more)\n", folded.unit.issue.frames.size() - visible_frames);
			}
			const String numeric_slots_text = _format_numeric_slots_text(folded.numeric_slots);
			if (!numeric_slots_text.is_empty()) {
				output += "    slots: " + numeric_slots_text + "\n";
			}
			emitted_issues++;
		}
	} else {
		output += "No issue blocks found; showing folded tail\n";
		const int preview_count = MIN(10, p_summary.folded_units.size());
		const int first_index = MAX(0, p_summary.folded_units.size() - preview_count);
		for (int i = first_index; i < p_summary.folded_units.size(); i++) {
			const FoldedUnit &folded = p_summary.folded_units[i];
			output += vformat("  - [line%s] %s\n", _format_repeat_count(folded.repeat), folded.unit.plain.normalized_text);
			const String numeric_slots_text = _format_numeric_slots_text(folded.numeric_slots);
			if (!numeric_slots_text.is_empty()) {
				output += "    slots: " + numeric_slots_text + "\n";
			}
		}
	}

	return output;
}

} // namespace

bool CLILatestLogRunner::_consume_argument_value(const String &p_arg, List<String>::Element *&r_next, String &r_value, String &r_error) {
	if (!r_next) {
		r_error = "Missing value after " + p_arg + ".";
		return false;
	}

	r_value = r_next->get();
	r_next = r_next->next();
	return true;
}

void CLILatestLogRunner::_print_stdout(const String &p_text) {
	const bool stdout_was_enabled = OS::get_singleton()->is_stdout_enabled();
	const bool print_line_was_enabled = Engine::get_singleton()->is_printing_to_stdout();
	OS::get_singleton()->set_stdout_enabled(true);
	Engine::get_singleton()->set_print_to_stdout(true);
	OS::get_singleton()->print("%s", p_text.utf8().get_data());
	Engine::get_singleton()->set_print_to_stdout(print_line_was_enabled);
	OS::get_singleton()->set_stdout_enabled(stdout_was_enabled);
}

bool CLILatestLogRunner::parse_argument(const String &p_arg, List<String>::Element *&r_next, Options &r_options, String &r_error) {
	r_error = String();
	String value;

	if (p_arg == "--latest-log") {
		r_options.enabled = true;
		return true;
	}

	if (p_arg == "--latest-log-lines") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}
		if (!value.is_valid_int()) {
			r_error = "--latest-log-lines must be followed by an integer.";
			return true;
		}
		r_options.lines = value.to_int();
		r_options.lines_set = true;
		return true;
	}

	if (p_arg == "--latest-log-format") {
		if (!_consume_argument_value(p_arg, r_next, value, r_error)) {
			return true;
		}

		value = value.to_lower();
		if (value == "text") {
			r_options.format = FORMAT_TEXT;
		} else if (value == "json") {
			r_options.format = FORMAT_JSON;
		} else if (value == "both") {
			r_options.format = FORMAT_BOTH;
		} else if (value == "raw") {
			r_options.format = FORMAT_RAW;
		} else {
			r_error = "--latest-log-format must be text, json, both or raw.";
		}
		r_options.format_set = true;
		return true;
	}

	return false;
}

Error CLILatestLogRunner::validate_options(const Options &p_options, bool p_found_project, bool p_editor, bool p_project_manager, String &r_error) {
	r_error = String();

	if (!p_options.enabled && (p_options.lines_set || p_options.format_set)) {
		r_error = "--latest-log-lines and --latest-log-format can only be used together with --latest-log.";
		return ERR_INVALID_PARAMETER;
	}

	if (p_options.lines < 0) {
		r_error = "--latest-log-lines must be greater than or equal to 0.";
		return ERR_INVALID_PARAMETER;
	}

	if (!p_options.enabled) {
		return OK;
	}

	if (p_editor || p_project_manager) {
		r_error = "--latest-log only supports CLI project contexts. It cannot be used with the editor or project manager.";
		return ERR_INVALID_PARAMETER;
	}

	if (!p_found_project) {
		r_error = "--latest-log requires a project context. Use --path <project> or run the command from a project directory.";
		return ERR_INVALID_PARAMETER;
	}

	return OK;
}

String CLILatestLogRunner::resolve_base_log_path(const String &p_log_file_override) {
	String configured_path = p_log_file_override;
	if (configured_path.is_empty()) {
		configured_path = GLOBAL_GET("debug/file_logging/log_path");
	}
	if (configured_path.is_empty()) {
		return String();
	}

	if (configured_path.begins_with("res://") || configured_path.begins_with("user://") || configured_path.begins_with("uid://")) {
		return ProjectSettings::get_singleton()->globalize_path(configured_path).simplify_path();
	}

	if (configured_path.is_absolute_path()) {
		return configured_path.simplify_path();
	}

	String project_path = ProjectSettings::get_singleton()->get_resource_path();
	if (project_path.is_empty()) {
		project_path = OS::get_singleton()->get_cwd();
	}
	return project_path.path_join(configured_path).simplify_path();
}

Error CLILatestLogRunner::find_latest_log_path(const String &p_base_log_path, String &r_latest_log_path) {
	r_latest_log_path = String();
	ERR_FAIL_COND_V_MSG(p_base_log_path.is_empty(), ERR_INVALID_PARAMETER, "Base log path must not be empty.");

	String base_dir = p_base_log_path.get_base_dir();
	if (base_dir.is_empty()) {
		base_dir = ".";
	}

	Ref<DirAccess> dir = DirAccess::open(base_dir);
	if (dir.is_null()) {
		if (FileAccess::exists(p_base_log_path)) {
			r_latest_log_path = p_base_log_path;
			return OK;
		}
		return ERR_FILE_NOT_FOUND;
	}

	const String basename = p_base_log_path.get_file().get_basename();
	const String extension = p_base_log_path.get_extension();
	uint64_t latest_modified_time = 0;
	bool found_match = false;

	dir->list_dir_begin();
	String file_name = dir->get_next();
	while (!file_name.is_empty()) {
		if (!dir->current_is_dir() && file_name.begins_with(basename) && file_name.get_extension() == extension) {
			const String full_path = base_dir.path_join(file_name).simplify_path();
			const uint64_t modified_time = FileAccess::get_modified_time(full_path);
			if (!found_match || modified_time > latest_modified_time || (modified_time == latest_modified_time && full_path == p_base_log_path)) {
				r_latest_log_path = full_path;
				latest_modified_time = modified_time;
				found_match = true;
			}
		}
		file_name = dir->get_next();
	}
	dir->list_dir_end();

	return found_match ? OK : ERR_FILE_NOT_FOUND;
}

Error CLILatestLogRunner::read_log(const String &p_log_path, int p_lines, ReadResult &r_result, String &r_error) {
	r_result = ReadResult();
	r_error = String();

	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_log_path, FileAccess::READ, &err);
	if (file.is_null() || err != OK) {
		r_error = vformat("Unable to open log file \"%s\" for reading (error code %d).", p_log_path, (int)err);
		return err == OK ? ERR_CANT_OPEN : err;
	}

	const uint64_t file_length = file->get_length();
	if (p_lines == 0) {
		while (file->get_position() < file_length) {
			if (!r_result.text.is_empty()) {
				r_result.text += "\n";
			}
			r_result.text += file->get_line();
			r_result.total_lines++;
		}
		r_result.shown_lines = r_result.total_lines;
		return OK;
	}

	Vector<String> tail_lines;
	tail_lines.resize(p_lines);
	int next_slot = 0;
	while (file->get_position() < file_length) {
		tail_lines.write[next_slot] = file->get_line();
		next_slot = (next_slot + 1) % p_lines;
		r_result.total_lines++;
	}

	r_result.shown_lines = MIN(r_result.total_lines, p_lines);
	r_result.truncated = r_result.total_lines > r_result.shown_lines;
	for (int i = 0; i < r_result.shown_lines; i++) {
		const int index = (r_result.total_lines - r_result.shown_lines + i) % p_lines;
		if (!r_result.text.is_empty()) {
			r_result.text += "\n";
		}
		r_result.text += tail_lines[index];
	}

	return OK;
}

String CLILatestLogRunner::get_format_name(Format p_format) {
	switch (p_format) {
		case FORMAT_TEXT:
			return "text";
		case FORMAT_JSON:
			return "json";
		case FORMAT_BOTH:
			return "both";
		case FORMAT_RAW:
			return "raw";
	}

	return "text";
}

Error CLILatestLogRunner::build_output(const String &p_log_path, const String &p_base_log_path, const Options &p_options, String &r_output, Dictionary &r_summary, String &r_error) {
	r_output = String();
	r_summary = Dictionary();
	r_error = String();

	ReadResult window_result;
	Error err = read_log(p_log_path, p_options.lines, window_result, r_error);
	if (err != OK) {
		return err;
	}

	if (p_options.format == FORMAT_RAW) {
		r_output += "[latest-log] path=" + p_log_path + "\n";
		r_output += vformat("[latest-log] lines=%d/%d", window_result.shown_lines, window_result.total_lines);
		if (window_result.truncated) {
			r_output += " truncated=true";
		}
		r_output += "\n";
		if (!window_result.text.is_empty()) {
			r_output += window_result.text;
			if (!r_output.ends_with("\n")) {
				r_output += "\n";
			}
		}
		return OK;
	}

	const Vector<String> window_lines = _split_lines(window_result.text);
	const int window_first_line = window_result.shown_lines > 0 ? window_result.total_lines - window_result.shown_lines + 1 : 1;

	AnalysisSummary analysis = _analyze_lines(window_lines, window_first_line, p_log_path, p_base_log_path, p_options, window_result.total_lines, false);
	if (p_options.lines > 0 && analysis.issue_block_count == 0 && window_result.truncated) {
		ReadResult full_result;
		err = read_log(p_log_path, 0, full_result, r_error);
		if (err != OK) {
			return err;
		}

		const Vector<String> full_lines = _split_lines(full_result.text);
		analysis = _analyze_lines(full_lines, 1, p_log_path, p_base_log_path, p_options, full_result.total_lines, true);
	}

	r_summary = _build_summary_dictionary(analysis);
	const String json_summary = JSON::stringify(r_summary);

	if (p_options.format == FORMAT_JSON) {
		r_output = json_summary + "\n";
		return OK;
	}

	const String text_summary = _render_text_summary(analysis);
	if (p_options.format == FORMAT_TEXT) {
		r_output = text_summary;
		if (!r_output.ends_with("\n")) {
			r_output += "\n";
		}
		return OK;
	}

	r_output = text_summary;
	if (!r_output.ends_with("\n")) {
		r_output += "\n";
	}
	r_output += json_summary + "\n";
	return OK;
}

int CLILatestLogRunner::run(const Options &p_options, const String &p_log_file_override) {
	const String base_log_path = resolve_base_log_path(p_log_file_override);
	if (base_log_path.is_empty()) {
		OS::get_singleton()->printerr("Error: Unable to resolve the configured project log path.\n");
		return EXIT_INVALID_ARGUMENTS;
	}

	String latest_log_path;
	if (find_latest_log_path(base_log_path, latest_log_path) != OK) {
		OS::get_singleton()->printerr("Error: No log files were found for \"%s\".\n", base_log_path.utf8().get_data());
		return EXIT_NOT_FOUND;
	}

	String output;
	Dictionary summary;
	String error;
	if (build_output(latest_log_path, base_log_path, p_options, output, summary, error) != OK) {
		OS::get_singleton()->printerr("Error: %s\n", error.utf8().get_data());
		return EXIT_READ_FAILED;
	}

	_print_stdout(output);
	return EXIT_OK;
}
