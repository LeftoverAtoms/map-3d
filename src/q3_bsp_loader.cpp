#include "q3_bsp_loader.h"

#include "bsp_format.h"

#include <algorithm>
#include <array>

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

namespace
{
	inline constexpr int PATCH_SUBDIVISIONS = 8;
	inline constexpr real_t FLARE_HALF_SIZE = 8.0;
	inline constexpr std::int32_t SURF_NODRAW = 0x80;

	struct srfGridMesh_t
	{
		int width = 0;
		int height = 0;
		std::vector<q3::bsp::drawVert_t> verts;
	};

	struct world_t
	{
		PackedVector3Array vertices;
		PackedVector3Array normals;
		PackedVector2Array uvs;
		PackedColorArray colors;
		PackedInt32Array indices;
		int bad_surfaces = 0;
		int planar_surfaces = 0;
		int patch_surfaces = 0;
		int triangle_soup_surfaces = 0;
		int flare_surfaces = 0;
		int skipped_surfaces = 0;
	};

	Vector3 q3_to_godot_position(const q3::bsp::vec3_t &p_value)
	{
		return Vector3(p_value[0], p_value[2], -p_value[1]);
	}

	Vector3 q3_to_godot_normal(const q3::bsp::vec3_t &p_value)
	{
		return Vector3(p_value[0], p_value[2], -p_value[1]).normalized();
	}

	Color q3_to_godot_color(const std::uint8_t p_color[4])
	{
		return Color(p_color[0] / 255.0f, p_color[1] / 255.0f, p_color[2] / 255.0f, p_color[3] / 255.0f);
	}

	Color q3_to_godot_color(const q3::bsp::vec3_t &p_color)
	{
		return Color(std::clamp(p_color[0], 0.0f, 1.0f), std::clamp(p_color[1], 0.0f, 1.0f), std::clamp(p_color[2], 0.0f, 1.0f), 1.0f);
	}

	float LerpFloat(float p_a, float p_b, float p_t)
	{
		return p_a + (p_b - p_a) * p_t;
	}

	bool CheckSurfaceRange(const q3::bsp::BspData &p_bsp, const q3::bsp::dsurface_t &p_surface)
	{
		if (p_surface.firstVert < 0 || p_surface.numVerts < 0 || p_surface.firstIndex < 0 || p_surface.numIndexes < 0) {
			return false;
		}

		const auto first_vert = static_cast<std::size_t>(p_surface.firstVert);
		const auto num_verts = static_cast<std::size_t>(p_surface.numVerts);
		const auto first_index = static_cast<std::size_t>(p_surface.firstIndex);
		const auto num_indexes = static_cast<std::size_t>(p_surface.numIndexes);
		return first_vert <= p_bsp.drawVerts.size() &&
				num_verts <= p_bsp.drawVerts.size() - first_vert &&
				first_index <= p_bsp.drawIndexes.size() &&
				num_indexes <= p_bsp.drawIndexes.size() - first_index;
	}

	bool ShaderIsNoDraw(const q3::bsp::BspData &p_bsp, int p_shader_num)
	{
		if (p_shader_num < 0 || static_cast<std::size_t>(p_shader_num) >= p_bsp.shaders.size()) {
			return false;
		}
		return (p_bsp.shaders[static_cast<std::size_t>(p_shader_num)].surfaceFlags & SURF_NODRAW) != 0;
	}

	void EmitDrawVert(world_t &r_worldData, const q3::bsp::drawVert_t &p_vertex)
	{
		r_worldData.vertices.push_back(q3_to_godot_position(p_vertex.xyz));
		r_worldData.normals.push_back(q3_to_godot_normal(p_vertex.normal));
		r_worldData.uvs.push_back(Vector2(p_vertex.st[0], p_vertex.st[1]));
		r_worldData.colors.push_back(q3_to_godot_color(p_vertex.color));
	}

	q3::bsp::drawVert_t LerpDrawVert(const q3::bsp::drawVert_t &p_a, const q3::bsp::drawVert_t &p_b, float p_t)
	{
		q3::bsp::drawVert_t out {};
		for (int i = 0; i < 3; ++i) {
			out.xyz[i] = LerpFloat(p_a.xyz[i], p_b.xyz[i], p_t);
			out.normal[i] = LerpFloat(p_a.normal[i], p_b.normal[i], p_t);
		}
		for (int i = 0; i < 2; ++i) {
			out.st[i] = LerpFloat(p_a.st[i], p_b.st[i], p_t);
			out.lightmap[i] = LerpFloat(p_a.lightmap[i], p_b.lightmap[i], p_t);
		}
		for (int i = 0; i < 4; ++i) {
			out.color[i] = static_cast<std::uint8_t>(LerpFloat(static_cast<float>(p_a.color[i]), static_cast<float>(p_b.color[i]), p_t));
		}
		return out;
	}

