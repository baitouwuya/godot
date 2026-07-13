/**************************************************************************/
/*  mcp_resource_import_options.cpp                                       */
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

#include "mcp_resource_import_options.h"

#include "mcp_tool_utils.h"
#include "mcp_variant_codec.h"

#include "core/config/project_settings.h"

namespace {

static bool _contains_importer(const List<Ref<ResourceImporter>> &p_candidates, const Ref<ResourceImporter> &p_importer) {
	for (const Ref<ResourceImporter> &candidate : p_candidates) {
		if (candidate == p_importer) {
			return true;
		}
	}
	return false;
}

static Dictionary _candidate_names(const List<Ref<ResourceImporter>> &p_candidates) {
	PackedStringArray names;
	for (const Ref<ResourceImporter> &candidate : p_candidates) {
		names.push_back(candidate->get_importer_name());
	}
	Dictionary details;
	details["candidates"] = names;
	return details;
}

static bool _is_option_value_compatible(const Variant &p_value, Variant::Type p_expected_type) {
	if (p_expected_type == Variant::NIL || p_value.get_type() == p_expected_type) {
		return true;
	}
	if (p_value.get_type() == Variant::NIL && p_expected_type == Variant::OBJECT) {
		return true;
	}
	return Variant::can_convert_strict(p_value.get_type(), p_expected_type);
}

static Array _make_presets(const Ref<ResourceImporter> &p_importer) {
	Array presets;
	const int count = p_importer->get_preset_count();
	if (count <= 0) {
		Dictionary preset;
		preset["index"] = 0;
		preset["name"] = "Default";
		presets.push_back(preset);
		return presets;
	}
	for (int i = 0; i < count; i++) {
		Dictionary preset;
		preset["index"] = i;
		preset["name"] = p_importer->get_preset_name(i);
		presets.push_back(preset);
	}
	return presets;
}

static Dictionary _make_importer_description(const Ref<ResourceImporter> &p_importer, const Ref<ResourceImporter> &p_selected, const Ref<ResourceImporter> &p_default) {
	PackedStringArray extensions;
	List<String> recognized_extensions;
	p_importer->get_recognized_extensions(&recognized_extensions);
	for (const String &extension : recognized_extensions) {
		extensions.push_back(extension);
	}

	Dictionary description;
	description["name"] = p_importer->get_importer_name();
	description["visibleName"] = p_importer->get_visible_name();
	description["resourceType"] = p_importer->get_resource_type();
	description["saveExtension"] = p_importer->get_save_extension();
	description["priority"] = p_importer->get_priority();
	description["extensions"] = extensions;
	description["presets"] = _make_presets(p_importer);
	description["selected"] = p_importer == p_selected;
	description["default"] = p_importer == p_default;
	return description;
}

static bool _make_option_descriptions(const String &p_source_path, const MCPResourceImportResolution &p_resolution, Array &r_options, Dictionary &r_error) {
	for (const ResourceImporter::ImportOption &option : p_resolution.option_definitions) {
		Variant encoded_default;
		Variant encoded_value;
		String encoding_error;
		if (MCPVariantCodec::encode(option.default_value, encoded_default, &encoding_error) != OK ||
				MCPVariantCodec::encode(p_resolution.values[option.option.name], encoded_value, &encoding_error) != OK) {
			Dictionary details;
			details["option"] = option.option.name;
			r_error = MCPToolUtils::make_error_result("OPTION_ENCODING_FAILED", "Could not encode import option '" + option.option.name + "': " + encoding_error, details);
			return false;
		}

		Dictionary description;
		description["property"] = MCPToolUtils::make_property_description(option.option);
		description["defaultValue"] = encoded_default;
		description["value"] = encoded_value;
		description["visible"] = p_resolution.importer->get_option_visibility(p_source_path, option.option.name, p_resolution.values);
		description["overridden"] = p_resolution.overridden_options.has(option.option.name);
		r_options.push_back(description);
	}
	return true;
}

} // namespace

