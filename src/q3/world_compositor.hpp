#pragma once

#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/framebuffer_cache_rd.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/rid.hpp>

#include <map_3d.hpp>
#include <q3/renderer.hpp>

#include <cstdint>

///////////////////////////

namespace rngo::q3
{
	class WorldCompositor : public godot::CompositorEffect
	{
		GDCLASS(WorldCompositor, CompositorEffect)

		struct GPUWorld
		{
			const rngo::Map3D *map = nullptr;
			bool uploaded = false;
			int vertex_count = 0;
			int index_count = 0;
			uint64_t world_revision = 0;
			godot::RID vertex_buffer;
			godot::RID vertex_array;
			godot::RID index_buffer;
			godot::RID index_array;
		};

		godot::Viewport::DebugDraw debug_draw = godot::Viewport::DEBUG_DRAW_DISABLED;
		int vertex_count = 0;
		int index_count = 0;
		int64_t framebuffer_format = -1;
		int64_t vertex_format = -1;
		godot::RID shader;
		godot::RID pipeline;
		godot::RID sampler;
		godot::Ref<godot::Texture2D> texture_resource;
		godot::RID texture;
		godot::RID texture_uniform_set;
		GPUWorld gpu_world;

	protected:
		// Glue.
		static void _bind_methods();

	public:
		// Properties.
		void set_debug_draw(godot::Viewport::DebugDraw p_debug_draw);
		godot::Viewport::DebugDraw get_debug_draw() const;
		int get_vertex_count() const;
		int get_index_count() const;

		// Helpers.
		godot::RID get_framebuffer(godot::RenderData *p_render_data);
		void free_render_resources(godot::RenderingDevice *p_rd);
		void free_gpu_world(godot::RenderingDevice *p_rd, GPUWorld &r_gpu_world);
		bool ensure_pipeline(godot::RenderingDevice *p_rd, int64_t p_framebuffer_format);
		bool ensure_texture_resources(godot::RenderingDevice *p_rd);
		bool upload_world(godot::RenderingDevice *p_rd, GPUWorld &r_gpu_world, const q3::World &p_world);

		// Entry point.
		void _render_callback(int32_t p_effect_callback_type, godot::RenderData *p_render_data) override;

		WorldCompositor();
		~WorldCompositor() override;
	};
}
