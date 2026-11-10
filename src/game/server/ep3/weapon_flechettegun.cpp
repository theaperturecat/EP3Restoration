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

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


class CWeaponFlechetteGun;
class CFlechetteProjectile;


ConVar sk_flechettegun_projectiles( "sk_flechettegun_projectiles", "1" );
ConVar sk_flechettegun_projectiles_remotedet( "sk_flechettegun_projectiles_remotedet", "1" );
ConVar sk_flechettegun_projectiles_explode_inair( "sk_flechettegun_projectiles_explode_inair", "1" );

ConVar sk_flechettegun_projectiles_gravity( "sk_flechettegun_projectiles_gravity", "0.5" );
ConVar sk_flechettegun_projectiles_vel( "sk_flechettegun_projectiles_vel", "800" );
ConVar sk_flechettegun_projectiles_arc( "sk_flechettegun_projectiles_arc", "10" );

ConVar sk_flechettegun_projectiles_hit_dmg( "sk_flechettegun_projectiles_hit_dmg", "3.0" );
ConVar sk_flechettegun_projectiles_explode_dmg( "sk_flechettegun_projectiles_explode_dmg", "25.0" );
ConVar sk_flechettegun_projectiles_explode_radius( "sk_flechettegun_projectiles_explode_radius", "100.0" );

ConVar sk_flechettegun_projectiles_burst( "sk_flechettegun_projectiles_burst", "6" );
ConVar sk_flechettegun_projectiles_rate( "sk_flechettegun_projectiles_rate", "0.05" );
ConVar sk_flechettegun_projectiles_cycle( "sk_flechettegun_projectiles_cycle", "2" );

ConVar sk_flechettegun_projectiles_det_delay( "sk_flechettegun_projectiles_det_delay", "0.30" );
ConVar sk_flechettegun_projectiles_det_rate( "sk_flechettegun_projectiles_det_rate", "0.14" );

ConVar sk_flechettegun_projectiles_warn( "sk_flechettegun_projectiles_warn", "10" );

ConVar sk_flechettegun_projectiles_shake_amp( "sk_flechettegun_projectiles_shake_amp", "5.0" );
ConVar sk_flechettegun_projectiles_shake_freq( "sk_flechettegun_projectiles_shake_freq", "150.0" );
ConVar sk_flechettegun_projectiles_shake_dur( "sk_flechettegun_projectiles_shake_dur", "1.0" );
ConVar sk_flechettegun_projectiles_shake_radius( "sk_flechettegun_projectiles_shake_radius", "384.0" );

ConVar sk_plr_dmg_flechette_projectile( "sk_plr_dmg_flechette_projectile", "15.0" );
ConVar sk_npc_dmg_flechette_projectile( "sk_npc_dmg_flechette_projectile", "10.0" );
ConVar sk_max_flechette_projectile( "sk_max_flechette_projectile", "60" );


//////////////////////////////////////////////////////////////////////////
//
// Projectile declaration
//
//////////////////////////////////////////////////////////////////////////

class CFlechetteProjectile : public CPhysicsProp
{
	DECLARE_CLASS( CFlechetteProjectile, CPhysicsProp );

public:

	CFlechetteProjectile();
	~CFlechetteProjectile();

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

	bool IsExplosive() const { return !m_bNoDetonation; }

	bool CreateVPhysics();

	unsigned int PhysicsSolidMaskForEntity() const;
	static CFlechetteProjectile *FlechetteCreate( const Vector &vecOrigin, const QAngle &angAngles, CBaseEntity *pentOwner );

protected:

	void SetupGlobalModelData();

	void StickTo( CBaseEntity *pOther, trace_t &tr );

	void BubbleThink();
	void DangerSoundThink();
	void ExplodeThink();
	void DopplerThink();
	void SeekThink();

	bool CreateSprites( bool bBright );

	void FlechetteTouch( CBaseEntity *pOther );

	void UpdateOnRemove();

	Vector	m_vecShootPosition;
	EHANDLE m_hSeekTarget;
	bool	m_bNoDetonation;
	
	CHandle< CBaseEntity > m_hGun;

	DECLARE_DATADESC();
	//DECLARE_SERVERCLASS();
};


//////////////////////////////////////////////////////////////////////////
//
// Weapon declaration
//
//////////////////////////////////////////////////////////////////////////


