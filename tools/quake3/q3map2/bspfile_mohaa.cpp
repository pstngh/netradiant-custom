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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <unordered_map>
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

constexpr int MOHAA_LIGHTGRID_PALETTE_COLORS = 256;
constexpr int MOHAA_LIGHTGRID_PALETTE_BYTES = MOHAA_LIGHTGRID_PALETTE_COLORS * 3;
constexpr int MOHAA_LIGHTGRID_SPACING = 32;
constexpr std::size_t MOHAA_LIGHTGRID_MAX_BYTES = 0x800000;

struct MOHAALightGridLumps
{
	std::vector<byte> palette;
	std::vector<byte> offsets;
	std::vector<byte> data;
};

MOHAALightGridLumps mohaaLightGrid;

std::array<int, 3> MOHAALightGridBounds(){
	if ( bspModels.empty() ) {
		Error( "MOHAALightGridBounds: MOHAA BSP contains no world model" );
	}

	std::array<int, 3> bounds{};
	for ( int axis = 0; axis < 3; ++axis )
	{
		const double minimum = std::ceil(
		    static_cast<double>( bspModels[0].minmax.mins[axis] ) / MOHAA_LIGHTGRID_SPACING
		);
		const double maximum = std::floor(
		    static_cast<double>( bspModels[0].minmax.maxs[axis] ) / MOHAA_LIGHTGRID_SPACING
		);
		const double count = maximum - minimum + 1.0;
		if ( count <= 0.0 || count > static_cast<double>( std::numeric_limits<int>::max() ) ) {
			Error( "MOHAA light grid has invalid bounds on axis %d", axis );
		}
		bounds[axis] = static_cast<int>( count );
	}
	return bounds;
}

std::uint16_t ReadLittleUnsignedShort( const std::vector<byte>& bytes, std::size_t index ){
	const std::size_t offset = index * 2;
	return static_cast<std::uint16_t>(
	    static_cast<std::uint16_t>( bytes[offset] ) |
	    ( static_cast<std::uint16_t>( bytes[offset + 1] ) << 8 )
	);
}

void AppendLittleUnsignedShort( std::vector<byte>& bytes, std::uint16_t value ){
	bytes.push_back( static_cast<byte>( value & 0xff ) );
	bytes.push_back( static_cast<byte>( value >> 8 ) );
}

std::array<byte, 3> MOHAAGridPointColor( const bspGridPoint_t& point ){
	std::array<byte, 3> color{};
	for ( int channel = 0; channel < 3; ++channel )
	{
		color[channel] = static_cast<byte>( std::min(
		    255,
		    static_cast<int>( point.ambient[0][channel] ) +
		        static_cast<int>( point.directed[0][channel] )
		) );
	}
	return color;
}

struct MOHAAHistogramCell
{
	std::uint64_t count = 0;
	std::array<std::uint64_t, 3> sum{};
};

struct MOHAAQuantizedColor
{
	std::array<int, 3> color{};
	std::uint64_t count = 0;
	int histogramIndex = 0;
};

struct MOHAAColorBox
{
	std::vector<int> colors;
	std::array<int, 3> minimum{};
	std::array<int, 3> maximum{};
	std::uint64_t count = 0;
};

void UpdateMOHAAColorBox(
    MOHAAColorBox& box,
    const std::vector<MOHAAQuantizedColor>& colors ){
	box.minimum.fill( 255 );
	box.maximum.fill( 0 );
	box.count = 0;
	for ( const int index : box.colors )
	{
		const MOHAAQuantizedColor& color = colors[index];
		box.count += color.count;
		for ( int channel = 0; channel < 3; ++channel )
		{
			box.minimum[channel] = std::min( box.minimum[channel], color.color[channel] );
			box.maximum[channel] = std::max( box.maximum[channel], color.color[channel] );
		}
	}
}

int MOHAAColorBoxSplitChannel( const MOHAAColorBox& box ){
	int channel = 0;
	for ( int candidate = 1; candidate < 3; ++candidate )
	{
		if ( box.maximum[candidate] - box.minimum[candidate] >
		     box.maximum[channel] - box.minimum[channel] ) {
			channel = candidate;
		}
	}
	return channel;
}

