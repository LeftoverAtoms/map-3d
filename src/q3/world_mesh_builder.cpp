#include "world_mesh_builder.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

using namespace godot;

namespace rngo::q3
{
	namespace
	{
		inline constexpr const char *WORLD_SHADER_PATH =
			"res://shaders/q3_world.gdshader";

		struct MaterialKey
		{
			int shader_index = -1;
			int lightmap_index = -1;

			bool operator<(const MaterialKey &p_other) const
			{
				if (shader_index != p_other.shader_index)
					return shader_index < p_other.shader_index;

				return lightmap_index < p_other.lightmap_index;
			}
		};

		struct MeshGeometry
		{
			std::vector<WorldVertex> vertices;
			std::vector<int32_t> indices;
		};

		bool IsRenderableSurface(const Surface &p_surface)
		{
			if (
				p_surface.type == SurfaceType::Flare ||
				p_surface.type == SurfaceType::Skip ||
				p_surface.type == SurfaceType::Bad
			)
			{
				return false;
			}

			return
				p_surface.vertex_count > 0 &&
				p_surface.index_count > 0;
		}

		Error AppendSurfaceGeometry(
			const World &p_world,
			const Surface &p_surface,
			MeshGeometry &r_geometry)
		{
			if (!IsRenderableSurface(p_surface))
				return OK;

			const std::size_t first_vertex =
				static_cast<std::size_t>(
					p_surface.first_vertex
				);

			const std::size_t vertex_count =
				static_cast<std::size_t>(
					p_surface.vertex_count
				);

			const std::size_t first_index =
				static_cast<std::size_t>(
					p_surface.first_index
				);

			const std::size_t index_count =
				static_cast<std::size_t>(
					p_surface.index_count
				);

			ERR_FAIL_COND_V_MSG(
				first_vertex > p_world.vertices.size() ||
					vertex_count >
						p_world.vertices.size() -
						first_vertex,
				ERR_INVALID_DATA,
				"World mesh builder: invalid surface vertex range."
			);

			ERR_FAIL_COND_V_MSG(
				first_index > p_world.indices.size() ||
					index_count >
						p_world.indices.size() -
						first_index,
				ERR_INVALID_DATA,
				"World mesh builder: invalid surface index range."
			);

			ERR_FAIL_COND_V_MSG(
				r_geometry.vertices.size() >
					static_cast<std::size_t>(
						std::numeric_limits<int32_t>::max()
					) -
					vertex_count,
				ERR_OUT_OF_MEMORY,
				"World mesh builder: geometry exceeds 32-bit index range."
			);

			const int32_t destination_first_vertex =
				static_cast<int32_t>(
					r_geometry.vertices.size()
				);

			r_geometry.vertices.reserve(
				r_geometry.vertices.size() +
				vertex_count
			);

			r_geometry.indices.reserve(
				r_geometry.indices.size() +
				index_count
			);

			for (
				std::size_t i = 0;
				i < vertex_count;
				++i
			)
			{
				r_geometry.vertices.push_back(
					p_world.vertices[
						first_vertex + i
					]
				);
			}

			for (
				std::size_t i = 0;
				i < index_count;
				++i
			)
			{
				const uint32_t source_index =
					p_world.indices[
						first_index + i
					];

				ERR_FAIL_COND_V_MSG(
					source_index <
						p_surface.first_vertex ||
					source_index >=
						p_surface.first_vertex +
						p_surface.vertex_count,
					ERR_INVALID_DATA,
					"World mesh builder: surface index is outside its vertex range."
				);

				const uint32_t local_index =
					source_index -
					p_surface.first_vertex;

				r_geometry.indices.push_back(
					destination_first_vertex +
					static_cast<int32_t>(
						local_index
					)
				);
			}

			return OK;
		}

