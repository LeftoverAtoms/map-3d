#pragma once

#include "bsp_loader.hpp"

#include <cstdint>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace rngo::q3
{
	struct ClipPlane
	{
		godot::Vector3 normal;
		float distance = 0.0f;
	};

	struct ClipBrushSide
	{
		uint32_t plane_index = 0;

		int shader_index = -1;
		int surface_flags = 0;
	};

	struct ClipBrush
	{
		godot::AABB bounds;

		uint32_t first_side = 0;
		uint32_t side_count = 0;

		int shader_index = -1;
		int contents = 0;
	};

	struct ClipPatch
	{
		std::vector<godot::Vector3> faces;

		godot::AABB bounds;

		uint32_t surface_index = 0;

		int shader_index = -1;
		int surface_flags = 0;
		int contents = 0;
	};

	struct ClipModel
	{
		godot::AABB bounds;

		uint32_t first_surface = 0;
		uint32_t surface_count = 0;

		uint32_t first_brush = 0;
		uint32_t brush_count = 0;
	};

	struct ClipMap
	{
		std::vector<ClipPlane> planes;
		std::vector<ClipBrushSide> brush_sides;
		std::vector<ClipBrush> brushes;
		std::vector<ClipPatch> patches;
		std::vector<ClipModel> models;
	};

	godot::Error BuildClipMap(
		const BSP &p_bsp,
		ClipMap &r_clip_map
	);

	godot::Error BuildCollisionShapes(
		const ClipMap &p_clip_map,
		godot::Node3D *p_parent
	);

	godot::Error BuildCollisionShapesExcluding(
		const ClipMap &p_clip_map,
		const std::vector<uint32_t> &p_excluded_brushes,
		const std::vector<uint32_t> &p_excluded_patches,
		godot::Node3D *p_parent
	);

	godot::Error BuildCollisionShapesForModel(
		const ClipMap &p_clip_map,
		uint32_t p_model_index,
		godot::Node3D *p_parent
	);

	godot::Error BuildCollisionShapesFromIndices(
		const ClipMap &p_clip_map,
		const std::vector<uint32_t> &p_brush_indices,
		const std::vector<uint32_t> &p_patch_indices,
		godot::Node3D *p_parent
	);
}