std::array<byte, 3> MOHAAColorBoxAverage(
    const MOHAAColorBox& box,
    const std::vector<MOHAAQuantizedColor>& colors ){
	std::array<std::uint64_t, 3> sum{};
	for ( const int index : box.colors )
	{
		const MOHAAQuantizedColor& color = colors[index];
		for ( int channel = 0; channel < 3; ++channel )
			sum[channel] += static_cast<std::uint64_t>( color.color[channel] ) * color.count;
	}

	std::array<byte, 3> average{};
	for ( int channel = 0; channel < 3; ++channel )
		average[channel] = static_cast<byte>( ( sum[channel] + box.count / 2 ) / box.count );
	return average;
}

std::vector<byte> BuildMOHAALightGridPalette(
    std::array<byte, 1 << 15>& histogramToPalette ){
	std::array<MOHAAHistogramCell, 1 << 15> histogram{};
	for ( const bspGridPoint_t& point : bspGridPoints )
	{
		const auto color = MOHAAGridPointColor( point );
		if ( color[0] == 0 && color[1] == 0 && color[2] == 0 ) {
			continue;
		}

		const int histogramIndex =
		    ( static_cast<int>( color[0] ) >> 3 ) |
		    ( ( static_cast<int>( color[1] ) >> 3 ) << 5 ) |
		    ( ( static_cast<int>( color[2] ) >> 3 ) << 10 );
		MOHAAHistogramCell& cell = histogram[histogramIndex];
		++cell.count;
		for ( int channel = 0; channel < 3; ++channel )
			cell.sum[channel] += color[channel];
	}

	std::vector<MOHAAQuantizedColor> colors;
	colors.reserve( histogram.size() );
	for ( std::size_t i = 0; i < histogram.size(); ++i )
	{
		const MOHAAHistogramCell& cell = histogram[i];
		if ( cell.count == 0 ) {
			continue;
		}

		MOHAAQuantizedColor& color = colors.emplace_back();
		color.count = cell.count;
		color.histogramIndex = static_cast<int>( i );
		for ( int channel = 0; channel < 3; ++channel )
			color.color[channel] = static_cast<int>( ( cell.sum[channel] + cell.count / 2 ) / cell.count );
	}

	std::vector<MOHAAColorBox> boxes;
	if ( !colors.empty() ) {
		MOHAAColorBox& initial = boxes.emplace_back();
		initial.colors.resize( colors.size() );
		std::iota( initial.colors.begin(), initial.colors.end(), 0 );
		UpdateMOHAAColorBox( initial, colors );
	}

	while ( boxes.size() < MOHAA_LIGHTGRID_PALETTE_COLORS - 1 )
	{
		std::size_t splitBox = boxes.size();
		std::uint64_t bestScore = 0;
		for ( std::size_t i = 0; i < boxes.size(); ++i )
		{
			const MOHAAColorBox& box = boxes[i];
			if ( box.colors.size() < 2 ) {
				continue;
			}
			const int channel = MOHAAColorBoxSplitChannel( box );
			const std::uint64_t score =
			    static_cast<std::uint64_t>( box.maximum[channel] - box.minimum[channel] + 1 ) *
			    box.count;
			if ( splitBox == boxes.size() || score > bestScore ) {
				splitBox = i;
				bestScore = score;
			}
		}
		if ( splitBox == boxes.size() ) {
			break;
		}

		MOHAAColorBox box = std::move( boxes[splitBox] );
		const int channel = MOHAAColorBoxSplitChannel( box );
		std::stable_sort(
		    box.colors.begin(), box.colors.end(),
		    [&colors, channel]( int left, int right ){
			    if ( colors[left].color[channel] != colors[right].color[channel] ) {
				    return colors[left].color[channel] < colors[right].color[channel];
			    }
			    return colors[left].histogramIndex < colors[right].histogramIndex;
		    }
		);

		const std::uint64_t half = ( box.count + 1 ) / 2;
		std::uint64_t accumulated = 0;
		std::size_t split = 0;
		while ( split + 1 < box.colors.size() )
		{
			accumulated += colors[box.colors[split]].count;
			++split;
			if ( accumulated >= half ) {
				break;
			}
		}

		MOHAAColorBox left;
		MOHAAColorBox right;
		left.colors.assign( box.colors.begin(), box.colors.begin() + split );
		right.colors.assign( box.colors.begin() + split, box.colors.end() );
		UpdateMOHAAColorBox( left, colors );
		UpdateMOHAAColorBox( right, colors );
		boxes[splitBox] = std::move( left );
		boxes.push_back( std::move( right ) );
	}

	std::vector<byte> palette( MOHAA_LIGHTGRID_PALETTE_BYTES, 0 );
	for ( std::size_t i = 0; i < boxes.size(); ++i )
	{
		const auto average = MOHAAColorBoxAverage( boxes[i], colors );
		const byte paletteIndex = static_cast<byte>( i + 1 );
		for ( int channel = 0; channel < 3; ++channel )
			palette[static_cast<std::size_t>( paletteIndex ) * 3 + channel] = average[channel];
		for ( const int colorIndex : boxes[i].colors )
			histogramToPalette[colors[colorIndex].histogramIndex] = paletteIndex;
	}
	return palette;
}

