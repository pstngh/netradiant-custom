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

	MOHAAShader() = default;

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

	MOHAALeaf() = default;

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

	MOHAABrushSide() = default;

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

	MOHAADrawVert() = default;

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

	MOHAADrawSurface() = default;

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

void ValidateMOHAACount( const char* name, std::size_t count, std::size_t limit ){
	if ( count > limit ) {
		Error(
		    "MOHAA BSP limit exceeded for %s: %zu (maximum %zu)",
		    name, count, limit
		);
	}
}

void ValidateMOHAALimits(){
	/* Limits used by OpenMoHAA's qcommon/qfiles.h. */
	ValidateMOHAACount( "models", bspModels.size(), 0x400 );
	ValidateMOHAACount( "shaders", bspShaders.size(), 0x400 );
	ValidateMOHAACount( "planes", bspPlanes.size(), 0x20000 );
	ValidateMOHAACount( "nodes", bspNodes.size(), 0x20000 );
	ValidateMOHAACount( "leafs", bspLeafs.size(), 0x20000 );
	ValidateMOHAACount( "leaf surfaces", bspLeafSurfaces.size(), 0x20000 );
	ValidateMOHAACount( "leaf brushes", bspLeafBrushes.size(), 0x40000 );
	ValidateMOHAACount( "brushes", bspBrushes.size(), 0x8000 );
	ValidateMOHAACount( "brush sides", bspBrushSides.size(), 0x20000 );
	ValidateMOHAACount( "draw surfaces", bspDrawSurfaces.size(), 0x20000 );
	ValidateMOHAACount( "draw vertices", bspDrawVerts.size(), 0x80000 );
	ValidateMOHAACount( "draw indexes", bspDrawIndexes.size(), 0x80000 );
	ValidateMOHAACount( "entity bytes", bspEntData.size(), 0x40000 );
	ValidateMOHAACount( "visibility bytes", bspVisBytes.size(), 0x200000 );
	ValidateMOHAACount( "lightmap bytes", bspLightBytes.size(), 0x800000 );
}

void ValidateMOHAAHeader( const MOHAAHeader& header, std::size_t fileSize ){
	for ( int i = 0; i < MOHAA_HEADER_LUMPS; ++i )
	{
		const int offset = header.lumps[i].offset;
		const int length = header.lumps[i].length;
		if ( offset < 0 || length < 0 ) {
			Error( "LoadMOHAABSPFile: negative offset or length in lump %d", i );
		}

		const std::size_t lumpOffset = static_cast<std::size_t>( offset );
		const std::size_t lumpLength = static_cast<std::size_t>( length );
		if ( lumpOffset > fileSize || lumpLength > fileSize - lumpOffset ) {
			Error(
			    "LoadMOHAABSPFile: lump %d extends past end of file "
			    "(offset %d, length %d, file size %zu)",
			    i, offset, length, fileSize
			);
		}
		if ( i == LUMP_VISIBILITY && length != 0 && length < 2 * static_cast<int>( sizeof( int ) ) ) {
			Error( "LoadMOHAABSPFile: visibility lump is too small (%d bytes)", length );
		}
	}
}

template<typename Destination, typename Source = Destination>
void CopyMOHAALump(
    const byte* fileData,
    const MOHAAHeader& header,
    MOHAALump lump,
    std::vector<Destination>& data ){
	const int length = header.lumps[lump].length;
	const int offset = header.lumps[lump].offset;
	if ( length <= 0 ) {
		data.clear();
		return;
	}
	if ( length % static_cast<int>( sizeof( Source ) ) != 0 ) {
		Error( "LoadMOHAABSPFile: odd lump size (%d) in lump %d", length, lump );
	}

	const std::size_t count = static_cast<std::size_t>( length ) / sizeof( Source );
	data.clear();
	data.reserve( count );
	for ( std::size_t i = 0; i < count; ++i )
	{
		Source source{};
		std::memcpy(
		    static_cast<void*>( &source ),
		    fileData + offset + i * sizeof( Source ),
		    sizeof( Source )
		);
		data.push_back( static_cast<Destination>( source ) );
	}
}
}

void LoadMOHAABSPFile( const char *filename ){
	MemBuffer file = LoadFile( filename );
	if ( file.size() < sizeof( MOHAAHeader ) ) {
		Error(
		    "%s is too small to contain a MOHAA BSP header "
		    "(%zu bytes, expected at least %zu)",
		    filename, file.size(), sizeof( MOHAAHeader )
		);
	}

	const byte* fileData = file.data();
	MOHAAHeader header{};
	std::memcpy( &header, fileData, sizeof( header ) );

	SwapBlock( reinterpret_cast<int*>( reinterpret_cast<byte*>( &header ) + 4 ), sizeof( header ) - 4 );

	if ( !force && std::memcmp( header.ident, g_game->bspIdent, 4 ) != 0 ) {
		Error( "%s is not a MOHAA 2015 BSP file", filename );
	}
	if ( !force && header.version != g_game->bspVersion ) {
		Error( "%s is version %d, not %d", filename, header.version, g_game->bspVersion );
	}

	ValidateMOHAAHeader( header, file.size() );

	CopyMOHAALump<bspShader_t, MOHAAShader>( fileData, header, LUMP_SHADERS, bspShaders );
	CopyMOHAALump( fileData, header, LUMP_MODELS, bspModels );
	CopyMOHAALump( fileData, header, LUMP_PLANES, bspPlanes );
	CopyMOHAALump<bspLeaf_t, MOHAALeaf>( fileData, header, LUMP_LEAFS, bspLeafs );
	CopyMOHAALump( fileData, header, LUMP_NODES, bspNodes );
	CopyMOHAALump( fileData, header, LUMP_LEAFSURFACES, bspLeafSurfaces );
	CopyMOHAALump( fileData, header, LUMP_LEAFBRUSHES, bspLeafBrushes );
	CopyMOHAALump( fileData, header, LUMP_BRUSHES, bspBrushes );
	CopyMOHAALump<bspBrushSide_t, MOHAABrushSide>( fileData, header, LUMP_BRUSHSIDES, bspBrushSides );
	CopyMOHAALump<bspDrawVert_t, MOHAADrawVert>( fileData, header, LUMP_DRAWVERTS, bspDrawVerts );
	CopyMOHAALump<bspDrawSurface_t, MOHAADrawSurface>( fileData, header, LUMP_SURFACES, bspDrawSurfaces );
	CopyMOHAALump( fileData, header, LUMP_DRAWINDEXES, bspDrawIndexes );
	CopyMOHAALump( fileData, header, LUMP_VISIBILITY, bspVisBytes );
	CopyMOHAALump( fileData, header, LUMP_LIGHTMAPS, bspLightBytes );
	CopyMOHAALump( fileData, header, LUMP_ENTITIES, bspEntData );

	for ( const bspShader_t& shader : bspShaders )
	{
		if ( std::memchr( shader.shader, '\0', sizeof( shader.shader ) ) == nullptr ) {
			Error( "LoadMOHAABSPFile: shader name is not null terminated" );
		}
	}
	if ( !bspEntData.empty() && bspEntData.back() != '\0' ) {
		Error( "LoadMOHAABSPFile: entity lump is not null terminated" );
	}
	ValidateMOHAALimits();

	bspFogs.clear();
	bspGridPoints.clear();
	bspAds.clear();
}

void WriteMOHAABSPFile( const char *filename ){
	ValidateMOHAALimits();

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