		Error BuildGeometryGroups(
			const World &p_world,
			const std::vector<uint32_t> &p_surface_indices,
			std::map<MaterialKey, MeshGeometry> &r_groups)
		{
			r_groups.clear();

			for (
				const uint32_t surface_index :
				p_surface_indices
			)
			{
				ERR_FAIL_COND_V_MSG(
					surface_index >=
						p_world.surfaces.size(),
					ERR_INVALID_PARAMETER,
					"World mesh builder: invalid surface index."
				);

				const Surface &surface =
					p_world.surfaces[
						surface_index
					];

				if (!IsRenderableSurface(surface))
					continue;

				ERR_FAIL_COND_V_MSG(
					surface.shader_index < 0 ||
						static_cast<std::size_t>(
							surface.shader_index
						) >=
						p_world.shaders.size(),
					ERR_INVALID_DATA,
					"World mesh builder: invalid shader index."
				);

				if (surface.lightmap_index >= 0)
				{
					ERR_FAIL_COND_V_MSG(
						static_cast<std::size_t>(
							surface.lightmap_index
						) >=
							p_world.lightmaps.size(),
						ERR_INVALID_DATA,
						"World mesh builder: invalid lightmap index."
					);
				}

				const MaterialKey key{
					surface.shader_index,
					surface.lightmap_index
				};

				const Error error =
					AppendSurfaceGeometry(
						p_world,
						surface,
						r_groups[key]
					);

				ERR_FAIL_COND_V(
					error != OK,
					error
				);
			}

			return OK;
		}

		Array CreateSurfaceArrays(
			const MeshGeometry &p_geometry)
		{
			const int64_t vertex_count =
				static_cast<int64_t>(
					p_geometry.vertices.size()
				);

			const int64_t index_count =
				static_cast<int64_t>(
					p_geometry.indices.size()
				);

			PackedVector3Array vertices;
			PackedVector3Array normals;
			PackedVector2Array uvs;
			PackedVector2Array lightmap_uvs;
			PackedColorArray colors;
			PackedInt32Array indices;

			vertices.resize(vertex_count);
			normals.resize(vertex_count);
			uvs.resize(vertex_count);
			lightmap_uvs.resize(vertex_count);
			colors.resize(vertex_count);
			indices.resize(index_count);

			for (
				int64_t i = 0;
				i < vertex_count;
				++i
			)
			{
				const WorldVertex &source =
					p_geometry.vertices[
						static_cast<std::size_t>(i)
					];

				vertices.set(
					i,
					source.position
				);

				normals.set(
					i,
					source.normal
				);

				uvs.set(
					i,
					source.texture_uv
				);

				lightmap_uvs.set(
					i,
					source.lightmap_uv
				);

				colors.set(
					i,
					Color(
						source.color[0] / 255.0f,
						source.color[1] / 255.0f,
						source.color[2] / 255.0f,
						source.color[3] / 255.0f
					)
				);
			}

			for (
				int64_t i = 0;
				i < index_count;
				++i
			)
			{
				indices.set(
					i,
					p_geometry.indices[
						static_cast<std::size_t>(i)
					]
				);
			}

			Array arrays;
			arrays.resize(
				Mesh::ARRAY_MAX
			);

			arrays[
				Mesh::ARRAY_VERTEX
			] = vertices;

			arrays[
				Mesh::ARRAY_NORMAL
			] = normals;

			arrays[
				Mesh::ARRAY_TEX_UV
			] = uvs;

			arrays[
				Mesh::ARRAY_TEX_UV2
			] = lightmap_uvs;

			arrays[
				Mesh::ARRAY_COLOR
			] = colors;

			arrays[
				Mesh::ARRAY_INDEX
			] = indices;

			return arrays;
		}

		Error LoadImageFromBuffer(
			const String &p_path,
			const PackedByteArray &p_data,
			Ref<Image> &r_image)
		{
			r_image.instantiate();

			const String extension =
				p_path
					.get_extension()
					.to_lower();

			if (extension == "tga")
				return r_image->load_tga_from_buffer(p_data);

			if (
				extension == "jpg" ||
				extension == "jpeg"
			)
			{
				return r_image->load_jpg_from_buffer(p_data);
			}

			if (extension == "png")
				return r_image->load_png_from_buffer(p_data);

			return ERR_FILE_UNRECOGNIZED;
		}

