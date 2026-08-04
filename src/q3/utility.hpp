#pragma once

#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <climits>
#include <cstddef>

namespace rngo::q3
{
	template <typename T>
	inline godot::Vector3 QuakeToGodot(const T p_vector[3])
	{
		return godot::Vector3(
			static_cast<godot::real_t>(p_vector[0]),
			static_cast<godot::real_t>(p_vector[2]),
			-static_cast<godot::real_t>(p_vector[1])
		);
	}

	template <typename T>
	inline godot::AABB QuakeToGodotBounds(const T p_mins[3], const T p_maxs[3])
	{
		const godot::Vector3 min = QuakeToGodot(p_mins);
		const godot::Vector3 max = QuakeToGodot(p_maxs);

		const godot::Vector3 position(
			godot::Math::min(min.x, max.x),
			godot::Math::min(min.y, max.y),
			godot::Math::min(min.z, max.z)
		);

		const godot::Vector3 end(
			godot::Math::max(min.x, max.x),
			godot::Math::max(min.y, max.y),
			godot::Math::max(min.z, max.z)
		);

		return godot::AABB(position, end - position);
	}

	inline bool IsValidRange(int p_first, int p_count, std::size_t p_size)
	{
		if (p_first < 0 || p_count < 0)
		{
			return false;
		}

		const std::size_t first = static_cast<std::size_t>(p_first);
		const std::size_t count = static_cast<std::size_t>(p_count);

		return first <= p_size && count <= p_size - first;
	}

	inline bool IsValidIndex(int p_index, std::size_t p_size)
	{
		return p_index >= 0 && static_cast<std::size_t>(p_index) < p_size;
	}

	inline bool IsValidBSPChildIndex(int p_child, std::size_t p_node_count, std::size_t p_leaf_count)
	{
		if (p_child >= 0)
		{
			return static_cast<std::size_t>(p_child) < p_node_count;
		}

		if (p_child == INT_MIN)
		{
			return false;
		}

		const int leaf_index = -1 - p_child;
		return static_cast<std::size_t>(leaf_index) < p_leaf_count;
	}
}