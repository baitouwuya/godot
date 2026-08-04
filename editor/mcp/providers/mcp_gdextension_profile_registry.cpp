/**************************************************************************/
/*  mcp_gdextension_profile_registry.cpp                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "mcp_gdextension_profile_registry.h"

namespace MCPGDExtensionProfileRegistry {

PackedStringArray names() {
	return PackedStringArray{ "debug", "release" };
}

bool resolve(const String &p_name, Profile &r_profile, String *r_error) {
	r_profile = Profile();
	if (p_name == "debug") {
		r_profile.name = p_name;
		r_profile.scons_target = "template_debug";
		return true;
	}
	if (p_name == "release") {
		r_profile.name = p_name;
		r_profile.scons_target = "template_release";
		r_profile.release = true;
		return true;
	}
	if (r_error) {
		*r_error = "profile must be debug or release; allowed values: " + String(", ").join(names());
	}
	return false;
}

String current_platform() {
#if defined(MACOS_ENABLED)
	return "macos";
#elif defined(WINDOWS_ENABLED)
	return "windows";
#elif defined(LINUXBSD_ENABLED)
	return "linux";
#elif defined(ANDROID_ENABLED)
	return "android";
#elif defined(IOS_ENABLED)
	return "ios";
#else
	return String();
#endif
}

} // namespace MCPGDExtensionProfileRegistry
