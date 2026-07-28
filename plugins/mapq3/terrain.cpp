/*
   Copyright (C) 2026 NetRadiant Custom contributors.

   This file is part of NetRadiant Custom.

   NetRadiant Custom is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
*/

#include "terrain.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "debugging/debugging.h"
#include "editable.h"
#include "igl.h"
#include "imap.h"
#include "irender.h"
#include "iselection.h"
#include "iundo.h"

#include "cullable.h"
#include "instancelib.h"
#include "mapfile.h"
#include "math/aabb.h"
#include "math/frustum.h"
#include "math/matrix.h"
#include "math/vector.h"
#include "render.h"
#include "renderable.h"
#include "scenelib.h"
#include "selectionlib.h"
#include "selectable.h"
#include "shaderlib.h"
#include "stream/stringstream.h"
#include "string/string.h"
#include "stringio.h"
#include "texturelib.h"
#include "transformlib.h"

namespace
{
constexpr double c_mohaaTerrainGrid = 64.0;
constexpr std::size_t c_maxTerrainVertices = 1024 * 1024;

using TokenLine = std::vector<CopiedString>;
using TokenLines = std::vector<TokenLine>;

struct TerrainVertex
{
	Vector3 m_vertex;
	TokenLine m_edgeFlags[2];
};

class MOHAATerrain;

class RenderableTerrainMesh final : public OpenGLRenderable
{
	const MOHAATerrain& m_terrain;
	bool m_wireframe;
public:
	RenderableTerrainMesh( const MOHAATerrain& terrain, bool wireframe )
		: m_terrain( terrain ), m_wireframe( wireframe ){
	}

