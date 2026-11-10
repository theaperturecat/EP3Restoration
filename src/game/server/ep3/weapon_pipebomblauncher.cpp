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

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar    sk_plr_dmg_smg1_grenade;	



class CWeaponPipeBombLauncher;
class CGrenadePipeBomb;



//////////////////////////////////////////////////////////////////////////
//
// Grenade declaration
//
//////////////////////////////////////////////////////////////////////////


class CGrenadePipeBomb : public CGrenadeAR2
{
public:
	DECLARE_CLASS( CGrenadePipeBomb, CGrenadeAR2 );

	CHandle< SmokeTrail > m_hSmokeTrail;
	CHandle< CBaseEntity > m_hLauncher;
	float				 m_fSpawnTime;
	float				m_fDangerRadius;


	void		Spawn( void );
	void		Precache( void );
	void 		GrenadePipeBombTouch( CBaseEntity *pOther );
	void		GrenadePipeBombThink( void );
	void		Event_Killed( const CTakeDamageInfo &info );
	void		UpdateOnRemove();

	void		SetLauncher( CBaseEntity *pLauncher ) { m_hLauncher = pLauncher; }
	CBaseEntity	*GetLauncher( void ) { return m_hLauncher; }

public:
	void EXPORT				Detonate(void);
	CGrenadePipeBomb(void);

	DECLARE_DATADESC();
};

BEGIN_DATADESC( CGrenadePipeBomb )

	// DEFINE_FIELD( m_fDangerRadius, FIELD_FLOAT ),

	// Function pointers
	DEFINE_ENTITYFUNC( GrenadePipeBombTouch ),
	DEFINE_THINKFUNC( GrenadePipeBombThink ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( grenade_pipebomb, CGrenadePipeBomb );



//////////////////////////////////////////////////////////////////////////
//
// Weapon declaration
//
//////////////////////////////////////////////////////////////////////////


class CWeaponPipeBombLauncher : public CHLSelectFireMachineGun
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS( CWeaponPipeBombLauncher, CHLSelectFireMachineGun );

	CWeaponPipeBombLauncher();

	DECLARE_SERVERCLASS();
	
	void	Precache( void );
	void	AddViewKick( void );
	void	PrimaryAttack( void );
	void	SecondaryAttack( void );

	int		GetMinBurst() { return 2; }
	int		GetMaxBurst() { return 5; }

	virtual void Equip( CBaseCombatCharacter *pOwner );
	bool	Reload( void );

	float	GetFireRate( void ) { return 0.075f; }	// 13.3hz
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

	void AddPipeBomb( CGrenadePipeBomb *pBomb );
	void RemovePipeBomb( CBaseEntity *pBomb );

	void DetonatePipeBombs();
	void UpdateOnRemove();

protected:

	// List of active pipebombs
	typedef CHandle<CGrenadePipeBomb>	PipebombHandle;
	CUtlVector<PipebombHandle>		m_Pipebombs;

	Vector	m_vecTossVelocity;
	float	m_flNextGrenadeCheck;
};

IMPLEMENT_SERVERCLASS_ST(CWeaponPipeBombLauncher, DT_WeaponPipeBombLauncher)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_pipebomblauncher, CWeaponPipeBombLauncher );
PRECACHE_WEAPON_REGISTER( weapon_pipebomblauncher );

BEGIN_DATADESC( CWeaponPipeBombLauncher )

	DEFINE_FIELD( m_vecTossVelocity, FIELD_VECTOR ),
	DEFINE_FIELD( m_flNextGrenadeCheck, FIELD_TIME ),

END_DATADESC()




//////////////////////////////////////////////////////////////////////////
//
// Implementation
//
//////////////////////////////////////////////////////////////////////////



#define PIPEBOMB_MAX_DANGER_RADIUS 50

CGrenadePipeBomb::CGrenadePipeBomb()
{
	;
}

void CGrenadePipeBomb::Spawn()
{
	BaseClass::Spawn();

	SetTouch( &CGrenadePipeBomb::GrenadePipeBombTouch );
	SetThink( &CGrenadePipeBomb::GrenadePipeBombThink );
}

