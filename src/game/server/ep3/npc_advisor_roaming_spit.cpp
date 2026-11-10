//====== Copyright c 1996-2008, Valve Corporation, All rights reserved. =======//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "npc_advisor_roaming_spit.h"
#include "soundent.h"
#include "decals.h"
#include "smoke_trail.h"
#include "hl2_shareddefs.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "particle_parse.h"
#include "particle_system.h"
#include "soundenvelope.h"
#include "ai_utils.h"
#include "te_effect_dispatch.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// checking a throw trajectory
Vector VecCheckThrowTolerance( CBaseEntity *pEdict, const Vector &vecSpot1, Vector vecSpot2, float flSpeed, float flTolerance, float flGravity );


ConVar sk_advisor_spit_grenade_dmg( "sk_advisor_spit_grenade_dmg", "20" );
ConVar sk_advisor_spit_grenade_radius( "sk_advisor_spit_grenade_radius", "40" );
ConVar sk_advisor_spit_grenade_poison_ratio( "sk_advisor_spit_grenade_poison_ratio", "0.3" ); 

ConVar sk_advisor_spit_grenade_gravity( "sk_advisor_spit_grenade_gravity", "200" );
ConVar sk_advisor_spit_grenade_velocity( "sk_advisor_spit_grenade_velocity", "800" );


LINK_ENTITY_TO_CLASS( advisor_grenade_spit, CAdvisorGrenadeSpit );


BEGIN_DATADESC( CAdvisorGrenadeSpit )

	DEFINE_FIELD( m_bPlaySound, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bSpitDoesPoisonDamage, FIELD_BOOLEAN ),
	
	// Function pointers
	DEFINE_ENTITYFUNC( GrenadeSpitTouch ),

END_DATADESC()


CAdvisorGrenadeSpit::CAdvisorGrenadeSpit( void ) :
	m_bPlaySound( true ),
	m_pHissSound( NULL ),
	m_bSpitDoesPoisonDamage( true )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CAdvisorGrenadeSpit::Spawn( void )
{
	Precache( );
	SetSolid( SOLID_BBOX );
	SetMoveType( MOVETYPE_FLYGRAVITY );
	SetSolidFlags( FSOLID_NOT_STANDABLE );

	SetModel( "models/spitball_large.mdl" );
	UTIL_SetSize( this, vec3_origin, vec3_origin );

	SetUse( &CBaseGrenade::DetonateUse );
	SetTouch( &CAdvisorGrenadeSpit::GrenadeSpitTouch );
	SetNextThink( gpGlobals->curtime + 0.1f );

	m_flDamage		= sk_advisor_spit_grenade_dmg.GetFloat();
	m_DmgRadius		= sk_advisor_spit_grenade_radius.GetFloat();
	m_takedamage	= DAMAGE_NO;
	m_iHealth		= 1;

	SetGravity( UTIL_ScaleForGravity( sk_advisor_spit_grenade_gravity.GetFloat() ) );
	SetFriction( 0.8f );

	SetCollisionGroup( HL2COLLISION_GROUP_SPIT );

	AddEFlags( EFL_FORCE_CHECK_TRANSMIT );

	// We're self-illuminating, so we don't take or give shadows
	AddEffects( EF_NOSHADOW|EF_NORECEIVESHADOW );

	// Create the dust effect in place
	m_hSpitEffect = (CParticleSystem *) CreateEntityByName( "info_particle_system" );
	if ( m_hSpitEffect != NULL )
	{
		// Setup our basic parameters
		m_hSpitEffect->KeyValue( "start_active", "1" );
		m_hSpitEffect->KeyValue( "effect_name", "antlion_spit_trail" );
		m_hSpitEffect->SetParent( this );
		m_hSpitEffect->SetLocalOrigin( vec3_origin );
		DispatchSpawn( m_hSpitEffect );
		if ( gpGlobals->curtime > 0.5f )
			m_hSpitEffect->Activate();
	}
}

void CAdvisorGrenadeSpit::SpitDoesPoisonDamage( bool bEnable )
{
	m_bSpitDoesPoisonDamage = bEnable;
}

void CAdvisorGrenadeSpit::SetSpitSize( int nSize )
{
	switch (nSize)
	{
	case SPIT_LARGE:
		{
			m_bPlaySound = true;
			SetModel( "models/spitball_large.mdl" );
			break;
		}
	case SPIT_MEDIUM:
		{
			m_bPlaySound = true;
			m_flDamage *= 0.5f;
			SetModel( "models/spitball_medium.mdl" );
			break;
		}
	case SPIT_SMALL:
		{
			m_bPlaySound = false;
			m_flDamage *= 0.25f;
			SetModel( "models/spitball_small.mdl" );
			break;
		}
	}
}

