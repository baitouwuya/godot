/**************************************************************************/
/*  test_custom_feature_trace_helpers.h                                   */
/**************************************************************************/

#pragma once

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"

#include "thirdparty/doctest/doctest.h"

namespace TestCustomFeatureTraceHelpers {

static void cleanup_directory_recursive(const String &p_path) {
	if (!DirAccess::dir_exists_absolute(p_path)) {
		return;
	}
	Ref<DirAccess> dir = DirAccess::open(p_path);
	REQUIRE_FALSE(dir.is_null());
	REQUIRE_EQ(dir->erase_contents_recursive(), OK);
	REQUIRE_EQ(DirAccess::remove_absolute(p_path), OK);
}

static Dictionary read_manifest(const String &p_session_dir) {
	const Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(p_session_dir.path_join("manifest.json")));
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	return parsed;
}

static Vector<Dictionary> read_events(const String &p_session_dir) {
	Vector<Dictionary> events;
	Dictionary manifest = read_manifest(p_session_dir);
	Array event_files = manifest["eventFiles"];
	for (int i = 0; i < event_files.size(); i++) {
		Error err = OK;
		Ref<FileAccess> file = FileAccess::open(p_session_dir.path_join(String(event_files[i])), FileAccess::READ, &err);
		REQUIRE_EQ(err, OK);
		REQUIRE(file.is_valid());
		while (!file->eof_reached()) {
			const String line = file->get_line();
			if (line.is_empty()) {
				continue;
			}
			const Variant parsed = JSON::parse_string(line);
			REQUIRE(parsed.get_type() == Variant::DICTIONARY);
			events.push_back(parsed);
		}
	}
	return events;
}

static void check_common_event_fields(const Dictionary &p_event) {
	CHECK(p_event.has("seq"));
	CHECK(p_event.has("tsUnixUsec"));
	CHECK(p_event.has("monoUsec"));
	CHECK(p_event.has("sessionId"));
	CHECK(p_event.has("feature"));
	CHECK(p_event.has("event"));
	CHECK(p_event.has("severity"));
	CHECK(p_event.has("correlationId"));
	CHECK(p_event.has("data"));
	CHECK(p_event.has("payloads"));
	CHECK(p_event.has("error"));
}

static Dictionary find_event(const Vector<Dictionary> &p_events, const String &p_feature, const String &p_event) {
	for (int i = 0; i < p_events.size(); i++) {
		if (String(p_events[i].get("feature", "")) == p_feature && String(p_events[i].get("event", "")) == p_event) {
			return p_events[i];
		}
	}
	return Dictionary();
}

static String read_payload_text(const String &p_session_dir, const Dictionary &p_descriptor) {
	if (p_descriptor.is_empty()) {
		return String();
	}
	const Variant inline_value = p_descriptor.get("inline", Variant());
	if (inline_value.get_type() != Variant::NIL) {
		return inline_value;
	}
	const String ref_path = p_descriptor.get("refPath", "");
	REQUIRE_FALSE(ref_path.is_empty());
	const String full_path = ref_path.is_absolute_path() ? ref_path : p_session_dir.path_join(ref_path);
	Error err = OK;
	const String text = FileAccess::get_file_as_string(full_path, &err);
	REQUIRE_EQ(err, OK);
	return text;
}

} // namespace TestCustomFeatureTraceHelpers
