#include "map_3d.hpp"

#include <q3/bsp_loader.hpp>
#include <q3/clipmap.hpp>
#include <q3/entity_parser.hpp>
#include <q3/inline_model_builder.hpp>
#include <q3/renderer.hpp>
#include <q3/world_mesh_builder.hpp>

#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <cstdint>
#include <utility>
#include <vector>

using namespace godot;

namespace rngo
{
	Map3D *Map3D::active_map = nullptr;

	void Map3D::_bind_methods()
	{
		ClassDB::bind_method(
			D_METHOD(
				"set_map_path",
				"map_path"
			),
			&Map3D::set_map_path
		);

		ClassDB::bind_method(
			D_METHOD(
				"get_map_path"
			),
			&Map3D::get_map_path
		);

		ClassDB::bind_method(
			D_METHOD(
				"get_loaded_map_path"
			),
			&Map3D::get_loaded_map_path
		);

		ClassDB::bind_method(
			D_METHOD(
				"load_map"
			),
			&Map3D::load_map
		);

		ClassDB::bind_method(
			D_METHOD(
				"get_visual_mesh"
			),
			&Map3D::get_visual_mesh
		);

		ADD_PROPERTY(
			PropertyInfo(
				Variant::STRING,
				"map_path",
				PROPERTY_HINT_FILE,
				"*.bsp"
			),
			"set_map_path",
			"get_map_path"
		);

		ADD_PROPERTY(
			PropertyInfo(
				Variant::STRING,
				"loaded_map_path",
				PROPERTY_HINT_NONE,
				"",
				PROPERTY_USAGE_EDITOR |
					PROPERTY_USAGE_READ_ONLY
			),
			"",
			"get_loaded_map_path"
		);

		ADD_SIGNAL(
			MethodInfo(
				"map_loaded",
				PropertyInfo(
					Variant::OBJECT,
					"mesh",
					PROPERTY_HINT_RESOURCE_TYPE,
					"Mesh"
				)
			)
		);
	}

	void Map3D::_notification(int p_what)
	{
		if (p_what == NOTIFICATION_ENTER_TREE)
		{
			ERR_FAIL_COND_MSG(
				active_map != nullptr &&
					active_map != this,
				"Only one Map3D can be active."
			);

			active_map = this;
		}
		else if (p_what == NOTIFICATION_EXIT_TREE)
		{
			if (active_map == this)
				active_map = nullptr;
		}
	}

	Map3D *Map3D::get_active_map()
	{
		return active_map;
	}

	void Map3D::set_map_path(
		const String &p_path)
	{
		map_path = p_path;
	}

	String Map3D::get_map_path() const
	{
		return map_path;
	}

	String Map3D::get_loaded_map_path() const
	{
		return loaded_map_path;
	}

	const q3::World &Map3D::get_world() const
	{
		return world;
	}

	const q3::ClipMap &Map3D::get_clip_map() const
	{
		return clip_map;
	}

	const std::vector<q3::Entity> &
	Map3D::get_entities() const
	{
		return entities;
	}

	uint64_t Map3D::get_world_revision() const
	{
		return world_revision;
	}

	bool Map3D::is_map_loaded() const
	{
		return visual_mesh.is_valid();
	}

	Ref<ArrayMesh> Map3D::get_visual_mesh() const
	{
		return visual_mesh;
	}

