#include "inline_model_builder.hpp"

#include "world_mesh_builder.hpp"

#include <godot_cpp/classes/area3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <cstdint>
#include <vector>

using namespace godot;

namespace rngo::q3
{
	namespace
	{
		godot::Node *GetSceneOwner(Node3D *p_parent)
		{
			godot::Node *owner =
				p_parent->get_owner();

			if (!owner)
				owner = p_parent;

			return owner;
		}

		void SetEditorOwner(
			godot::Node *p_node,
			godot::Node *p_owner)
		{
			if (
				Engine::get_singleton()
					->is_editor_hint()
			)
			{
				p_node->set_owner(
					p_owner
				);
			}
		}

		bool IsTriggerEntity(
			const Entity *p_entity)
		{
			if (!p_entity)
				return false;

			return
				p_entity->classname.rfind(
					"trigger_",
					0
				) == 0;
		}

		String GetModelName(
			const Entity *p_entity,
			uint32_t p_model_index)
		{
			if (
				p_entity &&
				!p_entity->classname.empty()
			)
			{
				return
					String::utf8(
						p_entity
							->classname
							.c_str()
					) +
					"_" +
					String::num_int64(
						static_cast<int64_t>(
							p_model_index
						)
					);
			}

			return
				String("Model_") +
				String::num_int64(
					static_cast<int64_t>(
						p_model_index
					)
				);
		}

		Vector3 GetEntityOrigin(
			const Entity *p_entity)
		{
			if (
				p_entity &&
				p_entity->has_origin
			)
			{
				return p_entity->origin;
			}

			return Vector3();
		}

		void ApplyEntityMetadata(
			Node3D *p_model,
			const Entity *p_entity,
			uint32_t p_model_index)
		{
			p_model->set_meta(
				StringName("model_index"),
				static_cast<int64_t>(
					p_model_index
				)
			);

			p_model->set_meta(
				StringName("model_name"),
				String("*") +
					String::num_int64(
						static_cast<int64_t>(
							p_model_index
						)
					)
			);

			if (!p_entity)
				return;

			if (
				!p_entity->classname.empty()
			)
			{
				p_model->set_meta(
					StringName("classname"),
					String::utf8(
						p_entity
							->classname
							.c_str()
					)
				);
			}

			if (
				!p_entity->targetname.empty()
			)
			{
				p_model->set_meta(
					StringName("targetname"),
					String::utf8(
						p_entity
							->targetname
							.c_str()
					)
				);
			}

			if (p_entity->has_origin)
			{
				p_model->set_meta(
					StringName("q3_origin"),
					p_entity->origin
				);
			}

			if (p_entity->has_angles)
			{
				p_model->set_meta(
					StringName("q3_angles"),
					p_entity->angles
				);
			}
		}

		Error AddVisual(
			const World &p_world,
			uint32_t p_model_index,
			const std::vector<Ref<ZIPReader>> &p_paks,
			const Vector3 &p_origin,
			Node3D *p_model,
			godot::Node *p_scene_owner)
		{
			/*
			 * Quake ownership:
			 *
			 * dmodel_t.firstSurface
			 * dmodel_t.numSurfaces
			 *
			 * BuildArrayMeshFromWorldModel() must use exactly that
			 * model's surface range.
			 *
			 * No world surfaces are searched, inferred, extracted or
			 * reassigned here.
			 */
			const Ref<ArrayMesh> mesh =
				BuildArrayMeshFromWorldModel(
					p_world,
					p_model_index,
					p_paks
				);

			/*
			 * A brush model with numSurfaces == 0 is valid.
			 *
			 * trigger_hurt is a good example of an entity that can
			 * reference a collision-only BSP model.
			 */
			if (mesh.is_null())
				return OK;

			MeshInstance3D *visual =
				memnew(
					MeshInstance3D
				);

			visual->set_name(
				"Visual"
			);

			visual->set_mesh(
				mesh
			);

			/*
			 * Our World vertices are currently stored in compiled BSP
			 * map coordinates.
			 *
			 * Putting the model Node3D at the entity origin while
			 * offsetting the visual by -origin preserves the original
			 * compiled placement:
			 *
			 *     origin + (-origin) + BSP vertex
			 *       == BSP vertex
			 *
			 * This is Godot transform representation only. It does not
			 * alter Quake model ownership.
			 */
			visual->set_position(
				-p_origin
			);

			visual->set_meta(
				StringName("model_index"),
				static_cast<int64_t>(
					p_model_index
				)
			);

			p_model->add_child(
				visual
			);

			SetEditorOwner(
				visual,
				p_scene_owner
			);

			return OK;
		}