void CAdvisorGrenadeSpit::Event_Killed( const CTakeDamageInfo &info )
{
	Detonate( );
}

//-----------------------------------------------------------------------------
// Purpose: Handle spitting
//-----------------------------------------------------------------------------
void CAdvisorGrenadeSpit::GrenadeSpitTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet(FSOLID_VOLUME_CONTENTS | FSOLID_TRIGGER) )
	{
		// Some NPCs are triggers that can take damage (like antlion grubs). We should hit them.
		if ( ( pOther->m_takedamage == DAMAGE_NO ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) )
			return;
	}

	// Don't hit other spit
	if ( pOther->GetCollisionGroup() == HL2COLLISION_GROUP_SPIT )
		return;

	// We want to collide with water
	const trace_t *pTrace = &CBaseEntity::GetTouchTrace();

	// copy out some important things about this trace, because the first TakeDamage
	// call below may cause another trace that overwrites the one global pTrace points
	// at.
	bool bHitWater = ( ( pTrace->contents & CONTENTS_WATER ) != 0 );
	CBaseEntity *const pTraceEnt = pTrace->m_pEnt;
	const Vector tracePlaneNormal = pTrace->plane.normal;

	if ( bHitWater )
	{
		// Splash!
		CEffectData data;
		data.m_fFlags = 0;
		data.m_vOrigin = pTrace->endpos;
		data.m_vNormal = Vector( 0, 0, 1 );
		data.m_flScale = 8.0f;

		DispatchEffect( "watersplash", data );
	}
	else
	{
		// Make a splat decal
		trace_t *pNewTrace = const_cast<trace_t*>( pTrace );
		UTIL_DecalTrace( pNewTrace, "BeerSplash" );
	}

	// Part normal damage, part poison damage
	float poisonratio = m_bSpitDoesPoisonDamage ? sk_advisor_spit_grenade_poison_ratio.GetFloat() : 0.0f;

	// Take direct damage if hit
	// NOTE: assume that pTrace is invalidated from this line forward!
	if ( pTraceEnt )
	{
		pTraceEnt->TakeDamage( CTakeDamageInfo( this, GetThrower(), m_flDamage * (1.0f-poisonratio), DMG_ACID ) );
		if ( poisonratio != 0.0f )
		{
			pTraceEnt->TakeDamage( CTakeDamageInfo( this, GetThrower(), m_flDamage * poisonratio, DMG_POISON ) );
		}
	}

	CSoundEnt::InsertSound( SOUND_DANGER, GetAbsOrigin(), m_DmgRadius * 2.0f, 0.5f, GetThrower() );

	QAngle vecAngles;
	VectorAngles( tracePlaneNormal, vecAngles );

	if ( pOther->IsPlayer() || bHitWater )
	{
		// Do a lighter-weight effect if we just hit a player
		DispatchParticleEffect( "antlion_spit_player", GetAbsOrigin(), vecAngles );
	}
	else
	{
		DispatchParticleEffect( "antlion_spit", GetAbsOrigin(), vecAngles );
	}

	Detonate();
}

void CAdvisorGrenadeSpit::Detonate(void)
{
	m_takedamage = DAMAGE_NO;

	EmitSound( "GrenadeSpit.Hit" );	

	// Stop our hissing sound
	if ( m_pHissSound != NULL )
	{
		CSoundEnvelopeController::GetController().SoundDestroy( m_pHissSound );
		m_pHissSound = NULL;
	}

	if ( m_hSpitEffect )
	{
		UTIL_Remove( m_hSpitEffect );
	}

	UTIL_Remove( this );
}

void CAdvisorGrenadeSpit::InitHissSound( void )
{
	if ( m_bPlaySound == false )
		return;

	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	if ( m_pHissSound == NULL )
	{
		CPASAttenuationFilter filter( this );
		m_pHissSound = controller.SoundCreate( filter, entindex(), "NPC_Antlion.PoisonBall" );
		controller.Play( m_pHissSound, 1.0f, 100 );
	}
}

void CAdvisorGrenadeSpit::Think( void )
{
	InitHissSound();
	if ( m_pHissSound == NULL )
		return;

	// Add a doppler effect to the balls as they travel
	CBaseEntity *pPlayer = AI_GetSinglePlayer();
	if ( pPlayer != NULL )
	{
		Vector dir;
		VectorSubtract( pPlayer->GetAbsOrigin(), GetAbsOrigin(), dir );
		VectorNormalize(dir);

		float velReceiver = DotProduct( pPlayer->GetAbsVelocity(), dir );
		float velTransmitter = -DotProduct( GetAbsVelocity(), dir );

		// speed of sound == 13049in/s
		int iPitch = 100 * ((1 - velReceiver / 13049) / (1 + velTransmitter / 13049));

		// clamp pitch shifts
		if ( iPitch > 250 )
		{
			iPitch = 250;
		}
		if ( iPitch < 50 )
		{
			iPitch = 50;
		}

		// Set the pitch we've calculated
		CSoundEnvelopeController::GetController().SoundChangePitch( m_pHissSound, iPitch, 0.1f );
	}

	// Set us up to think again shortly
	SetNextThink( gpGlobals->curtime + 0.05f );
}

