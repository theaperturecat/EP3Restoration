//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ====
//
// Purpose: 
//
//=============================================================================

#include "cbase.h"
#include "c_surfacerender.h"
#include "view_scene.h"
#include "materialsystem/imaterialvar.h"
//#include "model_types.h"


#define SCULPTURE_SURFACE_RADIUS 0.7f


class C_IceSculpture: public C_BaseAnimating
{
	DECLARE_CLASS( C_IceSculpture, C_BaseAnimating );
	DECLARE_SHAREDCLASS();

private:
	void UpdateRenderBoundsAndOriginWorldspace( void ) const;

public:
	C_IceSculpture( void );

	virtual IClientModelRenderable* GetClientModelRenderable() { return NULL; }//ported from blob, without this ice is broken

	virtual void			Activate( void );

	virtual const QAngle&	GetRenderAngles( void );
	virtual const Vector&	GetRenderOrigin( void );
	virtual void			GetRenderBoundsWorldspace( Vector& absMins, Vector& absMaxs );
	virtual void			GetRenderBounds( Vector& theMins, Vector& theMaxs );

	virtual const Vector&	WorldAlignMins( void ) const;
	virtual const Vector&	WorldAlignMaxs( void ) const;
	// TODO: This sucks.... I should use a renderCenter variable.
	virtual const Vector&	WorldSpaceCenter( void );
	virtual void			ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs );

	virtual int				DrawModel( int flags, const RenderableInstance_t &instance );
	virtual bool			ShouldDraw( void ) { return true; }

	void SetUpDrawLighting();

private:
	mutable Vector	m_vCachedRenderOrigin;
	mutable Vector	m_vCachedWorldMins;
	mutable Vector	m_vCachedWorldMaxs;
	mutable int		m_iCachedWorldBoundsUpdateTick;

public:
	Vector4D	m_lightingBoxColors[6];	// WORK IN PROGRESS
	bool		m_bOriginsRelativeToCenter;
	CSharedUtlVector( Vector, m_vecOrigins );

	DECLARE_CLIENTCLASS();
};


IMPLEMENT_CLIENTCLASS_DT( C_IceSculpture, DT_IceSculpture, CIceSculpture )
	RecvPropBool( RECVINFO( m_bOriginsRelativeToCenter ) )
END_RECV_TABLE()

IMPLEMENT_SHAREDCLASS_DT( C_IceSculpture )
	SharedProp( m_vecOrigins )
END_SHARED_TABLE()


C_IceSculpture::C_IceSculpture( void )
{
	memset( m_lightingBoxColors, 0, sizeof( m_lightingBoxColors ) );
}

void C_IceSculpture::Activate( void )
{
	// These aren't saved
	memset( m_lightingBoxColors, 0, sizeof( m_lightingBoxColors ) );
}

void C_IceSculpture::UpdateRenderBoundsAndOriginWorldspace( void ) const
{
	if( m_iCachedWorldBoundsUpdateTick != gpGlobals->tickcount )
	{
		m_iCachedWorldBoundsUpdateTick = gpGlobals->tickcount;

		if ( m_vecOrigins.Count() == 0 )
			return;

		Vector vecPos = m_vecOrigins[ 0 ];

		if ( m_bOriginsRelativeToCenter )
		{
			Vector vecTransformedPos;
			EntityToWorldSpace( vecPos, &vecTransformedPos );
			vecPos = vecTransformedPos;
		}

		m_vCachedWorldMins = vecPos;
		m_vCachedWorldMaxs = vecPos;

		if ( m_bOriginsRelativeToCenter )
		{
			for (int i = 1; i < m_vecOrigins.Count(); i++)
			{
				EntityToWorldSpace( m_vecOrigins[ i ], &vecPos );
				VectorMin( vecPos, m_vCachedWorldMins, m_vCachedWorldMins );
				VectorMax( vecPos, m_vCachedWorldMaxs, m_vCachedWorldMaxs );
			}
		}
		else
		{
			for (int i = 1; i < m_vecOrigins.Count(); i++)
			{
				vecPos = m_vecOrigins[ i ];
				VectorMin( vecPos, m_vCachedWorldMins, m_vCachedWorldMins );
				VectorMax( vecPos, m_vCachedWorldMaxs, m_vCachedWorldMaxs );
			}
		}

		m_vCachedRenderOrigin = ( m_vCachedWorldMaxs + m_vCachedWorldMins ) * 0.5f;

		Vector vRadius = ReplicateToVector( SCULPTURE_SURFACE_RADIUS * 12.0f );
		m_vCachedWorldMins -= vRadius;
		m_vCachedWorldMaxs += vRadius;
	}
}

