#include "clipmap.hpp"

#include "utility.hpp"

#include <godot_cpp/classes/area3d.hpp>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

using namespace godot;

namespace rngo::q3
{
	namespace
	{
		inline constexpr real_t PLANE_EPSILON = 0.01f;
		inline constexpr real_t POINT_EPSILON = 0.01f;
		inline constexpr int PATCH_SUBDIVISIONS = 8;

		inline constexpr uint32_t CONTENTS_SOLID = 0x00000001u;
		inline constexpr uint32_t CONTENTS_LAVA = 0x00000008u;
		inline constexpr uint32_t CONTENTS_SLIME = 0x00000010u;
		inline constexpr uint32_t CONTENTS_WATER = 0x00000020u;
		inline constexpr uint32_t CONTENTS_FOG = 0x00000040u;
		inline constexpr uint32_t CONTENTS_AREAPORTAL = 0x00008000u;
		inline constexpr uint32_t CONTENTS_PLAYERCLIP = 0x00010000u;
		inline constexpr uint32_t CONTENTS_MONSTERCLIP = 0x00020000u;
		inline constexpr uint32_t CONTENTS_TELEPORTER = 0x00040000u;
		inline constexpr uint32_t CONTENTS_JUMPPAD = 0x00080000u;
		inline constexpr uint32_t CONTENTS_CLUSTERPORTAL = 0x00100000u;
		inline constexpr uint32_t CONTENTS_DONOTENTER = 0x00200000u;
		inline constexpr uint32_t CONTENTS_BOTCLIP = 0x00400000u;
		inline constexpr uint32_t CONTENTS_MOVER = 0x00800000u;
		inline constexpr uint32_t CONTENTS_ORIGIN = 0x01000000u;
		inline constexpr uint32_t CONTENTS_BODY = 0x02000000u;
		inline constexpr uint32_t CONTENTS_CORPSE = 0x04000000u;
		inline constexpr uint32_t CONTENTS_DETAIL = 0x08000000u;
		inline constexpr uint32_t CONTENTS_STRUCTURAL = 0x10000000u;
		inline constexpr uint32_t CONTENTS_TRANSLUCENT = 0x20000000u;
		inline constexpr uint32_t CONTENTS_TRIGGER = 0x40000000u;
		inline constexpr uint32_t CONTENTS_NODROP = 0x80000000u;

		inline constexpr uint32_t SURF_NONSOLID = 0x00004000u;

		inline constexpr int GODOT_LAYER_WORLD_SOLID = 1;
		inline constexpr int GODOT_LAYER_PLAYER_CLIP = 2;
		inline constexpr int GODOT_LAYER_MONSTER_CLIP = 3;
		inline constexpr int GODOT_LAYER_BOT_CLIP = 4;
		inline constexpr int GODOT_LAYER_LIQUID = 5;
		inline constexpr int GODOT_LAYER_TRIGGER = 6;
		inline constexpr int GODOT_LAYER_TELEPORTER = 7;
		inline constexpr int GODOT_LAYER_JUMPPAD = 8;
		inline constexpr int GODOT_LAYER_NODROP = 9;
		inline constexpr int GODOT_LAYER_FOG = 10;

		struct BoxBrush
		{
			bool valid = false;
			Vector3 min;
			Vector3 max;
		};

		uint32_t ContentsBits(int p_contents)
		{
			return static_cast<uint32_t>(p_contents);
		}

		bool HasContents(int p_contents, uint32_t p_flag)
		{
			return (ContentsBits(p_contents) & p_flag) != 0;
		}

		uint32_t GodotLayerBit(int p_layer)
		{
			return 1u << static_cast<uint32_t>(p_layer - 1);
		}

		bool HasBlockingContents(int p_contents)
		{
			return (ContentsBits(p_contents) & (
				CONTENTS_SOLID |
				CONTENTS_PLAYERCLIP |
				CONTENTS_MONSTERCLIP |
				CONTENTS_BOTCLIP
			)) != 0;
		}

		bool HasAreaContents(int p_contents)
		{
			return (ContentsBits(p_contents) & (
				CONTENTS_LAVA |
				CONTENTS_SLIME |
				CONTENTS_WATER |
				CONTENTS_FOG |
				CONTENTS_TELEPORTER |
				CONTENTS_JUMPPAD |
				CONTENTS_TRIGGER |
				CONTENTS_NODROP
			)) != 0;
		}

		uint32_t GetBodyCollisionLayers(int p_contents)
		{
			uint32_t layers = 0;

			if (HasContents(p_contents, CONTENTS_SOLID))
				layers |= GodotLayerBit(GODOT_LAYER_WORLD_SOLID);

			if (HasContents(p_contents, CONTENTS_PLAYERCLIP))
				layers |= GodotLayerBit(GODOT_LAYER_PLAYER_CLIP);

			if (HasContents(p_contents, CONTENTS_MONSTERCLIP))
				layers |= GodotLayerBit(GODOT_LAYER_MONSTER_CLIP);

			if (HasContents(p_contents, CONTENTS_BOTCLIP))
				layers |= GodotLayerBit(GODOT_LAYER_BOT_CLIP);

			return layers;
		}

		uint32_t GetAreaCollisionLayers(int p_contents)
		{
			uint32_t layers = 0;

			if (
				HasContents(p_contents, CONTENTS_LAVA) ||
				HasContents(p_contents, CONTENTS_SLIME) ||
				HasContents(p_contents, CONTENTS_WATER)
			)
			{
				layers |= GodotLayerBit(GODOT_LAYER_LIQUID);
			}

			if (HasContents(p_contents, CONTENTS_TRIGGER))
				layers |= GodotLayerBit(GODOT_LAYER_TRIGGER);

			if (HasContents(p_contents, CONTENTS_TELEPORTER))
				layers |= GodotLayerBit(GODOT_LAYER_TELEPORTER);

			if (HasContents(p_contents, CONTENTS_JUMPPAD))
				layers |= GodotLayerBit(GODOT_LAYER_JUMPPAD);

			if (HasContents(p_contents, CONTENTS_NODROP))
				layers |= GodotLayerBit(GODOT_LAYER_NODROP);

			if (HasContents(p_contents, CONTENTS_FOG))
				layers |= GodotLayerBit(GODOT_LAYER_FOG);

			return layers;
		}

