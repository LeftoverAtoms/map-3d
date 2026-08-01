#include "q3_bsp_resource.h"

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

using namespace godot;

namespace
{
	const char *LumpName(q3::bsp::lumpIndex_t lumpNum)
	{
		switch (lumpNum) {
			case q3::bsp::LUMP_ENTITIES:
				return "Entities";
			case q3::bsp::LUMP_SHADERS:
				return "Shaders";
			case q3::bsp::LUMP_PLANES:
				return "Planes";
			case q3::bsp::LUMP_NODES:
				return "Nodes";
			case q3::bsp::LUMP_LEAFS:
				return "Leafs";
			case q3::bsp::LUMP_LEAFSURFACES:
				return "Leafsurfaces";
			case q3::bsp::LUMP_LEAFBRUSHES:
				return "Leafbrushes";
			case q3::bsp::LUMP_MODELS:
				return "Models";
			case q3::bsp::LUMP_BRUSHES:
				return "Brushes";
			case q3::bsp::LUMP_BRUSHSIDES:
				return "Brushsides";
			case q3::bsp::LUMP_DRAWVERTS:
				return "DrawVerts";
			case q3::bsp::LUMP_DRAWINDEXES:
				return "DrawIndexes";
			case q3::bsp::LUMP_FOGS:
				return "Fogs";
			case q3::bsp::LUMP_SURFACES:
				return "Surfaces";
			case q3::bsp::LUMP_LIGHTMAPS:
				return "Lightmaps";
			case q3::bsp::LUMP_LIGHTGRID:
				return "Lightgrid";
			case q3::bsp::LUMP_VISIBILITY:
				return "Visibility";
			case q3::bsp::HEADER_LUMPS:
				return "Count";
		}
		return "Unknown";
	}

	String qpath_to_string(const char *p_value, std::size_t p_length)
	{
		std::size_t length = 0;
		while (length < p_length && p_value[length] != '\0') {
			++length;
		}
		return String::utf8(p_value, length);
	}

	PackedFloat32Array vec3_to_array(const q3::bsp::vec3_t &p_value)
	{
		PackedFloat32Array array;
		array.resize(3);
		array[0] = p_value[0];
		array[1] = p_value[1];
		array[2] = p_value[2];
		return array;
	}

	PackedFloat32Array vec2_to_array(const float p_value[2])
	{
		PackedFloat32Array array;
		array.resize(2);
		array[0] = p_value[0];
		array[1] = p_value[1];
		return array;
	}

	PackedInt32Array int3_to_array(const std::int32_t p_value[3])
	{
		PackedInt32Array array;
		array.resize(3);
		array[0] = p_value[0];
		array[1] = p_value[1];
		array[2] = p_value[2];
		return array;
	}

	PackedByteArray bytes_to_array(const std::vector<std::uint8_t> &p_values)
	{
		PackedByteArray array;
		array.resize(static_cast<int64_t>(p_values.size()));
		for (std::size_t i = 0; i < p_values.size(); ++i) {
			array[static_cast<int64_t>(i)] = p_values[i];
		}
		return array;
	}

	PackedInt32Array ints_to_array(const std::vector<std::int32_t> &p_values)
	{
		PackedInt32Array array;
		array.resize(static_cast<int64_t>(p_values.size()));
		for (std::size_t i = 0; i < p_values.size(); ++i) {
			array[static_cast<int64_t>(i)] = p_values[i];
		}
		return array;
	}

	Dictionary shader_to_dict(const q3::bsp::dshader_t &p_shader)
	{
		Dictionary dict;
		dict["shader"] = qpath_to_string(p_shader.shader, q3::bsp::MAX_QPATH);
		dict["surfaceFlags"] = p_shader.surfaceFlags;
		dict["contentFlags"] = p_shader.contentFlags;
		return dict;
	}

	Dictionary plane_to_dict(const q3::bsp::dplane_t &p_plane)
	{
		Dictionary dict;
		dict["normal"] = vec3_to_array(p_plane.normal);
		dict["dist"] = p_plane.dist;
		return dict;
	}

	Dictionary node_to_dict(const q3::bsp::dnode_t &p_node)
	{
		Dictionary dict;
		PackedInt32Array children;
		children.resize(2);
		children[0] = p_node.children[0];
		children[1] = p_node.children[1];
		dict["planeNum"] = p_node.planeNum;
		dict["children"] = children;
		dict["mins"] = int3_to_array(p_node.mins);
		dict["maxs"] = int3_to_array(p_node.maxs);
		return dict;
	}

