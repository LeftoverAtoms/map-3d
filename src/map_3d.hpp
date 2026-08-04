#pragma once

#include <q3/clipmap.hpp>
#include <q3/entity_parser.hpp>
#include <q3/renderer.hpp>

#include <cstdint>
#include <vector>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/string.hpp>

namespace rngo
{
	class Map3D : public godot::Node3D
	{
		GDCLASS(Map3D, godot::Node3D)

	private:
		static Map3D *active_map;

		q3::World world;
		q3::ClipMap clip_map;
		std::vector<q3::Entity> entities;

		godot::Ref<godot::ArrayMesh> visual_mesh;

		godot::String map_path;
		godot::String loaded_map_path;

		uint64_t world_revision = 0;

	protected:
		static void _bind_methods();
		void _notification(int p_what);

	public:
		static Map3D *get_active_map();

		void set_map_path(
			const godot::String &p_path
		);

		godot::String get_map_path() const;
		godot::String get_loaded_map_path() const;

		const q3::World &get_world() const;
		const q3::ClipMap &get_clip_map() const;

		const std::vector<q3::Entity> &
		get_entities() const;

		uint64_t get_world_revision() const;

		bool is_map_loaded() const;

		godot::Ref<godot::ArrayMesh>
		get_visual_mesh() const;

		void load_map();
	};
}