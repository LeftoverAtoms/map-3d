#include "q3_bsp_draw_list_effect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state_attachment.hpp>
#include <godot_cpp/classes/rd_pipeline_depth_stencil_state.hpp>
#include <godot_cpp/classes/rd_pipeline_multisample_state.hpp>
#include <godot_cpp/classes/rd_pipeline_rasterization_state.hpp>
#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rd_vertex_attribute.hpp>
#include <godot_cpp/classes/render_scene_buffers.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/render_scene_data.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/plane.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

namespace
{
	inline constexpr int PATCH_SUBDIVISIONS = 8;
	inline constexpr int LIGHTMAP_SIZE = 128;
	inline constexpr int LIGHTMAP_BYTES = LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4;
	inline constexpr int VERTEX_STRIDE = 11 * 4;
	inline constexpr std::int32_t SURF_NODRAW = 0x80;

	struct srfGridMesh_t
	{
		int width = 0;
		int height = 0;
		std::vector<q3::bsp::drawVert_t> verts;
	};

	struct ViewFrustum
	{
		Projection view_projection;
	};

	Vector3 q3_to_godot_position(const q3::bsp::vec3_t &p_value)
	{
		return Vector3(p_value[0], p_value[2], -p_value[1]);
	}

	Vector3 godot_to_q3_position(const Vector3 &p_value)
	{
		return Vector3(p_value.x, -p_value.z, p_value.y);
	}

	Color q3_to_godot_color(const std::uint8_t p_color[4])
	{
		return Color(p_color[0] / 255.0f, p_color[1] / 255.0f, p_color[2] / 255.0f, p_color[3] / 255.0f);
	}

	String qpath_to_string(const char *p_value, std::size_t p_length)
	{
		std::size_t length = 0;
		while (length < p_length && p_value[length] != '\0') {
			++length;
		}
		return String::utf8(p_value, length);
	}

	String resolve_bsp_file_path(const String &p_path)
	{
		String file_path = p_path.strip_edges();
		if ((file_path.begins_with("\"") && file_path.ends_with("\"")) ||
				(file_path.begins_with("'") && file_path.ends_with("'"))) {
			file_path = file_path.substr(1, file_path.length() - 2).strip_edges();
		}

		if (file_path.begins_with("res://") || file_path.begins_with("user://")) {
			return ProjectSettings::get_singleton()->globalize_path(file_path);
		}

		const String normalized = file_path.replace("\\", "/");
		if (normalized.begins_with("/") || normalized.begins_with("//") ||
				(normalized.length() >= 3 && normalized[1] == ':' && normalized[2] == '/')) {
			return file_path;
		}

		if (normalized.begins_with("project/")) {
			return ProjectSettings::get_singleton()->globalize_path(String("res://") + normalized.substr(8));
		}

		if (normalized.begins_with("./")) {
			return ProjectSettings::get_singleton()->globalize_path(String("res://") + normalized.substr(2));
		}

		return ProjectSettings::get_singleton()->globalize_path(String("res://") + normalized);
	}

	RID create_texture_rgba8(RenderingDevice *p_rd, int p_width, int p_height, const PackedByteArray &p_data)
	{
		if (p_rd == nullptr || p_width <= 0 || p_height <= 0 || p_data.is_empty()) {
			return RID();
		}

		Ref<RDTextureFormat> format;
		format.instantiate();
		format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
		format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
		format->set_width(static_cast<uint32_t>(p_width));
		format->set_height(static_cast<uint32_t>(p_height));
		format->set_depth(1);
		format->set_array_layers(1);
		format->set_mipmaps(1);
		format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
		format->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT);

		Ref<RDTextureView> view;
		view.instantiate();