		String GetContentsName(int p_contents)
		{
			const uint32_t contents =
				ContentsBits(p_contents);

			String name;

			auto append = [&name](const char *p_flag)
			{
				if (!name.is_empty())
					name += "_";

				name += p_flag;
			};

			if (contents & CONTENTS_SOLID) append("SOLID");
			if (contents & CONTENTS_LAVA) append("LAVA");
			if (contents & CONTENTS_SLIME) append("SLIME");
			if (contents & CONTENTS_WATER) append("WATER");
			if (contents & CONTENTS_FOG) append("FOG");
			if (contents & CONTENTS_AREAPORTAL) append("AREAPORTAL");
			if (contents & CONTENTS_PLAYERCLIP) append("PLAYERCLIP");
			if (contents & CONTENTS_MONSTERCLIP) append("MONSTERCLIP");
			if (contents & CONTENTS_TELEPORTER) append("TELEPORTER");
			if (contents & CONTENTS_JUMPPAD) append("JUMPPAD");
			if (contents & CONTENTS_CLUSTERPORTAL) append("CLUSTERPORTAL");
			if (contents & CONTENTS_DONOTENTER) append("DONOTENTER");
			if (contents & CONTENTS_BOTCLIP) append("BOTCLIP");
			if (contents & CONTENTS_MOVER) append("MOVER");
			if (contents & CONTENTS_ORIGIN) append("ORIGIN");
			if (contents & CONTENTS_BODY) append("BODY");
			if (contents & CONTENTS_CORPSE) append("CORPSE");
			if (contents & CONTENTS_DETAIL) append("DETAIL");
			if (contents & CONTENTS_STRUCTURAL) append("STRUCTURAL");
			if (contents & CONTENTS_TRANSLUCENT) append("TRANSLUCENT");
			if (contents & CONTENTS_TRIGGER) append("TRIGGER");
			if (contents & CONTENTS_NODROP) append("NODROP");

			if (name.is_empty())
				name = "NONE";

			return name;
		}

		bool SurfaceBelongsToModel(
			const ClipModel &p_model,
			uint32_t p_surface_index)
		{
			if (p_surface_index < p_model.first_surface)
				return false;

			return
				p_surface_index - p_model.first_surface <
				p_model.surface_count;
		}

		AABB BuildBounds(
			const std::vector<Vector3> &p_points)
		{
			if (p_points.empty())
				return AABB();

			Vector3 min = p_points[0];
			Vector3 max = p_points[0];

			for (const Vector3 &point : p_points)
			{
				min.x = MIN(min.x, point.x);
				min.y = MIN(min.y, point.y);
				min.z = MIN(min.z, point.z);

				max.x = MAX(max.x, point.x);
				max.y = MAX(max.y, point.y);
				max.z = MAX(max.z, point.z);
			}

			return AABB(
				min,
				max - min
			);
		}

		Vector3 QuadraticPosition(
			const Vector3 &p_a,
			const Vector3 &p_b,
			const Vector3 &p_c,
			real_t p_t)
		{
			const real_t inverse =
				1.0f - p_t;

			return
				p_a * inverse * inverse +
				p_b * 2.0f * inverse * p_t +
				p_c * p_t * p_t;
		}

		Vector3 EvaluatePatchPosition(
			const Vector3 p_control[3][3],
			real_t p_u,
			real_t p_v)
		{
			const Vector3 row0 =
				QuadraticPosition(
					p_control[0][0],
					p_control[1][0],
					p_control[2][0],
					p_u
				);

			const Vector3 row1 =
				QuadraticPosition(
					p_control[0][1],
					p_control[1][1],
					p_control[2][1],
					p_u
				);

			const Vector3 row2 =
				QuadraticPosition(
					p_control[0][2],
					p_control[1][2],
					p_control[2][2],
					p_u
				);

			return
				QuadraticPosition(
					row0,
					row1,
					row2,
					p_v
				);
		}

