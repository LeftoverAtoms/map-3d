#pragma once

#include "bsp_format.h"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace godot
{
	class Q3BspResource : public Resource
	{
		GDCLASS(Q3BspResource, Resource)

		String path;
		bool valid = false;
		String error;
		q3::bsp::BspData bsp;

	protected:
		static void _bind_methods();
		bool _get(const StringName &p_name, Variant &r_property) const;
		void _get_property_list(List<PropertyInfo> *p_list) const;

	public:
		bool load_from_path(const String &p_path);

		String get_path() const;
		bool is_valid() const;
		String get_error() const;
		Dictionary get_header() const;
		Dictionary get_counts() const;
		Array get_lump_info() const;
		String get_entities() const;
		Array get_shaders() const;
		Array get_planes() const;
		Array get_nodes() const;
		Array get_leafs() const;
		PackedInt32Array get_leafsurfaces() const;
		PackedInt32Array get_leafbrushes() const;
		Array get_models() const;
		Array get_brushes() const;
		Array get_brushsides() const;
		Array get_drawVerts() const;
		PackedInt32Array get_drawIndexes() const;
		Array get_fogs() const;
		PackedByteArray get_lightmaps() const;
		PackedByteArray get_lightgrid() const;
		PackedByteArray get_visibility() const;
		Dictionary get_all_data() const;
	};
}