	void render( RenderStateFlags state ) const override;
};

class MOHAATerrain final :
	public MapImporter,
	public MapExporter,
	public TransformNode,
	public Bounded,
	public Cullable,
	public Snappable,
	public Undoable
{
public:
	class Observer
	{
	public:
		virtual void allocate( std::size_t size ) = 0;
	};

private:
	class SavedState final : public UndoMemento
	{
	public:
		std::size_t m_width;
		std::size_t m_height;
		int m_flags;
		Vector3 m_origin;
		TokenLines m_textureDefinitions;
		std::vector<TerrainVertex> m_vertices;

		SavedState( const MOHAATerrain& terrain )
			: m_width( terrain.m_width ),
			  m_height( terrain.m_height ),
			  m_flags( terrain.m_flags ),
			  m_origin( terrain.m_origin ),
			  m_textureDefinitions( terrain.m_textureDefinitions ),
			  m_vertices( terrain.m_vertices ){
		}

		void release() override {
			delete this;
		}
	};

	scene::Node& m_node;
	std::size_t m_width{};
	std::size_t m_height{};
	int m_flags{};
	Vector3 m_origin;
	TokenLines m_textureDefinitions;
	std::vector<TerrainVertex> m_vertices;
	std::vector<Vector3> m_verticesTransformed;

	std::vector<ArbitraryMeshVertex> m_renderVertices;
	std::vector<RenderIndex> m_triangleIndices;
	std::vector<RenderIndex> m_wireIndices;
	RenderableTerrainMesh m_renderSolid;
	RenderableTerrainMesh m_renderWireframe;

	CopiedString m_shader;
	Shader* m_state{};
	AABB m_aabbLocal;

	std::vector<Observer*> m_observers;
	InstanceCounter m_instanceCounter;
	UndoObserver* m_undoableObserver{};
	MapFile* m_map{};

	bool m_transformChanged{};
	Callback<void()> m_evaluateTransform;
	Callback<void()> m_boundsChanged;

	void captureShader(){
		m_state = GlobalShaderCache().capture( m_shader.c_str() );
	}

	void releaseShader(){
		if ( m_state != nullptr ) {
			GlobalShaderCache().release( m_shader.c_str() );
			m_state = nullptr;
		}
	}

	void setShader( const char* shader ){
		const CopiedString name( string_empty( shader ) ? texdef_name_default() : shader );
		if ( shader_equal( m_shader.c_str(), name.c_str() ) ) {
			return;
		}
		if ( m_instanceCounter.m_count != 0 ) {
			m_state->decrementUsed();
		}
		releaseShader();
		m_shader = name;
		captureShader();
		if ( m_instanceCounter.m_count != 0 ) {
			m_state->incrementUsed();
		}
	}

	void updateShaderFromDefinitions(){
		for ( const TokenLine& line : m_textureDefinitions )
		{
			for ( std::size_t i = 0; i + 1 < line.size(); ++i )
			{
				if ( string_equal( line[i].c_str(), "(" ) ) {
					setShader( StringStream<64>( GlobalTexturePrefix_get(), line[i + 1].c_str() ) );
					return;
				}
			}
		}
		setShader( texdef_name_default() );
	}

	void notifyAllocate(){
		for ( Observer* observer : m_observers )
		{
			observer->allocate( m_vertices.size() );
		}
	}

	void buildRenderData(){
		m_aabbLocal = AABB();
		m_renderVertices.clear();
		m_triangleIndices.clear();
		m_wireIndices.clear();

		m_renderVertices.reserve( m_verticesTransformed.size() );
		std::vector<Vector3> normals( m_verticesTransformed.size(), Vector3( 0, 0, 0 ) );

		for ( std::size_t y = 0; y < m_height; ++y )
		{
			for ( std::size_t x = 0; x < m_width; ++x )
			{
				const std::size_t index = y * m_width + x;
				const Vector3& vertex = m_verticesTransformed[index];
				m_renderVertices.emplace_back(
				    vertex3f_for_vector3( vertex ),
				    Normal3f( 0, 0, 1 ),
				    TexCoord2f( static_cast<float>( x ) / 8.0f, static_cast<float>( y ) / 8.0f )
				);
				m_renderVertices.back().tangent = Normal3f( 1, 0, 0 );
				m_renderVertices.back().bitangent = Normal3f( 0, 1, 0 );
				aabb_extend_by_point_safe( m_aabbLocal, vertex );

				if ( x + 1 < m_width ) {
					m_wireIndices.push_back( RenderIndex( index ) );
					m_wireIndices.push_back( RenderIndex( index + 1 ) );
				}
				if ( y + 1 < m_height ) {
					m_wireIndices.push_back( RenderIndex( index ) );
					m_wireIndices.push_back( RenderIndex( index + m_width ) );
				}
			}
		}

		auto addTriangle = [&]( std::size_t a, std::size_t b, std::size_t c ){
			m_triangleIndices.push_back( RenderIndex( a ) );
			m_triangleIndices.push_back( RenderIndex( b ) );
			m_triangleIndices.push_back( RenderIndex( c ) );

			const Vector3 normal = vector3_cross(
			    vector3_subtracted( m_verticesTransformed[b], m_verticesTransformed[a] ),
			    vector3_subtracted( m_verticesTransformed[c], m_verticesTransformed[a] )
			);
			normals[a] += normal;
			normals[b] += normal;
			normals[c] += normal;
		};

		for ( std::size_t y = 0; y + 1 < m_height; ++y )
		{
			for ( std::size_t x = 0; x + 1 < m_width; ++x )
			{
				const std::size_t a = y * m_width + x;
				const std::size_t b = a + 1;
				const std::size_t c = a + m_width;
				const std::size_t d = c + 1;
				if ( ( x + y ) & 1 ) {
					addTriangle( a, b, d );
					addTriangle( a, d, c );
				}
				else
				{
					addTriangle( a, b, c );
					addTriangle( b, d, c );
				}
			}
		}

		for ( std::size_t i = 0; i < normals.size(); ++i )
		{
			if ( vector3_length_squared( normals[i] ) == 0 ) {
				normals[i] = Vector3( 0, 0, 1 );
			}
			else
			{
				vector3_normalise( normals[i] );
			}
			m_renderVertices[i].normal = normal3f_for_vector3( normals[i] );
		}

		m_boundsChanged();
		SceneChangeNotify();
	}

	bool importTokenGroup( Tokeniser& tokeniser, TokenLine& tokens ){
		RETURN_FALSE_IF_FAIL( Tokeniser_parseToken( tokeniser, "(" ) );
		tokens.clear();
		for ( const char* token = tokeniser.getToken(); ; token = tokeniser.getToken() )
		{
			if ( token == nullptr ) {
				Tokeniser_unexpectedError( tokeniser, token, ")" );
				return false;
			}
			if ( string_equal( token, ")" ) ) {
				return true;
			}
			tokens.emplace_back( token );
		}
	}

	void exportToken( TokenWriter& writer, const CopiedString& token ) const {
		if ( string_empty( token.c_str() ) || strpbrk( token.c_str(), " \t" ) != nullptr ) {
			writer.writeString( token.c_str() );
		}
		else
		{
			writer.writeToken( token.c_str() );
		}
	}

public:
	MOHAATerrain( scene::Node& node, const Callback<void()>& evaluateTransform, const Callback<void()>& boundsChanged )
		: m_node( node ),
		  m_origin( 0, 0, 0 ),
		  m_verticesTransformed(),
		  m_renderSolid( *this, false ),
		  m_renderWireframe( *this, true ),
		  m_shader( texdef_name_default() ),
		  m_evaluateTransform( evaluateTransform ),
		  m_boundsChanged( boundsChanged ){
		captureShader();
	}

	MOHAATerrain( const MOHAATerrain& other, scene::Node& node, const Callback<void()>& evaluateTransform, const Callback<void()>& boundsChanged )
		: m_node( node ),
		  m_width( other.m_width ),
		  m_height( other.m_height ),
		  m_flags( other.m_flags ),
		  m_origin( other.m_origin ),
		  m_textureDefinitions( other.m_textureDefinitions ),
		  m_vertices( other.m_vertices ),
		  m_verticesTransformed(),
		  m_renderSolid( *this, false ),
		  m_renderWireframe( *this, true ),
		  m_shader( other.m_shader ),
		  m_evaluateTransform( evaluateTransform ),
		  m_boundsChanged( boundsChanged ){
		captureShader();
		revertTransform();
		buildRenderData();
	}

	~MOHAATerrain(){
		releaseShader();
		ASSERT_MESSAGE( m_observers.empty(), "MOHAATerrain::~MOHAATerrain: observers still attached" );
	}

	void attach( Observer* observer ){
		m_observers.push_back( observer );
		observer->allocate( m_vertices.size() );
	}

	void detach( Observer* observer ){
		const auto i = std::find( m_observers.begin(), m_observers.end(), observer );
		ASSERT_MESSAGE( i != m_observers.end(), "MOHAATerrain::detach: observer not attached" );
		m_observers.erase( i );
	}

	void instanceAttach( const scene::Path& path ){
		if ( ++m_instanceCounter.m_count == 1 ) {
			m_state->incrementUsed();
			m_map = path_find_mapfile( path.begin(), path.end() );
			m_undoableObserver = GlobalUndoSystem().observer( this );
		}
	}

	void instanceDetach( const scene::Path& path ){
		if ( --m_instanceCounter.m_count == 0 ) {
			m_state->decrementUsed();
			m_map = nullptr;
			m_undoableObserver = nullptr;
			GlobalUndoSystem().release( this );
		}
	}

	void undoSave(){
		if ( m_map != nullptr ) {
			m_map->changed();
		}
		if ( m_undoableObserver != nullptr ) {
			m_undoableObserver->save( this );
		}
	}

	UndoMemento* exportState() const override {
		return new SavedState( *this );
	}

	void importState( const UndoMemento* state ) override {
		undoSave();
		const SavedState& saved = *static_cast<const SavedState*>( state );
		m_width = saved.m_width;
		m_height = saved.m_height;
		m_flags = saved.m_flags;
		m_origin = saved.m_origin;
		m_textureDefinitions = saved.m_textureDefinitions;
		m_vertices = saved.m_vertices;
		revertTransform();
		updateShaderFromDefinitions();
		notifyAllocate();
		buildRenderData();
	}

	bool importTokens( Tokeniser& tokeniser ) override {
		tokeniser.nextLine();
		RETURN_FALSE_IF_FAIL( Tokeniser_parseToken( tokeniser, "{" ) );

		tokeniser.nextLine();
		RETURN_FALSE_IF_FAIL( Tokeniser_getSize( tokeniser, m_width ) );
		RETURN_FALSE_IF_FAIL( Tokeniser_getSize( tokeniser, m_height ) );
		RETURN_FALSE_IF_FAIL( Tokeniser_getInteger( tokeniser, m_flags ) );
		if ( m_width < 2 || m_height < 2 || m_width > c_maxTerrainVertices / m_height ) {
			globalErrorStream() << "MOHAA terrain has invalid dimensions " << m_width << 'x' << m_height << '\n';
			return false;
		}

		tokeniser.nextLine();
		double originX;
		double originY;
		double originZ;
		RETURN_FALSE_IF_FAIL( Tokeniser_getDouble( tokeniser, originX ) );
		RETURN_FALSE_IF_FAIL( Tokeniser_getDouble( tokeniser, originY ) );
		RETURN_FALSE_IF_FAIL( Tokeniser_getDouble( tokeniser, originZ ) );
		m_origin = Vector3( originX, originY, originZ );

		tokeniser.nextLine();
		RETURN_FALSE_IF_FAIL( Tokeniser_parseToken( tokeniser, "{" ) );
		m_textureDefinitions.clear();
		for (;; )
		{
			tokeniser.nextLine();
			const char* token = tokeniser.getToken();
			if ( token == nullptr ) {
				Tokeniser_unexpectedError( tokeniser, token, "}" );
				return false;
			}
			if ( string_equal( token, "}" ) ) {
				break;
			}

			m_textureDefinitions.emplace_back();
			TokenLine& line = m_textureDefinitions.back();
			line.emplace_back( token );
			while ( Tokeniser_inlineTokenAvailable( tokeniser ) )
			{
				line.emplace_back( tokeniser.getToken() );
			}
		}
		updateShaderFromDefinitions();

		tokeniser.nextLine();
		RETURN_FALSE_IF_FAIL( Tokeniser_parseToken( tokeniser, "{" ) );
		m_vertices.clear();
		m_vertices.reserve( m_width * m_height );
		for ( std::size_t y = 0; y < m_height; ++y )
		{
			for ( std::size_t x = 0; x < m_width; ++x )
			{
				tokeniser.nextLine();
				float height;
				RETURN_FALSE_IF_FAIL( Tokeniser_getFloat( tokeniser, height ) );

				TerrainVertex vertex;
				vertex.m_vertex = Vector3(
				    m_origin.x() + static_cast<double>( x ) * c_mohaaTerrainGrid,
				    m_origin.y() + static_cast<double>( y ) * c_mohaaTerrainGrid,
				    m_origin.z() + height
				);
				RETURN_FALSE_IF_FAIL( importTokenGroup( tokeniser, vertex.m_edgeFlags[0] ) );
				RETURN_FALSE_IF_FAIL( importTokenGroup( tokeniser, vertex.m_edgeFlags[1] ) );
				m_vertices.push_back( std::move( vertex ) );
			}
		}

		tokeniser.nextLine();
		RETURN_FALSE_IF_FAIL( Tokeniser_parseToken( tokeniser, "}" ) );
		tokeniser.nextLine();
		RETURN_FALSE_IF_FAIL( Tokeniser_parseToken( tokeniser, "}" ) );
		tokeniser.nextLine();
		RETURN_FALSE_IF_FAIL( Tokeniser_parseToken( tokeniser, "}" ) );

		revertTransform();
		notifyAllocate();
		buildRenderData();
		return true;
	}

	void exportTokens( TokenWriter& writer ) const override {
		writer.writeToken( "{" );
		writer.nextLine();
		writer.writeToken( "terrainDef" );
		writer.nextLine();
		writer.writeToken( "{" );
		writer.nextLine();
		writer.writeUnsigned( m_width );
		writer.writeUnsigned( m_height );
		writer.writeInteger( m_flags );
		writer.nextLine();
		writer.writeFloat( static_cast<float>( m_origin.x() ) );
		writer.writeFloat( static_cast<float>( m_origin.y() ) );
		writer.writeFloat( static_cast<float>( m_origin.z() ) );
		writer.nextLine();

		writer.writeToken( "{" );
		writer.nextLine();
		for ( const TokenLine& line : m_textureDefinitions )
		{
			for ( const CopiedString& token : line )
			{
				exportToken( writer, token );
			}
			writer.nextLine();
		}
		writer.writeToken( "}" );
		writer.nextLine();

		writer.writeToken( "{" );
		writer.nextLine();
		for ( const TerrainVertex& vertex : m_vertices )
		{
			writer.writeFloat( static_cast<float>( vertex.m_vertex.z() - m_origin.z() ) );
			for ( const TokenLine& flags : vertex.m_edgeFlags )
			{
				writer.writeToken( "(" );
				for ( const CopiedString& token : flags )
				{
					exportToken( writer, token );
				}
				writer.writeToken( ")" );
			}
			writer.nextLine();
		}
		writer.writeToken( "}" );
		writer.nextLine();
		writer.writeToken( "}" );
		writer.nextLine();
		writer.writeToken( "}" );
		writer.nextLine();
	}

	const Matrix4& localToParent() const override {
		return g_matrix4_identity;
	}

	const AABB& localAABB() const override {
		const_cast<MOHAATerrain*>( this )->evaluateTransform();
		return m_aabbLocal;
	}

	VolumeIntersectionValue intersectVolume( const VolumeTest& test, const Matrix4& localToWorld ) const override {
		return test.TestAABB( m_aabbLocal, localToWorld );
	}

	void renderSolid( Renderer& renderer, const Matrix4& localToWorld ) const {
		renderer.SetState( m_state, Renderer::eFullMaterials );
		renderer.addRenderable( m_renderSolid, localToWorld );
	}

	void renderWireframe( Renderer& renderer, const Matrix4& localToWorld ) const {
		renderer.SetState( m_state, Renderer::eFullMaterials );
		renderer.addRenderable( m_renderWireframe, localToWorld );
	}

	void testSelect( Selector& selector, SelectionTest& test ) const {
		if ( m_renderVertices.empty() || m_triangleIndices.empty() ) {
			return;
		}
		SelectionIntersection best;
		test.TestTriangles(
		    VertexPointer( VertexPointer::pointer( &m_renderVertices.data()->vertex ), sizeof( ArbitraryMeshVertex ) ),
		    IndexPointer( m_triangleIndices.data(), IndexPointer::index_type( m_triangleIndices.size() ) ),
		    best
		);
		if ( best.valid() ) {
			selector.addIntersection( best );
		}
	}

	const std::vector<Vector3>& transformedVertices() const {
		return m_verticesTransformed;
	}

	const std::vector<ArbitraryMeshVertex>& renderVertices() const {
		return m_renderVertices;
	}

	const std::vector<RenderIndex>& triangleIndices() const {
		return m_triangleIndices;
	}

	const std::vector<RenderIndex>& wireIndices() const {
		return m_wireIndices;
	}

	void transform( const Matrix4& matrix ){
		for ( Vector3& vertex : m_verticesTransformed )
		{
			matrix4_transform_point( matrix, vertex );
		}
		buildRenderData();
	}

	void transformComponents( const Matrix4& matrix, const std::vector<bool>& selected ){
		ASSERT_MESSAGE( selected.size() == m_verticesTransformed.size(), "MOHAATerrain::transformComponents: size mismatch" );
		for ( std::size_t i = 0; i < m_verticesTransformed.size(); ++i )
		{
			if ( selected[i] ) {
				matrix4_transform_point( matrix, m_verticesTransformed[i] );
			}
		}
		buildRenderData();
	}

	void transformChanged(){
		m_transformChanged = true;
		m_boundsChanged();
		SceneChangeNotify();
	}

	typedef MemberCaller<MOHAATerrain, void(), &MOHAATerrain::transformChanged> TransformChangedCaller;

	void evaluateTransform(){
		if ( m_transformChanged ) {
			revertTransform();
			m_evaluateTransform();
			m_transformChanged = false;
		}
	}

	void revertTransform(){
		m_verticesTransformed.clear();
		m_verticesTransformed.reserve( m_vertices.size() );
		for ( const TerrainVertex& vertex : m_vertices )
		{
			m_verticesTransformed.push_back( vertex.m_vertex );
		}
	}

	void freezePrimitiveTransform(){
		if ( m_vertices.empty() ) {
			return;
		}
		undoSave();
		const Vector3 delta = vector3_subtracted( m_verticesTransformed.front(), m_vertices.front().m_vertex );
		m_origin += delta;
		for ( std::size_t i = 0; i < m_vertices.size(); ++i )
		{
			m_vertices[i].m_vertex = m_verticesTransformed[i];
		}
		buildRenderData();
	}

	void freezeComponentTransform( const std::vector<bool>& selected ){
		undoSave();
		for ( std::size_t i = 0; i < m_vertices.size(); ++i )
		{
			if ( selected[i] ) {
				m_vertices[i].m_vertex.z() = m_verticesTransformed[i].z();
				m_verticesTransformed[i].x() = m_vertices[i].m_vertex.x();
				m_verticesTransformed[i].y() = m_vertices[i].m_vertex.y();
			}
		}
		buildRenderData();
	}

	void snapto( float snap ) override {
		if ( m_vertices.empty() ) {
			return;
		}
		const Vector3 snapped(
		    float_snapped( m_origin.x(), snap ),
		    float_snapped( m_origin.y(), snap ),
		    float_snapped( m_origin.z(), snap )
		);
		const Vector3 delta = vector3_subtracted( snapped, m_origin );
		if ( delta != Vector3( 0, 0, 0 ) ) {
			undoSave();
			m_origin = snapped;
			for ( TerrainVertex& vertex : m_vertices )
			{
				vertex.m_vertex += delta;
			}
			revertTransform();
			buildRenderData();
		}
	}
};

void RenderableTerrainMesh::render( RenderStateFlags state ) const {
	const std::vector<ArbitraryMeshVertex>& vertices = m_terrain.renderVertices();
	if ( vertices.empty() ) {
		return;
	}

	if ( ( state & RENDER_BUMP ) != 0 ) {
		gl().glNormalPointer( GL_FLOAT, sizeof( ArbitraryMeshVertex ), &vertices.data()->normal );
		gl().glVertexAttribPointer( c_attr_TexCoord0, 2, GL_FLOAT, 0, sizeof( ArbitraryMeshVertex ), &vertices.data()->texcoord );
		gl().glVertexAttribPointer( c_attr_Tangent, 3, GL_FLOAT, 0, sizeof( ArbitraryMeshVertex ), &vertices.data()->tangent );
		gl().glVertexAttribPointer( c_attr_Binormal, 3, GL_FLOAT, 0, sizeof( ArbitraryMeshVertex ), &vertices.data()->bitangent );
	}
	else
	{
		gl().glNormalPointer( GL_FLOAT, sizeof( ArbitraryMeshVertex ), &vertices.data()->normal );
		gl().glTexCoordPointer( 2, GL_FLOAT, sizeof( ArbitraryMeshVertex ), &vertices.data()->texcoord );
	}
	gl().glVertexPointer( 3, GL_FLOAT, sizeof( ArbitraryMeshVertex ), &vertices.data()->vertex );

	if ( m_wireframe ) {
		const std::vector<RenderIndex>& indices = m_terrain.wireIndices();
		gl().glDrawElements( GL_LINES, GLsizei( indices.size() ), RenderIndexTypeID, indices.data() );
	}
	else
	{
		const std::vector<RenderIndex>& indices = m_terrain.triangleIndices();
		gl().glDrawElements( GL_TRIANGLES, GLsizei( indices.size() ), RenderIndexTypeID, indices.data() );
	}
}

class TerrainVertexInstance
{
public:
	std::size_t m_index;
	ObservedSelectable m_selectable;

