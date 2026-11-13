//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ====
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
#include "weaponizer_shared.h"
	
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


class CMetalSphere : public CBaseAnimating
{
public:

	DECLARE_DATADESC();

	virtual void Touch( CBaseEntity *pOther );
	//virtual void VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );
	virtual void Precache( void );
	virtual void Spawn( void );


	DECLARE_CLASS( CMetalSphere, CBaseAnimating );

	int m_iModifier;
};

BEGIN_DATADESC( CMetalSphere )
	DEFINE_FIELD( m_iModifier, FIELD_INTEGER ),
	DEFINE_ENTITYFUNC( Touch ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( prop_metal_sphere, CMetalSphere );

void SetMassAmmoModifier( CBaseEntity *pEntity, int iModifier )
{
	CMetalSphere *pSphere = static_cast<CMetalSphere *>( pEntity );

	if ( pSphere )
	{
		pSphere->m_iModifier = iModifier;

		if ( pSphere->m_iModifier == WEAPONIZER_MODIFIER_EXPLOSIVE )
		{
			int iAttach = pSphere->LookupAttachment( "particle_attach" );
			DispatchParticleEffect( "matter_explosive", PATTACH_POINT_FOLLOW, pSphere, iAttach );

			pSphere->SetRenderColor( 255, 0, 0 );
		}
	}
}

void CMetalSphere::Precache( void )
{
	BaseClass::Precache();

	PrecacheModel( WEAPONIZER_MODEL_SPHERE );
	
	PrecacheParticleSystem( "matter_explosive" );
}

ConVar weaponizer_modifier_explosion_damage( "weaponizer_modifier_explosion_damage", "32" );
ConVar weaponizer_matter_damage( "weaponizer_matter_damage", "32" );

void CMetalSphere::Touch( CBaseEntity *pOther )
{
	if ( m_iModifier == WEAPONIZER_MODIFIER_EXPLOSIVE )
	{
		ExplosionCreate( GetAbsOrigin(), GetAbsAngles(), this, weaponizer_modifier_explosion_damage.GetInt(), 256, true );
	}

	AddEffects( EF_NODRAW );

	CTakeDamageInfo info( this, GetOwnerEntity(), weaponizer_matter_damage.GetInt(), DMG_DIRECT );

	Vector vDir = GetAbsVelocity();
	VectorNormalize ( vDir );

	CalculateMeleeDamageForce( &info, vDir, pOther->WorldSpaceCenter() );

	pOther->TakeDamage( info );

	const trace_t *pTrace = &CBaseEntity::GetTouchTrace();
	
	for ( int i = 1; i < 4; i++ )
	{
		CPhysicsProp *pSphere = static_cast<CPhysicsProp *>(CreateEntityByName( "prop_physics" ));

		if ( pSphere != NULL )
		{
			char szName[64];

			Q_snprintf( szName, 64, "models/weapons/w_ball_small_chunk_0%d.mdl", i );
			// Set the model
			pSphere->SetModel( szName );
			pSphere->SetAbsOrigin( pTrace->endpos + ( pTrace->plane.normal * 4.0f ) );
			pSphere->SetAbsAngles( QAngle( 0, 0, 0 ) );
			pSphere->Spawn();
			pSphere->SetCollisionGroup( COLLISION_GROUP_DEBRIS );

			Vector vDirection = GetAbsVelocity();

		
			// Throw it
			IPhysicsObject *pObj = pSphere->VPhysicsGetObject();
			
			if ( pObj )
			{
				AngularImpulse impulse = RandomAngularImpulse( -60, 60 );

				pObj->SetVelocity( &vDirection, &impulse );		
			}
		}
	}

	UTIL_RemoveImmediate(this);
}

void CMetalSphere::Spawn( void )
{
	BaseClass::Spawn();

	SetTouch( &CMetalSphere::Touch );
}

void CMetal_ShootSphere( CBaseEntity *pOwner )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );	
	if ( !pPlayer )
		return;

	CPhysicsProp *pSphere = static_cast<CPhysicsProp *>(CreateEntityByName( "prop_metal_sphere" ));
	if ( pSphere != NULL )
	{
		Vector	vForward, vRight, vUp;
		pPlayer->EyeVectors( &vForward, &vRight, &vUp );


		Vector vOrigin = pPlayer->Weapon_ShootPosition() + vForward * 12.0f + vRight * 3.25 + vUp * -2.25f;
		Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );	

		// Set the model
		pSphere->SetModel( WEAPONIZER_MODEL_SPHERE );
		pSphere->SetAbsOrigin( vOrigin + ( vecAiming * 30.0f ) );
		pSphere->SetAbsAngles( QAngle( 0, 0, 0 ) );
		pSphere->Spawn();


		// Throw it
		IPhysicsObject *pObj = pSphere->VPhysicsGetObject();
		pObj->ApplyForceCenter( vecAiming * 70000.0 );		
	}
}