	Dictionary leaf_to_dict(const q3::bsp::dleaf_t &p_leaf)
	{
		Dictionary dict;
		dict["cluster"] = p_leaf.cluster;
		dict["area"] = p_leaf.area;
		dict["mins"] = int3_to_array(p_leaf.mins);
		dict["maxs"] = int3_to_array(p_leaf.maxs);
		dict["firstLeafSurface"] = p_leaf.firstLeafSurface;
		dict["numLeafSurfaces"] = p_leaf.numLeafSurfaces;
		dict["firstLeafBrush"] = p_leaf.firstLeafBrush;
		dict["numLeafBrushes"] = p_leaf.numLeafBrushes;
		return dict;
	}

	Dictionary model_to_dict(const q3::bsp::dmodel_t &p_model)
	{
		Dictionary dict;
		dict["mins"] = vec3_to_array(p_model.mins);
		dict["maxs"] = vec3_to_array(p_model.maxs);
		dict["firstSurface"] = p_model.firstSurface;
		dict["numSurfaces"] = p_model.numSurfaces;
		dict["firstBrush"] = p_model.firstBrush;
		dict["numBrushes"] = p_model.numBrushes;
		return dict;
	}

	Dictionary brush_to_dict(const q3::bsp::dbrush_t &p_brush)
	{
		Dictionary dict;
		dict["firstSide"] = p_brush.firstSide;
		dict["numSides"] = p_brush.numSides;
		dict["shaderNum"] = p_brush.shaderNum;
		return dict;
	}

	Dictionary brush_side_to_dict(const q3::bsp::dbrushside_t &p_side)
	{
		Dictionary dict;
		dict["planeNum"] = p_side.planeNum;
		dict["shaderNum"] = p_side.shaderNum;
		return dict;
	}

	Dictionary draw_vert_to_dict(const q3::bsp::drawVert_t &p_vert)
	{
		Dictionary dict;
		PackedByteArray color;
		color.resize(4);
		color[0] = p_vert.color[0];
		color[1] = p_vert.color[1];
		color[2] = p_vert.color[2];
		color[3] = p_vert.color[3];
		dict["xyz"] = vec3_to_array(p_vert.xyz);
		dict["st"] = vec2_to_array(p_vert.st);
		dict["lightmap"] = vec2_to_array(p_vert.lightmap);
		dict["normal"] = vec3_to_array(p_vert.normal);
		dict["color"] = color;
		return dict;
	}

	Dictionary fog_to_dict(const q3::bsp::dfog_t &p_fog)
	{
		Dictionary dict;
		dict["shader"] = qpath_to_string(p_fog.shader, q3::bsp::MAX_QPATH);
		dict["brushNum"] = p_fog.brushNum;
		dict["visibleSide"] = p_fog.visibleSide;
		return dict;
	}

	Dictionary surface_to_dict(const q3::bsp::dsurface_t &p_surface)
	{
		Dictionary dict;
		dict["shaderNum"] = p_surface.shaderNum;
		dict["fogNum"] = p_surface.fogNum;
		dict["surfaceType"] = p_surface.surfaceType;
		dict["firstVert"] = p_surface.firstVert;
		dict["numVerts"] = p_surface.numVerts;
		dict["firstIndex"] = p_surface.firstIndex;
		dict["numIndexes"] = p_surface.numIndexes;
		dict["lightmapNum"] = p_surface.lightmapNum;
		dict["lightmapX"] = p_surface.lightmapX;
		dict["lightmapY"] = p_surface.lightmapY;
		dict["lightmapWidth"] = p_surface.lightmapWidth;
		dict["lightmapHeight"] = p_surface.lightmapHeight;
		dict["lightmapOrigin"] = vec3_to_array(p_surface.lightmapOrigin);
		Array lightmap_vecs;
		lightmap_vecs.resize(3);
		lightmap_vecs[0] = vec3_to_array(p_surface.lightmapVecs[0]);
		lightmap_vecs[1] = vec3_to_array(p_surface.lightmapVecs[1]);
		lightmap_vecs[2] = vec3_to_array(p_surface.lightmapVecs[2]);
		dict["lightmapVecs"] = lightmap_vecs;
		dict["patchWidth"] = p_surface.patchWidth;
		dict["patchHeight"] = p_surface.patchHeight;
		return dict;
	}

