//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: interfaces to co-operate with advisor
//
//=============================================================================//


#ifndef NPC_ADVISOR_ROAMING_H
#define NPC_ADVISOR_ROAMING_H


extern int g_interactionAdvisorImmobilize;
extern int g_interactionAdvisorRelease;


class IAdvisorCooperation;
class IAdvisorCooperationHost;

class IAdvisorCooperation
{
public:
	// Get NPC entity
	virtual CBaseEntity * Advisor_GetCoopEntity() = 0;

public:
	// Advisor requests to cooperate with the NPC
	// return true to accept cooperation request or false to decline
	virtual bool Advisor_RequestEnterCoopMode( IAdvisorCooperationHost *pHost ) = 0;

	// Advisor no longer wishes to cooperate with the NPC
	// abandon all cooperation activities and disconnect from the advisor
	virtual void Advisor_ExitCoopMode( IAdvisorCooperationHost *pHost ) = 0;

public:
	// Advisor requests the NPC to levitate
	// return a valid phys object that advisor will control for levitation
	//   or return NULL to decline levitation request
	// advisor will keep polling for levitation mode, so it's totally
	//   acceptable to return a valid phys object, but not obey its levitation
	//   until say current schedule is finished
	virtual IPhysicsObject * Advisor_RequestLevitationMode() = 0;

	// Advisor instructs the NPC that the levitation mode
	// should be terminated, phys object used for levitation can
	// be terminated
	virtual void Advisor_ExitLevitationMode() = 0;
};

class IAdvisorCooperationHost
{
public:
	// Advisor entity
	virtual CBaseEntity * AdvisorHost_GetEntity() = 0;

public:
	// Stop cooperating with advisor
	virtual void AdvisorHost_ExitCoopMode( IAdvisorCooperation *pCoop ) = 0;

	// Stop levitating with advisor
	virtual void AdvisorHost_ExitLevitationMode( IAdvisorCooperation *pCoop ) = 0;
};



//////////////////////////////////////////////////////////////////////////
//
// Default part of implementation for ease of inheritance
//
//////////////////////////////////////////////////////////////////////////

class CAdvisorCooperationDefaultImpl : public IAdvisorCooperation
{
public:
	CAdvisorCooperationDefaultImpl();

public:
	// Allow cooperating with advisor
	void Advisor_AllowCooperation( bool bAllow ) { m_advisor_bCooperate = bAllow; }
	bool Advisor_IsAllowedCooperation() { return m_advisor_bCooperate; }
	bool Advisor_IsLevitating() { return m_advisor_bLevitating; }
	IAdvisorCooperationHost * Advisor_GetHost();

	void Advisor_RememberShadowControllerSettings( IPhysicsShadowController *c );
	void Advisor_RestoreShadowControllerSettings( IPhysicsObject *p );

	virtual void Advisor_OnStartedLevitating() {}
	virtual void Advisor_OnFinishedLevitating() {}

public:
	// Get NPC entity
	virtual CBaseEntity * Advisor_GetCoopEntity();

public:
	// Advisor requests to cooperate with the NPC
	// return true to accept cooperation request or false to decline
	virtual bool Advisor_RequestEnterCoopMode( IAdvisorCooperationHost *pHost );

	// Advisor no longer wishes to cooperate with the NPC
	// abandon all cooperation activities and disconnect from the advisor
	virtual void Advisor_ExitCoopMode( IAdvisorCooperationHost *pHost );

public:
	// Advisor requests the NPC to levitate
	// return a valid phys object that advisor will control for levitation
	//   or return NULL to decline levitation request
	// advisor will keep polling for levitation mode, so it's totally
	//   acceptable to return a valid phys object, but not obey its levitation
	//   until say current schedule is finished
	virtual IPhysicsObject * Advisor_RequestLevitationMode();

	// Advisor instructs the NPC that the levitation mode
	// should be terminated, phys object used for levitation can
	// be terminated
	virtual void Advisor_ExitLevitationMode();

public:
	bool m_advisor_bCooperate;
	bool m_advisor_bLevitating;
	EHANDLE m_advisor_hEnt;

public:
	float m_shadow_flTeleportDist;
	float m_shadow_maxSpeed;
	float m_shadow_maxAngSpeed;
	unsigned m_shadow_flags;
	enum ShadowFlags_t
	{
		SF_CONTROLLER	= 1 << 0,
		SF_TRANSLATION	= 1 << 1,
		SF_ROTATION		= 1 << 2,
		SF_PHYSCTRL		= 1 << 3,
	};
};

// DEFINE_KEYFIELD( m_advisor_bCooperate, FIELD_BOOLEAN, "AdvisorCooperate" ),
// DEFINE_FIELD( m_advisor_bLevitating, FIELD_BOOLEAN ),
// DEFINE_FIELD( m_advisor_hEnt, FIELD_EHANDLE ),

// DEFINE_FIELD( m_shadow_flTeleportDist, FIELD_FLOAT ),
// DEFINE_FIELD( m_shadow_maxSpeed, FIELD_FLOAT ),
// DEFINE_FIELD( m_shadow_maxAngSpeed, FIELD_FLOAT ),
// DEFINE_FIELD( m_shadow_flags, FIELD_INTEGER ),



bool AdvisorRoaming_IsPullingPlayer( CBaseEntity *pPlayer );	// Will check all advisors around
bool AdvisorRoaming_IsPullingPlayer( CBaseEntity *pPlayer, CBaseEntity *pAdvisor );

bool AdvisorRoaming_RemapPlayerPhysDmg( CBaseEntity *pPlayer, CTakeDamageInfo const &info, CTakeDamageInfo &infoRemapped );


#endif // NPC_ADVISOR_ROAMING_H