class CMetalZapSphere : public CPhysicsProp
{
public:
	virtual void VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	DECLARE_CLASS( CMetalZapSphere, CPhysicsProp );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	void InputZapEnemies( inputdata_t &data );
};

LINK_ENTITY_TO_CLASS( prop_metal_zap_sphere, CMetalZapSphere );

IMPLEMENT_SERVERCLASS_ST(CMetalZapSphere, DT_MetalZapSphere)
//something
END_SEND_TABLE()

BEGIN_DATADESC( CMetalZapSphere )
	DEFINE_INPUTFUNC( FIELD_VOID, "ZapEnemies", InputZapEnemies ),
END_DATADESC()

extern ConVar weaponizer_electrical_shock_time;

ConVar weaponizer_stun_sphere_targets( "weaponizer_stun_sphere_targets", "10" );

void CMetalZapSphere::InputZapEnemies( inputdata_t &data )
{
	CBaseEntity *ppEnts[256];
	CBaseEntity *ppCandidates[256];
	Vector vecCenter = WorldSpaceCenter();
	float flRadius = 500.0f;
	int nEntCount = UTIL_EntitiesInSphere( ppEnts, 256, vecCenter, flRadius, 0 );

	int nCandidateCount = 0;

	for ( int i = 0; i < nEntCount; i++ )
	{
		CBaseEntity *pEntity = ppEnts[i];

		// FIXME: need to detect metal objects too
		if ( pEntity->IsNPC() /*|| GetWeaponizerMaterialType( pEntity ) == WEAPONIZER_MATERIAL_TYPE_METAL*/ )
		{
			if ( pEntity->FVisible( vecCenter ) )
			{
				ppCandidates[ nCandidateCount++ ] = ppEnts[i];
			}
		}
	}

	int iRandom = 0;
	int iSelected = 0;

	//FIXME: This will really only work if there are less than 32 valid targets.
	while ( iRandom < weaponizer_stun_sphere_targets.GetInt() )
	{
		int iRandomEnt = random->RandomInt( 0, nCandidateCount-1 );

		CBaseEntity *pEntity = ppCandidates[iRandomEnt];

		if ( pEntity && !(iSelected & iRandomEnt ) )
		{
			EntityMessageBegin( this, true );
			WRITE_BYTE( WEAPONIZER_FX_ELECTRICAL_ZAP );
			WRITE_VEC3COORD( pEntity->WorldSpaceCenter() );
			MessageEnd();

			CTakeDamageInfo info( this, GetOwnerEntity(), 1, DMG_SHOCK | DMG_DIRECT );

			Vector vDir = pEntity->WorldSpaceCenter() - vecCenter;
			VectorNormalize ( vDir );

			CalculateMeleeDamageForce( &info, vDir, pEntity->WorldSpaceCenter() );
			info.SetDamageForce( info.GetDamageForce() * random->RandomFloat( -50, 50 ) );

			pEntity->TakeDamage( info );
			iRandom++;
			iSelected |= ( 1 << iRandomEnt );
		}
	}

	g_EventQueue.AddEvent( this, "ZapEnemies", 3.0f, this, this );
}

void CMetalZapSphere::VPhysicsCollision(int index, gamevcollisionevent_t *pEvent )
{
	CBaseEntity *pHitEntity = pEvent->pEntities[!index];

	if ( pHitEntity && pHitEntity->IsWorld() )
	{
		IPhysicsObject *pObj = VPhysicsGetObject();
		
		if ( pObj )
		{
			pObj->EnableMotion( false );
		}

		g_EventQueue.AddEvent( this, "Break", 12.5f, this, this );
		g_EventQueue.AddEvent( this, "ZapEnemies", 0.5f, this, this );
	}

	BaseClass::VPhysicsCollision( index, pEvent );
}


void CMetal_ShootZapSphere( CBaseEntity *pOwner )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );

	if ( !pPlayer )
		return;

	CPhysicsProp *pSphere = static_cast<CPhysicsProp *>(CreateEntityByName( "prop_metal_zap_sphere" ));
	if ( pSphere != NULL )
	{
		Vector vecMuzzlePos = pPlayer->Weapon_ShootPosition();
		Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );	

		// Set the model
		pSphere->SetModel( WEAPONIZER_MODEL_SPHERE );
		pSphere->SetAbsOrigin( vecMuzzlePos + ( vecAiming * 30.0f ) );
		pSphere->SetAbsAngles( QAngle( 0, 0, 0 ) );
		pSphere->Spawn();
		pSphere->SetOwnerEntity( pPlayer );

		// Throw it
		IPhysicsObject *pObj = pSphere->VPhysicsGetObject();
		pObj->ApplyForceCenter( vecAiming * 70000.0 );		
	}
}


