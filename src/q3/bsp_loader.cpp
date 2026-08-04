#include "bsp_loader.hpp"

#include <vector>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

///////////////////////////

namespace rngo::q3
{
	namespace
	{
		template <typename T>
		bool Q3_CopyLump(const Ref<FileAccess> &p_file, const q3_lump_t &p_lump, std::vector<T> &r_dest)
		{
			static_assert(std::is_trivially_copyable_v<T>);

			ERR_FAIL_COND_V_MSG(p_lump.filelen < 0 || p_lump.fileofs < 0, false,
				"Q3_LoadBSPFile: negative lump offset or length");

			const auto file_length = p_file->get_length();
			const auto lump_offset = static_cast<uint64_t>(p_lump.fileofs);
			const auto lump_length = static_cast<uint64_t>(p_lump.filelen);

			ERR_FAIL_COND_V_MSG(lump_offset > file_length || lump_length > file_length - lump_offset, false,
				"Q3_LoadBSPFile: lump extends outside the BSP file");

			ERR_FAIL_COND_V_MSG(lump_length % sizeof(T) != 0, false,
				"Q3_LoadBSPFile: odd lump size");

			p_file->seek(lump_offset);
			r_dest.resize(lump_length / sizeof(T));

			const auto bytes = p_file->get_buffer(reinterpret_cast<uint8_t *>(r_dest.data()), lump_length);
			ERR_FAIL_COND_V(bytes != lump_length, false);

			return true;
		}
	}

	Error Q3_LoadBSPFile(const String &p_path, BSP &r_data)
	{
		static_assert(std::endian::native == std::endian::little);

		q3_dheader_t header = {};

		const auto file = FileAccess::open(p_path, FileAccess::READ);
		ERR_FAIL_COND_V(file.is_null(), FileAccess::get_open_error());

		const auto bytes = file->get_buffer(reinterpret_cast<uint8_t *>(&header), sizeof(header));
		ERR_FAIL_COND_V(bytes != sizeof(header), ERR_FILE_CANT_READ);

		ERR_FAIL_COND_V_MSG(header.ident != Q3_BSP_IDENT, ERR_FILE_UNRECOGNIZED,
			vformat("%s is not a IBSP file", p_path.get_file()));
		ERR_FAIL_COND_V_MSG(header.version != Q3_BSP_VERSION, ERR_FILE_UNRECOGNIZED,
			vformat("%s is version %i, not %i", p_path.get_file(), header.version, Q3_BSP_VERSION));

		BSP loaded;

		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_SHADERS],		loaded.q3_dshaders),		ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_MODELS],		loaded.q3_dmodels),			ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_PLANES],		loaded.q3_dplanes),			ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_LEAFS],			loaded.q3_dleafs),			ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_NODES],			loaded.q3_dnodes),			ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_LEAFSURFACES],	loaded.q3_dleafsurfaces),	ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_LEAFBRUSHES],	loaded.q3_dleafbrushes),	ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_BRUSHES],		loaded.q3_dbrushes),		ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_BRUSHSIDES],	loaded.q3_dbrushsides),		ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_DRAWVERTS],		loaded.q3_drawVerts),		ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_SURFACES],		loaded.q3_drawSurfaces),	ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_FOGS],			loaded.q3_dfogs),			ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_DRAWINDEXES],	loaded.q3_drawIndexes),		ERR_FILE_CORRUPT);

		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_VISIBILITY],	loaded.q3_visBytes),		ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_LIGHTMAPS],		loaded.q3_lightBytes),		ERR_FILE_CORRUPT);
		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_ENTITIES],		loaded.q3_dentdata),		ERR_FILE_CORRUPT);

		ERR_FAIL_COND_V(!Q3_CopyLump(file, header.lumps[Q3_LUMP_LIGHTGRID],		loaded.q3_gridData),		ERR_FILE_CORRUPT);

		r_data = std::move(loaded);

		return OK;
	}
}