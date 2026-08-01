extends SceneTree

func _fail(message: String) -> void:
	push_error(message)
	quit(1)

func _wait_frames(count: int) -> void:
	for i in count:
		await process_frame

func _get_draw_effect(root_node: Node) -> Object:
	var world_environment := root_node.get_node_or_null("Q3WorldEnvironment") as WorldEnvironment
	if world_environment == null or world_environment.compositor == null:
		return null

	for effect in world_environment.compositor.compositor_effects:
		if effect != null and effect.get_class() == "Q3BspDrawListEffect":
			return effect
	return null

func _init() -> void:
	var scene := load("res://renderer_demo.tscn") as PackedScene
	if scene == null:
		_fail("Could not load renderer_demo.tscn.")
		return

	var root_node := scene.instantiate()
	root_node.set("mirror_viewport_debug_draw", false)
	root_node.set("show_renderer_stats", false)
	root.add_child(root_node)
	await _wait_frames(8)

	var draw_effect := _get_draw_effect(root_node)
	if draw_effect == null:
		_fail("Renderer demo did not attach Q3BspDrawListEffect.")
		return

	draw_effect.set("enable_pvs_culling", false)
	draw_effect.set("enable_frustum_culling", false)

	var files := DirAccess.get_files_at("res://maps")
	files.sort()
	var tested := 0
	for file_name in files:
		if not file_name.ends_with(".bsp"):
			continue

		var bsp_path := "res://maps/%s" % file_name
		root_node.set("bsp_path", bsp_path)
		await _wait_frames(12)

		var geometry_uploaded := bool(draw_effect.get("geometry_uploaded"))
		var vertex_count := int(draw_effect.get("vertex_count"))
		var index_count := int(draw_effect.get("index_count"))
		var visible_surface_count := int(draw_effect.get("visible_surface_count"))
		var material_count := int(draw_effect.get("material_count"))
		var load_error := str(draw_effect.get("load_error"))

		print("map=%s geometry=%s vertices=%d indices=%d visible=%d materials=%d error=%s" % [
			file_name,
			geometry_uploaded,
			vertex_count,
			index_count,
			visible_surface_count,
			material_count,
			load_error,
		])

		if not geometry_uploaded:
			_fail("Renderer failed to upload geometry for %s: %s" % [bsp_path, load_error])
			return
		if not load_error.is_empty():
			_fail("Renderer reported a load error for %s: %s" % [bsp_path, load_error])
			return
		if vertex_count <= 0 or index_count <= 0:
			_fail("Renderer produced empty geometry for %s." % bsp_path)
			return
		if visible_surface_count <= 0:
			_fail("Renderer submitted no visible surfaces for %s with culling disabled." % bsp_path)
			return
		if material_count <= 0:
			_fail("Renderer created no material sets for %s." % bsp_path)
			return

		tested += 1

	if tested == 0:
		_fail("No BSP files found in res://maps.")
		return

	print("all_maps_renderer_tested=%d" % tested)
	quit()
