/**************************************************************************/
/*  mcp_runtime_screenshot_capture.cpp                                    */
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

#include "mcp_runtime_screenshot_capture.h"

#include "core/io/image.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/rendering/rendering_server.h"

namespace {

void _set_screenshot_error(String &r_code, String &r_message, const String &p_code, const String &p_message) {
	r_code = p_code;
	r_message = p_message;
}

bool _parse_crop(const Dictionary &p_value, Rect2i &r_crop) {
	static const char *fields[] = { "x", "y", "width", "height" };
	for (const char *field : fields) {
		if (!p_value.has(field) || !p_value[field].is_num()) {
			return false;
		}
		const double value = p_value[field];
		if (!Math::is_finite(value)) {
			return false;
		}
	}
	const double x = p_value["x"];
	const double y = p_value["y"];
	const double width = p_value["width"];
	const double height = p_value["height"];
	const Rect2 crop(Vector2(x, y), Vector2(width, height));
	if (crop.size.x < 1.0 || crop.size.y < 1.0) {
		return false;
	}
	r_crop = Rect2i(crop);
	return true;
}

} // namespace

Error MCPRuntimeScreenshotCapture::capture_image(Ref<Image> &r_image, String &r_error_message) {
	r_image.unref();
	r_error_message = String();
	SceneTree *scene_tree = SceneTree::get_singleton();
	Viewport *root = scene_tree ? scene_tree->get_root() : nullptr;
	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!root || !rendering_server) {
		r_error_message = "Root viewport image is unavailable.";
		return ERR_UNAVAILABLE;
	}
	const RID texture_rid = rendering_server->viewport_get_texture(root->get_viewport_rid());
	r_image = texture_rid.is_valid() ? rendering_server->texture_2d_get(texture_rid) : Ref<Image>();
	if (r_image.is_null() || r_image->is_empty()) {
		r_error_message = "Root viewport image is unavailable.";
		r_image.unref();
		return ERR_UNAVAILABLE;
	}
	r_image = r_image->duplicate();
	if (r_image.is_null()) {
		r_error_message = "Unable to duplicate the root viewport image.";
		return ERR_CANT_CREATE;
	}
	r_image->clear_mipmaps();
	r_image->convert(Image::FORMAT_RGBA8);
	return OK;
}

Error MCPRuntimeScreenshotCapture::capture(const Dictionary &p_arguments, Dictionary &r_result, String &r_error_code, String &r_error_message) {
	Ref<Image> image;
	if (capture_image(image, r_error_message) != OK) {
		r_error_code = "RUNTIME_SCREENSHOT_UNAVAILABLE";
		return ERR_UNAVAILABLE;
	}
	const Variant crop_value = p_arguments.get("crop", Variant());
	if (crop_value.get_type() != Variant::NIL) {
		Rect2i requested;
		if (crop_value.get_type() != Variant::DICTIONARY || !_parse_crop(crop_value, requested)) {
			_set_screenshot_error(r_error_code, r_error_message, "INVALID_ARGUMENTS", "crop must define a non-empty finite x, y, width, and height rectangle.");
			return ERR_INVALID_PARAMETER;
		}
		const Rect2i clipped = requested.intersection(Rect2i(Vector2i(), image->get_size()));
		if (!clipped.has_area()) {
			_set_screenshot_error(r_error_code, r_error_message, "INVALID_ARGUMENTS", "crop does not intersect the runtime viewport.");
			return ERR_INVALID_PARAMETER;
		}
		image = image->get_region(clipped);
	}
	const Variant name_value = p_arguments.get("name", String());
	if (name_value.get_type() != Variant::STRING) {
		_set_screenshot_error(r_error_code, r_error_message, "INVALID_ARGUMENTS", "name must be a string.");
		return ERR_INVALID_PARAMETER;
	}
	String name = String(name_value).validate_filename();
	if (name.length() > 64) {
		name = name.left(64);
	}
	const String suffix = name.is_empty() ? String() : "-" + name;
	const String path = OS::get_singleton()->get_temp_path().path_join(vformat("godot-mcp-runtime-%d-%d%s.png",
			OS::get_singleton()->get_process_id(), OS::get_singleton()->get_ticks_usec(), suffix));
	const Error save_error = image->save_png(path);
	if (save_error != OK) {
		_set_screenshot_error(r_error_code, r_error_message, "RUNTIME_SCREENSHOT_FAILED", vformat("Unable to save runtime screenshot (error %d).", int(save_error)));
		return save_error;
	}
	r_result["name"] = String(name_value);
	r_result["path"] = path;
	r_result["width"] = image->get_width();
	r_result["height"] = image->get_height();
	return OK;
}
