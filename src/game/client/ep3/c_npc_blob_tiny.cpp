//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include "cbase.h"
#include "c_npc_blob_tiny.h"
#include "materialsystem/IMaterial.h"
#include "tier2/renderutils.h"
#include "model_types.h"
#include "engine/ivdebugoverlay.h"


//-----------------------------------------------------------------------------
// Networking
//-----------------------------------------------------------------------------
IMPLEMENT_CLIENTCLASS_DT( C_NPC_BlobTiny, DT_BlobTiny, CNPC_BlobTiny )
	RecvPropBool( RECVINFO( m_bHeld ) ),
	RecvPropBool( RECVINFO( m_bIsFastBlob ) ),
	RecvPropBool( RECVINFO( m_bInFlight ) ),
	RecvPropBool( RECVINFO( m_bIsSplitter ) ),
	RecvPropFloat( RECVINFO( m_flRadius ) ),
	RecvPropVector( RECVINFO(m_vecBlobVelocity) ),
END_RECV_TABLE()


//-----------------------------------------------------------------------------
// Data changed
//-----------------------------------------------------------------------------
void C_NPC_BlobTiny::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( type == DATA_UPDATE_CREATED )
	{
		m_flLastSquashStretch = 1.0f;
		m_flLastSquashStretchTime = gpGlobals->curtime;
		SetNextClientThink( CLIENT_THINK_ALWAYS );
	}

	if ( m_bInFlight )
	{
		m_vecLastNonZeroBlobVelocity = m_vecBlobVelocity;
	}
	else
	{
		m_bReachedTargetVelocity = false;
	}
}


//-----------------------------------------------------------------------------
// Computes the stretch amount
//-----------------------------------------------------------------------------
void C_NPC_BlobTiny::ClientThink( void )
{
//	BaseClass::ClientThink();
	ComputeSquashStretch();
}


//-----------------------------------------------------------------------------
// Computes the stretch amount
//-----------------------------------------------------------------------------
#define SQUASH_LAG_TIME 0.1f
#define MAX_STRETCH 2.0f
#define MAX_SQUASH 0.5f
#define MIN_STRETCH_SPEED 40.0f
#define MAX_STRETCH_SPEED 50.0f

float C_NPC_BlobTiny::ComputeSquashStretch() const
{
	// FIXME: Should this be analog or digital? Pick digital for now
	// to exaggerate squash/stretch
	float flTargetSquashStretch = m_bHeld ? 1.0f : MAX_SQUASH;
	if ( m_bInFlight )
	{
		float flSpeed = m_vecBlobVelocity.Length();
		float flFactor = ( flSpeed - MIN_STRETCH_SPEED ) / MAX_STRETCH_SPEED;
		flFactor = clamp( flFactor, 0.0f, 1.0f );
		flTargetSquashStretch = Lerp( flFactor, 1.0f, MAX_STRETCH );
		if ( !m_bReachedTargetVelocity )
		{
			float dt = gpGlobals->curtime - m_flLastSquashStretchTime;
			dt /= SQUASH_LAG_TIME;
			dt = clamp( dt, 0.0f, 1.0f );
			flTargetSquashStretch = Lerp( dt, m_flLastSquashStretch, flTargetSquashStretch );
			m_bReachedTargetVelocity = fabs( flTargetSquashStretch - m_flLastSquashStretch ) < 0.01f;
		}
	}
	else
	{
		float dt = gpGlobals->curtime - m_flLastSquashStretchTime;
		dt /= SQUASH_LAG_TIME;
		dt = clamp( dt, 0.0f, 1.0f );
		flTargetSquashStretch = Lerp( dt, m_flLastSquashStretch, flTargetSquashStretch ); 
	}
	m_flLastSquashStretch = flTargetSquashStretch;
	m_flLastSquashStretchTime = gpGlobals->curtime;
	return flTargetSquashStretch;
}


//-----------------------------------------------------------------------------
// Computes ellipse origin + radii
//-----------------------------------------------------------------------------
void C_NPC_BlobTiny::ComputeOriginAndRadii( float flSquashStretch, Vector *pOrigin, float *pMainRadius, float *pCrossRadius ) const
{
	// We want to preserve volume. 
	// The volume of the unsquashed sphere is 4/3 PI r^3. 
	// The volume of the ellipsoid is 4/3 PI ( squash r ) * ( r' )^2
	// where r'	is the cross radius. Therefore, r' = sqrt( 1 / squash )
	*pMainRadius = flSquashStretch * m_flRadius;
	*pCrossRadius = sqrt( 1.0f / flSquashStretch ) * m_flRadius;

	// In this case, we're compressing owing to a collision.
	// Move the center along the impact normal
	if ( flSquashStretch < 1.0f )
	{
		Vector vecDir;
		AngleVectors( GetAbsAngles(), &vecDir );
		VectorMA( GetAbsOrigin(), -m_flRadius * ( 1.0f - flSquashStretch ), vecDir, *pOrigin );
	}
	else
	{
		*pOrigin = GetAbsOrigin();
	}
}

