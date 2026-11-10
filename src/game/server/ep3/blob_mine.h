//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#ifndef BLOB_MINE_H
#define BLOB_MINE_H
#ifdef _WIN32
#pragma once
#endif


#include "vphysics_interface.h"
#include "baseanimating.h"
#include "player_pickup.h"
#include "npc_blob_tiny.h"


#define BLOB_MINE_RADIUS 4.0f

//-----------------------------------------------------------------------------
// Tiny roller mine
//-----------------------------------------------------------------------------
class CBlobMine : public CBaseAnimating, public CDefaultPlayerPickupVPhysics
{
	DECLARE_CLASS( CBlobMine, CBaseAnimating );
	DECLARE_SERVERCLASS();

public:
	CBlobMine( void );
	virtual ~CBlobMine( void );

	virtual void	Spawn( void );
	virtual bool	CreateVPhysics();
	virtual void	VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );
	virtual void	OnRestore();
	void	AcidSplashTouch( CBaseEntity *pOther );
	void	Precache( void );
	void	OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason );
	void	OnPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t Reason );
	bool	ShouldSavePhysics()	{ return true; }
	void	SetRadius( float flRadius ) { m_flRadius = flRadius; }
	unsigned int PhysicsSolidMaskForEntity( void ) const;
	void	Attach( CBaseEntity *pOther );
	void	Event_Killed( const CTakeDamageInfo &info );
	void	SetDamageFactor( float flFactor );

	COutputEvent m_OnPhysGunDrop;
	COutputEvent m_OnPhysGunPickup;

protected:
	DECLARE_DATADESC();

	void Detonate();
	void Detach();

	CNetworkVar( bool,	m_bHeld );		//Whether or not the player is holding the mine
	CNetworkVar( float, m_flRadius );
	float	m_flDamageFactor;
	bool	m_bTimerStarted;
	IPhysicsMotionController	*m_pMotionController;
	CSimpleForceController	m_Controller;

	// Constraint used to stick us to a what we've hit
	IPhysicsConstraint *m_pConstraint;
};


#endif // BLOB_MINE_H