		void MoveCollisionShapesToArea(
			godot::Node *p_source,
			Area3D *p_area,
			godot::Node *p_scene_owner)
		{
			for (
				int i =
					p_source->get_child_count() -
					1;
				i >= 0;
				--i
			)
			{
				godot::Node *child =
					p_source->get_child(i);

				CollisionShape3D *shape =
					Object::cast_to<
						CollisionShape3D
					>(child);

				if (shape)
				{
					const Transform3D
						global_transform =
							shape
								->get_global_transform();

					p_source->remove_child(
						shape
					);

					p_area->add_child(
						shape
					);

					shape->set_global_transform(
						global_transform
					);

					SetEditorOwner(
						shape,
						p_scene_owner
					);

					continue;
				}

				MoveCollisionShapesToArea(
					child,
					p_area,
					p_scene_owner
				);
			}
		}

		Error AddNormalCollision(
			const ClipMap &p_clip_map,
			uint32_t p_model_index,
			const Vector3 &p_origin,
			Node3D *p_model,
			godot::Node *p_scene_owner)
		{
			Node3D *collision =
				memnew(Node3D);

			collision->set_name(
				"Collision"
			);

			/*
			 * Collision geometry is currently stored in BSP/map
			 * coordinates just like render geometry.
			 */
			collision->set_position(
				-p_origin
			);

			p_model->add_child(
				collision
			);

			SetEditorOwner(
				collision,
				p_scene_owner
			);

			/*
			 * Quake ownership:
			 *
			 * dmodel_t.firstBrush
			 * dmodel_t.numBrushes
			 *
			 * plus patch surfaces belonging to the model's exact
			 * firstSurface / numSurfaces range.
			 */
			const Error error =
				BuildCollisionShapesForModel(
					p_clip_map,
					p_model_index,
					collision
				);

			if (error != OK)
			{
				p_model->remove_child(
					collision
				);

				collision->queue_free();

				return error;
			}

			return OK;
		}

		Error AddTriggerCollision(
			const ClipMap &p_clip_map,
			const Entity &p_entity,
			uint32_t p_model_index,
			const Vector3 &p_origin,
			Node3D *p_model,
			godot::Node *p_scene_owner)
		{
			/*
			 * First construct exactly the collision owned by dmodel[N].
			 *
			 * No model-0 collision is searched for.
			 */
			Node3D *temporary =
				memnew(Node3D);

			temporary->set_name(
				"_TemporaryCollision"
			);

			temporary->set_position(
				-p_origin
			);

			p_model->add_child(
				temporary
			);

			const Error error =
				BuildCollisionShapesForModel(
					p_clip_map,
					p_model_index,
					temporary
				);

			if (error != OK)
			{
				p_model->remove_child(
					temporary
				);

				temporary->queue_free();

				return error;
			}

			/*
			 * Area3D is the Godot representation of an entity whose
			 * collision should behave as a trigger rather than a solid
			 * physics body.
			 *
			 * This is backend glue. It does not change which brushes
			 * Quake assigned to the model.
			 */
			Area3D *area =
				memnew(
					Area3D
				);

			area->set_name(
				"Trigger"
			);

			area->set_monitoring(
				true
			);

			area->set_monitorable(
				true
			);

			area->set_collision_layer(
				1u << 5
			);

			area->set_collision_mask(
				0xFFFFFFFFu
			);

			area->set_meta(
				StringName("model_index"),
				static_cast<int64_t>(
					p_model_index
				)
			);

			area->set_meta(
				StringName("classname"),
				String::utf8(
					p_entity
						.classname
						.c_str()
				)
			);

			if (
				!p_entity.targetname.empty()
			)
			{
				area->set_meta(
					StringName("targetname"),
					String::utf8(
						p_entity
							.targetname
							.c_str()
					)
				);
			}

			p_model->add_child(
				area
			);

			SetEditorOwner(
				area,
				p_scene_owner
			);

			MoveCollisionShapesToArea(
				temporary,
				area,
				p_scene_owner
			);

			p_model->remove_child(
				temporary
			);

			temporary->queue_free();

			return OK;
		}
	}

