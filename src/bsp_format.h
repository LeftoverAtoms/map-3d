#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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

	enum lumpIndex_t
	{
		LUMP_ENTITIES,
		LUMP_SHADERS,
		LUMP_PLANES,
		LUMP_NODES,
		LUMP_LEAFS,
		LUMP_LEAFSURFACES,
		LUMP_LEAFBRUSHES,
		LUMP_MODELS,
		LUMP_BRUSHES,
		LUMP_BRUSHSIDES,
		LUMP_DRAWVERTS,
		LUMP_DRAWINDEXES,
		LUMP_FOGS,
		LUMP_SURFACES,
		LUMP_LIGHTMAPS,
		LUMP_LIGHTGRID,
		LUMP_VISIBILITY,
		HEADER_LUMPS,
	};

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

	inline constexpr std::int32_t CONTENTS_NODE = -1;
	inline constexpr std::int32_t MAX_FACE_POINTS = 64;
	inline constexpr std::int32_t MAX_PATCH_SIZE = 32;
	inline constexpr std::int32_t MAX_GRID_SIZE = 65;
	inline constexpr std::int32_t SMP_FRAMES = 2;
	inline constexpr std::int32_t LIGHTMAP_BY_VERTEX = -3;
	inline constexpr std::int32_t LIGHTMAP_WHITEIMAGE = -2;
	inline constexpr std::int32_t LIGHTMAP_NONE = -1;
	inline constexpr std::int32_t SURF_NODRAW = 0x80;

	enum surfaceType_t
	{
		SF_BAD,
		SF_SKIP,
		SF_FACE,
		SF_GRID,
		SF_TRIANGLES,
		SF_POLY,
		SF_MD3,
		SF_MD4,
		SF_FLARE,
		SF_ENTITY,
		SF_DISPLAY_LIST,
	};

	struct cplane_t {
		vec3_t normal;
		float dist;
		std::uint8_t type;
		std::uint8_t signbits;
		std::uint8_t pad[2];
	};

	struct surfaceData_t
	{
		surfaceType_t surfaceType = SF_BAD;
		virtual ~surfaceData_t() = default;
	};

	struct srfSurfaceFace_t : surfaceData_t
	{
		cplane_t plane;
		std::int32_t dlightBits[SMP_FRAMES];
		std::int32_t numPoints;
		std::int32_t numIndices;
		std::vector<float> points;
		std::vector<std::int32_t> indices;
	};

	struct srfGridMesh_t : surfaceData_t
	{
		std::int32_t dlightBits[SMP_FRAMES];
		vec3_t meshBounds[2];
		vec3_t localOrigin;
		float meshRadius;
		vec3_t lodOrigin;
		float lodRadius;
		std::int32_t lodFixed;
		std::int32_t lodStitched;
		std::int32_t width;
		std::int32_t height;
		std::vector<float> widthLodError;
		std::vector<float> heightLodError;
		std::vector<drawVert_t> verts;
	};

	struct srfTriangles_t : surfaceData_t
	{
		std::int32_t dlightBits[SMP_FRAMES];
		vec3_t bounds[2];
		vec3_t localOrigin;
		float radius;
		std::int32_t numIndexes;
		std::vector<std::int32_t> indexes;
		std::int32_t numVerts;
		std::vector<drawVert_t> verts;
	};

	struct srfFlare_t : surfaceData_t
	{
		vec3_t origin;
		vec3_t normal;
		vec3_t color;
	};

	struct msurface_t
	{
		std::int32_t viewCount;
		struct shader_t *shader;
		std::int32_t fogIndex;
		std::shared_ptr<surfaceData_t> data;
	};

	struct shader_t
	{
		char name[MAX_QPATH];
		std::int32_t shaderNum;
		std::int32_t lightmapNum;
		shader_t *remappedShader;
		float timeOffset;
		bool defaultShader;
		bool isSky;
		struct fogParms_t
		{
			vec3_t color;
			float depthForOpaque;
		} fogParms;
	};

	struct mnode_t
	{
		std::int32_t contents;
		std::int32_t visframe;
		vec3_t mins;
		vec3_t maxs;
		mnode_t *parent;

		cplane_t *plane;
		mnode_t *children[2];

		std::int32_t cluster;
		std::int32_t area;

		msurface_t **firstmarksurface;
		std::int32_t nummarksurfaces;
	};

	struct bmodel_t
	{
		vec3_t bounds[2];
		msurface_t *firstSurface;
		std::int32_t numSurfaces;
	};

	struct fog_t
	{
		std::int32_t originalBrushNumber;
		vec3_t bounds[2];
		std::uint32_t colorInt;
		float tcScale;
		shader_t::fogParms_t parms;
		bool hasSurface;
		float surface[4];
	};

	struct world_t
	{
		char name[MAX_QPATH];
		char baseName[MAX_QPATH];

		std::int32_t dataSize;

		std::int32_t numShaders;
		std::unique_ptr<dshader_t[]> shaders;
		std::vector<std::unique_ptr<shader_t>> rendererShaders;

		std::unique_ptr<bmodel_t[]> bmodels;

		std::int32_t numLightmaps;
		std::vector<std::uint8_t> lightmaps;
		std::int32_t numLightGrid;
		std::vector<std::uint8_t> lightgrid;

		std::int32_t numplanes;
		std::unique_ptr<cplane_t[]> planes;

		std::int32_t numnodes;
		std::int32_t numDecisionNodes;
		std::unique_ptr<mnode_t[]> nodes;

		std::int32_t numsurfaces;
		std::unique_ptr<msurface_t[]> surfaces;

		std::int32_t nummarksurfaces;
		std::unique_ptr<msurface_t *[]> marksurfaces;

		std::int32_t numfogs;
		std::unique_ptr<fog_t[]> fogs;

		vec3_t lightGridOrigin;
		vec3_t lightGridSize;
		vec3_t lightGridInverseSize;
		std::int32_t lightGridBounds[3];
		std::byte* lightGridData;

		std::int32_t numClusters;
		std::int32_t clusterBytes;
		const std::byte* vis;

		std::vector<std::byte> novisData;
		std::byte* novis;
		std::vector<std::byte> visibility;
		std::unique_ptr<char[]> entityData;
		char* entityString;
		char* entityParsePoint;
	};

	void RE_LoadWorldMap(const char *name);
	void RE_ClearWorldMap();
	world_t *R_GetWorld();
	void RE_SetWorldVisData(const std::byte *vis);
	bool R_GetEntityToken(char *buffer, std::int32_t size);
}