		TypedArray<PackedByteArray> texture_data;
		texture_data.push_back(p_data);
		return p_rd->texture_create(format, view, texture_data);
	}

	Ref<Image> load_image_from_file_bytes(const String &p_path)
	{
		if (!FileAccess::file_exists(p_path)) {
			return Ref<Image>();
		}

		const PackedByteArray bytes = FileAccess::get_file_as_bytes(p_path);
		if (bytes.is_empty()) {
			return Ref<Image>();
		}

		Ref<Image> image;
		image.instantiate();
		const String lower_path = p_path.to_lower();
		Error error = ERR_UNAVAILABLE;
		if (lower_path.ends_with(".png")) {
			error = image->load_png_from_buffer(bytes);
		} else if (lower_path.ends_with(".jpg") || lower_path.ends_with(".jpeg")) {
			error = image->load_jpg_from_buffer(bytes);
		} else if (lower_path.ends_with(".webp")) {
			error = image->load_webp_from_buffer(bytes);
		} else if (lower_path.ends_with(".tga")) {
			error = image->load_tga_from_buffer(bytes);
		}

		if (error != OK || image->is_empty()) {
			return Ref<Image>();
		}
		return image;
	}

	bool shader_is_nodraw(const q3::bsp::world_t *p_world, int p_shader_num)
	{
		if (p_world == nullptr || p_shader_num < 0 || p_shader_num >= p_world->numShaders) {
			return false;
		}
		return (p_world->shaders[static_cast<std::size_t>(p_shader_num)].surfaceFlags & SURF_NODRAW) != 0;
	}

	q3::bsp::drawVert_t draw_vert_from_face_point(const q3::bsp::srfSurfaceFace_t &p_face, std::size_t p_point)
	{
		q3::bsp::drawVert_t out {};
		const std::size_t base = p_point * 8;
		out.xyz[0] = p_face.points[base + 0];
		out.xyz[1] = p_face.points[base + 1];
		out.xyz[2] = p_face.points[base + 2];
		out.st[0] = p_face.points[base + 3];
		out.st[1] = p_face.points[base + 4];
		out.lightmap[0] = p_face.points[base + 5];
		out.lightmap[1] = p_face.points[base + 6];
		std::memcpy(out.color, &p_face.points[base + 7], sizeof(out.color));
		return out;
	}

	void write_vertex(PackedByteArray &r_data, int p_index, const q3::bsp::drawVert_t &p_vertex)
	{
		const int offset = p_index * VERTEX_STRIDE;
		const Vector3 position = q3_to_godot_position(p_vertex.xyz);
		const Color color = q3_to_godot_color(p_vertex.color);
		r_data.encode_float(offset + 0, position.x);
		r_data.encode_float(offset + 4, position.y);
		r_data.encode_float(offset + 8, position.z);
		r_data.encode_float(offset + 12, color.r);
		r_data.encode_float(offset + 16, color.g);
		r_data.encode_float(offset + 20, color.b);
		r_data.encode_float(offset + 24, 1.0);
		r_data.encode_float(offset + 28, p_vertex.st[0]);
		r_data.encode_float(offset + 32, p_vertex.st[1]);
		r_data.encode_float(offset + 36, p_vertex.lightmap[0]);
		r_data.encode_float(offset + 40, p_vertex.lightmap[1]);
	}

	PackedByteArray pack_draw_push_constants(const Projection &p_projection, int p_debug_draw_mode)
	{
		PackedByteArray data;
		data.resize(20 * 4);
		for (int column = 0; column < 4; ++column) {
			for (int row = 0; row < 4; ++row) {
				data.encode_float((column * 4 + row) * 4, p_projection[column][row]);
			}
		}
		data.encode_float(16 * 4, static_cast<float>(p_debug_draw_mode));
		data.encode_float(17 * 4, 0.0f);
		data.encode_float(18 * 4, 0.0f);
		data.encode_float(19 * 4, 0.0f);
		return data;
	}

	float lerp_float(float p_a, float p_b, float p_t)
	{
		return p_a + (p_b - p_a) * p_t;
	}

	q3::bsp::drawVert_t lerp_draw_vert(const q3::bsp::drawVert_t &p_a, const q3::bsp::drawVert_t &p_b, float p_t)
	{
		q3::bsp::drawVert_t out {};
		for (int i = 0; i < 3; ++i) {
			out.xyz[i] = lerp_float(p_a.xyz[i], p_b.xyz[i], p_t);
			out.normal[i] = lerp_float(p_a.normal[i], p_b.normal[i], p_t);
		}
		for (int i = 0; i < 2; ++i) {
			out.st[i] = lerp_float(p_a.st[i], p_b.st[i], p_t);
			out.lightmap[i] = lerp_float(p_a.lightmap[i], p_b.lightmap[i], p_t);
		}
		for (int i = 0; i < 4; ++i) {
			out.color[i] = static_cast<std::uint8_t>(lerp_float(static_cast<float>(p_a.color[i]), static_cast<float>(p_b.color[i]), p_t));
		}
		return out;
	}

	q3::bsp::drawVert_t quadratic_bezier(
			const q3::bsp::drawVert_t &p_a,
			const q3::bsp::drawVert_t &p_b,
			const q3::bsp::drawVert_t &p_c,
			float p_t)
	{
		return lerp_draw_vert(lerp_draw_vert(p_a, p_b, p_t), lerp_draw_vert(p_b, p_c, p_t), p_t);
	}

	srfGridMesh_t subdivide_patch_to_grid(int p_width, int p_height, const q3::bsp::drawVert_t *p_points)
	{
		srfGridMesh_t grid;
		const int patch_count_x = (p_width - 1) / 2;
		const int patch_count_y = (p_height - 1) / 2;
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
					const int offset = (src_y + row) * p_width + src_x;
					row_points[row] = quadratic_bezier(p_points[offset], p_points[offset + 1], p_points[offset + 2], u);
				}
				grid.verts[static_cast<std::size_t>(y * grid.width + x)] = quadratic_bezier(row_points[0], row_points[1], row_points[2], v);
			}
		}

		return grid;
	}

	void append_patch_grid(const srfGridMesh_t &p_grid, std::vector<q3::bsp::drawVert_t> &r_vertices, std::vector<std::uint32_t> &r_indices)
	{
		const std::uint32_t base = static_cast<std::uint32_t>(r_vertices.size());
		r_vertices.insert(r_vertices.end(), p_grid.verts.begin(), p_grid.verts.end());

		for (int y = 0; y < p_grid.height - 1; ++y) {
			for (int x = 0; x < p_grid.width - 1; ++x) {
				const std::uint32_t i0 = base + static_cast<std::uint32_t>(y * p_grid.width + x);
				const std::uint32_t i1 = i0 + 1;
				const std::uint32_t i2 = base + static_cast<std::uint32_t>((y + 1) * p_grid.width + x);
				const std::uint32_t i3 = i2 + 1;
				r_indices.push_back(i0);
				r_indices.push_back(i2);
				r_indices.push_back(i1);
				r_indices.push_back(i1);
				r_indices.push_back(i2);
				r_indices.push_back(i3);
			}
		}
	}

	AABB make_bounds(const std::vector<q3::bsp::drawVert_t> &p_vertices, std::size_t p_first_vertex, std::size_t p_vertex_count)
	{
		if (p_vertex_count == 0 || p_first_vertex >= p_vertices.size()) {
			return AABB();
		}

		AABB bounds(q3_to_godot_position(p_vertices[p_first_vertex].xyz), Vector3());
		const std::size_t end = std::min(p_vertices.size(), p_first_vertex + p_vertex_count);
		for (std::size_t i = p_first_vertex + 1; i < end; ++i) {
			bounds.expand_to(q3_to_godot_position(p_vertices[i].xyz));
		}
		return bounds.grow(1.0);
	}

	bool aabb_intersects_frustum(const AABB &p_bounds, const ViewFrustum &p_frustum, float p_margin)
	{
		const AABB bounds = p_bounds.grow(std::max<real_t>(p_margin, 0.0));
		const Vector3 min = bounds.position;
		const Vector3 max = bounds.position + bounds.size;
		const Vector3 points[8] = {
			Vector3(min.x, min.y, min.z),
			Vector3(max.x, min.y, min.z),
			Vector3(min.x, max.y, min.z),
			Vector3(max.x, max.y, min.z),
			Vector3(min.x, min.y, max.z),
			Vector3(max.x, min.y, max.z),
			Vector3(min.x, max.y, max.z),
			Vector3(max.x, max.y, max.z),
		};

		bool all_left = true;
		bool all_right = true;
		bool all_bottom = true;
		bool all_top = true;
		bool all_near = true;
		bool all_far = true;
		for (const Vector3 &point : points) {
			const float x =
					p_frustum.view_projection[0][0] * point.x +
					p_frustum.view_projection[1][0] * point.y +
					p_frustum.view_projection[2][0] * point.z +
					p_frustum.view_projection[3][0];
			const float y =
					p_frustum.view_projection[0][1] * point.x +
					p_frustum.view_projection[1][1] * point.y +
					p_frustum.view_projection[2][1] * point.z +
					p_frustum.view_projection[3][1];
			const float z =
					p_frustum.view_projection[0][2] * point.x +
					p_frustum.view_projection[1][2] * point.y +
					p_frustum.view_projection[2][2] * point.z +
					p_frustum.view_projection[3][2];
			const float w =
					p_frustum.view_projection[0][3] * point.x +
					p_frustum.view_projection[1][3] * point.y +
					p_frustum.view_projection[2][3] * point.z +
					p_frustum.view_projection[3][3];
			const float clip_w = std::abs(w);
			all_left = all_left && x < -clip_w;
			all_right = all_right && x > clip_w;
			all_bottom = all_bottom && y < -clip_w;
			all_top = all_top && y > clip_w;
			all_near = all_near && z < -clip_w;
			all_far = all_far && z > clip_w;
		}
		return !(all_left || all_right || all_bottom || all_top || all_near || all_far);
	}

	ViewFrustum get_view_frustum(RenderSceneData *p_scene_data)
	{
		ViewFrustum frustum;
		if (p_scene_data == nullptr) {
			return frustum;
		}

		frustum.view_projection = p_scene_data->get_cam_projection() * Projection(p_scene_data->get_cam_transform().affine_inverse());
		return frustum;
	}

	bool cluster_visible(const q3::bsp::world_t *p_world, int p_view_cluster, int p_test_cluster)
	{
		if (p_test_cluster < 0) {
			return false;
		}
		if (p_world == nullptr || p_world->vis == nullptr || p_world->numClusters <= 0 || p_world->clusterBytes <= 0) {
			return true;
		}
		if (p_view_cluster < 0 || p_view_cluster >= p_world->numClusters || p_test_cluster >= p_world->numClusters) {
			return true;
		}
		const std::size_t offset = static_cast<std::size_t>(p_view_cluster) * static_cast<std::size_t>(p_world->clusterBytes) + static_cast<std::size_t>(p_test_cluster >> 3);
		return (static_cast<std::uint8_t>(p_world->vis[offset]) & (1 << (p_test_cluster & 7))) != 0;
	}

	int point_leafnum(const q3::bsp::world_t *p_world, const Vector3 &p_q3_point)
	{
		if (p_world == nullptr || p_world->nodes == nullptr || p_world->numnodes <= 0) {
			return -1;
		}

		q3::bsp::mnode_t *node = p_world->nodes.get();
		while (node->contents == q3::bsp::CONTENTS_NODE) {
			if (node->plane == nullptr) {
				return -1;
			}
			const q3::bsp::cplane_t &plane = *node->plane;
			const float distance =
					p_q3_point.x * plane.normal[0] +
					p_q3_point.y * plane.normal[1] +
					p_q3_point.z * plane.normal[2] -
					plane.dist;
			node = node->children[distance >= 0.0f ? 0 : 1];
			if (node == nullptr) {
				return -1;
			}
		}

		const auto leaf_num = static_cast<int>(node - (p_world->nodes.get() + p_world->numDecisionNodes));
		if (leaf_num < 0 || p_world->numDecisionNodes + leaf_num >= p_world->numnodes) {
			return -1;
		}
		return leaf_num;
	}

	std::vector<std::uint8_t> mark_pvs_surfaces(
			const q3::bsp::world_t *p_world,
			int p_view_cluster,
			int &r_marked_surface_count)
	{
		const int surface_count = p_world ? p_world->numsurfaces : 0;
		std::vector<std::uint8_t> surface_visible(static_cast<std::size_t>(std::max(surface_count, 0)), 0);
		r_marked_surface_count = 0;
		if (p_world == nullptr || p_world->nodes == nullptr || p_world->surfaces == nullptr) {
			return surface_visible;
		}
		for (int i = p_world->numDecisionNodes; i < p_world->numnodes; ++i) {
			const q3::bsp::mnode_t &leaf = p_world->nodes[static_cast<std::size_t>(i)];
			if (!cluster_visible(p_world, p_view_cluster, leaf.cluster)) {
				continue;
			}
			if (leaf.firstmarksurface == nullptr || leaf.nummarksurfaces < 0) {
				continue;
			}
			for (int mark = 0; mark < leaf.nummarksurfaces; ++mark) {
				q3::bsp::msurface_t *surface = leaf.firstmarksurface[mark];
				if (surface == nullptr) {
					continue;
				}
				const auto surface_index = static_cast<int>(surface - p_world->surfaces.get());
				if (surface_index < 0 || surface_index >= p_world->numsurfaces) {
					continue;
				}
				std::uint8_t &visible = surface_visible[static_cast<std::size_t>(surface_index)];
				if (!visible) {
					visible = 1;
					++r_marked_surface_count;
				}
			}
		}
		return surface_visible;
	}
}

