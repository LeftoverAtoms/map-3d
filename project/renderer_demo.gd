@tool
extends Node3D

const MAP_PATH := "res://maps/mptourney1.bsp"

@export_global_file("*.bsp") var bsp_path := MAP_PATH:
	set(value):
		bsp_path = value
		_apply_bsp_path()

@export var mirror_viewport_debug_draw := true
@export var show_renderer_stats := true

@export var ensure_draw_list_renderer := false:
	set(value):
		ensure_draw_list_renderer = false
		if is_inside_tree():
			_setup_draw_list_camera()

var _draw_list_effect: Resource
var _last_viewport_debug_draw := -1
var _stats_label: Label

func _ready() -> void:
	_setup_draw_list_camera()
	_setup_stats_overlay()
	set_process(mirror_viewport_debug_draw or show_renderer_stats)

func _process(_delta: float) -> void:
	if mirror_viewport_debug_draw:
		_sync_viewport_debug_draw()
	if show_renderer_stats:
		_update_stats_overlay()

func _setup_draw_list_camera() -> void:
	_get_or_create_camera()
	var environment := _get_or_create_world_environment()
	var effect := _get_or_create_draw_list_effect(environment)
	if effect == null:
		return

	_draw_list_effect = effect
	_apply_bsp_path()
	_sync_viewport_debug_draw()
	print("Q3 draw-list renderer installed on %s" % environment.name)

func _apply_bsp_path() -> void:
	if not is_inside_tree():
		return

	if _draw_list_effect == null:
		var environment := _get_or_create_world_environment()
		_draw_list_effect = _get_or_create_draw_list_effect(environment)
		if _draw_list_effect == null:
			return

	_draw_list_effect.set("bsp_path", bsp_path)
	if _draw_list_effect.has_method("load_bsp") and bool(_draw_list_effect.get("pipeline_valid")):
		var loaded := bool(_draw_list_effect.call("load_bsp", bsp_path))
		if not loaded and Engine.is_editor_hint():
			push_warning("Q3 BSP renderer could not load: %s" % bsp_path)

func _sync_viewport_debug_draw() -> void:
	if _draw_list_effect == null:
		return

	var viewport := get_viewport()
	if viewport == null:
		return

	var viewport_debug_draw := _get_active_viewport_debug_draw(viewport)
	if viewport_debug_draw == _last_viewport_debug_draw:
		return

	_last_viewport_debug_draw = viewport_debug_draw
	var effect_debug_mode := _effect_mode_from_viewport_debug_draw(viewport_debug_draw)
	if effect_debug_mode >= 0:
		_draw_list_effect.set("debug_draw_mode", effect_debug_mode)

func _get_active_viewport_debug_draw(fallback_viewport: Viewport) -> int:
	if Engine.is_editor_hint() and ClassDB.class_exists("EditorInterface"):
		var first_editor_viewport: SubViewport = null
		for i in 4:
			var editor_viewport := EditorInterface.get_editor_viewport_3d(i)
			if editor_viewport == null:
				continue
			if first_editor_viewport == null:
				first_editor_viewport = editor_viewport
			var editor_debug_draw := int(editor_viewport.debug_draw)
			if editor_debug_draw != Viewport.DEBUG_DRAW_DISABLED:
				return editor_debug_draw
		if first_editor_viewport != null:
			return int(first_editor_viewport.debug_draw)

	return int(fallback_viewport.debug_draw)

func _setup_stats_overlay() -> void:
	if not show_renderer_stats:
		return

	var existing := get_node_or_null("RendererStats") as CanvasLayer
	if existing != null:
		_stats_label = existing.get_node_or_null("StatsLabel") as Label
		return

	var layer := CanvasLayer.new()
	layer.name = "RendererStats"
	layer.layer = 64
	add_child(layer)

	var label := Label.new()
	label.name = "StatsLabel"
	label.position = Vector2(12.0, 12.0)
	label.add_theme_font_size_override("font_size", 14)
	label.add_theme_color_override("font_color", Color(0.88, 0.95, 1.0))
	label.add_theme_color_override("font_shadow_color", Color(0.0, 0.0, 0.0, 0.85))
	label.add_theme_constant_override("shadow_offset_x", 1)
	label.add_theme_constant_override("shadow_offset_y", 1)
	layer.add_child(label)
	_stats_label = label

func _update_stats_overlay() -> void:
	if _draw_list_effect == null or _stats_label == null:
		return

	var load_error := str(_draw_list_effect.get("load_error"))
	if not load_error.is_empty():
		_stats_label.text = "%s | %s" % [_draw_list_effect.get("bsp_path").get_file(), load_error]
		return

	_stats_label.text = "%s | cluster %d | visible %d | PVS reject %d | frustum reject %d | draws %d | mode %d" % [
		_draw_list_effect.get("bsp_path").get_file(),
		_draw_list_effect.get("view_cluster"),
		_draw_list_effect.get("visible_surface_count"),
		_draw_list_effect.get("pvs_rejected_surface_count"),
		_draw_list_effect.get("frustum_rejected_surface_count"),
		_draw_list_effect.get("draw_call_count"),
		_draw_list_effect.get("debug_draw_mode"),
	]

func _effect_mode_from_viewport_debug_draw(viewport_debug_draw: int) -> int:
	match viewport_debug_draw:
		Viewport.DEBUG_DRAW_DISABLED:
			return 0
		Viewport.DEBUG_DRAW_UNSHADED:
			return 5
		Viewport.DEBUG_DRAW_LIGHTING:
			return 3
		Viewport.DEBUG_DRAW_WIREFRAME:
			return 1
		_:
			return -1

func _get_or_create_camera() -> Camera3D:
	var existing := get_node_or_null("DrawListCamera") as Camera3D
	if existing != null:
		return existing

	var camera := Camera3D.new()
	camera.name = "DrawListCamera"
	camera.current = true
	camera.far = 20000.0
	camera.position = Vector3(0, 40, 80)
	camera.rotation_degrees = Vector3(-25, 0, 0)
	add_child(camera)
	if Engine.is_editor_hint():
		camera.owner = get_tree().edited_scene_root
	return camera

func _get_or_create_world_environment() -> WorldEnvironment:
	var existing := get_node_or_null("Q3WorldEnvironment") as WorldEnvironment
	if existing != null:
		return existing

	var environment := WorldEnvironment.new()
	environment.name = "Q3WorldEnvironment"
	add_child(environment)
	if Engine.is_editor_hint():
		environment.owner = get_tree().edited_scene_root
	return environment

func _get_or_create_draw_list_effect(environment: WorldEnvironment) -> Resource:
	if not ClassDB.class_exists("Q3BspDrawListEffect"):
		push_error("Q3BspDrawListEffect is not registered.")
		return null

	var compositor := environment.compositor
	if compositor == null:
		compositor = Compositor.new()
		environment.compositor = compositor

	var effects := compositor.compositor_effects
	for effect in effects:
		if effect != null and effect.get_class() == "Q3BspDrawListEffect":
			return effect

	var effect := ClassDB.instantiate("Q3BspDrawListEffect") as Resource
	if effect == null:
		push_error("Could not instantiate Q3BspDrawListEffect.")
		return null

	effect.set("bsp_path", bsp_path)
	effects.append(effect)
	compositor.compositor_effects = effects
	return effect
