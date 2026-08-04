#include "renderer.hpp"

#include <q3/utility.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include <godot_cpp/variant/string.hpp>

using namespace godot;

namespace rngo::q3
{
	namespace
	{
		inline constexpr int PATCH_SUBDIVISIONS = 8;

		WorldVertex ConvertDrawVertex(const q3_drawVert_t &p_source)
		{
			WorldVertex vertex;

			vertex.position = QuakeToGodot(p_source.xyz);
			vertex.normal = QuakeToGodot(p_source.normal).normalized();

			vertex.texture_uv = Vector2(p_source.st[0], p_source.st[1]);
			vertex.lightmap_uv = Vector2(p_source.lightmap[0], p_source.lightmap[1]);

			for (std::size_t i = 0; i < vertex.color.size(); ++i)
				vertex.color[i] = p_source.color[i];

			return vertex;
		}

		WorldVertex LerpVertex(const WorldVertex &p_a, const WorldVertex &p_b, float p_t)
		{
			WorldVertex vertex;

			vertex.position = p_a.position.lerp(p_b.position, p_t);
			vertex.normal = p_a.normal.lerp(p_b.normal, p_t).normalized();

			vertex.texture_uv = p_a.texture_uv.lerp(p_b.texture_uv, p_t);
			vertex.lightmap_uv = p_a.lightmap_uv.lerp(p_b.lightmap_uv, p_t);

			for (std::size_t i = 0; i < vertex.color.size(); ++i)
			{
				const float value =
					static_cast<float>(p_a.color[i]) * (1.0f - p_t) +
					static_cast<float>(p_b.color[i]) * p_t;

				vertex.color[i] = static_cast<byte>(
					std::clamp(static_cast<int>(value + 0.5f), 0, 255)
				);
			}

			return vertex;
		}

		WorldVertex QuadraticVertex(const WorldVertex &p_a, const WorldVertex &p_b, const WorldVertex &p_c, float p_t)
		{
			return LerpVertex(
				LerpVertex(p_a, p_b, p_t),
				LerpVertex(p_b, p_c, p_t),
				p_t
			);
		}

		WorldVertex EvaluatePatch(const WorldVertex p_control[3][3], float p_u, float p_v)
		{
			const WorldVertex row0 = QuadraticVertex(p_control[0][0], p_control[1][0], p_control[2][0], p_u);
			const WorldVertex row1 = QuadraticVertex(p_control[0][1], p_control[1][1], p_control[2][1], p_u);
			const WorldVertex row2 = QuadraticVertex(p_control[0][2], p_control[1][2], p_control[2][2], p_u);

			return QuadraticVertex(row0, row1, row2, p_v);
		}

		Error R_LoadShaders(const BSP &p_bsp, World &r_world)
		{
			ERR_FAIL_COND_V_MSG(p_bsp.q3_dshaders.empty(), ERR_FILE_CORRUPT, "R_LoadShaders: map has no shaders.");

			r_world.shaders.resize(p_bsp.q3_dshaders.size());

			for (std::size_t i = 0; i < p_bsp.q3_dshaders.size(); ++i)
			{
				const q3_dshader_t &source = p_bsp.q3_dshaders[i];
				Shader &shader = r_world.shaders[i];

				shader.name = String::utf8(source.shader, strnlen(source.shader, MAX_QPATH));
				shader.surface_flags = source.surfaceFlags;
				shader.content_flags = source.contentFlags;
			}

			return OK;
		}

		Error R_LoadLightmaps(const BSP &p_bsp, World &r_world)
		{
			constexpr std::size_t source_pixel_size = 3;
			constexpr std::size_t pixel_count = LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT;
			constexpr std::size_t source_lightmap_size = pixel_count * source_pixel_size;

			ERR_FAIL_COND_V_MSG(
				p_bsp.q3_lightBytes.size() % source_lightmap_size != 0,
				ERR_FILE_CORRUPT,
				"R_LoadLightmaps: invalid lightmap lump size."
			);

			const std::size_t lightmap_count =
				p_bsp.q3_lightBytes.size() / source_lightmap_size;

			r_world.lightmaps.resize(lightmap_count);

			for (std::size_t lightmap_index = 0; lightmap_index < lightmap_count; ++lightmap_index)
			{
				const std::size_t source_offset =
					lightmap_index * source_lightmap_size;

				Lightmap &lightmap = r_world.lightmaps[lightmap_index];

				for (std::size_t pixel = 0; pixel < pixel_count; ++pixel)
				{
					lightmap.rgba[pixel * 4 + 0] = p_bsp.q3_lightBytes[source_offset + pixel * 3 + 0];
					lightmap.rgba[pixel * 4 + 1] = p_bsp.q3_lightBytes[source_offset + pixel * 3 + 1];
					lightmap.rgba[pixel * 4 + 2] = p_bsp.q3_lightBytes[source_offset + pixel * 3 + 2];
					lightmap.rgba[pixel * 4 + 3] = 255;
				}
			}

			return OK;
		}