void Q3BspDrawListEffect::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_rendered_frame_count"), &Q3BspDrawListEffect::get_rendered_frame_count);
	ClassDB::bind_method(D_METHOD("get_draw_call_count"), &Q3BspDrawListEffect::get_draw_call_count);
	ClassDB::bind_method(D_METHOD("get_vertex_count"), &Q3BspDrawListEffect::get_vertex_count);
	ClassDB::bind_method(D_METHOD("get_index_count"), &Q3BspDrawListEffect::get_index_count);
	ClassDB::bind_method(D_METHOD("get_visible_surface_count"), &Q3BspDrawListEffect::get_visible_surface_count);
	ClassDB::bind_method(D_METHOD("get_pvs_rejected_surface_count"), &Q3BspDrawListEffect::get_pvs_rejected_surface_count);
	ClassDB::bind_method(D_METHOD("get_frustum_rejected_surface_count"), &Q3BspDrawListEffect::get_frustum_rejected_surface_count);
	ClassDB::bind_method(D_METHOD("get_pvs_surface_count"), &Q3BspDrawListEffect::get_pvs_surface_count);
	ClassDB::bind_method(D_METHOD("get_view_cluster"), &Q3BspDrawListEffect::get_view_cluster);
	ClassDB::bind_method(D_METHOD("get_lightmap_count"), &Q3BspDrawListEffect::get_lightmap_count);
	ClassDB::bind_method(D_METHOD("get_lightmap_bind_count"), &Q3BspDrawListEffect::get_lightmap_bind_count);
	ClassDB::bind_method(D_METHOD("get_base_texture_count"), &Q3BspDrawListEffect::get_base_texture_count);
	ClassDB::bind_method(D_METHOD("get_material_count"), &Q3BspDrawListEffect::get_material_count);
	ClassDB::bind_method(D_METHOD("get_material_bind_count"), &Q3BspDrawListEffect::get_material_bind_count);
	ClassDB::bind_method(D_METHOD("get_shader_script_mapping_count"), &Q3BspDrawListEffect::get_shader_script_mapping_count);
	ClassDB::bind_method(D_METHOD("get_last_draw_list_opened"), &Q3BspDrawListEffect::get_last_draw_list_opened);
	ClassDB::bind_method(D_METHOD("get_pipeline_valid"), &Q3BspDrawListEffect::get_pipeline_valid);
	ClassDB::bind_method(D_METHOD("get_geometry_uploaded"), &Q3BspDrawListEffect::get_geometry_uploaded);
	ClassDB::bind_method(D_METHOD("set_bsp_path", "path"), &Q3BspDrawListEffect::set_bsp_path);
	ClassDB::bind_method(D_METHOD("get_bsp_path"), &Q3BspDrawListEffect::get_bsp_path);
	ClassDB::bind_method(D_METHOD("get_resolved_bsp_path"), &Q3BspDrawListEffect::get_resolved_bsp_path);
	ClassDB::bind_method(D_METHOD("get_load_error"), &Q3BspDrawListEffect::get_load_error);
	ClassDB::bind_method(D_METHOD("load_bsp", "path"), &Q3BspDrawListEffect::load_bsp, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("set_enable_pvs_culling", "enabled"), &Q3BspDrawListEffect::set_enable_pvs_culling);
	ClassDB::bind_method(D_METHOD("get_enable_pvs_culling"), &Q3BspDrawListEffect::get_enable_pvs_culling);
	ClassDB::bind_method(D_METHOD("set_enable_frustum_culling", "enabled"), &Q3BspDrawListEffect::set_enable_frustum_culling);
	ClassDB::bind_method(D_METHOD("get_enable_frustum_culling"), &Q3BspDrawListEffect::get_enable_frustum_culling);
	ClassDB::bind_method(D_METHOD("set_debug_wireframe", "enabled"), &Q3BspDrawListEffect::set_debug_wireframe);
	ClassDB::bind_method(D_METHOD("get_debug_wireframe"), &Q3BspDrawListEffect::get_debug_wireframe);
	ClassDB::bind_method(D_METHOD("set_debug_draw_mode", "mode"), &Q3BspDrawListEffect::set_debug_draw_mode);
	ClassDB::bind_method(D_METHOD("get_debug_draw_mode"), &Q3BspDrawListEffect::get_debug_draw_mode);
	ClassDB::bind_method(D_METHOD("set_cull_bounds_margin", "margin"), &Q3BspDrawListEffect::set_cull_bounds_margin);
	ClassDB::bind_method(D_METHOD("get_cull_bounds_margin"), &Q3BspDrawListEffect::get_cull_bounds_margin);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "bsp_path", PROPERTY_HINT_GLOBAL_FILE, "*.bsp"), "set_bsp_path", "get_bsp_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "resolved_bsp_path", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_resolved_bsp_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "load_error", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_load_error");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_pvs_culling"), "set_enable_pvs_culling", "get_enable_pvs_culling");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enable_frustum_culling"), "set_enable_frustum_culling", "get_enable_frustum_culling");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_wireframe"), "set_debug_wireframe", "get_debug_wireframe");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_draw_mode", PROPERTY_HINT_ENUM, "Shaded,Wireframe,Base Texture,Lightmap,Vertex Color,Unshaded"), "set_debug_draw_mode", "get_debug_draw_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cull_bounds_margin", PROPERTY_HINT_RANGE, "0,1024,1"), "set_cull_bounds_margin", "get_cull_bounds_margin");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rendered_frame_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_rendered_frame_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "draw_call_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_draw_call_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "vertex_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_vertex_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "index_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_index_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "visible_surface_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_visible_surface_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pvs_rejected_surface_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_pvs_rejected_surface_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "frustum_rejected_surface_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_frustum_rejected_surface_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pvs_surface_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_pvs_surface_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "view_cluster", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_view_cluster");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lightmap_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_lightmap_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lightmap_bind_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_lightmap_bind_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "base_texture_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_base_texture_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_material_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "material_bind_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_material_bind_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shader_script_mapping_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_shader_script_mapping_count");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "last_draw_list_opened", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_last_draw_list_opened");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pipeline_valid", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_pipeline_valid");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "geometry_uploaded", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_geometry_uploaded");
}

