#pragma once

#include "bsp_loader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace rngo::q3
{
	using byte = uint8_t;

	enum class SurfaceType : uint8_t
	{
		Bad,
		Skip,
		Face,
		Grid,
		Triangles,
		Flare
	};

	struct Shader
	{
		godot::String name;
		int surface_flags = 0;
		int content_flags = 0;
	};

	struct Lightmap
	{
		std::array<byte, LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT * 4> rgba = {};
	};

	struct Plane
	{
		godot::Vector3 normal;
		float distance = 0.0f;
	};

	struct Node
	{
		godot::AABB bounds;
		uint32_t plane_index = 0;

		// child >= 0 -> World::nodes[child]
		// child < 0  -> World::leaves[-1 - child]
		std::array<int, 2> children = { 0, 0 };

		int parent = -1;
	};

	struct Leaf
	{
		godot::AABB bounds;

		int cluster = -1;
		int area = 0;

		// Range within World::mark_surfaces.
		uint32_t first_mark_surface = 0;
		uint32_t mark_surface_count = 0;

		int parent = -1;
	};

	struct WorldVertex
	{
		godot::Vector3 position;
		godot::Vector3 normal;

		godot::Vector2 texture_uv;
		godot::Vector2 lightmap_uv;

		std::array<byte, 4> color = {};
	};

	struct Surface
	{
		SurfaceType type = SurfaceType::Bad;

		int shader_index = -1;
		int fog_index = -1;
		int lightmap_index = -1;

		// Ranges within World::vertices / World::indices.
		uint32_t first_vertex = 0;
		uint32_t vertex_count = 0;

		uint32_t first_index = 0;
		uint32_t index_count = 0;

		godot::AABB bounds;

		uint32_t view_count = 0;
	};

	struct Fog
	{
		godot::String shader_name;

		int brush_index = -1;
		int visible_side = -1;

		godot::Vector3 color;
		float texture_coordinate_scale = 0.0f;
	};

	struct BrushModel
	{
		godot::AABB bounds;

		// Range within World::surfaces.
		uint32_t first_surface = 0;
		uint32_t surface_count = 0;
	};

	struct World
	{
		godot::String name;
		godot::String base_name;

		std::vector<WorldVertex> vertices;
		std::vector<uint32_t> indices;

		std::vector<Shader> shaders;
		std::vector<Lightmap> lightmaps;

		std::vector<Plane> planes;
		std::vector<Node> nodes;
		std::vector<Leaf> leaves;

		std::vector<Surface> surfaces;
		std::vector<uint32_t> mark_surfaces;

		std::vector<Fog> fogs;
		std::vector<BrushModel> brush_models;

		godot::Vector3 light_grid_origin;
		godot::Vector3 light_grid_size;
		godot::Vector3 light_grid_inverse_size;
		std::array<int, 3> light_grid_bounds = {};

		std::vector<byte> light_grid_data;

		int cluster_count = 0;
		int cluster_bytes = 0;

		std::vector<byte> visibility;
		std::vector<byte> no_visibility;

		std::string entity_string;
		std::size_t entity_parse_offset = 0;
	};

	godot::Error RE_LoadWorldMap(const BSP &p_bsp, World &r_world);
}