		Error BuildPatchFaces(
			const BSP &p_bsp,
			const q3_dsurface_t &p_surface,
			std::vector<Vector3> &r_faces)
		{
			ERR_FAIL_COND_V_MSG(
				p_surface.patchWidth < 3 ||
					p_surface.patchHeight < 3 ||
					(p_surface.patchWidth & 1) == 0 ||
					(p_surface.patchHeight & 1) == 0,
				ERR_FILE_CORRUPT,
				"BuildClipMap: invalid patch dimensions."
			);

			ERR_FAIL_COND_V_MSG(
				!IsValidRange(
					p_surface.firstVert,
					p_surface.numVerts,
					p_bsp.q3_drawVerts.size()
				),
				ERR_FILE_CORRUPT,
				"BuildClipMap: invalid patch vertex range."
			);

			const std::size_t width =
				static_cast<std::size_t>(
					p_surface.patchWidth
				);

			const std::size_t height =
				static_cast<std::size_t>(
					p_surface.patchHeight
				);

			ERR_FAIL_COND_V_MSG(
				width >
					std::numeric_limits<std::size_t>::max() /
						height,
				ERR_FILE_CORRUPT,
				"BuildClipMap: patch dimensions overflow."
			);

			ERR_FAIL_COND_V_MSG(
				static_cast<std::size_t>(
					p_surface.numVerts
				) <
					width * height,
				ERR_FILE_CORRUPT,
				"BuildClipMap: patch does not contain enough control points."
			);

			for (
				int y = 0;
				y < p_surface.patchHeight - 2;
				y += 2
			)
			{
				for (
					int x = 0;
					x < p_surface.patchWidth - 2;
					x += 2
				)
				{
					Vector3 control[3][3];

					for (int cy = 0; cy < 3; ++cy)
					{
						for (int cx = 0; cx < 3; ++cx)
						{
							const int local_index =
								(y + cy) *
									p_surface.patchWidth +
								x +
								cx;

							const std::size_t source_index =
								static_cast<std::size_t>(
									p_surface.firstVert
								) +
								static_cast<std::size_t>(
									local_index
								);

							control[cx][cy] =
								QuakeToGodot(
									p_bsp.q3_drawVerts[
										source_index
									].xyz
								);
						}
					}

					Vector3 vertices[
						PATCH_SUBDIVISIONS + 1
					][
						PATCH_SUBDIVISIONS + 1
					];

					for (
						int v = 0;
						v <= PATCH_SUBDIVISIONS;
						++v
					)
					{
						for (
							int u = 0;
							u <= PATCH_SUBDIVISIONS;
							++u
						)
						{
							const real_t patch_u =
								static_cast<real_t>(u) /
								static_cast<real_t>(
									PATCH_SUBDIVISIONS
								);

							const real_t patch_v =
								static_cast<real_t>(v) /
								static_cast<real_t>(
									PATCH_SUBDIVISIONS
								);

							vertices[v][u] =
								EvaluatePatchPosition(
									control,
									patch_u,
									patch_v
								);
						}
					}

					for (
						int v = 0;
						v < PATCH_SUBDIVISIONS;
						++v
					)
					{
						for (
							int u = 0;
							u < PATCH_SUBDIVISIONS;
							++u
						)
						{
							const Vector3 &i0 =
								vertices[v][u];

							const Vector3 &i1 =
								vertices[v][u + 1];

							const Vector3 &i2 =
								vertices[v + 1][u];

							const Vector3 &i3 =
								vertices[v + 1][u + 1];

							r_faces.push_back(i0);
							r_faces.push_back(i2);
							r_faces.push_back(i1);

							r_faces.push_back(i1);
							r_faces.push_back(i2);
							r_faces.push_back(i3);
						}
					}
				}
			}

			return OK;
		}

		BoxBrush GetBoxBrush(
			const ClipMap &p_clip_map,
			const ClipBrush &p_brush)
		{
			BoxBrush box;

			if (p_brush.side_count != 6)
				return box;

			bool positive_x = false;
			bool negative_x = false;
			bool positive_y = false;
			bool negative_y = false;
			bool positive_z = false;
			bool negative_z = false;

			for (
				uint32_t i = 0;
				i < p_brush.side_count;
				++i
			)
			{
				const ClipBrushSide &side =
					p_clip_map.brush_sides[
						p_brush.first_side + i
					];

				const ClipPlane &plane =
					p_clip_map.planes[
						side.plane_index
					];

				if (
					plane.normal.is_equal_approx(
						Vector3(1.0f, 0.0f, 0.0f)
					)
				)
				{
					if (positive_x)
						return box;

					positive_x = true;
					box.max.x = plane.distance;
				}
				else if (
					plane.normal.is_equal_approx(
						Vector3(-1.0f, 0.0f, 0.0f)
					)
				)
				{
					if (negative_x)
						return box;

					negative_x = true;
					box.min.x = -plane.distance;
				}
				else if (
					plane.normal.is_equal_approx(
						Vector3(0.0f, 1.0f, 0.0f)
					)
				)
				{
					if (positive_y)
						return box;

					positive_y = true;
					box.max.y = plane.distance;
				}
				else if (
					plane.normal.is_equal_approx(
						Vector3(0.0f, -1.0f, 0.0f)
					)
				)
				{
					if (negative_y)
						return box;

					negative_y = true;
					box.min.y = -plane.distance;
				}
				else if (
					plane.normal.is_equal_approx(
						Vector3(0.0f, 0.0f, 1.0f)
					)
				)
				{
					if (positive_z)
						return box;

					positive_z = true;
					box.max.z = plane.distance;
				}
				else if (
					plane.normal.is_equal_approx(
						Vector3(0.0f, 0.0f, -1.0f)
					)
				)
				{
					if (negative_z)
						return box;

					negative_z = true;
					box.min.z = -plane.distance;
				}
				else
				{
					return box;
				}
			}

			box.valid =
				positive_x &&
				negative_x &&
				positive_y &&
				negative_y &&
				positive_z &&
				negative_z &&
				box.max.x > box.min.x &&
				box.max.y > box.min.y &&
				box.max.z > box.min.z;

			return box;
		}

		bool IntersectPlanes(
			const ClipPlane &p_a,
			const ClipPlane &p_b,
			const ClipPlane &p_c,
			Vector3 &r_point)
		{
			const Vector3 bc =
				p_b.normal.cross(
					p_c.normal
				);

			const real_t determinant =
				p_a.normal.dot(bc);

			if (
				Math::abs(
					determinant
				) <= CMP_EPSILON
			)
			{
				return false;
			}

			r_point =
				(
					bc * p_a.distance +
					p_c.normal.cross(
						p_a.normal
					) * p_b.distance +
					p_a.normal.cross(
						p_b.normal
					) * p_c.distance
				) /
				determinant;

			return true;
		}

		bool IsPointInsideBrush(
			const ClipMap &p_clip_map,
			const ClipBrush &p_brush,
			const Vector3 &p_point)
		{
			for (
				uint32_t i = 0;
				i < p_brush.side_count;
				++i
			)
			{
				const ClipBrushSide &side =
					p_clip_map.brush_sides[
						p_brush.first_side + i
					];

				const ClipPlane &plane =
					p_clip_map.planes[
						side.plane_index
					];

				if (
					plane.normal.dot(
						p_point
					) -
						plane.distance >
					PLANE_EPSILON
				)
				{
					return false;
				}
			}

			return true;
		}