std::vector<byte> CompressMOHAALightGridColumn( const std::vector<byte>& samples ){
	std::vector<byte> compressed;
	compressed.reserve( samples.size() + 1 );

	std::size_t position = 0;
	while ( position < samples.size() )
	{
		std::size_t repeated = 1;
		while ( position + repeated < samples.size() &&
		        samples[position + repeated] == samples[position] &&
		        repeated < 129 ) {
			++repeated;
		}

		if ( repeated >= 2 ) {
			compressed.push_back( static_cast<byte>( repeated - 2 ) );
			compressed.push_back( samples[position] );
			position += repeated;
			continue;
		}

		const std::size_t literalStart = position++;
		while ( position < samples.size() && position - literalStart < 128 )
		{
			std::size_t nextRepeated = 1;
			while ( position + nextRepeated < samples.size() &&
			        samples[position + nextRepeated] == samples[position] &&
			        nextRepeated < 2 ) {
				++nextRepeated;
			}
			if ( nextRepeated >= 2 ) {
				break;
			}
			++position;
		}

		const std::size_t literalLength = position - literalStart;
		compressed.push_back( static_cast<byte>( 0 - static_cast<int>( literalLength ) ) );
		compressed.insert(
		    compressed.end(),
		    samples.begin() + literalStart,
		    samples.begin() + position
		);
	}
	return compressed;
}

std::uint64_t HashMOHAALightGridColumn( const std::vector<byte>& column ){
	std::uint64_t hash = UINT64_C( 14695981039346656037 );
	for ( const byte value : column )
	{
		hash ^= value;
		hash *= UINT64_C( 1099511628211 );
	}
	return hash;
}

struct MOHAAStoredGridColumn
{
	std::uint32_t offset;
	std::uint32_t length;
};