		Error R_LoadPlanes(const BSP &p_bsp, World &r_world)
		{
			ERR_FAIL_COND_V_MSG(p_bsp.q3_dplanes.empty(), ERR_FILE_CORRUPT, "R_LoadPlanes: map has no planes.");

			r_world.planes.resize(p_bsp.q3_dplanes.size());

			for (std::size_t i = 0; i < p_bsp.q3_dplanes.size(); ++i)
			{
				const q3_dplane_t &source = p_bsp.q3_dplanes[i];
				Plane &plane = r_world.planes[i];

				plane.normal = QuakeToGodot(source.normal);
				plane.distance = source.dist;
			}

			return OK;
		}

		Error R_LoadFogs(const BSP &p_bsp, World &r_world)
		{
			r_world.fogs.resize(p_bsp.q3_dfogs.size() + 1);

			// fogs[0] means "no fog".
			for (std::size_t i = 0; i < p_bsp.q3_dfogs.size(); ++i)
			{
				const q3_dfog_t &source = p_bsp.q3_dfogs[i];

				ERR_FAIL_COND_V_MSG(
					source.brushNum < -1 ||
					(source.brushNum >= 0 &&
					static_cast<std::size_t>(source.brushNum) >= p_bsp.q3_dbrushes.size()),
					ERR_FILE_CORRUPT,
					"R_LoadFogs: invalid brush index."
				);

				ERR_FAIL_COND_V_MSG(
					source.visibleSide < -1,
					ERR_FILE_CORRUPT,
					"R_LoadFogs: invalid visible side."
				);

				Fog &fog = r_world.fogs[i + 1];

				fog.shader_name = String::utf8(
					source.shader,
					strnlen(source.shader, MAX_QPATH)
				);

				fog.brush_index = source.brushNum;
				fog.visible_side = source.visibleSide;
			}

			return OK;
		}

		Error AppendIndexedSurface(const BSP &p_bsp, const q3_dsurface_t &p_source, Surface &r_surface, World &r_world)
		{
			ERR_FAIL_COND_V_MSG(
				!IsValidRange(p_source.firstVert, p_source.numVerts, p_bsp.q3_drawVerts.size()),
				ERR_FILE_CORRUPT,
				"R_LoadSurfaces: invalid vertex range."
			);

			ERR_FAIL_COND_V_MSG(
				!IsValidRange(p_source.firstIndex, p_source.numIndexes, p_bsp.q3_drawIndexes.size()),
				ERR_FILE_CORRUPT,
				"R_LoadSurfaces: invalid index range."
			);

			ERR_FAIL_COND_V_MSG(
				p_source.numIndexes % 3 != 0,
				ERR_FILE_CORRUPT,
				"R_LoadSurfaces: index count is not divisible by three."
			);

			const std::size_t vertex_count = static_cast<std::size_t>(p_source.numVerts);
			const std::size_t index_count = static_cast<std::size_t>(p_source.numIndexes);
			const std::size_t uint32_max = std::numeric_limits<uint32_t>::max();

			ERR_FAIL_COND_V_MSG(
				r_world.vertices.size() > uint32_max ||
				vertex_count > uint32_max - r_world.vertices.size(),
				ERR_OUT_OF_MEMORY,
				"R_LoadSurfaces: vertex count exceeds uint32_t."
			);

			ERR_FAIL_COND_V_MSG(
				r_world.indices.size() > uint32_max ||
				index_count > uint32_max - r_world.indices.size(),
				ERR_OUT_OF_MEMORY,
				"R_LoadSurfaces: index count exceeds uint32_t."
			);

			r_surface.first_vertex = static_cast<uint32_t>(r_world.vertices.size());
			r_surface.vertex_count = static_cast<uint32_t>(vertex_count);

			r_surface.first_index = static_cast<uint32_t>(r_world.indices.size());
			r_surface.index_count = static_cast<uint32_t>(index_count);

			r_world.vertices.reserve(r_world.vertices.size() + vertex_count);
			r_world.indices.reserve(r_world.indices.size() + index_count);

			for (int i = 0; i < p_source.numVerts; ++i)
			{
				const std::size_t source_index =
					static_cast<std::size_t>(p_source.firstVert) +
					static_cast<std::size_t>(i);

				const WorldVertex vertex =
					ConvertDrawVertex(p_bsp.q3_drawVerts[source_index]);

				if (i == 0)
					r_surface.bounds = AABB(vertex.position, Vector3());
				else
					r_surface.bounds = r_surface.bounds.expand(vertex.position);

				r_world.vertices.push_back(vertex);
			}

			for (int i = 0; i < p_source.numIndexes; ++i)
			{
				const std::size_t source_index =
					static_cast<std::size_t>(p_source.firstIndex) +
					static_cast<std::size_t>(i);

				const int local_index = p_bsp.q3_drawIndexes[source_index];

				ERR_FAIL_COND_V_MSG(
					local_index < 0 || local_index >= p_source.numVerts,
					ERR_FILE_CORRUPT,
					"R_LoadSurfaces: invalid local vertex index."
				);

				r_world.indices.push_back(
					r_surface.first_vertex +
					static_cast<uint32_t>(local_index)
				);
			}

			return OK;
		}