class CWeaponFlechetteGun : public CHLSelectFireMachineGun
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CWeaponFlechetteGun, CHLSelectFireMachineGun );

	CWeaponFlechetteGun();

  	DECLARE_SERVERCLASS();

	bool	ShouldDisplayReloadHUDHint();
	bool	HasAnyAmmo();
	bool	HasAmmo() { return HasAnyAmmo(); }
	
	void	Precache( void );
	void	AddViewKick( void );
	void	PrimaryAttack( void );
	void	SecondaryAttack( void );

	virtual void Equip( CBaseCombatCharacter *pOwner );
	bool	Reload( void );

	float	GetFireRate( void ) { return sk_flechettegun_projectiles_rate.GetFloat(); }
	float	GetBurstCycleRate() { return sk_flechettegun_projectiles_cycle.GetFloat(); }
	int		GetBurstSize()		{ return sk_flechettegun_projectiles_burst.GetInt(); }
	float	GetDetonateDelay()	{ return sk_flechettegun_projectiles_det_delay.GetFloat(); }
	float	GetDetonateRate()	{ return sk_flechettegun_projectiles_det_rate.GetFloat(); }

	int		CapabilitiesGet( void ) { return bits_CAP_WEAPON_RANGE_ATTACK1; }
	int		WeaponRangeAttack2Condition( float flDot, float flDist );
	Activity	GetPrimaryAttackActivity( void );

	virtual const Vector& GetBulletSpread( void )
	{
		static const Vector cone = VECTOR_CONE_5DEGREES;
		return cone;
	}

	const WeaponProficiencyInfo_t *GetProficiencyValues();

	void FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir );
	void Operator_ForceNPCFire( CBaseCombatCharacter  *pOperator, bool bSecondary, CBaseEntity *pTarget = NULL );
	void Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );

	void UpdateOnRemove();

	void FireBullets( const FireBulletsInfo_t &info );
	void BurstThink();
	void ReloadThink();
 	void DetonateThink();

	void AddFiredProjectile( CBaseEntity *pProjectile );
	void RemoveFiredProjectile( CBaseEntity *pProjectile );

	int DetonateFiredProjectiles();
	int DetonateChainProjectiles();

protected:
	typedef CHandle< CBaseEntity > FiredProjectileHandle;
	CUtlVector< FiredProjectileHandle > m_arrFiredProjectiles;

	int		m_nBurstTotalProjectiles;
	float	m_flNextBurstProjectile;
	int		m_nDetonating;
 	float	m_flNextDetonateProjectile;

};

IMPLEMENT_SERVERCLASS_ST(CWeaponFlechetteGun, DT_WeaponFlechetteGun)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_flechettegun, CWeaponFlechetteGun );
PRECACHE_WEAPON_REGISTER( weapon_flechettegun );


BEGIN_DATADESC( CWeaponFlechetteGun )

 	DEFINE_FIELD( m_flNextBurstProjectile, FIELD_TIME ),
	DEFINE_FIELD( m_nBurstTotalProjectiles, FIELD_INTEGER ),

 	DEFINE_FIELD( m_flNextDetonateProjectile, FIELD_TIME ),
	DEFINE_FIELD( m_nDetonating, FIELD_INTEGER ),
	
	DEFINE_FUNCTION( BurstThink ),
	DEFINE_FUNCTION( DetonateThink ),
	DEFINE_FUNCTION( ReloadThink ),

END_DATADESC()




//////////////////////////////////////////////////////////////////////////
//
// Implementation
//
//////////////////////////////////////////////////////////////////////////



