#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "q3_bsp_format_loader.h"
#include "q3_bsp_draw_list_effect.h"
#include "q3_bsp_loader.h"
#include "q3_bsp_renderer.h"
#include "q3_bsp_resource.h"

using namespace godot;

namespace
{
	Ref<Q3BspFormatLoader> q3_bsp_format_loader;
}

void initialize_gdextension_types(ModuleInitializationLevel p_level)
{
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(Q3BspResource);
	GDREGISTER_CLASS(Q3BspFormatLoader);
	GDREGISTER_CLASS(Q3BspLoader);
	GDREGISTER_CLASS(Q3BspRenderer);
	GDREGISTER_CLASS(Q3BspDrawListEffect);

	q3_bsp_format_loader.instantiate();
	ResourceLoader::get_singleton()->add_resource_format_loader(q3_bsp_format_loader, true);
}

void uninitialize_gdextension_types(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	if (q3_bsp_format_loader.is_valid()) {
		ResourceLoader::get_singleton()->remove_resource_format_loader(q3_bsp_format_loader);
		q3_bsp_format_loader.unref();
	}
}

extern "C"
{
	// Initialization
	GDExtensionBool GDE_EXPORT q3_world_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
	{
		GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
		init_obj.register_initializer(initialize_gdextension_types);
		init_obj.register_terminator(uninitialize_gdextension_types);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}