//-----------------------------------------------------------------------------
// Draws a blobby sphere
//-----------------------------------------------------------------------------
void C_NPC_BlobTiny::RenderBlob( int nFlags )
{
	const char *pMaterialName = m_bIsFastBlob ? "models/blob/blob_material_tiny_fast" : "models/blob/blob_material_tiny";
	if ( m_bIsSplitter )
	{
		pMaterialName = "models/blob/blob_material_tiny_splitter";
	}

	if ( nFlags & STUDIO_SHADOWTEXTURE )
	{
		pMaterialName = "engine/shadowbuild";
	}

	IMaterial *pMaterial = materials->FindMaterial( pMaterialName, TEXTURE_GROUP_OTHER, true );

	CMatRenderContextPtr pRenderContext( materials );

	Vector vecOrigin;
	float flMainRadius, flCrossRadius;
	float flStretch = m_flLastSquashStretch;
	ComputeOriginAndRadii( flStretch, &vecOrigin, &flMainRadius, &flCrossRadius );

	Vector vecMainDir = m_vecBlobVelocity;
	if ( flStretch < 1.0f )
	{
		AngleVectors( GetAbsAngles(), &vecMainDir );
	}
	else if ( !m_bInFlight )
	{
		vecMainDir = m_vecLastNonZeroBlobVelocity;
	}

	matrix3x4_t worldToBlob;
	VectorNormalize( vecMainDir );
	VectorMatrix( vecMainDir, worldToBlob );
	MatrixSetColumn( vecOrigin, 3, worldToBlob );

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadMatrix( worldToBlob );

	unsigned char chRed = 255;
	unsigned char chGreen = 255;
	unsigned char chBlue = 255;
	unsigned char chAlpha = 255;

	const int nTheta = 15;
	const int nPhi = 30;

	float flRippleMagnitude = flStretch - 1.0f;
	flRippleMagnitude = ( flRippleMagnitude - 0.5f ) / 0.5f;
	flRippleMagnitude = clamp( flRippleMagnitude, 0.0f, 1.0f );
	flRippleMagnitude *= m_flRadius / 16.0f;

	// Two extra degenerate triangles per row (except the last one)
	int nTriangles = 2 * nTheta * ( nPhi - 1 ); 
	int nIndices = 2 * ( nTheta + 1 ) * ( nPhi - 1 );

	pRenderContext->Bind( pMaterial );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( );
	CMeshBuilder meshBuilder;

	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLE_STRIP, nTriangles, nIndices );

	// Build the index buffer.
	float flOONPhi = 1.0f / (nPhi-1);
	float flOONTheta = 1.0f / (nTheta-1);

	int i, j;
	for ( i = 0; i < nPhi; ++i )
	{
		for ( j = 0; j < nTheta; ++j )
		{
			float u = j / ( float )( nTheta - 1 );
			float v = i / ( float )( nPhi - 1 );
			float theta = 2.0f * M_PI * u;
			float phi = M_PI * v;

			float r = flCrossRadius + flRippleMagnitude * sin( 2.0f * M_PI * ( -gpGlobals->curtime / 0.2f + 4.0f * v ) );
			Vector vecPos;
			vecPos.y = r * sin(phi) * cos(theta);
			vecPos.z = r * sin(phi) * sin(theta); 
			vecPos.x = flMainRadius * cos(phi);

			Vector vecNormal = vecPos;
			VectorNormalize(vecNormal);

			Vector4D vecTangent( -vecPos.y, vecPos.x, ( vecPos.x == 0.0f && vecPos.y == 0.0f ) ? 1.0f : 0.0f, 1.0f );
			VectorNormalize( vecTangent.AsVector3D() );

			meshBuilder.Position3f( vecPos.x, vecPos.y, vecPos.z );
			meshBuilder.Normal3f( vecNormal.x, vecNormal.y, vecNormal.z );
			meshBuilder.Color4ub( chRed, chGreen, chBlue, chAlpha );
			meshBuilder.UserData( vecTangent.Base() );
			meshBuilder.TexCoord2f( 0, j * flOONTheta, i * flOONPhi );
			meshBuilder.AdvanceVertex();
		}
	}

	// Emit the triangle strips.
	int idx = 0;
	for ( i = 0; i < nPhi - 1; ++i )
	{
		for ( j = 0; j < nTheta; ++j )
		{
			idx = nTheta * i + j;

			meshBuilder.Index( idx + nTheta );
			meshBuilder.AdvanceIndex();

			meshBuilder.Index( idx );
			meshBuilder.AdvanceIndex();
		}

		// Emit a degenerate triangle to skip to the next row without a connecting triangle
		if ( i < nPhi - 2 )
		{
			meshBuilder.Index( idx );
			meshBuilder.AdvanceIndex();

			meshBuilder.Index( idx + nTheta + 1 );
			meshBuilder.AdvanceIndex();
		}
	}

	meshBuilder.End();
	pMesh->Draw();

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flags - 
// Output : int
//-----------------------------------------------------------------------------
int C_NPC_BlobTiny::DrawModel( int flags )
{
	if ( GetModelInstance() == MODEL_INSTANCE_INVALID )
	{
		// we need a model instance so that we can perform smooth lighting interpolation
		CreateModelInstance();
	}

	modelrender->SetupLightingEx( GetRenderOrigin(), GetModelInstance() );

	RenderBlob( flags );
	return 1;
//	return BaseClass::DrawModel( flags );
}