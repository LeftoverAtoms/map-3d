@tool
extends Node

@export_global_file("*.bsp")
var bsp_path: String:
	set(value):
		if bsp_path == value:
			return

		bsp_path = value

		if is_inside_tree():
			_load_map()

@export var draw_list_effect: Q3BspDrawListEffect


func _ready() -> void:
	_load_map()


func _load_map() -> void:
	if draw_list_effect == null:
		push_error("Q3BspDrawListEffect is not assigned.")
		return

	if bsp_path.is_empty():
		return

	draw_list_effect.load_bsp(bsp_path)