		Ref<Texture2D> LoadTexturePathFromPaks(
			const std::vector<Ref<ZIPReader>> &p_paks,
			const String &p_path)
		{
			for (
				auto it = p_paks.rbegin();
				it != p_paks.rend();
				++it
			)
			{
				const Ref<ZIPReader> &pak =
					*it;

				if (
					pak.is_null() ||
					!pak->file_exists(p_path)
				)
				{
					continue;
				}

				const PackedByteArray data =
					pak->read_file(
						p_path
					);

				if (data.is_empty())
					continue;

				Ref<Image> image;

				if (
					LoadImageFromBuffer(
						p_path,
						data,
						image
					) != OK ||
					image.is_null()
				)
				{
					continue;
				}

				if (!image->has_mipmaps())
					image->generate_mipmaps();

				const Ref<ImageTexture> texture =
					ImageTexture::create_from_image(
						image
					);

				if (texture.is_valid())
					return texture;
			}

			return Ref<Texture2D>();
		}

		Ref<Texture2D> LoadTextureFromPaks(
			const std::vector<Ref<ZIPReader>> &p_paks,
			const String &p_shader_name)
		{
			const String shader_name =
				p_shader_name.replace(
					"\\",
					"/"
				);

			if (
				!shader_name
					.get_extension()
					.is_empty()
			)
			{
				const Ref<Texture2D> texture =
					LoadTexturePathFromPaks(
						p_paks,
						shader_name
					);

				if (texture.is_valid())
					return texture;
			}

			static const String extensions[] = {
				".tga",
				".jpg",
				".jpeg",
				".png"
			};

			for (
				const String &extension :
					extensions
			)
			{
				const Ref<Texture2D> texture =
					LoadTexturePathFromPaks(
						p_paks,
						shader_name +
						extension
					);

				if (texture.is_valid())
					return texture;
			}

			return Ref<Texture2D>();
		}

		Ref<Texture2D> CreateFallbackTexture()
		{
			Ref<Image> image =
				Image::create_empty(
					1,
					1,
					false,
					Image::FORMAT_RGBA8
				);

			if (image.is_null())
				return Ref<Texture2D>();

			image->fill(
				Color(
					1.0f,
					0.0f,
					1.0f,
					1.0f
				)
			);

			return
				ImageTexture::create_from_image(
					image
				);
		}

		Ref<Texture2D> CreateLightmapTexture(
			const Lightmap &p_lightmap)
		{
			PackedByteArray data;

			data.resize(
				static_cast<int64_t>(
					p_lightmap.rgba.size()
				)
			);

			for (
				std::size_t i = 0;
				i < p_lightmap.rgba.size();
				++i
			)
			{
				data.set(
					static_cast<int64_t>(i),
					p_lightmap.rgba[i]
				);
			}

			const Ref<Image> image =
				Image::create_from_data(
					LIGHTMAP_WIDTH,
					LIGHTMAP_HEIGHT,
					false,
					Image::FORMAT_RGBA8,
					data
				);

			if (image.is_null())
				return Ref<Texture2D>();

			return
				ImageTexture::create_from_image(
					image
				);
		}