void BuildMOHAALightGrid(){
	const std::array<int, 3> bounds = MOHAALightGridBounds();
	const std::size_t width = static_cast<std::size_t>( bounds[0] );
	const std::size_t height = static_cast<std::size_t>( bounds[1] );
	const std::size_t depth = static_cast<std::size_t>( bounds[2] );
	if ( width > std::numeric_limits<std::size_t>::max() / height ||
	     width * height > std::numeric_limits<std::size_t>::max() / depth ||
	     width * height * depth != bspGridPoints.size() ) {
		Error(
		    "MOHAA light grid size mismatch: runtime expects %d x %d x %d, compiler produced %zu points",
		    bounds[0], bounds[1], bounds[2], bspGridPoints.size()
		);
	}

	std::array<byte, 1 << 15> histogramToPalette{};
	mohaaLightGrid.palette = BuildMOHAALightGridPalette( histogramToPalette );
	mohaaLightGrid.data.clear();

	std::vector<std::uint32_t> columnOffsets( width * height );
	std::unordered_map<std::uint64_t, std::vector<MOHAAStoredGridColumn>> storedColumns;
	std::vector<byte> samples( depth );
	for ( std::size_t x = 0; x < width; ++x )
	{
		std::uint32_t sliceMinimum = std::numeric_limits<std::uint32_t>::max();
		std::uint32_t sliceMaximum = 0;
		for ( std::size_t y = 0; y < height; ++y )
		{
			for ( std::size_t z = 0; z < depth; ++z )
			{
				const bspGridPoint_t& point =
				    bspGridPoints[x + y * width + z * width * height];
				const auto color = MOHAAGridPointColor( point );
				if ( color[0] == 0 && color[1] == 0 && color[2] == 0 ) {
					samples[z] = 0;
				}
				else
				{
					const int histogramIndex =
					    ( static_cast<int>( color[0] ) >> 3 ) |
					    ( ( static_cast<int>( color[1] ) >> 3 ) << 5 ) |
					    ( ( static_cast<int>( color[2] ) >> 3 ) << 10 );
					samples[z] = histogramToPalette[histogramIndex];
				}
			}

			const std::vector<byte> compressed = CompressMOHAALightGridColumn( samples );
			const std::uint64_t hash = HashMOHAALightGridColumn( compressed );
			std::uint32_t offset = std::numeric_limits<std::uint32_t>::max();
			auto& candidates = storedColumns[hash];
			for ( auto candidate = candidates.rbegin(); candidate != candidates.rend(); ++candidate )
			{
				const std::uint32_t candidateMinimum = std::min( sliceMinimum, candidate->offset );
				const std::uint32_t candidateMaximum = std::max( sliceMaximum, candidate->offset );
				if ( candidateMaximum - candidateMinimum > std::numeric_limits<std::uint16_t>::max() ||
				     candidate->length != compressed.size() ) {
					continue;
				}
				if ( std::equal(
				         compressed.begin(), compressed.end(),
				         mohaaLightGrid.data.begin() + candidate->offset
				     ) ) {
					offset = candidate->offset;
					break;
				}
			}

			if ( offset == std::numeric_limits<std::uint32_t>::max() ) {
				if ( mohaaLightGrid.data.size() > std::numeric_limits<std::uint32_t>::max() ) {
					Error( "MOHAA light grid data exceeds addressable offset range" );
				}
				offset = static_cast<std::uint32_t>( mohaaLightGrid.data.size() );
				const std::uint32_t candidateMinimum = std::min( sliceMinimum, offset );
				const std::uint32_t candidateMaximum = std::max( sliceMaximum, offset );
				if ( candidateMaximum - candidateMinimum > std::numeric_limits<std::uint16_t>::max() ) {
					Error(
					    "MOHAA light grid x slice %zu exceeds the 16-bit relative offset range",
					    x
					);
				}
				mohaaLightGrid.data.insert(
				    mohaaLightGrid.data.end(), compressed.begin(), compressed.end()
				);
				candidates.push_back( {
					offset,
					static_cast<std::uint32_t>( compressed.size() )
				} );
			}

			sliceMinimum = std::min( sliceMinimum, offset );
			sliceMaximum = std::max( sliceMaximum, offset );
			columnOffsets[x * height + y] = offset;
		}
	}

	if ( mohaaLightGrid.data.empty() ) {
		Error( "MOHAA light grid compressor produced no row data" );
	}
	if ( mohaaLightGrid.data.size() > MOHAA_LIGHTGRID_MAX_BYTES ) {
		Error(
		    "MOHAA light grid data is %zu bytes (maximum %zu)",
		    mohaaLightGrid.data.size(), MOHAA_LIGHTGRID_MAX_BYTES
		);
	}

	mohaaLightGrid.offsets.clear();
	mohaaLightGrid.offsets.reserve( ( width + width * height ) * 2 );
	for ( std::size_t x = 0; x < width; ++x )
	{
		const auto begin = columnOffsets.begin() + x * height;
		const std::uint32_t minimum = *std::min_element( begin, begin + height );
		const std::uint32_t high = minimum >> 8;
		if ( high > std::numeric_limits<std::uint16_t>::max() ) {
			Error( "MOHAA light grid data offset exceeds the 16-bit page range" );
		}
		AppendLittleUnsignedShort( mohaaLightGrid.offsets, static_cast<std::uint16_t>( high ) );
	}
	for ( std::size_t x = 0; x < width; ++x )
	{
		const std::uint32_t base =
		    static_cast<std::uint32_t>( ReadLittleUnsignedShort( mohaaLightGrid.offsets, x ) ) << 8;
		for ( std::size_t y = 0; y < height; ++y )
		{
			const std::uint32_t offset = columnOffsets[x * height + y];
			if ( offset < base || offset - base > std::numeric_limits<std::uint16_t>::max() ) {
				Error( "MOHAA light grid column offset cannot be represented" );
			}
			AppendLittleUnsignedShort(
			    mohaaLightGrid.offsets,
			    static_cast<std::uint16_t>( offset - base )
			);
		}
	}

	Sys_Printf(
	    "MOHAA light grid: %d x %d x %d, %zu palette bytes, %zu offset bytes, %zu data bytes\n",
	    bounds[0], bounds[1], bounds[2],
	    mohaaLightGrid.palette.size(),
	    mohaaLightGrid.offsets.size(),
	    mohaaLightGrid.data.size()
	);
}