		bool ContainsPoint(
			const std::vector<Vector3> &p_points,
			const Vector3 &p_point)
		{
			const real_t epsilon_squared =
				POINT_EPSILON *
				POINT_EPSILON;

			for (
				const Vector3 &point :
					p_points
			)
			{
				if (
					point.distance_squared_to(
						p_point
					) <=
					epsilon_squared
				)
				{
					return true;
				}
			}

			return false;
		}

		PackedVector3Array BuildConvexBrushPoints(
			const ClipMap &p_clip_map,
			const ClipBrush &p_brush)
		{
			std::vector<Vector3> points;

			for (
				uint32_t a = 0;
				a < p_brush.side_count;
				++a
			)
			{
				const ClipBrushSide &side_a =
					p_clip_map.brush_sides[
						p_brush.first_side + a
					];

				const ClipPlane &plane_a =
					p_clip_map.planes[
						side_a.plane_index
					];

				for (
					uint32_t b = a + 1;
					b < p_brush.side_count;
					++b
				)
				{
					const ClipBrushSide &side_b =
						p_clip_map.brush_sides[
							p_brush.first_side + b
						];

					const ClipPlane &plane_b =
						p_clip_map.planes[
							side_b.plane_index
						];

					for (
						uint32_t c = b + 1;
						c < p_brush.side_count;
						++c
					)
					{
						const ClipBrushSide &side_c =
							p_clip_map.brush_sides[
								p_brush.first_side + c
							];

						const ClipPlane &plane_c =
							p_clip_map.planes[
								side_c.plane_index
							];

						Vector3 point;

						if (
							!IntersectPlanes(
								plane_a,
								plane_b,
								plane_c,
								point
							)
						)
						{
							continue;
						}

						if (
							!IsPointInsideBrush(
								p_clip_map,
								p_brush,
								point
							)
						)
						{
							continue;
						}

						if (
							ContainsPoint(
								points,
								point
							)
						)
						{
							continue;
						}

						points.push_back(
							point
						);
					}
				}
			}

			PackedVector3Array result;

			result.resize(
				static_cast<int64_t>(
					points.size()
				)
			);

			for (
				int64_t i = 0;
				i < result.size();
				++i
			)
			{
				result.set(
					i,
					points[
						static_cast<std::size_t>(
							i
						)
					]
				);
			}

			return result;
		}

		AABB BuildBoundsFromPoints(
			const PackedVector3Array &p_points)
		{
			if (p_points.is_empty())
				return AABB();

			AABB bounds(
				p_points[0],
				Vector3()
			);

			for (
				int64_t i = 1;
				i < p_points.size();
				++i
			)
			{
				bounds =
					bounds.expand(
						p_points[i]
					);
			}

			return bounds;
		}

		void BuildBrushBounds(
			const ClipMap &p_clip_map,
			ClipBrush &r_brush)
		{
			const BoxBrush box =
				GetBoxBrush(
					p_clip_map,
					r_brush
				);

			if (box.valid)
			{
				r_brush.bounds =
					AABB(
						box.min,
						box.max - box.min
					);

				return;
			}

			const PackedVector3Array points =
				BuildConvexBrushPoints(
					p_clip_map,
					r_brush
				);

			if (points.size() >= 4)
			{
				r_brush.bounds =
					BuildBoundsFromPoints(
						points
					);
			}
		}

		Error LoadPlanes(
			const BSP &p_bsp,
			ClipMap &r_clip_map)
		{
			ERR_FAIL_COND_V_MSG(
				p_bsp.q3_dplanes.empty(),
				ERR_FILE_CORRUPT,
				"BuildClipMap: map has no planes."
			);

			r_clip_map.planes.resize(
				p_bsp.q3_dplanes.size()
			);

			for (
				std::size_t i = 0;
				i < p_bsp.q3_dplanes.size();
				++i
			)
			{
				const q3_dplane_t &source =
					p_bsp.q3_dplanes[i];

				ClipPlane &plane =
					r_clip_map.planes[i];

				plane.normal =
					QuakeToGodot(
						source.normal
					);

				plane.distance =
					source.dist;
			}

			return OK;
		}

		Error LoadBrushSides(
			const BSP &p_bsp,
			ClipMap &r_clip_map)
		{
			r_clip_map.brush_sides.resize(
				p_bsp.q3_dbrushsides.size()
			);

			for (
				std::size_t i = 0;
				i < p_bsp.q3_dbrushsides.size();
				++i
			)
			{
				const q3_dbrushside_t &source =
					p_bsp.q3_dbrushsides[i];

				ERR_FAIL_COND_V_MSG(
					source.planeNum < 0 ||
						static_cast<std::size_t>(
							source.planeNum
						) >=
						r_clip_map.planes.size(),
					ERR_FILE_CORRUPT,
					"BuildClipMap: brush side has invalid plane index."
				);

				ERR_FAIL_COND_V_MSG(
					source.shaderNum < 0 ||
						static_cast<std::size_t>(
							source.shaderNum
						) >=
						p_bsp.q3_dshaders.size(),
					ERR_FILE_CORRUPT,
					"BuildClipMap: brush side has invalid shader index."
				);

				ClipBrushSide &side =
					r_clip_map.brush_sides[i];

				side.plane_index =
					static_cast<uint32_t>(
						source.planeNum
					);

				side.shader_index =
					source.shaderNum;

				side.surface_flags =
					p_bsp.q3_dshaders[
						static_cast<std::size_t>(
							source.shaderNum
						)
					].surfaceFlags;
			}

			return OK;
		}