	template <typename T, typename F>
	Array vector_to_array(const std::vector<T> &p_values, F p_convert)
	{
		Array array;
		array.resize(static_cast<int64_t>(p_values.size()));
		for (std::size_t i = 0; i < p_values.size(); ++i) {
			array[static_cast<int64_t>(i)] = p_convert(p_values[i]);
		}
		return array;
	}

	void add_property(List<PropertyInfo> *p_list, Variant::Type p_type, const String &p_name)
	{
		p_list->push_back(PropertyInfo(p_type, p_name, PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY));
	}
}

void Q3BspResource::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("load_from_path", "path"), &Q3BspResource::load_from_path);
	ClassDB::bind_method(D_METHOD("get_path"), &Q3BspResource::get_path);
	ClassDB::bind_method(D_METHOD("is_valid"), &Q3BspResource::is_valid);
	ClassDB::bind_method(D_METHOD("get_error"), &Q3BspResource::get_error);
	ClassDB::bind_method(D_METHOD("get_header"), &Q3BspResource::get_header);
	ClassDB::bind_method(D_METHOD("get_counts"), &Q3BspResource::get_counts);
	ClassDB::bind_method(D_METHOD("get_lump_info"), &Q3BspResource::get_lump_info);
	ClassDB::bind_method(D_METHOD("get_entities"), &Q3BspResource::get_entities);
	ClassDB::bind_method(D_METHOD("get_shaders"), &Q3BspResource::get_shaders);
	ClassDB::bind_method(D_METHOD("get_planes"), &Q3BspResource::get_planes);
	ClassDB::bind_method(D_METHOD("get_nodes"), &Q3BspResource::get_nodes);
	ClassDB::bind_method(D_METHOD("get_leafs"), &Q3BspResource::get_leafs);
	ClassDB::bind_method(D_METHOD("get_leafsurfaces"), &Q3BspResource::get_leafsurfaces);
	ClassDB::bind_method(D_METHOD("get_leafbrushes"), &Q3BspResource::get_leafbrushes);
	ClassDB::bind_method(D_METHOD("get_models"), &Q3BspResource::get_models);
	ClassDB::bind_method(D_METHOD("get_brushes"), &Q3BspResource::get_brushes);
	ClassDB::bind_method(D_METHOD("get_brushsides"), &Q3BspResource::get_brushsides);
	ClassDB::bind_method(D_METHOD("get_drawVerts"), &Q3BspResource::get_drawVerts);
	ClassDB::bind_method(D_METHOD("get_drawIndexes"), &Q3BspResource::get_drawIndexes);
	ClassDB::bind_method(D_METHOD("get_fogs"), &Q3BspResource::get_fogs);
	ClassDB::bind_method(D_METHOD("get_lightmaps"), &Q3BspResource::get_lightmaps);
	ClassDB::bind_method(D_METHOD("get_lightgrid"), &Q3BspResource::get_lightgrid);
	ClassDB::bind_method(D_METHOD("get_visibility"), &Q3BspResource::get_visibility);
	ClassDB::bind_method(D_METHOD("get_all_data"), &Q3BspResource::get_all_data);
}

bool Q3BspResource::load_from_path(const String &p_path)
{
	path = p_path;
	String file_path = p_path;
	if (file_path.begins_with("res://") || file_path.begins_with("user://")) {
		file_path = ProjectSettings::get_singleton()->globalize_path(file_path);
	}

	std::string load_error;
	valid = q3::bsp::load_file(file_path.utf8().get_data(), bsp, load_error);
	error = valid ? String() : String(load_error.c_str());
	return valid;
}