Q3BspDrawListEffect::Q3BspDrawListEffect()
{
	set_effect_callback_type(CompositorEffect::EFFECT_CALLBACK_TYPE_POST_OPAQUE);
	set_enabled(true);
}

Q3BspDrawListEffect::~Q3BspDrawListEffect()
{
	free_rd_resources();
}

void Q3BspDrawListEffect::free_rd_resources()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr) {
		return;
	}
	RenderingDevice *rd = rs->get_rendering_device();
	if (rd == nullptr) {
		return;
	}

	if (pipeline.is_valid()) {
		rd->free_rid(pipeline);
		pipeline = RID();
	}
	free_geometry_resources(rd);
	if (shader.is_valid()) {
		rd->free_rid(shader);
		shader = RID();
	}
	if (lightmap_sampler.is_valid()) {
		rd->free_rid(lightmap_sampler);
		lightmap_sampler = RID();
	}
	if (base_sampler.is_valid()) {
		rd->free_rid(base_sampler);
		base_sampler = RID();
	}
	pipeline_valid = false;
	framebuffer_format = -1;
	vertex_format = -1;
	pipeline_wireframe = false;
}

void Q3BspDrawListEffect::free_geometry_resources(RenderingDevice *p_rd)
{
	if (vertex_array.is_valid()) {
		p_rd->free_rid(vertex_array);
		vertex_array = RID();
	}
	if (index_array.is_valid()) {
		p_rd->free_rid(index_array);
		index_array = RID();
	}
	for (DrawSurface &surface : draw_surfaces) {
		if (surface.index_array.is_valid()) {
			p_rd->free_rid(surface.index_array);
			surface.index_array = RID();
		}
		surface.uniform_set = RID();
	}
	draw_surfaces.clear();
	if (vertex_buffer.is_valid()) {
		p_rd->free_rid(vertex_buffer);
		vertex_buffer = RID();
	}
	if (index_buffer.is_valid()) {
		p_rd->free_rid(index_buffer);
		index_buffer = RID();
	}
	for (MaterialResource &material : materials) {
		if (material.uniform_set.is_valid()) {
			p_rd->free_rid(material.uniform_set);
			material.uniform_set = RID();
		}
	}
	materials.clear();
	if (lightmap_sampler.is_valid()) {
		p_rd->free_rid(lightmap_sampler);
		lightmap_sampler = RID();
	}
	if (base_sampler.is_valid()) {
		p_rd->free_rid(base_sampler);
		base_sampler = RID();
	}
	for (MaterialResource &lightmap : lightmaps) {
		if (lightmap.texture.is_valid()) {
			p_rd->free_rid(lightmap.texture);
			lightmap.texture = RID();
			lightmap.lightmap_texture = RID();
		}
	}
	lightmaps.clear();
	for (BaseTextureResource &base_texture : base_textures) {
		if (base_texture.loaded_from_asset && base_texture.texture.is_valid()) {
			p_rd->free_rid(base_texture.texture);
			base_texture.texture = RID();
		}
	}
	base_textures.clear();
	if (fallback_base_texture.is_valid()) {
		p_rd->free_rid(fallback_base_texture);
		fallback_base_texture = RID();
	}
	if (fallback_lightmap_uniform_set.is_valid()) {
		p_rd->free_rid(fallback_lightmap_uniform_set);
		fallback_lightmap_uniform_set = RID();
	}
	if (fallback_lightmap_texture.is_valid()) {
		p_rd->free_rid(fallback_lightmap_texture);
		fallback_lightmap_texture = RID();
	}
	geometry_uploaded = false;
	vertex_count = 0;
	index_count = 0;
	visible_surface_count = 0;
	pvs_rejected_surface_count = 0;
	frustum_rejected_surface_count = 0;
	pvs_surface_count = 0;
	view_cluster = -1;
	lightmap_count = 0;
	lightmap_bind_count = 0;
	base_texture_count = 0;
	material_count = 0;
	material_bind_count = 0;
	shader_script_mapping_count = 0;
	shader_scripts_loaded = false;
	shader_script_names.clear();
	shader_script_images.clear();
}

bool Q3BspDrawListEffect::upload_lightmaps(RenderingDevice *p_rd)
{
	if (!shader.is_valid()) {
		return false;
	}
	if (lightmap_sampler.is_valid() && base_sampler.is_valid() && fallback_base_texture.is_valid() && fallback_lightmap_texture.is_valid()) {
		return true;
	}

	Ref<RDSamplerState> lightmap_sampler_state;
	lightmap_sampler_state.instantiate();
	lightmap_sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
	lightmap_sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
	lightmap_sampler_state->set_mip_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
	lightmap_sampler_state->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	lightmap_sampler_state->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	lightmap_sampler_state->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE);
	lightmap_sampler = p_rd->sampler_create(lightmap_sampler_state);
	if (!lightmap_sampler.is_valid()) {
		return false;
	}

	Ref<RDSamplerState> base_sampler_state;
	base_sampler_state.instantiate();
	base_sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
	base_sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
	base_sampler_state->set_mip_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
	base_sampler_state->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT);
	base_sampler_state->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT);
	base_sampler_state->set_repeat_w(RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT);
	base_sampler = p_rd->sampler_create(base_sampler_state);
	if (!base_sampler.is_valid()) {
		return false;
	}

	PackedByteArray white_data;
	white_data.resize(LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4);
	for (int64_t i = 0; i < white_data.size(); i += 4) {
		white_data.encode_u8(i + 0, 255);
		white_data.encode_u8(i + 1, 255);
		white_data.encode_u8(i + 2, 255);
		white_data.encode_u8(i + 3, 255);
	}
	fallback_lightmap_texture = create_texture_rgba8(p_rd, LIGHTMAP_SIZE, LIGHTMAP_SIZE, white_data);
	if (!fallback_lightmap_texture.is_valid()) {
		return false;
	}

	PackedByteArray checker_data;
	checker_data.resize(LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4);
	for (int y = 0; y < LIGHTMAP_SIZE; ++y) {
		for (int x = 0; x < LIGHTMAP_SIZE; ++x) {
			const bool bright = ((x / 16) + (y / 16)) % 2 == 0;
			const uint8_t v = bright ? 220 : 150;
			const int64_t dst = static_cast<int64_t>(y * LIGHTMAP_SIZE + x) * 4;
			checker_data.encode_u8(dst + 0, v);
			checker_data.encode_u8(dst + 1, v);
			checker_data.encode_u8(dst + 2, v);
			checker_data.encode_u8(dst + 3, 255);
		}
	}
	fallback_base_texture = create_texture_rgba8(p_rd, LIGHTMAP_SIZE, LIGHTMAP_SIZE, checker_data);
	if (!fallback_base_texture.is_valid()) {
		return false;
	}
	base_texture_count = 1;

	if (world == nullptr) {
		lightmap_count = 0;
		return true;
	}

	const std::size_t count = static_cast<std::size_t>(world->numLightmaps);
	lightmaps.resize(count);
	for (std::size_t lightmap_index = 0; lightmap_index < count; ++lightmap_index) {
		PackedByteArray rgba;
		rgba.resize(LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4);
		const std::size_t src_base = lightmap_index * LIGHTMAP_BYTES;
		for (int pixel = 0; pixel < LIGHTMAP_SIZE * LIGHTMAP_SIZE; ++pixel) {
			const std::size_t src = src_base + static_cast<std::size_t>(pixel) * 4;
			const int64_t dst = static_cast<int64_t>(pixel) * 4;
			if (src + 3 < world->lightmaps.size()) {
				rgba.encode_u8(dst + 0, world->lightmaps[src + 0]);
				rgba.encode_u8(dst + 1, world->lightmaps[src + 1]);
				rgba.encode_u8(dst + 2, world->lightmaps[src + 2]);
				rgba.encode_u8(dst + 3, world->lightmaps[src + 3]);
			}
		}
		lightmaps[lightmap_index].lightmap_num = static_cast<int>(lightmap_index);
		lightmaps[lightmap_index].texture = create_texture_rgba8(p_rd, LIGHTMAP_SIZE, LIGHTMAP_SIZE, rgba);
		lightmaps[lightmap_index].lightmap_texture = lightmaps[lightmap_index].texture;
		if (!lightmaps[lightmap_index].texture.is_valid()) {
			return false;
		}
	}
	lightmap_count = static_cast<int>(lightmaps.size());
	return true;
}