bool MCPResourceImportOptions::resolve(const Dictionary &p_arguments, const String &p_source_path, MCPResourceImportResolution &r_resolution, Dictionary &r_error) {
	ResourceFormatImporter *format_importer = ResourceFormatImporter::get_singleton();
	if (!format_importer) {
		r_error = MCPToolUtils::make_error_result("IMPORTERS_UNAVAILABLE", "Resource importers are not initialized.");
		return false;
	}

	List<Ref<ResourceImporter>> candidates;
	format_importer->get_importers_for_file(p_source_path, &candidates);
	if (candidates.is_empty()) {
		r_error = MCPToolUtils::make_error_result("NO_IMPORTER", "No ResourceImporter recognizes source: " + p_source_path);
		return false;
	}

	r_resolution.importer_explicit = p_arguments.has("importer");
	if (r_resolution.importer_explicit) {
		const Variant importer_value = p_arguments["importer"];
		if (importer_value.get_type() != Variant::STRING || String(importer_value).is_empty()) {
			r_error = MCPToolUtils::make_error_result("INVALID_ARGUMENTS", "importer must be a non-empty string when provided.");
			return false;
		}
		r_resolution.importer = format_importer->get_importer_by_name(importer_value);
		if (r_resolution.importer.is_null()) {
			r_error = MCPToolUtils::make_error_result("UNKNOWN_IMPORTER", "Unknown ResourceImporter: " + String(importer_value), _candidate_names(candidates));
			return false;
		}
		if (!_contains_importer(candidates, r_resolution.importer)) {
			r_error = MCPToolUtils::make_error_result("IMPORTER_NOT_AVAILABLE", "ResourceImporter does not recognize the source extension: " + String(importer_value), _candidate_names(candidates));
			return false;
		}
	} else {
		r_resolution.importer = format_importer->get_importer_by_file(p_source_path);
		if (r_resolution.importer.is_null()) {
			r_error = MCPToolUtils::make_error_result("NO_DEFAULT_IMPORTER", "No default ResourceImporter is available for source: " + p_source_path, _candidate_names(candidates));
			return false;
		}
	}

	r_resolution.preset_explicit = p_arguments.has("preset");
	if (r_resolution.preset_explicit) {
		const Variant preset_value = p_arguments["preset"];
		if (preset_value.get_type() != Variant::INT || int64_t(preset_value) < 0 || int64_t(preset_value) > 0x7FFFFFFF) {
			r_error = MCPToolUtils::make_error_result("INVALID_PRESET", "preset must be a non-negative integer.");
			return false;
		}
		r_resolution.preset = int(preset_value);
	}

	const int preset_count = r_resolution.importer->get_preset_count();
	const bool preset_valid = preset_count > 0 ? r_resolution.preset < preset_count : r_resolution.preset == 0;
	if (!preset_valid) {
		Dictionary details;
		details["preset"] = r_resolution.preset;
		details["presetCount"] = preset_count;
		r_error = MCPToolUtils::make_error_result("INVALID_PRESET", "Preset index is not available for importer: " + r_resolution.importer->get_importer_name(), details);
		return false;
	}

	r_resolution.importer->get_import_options(p_source_path, &r_resolution.option_definitions, r_resolution.preset);
	HashMap<StringName, PropertyInfo> property_infos;
	for (const ResourceImporter::ImportOption &option : r_resolution.option_definitions) {
		r_resolution.values[option.option.name] = option.default_value;
		property_infos[option.option.name] = option.option;
	}

	if (!r_resolution.importer_explicit && !r_resolution.preset_explicit) {
		const String setting_name = "importer_defaults/" + r_resolution.importer->get_importer_name();
		if (ProjectSettings::get_singleton()->has_setting(setting_name)) {
			const Dictionary project_defaults = GLOBAL_GET(setting_name);
			for (const KeyValue<Variant, Variant> &entry : project_defaults) {
				if (!entry.key.is_string()) {
					continue;
				}
				const StringName option_name = entry.key;
				if (r_resolution.values.has(option_name)) {
					r_resolution.values[option_name] = entry.value;
					r_resolution.project_defaults_applied = true;
				}
			}
		}
	}

	if (!p_arguments.has("options")) {
		return true;
	}
	const Variant options_value = p_arguments["options"];
	if (options_value.get_type() != Variant::DICTIONARY) {
		r_error = MCPToolUtils::make_error_result("INVALID_OPTIONS", "options must be an object keyed by import option name.");
		return false;
	}

	const Dictionary overrides = options_value;
	for (const KeyValue<Variant, Variant> &entry : overrides) {
		if (!entry.key.is_string() || String(entry.key).is_empty()) {
			r_error = MCPToolUtils::make_error_result("INVALID_OPTIONS", "Every import option key must be a non-empty string.");
			return false;
		}
		const StringName option_name = entry.key;
		const PropertyInfo *property_info = property_infos.getptr(option_name);
		if (!property_info) {
			Dictionary details;
			details["option"] = String(option_name);
			r_error = MCPToolUtils::make_error_result("INVALID_OPTIONS", "Unknown import option: " + String(option_name), details);
			return false;
		}

		Variant decoded_value;
		String decode_error;
		if (MCPVariantCodec::decode(entry.value, decoded_value, &decode_error) != OK) {
			Dictionary details;
			details["option"] = String(option_name);
			r_error = MCPToolUtils::make_error_result("INVALID_OPTIONS", "Invalid value for import option '" + String(option_name) + "': " + decode_error, details);
			return false;
		}
		if (!_is_option_value_compatible(decoded_value, property_info->type)) {
			Dictionary details;
			details["option"] = String(option_name);
			details["expectedType"] = Variant::get_type_name(property_info->type);
			details["actualType"] = Variant::get_type_name(decoded_value.get_type());
			r_error = MCPToolUtils::make_error_result("INVALID_OPTIONS", "Import option has an incompatible value type: " + String(option_name), details);
			return false;
		}

		r_resolution.values[option_name] = decoded_value;
		r_resolution.overridden_options.insert(option_name);
	}
	return true;
}

