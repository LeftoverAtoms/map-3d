#pragma once

#include "bsp_format.h"

#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include <vector>

namespace godot
{
	class Q3BspDrawListEffect : public CompositorEffect
	{
		GDCLASS(Q3BspDrawListEffect, CompositorEffect)

		int rendered_frame_count = 0;
		int draw_call_count = 0;
		int vertex_count = 0;
		int index_count = 0;
		bool last_draw_list_opened = false;
		bool pipeline_valid = false;
		bool geometry_uploaded = false;
		bool enable_pvs_culling = true;
		bool enable_frustum_culling = true;
		bool debug_wireframe = false;
		int debug_draw_mode = 0;
		int visible_surface_count = 0;
		int pvs_rejected_surface_count = 0;
		int frustum_rejected_surface_count = 0;
		int pvs_surface_count = 0;
		int view_cluster = -1;
		int lightmap_count = 0;
		int lightmap_bind_count = 0;
		int base_texture_count = 0;
		int material_count = 0;
		int material_bind_count = 0;
		float cull_bounds_margin = 64.0f;
		int shader_script_mapping_count = 0;
		String bsp_path = "res://maps/mptourney1.bsp";
		String resolved_bsp_path;
		String load_error;

		struct DrawSurface
		{
			int source_surface = -1;
			int shader_num = -1;
			int lightmap_num = -1;
			uint32_t first_index = 0;
			uint32_t index_count = 0;
			AABB bounds;
			RID index_array;
			RID uniform_set;
		};

		struct MaterialResource
		{
			int shader_num = -1;
			int lightmap_num = -1;
			RID base_texture;
			RID lightmap_texture;
			RID texture;
			RID uniform_set;
		};

		struct BaseTextureResource
		{
			int shader_num = -1;
			RID texture;
			bool loaded_from_asset = false;
		};

		RID shader;
		RID pipeline;
		RID vertex_buffer;
		RID vertex_array;
		RID index_buffer;
		RID index_array;
		RID lightmap_sampler;
		RID base_sampler;
		RID fallback_base_texture;
		RID fallback_lightmap_texture;
		RID fallback_lightmap_uniform_set;
		int64_t framebuffer_format = -1;
		int64_t vertex_format = -1;
		bool pipeline_wireframe = false;
		q3::bsp::world_t *world = nullptr;
		bool bsp_loaded = false;
		bool shader_scripts_loaded = false;
		int num_clusters = 0;
		int cluster_bytes = 0;
		std::vector<DrawSurface> draw_surfaces;
		std::vector<MaterialResource> lightmaps;
		std::vector<MaterialResource> materials;
		std::vector<BaseTextureResource> base_textures;
		std::vector<String> shader_script_names;
		std::vector<String> shader_script_images;

		void free_rd_resources();
		void free_geometry_resources(RenderingDevice *p_rd);
		bool upload_bsp_geometry(RenderingDevice *p_rd);
		bool upload_lightmaps(RenderingDevice *p_rd);
		void load_shader_scripts();
		String resolve_shader_image(const String &p_shader_path) const;
		RID get_base_texture_for_shader(RenderingDevice *p_rd, int p_shader_num);
		bool build_material_sets(RenderingDevice *p_rd);
		bool ensure_debug_pipeline(RenderingDevice *p_rd, int64_t p_framebuffer_format);

	protected:
		static void _bind_methods();

	public:
		Q3BspDrawListEffect();
		~Q3BspDrawListEffect();

		void _render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) override;

		void set_bsp_path(const String &p_path);
		String get_bsp_path() const;
		String get_resolved_bsp_path() const;
		String get_load_error() const;
		bool load_bsp(const String &p_path = String());
		void set_enable_pvs_culling(bool p_enabled);
		bool get_enable_pvs_culling() const;
		void set_enable_frustum_culling(bool p_enabled);
		bool get_enable_frustum_culling() const;
		void set_debug_wireframe(bool p_enabled);
		bool get_debug_wireframe() const;
		void set_debug_draw_mode(int p_mode);
		int get_debug_draw_mode() const;
		void set_cull_bounds_margin(float p_margin);
		float get_cull_bounds_margin() const;
		int get_rendered_frame_count() const;
		int get_draw_call_count() const;
		int get_vertex_count() const;
		int get_index_count() const;
		int get_visible_surface_count() const;
		int get_pvs_rejected_surface_count() const;
		int get_frustum_rejected_surface_count() const;
		int get_pvs_surface_count() const;
		int get_view_cluster() const;
		int get_lightmap_count() const;
		int get_lightmap_bind_count() const;
		int get_base_texture_count() const;
		int get_material_count() const;
		int get_material_bind_count() const;
		int get_shader_script_mapping_count() const;
		bool get_last_draw_list_opened() const;
		bool get_pipeline_valid() const;
		bool get_geometry_uploaded() const;
	};
}
