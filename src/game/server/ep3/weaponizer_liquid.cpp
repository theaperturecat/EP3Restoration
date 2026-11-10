//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ====
//
// Concrete. Fires flak.
//
//=============================================================================

#include "cbase.h"
#include "decals.h"
#include "player.h"
#include "gamerules.h"
#include "soundenvelope.h"
#include "engine/IEngineSound.h"
#include "physics.h"
#include "in_buttons.h"
#include "soundent.h"
#include "IEffects.h"
#include "ndebugoverlay.h"
#include "shake.h"
#include "shot_manipulator.h"
#include "hl2_player.h"
#include "beam_shared.h"
#include "sprite.h"
#include "util.h"
#include "weapon_weaponizer.h"
#include "physics_saverestore.h"
#include "ai_basenpc.h"
#include "player_pickup.h"
#include "physics_prop_ragdoll.h"
#include "globalstate.h"
#include "props.h"
#include "movevars_shared.h"
#include "basehlcombatweapon.h"
#include "te_effect_dispatch.h"
#include "vphysics/friction.h"
#include "saverestore_utlvector.h"
#include "prop_combine_ball.h"
#include "physobj.h"
#include "props.h"
#include "hl2_gamerules.h"
#include "citadel_effects_shared.h"
#include "eventqueue.h"
#include "model_types.h"
#include "ai_interactions.h"
#include "rumble_shared.h"
#include "gamestats.h"
#include "particle_system.h"
#include "particle_parse.h"
#include "fire.h"
#include "EntityFlame.h"
#include "triggers.h"
#include "explode.h"
#include "collisionutils.h"
#include "weaponizer_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar weaponizer_liquid_lifetime( "weaponizer_liquid_lifetime", "60" );
ConVar weaponizer_liquid_propagate_time( "weaponizer_liquid_propagate_time", "0.1" );

class CLiquidBlob : public CBaseAnimating
{
public:
	
	DECLARE_CLASS( CLiquidBlob, CBaseAnimating );
	DECLARE_DATADESC();

	virtual void Precache();
	void Spawn( void );
	void InputActivateLiquid( inputdata_t &data );
	int OnTakeDamage( const CTakeDamageInfo &info );
	void CheckPVSThink( void );
	void Electrify( void );

	virtual void Ignite( float flFlameLifetime, bool bNPCOnly = true, float flSize = 0.0f, bool bCalledByLevelDesigner = false );

	Vector m_vDirection;
	int		m_iType;

	bool IsElectrified ( void ){ return m_bElectrified; }

	int GetWeaponizerMaterialType( void ) 
	{ 
		if ( IsElectrified() == true )
			return WEAPONIZER_MATERIAL_TYPE_ELECTRICAL;

		if ( IsOnFire() == true )
			return WEAPONIZER_MATERIAL_TYPE_FIRE;

		return m_iType; 
	}
	

	void FindWaterBlobsThink( void );
	static char const *s_szFindWaterBlobsThink;
	void FindWaterBlobs( void ) { SetContextThink( &CLiquidBlob::FindWaterBlobsThink, gpGlobals->curtime + weaponizer_liquid_propagate_time.GetFloat(), s_szFindWaterBlobsThink ); }

private:

	int m_iBurnDamage;

	bool m_bInPVS;
	float m_flTimeNotInPVS;

	bool	m_bElectrified;
};

char const *CLiquidBlob::s_szFindWaterBlobsThink = "FindWaterBlobsThink";

