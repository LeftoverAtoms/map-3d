#include "entity_parser.hpp"

#include "utility.hpp"

#include <godot_cpp/core/error_macros.hpp>

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace godot;

namespace rngo::q3
{
	namespace
	{
		class EntityTokenizer
		{
		private:
			const std::string &text;
			std::size_t position = 0;

			void SkipWhitespaceAndComments()
			{
				while (position < text.size())
				{
					if (std::isspace(static_cast<unsigned char>(text[position])))
					{
						++position;
						continue;
					}

					if (text[position] == '/' && position + 1 < text.size() && text[position + 1] == '/')
					{
						position += 2;

						while (position < text.size() && text[position] != '\n')
							++position;

						continue;
					}

					break;
				}
			}

		public:
			explicit EntityTokenizer(const std::string &p_text) : text(p_text)
			{
			}

			bool Next(std::string &r_token)
			{
				SkipWhitespaceAndComments();

				if (position >= text.size())
					return false;

				const char first = text[position];

				if (first == '{' || first == '}')
				{
					r_token.assign(1, first);
					++position;
					return true;
				}

				if (first == '"')
				{
					++position;
					r_token.clear();

					while (position < text.size())
					{
						const char c = text[position++];

						if (c == '"')
							return true;

						if (c == '\\' && position < text.size())
						{
							const char escaped = text[position++];

							if (escaped == '"' || escaped == '\\')
							{
								r_token.push_back(escaped);
							}
							else
							{
								r_token.push_back('\\');
								r_token.push_back(escaped);
							}

							continue;
						}

						r_token.push_back(c);
					}

					return false;
				}

				const std::size_t start = position;

				while (
					position < text.size() &&
					!std::isspace(static_cast<unsigned char>(text[position])) &&
					text[position] != '{' &&
					text[position] != '}'
				)
				{
					++position;
				}

				r_token = text.substr(start, position - start);

				return !r_token.empty();
			}
		};

		bool ParseVector3(const std::string &p_value, Vector3 &r_value, bool p_convert_coordinates)
		{
			float values[3];

			if (std::sscanf(p_value.c_str(), "%f %f %f", &values[0], &values[1], &values[2]) != 3)
				return false;

			if (p_convert_coordinates)
				r_value = QuakeToGodot(values);
			else
				r_value = Vector3(values[0], values[1], values[2]);

			return true;
		}

		int ParseInlineModelIndex(const std::string &p_value)
		{
			if (p_value.size() < 2 || p_value[0] != '*')
				return -1;

			char *end = nullptr;
			const long value = std::strtol(p_value.c_str() + 1, &end, 10);

			if (
				end == p_value.c_str() + 1 ||
				*end != '\0' ||
				value < 0 ||
				value > std::numeric_limits<int>::max()
			)
			{
				return -1;
			}

			return static_cast<int>(value);
		}

		void FinalizeEntity(Entity &r_entity)
		{
			if (const std::string *classname = GetEntityProperty(r_entity, "classname"))
				r_entity.classname = *classname;

			if (const std::string *targetname = GetEntityProperty(r_entity, "targetname"))
				r_entity.targetname = *targetname;

			if (const std::string *model = GetEntityProperty(r_entity, "model"))
				r_entity.model_index = ParseInlineModelIndex(*model);

			if (const std::string *origin = GetEntityProperty(r_entity, "origin"))
				r_entity.has_origin = ParseVector3(*origin, r_entity.origin, true);

			if (const std::string *angles = GetEntityProperty(r_entity, "angles"))
			{
				r_entity.has_angles = ParseVector3(*angles, r_entity.angles, false);
			}
			else if (const std::string *angle = GetEntityProperty(r_entity, "angle"))
			{
				float yaw = 0.0f;

				if (std::sscanf(angle->c_str(), "%f", &yaw) == 1)
				{
					r_entity.angles = Vector3(0.0f, yaw, 0.0f);
					r_entity.has_angles = true;
				}
			}
		}
	}

	const std::string *GetEntityProperty(const Entity &p_entity, const std::string &p_key)
	{
		const auto found = p_entity.properties.find(p_key);

		if (found == p_entity.properties.end())
			return nullptr;

		return &found->second;
	}

	const Entity *FindEntityForModel(const std::vector<Entity> &p_entities, int p_model_index)
	{
		for (const Entity &entity : p_entities)
		{
			if (entity.model_index == p_model_index)
				return &entity;
		}

		return nullptr;
	}

	Error ParseEntities(const std::vector<char> &p_entity_data, std::vector<Entity> &r_entities)
	{
		r_entities.clear();

		if (p_entity_data.empty())
			return OK;

		std::string entity_text(p_entity_data.begin(), p_entity_data.end());

		while (!entity_text.empty() && entity_text.back() == '\0')
			entity_text.pop_back();

		EntityTokenizer tokenizer(entity_text);
		std::string token;

		while (tokenizer.Next(token))
		{
			ERR_FAIL_COND_V_MSG(
				token != "{",
				ERR_PARSE_ERROR,
				"ParseEntities: expected '{'."
			);

			Entity entity;

			while (true)
			{
				ERR_FAIL_COND_V_MSG(
					!tokenizer.Next(token),
					ERR_PARSE_ERROR,
					"ParseEntities: unexpected end of entity data."
				);

				if (token == "}")
					break;

				const std::string key = token;

				ERR_FAIL_COND_V_MSG(
					!tokenizer.Next(token),
					ERR_PARSE_ERROR,
					"ParseEntities: entity key has no value."
				);

				ERR_FAIL_COND_V_MSG(
					token == "{" || token == "}",
					ERR_PARSE_ERROR,
					"ParseEntities: malformed entity key/value pair."
				);

				entity.properties[key] = token;
			}

			FinalizeEntity(entity);
			r_entities.push_back(std::move(entity));
		}

		return OK;
	}
}