bool Q3BspResource::_get(const StringName &p_name, Variant &r_property) const
{
	const StringName name = p_name;
	if (name == StringName("BSP/path")) {
		r_property = path;
	} else if (name == StringName("BSP/valid")) {
		r_property = valid;
	} else if (name == StringName("BSP/error")) {
		r_property = error;
	} else if (name == StringName("Header/header")) {
		r_property = get_header();
	} else if (name == StringName("Header/counts")) {
		r_property = get_counts();
	} else if (name == StringName("Header/lump_info")) {
		r_property = get_lump_info();
	} else if (name == StringName("Lumps/entityString")) {
		r_property = get_entities();
	} else if (name == StringName("Lumps/shaders")) {
		r_property = get_shaders();
	} else if (name == StringName("Lumps/planes")) {
		r_property = get_planes();
	} else if (name == StringName("Lumps/nodes")) {
		r_property = get_nodes();
	} else if (name == StringName("Lumps/leafs")) {
		r_property = get_leafs();
	} else if (name == StringName("Lumps/leafsurfaces")) {
		r_property = get_leafsurfaces();
	} else if (name == StringName("Lumps/leafbrushes")) {
		r_property = get_leafbrushes();
	} else if (name == StringName("Lumps/models")) {
		r_property = get_models();
	} else if (name == StringName("Lumps/brushes")) {
		r_property = get_brushes();
	} else if (name == StringName("Lumps/brushsides")) {
		r_property = get_brushsides();
	} else if (name == StringName("Lumps/drawVerts")) {
		r_property = get_drawVerts();
	} else if (name == StringName("Lumps/drawIndexes")) {
		r_property = get_drawIndexes();
	} else if (name == StringName("Lumps/fogs")) {
		r_property = get_fogs();
	} else if (name == StringName("Lumps/surfaces")) {
		r_property = vector_to_array(bsp.surfaces, surface_to_dict);
	} else if (name == StringName("Lumps/lightmaps")) {
		r_property = get_lightmaps();
	} else if (name == StringName("Lumps/lightgrid")) {
		r_property = get_lightgrid();
	} else if (name == StringName("Lumps/visibility")) {
		r_property = get_visibility();
	} else {
		return false;
	}
	return true;
}

void Q3BspResource::_get_property_list(List<PropertyInfo> *p_list) const
{
	add_property(p_list, Variant::STRING, "BSP/path");
	add_property(p_list, Variant::BOOL, "BSP/valid");
	add_property(p_list, Variant::STRING, "BSP/error");
	add_property(p_list, Variant::DICTIONARY, "Header/header");
	add_property(p_list, Variant::DICTIONARY, "Header/counts");
	add_property(p_list, Variant::ARRAY, "Header/lump_info");
	add_property(p_list, Variant::STRING, "Lumps/entityString");
	add_property(p_list, Variant::ARRAY, "Lumps/shaders");
	add_property(p_list, Variant::ARRAY, "Lumps/planes");
	add_property(p_list, Variant::ARRAY, "Lumps/nodes");
	add_property(p_list, Variant::ARRAY, "Lumps/leafs");
	add_property(p_list, Variant::PACKED_INT32_ARRAY, "Lumps/leafsurfaces");
	add_property(p_list, Variant::PACKED_INT32_ARRAY, "Lumps/leafbrushes");
	add_property(p_list, Variant::ARRAY, "Lumps/models");
	add_property(p_list, Variant::ARRAY, "Lumps/brushes");
	add_property(p_list, Variant::ARRAY, "Lumps/brushsides");
	add_property(p_list, Variant::ARRAY, "Lumps/drawVerts");
	add_property(p_list, Variant::PACKED_INT32_ARRAY, "Lumps/drawIndexes");
	add_property(p_list, Variant::ARRAY, "Lumps/fogs");
	add_property(p_list, Variant::ARRAY, "Lumps/surfaces");
	add_property(p_list, Variant::PACKED_BYTE_ARRAY, "Lumps/lightmaps");
	add_property(p_list, Variant::PACKED_BYTE_ARRAY, "Lumps/lightgrid");
	add_property(p_list, Variant::PACKED_BYTE_ARRAY, "Lumps/visibility");
}

String Q3BspResource::get_path() const { return path; }
bool Q3BspResource::is_valid() const { return valid; }
String Q3BspResource::get_error() const { return error; }
Dictionary Q3BspResource::get_header() const
{
	Dictionary out;
	if (!valid) {
		return out;
	}
	out["ident"] = bsp.header.ident;
	out["version"] = bsp.header.version;
	out["lump_count"] = static_cast<int>(q3::bsp::HEADER_LUMPS);
	return out;
}

