@tool
extends Node

const MAP_DIR := "res://maps"
const OUT_DIR := "res://array_meshes"

func _ready() -> void:
	if not ClassDB.class_exists("Q3BspLoader"):
		push_error("Q3BspLoader is not registered. Check that the GDExtension DLL loaded.")
		return

	var dir_err := DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(OUT_DIR))
	if dir_err != OK:
		push_error("Failed to create %s: %s" % [OUT_DIR, error_string(dir_err)])
		return

	var maps := PackedStringArray()
	for path in DirAccess.get_files_at(MAP_DIR):
		if path.get_extension().to_lower() == "bsp":
			maps.append(path)
	maps.sort()

	var loader: Object = ClassDB.instantiate("Q3BspLoader")
	var saved := 0
	var failed := 0

	for map_file in maps:
		var bsp_path := "%s/%s" % [MAP_DIR, map_file]
		var out_path := "%s/%s_array_mesh.tres" % [OUT_DIR, map_file.get_basename()]
		var mesh := loader.call("load_mesh", bsp_path) as ArrayMesh
		if mesh == null:
			push_error("Failed to create ArrayMesh from %s" % bsp_path)
			failed += 1
			continue

		var err := ResourceSaver.save(mesh, out_path)
		if err != OK:
			push_error("Failed to save ArrayMesh to %s: %s" % [out_path, error_string(err)])
			failed += 1
			continue

		saved += 1
		print("Saved %s with %d surface(s)." % [out_path, mesh.get_surface_count()])

	print("BSP ArrayMesh dump complete: saved=%d failed=%d" % [saved, failed])
