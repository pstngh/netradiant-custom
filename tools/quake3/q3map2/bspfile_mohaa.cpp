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
	ValidateMOHAACount( "entity bytes", bspEntData.size(), 0x100000 );
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

void ValidateMOHAABSPFile( const char *context ){
	const auto validateRange = [context](
	    const char* owner,
	    std::size_t ownerIndex,
	    const char* target,
	    int first,
	    int count,
	    std::size_t targetSize ){
		if ( first < 0 || count < 0 ||
		     static_cast<std::size_t>( first ) > targetSize ||
		     static_cast<std::size_t>( count ) > targetSize - static_cast<std::size_t>( first ) ) {
			Error(
			    "%s: %s %zu has invalid %s range (%d + %d, total %zu)",
			    context, owner, ownerIndex, target, first, count, targetSize
			);
		}
	};
	const auto validateIndex = [context](
	    const char* owner,
	    std::size_t ownerIndex,
	    const char* target,
	    int index,
	    std::size_t targetSize ){
		if ( index < 0 || static_cast<std::size_t>( index ) >= targetSize ) {
			Error(
			    "%s: %s %zu has invalid %s index %d (total %zu)",
			    context, owner, ownerIndex, target, index, targetSize
			);
		}
	};
	const auto finiteVector = []( const Vector3& vector ){
		return std::isfinite( vector.x() ) &&
		       std::isfinite( vector.y() ) &&
		       std::isfinite( vector.z() );
	};

	ValidateMOHAALimits();
	if ( bspModels.empty() ) {
		Error( "%s: MOHAA BSP contains no world model", context );
	}

	constexpr float maxWorldCoordinate = 128.0f * 128.0f;
	for ( std::size_t i = 0; i < bspModels.size(); ++i )
	{
		const bspModel_t& model = bspModels[i];
		validateRange(
		    "model", i, "draw surface",
		    model.firstBSPSurface, model.numBSPSurfaces, bspDrawSurfaces.size()
		);
		validateRange(
		    "model", i, "brush",
		    model.firstBSPBrush, model.numBSPBrushes, bspBrushes.size()
		);
		if ( !finiteVector( model.minmax.mins ) || !finiteVector( model.minmax.maxs ) ) {
			Error( "%s: model %zu has non-finite bounds", context, i );
		}
		for ( int axis = 0; axis < 3; ++axis )
		{
			if ( model.minmax.mins[axis] > model.minmax.maxs[axis] ||
			     model.minmax.mins[axis] < -maxWorldCoordinate ||
			     model.minmax.maxs[axis] > maxWorldCoordinate ) {
				Error(
				    "%s: model %zu has invalid bounds on axis %d (%f to %f)",
				    context, i, axis, model.minmax.mins[axis], model.minmax.maxs[axis]
				);
			}
		}
	}

	for ( std::size_t i = 0; i < bspPlanes.size(); ++i )
	{
		const bspPlane_t& plane = bspPlanes[i];
		if ( !finiteVector( plane.normal() ) || !std::isfinite( plane.dist() ) ||
		     vector3_length_squared( plane.normal() ) == 0.0f ) {
			Error( "%s: plane %zu is not finite and non-degenerate", context, i );
		}
	}

	for ( std::size_t i = 0; i < bspNodes.size(); ++i )
	{
		const bspNode_t& node = bspNodes[i];
		validateIndex( "node", i, "plane", node.planeNum, bspPlanes.size() );
		for ( int side = 0; side < 2; ++side )
		{
			const int child = node.children[side];
			if ( child >= 0 ) {
				validateIndex( "node", i, "child node", child, bspNodes.size() );
			}
			else
			{
				const std::int64_t leaf = -static_cast<std::int64_t>( child ) - 1;
				if ( leaf < 0 || static_cast<std::size_t>( leaf ) >= bspLeafs.size() ) {
					Error(
					    "%s: node %zu has invalid child leaf index %lld (total %zu)",
					    context, i, static_cast<long long>( leaf ), bspLeafs.size()
					);
				}
			}
		}
	}

	int visClusters = 0;
	if ( !bspVisBytes.empty() ) {
		int visHeader[2];
		std::memcpy( visHeader, bspVisBytes.data(), sizeof( visHeader ) );
		visClusters = visHeader[0];
		const int bytesPerCluster = visHeader[1];
		if ( visClusters < 0 || bytesPerCluster < 0 ||
		     ( visClusters > 0 && bytesPerCluster == 0 ) ||
		     static_cast<std::size_t>( visClusters ) >
		         ( bspVisBytes.size() - sizeof( visHeader ) ) /
		             std::max( 1, bytesPerCluster ) ) {
			Error(
			    "%s: invalid visibility dimensions (%d clusters, %d bytes each, %zu bytes total)",
			    context, visClusters, bytesPerCluster, bspVisBytes.size()
			);
		}
	}

	for ( std::size_t i = 0; i < bspLeafs.size(); ++i )
	{
		const bspLeaf_t& leaf = bspLeafs[i];
		validateRange(
		    "leaf", i, "leaf surface",
		    leaf.firstBSPLeafSurface, leaf.numBSPLeafSurfaces, bspLeafSurfaces.size()
		);
		validateRange(
		    "leaf", i, "leaf brush",
		    leaf.firstBSPLeafBrush, leaf.numBSPLeafBrushes, bspLeafBrushes.size()
		);
		if ( leaf.cluster < -1 || ( !bspVisBytes.empty() && leaf.cluster >= visClusters ) ) {
			Error(
			    "%s: leaf %zu has invalid visibility cluster %d (total %d)",
			    context, i, leaf.cluster, visClusters
			);
		}
	}

	for ( std::size_t i = 0; i < bspLeafSurfaces.size(); ++i )
		validateIndex( "leaf surface", i, "draw surface", bspLeafSurfaces[i], bspDrawSurfaces.size() );
	for ( std::size_t i = 0; i < bspLeafBrushes.size(); ++i )
		validateIndex( "leaf brush", i, "brush", bspLeafBrushes[i], bspBrushes.size() );

	for ( std::size_t i = 0; i < bspBrushes.size(); ++i )
	{
		const bspBrush_t& brush = bspBrushes[i];
		validateRange( "brush", i, "brush side", brush.firstSide, brush.numSides, bspBrushSides.size() );
		validateIndex( "brush", i, "shader", brush.shaderNum, bspShaders.size() );
	}
	for ( std::size_t i = 0; i < bspBrushSides.size(); ++i )
	{
		const bspBrushSide_t& side = bspBrushSides[i];
		validateIndex( "brush side", i, "plane", side.planeNum, bspPlanes.size() );
		validateIndex( "brush side", i, "shader", side.shaderNum, bspShaders.size() );
		if ( side.surfaceNum < -1 ||
		     ( side.surfaceNum >= 0 && static_cast<std::size_t>( side.surfaceNum ) >= bspDrawSurfaces.size() ) ) {
			Error(
			    "%s: brush side %zu has invalid draw surface index %d (total %zu)",
			    context, i, side.surfaceNum, bspDrawSurfaces.size()
			);
		}
	}

	constexpr std::size_t lightmapPageBytes = 128 * 128 * 3;
	if ( bspLightBytes.size() % lightmapPageBytes != 0 ) {
		Error( "%s: lightmap lump has invalid size %zu", context, bspLightBytes.size() );
	}
	const std::size_t lightmapPages = bspLightBytes.size() / lightmapPageBytes;

	for ( std::size_t i = 0; i < bspDrawSurfaces.size(); ++i )
	{
		const bspDrawSurface_t& surface = bspDrawSurfaces[i];
		validateIndex( "draw surface", i, "shader", surface.shaderNum, bspShaders.size() );
		if ( surface.fogNum < -1 ||
		     ( surface.fogNum >= 0 && static_cast<std::size_t>( surface.fogNum ) >= bspFogs.size() ) ) {
			Error(
			    "%s: draw surface %zu has invalid fog index %d (total %zu)",
			    context, i, surface.fogNum, bspFogs.size()
			);
		}
		if ( surface.surfaceType < MST_PLANAR || surface.surfaceType > MST_FOLIAGE ) {
			Error( "%s: draw surface %zu has invalid type %d", context, i, surface.surfaceType );
		}
		validateRange(
		    "draw surface", i, "vertex",
		    surface.firstVert, surface.numVerts, bspDrawVerts.size()
		);
		validateRange(
		    "draw surface", i, "draw index",
		    surface.firstIndex, surface.numIndexes, bspDrawIndexes.size()
		);
		if ( surface.numIndexes % 3 != 0 ) {
			Error( "%s: draw surface %zu has a non-triangular index count %d", context, i, surface.numIndexes );
		}
		for ( int j = 0; j < surface.numIndexes; ++j )
		{
			const int index = bspDrawIndexes[surface.firstIndex + j];
			if ( index < 0 || index >= surface.numVerts ) {
				Error(
				    "%s: draw surface %zu has invalid relative vertex index %d (total %d)",
				    context, i, index, surface.numVerts
				);
			}
		}
		if ( surface.surfaceType == MST_PATCH ) {
			if ( surface.patchWidth < 2 || surface.patchHeight < 2 ||
			     static_cast<std::size_t>( surface.patchWidth ) >
			         static_cast<std::size_t>( surface.numVerts ) /
			             static_cast<std::size_t>( surface.patchHeight ) ||
			     surface.patchWidth * surface.patchHeight != surface.numVerts ) {
				Error(
				    "%s: patch surface %zu has invalid dimensions %dx%d for %d vertices",
				    context, i, surface.patchWidth, surface.patchHeight, surface.numVerts
				);
			}
		}
		if ( surface.lightmapNum[0] >= 0 &&
		     static_cast<std::size_t>( surface.lightmapNum[0] ) >= lightmapPages ) {
			Error(
			    "%s: draw surface %zu has invalid lightmap index %d (total %zu)",
			    context, i, surface.lightmapNum[0], lightmapPages
			);
		}
	}

	for ( std::size_t i = 0; i < bspDrawVerts.size(); ++i )
	{
		if ( !finiteVector( bspDrawVerts[i].xyz ) || !finiteVector( bspDrawVerts[i].normal ) ) {
			Error( "%s: draw vertex %zu has non-finite geometry", context, i );
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
