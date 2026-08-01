# q3-world

Godot 4.7 GDExtension renderer for Quake 3 BSP worlds.

## Build

Use SCons:

```powershell
scons target=template_debug
```

The debug build installs the extension DLL into `project/bin/windows/`.

## Renderer Demo

Open `project/renderer_demo.tscn` in Godot.

The scene uses:

- `Q3WorldEnvironment` with a `Compositor`
- `Q3BspDrawListEffect` for RenderingDevice draw-list rendering
- A plain `Camera3D` named `DrawListCamera`

The BSP is rendered by `RenderingDevice`; the demo must not use `MeshInstance3D` for BSP rendering.

## Selecting Maps

Select the `RendererDemo` node and set `bsp_path`.

The path supports:

- Godot project paths, such as `res://maps/mptourney1.bsp`
- project-relative paths, such as `maps/mptourney1.bsp` or `project/maps/mptourney1.bsp`
- absolute/global paths, such as `D:/DEV/q3-world/project/maps/mpq3ctf1.bsp`
- quoted pasted paths, such as `"D:/DEV/q3-world/project/maps/mpq3ctf1.bsp"`

The overlay shows the active BSP file and renderer stats. If a map fails to load, `Q3BspDrawListEffect.load_error` reports the native BSP loader error.

## Debug Rendering

`Q3BspDrawListEffect.debug_draw_mode` supports:

- `0` Shaded
- `1` Wireframe
- `2` Base Texture
- `3` Lightmap
- `4` Vertex Color
- `5` Unshaded

In the editor, `renderer_demo.gd` mirrors the Perspective viewport debug mode into the BSP renderer where possible. In particular, `Perspective > Display Debug > Wireframe` maps to `debug_draw_mode = 1`, which rebuilds the RD raster pipeline with wireframe enabled.

## Verification

Run the strict renderer verifier:

```powershell
D:\DEV\Godot_4.7\Godot_v4.7-stable_win64_console.exe --path project --script res://verify_renderer_demo.gd
```

Run the all-maps smoke verifier:

```powershell
D:\DEV\Godot_4.7\Godot_v4.7-stable_win64_console.exe --path project --script res://verify_all_maps_renderer.gd
```

`verify_renderer_demo.gd` checks renderer wiring, no `MeshInstance3D` nodes, RD draw-list execution, texture/lightmap/material setup, culling behavior, BSP path switching, missing-path error reporting, debug modes, and viewport wireframe mirroring.

`verify_all_maps_renderer.gd` loads every `.bsp` in `project/maps` through the RD renderer with culling disabled and fails if any map cannot upload geometry, submit visible surfaces, or create material sets.
