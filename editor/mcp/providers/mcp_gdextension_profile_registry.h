/**************************************************************************/
/*  mcp_gdextension_profile_registry.h                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/variant/variant.h"

namespace MCPGDExtensionProfileRegistry {

struct Profile {
	String name;
	String scons_target;
	bool release = false;
};

PackedStringArray names();
bool resolve(const String &p_name, Profile &r_profile, String *r_error = nullptr);
String current_platform();

} // namespace MCPGDExtensionProfileRegistry