		Error LoadBrushes(
			const BSP &p_bsp,
			ClipMap &r_clip_map)
		{
			r_clip_map.brushes.resize(
				p_bsp.q3_dbrushes.size()
			);

			for (
				std::size_t i = 0;
				i < p_bsp.q3_dbrushes.size();
				++i
			)
			{
				const q3_dbrush_t &source =
					p_bsp.q3_dbrushes[i];

				ERR_FAIL_COND_V_MSG(
					source.shaderNum < 0 ||
						static_cast<std::size_t>(
							source.shaderNum
						) >=
						p_bsp.q3_dshaders.size(),
					ERR_FILE_CORRUPT,
					"BuildClipMap: brush has invalid shader index."
				);

				ERR_FAIL_COND_V_MSG(
					!IsValidRange(
						source.firstSide,
						source.numSides,
						r_clip_map.brush_sides.size()
					),
					ERR_FILE_CORRUPT,
					"BuildClipMap: brush has invalid side range."
				);

				ClipBrush &brush =
					r_clip_map.brushes[i];

				brush.first_side =
					static_cast<uint32_t>(
						source.firstSide
					);

				brush.side_count =
					static_cast<uint32_t>(
						source.numSides
					);

				brush.shader_index =
					source.shaderNum;

				brush.contents =
					p_bsp.q3_dshaders[
						static_cast<std::size_t>(
							source.shaderNum
						)
					].contentFlags;

				BuildBrushBounds(
					r_clip_map,
					brush
				);
			}

			return OK;
		}

		Error LoadModels(
			const BSP &p_bsp,
			ClipMap &r_clip_map)
		{
			ERR_FAIL_COND_V_MSG(
				p_bsp.q3_dmodels.empty(),
				ERR_FILE_CORRUPT,
				"BuildClipMap: map has no models."
			);

			r_clip_map.models.resize(
				p_bsp.q3_dmodels.size()
			);

			for (
				std::size_t i = 0;
				i < p_bsp.q3_dmodels.size();
				++i
			)
			{
				const q3_dmodel_t &source =
					p_bsp.q3_dmodels[i];

				ERR_FAIL_COND_V_MSG(
					!IsValidRange(
						source.firstBrush,
						source.numBrushes,
						r_clip_map.brushes.size()
					),
					ERR_FILE_CORRUPT,
					"BuildClipMap: model has invalid brush range."
				);

				ERR_FAIL_COND_V_MSG(
					!IsValidRange(
						source.firstSurface,
						source.numSurfaces,
						p_bsp.q3_drawSurfaces.size()
					),
					ERR_FILE_CORRUPT,
					"BuildClipMap: model has invalid surface range."
				);

				ClipModel &model =
					r_clip_map.models[i];

				model.bounds =
					QuakeToGodotBounds(
						source.mins,
						source.maxs
					);

				model.first_surface =
					static_cast<uint32_t>(
						source.firstSurface
					);

				model.surface_count =
					static_cast<uint32_t>(
						source.numSurfaces
					);

				model.first_brush =
					static_cast<uint32_t>(
						source.firstBrush
					);

				model.brush_count =
					static_cast<uint32_t>(
						source.numBrushes
					);
			}

			return OK;
		}

		Error LoadPatches(
			const BSP &p_bsp,
			ClipMap &r_clip_map)
		{
			r_clip_map.patches.clear();

			for (
				std::size_t surface_index = 0;
				surface_index <
					p_bsp.q3_drawSurfaces.size();
				++surface_index
			)
			{
				const q3_dsurface_t &surface =
					p_bsp.q3_drawSurfaces[
						surface_index
					];

				if (
					surface.surfaceType !=
						MST_PATCH
				)
				{
					continue;
				}

				ERR_FAIL_COND_V_MSG(
					surface.shaderNum < 0 ||
						static_cast<std::size_t>(
							surface.shaderNum
						) >=
						p_bsp.q3_dshaders.size(),
					ERR_FILE_CORRUPT,
					"BuildClipMap: patch has invalid shader index."
				);

				const q3_dshader_t &shader =
					p_bsp.q3_dshaders[
						static_cast<std::size_t>(
							surface.shaderNum
						)
					];

				if (
					(static_cast<uint32_t>(
						shader.surfaceFlags
					) &
						SURF_NONSOLID) != 0
				)
				{
					continue;
				}

				if (
					!HasBlockingContents(
						shader.contentFlags
					)
				)
				{
					continue;
				}

				ClipPatch patch;

				patch.surface_index =
					static_cast<uint32_t>(
						surface_index
					);

				patch.shader_index =
					surface.shaderNum;

				patch.surface_flags =
					shader.surfaceFlags;

				patch.contents =
					shader.contentFlags;

				const Error error =
					BuildPatchFaces(
						p_bsp,
						surface,
						patch.faces
					);

				ERR_FAIL_COND_V(
					error != OK,
					error
				);

				if (patch.faces.empty())
					continue;

				patch.bounds =
					BuildBounds(
						patch.faces
					);

				r_clip_map.patches.push_back(
					std::move(
						patch
					)
				);
			}

			return OK;
		}

		CollisionShape3D *CreateBrushCollision(
			const ClipMap &p_clip_map,
			const ClipBrush &p_brush,
			bool &r_is_box)
		{
			const BoxBrush box =
				GetBoxBrush(
					p_clip_map,
					p_brush
				);

			if (box.valid)
			{
				Ref<BoxShape3D> shape;
				shape.instantiate();

				shape->set_size(
					box.max -
						box.min
				);

				CollisionShape3D *collision =
					memnew(
						CollisionShape3D
					);

				collision->set_shape(
					shape
				);

				collision->set_position(
					(
						box.min +
						box.max
					) *
					0.5f
				);

				r_is_box =
					true;

				return collision;
			}

			const PackedVector3Array points =
				BuildConvexBrushPoints(
					p_clip_map,
					p_brush
				);

			if (points.size() < 4)
				return nullptr;

			Ref<ConvexPolygonShape3D> shape;
			shape.instantiate();

			shape->set_points(
				points
			);

			CollisionShape3D *collision =
				memnew(
					CollisionShape3D
				);

			collision->set_shape(
				shape
			);

			r_is_box =
				false;

			return collision;
		}