	TerrainVertexInstance( std::size_t index, const SelectionChangeCallback& observer )
		: m_index( index ), m_selectable( observer ){
	}
};

class MOHAATerrainInstance final :
	public MOHAATerrain::Observer,
	public scene::Instance,
	public Selectable,
	public Renderable,
	public SelectionTestable,
	public ComponentSelectionTestable,
	public ComponentEditable,
	public ComponentSnappable
{
	class TypeCasts
	{
		InstanceTypeCastTable m_casts;
	public:
		TypeCasts(){
			InstanceStaticCast<MOHAATerrainInstance, Selectable>::install( m_casts );
			InstanceContainedCast<MOHAATerrainInstance, Bounded>::install( m_casts );
			InstanceContainedCast<MOHAATerrainInstance, Cullable>::install( m_casts );
			InstanceStaticCast<MOHAATerrainInstance, Renderable>::install( m_casts );
			InstanceStaticCast<MOHAATerrainInstance, SelectionTestable>::install( m_casts );
			InstanceStaticCast<MOHAATerrainInstance, ComponentSelectionTestable>::install( m_casts );
			InstanceStaticCast<MOHAATerrainInstance, ComponentEditable>::install( m_casts );
			InstanceStaticCast<MOHAATerrainInstance, ComponentSnappable>::install( m_casts );
			InstanceContainedCast<MOHAATerrainInstance, Transformable>::install( m_casts );
			InstanceIdentityCast<MOHAATerrainInstance>::install( m_casts );
		}

		InstanceTypeCastTable& get(){
			return m_casts;
		}
	};

	MOHAATerrain& m_terrain;
	ObservedSelectable m_selectable;
	std::vector<TerrainVertexInstance> m_vertexInstances;
	mutable RenderablePointVector m_renderPoints;
	mutable RenderablePointVector m_renderSelected;
	mutable AABB m_componentAABB;
	Shader* m_statePoint;
	Shader* m_stateSelectedPoint;
	TransformModifier m_transform;

public:
	typedef LazyStatic<TypeCasts> StaticTypeCasts;

	STRING_CONSTANT( Name, "MOHAATerrainInstance" );

	MOHAATerrainInstance( const scene::Path& path, scene::Instance* parent, MOHAATerrain& terrain )
		: Instance( path, parent, this, StaticTypeCasts::instance().get() ),
		  m_terrain( terrain ),
		  m_selectable( SelectedChangedCaller( *this ) ),
		  m_renderPoints( GL_POINTS ),
		  m_renderSelected( GL_POINTS ),
		  m_statePoint( GlobalShaderCache().capture( "$POINT" ) ),
		  m_stateSelectedPoint( GlobalShaderCache().capture( "$SELPOINT" ) ),
		  m_transform( MOHAATerrain::TransformChangedCaller( m_terrain ), ApplyTransformCaller( *this ) ){
		m_terrain.instanceAttach( path );
		m_terrain.attach( this );
	}

	~MOHAATerrainInstance(){
		m_terrain.detach( this );
		m_terrain.instanceDetach( path() );
		GlobalShaderCache().release( "$SELPOINT" );
		GlobalShaderCache().release( "$POINT" );
	}

	Bounded& get( NullType<Bounded> ){
		return m_terrain;
	}

	Cullable& get( NullType<Cullable> ){
		return m_terrain;
	}

	Transformable& get( NullType<Transformable> ){
		return m_transform;
	}

	void allocate( std::size_t size ) override {
		m_vertexInstances.clear();
		m_vertexInstances.reserve( size );
		for ( std::size_t i = 0; i < size; ++i )
		{
			m_vertexInstances.emplace_back( i, SelectedChangedComponentCaller( *this ) );
		}
	}

	void selectedChanged( const Selectable& selectable ){
		GlobalSelectionSystem().getObserver( SelectionSystem::ePrimitive )( selectable );
		GlobalSelectionSystem().onSelectedChanged( *this, selectable );
		Instance::selectedChanged();
	}

	typedef MemberCaller<MOHAATerrainInstance, void(const Selectable&), &MOHAATerrainInstance::selectedChanged> SelectedChangedCaller;

	void selectedChangedComponent( const Selectable& selectable ){
		GlobalSelectionSystem().getObserver( SelectionSystem::eComponent )( selectable );
		GlobalSelectionSystem().onComponentSelection( *this, selectable );
	}

	typedef MemberCaller<MOHAATerrainInstance, void(const Selectable&), &MOHAATerrainInstance::selectedChangedComponent> SelectedChangedComponentCaller;

	void setSelected( bool select ) override {
		m_selectable.setSelected( select );
		if ( !select && parent() != nullptr ) {
			Selectable* selectable = Instance_getSelectable( *parent() );
			if ( selectable != nullptr && selectable->isSelected() ) {
				selectable->setSelected( false );
			}
		}
	}

	bool isSelected() const override {
		return m_selectable.isSelected();
	}

	void renderSolid( Renderer& renderer, const VolumeTest& volume ) const override {
		m_terrain.evaluateTransform();
		renderer.Highlight( Renderer::ePrimitiveWire );
		m_terrain.renderSolid( renderer, localToWorld() );
		renderComponentsSelected( renderer );
	}

	void renderWireframe( Renderer& renderer, const VolumeTest& volume ) const override {
		m_terrain.evaluateTransform();
		m_terrain.renderWireframe( renderer, localToWorld() );
		renderComponentsSelected( renderer );
	}

	void renderComponents( Renderer& renderer, const VolumeTest& volume ) const override {
		if ( GlobalSelectionSystem().ComponentMode() != SelectionSystem::eVertex ) {
			return;
		}
		m_terrain.evaluateTransform();
		m_renderPoints.clear();
		m_renderPoints.reserve( m_terrain.transformedVertices().size() );
		for ( const Vector3& vertex : m_terrain.transformedVertices() )
		{
			m_renderPoints.push_back( PointVertex( vertex3f_for_vector3( vertex ), Colour4b( 0, 255, 0, 255 ) ) );
		}
		if ( !m_renderPoints.empty() ) {
			renderer.SetState( m_statePoint, Renderer::eWireframeOnly );
			renderer.SetState( m_statePoint, Renderer::eFullMaterials );
			renderer.addRenderable( m_renderPoints, localToWorld() );
		}
	}

	void renderComponentsSelected( Renderer& renderer ) const {
		m_renderSelected.clear();
		const std::vector<Vector3>& vertices = m_terrain.transformedVertices();
		for ( const TerrainVertexInstance& instance : m_vertexInstances )
		{
			if ( instance.m_selectable.isSelected() ) {
				m_renderSelected.push_back( PointVertex( vertex3f_for_vector3( vertices[instance.m_index] ), Colour4b( 0, 0, 255, 255 ) ) );
			}
		}
		if ( !m_renderSelected.empty() ) {
			renderer.SetState( m_stateSelectedPoint, Renderer::eWireframeOnly );
			renderer.SetState( m_stateSelectedPoint, Renderer::eFullMaterials );
			renderer.addRenderable( m_renderSelected, localToWorld() );
		}
	}

	void testSelect( Selector& selector, SelectionTest& test ) override {
		test.BeginMesh( localToWorld(), true );
		m_terrain.testSelect( selector, test );
	}

	bool isSelectedComponents() const override {
		return std::ranges::any_of( m_vertexInstances, []( const TerrainVertexInstance& instance ){
			return instance.m_selectable.isSelected();
		} );
	}

	void setSelectedComponents( bool select, SelectionSystem::EComponentMode mode ) override {
		if ( mode == SelectionSystem::eVertex ) {
			for ( TerrainVertexInstance& instance : m_vertexInstances )
			{
				instance.m_selectable.setSelected( select );
			}
		}
	}

	void testSelectComponents( Selector& selector, SelectionTest& test, SelectionSystem::EComponentMode mode ) override {
		if ( mode != SelectionSystem::eVertex ) {
			return;
		}
		test.BeginMesh( localToWorld() );
		const std::vector<Vector3>& vertices = m_terrain.transformedVertices();
		for ( TerrainVertexInstance& instance : m_vertexInstances )
		{
			SelectionIntersection best;
			test.TestPoint( vertices[instance.m_index], best );
			if ( best.valid() ) {
				Selector_add( selector, instance.m_selectable, best );
			}
		}
	}

	void gatherComponentsHighlight( std::vector<std::vector<Vector3>>& polygons, SelectionIntersection& intersection, SelectionTest& test, SelectionSystem::EComponentMode mode ) const override {
		if ( mode != SelectionSystem::eVertex ) {
			return;
		}
		test.BeginMesh( localToWorld() );
		const std::vector<Vector3>& vertices = m_terrain.transformedVertices();
		for ( const TerrainVertexInstance& instance : m_vertexInstances )
		{
			SelectionIntersection best;
			test.TestPoint( vertices[instance.m_index], best );
			if ( SelectionIntersection_closer( best, intersection ) ) {
				intersection = best;
				polygons.clear();
				polygons.emplace_back( std::initializer_list<Vector3>( { vertices[instance.m_index] } ) );
			}
		}
	}

	const AABB& getSelectedComponentsBounds() const override {
		m_componentAABB = AABB();
		const std::vector<Vector3>& vertices = m_terrain.transformedVertices();
		for ( const TerrainVertexInstance& instance : m_vertexInstances )
		{
			if ( instance.m_selectable.isSelected() ) {
				aabb_extend_by_point_safe( m_componentAABB, vertices[instance.m_index] );
			}
		}
		return m_componentAABB;
	}

	void gatherSelectedComponents( const Vector3Callback& callback ) const override {
		const std::vector<Vector3>& vertices = m_terrain.transformedVertices();
		for ( const TerrainVertexInstance& instance : m_vertexInstances )
		{
			if ( instance.m_selectable.isSelected() ) {
				callback( vertices[instance.m_index] );
			}
		}
	}

	std::vector<bool> selectedVertices() const {
		std::vector<bool> selected( m_vertexInstances.size(), false );
		for ( const TerrainVertexInstance& instance : m_vertexInstances )
		{
			selected[instance.m_index] = instance.m_selectable.isSelected();
		}
		return selected;
	}

	void transformComponents( const Matrix4& matrix ){
		const std::vector<bool> selected = selectedVertices();
		m_terrain.transformComponents( matrix, selected );
	}

	void snapComponents( float snap ) override {
		std::vector<bool> selected = selectedVertices();
		if ( std::ranges::none_of( selected, []( bool value ){ return value; } ) ) {
			return;
		}
		m_terrain.revertTransform();
		Matrix4 matrix( g_matrix4_identity );
		for ( std::size_t i = 0; i < m_terrain.transformedVertices().size(); ++i )
		{
			if ( selected[i] ) {
				const double z = m_terrain.transformedVertices()[i].z();
				matrix.t().z() = float_snapped( z, snap ) - z;
				std::vector<bool> oneSelected( selected.size(), false );
				oneSelected[i] = true;
				m_terrain.transformComponents( matrix, oneSelected );
				matrix.t().z() = 0;
			}
		}
		m_terrain.freezeComponentTransform( selected );
	}

	void evaluateTransform(){
		const Matrix4 matrix( m_transform.calculateTransform() );
		if ( m_transform.getType() == TRANSFORM_PRIMITIVE ) {
			m_terrain.transform( matrix );
		}
		else
		{
			transformComponents( matrix );
		}
	}

	void applyTransform(){
		m_terrain.revertTransform();
		evaluateTransform();
		if ( m_transform.getType() == TRANSFORM_PRIMITIVE ) {
			m_terrain.freezePrimitiveTransform();
		}
		else
		{
			m_terrain.freezeComponentTransform( selectedVertices() );
		}
	}

	typedef MemberCaller<MOHAATerrainInstance, void(), &MOHAATerrainInstance::applyTransform> ApplyTransformCaller;
};

class MOHAATerrainNode final :
	public scene::Node::Symbiot,
	public scene::Instantiable,
	public scene::Cloneable
{
	class TypeCasts
	{
		NodeTypeCastTable m_casts;
	public:
		TypeCasts(){
			NodeStaticCast<MOHAATerrainNode, scene::Instantiable>::install( m_casts );
			NodeStaticCast<MOHAATerrainNode, scene::Cloneable>::install( m_casts );
			NodeContainedCast<MOHAATerrainNode, MapImporter>::install( m_casts );
			NodeContainedCast<MOHAATerrainNode, MapExporter>::install( m_casts );
			NodeContainedCast<MOHAATerrainNode, TransformNode>::install( m_casts );
			NodeContainedCast<MOHAATerrainNode, Bounded>::install( m_casts );
			NodeContainedCast<MOHAATerrainNode, Cullable>::install( m_casts );
			NodeContainedCast<MOHAATerrainNode, Snappable>::install( m_casts );
		}

		NodeTypeCastTable& get(){
			return m_casts;
		}
	};

	scene::Node m_node;
	InstanceSet m_instances;
	MOHAATerrain m_terrain;

public:
	typedef LazyStatic<TypeCasts> StaticTypeCasts;

	MOHAATerrainNode()
		: m_node( this, this, StaticTypeCasts::instance().get(), GlobalSceneGraph().currentLayer() ),
		  m_terrain(
		      m_node,
		      InstanceSetEvaluateTransform<MOHAATerrainInstance>::Caller( m_instances ),
		      InstanceSet::BoundsChangedCaller( m_instances )
		  ){
	}

	MOHAATerrainNode( const MOHAATerrainNode& other )
		: scene::Node::Symbiot( other ),
		  scene::Instantiable( other ),
		  scene::Cloneable( other ),
		  m_node( this, this, StaticTypeCasts::instance().get(), other.m_node.m_layer ),
		  m_terrain(
		      other.m_terrain,
		      m_node,
		      InstanceSetEvaluateTransform<MOHAATerrainInstance>::Caller( m_instances ),
		      InstanceSet::BoundsChangedCaller( m_instances )
		  ){
	}

	void release() override {
		delete this;
	}

	scene::Node& node(){
		return m_node;
	}

	MapImporter& get( NullType<MapImporter> ){
		return m_terrain;
	}

	MapExporter& get( NullType<MapExporter> ){
		return m_terrain;
	}

	TransformNode& get( NullType<TransformNode> ){
		return m_terrain;
	}

	Bounded& get( NullType<Bounded> ){
		return m_terrain;
	}

	Cullable& get( NullType<Cullable> ){
		return m_terrain;
	}

	Snappable& get( NullType<Snappable> ){
		return m_terrain;
	}

	scene::Node& clone() const override {
		return ( new MOHAATerrainNode( *this ) )->node();
	}

	scene::Instance* create( const scene::Path& path, scene::Instance* parent ) override {
		return new MOHAATerrainInstance( path, parent, m_terrain );
	}

	void forEachInstance( const scene::Instantiable::Visitor& visitor ) override {
		m_instances.forEachInstance( visitor );
	}

	void insert( scene::Instantiable::Observer* observer, const scene::Path& path, scene::Instance* instance ) override {
		m_instances.insert( observer, path, instance );
	}

	scene::Instance* erase( scene::Instantiable::Observer* observer, const scene::Path& path ) override {
		return m_instances.erase( observer, path );
	}
};
}

scene::Node& NewMOHAATerrain(){
	return ( new MOHAATerrainNode )->node();
}
