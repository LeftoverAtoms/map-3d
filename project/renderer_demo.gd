@tool
extends Map3D

@export_tool_button("Load Map") var my_button: Callable = _load_map

@export var mesh_instance: MeshInstance3D


func _ready() -> void:
	_load_map()


func _load_map() -> void:
	if map_path.is_empty():
		return

	load_map()

	mesh_instance.mesh = get_visual_mesh()