void ValidateMOHAALightGrid( const char* context ){
	const bool hasPalette = !mohaaLightGrid.palette.empty();
	const bool hasOffsets = !mohaaLightGrid.offsets.empty();
	const bool hasData = !mohaaLightGrid.data.empty();
	if ( !hasPalette && !hasOffsets && !hasData ) {
		return;
	}
	if ( !hasPalette || !hasOffsets || !hasData ) {
		Error( "%s: MOHAA light grid has incomplete palette, offset, or row data", context );
	}
	if ( mohaaLightGrid.palette.size() != MOHAA_LIGHTGRID_PALETTE_BYTES ) {
		Error(
		    "%s: MOHAA light grid palette has invalid size %zu (expected %d)",
		    context, mohaaLightGrid.palette.size(), MOHAA_LIGHTGRID_PALETTE_BYTES
		);
	}
	if ( mohaaLightGrid.data.size() > MOHAA_LIGHTGRID_MAX_BYTES ) {
		Error(
		    "%s: MOHAA light grid data has invalid size %zu (maximum %zu)",
		    context, mohaaLightGrid.data.size(), MOHAA_LIGHTGRID_MAX_BYTES
		);
	}

	const std::array<int, 3> bounds = MOHAALightGridBounds();
	const std::size_t width = static_cast<std::size_t>( bounds[0] );
	const std::size_t height = static_cast<std::size_t>( bounds[1] );
	const std::size_t depth = static_cast<std::size_t>( bounds[2] );
	const std::size_t expectedOffsets = ( width + width * height ) * 2;
	if ( mohaaLightGrid.offsets.size() != expectedOffsets ) {
		Error(
		    "%s: MOHAA light grid offsets have invalid size %zu (expected %zu)",
		    context, mohaaLightGrid.offsets.size(), expectedOffsets
		);
	}

	for ( std::size_t x = 0; x < width; ++x )
	{
		const std::size_t high = static_cast<std::size_t>(
		    ReadLittleUnsignedShort( mohaaLightGrid.offsets, x )
		) << 8;
		for ( std::size_t y = 0; y < height; ++y )
		{
			std::size_t offset = high + ReadLittleUnsignedShort(
			    mohaaLightGrid.offsets,
			    width + x * height + y
			);
			std::size_t decoded = 0;
			while ( decoded < depth )
			{
				if ( offset >= mohaaLightGrid.data.size() ) {
					Error(
					    "%s: MOHAA light grid column %zu,%zu points outside row data",
					    context, x, y
					);
				}
				const int markerByte = mohaaLightGrid.data[offset];
				const int marker = markerByte < 128 ? markerByte : markerByte - 256;
				const std::size_t runLength =
				    marker >= 0
				        ? static_cast<std::size_t>( marker ) + 2
				        : static_cast<std::size_t>( -marker );
				const std::size_t encodedLength = marker >= 0 ? 2 : runLength + 1;
				if ( offset > mohaaLightGrid.data.size() ||
				     encodedLength > mohaaLightGrid.data.size() - offset ) {
					Error(
					    "%s: MOHAA light grid column %zu,%zu has truncated row data",
					    context, x, y
					);
				}
				offset += encodedLength;
				decoded += runLength;
			}
		}
	}
}

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

	if ( !bspGridPoints.empty() ) {
		BuildMOHAALightGrid();
	}
	ValidateMOHAALightGrid( context );
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
	CopyMOHAALump( fileData, header, LUMP_LIGHTGRIDPALETTE, mohaaLightGrid.palette );
	CopyMOHAALump( fileData, header, LUMP_LIGHTGRIDOFFSETS, mohaaLightGrid.offsets );
	CopyMOHAALump( fileData, header, LUMP_LIGHTGRIDDATA, mohaaLightGrid.data );

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
	const std::vector<byte> emptyLump;

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
	AddLump(
	    file, header.lumps[LUMP_LIGHTGRIDPALETTE],
	    noGridLighting ? emptyLump : mohaaLightGrid.palette
	);
	AddLump(
	    file, header.lumps[LUMP_LIGHTGRIDOFFSETS],
	    noGridLighting ? emptyLump : mohaaLightGrid.offsets
	);
	AddLump(
	    file, header.lumps[LUMP_LIGHTGRIDDATA],
	    noGridLighting ? emptyLump : mohaaLightGrid.data
	);
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
