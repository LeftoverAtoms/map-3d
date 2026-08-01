#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot
{
	class Q3BspLoader : public RefCounted
	{
		GDCLASS(Q3BspLoader, RefCounted)

	protected:
		static void _bind_methods();

	public:
		Ref<ArrayMesh> load_mesh(const String &p_path) const;
	};
}
