//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#ifndef C_BLOB_MINE
#define C_BLOB_MINE

#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimating.h"

class C_BlobMine : public C_BaseAnimating
{
	DECLARE_CLASS( C_BlobMine, C_BaseAnimating );
	DECLARE_CLIENTCLASS();

public:
	C_BlobMine( void );

	virtual void Activate( void );
	virtual IClientModelRenderable*	GetClientModelRenderable() { return NULL; }
	virtual void OnDataChanged( DataUpdateType_t updateType );
	virtual int DrawModel( int flags );
	virtual ShadowType_t ShadowCastType( void ) { return SHADOWS_RENDER_TO_TEXTURE_DYNAMIC_CUSTOM; }
	virtual void			GetRenderBoundsWorldspace( Vector& absMins, Vector& absMaxs );
	virtual void			GetRenderBounds( Vector& theMins, Vector& theMaxs );
	virtual void			ClientThink( void );
	virtual const Vector&	WorldAlignMins( void ) const;
	virtual const Vector&	WorldAlignMaxs( void ) const;
	// TODO: This sucks.... I should use a renderCenter variable.
	virtual const Vector&	WorldSpaceCenter( void );
	virtual void			ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs );

	virtual RenderGroup_t GetRenderGroup( void ) 
	{
		return RENDER_GROUP_OPAQUE;
	}

private:
	C_BlobMine( const C_BlobMine & ) {}
	void SimulateParticles();
	void SetUpDrawLighting();
	void UpdateRenderBounds() const;

	float	m_flRadius;
	bool	m_bHeld;
	Vector4D m_lightingBoxColors[6];	// WORK IN PROGRESS
	CUtlVector< Vector > m_vecOrigins;
	CUtlVector< Vector > m_vecVelocities;
	mutable Vector	m_vCachedWorldMins;
	mutable Vector	m_vCachedWorldMaxs;
	mutable int		m_iCachedWorldBoundsUpdateTick;

	friend class CBlobMineLightingMaterialProxy;
};


#endif // C_BLOB_MINE