#pragma once

#include <vector>

#include "godot_cpp/variant/string.hpp"

///////////////////////////

namespace rngo::q3
{
	typedef unsigned char byte;
	typedef float vec_t;
	typedef vec_t vec3_t[3];

	// surface geometry should not exceed these limits
	#define	SHADER_MAX_VERTEXES	1000
	#define	SHADER_MAX_INDEXES	(6*SHADER_MAX_VERTEXES)


	// the maximum size of game reletive pathnames
	#define	MAX_QPATH		64



	#define Q3_BSP_IDENT	(('P'<<24)+('S'<<16)+('B'<<8)+'I')
			// little-endian "IBSP"

	#define Q3_BSP_VERSION			46


	// there shouldn't be any problem with increasing these values at the
	// expense of more memory allocation in the utilities
	#define	Q3_MAX_MAP_MODELS		0x400
	#define	Q3_MAX_MAP_BRUSHES		0x8000
	#define	Q3_MAX_MAP_ENTITIES	0x800
	#define	Q3_MAX_MAP_ENTSTRING	0x10000
	#define	Q3_MAX_MAP_SHADERS		0x400

	#define	Q3_MAX_MAP_AREAS		0x100	// MAX_MAP_AREA_BYTES in q_shared must match!
	#define	Q3_MAX_MAP_FOGS		0x100
	#define	Q3_MAX_MAP_PLANES		0x10000
	#define	Q3_MAX_MAP_NODES		0x10000
	#define	Q3_MAX_MAP_BRUSHSIDES	0x10000
	#define	Q3_MAX_MAP_LEAFS		0x10000
	#define	Q3_MAX_MAP_LEAFFACES	0x10000
	#define	Q3_MAX_MAP_LEAFBRUSHES	0x10000
	#define	Q3_MAX_MAP_PORTALS		0x10000
	#define	Q3_MAX_MAP_LIGHTING	0x400000
	#define	Q3_MAX_MAP_LIGHTGRID	0x400000
	#define	Q3_MAX_MAP_VISIBILITY	0x200000

	#define	Q3_MAX_MAP_DRAW_SURFS	0x20000
	#define	Q3_MAX_MAP_DRAW_VERTS	0x80000
	#define	Q3_MAX_MAP_DRAW_INDEXES	0x80000


	// key / value pair sizes in the entities lump
	#define	Q3_MAX_KEY				32
	#define	Q3_MAX_VALUE			1024

	// the editor uses these predefined yaw angles to orient entities up or down
	#define	ANGLE_UP			-1
	#define	ANGLE_DOWN			-2

	#define	LIGHTMAP_WIDTH		128
	#define	LIGHTMAP_HEIGHT		128


	//=============================================================================


	typedef struct {
		int		fileofs, filelen;
	} q3_lump_t;

	#define	Q3_LUMP_ENTITIES		0
	#define	Q3_LUMP_SHADERS		1
	#define	Q3_LUMP_PLANES			2
	#define	Q3_LUMP_NODES			3
	#define	Q3_LUMP_LEAFS			4
	#define	Q3_LUMP_LEAFSURFACES	5
	#define	Q3_LUMP_LEAFBRUSHES	6
	#define	Q3_LUMP_MODELS			7
	#define	Q3_LUMP_BRUSHES		8
	#define	Q3_LUMP_BRUSHSIDES		9
	#define	Q3_LUMP_DRAWVERTS		10
	#define	Q3_LUMP_DRAWINDEXES	11
	#define	Q3_LUMP_FOGS			12
	#define	Q3_LUMP_SURFACES		13
	#define	Q3_LUMP_LIGHTMAPS		14
	#define	Q3_LUMP_LIGHTGRID		15
	#define	Q3_LUMP_VISIBILITY		16
	#define	Q3_HEADER_LUMPS		17

	typedef struct {
		int			ident;
		int			version;

		q3_lump_t		lumps[Q3_HEADER_LUMPS];
	} q3_dheader_t;

	typedef struct {
		float		mins[3], maxs[3];
		int			firstSurface, numSurfaces;
		int			firstBrush, numBrushes;
	} q3_dmodel_t;

	typedef struct {
		char		shader[MAX_QPATH];
		int			surfaceFlags;
		int			contentFlags;
	} q3_dshader_t;

	// planes (x&~1) and (x&~1)+1 are allways opposites

	typedef struct {
		float		normal[3];
		float		dist;
	} q3_dplane_t;