Dictionary MCPResourceImportOptions::make_result(const String &p_source_path, const MCPResourceImportResolution &p_resolution, Dictionary &r_error) {
	ResourceFormatImporter *format_importer = ResourceFormatImporter::get_singleton();
	List<Ref<ResourceImporter>> candidates;
	format_importer->get_importers_for_file(p_source_path, &candidates);
	const Ref<ResourceImporter> default_importer = format_importer->get_importer_by_file(p_source_path);

	Array importer_descriptions;
	for (const Ref<ResourceImporter> &candidate : candidates) {
		importer_descriptions.push_back(_make_importer_description(candidate, p_resolution.importer, default_importer));
	}

	Array option_descriptions;
	if (!_make_option_descriptions(p_source_path, p_resolution, option_descriptions, r_error)) {
		return Dictionary();
	}

	Dictionary selected_preset;
	selected_preset["index"] = p_resolution.preset;
	selected_preset["name"] = p_resolution.importer->get_preset_count() > 0 ? p_resolution.importer->get_preset_name(p_resolution.preset) : String("Default");

	Dictionary result;
	result["source"] = p_source_path;
	result["extension"] = p_source_path.get_extension().to_lower();
	result["defaultImporter"] = default_importer.is_valid() ? default_importer->get_importer_name() : String();
	result["selectedImporter"] = p_resolution.importer->get_importer_name();
	result["selectedPreset"] = selected_preset;
	result["projectDefaultsApplied"] = p_resolution.project_defaults_applied;
	result["importers"] = importer_descriptions;
	result["options"] = option_descriptions;
	return result;
}
