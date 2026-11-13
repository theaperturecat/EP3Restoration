//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#ifndef C_NPC_BLOB_TINY
#define C_NPC_BLOB_TINY

#ifdef _WIN32
#pragma once
#endif

#include "c_ai_basenpc.h"

class C_NPC_BlobTiny : public C_AI_BaseNPC
{
	DECLARE_CLASS( C_NPC_BlobTiny, C_AI_BaseNPC );
	DECLARE_CLIENTCLASS();

public:

	C_NPC_BlobTiny( void ) {}

	virtual IClientModelRenderable*	GetClientModelRenderable() { return NULL; }
	virtual void OnDataChanged( DataUpdateType_t type );
	virtual void ClientThink( void );
	virtual int DrawModel( int flags, const RenderableInstance_t& instance);
	virtual ShadowType_t ShadowCastType() { return SHADOWS_RENDER_TO_TEXTURE_DYNAMIC_CUSTOM; }

	RenderGroup_t GetRenderGroup( void ) 
	{	
		return RENDER_GROUP_OPAQUE;
	}

private:
	C_NPC_BlobTiny( const C_NPC_BlobTiny & ) {}

	float ComputeSquashStretch() const;
	void ComputeOriginAndRadii( float flSquashStretch, Vector *pOrigin, float *pMainRadius, float *pCrossRadius ) const;
	void RenderBlob( int nFlags );

	mutable float	m_flLastSquashStretch;
	mutable float	m_flLastSquashStretchTime;
	Vector	m_vecLastNonZeroBlobVelocity;
	float	m_flRadius;
	bool	m_bInFlight;
	bool	m_bHeld;
	bool	m_bIsSplitter;
	bool	m_bIsFastBlob;
	mutable bool	m_bReachedTargetVelocity;
	Vector	m_vecBlobVelocity;
};


#endif // C_NPC_BLOB_TINY