		Error AppendPatchSurface(const BSP &p_bsp, const q3_dsurface_t &p_source, Surface &r_surface, World &r_world)
		{
			ERR_FAIL_COND_V_MSG(
				p_source.patchWidth < 3 ||
				p_source.patchHeight < 3 ||
				(p_source.patchWidth & 1) == 0 ||
				(p_source.patchHeight & 1) == 0,
				ERR_FILE_CORRUPT,
				"R_LoadSurfaces: invalid patch dimensions."
			);

			ERR_FAIL_COND_V_MSG(
				!IsValidRange(p_source.firstVert, p_source.numVerts, p_bsp.q3_drawVerts.size()),
				ERR_FILE_CORRUPT,
				"R_LoadSurfaces: invalid patch vertex range."
			);

			const std::size_t patch_width = static_cast<std::size_t>(p_source.patchWidth);
			const std::size_t patch_height = static_cast<std::size_t>(p_source.patchHeight);

			ERR_FAIL_COND_V_MSG(
				patch_width > std::numeric_limits<std::size_t>::max() / patch_height,
				ERR_FILE_CORRUPT,
				"R_LoadSurfaces: patch dimensions overflow."
			);

			ERR_FAIL_COND_V_MSG(
				static_cast<std::size_t>(p_source.numVerts) < patch_width * patch_height,
				ERR_FILE_CORRUPT,
				"R_LoadSurfaces: patch has too few control points."
			);

			r_surface.first_vertex = static_cast<uint32_t>(r_world.vertices.size());
			r_surface.first_index = static_cast<uint32_t>(r_world.indices.size());

			for (int y = 0; y < p_source.patchHeight - 2; y += 2)
			{
				for (int x = 0; x < p_source.patchWidth - 2; x += 2)
				{
					WorldVertex control[3][3];

					for (int cy = 0; cy < 3; ++cy)
					{
						for (int cx = 0; cx < 3; ++cx)
						{
							const int control_index =
								(y + cy) * p_source.patchWidth +
								x + cx;

							const std::size_t source_index =
								static_cast<std::size_t>(p_source.firstVert) +
								static_cast<std::size_t>(control_index);

							control[cx][cy] =
								ConvertDrawVertex(p_bsp.q3_drawVerts[source_index]);
						}
					}

					ERR_FAIL_COND_V_MSG(
						r_world.vertices.size() >
						std::numeric_limits<uint32_t>::max() -
						static_cast<std::size_t>((PATCH_SUBDIVISIONS + 1) * (PATCH_SUBDIVISIONS + 1)),
						ERR_OUT_OF_MEMORY,
						"R_LoadSurfaces: patch vertex count exceeds uint32_t."
					);

					const uint32_t tile_first_vertex =
						static_cast<uint32_t>(r_world.vertices.size());

					for (int v = 0; v <= PATCH_SUBDIVISIONS; ++v)
					{
						for (int u = 0; u <= PATCH_SUBDIVISIONS; ++u)
						{
							const float patch_u =
								static_cast<float>(u) /
								static_cast<float>(PATCH_SUBDIVISIONS);

							const float patch_v =
								static_cast<float>(v) /
								static_cast<float>(PATCH_SUBDIVISIONS);

							const WorldVertex vertex =
								EvaluatePatch(control, patch_u, patch_v);

							if (r_world.vertices.size() == r_surface.first_vertex)
								r_surface.bounds = AABB(vertex.position, Vector3());
							else
								r_surface.bounds = r_surface.bounds.expand(vertex.position);

							r_world.vertices.push_back(vertex);
						}
					}

					const uint32_t row_width = PATCH_SUBDIVISIONS + 1;

					for (int v = 0; v < PATCH_SUBDIVISIONS; ++v)
					{
						for (int u = 0; u < PATCH_SUBDIVISIONS; ++u)
						{
							const uint32_t i0 =
								tile_first_vertex +
								static_cast<uint32_t>(v) * row_width +
								static_cast<uint32_t>(u);

							const uint32_t i1 = i0 + 1;
							const uint32_t i2 = i0 + row_width;
							const uint32_t i3 = i2 + 1;

							r_world.indices.push_back(i0);
							r_world.indices.push_back(i2);
							r_world.indices.push_back(i1);

							r_world.indices.push_back(i1);
							r_world.indices.push_back(i2);
							r_world.indices.push_back(i3);
						}
					}
				}
			}

			ERR_FAIL_COND_V_MSG(
				r_world.vertices.size() - r_surface.first_vertex >
				std::numeric_limits<uint32_t>::max(),
				ERR_OUT_OF_MEMORY,
				"R_LoadSurfaces: patch vertex count exceeds uint32_t."
			);

			ERR_FAIL_COND_V_MSG(
				r_world.indices.size() - r_surface.first_index >
				std::numeric_limits<uint32_t>::max(),
				ERR_OUT_OF_MEMORY,
				"R_LoadSurfaces: patch index count exceeds uint32_t."
			);

			r_surface.vertex_count =
				static_cast<uint32_t>(
					r_world.vertices.size() -
					r_surface.first_vertex
				);

			r_surface.index_count =
				static_cast<uint32_t>(
					r_world.indices.size() -
					r_surface.first_index
				);

			return OK;
		}

