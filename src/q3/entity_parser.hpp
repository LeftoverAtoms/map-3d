#pragma once

#include <map>
#include <string>
#include <vector>

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace rngo::q3
{
	struct Entity
	{
		std::map<std::string, std::string> properties;

		std::string classname;
		std::string targetname;

		int model_index = -1;

		godot::Vector3 origin;
		godot::Vector3 angles;

		bool has_origin = false;
		bool has_angles = false;
	};

	godot::Error ParseEntities(const std::vector<char> &p_entity_data, std::vector<Entity> &r_entities);

	const std::string *GetEntityProperty(const Entity &p_entity, const std::string &p_key);
	const Entity *FindEntityForModel(const std::vector<Entity> &p_entities, int p_model_index);
}