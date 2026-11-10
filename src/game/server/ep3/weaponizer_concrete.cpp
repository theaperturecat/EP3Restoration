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
#include "weaponizer_concrete.h"
#include "dt_utlvector_send.h"
#include "weaponizer_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Find a right and up given a forward.
//-----------------------------------------------------------------------------
static void GetBasisFromDir( const Vector &vecForward, Vector &vecRight, Vector &vecUp )
{ 
	vecRight = CrossProduct( vecForward, Vector( 0, 0, 1 ) );
	if ( vecRight.LengthSqr() < 0.00001 )
	{
		vecRight = CrossProduct( vecForward , Vector( 0, -1, 0 ) );
	}

	vecUp = CrossProduct( vecRight, vecForward );
	vecRight = CrossProduct( vecForward , vecUp );
}


const char *WEAPONIZER_MODEL_FLAK = "models/weapons/w_flak_01.mdl";
const char *WEAPONIZER_MODEL_FLAMING_FLAK = "models/props_debris/concrete_spawnchunk001b.mdl";


ConVar flak_lifetime( "flak_lifetime", "1.2" );
ConVar flak_num_flak( "flak_num_flak", "10" );
ConVar flak_velocity( "flak_velocity", "2200.0" );
ConVar sk_plr_dmg_flak( "sk_plr_dmg_flak", "20.0" );
ConVar sk_plr_self_dmg_flak( "sk_plr_self_dmg_flak", "2.0" );


ConVar flak_grenade_num_flak( "flak_grenade_num_flak", "10" );
ConVar flak_grenade_launch_force( "flak_grenade_launch_force", "40000.0" );


class CFlak : public CBaseAnimating
{
public:

	void Spawn();
	void Precache();
	void FlakTouch( CBaseEntity *pOther );

	DECLARE_DATADESC();
	DECLARE_CLASS( CFlak, CBaseAnimating );
};

LINK_ENTITY_TO_CLASS( prop_flak, CFlak );

BEGIN_DATADESC( CFlak )
	DEFINE_ENTITYFUNC( FlakTouch ),
END_DATADESC()


ConVar flaming_flak_force( "flaming_flak_force", "4000.0" );

class CFlamingFlak : public CPhysicsProp
{
public:
	void Spawn();
	bool AllowedToIgnite() { return true; }
	void VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	DECLARE_CLASS( CFlamingFlak, CPhysicsProp );
};

LINK_ENTITY_TO_CLASS( prop_flaming_flak, CFlamingFlak );


class CFlamingFlakProjectile : public CBaseAnimating
{
public:

	void Spawn();
	void Precache();
	bool AllowedToIgnite() { return true; }
	void FlamingFlakProjectileTouch( CBaseEntity *pOther );

	DECLARE_DATADESC();
	DECLARE_CLASS( CFlamingFlakProjectile, CBaseAnimating );
};

LINK_ENTITY_TO_CLASS( prop_flaming_flak_projectile, CFlamingFlakProjectile );

BEGIN_DATADESC( CFlamingFlakProjectile )
	DEFINE_ENTITYFUNC( FlamingFlakProjectileTouch ),
END_DATADESC()


#define ZAP_FLAK_NUM_BEAMS 8

ConVar zap_flak_lifetime( "zap_flak_lifetime", "8.0" );
ConVar zap_flak_dmg_npc( "zap_flak_dmg_npc", "2.0" );
ConVar zap_flak_dmg_plr( "zap_flak_dmg_plr", "1.0" );

class CZapFlak : public CBaseAnimating
{
public:

	void Spawn();
	void Precache();
	bool AllowedToIgnite() { return true; }
	
	void ZapThink();
	void ZapFlakTouch( CBaseEntity *pOther );
	void ShootZapFlakBeam( int i, CBaseEntity *pOwner, const Vector &vecPos, const Vector &vecDir );

	DECLARE_SERVERCLASS(); 
	DECLARE_DATADESC();
	DECLARE_CLASS( CZapFlak, CBaseAnimating );

	CNetworkVar( bool, m_bActive );
	CNetworkArray( Vector, m_beamEndPoints, ZAP_FLAK_NUM_BEAMS );
	
	Vector m_vecBeamDir[ZAP_FLAK_NUM_BEAMS];
};

IMPLEMENT_SERVERCLASS_ST( CZapFlak, DT_ZapFlak )
	//SendPropArray3( SENDINFO_ARRAY3( m_beamEndPoints), SendPropVector( SENDINFO_ARRAY( m_beamEndPoints ), 10, SPROP_UNSIGNED ) ),
	SendPropVector( SENDINFO_NETWORKARRAYELEM( m_beamEndPoints, 0 ), -1, SPROP_COORD ),
	SendPropVector( SENDINFO_NETWORKARRAYELEM( m_beamEndPoints, 1 ), -1, SPROP_COORD ),
	SendPropVector( SENDINFO_NETWORKARRAYELEM( m_beamEndPoints, 2 ), -1, SPROP_COORD ),
	SendPropVector( SENDINFO_NETWORKARRAYELEM( m_beamEndPoints, 3 ), -1, SPROP_COORD ),
	SendPropVector( SENDINFO_NETWORKARRAYELEM( m_beamEndPoints, 4 ), -1, SPROP_COORD ),
	SendPropVector( SENDINFO_NETWORKARRAYELEM( m_beamEndPoints, 5 ), -1, SPROP_COORD ),
	SendPropVector( SENDINFO_NETWORKARRAYELEM( m_beamEndPoints, 6 ), -1, SPROP_COORD ),
	SendPropVector( SENDINFO_NETWORKARRAYELEM( m_beamEndPoints, 7 ), -1, SPROP_COORD ),
	SendPropBool( SENDINFO( m_bActive ) ),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( prop_zap_flak, CZapFlak );