		Ref<Texture2D> GetBaseTexture(
			const World &p_world,
			const std::vector<Ref<ZIPReader>> &p_paks,
			int p_shader_index,
			std::map<int, Ref<Texture2D>> &r_cache,
			const Ref<Texture2D> &p_fallback)
		{
			const auto found =
				r_cache.find(
					p_shader_index
				);

			if (found != r_cache.end())
				return found->second;

			const Shader &shader =
				p_world.shaders[
					static_cast<std::size_t>(
						p_shader_index
					)
				];

			Ref<Texture2D> texture =
				LoadTextureFromPaks(
					p_paks,
					shader.name
				);

			if (texture.is_null())
			{
				UtilityFunctions::push_warning(
					String(
						"World mesh builder: could not resolve Quake shader image '"
					) +
					shader.name +
					String("'.")
				);

				texture =
					p_fallback;
			}

			r_cache.emplace(
				p_shader_index,
				texture
			);

			return texture;
		}

		Ref<Texture2D> GetLightmapTexture(
			const World &p_world,
			int p_lightmap_index,
			std::map<int, Ref<Texture2D>> &r_cache)
		{
			if (p_lightmap_index < 0)
				return Ref<Texture2D>();

			const auto found =
				r_cache.find(
					p_lightmap_index
				);

			if (found != r_cache.end())
				return found->second;

			const Ref<Texture2D> texture =
				CreateLightmapTexture(
					p_world.lightmaps[
						static_cast<std::size_t>(
							p_lightmap_index
						)
					]
				);

			r_cache.emplace(
				p_lightmap_index,
				texture
			);

			return texture;
		}

		Ref<ShaderMaterial> CreateMaterial(
			const World &p_world,
			const MaterialKey &p_key,
			const std::vector<Ref<ZIPReader>> &p_paks,
			const Ref<godot::Shader> &p_shader,
			std::map<int, Ref<Texture2D>> &r_base_texture_cache,
			std::map<int, Ref<Texture2D>> &r_lightmap_cache,
			const Ref<Texture2D> &p_fallback)
		{
			Ref<ShaderMaterial> material;
			material.instantiate();

			material->set_name(
				p_world.shaders[
					static_cast<std::size_t>(
						p_key.shader_index
					)
				].name
			);

			material->set_shader(
				p_shader
			);

			material->set_shader_parameter(
				StringName("base_texture"),
				GetBaseTexture(
					p_world,
					p_paks,
					p_key.shader_index,
					r_base_texture_cache,
					p_fallback
				)
			);

			const Ref<Texture2D> lightmap =
				GetLightmapTexture(
					p_world,
					p_key.lightmap_index,
					r_lightmap_cache
				);

			const bool use_lightmap =
				lightmap.is_valid();

			material->set_shader_parameter(
				StringName("use_lightmap"),
				use_lightmap
			);

			if (use_lightmap)
			{
				material->set_shader_parameter(
					StringName("lightmap_texture"),
					lightmap
				);
			}

			material->set_shader_parameter(
				StringName("lightmap_scale"),
				2.0f
			);

			return material;
		}

		Ref<ArrayMesh> CreateArrayMesh(
			const World &p_world,
			const std::map<MaterialKey, MeshGeometry> &p_groups,
			const std::vector<Ref<ZIPReader>> &p_paks,
			const Ref<godot::Shader> &p_shader)
		{
			Ref<ArrayMesh> mesh;
			mesh.instantiate();

			const Ref<Texture2D> fallback =
				CreateFallbackTexture();

			std::map<int, Ref<Texture2D>>
				base_texture_cache;

			std::map<int, Ref<Texture2D>>
				lightmap_cache;

			for (
				const auto &[key, geometry] :
					p_groups
			)
			{
				if (
					geometry.vertices.empty() ||
					geometry.indices.empty()
				)
				{
					continue;
				}

				mesh->add_surface_from_arrays(
					Mesh::PRIMITIVE_TRIANGLES,
					CreateSurfaceArrays(
						geometry
					)
				);

				const int surface_index =
					mesh->get_surface_count() - 1;

				mesh->surface_set_name(
					surface_index,
					p_world.shaders[
						static_cast<std::size_t>(
							key.shader_index
						)
					].name
				);

				mesh->surface_set_material(
					surface_index,
					CreateMaterial(
						p_world,
						key,
						p_paks,
						p_shader,
						base_texture_cache,
						lightmap_cache,
						fallback
					)
				);
			}

			if (
				mesh->get_surface_count() == 0
			)
			{
				return Ref<ArrayMesh>();
			}

			return mesh;
		}