void Q3BspDrawListEffect::load_shader_scripts()
{
	if (shader_scripts_loaded) {
		return;
	}
	shader_scripts_loaded = true;
	shader_script_names.clear();
	shader_script_images.clear();

	const PackedStringArray files = DirAccess::get_files_at("res://pak0/scripts");
	for (int file_index = 0; file_index < files.size(); ++file_index) {
		const String file_name = files[file_index];
		if (!file_name.ends_with(".shader")) {
			continue;
		}

		const String text = FileAccess::get_file_as_string(String("res://pak0/scripts/") + file_name);
		const PackedStringArray lines = text.split("\n");
		String current_shader;
		String first_image;
		int depth = 0;
		for (int line_index = 0; line_index < lines.size(); ++line_index) {
			String line = lines[line_index].strip_edges();
			const int comment = line.find("//");
			if (comment >= 0) {
				line = line.substr(0, comment).strip_edges();
			}
			if (line.is_empty()) {
				continue;
			}

			if (line == "{") {
				++depth;
				continue;
			}
			if (line == "}") {
				if (depth == 1 && !current_shader.is_empty() && !first_image.is_empty()) {
					shader_script_names.push_back(current_shader);
					shader_script_images.push_back(first_image);
				}
				if (depth > 0) {
					--depth;
				}
				if (depth == 0) {
					current_shader = String();
					first_image = String();
				}
				continue;
			}

			if (depth == 0) {
				current_shader = line;
				first_image = String();
				continue;
			}

			if (depth >= 1 && first_image.is_empty()) {
				const PackedStringArray tokens = line.split(" ", false);
				if (tokens.size() < 2) {
					continue;
				}
				const String keyword = tokens[0].to_lower();
				if (keyword == "map" || keyword == "clampmap") {
					const String image = tokens[1];
					if (!image.begins_with("$")) {
						first_image = image;
					}
				} else if (keyword == "animmap" && tokens.size() >= 3) {
					for (int token_index = 2; token_index < tokens.size(); ++token_index) {
						const String image = tokens[token_index];
						if (!image.begins_with("$")) {
							first_image = image;
							break;
						}
					}
				}
			}
		}
	}

	shader_script_mapping_count = shader_script_names.size();
}

String Q3BspDrawListEffect::resolve_shader_image(const String &p_shader_path) const
{
	for (int i = 0; i < shader_script_names.size(); ++i) {
		if (shader_script_names[i] == p_shader_path) {
			return shader_script_images[i];
		}
	}
	return p_shader_path;
}

RID Q3BspDrawListEffect::get_base_texture_for_shader(RenderingDevice *p_rd, int p_shader_num)
{
	for (const BaseTextureResource &resource : base_textures) {
		if (resource.shader_num == p_shader_num) {
			return resource.texture.is_valid() ? resource.texture : fallback_base_texture;
		}
	}

	BaseTextureResource resource;
	resource.shader_num = p_shader_num;
	resource.texture = fallback_base_texture;

	if (world != nullptr && p_shader_num >= 0 && p_shader_num < world->numShaders) {
		load_shader_scripts();
		const String shader_path = qpath_to_string(world->shaders[static_cast<std::size_t>(p_shader_num)].shader, q3::bsp::MAX_QPATH);
		const String image_path = resolve_shader_image(shader_path);
		PackedStringArray candidates;
		candidates.push_back(String("res://") + image_path + ".png");
		candidates.push_back(String("res://") + image_path + ".jpg");
		candidates.push_back(String("res://") + image_path + ".jpeg");
		candidates.push_back(String("res://") + image_path + ".webp");
		candidates.push_back(String("res://") + image_path + ".tga");
		candidates.push_back(String("res://pak0/") + image_path + ".png");
		candidates.push_back(String("res://pak0/") + image_path + ".jpg");
		candidates.push_back(String("res://pak0/") + image_path + ".jpeg");
		candidates.push_back(String("res://pak0/") + image_path + ".webp");
		candidates.push_back(String("res://pak0/") + image_path + ".tga");
		for (int i = 0; i < candidates.size(); ++i) {
			if (!FileAccess::file_exists(candidates[i])) {
				continue;
			}
			Ref<Image> image = load_image_from_file_bytes(candidates[i]);
			if (image.is_null() || image->is_empty()) {
				continue;
			}
			image->convert(Image::FORMAT_RGBA8);
			const RID texture = create_texture_rgba8(p_rd, image->get_width(), image->get_height(), image->get_data());
			if (texture.is_valid()) {
				resource.texture = texture;
				resource.loaded_from_asset = true;
				++base_texture_count;
				break;
			}
		}
	}

	base_textures.push_back(resource);
	return resource.texture.is_valid() ? resource.texture : fallback_base_texture;
}

bool Q3BspDrawListEffect::build_material_sets(RenderingDevice *p_rd)
{
	auto create_uniform_set = [&](const RID &p_base_texture, const RID &p_lightmap_texture) -> RID {
		Ref<RDUniform> base_uniform;
		base_uniform.instantiate();
		base_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
		base_uniform->set_binding(0);
		base_uniform->add_id(base_sampler);
		base_uniform->add_id(p_base_texture);

		Ref<RDUniform> lightmap_uniform;
		lightmap_uniform.instantiate();
		lightmap_uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
		lightmap_uniform->set_binding(1);
		lightmap_uniform->add_id(lightmap_sampler);
		lightmap_uniform->add_id(p_lightmap_texture);

		TypedArray<RDUniform> uniforms;
		uniforms.push_back(base_uniform);
		uniforms.push_back(lightmap_uniform);
		return p_rd->uniform_set_create(uniforms, shader, 0);
	};

	for (DrawSurface &surface : draw_surfaces) {
		RID lightmap_texture = fallback_lightmap_texture;
		if (surface.lightmap_num >= 0 && static_cast<std::size_t>(surface.lightmap_num) < lightmaps.size()) {
			lightmap_texture = lightmaps[static_cast<std::size_t>(surface.lightmap_num)].lightmap_texture;
		}

		MaterialResource *resource = nullptr;
		for (MaterialResource &candidate : materials) {
			if (candidate.shader_num == surface.shader_num && candidate.lightmap_num == surface.lightmap_num) {
				resource = &candidate;
				break;
			}
		}
		if (resource == nullptr) {
			MaterialResource material;
			material.shader_num = surface.shader_num;
			material.lightmap_num = surface.lightmap_num;
			material.base_texture = get_base_texture_for_shader(p_rd, surface.shader_num);
			material.lightmap_texture = lightmap_texture;
			material.uniform_set = create_uniform_set(material.base_texture, material.lightmap_texture);
			if (!material.uniform_set.is_valid()) {
				return false;
			}
			materials.push_back(material);
			material_count = static_cast<int>(materials.size());
			resource = &materials.back();
		}
		surface.uniform_set = resource->uniform_set;
	}

	return true;
}

