#pragma once

#include "renderer.hpp"

#include <cstdint>
#include <vector>

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/zip_reader.hpp>

namespace rngo::q3
{
	godot::Ref<godot::ArrayMesh> BuildArrayMeshFromWorld(
		const World &p_world,
		const std::vector<godot::Ref<godot::ZIPReader>> &p_paks
	);

	godot::Ref<godot::ArrayMesh> BuildArrayMeshFromWorldExcludingSurfaces(
		const World &p_world,
		const std::vector<godot::Ref<godot::ZIPReader>> &p_paks,
		const std::vector<uint32_t> &p_excluded_surfaces
	);

	godot::Ref<godot::ArrayMesh> BuildArrayMeshFromWorldModel(
		const World &p_world,
		uint32_t p_model_index,
		const std::vector<godot::Ref<godot::ZIPReader>> &p_paks
	);

	godot::Ref<godot::ArrayMesh> BuildArrayMeshFromSurfaces(
		const World &p_world,
		const std::vector<uint32_t> &p_surface_indices,
		const std::vector<godot::Ref<godot::ZIPReader>> &p_paks
	);
}