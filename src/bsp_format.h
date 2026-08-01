#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace q3::bsp
{
	using vec3_t = float[3];

	static_assert(sizeof(float) == 4);

	inline constexpr std::size_t MAX_QPATH = 64;

	inline constexpr std::int32_t BSP_IDENT =
		('P' << 24) |
		('S' << 16) |
		('B' <<  8) |
		('I' <<  0);
	inline constexpr std::int32_t BSP_VERSION = 46;

	enum class LumpType
	{
		Entities,
		Shaders,
		Planes,
		Nodes,
		Leaves,
		LeafSurfaces,
		LeafBrushes,
		Models,
		Brushes,
		BrushSides,
		DrawVertices,
		DrawIndexes,
		Fogs,
		Surfaces,
		Lightmaps,
		LightGrid,
		Visibility,
		Count,
	};

	inline constexpr std::size_t HEADER_LUMPS = static_cast<std::size_t>(LumpType::Count);

	struct lump_t
	{
		std::int32_t fileofs;
		std::int32_t filelen;
	};

	static_assert(sizeof(lump_t) == 8);

	struct dheader_t
	{
		std::int32_t ident;
		std::int32_t version;

		lump_t lumps[HEADER_LUMPS];
	};

	static_assert(sizeof(dheader_t) == 144);

	struct dshader_t
	{
		char shader[MAX_QPATH];
		std::int32_t surfaceFlags;
		std::int32_t contentFlags;
	};

	static_assert(sizeof(dshader_t) == 72);

	struct dplane_t
	{
		float normal[3];
		float dist;
	};

	static_assert(sizeof(dplane_t) == 16);

	struct dnode_t
	{
		std::int32_t planeNum;
		std::int32_t children[2];
		std::int32_t mins[3];
		std::int32_t maxs[3];
	};

	static_assert(sizeof(dnode_t) == 36);

	struct dleaf_t
	{
		std::int32_t cluster;
		std::int32_t area;

		std::int32_t mins[3];
		std::int32_t maxs[3];

		std::int32_t firstLeafSurface;
		std::int32_t numLeafSurfaces;

		std::int32_t firstLeafBrush;
		std::int32_t numLeafBrushes;
	};

	static_assert(sizeof(dleaf_t) == 48);

	struct dmodel_t
	{
		float mins[3];
		float maxs[3];
		std::int32_t firstSurface;
		std::int32_t numSurfaces;
		std::int32_t firstBrush;
		std::int32_t numBrushes;
	};

	static_assert(sizeof(dmodel_t) == 40);

	struct dbrush_t
	{
		std::int32_t firstSide;
		std::int32_t numSides;
		std::int32_t shaderNum;
	};

	static_assert(sizeof(dbrush_t) == 12);

	struct dbrushside_t
	{
		std::int32_t planeNum;
		std::int32_t shaderNum;
	};

	static_assert(sizeof(dbrushside_t) == 8);

	struct dfog_t
	{
		char shader[MAX_QPATH];
		std::int32_t brushNum;
		std::int32_t visibleSide;
	};

	static_assert(sizeof(dfog_t) == 72);

	struct drawVert_t
	{
		vec3_t xyz;
		float st[2];
		float lightmap[2];
		vec3_t normal;
		std::uint8_t color[4];
	};

	static_assert(sizeof(drawVert_t) == 44);

	enum mapSurfaceType_t
	{
		MST_BAD,
		MST_PLANAR,
		MST_PATCH,
		MST_TRIANGLE_SOUP,
		MST_FLARE,
	};

	struct dsurface_t
	{
		std::int32_t shaderNum;
		std::int32_t fogNum;
		std::int32_t surfaceType;

		std::int32_t firstVert;
		std::int32_t numVerts;

		std::int32_t firstIndex;
		std::int32_t numIndexes;

		std::int32_t lightmapNum;
		std::int32_t lightmapX;
		std::int32_t lightmapY;
		std::int32_t lightmapWidth;
		std::int32_t lightmapHeight;

		vec3_t lightmapOrigin;
		vec3_t lightmapVecs[3];

		std::int32_t patchWidth;
		std::int32_t patchHeight;
	};

	static_assert(sizeof(dsurface_t) == 104);

	struct BspData
	{
		dheader_t header {};
		std::string entities;
		std::vector<dshader_t> shaders;
		std::vector<dplane_t> planes;
		std::vector<dnode_t> nodes;
		std::vector<dleaf_t> leaves;
		std::vector<std::int32_t> leaf_surfaces;
		std::vector<std::int32_t> leaf_brushes;
		std::vector<dmodel_t> models;
		std::vector<dbrush_t> brushes;
		std::vector<dbrushside_t> brush_sides;
		std::vector<drawVert_t> draw_vertices;
		std::vector<std::int32_t> draw_indexes;
		std::vector<dfog_t> fogs;
		std::vector<dsurface_t> surfaces;
		std::vector<std::uint8_t> lightmaps;
		std::vector<std::uint8_t> light_grid;
		std::vector<std::uint8_t> visibility;
	};

	bool load_file(const std::string &p_path, BspData &r_data, std::string &r_error);
}