bool Q3BspDrawListEffect::upload_bsp_geometry(RenderingDevice *p_rd)
{
	if (geometry_uploaded && vertex_array.is_valid() && index_array.is_valid()) {
		return true;
	}

	free_geometry_resources(p_rd);

	String file_path = resolve_bsp_file_path(bsp_path);
	resolved_bsp_path = file_path;
	load_error = String();

	q3::bsp::RE_LoadWorldMap(file_path.utf8().get_data());
	world = q3::bsp::R_GetWorld();
	if (world == nullptr) {
		bsp_loaded = false;
		load_error = String("Failed loading resource: ") + bsp_path + ".";
		return false;
	}
	bsp_loaded = true;
	num_clusters = world->numClusters;
	cluster_bytes = world->clusterBytes;

	std::vector<q3::bsp::drawVert_t> vertices;
	std::vector<std::uint32_t> indices;
	std::vector<DrawSurface> surface_ranges;
	for (std::int32_t bsp_surface_index = 0; bsp_surface_index < world->numsurfaces; ++bsp_surface_index) {
		const q3::bsp::msurface_t &surface = world->surfaces[static_cast<std::size_t>(bsp_surface_index)];
		const int shader_num = surface.shader ? surface.shader->shaderNum : -1;
		const int lightmap_num = surface.shader ? surface.shader->lightmapNum : q3::bsp::LIGHTMAP_NONE;
		if (shader_is_nodraw(world, shader_num) || !surface.data) {
			continue;
		}

		const std::size_t surface_first_vertex = vertices.size();
		const std::uint32_t surface_first_index = static_cast<std::uint32_t>(indices.size());

		if (auto face = std::dynamic_pointer_cast<q3::bsp::srfSurfaceFace_t>(surface.data)) {
			const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
			for (std::int32_t i = 0; i < face->numPoints; ++i) {
				vertices.push_back(draw_vert_from_face_point(*face, static_cast<std::size_t>(i)));
			}
			for (std::int32_t index : face->indices) {
				if (index >= 0 && index < face->numPoints) {
					indices.push_back(base + static_cast<std::uint32_t>(index));
				}
			}
		} else if (auto grid = std::dynamic_pointer_cast<q3::bsp::srfGridMesh_t>(surface.data)) {
			const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
			vertices.insert(vertices.end(), grid->verts.begin(), grid->verts.end());
			for (std::int32_t y = 0; y < grid->height - 1; ++y) {
				for (std::int32_t x = 0; x < grid->width - 1; ++x) {
					const std::uint32_t i0 = base + static_cast<std::uint32_t>(y * grid->width + x);
					const std::uint32_t i1 = i0 + 1;
					const std::uint32_t i2 = base + static_cast<std::uint32_t>((y + 1) * grid->width + x);
					const std::uint32_t i3 = i2 + 1;
					indices.push_back(i0);
					indices.push_back(i2);
					indices.push_back(i1);
					indices.push_back(i1);
					indices.push_back(i2);
					indices.push_back(i3);
				}
			}
		} else if (auto tri = std::dynamic_pointer_cast<q3::bsp::srfTriangles_t>(surface.data)) {
			const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
			vertices.insert(vertices.end(), tri->verts.begin(), tri->verts.end());
			for (std::int32_t index : tri->indexes) {
				if (index >= 0 && index < tri->numVerts) {
					indices.push_back(base + static_cast<std::uint32_t>(index));
				}
			}
		} else {
			continue;
		}

		DrawSurface draw_surface;
		draw_surface.source_surface = bsp_surface_index;
		draw_surface.shader_num = shader_num;
		draw_surface.lightmap_num = lightmap_num;
		draw_surface.first_index = surface_first_index;
		draw_surface.index_count = static_cast<std::uint32_t>(indices.size()) - surface_first_index;
		draw_surface.bounds = make_bounds(vertices, surface_first_vertex, vertices.size() - surface_first_vertex);
		if (draw_surface.index_count > 0) {
			surface_ranges.push_back(draw_surface);
		}
	}

	if (vertices.empty() || indices.empty()) {
		load_error = String("BSP loaded but produced no renderable surfaces: ") + bsp_path;
		return false;
	}

	PackedByteArray vertex_data;
	vertex_data.resize(static_cast<int64_t>(vertices.size() * VERTEX_STRIDE));
	for (std::size_t i = 0; i < vertices.size(); ++i) {
		write_vertex(vertex_data, static_cast<int>(i), vertices[i]);
	}

	PackedByteArray index_data;
	index_data.resize(static_cast<int64_t>(indices.size() * 4));
	for (std::size_t i = 0; i < indices.size(); ++i) {
		index_data.encode_u32(static_cast<int64_t>(i * 4), indices[i]);
	}

	vertex_buffer = p_rd->vertex_buffer_create(static_cast<uint32_t>(vertex_data.size()), vertex_data);
	index_buffer = p_rd->index_buffer_create(static_cast<uint32_t>(indices.size()), RenderingDevice::INDEX_BUFFER_FORMAT_UINT32, index_data);
	if (!vertex_buffer.is_valid() || !index_buffer.is_valid()) {
		free_geometry_resources(p_rd);
		return false;
	}

	TypedArray<RID> vertex_buffers;
	vertex_buffers.push_back(vertex_buffer);
	vertex_buffers.push_back(vertex_buffer);
	vertex_buffers.push_back(vertex_buffer);
	vertex_buffers.push_back(vertex_buffer);
	vertex_array = p_rd->vertex_array_create(static_cast<uint32_t>(vertices.size()), vertex_format, vertex_buffers);
	index_array = p_rd->index_array_create(index_buffer, 0, static_cast<uint32_t>(indices.size()));
	if (!vertex_array.is_valid() || !index_array.is_valid()) {
		free_geometry_resources(p_rd);
		return false;
	}

	for (DrawSurface &surface : surface_ranges) {
		surface.index_array = p_rd->index_array_create(index_buffer, surface.first_index, surface.index_count);
		if (!surface.index_array.is_valid()) {
			free_geometry_resources(p_rd);
			return false;
		}
	}

	if (!upload_lightmaps(p_rd)) {
		free_geometry_resources(p_rd);
		return false;
	}
	std::stable_sort(surface_ranges.begin(), surface_ranges.end(), [](const DrawSurface &p_a, const DrawSurface &p_b) {
		if (p_a.shader_num != p_b.shader_num) {
			return p_a.shader_num < p_b.shader_num;
		}
		if (p_a.lightmap_num != p_b.lightmap_num) {
			return p_a.lightmap_num < p_b.lightmap_num;
		}
		return p_a.source_surface < p_b.source_surface;
	});

	vertex_count = static_cast<int>(vertices.size());
	index_count = static_cast<int>(indices.size());
	draw_surfaces = std::move(surface_ranges);
	if (!build_material_sets(p_rd)) {
		free_geometry_resources(p_rd);
		return false;
	}
	geometry_uploaded = true;
	load_error = String();
	return true;
}

