extends SceneTree

func _init() -> void:
	var resource := ResourceLoader.load("res://maps/mptourney1.bsp")
	if resource == null:
		push_error("ResourceLoader could not load the BSP.")
		quit(1)
		return

	print("loaded_class=%s" % resource.get_class())
	print("valid=%s" % resource.get("BSP/valid"))
	print("counts=%s" % resource.get("Header/counts"))
	print("lumps=%d" % (resource.get("Header/lump_info") as Array).size())
	print("surfaces=%d" % (resource.get("Lumps/surfaces") as Array).size())
	quit()
