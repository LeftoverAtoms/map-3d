#include "bsp_format.h"

#include <cstring>
#include <fstream>
#include <limits>

namespace q3::bsp
{
	namespace
	{
		template <typename T>
		bool copy_lump(const std::vector<std::uint8_t> &p_file, const dheader_t &p_header, lumpIndex_t lumpNum, std::vector<T> &r_out, std::string &r_error)
		{
			const lump_t &lump = p_header.lumps[static_cast<std::size_t>(lumpNum)];
			if (lump.fileofs < 0 || lump.filelen < 0) {
				r_error = "BSP lump has a negative offset or length.";
				return false;
			}

			const auto offset = static_cast<std::size_t>(lump.fileofs);
			const auto length = static_cast<std::size_t>(lump.filelen);
			if (offset > p_file.size() || length > p_file.size() - offset) {
				r_error = "BSP lump points outside the file.";
				return false;
			}

			if (length % sizeof(T) != 0) {
				r_error = "BSP lump size is not aligned to the expected structure.";
				return false;
			}

			r_out.resize(length / sizeof(T));
			if (length > 0) {
				std::memcpy(r_out.data(), p_file.data() + offset, length);
			}
			return true;
		}

		bool copy_lump_bytes(const std::vector<std::uint8_t> &p_file, const dheader_t &p_header, lumpIndex_t lumpNum, std::vector<std::uint8_t> &r_out, std::string &r_error)
		{
			const lump_t &lump = p_header.lumps[static_cast<std::size_t>(lumpNum)];
			if (lump.fileofs < 0 || lump.filelen < 0) {
				r_error = "BSP lump has a negative offset or length.";
				return false;
			}

			const auto offset = static_cast<std::size_t>(lump.fileofs);
			const auto length = static_cast<std::size_t>(lump.filelen);
			if (offset > p_file.size() || length > p_file.size() - offset) {
				r_error = "BSP lump points outside the file.";
				return false;
			}

			r_out.assign(p_file.begin() + offset, p_file.begin() + offset + length);
			return true;
		}
	}

	bool load_file(const std::string &p_path, BspData &r_data, std::string &r_error)
	{
		std::ifstream file(p_path, std::ios::binary | std::ios::ate);
		if (!file) {
			r_error = "Could not open BSP file.";
			return false;
		}

		const std::ifstream::pos_type end = file.tellg();
		if (end < static_cast<std::ifstream::pos_type>(sizeof(dheader_t))) {
			r_error = "File is too small to contain a Quake 3 BSP header.";
			return false;
		}
		if (end > static_cast<std::ifstream::pos_type>(std::numeric_limits<std::int32_t>::max())) {
			r_error = "BSP file is too large.";
			return false;
		}

		std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char *>(bytes.data()), end);
		if (!file) {
			r_error = "Could not read BSP file.";
			return false;
		}

		std::memcpy(&r_data.header, bytes.data(), sizeof(dheader_t));
		if (r_data.header.ident != BSP_IDENT) {
			r_error = "File is not a Quake 3 IBSP file.";
			return false;
		}
		if (r_data.header.version != BSP_VERSION) {
			r_error = "Unsupported BSP version.";
			return false;
		}

		std::vector<std::uint8_t> entity_bytes;
		if (!copy_lump_bytes(bytes, r_data.header, LUMP_ENTITIES, entity_bytes, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_SHADERS, r_data.shaders, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_PLANES, r_data.planes, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_NODES, r_data.nodes, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_LEAFS, r_data.leafs, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_LEAFSURFACES, r_data.leafsurfaces, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_LEAFBRUSHES, r_data.leafbrushes, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_MODELS, r_data.models, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_BRUSHES, r_data.brushes, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_BRUSHSIDES, r_data.brushsides, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_DRAWVERTS, r_data.drawVerts, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_DRAWINDEXES, r_data.drawIndexes, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_FOGS, r_data.fogs, r_error) ||
				!copy_lump(bytes, r_data.header, LUMP_SURFACES, r_data.surfaces, r_error) ||
				!copy_lump_bytes(bytes, r_data.header, LUMP_LIGHTMAPS, r_data.lightmaps, r_error) ||
				!copy_lump_bytes(bytes, r_data.header, LUMP_LIGHTGRID, r_data.lightgrid, r_error) ||
				!copy_lump_bytes(bytes, r_data.header, LUMP_VISIBILITY, r_data.visibility, r_error)) {
			return false;
		}

		r_data.entityString.assign(reinterpret_cast<const char *>(entity_bytes.data()), entity_bytes.size());
		return true;
	}
}
