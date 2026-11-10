//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#include "cbase.h"
#include "c_blob_mine.h"
#include "materialsystem/IMaterial.h"
#include "tier2/renderutils.h"
#include "model_types.h"
#include "engine/ivdebugoverlay.h"
#include "c_surfacerender.h"
#include "functionproxy.h"
#include "proxyentity.h"
#include "materialsystem/imaterialvar.h"


#define BLOB_MINE_PARTICLE_COUNT 8
#define BLOB_MINE_PARTICLE_SPEED 5
#define BLOB_MINE_SURFACE_RADIUS 3	    

//-----------------------------------------------------------------------------
// Networking
//-----------------------------------------------------------------------------
IMPLEMENT_CLIENTCLASS_DT( C_BlobMine, DT_BlobMine, CBlobMine )
	RecvPropBool( RECVINFO( m_bHeld ) ),
	RecvPropFloat( RECVINFO( m_flRadius ) ),
END_RECV_TABLE()


C_BlobMine::C_BlobMine( void )
{
	memset( m_lightingBoxColors, 0, sizeof( m_lightingBoxColors ) );
}

void C_BlobMine::Activate( void )
{
	// These aren't saved
	memset( m_lightingBoxColors, 0, sizeof( m_lightingBoxColors ) );
}


//-----------------------------------------------------------------------------
// Think
//-----------------------------------------------------------------------------
void C_BlobMine::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );
	if ( updateType == DATA_UPDATE_CREATED )
	{
		for ( int i = 0; i < BLOB_MINE_PARTICLE_COUNT; ++i )
		{
			Vector vecPos, vecVel;
			RandomVectorInUnitSphere( &vecPos );
			RandomVectorInUnitSphere( &vecVel );
			VectorNormalize( vecVel );
			vecVel *= BLOB_MINE_PARTICLE_SPEED;
			m_vecOrigins.AddToTail( vecPos );
			m_vecVelocities.AddToTail( vecVel );
		}
		SetNextClientThink( CLIENT_THINK_ALWAYS );
	}
}


//-----------------------------------------------------------------------------
// Think
//-----------------------------------------------------------------------------
void C_BlobMine::ClientThink()
{
	BaseClass::ClientThink();
	SimulateParticles();
	UpdateRenderBounds();
}


//-----------------------------------------------------------------------------
// Simulate particles
//-----------------------------------------------------------------------------
void C_BlobMine::SimulateParticles()
{
	int nCount = m_vecOrigins.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		float dt = gpGlobals->frametime;
		Vector vecNewPos;
		VectorMA( m_vecOrigins[i], gpGlobals->frametime, m_vecVelocities[i], vecNewPos );
		Vector vecNewVel = m_vecVelocities[i];
		int nLoopCount = 5;
		while ( vecNewPos.LengthSqr() > 1.0f )
		{
			float a = vecNewVel.LengthSqr();
			float b = 2.0f * DotProduct( vecNewVel, vecNewPos );
			float c = vecNewPos.LengthSqr() - 1.0f;
			float t = ( -b + sqrt( b * b - 4.0f * a * c ) ) / ( 2.0f * a );
			VectorMA( vecNewPos, t, vecNewVel, vecNewPos );
			VectorNormalize( vecNewPos );
			VectorMA( vecNewVel, -2.0f * DotProduct( vecNewPos, vecNewVel ), vecNewPos, vecNewVel );
			VectorMA( vecNewPos, dt - t, vecNewVel, vecNewPos );
			if ( t >= 0.0f )
			{
				dt -= t;
			}
			if ( ++nLoopCount >= 5 )
				break;
		}
		m_vecOrigins[i] = vecNewPos;
		m_vecVelocities[i] = vecNewVel;
	}
}



