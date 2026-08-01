extends SceneTree

func _require(condition: bool, message: String) -> bool:
	if condition:
		return true
	push_error(message)
	quit(1)
	return false

func _contains_mesh_instance(node: Node) -> bool:
	if node is MeshInstance3D:
		return true
	for child in node.get_children():
		if _contains_mesh_instance(child):
			return true
	return false

func _init() -> void:
	var scene := load("res://renderer_demo.tscn") as PackedScene
	if scene == null:
		push_error("Could not load renderer_demo.tscn.")
		quit(1)
		return

	var root_node := scene.instantiate()
	root_node.set("mirror_viewport_debug_draw", false)
	root.add_child(root_node)
	for i in 8:
		await process_frame
	if not _require(not _contains_mesh_instance(root_node), "Renderer demo should not contain MeshInstance3D nodes."):
		return

	var camera := root_node.get_node_or_null("DrawListCamera") as Camera3D
	if camera == null:
		push_error("Renderer demo did not create a draw-list camera.")
		quit(1)
		return

	if camera.compositor != null:
		push_error("DrawListCamera should not own the BSP compositor; the world environment should.")
		quit(1)
		return

	var world_environment := root_node.get_node_or_null("Q3WorldEnvironment") as WorldEnvironment
	if world_environment == null or world_environment.compositor == null:
		push_error("Renderer demo did not create a world environment with a compositor.")
		quit(1)
		return

	var found_effect := false
	var draw_effect: Object = null
	for effect in world_environment.compositor.compositor_effects:
		if effect != null and effect.get_class() == "Q3BspDrawListEffect":
			found_effect = true
			draw_effect = effect
			break
	if not found_effect:
		push_error("Renderer demo did not attach Q3BspDrawListEffect.")
		quit(1)
		return

	print("camera=%s" % camera.name)
	print("world_environment=%s" % world_environment.name)
	print("draw_list_effect_attached=true")
	print("pipeline_valid=%s" % draw_effect.get("pipeline_valid"))
	print("geometry_uploaded=%s" % draw_effect.get("geometry_uploaded"))
	print("resolved_bsp_path=%s" % draw_effect.get("resolved_bsp_path"))
	print("load_error=%s" % draw_effect.get("load_error"))
	print("vertex_count=%d" % draw_effect.get("vertex_count"))
	print("index_count=%d" % draw_effect.get("index_count"))
	print("base_texture_count=%d" % draw_effect.get("base_texture_count"))
	print("shader_script_mapping_count=%d" % draw_effect.get("shader_script_mapping_count"))
	print("lightmap_count=%d" % draw_effect.get("lightmap_count"))
	print("lightmap_bind_count=%d" % draw_effect.get("lightmap_bind_count"))
	print("material_count=%d" % draw_effect.get("material_count"))
	print("material_bind_count=%d" % draw_effect.get("material_bind_count"))
	print("enable_pvs_culling=%s" % draw_effect.get("enable_pvs_culling"))
	print("enable_frustum_culling=%s" % draw_effect.get("enable_frustum_culling"))
	print("debug_draw_mode=%d" % draw_effect.get("debug_draw_mode"))
	print("debug_wireframe=%s" % draw_effect.get("debug_wireframe"))
	print("cull_bounds_margin=%.1f" % draw_effect.get("cull_bounds_margin"))
	print("view_cluster=%d" % draw_effect.get("view_cluster"))
	print("pvs_surface_count=%d" % draw_effect.get("pvs_surface_count"))
	print("pvs_rejected_surface_count=%d" % draw_effect.get("pvs_rejected_surface_count"))
	print("frustum_rejected_surface_count=%d" % draw_effect.get("frustum_rejected_surface_count"))
	print("visible_surface_count=%d" % draw_effect.get("visible_surface_count"))
	print("draw_call_count=%d" % draw_effect.get("draw_call_count"))
	print("last_draw_list_opened=%s" % draw_effect.get("last_draw_list_opened"))
	if not _require(bool(draw_effect.get("pipeline_valid")), "Draw-list pipeline was not valid after startup."):
		return
	if not _require(bool(draw_effect.get("geometry_uploaded")), "BSP geometry was not uploaded after startup."):
		return
	if not _require(bool(draw_effect.get("last_draw_list_opened")), "RenderingDevice draw list did not open after startup."):
		return
	if not _require(int(draw_effect.get("draw_call_count")) > 0, "Renderer submitted no draw calls after startup."):
		return
	if not _require(int(draw_effect.get("vertex_count")) > 0, "Startup BSP produced no vertices."):
		return
	if not _require(int(draw_effect.get("base_texture_count")) > 0, "Renderer did not create any base textures."):
		return
	if not _require(int(draw_effect.get("shader_script_mapping_count")) > 0, "Renderer did not parse any Quake shader script mappings."):
		return
	if not _require(int(draw_effect.get("lightmap_count")) > 0, "Renderer did not upload any lightmaps."):
		return
	if not _require(int(draw_effect.get("material_count")) > 0, "Renderer did not create any material uniform sets."):
		return
	if not _require(int(draw_effect.get("material_bind_count")) > 0, "Renderer did not bind any material uniform sets."):
		return
	if not _require(str(draw_effect.get("load_error")).is_empty(), "Startup BSP reported a load error: %s" % draw_effect.get("load_error")):
		return

	var default_vertex_count := int(draw_effect.get("vertex_count"))
	root_node.set("bsp_path", "res://maps/mpq3ctf4.bsp")
	for i in 12:
		await process_frame
	var switched_vertex_count := int(draw_effect.get("vertex_count"))
	print("switched_bsp_path=%s" % draw_effect.get("bsp_path"))
	print("switched_geometry_uploaded=%s" % draw_effect.get("geometry_uploaded"))
	print("switched_load_error=%s" % draw_effect.get("load_error"))
	print("switched_vertex_count=%d" % switched_vertex_count)
	print("exported_bsp_path_reload_changed_geometry=%s" % (switched_vertex_count > 0 and switched_vertex_count != default_vertex_count))
	if not _require(bool(draw_effect.get("geometry_uploaded")), "Project-relative BSP switch did not upload geometry."):
		return
	if not _require(str(draw_effect.get("load_error")).is_empty(), "Project-relative BSP switch failed: %s" % draw_effect.get("load_error")):
		return
	if not _require(switched_vertex_count > 0 and switched_vertex_count != default_vertex_count, "Project-relative BSP switch did not change geometry."):
		return

	var absolute_bsp_path := ProjectSettings.globalize_path("res://maps/mpq3ctf1.bsp")
	root_node.set("bsp_path", absolute_bsp_path)
	for i in 12:
		await process_frame
	var absolute_vertex_count := int(draw_effect.get("vertex_count"))
	print("absolute_bsp_path=%s" % draw_effect.get("bsp_path"))
	print("absolute_resolved_bsp_path=%s" % draw_effect.get("resolved_bsp_path"))
	print("absolute_geometry_uploaded=%s" % draw_effect.get("geometry_uploaded"))
	print("absolute_load_error=%s" % draw_effect.get("load_error"))
	print("absolute_vertex_count=%d" % absolute_vertex_count)
	print("absolute_bsp_path_reload_changed_geometry=%s" % (absolute_vertex_count > 0 and absolute_vertex_count != switched_vertex_count))
	if not _require(bool(draw_effect.get("geometry_uploaded")), "Absolute BSP switch did not upload geometry."):
		return
	if not _require(str(draw_effect.get("load_error")).is_empty(), "Absolute BSP switch failed: %s" % draw_effect.get("load_error")):
		return
	if not _require(absolute_vertex_count > 0 and absolute_vertex_count != switched_vertex_count, "Absolute BSP switch did not change geometry."):
		return

	root_node.set("bsp_path", "maps/mpq3ctf4.bsp")
	for i in 12:
		await process_frame
	print("relative_maps_bsp_path=%s" % draw_effect.get("bsp_path"))
	print("relative_maps_resolved_bsp_path=%s" % draw_effect.get("resolved_bsp_path"))
	print("relative_maps_load_error=%s" % draw_effect.get("load_error"))
	if not _require(bool(draw_effect.get("geometry_uploaded")), "maps/ BSP path did not upload geometry."):
		return
	if not _require(str(draw_effect.get("load_error")).is_empty(), "maps/ BSP path failed: %s" % draw_effect.get("load_error")):
		return
	if not _require(int(draw_effect.get("vertex_count")) == switched_vertex_count, "maps/ BSP path did not load the expected map."):
		return

	root_node.set("bsp_path", "project/maps/mpq3ctf1.bsp")
	for i in 12:
		await process_frame
	print("project_relative_bsp_path=%s" % draw_effect.get("bsp_path"))
	print("project_relative_resolved_bsp_path=%s" % draw_effect.get("resolved_bsp_path"))
	print("project_relative_load_error=%s" % draw_effect.get("load_error"))
	if not _require(bool(draw_effect.get("geometry_uploaded")), "project/maps/ BSP path did not upload geometry."):
		return
	if not _require(str(draw_effect.get("load_error")).is_empty(), "project/maps/ BSP path failed: %s" % draw_effect.get("load_error")):
		return
	if not _require(int(draw_effect.get("vertex_count")) == absolute_vertex_count, "project/maps/ BSP path did not load the expected map."):
		return

	root_node.set("bsp_path", "\"%s\"" % absolute_bsp_path)
	for i in 12:
		await process_frame
	print("quoted_absolute_bsp_path=%s" % draw_effect.get("bsp_path"))
	print("quoted_absolute_resolved_bsp_path=%s" % draw_effect.get("resolved_bsp_path"))
	print("quoted_absolute_load_error=%s" % draw_effect.get("load_error"))
	if not _require(bool(draw_effect.get("geometry_uploaded")), "Quoted absolute BSP path did not upload geometry."):
		return
	if not _require(str(draw_effect.get("load_error")).is_empty(), "Quoted absolute BSP path failed: %s" % draw_effect.get("load_error")):
		return
	if not _require(int(draw_effect.get("vertex_count")) == absolute_vertex_count, "Quoted absolute BSP path did not load the expected map."):
		return

	root_node.set("bsp_path", "res://maps/does_not_exist.bsp")
	for i in 8:
		await process_frame
	print("missing_bsp_path=%s" % draw_effect.get("bsp_path"))
	print("missing_bsp_geometry_uploaded=%s" % draw_effect.get("geometry_uploaded"))
	print("missing_bsp_load_error=%s" % draw_effect.get("load_error"))
	if not _require(not bool(draw_effect.get("geometry_uploaded")), "Missing BSP path should not leave geometry_uploaded=true."):
		return
	if not _require(not str(draw_effect.get("load_error")).is_empty(), "Missing BSP path did not report a load error."):
		return

	root_node.set("bsp_path", "res://maps/mptourney1.bsp")
	for i in 12:
		await process_frame
	print("restored_bsp_path=%s" % draw_effect.get("bsp_path"))
	print("restored_load_error=%s" % draw_effect.get("load_error"))
	print("restored_vertex_count=%d" % draw_effect.get("vertex_count"))
	if not _require(bool(draw_effect.get("geometry_uploaded")), "Restored BSP did not upload geometry."):
		return
	if not _require(str(draw_effect.get("load_error")).is_empty(), "Restored BSP still has load error: %s" % draw_effect.get("load_error")):
		return
	if not _require(int(draw_effect.get("vertex_count")) == default_vertex_count, "Restored BSP vertex count did not match default."):
		return

	var initial_visible := int(draw_effect.get("visible_surface_count"))
	draw_effect.set("enable_pvs_culling", false)
	draw_effect.set("enable_frustum_culling", false)
	for i in 4:
		await process_frame
	var uncull_visible := int(draw_effect.get("visible_surface_count"))
	print("unculled_visible_surface_count=%d" % uncull_visible)
	print("culling_toggles_changed_visible_count=%s" % (uncull_visible > initial_visible))
	if not _require(uncull_visible > initial_visible, "Disabling PVS/frustum culling did not increase visible surface count."):
		return

	draw_effect.set("enable_pvs_culling", true)
	draw_effect.set("enable_frustum_culling", true)
	for i in 4:
		await process_frame
	initial_visible = int(draw_effect.get("visible_surface_count"))
	print("recull_visible_surface_count=%d" % initial_visible)

	camera.rotate_y(PI)
	for i in 8:
		await process_frame
	var rotated_visible := int(draw_effect.get("visible_surface_count"))
	print("rotated_visible_surface_count=%d" % rotated_visible)
	print("look_culling_changed=%s" % (initial_visible != rotated_visible))
	if not _require(initial_visible != rotated_visible, "Looking the opposite direction did not change visible surface count."):
		return

	draw_effect.set("debug_draw_mode", 1)
	for i in 8:
		await process_frame
	print("wireframe_pipeline_valid=%s" % draw_effect.get("pipeline_valid"))
	print("wireframe_debug_draw_mode=%d" % draw_effect.get("debug_draw_mode"))
	print("wireframe_debug_wireframe=%s" % draw_effect.get("debug_wireframe"))
	if not _require(bool(draw_effect.get("pipeline_valid")) and int(draw_effect.get("debug_draw_mode")) == 1 and bool(draw_effect.get("debug_wireframe")), "Wireframe debug mode did not activate."):
		return

	draw_effect.set("debug_draw_mode", 2)
	for i in 4:
		await process_frame
	print("base_texture_debug_draw_mode=%d" % draw_effect.get("debug_draw_mode"))
	print("base_texture_pipeline_valid=%s" % draw_effect.get("pipeline_valid"))
	if not _require(bool(draw_effect.get("pipeline_valid")) and int(draw_effect.get("debug_draw_mode")) == 2, "Base texture debug mode did not activate."):
		return

	draw_effect.set("debug_draw_mode", 3)
	for i in 4:
		await process_frame
	print("lightmap_debug_draw_mode=%d" % draw_effect.get("debug_draw_mode"))
	print("lightmap_pipeline_valid=%s" % draw_effect.get("pipeline_valid"))
	if not _require(bool(draw_effect.get("pipeline_valid")) and int(draw_effect.get("debug_draw_mode")) == 3, "Lightmap debug mode did not activate."):
		return

	draw_effect.set("debug_draw_mode", 4)
	for i in 4:
		await process_frame
	print("vertex_color_debug_draw_mode=%d" % draw_effect.get("debug_draw_mode"))
	print("vertex_color_pipeline_valid=%s" % draw_effect.get("pipeline_valid"))
	if not _require(bool(draw_effect.get("pipeline_valid")) and int(draw_effect.get("debug_draw_mode")) == 4, "Vertex color debug mode did not activate."):
		return

	draw_effect.set("debug_draw_mode", 5)
	for i in 4:
		await process_frame
	print("unshaded_debug_draw_mode=%d" % draw_effect.get("debug_draw_mode"))
	print("unshaded_pipeline_valid=%s" % draw_effect.get("pipeline_valid"))
	if not _require(bool(draw_effect.get("pipeline_valid")) and int(draw_effect.get("debug_draw_mode")) == 5, "Unshaded debug mode did not activate."):
		return

	root_node.set("mirror_viewport_debug_draw", true)
	root.debug_draw = Viewport.DEBUG_DRAW_WIREFRAME
	for i in 4:
		await process_frame
	print("mirrored_wireframe_debug_draw_mode=%d" % draw_effect.get("debug_draw_mode"))
	print("mirrored_wireframe_debug_wireframe=%s" % draw_effect.get("debug_wireframe"))
	if not _require(int(draw_effect.get("debug_draw_mode")) == 1 and bool(draw_effect.get("debug_wireframe")), "Viewport wireframe debug did not mirror into the BSP renderer."):
		return

	root.debug_draw = Viewport.DEBUG_DRAW_DISABLED
	for i in 4:
		await process_frame
	print("mirrored_disabled_debug_draw_mode=%d" % draw_effect.get("debug_draw_mode"))
	if not _require(int(draw_effect.get("debug_draw_mode")) == 0, "Viewport debug disable did not mirror into the BSP renderer."):
		return
	quit()
