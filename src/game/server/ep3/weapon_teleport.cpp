//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "basehlcombatweapon.h"
#include "NPCevent.h"
#include "basecombatcharacter.h"
#include "AI_BaseNPC.h"
#include "player.h"
#include "game.h"
#include "in_buttons.h"
#include "grenade_ar2.h"
#include "AI_Memory.h"
#include "soundent.h"
#include "rumble_shared.h"
#include "gamestats.h"
#include "basegrenade_shared.h"
#include "decals.h"
#include "shake.h"
#include "smoke_trail.h"
#include "ar2_explosion.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "world.h"
#include "physobj.h"
#include "datacache/imdlcache.h"
#include "particle_parse.h"
#include "IEffects.h"
#include "physics_collisionevent.h"
#include "saverestore_utlvector.h"

#include "weapon_teleport.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


class CWeaponTeleport;
class CTeleportProjectile;
void ShootTeleportProjectile( Vector vOrigin, QAngle angles, CBaseEntity *pOwner, CBaseEntity *pGun );


ConVar sk_teleport_projectile_vel( "sk_teleport_projectile_vel", "1000" );			// Velocity
ConVar sk_teleport_projectile_arc( "sk_teleport_projectile_arc", "5" );			// Pre-arc for shooting

ConVar sk_teleport_projectile_ang_limit( "sk_teleport_projectile_ang_limit", "0" );	// 1 = Limit possible shooting angles
ConVar sk_teleport_projectile_ang_min( "sk_teleport_projectile_ang_min", "-45" );	// min angle (down)
ConVar sk_teleport_projectile_ang_max( "sk_teleport_projectile_ang_max", "45" );	// max angle (up)

ConVar sk_teleport_projectile_max_dist( "sk_teleport_projectile_max_dist", "1024.0" );	// Max dist from the owner
ConVar sk_teleport_projectile_max_time( "sk_teleport_projectile_max_time", "3.5" );		// Max time alive, then self-destruct

ConVar sk_teleport_projectile_revoke_allow( "sk_teleport_projectile_revoke_allow", "0.2" );		// How soon teleport projectile can be revoked
ConVar sk_teleport_projectile_rechargetime( "sk_teleport_projectile_rechargetime", "3", FCVAR_REPLICATED );		// Time to recharge
ConVar sk_teleport_projectile_npc_gracedist( "sk_teleport_projectile_npc_gracedist", "512" );		// Distance to enemy to give grace time
ConVar sk_teleport_projectile_npc_gracetime( "sk_teleport_projectile_npc_gracetime", "1" );		// Time to allow tumbling before NPC teleports
ConVar sk_teleport_projectile_npc_gracetime2( "sk_teleport_projectile_npc_gracetime2", "3" );		// Time to allow after picked up with gravgun

ConVar sk_teleport_projectile_bounce_teleport( "sk_teleport_projectile_bounce_teleport", "1" );	// 1 = Teleport owner on bounce
ConVar sk_teleport_projectile_bounce_down( "sk_teleport_projectile_bounce_down", "3" );		// How much to angle towards the ground on bounce (0 = mirror bounce, 1 = solid drop)
ConVar sk_teleport_projectile_bounce_velf( "sk_teleport_projectile_bounce_velf", "0.5" );		// How much of velocity to retain on bounce

ConVar sk_teleport_projectile_opt_lookat( "sk_teleport_projectile_opt_lookat", "0" );	// How owner's look at point is set
ConVar sk_teleport_projectile_opt_velf( "sk_teleport_projectile_opt_velf", "0.3" );		// How much velocity owner inherits on teleport
ConVar sk_teleport_projectile_opt_radius( "sk_teleport_projectile_opt_radius", "30" );		// How much distance is used to resolve the teleportation location
ConVar sk_teleport_projectile_opt_physobj( "sk_teleport_projectile_opt_physobj", "1" );		// Whether projectile avoids a held physobj

ConVar sk_teleport_projectile_ready( "sk_teleport_projectile_ready", "0.4" );				// How often teleport ready sound sounds

ConVar sk_teleport_projectile_track( "sk_teleport_projectile_track", "1" );				// Whether projectile tracks path
ConVar sk_teleport_projectile_track_freq( "sk_teleport_projectile_track_freq", "0.3" );		// How often projectile tracks path
ConVar sk_teleport_projectile_track_speed( "sk_teleport_projectile_track_speed", "5" );		// How fast projectile replays player's path

ConVar sk_teleport_projectile_shake_amp( "sk_teleport_projectile_shake_amp", "15.0" );			// Teleportation screen shake effect settings
ConVar sk_teleport_projectile_shake_freq( "sk_teleport_projectile_shake_freq", "150.0" );
ConVar sk_teleport_projectile_shake_dur( "sk_teleport_projectile_shake_dur", "1.0" );
ConVar sk_teleport_projectile_shake_radius( "sk_teleport_projectile_shake_radius", "384.0" );

extern ConVar sv_special_suit_abilities;


//////////////////////////////////////////////////////////////////////////
//
// Projectile declaration
//
//////////////////////////////////////////////////////////////////////////

class CTeleportProjectile : public CBaseCombatCharacter, public CDefaultPlayerPickupVPhysics
{
	DECLARE_CLASS( CTeleportProjectile, CBaseCombatCharacter );
	typedef CDefaultPlayerPickupVPhysics BaseClass2;

public:

	CTeleportProjectile();
	~CTeleportProjectile();

	Class_T Classify() { return CLASS_NONE; }

public:

	void Spawn();
	void Activate();
	void Precache();
	void Shoot( Vector &vecVelocity, bool bBright );
	void SetSeekTarget( CBaseEntity *pTargetEntity );
	void Explode();

	void SetGun( CBaseEntity *pGun ) { m_hGun = pGun; }
	CBaseEntity *GetGun() { return m_hGun; }

	bool CreateVPhysics();

	unsigned int PhysicsSolidMaskForEntity() const;
	static CTeleportProjectile *ProjectileCreate( const Vector &vecOrigin, const QAngle &angAngles, CBaseEntity *pentOwner );

	void TeleportPerformTeleport();
	void TeleportPerformReset();

	bool CanTeleportPlayer( Vector const &pos, Vector const &off );

	bool IsValidPosition() const { return m_bValidPosition; }
	bool IsStablePosition() const { return m_iStablePosition > 0; }

	void SetNpcOwner( bool bNpcOwner = true ) { m_bNpcOwner = bNpcOwner; }
	bool IsNpcOwner() const { return m_bNpcOwner; }

	void CtrlPtSetTracking( bool bMustTrack = true ) { m_cpEnableTracking = bMustTrack; }
	bool CtrlPtIsTracking() const { return m_cpEnableTracking; }
	
	void CtrlPtInsert( float flTime, Vector const &pos, QAngle const &ang );
	void CtrlPtInsertForce( float flTime, Vector const &pos, QAngle const &ang );
	int  CtrlPtGetPtrs( float *&pTime, Vector *&pPos, QAngle *&pAng );
	bool CtrlPtGetData( float flTime, Vector *pPos, QAngle *pAng );
	bool CtrlPtGetDataTimeRange( float &flMin, float &flMax );

	void OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason );
	void VPhysicsUpdate( IPhysicsObject *pPhysics );
	void VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	void ScheduleRemove()
	{
		SetThink( &CBaseEntity::SUB_Remove );
		SetNextThink( gpGlobals->curtime + 0.1f );
	}