		Error R_LoadSurfaces(const BSP &p_bsp, World &r_world)
		{
			r_world.surfaces.resize(p_bsp.q3_drawSurfaces.size());

			for (std::size_t i = 0; i < p_bsp.q3_drawSurfaces.size(); ++i)
			{
				const q3_dsurface_t &source = p_bsp.q3_drawSurfaces[i];
				Surface &surface = r_world.surfaces[i];

				ERR_FAIL_COND_V_MSG(
					source.shaderNum < 0 ||
					static_cast<std::size_t>(source.shaderNum) >= r_world.shaders.size(),
					ERR_FILE_CORRUPT,
					"R_LoadSurfaces: invalid shader index."
				);

				ERR_FAIL_COND_V_MSG(
					source.fogNum != -1 &&
					(source.fogNum < 0 ||
					static_cast<std::size_t>(source.fogNum) >= r_world.fogs.size()),
					ERR_FILE_CORRUPT,
					"R_LoadSurfaces: invalid fog index."
				);

				ERR_FAIL_COND_V_MSG(
					source.lightmapNum != -1 &&
					(source.lightmapNum < 0 ||
					static_cast<std::size_t>(source.lightmapNum) >= r_world.lightmaps.size()),
					ERR_FILE_CORRUPT,
					"R_LoadSurfaces: invalid lightmap index."
				);

				surface.shader_index = source.shaderNum;
				surface.fog_index = source.fogNum;
				surface.lightmap_index = source.lightmapNum;

				switch (source.surfaceType)
				{
					case MST_PLANAR:
						surface.type = SurfaceType::Face;
						ERR_FAIL_COND_V(
							AppendIndexedSurface(p_bsp, source, surface, r_world) != OK,
							ERR_FILE_CORRUPT
						);
						break;

					case MST_TRIANGLE_SOUP:
						surface.type = SurfaceType::Triangles;
						ERR_FAIL_COND_V(
							AppendIndexedSurface(p_bsp, source, surface, r_world) != OK,
							ERR_FILE_CORRUPT
						);
						break;

					case MST_PATCH:
						surface.type = SurfaceType::Grid;
						ERR_FAIL_COND_V(
							AppendPatchSurface(p_bsp, source, surface, r_world) != OK,
							ERR_FILE_CORRUPT
						);
						break;

					case MST_FLARE:
						surface.type = SurfaceType::Flare;
						break;

					default:
						ERR_FAIL_V_MSG(
							ERR_FILE_CORRUPT,
							"R_LoadSurfaces: unknown BSP surface type."
						);
				}
			}

			return OK;
		}