		StaticBody3D *GetOrCreateContentsBody(
			Node3D *p_root,
			godot::Node *p_scene_owner,
			int p_contents,
			std::map<int, StaticBody3D *> &r_bodies)
		{
			const auto found =
				r_bodies.find(
					p_contents
				);

			if (found != r_bodies.end())
				return found->second;

			const uint32_t layers =
				GetBodyCollisionLayers(
					p_contents
				);

			if (layers == 0)
				return nullptr;

			StaticBody3D *body =
				memnew(
					StaticBody3D
				);

			body->set_name(
				String("Contents_") +
					GetContentsName(
						p_contents
					)
			);

			body->set_meta(
				StringName("contents"),
				p_contents
			);

			body->set_collision_layer(
				layers
			);

			body->set_collision_mask(
				std::numeric_limits<
					uint32_t
				>::max()
			);

			p_root->add_child(
				body
			);

			if (
				Engine::get_singleton()
					->is_editor_hint()
			)
			{
				body->set_owner(
					p_scene_owner
				);
			}

			r_bodies.emplace(
				p_contents,
				body
			);

			return body;
		}

		Area3D *GetOrCreateContentsArea(
			Node3D *p_root,
			godot::Node *p_scene_owner,
			int p_contents,
			std::map<int, Area3D *> &r_areas)
		{
			const auto found =
				r_areas.find(
					p_contents
				);

			if (found != r_areas.end())
				return found->second;

			const uint32_t layers =
				GetAreaCollisionLayers(
					p_contents
				);

			if (layers == 0)
				return nullptr;

			Area3D *area =
				memnew(
					Area3D
				);

			area->set_name(
				String("Area_") +
					GetContentsName(
						p_contents
					)
			);

			area->set_meta(
				StringName("contents"),
				p_contents
			);

			area->set_collision_layer(
				layers
			);

			area->set_collision_mask(
				std::numeric_limits<
					uint32_t
				>::max()
			);

			area->set_monitoring(
				true
			);

			area->set_monitorable(
				true
			);

			p_root->add_child(
				area
			);

			if (
				Engine::get_singleton()
					->is_editor_hint()
			)
			{
				area->set_owner(
					p_scene_owner
				);
			}

			r_areas.emplace(
				p_contents,
				area
			);

			return area;
		}

		void AddBrushCollision(
			Node3D *p_model_root,
			godot::Node *p_scene_owner,
			const ClipMap &p_clip_map,
			uint32_t p_brush_index,
			std::map<int, StaticBody3D *> &r_bodies,
			std::map<int, Area3D *> &r_areas,
			uint32_t &r_box_count,
			uint32_t &r_convex_count,
			uint32_t &r_area_count)
		{
			const ClipBrush &brush =
				p_clip_map.brushes[
					p_brush_index
				];

			if (brush.side_count < 4)
				return;

			if (
				HasBlockingContents(
					brush.contents
				)
			)
			{
				bool is_box =
					false;

				CollisionShape3D *collision =
					CreateBrushCollision(
						p_clip_map,
						brush,
						is_box
					);

				if (collision)
				{
					StaticBody3D *body =
						GetOrCreateContentsBody(
							p_model_root,
							p_scene_owner,
							brush.contents,
							r_bodies
						);

					if (body)
					{
						collision->set_name(
							String("Brush_") +
								String::num_int64(
									static_cast<int64_t>(
										p_brush_index
									)
								)
						);

						collision->set_meta(
							StringName(
								"brush_index"
							),
							static_cast<int64_t>(
								p_brush_index
							)
						);

						collision->set_meta(
							StringName(
								"shader_index"
							),
							brush.shader_index
						);

						body->add_child(
							collision
						);

						if (
							Engine::get_singleton()
								->is_editor_hint()
						)
						{
							collision->set_owner(
								p_scene_owner
							);
						}

						if (is_box)
							++r_box_count;
						else
							++r_convex_count;
					}
					else
					{
						memdelete(
							collision
						);
					}
				}
			}

			if (
				HasAreaContents(
					brush.contents
				)
			)
			{
				bool is_box =
					false;

				CollisionShape3D *collision =
					CreateBrushCollision(
						p_clip_map,
						brush,
						is_box
					);

				if (collision)
				{
					Area3D *area =
						GetOrCreateContentsArea(
							p_model_root,
							p_scene_owner,
							brush.contents,
							r_areas
						);

					if (area)
					{
						collision->set_name(
							String("Brush_") +
								String::num_int64(
									static_cast<int64_t>(
										p_brush_index
									)
								)
						);

						collision->set_meta(
							StringName(
								"brush_index"
							),
							static_cast<int64_t>(
								p_brush_index
							)
						);

						collision->set_meta(
							StringName(
								"shader_index"
							),
							brush.shader_index
						);

						area->add_child(
							collision
						);

						if (
							Engine::get_singleton()
								->is_editor_hint()
						)
						{
							collision->set_owner(
								p_scene_owner
							);
						}

						++r_area_count;
					}
					else
					{
						memdelete(
							collision
						);
					}
				}
			}
		}