protected:

	void SetupGlobalModelData();

	void StickTo( CBaseEntity *pOther, trace_t &tr );

	void BubbleThink();
	void TeleportReadyThink();
	void TeleportAboutToExplodeThink();
	void DopplerThink();
	void SeekThink();
	void LimitThink();
	void TrackDataThink();
	void SimTeleportThink();

	bool CreateSprites( bool bBright );

	void ProjectileTouch( CBaseEntity *pOther );

	void UpdateOnRemove();

	void ScheduleStableExplodeTeleport( float flDelay );

	Vector	m_vecShootPosition;
	EHANDLE m_hSeekTarget;
	
	CHandle< CBaseEntity > m_hGun;
	int		m_iStablePosition;
	bool	m_bValidPosition;
	bool	m_bNpcOwner;
	bool	m_bInSolid;
	
	bool	m_cpEnableTracking;
	CUtlVector< float > m_cpTrackDataTime;
	CUtlVector< Vector > m_cpTrackDataPos;
	CUtlVector< QAngle > m_cpTrackDataAng;

	Vector	m_velTeleportEnt;
	float	m_flTeleportSimTime;

	DECLARE_DATADESC();
	//DECLARE_SERVERCLASS();
};


//////////////////////////////////////////////////////////////////////////
//
// Weapon declaration
//
//////////////////////////////////////////////////////////////////////////


class CWeaponTeleport : public CBaseHLCombatWeapon
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CWeaponTeleport, CBaseHLCombatWeapon );

	CWeaponTeleport();
	~CWeaponTeleport();

  	DECLARE_SERVERCLASS();

// Client needs attack times networked so that HUD could compute charge
// 	IMPLEMENT_NETWORK_VAR_FOR_DERIVED( m_flNextPrimaryAttack );
// 	IMPLEMENT_NETWORK_VAR_FOR_DERIVED( m_flNextSecondaryAttack );

	void	Precache( void );
	
	void	PrimaryAttack( void );
	void	SecondaryAttack( void );
	bool	Reload( void );

	virtual void Equip( CBaseCombatCharacter *pOwner );
	bool	HasAnyAmmo( void ) { return true; }

	bool IsSpecialSuitAbility( void ) { return true && sv_special_suit_abilities.GetBool(); }

	int		CapabilitiesGet( void ) { return bits_CAP_WEAPON_RANGE_ATTACK1; }
	int		WeaponRangeAttack2Condition( float flDot, float flDist );
	Activity	GetPrimaryAttackActivity( void );

	const WeaponProficiencyInfo_t *GetProficiencyValues();

	void FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir );
	void Operator_ForceNPCFire( CBaseCombatCharacter  *pOperator, bool bSecondary, CBaseEntity *pTarget = NULL );
	void Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );

	void UpdateOnRemove();

	void AddFiredProjectile( CBaseEntity *pProjectile );
	void RemoveFiredProjectile( CBaseEntity *pProjectile );

	CTeleportProjectile * GetProjectile() { return dynamic_cast< CTeleportProjectile * >( m_Projectile.Get() ); }

protected:
	typedef CHandle< CBaseEntity > FiredProjectileHandle;
	// CUtlVector< FiredProjectileHandle > m_arrFiredProjectiles;
	FiredProjectileHandle m_Projectile;
};

IMPLEMENT_SERVERCLASS_ST(CWeaponTeleport, DT_WeaponTeleport)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_teleport, CWeaponTeleport );
PRECACHE_WEAPON_REGISTER( weapon_teleport );


BEGIN_DATADESC( CWeaponTeleport )

// 	DEFINE_FIELD( m_nDetonating, FIELD_INTEGER ),
	
// 	DEFINE_FUNCTION( BurstThink ),

END_DATADESC()




//////////////////////////////////////////////////////////////////////////
//
// Temp Implementation
//
//////////////////////////////////////////////////////////////////////////

namespace TempTeleport
{
	typedef CUtlVector< CWeaponTeleport * > WeaponTeleportList;
	WeaponTeleportList & GetWeaponTeleportList()
	{
		static WeaponTeleportList s_lst;
		return s_lst;
	}
};

void FireTeleportProjectileFn()
{
	TempTeleport::WeaponTeleportList &lst = TempTeleport::GetWeaponTeleportList();
	for ( int k = 0; k < lst.Count(); ++ k )
	{
		CWeaponTeleport *pWeapon = lst[ k ];
		pWeapon->PrimaryAttack();
	}
}

CON_COMMAND( fire_teleport_projectile, "Fires teleport projectile" )
{
	FireTeleportProjectileFn();
}

ConVar dev_test_fire_teleport_projectile( "dev_test_fire_teleport_projectile", "0" );

//////////////////////////////////////////////////////////////////////////
//
// Implementation
//
//////////////////////////////////////////////////////////////////////////

//=========================================================
CWeaponTeleport::CWeaponTeleport( )
{
	m_fMinRange1		= 0;// No minimum range. 
	m_fMaxRange1		= 1400;

	// m_bReloadsSingly	= true;
	m_bFiresUnderwater	= false;

	TempTeleport::GetWeaponTeleportList().AddToTail( this );
}

