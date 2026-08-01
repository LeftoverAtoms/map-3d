#include "q3_bsp_renderer.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace
{
	inline constexpr int LIGHTMAP_SIZE = 128;
	inline constexpr int LIGHTMAP_BYTES = LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3;

	String resolve_bsp_file_path(const String &p_path)
	{
		String file_path = p_path.strip_edges();
		if ((file_path.begins_with("\"") && file_path.ends_with("\"")) ||
				(file_path.begins_with("'") && file_path.ends_with("'"))) {
			file_path = file_path.substr(1, file_path.length() - 2).strip_edges();
		}

		if (file_path.begins_with("res://") || file_path.begins_with("user://")) {
			return ProjectSettings::get_singleton()->globalize_path(file_path);
		}

		const String normalized = file_path.replace("\\", "/");
		if (normalized.begins_with("/") || normalized.begins_with("//") ||
				(normalized.length() >= 3 && normalized[1] == ':' && normalized[2] == '/')) {
			return file_path;
		}

		if (normalized.begins_with("project/")) {
			return ProjectSettings::get_singleton()->globalize_path(String("res://") + normalized.substr(8));
		}

		if (normalized.begins_with("./")) {
			return ProjectSettings::get_singleton()->globalize_path(String("res://") + normalized.substr(2));
		}

		return ProjectSettings::get_singleton()->globalize_path(String("res://") + normalized);
	}
}

void Q3BspRenderer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_bsp_path", "path"), &Q3BspRenderer::set_bsp_path);
	ClassDB::bind_method(D_METHOD("get_bsp_path"), &Q3BspRenderer::get_bsp_path);
	ClassDB::bind_method(D_METHOD("load_bsp", "path"), &Q3BspRenderer::load_bsp, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("clear"), &Q3BspRenderer::clear);
	ClassDB::bind_method(D_METHOD("is_valid"), &Q3BspRenderer::is_valid);
	ClassDB::bind_method(D_METHOD("get_error"), &Q3BspRenderer::get_error);
	ClassDB::bind_method(D_METHOD("get_surface_count"), &Q3BspRenderer::get_surface_count);
	ClassDB::bind_method(D_METHOD("get_shader_count"), &Q3BspRenderer::get_shader_count);
	ClassDB::bind_method(D_METHOD("get_lightmap_count"), &Q3BspRenderer::get_lightmap_count);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "bsp_path", PROPERTY_HINT_GLOBAL_FILE, "*.bsp"), "set_bsp_path", "get_bsp_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "valid", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "is_valid");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "error", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_error");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "surface_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_surface_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shader_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_shader_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "lightmap_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_lightmap_count");
}

void Q3BspRenderer::_notification(int p_what)
{
	if (p_what == NOTIFICATION_READY && !bsp_path.is_empty() && !valid) {
		load_bsp();
	}
}

void Q3BspRenderer::set_bsp_path(const String &p_path)
{
	bsp_path = p_path;
	if (is_inside_tree()) {
		load_bsp();
	}
}

String Q3BspRenderer::get_bsp_path() const
{
	return bsp_path;
}

bool Q3BspRenderer::load_bsp(const String &p_path)
{
	if (!p_path.is_empty()) {
		bsp_path = p_path;
	}
	if (bsp_path.is_empty()) {
		clear();
		error = "Q3BspRenderer has no BSP path.";
		UtilityFunctions::push_warning(error);
		return false;
	}

	String file_path = resolve_bsp_file_path(bsp_path);

	std::string load_error;
	valid = q3::bsp::load_file(file_path.utf8().get_data(), bsp, load_error);
	error = valid ? String() : String(load_error.c_str());
	if (!valid) {
		UtilityFunctions::push_error(String("Failed to load Quake 3 BSP '") + bsp_path + "': " + error);
	}
	return valid;
}

void Q3BspRenderer::clear()
{
	bsp = q3::bsp::BspData();
	valid = false;
	error = String();
}

bool Q3BspRenderer::is_valid() const
{
	return valid;
}

String Q3BspRenderer::get_error() const
{
	return error;
}

int Q3BspRenderer::get_surface_count() const
{
	return static_cast<int>(bsp.surfaces.size());
}

int Q3BspRenderer::get_shader_count() const
{
	return static_cast<int>(bsp.shaders.size());
}

int Q3BspRenderer::get_lightmap_count() const
{
	return static_cast<int>(bsp.lightmaps.size() / LIGHTMAP_BYTES);
}