		Error AddPatchCollision(
			Node3D *p_model_root,
			godot::Node *p_scene_owner,
			const ClipMap &p_clip_map,
			uint32_t p_patch_index,
			std::map<int, StaticBody3D *> &r_bodies,
			uint32_t &r_patch_count)
		{
			ERR_FAIL_COND_V_MSG(
				p_patch_index >=
					p_clip_map.patches.size(),
				ERR_INVALID_PARAMETER,
				"AddPatchCollision: invalid patch index."
			);

			const ClipPatch &patch =
				p_clip_map.patches[
					p_patch_index
				];

			if (
				!HasBlockingContents(
					patch.contents
				)
			)
			{
				return OK;
			}

			if (patch.faces.empty())
				return OK;

			StaticBody3D *body =
				GetOrCreateContentsBody(
					p_model_root,
					p_scene_owner,
					patch.contents,
					r_bodies
				);

			if (!body)
				return OK;

			PackedVector3Array faces;

			faces.resize(
				static_cast<int64_t>(
					patch.faces.size()
				)
			);

			for (
				int64_t i = 0;
				i < faces.size();
				++i
			)
			{
				faces.set(
					i,
					patch.faces[
						static_cast<std::size_t>(
							i
						)
					]
				);
			}

			Ref<ConcavePolygonShape3D> shape;
			shape.instantiate();

			shape->set_faces(
				faces
			);

			CollisionShape3D *collision =
				memnew(
					CollisionShape3D
				);

			collision->set_name(
				String("Patch_") +
					String::num_int64(
						static_cast<int64_t>(
							p_patch_index
						)
					)
			);

			collision->set_shape(
				shape
			);

			collision->set_meta(
				StringName("patch_index"),
				static_cast<int64_t>(
					p_patch_index
				)
			);

			body->add_child(
				collision
			);

			if (
				Engine::get_singleton()
					->is_editor_hint()
			)
			{
				collision->set_owner(
					p_scene_owner
				);
			}

			++r_patch_count;

			return OK;
		}

		Error BuildCollisionFromIndicesInternal(
			const ClipMap &p_clip_map,
			const std::vector<uint32_t> &p_brush_indices,
			const std::vector<uint32_t> &p_patch_indices,
			Node3D *p_parent,
			godot::Node *p_scene_owner,
			uint32_t &r_box_count,
			uint32_t &r_convex_count,
			uint32_t &r_patch_count,
			uint32_t &r_area_count)
		{
			std::map<
				int,
				StaticBody3D *
			> bodies;

			std::map<
				int,
				Area3D *
			> areas;

			for (
				const uint32_t brush_index :
					p_brush_indices
			)
			{
				ERR_FAIL_COND_V_MSG(
					brush_index >=
						p_clip_map.brushes.size(),
					ERR_INVALID_PARAMETER,
					"BuildCollisionShapesFromIndices: invalid brush index."
				);

				AddBrushCollision(
					p_parent,
					p_scene_owner,
					p_clip_map,
					brush_index,
					bodies,
					areas,
					r_box_count,
					r_convex_count,
					r_area_count
				);
			}

			for (
				const uint32_t patch_index :
					p_patch_indices
			)
			{
				const Error error =
					AddPatchCollision(
						p_parent,
						p_scene_owner,
						p_clip_map,
						patch_index,
						bodies,
						r_patch_count
					);

				ERR_FAIL_COND_V(
					error != OK,
					error
				);
			}

			return OK;
		}

		Error BuildModelCollision(
			const ClipMap &p_clip_map,
			uint32_t p_model_index,
			Node3D *p_parent,
			godot::Node *p_scene_owner,
			uint32_t &r_box_count,
			uint32_t &r_convex_count,
			uint32_t &r_patch_count,
			uint32_t &r_area_count)
		{
			ERR_FAIL_COND_V_MSG(
				p_model_index >=
					p_clip_map.models.size(),
				ERR_INVALID_PARAMETER,
				"BuildModelCollision: invalid BSP model index."
			);

			const ClipModel &model =
				p_clip_map.models[
					p_model_index
				];

			ERR_FAIL_COND_V_MSG(
				static_cast<std::size_t>(
					model.first_brush
				) >
					p_clip_map.brushes.size() ||
				static_cast<std::size_t>(
					model.brush_count
				) >
					p_clip_map.brushes.size() -
						static_cast<std::size_t>(
							model.first_brush
						),
				ERR_INVALID_DATA,
				"BuildModelCollision: model has invalid brush range."
			);

			std::vector<uint32_t> brush_indices;
			std::vector<uint32_t> patch_indices;

			brush_indices.reserve(
				model.brush_count
			);

			for (
				uint32_t i = 0;
				i < model.brush_count;
				++i
			)
			{
				brush_indices.push_back(
					model.first_brush +
						i
				);
			}

			for (
				uint32_t patch_index = 0;
				patch_index <
					static_cast<uint32_t>(
						p_clip_map.patches.size()
					);
				++patch_index
			)
			{
				const ClipPatch &patch =
					p_clip_map.patches[
						patch_index
					];

				if (
					SurfaceBelongsToModel(
						model,
						patch.surface_index
					)
				)
				{
					patch_indices.push_back(
						patch_index
					);
				}
			}

			return
				BuildCollisionFromIndicesInternal(
					p_clip_map,
					brush_indices,
					patch_indices,
					p_parent,
					p_scene_owner,
					r_box_count,
					r_convex_count,
					r_patch_count,
					r_area_count
				);
		}
	}

	Error BuildClipMap(
		const BSP &p_bsp,
		ClipMap &r_clip_map)
	{
		ClipMap loaded;

		Error error =
			LoadPlanes(
				p_bsp,
				loaded
			);

		ERR_FAIL_COND_V(
			error != OK,
			error
		);

		error =
			LoadBrushSides(
				p_bsp,
				loaded
			);

		ERR_FAIL_COND_V(
			error != OK,
			error
		);

		error =
			LoadBrushes(
				p_bsp,
				loaded
			);

		ERR_FAIL_COND_V(
			error != OK,
			error
		);

		error =
			LoadModels(
				p_bsp,
				loaded
			);

		ERR_FAIL_COND_V(
			error != OK,
			error
		);

		error =
			LoadPatches(
				p_bsp,
				loaded
			);

		ERR_FAIL_COND_V(
			error != OK,
			error
		);

		r_clip_map =
			std::move(
				loaded
			);

		return OK;
	}

