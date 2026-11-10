//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Medic's portable power generator (Buff Station)
//
//===========================================================================//

#ifndef GAS_PUMP_H
#define GAS_PUMP_H

#ifdef _WIN32
#pragma once
#endif


#include "baseanimating.h"


class CRopeKeyframe;


//=============================================================================
//
// Portable Power Generator Class (Buff Station)
//
class CGasPump : public CBaseAnimating  
{
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();
	DECLARE_CLASS( CGasPump, CBaseAnimating );

public:
	CGasPump();

	void	Spawn();
	void	Precache();
	void	Activate();
	virtual void	UpdateOnRemove( void );

	// Attach/Detach 
	virtual int	ObjectCaps() { return BaseClass::ObjectCaps() | FCAP_IMPULSE_USE; };
	void	Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

private:
	void			InitAttachmentData( void );
	CRopeKeyframe	*CreateRope( CBasePlayer *pPlayer, int iAttachPoint );

	// Attach/Detach
	bool			IsPlayerAttached( CBasePlayer *pPlayer );

	void			AttachPlayer( CBasePlayer *pPlayer );
	void			DetachPlayer( );
	void			UpdatePlayerAttachment( CBasePlayer *pPlayer );

	// Input handlers
	void			InputPlayerSpawned( inputdata_t &inputdata );
	void			InputPlayerAttachedToGenerator( inputdata_t &inputdata );

	// Think
	void			BoostPlayerThink( void );

private:
	CNetworkVar( CHandle<CBasePlayer>,	m_hPlayer );
	CHandle<CRopeKeyframe>				m_hRope;
	int									m_iAttachPoint;
	float								m_flFractionalFuelAmount;
};

#endif // GAS_PUMP_H