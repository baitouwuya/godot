/**************************************************************************/
/*  mcp_resource_import_options.h                                         */
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

#pragma once

#include "core/io/resource_importer.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

struct MCPResourceImportResolution {
	Ref<ResourceImporter> importer;
	List<ResourceImporter::ImportOption> option_definitions;
	HashMap<StringName, Variant> values;
	HashSet<StringName> overridden_options;
	int preset = 0;
	bool importer_explicit = false;
	bool preset_explicit = false;
	bool project_defaults_applied = false;
};

namespace MCPResourceImportOptions {

bool resolve(const Dictionary &p_arguments, const String &p_source_path, MCPResourceImportResolution &r_resolution, Dictionary &r_error);
Dictionary make_result(const String &p_source_path, const MCPResourceImportResolution &p_resolution, Dictionary &r_error);

} // namespace MCPResourceImportOptions