void CGrenadePipeBomb::GrenadePipeBombThink()
{
	SetNextThink( gpGlobals->curtime + 0.05f );

	if (!m_bIsLive)
	{
		// Go live after a short delay
		if (m_fSpawnTime + 0.1 < gpGlobals->curtime)
		{
			m_bIsLive  = true;
		}
	}

	if( m_fDangerRadius <= PIPEBOMB_MAX_DANGER_RADIUS )
	{
		m_fDangerRadius += ( PIPEBOMB_MAX_DANGER_RADIUS * 0.05 );
	}

	CSoundEnt::InsertSound( SOUND_DANGER, GetAbsOrigin() + GetAbsVelocity() * 0.5, m_fDangerRadius, 0.2, this, SOUNDENT_CHANNEL_REPEATED_DANGER );
	
	// BaseClass::GrenadeAR2Think();
}

void CGrenadePipeBomb::GrenadePipeBombTouch( CBaseEntity *pOther )
{
	Assert( pOther );
	if ( !pOther->IsSolid() )
		return;

	// If I'm live go ahead and blow up
	if (m_bIsLive && 0)
	{
		Detonate();
	}
	else
	{
		// If I'm not live, only blow up if I'm hitting an chacter that
		// is not the owner of the weapon
		CBaseCombatCharacter *pBCC = ToBaseCombatCharacter( pOther );
		if (pBCC && GetThrower() != pBCC)
		{
			m_bIsLive = true;
			Detonate();
		}
	}
	
	// BaseClass::GrenadeAR2Touch( pOther );
}

void CGrenadePipeBomb::Detonate()
{
	BaseClass::Detonate();
}

void CGrenadePipeBomb::Event_Killed( const CTakeDamageInfo &info )
{
	Detonate( );
}

void CGrenadePipeBomb::UpdateOnRemove()
{
	if ( CWeaponPipeBombLauncher *pLauncher = dynamic_cast< CWeaponPipeBombLauncher * >( GetLauncher() ) )
	{
		pLauncher->RemovePipeBomb( this );
	}
	BaseClass::UpdateOnRemove();
}

void CGrenadePipeBomb::Precache()
{
	BaseClass::Precache();
}




