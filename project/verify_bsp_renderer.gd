extends SceneTree

func _init() -> void:
	if not ClassDB.class_exists("Q3BspRenderer"):
		push_error("Q3BspRenderer is not registered.")
		quit(1)
		return

	var renderer := ClassDB.instantiate("Q3BspRenderer") as Node3D
	if renderer == null:
		push_error("Could not instantiate Q3BspRenderer.")
		quit(1)
		return

	root.add_child(renderer)
	var ok := renderer.call("load_bsp", "res://maps/mptourney1.bsp") as bool
	if not ok:
		push_error("Q3BspRenderer failed to load the BSP.")
		quit(1)
		return

	if renderer.get_child_count() != 0:
		push_error("Q3BspRenderer should not create render nodes; RenderingDevice owns rendering.")
		quit(1)
		return

	print("renderer_class=%s" % renderer.get_class())
	print("valid=%s" % renderer.call("is_valid"))
	print("surface_count=%d" % renderer.call("get_surface_count"))
	print("shader_count=%d" % renderer.call("get_shader_count"))
	print("lightmap_count=%d" % renderer.call("get_lightmap_count"))
	quit()