	q3::bsp::drawVert_t QuadraticBezier(
			const q3::bsp::drawVert_t &p_a,
			const q3::bsp::drawVert_t &p_b,
			const q3::bsp::drawVert_t &p_c,
			float p_t)
	{
		return LerpDrawVert(LerpDrawVert(p_a, p_b, p_t), LerpDrawVert(p_b, p_c, p_t), p_t);
	}

	srfGridMesh_t R_SubdividePatchToGrid(int width, int height, const q3::bsp::drawVert_t *points)
	{
		srfGridMesh_t grid;
		const int patch_count_x = (width - 1) / 2;
		const int patch_count_y = (height - 1) / 2;
		grid.width = patch_count_x * PATCH_SUBDIVISIONS + 1;
		grid.height = patch_count_y * PATCH_SUBDIVISIONS + 1;
		grid.verts.resize(static_cast<std::size_t>(grid.width * grid.height));

		for (int y = 0; y < grid.height; ++y) {
			const float patch_y = static_cast<float>(y) / static_cast<float>(PATCH_SUBDIVISIONS);
			const int py = std::min(static_cast<int>(patch_y), patch_count_y - 1);
			const float v = patch_y - static_cast<float>(py);
			for (int x = 0; x < grid.width; ++x) {
				const float patch_x = static_cast<float>(x) / static_cast<float>(PATCH_SUBDIVISIONS);
				const int px = std::min(static_cast<int>(patch_x), patch_count_x - 1);
				const float u = patch_x - static_cast<float>(px);
				const int src_x = px * 2;
				const int src_y = py * 2;

				std::array<q3::bsp::drawVert_t, 3> row_points;
				for (int row = 0; row < 3; ++row) {
					const int offset = (src_y + row) * width + src_x;
					row_points[row] = QuadraticBezier(points[offset], points[offset + 1], points[offset + 2], u);
				}
				grid.verts[static_cast<std::size_t>(y * grid.width + x)] = QuadraticBezier(row_points[0], row_points[1], row_points[2], v);
			}
		}

		return grid;
	}

	void EmitGridMesh(const srfGridMesh_t &grid, world_t &r_worldData)
	{
		const int base = r_worldData.vertices.size();
		for (const q3::bsp::drawVert_t &vert : grid.verts) {
			EmitDrawVert(r_worldData, vert);
		}

		for (int y = 0; y < grid.height - 1; ++y) {
			for (int x = 0; x < grid.width - 1; ++x) {
				const int i0 = base + y * grid.width + x;
				const int i1 = base + y * grid.width + x + 1;
				const int i2 = base + (y + 1) * grid.width + x;
				const int i3 = base + (y + 1) * grid.width + x + 1;
				r_worldData.indices.push_back(i0);
				r_worldData.indices.push_back(i2);
				r_worldData.indices.push_back(i1);
				r_worldData.indices.push_back(i1);
				r_worldData.indices.push_back(i2);
				r_worldData.indices.push_back(i3);
			}
		}
	}

	void ParseFace(const q3::bsp::BspData &p_bsp, const q3::bsp::dsurface_t &ds, world_t &r_worldData)
	{
		if (ds.numIndexes % 3 != 0 || !CheckSurfaceRange(p_bsp, ds)) {
			UtilityFunctions::push_warning("Skipping malformed BSP face.");
			++r_worldData.skipped_surfaces;
			return;
		}

		const int base = r_worldData.vertices.size();
		for (int i = 0; i < ds.numVerts; ++i) {
			EmitDrawVert(r_worldData, p_bsp.drawVerts[static_cast<std::size_t>(ds.firstVert + i)]);
		}

		for (int i = 0; i < ds.numIndexes; ++i) {
			const std::int32_t index = p_bsp.drawIndexes[static_cast<std::size_t>(ds.firstIndex + i)];
			if (index < 0 || index >= ds.numVerts) {
				UtilityFunctions::push_warning("Skipping malformed BSP face index.");
				continue;
			}
			r_worldData.indices.push_back(base + index);
		}
	}

	void ParseMesh(const q3::bsp::BspData &p_bsp, const q3::bsp::dsurface_t &ds, world_t &r_worldData)
	{
		if (ShaderIsNoDraw(p_bsp, ds.shaderNum)) {
			++r_worldData.skipped_surfaces;
			return;
		}

		if (!CheckSurfaceRange(p_bsp, ds) ||
				ds.patchWidth < 3 ||
				ds.patchHeight < 3 ||
				(ds.patchWidth & 1) == 0 ||
				(ds.patchHeight & 1) == 0 ||
				ds.patchWidth * ds.patchHeight > ds.numVerts) {
			UtilityFunctions::push_warning("Skipping malformed BSP patch mesh.");
			++r_worldData.skipped_surfaces;
			return;
		}

		const q3::bsp::drawVert_t *verts = p_bsp.drawVerts.data() + ds.firstVert;
		const srfGridMesh_t grid = R_SubdividePatchToGrid(ds.patchWidth, ds.patchHeight, verts);
		EmitGridMesh(grid, r_worldData);
	}