		std::vector<uint32_t> BuildModelSurfaceList(
			const World &p_world,
			uint32_t p_model_index)
		{
			std::vector<uint32_t> result;

			if (
				p_model_index >=
					p_world.brush_models.size()
			)
			{
				return result;
			}

			const BrushModel &model =
				p_world.brush_models[
					p_model_index
				];

			result.reserve(
				model.surface_count
			);

			for (
				uint32_t i = 0;
				i < model.surface_count;
				++i
			)
			{
				result.push_back(
					model.first_surface + i
				);
			}

			return result;
		}
	}

	Ref<ArrayMesh> BuildArrayMeshFromSurfaces(
		const World &p_world,
		const std::vector<uint32_t> &p_surface_indices,
		const std::vector<Ref<ZIPReader>> &p_paks)
	{
		if (p_surface_indices.empty())
			return Ref<ArrayMesh>();

		Ref<godot::Shader> world_shader =
			ResourceLoader::get_singleton()
				->load(
					WORLD_SHADER_PATH
				);

		ERR_FAIL_COND_V_MSG(
			world_shader.is_null(),
			Ref<ArrayMesh>(),
			"World mesh builder: failed to load q3_world.gdshader."
		);

		std::map<
			MaterialKey,
			MeshGeometry
		> groups;

		const Error error =
			BuildGeometryGroups(
				p_world,
				p_surface_indices,
				groups
			);

		ERR_FAIL_COND_V(
			error != OK,
			Ref<ArrayMesh>()
		);

		if (groups.empty())
			return Ref<ArrayMesh>();

		return CreateArrayMesh(
			p_world,
			groups,
			p_paks,
			world_shader
		);
	}

	Ref<ArrayMesh> BuildArrayMeshFromWorldModel(
		const World &p_world,
		uint32_t p_model_index,
		const std::vector<Ref<ZIPReader>> &p_paks)
	{
		ERR_FAIL_COND_V_MSG(
			p_model_index >=
				p_world.brush_models.size(),
			Ref<ArrayMesh>(),
			"World mesh builder: invalid BSP model index."
		);

		return BuildArrayMeshFromSurfaces(
			p_world,
			BuildModelSurfaceList(
				p_world,
				p_model_index
			),
			p_paks
		);
	}

	Ref<ArrayMesh> BuildArrayMeshFromWorldExcludingSurfaces(
		const World &p_world,
		const std::vector<Ref<ZIPReader>> &p_paks,
		const std::vector<uint32_t> &p_excluded_surfaces)
	{
		ERR_FAIL_COND_V_MSG(
			p_world.brush_models.empty(),
			Ref<ArrayMesh>(),
			"World mesh builder: world contains no BSP models."
		);

		const BrushModel &world_model =
			p_world.brush_models[0];

		std::vector<uint32_t> surfaces;

		surfaces.reserve(
			world_model.surface_count
		);

		for (
			uint32_t i = 0;
			i < world_model.surface_count;
			++i
		)
		{
			const uint32_t surface_index =
				world_model.first_surface + i;

			if (
				std::find(
					p_excluded_surfaces.begin(),
					p_excluded_surfaces.end(),
					surface_index
				) !=
				p_excluded_surfaces.end()
			)
			{
				continue;
			}

			surfaces.push_back(
				surface_index
			);
		}

		return BuildArrayMeshFromSurfaces(
			p_world,
			surfaces,
			p_paks
		);
	}

	Ref<ArrayMesh> BuildArrayMeshFromWorld(
		const World &p_world,
		const std::vector<Ref<ZIPReader>> &p_paks)
	{
		return BuildArrayMeshFromWorldModel(
			p_world,
			0,
			p_paks
		);
	}
}