	typedef struct {
		int			planeNum;
		int			children[2];	// negative numbers are -(leafs+1), not nodes
		int			mins[3];		// for frustom culling
		int			maxs[3];
	} q3_dnode_t;

	typedef struct {
		int			cluster;			// -1 = opaque cluster (do I still store these?)
		int			area;

		int			mins[3];			// for frustum culling
		int			maxs[3];

		int			firstLeafSurface;
		int			numLeafSurfaces;

		int			firstLeafBrush;
		int			numLeafBrushes;
	} q3_dleaf_t;

	typedef struct {
		int			planeNum;			// positive plane side faces out of the leaf
		int			shaderNum;
	} q3_dbrushside_t;

	typedef struct {
		int			firstSide;
		int			numSides;
		int			shaderNum;		// the shader that determines the contents flags
	} q3_dbrush_t;

	typedef struct {
		char		shader[MAX_QPATH];
		int			brushNum;
		int			visibleSide;	// the brush side that ray tests need to clip against (-1 == none)
	} q3_dfog_t;

	typedef struct {
		vec3_t		xyz;
		float		st[2];
		float		lightmap[2];
		vec3_t		normal;
		byte		color[4];
	} q3_drawVert_t;

	typedef enum {
		MST_BAD,
		MST_PLANAR,
		MST_PATCH,
		MST_TRIANGLE_SOUP,
		MST_FLARE
	} q3_mapSurfaceType_t;

	typedef struct {
		int			shaderNum;
		int			fogNum;
		int			surfaceType;

		int			firstVert;
		int			numVerts;

		int			firstIndex;
		int			numIndexes;

		int			lightmapNum;
		int			lightmapX, lightmapY;
		int			lightmapWidth, lightmapHeight;

		vec3_t		lightmapOrigin;
		vec3_t		lightmapVecs[3];	// for patches, [0] and [1] are lodbounds

		int			patchWidth;
		int			patchHeight;
	} q3_dsurface_t;

	// RNGO!
	// Needed after removing the size parameter in copy lump.
	// Maybe it will also make our lives easier now that it is an explicit structure.
	struct q3_dgridpoint_t
	{
		byte ambient[3];
		byte directed[3];
		byte longitude;
		byte latitude;
	};

	// RNGO!
	// BSP loading relies on these structures having specific sizes.
	static_assert(sizeof(int)				== 4);
	static_assert(sizeof(float)				== 4);
	static_assert(sizeof(q3_lump_t)			== 8);
	static_assert(sizeof(q3_dheader_t)		== 144);
	static_assert(sizeof(q3_dmodel_t)		== 40);
	static_assert(sizeof(q3_dshader_t)		== 72);
	static_assert(sizeof(q3_dleaf_t)		== 48);
	static_assert(sizeof(q3_dplane_t)		== 16);
	static_assert(sizeof(q3_dnode_t)		== 36);
	static_assert(sizeof(q3_dbrush_t)		== 12);
	static_assert(sizeof(q3_dbrushside_t)	== 8);
	static_assert(sizeof(q3_drawVert_t)		== 44);
	static_assert(sizeof(q3_dsurface_t)		== 104);
	static_assert(sizeof(q3_dfog_t)			== 72);
	static_assert(sizeof(q3_dgridpoint_t)	== 8);

	// RNGO!
	// Reading binary files is annoying as shit so just keep it loaded in memory.
	struct BSP
	{
		std::vector<q3_dmodel_t>		q3_dmodels;
		std::vector<q3_dshader_t>		q3_dshaders;
		std::vector<char>				q3_dentdata;
		std::vector<q3_dleaf_t>			q3_dleafs;
		std::vector<q3_dplane_t>		q3_dplanes;
		std::vector<q3_dnode_t>			q3_dnodes;
		std::vector<int>				q3_dleafsurfaces;
		std::vector<int>				q3_dleafbrushes;
		std::vector<q3_dbrush_t>		q3_dbrushes;
		std::vector<q3_dbrushside_t>	q3_dbrushsides;
		std::vector<byte>				q3_lightBytes;
		std::vector<q3_dgridpoint_t>	q3_gridData;
		std::vector<byte>				q3_visBytes;
		std::vector<q3_drawVert_t>		q3_drawVerts;
		std::vector<int>				q3_drawIndexes;
		std::vector<q3_dsurface_t>		q3_drawSurfaces;
		std::vector<q3_dfog_t>			q3_dfogs;
	};

	godot::Error Q3_LoadBSPFile(const godot::String &p_path, BSP &r_data);
}