BEGIN_DATADESC( CLiquidBlob )
	DEFINE_THINKFUNC( CheckPVSThink ),
	DEFINE_THINKFUNC( FindWaterBlobsThink ),
	DEFINE_INPUTFUNC( FIELD_VOID, "ActivateLiquid", InputActivateLiquid ),
	DEFINE_FIELD( m_iBurnDamage, FIELD_INTEGER ),
	DEFINE_FIELD( m_vDirection, FIELD_VECTOR ),
	DEFINE_FIELD( m_bInPVS, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flTimeNotInPVS, FIELD_TIME ),
	DEFINE_FIELD( m_bElectrified, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_iType, FIELD_INTEGER ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( prop_liquid_blob, CLiquidBlob );


void CLiquidBlob::Precache()
{
	BaseClass::Precache();
	PrecacheModel( "models/spitball_small.mdl" );
	UTIL_PrecacheOther( "entityflame" );
}

void CLiquidBlob::Spawn( void )
{
	Precache( );
	SetSolid( SOLID_BBOX );
	SetMoveType( MOVETYPE_NONE );
	SetSolidFlags( FSOLID_NOT_STANDABLE );

	SetModel("models/spitball_small.mdl"); 
	AddEffects( EF_NODRAW );
	
	//UTIL_SetSize( this, vec3_origin, vec3_origin );

	SetCollisionGroup( HL2COLLISION_GROUP_FUEL );

	AddEFlags( EFL_FORCE_CHECK_TRANSMIT );

	// We're self-illuminating, so we don't take or give shadows
	AddEffects( EF_NOSHADOW|EF_NORECEIVESHADOW );

//	DispatchParticleEffect( "water", PATTACH_ABSORIGIN_FOLLOW, this );

	if ( m_iType == WEAPONIZER_MATERIAL_TYPE_EXPLOSIVE )
	{
		// WEAPONIZER TODO: burning objects
		//SetLongerBurnState( true );
		//SetAllowedToIgnite( true );
		UTIL_SetSize( this, Vector( -24, -24, -24 ), Vector( 24, 24, 24 ) );
	}
	else
	{
		UTIL_SetSize( this, Vector( -8, -8, -8 ), Vector( 8, 8, 8 ) );
		SetRenderColor( 0, 0, 255 );
	}

	SetMaxHealth( 100 );
	SetHealth( 100 );
	m_takedamage = DAMAGE_YES;

	m_iBurnDamage = 0;
	m_bInPVS = true;
	m_flTimeNotInPVS = 0.0f;

	SetNextThink( gpGlobals->curtime + 1.0f );
	SetThink( &CLiquidBlob::CheckPVSThink );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CLiquidBlob::CheckPVSThink( void )
{
	if ( IsOnFire() == true )
		return;

	if ( m_bInPVS == false )
	{
		if ( gpGlobals->curtime - m_flTimeNotInPVS > weaponizer_liquid_lifetime.GetInt() )
		{
			UTIL_Remove( this );
			return;
		}
	}

	edict_t *pentPlayer = UTIL_FindClientInPVS( edict() );

	if ( pentPlayer )
	{
		CBasePlayer *pPlayer = (CBasePlayer *)CBaseEntity::Instance( pentPlayer );

		if ( pPlayer )
		{
			m_bInPVS = true;
		}
	}
	else
	{
		if ( m_bInPVS == true )
		{
			m_flTimeNotInPVS = gpGlobals->curtime;
			m_bInPVS = false;
		}
	}

	SetNextThink( gpGlobals->curtime + 1.0f );
	SetThink( &CLiquidBlob::CheckPVSThink );
}

void CLiquidBlob::Ignite( float flFlameLifetime, bool bNPCOnly, float flSize, bool bCalledByLevelDesigner )
{
	BaseClass::Ignite( flFlameLifetime, bNPCOnly, 24, bCalledByLevelDesigner );
	IgniteUseCheapEffect( true );

	FindWaterBlobs();
}

void CLiquidBlob::InputActivateLiquid( inputdata_t &data )
{
	RemoveEffects( EF_NODRAW );

	trace_t tr;
	UTIL_TraceLine( GetAbsOrigin(), GetAbsOrigin() + m_vDirection * 24, MASK_ALL, this, COLLISION_GROUP_NONE, &tr );

	UTIL_DecalTrace( &tr, "BeerSplash" );

	QAngle angDir;
	VectorAngles( tr.plane.normal, angDir );

	if ( IsElectrified() )
	{
		DispatchParticleEffect( "water_puddle_electrified", PATTACH_ABSORIGIN_FOLLOW, this );
	}
	else
	{
		DispatchParticleEffect( "water_hose_droplets", tr.endpos, -angDir );
	}
}

void CLiquidBlob::Electrify( void )
{
	if ( m_bElectrified == true )
		return;

	DispatchParticleEffect( "water_puddle_electrified", PATTACH_ABSORIGIN_FOLLOW, this );

	m_bElectrified = true;
	g_EventQueue.AddEvent( this, "Kill", 30, this, this );

	g_pEffects->Sparks( GetAbsOrigin() );

	FindWaterBlobs();
}

void CLiquidBlob::FindWaterBlobsThink( void )
{
	SetContextThink( NULL, 0, s_szFindWaterBlobsThink );

	CBaseEntity *pEntity = NULL;

	while ( (pEntity  = gEntList.FindEntityInSphere( pEntity, WorldSpaceCenter(), BoundingRadius() )) != NULL )
	{
		CLiquidBlob *pLiquidBlob = dynamic_cast<CLiquidBlob *>(pEntity);

		if( pLiquidBlob == NULL )
		{
			if ( IsElectrified() == true )
			{
				// FIXME: need to find water entities
				//if (  GetWeaponizerMaterialType( pEntity ) == WEAPONIZER_MATERIAL_TYPE_WATER )
				//{
				//	g_EventQueue.AddEvent( pEntity, "StartWaterShock", 0.0f, this, this );
				//}
			}
			continue;
		}

		if ( IsElectrified() != pLiquidBlob->IsElectrified() )
		{
			CLiquidBlob *pElectrify = ( IsElectrified() ? pLiquidBlob : this );
			pElectrify->Electrify();
		}

		if ( IsOnFire() != pLiquidBlob->IsOnFire() )
		{
			CLiquidBlob *pIgnite = ( IsOnFire() ? pLiquidBlob : this );
			float flLength = (float)( pIgnite->GetMaxHealth() / FLAME_DIRECT_DAMAGE_PER_SEC );
			pIgnite->Ignite( flLength, false );
		}
		
	}
}

int CLiquidBlob::OnTakeDamage( const CTakeDamageInfo &info )
{
	if ( m_iType == WEAPONIZER_MATERIAL_TYPE_WATER )
	{
		if ( info.GetDamageType() & DMG_SHOCK )
		{
			Electrify();
		}

		return 0;
	}

	if ( IsEffectActive( EF_NODRAW ) )
		return 0;

	if ( info.GetDamageType() & DMG_BLAST && IsMarkedForDeletion() == false )
	{
		CLiquidBlob *pLiquid = dynamic_cast< CLiquidBlob *>( info.GetInflictor() );

		if ( pLiquid )
		{
			float xSize = CollisionProp()->OBBMaxs().x - CollisionProp()->OBBMins().x;
			float ySize = CollisionProp()->OBBMaxs().y - CollisionProp()->OBBMins().y;

			float size = ( xSize + ySize );

			UTIL_Remove( this );

			ExplosionCreate( GetAbsOrigin(), GetAbsAngles(), this, size * 2.0f, size * 2.0f, true );

			return 0;
		}
		else
		{
			float flLength = (float)(GetMaxHealth() / FLAME_DIRECT_DAMAGE_PER_SEC );
			Ignite( flLength, false );
		}
	}

	if ( info.GetDamageType() & DMG_BURN )
	{
		if ( info.GetInflictor() != GetEffectEntity() )
		{
			m_iBurnDamage += info.GetDamage();

			if ( m_iBurnDamage >= 1.0f )
			{
				float flLength = (float)(GetMaxHealth() / FLAME_DIRECT_DAMAGE_PER_SEC );
				Ignite( flLength, false );
			}

			return 0;
		}
	}


	if ( info.GetDamage() != 1.0f )
		return 0;

	return BaseClass::OnTakeDamage( info );
}

void CreateLiquid( Vector vOrigin, Vector vDirection, int iType, float flTime )
{
	CLiquidBlob *pLiquid = static_cast<CLiquidBlob *>(CreateEntityByName( "prop_liquid_blob" ));
	if ( pLiquid != NULL )
	{

		// Set the model
		pLiquid->m_iType = iType;
		pLiquid->SetAbsOrigin( vOrigin - ( vDirection * 4 ) );
		pLiquid->SetAbsAngles( vec3_angle );
		pLiquid->Spawn();
		pLiquid->FindWaterBlobs();
		
		pLiquid->m_vDirection = vDirection;

		DispatchSpawn( pLiquid );
		g_EventQueue.AddEvent( pLiquid, "ActivateLiquid", flTime, pLiquid, pLiquid );
	}
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CFuncLiquidPipe : public CBaseEntity
{
	DECLARE_CLASS( CFuncLiquidPipe, CBaseEntity );
public:

	DECLARE_DATADESC();

	void Spawn( void );
	bool CreateVPhysics( void );

	int OnTakeDamage( const CTakeDamageInfo &info );

private:

	int m_iType;

};

BEGIN_DATADESC( CFuncLiquidPipe )
DEFINE_KEYFIELD( m_iType, FIELD_INTEGER, "type" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( func_liquids_pipe, CFuncLiquidPipe );


void CFuncLiquidPipe::Spawn( void )
{
	Precache();

	SetMoveType( MOVETYPE_NONE );
	SetSolid( SOLID_BSP );
	SetModel( STRING( GetModelName() ) );

	m_takedamage = DAMAGE_YES;

	CreateVPhysics();
}

//-----------------------------------------------------------------------------

bool CFuncLiquidPipe::CreateVPhysics()
{
	VPhysicsInitShadow( false, false );
	return true;
}

int CFuncLiquidPipe::OnTakeDamage( const CTakeDamageInfo &info )
{
	if ( !(info.GetDamageType() & DMG_BULLET) && !(info.GetDamageType() & DMG_BUCKSHOT ) )
		return 0;

	Vector vDir = info.GetDamageForce();

	VectorNormalize( vDir );


	trace_t tr;

	UTIL_TraceLine( info.GetDamagePosition(), info.GetDamagePosition() + vDir * 8, MASK_ALL, this, COLLISION_GROUP_NONE, &tr );

	if ( tr.fraction == 1.0f )
		return 0;

	const char *pszEntityName;
	const char *pszEffectName;

	if ( m_iType == 0 )
	{
		pszEntityName = "func_weaponizer_fuel";
		pszEffectName = "world_fuel_source";

	}
	else
	{
		pszEntityName = "func_weaponizer_water";
		pszEffectName = "world_water_source";
	}

	CBaseEntity *pLiquid = CreateEntityByName( pszEntityName );

	if ( pLiquid )
	{
		pLiquid->Spawn();
		pLiquid->SetAbsOrigin( tr.endpos );
		UTIL_SetSize( pLiquid, Vector( -8, -8, -8 ), Vector( 8, 8, 8 ) );

		QAngle angDir;
		VectorAngles( tr.plane.normal, angDir );

		DispatchParticleEffect( pszEffectName, tr.endpos, angDir );

		g_EventQueue.AddEvent( pLiquid, "PlayLeakSound", 0.0f, this, this );
	}

	return 0;
}