BEGIN_DATADESC( CZapFlak )
	DEFINE_THINKFUNC( ZapThink ),
	DEFINE_ENTITYFUNC( ZapFlakTouch ),
	//	Class CZapFlak:
	DEFINE_FIELD( m_bActive, FIELD_BOOLEAN ),
	//	Class CZapFlak:
	DEFINE_ARRAY( m_beamEndPoints, FIELD_VECTOR, ZAP_FLAK_NUM_BEAMS ),
	//	Class CZapFlak:
	DEFINE_ARRAY( m_vecBeamDir, FIELD_VECTOR, ZAP_FLAK_NUM_BEAMS ),
END_DATADESC()


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlak_ShootSingleFlamingFlak( CBaseEntity *pOwner, const Vector &vecPos, const Vector &vecDir )
{
	CFlamingFlak *pFlak = static_cast<CFlamingFlak *>(CreateEntityByName( "prop_flaming_flak" ));
	if ( pFlak != NULL )
	{
		QAngle angDir;
		VectorAngles( vecDir, angDir );

		pFlak->SetAbsOrigin( vecPos );
		pFlak->SetAbsAngles( angDir );
		pFlak->SetOwnerEntity( pOwner );

		pFlak->Spawn();
		
		IPhysicsObject *pobj = pFlak->VPhysicsGetObject();
		pobj->ApplyForceCenter( vecDir * flaming_flak_force.GetFloat() );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlamingFlak::Spawn()
{
	SetModel( WEAPONIZER_MODEL_FLAK );
	BaseClass::Spawn();
	
	DispatchParticleEffect( "flak_trail", PATTACH_ABSORIGIN_FOLLOW, this );
	Ignite( 10.0, false, 32 );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlamingFlak::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	// Break in a few seconds
	g_EventQueue.AddEvent( this, "Break", 3.0f, this, this );

	CBaseEntity *pHitEntity = pEvent->pEntities[!index];

	CAI_BaseNPC *pNPC = pHitEntity->MyNPCPointer();
	if ( pNPC && pNPC->AllowedToIgnite() )
	{
		g_EventQueue.AddEvent( pNPC, "Ignite", 0.0f, this, this );
	}

	BaseClass::VPhysicsCollision( index, pEvent );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlak_ShootSingleFlak( CBaseEntity *pOwner, const Vector &vecPos, const Vector &vecDir )
{
	CFlak *pFlak = static_cast<CFlak *>(CreateEntityByName( "prop_flak" ));
	if ( pFlak != NULL )
	{
		QAngle angDir;
		VectorAngles( vecDir, angDir );
		pFlak->SetAbsAngles( angDir );
		pFlak->Spawn();
		
		// HACK: we want flak to collide with the player that shot it
		//pFlak->SetOwnerEntity( pOwner );

		pFlak->SetAbsOrigin( vecPos );
		pFlak->SetAbsVelocity( vecDir * flak_velocity.GetFloat() );

		pFlak->SetThink( &CFlak::SUB_Remove );
		pFlak->SetNextThink( gpGlobals->curtime + flak_lifetime.GetFloat() );
	}
}
/*

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlak_ShootFlak( CBaseEntity *pOwner )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	if ( !pPlayer )
		return;
		
	pOwner->EmitSound( "Weapon_Flak.Shoot" );

	Vector vecMuzzlePos = pPlayer->Weapon_ShootPosition();
	Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );
	Vector vecAimRight = CrossProduct( vecAiming , Vector( 0, 0, 1 ) );
	Vector vecAimUp = CrossProduct( vecAimRight, vecAiming );

	CShotManipulator shot( vecAiming );

	for ( int i = 0; i < flak_num_flak.GetInt(); i++ )
	{
		Vector vecPos = vecMuzzlePos + vecAiming * 36.0f;
		vecPos += vecAimRight * random->RandomFloat( -20, 20 );
		vecPos += vecAimUp * random->RandomFloat( -20, 20 );

		shot.ApplySpread( VECTOR_CONE_9DEGREES );
		CFlak_ShootSingleFlak( pPlayer, vecPos, shot.GetResult() );
	}
}*/

void SetMassAmmoModifier( CBaseEntity *pEntity, int iModifier );
void ShootFlechettes( Vector vOrigin, QAngle angles, CBaseEntity *pOwner );

void CFlak_ShootFlak( CBaseEntity *pOwner, int iModifier )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	if ( !pPlayer )
		return;

	if ( iModifier != WEAPONIZER_MODIFIER_FLECHETTE )
	{
		pPlayer->GetActiveWeapon()->EmitSound( "Weapon_Flak.Shoot" );

		CBaseAnimating *pSphere = static_cast<CBaseAnimating *>( CreateEntityByName( "prop_metal_sphere" ) );
		if ( pSphere != NULL )
		{
			Vector	vForward, vRight, vUp;
			pPlayer->EyeVectors( &vForward, &vRight, &vUp );


			Vector vOrigin = pPlayer->Weapon_ShootPosition() + vForward * 12.0f + vRight * 15 + vUp * -2.25f;
			Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );	

			// Set the model
			pSphere->SetModel( WEAPONIZER_MODEL_MATTER );
			pSphere->SetAbsOrigin( vOrigin + ( vecAiming * 30.0f ) );
			pSphere->SetAbsAngles(RandomAngle( -30, 30 ) );
			pSphere->Spawn();	
			pSphere->VPhysicsDestroyObject();
			pSphere->SetSolid( SOLID_BBOX );
			pSphere->SetMoveType( MOVETYPE_FLYGRAVITY );	
			pSphere->SetAbsVelocity( vecAiming * 2000 );
			pSphere->ApplyLocalAngularVelocityImpulse( RandomAngularImpulse( -60, 60 ) );
			UTIL_SetSize( pSphere, Vector( -8, -8, -8 ), Vector( 8, 8, 8 ) );

			SetMassAmmoModifier( pSphere, iModifier );
		}
	}
	else
	{
		pPlayer->GetActiveWeapon()->EmitSound( "NPC_Hunter.FlechetteShoot" );

		CBasePlayer *pPlayer = (CBasePlayer*)pOwner;
		QAngle angEye = pPlayer->EyeAngles();
		Vector	vForward, vRight, vUp;
		pPlayer->EyeVectors( &vForward, &vRight, &vUp );

		Vector vOrigin = pPlayer->Weapon_ShootPosition() + vForward * 48.0f + vRight * 8 + vUp * -8.25f;

		ShootFlechettes( vOrigin, angEye, pPlayer );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlak_ShootFlamingFlakProjectile( CBaseEntity *pOwner )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	if ( !pPlayer )
		return;

	CFlamingFlakProjectile *pFlak = static_cast<CFlamingFlakProjectile *>(CreateEntityByName( "prop_flaming_flak_projectile" ));
	if ( pFlak != NULL )
	{
		Vector vecMuzzlePos = pPlayer->Weapon_ShootPosition();
		Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );	
		
		pFlak->SetAbsOrigin( vecMuzzlePos + ( vecAiming * 36 ) );
		pFlak->SetAbsAngles( QAngle( 0, 0, 0 ) );

		pFlak->Spawn();

		pFlak->Ignite( 10.0, false );
		pFlak->SetAbsVelocity( vecAiming * flak_velocity.GetFloat() );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlak_ShootZapFlak( CBaseEntity *pOwner )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	if ( !pPlayer )
		return;

	CFlamingFlakProjectile *pFlak = static_cast<CFlamingFlakProjectile *>(CreateEntityByName( "prop_zap_flak" ));
	if ( pFlak != NULL )
	{
		Vector vecMuzzlePos = pPlayer->Weapon_ShootPosition();
		Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );	

		pFlak->SetAbsOrigin( vecMuzzlePos + ( vecAiming * 36 ) );
		pFlak->SetAbsAngles( QAngle( 0, 0, 0 ) );

		pFlak->Spawn();
		pFlak->SetAbsVelocity( vecAiming * flak_velocity.GetFloat() );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlak::Spawn()
{
	// Set the model
	SetModel( WEAPONIZER_MODEL_FLAK );

	BaseClass::Spawn();

	SetMoveType( MOVETYPE_FLY, MOVECOLLIDE_FLY_CUSTOM );
	UTIL_SetSize( this, -Vector(0.3f,0.3f,0.3f), Vector(0.3f,0.3f,0.3f) );
	SetSolid( SOLID_BBOX );
	SetTouch( &CFlak::FlakTouch );

	DispatchParticleEffect( "flak_trail", PATTACH_ABSORIGIN_FOLLOW, this );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlak::Precache()
{
	BaseClass::Precache();

	PrecacheModel( WEAPONIZER_MODEL_FLAK );
	PrecacheParticleSystem( "flak_trail" );
	
	PrecacheScriptSound( "Weapon_Flak.Shoot" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlak::FlakTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet( FSOLID_VOLUME_CONTENTS | FSOLID_TRIGGER ) )
	{
		// Some NPCs are triggers that can take damage (like antlion grubs). We should hit them.
		if ( ( pOther->m_takedamage == DAMAGE_NO ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) )
			return;
	}

	trace_t	tr, tr2;
	tr = BaseClass::GetTouchTrace();

	if ( pOther->m_takedamage != DAMAGE_NO )
	{
		ClearMultiDamage();

		Vector vecNormalizedVel = GetAbsVelocity();
		VectorNormalize( vecNormalizedVel );

		if ( !pOther->IsPlayer() )
		{
			CTakeDamageInfo	dmgInfo( this, this, sk_plr_dmg_flak.GetFloat(), DMG_NEVERGIB );
			dmgInfo.AdjustPlayerDamageInflictedForSkillLevel();
			CalculateMeleeDamageForce( &dmgInfo, vecNormalizedVel, tr.endpos, 0.7f );
			dmgInfo.SetDamagePosition( tr.endpos );
			
			if ( pOther->IsNPC() )
			{
				dmgInfo.ScaleDamageForce( 5 );
			}

			pOther->DispatchTraceAttack( dmgInfo, vecNormalizedVel, &tr );

			CBasePlayer *pPlayer = ToBasePlayer( GetOwnerEntity() );
			if ( pPlayer )
			{
				gamestats->Event_WeaponHit( pPlayer, true, "weapon_flak", dmgInfo );
			}

		}
		else
		{
			CTakeDamageInfo	dmgInfo( this, GetOwnerEntity(), sk_plr_self_dmg_flak.GetFloat(), DMG_BULLET | DMG_NEVERGIB );
			CalculateMeleeDamageForce( &dmgInfo, vecNormalizedVel, tr.endpos, 0.7f );
			dmgInfo.SetDamagePosition( tr.endpos );
			pOther->DispatchTraceAttack( dmgInfo, vecNormalizedVel, &tr );
		}

		ApplyMultiDamage();

		if ( pOther->GetCollisionGroup() == COLLISION_GROUP_BREAKABLE_GLASS )
			return;

		if ( !pOther->IsAlive() )
		{
			// We killed it! 
			const surfacedata_t *pdata = physprops->GetSurfaceData( tr.surface.surfaceProps );
			if ( pdata->game.material == CHAR_TEX_GLASS )
				return;
		}

		// play body "thwack" sound
		EmitSound( "Weapon_Crossbow.BoltHitBody" );

		Vector vForward;
		AngleVectors( GetAbsAngles(), &vForward );
		VectorNormalize ( vForward );

		UTIL_TraceLine( GetAbsOrigin(),	GetAbsOrigin() + vForward * 128, MASK_BLOCKLOS, pOther, COLLISION_GROUP_NONE, &tr2 );

		if ( tr2.fraction != 1.0f )
		{
			//NDebugOverlay::Box( tr2.endpos, Vector( -16, -16, -16 ), Vector( 16, 16, 16 ), 0, 255, 0, 0, 10 );
			//NDebugOverlay::Box( GetAbsOrigin(), Vector( -16, -16, -16 ), Vector( 16, 16, 16 ), 0, 0, 255, 0, 10 );

			if ( tr2.m_pEnt == NULL || ( tr2.m_pEnt && tr2.m_pEnt->GetMoveType() == MOVETYPE_NONE ) )
			{
				CEffectData	data;

				data.m_vOrigin = tr2.endpos;
				data.m_vNormal = vForward;
				data.m_nEntIndex = tr2.fraction != 1.0f;
			}
		}
	}
	else
	{
		trace_t	tr;
		tr = BaseClass::GetTouchTrace();

		// See if we struck the world
		if ( pOther->GetMoveType() == MOVETYPE_NONE && !( tr.surface.flags & SURF_SKY ) )
		{
			EmitSound( "Weapon_Crossbow.BoltHitWorld" );

			// Shoot some sparks
			if ( UTIL_PointContents( GetAbsOrigin(), CONTENTS_WATER ) != CONTENTS_WATER)
			{
				g_pEffects->Sparks( GetAbsOrigin() );
			}
		}
		else
		{
			// Put a mark unless we've hit the sky
			if ( ( tr.surface.flags & SURF_SKY ) == false )
			{
				UTIL_ImpactTrace( &tr, DMG_BULLET );
			}

			UTIL_Remove( this );
			return;
		}
	}

	//
	// Reflect
	//
	Vector vecDir = GetAbsVelocity();
	float speed = VectorNormalize( vecDir );

	float hitDot = DotProduct( tr.plane.normal, -vecDir );

	Vector vReflection = 2.0f * tr.plane.normal * hitDot + vecDir;

	QAngle reflectAngles;
	VectorAngles( vReflection, reflectAngles );
	SetLocalAngles( reflectAngles );
	SetAbsVelocity( vReflection * speed );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlamingFlakProjectile::Spawn()
{
	// Set the model
	SetModel( WEAPONIZER_MODEL_FLAMING_FLAK );

	BaseClass::Spawn();

	SetMoveType( MOVETYPE_FLY, MOVECOLLIDE_FLY_CUSTOM );
	UTIL_SetSize( this, -Vector(0.3f,0.3f,0.3f), Vector(0.3f,0.3f,0.3f) );
	SetSolid( SOLID_BBOX );
	SetTouch( &CFlamingFlakProjectile::FlamingFlakProjectileTouch );

	DispatchParticleEffect( "flak_trail", PATTACH_ABSORIGIN_FOLLOW, this );
	
	Ignite( 10.0f, false );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlamingFlakProjectile::Precache()
{
	BaseClass::Precache();

	PrecacheModel( WEAPONIZER_MODEL_FLAMING_FLAK );
	PrecacheParticleSystem( "flak_trail" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlamingFlakProjectile::FlamingFlakProjectileTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet( FSOLID_VOLUME_CONTENTS | FSOLID_TRIGGER ) )
	{
		// Some NPCs are triggers that can take damage (like antlion grubs). We should hit them.
		if ( ( pOther->m_takedamage == DAMAGE_NO ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) )
			return;
	}

	trace_t	tr;
	tr = BaseClass::GetTouchTrace();

	// See if we struck the world
	if ( !( tr.surface.flags & SURF_SKY ) )
	{
		CAI_BaseNPC *pNPC = pOther->MyNPCPointer();
		if ( pNPC && pNPC->AllowedToIgnite() )
		{
			g_EventQueue.AddEvent( pNPC, "Ignite", 0.0f, this, this );
		}
	
		// if what we hit is static architecture, can stay around for a while.
		Vector vecDir = GetAbsVelocity();
		float speed = VectorNormalize( vecDir );

		// See if we should reflect off this surface
		float hitDot = DotProduct( tr.plane.normal, -vecDir );

		if ( speed > 100 )
		{
			Vector vReflection = 2.0f * tr.plane.normal * hitDot + vecDir;

			QAngle reflectAngles;
			VectorAngles( vReflection, reflectAngles );

			Vector vecMuzzlePos = tr.endpos;
			Vector vecAiming = vReflection;
			Vector vecAimRight = CrossProduct( vecAiming , Vector( 0, 0, 1 ) );
			Vector vecAimUp = CrossProduct( vecAimRight, vecAiming );

			CShotManipulator shot( vecAiming );

			for ( int i = 0; i < flak_num_flak.GetInt(); i++ )
			{
				Vector vecPos = vecMuzzlePos + vecAiming * 36.0f;
				vecPos += vecAimRight * random->RandomFloat( -5, 5 );
				vecPos += vecAimUp * random->RandomFloat( -5, 5 );

				shot.ApplySpread( VECTOR_CONE_20DEGREES );
				
				//debugoverlay->AddLineOverlay( vecMuzzlePos, vecMuzzlePos + ( shot.GetResult() * 36 ), 255, 0, 0, true, 10.0f );
				
				CFlak_ShootSingleFlamingFlak( GetOwnerEntity(), vecPos, shot.GetResult() );
			}			
			
			UTIL_Remove( this );
		}

		// Shoot some sparks
		if ( UTIL_PointContents( GetAbsOrigin(), CONTENTS_WATER ) != CONTENTS_WATER)
		{
			EmitSound( "Weapon_Crossbow.BoltHitWorld" );
			UTIL_ImpactTrace( &tr, DMG_BULLET );
			g_pEffects->Sparks( GetAbsOrigin() );
		}
	}
	else
	{
		// we hit the sky
		UTIL_Remove( this );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CZapFlak::Spawn()
{
	// Set the model
	SetModel( WEAPONIZER_MODEL_FLAMING_FLAK );

	BaseClass::Spawn();

	SetMoveType( MOVETYPE_FLY, MOVECOLLIDE_FLY_CUSTOM );
	UTIL_SetSize( this, -Vector(0.3f,0.3f,0.3f), Vector(0.3f,0.3f,0.3f) );
	SetSolid( SOLID_BBOX );
	SetTouch( &CZapFlak::ZapFlakTouch );

	DispatchParticleEffect( "flak_trail", PATTACH_ABSORIGIN_FOLLOW, this );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CZapFlak::Precache()
{
	BaseClass::Precache();

	PrecacheModel( WEAPONIZER_MODEL_FLAMING_FLAK );
	PrecacheParticleSystem( "flak_trail" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CZapFlak::ZapThink()
{
	for ( int i = 0; i < ZAP_FLAK_NUM_BEAMS; i++ )
	{
		trace_t tr;
		UTIL_TraceLine( GetAbsOrigin(), GetAbsOrigin() + 4096.0f * m_vecBeamDir[i], MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );
		if ( ( tr.fraction != 1.0f ) && tr.m_pEnt )
		{
			m_beamEndPoints.Set( i, tr.endpos );
		
			float flDamage = 0;
		
			if ( tr.m_pEnt->IsNPC() )
			{
				flDamage = zap_flak_dmg_npc.GetFloat();
			
				// WEAPONIZER TODO: support shocking NPCs?
				#if 0
				CAI_BaseNPC *pNPC = static_cast<CAI_BaseNPC *>( tr.m_pEnt );
				if ( !pNPC->IsShocked() )
				{
					// stun them for a period of time			
					variant_t duration;
					duration.SetFloat( random->RandomFloat( 1, 3 ) );

					g_EventQueue.AddEvent( tr.m_pEnt, "Shock", duration, 0.0f, GetOwnerEntity(), this );
				}
				#endif
			}
			else
			{
				flDamage = zap_flak_dmg_plr.GetFloat();
			}

			CTakeDamageInfo info( this, NULL, flDamage, DMG_SHOCK | DMG_DIRECT );
			tr.m_pEnt->TakeDamage( info );
		}
	}

	SetNextThink( gpGlobals->curtime + 0.1f );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CZapFlak::ZapFlakTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet( FSOLID_VOLUME_CONTENTS | FSOLID_TRIGGER ) )
	{
		// Some NPCs are triggers that can take damage (like antlion grubs). We should hit them.
		if ( ( pOther->m_takedamage == DAMAGE_NO ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) )
			return;
	}

	trace_t	tr;
	tr = BaseClass::GetTouchTrace();

	// See if we struck the world
	if ( tr.surface.flags & SURF_SKY )
	{
		UTIL_Remove( this );
		return;
	}

	// if what we hit is static architecture, can stay around for a while.
	Vector vecDir = GetAbsVelocity();
	VectorNormalize( vecDir );

	// If we hit something damageable, break/kill it.
	if ( pOther->m_takedamage != DAMAGE_NO )
	{
		CTakeDamageInfo	info( this, GetOwnerEntity(), pOther->GetHealth(), DMG_SHOCK | DMG_NEVERGIB );
		CalculateMeleeDamageForce( &info, vecDir, tr.endpos, 0.7f );
		info.SetDamagePosition( tr.endpos );
		pOther->DispatchTraceAttack( info, vecDir, &tr );
		
		UTIL_Remove( this );
		return;
	}

	//
	// We hit static / world geometry. Stick and shoot out energy beams.
	//
	DispatchParticleEffect( "npc_stun_electricity", PATTACH_ABSORIGIN_FOLLOW, this );

	// Reflect off this surface
	float hitDot = DotProduct( tr.plane.normal, -vecDir );
	Vector vReflection = 2.0f * tr.plane.normal * hitDot + vecDir;
	//Vector vReflection = tr.plane.normal;

	QAngle reflectAngles;
	VectorAngles( vReflection, reflectAngles );

	Vector vecMuzzlePos = tr.endpos;
	Vector vecAiming = vReflection;
	Vector vecAimRight = CrossProduct( vecAiming , Vector( 0, 0, 1 ) );
	Vector vecAimUp = CrossProduct( vecAimRight, vecAiming );

	CShotManipulator shot( vecAiming );

	for ( int i = 0; i < flak_num_flak.GetInt(); i++ )
	{
		Vector vecPos = vecMuzzlePos;// + vecAiming * 36.0f;
		vecPos += vecAimRight * random->RandomFloat( -5, 5 );
		vecPos += vecAimUp * random->RandomFloat( -5, 5 );

		shot.ApplySpread( VECTOR_CONE_20DEGREES );

		//debugoverlay->AddLineOverlay( vecMuzzlePos, vecMuzzlePos + ( shot.GetResult() * 36 ), 255, 0, 0, true, 10.0f );

		ShootZapFlakBeam( i, GetOwnerEntity(), vecPos, shot.GetResult() );
	}			

	m_bActive = true;

	SetEFlags( EF_NODRAW );
	SetThink( &CZapFlak::ZapThink );
	SetNextThink( gpGlobals->curtime );

	SetContextThink( &CZapFlak::SUB_Remove, gpGlobals->curtime + zap_flak_lifetime.GetFloat(), "ZapFlakDieThink" );

	SetAbsVelocity( Vector( 0, 0, 0 ) );
	SetTouch( NULL );

	// Shoot some sparks
	if ( UTIL_PointContents( GetAbsOrigin(), CONTENTS_WATER ) != CONTENTS_WATER)
	{
		EmitSound( "Weapon_Crossbow.BoltHitWorld" );
		UTIL_ImpactTrace( &tr, DMG_BULLET );
		g_pEffects->Sparks( GetAbsOrigin() );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CZapFlak::ShootZapFlakBeam( int i, CBaseEntity *pOwner, const Vector &vecPos, const Vector &vecDir )
{
	trace_t tr;
	UTIL_TraceLine( vecPos, vecPos + vecDir * 4096, MASK_SOLID, this, COLLISION_GROUP_NONE, &tr );

	m_vecBeamDir[i] = vecDir;
	m_beamEndPoints.Set( i, tr.endpos );

	//debugoverlay->AddLineOverlay( vecPos, tr.endpos, 0, 100, 200, false, 10.0f );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
ConVar flak_grenade_velocity( "flak_grenade_velocity", "800.0" );
ConVar flak_grenade_delay( "flak_grenade_delay", "1.2" );

class CFlakGrenade : public CPhysicsProp
{
public:

	void Spawn();
	void Precache();
	void VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	void Explode();

	Vector m_vecNormal;

	DECLARE_DATADESC();
	DECLARE_CLASS( CFlakGrenade, CPhysicsProp );
};

BEGIN_DATADESC( CFlakGrenade )
	DEFINE_THINKFUNC( Explode ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( prop_flak_grenade, CFlakGrenade );


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlakGrenade::Spawn()
{
	SetModel( WEAPONIZER_MODEL_FLAK );
	BaseClass::Spawn();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlakGrenade::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Weapon_FlakGrenade.Stick" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlakGrenade::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	BaseClass::VPhysicsCollision( index, pEvent );

	pEvent->pInternalData->GetSurfaceNormal( m_vecNormal );
	m_vecNormal *= -1;

	EmitSound( "Weapon_FlakGrenade.Stick" );

	SetThink( &CFlakGrenade::Explode );
	SetNextThink( gpGlobals->curtime + flak_grenade_delay.GetFloat() );

	VPhysicsDestroyObject();
	AddSolidFlags( FSOLID_NOT_SOLID );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlakGrenade::Explode()
{
	g_EventQueue.AddEvent( this, "Break", 0.0f, this, this );

	Vector vecAiming = m_vecNormal;
	Vector vecAimUp;
	Vector vecAimRight;
	GetBasisFromDir( vecAiming, vecAimUp, vecAimRight );

	//debugoverlay->AddBoxOverlay( GetAbsOrigin(), Vector(-2,-2,-2), Vector(2,2,2), QAngle(0,0,0), 255, 0 , 0, 0, 10 );
	//debugoverlay->AddLineOverlay( GetAbsOrigin(), GetAbsOrigin() + vecAiming * 36, 255, 0 , 0, true, 10 );

	CShotManipulator shot( vecAiming );

	for ( int i = 0; i < flak_grenade_num_flak.GetInt(); i++ )
	{
		Vector vecPos = GetAbsOrigin() + vecAiming * 24.0f;
		vecPos += vecAimRight * random->RandomFloat( -5, 5 );
		vecPos += vecAimUp * random->RandomFloat( -5, 5 );

		shot.ApplySpread( Vector( 1, 1, 1 ) ); // 180 degree cone

		CFlak_ShootSingleFlak( GetOwnerEntity(), vecPos, shot.GetResult() );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CFlak_ShootFlakGrenade( CBaseEntity *pOwner )//, const Vector &vecPos, const Vector &vecDir )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	if ( !pPlayer )
		return;

	Vector vecPos = pPlayer->Weapon_ShootPosition();
	Vector vecDir = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );

	CFlakGrenade *pFlak = static_cast<CFlakGrenade *>(CreateEntityByName( "prop_flak_grenade" ));
	if ( pFlak != NULL )
	{
		QAngle angDir;
		VectorAngles( vecDir, angDir );

		pFlak->SetAbsOrigin( vecPos );
		pFlak->SetAbsAngles( angDir );

		pFlak->Spawn();

		IPhysicsObject *pobj = pFlak->VPhysicsGetObject();
		
		Vector velocity = vecDir * flak_grenade_velocity.GetFloat();
		pobj->AddVelocity( &velocity, NULL );
	}
}


ConVar zap_flak_beam_dist( "zap_flak_beam_dist", "1024.0" );
ConVar zap_flak_beam_dmg_npc( "zap_flak_beam_dmg_npc", "0.2" );
ConVar zap_flak_beam_stun_time( "zap_flak_beam_stun_time", "1.2" );

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
/*void CFlak_ShootZapFlakBeam1( CBaseEntity *pOwner, attackstate_t eState )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	if ( !pPlayer )
		return;

	Vector vecMuzzlePos = pPlayer->Weapon_ShootPosition();
	Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );	

	Vector vecPos = vecMuzzlePos;
	Vector vecDir = vecAiming;

	Msg( "======== SHOOT\n" );

	float flDist = zap_flak_beam_dist.GetFloat();
	while ( flDist > 0 )
	{
		trace_t tr;
		UTIL_TraceLine( vecPos, vecPos + vecDir * flDist, MASK_SHOT & ~CONTENTS_HITBOX, pOwner, COLLISION_GROUP_NONE, &tr );
		
		Vector vecDelta = tr.endpos - vecPos;
		Vector vecReflection = vecDir;
		
		if ( ( tr.fraction != 0.0f ) && ( tr.fraction != 1.0f ) )
		{
			if ( tr.m_pEnt != zapFlakBeam.m_hEntHit )
			{
				zapFlakBeam.m_hEntHit = tr.m_pEnt;
				zapFlakBeam.m_flStunTime = gpGlobals->curtime + zap_flak_beam_stun_time.GetFloat();
			}
			else if ( gpGlobals->curtime > zapFlakBeam.m_flStunTime )
			{
				if ( tr.m_pEnt->IsNPC() )
				{
					CAI_BaseNPC *pNPC = static_cast<CAI_BaseNPC *>( tr.m_pEnt ); 
					if ( !pNPC->IsShocked() )
					{
						variant_t duration;
						duration.SetFloat( 2 );
						g_EventQueue.AddEvent( tr.m_pEnt, "Shock", duration, 0.0f, pOwner, pOwner );
					}
				}
			}
		
			Msg( "BOUNCE\n" );
			
			Vector vecCenter = tr.m_pEnt->CollisionProp()->WorldSpaceCenter();
			vecCenter.z = tr.endpos.z;
			
			Vector vecNormal = tr.endpos - vecCenter;
			VectorNormalize( vecNormal );
		
			// Reflect
			float hitDot = DotProduct( vecNormal, -vecDir );
			vecReflection = 2.0f * vecNormal * hitDot + vecDir;
			VectorNormalize( vecReflection );

			flDist -= vecDelta.Length();
			if ( flDist < 0 )
			{
				flDist = 0;
			}
		}
		else
		{
			Msg( "STOP\n" );
			flDist = 0;
		}

		debugoverlay->AddBoxOverlay( tr.endpos, Vector(-4,-4,-4), Vector(4,4,4), QAngle(0,0,0), 255, 255, 255, 255, 0.05f );
		debugoverlay->AddLineOverlay( tr.endpos, tr.endpos + ( vecReflection * 36.0f ), 255, 255, 255, false, 0.05f );
		debugoverlay->AddLineOverlay( vecPos, tr.endpos, 0, 180, 220, false, 0.05f );

		vecDir = vecReflection;
		vecPos = tr.endpos + ( vecDir * 0.1f );
	}	
}*/


ConVar zap_flak_beam_radius( "zap_flak_beam_radius", "120" );
ConVar zap_flak_beam_dmg( "zap_flak_beam_dmg", "100" );
ConVar zap_flak_beam_shock( "zap_flak_beam_shock", "0" );


class CZapFlakBeam : public CBaseEntity
{
public:

	void RunAttack( CBasePlayer *pPlayer );
	void EndAttack( CBasePlayer *pPlayer );

	void CheckBranch();

	DECLARE_CLASS( CZapFlakBeam, CBaseEntity );
	DECLARE_SERVERCLASS(); 

	EHANDLE m_hEntHit;
	float m_flStunTime;

	CUtlVector<EHANDLE> m_hEnts;
	CUtlVector<float> m_flStunTimes;
	CUtlVector<int> m_nFromIndex;		// index of the entity in m_hEnts that this was chained from
};

LINK_ENTITY_TO_CLASS( zap_flak_beam, CZapFlakBeam );

IMPLEMENT_SERVERCLASS_ST( CZapFlakBeam, DT_ZapFlakBeam )
	SendPropUtlVector(
				SENDINFO_UTLVECTOR( m_hEnts ),
				ZAP_FLAK_MAX_ENTS, // max elements
				SendPropEHandle( NULL, 0 ) ),
	SendPropUtlVector(
				SENDINFO_UTLVECTOR( m_nFromIndex ),
				ZAP_FLAK_MAX_ENTS, // max elements
				SendPropInt( NULL, 0, sizeof(int) ) ),
END_SEND_TABLE()

CHandle<CZapFlakBeam> s_hZapFlakBeam;


//-----------------------------------------------------------------------------
// Called when the player releases the attack buttons. Damages all the NPCs in the chain.
//-----------------------------------------------------------------------------
void CZapFlakBeam::EndAttack( CBasePlayer *pPlayer )
{
	for ( int i = 0; i < m_hEnts.Count(); i++ )
	{
		CBaseEntity *pEntFrom = m_hEnts.Element( m_nFromIndex.Element( i ) );
		CAI_BaseNPC *pNPC = static_cast<CAI_BaseNPC *>( m_hEnts.Element( i ).Get() );
		if ( !pNPC )
			continue;

		CTakeDamageInfo	info( GetOwnerEntity(), GetOwnerEntity(), zap_flak_beam_dmg.GetFloat(), DMG_SHOCK | DMG_NEVERGIB );

		Vector vecDir( 0, 0, 0 );
		if ( pEntFrom )
		{
			vecDir = pNPC->CollisionProp()->WorldSpaceCenter() - pEntFrom->CollisionProp()->WorldSpaceCenter();
		}
		else if ( GetOwnerEntity() )
		{
			vecDir = pNPC->CollisionProp()->WorldSpaceCenter() - GetOwnerEntity()->CollisionProp()->WorldSpaceCenter();
		}
		VectorNormalize( vecDir );

		CalculateMeleeDamageForce( &info, vecDir, pNPC->CollisionProp()->WorldSpaceCenter(), 0.7f );

		info.SetDamagePosition( pNPC->CollisionProp()->WorldSpaceCenter() );
		pNPC->TakeDamage( info );
	}
}


//-----------------------------------------------------------------------------
// Searches for NPCs to chain to.
//-----------------------------------------------------------------------------
void CZapFlakBeam::CheckBranch()
{
	for ( int i = 0; i < m_hEnts.Count(); i++ )
	{
		if ( m_flStunTimes.Element( i ) > gpGlobals->curtime )
			continue;

		if ( zap_flak_beam_shock.GetBool() )
		{
			// WEAPONIZER TODO: support shocking NPCs?
			#if 0
			CAI_BaseNPC *pNPC = static_cast<CAI_BaseNPC *>( m_hEnts.Element( i ).Get() );
			if ( !pNPC->IsShocked() || ( pNPC->m_flShockEndTime < gpGlobals->curtime + 0.2 ) )
			{
				variant_t duration;
				duration.SetFloat( 2 );
				g_EventQueue.AddEvent( pNPC, "Shock", duration, 0.0f, GetOwnerEntity(), GetOwnerEntity() );
			}
			#endif
		}

		Vector vecRootCenter = m_hEnts.Element( i )->CollisionProp()->WorldSpaceCenter();

		CBaseEntity *ents[100];
		int nFound = UTIL_EntitiesInSphere( ents, 100, vecRootCenter, zap_flak_beam_radius.GetFloat(), FL_NPC );

		for ( int j = 0; j < nFound; j++ )
		{
			if ( m_hEnts.Find( ents[j] ) == -1 )
			{
				m_hEnts.AddToTail( ents[j] );
				m_flStunTimes.AddToTail( gpGlobals->curtime + 2 );
				m_nFromIndex.AddToTail( i );
			}
		}
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CZapFlakBeam::RunAttack( CBasePlayer *pPlayer )
{
	Vector vecMuzzlePos = pPlayer->Weapon_ShootPosition();
	Vector vecAiming = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );	

	Vector vecPos = vecMuzzlePos;
	Vector vecDir = vecAiming;

	trace_t tr;
	UTIL_TraceLine( vecPos, vecPos + vecDir * zap_flak_beam_dist.GetFloat(), MASK_SHOT & ~CONTENTS_HITBOX, pPlayer, COLLISION_GROUP_NONE, &tr );

	if ( ( tr.fraction != 0.0f ) && ( tr.fraction != 1.0f ) )
	{
		if ( tr.m_pEnt != m_hEntHit )
		{
			EndAttack( pPlayer );

			m_hEnts.RemoveAll();
			m_nFromIndex.RemoveAll();
			m_flStunTimes.RemoveAll();

			m_hEntHit = tr.m_pEnt;
			m_flStunTime = gpGlobals->curtime + zap_flak_beam_stun_time.GetFloat();
		}
		else if ( tr.m_pEnt && ( gpGlobals->curtime > m_flStunTime ) )
		{
			if ( tr.m_pEnt->IsNPC() )
			{
				if ( m_hEnts.Find( tr.m_pEnt ) == -1 )
				{
					m_hEnts.RemoveAll();
					m_nFromIndex.RemoveAll();
					m_flStunTimes.RemoveAll();

					m_hEnts.AddToTail( tr.m_pEnt );
					m_nFromIndex.AddToTail( -1 );
					m_flStunTimes.AddToTail( m_flStunTime );
				}

				CheckBranch();
			}
		}
	}	

	// Render the chain
	debugoverlay->AddLineOverlay( vecMuzzlePos, tr.endpos, 0, 180, 220, false, 0.1f );
	debugoverlay->AddBoxOverlay( tr.endpos, Vector(-4,-4,-4), Vector(4,4,4), QAngle(0,0,0), 255, 255, 255, 255, 0.05f );

	for ( int i = 1; i < m_hEnts.Count(); i++ )
	{
		CBaseEntity *pEntFrom = m_hEnts.Element( m_nFromIndex.Element( i ) );
		CBaseEntity *pEntTo = m_hEnts.Element( i );

		if ( !pEntFrom || !pEntTo )
			continue;

		debugoverlay->AddLineOverlay( pEntFrom->CollisionProp()->WorldSpaceCenter(), pEntTo->CollisionProp()->WorldSpaceCenter(), 0, 180, 220, false, 0.1f );
	}
}


//-----------------------------------------------------------------------------
// Maintains the continuous beam of chain lightning.
//-----------------------------------------------------------------------------
void CFlak_ShootZapFlakBeam( CBaseEntity *pOwner, attackstate_t eState )
{
	CBasePlayer *pPlayer = ToBasePlayer( pOwner );
	if ( !pPlayer )
		return;
		
	CZapFlakBeam *pBeams = s_hZapFlakBeam.Get();
	if ( !pBeams )
	{
		pBeams = (CZapFlakBeam *)CreateEntityByName( "zap_flak_Beam" );
		pBeams->SetAbsOrigin( pOwner->GetAbsOrigin() );
		pBeams->Spawn();
		pBeams->SetOwnerEntity( pOwner );
		pBeams->NetworkStateChanged();
		
		s_hZapFlakBeam.Set( pBeams );
	}
		
	if ( eState == ATTACK_POST )
	{
		pBeams->EndAttack( pPlayer );
		return;
	}
	
	pBeams->RunAttack( pPlayer );
}
