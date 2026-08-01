#include "q3_bsp_format_loader.h"

#include "q3_bsp_resource.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void Q3BspFormatLoader::_bind_methods() {}

PackedStringArray Q3BspFormatLoader::_get_recognized_extensions() const
{
	PackedStringArray extensions;
	extensions.append("bsp");
	return extensions;
}

bool Q3BspFormatLoader::_recognize_path(const String &p_path, const StringName &p_type) const
{
	return p_path.get_extension().to_lower() == "bsp" && (p_type == StringName() || _handles_type(p_type));
}

bool Q3BspFormatLoader::_handles_type(const StringName &p_type) const
{
	return p_type == StringName("Resource") || p_type == StringName("Q3BspResource");
}

String Q3BspFormatLoader::_get_resource_type(const String &p_path) const
{
	if (p_path.get_extension().to_lower() == "bsp") {
		return "Q3BspResource";
	}
	return String();
}

Variant Q3BspFormatLoader::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const
{
	Ref<Q3BspResource> resource;
	resource.instantiate();
	resource->load_from_path(p_original_path.is_empty() ? p_path : p_original_path);
	resource->take_over_path(p_original_path.is_empty() ? p_path : p_original_path);
	return resource;
}