	Error BuildCollisionShapesFromIndices(
		const ClipMap &p_clip_map,
		const std::vector<uint32_t> &p_brush_indices,
		const std::vector<uint32_t> &p_patch_indices,
		Node3D *p_parent)
	{
		ERR_FAIL_NULL_V(
			p_parent,
			ERR_INVALID_PARAMETER
		);

		godot::Node *scene_owner =
			p_parent->get_owner();

		if (!scene_owner)
			scene_owner = p_parent;

		uint32_t box_count = 0;
		uint32_t convex_count = 0;
		uint32_t patch_count = 0;
		uint32_t area_count = 0;

		return
			BuildCollisionFromIndicesInternal(
				p_clip_map,
				p_brush_indices,
				p_patch_indices,
				p_parent,
				scene_owner,
				box_count,
				convex_count,
				patch_count,
				area_count
			);
	}

	Error BuildCollisionShapesForModel(
		const ClipMap &p_clip_map,
		uint32_t p_model_index,
		Node3D *p_parent)
	{
		ERR_FAIL_NULL_V(
			p_parent,
			ERR_INVALID_PARAMETER
		);

		ERR_FAIL_COND_V_MSG(
			p_model_index >=
				p_clip_map.models.size(),
			ERR_INVALID_PARAMETER,
			"BuildCollisionShapesForModel: invalid BSP model index."
		);

		godot::Node *scene_owner =
			p_parent->get_owner();

		if (!scene_owner)
			scene_owner = p_parent;

		uint32_t box_count = 0;
		uint32_t convex_count = 0;
		uint32_t patch_count = 0;
		uint32_t area_count = 0;

		return
			BuildModelCollision(
				p_clip_map,
				p_model_index,
				p_parent,
				scene_owner,
				box_count,
				convex_count,
				patch_count,
				area_count
			);
	}

	Error BuildCollisionShapesExcluding(
		const ClipMap &p_clip_map,
		const std::vector<uint32_t> &p_excluded_brushes,
		const std::vector<uint32_t> &p_excluded_patches,
		Node3D *p_parent)
	{
		ERR_FAIL_NULL_V(
			p_parent,
			ERR_INVALID_PARAMETER
		);

		ERR_FAIL_COND_V_MSG(
			p_clip_map.models.empty(),
			ERR_INVALID_DATA,
			"BuildCollisionShapesExcluding: ClipMap has no world model."
		);

		if (
			godot::Node *existing =
				p_parent->get_node_or_null(
					NodePath(
						"Q3Collision"
					)
				)
		)
		{
			p_parent->remove_child(
				existing
			);

			existing->queue_free();
		}

		Node3D *collision_root =
			memnew(
				Node3D
			);

		collision_root->set_name(
			"Q3Collision"
		);

		p_parent->add_child(
			collision_root
		);

		godot::Node *scene_owner =
			p_parent->get_owner();

		if (!scene_owner)
			scene_owner = p_parent;

		if (
			Engine::get_singleton()
				->is_editor_hint()
		)
		{
			collision_root->set_owner(
				scene_owner
			);
		}

		const ClipModel &world_model =
			p_clip_map.models[0];

		std::vector<uint32_t> brushes;
		std::vector<uint32_t> patches;

		for (
			uint32_t i = 0;
			i < world_model.brush_count;
			++i
		)
		{
			const uint32_t brush_index =
				world_model.first_brush +
					i;

			if (
				std::find(
					p_excluded_brushes.begin(),
					p_excluded_brushes.end(),
					brush_index
				) !=
					p_excluded_brushes.end()
			)
			{
				continue;
			}

			brushes.push_back(
				brush_index
			);
		}

		for (
			uint32_t patch_index = 0;
			patch_index <
				static_cast<uint32_t>(
					p_clip_map.patches.size()
				);
			++patch_index
		)
		{
			const ClipPatch &patch =
				p_clip_map.patches[
					patch_index
				];

			if (
				!SurfaceBelongsToModel(
					world_model,
					patch.surface_index
				)
			)
			{
				continue;
			}

			if (
				std::find(
					p_excluded_patches.begin(),
					p_excluded_patches.end(),
					patch_index
				) !=
					p_excluded_patches.end()
			)
			{
				continue;
			}

			patches.push_back(
				patch_index
			);
		}

		uint32_t box_count = 0;
		uint32_t convex_count = 0;
		uint32_t patch_count = 0;
		uint32_t area_count = 0;

		const Error error =
			BuildCollisionFromIndicesInternal(
				p_clip_map,
				brushes,
				patches,
				collision_root,
				scene_owner,
				box_count,
				convex_count,
				patch_count,
				area_count
			);

		ERR_FAIL_COND_V(
			error != OK,
			error
		);

		UtilityFunctions::print(
			"Q3 world collision: ",
			static_cast<int64_t>(
				box_count
			),
			" boxes, ",
			static_cast<int64_t>(
				convex_count
			),
			" convex brushes, ",
			static_cast<int64_t>(
				patch_count
			),
			" patches, ",
			static_cast<int64_t>(
				area_count
			),
			" area shapes."
		);

		return OK;
	}

	Error BuildCollisionShapes(
		const ClipMap &p_clip_map,
		Node3D *p_parent)
	{
		static const
			std::vector<uint32_t>
				empty;

		return
			BuildCollisionShapesExcluding(
				p_clip_map,
				empty,
				empty,
				p_parent
			);
	}
}