class CMetalFlameSphere : public CPhysicsProp
{
public:
	virtual void VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	DECLARE_CLASS( CMetalFlameSphere, CPhysicsProp );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	void InputBurnEnemies( inputdata_t &data );

	CNetworkVar( bool, m_bActive );
};

LINK_ENTITY_TO_CLASS( prop_metal_flame_sphere, CMetalFlameSphere );

IMPLEMENT_SERVERCLASS_ST( CMetalFlameSphere, DT_MetalFlameSphere )
	SendPropBool( SENDINFO( m_bActive ) ),
END_SEND_TABLE()

BEGIN_DATADESC( CMetalFlameSphere )
	DEFINE_FIELD( m_bActive, FIELD_BOOLEAN ),
	DEFINE_INPUTFUNC( FIELD_VOID, "BurnEnemies", InputBurnEnemies ),
END_DATADESC()


void CMetalFlameSphere::InputBurnEnemies( inputdata_t &data )
{
	CBaseEntity *list[32];
	float flMaxDist = 128 + 1.0;
	float flNearestDist = flMaxDist;
	Vector mins = GetAbsOrigin() - Vector( flNearestDist, flNearestDist, flNearestDist );
	Vector maxs = GetAbsOrigin() + Vector( flNearestDist, flNearestDist, flNearestDist );

	int count = UTIL_EntitiesInBox( list, 32, mins, maxs, 0 );

	//debugoverlay->AddBoxOverlay( GetAbsOrigin(), mins, maxs, GetAbsAngles(), 255, 255, 0, 0, 0.25 );

	for( int i = 0 ; i < count ; i++ )
	{
		CAI_BaseNPC *pNPC = list[ i ]->MyNPCPointer();
		if ( !pNPC || !pNPC->AllowedToIgnite() )
			continue;

		g_EventQueue.AddEvent( list[ i ], "Ignite", 0.0, this, this );
	}


	CSoundEnt::InsertSound ( SOUND_THUMPER, GetAbsOrigin(), 512, 0.45f, this );

	g_EventQueue.AddEvent( this, "BurnEnemies", 0.5, this, this );
}

void CMetalFlameSphere::VPhysicsCollision(int index, gamevcollisionevent_t *pEvent )
{
	CBaseEntity *pHitEntity = pEvent->pEntities[!index];

	if ( pHitEntity && pHitEntity->IsWorld() )
	{
		IPhysicsObject *pObj = VPhysicsGetObject();

		if ( pObj )
		{
			VPhysicsDestroyObject();

			SetMoveType( MOVETYPE_NONE );

			Vector vNormal;
			pEvent->pInternalData->GetSurfaceNormal( vNormal );
		
			SetAbsOrigin( GetAbsOrigin() + vNormal * 10 );

			QAngle newangles;
			VectorAngles( -vNormal, newangles );
			SetAbsAngles( newangles );
			
			m_bActive = true;

			g_EventQueue.AddEvent( this, "BurnEnemies", 0, this, this );

			
		}
	}

	BaseClass::VPhysicsCollision( index, pEvent );
}

void CMetal_ShootFlamingSphere( CBaseEntity *pOwner )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	if ( !pPlayer )
		return;

	CPhysicsProp *pSphere = static_cast<CPhysicsProp *>(CreateEntityByName( "prop_metal_flame_sphere" ));
	if ( pSphere != NULL )
	{
		Vector vecMuzzlePos = pPlayer->Weapon_ShootPosition();
		Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );	

		// Set the model
		pSphere->SetModel( WEAPONIZER_MODEL_SPHERE );
		pSphere->SetAbsOrigin( vecMuzzlePos + ( vecAiming * 30.0f ) );
		pSphere->SetAbsAngles( QAngle( 0, 0, 0 ) );
		pSphere->Spawn();
		pSphere->Ignite( 15, false );

		g_EventQueue.AddEvent( pSphere, "Break", 15, pSphere, pSphere );

		// Throw it
		IPhysicsObject *pObj = pSphere->VPhysicsGetObject();
		pObj->ApplyForceCenter( vecAiming * 70000.0 );		
	}
}
