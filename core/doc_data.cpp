/**************************************************************************/
/*  doc_data.cpp                                                          */
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

#include "doc_data.h"

#include "core/object/method_info.h"
#include "core/object/property_info.h"

String DocData::get_default_value_string(const Variant &p_value) {
	const Variant::Type type = p_value.get_type();
	if (type == Variant::ARRAY) {
		return Variant(Array(p_value, 0, StringName(), Variant())).get_construct_string().replace_char('\n', ' ');
	} else if (type == Variant::DICTIONARY) {
		return Variant(Dictionary(p_value, 0, StringName(), Variant(), 0, StringName(), Variant())).get_construct_string().replace_char('\n', ' ');
	} else if (type == Variant::INT) {
		return itos(p_value);
	} else if (type == Variant::FLOAT) {
		// Since some values are 32-bit internally, use 32-bit for all
		// documentation values to avoid garbage digits at the end.
		const String s = String::num_scientific((float)p_value);
		// Use float literals for floats in the documentation for clarity.
		if (s != "inf" && s != "-inf" && s != "nan") {
			if (!s.contains_char('.') && !s.contains_char('e')) {
				return s + ".0";
			}
		}
		return s;
	} else {
		return p_value.get_construct_string().replace_char('\n', ' ');
	}
}

void DocData::return_doc_from_retinfo(DocData::MethodDoc &p_method, const PropertyInfo &p_retinfo) {
	if (p_retinfo.type == Variant::INT && p_retinfo.hint == PROPERTY_HINT_INT_IS_POINTER) {
		p_method.return_type = p_retinfo.hint_string;
		if (p_method.return_type.is_empty()) {
			p_method.return_type = "void*";
		} else {
			p_method.return_type += "*";
		}
	} else if (p_retinfo.type == Variant::INT && p_retinfo.usage & (PROPERTY_USAGE_CLASS_IS_ENUM | PROPERTY_USAGE_CLASS_IS_BITFIELD)) {
		p_method.return_enum = p_retinfo.class_name;
		if (p_method.return_enum.begins_with("_")) { //proxy class
			p_method.return_enum = p_method.return_enum.substr(1);
		}
		p_method.return_is_bitfield = p_retinfo.usage & PROPERTY_USAGE_CLASS_IS_BITFIELD;
		p_method.return_type = "int";
	} else if (p_retinfo.class_name != StringName()) {
		p_method.return_type = p_retinfo.class_name;
	} else if (p_retinfo.type == Variant::ARRAY && p_retinfo.hint == PROPERTY_HINT_ARRAY_TYPE) {
		p_method.return_type = p_retinfo.hint_string + "[]";
	} else if (p_retinfo.type == Variant::DICTIONARY && p_retinfo.hint == PROPERTY_HINT_DICTIONARY_TYPE) {
		p_method.return_type = "Dictionary[" + p_retinfo.hint_string.replace(";", ", ") + "]";
	} else if (p_retinfo.hint == PROPERTY_HINT_RESOURCE_TYPE) {
		p_method.return_type = p_retinfo.hint_string;
	} else if (p_retinfo.type == Variant::NIL && p_retinfo.usage & PROPERTY_USAGE_NIL_IS_VARIANT) {
		p_method.return_type = "Variant";
	} else if (p_retinfo.type == Variant::NIL) {
		p_method.return_type = "void";
	} else {
		p_method.return_type = Variant::get_type_name(p_retinfo.type);
	}
}

void DocData::argument_doc_from_arginfo(DocData::ArgumentDoc &p_argument, const PropertyInfo &p_arginfo) {
	p_argument.name = p_arginfo.name;

	if (p_arginfo.type == Variant::INT && p_arginfo.hint == PROPERTY_HINT_INT_IS_POINTER) {
		p_argument.type = p_arginfo.hint_string;
		if (p_argument.type.is_empty()) {
			p_argument.type = "void*";
		} else {
			p_argument.type += "*";
		}
	} else if (p_arginfo.type == Variant::INT && p_arginfo.usage & (PROPERTY_USAGE_CLASS_IS_ENUM | PROPERTY_USAGE_CLASS_IS_BITFIELD)) {
		p_argument.enumeration = p_arginfo.class_name;
		if (p_argument.enumeration.begins_with("_")) { //proxy class
			p_argument.enumeration = p_argument.enumeration.substr(1);
		}
		p_argument.is_bitfield = p_arginfo.usage & PROPERTY_USAGE_CLASS_IS_BITFIELD;
		p_argument.type = "int";
	} else if (p_arginfo.class_name != StringName()) {
		p_argument.type = p_arginfo.class_name;
	} else if (p_arginfo.type == Variant::ARRAY && p_arginfo.hint == PROPERTY_HINT_ARRAY_TYPE) {
		p_argument.type = p_arginfo.hint_string + "[]";
	} else if (p_arginfo.type == Variant::DICTIONARY && p_arginfo.hint == PROPERTY_HINT_DICTIONARY_TYPE) {
		p_argument.type = "Dictionary[" + p_arginfo.hint_string.replace(";", ", ") + "]";
	} else if (p_arginfo.hint == PROPERTY_HINT_RESOURCE_TYPE) {
		p_argument.type = p_arginfo.hint_string;
	} else if (p_arginfo.type == Variant::NIL) {
		// Parameters cannot be void, so PROPERTY_USAGE_NIL_IS_VARIANT is not necessary
		p_argument.type = "Variant";
	} else {
		p_argument.type = Variant::get_type_name(p_arginfo.type);
	}
}

