/*
   Copyright (C) 2026 NetRadiant Custom contributors.

   This file is part of NetRadiant Custom.

   NetRadiant Custom is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include "q3map2.h"
#include "bspfile_abstract.h"
#include "bspfile_mohaa.h"

#include <array>
#include <cstring>
#include <vector>

namespace
{
enum MOHAALump
{
	LUMP_SHADERS,
	LUMP_PLANES,
	LUMP_LIGHTMAPS,
	LUMP_SURFACES,
	LUMP_DRAWVERTS,
	LUMP_DRAWINDEXES,
	LUMP_LEAFBRUSHES,
	LUMP_LEAFSURFACES,
	LUMP_LEAFS,
	LUMP_NODES,
	LUMP_SIDEEQUATIONS,
	LUMP_BRUSHSIDES,
	LUMP_BRUSHES,
	LUMP_MODELS,
	LUMP_ENTITIES,
	LUMP_VISIBILITY,
	LUMP_LIGHTGRIDPALETTE,
	LUMP_LIGHTGRIDOFFSETS,
	LUMP_LIGHTGRIDDATA,
	LUMP_SPHERELIGHTS,
	LUMP_SPHERELIGHTVIS,
	LUMP_LIGHTDEFS,
	LUMP_TERRAIN,
	LUMP_TERRAININDEXES,
	LUMP_STATICMODELDATA,
	LUMP_STATICMODELDEF,
	LUMP_STATICMODELINDEXES,
	LUMP_DUMMY10,
	MOHAA_HEADER_LUMPS
};

struct MOHAAHeader
{
	char ident[4];
	int version;
	int checksum;
	bspLump_t lumps[MOHAA_HEADER_LUMPS];
};

struct MOHAAShader
{
	char shader[MAX_QPATH];
	int surfaceFlags;
	int contentFlags;
	int subdivisions;
	char fenceMaskImage[MAX_QPATH];

	MOHAAShader( const bspShader_t& other )
		: surfaceFlags( other.surfaceFlags ),
		  contentFlags( other.contentFlags ),
		  subdivisions( 16 ){
		std::memcpy( shader, other.shader, sizeof( shader ) );
		std::memset( fenceMaskImage, 0, sizeof( fenceMaskImage ) );
	}

	operator bspShader_t() const {
		bspShader_t result{};
		std::memcpy( result.shader, shader, sizeof( result.shader ) );
		result.surfaceFlags = surfaceFlags;
		result.contentFlags = contentFlags;
		return result;
	}
};

struct MOHAALeaf
{
	int cluster;
	int area;
	MinMax___<int> minmax;
	int firstLeafSurface;
	int numLeafSurfaces;
	int firstLeafBrush;
	int numLeafBrushes;
	int firstTerrainPatch;
	int numTerrainPatches;
	int firstStaticModel;
	int numStaticModels;

	MOHAALeaf( const bspLeaf_t& other )
		: cluster( other.cluster ),
		  area( other.area ),
		  minmax( other.minmax ),
		  firstLeafSurface( other.firstBSPLeafSurface ),
		  numLeafSurfaces( other.numBSPLeafSurfaces ),
		  firstLeafBrush( other.firstBSPLeafBrush ),
		  numLeafBrushes( other.numBSPLeafBrushes ),
		  firstTerrainPatch( 0 ),
		  numTerrainPatches( 0 ),
		  firstStaticModel( 0 ),
		  numStaticModels( 0 ){
	}

	operator bspLeaf_t() const {
		return {
			.cluster = cluster,
			.area = area,
			.minmax = minmax,
			.firstBSPLeafSurface = firstLeafSurface,
			.numBSPLeafSurfaces = numLeafSurfaces,
			.firstBSPLeafBrush = firstLeafBrush,
			.numBSPLeafBrushes = numLeafBrushes
		};
	}
};

struct MOHAABrushSide
{
	int planeNum;
	int shaderNum;
	int equationNum;

	MOHAABrushSide( const bspBrushSide_t& other )
		: planeNum( other.planeNum ),
		  shaderNum( other.shaderNum ),
		  equationNum( 0 ){
	}

	operator bspBrushSide_t() const {
		return { planeNum, shaderNum, -1 };
	}
};

struct MOHAADrawVert
{
	Vector3 xyz;
	Vector2 st;
	Vector2 lightmap;
	Vector3 normal;
	Color4b color;

	MOHAADrawVert( const bspDrawVert_t& other )
		: xyz( other.xyz ),
		  st( other.st ),
		  lightmap( other.lightmap[0] ),
		  normal( other.normal ),
		  color( other.color[0] ){
	}

	operator bspDrawVert_t() const {
		return {
			.xyz = xyz,
			.st = st,
			.lightmap{ lightmap, Vector2( 0 ), Vector2( 0 ), Vector2( 0 ) },
			.normal = normal,
			.color{ color, Color4b( 0 ), Color4b( 0 ), Color4b( 0 ) }
		};
	}
};

struct MOHAADrawSurface
{
	int shaderNum;
	int fogNum;
	bspSurfaceType_t surfaceType;
	int firstVert;
	int numVerts;
	int firstIndex;
	int numIndexes;
	int lightmapNum;
	int lightmapX;
	int lightmapY;
	int lightmapWidth;
	int lightmapHeight;
	Vector3 lightmapOrigin;
	std::array<Vector3, 3> lightmapVecs;
	int patchWidth;
	int patchHeight;
	float subdivisions;

	MOHAADrawSurface( const bspDrawSurface_t& other )
		: shaderNum( other.shaderNum ),
		  fogNum( other.fogNum ),
		  surfaceType( other.surfaceType ),
		  firstVert( other.firstVert ),
		  numVerts( other.numVerts ),
		  firstIndex( other.firstIndex ),
		  numIndexes( other.numIndexes ),
		  lightmapNum( other.lightmapNum[0] ),
		  lightmapX( other.lightmapX[0] ),
		  lightmapY( other.lightmapY[0] ),
		  lightmapWidth( other.lightmapWidth ),
		  lightmapHeight( other.lightmapHeight ),
		  lightmapOrigin( other.lightmapOrigin ),
		  lightmapVecs( other.lightmapVecs ),
		  patchWidth( other.patchWidth ),
		  patchHeight( other.patchHeight ),
		  subdivisions( 16.0f ){
	}

	operator bspDrawSurface_t() const {
		return {
			.shaderNum = shaderNum,
			.fogNum = fogNum,
			.surfaceType = surfaceType,
			.firstVert = firstVert,
			.numVerts = numVerts,
			.firstIndex = firstIndex,
			.numIndexes = numIndexes,
			.lightmapStyles{ LS_NORMAL, LS_NONE, LS_NONE, LS_NONE },
			.vertexStyles{ LS_NORMAL, LS_NONE, LS_NONE, LS_NONE },
			.lightmapNum{ lightmapNum, LIGHTMAP_BY_VERTEX, LIGHTMAP_BY_VERTEX, LIGHTMAP_BY_VERTEX },
			.lightmapX{ lightmapX, 0, 0, 0 },
			.lightmapY{ lightmapY, 0, 0, 0 },
			.lightmapWidth = lightmapWidth,
			.lightmapHeight = lightmapHeight,
			.lightmapOrigin = lightmapOrigin,
			.lightmapVecs = lightmapVecs,
			.patchWidth = patchWidth,
			.patchHeight = patchHeight
		};
	}
};

static_assert( sizeof( MOHAAHeader ) == 236 );
static_assert( sizeof( MOHAAShader ) == 140 );
static_assert( sizeof( MOHAALeaf ) == 64 );
static_assert( sizeof( MOHAABrushSide ) == 12 );
static_assert( sizeof( MOHAADrawVert ) == 44 );
static_assert( sizeof( MOHAADrawSurface ) == 108 );

template<typename Destination, typename Source = Destination>
void CopyMOHAALump( MOHAAHeader *header, MOHAALump lump, std::vector<Destination>& data ){
	const int length = header->lumps[lump].length;
	const int offset = header->lumps[lump].offset;
	if ( length <= 0 ) {
		data.clear();
		return;
	}
	if ( length % sizeof( Source ) != 0 ) {
		Error( "LoadMOHAABSPFile: odd lump size (%d) in lump %d", length, lump );
	}
	const Source *begin = reinterpret_cast<const Source*>( reinterpret_cast<const byte*>( header ) + offset );
	data = { begin, begin + length / sizeof( Source ) };
}
}

void LoadMOHAABSPFile( const char *filename ){
	MemBuffer file = LoadFile( filename );
	MOHAAHeader *header = file.data();

	SwapBlock( reinterpret_cast<int*>( reinterpret_cast<byte*>( header ) + 4 ), sizeof( *header ) - 4 );

	if ( !force && std::memcmp( header->ident, g_game->bspIdent, 4 ) != 0 ) {
		Error( "%s is not a MOHAA 2015 BSP file", filename );
	}
	if ( !force && header->version != g_game->bspVersion ) {
		Error( "%s is version %d, not %d", filename, header->version, g_game->bspVersion );
	}

	CopyMOHAALump<bspShader_t, MOHAAShader>( header, LUMP_SHADERS, bspShaders );
	CopyMOHAALump( header, LUMP_MODELS, bspModels );
	CopyMOHAALump( header, LUMP_PLANES, bspPlanes );
	CopyMOHAALump<bspLeaf_t, MOHAALeaf>( header, LUMP_LEAFS, bspLeafs );
	CopyMOHAALump( header, LUMP_NODES, bspNodes );
	CopyMOHAALump( header, LUMP_LEAFSURFACES, bspLeafSurfaces );
	CopyMOHAALump( header, LUMP_LEAFBRUSHES, bspLeafBrushes );
	CopyMOHAALump( header, LUMP_BRUSHES, bspBrushes );
	CopyMOHAALump<bspBrushSide_t, MOHAABrushSide>( header, LUMP_BRUSHSIDES, bspBrushSides );
	CopyMOHAALump<bspDrawVert_t, MOHAADrawVert>( header, LUMP_DRAWVERTS, bspDrawVerts );
	CopyMOHAALump<bspDrawSurface_t, MOHAADrawSurface>( header, LUMP_SURFACES, bspDrawSurfaces );
	CopyMOHAALump( header, LUMP_DRAWINDEXES, bspDrawIndexes );
	CopyMOHAALump( header, LUMP_VISIBILITY, bspVisBytes );
	CopyMOHAALump( header, LUMP_LIGHTMAPS, bspLightBytes );
	CopyMOHAALump( header, LUMP_ENTITIES, bspEntData );

	bspFogs.clear();
	bspGridPoints.clear();
	bspAds.clear();
}

void WriteMOHAABSPFile( const char *filename ){
	MOHAAHeader header{};
	std::memcpy( header.ident, g_game->bspIdent, 4 );
	header.version = LittleLong( g_game->bspVersion );
	header.checksum = 0;

	FILE *file = SafeOpenWrite( filename );
	SafeWrite( file, &header, sizeof( header ) );

	AddLump( file, header.lumps[LUMP_SHADERS], std::vector<MOHAAShader>( bspShaders.begin(), bspShaders.end() ) );
	AddLump( file, header.lumps[LUMP_PLANES], bspPlanes );
	AddLump( file, header.lumps[LUMP_LIGHTMAPS], bspLightBytes );
	AddLump( file, header.lumps[LUMP_SURFACES], std::vector<MOHAADrawSurface>( bspDrawSurfaces.begin(), bspDrawSurfaces.end() ) );
	AddLump( file, header.lumps[LUMP_DRAWVERTS], std::vector<MOHAADrawVert>( bspDrawVerts.begin(), bspDrawVerts.end() ) );
	AddLump( file, header.lumps[LUMP_DRAWINDEXES], bspDrawIndexes );
	AddLump( file, header.lumps[LUMP_LEAFBRUSHES], bspLeafBrushes );
	AddLump( file, header.lumps[LUMP_LEAFSURFACES], bspLeafSurfaces );
	AddLump( file, header.lumps[LUMP_LEAFS], std::vector<MOHAALeaf>( bspLeafs.begin(), bspLeafs.end() ) );
	AddLump( file, header.lumps[LUMP_NODES], bspNodes );
	AddLump( file, header.lumps[LUMP_SIDEEQUATIONS], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_BRUSHSIDES], std::vector<MOHAABrushSide>( bspBrushSides.begin(), bspBrushSides.end() ) );
	AddLump( file, header.lumps[LUMP_BRUSHES], bspBrushes );
	AddLump( file, header.lumps[LUMP_MODELS], bspModels );
	AddLump( file, header.lumps[LUMP_ENTITIES], bspEntData );
	AddLump( file, header.lumps[LUMP_VISIBILITY], bspVisBytes );
	AddLump( file, header.lumps[LUMP_LIGHTGRIDPALETTE], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_LIGHTGRIDOFFSETS], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_LIGHTGRIDDATA], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_SPHERELIGHTS], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_SPHERELIGHTVIS], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_LIGHTDEFS], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_TERRAIN], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_TERRAININDEXES], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_STATICMODELDATA], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_STATICMODELDEF], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_STATICMODELINDEXES], std::vector<byte>() );
	AddLump( file, header.lumps[LUMP_DUMMY10], std::vector<byte>() );

	fseek( file, 0, SEEK_SET );
	SafeWrite( file, &header, sizeof( header ) );
	fclose( file );
}