//-----------------------------------------------------------------------------
// Recompute bounds
//-----------------------------------------------------------------------------
void C_BlobMine::UpdateRenderBounds( void ) const
{
	if( m_iCachedWorldBoundsUpdateTick == gpGlobals->tickcount )
		return;

	if ( m_vecOrigins.Count() == 0 )
		return;

	m_iCachedWorldBoundsUpdateTick = gpGlobals->tickcount;

	Vector vecPos = m_vecOrigins[ 0 ] * m_flRadius;
	m_vCachedWorldMins = vecPos;
	m_vCachedWorldMaxs = vecPos;

	int nCount = m_vecOrigins.Count();
	for (int i = 1; i < nCount; i++)
	{
		vecPos = m_vecOrigins[ i ] * m_flRadius;
		VectorMin( vecPos, m_vCachedWorldMins, m_vCachedWorldMins );
		VectorMax( vecPos, m_vCachedWorldMaxs, m_vCachedWorldMaxs );
	}

	m_vCachedWorldMins -= BLOB_MINE_SURFACE_RADIUS;
	m_vCachedWorldMaxs += BLOB_MINE_SURFACE_RADIUS;
	m_vCachedWorldMins += const_cast<C_BlobMine*>(this)->GetRenderOrigin();
	m_vCachedWorldMaxs += const_cast<C_BlobMine*>(this)->GetRenderOrigin();
}

void C_BlobMine::GetRenderBoundsWorldspace( Vector& absMins, Vector& absMaxs )
{
	absMins = m_vCachedWorldMins;
	absMaxs = m_vCachedWorldMaxs;
}

void C_BlobMine::GetRenderBounds( Vector& theMins, Vector& theMaxs )
{
	theMins = m_vCachedWorldMins - GetRenderOrigin();
	theMaxs = m_vCachedWorldMaxs - GetRenderOrigin();
}

const Vector& C_BlobMine::WorldAlignMins() const
{
	return m_vCachedWorldMins;
}

const Vector& C_BlobMine::WorldAlignMaxs() const
{
	return m_vCachedWorldMaxs;
}

const Vector& C_BlobMine::WorldSpaceCenter()
{
	return GetRenderOrigin();
}

void C_BlobMine::ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs )
{
	GetRenderBoundsWorldspace( *pMins, *pMaxs );
}



//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flags - 
// Output : int
//-----------------------------------------------------------------------------
int C_BlobMine::DrawModel( int flags )
{
	// See if we should draw
	if ( m_bReadyToDraw == false )
		return 0;

	SetUpDrawLighting();

	IMaterial *pMaterial = materials->FindMaterial( "models/blob/blob_mine_material", TEXTURE_GROUP_OTHER, true );
	int iNumParticles = 0;

	int nCount = m_vecOrigins.Count();
	g_SurfaceRenderParticles.SetCount( nCount );
	const Vector &vecOrigin = GetRenderOrigin();
	for ( int i = 0; i < nCount; ++i )
	{
		Vector vecWorldCenter;
		VectorMA( vecOrigin, m_flRadius, m_vecOrigins[ i ], vecWorldCenter );
		ImpParticleWithOneInterpolant* imp_particle = &(g_SurfaceRenderParticles[ i ]);
		imp_particle->center = vecWorldCenter;
		imp_particle->setFieldScale( 0.4f ); 
		imp_particle->interpolants1.set( 1.0f, 0.0f, 0.0f );
		imp_particle->interpolants1[ 3 ] = 0.0f;

		++iNumParticles;
	}

	g_SurfaceRenderParticles.SetCountNonDestructively( iNumParticles );
									 
	Surface_Draw( GetClientRenderable(), GetRenderOrigin(), pMaterial, 4.5f );
	return 1;
}

void C_BlobMine::SetUpDrawLighting()
{
	if ( GetModelInstance() == MODEL_INSTANCE_INVALID )
	{
		// we need a model instance so that we can perform smooth lighting interpolation
		CreateModelInstance();
	}

	modelrender->SetupLightingEx( GetRenderOrigin(), GetModelInstance() );

	// We sometimes get an invalid ambient lightcube, this func smooths over that
	// FIXME: this is probably to do with sampling at invalid locations
	Surface_SafeLightCubeUpdate( GetRenderOrigin(), m_lightingBoxColors );

	// Set our manually interpolated lightcube
	if ( materials && materials->GetRenderContext() )
	{
		materials->GetRenderContext()->SetAmbientLightCube( m_lightingBoxColors );
	}
}
