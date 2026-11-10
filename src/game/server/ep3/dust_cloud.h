//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#ifndef SMOKE_CLOUD_H
#define SMOKE_CLOUD_H

#include "baseparticleentity.h"

//==================================================
// DustCloud
//==================================================

class DustCloud : public CBaseParticleEntity
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( DustCloud, CBaseParticleEntity );
	DECLARE_SERVERCLASS();

	DustCloud();
	virtual bool KeyValue( const char *szKeyName, const char *szValue ); 
	void					SetEmit(bool bVal);
	static	DustCloud*		CreateDustCloud();

public:
	// Effect parameters. These will assume default values but you can change them.
	CNetworkVector( m_Color );
	CNetworkVar( float, m_Opacity );

	CNetworkVar( float, m_SpawnRate );			// How many particles per second.
	CNetworkVar( float, m_ParticleLifetime );		// How long do the particles live?
	CNetworkVar( float, m_StopEmitTime );			// When do I stop emitting particles?
	CNetworkVar( float, m_MinSpeed );				// Speed range.
	CNetworkVar( float, m_MaxSpeed );
	CNetworkVar( float, m_StartSize );			// Size ramp.
	CNetworkVar( float, m_EndSize );	
	CNetworkVar( float, m_SpawnRadius );
	CNetworkVar( float, m_MinDirectedSpeed );				// Speed range.
	CNetworkVar( float, m_MaxDirectedSpeed );
	CNetworkVar( bool, m_bEmit );

	CNetworkVar( int, m_nAttachment );
};


#endif