		Error R_LoadMarkSurfaces(const BSP &p_bsp, World &r_world)
		{
			r_world.mark_surfaces.resize(p_bsp.q3_dleafsurfaces.size());

			for (std::size_t i = 0; i < p_bsp.q3_dleafsurfaces.size(); ++i)
			{
				const int surface_index = p_bsp.q3_dleafsurfaces[i];

				ERR_FAIL_COND_V_MSG(
					surface_index < 0 ||
					static_cast<std::size_t>(surface_index) >= r_world.surfaces.size(),
					ERR_FILE_CORRUPT,
					"R_LoadMarkSurfaces: invalid surface index."
				);

				r_world.mark_surfaces[i] =
					static_cast<uint32_t>(surface_index);
			}

			return OK;
		}

		Error R_LoadLeaves(const BSP &p_bsp, World &r_world)
		{
			ERR_FAIL_COND_V_MSG(
				p_bsp.q3_dleafs.empty(),
				ERR_FILE_CORRUPT,
				"R_LoadLeaves: map has no leaves."
			);

			r_world.leaves.resize(p_bsp.q3_dleafs.size());

			for (std::size_t i = 0; i < p_bsp.q3_dleafs.size(); ++i)
			{
				const q3_dleaf_t &source = p_bsp.q3_dleafs[i];

				ERR_FAIL_COND_V_MSG(
					!IsValidRange(
						source.firstLeafSurface,
						source.numLeafSurfaces,
						p_bsp.q3_dleafsurfaces.size()
					),
					ERR_FILE_CORRUPT,
					"R_LoadLeaves: invalid leaf surface range."
				);

				Leaf &leaf = r_world.leaves[i];

				leaf.bounds =
					QuakeToGodotBounds(source.mins, source.maxs);

				leaf.cluster = source.cluster;
				leaf.area = source.area;

				leaf.first_mark_surface =
					static_cast<uint32_t>(source.firstLeafSurface);

				leaf.mark_surface_count =
					static_cast<uint32_t>(source.numLeafSurfaces);

				if (leaf.cluster >= r_world.cluster_count)
					r_world.cluster_count = leaf.cluster + 1;
			}

			return OK;
		}

		Error R_LoadNodes(const BSP &p_bsp, World &r_world)
		{
			r_world.nodes.resize(p_bsp.q3_dnodes.size());

			for (std::size_t i = 0; i < p_bsp.q3_dnodes.size(); ++i)
			{
				const q3_dnode_t &source = p_bsp.q3_dnodes[i];

				ERR_FAIL_COND_V_MSG(
					source.planeNum < 0 ||
					static_cast<std::size_t>(source.planeNum) >= r_world.planes.size(),
					ERR_FILE_CORRUPT,
					"R_LoadNodes: invalid plane index."
				);

				ERR_FAIL_COND_V_MSG(
					!IsValidBSPChildIndex(
						source.children[0],
						p_bsp.q3_dnodes.size(),
						r_world.leaves.size()
					),
					ERR_FILE_CORRUPT,
					"R_LoadNodes: invalid first child."
				);

				ERR_FAIL_COND_V_MSG(
					!IsValidBSPChildIndex(
						source.children[1],
						p_bsp.q3_dnodes.size(),
						r_world.leaves.size()
					),
					ERR_FILE_CORRUPT,
					"R_LoadNodes: invalid second child."
				);

				Node &node = r_world.nodes[i];

				node.bounds =
					QuakeToGodotBounds(source.mins, source.maxs);

				node.plane_index =
					static_cast<uint32_t>(source.planeNum);

				node.children[0] = source.children[0];
				node.children[1] = source.children[1];
			}

			return OK;
		}

		Error R_LoadSubmodels(const BSP &p_bsp, World &r_world)
		{
			ERR_FAIL_COND_V_MSG(
				p_bsp.q3_dmodels.empty(),
				ERR_FILE_CORRUPT,
				"R_LoadSubmodels: map has no models."
			);

			r_world.brush_models.resize(p_bsp.q3_dmodels.size());

			for (std::size_t i = 0; i < p_bsp.q3_dmodels.size(); ++i)
			{
				const q3_dmodel_t &source = p_bsp.q3_dmodels[i];

				ERR_FAIL_COND_V_MSG(
					!IsValidRange(
						source.firstSurface,
						source.numSurfaces,
						r_world.surfaces.size()
					),
					ERR_FILE_CORRUPT,
					"R_LoadSubmodels: invalid surface range."
				);

				BrushModel &model = r_world.brush_models[i];

				model.bounds =
					QuakeToGodotBounds(source.mins, source.maxs);

				model.first_surface =
					static_cast<uint32_t>(source.firstSurface);

				model.surface_count =
					static_cast<uint32_t>(source.numSurfaces);
			}

			return OK;
		}

