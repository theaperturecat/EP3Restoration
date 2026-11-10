//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#ifndef NPC_BLOB_TINY_H
#define NPC_BLOB_TINY_H
#ifdef _WIN32
#pragma once
#endif


#include "vphysics_interface.h"
#include "ai_basenpc.h"
#include "player_pickup.h"


//-----------------------------------------------------------------------------
// Motion controller which just applies torques + forces
//-----------------------------------------------------------------------------
class CSimpleForceController : public IMotionEvent
{
	DECLARE_SIMPLE_DATADESC();

	// Inherited from IMotionEvent
public:
	virtual IMotionEvent::simresult_e Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular );

public:
	void Off( );
	void On( );
	bool IsOn( ) const;

public:
	AngularImpulse	m_vecAngular;
	Vector			m_vecLinear;

private:
	bool			m_fIsStopped;
};


//-----------------------------------------------------------------------------
// Inline methods
//-----------------------------------------------------------------------------
inline void CSimpleForceController::Off( ) 
{ 
	m_fIsStopped = true; 
}

inline void CSimpleForceController::On( ) 
{ 
	m_fIsStopped = false; 
}

inline bool CSimpleForceController::IsOn( ) const 
{ 
	return !m_fIsStopped; 
}


//-----------------------------------------------------------------------------
// Tiny roller mine
//-----------------------------------------------------------------------------
class CNPC_BlobTiny : public CAI_BaseNPC, public CDefaultPlayerPickupVPhysics
{
	DECLARE_CLASS( CNPC_BlobTiny, CAI_BaseNPC );
	DECLARE_SERVERCLASS();

public:
	CNPC_BlobTiny( void );
	virtual ~CNPC_BlobTiny( void );

	void SetSplitterBlob( bool bIsSplitter, int nMaxSplitCount = 0 );
	void SetFastBlob( bool bIsFastBlob ) { m_bIsFastBlob = bIsFastBlob; }
	virtual void	Spawn( void );
	virtual bool	CreateVPhysics();
	virtual void	VPhysicsUpdate( IPhysicsObject *pPhysics );
	void	RunAI();
	void			GatherConditions();
	virtual void	StartTask( const Task_t *pTask );
	virtual void	RunTask( const Task_t *pTask );
	void	AcidSplashTouch( CBaseEntity *pOther );
	virtual void	VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );
	virtual void	OnStartSchedule( int scheduleType );
	void	Precache( void );
	void	OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason );
	void	OnPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t Reason );
	bool	ShouldSavePhysics()	{ return true; }
	virtual void	OnRestore();
	bool	QuerySeeEntity(CBaseEntity *pSightEnt, bool bOnlyHateOrFearIfNPC = false );

	int		RangeAttack1Conditions ( float flDot, float flDist );
	int		SelectSchedule( void );
	int		TranslateSchedule( int scheduleType );
	virtual float		HearingSensitivity( void )		{ return 2.0f;	}

	virtual bool	OverrideMove( float flInterval ) { return true; }
	bool	IsValidEnemy( CBaseEntity *pEnemy );
	bool	IsPlayerVehicle( CBaseEntity *pEntity );
	float	GetStunDelay();
	void	UpdateEfficiency( bool bInPVS )	{ SetEfficiency( ( GetSleepState() != AISS_AWAKE ) ? AIE_DORMANT : AIE_NORMAL ); SetMoveEfficiency( AIME_NORMAL ); }
	void	SetRadius( float flRadius ) { m_flRadius = flRadius; }
	void	DrawDebugGeometryOverlays();
	unsigned int PhysicsSolidMaskForEntity( void ) const;
	Vector	EyePosition();

	Class_T	Classify();
	virtual bool ShouldGoToIdleState();
	virtual	void OnStateChange( NPC_STATE OldState, NPC_STATE NewState );
	virtual void Event_Killed( const CTakeDamageInfo &info );

	NPC_STATE SelectIdealState();

	void	Attach( CBaseEntity *pOther );

	COutputEvent m_OnPhysGunDrop;
	COutputEvent m_OnPhysGunPickup;

protected:
	DEFINE_CUSTOM_AI;
	DECLARE_DATADESC();

	void	PerformMovement( );

	bool	Detach( Vector *pLeapDir = NULL );

	bool	IsActive() { return m_flActiveTime > gpGlobals->curtime ? false : true; }
	void	ComputeHopTarget( Vector *pTarget, bool *pIsCeilingTarget, const Vector &vecGoal, CBaseEntity *pEnemy );
	void	ComputeHopAwayTarget( Vector *pTarget, bool *pIsCeilingTarget, const Vector &vecFleeTarget, const Vector &vecFleeVelocity );
	void	ComputeNearEnemyHopTarget( Vector *pTarget, bool *pIsCeilingTarget, const Vector &vecOriginalGoal, CBaseEntity *pEnemy );
	void	Hop( const Vector &vecHopTarget, bool bIsCeilingTarget, const Vector *pPreciseMovementPos );
	bool	ComputeCeilingTarget( Vector *pTarget, bool *pIsCeilingTarget );
	void	Split();
	void	ConstrainJumpDirection( Vector *pVecJumpDir );
	void	ConstrainJumpDirectionV2( Vector *pVecJumpDir );
	float	GetJumpDelay() const;

	// Input handlers
	void InputSplitOnce( inputdata_t &inputdata );

	CSimpleForceController		m_BlobController;
	IPhysicsMotionController	*m_pMotionController;

	float	m_flGoIdleTime;

	CNetworkVar( float, m_flActiveTime );	//If later than the current time, this will force the mine to be active

	struct HopConstraint_t
	{
		Vector m_vecConstraintNormal;
	};

	CHandle< CBasePlayer >	m_hHitPlayer;
	float	m_flNextHopTime;
	Vector	m_vecImpactNormal;
	CUtlVector< Vector > m_Constraints;
	Vector	m_vecImpactPosition;
	float	m_flNextSplitTime;
	int		m_nTaskSplitCount;
	bool	m_bIsFleeing;
	int		m_nMaxSplitCount;
	int		m_nSplitCount;
	float	m_flSplitOnceIgnoreTime;
	CNetworkVar( bool,	m_bIsFastBlob );
	CNetworkVar( bool,	m_bIsSplitter );
	CNetworkVar( Vector, m_vecBlobVelocity );
	CNetworkVar( bool,	m_bInFlight );
	CNetworkVar( bool,	m_bHeld );		//Whether or not the player is holding the mine
	CNetworkVar( float, m_flRadius );

	// Constraint used to stick us to a what we've hit
	IPhysicsConstraint *m_pConstraint;
};


#endif // NPC_BLOB_TINY_H