const QAngle& C_IceSculpture::GetRenderAngles()
{
	return g_SurfaceRenderAnglesAngles;
}

const Vector& C_IceSculpture::GetRenderOrigin( void )
{
	UpdateRenderBoundsAndOriginWorldspace();
	return m_vCachedRenderOrigin;
}

void C_IceSculpture::GetRenderBoundsWorldspace( Vector& absMins, Vector& absMaxs )
{
	UpdateRenderBoundsAndOriginWorldspace();
	absMins = m_vCachedWorldMins;
	absMaxs = m_vCachedWorldMaxs;
}

void C_IceSculpture::GetRenderBounds( Vector& theMins, Vector& theMaxs )
{
	UpdateRenderBoundsAndOriginWorldspace();

	theMins = m_vCachedWorldMins - m_vCachedRenderOrigin;
	theMaxs = m_vCachedWorldMaxs - m_vCachedRenderOrigin;
}

const Vector& C_IceSculpture::WorldAlignMins() const
{
	UpdateRenderBoundsAndOriginWorldspace();
	return m_vCachedWorldMins;
}

const Vector& C_IceSculpture::WorldAlignMaxs() const
{
	UpdateRenderBoundsAndOriginWorldspace();
	return m_vCachedWorldMaxs;
}

const Vector& C_IceSculpture::WorldSpaceCenter()
{
	return GetRenderOrigin();
}

void C_IceSculpture::ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs )
{
	GetRenderBoundsWorldspace( *pMins, *pMaxs );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flags - 
// Output : int
//-----------------------------------------------------------------------------
int C_IceSculpture::DrawModel( int flags, const RenderableInstance_t &instance )
{
	// See if we should draw
	if ( m_bReadyToDraw == false )
		return 0;

	SetUpDrawLighting();

	IMaterial *pMaterial = materials->FindMaterial( "models/weapons/w_icegun/ice_surface", TEXTURE_GROUP_OTHER, true );

	// FIXME: Only do this once a frame for the first ice sculpture rendered
	if ( pMaterial->NeedsFullFrameBufferTexture() )
	{
		UpdateScreenEffectTexture();
	}

	int iNumParticles = 0;

	g_SurfaceRenderParticles.SetCount( m_vecOrigins->Count() );

	if ( m_bOriginsRelativeToCenter )
	{
		for ( int i = 0; i < m_vecOrigins->Count(); ++i )
		{
			Vector vecWorldCenter = m_vecOrigins[ i ];

			Vector vecTransformedWorldCenter;
			EntityToWorldSpace( vecWorldCenter, &vecTransformedWorldCenter );

			ImpParticleWithOneInterpolant* imp_particle = &(g_SurfaceRenderParticles[ i ]);
			imp_particle->center = vecTransformedWorldCenter;
			imp_particle->setFieldScale( SCULPTURE_SURFACE_RADIUS );
			imp_particle->interpolants1.set( 1.0f, 0.0f, 0.0f );
			imp_particle->interpolants1[ 3 ] = 0.0f;

			++iNumParticles;
		}
	}
	else
	{
		for ( int i = 0; i < m_vecOrigins->Count(); ++i )
		{
			Vector vecWorldCenter = m_vecOrigins[ i ];

			ImpParticleWithOneInterpolant* imp_particle = &(g_SurfaceRenderParticles[ i ]);
			imp_particle->center = vecWorldCenter;
			imp_particle->setFieldScale( SCULPTURE_SURFACE_RADIUS );
			imp_particle->interpolants1.set( 1.0f, 0.0f, 0.0f );
			imp_particle->interpolants1[ 3 ] = 0.0f;

			++iNumParticles;
		}
	}

	g_SurfaceRenderParticles.SetCountNonDestructively( iNumParticles );

	Surface_Draw( GetClientRenderable(), GetRenderOrigin(), pMaterial, 6.5f );
	return 1;
}

void C_IceSculpture::SetUpDrawLighting()
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

	// Set our 'fixed' lightcube
	if ( materials && materials->GetRenderContext() )
	{
		materials->GetRenderContext()->SetAmbientLightCube( m_lightingBoxColors );
	}
}