	void Map3D::load_map()
	{
		ERR_FAIL_COND_MSG(
			map_path.is_empty(),
			"Map3D: map_path is empty."
		);

		/*
		 * ------------------------------------------------------------
		 * Load BSP
		 * ------------------------------------------------------------
		 */

		q3::BSP bsp;

		const Error bsp_error =
			q3::Q3_LoadBSPFile(
				map_path,
				bsp
			);

		ERR_FAIL_COND_MSG(
			bsp_error != OK,
			"Map3D: failed to load BSP."
		);

		/*
		 * ------------------------------------------------------------
		 * Build renderer world
		 * ------------------------------------------------------------
		 *
		 * Renderer model ownership comes directly from dmodel_t:
		 *
		 *     firstSurface
		 *     numSurfaces
		 *
		 * Model 0 is the world.
		 * Model 1+ are inline brush models.
		 */

		q3::World loaded_world;

		const Error world_error =
			q3::RE_LoadWorldMap(
				bsp,
				loaded_world
			);

		ERR_FAIL_COND_MSG(
			world_error != OK,
			"Map3D: failed to build renderer world."
		);

		/*
		 * ------------------------------------------------------------
		 * Build clip map
		 * ------------------------------------------------------------
		 *
		 * Collision model ownership comes directly from dmodel_t:
		 *
		 *     firstBrush
		 *     numBrushes
		 *
		 * Patch ownership comes from the exact dmodel surface range.
		 */

		q3::ClipMap loaded_clip_map;

		const Error clipmap_error =
			q3::BuildClipMap(
				bsp,
				loaded_clip_map
			);

		ERR_FAIL_COND_MSG(
			clipmap_error != OK,
			"Map3D: failed to build ClipMap."
		);

		/*
		 * ------------------------------------------------------------
		 * Parse entity string
		 * ------------------------------------------------------------
		 *
		 * Entity parsing associates:
		 *
		 *     "model" "*1"
		 *
		 * with dmodel[1].
		 *
		 * It does not decide geometry ownership.
		 */

		std::vector<q3::Entity>
			loaded_entities;

		const Error entity_error =
			q3::ParseEntities(
				bsp.q3_dentdata,
				loaded_entities
			);

		ERR_FAIL_COND_MSG(
			entity_error != OK,
			"Map3D: failed to parse BSP entities."
		);

		/*
		 * ------------------------------------------------------------
		 * Open PK3 archives
		 * ------------------------------------------------------------
		 */

		std::vector<Ref<ZIPReader>> paks;

		static const String pak_paths[] = {
			"user://baseq3/pak0.pk3",
			"user://baseq3/pak1.pk3",
			"user://baseq3/pak2.pk3",
			"user://baseq3/pak3.pk3",
			"user://baseq3/pak4.pk3",
			"user://baseq3/pak5.pk3",
			"user://baseq3/pak6.pk3",
			"user://baseq3/pak7.pk3",
			"user://baseq3/pak8.pk3"
		};

		for (
			const String &pak_path :
				pak_paths
		)
		{
			Ref<ZIPReader> pak;
			pak.instantiate();

			if (
				pak->open(
					pak_path
				) != OK
			)
			{
				continue;
			}

			paks.push_back(
				pak
			);
		}

		ERR_FAIL_COND_MSG(
			paks.empty(),
			"Map3D: no Quake III PK3 files could be opened."
		);

		/*
		 * ------------------------------------------------------------
		 * Build world visual
		 * ------------------------------------------------------------
		 *
		 * BuildArrayMeshFromWorld() must build only dmodel[0].
		 *
		 * No:
		 *
		 *     FindInlineSurfaceAssignments
		 *     spatial matching
		 *     connected-surface flood
		 *     surface extraction
		 *     world-surface exclusion
		 */

		Ref<ArrayMesh> loaded_visual_mesh =
			q3::BuildArrayMeshFromWorld(
				loaded_world,
				paks
			);

		ERR_FAIL_COND_MSG(
			loaded_visual_mesh.is_null(),
			"Map3D: failed to build world ArrayMesh."
		);

		/*
		 * ------------------------------------------------------------
		 * Build world collision
		 * ------------------------------------------------------------
		 *
		 * BuildCollisionShapes() must build only the collision owned
		 * by dmodel[0].
		 *
		 * No world brush/patch is reassigned spatially to inline models.
		 */

		const Error collision_error =
			q3::BuildCollisionShapes(
				loaded_clip_map,
				this
			);

		ERR_FAIL_COND_MSG(
			collision_error != OK,
			"Map3D: failed to build world collision."
		);

		/*
		 * ------------------------------------------------------------
		 * Build inline brush models
		 * ------------------------------------------------------------
		 *
		 * Each dmodel[N], N > 0:
		 *
		 * render ownership:
		 *
		 *     firstSurface
		 *     numSurfaces
		 *
		 * collision ownership:
		 *
		 *     firstBrush
		 *     numBrushes
		 *
		 * Entity association:
		 *
		 *     "model" "*N"
		 *
		 * Nothing is inferred from spatial proximity.
		 */

		const Error inline_error =
			q3::BuildInlineModels(
				loaded_world,
				loaded_clip_map,
				loaded_entities,
				paks,
				this
			);

		for (
			Ref<ZIPReader> &pak :
				paks
		)
		{
			pak->close();
		}

		ERR_FAIL_COND_MSG(
			inline_error != OK,
			"Map3D: failed to build inline models."
		);

		/*
		 * ------------------------------------------------------------
		 * Commit successfully-loaded map
		 * ------------------------------------------------------------
		 */

		world =
			std::move(
				loaded_world
			);

		clip_map =
			std::move(
				loaded_clip_map
			);

		entities =
			std::move(
				loaded_entities
			);

		visual_mesh =
			std::move(
				loaded_visual_mesh
			);

		loaded_map_path =
			map_path;

		++world_revision;

		emit_signal(
			"map_loaded",
			visual_mesh
		);
	}
}