CWeaponTeleport::~CWeaponTeleport()
{
	TempTeleport::GetWeaponTeleportList().FindAndRemove( this );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponTeleport::Precache( void )
{
	UTIL_PrecacheOther("teleport_projectile");

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: Give this weapon longer range when wielded by an ally NPC.
//-----------------------------------------------------------------------------
void CWeaponTeleport::Equip( CBaseCombatCharacter *pOwner )
{
	if( pOwner->Classify() == CLASS_PLAYER_ALLY )
	{
		m_fMaxRange1 = 3000;
	}
	else
	{
		m_fMaxRange1 = 1400;
	}

	BaseClass::Equip( pOwner );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponTeleport::FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir )
{
	WeaponSound( SINGLE_NPC );

	CSoundEnt::InsertSound( SOUND_COMBAT|SOUND_CONTEXT_GUNFIRE, pOperator->GetAbsOrigin(), SOUNDENT_VOLUME_MACHINEGUN, 0.2, pOperator, SOUNDENT_CHANNEL_WEAPON, pOperator->GetEnemy() );
	pOperator->FireBullets( 1, vecShootOrigin, vecShootDir, VECTOR_CONE_PRECALCULATED,
		MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 2, entindex(), 0 );

	pOperator->DoMuzzleFlash();
	m_iClip1 = m_iClip1 - 1;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponTeleport::Operator_ForceNPCFire( CBaseCombatCharacter *pOperator, bool bSecondary, CBaseEntity *pTarget )
{
	// Ensure we have enough rounds in the clip
	m_iClip1++;

	Vector vecShootOrigin, vecShootDir;
	QAngle	angShootDir;
	GetAttachment( LookupAttachment( "muzzle" ), vecShootOrigin, angShootDir );

	if ( pTarget )
	{
		vecShootDir = pTarget->WorldSpaceCenter() - vecShootOrigin;
	}
	else
	{
		AngleVectors( angShootDir, &vecShootDir );
	}

	FireNPCPrimaryAttack( pOperator, vecShootOrigin, vecShootDir );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponTeleport::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	switch( pEvent->Event() )
	{
	case EVENT_WEAPON_SMG1:
		{
			Vector vecShootOrigin, vecShootDir;
			QAngle angDiscard;

			// Support old style attachment point firing
			if ((pEvent->options == NULL) || (pEvent->options[0] == '\0') || (!pOperator->GetAttachment(pEvent->options, vecShootOrigin, angDiscard)))
			{
				vecShootOrigin = pOperator->Weapon_ShootPosition();
			}

			CAI_BaseNPC *npc = pOperator->MyNPCPointer();
			ASSERT( npc != NULL );
			vecShootDir = npc->GetActualShootTrajectory( vecShootOrigin );

			FireNPCPrimaryAttack( pOperator, vecShootOrigin, vecShootDir );
		}
		break;

	default:
		BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Activity
//-----------------------------------------------------------------------------
Activity CWeaponTeleport::GetPrimaryAttackActivity( void )
{
	return ACT_VM_PRIMARYATTACK;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponTeleport::PrimaryAttack( void )
{
	CTeleportProjectile *pProj = GetProjectile();
	if ( pProj )
	{
		if ( gpGlobals->curtime >= m_flNextPrimaryAttack )
		{
			pProj->TeleportPerformReset();
			m_flNextPrimaryAttack = gpGlobals->curtime + sk_teleport_projectile_revoke_allow.GetFloat();
		}

		return;
	}

	if ( gpGlobals->curtime < m_flNextSecondaryAttack )
		return;
	
	m_flNextSecondaryAttack = gpGlobals->curtime + sk_teleport_projectile_rechargetime.GetFloat();
	m_flNextPrimaryAttack = gpGlobals->curtime + sk_teleport_projectile_revoke_allow.GetFloat();

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	if ( pOwner )
	{
		m_iPrimaryAttacks++;
		gamestats->Event_WeaponFired( pOwner, true, GetClassname() );
	}

	// Launch the projectile
	Vector vecSrc = pOwner->Weapon_ShootPosition();
	Vector vecDir = pOwner->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );
	QAngle angDir;
	VectorAngles( vecDir, angDir );

	angDir.x -= sk_teleport_projectile_arc.GetFloat();

	// Limit the angle
	if ( sk_teleport_projectile_ang_limit.GetBool() )
	{
		while ( angDir.x < -180.0f )
			angDir.x += 360.0f;
		while ( angDir.x > 180.0f )
			angDir.x -= 360.0f;

		if ( angDir.x > - sk_teleport_projectile_ang_min.GetFloat() )
			angDir.x = - sk_teleport_projectile_ang_min.GetFloat();
		if ( angDir.x < - sk_teleport_projectile_ang_max.GetFloat() )
			angDir.x = - sk_teleport_projectile_ang_max.GetFloat();
	}

	// Determine if the object that the player is holding will be blocking
	// teleport projectile's path
	if ( sk_teleport_projectile_opt_physobj.GetBool() )
	{
		Vector vecLaunch;
		AngleVectors( angDir, &vecLaunch );
		vecLaunch.NormalizeInPlace();

		const float flTraceLen = 100.0;
		Vector vecEnd;
		trace_t tr;
		
recheck_traceline_against_held_physobj:
		vecEnd = vecSrc + flTraceLen * vecLaunch;
		UTIL_TraceLine( vecSrc, vecEnd, MASK_PLAYERSOLID, pOwner, COLLISION_GROUP_NONE, &tr );

		if ( tr.fraction < 1.0 && tr.m_pEnt )
		{
			if ( IPhysicsObject *pPhysObj = tr.m_pEnt->VPhysicsGetObject() )
			{
				if ( pPhysObj->GetGameFlags() & FVPHYSICS_PLAYER_HELD )
				{
					// Do a second trace from inside the phys object
					vecSrc += ( tr.fraction * flTraceLen + 0.5 ) * vecLaunch;
					vecEnd = vecSrc + flTraceLen * vecLaunch;

					trace_t tr2;
					UTIL_TraceLine( vecSrc, vecEnd, MASK_PLAYERSOLID, pOwner, COLLISION_GROUP_NONE, &tr2 );
					if ( tr2.fraction < 1.0 && tr2.m_pEnt == tr.m_pEnt )
					{
						vecSrc += ( tr2.fraction * flTraceLen + 0.5 ) * vecLaunch;
						goto recheck_traceline_against_held_physobj;
					}
				}
			}
		}
	}

	ShootTeleportProjectile( vecSrc, angDir, pOwner, this );
}

void CWeaponTeleport::SecondaryAttack()
{
// 	if ( gpGlobals->curtime < m_flNextSecondaryAttack )
// 		return;
// 
// 	// m_flNextPrimaryAttack = gpGlobals->curtime + sk_teleport_projectile_rechargetime.GetFloat();
// 	m_flNextSecondaryAttack = gpGlobals->curtime + sk_teleport_projectile_revoke_allow.GetFloat();
// 
// 	CTeleportProjectile *pProj = GetProjectile();
// 	if ( !pProj )
// 	{
// 		WeaponSound( EMPTY );
// 		return;
// 	}
// 
// 	pProj->TeleportPerformTeleport();
}

bool CWeaponTeleport::Reload()
{
	return true;
// 	if ( gpGlobals->curtime < m_flNextPrimaryAttack )
// 		return false;
// 
// 	m_flNextPrimaryAttack = gpGlobals->curtime + sk_teleport_projectile_rechargetime.GetFloat();
// 	m_flNextSecondaryAttack = gpGlobals->curtime + sk_teleport_projectile_rechargetime.GetFloat();
// 
// 	CTeleportProjectile *pProj = GetProjectile();
// 	if ( !pProj )
// 	{
// 		WeaponSound( EMPTY );
// 		return false;
// 	}
// 
// 	pProj->TeleportPerformReset();
// 
// 	return true;
}

void CWeaponTeleport::UpdateOnRemove()
{
	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flDot - 
//			flDist - 
// Output : int
//-----------------------------------------------------------------------------
int CWeaponTeleport::WeaponRangeAttack2Condition( float flDot, float flDist )
{
	// CAI_BaseNPC *npcOwner = GetOwner()->MyNPCPointer();

	return COND_NONE;
}

//-----------------------------------------------------------------------------
const WeaponProficiencyInfo_t *CWeaponTeleport::GetProficiencyValues()
{
	static WeaponProficiencyInfo_t proficiencyTable[] =
	{
		{ 7.0,		0.75	},
		{ 5.00,		0.75	},
		{ 10.0/3.0, 0.75	},
		{ 5.0/3.0,	0.75	},
		{ 1.00,		1.0		},
	};

	COMPILE_TIME_ASSERT( ARRAYSIZE(proficiencyTable) == WEAPON_PROFICIENCY_PERFECT + 1);

	return proficiencyTable;
}

void CWeaponTeleport::AddFiredProjectile( CBaseEntity *pProjectile )
{
	FiredProjectileHandle h;
	h = pProjectile;

	Assert( NULL == GetProjectile() );
	m_Projectile = h;
}

void CWeaponTeleport::RemoveFiredProjectile( CBaseEntity *pProjectile )
{
// 	FiredProjectileHandle h;
// 	h = pProjectile;

	Assert( pProjectile == GetProjectile() );

	m_Projectile.Term();
}

//////////////////////////////////////////////////////////////////////////
//
// Projectile implementation
//
//////////////////////////////////////////////////////////////////////////


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static const char* TELEPORT_PROJECTILE_MODEL = "models/weapons/w_grenade.mdl";//"models/props_combine/teleporter_item.mdl"; theaperturecat models
static const char *TELEPORT_PROJECTILE_SPRITE = "striderbuster_trail";

static const char *s_szTeleportProjectileBubbles = "TeleportProjectileBubbles";
static const char *s_szTeleportProjectileSeekThink = "TeleportProjectileSeekThink";
static const char *s_szTeleportProjectileLimitThink = "TeleportProjectileLimitThink";
static const char *s_szTeleportProjectileTrackDataThink = "TeleportProjectileTrackDataThink";
static const char *s_szTeleportProjectileResetThink = "TeleportProjectileResetThink";
static const char *s_szTeleportProjectileTeleportThink = "TeleportProjectileTeleportThink";
static const char *s_szTeleportProjectileTeleportReadyThink = "TeleportProjectileTeleportReadyThink";
static const char *s_szTeleportProjectileTeleportAboutToExplodeThink = "TeleportProjectileTeleportAboutToExplodeThink";
static const char *s_szTeleportProjectileSpriteTrail = "sprites/bluelaser1.vmt";
static int s_nTeleportProjectileImpact = -2;
static int s_nTeleportProjectileAttach = -1;


LINK_ENTITY_TO_CLASS( teleport_projectile, CTeleportProjectile );

BEGIN_DATADESC( CTeleportProjectile )

	DEFINE_THINKFUNC( BubbleThink ),
	DEFINE_THINKFUNC( TeleportReadyThink ),
	DEFINE_THINKFUNC( TeleportAboutToExplodeThink ),
	DEFINE_THINKFUNC( DopplerThink ),
	DEFINE_THINKFUNC( SeekThink ),
	DEFINE_THINKFUNC( LimitThink ),
	DEFINE_THINKFUNC( TrackDataThink ),
	DEFINE_THINKFUNC( SimTeleportThink ),

	DEFINE_THINKFUNC( TeleportPerformTeleport ),
	DEFINE_THINKFUNC( TeleportPerformReset ),

	DEFINE_ENTITYFUNC( ProjectileTouch ),

	DEFINE_FIELD( m_vecShootPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_hSeekTarget, FIELD_EHANDLE ),

	DEFINE_FIELD( m_iStablePosition, FIELD_INTEGER ),
	DEFINE_FIELD( m_bValidPosition, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bNpcOwner, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bInSolid, FIELD_BOOLEAN ),
	
	DEFINE_FIELD( m_cpEnableTracking, FIELD_BOOLEAN ),
	DEFINE_UTLVECTOR( m_cpTrackDataTime, FIELD_TIME ),
	DEFINE_UTLVECTOR( m_cpTrackDataPos, FIELD_POSITION_VECTOR ),
	DEFINE_UTLVECTOR( m_cpTrackDataAng, FIELD_VECTOR ),

	DEFINE_FIELD( m_velTeleportEnt, FIELD_VECTOR ),
	DEFINE_FIELD( m_flTeleportSimTime, FIELD_TIME ),

END_DATADESC()

//IMPLEMENT_SERVERCLASS_ST( CTeleportProjectile, DT_TeleportProjectile )
//END_SEND_TABLE()


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CTeleportProjectile *CTeleportProjectile::ProjectileCreate( const Vector &vecOrigin, const QAngle &angAngles, CBaseEntity *pentOwner )
{
	// Create a new entity with CTeleportProjectile private data
	// CTeleportProjectile *pProjectile = (CTeleportProjectile *)CreateEntityByName( "teleport_projectile" );
	// UTIL_SetOrigin( pProjectile, vecOrigin );
	// pProjectile->SetAbsAngles( angAngles );
	CTeleportProjectile *pProjectile = ( CTeleportProjectile * ) CBaseEntity::Create( "teleport_projectile", vecOrigin, angAngles, pentOwner );
	pProjectile->Spawn();
	pProjectile->Activate();
	// pProjectile->SetOwnerEntity( pentOwner );

	return pProjectile;
}


void ShootTeleportProjectile( Vector vOrigin, QAngle angles, CBaseEntity *pOwner, CBaseEntity *pGun )
{
	CTeleportProjectile *entity = CTeleportProjectile::ProjectileCreate( vOrigin, angles, pOwner );
	if ( entity )
	{
 		entity->Precache();
		entity->SetGun( pGun );
 		DispatchSpawn( entity );

		// Shoot the projectile.
		Vector forward;
		AngleVectors( angles, &forward );

		if ( sk_teleport_projectile_track.GetBool() )
		{
			entity->CtrlPtSetTracking();
		}

		entity->Shoot( forward * sk_teleport_projectile_vel.GetFloat(), false );
	}
}

CBaseEntity *ShootTeleportProjectileNPC( Vector vOrigin, Vector vTossVelocity, CBaseEntity *pOwner )
{
	QAngle angles;
	VectorAngles( vTossVelocity, angles );

	CTeleportProjectile *entity = CTeleportProjectile::ProjectileCreate( vOrigin, angles, pOwner );
	
	if ( entity )
	{
		entity->SetNpcOwner( true );
 		entity->Precache();
 		DispatchSpawn( entity );

		// Toss the projectile.
		entity->Shoot( vTossVelocity, false );
	}

	return entity;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CTeleportProjectile::CTeleportProjectile()
{
 	UseClientSideAnimation();
	m_iStablePosition = 0;
	m_bValidPosition = false;
	m_bNpcOwner = false;
	m_bInSolid = false;
	m_cpEnableTracking = false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CTeleportProjectile::~CTeleportProjectile()
{
}


//-----------------------------------------------------------------------------
// If set, the projectile will seek unerringly toward the target as it flies.
//-----------------------------------------------------------------------------
void CTeleportProjectile::SetSeekTarget( CBaseEntity *pTargetEntity )
{
	if ( pTargetEntity )
	{
		m_hSeekTarget = pTargetEntity;
		SetContextThink( &CTeleportProjectile::SeekThink, gpGlobals->curtime, s_szTeleportProjectileSeekThink );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CTeleportProjectile::CreateVPhysics()
{
	// Create the object in the physics system

	const float flSize = 4;
	Vector vecMax( flSize, flSize, flSize );
	SetSize( -vecMax, vecMax );

	SetCollisionGroup( COLLISION_GROUP_WEAPON );

	IPhysicsObject *pObj;

	pObj = VPhysicsInitNormal( SOLID_BBOX, FSOLID_NOT_STANDABLE, false );

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
unsigned int CTeleportProjectile::PhysicsSolidMaskForEntity() const
{
	return ( BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_HITBOX );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CTeleportProjectile::CreateSprites( bool bBright )
{
	if ( bBright )
	{
		DispatchParticleEffect( "hunter_flechette_trail_striderbuster", PATTACH_ABSORIGIN_FOLLOW, this );
	}
	else
	{
		DispatchParticleEffect( TELEPORT_PROJECTILE_SPRITE, PATTACH_ABSORIGIN_FOLLOW, this );
	}

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::Spawn()
{
	Precache( );

	SetModel( TELEPORT_PROJECTILE_MODEL );
	
	SetMoveType( MOVETYPE_VPHYSICS );
	
	CreateVPhysics();
	// SetGravity( sk_teleport_projectile_gravity.GetFloat() );

	// Make sure we're updated if we're underwater
	UpdateWaterState();

	// Make us glow until we've hit the wall
	m_nSkin = 1;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::Activate()
{
	BaseClass::Activate();
	SetupGlobalModelData();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::SetupGlobalModelData()
{
	if ( s_nTeleportProjectileImpact == -2 )
	{
		s_nTeleportProjectileImpact = LookupSequence( "impact" );
		s_nTeleportProjectileAttach = LookupAttachment( "attach_fuse" );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::Precache()
{
	PrecacheModel( TELEPORT_PROJECTILE_MODEL );
	PrecacheModel( "sprites/light_glow02_noz.vmt" );

	PrecacheScriptSound( "NPC_Hunter.FlechetteNearmiss" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteHitBody" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteHitWorld" );
	PrecacheScriptSound( "NPC_Hunter.FlechettePreExplode" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteExplode" );

	PrecacheScriptSound( "Grenade.Blip" );

	PrecacheParticleSystem( "hunter_flechette_trail_striderbuster" );
	PrecacheParticleSystem( TELEPORT_PROJECTILE_SPRITE );
	PrecacheParticleSystem( "hunter_projectile_explosion_1" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::StickTo( CBaseEntity *pOther, trace_t &tr )
{
	EmitSound( "NPC_Hunter.FlechetteHitWorld" );

	// Bullseyes don't have valid bones so don't attach to them because it causes
	// a crash in bone setup.
	if ( !pOther->IsWorld() && ( pOther->Classify() != CLASS_BULLSEYE ) )
	{
		SetParent( pOther );
		SetSolid( SOLID_NONE );
		SetSolidFlags( FSOLID_NOT_SOLID );
		SetMoveType( MOVETYPE_NONE );
	}
	else
	{
		CreateVPhysics();

		if ( VPhysicsGetObject() )
		{
			SetMoveType( MOVETYPE_VPHYSICS );
			VPhysicsGetObject()->EnableMotion( false );
			AddSpawnFlags( SF_PHYSPROP_ENABLE_ON_PHYSCANNON );
			UTIL_SetSize( this, -Vector(8,8,8), Vector(8,8,8) );
		}
	}


	SetTouch( NULL );

	// We're no longer flying. Stop checking for water volumes.
	SetContextThink( NULL, 0, s_szTeleportProjectileBubbles );

	// Stop seeking.
	m_hSeekTarget = NULL;
	SetContextThink( NULL, 0, s_szTeleportProjectileSeekThink );

	m_iStablePosition = 1;

	// Get ready to teleport
	TeleportReadyThink();

	// Play our impact animation.
	ResetSequence( s_nTeleportProjectileImpact );

	// Leave a trace
	UTIL_ImpactTrace( &tr, DMG_BULLET );

	// Shoot some sparks
	if ( UTIL_PointContents( GetAbsOrigin(), CONTENTS_WATER ) != CONTENTS_WATER)
	{
		g_pEffects->Sparks( GetAbsOrigin() );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::ProjectileTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet(FSOLID_VOLUME_CONTENTS | FSOLID_TRIGGER) )
	{
		// Some NPCs are triggers that can take damage (like antlion grubs). We should hit them.
		if ( ( pOther->m_takedamage == DAMAGE_NO ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) )
			return;
	}

	if ( FClassnameIs( pOther, "teleport_projectile" ) )
		return;

	trace_t	tr;
	tr = BaseClass::GetTouchTrace();


	// Should teleport on bounce
	if ( sk_teleport_projectile_bounce_teleport.GetBool() )
	{
		// Check if player is hitting the floor (surface with no more than 30 degree incline)
		if ( IsNpcOwner() || ( tr.plane.normal.z > 0.87 ) ) // 0.87 = cos(30)
		{
			TeleportPerformTeleport();
			return;
		}
		// otherwise bounce
	}

	// Simply bounce the projectile
	static const bool bBounce = true;
	if ( bBounce )
	{
		Vector vel = GetAbsVelocity();
		float flDotAngle = vel.Dot( tr.plane.normal );
		if ( flDotAngle > 0 )
			return; // let it fly through

		// Bounce
		float flMagnitude = vel.Length();
		vel -= 2 * flDotAngle * tr.plane.normal;

		// Now make the adjustment to go down
		vel.NormalizeInPlace();
		vel += Vector( 0, 0, -1 ) * sk_teleport_projectile_bounce_down.GetFloat();
		vel.NormalizeInPlace();

		vel *= flMagnitude * sk_teleport_projectile_bounce_velf.GetFloat();

		SetAbsVelocity( vel );

		Vector pos = GetAbsOrigin();
		pos += tr.plane.normal / 2.0f;
		SetAbsOrigin( pos );

		return;
	}


	if ( pOther->m_takedamage != DAMAGE_NO )
	{
		SetAbsVelocity( Vector( 0, 0, 0 ) );

		// play body "thwack" sound
		EmitSound( "NPC_Hunter.FlechetteHitBody" );

		StopParticleEffects( this );

		Vector vForward;
		AngleVectors( GetAbsAngles(), &vForward );
		VectorNormalize ( vForward );

		trace_t	tr2;
		UTIL_TraceLine( GetAbsOrigin(),	GetAbsOrigin() + vForward * 128, MASK_BLOCKLOS, pOther, COLLISION_GROUP_NONE, &tr2 );

		if ( ( ( pOther->GetMoveType() == MOVETYPE_VPHYSICS ) ||
			   ( pOther->GetMoveType() == MOVETYPE_PUSH ) ) &&
			 ( ( pOther->GetHealth() > 0 ) ||
			   ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) ) )
		{
			CPhysicsProp *pProp = dynamic_cast<CPhysicsProp *>( pOther );
			if ( pProp )
			{
				pProp->SetInteraction( PROPINTER_PHYSGUN_NOTIFY_CHILDREN );
			}

			// We hit a physics object that survived the impact. Stick to it.
			StickTo( pOther, tr );
		}
		else
		{
			TeleportPerformReset();
		}
	}
	else
	{
		// See if we struck the world
		if ( pOther->GetMoveType() == MOVETYPE_NONE && !( tr.surface.flags & SURF_SKY ) )
		{
			// We hit a physics object that survived the impact. Stick to it.
			StickTo( pOther, tr );
		}
		else if( pOther->GetMoveType() == MOVETYPE_PUSH && FClassnameIs(pOther, "func_breakable") )
		{
			// We hit a func_breakable, stick to it.
			// The MOVETYPE_PUSH is a micro-optimization to cut down on the classname checks.
			StickTo( pOther, tr );
		}
		else
		{
			TeleportPerformReset();
		}
	}
}


//-----------------------------------------------------------------------------
// Fixup projectile position when seeking towards a target.
//-----------------------------------------------------------------------------
void CTeleportProjectile::SeekThink()
{
	if ( m_hSeekTarget )
	{
		Vector vecBodyTarget = m_hSeekTarget->BodyTarget( GetAbsOrigin() );

		Vector vecClosest;
		CalcClosestPointOnLineSegment( GetAbsOrigin(), m_vecShootPosition, vecBodyTarget, vecClosest, NULL );

		Vector vecDelta = vecBodyTarget - m_vecShootPosition;
		VectorNormalize( vecDelta );

		QAngle angShoot;
		VectorAngles( vecDelta, angShoot );

		float flSpeed = sk_teleport_projectile_vel.GetFloat();
		if ( !flSpeed )
		{
			flSpeed = 2500.0f;
		}

		Vector vecVelocity = vecDelta * flSpeed;
		Teleport( &vecClosest, &angShoot, &vecVelocity );

		SetNextThink( gpGlobals->curtime, s_szTeleportProjectileSeekThink );
	}
}


//-----------------------------------------------------------------------------
// Play a near miss sound as we travel past the player.
//-----------------------------------------------------------------------------
void CTeleportProjectile::DopplerThink()
{
	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if ( !pPlayer )
		return;

	Vector vecVelocity = GetAbsVelocity();
	VectorNormalize( vecVelocity );

	float flMyDot = DotProduct( vecVelocity, GetAbsOrigin() );
	float flPlayerDot = DotProduct( vecVelocity, pPlayer->GetAbsOrigin() );

	if ( flPlayerDot <= flMyDot )
	{
		EmitSound( "NPC_Hunter.FlechetteNearMiss" );

		// We've played the near miss sound and we're not seeking. Stop thinking.
		SetThink( NULL );
	}
	else
	{
		SetNextThink( gpGlobals->curtime );
	}
}


//-----------------------------------------------------------------------------
// Think every 0.1 seconds to make bubbles if we're flying through water.
//-----------------------------------------------------------------------------
void CTeleportProjectile::BubbleThink()
{
	SetNextThink( gpGlobals->curtime + 0.1f, s_szTeleportProjectileBubbles );

	if ( GetWaterLevel()  == 0 )
		return;

	UTIL_BubbleTrail( GetAbsOrigin() - GetAbsVelocity() * 0.1f, GetAbsOrigin(), 5 );
}

//
// Think every 0.1 seconds to comply with teleport projectile limits
//
void CTeleportProjectile::LimitThink()
{
	CBaseEntity *pEnt = GetOwnerEntity();
	if ( !pEnt )
	{
		TeleportPerformReset();
		return;
	}

	if ( IsNpcOwner() )
	{
		SetNextThink( gpGlobals->curtime + 0.1, s_szTeleportProjectileLimitThink );
		return;
	}

	Vector vecPlayer = pEnt->GetAbsOrigin();
	Vector vecProj = GetAbsOrigin();

	if ( ( vecProj - vecPlayer ).Length() >= sk_teleport_projectile_max_dist.GetFloat() )
	{
		SetAbsVelocity( Vector( 0, 0, 0 ) );
		SetContextThink( NULL, 0, s_szTeleportProjectileLimitThink );
	}
	else
	{
		SetNextThink( gpGlobals->curtime + 0.1, s_szTeleportProjectileLimitThink );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::Shoot( Vector &vecVelocity, bool bBrightFX )
{
	CreateSprites( bBrightFX );

	m_vecShootPosition = GetAbsOrigin();

	SetAbsVelocity( vecVelocity );

	Vector vecSpin;
	vecSpin.x = random->RandomFloat( -1000.0, 1000.0 );
	vecSpin.y = random->RandomFloat( -1000.0, 1000.0 );
	vecSpin.z = random->RandomFloat( -1000.0, 1000.0 );

	if ( IPhysicsObject *pPhysObj = VPhysicsGetObject() )
	{
		pPhysObj->AddVelocity( &vecVelocity, &vecSpin );
	}

	SetThink( &CTeleportProjectile::DopplerThink );
	SetNextThink( gpGlobals->curtime );

	SetContextThink( &CTeleportProjectile::BubbleThink, gpGlobals->curtime + 0.1, s_szTeleportProjectileBubbles );
	SetContextThink( &CTeleportProjectile::LimitThink, gpGlobals->curtime + 0.1, s_szTeleportProjectileLimitThink );
	
	SetContextThink( &CTeleportProjectile::TeleportPerformReset,
		gpGlobals->curtime + sk_teleport_projectile_max_time.GetFloat(),
		s_szTeleportProjectileResetThink );

	if ( CWeaponTeleport *pGun = dynamic_cast< CWeaponTeleport * >( GetGun() ) )
	{
		pGun->AddFiredProjectile( this );
	}

	m_bValidPosition = true;

	TrackDataThink();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::TeleportReadyThink()
{
//	EmitSound( "NPC_Hunter.FlechettePreExplode" );
// 	CSoundEnt::InsertSound( SOUND_DANGER|SOUND_CONTEXT_EXCLUDE_COMBINE, GetAbsOrigin(), 150.0f, 0.5, this );
	
	EmitSound( "Grenade.Blip" );
	
// 	SetContextThink( &CTeleportProjectile::TeleportReadyThink,
// 		gpGlobals->curtime + sk_teleport_projectile_ready.GetFloat(),
// 		s_szTeleportProjectileTeleportReadyThink );
	SetContextThink( NULL, 0, s_szTeleportProjectileTeleportReadyThink );
}

void CTeleportProjectile::TeleportAboutToExplodeThink()
{
	EmitSound( "NPC_Hunter.FlechettePreExplode" );

	SetContextThink( NULL, 0, s_szTeleportProjectileTeleportAboutToExplodeThink );
}

void CTeleportProjectile::TrackDataThink()
{
	if ( CtrlPtIsTracking() )
	{
		CtrlPtInsert( gpGlobals->curtime, GetAbsOrigin(), GetAbsAngles() );
		SetContextThink( &CTeleportProjectile::TrackDataThink,
			gpGlobals->curtime + sk_teleport_projectile_track_freq.GetFloat(),
			s_szTeleportProjectileTrackDataThink );
	}
	else
	{
		SetContextThink( NULL, 0, s_szTeleportProjectileTrackDataThink );
	}
}

void CTeleportProjectile::SimTeleportThink()
{
	SetNextThink( gpGlobals->curtime + 0.001 );

	if ( gpGlobals->frametime <= 0 )
		return;

	for ( ; ; )
	{
		m_flTeleportSimTime += sk_teleport_projectile_track_speed.GetFloat() * gpGlobals->frametime;
		
		float flSimRange[2];
		CtrlPtGetDataTimeRange( flSimRange[0], flSimRange[1] );

		CBaseEntity *pEnt = GetOwnerEntity();
		if ( !pEnt )
		{
			ScheduleRemove();
			return;
		}

		Vector pos;
		QAngle ang;
		CtrlPtGetData( m_flTeleportSimTime, &pos, &ang );

		if ( m_flTeleportSimTime >= flSimRange[1] )
		{
			// Reached end of simulation
			SetSolid( SOLID_NONE );

			VPhysicsDestroyObject();

			ang = pEnt->GetAbsAngles();
			pEnt->Teleport( &pos, &ang, &m_velTeleportEnt );

			ScheduleRemove();
			return;
		}
		else
		{
			ang = pEnt->GetAbsAngles();
			Vector vecEyeOff = pEnt->GetEyeOffset();
			vecEyeOff *= ( m_flTeleportSimTime - flSimRange[1] ) / ( flSimRange[0] - flSimRange[1] - 0.01 );

			if ( CanTeleportPlayer( pos, vecEyeOff ) )
			{
				pEnt->Teleport( &pos, &ang, &vec3_origin );
				return;
			}
			else
				continue;
		}
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::TeleportPerformTeleport()
{
	extern bool PhysIsInCallback();
	if ( PhysIsInCallback() )
	{
		g_PostSimulationQueue.QueueCall( this, &CTeleportProjectile::TeleportPerformTeleport );
// 		SetContextThink( &CTeleportProjectile::TeleportPerformTeleport,
// 			gpGlobals->curtime, s_szTeleportProjectileTeleportThink );
		return;
	}

	CBaseEntity *pEnt = GetOwnerEntity();
	if ( !pEnt )
	{
		TeleportPerformReset();
		return;
	}

	Vector pos;
	QAngle ang;
	Vector vel, vDirOut;

	pos = GetAbsOrigin();
	ang = GetAbsAngles();
	vel = GetAbsVelocity();

	if ( s_nTeleportProjectileAttach > 0 )
	{
		GetAttachment( s_nTeleportProjectileAttach, pos );
		vDirOut = pos - GetAbsOrigin();
	}
	else
	{
		vDirOut = pEnt->GetAbsOrigin() - pos;
	}
	
	vDirOut.z = 0;
	if ( vDirOut.LengthSqr() > 0.1 )
	{
		vDirOut.NormalizeInPlace();
	}
	else
	{
		vDirOut = Vector( 1, 0, 0 );
	}
	Vector vDirOption = vDirOut * sk_teleport_projectile_opt_radius.GetFloat();
	bool bTeleportingStraight = false;
	
	if ( CanTeleportPlayer( pos, vDirOut ) )
	{
		pos += vDirOut;
		Msg( "Teleporting: straight ( %f  %f  %f )\n",
			pos.x, pos.y, pos.z );
		bTeleportingStraight = true;
		goto perform_teleport;
	}

	for ( int off = 0; off < 1; ++ off )
	{
		for ( int kZ = 1; kZ >= 0; -- kZ )
		{
			for ( int idx = 1; idx < 9; ++ idx )
			{
				QAngle angOption( 0, 0, 0 );
				angOption.y += ( idx / 2 ) * 45 * ( idx%2 ? 1 : -1 );

				matrix3x4_t mat;
				AngleMatrix( angOption, mat );

				Vector vecOption;
				VectorTransform( vDirOption, mat, vecOption );
				vecOption.z += 12 * kZ;

				Vector check = pos + off * vecOption;
				if ( CanTeleportPlayer( check, vecOption ) )
				{
					Msg( "Teleporting: option (%d X %d,%d) ( %f  %f  %f )\n", off, kZ, idx,
						vecOption.x, vecOption.y, vecOption.z );
					pos = check + vecOption;
					goto perform_teleport;
				}
			}
		}
	}

	if ( 1 )
	{
		// No valid spot to teleport
		Msg( "Teleporting: No valid spot to teleport\n" );
		TeleportPerformReset();
		return;
	}

perform_teleport:

	if ( IsNpcOwner() )
	{
		if ( CAI_BaseNPC *pNpc = dynamic_cast< CAI_BaseNPC * >( pEnt ) )
		{
			if ( CBaseEntity *pNpcEnemy = pNpc->GetEnemy() )
			{
				Vector posEnemy = pNpcEnemy->GetAbsOrigin();
				Vector vecLookAtEnemy = posEnemy - pos;
				vecLookAtEnemy.z = 0;
				if ( vecLookAtEnemy.LengthSqr() > 0.1 )
				{
					VectorAngles( vecLookAtEnemy, ang );
				}
			}
		}
	}
	else
	{
		switch ( sk_teleport_projectile_opt_lookat.GetInt() )
		{
		case 0:	// Maintain player's angles
			ang = pEnt->GetAbsAngles();
			break;
		case 1:	// Maintain auto-aim
			{
				// Now we know where we are teleporting
				// try to look at something useful
				autoaim_params_t autoaim;
				ToBasePlayer( pEnt )->GetAutoaimVector( autoaim );

				if ( autoaim.m_bAutoAimAssisting )
				{
					Vector ptWorldAutoAim = autoaim.m_vecAutoAimPoint;
					VectorAngles( ptWorldAutoAim - pos, ang );
				}
				else if ( !bTeleportingStraight )
				{
					// look back where we came from
					VectorAngles( pEnt->GetAbsOrigin() - pos, ang );
				}
			}
			break;
		case 2:	// Maintain projectile's angles
			ang = GetAbsAngles();
			break;
		}
	}

	vel *= sk_teleport_projectile_opt_velf.GetFloat();

	Explode();

	if ( sk_teleport_projectile_track.GetBool() )
	{
		// Put our teleport grenade to keep the player's hull worth of space
// 		SetSize( VEC_HULL_MIN, VEC_HULL_MAX );
// 		SetCollisionGroup( COLLISION_GROUP_INTERACTIVE );
// 		SetSolid( SOLID_BBOX );
// 		Teleport( &pos, &ang, &vec3_origin );
// 		SetMoveType( MOVETYPE_NONE );

		CtrlPtInsertForce( gpGlobals->curtime + 0.01, pos, ang );

		// Schedule traversal
		m_flTeleportSimTime = m_cpTrackDataTime[0];
		m_velTeleportEnt = vel;

		SetThink( &CTeleportProjectile::SimTeleportThink );
		SetNextThink( gpGlobals->curtime );
	}
	else
	{
		pEnt->Teleport( &pos, &ang, &vel );
		ScheduleRemove();
	}
}

void CTeleportProjectile::TeleportPerformReset()
{
	Explode();
	ScheduleRemove();
}

bool CTeleportProjectile::CanTeleportPlayer( Vector const &pos, Vector const &off )
{
	trace_t tr;

	UTIL_TraceLine( pos, pos + off,
		MASK_PLAYERSOLID, this,
		COLLISION_GROUP_NONE, &tr );
	
	if ( tr.startsolid )
		return false;
	if ( tr.fraction < 1.0 )
		return false;

// 	UTIL_TraceHull( pos, pos + off,
// 		VEC_HULL_MIN, VEC_HULL_MAX,
// 		MASK_PLAYERSOLID, this,
// 		COLLISION_GROUP_NONE, &tr );
// 
// 	if ( tr.startsolid )
// 		return false;
// 	if ( tr.fraction < 1.0 )
// 		return false;

	UTIL_TraceHull( pos + off, pos + off,
		VEC_HULL_MIN, VEC_HULL_MAX,
		MASK_PLAYERSOLID, this,
		COLLISION_GROUP_NONE, &tr );

	if ( tr.startsolid )
		return false;

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::Explode()
{
	if ( !IsValidPosition() )
		return;

	m_bValidPosition = false;

	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );

	SetContextThink( NULL, 0, s_szTeleportProjectileTeleportThink );
	SetContextThink( NULL, 0, s_szTeleportProjectileResetThink );

	CtrlPtSetTracking( false );

	// Don't catch self in own explosion!
	m_takedamage = DAMAGE_NO;

	EmitSound( "NPC_Hunter.FlechetteExplode" );

	// Move the explosion effect to the tip to reduce intersection with the world.
	Vector vecFuse = GetAbsOrigin();
	if ( s_nTeleportProjectileAttach > 0 )
	{
		GetAttachment( s_nTeleportProjectileAttach, vecFuse );
	}
	DispatchParticleEffect( "hunter_projectile_explosion_1", vecFuse, GetAbsAngles(), NULL );

	UTIL_ScreenShake( GetAbsOrigin(),
		sk_teleport_projectile_shake_amp.GetFloat(),
		sk_teleport_projectile_shake_freq.GetFloat(),
		sk_teleport_projectile_shake_dur.GetFloat(),
		sk_teleport_projectile_shake_radius.GetFloat(),
		SHAKE_START );

	AddEffects( EF_NODRAW );
}

void CTeleportProjectile::UpdateOnRemove()
{
	if ( CWeaponTeleport *pGun = dynamic_cast< CWeaponTeleport * >( GetGun() ) )
	{
		pGun->RemoveFiredProjectile( this );
	}

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CTeleportProjectile::OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason )
{
	ScheduleStableExplodeTeleport( sk_teleport_projectile_npc_gracetime2.GetFloat() );
	BaseClass2::OnPhysGunPickup( pPhysGunUser, reason );
}

// this will hit only things that are in newCollisionGroup, but NOT in collisionGroupAlreadyChecked
class CTeleportTraceFilterCollisionGroupDelta : public CTraceFilterEntitiesOnly
{
public:
	// It does have a base, but we'll never network anything below here..
	DECLARE_CLASS_NOBASE( CTeleportTraceFilterCollisionGroupDelta );

	CTeleportTraceFilterCollisionGroupDelta( const IHandleEntity *passentity, int collisionGroupAlreadyChecked, int newCollisionGroup )
		: m_pPassEnt(passentity), m_collisionGroupAlreadyChecked( collisionGroupAlreadyChecked ), m_newCollisionGroup( newCollisionGroup )
	{
	}

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		if ( !PassServerEntityFilter( pHandleEntity, m_pPassEnt ) )
			return false;
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );

		if ( pEntity )
		{
			if ( g_pGameRules->ShouldCollide( m_collisionGroupAlreadyChecked, pEntity->GetCollisionGroup() ) )
				return false;
			if ( g_pGameRules->ShouldCollide( m_newCollisionGroup, pEntity->GetCollisionGroup() ) )
				return true;
		}

		return false;
	}

protected:
	const IHandleEntity *m_pPassEnt;
	int		m_collisionGroupAlreadyChecked;
	int		m_newCollisionGroup;
};

void CTeleportProjectile::VPhysicsUpdate( IPhysicsObject *pPhysics )
{
	BaseClass::VPhysicsUpdate( pPhysics );
	Vector vel;
	AngularImpulse angVel;
	pPhysics->GetVelocity( &vel, &angVel );

	Vector start = GetAbsOrigin();
	// find all entities that my collision group wouldn't hit, but COLLISION_GROUP_NONE would and bounce off of them as a ray cast
	CTeleportTraceFilterCollisionGroupDelta filter( this, GetCollisionGroup(), COLLISION_GROUP_NONE );
	trace_t tr;

	// UNDONE: Hull won't work with hitboxes - hits outer hull.  But the whole point of this test is to hit hitboxes.
#if 0
	UTIL_TraceHull( start, start + vel * gpGlobals->frametime, CollisionProp()->OBBMins(), CollisionProp()->OBBMaxs(), CONTENTS_HITBOX|CONTENTS_MONSTER|CONTENTS_SOLID, &filter, &tr );
#else
	UTIL_TraceLine( start, start + vel * gpGlobals->frametime, CONTENTS_HITBOX|CONTENTS_MONSTER|CONTENTS_SOLID, &filter, &tr );
#endif
	if ( tr.startsolid )
	{
		if ( !m_bInSolid )
		{
			// UNDONE: Do a better contact solution that uses relative velocity?
			vel *= -sk_teleport_projectile_bounce_velf.GetFloat(); // bounce backwards
			pPhysics->SetVelocity( &vel, NULL );
		}
		m_bInSolid = true;
		return;
	}
	m_bInSolid = false;
	if ( tr.DidHit() )
	{
		Vector dir = vel;
		VectorNormalize(dir);
		// send a tiny amount of damage so the character will react to getting bonked
		CTakeDamageInfo info( this, GetOwnerEntity(), pPhysics->GetMass() * vel, GetAbsOrigin(), 0.1f, DMG_CRUSH );
		tr.m_pEnt->TakeDamage( info );

		// reflect velocity around normal
		vel = -2.0f * tr.plane.normal * DotProduct(vel,tr.plane.normal) + vel;

		// absorb 80% in impact
		vel *= sk_teleport_projectile_bounce_velf.GetFloat();
		angVel *= -0.5f;
		pPhysics->SetVelocity( &vel, &angVel );
	}
}

void CTeleportProjectile::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	// Insert a track data on phys collision
	CtrlPtInsert( gpGlobals->curtime, GetAbsOrigin(), GetAbsAngles() );

	// Find out what we hit.
	CBaseEntity *pVictim = pEvent->pEntities[!index];
	if ( !pVictim )
		goto baseclass_bounce;

	// Don't bounce if we're hitting an object held by the player
	if ( IPhysicsObject *pObjVictim = pVictim->VPhysicsGetObject() )
	{
		if ( pObjVictim->GetGameFlags() & FVPHYSICS_PLAYER_HELD )
		{
			Assert( 0 );
			// Case of player holding a phys object must be resolved
			// at the moment of projectile launch
		}
	}

	// Don't attach if we're being held by the player
	if ( VPhysicsGetObject()->GetGameFlags() & FVPHYSICS_PLAYER_HELD )
		goto baseclass_bounce;

	// Should teleport on bounce
	if ( IsStablePosition() )
		goto baseclass_bounce_affected;

	if ( !sk_teleport_projectile_bounce_teleport.GetBool() )
		goto baseclass_bounce_affected;

	// Check if player is hitting the floor (surface with no more than 30 degree incline)
	// if ( !IsNpcOwner() )
	{
		Vector vecReflectNormal;
		pEvent->pInternalData->GetSurfaceNormal( vecReflectNormal );
		if ( !index )
			vecReflectNormal = -vecReflectNormal;
		
		if ( vecReflectNormal.z < 0.87 ) // 0.87 = cos(30)
			goto baseclass_bounce_affected;
	}
	
	if ( IsNpcOwner() )
	{
		if ( CAI_BaseNPC *pNpc = dynamic_cast< CAI_BaseNPC * >( GetOwnerEntity() ) )
		{
			if ( CBaseEntity *pNpcEnemy = pNpc->GetEnemy() )
			{
				if ( pNpcEnemy->IsPlayer() )
				{
					Vector posEnemy = pNpcEnemy->GetAbsOrigin();
					Vector posTeleport = GetAbsOrigin();
					Vector vecEnemyToTeleport = ( posTeleport - posEnemy );
					if ( vecEnemyToTeleport.Length() < sk_teleport_projectile_npc_gracedist.GetFloat() )
					{
						ScheduleStableExplodeTeleport( sk_teleport_projectile_npc_gracetime.GetFloat() );
						VPhysicsGetObject()->SetVelocityInstantaneous( &vec3_origin, &vec3_origin );
						// BaseClass::VPhysicsCollision( index, pEvent );
						return;
					}
				}
			}
		}

		TeleportPerformTeleport();
		return;
	}
	else
	{
		TeleportPerformTeleport();
		return;
	}

baseclass_bounce_affected:
	{
		float flCoeff = sk_teleport_projectile_bounce_velf.GetFloat();
		switch ( sk_teleport_projectile_bounce_down.GetInt() )
		{
		case 0:
			VPhysicsGetObject()->SetDragCoefficient( &flCoeff, NULL );
			break;
		case 1:
			VPhysicsGetObject()->SetDamping( &flCoeff, NULL );
			break;
		case 2:
			VPhysicsGetObject()->SetDragCoefficient( &flCoeff, NULL );
			VPhysicsGetObject()->SetDamping( &flCoeff, NULL );
			break;
		case 3:
			{
				Vector vel, ang;
				vel = pEvent->postVelocity[ index ] * flCoeff;
				ang = pEvent->preAngularVelocity[ index ] * flCoeff;
				VPhysicsGetObject()->SetVelocityInstantaneous( &vel, &ang );
			}
			break;
		}
	}

baseclass_bounce:
	// Just bounce
	BaseClass::VPhysicsCollision( index, pEvent );
}

void CTeleportProjectile::ScheduleStableExplodeTeleport( float flDelay )
{
	CtrlPtInsert( gpGlobals->curtime, GetAbsOrigin(), GetAbsAngles() );
	CtrlPtSetTracking( false );
	TrackDataThink();

	if ( flDelay <= 0 )
	{
		TeleportPerformTeleport();
		return;
	}

	if ( ++ m_iStablePosition > 2 )
		return;

	SetContextThink( NULL, 0, s_szTeleportProjectileResetThink );
	SetContextThink( &CTeleportProjectile::TeleportPerformTeleport,
		gpGlobals->curtime + flDelay,
		s_szTeleportProjectileTeleportThink );

	TeleportReadyThink();
	SetContextThink( &CTeleportProjectile::TeleportAboutToExplodeThink,
		gpGlobals->curtime + flDelay - 1,
		s_szTeleportProjectileTeleportAboutToExplodeThink );
}

void CTeleportProjectile::CtrlPtInsert( float flTime, Vector const &pos, QAngle const &ang )
{
	if ( !CtrlPtIsTracking() )
		return;

	int numPts = m_cpTrackDataTime.Count();

	Assert( !numPts ||
			m_cpTrackDataTime[ numPts - 1 ] <= flTime );

	if ( numPts &&
		 m_cpTrackDataTime[ numPts - 1 ] == flTime )
	{
		m_cpTrackDataPos[ numPts - 1 ] = pos;
		m_cpTrackDataAng[ numPts - 1 ] = ang;
	}
	else
	{
		m_cpTrackDataTime.AddToTail( flTime );
		m_cpTrackDataPos.AddToTail( pos );
		m_cpTrackDataAng.AddToTail( ang );
	}
}

void CTeleportProjectile::CtrlPtInsertForce( float flTime, Vector const &pos, QAngle const &ang )
{
	Assert( m_cpTrackDataTime.Count() );

	bool bIsTracking = CtrlPtIsTracking();
	CtrlPtSetTracking( true );

	CtrlPtInsert( flTime, pos, ang );

	CtrlPtSetTracking( bIsTracking );
}

int CTeleportProjectile::CtrlPtGetPtrs( float *&pTime, Vector *&pPos, QAngle *&pAng )
{
	Assert( m_cpTrackDataTime.Count() == m_cpTrackDataPos.Count() );
	Assert( m_cpTrackDataTime.Count() == m_cpTrackDataAng.Count() );

	pTime = m_cpTrackDataTime.Base();
	pPos = m_cpTrackDataPos.Base();
	pAng = m_cpTrackDataAng.Base();

	return m_cpTrackDataTime.Count();
}

bool CTeleportProjectile::CtrlPtGetData( float flTime, Vector *pPos, QAngle *pAng )
{
	// Find the data time segment
	float *dataTime;
	Vector *dataPos;
	QAngle *dataAng;
	
	int nMoreDataPts = CtrlPtGetPtrs( dataTime, dataPos, dataAng );

	if ( !nMoreDataPts )
		return false;

	if ( flTime <= dataTime[0] )
	{
		*pPos = dataPos[0];
		*pAng = dataAng[0];
		return true;
	}

	for ( ; nMoreDataPts -- > 0; ++ dataTime, ++ dataPos, ++ dataAng )
	{
		if ( *dataTime >= flTime )
			break;
	}

	// If beyond our data
	if ( nMoreDataPts < 0 )
	{
		*pPos = dataPos[-1];
		*pAng = dataAng[-1];
		return false;
	}

	// If found exact track data
	if ( flTime == dataTime[0] )
	{
		*pPos = dataPos[0];
		*pAng = dataAng[0];
	}
	// Otherwise interpolate
	else
	{
		float flFraction = ( flTime - dataTime[-1] ) / ( dataTime[0] - dataTime[-1] );
		*pPos = Lerp( flFraction, dataPos[-1], dataPos[0] );
		*pAng = Lerp( flFraction, dataAng[-1], dataAng[0] );
	}

	return true;
}

bool CTeleportProjectile::CtrlPtGetDataTimeRange( float &flMin, float &flMax )
{
	int nCount = m_cpTrackDataTime.Count();
	if ( !nCount )
		return false;

	flMin = m_cpTrackDataTime[ 0 ];
	flMax = m_cpTrackDataTime[ nCount - 1 ];
	return true;
}


