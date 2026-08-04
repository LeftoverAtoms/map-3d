#pragma once

#include "clipmap.hpp"
#include "entity_parser.hpp"
#include "renderer.hpp"

#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/zip_reader.hpp>

namespace rngo::q3
{
	godot::Error BuildInlineModels(
		const World &p_world,
		const ClipMap &p_clip_map,
		const std::vector<Entity> &p_entities,
		const std::vector<godot::Ref<godot::ZIPReader>> &p_paks,
		godot::Node3D *p_parent
	);
}