void DocData::method_doc_from_methodinfo(DocData::MethodDoc &p_method, const MethodInfo &p_methodinfo, const String &p_desc) {
	p_method.name = p_methodinfo.name;
	p_method.description = p_desc;

	if (p_methodinfo.flags & METHOD_FLAG_VIRTUAL) {
		p_method.qualifiers = "virtual";
	}

	if (p_methodinfo.flags & METHOD_FLAG_VIRTUAL_REQUIRED) {
		if (!p_method.qualifiers.is_empty()) {
			p_method.qualifiers += " ";
		}
		p_method.qualifiers += "required";
	}

	if (p_methodinfo.flags & METHOD_FLAG_CONST) {
		if (!p_method.qualifiers.is_empty()) {
			p_method.qualifiers += " ";
		}
		p_method.qualifiers += "const";
	}

	if (p_methodinfo.flags & METHOD_FLAG_VARARG) {
		if (!p_method.qualifiers.is_empty()) {
			p_method.qualifiers += " ";
		}
		p_method.qualifiers += "vararg";
	}

	if (p_methodinfo.flags & METHOD_FLAG_STATIC) {
		if (!p_method.qualifiers.is_empty()) {
			p_method.qualifiers += " ";
		}
		p_method.qualifiers += "static";
	}

	return_doc_from_retinfo(p_method, p_methodinfo.return_val);

	for (int64_t i = 0; i < p_methodinfo.arguments.size(); ++i) {
		DocData::ArgumentDoc argument;
		argument_doc_from_arginfo(argument, p_methodinfo.arguments[i]);
		int64_t default_arg_index = i - (p_methodinfo.arguments.size() - p_methodinfo.default_arguments.size());
		if (default_arg_index >= 0) {
			Variant default_arg = p_methodinfo.default_arguments[default_arg_index];
			argument.default_value = get_default_value_string(default_arg);
		}
		p_method.arguments.push_back(argument);
	}
}