//=========================================================
CWeaponFlechetteGun::CWeaponFlechetteGun( )
{
	m_fMinRange1		= 0;// No minimum range. 
	m_fMaxRange1		= 1400;

	// m_bReloadsSingly	= true;
	m_bFiresUnderwater	= false;

	m_iFireMode = FIREMODE_3RNDBURST;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFlechetteGun::Precache( void )
{
	UTIL_PrecacheOther("hunter_flechette");
	UTIL_PrecacheOther("flechette_projectile");

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: Give this weapon longer range when wielded by an ally NPC.
//-----------------------------------------------------------------------------
void CWeaponFlechetteGun::Equip( CBaseCombatCharacter *pOwner )
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
void CWeaponFlechetteGun::FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir )
{
	// FIXME: use the returned number of bullets to account for >10hz firerate
	WeaponSoundRealtime( SINGLE_NPC );

	CSoundEnt::InsertSound( SOUND_COMBAT|SOUND_CONTEXT_GUNFIRE, pOperator->GetAbsOrigin(), SOUNDENT_VOLUME_MACHINEGUN, 0.2, pOperator, SOUNDENT_CHANNEL_WEAPON, pOperator->GetEnemy() );
	pOperator->FireBullets( 1, vecShootOrigin, vecShootDir, VECTOR_CONE_PRECALCULATED,
		MAX_TRACE_LENGTH, m_iPrimaryAmmoType, 2, entindex(), 0 );

	pOperator->DoMuzzleFlash();
	m_iClip1 = m_iClip1 - 1;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFlechetteGun::Operator_ForceNPCFire( CBaseCombatCharacter *pOperator, bool bSecondary, CBaseEntity *pTarget )
{
	// Ensure we have enough rounds in the clip
	m_iClip1++;

	Vector vecShootOrigin, vecShootDir;
	QAngle	angShootDir;
	GetAttachment( LookupAttachment( "muzzle" ), vecShootOrigin, angShootDir );

	if ( pTarget )
	{
		vecShootDir = pTarget->WorldSpaceCenter() - vecShootOrigin;
		VectorNormalize( vecShootDir );
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
void CWeaponFlechetteGun::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
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

		/*//FIXME: Re-enable
		case EVENT_WEAPON_AR2_GRENADE:
		{
		CAI_BaseNPC *npc = pOperator->MyNPCPointer();

		Vector vecShootOrigin, vecShootDir;
		vecShootOrigin = pOperator->Weapon_ShootPosition();
		vecShootDir = npc->GetShootEnemyDir( vecShootOrigin );

		Vector vecThrow = m_vecTossVelocity;

		CGrenadeAR2 *pGrenade = (CGrenadeAR2*)Create( "grenade_ar2", vecShootOrigin, vec3_angle, npc );
		pGrenade->SetAbsVelocity( vecThrow );
		pGrenade->SetLocalAngularVelocity( QAngle( 0, 400, 0 ) );
		pGrenade->SetMoveType( MOVETYPE_FLYGRAVITY ); 
		pGrenade->m_hOwner			= npc;
		pGrenade->m_pMyWeaponAR2	= this;
		pGrenade->SetDamage(sk_npc_dmg_ar2_grenade.GetFloat());

		// FIXME: arrgg ,this is hard coded into the weapon???
		m_flNextGrenadeCheck = gpGlobals->curtime + 6;// wait six seconds before even looking again to see if a grenade can be thrown.

		m_iClip2--;
		}
		break;
		*/

	default:
		BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Activity
//-----------------------------------------------------------------------------
Activity CWeaponFlechetteGun::GetPrimaryAttackActivity( void )
{
	if ( m_nShotsFired < 2 )
		return ACT_VM_PRIMARYATTACK;

	if ( m_nShotsFired < 3 )
		return ACT_VM_RECOIL1;
	
	if ( m_nShotsFired < 4 )
		return ACT_VM_RECOIL2;

	return ACT_VM_RECOIL3;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponFlechetteGun::Reload( void )
{
	bool fRet;
	float fCacheTime = m_flNextSecondaryAttack;

	fRet = DefaultReload( GetMaxClip1(), GetMaxClip2(), ACT_VM_RELOAD );
	if ( fRet )
	{
		// Undo whatever the reload process has done to our secondary
		// attack timer. We allow you to interrupt reloading to fire
		// a grenade.
		m_flNextSecondaryAttack = GetOwner()->m_flNextAttack = fCacheTime;

		WeaponSound( RELOAD );
	}

	return fRet;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFlechetteGun::AddViewKick( void )
{
	#define	EASY_DAMPEN			0.5f
	#define	MAX_VERTICAL_KICK	1.0f	//Degrees
	#define	SLIDE_LIMIT			2.0f	//Seconds
	
	//Get the view kick
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );

	if ( pPlayer == NULL )
		return;

	DoMachineGunKick( pPlayer, EASY_DAMPEN, MAX_VERTICAL_KICK, m_fFireDuration, SLIDE_LIMIT );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFlechetteGun::PrimaryAttack( void )
{
	if ( m_bFireOnEmpty )
		return;

	if ( m_nDetonating )
		return;

	if ( m_iBurstSize )
		return;

// 	if ( m_arrFiredProjectiles.Count() )
// 	{
// 		SecondaryAttack();
// 		return;
// 	}

	m_iBurstSize = m_nBurstTotalProjectiles = GetBurstSize();
	m_flNextBurstProjectile = gpGlobals->curtime;

	// Call the think function directly so that the first round gets fired immediately.
	BurstThink();
	SetThink( &CWeaponFlechetteGun::BurstThink );

	// Pick up the rest of the burst through the think function.
	SetNextThink( gpGlobals->curtime + GetFireRate() );

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner )
	{
		m_iPrimaryAttacks++;
		gamestats->Event_WeaponFired( pOwner, true, GetClassname() );
	}

	m_flNextPrimaryAttack = gpGlobals->curtime + GetBurstCycleRate();
	m_flNextSecondaryAttack = gpGlobals->curtime + GetDetonateDelay();
}

void CWeaponFlechetteGun::SecondaryAttack()
{
	if ( m_iBurstSize )
		return;

	if ( m_nDetonating )
		return;

	m_nDetonating = 1;
	m_flNextDetonateProjectile = gpGlobals->curtime;
	if ( DetonateChainProjectiles() )
	{
		SetThink( &CWeaponFlechetteGun::DetonateThink );
		SetNextThink( gpGlobals->curtime + GetDetonateRate() );
	}
	else
	{
		WeaponSound( EMPTY );
	}
}

void CWeaponFlechetteGun::UpdateOnRemove()
{
	BaseClass::UpdateOnRemove();
}

#define	COMBINE_MIN_GRENADE_CLEAR_DIST 256

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flDot - 
//			flDist - 
// Output : int
//-----------------------------------------------------------------------------
int CWeaponFlechetteGun::WeaponRangeAttack2Condition( float flDot, float flDist )
{
	// CAI_BaseNPC *npcOwner = GetOwner()->MyNPCPointer();

	return COND_NONE;
}

//-----------------------------------------------------------------------------
const WeaponProficiencyInfo_t *CWeaponFlechetteGun::GetProficiencyValues()
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

void CWeaponFlechetteGun::FireBullets( const FireBulletsInfo_t &info )
{
	extern void ShootFlechettes( Vector vOrigin, QAngle angles, CBaseEntity *pOwner );
	extern void ShootFlechetteProjectiles( Vector vOrigin, QAngle angles, CBaseEntity *pOwner, CBaseEntity *pGun );

	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (!pPlayer)
		return;

	// Determine the bias range
	int iBiasRange = 0;
	while ( ( iBiasRange + 1 ) * ( iBiasRange + 1 ) < m_nBurstTotalProjectiles )
		++ iBiasRange;

	int iLoopRange = 2 * iBiasRange + 1;
	int numShotsRemaining = MIN( info.m_iShots, m_iBurstSize );

	int numShotsAlreadyFired = m_nBurstTotalProjectiles - m_iBurstSize;
	int x = numShotsAlreadyFired / iLoopRange;
	int y = numShotsAlreadyFired % iLoopRange;

	for ( ; ( x < iLoopRange ) && ( numShotsRemaining > 0 ); ++ x )
	{
		for ( ; ( y < iLoopRange ) && ( numShotsRemaining > 0 ); ++ y )
		{
			int offX = ( x + 1 ) / 2 * ( (x % 2) ? 1 : -1 );
			int offY = ( y + 1 ) / 2 * ( (y % 2) ? 1 : -1 );

			Vector vOrigin = info.m_vecSrc;
			Vector vDirection = info.m_vecDirShooting;

			vOrigin.x += offX * 5;
			vOrigin.y += offY * 5;

			QAngle qAngDirection;
			VectorAngles( vDirection, qAngDirection );

			if ( x || y )
			{
				qAngDirection.x += ((rand() % 250) / 100.0) * (rand() % 2 == 1 ? -1 : 1);
				qAngDirection.y += ((rand() % 250) / 100.0) * (rand() % 2 == 1 ? -1 : 1);
			}

			qAngDirection.x -= sk_flechettegun_projectiles_arc.GetFloat(); // arc incline

			if ( sk_flechettegun_projectiles.GetInt() )
				ShootFlechetteProjectiles( info.m_vecSrc, qAngDirection, pPlayer, this );
			else
				ShootFlechettes( info.m_vecSrc, qAngDirection, pPlayer );

			-- numShotsRemaining;
			-- m_iBurstSize;
		}
	}
}

void CWeaponFlechetteGun::BurstThink()
{
	float flSaveNextPrimaryAttack = m_flNextPrimaryAttack;
	m_flNextPrimaryAttack = m_flNextBurstProjectile;

	CHLMachineGun::PrimaryAttack();

	m_flNextBurstProjectile = m_flNextPrimaryAttack;
	m_flNextPrimaryAttack = flSaveNextPrimaryAttack;

	if( m_iBurstSize == 0 )
	{
		// The burst is over!
		m_flNextBurstProjectile = gpGlobals->curtime - GetBurstSize() * GetFireRate() + GetBurstCycleRate();
		SetThink( &CWeaponFlechetteGun::ReloadThink );
		SetNextThink( m_flNextBurstProjectile );

		// idle immediately to stop the firing animation
		SetWeaponIdleTime( gpGlobals->curtime );
		return;
	}

	SetNextThink( m_flNextBurstProjectile );
}

void CWeaponFlechetteGun::DetonateThink()
{
	while ( ( m_flNextDetonateProjectile <= gpGlobals->curtime ) &&
			DetonateChainProjectiles() )
			continue;

	if ( m_nDetonating )
	{
		SetNextThink( m_flNextDetonateProjectile );
	}
	else
	{
		SetThink( &CWeaponFlechetteGun::ReloadThink );
		SetNextThink( m_flNextBurstProjectile );
	}
}

void CWeaponFlechetteGun::ReloadThink()
{
	Reload();
	SetThink( NULL );
}

bool CWeaponFlechetteGun::ShouldDisplayReloadHUDHint()
{
	return false;
}

bool CWeaponFlechetteGun::HasAnyAmmo()
{
	if ( BaseClass::HasAnyAmmo() )
		return true;

	// Have fired undetonated projectiles
	if ( m_arrFiredProjectiles.Count() )
	{
		return true;
	}

	return false;
}

void CWeaponFlechetteGun::AddFiredProjectile( CBaseEntity *pProjectile )
{
	FiredProjectileHandle h;
	h = pProjectile;
	m_arrFiredProjectiles.AddToTail( h );
}

void CWeaponFlechetteGun::RemoveFiredProjectile( CBaseEntity *pProjectile )
{
	FiredProjectileHandle h;
	h = pProjectile;
	
	while ( m_arrFiredProjectiles.FindAndRemove( h ) )
		continue;
}

int CWeaponFlechetteGun::DetonateFiredProjectiles()
{
	int numDetonated = 0;

	for ( int i = 0; i < m_arrFiredProjectiles.Count(); ++ i )
	{
		CFlechetteProjectile *pProjectile = ( CFlechetteProjectile * ) m_arrFiredProjectiles[i].Get();
		if ( pProjectile )
		{
			if ( pProjectile->IsEffectActive( EF_NODRAW ) )
				continue;

			pProjectile->Explode();
			++ numDetonated;
		}
	}

	return numDetonated;
}

int CWeaponFlechetteGun::DetonateChainProjectiles()
{
	m_flNextDetonateProjectile += GetDetonateRate();

	m_flNextPrimaryAttack = m_flNextSecondaryAttack = m_flNextDetonateProjectile + GetDetonateRate();

	for ( int i = 0; i < m_arrFiredProjectiles.Count(); ++ i )
	{
		CFlechetteProjectile *pProjectile = ( CFlechetteProjectile * ) m_arrFiredProjectiles[i].Get();
		if ( pProjectile )
		{
			if ( pProjectile->IsEffectActive( EF_NODRAW ) )
				continue;
			if ( !pProjectile->IsExplosive() )
				continue;

			pProjectile->Explode();
			return 1;
		}
	}

	m_arrFiredProjectiles.RemoveAll();
	m_nDetonating = 0;
	return 0;
}

//////////////////////////////////////////////////////////////////////////
//
// Flechette implementation
//
//////////////////////////////////////////////////////////////////////////


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static const char *FLECHETTE_PROJECTILE_MODEL = "models/weapons/hunter_flechette.mdl";

static const char *s_szFlechetteProjectileBubbles = "HunterFlechetteBubbles";
static const char *s_szFlechetteProjectileSeekThink = "HunterFlechetteSeekThink";
static const char *s_szFlechetteProjectileDangerSoundThink = "HunterFlechetteDangerSoundThink";
static const char *s_szFlechetteProjectileSpriteTrail = "sprites/bluelaser1.vmt";
static int s_nHunterFlechetteImpact = -2;
static int s_nFlechetteFuseAttach = -1;


LINK_ENTITY_TO_CLASS( flechette_projectile, CFlechetteProjectile );

BEGIN_DATADESC( CFlechetteProjectile )

	DEFINE_THINKFUNC( BubbleThink ),
	DEFINE_THINKFUNC( DangerSoundThink ),
	DEFINE_THINKFUNC( ExplodeThink ),
	DEFINE_THINKFUNC( DopplerThink ),
	DEFINE_THINKFUNC( SeekThink ),

	DEFINE_ENTITYFUNC( FlechetteTouch ),

	DEFINE_FIELD( m_vecShootPosition, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_hSeekTarget, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bNoDetonation, FIELD_BOOLEAN ),

END_DATADESC()

//IMPLEMENT_SERVERCLASS_ST( CFlechetteProjectile, DT_HunterFlechette )
//END_SEND_TABLE()


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CFlechetteProjectile *CFlechetteProjectile::FlechetteCreate( const Vector &vecOrigin, const QAngle &angAngles, CBaseEntity *pentOwner )
{
	// Create a new entity with CFlechetteProjectile private data
	CFlechetteProjectile *pFlechette = (CFlechetteProjectile *)CreateEntityByName( "flechette_projectile" );
	UTIL_SetOrigin( pFlechette, vecOrigin );
	pFlechette->SetAbsAngles( angAngles );
	pFlechette->Spawn();
	pFlechette->Activate();
	pFlechette->SetOwnerEntity( pentOwner );

	return pFlechette;
}


void ShootFlechetteProjectiles( Vector vOrigin, QAngle angles, CBaseEntity *pOwner, CBaseEntity *pGun )
{
// 	MDLCACHE_CRITICAL_SECTION();
// 
// 	bool allowPrecache = CBaseEntity::IsPrecacheAllowed();
// 	CBaseEntity::SetAllowPrecache( true );

	CFlechetteProjectile *entity = CFlechetteProjectile::FlechetteCreate( vOrigin, angles, pOwner );
	if ( entity )
	{
		entity->Precache();
		entity->SetGun( pGun );
		DispatchSpawn( entity );

		// Shoot the flechette.
		Vector forward;
		AngleVectors( angles, &forward );
		entity->Shoot( forward * sk_flechettegun_projectiles_vel.GetFloat(), false );
	}

// 	CBaseEntity::SetAllowPrecache( allowPrecache );

}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CFlechetteProjectile::CFlechetteProjectile()
{
	UseClientSideAnimation();
	m_bNoDetonation = false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CFlechetteProjectile::~CFlechetteProjectile()
{
}


//-----------------------------------------------------------------------------
// If set, the flechette will seek unerringly toward the target as it flies.
//-----------------------------------------------------------------------------
void CFlechetteProjectile::SetSeekTarget( CBaseEntity *pTargetEntity )
{
	if ( pTargetEntity )
	{
		m_hSeekTarget = pTargetEntity;
		SetContextThink( &CFlechetteProjectile::SeekThink, gpGlobals->curtime, s_szFlechetteProjectileSeekThink );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CFlechetteProjectile::CreateVPhysics()
{
	// Create the object in the physics system
	VPhysicsInitNormal( SOLID_BBOX, FSOLID_NOT_STANDABLE, false );

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
unsigned int CFlechetteProjectile::PhysicsSolidMaskForEntity() const
{
	return ( BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_HITBOX ) & ~CONTENTS_GRATE;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CFlechetteProjectile::CreateSprites( bool bBright )
{
	if ( bBright )
	{
		DispatchParticleEffect( "hunter_flechette_trail_striderbuster", PATTACH_ABSORIGIN_FOLLOW, this );
	}
	else
	{
		DispatchParticleEffect( "hunter_flechette_trail", PATTACH_ABSORIGIN_FOLLOW, this );
	}

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlechetteProjectile::Spawn()
{
	Precache( );

	SetModel( FLECHETTE_PROJECTILE_MODEL );
	SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM );
	UTIL_SetSize( this, -Vector(1,1,1), Vector(1,1,1) );
	SetSolid( SOLID_BBOX );
	SetGravity( sk_flechettegun_projectiles_gravity.GetFloat() );
	SetCollisionGroup( COLLISION_GROUP_PROJECTILE );

	// Make sure we're updated if we're underwater
	UpdateWaterState();

	SetTouch( &CFlechetteProjectile::FlechetteTouch );

	// Make us glow until we've hit the wall
	m_nSkin = 1;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlechetteProjectile::Activate()
{
	BaseClass::Activate();
	SetupGlobalModelData();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlechetteProjectile::SetupGlobalModelData()
{
	if ( s_nHunterFlechetteImpact == -2 )
	{
		s_nHunterFlechetteImpact = LookupSequence( "impact" );
		s_nFlechetteFuseAttach = LookupAttachment( "attach_fuse" );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlechetteProjectile::Precache()
{
	PrecacheModel( FLECHETTE_PROJECTILE_MODEL );
	PrecacheModel( "sprites/light_glow02_noz.vmt" );

	PrecacheScriptSound( "NPC_Hunter.FlechetteNearmiss" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteHitBody" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteHitWorld" );
	PrecacheScriptSound( "NPC_Hunter.FlechettePreExplode" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteExplode" );

	PrecacheParticleSystem( "hunter_flechette_trail_striderbuster" );
	PrecacheParticleSystem( "hunter_flechette_trail" );
	PrecacheParticleSystem( "hunter_projectile_explosion_1" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlechetteProjectile::StickTo( CBaseEntity *pOther, trace_t &tr )
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
	SetContextThink( NULL, 0, s_szFlechetteProjectileBubbles );

	// Stop seeking.
	m_hSeekTarget = NULL;
	SetContextThink( NULL, 0, s_szFlechetteProjectileSeekThink );

	if ( CWeaponFlechetteGun *pGun = dynamic_cast< CWeaponFlechetteGun * >( GetGun() ) )
	{
		pGun->AddFiredProjectile( this );
	}

	// Get ready to explode.
	SetThink( &CFlechetteProjectile::DangerSoundThink );
	SetNextThink( gpGlobals->curtime + sk_flechettegun_projectiles_warn.GetFloat() );

	// Play our impact animation.
	ResetSequence( s_nHunterFlechetteImpact );

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
void CFlechetteProjectile::FlechetteTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet(FSOLID_VOLUME_CONTENTS | FSOLID_TRIGGER) )
	{
		// Some NPCs are triggers that can take damage (like antlion grubs). We should hit them.
		if ( ( pOther->m_takedamage == DAMAGE_NO ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) )
			return;
	}

	if ( FClassnameIs( pOther, "flechette_projectile" ) )
		return;

	trace_t	tr;
	tr = BaseClass::GetTouchTrace();

	if ( pOther->m_takedamage != DAMAGE_NO )
	{
		Vector	vecNormalizedVel = GetAbsVelocity();

		ClearMultiDamage();
		VectorNormalize( vecNormalizedVel );

		float flDamage = sk_flechettegun_projectiles_hit_dmg.GetFloat();
		CBreakable *pBreak = dynamic_cast <CBreakable *>(pOther);
		if ( pBreak && ( pBreak->GetMaterialType() == matGlass ) )
		{
			flDamage = MAX( pOther->GetHealth(), flDamage );
		}

		CTakeDamageInfo	dmgInfo( this, GetOwnerEntity(), flDamage, DMG_BULLET | DMG_NEVERGIB );
		CalculateMeleeDamageForce( &dmgInfo, vecNormalizedVel, tr.endpos, 0.7f );
		dmgInfo.SetDamagePosition( tr.endpos );
		pOther->DispatchTraceAttack( dmgInfo, vecNormalizedVel, &tr );

		ApplyMultiDamage();

		// Keep going through breakable glass.
		if ( pOther->GetCollisionGroup() == COLLISION_GROUP_BREAKABLE_GLASS )
			return;

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
// 			SetTouch( NULL );
// 			SetThink( NULL );
// 			SetContextThink( NULL, 0, s_szFlechetteProjectileBubbles );
// 
// 			UTIL_Remove( this );
			return;
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
			// Put a mark unless we've hit the sky
			if ( ( tr.surface.flags & SURF_SKY ) == false )
			{
				UTIL_ImpactTrace( &tr, DMG_BULLET );
			}

			m_bNoDetonation = true;
			UTIL_Remove( this );
		}
	}
}


//-----------------------------------------------------------------------------
// Fixup flechette position when seeking towards a striderbuster.
//-----------------------------------------------------------------------------
void CFlechetteProjectile::SeekThink()
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

		float flSpeed = sk_flechettegun_projectiles_vel.GetFloat();
		if ( !flSpeed )
		{
			flSpeed = 2500.0f;
		}

		Vector vecVelocity = vecDelta * flSpeed;
		Teleport( &vecClosest, &angShoot, &vecVelocity );

		SetNextThink( gpGlobals->curtime, s_szFlechetteProjectileSeekThink );
	}
}


//-----------------------------------------------------------------------------
// Play a near miss sound as we travel past the player.
//-----------------------------------------------------------------------------
void CFlechetteProjectile::DopplerThink()
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
void CFlechetteProjectile::BubbleThink()
{
	SetNextThink( gpGlobals->curtime + 0.1f, s_szFlechetteProjectileBubbles );

	if ( GetWaterLevel()  == 0 )
		return;

	UTIL_BubbleTrail( GetAbsOrigin() - GetAbsVelocity() * 0.1f, GetAbsOrigin(), 5 );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlechetteProjectile::Shoot( Vector &vecVelocity, bool bBrightFX )
{
	CreateSprites( bBrightFX );

	m_vecShootPosition = GetAbsOrigin();

	SetAbsVelocity( vecVelocity );

	SetThink( &CFlechetteProjectile::DopplerThink );
	SetNextThink( gpGlobals->curtime );

	SetContextThink( &CFlechetteProjectile::BubbleThink, gpGlobals->curtime + 0.1, s_szFlechetteProjectileBubbles );

	if ( sk_flechettegun_projectiles_explode_inair.GetInt() )
	{
		if ( CWeaponFlechetteGun *pGun = dynamic_cast< CWeaponFlechetteGun * >( GetGun() ) )
		{
			pGun->AddFiredProjectile( this );
		}
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlechetteProjectile::DangerSoundThink()
{
	EmitSound( "NPC_Hunter.FlechettePreExplode" );

	CSoundEnt::InsertSound( SOUND_DANGER|SOUND_CONTEXT_EXCLUDE_COMBINE, GetAbsOrigin(), 150.0f, 0.5, this );
	
	if ( sk_flechettegun_projectiles_remotedet.GetInt() )
	{
		SetThink( &CFlechetteProjectile::DangerSoundThink );
		SetNextThink( gpGlobals->curtime + sk_flechettegun_projectiles_warn.GetFloat() * 2 );
	}
	else
	{
		SetThink( &CFlechetteProjectile::ExplodeThink );
		SetNextThink( gpGlobals->curtime + sk_flechettegun_projectiles_warn.GetFloat() );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlechetteProjectile::ExplodeThink()
{
	Explode();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlechetteProjectile::Explode()
{
	SetSolid( SOLID_NONE );

	// Don't catch self in own explosion!
	m_takedamage = DAMAGE_NO;

	EmitSound( "NPC_Hunter.FlechetteExplode" );

	// Move the explosion effect to the tip to reduce intersection with the world.
	Vector vecFuse;
	GetAttachment( s_nFlechetteFuseAttach, vecFuse );
	DispatchParticleEffect( "hunter_projectile_explosion_1", vecFuse, GetAbsAngles(), NULL );

	UTIL_ScreenShake( GetAbsOrigin(),
		sk_flechettegun_projectiles_shake_amp.GetFloat(),
		sk_flechettegun_projectiles_shake_freq.GetFloat(),
		sk_flechettegun_projectiles_shake_dur.GetFloat(),
		sk_flechettegun_projectiles_shake_radius.GetFloat(),
		SHAKE_START );

	// int nDamageType = DMG_DISSOLVE;
	int nDamageType = DMG_BLAST;

	RadiusDamage( CTakeDamageInfo( this, GetOwnerEntity(),
		sk_flechettegun_projectiles_explode_dmg.GetFloat(), nDamageType ),
		GetAbsOrigin(),
		sk_flechettegun_projectiles_explode_radius.GetFloat(),
		CLASS_NONE, NULL );

	AddEffects( EF_NODRAW );

// 	float flRate = sk_flechettegun_projectiles_det_rate.GetFloat();
// 	if ( flRate > 0 )
// 	{
// 		SetThink( &CBaseEntity::SUB_Remove );
// 		SetNextThink( gpGlobals->curtime + flRate );
// 	}
// 	else
// 	{
// 		if ( CWeaponFlechetteGun *pGun = dynamic_cast< CWeaponFlechetteGun * >( GetGun() ) )
// 		{
// 			pGun->RemoveFiredProjectile( this );
// 			pGun->DetonateChainProjectiles();
// 		}

		SetThink( &CBaseEntity::SUB_Remove );
		SetNextThink( gpGlobals->curtime + 0.1f );
// 	}
}

void CFlechetteProjectile::UpdateOnRemove()
{
	if ( CWeaponFlechetteGun *pGun = dynamic_cast< CWeaponFlechetteGun * >( GetGun() ) )
	{
// 		if ( sk_flechettegun_projectiles_det_rate.GetFloat() > 0 &&
// 			 !m_bNoDetonation )
// 		{
// 			pGun->DetonateChainProjectiles();
// 		}

		pGun->RemoveFiredProjectile( this );
	}

	BaseClass::UpdateOnRemove();
}