	Error BuildInlineModels(
		const World &p_world,
		const ClipMap &p_clip_map,
		const std::vector<Entity> &p_entities,
		const std::vector<Ref<ZIPReader>> &p_paks,
		Node3D *p_parent)
	{
		ERR_FAIL_NULL_V(
			p_parent,
			ERR_INVALID_PARAMETER
		);

		ERR_FAIL_COND_V_MSG(
			p_world.brush_models.size() !=
				p_clip_map.models.size(),
			ERR_INVALID_DATA,
			"Inline model builder: renderer and clipmap model counts differ."
		);

		/*
		 * Remove the previous generated hierarchy when Load Map is
			 * pressed again in the editor.
		 */
		if (
			godot::Node *existing =
				p_parent->get_node_or_null(
					NodePath(
						"Q3InlineModels"
					)
				)
		)
		{
			p_parent->remove_child(
				existing
			);

			existing->queue_free();
		}

		Node3D *models =
			memnew(
				Node3D
			);

		models->set_name(
			"Q3InlineModels"
		);

		p_parent->add_child(
			models
		);

		godot::Node *scene_owner =
			GetSceneOwner(
				p_parent
			);

		SetEditorOwner(
			models,
			scene_owner
		);

		/*
		 * dmodel[0] is the world.
		 *
		 * Quake inline brush models begin at dmodel[1].
		 */
		for (
			uint32_t model_index = 1;
			model_index <
				static_cast<uint32_t>(
					p_world
						.brush_models
						.size()
				);
			++model_index
		)
		{
			const Entity *entity =
				FindEntityForModel(
					p_entities,
					static_cast<int>(
						model_index
					)
				);

			/*
			 * Entity association is through:
			 *
			 *     "model" "*N"
			 *
			 * It is not discovered spatially.
			 */
			const Vector3 origin =
				GetEntityOrigin(
					entity
				);

			Node3D *model =
				memnew(
					Node3D
				);

			model->set_name(
				GetModelName(
					entity,
					model_index
				)
			);

			model->set_position(
				origin
			);

			models->add_child(
				model
			);

			SetEditorOwner(
				model,
				scene_owner
			);

			ApplyEntityMetadata(
				model,
				entity,
				model_index
			);

			const Error visual_error =
				AddVisual(
					p_world,
					model_index,
					p_paks,
					origin,
					model,
					scene_owner
				);

			ERR_FAIL_COND_V_MSG(
				visual_error != OK,
				visual_error,
				String(
					"Inline model builder: failed to build visual for *"
				) +
					String::num_int64(
						static_cast<int64_t>(
							model_index
						)
					) +
					String(".")
			);

			Error collision_error =
				OK;

			if (IsTriggerEntity(entity))
			{
				collision_error =
					AddTriggerCollision(
						p_clip_map,
						*entity,
						model_index,
						origin,
						model,
						scene_owner
					);
			}
			else
			{
				collision_error =
					AddNormalCollision(
						p_clip_map,
						model_index,
						origin,
						model,
						scene_owner
					);
			}

			ERR_FAIL_COND_V_MSG(
				collision_error != OK,
				collision_error,
				String(
					"Inline model builder: failed to build collision for *"
				) +
					String::num_int64(
						static_cast<int64_t>(
							model_index
						)
					) +
					String(".")
			);
		}

		return OK;
	}
}