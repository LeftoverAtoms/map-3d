#include "world_compositor.hpp"

#include <godot_cpp/classes/framebuffer_cache_rd.hpp>
#include <godot_cpp/classes/rd_framebuffer_pass.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state.hpp>
#include <godot_cpp/classes/rd_pipeline_color_blend_state_attachment.hpp>
#include <godot_cpp/classes/rd_pipeline_depth_stencil_state.hpp>
#include <godot_cpp/classes/rd_pipeline_multisample_state.hpp>
#include <godot_cpp/classes/rd_pipeline_rasterization_state.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rd_vertex_attribute.hpp>
#include <godot_cpp/classes/render_scene_data.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

///////////////////////////

namespace rngo::q3
{
	namespace
	{
		inline constexpr int VERTEX_STRIDE = 20;

		PackedByteArray PackPushConstant(const Projection &p_model_view_projection)
		{
			PackedByteArray data;
			data.resize(16 * 4);
			for (int column = 0; column < 4; ++column) { for (int row = 0; row < 4; ++row) { data.encode_float((column * 4 + row) * 4, p_model_view_projection[column][row]); } }
			return data;
		}
	}

	void WorldCompositor::_bind_methods()
	{
		ClassDB::bind_method(D_METHOD("set_debug_draw", "debug_draw"), &WorldCompositor::set_debug_draw);
		ClassDB::bind_method(D_METHOD("get_debug_draw"), &WorldCompositor::get_debug_draw);
		ClassDB::bind_method(D_METHOD("get_vertex_count"), &WorldCompositor::get_vertex_count);
		ClassDB::bind_method(D_METHOD("get_index_count"), &WorldCompositor::get_index_count);
		ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_draw", PROPERTY_HINT_ENUM, "Disabled,Unshaded,Lighting,Overdraw,Wireframe"), "set_debug_draw", "get_debug_draw");
		ADD_PROPERTY(PropertyInfo(Variant::INT, "vertex_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_vertex_count");
		ADD_PROPERTY(PropertyInfo(Variant::INT, "index_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_index_count");
	}

	void WorldCompositor::set_debug_draw(Viewport::DebugDraw p_debug_draw) { debug_draw = p_debug_draw; }
	Viewport::DebugDraw WorldCompositor::get_debug_draw() const { return debug_draw; }
	int WorldCompositor::get_vertex_count() const { return vertex_count; }
	int WorldCompositor::get_index_count() const { return index_count; }

	RID WorldCompositor::get_framebuffer(RenderData *p_render_data)
	{
		TypedArray<RID> textures;
		TypedArray<RDFramebufferPass> passes;
		ERR_FAIL_NULL_V(p_render_data, RID());
		const auto buffers = Ref<RenderSceneBuffersRD>(p_render_data->get_render_scene_buffers());
		ERR_FAIL_COND_V_MSG(buffers.is_null(), RID(), "Cannot get cached framebuffer without RenderSceneBuffersRD");
		textures.push_back(buffers->get_color_texture());
		textures.push_back(buffers->get_depth_texture());
		const auto framebuffer = FramebufferCacheRD::get_cache_multipass(textures, passes, buffers->get_view_count());
		ERR_FAIL_COND_V_MSG(!framebuffer.is_valid(), RID(), "Failed to get cached framebuffer");
		return framebuffer;
	}

	void WorldCompositor::free_render_resources(RenderingDevice *p_rd)
	{
		free_gpu_world(p_rd, gpu_world);
		if (texture_uniform_set.is_valid()) { p_rd->free_rid(texture_uniform_set); texture_uniform_set = RID(); }
		if (sampler.is_valid()) { p_rd->free_rid(sampler); sampler = RID(); }
		if (pipeline.is_valid()) { p_rd->free_rid(pipeline); pipeline = RID(); }
		if (shader.is_valid()) { p_rd->free_rid(shader); shader = RID(); }
		texture = RID();
		texture_resource.unref();
		vertex_format = -1;
		framebuffer_format = -1;
		vertex_count = 0;
		index_count = 0;
	}

	void WorldCompositor::free_gpu_world(RenderingDevice *p_rd, GPUWorld &r_gpu_world)
	{
		if (r_gpu_world.vertex_array.is_valid()) { p_rd->free_rid(r_gpu_world.vertex_array); } if (r_gpu_world.index_array.is_valid()) { p_rd->free_rid(r_gpu_world.index_array); } if (r_gpu_world.vertex_buffer.is_valid()) { p_rd->free_rid(r_gpu_world.vertex_buffer); } if (r_gpu_world.index_buffer.is_valid()) { p_rd->free_rid(r_gpu_world.index_buffer); }
		r_gpu_world = GPUWorld {};
	}

	bool WorldCompositor::ensure_pipeline(RenderingDevice *p_rd, int64_t p_framebuffer_format)
	{
		if (pipeline.is_valid() && framebuffer_format == p_framebuffer_format) { return true; }
		if (pipeline.is_valid()) { p_rd->free_rid(pipeline); pipeline = RID(); }
		free_gpu_world(p_rd, gpu_world);
		if (shader.is_valid()) { p_rd->free_rid(shader); shader = RID(); }
		framebuffer_format = p_framebuffer_format;
		Ref<Resource> shader_resource = ResourceLoader::get_singleton()->load("res://shaders/q3_world.glsl", "RDShaderFile", ResourceLoader::CACHE_MODE_REPLACE);
		Ref<RDShaderFile> shader_file = shader_resource;
		ERR_FAIL_COND_V_MSG(shader_file.is_null(), false, "WorldCompositor: failed to load res://shaders/q3_world.glsl.");
		Ref<RDShaderSPIRV> spirv = shader_file->get_spirv();
		ERR_FAIL_COND_V_MSG(spirv.is_null(), false, "WorldCompositor: shader file has no SPIR-V.");
		ERR_FAIL_COND_V_MSG(!spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_VERTEX).is_empty(), false, spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_VERTEX));
		ERR_FAIL_COND_V_MSG(!spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_FRAGMENT).is_empty(), false, spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_FRAGMENT));
		shader = p_rd->shader_create_from_spirv(spirv, "Q3 bare BSP");
		ERR_FAIL_COND_V(!shader.is_valid(), false);
		TypedArray<RDVertexAttribute> attributes;
		Ref<RDVertexAttribute> position_attribute;
		position_attribute.instantiate();
		position_attribute->set_location(0);
		position_attribute->set_offset(0);
		position_attribute->set_format(RenderingDevice::DATA_FORMAT_R32G32B32_SFLOAT);
		position_attribute->set_stride(VERTEX_STRIDE);
		position_attribute->set_frequency(RenderingDevice::VERTEX_FREQUENCY_VERTEX);
		attributes.push_back(position_attribute);
		Ref<RDVertexAttribute> uv_attribute;
		uv_attribute.instantiate();
		uv_attribute->set_location(1);
		uv_attribute->set_offset(12);
		uv_attribute->set_format(RenderingDevice::DATA_FORMAT_R32G32_SFLOAT);
		uv_attribute->set_stride(VERTEX_STRIDE);
		uv_attribute->set_frequency(RenderingDevice::VERTEX_FREQUENCY_VERTEX);
		attributes.push_back(uv_attribute);
		vertex_format = p_rd->vertex_format_create(attributes);
		ERR_FAIL_COND_V(vertex_format < 0, false);
		Ref<RDPipelineRasterizationState> raster;
		raster.instantiate();
		raster->set_cull_mode(RenderingDevice::POLYGON_CULL_DISABLED);
		Ref<RDPipelineMultisampleState> multisample;
		multisample.instantiate();
		Ref<RDPipelineDepthStencilState> depth;
		depth.instantiate();
		depth->set_enable_depth_test(true);
		depth->set_enable_depth_write(true);
		depth->set_depth_compare_operator(RenderingDevice::COMPARE_OP_GREATER_OR_EQUAL);
		Ref<RDPipelineColorBlendStateAttachment> color_attachment;
		color_attachment.instantiate();
		color_attachment->set_write_r(true);
		color_attachment->set_write_g(true);
		color_attachment->set_write_b(true);
		color_attachment->set_write_a(true);
		TypedArray<RDPipelineColorBlendStateAttachment> attachments;
		attachments.push_back(color_attachment);
		Ref<RDPipelineColorBlendState> blend;
		blend.instantiate();
		blend->set_attachments(attachments);
		pipeline = p_rd->render_pipeline_create(shader, framebuffer_format, vertex_format, RenderingDevice::RENDER_PRIMITIVE_TRIANGLES, raster, multisample, depth, blend);
		return pipeline.is_valid() && p_rd->render_pipeline_is_valid(pipeline);
	}

	bool WorldCompositor::ensure_texture_resources(RenderingDevice *p_rd)
	{
		if (texture_uniform_set.is_valid()) { return true; }
		if (texture_resource.is_null()) { Ref<Resource> resource = ResourceLoader::get_singleton()->load("res://uv_texture.png", "Texture2D", ResourceLoader::CACHE_MODE_REUSE); texture_resource = resource; }
		ERR_FAIL_COND_V_MSG(texture_resource.is_null(), false, "WorldCompositor: failed to load res://uv_texture.png.");
		if (!texture.is_valid()) { texture = RenderingServer::get_singleton()->texture_get_rd_texture(texture_resource->get_rid()); }
		ERR_FAIL_COND_V_MSG(!texture.is_valid(), false, "WorldCompositor: failed to get RD texture for res://uv_texture.png.");
		if (!sampler.is_valid()) { Ref<RDSamplerState> sampler_state; sampler_state.instantiate(); sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR); sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR); sampler_state->set_repeat_u(RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT); sampler_state->set_repeat_v(RenderingDevice::SAMPLER_REPEAT_MODE_REPEAT); sampler = p_rd->sampler_create(sampler_state); }
		ERR_FAIL_COND_V_MSG(!sampler.is_valid(), false, "WorldCompositor: failed to create sampler.");
		Ref<RDUniform> uniform;
		uniform.instantiate();
		uniform->set_uniform_type(RenderingDevice::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE);
		uniform->set_binding(0);
		uniform->add_id(sampler);
		uniform->add_id(texture);
		TypedArray<RDUniform> uniforms;
		uniforms.push_back(uniform);
		texture_uniform_set = p_rd->uniform_set_create(uniforms, shader, 0);
		return texture_uniform_set.is_valid();
	}

	bool WorldCompositor::upload_world(RenderingDevice *p_rd, GPUWorld &r_gpu_world, const q3::World &p_world)
	{
		if (r_gpu_world.uploaded && r_gpu_world.vertex_array.is_valid() && r_gpu_world.index_array.is_valid()) { return true; }
		ERR_FAIL_COND_V_MSG(p_world.vertices.empty() || p_world.indices.empty(), false, "WorldCompositor: map has no bare surface geometry.");
		PackedByteArray vertex_data;
		vertex_data.resize(static_cast<int64_t>(p_world.vertices.size() * VERTEX_STRIDE));
		for (std::size_t i = 0; i < p_world.vertices.size(); ++i) { const WorldVertex &vertex = p_world.vertices[i]; const int64_t offset = static_cast<int64_t>(i * VERTEX_STRIDE); vertex_data.encode_float(offset + 0, vertex.position.x); vertex_data.encode_float(offset + 4, vertex.position.y); vertex_data.encode_float(offset + 8, vertex.position.z); vertex_data.encode_float(offset + 12, vertex.texture_uv.x); vertex_data.encode_float(offset + 16, vertex.texture_uv.y); }
		PackedByteArray index_data;
		index_data.resize(static_cast<int64_t>(p_world.indices.size() * 4));
		for (std::size_t i = 0; i < p_world.indices.size(); ++i) { index_data.encode_u32(static_cast<int64_t>(i * 4), p_world.indices[i]); }
		r_gpu_world.vertex_buffer = p_rd->vertex_buffer_create(static_cast<uint32_t>(vertex_data.size()), vertex_data);
		r_gpu_world.index_buffer = p_rd->index_buffer_create(static_cast<uint32_t>(p_world.indices.size()), RenderingDevice::INDEX_BUFFER_FORMAT_UINT32, index_data);
		if (!r_gpu_world.vertex_buffer.is_valid() || !r_gpu_world.index_buffer.is_valid()) { free_gpu_world(p_rd, r_gpu_world); return false; }
		TypedArray<RID> vertex_buffers;
		vertex_buffers.push_back(r_gpu_world.vertex_buffer);
		vertex_buffers.push_back(r_gpu_world.vertex_buffer);
		r_gpu_world.vertex_array = p_rd->vertex_array_create(static_cast<uint32_t>(p_world.vertices.size()), vertex_format, vertex_buffers);
		r_gpu_world.index_array = p_rd->index_array_create(r_gpu_world.index_buffer, 0, static_cast<uint32_t>(p_world.indices.size()));
		if (!r_gpu_world.vertex_array.is_valid() || !r_gpu_world.index_array.is_valid()) { free_gpu_world(p_rd, r_gpu_world); return false; }
		r_gpu_world.uploaded = true;
		r_gpu_world.vertex_count = static_cast<int>(p_world.vertices.size());
		r_gpu_world.index_count = static_cast<int>(p_world.indices.size());
		return true;
	}

	void WorldCompositor::_render_callback(int32_t p_effect_callback_type, RenderData *p_render_data)
	{
		if (p_effect_callback_type != CompositorEffect::EFFECT_CALLBACK_TYPE_POST_OPAQUE || p_render_data == nullptr) { return; }
		RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
		ERR_FAIL_NULL(rd);
		const RID framebuffer = get_framebuffer(p_render_data);
		ERR_FAIL_COND(!framebuffer.is_valid());
		RenderSceneData *scene_data = p_render_data->get_render_scene_data();
		ERR_FAIL_NULL(scene_data);
		const int64_t format = rd->framebuffer_get_format(framebuffer);
		ERR_FAIL_COND(!ensure_pipeline(rd, format));
		ERR_FAIL_COND(!ensure_texture_resources(rd));
		const Projection view_projection = scene_data->get_cam_projection() * Projection(scene_data->get_cam_transform().affine_inverse());
		Map3D *map = Map3D::get_active_map();
		if (map == nullptr || !map->is_map_loaded()) { return; }
		const uint64_t world_revision = map->get_world_revision();
		if (gpu_world.map != map || gpu_world.world_revision != world_revision) { free_gpu_world(rd, gpu_world); gpu_world.map = map; gpu_world.world_revision = world_revision; }
		const q3::World &world = map->get_world();
		if (!upload_world(rd, gpu_world, world)) { return; }
		const int64_t draw_list = rd->draw_list_begin(framebuffer);
		ERR_FAIL_COND(draw_list < 0);
		vertex_count = 0;
		index_count = 0;
		rd->draw_list_bind_render_pipeline(draw_list, pipeline);
		const Projection model_view_projection = view_projection * Projection(map->get_global_transform()); const PackedByteArray push_constant = PackPushConstant(model_view_projection); rd->draw_list_bind_vertex_array(draw_list, gpu_world.vertex_array); rd->draw_list_bind_index_array(draw_list, gpu_world.index_array); rd->draw_list_bind_uniform_set(draw_list, texture_uniform_set, 0); rd->draw_list_set_push_constant(draw_list, push_constant, static_cast<uint32_t>(push_constant.size())); rd->draw_list_draw(draw_list, true, 1); vertex_count = gpu_world.vertex_count; index_count = gpu_world.index_count;
		rd->draw_list_end();
	}

	WorldCompositor::WorldCompositor()
	{
		set_effect_callback_type(CompositorEffect::EFFECT_CALLBACK_TYPE_POST_OPAQUE);
	}

	WorldCompositor::~WorldCompositor()
	{
		RenderingServer *rs = RenderingServer::get_singleton();
		if (rs == nullptr) { return; }
		RenderingDevice *rd = rs->get_rendering_device();
		if (rd == nullptr) { return; }
		free_render_resources(rd);
	}
}
