//====== Copyright c 1996-2008, Valve Corporation, All rights reserved. =======//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef NPC_ADVISOR_ROAMING_SPIT_H
#define NPC_ADVISOR_ROAMING_SPIT_H
#ifdef _WIN32
#pragma once
#endif

#include "basegrenade_shared.h"

class CParticleSystem;

class CAdvisorGrenadeSpit : public CBaseGrenade
{
	DECLARE_CLASS( CAdvisorGrenadeSpit, CBaseGrenade );

public:
	CAdvisorGrenadeSpit( void );

public:
	enum Size_t
	{
		SPIT_SMALL,
		SPIT_MEDIUM,
		SPIT_LARGE,
	};

public:
	virtual void		Spawn( void );
	virtual void		Precache( void );
	virtual void		Event_Killed( const CTakeDamageInfo &info );

	virtual	unsigned int	PhysicsSolidMaskForEntity( void ) const { return ( BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_WATER ) & (~CONTENTS_GRATE); }

	void 				GrenadeSpitTouch( CBaseEntity *pOther );
	void				SetSpitSize( int nSize );
	void				Detonate( void );
	void				Think( void );
	void				SpitDoesPoisonDamage( bool bEnable );

public:
	static bool ComputeSpitParameters( CBaseEntity *pAI, Vector const &org, Vector const &tgt, Vector &params );

private:
	DECLARE_DATADESC();

	void	InitHissSound( void );

	CHandle< CParticleSystem >	m_hSpitEffect;
	CSoundPatch		*m_pHissSound;
	bool			m_bPlaySound;
	bool			m_bSpitDoesPoisonDamage;
};

#endif // #ifndef NPC_ADVISOR_ROAMING_SPIT_H
