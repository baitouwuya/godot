/**************************************************************************/
/*  mcp_log_analyzer.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "mcp_log_analyzer.h"

#include "core/templates/hash_map.h"
#include "core/templates/hashfuncs.h"
#include "core/templates/vector.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_REGEX_ENABLED
#include "modules/regex/regex.h"
#endif

namespace {

struct NumericSlot {
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

struct Callsite {
	bool valid = false;
	String function;
	String file;
	int line = 0;

	String anchor() const {
		return valid ? function + "|" + file + "|" + itos(line) : "<none>";
	}

	Dictionary to_dictionary() const {
		Dictionary result;
		if (valid) {
			result["function"] = function;
			result["file"] = file;
			result["line"] = line;
		}
		return result;
	}
};

struct StackFrame {
	int index = 0;
	String function;
	String file;
	int line = 0;

	String anchor() const {
		return itos(index) + "|" + function + "|" + file + "|" + itos(line);
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

struct Unit {
	bool issue = false;
	int first_line = 0;
	int end_line = 0;
	String severity;
	String headline;
	String template_text;
	String template_id;
	String fold_signature;
	Vector<double> numeric_values;
	Callsite callsite;
	String stack_header;
	Vector<StackFrame> frames;
	String stack_fingerprint;

	String kind() const { return issue ? "issueBlock" : "line"; }
};

struct FoldedUnit {
	Unit unit;
	int repeat = 1;
	int last_line = 0;
	Vector<NumericSlot> numeric_slots;

	void append_numeric(const Vector<double> &p_values) {
		if (numeric_slots.size() < p_values.size()) {
			numeric_slots.resize(p_values.size());
		}
		for (int i = 0; i < p_values.size(); i++) {
			numeric_slots.write[i].push(p_values[i]);
		}
	}

	Array numeric_slots_array() const {
		Array result;
		for (int i = 0; i < numeric_slots.size(); i++) {
			if (numeric_slots[i].count > 0) {
				result.push_back(numeric_slots[i].to_dictionary(i));
			}
		}
		return result;
	}
};

struct TemplateAggregate {
	String id;
	String kind;
	String text;
	int count = 0;
	int first_line = 0;
	int last_line = 0;
	Vector<NumericSlot> numeric_slots;

	void append(const Unit &p_unit) {
		if (count == 0) {
			id = p_unit.template_id;
			kind = p_unit.kind();
			text = p_unit.template_text;
			first_line = p_unit.first_line;
		}
		count++;
		last_line = p_unit.first_line;
		if (numeric_slots.size() < p_unit.numeric_values.size()) {
			numeric_slots.resize(p_unit.numeric_values.size());
		}
		for (int i = 0; i < p_unit.numeric_values.size(); i++) {
			numeric_slots.write[i].push(p_unit.numeric_values[i]);
		}
	}

	Dictionary to_dictionary() const {
		Dictionary result;
		result["templateId"] = id;
		result["kind"] = kind;
		result["template"] = text;
		result["count"] = count;
		result["firstOccurrenceLine"] = first_line;
		result["lastOccurrenceLine"] = last_line;
		Array slots;
		for (int i = 0; i < numeric_slots.size(); i++) {
			if (numeric_slots[i].count > 0) {
				slots.push_back(numeric_slots[i].to_dictionary(i));
			}
		}
		result["numericSlots"] = slots;
		return result;
	}
};

struct TemplateComparator {
	bool operator()(const TemplateAggregate &p_left, const TemplateAggregate &p_right) const {
		if (p_left.count != p_right.count) {
			return p_left.count > p_right.count;
		}
		if (p_left.last_line != p_right.last_line) {
			return p_left.last_line > p_right.last_line;
		}
		return p_left.id < p_right.id;
	}
};

struct Keyword {
	String token;
	int count = 0;
};

struct KeywordComparator {
	bool operator()(const Keyword &p_left, const Keyword &p_right) const {
		return p_left.count == p_right.count ? p_left.token < p_right.token : p_left.count > p_right.count;
	}
};

struct Replacement {
	int start = 0;
	int end = 0;
	String text;
	bool numeric = false;
	double value = 0.0;
};

struct ReplacementComparator {
	bool operator()(const Replacement &p_left, const Replacement &p_right) const { return p_left.start < p_right.start; }
};

struct NormalizedText {
	String text;
	Vector<double> numeric_values;
};

static String _short_hash(const String &p_text) {
	const CharString utf8 = p_text.utf8();
	const uint32_t hash = utf8.length() > 0 ? hash_murmur3_buffer(utf8.get_data(), utf8.length()) : hash_murmur3_one_32(0);
	return String::num_uint64(hash, 16).pad_zeros(8);
}

static Vector<String> _split_lines(const String &p_text) {
	Vector<String> lines;
	if (p_text.is_empty()) {
		return lines;
	}
	int from = 0;
	while (from <= p_text.length()) {
		const int newline = p_text.find_char('\n', from);
		if (newline < 0) {
			lines.push_back(p_text.substr(from).trim_suffix("\r"));
			break;
		}
		lines.push_back(p_text.substr(from, newline - from).trim_suffix("\r"));
		from = newline + 1;
	}
	return lines;
}

static bool _pathish(char32_t p_character) {
	return (p_character >= 'a' && p_character <= 'z') || (p_character >= 'A' && p_character <= 'Z') || (p_character >= '0' && p_character <= '9') || p_character == '_' || p_character == '.' || p_character == '/' || p_character == '\\' || p_character == ':';
}

#ifdef MODULE_REGEX_ENABLED
static Ref<RegEx> _regex(const char *p_pattern) {
	Ref<RegEx> result = RegEx::create_from_string(p_pattern, false);
	if (result.is_valid()) {
		result->detach_from_objectdb();
	}
	return result;
}

static void _collect(const Ref<RegEx> &p_regex, const String &p_text, const String &p_placeholder, bool p_numeric, bool p_safe_boundary, Vector<Replacement> &r_replacements, Vector<uint8_t> &r_occupied) {
	for (const Variant &match_value : p_regex->search_all(p_text)) {
		Ref<RegExMatch> match = match_value;
		const int start = match->get_start(0);
		const int end = match->get_end(0);
		if (p_safe_boundary && ((start > 0 && _pathish(p_text[start - 1])) || (end < p_text.length() && _pathish(p_text[end])))) {
			continue;
		}
		bool free = true;
		for (int i = start; i < end; i++) {
			if (r_occupied[i]) {
				free = false;
				break;
			}
		}
		if (!free) {
			continue;
		}
		Replacement replacement;
		replacement.start = start;
		replacement.end = end;
		replacement.text = p_placeholder;
		replacement.numeric = p_numeric;
		if (p_numeric) {
			const String value = match->get_string(0);
			replacement.value = value.contains_char('.') || value.contains_char('e') || value.contains_char('E') ? value.to_float() : double(value.to_int());
		}
		for (int i = start; i < end; i++) {
			r_occupied.write[i] = 1;
		}
		r_replacements.push_back(replacement);
	}
}
#endif

static NormalizedText _normalize(const String &p_text) {
	NormalizedText result;
	result.text = p_text;
#ifdef MODULE_REGEX_ENABLED
	if (p_text.is_empty()) {
		return result;
	}
	static Ref<RegEx> timestamp = _regex("\\b\\d{4}-\\d{2}-\\d{2}[T ]\\d{2}:\\d{2}:\\d{2}(?:[\\.,]\\d+)?\\b|\\b\\d{2}:\\d{2}:\\d{2}(?:[\\.,]\\d+)?\\b");
	static Ref<RegEx> uuid = _regex("\\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\b");
	static Ref<RegEx> ipv4 = _regex("\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b");
	static Ref<RegEx> hex = _regex("\\b0x[0-9a-fA-F]{6,}\\b");
	static Ref<RegEx> long_integer = _regex("[-+]?\\d{6,}");
	static Ref<RegEx> number = _regex("[-+]?(?:\\d+\\.\\d+(?:[eE][-+]?\\d+)?|\\d+[eE][-+]?\\d+)");
	static Ref<RegEx> integer = _regex("[-+]?\\d+");
	Vector<uint8_t> occupied;
	occupied.resize(p_text.length());
	for (int i = 0; i < occupied.size(); i++) {
		occupied.write[i] = 0;
	}
	Vector<Replacement> replacements;
	_collect(timestamp, p_text, "<ts>", false, false, replacements, occupied);
	_collect(uuid, p_text, "<uuid>", false, false, replacements, occupied);
	_collect(ipv4, p_text, "<ip4>", false, false, replacements, occupied);
	_collect(hex, p_text, "<hex>", false, false, replacements, occupied);
	_collect(long_integer, p_text, "<int>", true, true, replacements, occupied);
	_collect(number, p_text, "<num>", true, true, replacements, occupied);
	_collect(integer, p_text, "<int>", true, true, replacements, occupied);
	if (replacements.is_empty()) {
		return result;
	}
	replacements.sort_custom<ReplacementComparator>();
	result.text = String();
	int cursor = 0;
	for (const Replacement &replacement : replacements) {
		result.text += p_text.substr(cursor, replacement.start - cursor) + replacement.text;
		cursor = replacement.end;
		if (replacement.numeric) {
			result.numeric_values.push_back(replacement.value);
		}
	}
	result.text += p_text.substr(cursor);
#endif
	return result;
}

static bool _severity(const String &p_line, String &r_key, String &r_label, String &r_headline) {
	static const char *labels[] = { "SCRIPT ERROR", "SHADER ERROR", "ERROR", "WARNING" };
	static const char *keys[] = { "scriptError", "shaderError", "error", "warning" };
	for (int i = 0; i < 4; i++) {
		const String prefix = String(labels[i]) + ":";
		if (p_line.begins_with(prefix)) {
			r_key = keys[i];
			r_label = labels[i];
			r_headline = p_line.substr(prefix.length()).strip_edges();
			return true;
		}
	}
	return false;
}

static bool _parse_callsite(const String &p_line, Callsite &r_callsite) {
#ifdef MODULE_REGEX_ENABLED
	static Ref<RegEx> regex = _regex("^\\s+at:\\s+(.+) \\((.+):(\\d+)\\)\\s*$");
	Ref<RegExMatch> match = regex->search(p_line);
	if (match.is_valid()) {
		r_callsite.valid = true;
		r_callsite.function = match->get_string(1);
		r_callsite.file = match->get_string(2);
		r_callsite.line = match->get_string(3).to_int();
		return true;
	}
#endif
	return false;
}

static bool _parse_frame(const String &p_line, StackFrame &r_frame) {
#ifdef MODULE_REGEX_ENABLED
	static Ref<RegEx> regex = _regex("^\\s*\\[(\\d+)\\]\\s+(.+) \\((.+):(\\d+)\\)\\s*$");
	Ref<RegExMatch> match = regex->search(p_line);
	if (match.is_valid()) {
		r_frame.index = match->get_string(1).to_int();
		r_frame.function = match->get_string(2);
		r_frame.file = match->get_string(3);
		r_frame.line = match->get_string(4).to_int();
		return true;
	}
#endif
	return false;
}

static String _stack_anchor(const Vector<StackFrame> &p_frames) {
	String result;
	for (int i = 0; i < MIN(3, p_frames.size()); i++) {
		result += (i > 0 ? ";" : "") + p_frames[i].anchor();
	}
	return result;
}

static Vector<Unit> _parse_units(const Vector<String> &p_lines, int p_first_line, Dictionary &r_counts) {
	Vector<Unit> units;
	for (int i = 0; i < p_lines.size();) {
		String key;
		String label;
		String headline;
		if (_severity(p_lines[i], key, label, headline)) {
			Unit unit;
			unit.issue = true;
			unit.first_line = p_first_line + i;
			unit.severity = key;
			NormalizedText normalized = _normalize(headline);
			unit.headline = normalized.text;
			unit.numeric_values = normalized.numeric_values;
			Vector<String> continuations;
			int j = i + 1;
			for (; j < p_lines.size(); j++) {
				String next_key;
				String next_label;
				String next_headline;
				if (_severity(p_lines[j], next_key, next_label, next_headline)) {
					break;
				}
				if (_parse_callsite(p_lines[j], unit.callsite)) {
					continue;
				}
				if (p_lines[j].strip_edges().ends_with("backtrace (most recent call first):")) {
					unit.stack_header = p_lines[j].strip_edges();
					continue;
				}
				StackFrame frame;
				if (_parse_frame(p_lines[j], frame)) {
					unit.frames.push_back(frame);
					continue;
				}
				if (!p_lines[j].is_empty() && (p_lines[j][0] == ' ' || p_lines[j][0] == '\t')) {
					NormalizedText continuation = _normalize(p_lines[j].strip_edges());
					if (!continuation.text.is_empty()) {
						continuations.push_back(continuation.text);
					}
					unit.numeric_values.append_array(continuation.numeric_values);
					continue;
				}
				break;
			}
			unit.end_line = p_first_line + j - 1;
			unit.template_text = label + ": " + unit.headline;
			for (const String &continuation : continuations) {
				unit.template_text += "\n" + continuation;
			}
			unit.template_id = _short_hash(key + "|" + unit.template_text);
			const String stack_anchor = _stack_anchor(unit.frames);
			unit.stack_fingerprint = _short_hash(key + "|" + unit.template_text + "|" + unit.callsite.anchor() + "|" + stack_anchor);
			unit.fold_signature = unit.template_id + "|" + unit.callsite.anchor() + "|" + stack_anchor;
			units.push_back(unit);
			r_counts[key] = int(r_counts.get(key, 0)) + 1;
			i = j;
			continue;
		}
		if (!p_lines[i].strip_edges().is_empty()) {
			Unit unit;
			unit.first_line = p_first_line + i;
			unit.end_line = unit.first_line;
			NormalizedText normalized = _normalize(p_lines[i]);
			unit.template_text = normalized.text;
			unit.numeric_values = normalized.numeric_values;
			unit.template_id = _short_hash("line|" + unit.template_text);
			unit.fold_signature = unit.template_id;
			units.push_back(unit);
		}
		i++;
	}
	return units;
}

static bool _stopword(const String &p_token) {
	static const char *words[] = { "and", "are", "at", "backtrace", "error", "file", "for", "frame", "from", "godot", "line", "script", "shader", "stack", "the", "this", "warning", "with" };
	for (const char *word : words) {
		if (p_token == word) {
			return true;
		}
	}
	return false;
}

static void _keywords(const String &p_text, int p_weight, HashMap<String, int> &r_counts) {
	String token;
	const String lowered = p_text.to_lower();
	for (int i = 0; i <= lowered.length(); i++) {
		const char32_t character = i < lowered.length() ? lowered[i] : ' ';
		if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')) {
			token += String::chr(character);
			continue;
		}
		if (token.length() >= 3 && !_stopword(token) && !token.is_valid_int()) {
			int count = 0;
			if (const int *existing = r_counts.getptr(token)) {
				count = *existing;
			}
			r_counts[token] = count + p_weight;
		}
		token = String();
	}
}

static Dictionary _issue_dictionary(const FoldedUnit &p_folded) {
	Dictionary result;
	result["templateId"] = p_folded.unit.template_id;
	result["severity"] = p_folded.unit.severity;
	result["headline"] = p_folded.unit.headline;
	result["repeat"] = p_folded.repeat;
	result["firstLine"] = p_folded.unit.first_line;
	result["lastLine"] = p_folded.last_line;
	result["callsite"] = p_folded.unit.callsite.to_dictionary();
	result["stackFingerprint"] = p_folded.unit.stack_fingerprint;
	Dictionary stack;
	stack["header"] = p_folded.unit.stack_header;
	Array frames;
	for (int i = 0; i < MIN(10, p_folded.unit.frames.size()); i++) {
		frames.push_back(p_folded.unit.frames[i].to_dictionary());
	}
	stack["frames"] = frames;
	result["stackSummary"] = stack;
	return result;
}

} // namespace

Dictionary MCPLogAnalyzer::analyze(const String &p_text) {
	return analyze(p_text, Options());
}

Dictionary MCPLogAnalyzer::analyze(const String &p_text, const Options &p_options) {
	Dictionary counts;
	counts["error"] = 0;
	counts["warning"] = 0;
	counts["scriptError"] = 0;
	counts["shaderError"] = 0;
	const Vector<String> lines = _split_lines(p_text);
	const Vector<Unit> units = _parse_units(lines, MAX(1, p_options.first_line), counts);

	Vector<FoldedUnit> folded;
	int folded_repeats = 0;
	for (const Unit &unit : units) {
		if (!folded.is_empty() && folded[folded.size() - 1].unit.fold_signature == unit.fold_signature) {
			FoldedUnit &entry = folded.write[folded.size() - 1];
			entry.repeat++;
			entry.last_line = unit.first_line;
			entry.append_numeric(unit.numeric_values);
			folded_repeats++;
		} else {
			FoldedUnit entry;
			entry.unit = unit;
			entry.last_line = unit.first_line;
			entry.append_numeric(unit.numeric_values);
			folded.push_back(entry);
		}
	}

	HashMap<String, int> aggregate_indexes;
	Vector<TemplateAggregate> aggregates;
	for (const Unit &unit : units) {
		const String key = unit.kind() + "|" + unit.template_id;
		int index = -1;
		if (const int *existing = aggregate_indexes.getptr(key)) {
			index = *existing;
		}
		if (index < 0) {
			index = aggregates.size();
			aggregate_indexes[key] = index;
			aggregates.push_back(TemplateAggregate());
		}
		aggregates.write[index].append(unit);
	}
	aggregates.sort_custom<TemplateComparator>();
	if (aggregates.size() > 5) {
		aggregates.resize(5);
	}

	HashMap<String, int> keyword_counts;
	for (const FoldedUnit &entry : folded) {
		_keywords(entry.unit.template_text, entry.repeat, keyword_counts);
	}
	Vector<Keyword> keywords;
	for (const KeyValue<String, int> &entry : keyword_counts) {
		keywords.push_back({ entry.key, entry.value });
	}
	keywords.sort_custom<KeywordComparator>();
	if (keywords.size() > 10) {
		keywords.resize(10);
	}

	Dictionary analysis;
	analysis["windowLinesRequested"] = p_options.requested_lines;
	analysis["windowLinesAnalyzed"] = lines.size();
	analysis["totalLines"] = p_options.total_lines;
	analysis["truncated"] = p_options.truncated;
	analysis["foldedRepeatCount"] = folded_repeats;
	analysis["issueBlockCount"] = int(counts["error"]) + int(counts["warning"]) + int(counts["scriptError"]) + int(counts["shaderError"]);

	Dictionary result;
	result["analysis"] = analysis;
	result["counts"] = counts;
	Array keyword_array;
	for (const Keyword &keyword : keywords) {
		Dictionary item;
		item["token"] = keyword.token;
		item["count"] = keyword.count;
		keyword_array.push_back(item);
	}
	result["keywordHits"] = keyword_array;
	Array template_array;
	for (const TemplateAggregate &aggregate : aggregates) {
		template_array.push_back(aggregate.to_dictionary());
	}
	result["topTemplates"] = template_array;
	Array recent_issues;
	for (int i = folded.size() - 1; i >= 0 && recent_issues.size() < 3; i--) {
		if (folded[i].unit.issue) {
			recent_issues.push_back(_issue_dictionary(folded[i]));
		}
	}
	result["recentIssueBlocks"] = recent_issues;
	if (int(analysis["issueBlockCount"]) == 0) {
		Array tail;
		for (int i = MAX(0, folded.size() - 10); i < folded.size(); i++) {
			Dictionary item;
			item["templateId"] = folded[i].unit.template_id;
			item["text"] = folded[i].unit.template_text;
			item["repeat"] = folded[i].repeat;
			item["firstLine"] = folded[i].unit.first_line;
			item["lastLine"] = folded[i].last_line;
			item["numericSlots"] = folded[i].numeric_slots_array();
			tail.push_back(item);
		}
		result["fallbackTail"] = tail;
	}
	return result;
}
