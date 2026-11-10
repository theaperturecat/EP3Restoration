//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ====
//
// Flame throwa!
//
//=============================================================================

#include "cbase.h"
#include "weapon_flamethrower.h"
#include "triggers.h"
#include "soundenvelope.h"
#include "basehlcombatweapon.h"
#include "soundent.h"
#include "ai_basenpc.h"
#include "fire.h"
#include "EntityFlame.h"
#include "hl2_shareddefs.h"
#include "particle_system.h"
#include "particle_parse.h"
#include "in_buttons.h"
#include "eventqueue.h"
#include "weaponizer_shared.h"
#include "saverestore_utlvector.h"
#include "collisionutils.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#define	MEGACANNON_MODEL "models/weapons/v_superphyscannon.mdl"
#define	MEGACANNON_SKIN	1
#define PRIMARY_REFIRE_RATE 0.1f
#define SECONDARY_REFIRE_RATE 0.01f

// position of end of muzzle relative to shoot position
#define FLAMETHROWER_MUZZLEPOS_FORWARD		70.0f
#define FLAMETHROWER_MUZZLEPOS_RIGHT			12.0f
#define FLAMETHROWER_MUZZLEPOS_UP			-12.0f

ConVar flamethrower_fire_damage( "flamethrower_fire_damage", "2", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );
ConVar flamethrower_liquid_velocity( "flamethrower_liquid_velocity", "900", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );
ConVar flamethrower_liquid_debug( "flamethrower_liquid_debug", "0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );
ConVar flamethrower_liquid_max_travel_time( "flamethrower_liquid_max_travel_time", "0.75", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );
ConVar flamethrower_flametime( "flamethrower_flametime", "0.5", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Time to live of flame damage entities." );
ConVar flamethrower_boxsize( "flamethrower_boxsize", "6.0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Size of flame damage entities." );
ConVar flamethrower_touchsize( "flamethrower_touchsize", "36.0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Size of flame damage entities." );
ConVar flamethrower_maxdamagedist( "flamethrower_maxdamagedist", "350.0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Maximum damage distance for flamethrower." );
ConVar flamethrower_shortrangedamagemultiplier( "flamethrower_shortrangedamagemultiplier", "1.2", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Damage multiplier for close-in flamethrower damage." );
ConVar flamethrower_velocityfadestart( "flamethrower_velocityfadestart", ".3", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Time at which attacker's velocity contribution starts to fade." );
ConVar flamethrower_velocityfadeend( "flamethrower_velocityfadeend", ".5", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Time at which attacker's velocity contribution finishes fading." );
ConVar flamethrower_velocity( "flamethrower_velocity", "2300.0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Initial velocity of flame damage entities." );
ConVar flamethrower_vecrand( "flamethrower_vecrand", "0.05", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Random vector added to initial velocity of flame damage entities." );
ConVar flamethrower_drag( "flamethrower_drag", "0.89", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Air drag of flame damage entities." );
ConVar flamethrower_debug( "flamethrower_debug", "0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Visualize the flamethrower damage." );
ConVar flamethrower_float( "flamethrower_float", "50.0", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY, "Upward float velocity of flame damage entities." );


//-----------------------------------------------------------------------------
// Flames shot from the flamethrower
//-----------------------------------------------------------------------------
class CFlamethrowerFlame : public CBaseEntity
{
	DECLARE_CLASS( CFlamethrowerFlame, CBaseEntity );
	DECLARE_DATADESC();

public:
	virtual void Precache();
	virtual void Spawn( void );

public:
	static CFlamethrowerFlame *Create( const Vector &vecOrigin, const QAngle &vecAngles, CBaseEntity *pOwner, int iDmgType, float m_flDmgAmount );

private:
	void FlameThink( void );
	void BurnTouch( CBaseEntity *pOther );

	Vector					m_vecInitialPos;		// position the flame was fired from
	Vector					m_vecPrevPos;			// position from previous frame
	Vector					m_vecBaseVelocity;		// base velocity vector of the flame (ignoring rise effect)
	Vector					m_vecAttackerVelocity;	// velocity of attacking player at time flame was fired
	float					m_flTimeRemove;			// time at which the flame should be removed
	int						m_iDmgType;				// damage type
	float					m_flDmgAmount;			// amount of base damage
	CUtlVector<EHANDLE>		m_hEntitiesBurnt;		// list of entities this flame has burnt
	EHANDLE					m_hAttacker;			// attacking player
};


//-----------------------------------------------------------------------------
// Flame thrower weapon 
//-----------------------------------------------------------------------------
class CWeaponFlamethrower : public CBaseHLCombatWeapon
{
	DECLARE_CLASS( CWeaponFlamethrower, CBaseHLCombatWeapon );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

public:
	CWeaponFlamethrower( void );
	virtual ~CWeaponFlamethrower( void );

	void Precache();

	virtual void	Spawn();
	virtual void	StopLoopingSounds();

	virtual void	PrimaryAttack( );
	virtual void	SecondaryAttack( );
	virtual	void	WeaponIdle();
	virtual bool	Holster( CBaseCombatWeapon *pSwitchingTo );
	void	ItemPostFrame();

	virtual float GetMaxAutoAimDeflection() { return 0.0f; }

	void	SetSkin( int skinNum );
	
	bool	ShouldDisplayHUDHint() { return true; }

	void	ShootFlames( );
	void	ShootLiquidFuel( );
	Vector GetFlameOriginPos();
	Vector GetMuzzlePosHelper( bool bVisualPos );

protected:
	// Holds fractional part of ammo for accumulation
	float				m_flAmmoRemainder; 

	CNetworkVar( bool,	m_bFiringSecondary );
	CNetworkVar( bool,	m_bFiringPrimary );

	float				m_flNextPrimaryAnimationTime;
	float				m_flNextSecondaryAnimationTime;
	
	float				m_flNextWarningSound;

	CSoundPatch			*m_pFiringLoop;
	CSoundPatch			*m_pLiquidLoop;
};


IMPLEMENT_SERVERCLASS_ST( CWeaponFlamethrower, DT_WeaponFlamethrower )
	SendPropBool( SENDINFO( m_bFiringSecondary ) ),
	SendPropBool( SENDINFO( m_bFiringPrimary ) ),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_flamethrower, CWeaponFlamethrower );
PRECACHE_WEAPON_REGISTER( weapon_flamethrower );

BEGIN_DATADESC( CWeaponFlamethrower )

	DEFINE_FIELD( m_flNextPrimaryAnimationTime , FIELD_TIME ),
	DEFINE_FIELD( m_flNextSecondaryAnimationTime , FIELD_TIME ),

	DEFINE_FIELD( m_flAmmoRemainder, FIELD_FLOAT ),

	DEFINE_FIELD( m_bFiringSecondary, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bFiringPrimary, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flNextWarningSound, FIELD_TIME ),

END_DATADESC()


//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CWeaponFlamethrower::CWeaponFlamethrower( void )
{
}

CWeaponFlamethrower::~CWeaponFlamethrower( void )
{
}

						        
//-----------------------------------------------------------------------------
// Purpose: Precache
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::Precache( void )
{
	UTIL_PrecacheOther( "weapon_flamethrower_flame" );
	UTIL_PrecacheOther( "prop_liquid_blob" );

	// Precache our alternate model
	PrecacheModel( MEGACANNON_MODEL );
	PrecacheModel("models/spitball_small.mdl"); 
						      
	PrecacheScriptSound( "Weapon_FlameThrower.FireLoop" );
	PrecacheScriptSound( "Weapon_FlameThrower.FireStart" );
	PrecacheScriptSound( "Weapon_FlameThrower.FuelFireStart" );
	PrecacheScriptSound( "Weapon_FlameThrower.FuelFireLoop" );

	PrecacheParticleSystem( "water" );
	PrecacheParticleSystem( "flamethrower" );
	PrecacheParticleSystem( "fire_jet_01" );
	PrecacheParticleSystem( "fusion_flamethrower" );
	PrecacheParticleSystem( "burning_character_weaponizer" );
	PrecacheParticleSystem( "fuel_hose" );
	PrecacheParticleSystem( "absorb_fuel" );
	PrecacheParticleSystem( "tube_fire" );
	PrecacheParticleSystem(	"tube_fuel" );
	PrecacheParticleSystem( "world_fuel_source" );

	BaseClass::Precache();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::Spawn( void )
{
	BaseClass::Spawn();
	
	// The megacannon uses a different skin
	m_nSkin = MEGACANNON_SKIN;

	m_bFiresUnderwater = m_bAltFiresUnderwater = false;

	m_flAmmoRemainder = 0.0f;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the position at which to spawn flame damage entities
//-----------------------------------------------------------------------------
Vector CWeaponFlamethrower::GetFlameOriginPos()
{
	return GetMuzzlePosHelper( false );
}


//-----------------------------------------------------------------------------
// Purpose: Returns the position of the tip of the muzzle
//-----------------------------------------------------------------------------
Vector CWeaponFlamethrower::GetMuzzlePosHelper( bool bVisualPos )
{
	Vector vecMuzzlePos = GetAbsOrigin();
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner ) 
	{
		Vector vecForward, vecRight, vecUp;
		AngleVectors( pOwner->GetAbsAngles(), &vecForward, &vecRight, &vecUp );
		vecMuzzlePos = pOwner->Weapon_ShootPosition();
		vecMuzzlePos +=  vecRight * FLAMETHROWER_MUZZLEPOS_RIGHT;
		// if asking for visual position of muzzle, include the forward component
		if ( bVisualPos )
		{
			vecMuzzlePos +=  vecForward * FLAMETHROWER_MUZZLEPOS_FORWARD;
		}
	}
	return vecMuzzlePos;
}


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::ShootFlames( )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	//Don't fire fire underwater.
	if ( pOwner->GetWaterLevel() == 3 )
		return;

	if ( m_flNextWarningSound <= gpGlobals->curtime )
	{
		CSoundEnt::InsertSound( SOUND_THUMPER | SOUND_DANGER | SOUND_CONTEXT_FROM_FIRE, pOwner->GetAbsOrigin(), 384, 1.0f, this );
		m_flNextWarningSound = gpGlobals->curtime + 1.0f;
	}
	
	if ( m_pFiringLoop == NULL )
	{
		EmitSound( "Weapon_FlameThrower.FireStart" );

		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		CPASAttenuationFilter filter( this );
		m_pFiringLoop = controller.SoundCreate( filter, entindex(), "Weapon_FlameThrower.FireLoop" );
		controller.Play( m_pFiringLoop, 0.0, 100 );
		controller.SoundChangeVolume( m_pFiringLoop, 1.0, 3.4 );
	}

	// create the flame entity
	float flDamage = flamethrower_fire_damage.GetFloat();
	CFlamethrowerFlame::Create( GetFlameOriginPos(), pOwner->EyeAngles(), this, DMG_BURN | DMG_DIRECT, flDamage );
}

//-----------------------------------------------------------------------------
// this will hit skip the pass entity, but not anything it owns 
// (lets player grab own grenades)
class CTraceFilterNoLiquid : public CTraceFilterSimple
{
public:
	DECLARE_CLASS( CTraceFilterNoLiquid, CTraceFilterSimple );

	CTraceFilterNoLiquid( IHandleEntity *passentity, int collisionGroup )
		: CTraceFilterSimple( NULL, collisionGroup ), m_pIgnoreEnt(passentity)
	{
	}


	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		CBaseEntity *pIgnore = EntityFromEntityHandle( m_pIgnoreEnt );

		if ( pEntity == NULL )
			return false;

		if ( pHandleEntity == m_pIgnoreEnt )
			return false;

		if ( pEntity == pIgnore->GetOwnerEntity() )
			return false;

		if ( pEntity->GetCollisionGroup() == HL2COLLISION_GROUP_FUEL )
			return false;

		return BaseClass::ShouldHitEntity( pHandleEntity, contentsMask );
	}

protected:
	IHandleEntity *m_pIgnoreEnt;
};

void CreateLiquid( Vector vOrigin, Vector vDirection, int iType, float flTime );

void ShootLiquid( CBasePlayer *pPlayer )
{
	Vector	vForward, vRight, vUp;

	pPlayer->EyeVectors( &vForward, &vRight, &vUp );

	bool bCollided = false;
	bool bInSky = false;
	float gravity = -gpGlobals->frametime * 800;

	Vector vOrigin = pPlayer->Weapon_ShootPosition() + vForward * 12.0f + vRight * 3.25 + vUp * -2.25f;
	Vector vVelocity = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DIRECT_ONLY ) * flamethrower_liquid_velocity.GetInt();
	CBaseEntity *pEntity = NULL;
	trace_t pm;

	float flDistance = 0.0f;

	int iFailSafe = 0;

	while ( bCollided == false && iFailSafe < 1000 )
	{
		Vector vOldOrigin = vOrigin;
		vOrigin = vOrigin + vVelocity * gpGlobals->frametime;

		if ( flamethrower_liquid_debug.GetBool() == true )
		{
			NDebugOverlay::SweptBox( vOldOrigin, vOrigin, Vector( -8, -8, -8 ), Vector( 8, 8, 8 ), pPlayer->GetAbsAngles(), 255, 0, 0, 255, 5 );
		}

		flDistance += (vOrigin - vOldOrigin).Length();

		float flTravelTime = flDistance / vVelocity.Length();

		if ( flTravelTime > flamethrower_liquid_max_travel_time.GetFloat() )
		{
			flTravelTime = flamethrower_liquid_max_travel_time.GetFloat();

			if ( flamethrower_liquid_debug.GetBool() == true )
			{
				NDebugOverlay::Box( vOrigin, Vector( 16, 16, 16 ), Vector( -16, -16, -16 ), 255, 0, 0, 0, 5 );
			}

			break;
		}


		CTraceFilterNoLiquid filter( pPlayer->GetActiveWeapon(), COLLISION_GROUP_NONE );
		
		UTIL_TraceLine( vOldOrigin, vOrigin, MASK_SOLID, &filter, &pm );

		int iContent = UTIL_PointContents( pm.endpos, CONTENTS_WATER );

		if ( pm.surface.flags & SURF_SKY || pm.allsolid == true || iContent & CONTENTS_WATER ) 
		{
			bInSky = true;
			break;
		}

		if ( pm.fraction != 1.0f )
		{
			bCollided = true;
			vOrigin = pm.endpos;
			pEntity = pm.m_pEnt;

			if ( flamethrower_liquid_debug.GetBool() == true )
			{
				NDebugOverlay::Box( vOrigin, Vector( 16, 16, 16 ), Vector( -16, -16, -16 ), 0, 255, 0, 0, 5 );
			}
			break;
		}
		else
		{
			vVelocity[2] += gravity;
		}
	}

	if ( !bCollided )
		return;

	if ( pEntity && pEntity->IsWorld() == false )
	{
		// WEAPONIZER TODO: burning objects
		//pEntity->SetAllowedToIgnite( true );
		//pEntity->SetLongerBurnState( true );

		DispatchParticleEffect( "water_hose_droplets", pm.endpos, vec3_angle );
		return;
	}

	while ( (pEntity = gEntList.FindEntityInSphere( pEntity, pm.endpos, 8 )) != NULL )
	{
		// Prevent fuel from getting too close to other fuel
		if ( pEntity->GetCollisionGroup() == HL2COLLISION_GROUP_FUEL )
		{
			if ( pm.endpos.DistToSqr( pEntity->GetAbsOrigin() ) < 196 )
				return;
			continue;
		}
	}

	trace_t tr;
	CTraceFilterNoLiquid filter( pPlayer->GetActiveWeapon(), COLLISION_GROUP_NONE );
	UTIL_TraceLine( pm.endpos, pm.endpos + vVelocity * 64, MASK_SOLID, &filter, &tr );

	if ( tr.fraction != 1.0f )
	{
		float flSpeed = VectorNormalize( vVelocity );
		float flTravelTime = flDistance / ( flSpeed != 0.0 ? flSpeed : 1.0f );
		CreateLiquid( tr.endpos, vVelocity, WEAPONIZER_MATERIAL_TYPE_EXPLOSIVE, flTravelTime );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::ShootLiquidFuel( )
{
	ShootLiquid( ToBasePlayer( GetOwner() ) );

	if ( m_pLiquidLoop == NULL )
	{
		EmitSound( "Weapon_FlameThrower.FuelFireStart" );

		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		CPASAttenuationFilter filter( this );
		m_pLiquidLoop = controller.SoundCreate( filter, entindex(), "Weapon_FlameThrower.FuelFireLoop" );
		controller.Play( m_pLiquidLoop, 0.0, 100 );
		controller.SoundChangeVolume( m_pLiquidLoop, 1.0, 3.4 );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::SetSkin( int skinNum )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	CBaseViewModel *pViewModel = pOwner->GetViewModel();
	if ( pViewModel == NULL )
		return;

	pViewModel->m_nSkin = skinNum;
}


//-----------------------------------------------------------------------------
// Shoot the weaponizer's primary weapon!
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::PrimaryAttack( )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	if ( pOwner->GetAmmoCount( m_iPrimaryAmmoType ) <= 0 )
	{
		if ( m_flNextEmptySoundTime < gpGlobals->curtime )
		{
			WeaponSound(EMPTY);
			m_flNextSecondaryAttack = m_flNextEmptySoundTime = gpGlobals->curtime + 0.5;
		}
		Holster( NULL );
		return;
	}

	pOwner->RemoveAmmo( 1, m_iPrimaryAmmoType );

	m_bFiringPrimary = true;
	m_bFiringSecondary = false;

	ShootFlames( );

	if ( m_flNextPrimaryAnimationTime <= gpGlobals->curtime )
	{
		SendWeaponAnim( ACT_VM_PRIMARYATTACK );
		m_flNextPrimaryAnimationTime = gpGlobals->curtime + SequenceDuration();
	}

	m_flNextPrimaryAttack = gpGlobals->curtime + PRIMARY_REFIRE_RATE;
}


//-----------------------------------------------------------------------------
// Shoot the weaponizer's secondary weapon!
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::SecondaryAttack( )
{					    
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	// Secondary attack uses primary ammo
	if ( pOwner->GetAmmoCount( m_iPrimaryAmmoType ) <=0 )
	{
		if ( m_flNextEmptySoundTime < gpGlobals->curtime )
		{
			WeaponSound(EMPTY);
			m_flNextSecondaryAttack = m_flNextEmptySoundTime = gpGlobals->curtime + 0.5;
		}
		Holster( NULL );
		return;
	}

	COMPILE_TIME_ASSERT( SECONDARY_REFIRE_RATE < PRIMARY_REFIRE_RATE );
	m_flAmmoRemainder += ( SECONDARY_REFIRE_RATE / PRIMARY_REFIRE_RATE );
	if ( m_flAmmoRemainder >= 1.0f )
	{
		pOwner->RemoveAmmo( 1, m_iPrimaryAmmoType );
		m_flAmmoRemainder -= 1.0f;
	}

	m_bFiringPrimary = false;
	m_bFiringSecondary = true;

	ShootLiquidFuel( );

	if ( m_flNextSecondaryAnimationTime <= gpGlobals->curtime )
	{
		SendWeaponAnim( ACT_VM_PRIMARYATTACK );
		m_flNextSecondaryAnimationTime = gpGlobals->curtime + SequenceDuration();
	}

	m_flNextSecondaryAttack = gpGlobals->curtime + SECONDARY_REFIRE_RATE;
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::WeaponIdle( void )
{
	BaseClass::WeaponIdle();
	m_bFiringSecondary = false;
	m_bFiringPrimary = false;
}


bool CWeaponFlamethrower::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	m_bFiringSecondary = false;
	m_bFiringPrimary = false;
	return BaseClass::Holster( pSwitchingTo );
}


//-----------------------------------------------------------------------------
// Purpose: Update our idle effects even when deploying
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::ItemPostFrame()
{
	BaseClass::ItemPostFrame();

	if ( m_bFiringPrimary == false && m_pFiringLoop )
	{
		StopSound( "Weapon_FlameThrower.FireStart" );

		CSoundEnvelopeController::GetController().SoundDestroy( m_pFiringLoop );
		m_pFiringLoop = NULL;
	}

	if ( m_bFiringSecondary == false && m_pLiquidLoop )
	{
		StopSound( "Weapon_FlameThrower.FuelFireStart" );

		CSoundEnvelopeController::GetController().SoundFadeOut( m_pLiquidLoop, 0.5f, true );
		m_pLiquidLoop = NULL;
	}
}



//-----------------------------------------------------------------------------
// Shuts down sounds
//-----------------------------------------------------------------------------
void CWeaponFlamethrower::StopLoopingSounds()
{
	BaseClass::StopLoopingSounds();

	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	if ( m_pFiringLoop )
	{
		controller.SoundDestroy( m_pFiringLoop );
		m_pFiringLoop = NULL;
	}

	if ( m_pLiquidLoop )
	{
		controller.SoundDestroy( m_pLiquidLoop );
		m_pLiquidLoop = NULL;
	}
}



//-----------------------------------------------------------------------------
// Flames are used to burn slimes
//-----------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS( weapon_flamethrower_flame, CFlamethrowerFlame );

BEGIN_DATADESC( CFlamethrowerFlame )
	DEFINE_FIELD( m_vecInitialPos,			FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecPrevPos,				FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecBaseVelocity,		FIELD_VECTOR ),
	DEFINE_FIELD( m_vecAttackerVelocity,	FIELD_VECTOR ),
	DEFINE_FIELD( m_flTimeRemove,			FIELD_TIME ),
	DEFINE_FIELD( m_iDmgType,				FIELD_INTEGER ),
	DEFINE_FIELD( m_flDmgAmount,			FIELD_FLOAT ),
	DEFINE_UTLVECTOR( m_hEntitiesBurnt,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_hAttacker,				FIELD_EHANDLE ),
	DEFINE_ENTITYFUNC( BurnTouch ),
	DEFINE_THINKFUNC( FlameThink ),
END_DATADESC()


//-----------------------------------------------------------------------------
// Purpose: Spawns this entity
//-----------------------------------------------------------------------------
void CFlamethrowerFlame::Precache()
{
	BaseClass::Precache();
}

void CFlamethrowerFlame::Spawn( void )
{
	BaseClass::Spawn();

	SetSolid( SOLID_BBOX );
	SetSolidFlags( FSOLID_NOT_SOLID | FSOLID_TRIGGER );
	SetCollisionGroup( HL2COLLISION_GROUP_FIRE );

	// move noclip: update position from velocity, that's it
	SetMoveType( MOVETYPE_FLY, MOVECOLLIDE_DEFAULT );
	AddEFlags( EFL_NO_WATER_VELOCITY_CHANGE );
	SetTouch( &CFlamethrowerFlame::BurnTouch );

	float iBoxSize = flamethrower_boxsize.GetFloat();
	UTIL_SetSize( this, -Vector( iBoxSize, iBoxSize, iBoxSize ), Vector( iBoxSize, iBoxSize, iBoxSize ) );
	float flTouchSize = flamethrower_touchsize.GetFloat();
	float flBloat = flTouchSize - iBoxSize;
	CollisionProp()->UseTriggerBounds( true, flBloat );

	// Setup attributes.
	m_takedamage = DAMAGE_NO;
	m_vecInitialPos = GetAbsOrigin();
	m_vecPrevPos = m_vecInitialPos;
	float flLifeTime = flamethrower_flametime.GetFloat() * random->RandomFloat( 0.9, 1.1 );
	m_flTimeRemove = gpGlobals->curtime + flLifeTime;

	CSoundEnt::InsertSound( SOUND_DANGER | SOUND_CONTEXT_FROM_FIRE | SOUND_CONTEXT_FOLLOW_OWNER, 
		GetAbsOrigin(), 256, flLifeTime, this );

	// Setup the think function.
	SetThink( &CFlamethrowerFlame::FlameThink );
	SetNextThink( gpGlobals->curtime );
}


//-----------------------------------------------------------------------------
// Purpose: Creates an instance of this entity
//-----------------------------------------------------------------------------
CFlamethrowerFlame *CFlamethrowerFlame::Create( const Vector &vecOrigin, const QAngle &vecAngles, CBaseEntity *pOwner, int iDmgType, float flDmgAmount )
{
	CFlamethrowerFlame *pFlame = static_cast<CFlamethrowerFlame*>( CBaseEntity::Create( "weapon_flamethrower_flame", vecOrigin, vecAngles, pOwner ) );
	if ( !pFlame )
		return NULL;

	// Initialize the owner.
	pFlame->SetOwnerEntity( pOwner );
	pFlame->m_hAttacker = pOwner->GetOwnerEntity();
	pFlame->m_iDmgType = iDmgType;
	pFlame->m_flDmgAmount = flDmgAmount;

	// Setup the initial velocity.
	Vector vecForward, vecRight, vecUp;
	AngleVectors( vecAngles, &vecForward, &vecRight, &vecUp );

	float velocity = flamethrower_velocity.GetFloat();
	pFlame->m_vecBaseVelocity = vecForward * velocity;
	pFlame->m_vecBaseVelocity += RandomVector( -velocity * flamethrower_vecrand.GetFloat(), velocity * flamethrower_vecrand.GetFloat() );
	pFlame->m_vecAttackerVelocity = pOwner->GetOwnerEntity()->GetAbsVelocity();
	pFlame->SetAbsVelocity( pFlame->m_vecBaseVelocity );	
	// Setup the initial angles.
	pFlame->SetAbsAngles( vecAngles );

	return pFlame;
}

//-----------------------------------------------------------------------------
// Purpose: Think method
//-----------------------------------------------------------------------------
void CFlamethrowerFlame::FlameThink( void )
{
	// if we've expired, remove ourselves
	if ( gpGlobals->curtime >= m_flTimeRemove )
	{
		UTIL_Remove( this );
		return;
	}

	// Calculate how long the flame has been alive for
	float flFlameElapsedTime = flamethrower_flametime.GetFloat() - ( m_flTimeRemove - gpGlobals->curtime );
	// Calculate how much of the attacker's velocity to blend in to the flame's velocity.  The flame gets the attacker's velocity
	// added right when the flame is fired, but that velocity addition fades quickly to zero.
	float flAttackerVelocityBlend = RemapValClamped( flFlameElapsedTime, flamethrower_velocityfadestart.GetFloat(), 
		flamethrower_velocityfadeend.GetFloat(), 1.0, 0 );

	// Reduce our base velocity by the air drag constant
	m_vecBaseVelocity *= flamethrower_drag.GetFloat();

	// Add our float upward velocity
	Vector vecVelocity = m_vecBaseVelocity + Vector( 0, 0, flamethrower_float.GetFloat() ) + ( flAttackerVelocityBlend * m_vecAttackerVelocity );

	// Update our velocity
	SetAbsVelocity( vecVelocity );

	// Render debug visualization if convar on
	if ( flamethrower_debug.GetInt() )
	{
		if ( m_hEntitiesBurnt.Count() > 0 )
		{
			int val = ( (int) ( gpGlobals->curtime * 10 ) ) % 255;
			NDebugOverlay::EntityBounds(this, val, 255, val, 0 ,0 );
		} 
		else 
		{
			NDebugOverlay::EntityBounds(this, 0, 100, 255, 0 ,0) ;
		}
	}

	SetNextThink( gpGlobals->curtime );

	m_vecPrevPos = GetAbsOrigin();
}


//-----------------------------------------------------------------------------
// Purpose: Called when we've collided with another entity
//-----------------------------------------------------------------------------
void CFlamethrowerFlame::BurnTouch( CBaseEntity *pOther )
{						  
	CBaseEntity *pAttacker = m_hAttacker;
	if ( !pAttacker )
		return;

	if ( pOther == pAttacker )
		return;

	if ( pOther->m_takedamage == DAMAGE_NO )
		return;
						 
	// remember that we've burnt this player
	m_hEntitiesBurnt.AddToTail( pOther );

	float flDistance = GetAbsOrigin().DistTo( m_vecInitialPos );
	float flMultiplier;
	if ( flDistance <= 125 )
	{
		// at very short range, apply short range damage multiplier
		flMultiplier = flamethrower_shortrangedamagemultiplier.GetFloat();
	}
	else
	{
		// make damage ramp down from 100% to 25% from half the max dist to the max dist
		flMultiplier = RemapValClamped( flDistance, flamethrower_maxdamagedist.GetFloat()/2, flamethrower_maxdamagedist.GetFloat(), 1.0, 0.25 );
	}
	float flDamage = m_flDmgAmount * flMultiplier;
	flDamage = MAX( flDamage, 1.0 );
	if ( flamethrower_debug.GetInt() )
	{
		Msg( "Flame touch dmg: %.1f\n", flDamage );
	}

	// We collided with pOther, so try to find a place on their surface to show blood
	trace_t pTrace;
	UTIL_TraceLine( WorldSpaceCenter(), pOther->WorldSpaceCenter(), MASK_SOLID|CONTENTS_HITBOX, this, COLLISION_GROUP_NONE, &pTrace );

	CTakeDamageInfo info( GetOwnerEntity(), pAttacker, GetOwnerEntity(), flDamage, m_iDmgType );
	info.SetReportedPosition( pAttacker->GetAbsOrigin() );
	info.SetDamagePosition( pTrace.endpos );
	pOther->TakeDamage( info );
	ApplyMultiDamage();
}