bool Q3BspDrawListEffect::ensure_debug_pipeline(RenderingDevice *p_rd, int64_t p_framebuffer_format)
{
	const bool wireframe_mode = debug_draw_mode == 1;
	if (pipeline.is_valid() && framebuffer_format == p_framebuffer_format && pipeline_wireframe == wireframe_mode) {
		pipeline_valid = true;
		return true;
	}

	free_rd_resources();
	framebuffer_format = p_framebuffer_format;

	Ref<RDShaderSource> source;
	source.instantiate();
	source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	source->set_stage_source(RenderingDevice::SHADER_STAGE_VERTEX,
			"#version 450\n"
			"layout(push_constant, std430) uniform DrawData {\n"
			"	mat4 view_projection;\n"
			"	vec4 debug_params;\n"
			"} draw_data;\n"
			"layout(location = 0) in vec3 position;\n"
			"layout(location = 1) in vec4 in_color;\n"
			"layout(location = 2) in vec2 in_base_uv;\n"
			"layout(location = 3) in vec2 in_lightmap_uv;\n"
			"layout(location = 0) out vec3 color;\n"
			"layout(location = 1) out vec2 base_uv;\n"
			"layout(location = 2) out vec2 lightmap_uv;\n"
			"void main() {\n"
			"	gl_Position = draw_data.view_projection * vec4(position, 1.0);\n"
			"	color = in_color.rgb;\n"
			"	base_uv = in_base_uv;\n"
			"	lightmap_uv = in_lightmap_uv;\n"
			"}\n");
	source->set_stage_source(RenderingDevice::SHADER_STAGE_FRAGMENT,
			"#version 450\n"
			"layout(push_constant, std430) uniform DrawData {\n"
			"	mat4 view_projection;\n"
			"	vec4 debug_params;\n"
			"} draw_data;\n"
			"layout(set = 0, binding = 0) uniform sampler2D base_texture;\n"
			"layout(set = 0, binding = 1) uniform sampler2D lightmap_texture;\n"
			"layout(location = 0) in vec3 color;\n"
			"layout(location = 1) in vec2 base_uv;\n"
			"layout(location = 2) in vec2 lightmap_uv;\n"
			"layout(location = 0) out vec4 frag_color;\n"
			"void main() {\n"
			"	vec3 base = texture(base_texture, base_uv).rgb;\n"
			"	vec3 lightmap = texture(lightmap_texture, lightmap_uv).rgb;\n"
			"	int debug_mode = int(draw_data.debug_params.x + 0.5);\n"
			"	if (debug_mode == 2) {\n"
			"		frag_color = vec4(base, 1.0);\n"
			"	} else if (debug_mode == 3) {\n"
			"		frag_color = vec4(lightmap, 1.0);\n"
			"	} else if (debug_mode == 4) {\n"
			"		frag_color = vec4(color, 1.0);\n"
			"	} else if (debug_mode == 5) {\n"
			"		frag_color = vec4(max(base * color, vec3(0.025)), 1.0);\n"
			"	} else {\n"
			"		frag_color = vec4(max(base * color * lightmap, vec3(0.025)), 1.0);\n"
			"	}\n"
			"}\n");

	Ref<RDShaderSPIRV> spirv = p_rd->shader_compile_spirv_from_source(source, true);
	if (spirv.is_null() ||
			!spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_VERTEX).is_empty() ||
			!spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_FRAGMENT).is_empty()) {
		pipeline_valid = false;
		return false;
	}

	shader = p_rd->shader_create_from_spirv(spirv, "Q3 BSP draw-list world");
	if (!shader.is_valid()) {
		pipeline_valid = false;
		return false;
	}

	TypedArray<RDVertexAttribute> attributes;
	Ref<RDVertexAttribute> position_attribute;
	position_attribute.instantiate();
	position_attribute->set_location(0);
	position_attribute->set_offset(0);
	position_attribute->set_format(RenderingDevice::DATA_FORMAT_R32G32B32_SFLOAT);
	position_attribute->set_stride(VERTEX_STRIDE);
	position_attribute->set_frequency(RenderingDevice::VERTEX_FREQUENCY_VERTEX);
	attributes.push_back(position_attribute);

	Ref<RDVertexAttribute> color_attribute;
	color_attribute.instantiate();
	color_attribute->set_location(1);
	color_attribute->set_offset(12);
	color_attribute->set_format(RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT);
	color_attribute->set_stride(VERTEX_STRIDE);
	color_attribute->set_frequency(RenderingDevice::VERTEX_FREQUENCY_VERTEX);
	attributes.push_back(color_attribute);

	Ref<RDVertexAttribute> base_uv_attribute;
	base_uv_attribute.instantiate();
	base_uv_attribute->set_location(2);
	base_uv_attribute->set_offset(28);
	base_uv_attribute->set_format(RenderingDevice::DATA_FORMAT_R32G32_SFLOAT);
	base_uv_attribute->set_stride(VERTEX_STRIDE);
	base_uv_attribute->set_frequency(RenderingDevice::VERTEX_FREQUENCY_VERTEX);
	attributes.push_back(base_uv_attribute);

	Ref<RDVertexAttribute> lightmap_uv_attribute;
	lightmap_uv_attribute.instantiate();
	lightmap_uv_attribute->set_location(3);
	lightmap_uv_attribute->set_offset(36);
	lightmap_uv_attribute->set_format(RenderingDevice::DATA_FORMAT_R32G32_SFLOAT);
	lightmap_uv_attribute->set_stride(VERTEX_STRIDE);
	lightmap_uv_attribute->set_frequency(RenderingDevice::VERTEX_FREQUENCY_VERTEX);
	attributes.push_back(lightmap_uv_attribute);

	vertex_format = p_rd->vertex_format_create(attributes);
	if (vertex_format < 0) {
		pipeline_valid = false;
		return false;
	}

	Ref<RDPipelineRasterizationState> raster;
	raster.instantiate();
	raster->set_cull_mode(RenderingDevice::POLYGON_CULL_DISABLED);
	raster->set_wireframe(wireframe_mode);
	raster->set_line_width(1.0f);

	Ref<RDPipelineMultisampleState> multisample;
	multisample.instantiate();

	Ref<RDPipelineDepthStencilState> depth_stencil;
	depth_stencil.instantiate();
	depth_stencil->set_enable_depth_test(true);
	depth_stencil->set_enable_depth_write(true);
	depth_stencil->set_depth_compare_operator(RenderingDevice::COMPARE_OP_GREATER_OR_EQUAL);

	Ref<RDPipelineColorBlendStateAttachment> color_attachment;
	color_attachment.instantiate();
	color_attachment->set_enable_blend(false);
	color_attachment->set_write_r(true);
	color_attachment->set_write_g(true);
	color_attachment->set_write_b(true);
	color_attachment->set_write_a(true);

	TypedArray<RDPipelineColorBlendStateAttachment> attachments;
	attachments.push_back(color_attachment);
	Ref<RDPipelineColorBlendState> blend;
	blend.instantiate();
	blend->set_attachments(attachments);

	pipeline = p_rd->render_pipeline_create(
			shader,
			framebuffer_format,
			vertex_format,
			RenderingDevice::RENDER_PRIMITIVE_TRIANGLES,
			raster,
			multisample,
			depth_stencil,
			blend);
	pipeline_valid = pipeline.is_valid() && p_rd->render_pipeline_is_valid(pipeline);
	pipeline_wireframe = wireframe_mode;
	return pipeline_valid;
}