//=========================================================
CWeaponPipeBombLauncher::CWeaponPipeBombLauncher( )
{
	m_fMinRange1		= 0;// No minimum range. 
	m_fMaxRange1		= 1400;

	// m_bReloadsSingly	= true;
	m_bFiresUnderwater	= true;
	m_bAltFiresUnderwater = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponPipeBombLauncher::Precache( void )
{
	UTIL_PrecacheOther("grenade_pipebomb");

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: Give this weapon longer range when wielded by an ally NPC.
//-----------------------------------------------------------------------------
void CWeaponPipeBombLauncher::Equip( CBaseCombatCharacter *pOwner )
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
void CWeaponPipeBombLauncher::FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir )
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
void CWeaponPipeBombLauncher::Operator_ForceNPCFire( CBaseCombatCharacter *pOperator, bool bSecondary, CBaseEntity *pTarget )
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
void CWeaponPipeBombLauncher::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
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
Activity CWeaponPipeBombLauncher::GetPrimaryAttackActivity( void )
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
bool CWeaponPipeBombLauncher::Reload( void )
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
void CWeaponPipeBombLauncher::AddViewKick( void )
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
void CWeaponPipeBombLauncher::PrimaryAttack( void )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	
	if ( pPlayer == NULL )
		return;

	//Must have ammo
	if ( ( pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 ) || ( pPlayer->GetWaterLevel() == 3 ) )
	{
		SendWeaponAnim( ACT_VM_DRYFIRE );
		BaseClass::WeaponSound( EMPTY );
		m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;
		return;
	}

	if( m_bInReload )
		m_bInReload = false;

	// MUST call sound before removing a round from the clip of a CMachineGun
	BaseClass::WeaponSound( WPN_DOUBLE );

	pPlayer->RumbleEffect( RUMBLE_357, 0, RUMBLE_FLAGS_NONE );

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector	vecThrow;
	// Don't autoaim on grenade tosses
	// TrackIR
	if ( IsHeadTrackingEnabled() )
	{
		QAngle anglesTemp;
		Vector vAim = pPlayer->GetAutoaimVector(AUTOAIM_5DEGREES);
		VectorAngles(vAim, anglesTemp);
		AngleVectors( anglesTemp + pPlayer->GetPunchAngle(), &vecThrow );
		// TrackIR
	}
	else
	{
		AngleVectors( pPlayer->EyeAngles() + pPlayer->GetPunchAngle(), &vecThrow );
	}
	
	VectorScale( vecThrow, 1000.0f, vecThrow );
	
	//Create the grenade
	QAngle angles;
	VectorAngles( vecThrow, angles );
	CGrenadePipeBomb *pGrenade = (CGrenadePipeBomb*)Create( "grenade_pipebomb", vecSrc, angles, pPlayer );
	pGrenade->SetAbsVelocity( vecThrow );

	pGrenade->SetLocalAngularVelocity( RandomAngle( -400, 400 ) );
	pGrenade->SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_BOUNCE ); 
	pGrenade->SetThrower( GetOwner() );
	pGrenade->SetDamage( sk_plr_dmg_smg1_grenade.GetFloat() );

	AddPipeBomb( pGrenade );

	SendWeaponAnim( ACT_VM_SECONDARYATTACK );

	AddViewKick();

	CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), 1000, 0.2, GetOwner(), SOUNDENT_CHANNEL_WEAPON );

	// player "shoot" animation
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	// Decrease ammo
	pPlayer->RemoveAmmo( 1, m_iPrimaryAmmoType );

	// Can shoot again immediately
 	m_flNextPrimaryAttack = gpGlobals->curtime + 1.0f;

	// Can blow up after a short delay (so have time to release mouse button)
	m_flNextSecondaryAttack = gpGlobals->curtime + 1.0f;

	// Register a muzzleflash for the AI.
	pPlayer->SetMuzzleFlashTime( gpGlobals->curtime + 0.5 );	

	m_iPrimaryAttacks++;
	gamestats->Event_WeaponFired( pPlayer, true, GetClassname() );
}

void CWeaponPipeBombLauncher::SecondaryAttack()
{
	DetonatePipeBombs();
}

void CWeaponPipeBombLauncher::DetonatePipeBombs()
{
	for ( int i = 0; i < m_Pipebombs.Count(); ++ i )
	{
		CGrenadePipeBomb *pBomb = m_Pipebombs[i];
		if ( pBomb )
		{
			if ( pBomb->IsEffectActive( EF_NODRAW ) )
				continue;

			pBomb->Detonate();
		}
	}
}

void CWeaponPipeBombLauncher::UpdateOnRemove()
{
	if ( g_pGameRules->IsMultiplayer() )
	{
		DetonatePipeBombs();
	}

	BaseClass::UpdateOnRemove();
}

#define	COMBINE_MIN_GRENADE_CLEAR_DIST 256

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flDot - 
//			flDist - 
// Output : int
//-----------------------------------------------------------------------------
int CWeaponPipeBombLauncher::WeaponRangeAttack2Condition( float flDot, float flDist )
{
	// CAI_BaseNPC *npcOwner = GetOwner()->MyNPCPointer();

	return COND_NONE;
}

//-----------------------------------------------------------------------------
const WeaponProficiencyInfo_t *CWeaponPipeBombLauncher::GetProficiencyValues()
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

void CWeaponPipeBombLauncher::AddPipeBomb( CGrenadePipeBomb *pBomb )
{
	pBomb->SetLauncher( this );

	PipebombHandle hHandle;
	hHandle = pBomb;
	m_Pipebombs.AddToTail( hHandle );
}

void CWeaponPipeBombLauncher::RemovePipeBomb( CBaseEntity *pBomb )
{
	PipebombHandle hHandle;
	hHandle = ( CGrenadePipeBomb * ) pBomb;
	m_Pipebombs.FindAndRemove( hHandle );
}