	void ParseTriSurf(const q3::bsp::BspData &p_bsp, const q3::bsp::dsurface_t &ds, world_t &r_worldData)
	{
		ParseFace(p_bsp, ds, r_worldData);
	}

	void ParseFlare(const q3::bsp::dsurface_t &ds, world_t &r_worldData)
	{
		Vector3 origin = q3_to_godot_position(ds.lightmapOrigin);
		Vector3 normal = q3_to_godot_normal(ds.lightmapVecs[2]);
		if (normal.length_squared() == 0.0) {
			normal = Vector3(0, 1, 0);
		}

		Vector3 tangent = normal.cross(Vector3(0, 1, 0));
		if (tangent.length_squared() < 0.001) {
			tangent = normal.cross(Vector3(1, 0, 0));
		}
		tangent.normalize();
		Vector3 bitangent = normal.cross(tangent).normalized();
		const Color color = q3_to_godot_color(ds.lightmapVecs[0]);
		const int base = r_worldData.vertices.size();

		const std::array<Vector3, 4> positions {
			origin - tangent * FLARE_HALF_SIZE - bitangent * FLARE_HALF_SIZE,
			origin + tangent * FLARE_HALF_SIZE - bitangent * FLARE_HALF_SIZE,
			origin - tangent * FLARE_HALF_SIZE + bitangent * FLARE_HALF_SIZE,
			origin + tangent * FLARE_HALF_SIZE + bitangent * FLARE_HALF_SIZE,
		};
		const std::array<Vector2, 4> uvs {
			Vector2(0, 0),
			Vector2(1, 0),
			Vector2(0, 1),
			Vector2(1, 1),
		};

		for (int i = 0; i < 4; ++i) {
			r_worldData.vertices.push_back(positions[i]);
			r_worldData.normals.push_back(normal);
			r_worldData.uvs.push_back(uvs[i]);
			r_worldData.colors.push_back(color);
		}
		r_worldData.indices.push_back(base + 0);
		r_worldData.indices.push_back(base + 2);
		r_worldData.indices.push_back(base + 1);
		r_worldData.indices.push_back(base + 1);
		r_worldData.indices.push_back(base + 2);
		r_worldData.indices.push_back(base + 3);
	}
}

void Q3BspLoader::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("load_mesh", "path"), &Q3BspLoader::load_mesh);
}

Ref<ArrayMesh> Q3BspLoader::load_mesh(const String &p_path) const
{
	String file_path = p_path;
	if (file_path.begins_with("res://") || file_path.begins_with("user://")) {
		file_path = ProjectSettings::get_singleton()->globalize_path(file_path);
	}

	q3::bsp::BspData bsp;
	std::string error;
	if (!q3::bsp::load_file(file_path.utf8().get_data(), bsp, error)) {
		UtilityFunctions::push_error(String("Failed to load Quake 3 BSP '") + p_path + "': " + error.c_str());
		return Ref<ArrayMesh>();
	}

	world_t worldData;

	for (const q3::bsp::dsurface_t &ds : bsp.surfaces) {
		switch (ds.surfaceType) {
			case q3::bsp::MST_BAD:
				++worldData.bad_surfaces;
				break;
			case q3::bsp::MST_PLANAR:
				++worldData.planar_surfaces;
				ParseFace(bsp, ds, worldData);
				break;
			case q3::bsp::MST_PATCH:
				++worldData.patch_surfaces;
				ParseMesh(bsp, ds, worldData);
				break;
			case q3::bsp::MST_TRIANGLE_SOUP:
				++worldData.triangle_soup_surfaces;
				ParseTriSurf(bsp, ds, worldData);
				break;
			case q3::bsp::MST_FLARE:
				++worldData.flare_surfaces;
				ParseFlare(ds, worldData);
				break;
			default:
				UtilityFunctions::push_warning(String("Skipping unknown BSP surface type: ") + String::num_int64(ds.surfaceType));
				++worldData.skipped_surfaces;
				break;
		}
	}

	if (worldData.vertices.is_empty() || worldData.indices.is_empty()) {
		UtilityFunctions::push_error("BSP loaded, but no mesh geometry was found.");
		return Ref<ArrayMesh>();
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = worldData.vertices;
	arrays[Mesh::ARRAY_NORMAL] = worldData.normals;
	arrays[Mesh::ARRAY_TEX_UV] = worldData.uvs;
	arrays[Mesh::ARRAY_COLOR] = worldData.colors;
	arrays[Mesh::ARRAY_INDEX] = worldData.indices;

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	UtilityFunctions::print(String("Q3 BSP surfaces: bad=") + String::num_int64(worldData.bad_surfaces) +
			" planar=" + String::num_int64(worldData.planar_surfaces) +
			" patch=" + String::num_int64(worldData.patch_surfaces) +
			" triangle_soup=" + String::num_int64(worldData.triangle_soup_surfaces) +
			" flare=" + String::num_int64(worldData.flare_surfaces) +
			" skipped=" + String::num_int64(worldData.skipped_surfaces));
	return mesh;
}