void CAdvisorGrenadeSpit::Precache( void )
{
	// m_nSquidSpitSprite = PrecacheModel("sprites/greenglow1.vmt");// client side spittle.

	PrecacheModel( "models/spitball_large.mdl" ); 
	PrecacheModel("models/spitball_medium.mdl"); 
	PrecacheModel("models/spitball_small.mdl"); 

	PrecacheScriptSound( "GrenadeSpit.Hit" );
	PrecacheScriptSound( "NPC_Antlion.PoisonBall" );

	PrecacheParticleSystem( "antlion_spit_player" );
	PrecacheParticleSystem( "antlion_spit" );

	PrecacheEffect( "watersplash" );
}

bool CAdvisorGrenadeSpit::ComputeSpitParameters( CBaseEntity *pAI, Vector const &org, Vector const &tgt, Vector &params )
{
	const float flTolerance = 120.f;
	float flGravity = sk_advisor_spit_grenade_gravity.GetFloat();
	float flDesiredVelocity = sk_advisor_spit_grenade_velocity.GetFloat();
	
	// Check throw
	params = VecCheckThrowTolerance( pAI, org, tgt, flDesiredVelocity, flTolerance, flGravity );
	if ( vec3_origin != params )
		return true;
	
	// Try boosting our velocity thus flattening the arc
	flDesiredVelocity *= 2;
	params = VecCheckThrowTolerance( pAI, org, tgt, flDesiredVelocity, flTolerance, flGravity );
	if ( vec3_origin != params )
		return true;

	// No good LOS
	return false;
}


Vector VecCheckThrowTolerance( CBaseEntity *pEdict, const Vector &vecSpot1, Vector vecSpot2, float flSpeed, float flTolerance, float flGravity )
{
	flSpeed = MAX( 1.0f, flSpeed );

	Vector vecGrenadeVel = (vecSpot2 - vecSpot1);

	// throw at a constant time
	float time = vecGrenadeVel.Length( ) / flSpeed;
	vecGrenadeVel = vecGrenadeVel * (1.0 / time);

	// adjust upward toss to compensate for gravity loss
	vecGrenadeVel.z += flGravity * time * 0.5;

	Vector vecApex = vecSpot1 + (vecSpot2 - vecSpot1) * 0.5;
	vecApex.z += 0.5 * flGravity * (time * 0.5) * (time * 0.5);


	trace_t tr;
	UTIL_TraceLine( vecSpot1, vecApex, MASK_SOLID, pEdict, COLLISION_GROUP_NONE, &tr );
	if (tr.fraction != 1.0)
	{
 		// fail!
// 		if ( g_debug_antlion_worker.GetBool() )
// 		{
// 			NDebugOverlay::Line( vecSpot1, vecApex, 255, 0, 0, true, 5.0 );
// 		}

		return vec3_origin;
	}

// 	if ( g_debug_antlion_worker.GetBool() )
// 	{
// 		NDebugOverlay::Line( vecSpot1, vecApex, 0, 255, 0, true, 5.0 );
// 	}

	UTIL_TraceLine( vecApex, vecSpot2, MASK_SOLID_BRUSHONLY, pEdict, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction != 1.0 )
	{
		bool bFail = true;

		// Didn't make it all the way there, but check if we're within our tolerance range
		if ( flTolerance > 0.0f )
		{
			float flNearness = ( tr.endpos - vecSpot2 ).LengthSqr();
			if ( flNearness < Square( flTolerance ) )
			{
// 				if ( g_debug_antlion_worker.GetBool() )
// 				{
// 					NDebugOverlay::Sphere( tr.endpos, vec3_angle, flTolerance, 0, 255, 0, 0, true, 5.0 );
// 				}

				bFail = false;
			}
		}

		if ( bFail )
		{
// 			if ( g_debug_antlion_worker.GetBool() )
// 			{
// 				NDebugOverlay::Line( vecApex, vecSpot2, 255, 0, 0, true, 5.0 );
// 				NDebugOverlay::Sphere( tr.endpos, vec3_angle, flTolerance, 255, 0, 0, 0, true, 5.0 );
// 			}
			return vec3_origin;
		}
	}

// 	if ( g_debug_antlion_worker.GetBool() )
// 	{
// 		NDebugOverlay::Line( vecApex, vecSpot2, 0, 255, 0, true, 5.0 );
// 	}

	return vecGrenadeVel;
}