		Error R_LoadVisibility(const BSP &p_bsp, World &r_world)
		{
			ERR_FAIL_COND_V_MSG(
				r_world.cluster_count < 0,
				ERR_FILE_CORRUPT,
				"R_LoadVisibility: invalid leaf cluster count."
			);

			const std::size_t leaf_cluster_count =
				static_cast<std::size_t>(r_world.cluster_count);

			const std::size_t no_visibility_bytes =
				(leaf_cluster_count + 63) &
				~static_cast<std::size_t>(63);

			r_world.no_visibility.assign(no_visibility_bytes, 0xff);

			if (p_bsp.q3_visBytes.empty())
				return OK;

			ERR_FAIL_COND_V_MSG(
				p_bsp.q3_visBytes.size() < 8,
				ERR_FILE_CORRUPT,
				"R_LoadVisibility: visibility lump is too small."
			);

			int32_t cluster_count = 0;
			int32_t cluster_bytes = 0;

			std::memcpy(
				&cluster_count,
				p_bsp.q3_visBytes.data(),
				sizeof(cluster_count)
			);

			std::memcpy(
				&cluster_bytes,
				p_bsp.q3_visBytes.data() + sizeof(cluster_count),
				sizeof(cluster_bytes)
			);

			ERR_FAIL_COND_V_MSG(
				cluster_count < 0 || cluster_bytes < 0,
				ERR_FILE_CORRUPT,
				"R_LoadVisibility: invalid visibility header."
			);

			const std::size_t count =
				static_cast<std::size_t>(cluster_count);

			const std::size_t bytes =
				static_cast<std::size_t>(cluster_bytes);

			ERR_FAIL_COND_V_MSG(
				bytes != 0 &&
				count > (std::numeric_limits<std::size_t>::max() - 8) / bytes,
				ERR_FILE_CORRUPT,
				"R_LoadVisibility: visibility data size overflow."
			);

			const std::size_t data_size = count * bytes;

			ERR_FAIL_COND_V_MSG(
				p_bsp.q3_visBytes.size() < 8 + data_size,
				ERR_FILE_CORRUPT,
				"R_LoadVisibility: visibility data is truncated."
			);

			r_world.cluster_count = cluster_count;
			r_world.cluster_bytes = cluster_bytes;

			r_world.visibility.assign(
				p_bsp.q3_visBytes.begin() + 8,
				p_bsp.q3_visBytes.begin() + 8 + data_size
			);

			return OK;
		}

		void R_LoadEntities(const BSP &p_bsp, World &r_world)
		{
			r_world.entity_string.assign(
				p_bsp.q3_dentdata.begin(),
				p_bsp.q3_dentdata.end()
			);

			r_world.entity_parse_offset = 0;
		}

		void R_LoadLightGrid(const BSP &p_bsp, World &r_world)
		{
			r_world.light_grid_data.resize(
				p_bsp.q3_gridData.size() *
				sizeof(q3_dgridpoint_t)
			);

			if (!p_bsp.q3_gridData.empty())
			{
				std::memcpy(
					r_world.light_grid_data.data(),
					p_bsp.q3_gridData.data(),
					r_world.light_grid_data.size()
				);
			}
		}
	}

	Error RE_LoadWorldMap(const BSP &p_bsp, World &r_world)
	{
		World loaded;

		Error error = R_LoadShaders(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		error = R_LoadLightmaps(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		error = R_LoadPlanes(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		error = R_LoadFogs(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		error = R_LoadSurfaces(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		error = R_LoadMarkSurfaces(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		error = R_LoadLeaves(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		error = R_LoadNodes(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		error = R_LoadSubmodels(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		error = R_LoadVisibility(p_bsp, loaded);
		ERR_FAIL_COND_V(error != OK, error);

		R_LoadEntities(p_bsp, loaded);
		R_LoadLightGrid(p_bsp, loaded);

		r_world = std::move(loaded);

		return OK;
	}
}