String DocData::bbcode_to_markdown(const String &p_bbcode) {
	String markdown = p_bbcode.strip_edges();

	Vector<String> lines = markdown.split("\n");
	bool in_codeblock_tag = false;
	// This is for handling the special [codeblocks] syntax used by the built-in class reference.
	bool in_codeblocks_tag = false;
	bool in_codeblocks_gdscript_tag = false;

	markdown = "";
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i];

		// For [codeblocks] tags we locate a child [gdscript] tag and turn that
		// into a GDScript code listing. Other languages and the surrounding tag
		// are skipped.
		if (line.contains("[codeblocks]")) {
			in_codeblocks_tag = true;
			continue;
		}
		if (in_codeblocks_tag && line.contains("[/codeblocks]")) {
			in_codeblocks_tag = false;
			continue;
		}
		if (in_codeblocks_tag) {
			if (line.contains("[gdscript]")) {
				in_codeblocks_gdscript_tag = true;
				line = "```gdscript";
			} else if (in_codeblocks_gdscript_tag && line.contains("[/gdscript]")) {
				line = "```";
				in_codeblocks_gdscript_tag = false;
			} else if (!in_codeblocks_gdscript_tag) {
				continue;
			}
		}

		// We need to account for both [codeblock] and [codeblock lang=...].
		String codeblock_lang = "gdscript";
		int block_start = line.find("[codeblock");
		if (block_start != -1) {
			int bracket_pos = line.find_char(']', block_start);
			if (bracket_pos != -1) {
				int lang_start = line.find("lang=", block_start);
				if (lang_start != -1 && lang_start < bracket_pos) {
					constexpr int LANG_PARAM_LENGTH = 5; // Length of "lang=".
					int lang_value_start = lang_start + LANG_PARAM_LENGTH;
					int lang_end = bracket_pos;
					if (lang_value_start < lang_end) {
						codeblock_lang = line.substr(lang_value_start, lang_end - lang_value_start);
					}
				}
				in_codeblock_tag = true;
				line = "```" + codeblock_lang;
			}
		}

		if (in_codeblock_tag && line.contains("[/codeblock]")) {
			line = "```";
			in_codeblock_tag = false;
		}

		if (!in_codeblock_tag) {
			line = line.strip_edges();
			line = line.replace("[br]", "\n\n");

			line = line.replace("[code]", "`");
			line = line.replace("[/code]", "`");
			line = line.replace("[i]", "*");
			line = line.replace("[/i]", "*");
			line = line.replace("[b]", "**");
			line = line.replace("[/b]", "**");
			line = line.replace("[u]", "__");
			line = line.replace("[/u]", "__");
			line = line.replace("[s]", "~~");
			line = line.replace("[/s]", "~~");
			line = line.replace("[kbd]", "`");
			line = line.replace("[/kbd]", "`");
			line = line.replace("[center]", "");
			line = line.replace("[/center]", "");
			line = line.replace("[/font]", "");
			line = line.replace("[/color]", "");
			line = line.replace("[/img]", "");

			// Convert remaining simple bracketed class names to backticks and literal brackets.
			// This handles cases like [Node2D], [Sprite2D], etc. and [lb] and [rb].
			int pos = 0;
			while ((pos = line.find_char('[', pos)) != -1) {
				// Replace the special cases for [lb] and [rb] first and walk
				// past them to avoid conflicts with class names.
				const bool is_within_bounds = pos + 4 <= line.length();
				if (is_within_bounds && line.substr(pos, 4) == "[lb]") {
					line = line.substr(0, pos) + "\\[" + line.substr(pos + 4);
					// We advance past the newly inserted `\\` and `[` characters (2 chars) so the
					// next `line.find()` does not stop at the same position.
					pos += 2;
					continue;
				} else if (is_within_bounds && line.substr(pos, 4) == "[rb]") {
					line = line.substr(0, pos) + "\\]" + line.substr(pos + 4);
					pos += 2;
					continue;
				}

				// Replace class names in brackets.
				int end_pos = line.find_char(']', pos);
				if (end_pos == -1) {
					break;
				}

				String content = line.substr(pos + 1, end_pos - pos - 1);
				// We only convert if it looks like a simple class name (no spaces, no special chars).
				// GDScript supports unicode characters as identifiers so we only exclude markers of other BBCode tags to avoid conflicts.
				bool is_class_name = (!content.is_empty() && content != "url" && !content.contains_char(' ') && !content.contains_char('=') && !content.contains_char('/'));
				if (is_class_name) {
					line = line.substr(0, pos) + "`" + content + "`" + line.substr(end_pos + 1);
					pos += content.length() + 2;
				} else {
					pos = end_pos + 1;
				}
			}

			constexpr int URL_OPEN_TAG_LENGTH = 5; // Length of "[url=".
			constexpr int URL_CLOSE_TAG_LENGTH = 6; // Length of "[/url]".

			// This is for the case [url=$url]$text[/url].
			pos = 0;
			while ((pos = line.find("[url=", pos)) != -1) {
				int url_end = line.find_char(']', pos);
				int close_start = line.find("[/url]", url_end);
				if (url_end == -1 || close_start == -1) {
					break;
				}

				String url = line.substr(pos + URL_OPEN_TAG_LENGTH, url_end - pos - URL_OPEN_TAG_LENGTH);
				String text = line.substr(url_end + 1, close_start - url_end - 1);
				String replacement = "[" + text + "](" + url + ")";
				line = line.substr(0, pos) + replacement + line.substr(close_start + URL_CLOSE_TAG_LENGTH);
				pos += replacement.length();
			}

			// This is for the case [url]$url[/url].
			pos = 0;
			while ((pos = line.find("[url]", pos)) != -1) {
				int close_pos = line.find("[/url]", pos);
				if (close_pos == -1) {
					break;
				}

				String url = line.substr(pos + URL_OPEN_TAG_LENGTH, close_pos - pos - URL_OPEN_TAG_LENGTH);
				String replacement = "[" + url + "](" + url + ")";
				line = line.substr(0, pos) + replacement + line.substr(close_pos + URL_CLOSE_TAG_LENGTH);
				pos += replacement.length();
			}

			// Replace the various link types with inline code ([class MyNode] to `MyNode`).
			// Uses a while loop because there can occasionally be multiple links of the same type in a single line.
			const Vector<String> link_start_patterns = {
				"[class ", "[method ", "[member ", "[signal ", "[enum ", "[constant ",
				"[annotation ", "[constructor ", "[operator ", "[theme_item ", "[param "
			};
			for (const String &pattern : link_start_patterns) {
				int pattern_pos = 0;
				while ((pattern_pos = line.find(pattern, pattern_pos)) != -1) {
					int end_pos = line.find_char(']', pattern_pos);
					if (end_pos == -1) {
						break;
					}

					String content = line.substr(pattern_pos + pattern.length(), end_pos - pattern_pos - pattern.length());
					String replacement = "`" + content + "`";
					line = line.substr(0, pattern_pos) + replacement + line.substr(end_pos + 1);
					pattern_pos += replacement.length();
				}
			}

			// Remove tags with attributes like [color=red], as they don't have a direct Markdown
			// equivalent supported by external tools.
			const String attribute_tags[] = {
				"color", "font", "img"
			};
			for (const String &tag_name : attribute_tags) {
				int tag_pos = 0;
				while ((tag_pos = line.find("[" + tag_name + "=", tag_pos)) != -1) {
					int end_pos = line.find_char(']', tag_pos);
					if (end_pos == -1) {
						break;
					}

					line = line.substr(0, tag_pos) + line.substr(end_pos + 1);
				}
			}
		}

		if (i < lines.size() - 1) {
			line += "\n";
		}
		markdown += line;
	}
	return markdown;
}