void Q3BspDrawListEffect::_render_callback(int32_t p_effect_callback_type, RenderData *p_render_data)
{
	last_draw_list_opened = false;
	if (p_effect_callback_type != CompositorEffect::EFFECT_CALLBACK_TYPE_POST_OPAQUE || p_render_data == nullptr) {
		return;
	}

	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	if (rd == nullptr) {
		return;
	}

	Ref<RenderSceneBuffers> buffers = p_render_data->get_render_scene_buffers();
	Ref<RenderSceneBuffersRD> rd_buffers = buffers;
	if (rd_buffers.is_null()) {
		return;
	}

	const RID color_texture = rd_buffers->get_color_texture(false);
	const RID depth_texture = rd_buffers->get_depth_texture(false);
	if (!color_texture.is_valid() || !depth_texture.is_valid()) {
		return;
	}

	TypedArray<RID> framebuffer_textures;
	framebuffer_textures.push_back(color_texture);
	framebuffer_textures.push_back(depth_texture);
	const RID framebuffer = rd->framebuffer_create(framebuffer_textures);
	if (!framebuffer.is_valid()) {
		return;
	}

	const int64_t format = rd->framebuffer_get_format(framebuffer);
	RenderSceneData *scene_data = p_render_data->get_render_scene_data();
	const bool can_draw = scene_data != nullptr && ensure_debug_pipeline(rd, format) && upload_bsp_geometry(rd);
	const int64_t draw_list = rd->draw_list_begin(framebuffer);
	if (draw_list < 0) {
		rd->free_rid(framebuffer);
		return;
	}

	if (can_draw) {
		const Projection view_projection = scene_data->get_cam_projection() * Projection(scene_data->get_cam_transform().affine_inverse());
		const PackedByteArray push_constant = pack_draw_push_constants(view_projection, debug_draw_mode);
		const ViewFrustum frustum = get_view_frustum(scene_data);
		const int leaf_num = bsp_loaded ? point_leafnum(world, godot_to_q3_position(scene_data->get_cam_transform().origin)) : -1;
		view_cluster = (world != nullptr && leaf_num >= 0) ? world->nodes[static_cast<std::size_t>(world->numDecisionNodes + leaf_num)].cluster : -1;
		int marked_surface_count = 0;
		const std::vector<std::uint8_t> pvs_surfaces = mark_pvs_surfaces(world, view_cluster, marked_surface_count);
		pvs_surface_count = enable_pvs_culling ? marked_surface_count : static_cast<int>(draw_surfaces.size());
		visible_surface_count = 0;
		pvs_rejected_surface_count = 0;
		frustum_rejected_surface_count = 0;
		lightmap_bind_count = 0;
		material_bind_count = 0;
		int bound_shader_num = std::numeric_limits<int>::min();
		int bound_lightmap_num = std::numeric_limits<int>::min();
		rd->draw_list_bind_render_pipeline(draw_list, pipeline);
		rd->draw_list_bind_vertex_array(draw_list, vertex_array);
		rd->draw_list_set_push_constant(draw_list, push_constant, static_cast<uint32_t>(push_constant.size()));
		for (const DrawSurface &surface : draw_surfaces) {
			if (enable_pvs_culling &&
					(surface.source_surface < 0 ||
							static_cast<std::size_t>(surface.source_surface) >= pvs_surfaces.size() ||
							!pvs_surfaces[static_cast<std::size_t>(surface.source_surface)])) {
				++pvs_rejected_surface_count;
				continue;
			}
			if (enable_frustum_culling && !aabb_intersects_frustum(surface.bounds, frustum, cull_bounds_margin)) {
				++frustum_rejected_surface_count;
				continue;
			}
			if (!surface.uniform_set.is_valid()) {
				continue;
			}
			if (surface.shader_num != bound_shader_num || surface.lightmap_num != bound_lightmap_num) {
				rd->draw_list_bind_uniform_set(draw_list, surface.uniform_set, 0);
				if (surface.lightmap_num != bound_lightmap_num) {
					++lightmap_bind_count;
				}
				bound_shader_num = surface.shader_num;
				bound_lightmap_num = surface.lightmap_num;
				++material_bind_count;
			}
			rd->draw_list_bind_index_array(draw_list, surface.index_array);
			rd->draw_list_draw(draw_list, true, 1);
			++visible_surface_count;
			++draw_call_count;
		}
	}

	rd->draw_list_end();
	rd->free_rid(framebuffer);
	last_draw_list_opened = true;
	++rendered_frame_count;
}

int Q3BspDrawListEffect::get_rendered_frame_count() const
{
	return rendered_frame_count;
}

void Q3BspDrawListEffect::set_bsp_path(const String &p_path)
{
	bsp_path = p_path;
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs != nullptr && rs->get_rendering_device() != nullptr) {
		free_geometry_resources(rs->get_rendering_device());
	}
}

String Q3BspDrawListEffect::get_bsp_path() const
{
	return bsp_path;
}

String Q3BspDrawListEffect::get_resolved_bsp_path() const
{
	return resolved_bsp_path;
}

String Q3BspDrawListEffect::get_load_error() const
{
	return load_error;
}

bool Q3BspDrawListEffect::load_bsp(const String &p_path)
{
	if (!p_path.is_empty()) {
		set_bsp_path(p_path);
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr || rs->get_rendering_device() == nullptr) {
		return false;
	}
	return upload_bsp_geometry(rs->get_rendering_device());
}

void Q3BspDrawListEffect::set_enable_pvs_culling(bool p_enabled)
{
	enable_pvs_culling = p_enabled;
}

bool Q3BspDrawListEffect::get_enable_pvs_culling() const
{
	return enable_pvs_culling;
}

void Q3BspDrawListEffect::set_enable_frustum_culling(bool p_enabled)
{
	enable_frustum_culling = p_enabled;
}

bool Q3BspDrawListEffect::get_enable_frustum_culling() const
{
	return enable_frustum_culling;
}

void Q3BspDrawListEffect::set_debug_wireframe(bool p_enabled)
{
	set_debug_draw_mode(p_enabled ? 1 : 0);
}

bool Q3BspDrawListEffect::get_debug_wireframe() const
{
	return debug_draw_mode == 1;
}

void Q3BspDrawListEffect::set_debug_draw_mode(int p_mode)
{
	const int previous_mode = debug_draw_mode;
	const int clamped_mode = std::clamp(p_mode, 0, 5);
	if (debug_draw_mode == clamped_mode) {
		return;
	}
	debug_draw_mode = clamped_mode;
	debug_wireframe = debug_draw_mode == 1;
	if ((previous_mode == 1) != (debug_draw_mode == 1)) {
		RenderingServer *rs = RenderingServer::get_singleton();
		if (rs != nullptr && rs->get_rendering_device() != nullptr && pipeline.is_valid()) {
			rs->get_rendering_device()->free_rid(pipeline);
			pipeline = RID();
			pipeline_valid = false;
		}
	}
}

int Q3BspDrawListEffect::get_debug_draw_mode() const
{
	return debug_draw_mode;
}

void Q3BspDrawListEffect::set_cull_bounds_margin(float p_margin)
{
	cull_bounds_margin = std::max(p_margin, 0.0f);
}

float Q3BspDrawListEffect::get_cull_bounds_margin() const
{
	return cull_bounds_margin;
}

int Q3BspDrawListEffect::get_draw_call_count() const
{
	return draw_call_count;
}

int Q3BspDrawListEffect::get_vertex_count() const
{
	return vertex_count;
}

int Q3BspDrawListEffect::get_index_count() const
{
	return index_count;
}

int Q3BspDrawListEffect::get_visible_surface_count() const
{
	return visible_surface_count;
}

int Q3BspDrawListEffect::get_pvs_rejected_surface_count() const
{
	return pvs_rejected_surface_count;
}

int Q3BspDrawListEffect::get_frustum_rejected_surface_count() const
{
	return frustum_rejected_surface_count;
}

int Q3BspDrawListEffect::get_pvs_surface_count() const
{
	return pvs_surface_count;
}

int Q3BspDrawListEffect::get_view_cluster() const
{
	return view_cluster;
}

int Q3BspDrawListEffect::get_lightmap_count() const
{
	return lightmap_count;
}

int Q3BspDrawListEffect::get_lightmap_bind_count() const
{
	return lightmap_bind_count;
}

int Q3BspDrawListEffect::get_base_texture_count() const
{
	return base_texture_count;
}

int Q3BspDrawListEffect::get_material_count() const
{
	return material_count;
}

int Q3BspDrawListEffect::get_material_bind_count() const
{
	return material_bind_count;
}

int Q3BspDrawListEffect::get_shader_script_mapping_count() const
{
	return shader_script_mapping_count;
}

bool Q3BspDrawListEffect::get_last_draw_list_opened() const
{
	return last_draw_list_opened;
}

bool Q3BspDrawListEffect::get_pipeline_valid() const
{
	return pipeline_valid;
}

bool Q3BspDrawListEffect::get_geometry_uploaded() const
{
	return geometry_uploaded;
}