Dictionary Q3BspResource::get_counts() const
{
	Dictionary out;
	if (!valid) {
		return out;
	}
	out["entityString_bytes"] = static_cast<int64_t>(bsp.entityString.size());
	out["shaders"] = static_cast<int64_t>(bsp.shaders.size());
	out["planes"] = static_cast<int64_t>(bsp.planes.size());
	out["nodes"] = static_cast<int64_t>(bsp.nodes.size());
	out["leafs"] = static_cast<int64_t>(bsp.leafs.size());
	out["leafsurfaces"] = static_cast<int64_t>(bsp.leafsurfaces.size());
	out["leafbrushes"] = static_cast<int64_t>(bsp.leafbrushes.size());
	out["models"] = static_cast<int64_t>(bsp.models.size());
	out["brushes"] = static_cast<int64_t>(bsp.brushes.size());
	out["brushsides"] = static_cast<int64_t>(bsp.brushsides.size());
	out["drawVerts"] = static_cast<int64_t>(bsp.drawVerts.size());
	out["drawIndexes"] = static_cast<int64_t>(bsp.drawIndexes.size());
	out["fogs"] = static_cast<int64_t>(bsp.fogs.size());
	out["surfaces"] = static_cast<int64_t>(bsp.surfaces.size());
	out["lightmaps_bytes"] = static_cast<int64_t>(bsp.lightmaps.size());
	out["lightgrid_bytes"] = static_cast<int64_t>(bsp.lightgrid.size());
	out["visibility_bytes"] = static_cast<int64_t>(bsp.visibility.size());
	return out;
}

Array Q3BspResource::get_lump_info() const
{
	Array out;
	if (!valid) {
		return out;
	}
	for (std::size_t i = 0; i < q3::bsp::HEADER_LUMPS; ++i) {
		Dictionary lump;
		lump["index"] = static_cast<int>(i);
		lump["name"] = LumpName(static_cast<q3::bsp::lumpIndex_t>(i));
		lump["fileofs"] = bsp.header.lumps[i].fileofs;
		lump["filelen"] = bsp.header.lumps[i].filelen;
		out.push_back(lump);
	}
	return out;
}
String Q3BspResource::get_entities() const { return String::utf8(bsp.entityString.c_str(), bsp.entityString.size()); }
Array Q3BspResource::get_shaders() const { return vector_to_array(bsp.shaders, shader_to_dict); }
Array Q3BspResource::get_planes() const { return vector_to_array(bsp.planes, plane_to_dict); }
Array Q3BspResource::get_nodes() const { return vector_to_array(bsp.nodes, node_to_dict); }
Array Q3BspResource::get_leafs() const { return vector_to_array(bsp.leafs, leaf_to_dict); }
PackedInt32Array Q3BspResource::get_leafsurfaces() const { return ints_to_array(bsp.leafsurfaces); }
PackedInt32Array Q3BspResource::get_leafbrushes() const { return ints_to_array(bsp.leafbrushes); }
Array Q3BspResource::get_models() const { return vector_to_array(bsp.models, model_to_dict); }
Array Q3BspResource::get_brushes() const { return vector_to_array(bsp.brushes, brush_to_dict); }
Array Q3BspResource::get_brushsides() const { return vector_to_array(bsp.brushsides, brush_side_to_dict); }
Array Q3BspResource::get_drawVerts() const { return vector_to_array(bsp.drawVerts, draw_vert_to_dict); }
PackedInt32Array Q3BspResource::get_drawIndexes() const { return ints_to_array(bsp.drawIndexes); }
Array Q3BspResource::get_fogs() const { return vector_to_array(bsp.fogs, fog_to_dict); }
PackedByteArray Q3BspResource::get_lightmaps() const { return bytes_to_array(bsp.lightmaps); }
PackedByteArray Q3BspResource::get_lightgrid() const { return bytes_to_array(bsp.lightgrid); }
PackedByteArray Q3BspResource::get_visibility() const { return bytes_to_array(bsp.visibility); }

Dictionary Q3BspResource::get_all_data() const
{
	Dictionary data;
	data["path"] = path;
	data["valid"] = valid;
	data["error"] = error;
	data["header"] = get_header();
	data["counts"] = get_counts();
	data["lump_info"] = get_lump_info();
	data["entityString"] = get_entities();
	data["shaders"] = get_shaders();
	data["planes"] = get_planes();
	data["nodes"] = get_nodes();
	data["leafs"] = get_leafs();
	data["leafsurfaces"] = get_leafsurfaces();
	data["leafbrushes"] = get_leafbrushes();
	data["models"] = get_models();
	data["brushes"] = get_brushes();
	data["brushsides"] = get_brushsides();
	data["drawVerts"] = get_drawVerts();
	data["drawIndexes"] = get_drawIndexes();
	data["fogs"] = get_fogs();
	data["surfaces"] = vector_to_array(bsp.surfaces, surface_to_dict);
	data["lightmaps"] = get_lightmaps();
	data["lightgrid"] = get_lightgrid();
	data["visibility"] = get_visibility();
	return data;
}
