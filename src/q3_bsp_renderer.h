#pragma once

#include "bsp_format.h"

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot
{
	class Q3BspRenderer : public Node3D
	{
		GDCLASS(Q3BspRenderer, Node3D)

		String bsp_path;
		bool valid = false;
		String error;
		q3::bsp::BspData bsp;

	protected:
		static void _bind_methods();
		void _notification(int p_what);

	public:
		void set_bsp_path(const String &p_path);
		String get_bsp_path() const;

		bool load_bsp(const String &p_path = String());
		void clear();

		bool is_valid() const;
		String get_error() const;
		int get_surface_count() const;
		int get_shader_count() const;
		int get_lightmap_count() const;
	};
}
