//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ====
//
// A weapon that sprays ice allowing the player to create ice walls and bridges.
// Secondary fire shoots cryodarts that violently shatter the ice!
//
// TODO: handle depleted ammo when the probe is detached
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
#include "effects.h"
#include "IEffects.h"
#include "interpolatortypes.h"
#include "ndebugoverlay.h"
#include "shake.h"
#include "shot_manipulator.h"
#include "hl2_player.h"
#include "beam_shared.h"
#include "sprite.h"
#include "util.h"
#include "physics_saverestore.h"
#include "ai_basenpc.h"
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
#include "eventqueue.h"
#include "model_types.h"
#include "ai_interactions.h"
#include "rumble_shared.h"
#include "gamestats.h"
#include "particle_system.h"
#include "particle_parse.h"
#include "triggers.h"
#include "vphysics/constraints.h"
#include "coordsize.h"
#include "collisionutils.h"
#include "explode.h"
#include "weapon_icegun.h"
#include "weapon_weaponizer.h"
#include "EntityFreezing.h"
#include "physics_prop_statue.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


class CWeaponIcegun;


#define ICE_SPHERE_RADIUS				8
#define ICE_SPHERE_LINK_DISTANCE_SQR	( ( ( ICE_SPHERE_RADIUS * 2.0f ) * ( ICE_SPHERE_RADIUS * 2.0f ) ) + 1.0f )
#define ICE_SPHERE_VOLUME				( 1.33 * 3.1416 * ICE_SPHERE_RADIUS * ICE_SPHERE_RADIUS * ICE_SPHERE_RADIUS )

#define ICEGUN_SCULPT_BEAM_SPRITE "sprites/physbeam.vmt"
#define ICEGUN_MODEL "models/weapons/v_models/v_weaponizer.mdl"

#define SCULPTURE_SURFACE_RADIUS 0.7f


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static ConVar icegun_debug( "icegun_debug", "0" );
static ConVar icegun_debug_flow( "icegun_debug_flow", "0" );

static ConVar icegun_max_build_dist( "icegun_max_build_dist", "512" );
static ConVar icegun_max_melt_dist( "icegun_max_melt_dist", "2048" );
static ConVar icegun_min_eye_dist( "icegun_min_eye_dist", "20" );

static ConVar icegun_auto_reload( "icegun_auto_reload", "1" );
static ConVar icegun_recycle_ice( "icegun_recycle_ice", "1" );
static ConVar icegun_regen_ice( "icegun_regen_ice", "0" );
static ConVar icegun_max_fixed_ice( "icegun_max_fixed_ice", "3000" ); // max number of ice that can be created
static ConVar icegun_max_regen_ice( "icegun_max_regen_ice", "60" );
static ConVar icegun_ice_regen_rate( "icegun_ice_regen_rate", "0.1" );
static ConVar icegun_ice_fire_rate( "icegun_ice_fire_rate", "0.02" ); // seconds per shot
static ConVar icegun_reload_melt_time( "icegun_reload_melt_time", "1.5" ); // maximum time it will take to do a full reload (melt all the ice)
static ConVar icegun_shatter_chain_reaction_speed( "icegun_shatter_chain_reaction_speed", "300.0" ); // the bigger the number the faster the chain
static ConVar icegun_simplified_collision_debug( "icegun_simplified_collision_debug", "0" );

static ConVar icegun_melt_sculptures( "icegun_melt_sculptures", "1" ); // 0=no, 1=melt instantly, 2=paint melting
static ConVar icegun_melt_delay( "icegun_melt_delay", "0.3" );

static ConVar icegun_cryodart_regen_rate( "icegun_cryodart_regen_rate", "0.3" );
static ConVar icegun_max_cryodart( "icegun_max_cryodart", "10" );
static ConVar icegun_cryodart_fire_rate( "icegun_cryodart_fire_rate", "0.1" ); // seconds per shot
static ConVar icegun_extrude_dist( "icegun_extrude_dist", "128" );

static ConVar icegun_ramming_speed( "icegun_ramming_speed", "300" );
static ConVar icegun_ramming_force( "icegun_ramming_force", "1000" );

static ConVar icegun_autoramp( "icegun_autoramp", "0" );
static ConVar icegun_extrude_spherical( "icegun_extrude_spherical", "1" );
static ConVar icegun_sphere_explode( "icegun_sphere_explode", "1" );
static ConVar icegun_sphere_damage( "icegun_sphere_damage", "20" );
static ConVar icegun_sphere_explode_radius( "icegun_sphere_explode_radius", "60" );
static ConVar icegun_sphere_health( "icegun_sphere_health", "15" );
static ConVar icegun_sphere_lifetime( "icegun_sphere_lifetime", "30" );
static ConVar icegun_sphere_freeze_amount( "icegun_sphere_freeze_amount", "0.05" );
static ConVar icegun_fixed_width( "icegun_fixed_width", "5" );

static ConVar icegun_mindistsq( "icegun_mindistsq", "50" );
static ConVar icegun_sphere_spacing( "icegun_sphere_spacing", "5" );

// TODO: bake these into constants in code!
static ConVar icegun_ramp_upslope( "icegun_ramp_upslope", "30" );		 // must be positive and < 90
static ConVar icegun_ramp_downslope( "icegun_ramp_downslope", "-40" );	 // must be negative and > -90

static ConVar ice_probe_gravity( "ice_probe_gravity", "0" );
static ConVar ice_probe_velocity( "ice_probe_velocity", "4000" );

static ConVar cryodart_fade_time( "cryodart_fade_time", "3" );
static ConVar sk_plr_dmg_cryodart( "sk_plr_dmg_cryodart", "6" );
static ConVar sk_npc_dmg_cryodart( "sk_npc_dmg_cryodart", "4" );

extern ConVar sv_special_suit_abilities;

static const char* ICEGUN_MODEL_SPHERE = "models/props_junk/watermelon01.mdl";//"models/weapons/w_ball_small.mdl"; theaperturecat models fixme

// FIXME: not multiplayer friendly
static CWeaponIcegun *s_pIcegun = NULL;

enum SculptIceState_t
{
	SCULPT_STATE_NONE = 0,
	SCULPT_STATE_WAIT_PROBE,		// Waiting for the ice probe to land
	SCULPT_STATE_EXTRUDE,			// Building a freestanding sculpture
	SCULPT_STATE_FREEZE_OBJECT,		// Freezing an object or NPC
	SCULPT_STATE_MELT_SCULPTURE,	// Melting/shattering an existing ice sculpture
};


BEGIN_SIMPLE_DATADESC( icesphere_t )
	DEFINE_FIELD( m_hSculpture, FIELD_EHANDLE ),
	//DEFINE_PHYSPTR( m_pPhysObject ),
	DEFINE_AUTO_ARRAY( m_iNeighborSpheres, FIELD_SHORT ),
	DEFINE_FIELD( m_bRootedToWorld, FIELD_BOOLEAN ),
	//DEFINE_FIELD( m_bPeeked, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecPos, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecExtrudeDir, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecNextDelta, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecPrevDelta, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecMid, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecNextMid, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_iHealth, FIELD_INTEGER ),
	DEFINE_FIELD( m_flDieTime, FIELD_FLOAT ),
END_DATADESC()


static int s_nIceSpheres = 0;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
icesphere_t::icesphere_t()
{
	s_nIceSpheres++;

	m_pPhysObject = NULL;

	for ( int i = 0; i < ICE_SPHERE_MAX_NEIGHBORS; ++i )
	{
		m_iNeighborSpheres[ i ] = -1;
	}

	m_bRootedToWorld = false;
	m_bPeeked = false;

	m_vecPos = Vector( 0.0f, 0.0f, 0.0f );
	m_vecExtrudeDir = Vector( 0.0f, 0.0f, 0.0f );
	m_vecPrevDelta = Vector( 0.0f, 0.0f, 0.0f );
	m_vecNextDelta = Vector( 0.0f, 0.0f, 0.0f );
	m_vecMid = Vector( 0.0f, 0.0f, 0.0f );
	m_vecNextMid = Vector( 0.0f, 0.0f, 0.0f );

	m_iHealth = icegun_sphere_health.GetInt();

	m_iOBB = -1;

#ifdef PAINT
	m_color = Color( 0, 0, 0 );
	m_vecSurfNormal = Vector( 0, 0, 1 );
#endif
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
icesphere_t::~icesphere_t()
{
	s_nIceSpheres--;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
BEGIN_DATADESC( CIceSculpture )
	DEFINE_FIELD( m_bOriginsRelativeToCenter, FIELD_BOOLEAN ),
	DEFINE_UTLVECTOR( m_vecBaseOrigins, FIELD_POSITION_VECTOR ),
	DEFINE_SHARED_FIELD( m_vecOrigins ),
	DEFINE_UTLVECTOR( m_IceSpheres, FIELD_EMBEDDED ),
	//DEFINE_UTLVECTOR( m_CollisionOBBs, FIELD_EMBEDDED ),
	DEFINE_FIELD( m_eLifeStatus, FIELD_INTEGER ),
	DEFINE_FIELD( m_vecNormal, FIELD_VECTOR ),
	DEFINE_FIELD( m_flNextShatterSoundTime, FIELD_TIME ),

	DEFINE_THINKFUNC( RemoveSphereThink ),
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CIceSculpture, DT_IceSculpture )
	SendPropBool( SENDINFO( m_bOriginsRelativeToCenter ) )
END_SEND_TABLE() 

IMPLEMENT_SHAREDCLASS_DT( CIceSculpture )
	SharedProp( m_vecOrigins )
END_SHARED_TABLE()

LINK_ENTITY_TO_CLASS( ice_sculpture, CIceSculpture );


CUtlVector<IPhysicsObject*>	CIceSculpture::m_PhysicsObjects;
int CIceSculpture::m_iPhysicsObjectsRef;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CIceSculpture *CIceSculpture::CreateIceSculpture( const Vector &vecPos, const Vector &vecNormal )
{
	CIceSculpture *pSculpture = static_cast<CIceSculpture *>( CreateEntityByName( "ice_sculpture" ) );
	pSculpture->SetAbsOrigin( vecPos );
	pSculpture->m_vecNormal = vecNormal;
	DispatchSpawn( pSculpture );
	
	return pSculpture;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::DumpSpheres()
{
	for ( int i = 0; i < m_IceSpheres.Count(); i++ )
	{
		Msg( "    %d: %x\n", i, &(m_IceSpheres[i]) );
	}
}


CIceSculpture *CIceSculpture::GetOldestIceSculpture( void )
{
	float fOldestTime = FLT_MAX;

	CIceSculpture *pOldestSculpture = NULL;

	CIceSculpture *pSculpture = NULL;
	while ( ( pSculpture = (CIceSculpture *)gEntList.FindEntityByClassname( pSculpture, "ice_sculpture" ) ) != NULL )
	{
		if ( pSculpture->m_flCreationTime < fOldestTime )
		{
			fOldestTime = pSculpture->m_flCreationTime;
			pOldestSculpture = pSculpture;
		}
	}

	return pOldestSculpture;
}

CIceSculpture::CIceSculpture()
{
	m_iCachedWorldBoundsUpdateTick = -1;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::Spawn()
{
	SetModel( ICEGUN_MODEL_SPHERE );
	Precache();

	SetSolid( SOLID_VPHYSICS );
	AddSolidFlags( FSOLID_CUSTOMBOXTEST | FSOLID_CUSTOMRAYTEST );

	// TODO: address shadow perf/quality issues
	AddEffects( EF_NOSHADOW | EF_NORECEIVESHADOW );

	m_takedamage = DAMAGE_EVENTS_ONLY;
	
	// So that AI code sees this as a breakable thing.
	m_iHealth = 5000;

	CreateVPhysics();

	m_flCreationTime = gpGlobals->curtime;

	SetThink( &CIceSculpture::RemoveSphereThink );
	SetNextThink( gpGlobals->curtime + icegun_sphere_lifetime.GetFloat() );

	BaseClass::Spawn();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::Precache()
{
	PrecacheModel( ICEGUN_MODEL_SPHERE );

	PrecacheParticleSystem( "ice_shatter" );
	PrecacheParticleSystem( "blood_impact_synth_01" );

	PrecacheScriptSound( "Weapon_Icegun.SculptureShatter" );
	PrecacheScriptSound( "Glass.BulletImpact" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CIceSculpture::CreateVPhysics( void )
{
	// No physics
	if ( GetSolid() == SOLID_NONE )
	{
		return false;
	}

	CollisionProp()->SetSurroundingBoundsType( USE_GAME_CODE );

	if ( m_iPhysicsObjectsRef == 0 )
	{
		// Create our global pool of physics objects!
		for ( int i = 0; i < icegun_max_fixed_ice.GetInt(); ++i )
		{
			IPhysicsObject *pPhysicsObject = PhysModelCreate( this, GetModelIndex(), GetAbsOrigin(), GetAbsAngles() );
			if ( pPhysicsObject )
			{
				pPhysicsObject->EnableCollisions( false );
				pPhysicsObject->EnableMotion( false );
				pPhysicsObject->SetGameData( NULL );
				m_PhysicsObjects.AddToTail( pPhysicsObject );
			}
		}
	}

	VPhysicsSetObject( m_PhysicsObjects[ 0 ] );
	SetMoveType( MOVETYPE_VPHYSICS );

	++m_iPhysicsObjectsRef;

	return true;
}


void CIceSculpture::OnRestore( void )
{
	BaseClass::OnRestore();

	for ( int i = 0; i < m_IceSpheres.Count(); ++i )
	{
		icesphere_t *pSphere = &(m_IceSpheres[ i ]);
		AddPhysicsObjectForSphere( pSphere );
	}

	RecenterAbsOrigin();
}

int CIceSculpture::VPhysicsGetObjectList( IPhysicsObject **pList, int listMax )
{
	int count = 0;
	for ( int i = 0; count < listMax && i < m_IceSpheres.Count(); i++ )
	{
#ifdef _DEBUG
		// In debug we want to break here to track down why this is happening
		Assert( m_IceSpheres[ i ].m_pPhysObject );
#else
		// In release mode we want to be extra safe and skip over null pointers to prevent a crash
		if ( m_IceSpheres[ i ].m_pPhysObject )
#endif
		{
			pList[count++] = m_IceSpheres[ i ].m_pPhysObject;
		}
	}

	return count;
}

void CIceSculpture::VPhysicsDestroyObject()
{
	if ( m_iPhysicsObjectsRef )
	{
		// Decrement our ref count
		--m_iPhysicsObjectsRef;

		if ( !m_iPhysicsObjectsRef )
		{
			// That was the last one, clean up our physics objects!
			for ( int i = m_PhysicsObjects.Count(); --i >=0;  )
			{
				physenv->DestroyObject( m_PhysicsObjects[i] );
			}
			m_PhysicsObjects.RemoveAll();
		}
	}

	VPhysicsSetObject( NULL );
}

void CIceSculpture::VPhysicsUpdate( IPhysicsObject *pPhysics )
{
	// Don't want to change our position here because it should be in the center of all the particles	
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CIceSculpture::OnTakeDamage( const CTakeDamageInfo &info )
{
	if ( info.GetInflictor() && FClassnameIs( info.GetInflictor(), "cryo_dart" ) )
	{
		// Cryodarts shatter us on contact
		Shatter( info.GetDamagePosition(), 0.5 );
	}
	else if ( info.GetInflictor() && ( info.GetDamageType() & DMG_BLAST ) && FClassnameIs( info.GetInflictor(), "flechette_projectile" ) )
	{
		// The explosion of the flechette projectile shatters us, but the initial impact does not
		Shatter( info.GetDamagePosition(), 0.5 );
	}
	else if ( info.GetInflictor() && FClassnameIs( info.GetInflictor(), "flechette_projectile" ) )
	{
		// Don't do anything
	}
	else
	{
		int iSpheresDestroyed = 0;
		float flHealthToRemove = info.GetDamage();

		float flRadius = info.GetRadius();
		if ( flRadius > 0.0f )
		{
			EmitSound( "Glass.BulletImpact" );

			// Loop through all the spheres for radius damage
			for ( int iSphere = m_IceSpheres.Count() - 1; iSphere >= 0; --iSphere )
			{
				icesphere_t *pIceSphere = &(m_IceSpheres[ iSphere ]);

				float flDistanceToSphere = pIceSphere->m_vecPos.DistTo( info.GetDamagePosition() );

				if ( flDistanceToSphere < flRadius )
				{
					float flFalloff = ( ( flRadius - flDistanceToSphere ) / flRadius );

					// Falloff is at a squared rate
					float flAdjustedDamage = flHealthToRemove * ( flFalloff * flFalloff );

					// Sphere was within the radius so kill it if the damage is enough
					if ( flAdjustedDamage >= pIceSphere->m_iHealth )
					{
						DispatchParticleEffect( "blood_impact_synth_01", pIceSphere->m_vecPos, QAngle( 0.0f, 0.0f, 0.0f ) );
						RemoveSphere( iSphere );
						++iSpheresDestroyed;
					}
				}
			}
		}
		else
		{
			icesphere_t *pIceSphere;
			int iSphere = GetIceSphere( info.GetDamagePosition(), &pIceSphere );

			EmitSound( "Glass.BulletImpact" );

			while ( flHealthToRemove > 0.0f && pIceSphere && iSpheresDestroyed < 10 )
			{
				DispatchParticleEffect( "blood_impact_synth_01", pIceSphere->m_vecPos, QAngle( 0.0f, 0.0f, 0.0f ) );

				float flAdjustedDamage = flHealthToRemove * ( ( info.GetAttacker()->IsPlayer() ) ? ( 9.0f ) : ( 1.0f ) );

				if ( flAdjustedDamage >= pIceSphere->m_iHealth )
				{
					RemoveSphere( iSphere );
					++iSpheresDestroyed;
				}
				else
				{
					pIceSphere->m_iHealth -= flAdjustedDamage;
				}

				flHealthToRemove -= pIceSphere->m_iHealth;
				iSphere = GetIceSphere( info.GetDamagePosition(), &pIceSphere );
			}
		}

		if ( iSpheresDestroyed )
		{
			RecenterAbsOrigin();
			CheckForNonRootedIceChunks();
		}
	}

	return BaseClass::OnTakeDamage( info );
}

bool CIceSculpture::TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	Vector vRadius = ReplicateToVector( ICE_SPHERE_RADIUS );
	tr.fraction = 2.0f;
#ifdef PAINT
	int iceIndex = -1; // index of ice sphere that collides
	Vector surfNormal = Vector( 0, 0, 1 );
#endif

	if ( m_bOriginsRelativeToCenter )
	{
		// Check against the simplified collision OBBs
		for ( int iCollisionOBB = 0; iCollisionOBB < m_CollisionOBBs.Count(); ++iCollisionOBB )
		{
			outer_collision_obb_t *pCollisionOBB = &(m_CollisionOBBs[ iCollisionOBB ]);

			trace_t trTemp;

			Vector vecPos;
			EntityToWorldSpace( pCollisionOBB->vecPos, &vecPos );

			IntersectRayWithOBB( ray, vecPos, pCollisionOBB->angAngles, pCollisionOBB->vecMins, pCollisionOBB->vecMaxs, 0.0f, &trTemp );

			if ( trTemp.fraction < tr.fraction )
			{
				// Hit an OBB! Now check all the spheres in it
				for ( int i = 0; i < m_vecOrigins.Count(); ++i )
				{
					EntityToWorldSpace( m_vecOrigins[ i ], &vecPos );

					IntersectRayWithOBB( ray, vecPos, QAngle(0,0,0), -vRadius, vRadius, 1.0f, &trTemp );

					if ( trTemp.fraction < tr.fraction )
					{
						// Hit a sphere closer than the current trace
						tr = trTemp;
					}
				}
			}
		}
	}
	else
	{
		// Check against the simplified collision OBBs
		for ( int iCollisionOBB = 0; iCollisionOBB < m_CollisionOBBs.Count(); ++iCollisionOBB )
		{
			outer_collision_obb_t *pCollisionOBB = &(m_CollisionOBBs[ iCollisionOBB ]);

			trace_t trTemp;

			IntersectRayWithOBB( ray, pCollisionOBB->vecPos, pCollisionOBB->angAngles, pCollisionOBB->vecMins, pCollisionOBB->vecMaxs, 0.0f, &trTemp );
#ifdef PAINT
			Vector tempNormal = trTemp.plane.normal;
#endif

			if ( trTemp.fraction < tr.fraction )
			{
				// Hit an OBB! Now check all the spheres in it
				for ( int iContainedSphere = 0; iContainedSphere < pCollisionOBB->iSpheres.Count(); ++iContainedSphere )
				{
					short sphereIndex = pCollisionOBB->iSpheres[ iContainedSphere ];
					IntersectRayWithOBB( ray, m_vecOrigins[ sphereIndex ], QAngle(0,0,0), -vRadius, vRadius, 1.0f, &trTemp );

					if ( trTemp.fraction < tr.fraction )
					{
						// Hit a sphere closer than the current trace
						tr = trTemp;
#ifdef PAINT
						iceIndex = sphereIndex;
						surfNormal = tempNormal;
#endif
					}
				}
			}
		}

		// Test against loner spheres
		for ( int iSphere = 0; iSphere < m_IceSpheres.Count(); ++iSphere )
		{
			if ( m_IceSpheres[ iSphere ].m_iOBB != -1 )
				continue;

			trace_t trTemp;

			IntersectRayWithOBB( ray, m_vecOrigins[ iSphere ], QAngle(0,0,0), -vRadius, vRadius, 1.0f, &trTemp );

			if ( trTemp.fraction < tr.fraction )
			{
				// Hit a sphere closer than the current trace
				tr = trTemp;
#ifdef PAINT
				iceIndex = iSphere;
				surfNormal = trTemp.plane.normal;
#endif
			}
		}
	}

	if ( tr.fraction < 1.0f )
	{
		IPhysicsSurfaceProps *physprop = MoveHelper()->GetSurfaceProps();
		CStudioHdr *pStudioHdr = GetModelPtr();

		tr.m_pEnt = this;
		tr.contents = pStudioHdr->contents();
		tr.surface.name = "**studio**";
		tr.surface.flags = 0;
		tr.surface.surfaceProps = physprop->GetSurfaceIndex( pStudioHdr->pszSurfaceProp() );

#ifdef PAINT
		//temp sphere index when player touch ice
		tr.worldSurfaceIndex = iceIndex;
		m_IceSpheres[ iceIndex ].m_vecSurfNormal = surfNormal;
#endif

		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs )
{
	if( m_iCachedWorldBoundsUpdateTick != gpGlobals->tickcount )
	{
		// Recalculate our cached mins/maxs
		m_iCachedWorldBoundsUpdateTick = gpGlobals->tickcount;

		Vector vecPos;

		if ( m_vecOrigins.Count() > 0 )
		{
			vecPos = m_vecOrigins[ 0 ];
		}
		else
		{
			vecPos.Init();
		}

		if ( m_bOriginsRelativeToCenter )
		{
			Vector vecTransformedPos;
			EntityToWorldSpace( vecPos, &vecTransformedPos );
			vecPos = vecTransformedPos;
		}

		m_vCachedWorldMins = vecPos;
		m_vCachedWorldMaxs = vecPos;

		if ( m_bOriginsRelativeToCenter )
		{
			for ( int i = 1; i < m_vecOrigins.Count(); ++i )
			{
				EntityToWorldSpace( m_vecOrigins[ i ], &vecPos );
				VectorMin( vecPos, m_vCachedWorldMins, m_vCachedWorldMins );
				VectorMax( vecPos, m_vCachedWorldMaxs, m_vCachedWorldMaxs );
			}
		}
		else
		{
			for ( int i = 1; i < m_vecOrigins.Count(); ++i )
			{
				vecPos = m_vecOrigins[ i ];
				VectorMin( vecPos, m_vCachedWorldMins, m_vCachedWorldMins );
				VectorMax( vecPos, m_vCachedWorldMaxs, m_vCachedWorldMaxs );
			}
		}

		Vector vRadius = ReplicateToVector( SCULPTURE_SURFACE_RADIUS * 12.0f );
		m_vCachedWorldMins -= vRadius;
		m_vCachedWorldMaxs += vRadius;
	}

	*pMins = m_vCachedWorldMins;
	*pMaxs = m_vCachedWorldMaxs;
}


icesphere_t	*CIceSculpture::GetTouchSphere( const Ray_t& ray, trace_t& tr )
{
	Vector vRadius = ReplicateToVector( ICE_SPHERE_RADIUS );
	tr.fraction = 2.0f;
	icesphere_t *pSphere = NULL;

	// Check against the simplified collision OBBs
	for ( int iCollisionOBB = 0; iCollisionOBB < m_CollisionOBBs.Count(); ++iCollisionOBB )
	{
		outer_collision_obb_t *pCollisionOBB = &(m_CollisionOBBs[ iCollisionOBB ]);

		trace_t trTemp;

		IntersectRayWithOBB( ray, pCollisionOBB->vecPos, pCollisionOBB->angAngles, pCollisionOBB->vecMins, pCollisionOBB->vecMaxs, 0.0f, &trTemp );

		if ( trTemp.fraction < tr.fraction )
		{
			// Hit an OBB! Now check all the spheres in it
			for ( int iContainedSphere = 0; iContainedSphere < pCollisionOBB->iSpheres.Count(); ++iContainedSphere )
			{
				IntersectRayWithOBB( ray, m_vecOrigins[ pCollisionOBB->iSpheres[ iContainedSphere ] ], QAngle(0,0,0), -vRadius, vRadius, 1.0f, &trTemp );

				if ( trTemp.fraction < tr.fraction )
				{
					// Hit a sphere closer than the current trace
					tr = trTemp;
					pSphere = GetSphere( pCollisionOBB->iSpheres[ iContainedSphere ] );
				}
			}
		}
	}

	// Test against loner spheres
	for ( int iSphere = 0; iSphere < m_IceSpheres.Count(); ++iSphere )
	{
		if ( m_IceSpheres[ iSphere ].m_iOBB != -1 )
			continue;

		trace_t trTemp;

		IntersectRayWithOBB( ray, m_vecOrigins[ iSphere ], QAngle(0,0,0), -vRadius, vRadius, 1.0f, &trTemp );

		if ( trTemp.fraction < tr.fraction )
		{
			// Hit a sphere closer than the current trace
			tr = trTemp;
			pSphere = &m_IceSpheres[ iSphere ];
		}
	}

	if ( tr.fraction < 1.0f )
	{
		IPhysicsSurfaceProps *physprop = MoveHelper()->GetSurfaceProps();
		CStudioHdr *pStudioHdr = GetModelPtr();

		tr.m_pEnt = this;
		tr.contents = pStudioHdr->contents();
		tr.surface.name = "**studio**";
		tr.surface.flags = 0;
		tr.surface.surfaceProps = physprop->GetSurfaceIndex( pStudioHdr->pszSurfaceProp() );

		return pSphere;
	}

	return NULL;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::RecenterAbsOrigin()
{
	m_iCachedWorldBoundsUpdateTick = -1;

	RebuildSimplifiedCollision();

	Vector vecMins, vecMaxs;
	ComputeWorldSpaceSurroundingBox( &vecMins, &vecMaxs );

	SetAbsOrigin( ( vecMaxs + vecMins ) * 0.5f );

	CollisionProp()->MarkSurroundingBoundsDirty();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::AddNeighborSphereLinks( int iSphere )
{
	icesphere_t *pSphere = &(m_IceSpheres[ iSphere ]);
	int iLinks = 0;

#ifdef _DEBUG
	// Make sure it starts with no links
	for ( int iLink = 0; iLink < ICE_SPHERE_MAX_NEIGHBORS; ++iLink )
	{
		Assert( pSphere->m_iNeighborSpheres[ iLink ] == -1 );
	}
#endif

	for ( int iSphereLink = 0; iSphereLink < m_IceSpheres.Count() && iLinks < ICE_SPHERE_MAX_NEIGHBORS; ++iSphereLink )
	{
		icesphere_t *pSphereLink = &(m_IceSpheres[ iSphereLink ]);

		if ( pSphere == pSphereLink )
			continue;

		float flDistanceSqr = pSphereLink->m_vecPos.DistToSqr( pSphere->m_vecPos );

		if ( flDistanceSqr < ICE_SPHERE_LINK_DISTANCE_SQR )
		{
			// Sphere is close enough to link!
			pSphere->m_iNeighborSpheres[ iLinks ] = iSphereLink;
			++iLinks;

			// Link us into them at the first empty slot
			for ( int iLinkedLink = 0; iLinkedLink < ICE_SPHERE_MAX_NEIGHBORS; ++iLinkedLink )
			{
				if ( pSphereLink->m_iNeighborSpheres[ iLinkedLink ] == -1 )
				{
					pSphereLink->m_iNeighborSpheres[ iLinkedLink ] = iSphere;
					break;
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::RemoveNeighborSphereLinks( int iSphere )
{
	// Fix up indices and remove us from their links
	for ( int iSphereLink = 0; iSphereLink < m_IceSpheres.Count(); ++iSphereLink )
	{
		if ( iSphereLink == iSphere )
		{
			// This one is going away, so don't do anything
			continue;
		}

		icesphere_t *pSphereLink = &(m_IceSpheres[ iSphereLink ]);

		// Loop through our links
		for ( int iLink = 0; iLink < ICE_SPHERE_MAX_NEIGHBORS; ++iLink )
		{
			if ( pSphereLink->m_iNeighborSpheres[ iLink ] == iSphere )
			{
				// Clear out this stale link
				pSphereLink->m_iNeighborSpheres[ iLink ] = -1;
			}
			else if ( pSphereLink->m_iNeighborSpheres[ iLink ] > iSphere )
			{
				// Shift down the inices by 1
				--pSphereLink->m_iNeighborSpheres[ iLink ];
			}
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CIceSculpture::BuildIceChunk( CUtlVector<short> &chunk, short iSphere, const Vector *pvPlaneNormal, const Vector *pvPlanePoint )
{
	icesphere_t *pSphere = &(m_IceSpheres[ iSphere ]);

	if ( pvPlaneNormal && pvPlanePoint )
	{
		Vector vecCenterToPlanePoint = pSphere->m_vecPos - *pvPlanePoint;
		Vector vecProjected = *pvPlaneNormal * pvPlaneNormal->Dot( vecCenterToPlanePoint );

		float fDistanceToPlane = vecProjected.Length() - ICE_SPHERE_RADIUS;

		// Skip this sphere if it doesn't lie within the plane constraint
		if ( fDistanceToPlane > 0 )
		{
			return pSphere->m_bRootedToWorld;
		}
	}

	chunk.AddToTail( iSphere );
	pSphere->m_bPeeked = true;

	bool bRootedInSameChunk = pSphere->m_bRootedToWorld;
	
	// Loop through our neighbor spheres
	for ( int iLink = 0; iLink < ICE_SPHERE_MAX_NEIGHBORS; ++iLink )
	{
		int iNeighbor = pSphere->m_iNeighborSpheres[ iLink ];

		// Skip empty slots
		if ( iNeighbor == -1 )
			continue;
	
		icesphere_t *pSphereLink = &(m_IceSpheres[ iNeighbor ]);

		// Skip over ones that have already been checked
		if ( pSphereLink->m_bPeeked )
			continue;

		// Recurse into linked spheres
		if ( BuildIceChunk( chunk, iNeighbor, pvPlaneNormal, pvPlanePoint ) )
			bRootedInSameChunk = true;
	}

	return bRootedInSameChunk;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::CheckForNonRootedIceChunks( void )
{
	// Reset all the infinite-recursion-preventing peeks
	for ( int i = 0; i < m_IceSpheres.Count(); ++i )
	{
		m_IceSpheres[ i ].m_bPeeked = false;
	}

	int iSpheresRemoved = 0;

	// Check all the spheres to see if there are non-rooted chucks
	for ( int i = 0; i < m_IceSpheres.Count(); ++i )
	{
		icesphere_t *pSphere = &(m_IceSpheres[ i ]);

		// Skip over ones that have already been checked
		if ( pSphere->m_bPeeked )
			continue;

		pSphere->m_bPeeked = true;

		// Find all connected ice spheres and check if they are rooted
		CUtlVector<short> chunk;

		if ( !BuildIceChunk( chunk, i ) )
		{
			// Simulate the chunk if we're broken through normal means or shattering and small
			if ( m_eLifeStatus == ICE_SCULPTURE_LIFE_STATUS_HEALTHY || 
				 ( m_eLifeStatus == ICE_SCULPTURE_LIFE_STATUS_SHATTERING && chunk.Count() < 10 ) )
			{
				// If this chunk is very small go ahead and kill those spheres
				if ( chunk.Count() <= 2 )
				{
					// Give it all the origins in the chunk
					for ( int iChunkSphere = 0; iChunkSphere < chunk.Count(); ++iChunkSphere )
					{
						// Remember to remove this sphere from the main sculpture
						icesphere_t *pChunkSphere = &(m_IceSpheres[ chunk[ iChunkSphere ] ]);
						pChunkSphere->m_flDieTime = gpGlobals->curtime;
					}

					// Remove spheres marked for deletion from the main sculpture
					for ( int iSphere = m_IceSpheres.Count() - 1; iSphere >= 0; --iSphere )
					{
						if ( m_IceSpheres[ iSphere ].m_flDieTime <= gpGlobals->curtime )
						{
							RemoveSphere( iSphere );
							++iSpheresRemoved;
						}
					}
				}
				else
				{
					CIceSculpture *pNewIceSculpture = static_cast<CIceSculpture *>( CreateEntityByName( "ice_sculpture" ) );

					if ( pNewIceSculpture )
					{
						// Create a new sculpture for rendering the ice
						DispatchSpawn( pNewIceSculpture );
						pNewIceSculpture->SetSolid( SOLID_NONE );
						pNewIceSculpture->SetMoveType( MOVETYPE_NONE );
						pNewIceSculpture->VPhysicsSetObject( NULL );

						// Rebuild hulls from scratch so complete breaks in the ice are accounted for
						RebuildSimplifiedCollision( true );

						CUtlVector<short> chunkOBBs;

						for ( int iChunkSphere = 0; iChunkSphere < chunk.Count(); ++iChunkSphere )
						{
							// Copy over the origin info
							icesphere_t *pChunkSphere = &(m_IceSpheres[ chunk[ iChunkSphere ] ]);
							pNewIceSculpture->m_vecOrigins.GetForModify().AddToTail( pChunkSphere->m_vecPos );

							// Copy over the OBB info					
							int iFound = chunkOBBs.Find( pChunkSphere->m_iOBB );

							outer_collision_obb_t *pNewOBB = NULL;

							if ( iFound < 0 || iFound >= chunkOBBs.Count() )
							{
								// Didn't find it, so make a new one
								chunkOBBs.AddToTail( pChunkSphere->m_iOBB );
								iFound = pNewIceSculpture->m_CollisionOBBs.AddToTail();
								pNewOBB = &(pNewIceSculpture->m_CollisionOBBs[ iFound ]);

								if ( pChunkSphere->m_iOBB != -1 )
								{
									// Copy the existing data
									outer_collision_obb_t *pOldOBB = &(m_CollisionOBBs[ pChunkSphere->m_iOBB ]);
									pNewOBB->angAngles = pOldOBB->angAngles;
									pNewOBB->vecPos = pOldOBB->vecPos;
									pNewOBB->vecMins = pOldOBB->vecMins;
									pNewOBB->vecMaxs = pOldOBB->vecMaxs;
									pNewOBB->bDirty = false;
								}
								else
								{
									// Build a simple hull around the single sphere
									pNewOBB->angAngles = QAngle( 0.0f, 0.0f, 0.0f );
									pNewOBB->vecPos = pChunkSphere->m_vecPos;
									pNewOBB->vecMins = ReplicateToVector( -ICE_SPHERE_RADIUS );
									pNewOBB->vecMaxs = ReplicateToVector( ICE_SPHERE_RADIUS );
									pNewOBB->bDirty = false;
								}
							}
							else
							{
								pNewOBB = &(pNewIceSculpture->m_CollisionOBBs[ iFound ]);
							}

							// Shpere lives in the new OBB
							pNewOBB->iSpheres.AddToTail( iChunkSphere );

							// Remember to remove this sphere from the main sculpture
							pChunkSphere->m_flDieTime = gpGlobals->curtime;
						}

						pNewIceSculpture->RecenterAbsOrigin();

						// Remove spheres marked for deletion from the main sculpture
						for ( int iSphere = m_IceSpheres.Count() - 1; iSphere >= 0; --iSphere )
						{
							if ( m_IceSpheres[ iSphere ].m_flDieTime <= gpGlobals->curtime )
							{
								RemoveSphere( iSphere );
								++iSpheresRemoved;
							}
						}

						// Create the physics representation
						CreateServerStatueFromOBBs( pNewIceSculpture->m_CollisionOBBs, pNewIceSculpture );

						// No longer takes damage, but melts soon
						pNewIceSculpture->m_takedamage = DAMAGE_NO;
						pNewIceSculpture->SetNextThink( gpGlobals->curtime + 10.0f );

						// Spheres are now relative to the parented origin/angles
						pNewIceSculpture->m_bOriginsRelativeToCenter = true;

						for ( int iOrigin = 0; iOrigin < pNewIceSculpture->m_vecOrigins.Count(); ++iOrigin )
						{
							pNewIceSculpture->m_vecOrigins[ iOrigin ] -= pNewIceSculpture->GetAbsOrigin();
						}

						// Collision OBBs are also relative
						for ( int iOBB = 0; iOBB < pNewIceSculpture->m_CollisionOBBs.Count(); ++iOBB )
						{
							pNewIceSculpture->m_CollisionOBBs[ iOBB ].vecPos -= pNewIceSculpture->GetAbsOrigin();
						}

						// Since we removed spheres from our list, reset the traversal index
						i = 0;
					}
				}
			}
		}
	}

	// If spheres were removed in the process we need to recenter our origin
	if ( iSpheresRemoved > 0 )
	{
		RecenterAbsOrigin();
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::RemoveFromOBB( int iSphere )
{
	icesphere_t *pSphere = &(m_IceSpheres[ iSphere ]);

	if ( pSphere->m_iOBB != -1 )
	{
		// Remove the sphere from the OBB it's in
		outer_collision_obb_t *pOBB = &(m_CollisionOBBs[ pSphere->m_iOBB ]);

		bool bFound;
		bFound = pOBB->iSpheres.FindAndRemove( iSphere );
		pOBB->bDirty = true;

		Assert( bFound );

		// Check if all the spheres have been removed
		if ( pOBB->iSpheres.Count() == 0 )
		{
			m_CollisionOBBs.Remove( pSphere->m_iOBB );

			// Update the spheres in all the hulls after this one removed
			for ( int iRemainingOBB = pSphere->m_iOBB; iRemainingOBB < m_CollisionOBBs.Count(); ++iRemainingOBB )
			{
				pOBB = &(m_CollisionOBBs[ iRemainingOBB ]);

				for ( int iContainedSphere = 0; iContainedSphere < pOBB->iSpheres.Count(); ++iContainedSphere )
				{
					// Set inices to the proper value
					icesphere_t *pContainedSphere = &(m_IceSpheres[ pOBB->iSpheres[ iContainedSphere ] ]);

					Assert( pContainedSphere->m_iOBB == iRemainingOBB + 1 );
					pContainedSphere->m_iOBB = iRemainingOBB;
				}
			}
		}
	}

	// Update the spheres in all hulls
	for ( int iOBB = 0; iOBB < m_CollisionOBBs.Count(); ++iOBB )
	{
		outer_collision_obb_t *pOBB = &(m_CollisionOBBs[ iOBB ]);

		// Update all the spheres in this hull
		for ( int iContainedSphere = pOBB->iSpheres.Count() - 1; iContainedSphere >= 0; --iContainedSphere )
		{
			Assert( pOBB->iSpheres[ iContainedSphere ] != iSphere );

			if ( pOBB->iSpheres[ iContainedSphere ] > iSphere )
			{
				// Shift down the inices by 1
				pOBB->iSpheres[ iContainedSphere ]--;
			}
		}
	}

	pSphere->m_iOBB = -1;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::RebuildCollisionOBB( short iOBB )
{
	outer_collision_obb_t *pOBB = &(m_CollisionOBBs[ iOBB ]);

	// Bail if this has already been built
	if ( !pOBB->bDirty )
		return;

	// We'll need to convert those sphere positions into the plane space
	matrix3x4_t matPlaneRot;
	AngleMatrix( pOBB->angAngles, matPlaneRot );

	// Get the extents of those spheres in world space
	pOBB->vecMins = ReplicateToVector( FLT_MAX );
	pOBB->vecMaxs = -pOBB->vecMins;

	for ( int iChunkSphere = 0; iChunkSphere < pOBB->iSpheres.Count(); ++iChunkSphere )
	{
		Vector vecPosTransformed;
		VectorIRotate( m_vecOrigins[ pOBB->iSpheres[ iChunkSphere ] ], matPlaneRot, vecPosTransformed );

		VectorMin( vecPosTransformed, pOBB->vecMins, pOBB->vecMins );
		VectorMax( vecPosTransformed, pOBB->vecMaxs, pOBB->vecMaxs );
	}

	Vector vecRadius = ReplicateToVector( ICE_SPHERE_RADIUS );
	pOBB->vecMins -= vecRadius;
	pOBB->vecMaxs += vecRadius;

	// Position is right in the center of the extents
	Vector vecCenterPos = ( pOBB->vecMaxs + pOBB->vecMins ) * 0.5f;

	// Extents in local space
	pOBB->vecMins -= vecCenterPos;
	pOBB->vecMaxs -= vecCenterPos;

	VectorRotate( vecCenterPos, matPlaneRot, pOBB->vecPos );

	pOBB->bDirty = false;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::RebuildSimplifiedCollision( bool bCleanRebuild )
{
	if ( bCleanRebuild )
	{
		// Clean out all the OBBs to rebuild from scratch
		m_CollisionOBBs.RemoveAll();

		for ( int iSphere = 0; iSphere < m_IceSpheres.Count(); ++iSphere )
		{
			m_IceSpheres[ iSphere ].m_iOBB = -1;
		}
	}

	// Reset all the infinite-recursion-preventing peeks
	for ( int i = 0; i < m_IceSpheres.Count(); ++i )
	{
		icesphere_t *pSphere = &(m_IceSpheres[ i ]);
		pSphere->m_bPeeked = ( pSphere->m_iOBB != -1 );
	}

	// Check all the spheres to fit into existing collision hulls
	for ( int i = 0; i < m_IceSpheres.Count(); ++i )
	{
		icesphere_t *pSphere = &(m_IceSpheres[ i ]);

		// Skip over ones that have already been checked
		if ( pSphere->m_bPeeked )
			continue;

		// See if it fits any of the adjacent chunks
		for ( int iSphereLink = 0; iSphereLink < ICE_SPHERE_MAX_NEIGHBORS && pSphere->m_iOBB == -1; ++iSphereLink )
		{
			int iNeighbor = pSphere->m_iNeighborSpheres[ iSphereLink ];

			// Skip empty slots
			if ( iNeighbor == -1 )
				continue;

			icesphere_t *pSphereLink = &(m_IceSpheres[ iNeighbor ]);

			if ( pSphereLink->m_iOBB != -1 )
			{
				// This linked sphere lives in an OBB that we can try out
				outer_collision_obb_t *pOBB = &(m_CollisionOBBs[ pSphereLink->m_iOBB ]);

				Vector vPlaneNormal;
				AngleVectors( pOBB->angAngles, &vPlaneNormal );

				Vector vecCenterToPlanePoint = pSphere->m_vecPos - pOBB->vecPos;
				Vector vecProjected = vPlaneNormal * vPlaneNormal.Dot( vecCenterToPlanePoint );

				float fDistanceToPlane = vecProjected.Length() - ICE_SPHERE_RADIUS;

				if ( fDistanceToPlane <= 0 )
				{
					// This sphere lies within the plane constraint, so add a chunk of others that want to join
					int iChunkSphere = pOBB->iSpheres.Count();
					BuildIceChunk( pOBB->iSpheres, i, &vPlaneNormal, &pOBB->vecPos );

					// Add the chunk spheres to the list of spheres in this OBB
					for ( iChunkSphere; iChunkSphere < pOBB->iSpheres.Count(); ++iChunkSphere )
					{
						icesphere_t *pChunkSphere = &(m_IceSpheres[ pOBB->iSpheres[ iChunkSphere ] ]);
						pChunkSphere->m_iOBB = pSphereLink->m_iOBB;
					}

					pOBB->bDirty = true;
				}
			}
		}
	}

	// Check all the spheres to build new collision hulls
	for ( int i = 0; i < m_IceSpheres.Count(); ++i )
	{
		icesphere_t *pSphere = &(m_IceSpheres[ i ]);

		// Skip over ones that have already been checked
		if ( pSphere->m_bPeeked )
			continue;

		pSphere->m_bPeeked = true;

		// Make a plane from 3 points for this collision hull to approximate
		Vector vecPlaneBasis[ 2 ];
		int iBasis = 0;

		for ( int iSphereLink = 0; iSphereLink < ICE_SPHERE_MAX_NEIGHBORS && iBasis < 2; ++iSphereLink )
		{
			int iNeighbor = pSphere->m_iNeighborSpheres[ iSphereLink ];

			// Skip empty slots
			if ( iNeighbor == -1 )
				continue;

			icesphere_t *pSphereLink = &(m_IceSpheres[ iNeighbor ]);

			// Skip over ones that have already been checked
			if ( pSphereLink->m_bPeeked )
				continue;

			// Set the vector between the original sphere and it's neighbor as a basis
			vecPlaneBasis[ iBasis ] = pSphereLink->m_vecPos - pSphere->m_vecPos;
			VectorNormalize( vecPlaneBasis[ iBasis ] );

			if ( iBasis == 0 )
			{
				++iBasis;
			}
			else
			{
				// If this will be the second basis, make sure it isn't too similar to the first basis
				float fDot = vecPlaneBasis[ 0 ].Dot( vecPlaneBasis[ 1 ] );

				if ( fDot > -0.5 && fDot < 0.5 )
				{
					++iBasis;
				}
			}
		}

		if ( iBasis < 2 )
		{
			// Couldn't find a plane to work with! This sphere is a loner.
		}
		else
		{
			// Build the OBB for this chunk
			int iOBB = m_CollisionOBBs.AddToTail();
			outer_collision_obb_t *pOBB = &(m_CollisionOBBs[ iOBB ]);

			// Get the plane's normal
			Vector vecNormal = vecPlaneBasis[ 0 ].Cross( vecPlaneBasis[ 1 ] );
			VectorNormalize( vecNormal );

			// The major axis is usually the extrude direction
			Vector vecOBBLongAxis;
			if ( pSphere->m_vecExtrudeDir.IsZero() )
			{
				vecOBBLongAxis = vecPlaneBasis[ 0 ];
			}
			else
			{
				vecOBBLongAxis = pSphere->m_vecExtrudeDir;
			}

			// Set the OBBs angles
			VectorAngles( vecNormal, vecOBBLongAxis, pOBB->angAngles );

			// Find a chunk of spheres that lie within the point-normal defined plane
			BuildIceChunk( pOBB->iSpheres, i, &vecNormal, &pSphere->m_vecPos );

			// Add the chunk spheres to the list of spheres in this OBB
			for ( int iChunkSphere = 0; iChunkSphere < pOBB->iSpheres.Count(); ++iChunkSphere )
			{
				icesphere_t *pChunkSphere = &(m_IceSpheres[ pOBB->iSpheres[ iChunkSphere ] ]);
				pChunkSphere->m_iOBB = iOBB;
			}

			pOBB->bDirty = true;
		}
	}

	// Rebuild all the dirty hulls
	for ( int iHull = 0; iHull < m_CollisionOBBs.Count(); ++iHull )
	{
		RebuildCollisionOBB( iHull );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::AddBaseSphere( int iSphere )
{
	icesphere_t *pSphere = &(m_IceSpheres[ iSphere ]);

	m_vecBaseOrigins.AddToTail( pSphere->m_vecPos );
	pSphere->m_bRootedToWorld = true;
	AddSphere( iSphere );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::AddSphere( int iSphere )
{
	icesphere_t *pSphere = &(m_IceSpheres[ iSphere ]);

	pSphere->m_hSculpture = this;
	m_vecOrigins->AddToTail( pSphere->m_vecPos );

	if ( m_eLifeStatus != ICE_SCULPTURE_LIFE_STATUS_HEALTHY )
	{
		// This sculpture is dying, so kill this sphere very soon
		pSphere->m_flDieTime = ( 1.0f / icegun_shatter_chain_reaction_speed.GetFloat() );
	}
	else
	{
		pSphere->m_flDieTime = gpGlobals->curtime + icegun_sphere_lifetime.GetFloat();
	}

	AddPhysicsObjectForSphere( pSphere );

	AddNeighborSphereLinks( iSphere );
}

void CIceSculpture::AddPhysicsObjectForSphere( icesphere_t *pSphere )
{
	IPhysicsObject *pPhysics = NULL;

	for ( int i = 0; i < m_PhysicsObjects.Count(); ++i )
	{
		pPhysics = m_PhysicsObjects[ i ];

		if ( !pPhysics->IsCollisionEnabled() )
		{
			// Found a free one!
			break;
		}
	}

	Assert( pPhysics );

	if ( pPhysics )
	{
		pSphere->m_pPhysObject = pPhysics;
		pPhysics->SetGameData( this );
		pPhysics->SetPosition( pSphere->m_vecPos, QAngle( 0.0f, 0.0f, 0.0f ), true );
		pPhysics->EnableCollisions( true );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::RemoveSphere( int iSphere, bool bFast )
{
	icesphere_t *pSphere = &(m_IceSpheres[ iSphere ]);

	if ( !bFast )
	{
		// FIXME: bleh, looking up by origin seems dangerous
		int iOldOrigin = m_vecOrigins->Find( pSphere->m_vecPos );

		Assert( iOldOrigin >= 0 && iOldOrigin < m_vecOrigins->Count() );

		m_vecOrigins->Remove( iOldOrigin );
	}

	// Get the associated physics object
	IPhysicsObject *pPhys = pSphere->m_pPhysObject;
	pSphere->m_pPhysObject = NULL;

	if ( !bFast )
	{
		// Clean out links to this sphere
		RemoveNeighborSphereLinks( iSphere );

		// Remove this sphere from the simplified collision chunk
		RemoveFromOBB( iSphere );

		m_IceSpheres.Remove( iSphere );
	}

	// Disable the associated physics object
	pPhys->EnableCollisions( false );
	pPhys->SetGameData( NULL );

	// Kill this sculpture if this was the final sphere
	if ( !m_IceSpheres.Count() )
	{
		UTIL_Remove( this );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CIceSculpture::GetIceSphere( Vector vPosition, icesphere_t **ppIceSphere )
{
	float flNearestDistSqr = FLT_MAX;
	int iNearestSphere = -1;
	icesphere_t *pNearestIceSphere = NULL;

	for ( int i = 0; i < m_IceSpheres.Count(); ++i )
	{
		icesphere_t *pIceSphere = &(m_IceSpheres[ i ]);

		float flDistSqr = vPosition.DistToSqr( pIceSphere->m_vecPos );

		if ( flDistSqr < flNearestDistSqr )
		{
			flNearestDistSqr = flDistSqr;
			iNearestSphere = i;
			pNearestIceSphere = pIceSphere;
		}
	}

	if ( ppIceSphere )
	{
		*ppIceSphere = pNearestIceSphere;
	}

	return iNearestSphere;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::Melt( void )
{
	m_eLifeStatus = ICE_SCULPTURE_LIFE_STATUS_MELTING;
	m_takedamage = DAMAGE_NO;

	float dt = 0.01;
	if ( icegun_max_fixed_ice.GetFloat() != 0 )
	{
		dt = icegun_reload_melt_time.GetFloat() / icegun_max_fixed_ice.GetFloat();
	}

	// Melt all the spheres that live in this sculpture
	float flCount = 0;

	for ( int i = 0; i < m_IceSpheres.Count(); ++i )
	{
		icesphere_t *pSphere = &(m_IceSpheres[ i ]);
		pSphere->m_flDieTime = gpGlobals->curtime + flCount * dt;
		flCount++;
	}

	if ( flCount == 0.0f )
	{
		// Couldn't find any attached spheres
		UTIL_Remove( this );
	}
	else
	{
		SetThink( &CIceSculpture::RemoveSphereThink );
		SetNextThink( gpGlobals->curtime + dt );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::Shatter( Vector vecPos, float flDelay )
{
	m_eLifeStatus = ICE_SCULPTURE_LIFE_STATUS_SHATTERING;
	m_takedamage = DAMAGE_NO;

	// Shatter the spheres delayed by distance
	for ( int i = 0; i < m_IceSpheres.Count(); ++i )
	{
		icesphere_t *pSphere = &(m_IceSpheres[ i ]);

		float flDistance = vecPos.DistTo( pSphere->m_vecPos );

		// Dies sooner the closer it is
		pSphere->m_flDieTime = gpGlobals->curtime + flDelay + flDistance * ( 1.0f / icegun_shatter_chain_reaction_speed.GetFloat() );
	}

	SetThink( &CIceSculpture::RemoveSphereThink );
	SetNextThink( gpGlobals->curtime + flDelay );
}

void CIceSculpture::RemoveSphereThink( void )
{
	if ( m_IceSpheres.Count() == 0.0f )
	{
		// Couldn't find any attached spheres
		UTIL_Remove( this );
		return;
	}

	float flNextThink = 0.0f;

	// Loop through all the ice spheres to see if they have died
	for ( int i = m_IceSpheres.Count() - 1; i >= 0; --i )
	{
		icesphere_t *pSphere = &(m_IceSpheres[ i ]);

		if ( pSphere->m_flDieTime > 0.0f )
		{
			if ( pSphere->m_flDieTime <= gpGlobals->curtime )
			{
				// This sphere is ready to die
				if ( m_eLifeStatus == ICE_SCULPTURE_LIFE_STATUS_SHATTERING )
				{
					// To help perf, only do effects once per row of spheres.
					if ( ( i % icegun_fixed_width.GetInt() ) == 0 )
					{
						DispatchParticleEffect( "ice_shatter", pSphere->m_vecPos, QAngle( 0.0f, 0.0f, 0.0f ), NULL );
						
						// TODO: remove this code if we don't do damage
						if ( icegun_sphere_damage.GetFloat() > 0 )
						{
							UTIL_ScreenShake( GetAbsOrigin(), 15.0, 150.0, 1.0, 250.0f, SHAKE_START );
						}
						
						if ( gpGlobals->curtime > m_flNextShatterSoundTime )
						{
							EmitSound( "Weapon_Icegun.SculptureShatter" );
							m_flNextShatterSoundTime = gpGlobals->curtime + random->RandomFloat( 0, 0.3 );
						}
					}

					// To help perf, only do physics forces every other sphere.
					// TODO: remove this code if we don't do damage
					if ( icegun_sphere_damage.GetFloat() > 0 )
					{
						int nDamageType = DMG_BLAST;
						if ( ( i % 2 ) == 0 )
						{
							nDamageType |= DMG_PREVENT_PHYSICS_FORCE;
						}
					
						RadiusDamage( CTakeDamageInfo( this, GetOwnerEntity(), icegun_sphere_damage.GetFloat(), nDamageType ), GetAbsOrigin(), icegun_sphere_explode_radius.GetFloat(), CLASS_NONE, NULL );
					}
				}

				RemoveSphere( i );
			}
			else
			{
				// The sphere will die some time in the future
				if ( flNextThink == 0.0f || flNextThink > pSphere->m_flDieTime )
				{
					// Remember the time that the soonest sphere will die
					flNextThink = pSphere->m_flDieTime;
				}
			}
		}
	}

	if ( m_eLifeStatus != ICE_SCULPTURE_LIFE_STATUS_SHATTERING )
	{
		m_eLifeStatus = ICE_SCULPTURE_LIFE_STATUS_MELTING;
	}

	RecenterAbsOrigin();
	CheckForNonRootedIceChunks();

	// We think again when the soonest sphere will die
	if ( flNextThink != 0.0f )
	{
		SetNextThink( flNextThink );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceSculpture::GetSculptureGestureDir( Vector &vecDir )
{
	Assert( m_vecBaseOrigins.Count() > 1 );

	Vector vecOrigin1 = m_vecBaseOrigins.Head();
	Vector vecOrigin2 = m_vecBaseOrigins.Tail();

	vecDir = vecOrigin1 - vecOrigin2;
	VectorNormalize( vecDir );
}


//-----------------------------------------------------------------------------
// TODO: deal with falling off the side of the sculpture
//-----------------------------------------------------------------------------
bool CIceSculpture::PlayerIsOnSculpture( CBasePlayer *pPlayer, icesphere_t *pLastGround )
{
	Vector vecTail = m_vecOrigins->Tail();

	if ( DotProduct( pPlayer->GetAbsOrigin(), pLastGround->m_vecExtrudeDir ) > DotProduct( vecTail, pLastGround->m_vecExtrudeDir ) )
	{
		// We've flown off the end of the sculpture
		//Msg( "FLEW OFF THE END\n" );
		return false;
	}

	return true;
}


void CIceSculpture::DrawSimplifiedOBB()
{
	// Debug draw simplified OBB
	for ( int iHull = 0; iHull < m_CollisionOBBs.Count(); ++iHull )
	{
		color32 color;
		color.r = iHull * 20;
		color.g = iHull * 30;
		color.b = iHull * 50;

		outer_collision_obb_t *pOBB = &(m_CollisionOBBs[ iHull ]);
		NDebugOverlay::BoxAngles( pOBB->vecPos, pOBB->vecMins, pOBB->vecMaxs, pOBB->angAngles, color.r, color.g, color.b, 64, 0.1f );
	}
}


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


//-----------------------------------------------------------------------------
// Ice gun shoots this out to find a surface to start applying ice to.
//-----------------------------------------------------------------------------
class CIceProbe : public CPhysicsProp
{
public:
	void Spawn();
	void Precache();
	void ProbeTouch( CBaseEntity *pOther );

	void Land( CBaseEntity *pOther, trace_t &tr );
	inline void Detach();
	
	void StartFading();

	void LandOnHotSurface( CBaseEntity *pOther, trace_t &tr );
	void LandOnIceSphere( CBaseEntity *pOther, trace_t &tr );
	void LandOnNPC( CBaseEntity *pOther, trace_t &tr );
	void LandOnPhysics( CBaseEntity *pOther, trace_t &tr );
	void LandOnWorld( CBaseEntity *pOther, trace_t &tr );

	void Vaporize();

	void InputVaporize( inputdata_t &inputdata );

	bool m_bLanded;
	bool m_bLandedOnObject;
	bool m_bAttached;		// Whether this probe is still attached to the gun

	EHANDLE m_hLandedEnt;	// The entity we landed on
	Vector m_vecNormal;		// The normal of the surface we landed on

	DECLARE_CLASS( CIceProbe, CPhysicsProp );
	DECLARE_DATADESC();
};

BEGIN_DATADESC( CIceProbe)
	DEFINE_FIELD( m_bLanded, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bAttached, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hLandedEnt, FIELD_EHANDLE ),
	DEFINE_FIELD( m_vecNormal, FIELD_VECTOR ),
	DEFINE_ENTITYFUNC( ProbeTouch ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Vaporize", InputVaporize ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( ice_probe, CIceProbe );


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::Precache()
{
	PrecacheModel( ICEGUN_MODEL_SPHERE );
	PrecacheScriptSound( "Weapon_IceGun.HitHotSurface" );

	// FIXME: temp!
	PrecacheParticleSystem( "acid_sizzle" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void CIceProbe::Detach()
{
	m_bAttached = false;

	if ( m_bLanded || m_bLandedOnObject )
	{
		UTIL_Remove( this );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::Spawn()
{
	SetModel( ICEGUN_MODEL_SPHERE );

	SetSolid( SOLID_VPHYSICS );
	SetSize( vec3_origin, vec3_origin );

	AddEffects( EF_NOSHADOW | EF_NORECEIVESHADOW );

	if ( ice_probe_gravity.GetFloat() )
	{
		SetGravity( ice_probe_gravity.GetFloat() );
		SetMoveType( MOVETYPE_FLYGRAVITY );
	}
	else
	{
		SetMoveType( MOVETYPE_FLY );
	}

	SetTouch( &CIceProbe::ProbeTouch );

	SetCollisionGroup( COLLISION_GROUP_NONE );
	
	m_bAttached = true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool IsHotSurface( const csurface_t &surface )
{
	if ( surface.flags & SURF_NOPORTAL )
		return true;

	//const surfacedata_t *pdata = physprops->GetSurfaceData( surface.surfaceProps );
	//if ( pdata->game.material == CHAR_TEX_GLASS )
	//	return true;

	// Skipping all studio models
	//if ( StringHasPrefix( surface.name, "**studio**" ) )
	//	return true;

	return false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::StartFading()
{
	// TODO: particle effect when ice probe goes out of range
	UTIL_Remove( this );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::Vaporize()
{
	// We've landed on a hot surface that doesn't allow ice.
	// HACK: make a melting state instead?
	EmitSound( "Weapon_IceGun.HitHotSurface" );

	m_bLanded = false;

	QAngle angles;
	VectorAngles( m_vecNormal, angles );
	DispatchParticleEffect( "acid_sizzle", GetAbsOrigin(), angles );

	UTIL_Remove( this );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::InputVaporize( inputdata_t &inputdata )
{
	Vaporize();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::LandOnHotSurface( CBaseEntity *pOther, trace_t &tr )
{
	Vaporize();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::LandOnNPC( CBaseEntity *pOther, trace_t &tr )
{
	CBaseAnimating *pBaseAnimating = dynamic_cast<CBaseAnimating*>( pOther );
	if ( pBaseAnimating )
	{
		m_bLanded = false;
		m_bLandedOnObject = true;

		if ( !m_bAttached )
		{
			UTIL_Remove( this );
		}

		return;
	}

	LandOnWorld( pOther, tr );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::LandOnIceSphere( CBaseEntity *pOther, trace_t &tr )
{
	if ( m_bAttached )
	{
		LandOnWorld( pOther, tr );
	}
	else
	{
		UTIL_Remove( this );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::LandOnPhysics( CBaseEntity *pOther, trace_t &tr )
{
	// Freezes just like NPCs do
	LandOnNPC( pOther, tr );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::LandOnWorld( CBaseEntity *pOther, trace_t &tr )
{
	Vector vecPos = tr.endpos;
	Vector vecNormal = tr.plane.normal;

	//EmitSound( "IceGun.ProbeHitWorld" );

	// Move the origin into the surface by 2 units to make stepping onto ramps easier
	Assert( vecPos.LengthSqr() != 0 );
	SetAbsOrigin( vecPos - 2.0f * vecNormal );

	if ( !m_bAttached )
	{
		// HACK:
		float flSpacing = 8; //icegun_sphere_spacing.GetFloat();

		//
		// Derive build vectors from the view direction and surface normal so
		// that we build a line along the surface from view left to view right.
		//
		Vector vecBuildUp = vecNormal;

		// HACK: generate a better right vector
		Vector vecBuildRight;
		Vector vecTemp;
		GetBasisFromDir( vecNormal, vecBuildRight, vecTemp );

		const int width = 3;
		Vector vecNewPos = vecPos - ( vecBuildRight * 0.5 * width * flSpacing );
		for ( int i = 0; i < width; i++ )
		{
			// TODO: Don't create spheres that will intersect the player.
			// FIXME: create a temp sphere and measure its bbox
			//Vector vecMins = vecNewPos - Vector( ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS );
			//Vector vecMaxs = vecNewPos + Vector( ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS );

			//Vector vecPlayerMins;
			//Vector vecPlayerMaxs;
			//GetOwner()->CollisionProp()->WorldSpaceAABB( &vecPlayerMins, &vecPlayerMaxs );
			//if ( IsBoxIntersectingBox(vecMins, vecMaxs,  vecPlayerMins, vecPlayerMaxs) )
			//	continue;						

			//
			// Okay, we're going to actually create a new sphere.
			//
			// FIXFIX!!!
			DevMsg( "Unattached ice probe landed! Need to fix code that creates ice here!\n" );
			/*CIceSphere *pSphere = static_cast<CIceSphere *>(CreateEntityByName( "ice_sculpture" ));
			if ( pSphere != NULL )
			{
				pSphere->SetAbsOrigin( vecNewPos );
				pSphere->SetAbsAngles( QAngle( 0, 0, 0 ) );
				pSphere->Spawn();

				IPhysicsObject *pPhys = pSphere->VPhysicsGetObject();
				if ( pPhys )
				{
					pPhys->EnableMotion( false );
				}
			}*/

			vecNewPos += vecBuildRight * 2.0f * flSpacing;
		}

		UTIL_Remove( this );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static bool IsMovablePhysicsObject( CBaseEntity *pEntity )
{
	return pEntity && pEntity->GetMoveType() == MOVETYPE_VPHYSICS && pEntity->VPhysicsGetObject() && pEntity->VPhysicsGetObject()->IsMoveable();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::Land( CBaseEntity *pOther, trace_t &tr )
{
	// Stop moving
	SetTouch( NULL );
	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
	SetAbsVelocity( vec3_origin );

	// Fill in information about what we landed on
	m_bLanded = true;
	m_bLandedOnObject = false;
	m_vecNormal = tr.plane.normal;
	m_hLandedEnt = pOther;

	// Gather more info and trigger effects	
	if ( IsHotSurface( tr.surface ) )
	{
		//Msg( "Land on Hot Surface\n" );
		LandOnHotSurface( pOther, tr );	
	}
	else if ( FClassnameIs( pOther, "ice_sculpture" ) )
	{
		//Msg( "Land on Ice Sphere\n" );
		LandOnIceSphere( pOther, tr );
	}
	else if ( pOther->MyNPCPointer() && ( pOther->Classify() != CLASS_BULLSEYE ) )
	{
		//Msg( "Land on NPC\n" );
		LandOnNPC( pOther, tr );
	}
	else if ( IsMovablePhysicsObject( pOther ) )
	{
		//Msg( "Land on physics\n" );
		LandOnPhysics( pOther, tr );
	}
	else
	{
		//Msg( "Land on world\n" );
		LandOnWorld( pOther, tr );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CIceProbe::ProbeTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet(FSOLID_VOLUME_CONTENTS | FSOLID_TRIGGER) )
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
		Land( pOther, tr );
		SetTouch( NULL );
	}
	else
	{	
		UTIL_Remove( this );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
typedef void (CWeaponIcegun::*WeaponizerShootFunc_t)( attackstate_t eAttackState );

struct WeaponParams_t
{
	bool bContinuous;					// Whether the user can press & hold to keep firing
	float flPrimaryAmmoBurnRate;		// Primary ammo consumed per shot while firing
	float flSecondaryAmmoBurnRate;		// Secondary ammo consumed per shot while firing
	WeaponizerShootFunc_t funcShoot;	// Function to call from PrimaryAttack
};


struct AmmoParams_t
{
	int iMaxAmmo;				// The maximum carry for this ammo type

	bool bMassBased;			// Whether we absorb based on mass or 
	float flAmmoPerAbsorb;		// Either ammo per kg or ammo per absorb, depending on the above

	bool bRemoveOnAbsorb;		// Whether to remove the entity after absorbing the ammo
};


enum ProbeState_t
{
	PROBE_STATE_IN_FLIGHT = 0,
	PROBE_STATE_LOST,
	PROBE_STATE_LANDED,
	PROBE_STATE_LANDED_ON_SCULPTURE,
};


class CWeaponIcegun : public CBaseHLCombatWeapon
{
public:
	DECLARE_CLASS( CWeaponIcegun, CBaseHLCombatWeapon );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CWeaponIcegun( void );
	~CWeaponIcegun( void );

	void Precache();
	void Equip( CBaseCombatCharacter *pOwner );

	virtual void Spawn();
	virtual void OnRestore();
	virtual void StopLoopingSounds();
	virtual void UpdateOnRemove(void);
	virtual void Think();

	bool Reload();
	void PrimaryAttack( attackstate_t eState );
	void SecondaryAttack( attackstate_t eState );
	void WeaponIdle();
	void ItemPreFrame();
	void ItemPostFrame();
	void ItemHolsterFrame( void );

	void UpdateSculptBeam( const Vector &forward, const Vector &vecEndPos );
	void DestroySculptBeam();

	void StopSculpting();
	void Init();
	void SetSkin( int skinNum );

	bool Deploy( void );
	bool Holster( CBaseCombatWeapon *pSwitchingTo );
	bool HasAnyAmmo( void ) { return true; }
	bool ShouldDisplayHUDHint() { return true; }

	bool IsSpecialSuitAbility( void ) { return true && sv_special_suit_abilities.GetBool(); }

	void MeltSculpture( const Vector &forward );
	void AddSculptSpheres( attackstate_t eState );
	void ShootIce( attackstate_t eState );

	void InputOnSculptureVaporized( inputdata_t &inputdata );

protected:

	void UpdateSculptSound();
	bool SculptStateRequiresAmmo();

	void GetMuzzlePos( Vector &vecMuzzle );

	void DoEffect( int effectType, Vector *pos = NULL );
	void StartEffects( void );					// Initialize all sprites and beams
	void StopEffects( bool stopSound = true );	// Hide all effects temporarily
	void DestroyEffects( void );				// Destroy all sprites and beams
	void DoEffectNone( void );

	void DryFire();
	void PrimaryFireEffect();

	bool IsAmmoFull( int iType );

	int					m_EffectState;		// Current state of the effects on the gun

	CNetworkVar( bool, m_bFiringSecondary );
	CNetworkVar( bool, m_bFiringPrimary );

	bool m_bPrimaryAttackDebounce;

	attackstate_t		m_eAttackState;

	float				m_flNextReloadTime;

	float				m_flNextPrimaryRegenTime;
	float				m_flNextSecondaryRegenTime;
	float				m_flNextPrimaryAnimationTime;
	float				m_flNextSecondaryAnimationTime;
	float				m_flNextAbsorbAnimationTime;

	float				m_flSphereCreationSpeed;

	WeaponParams_t m_weaponParams[LAST_WEAPON_PARAM_COUNT];

	CSoundPatch *m_pIcegunLoopSound;
	CSoundPatch *m_pIceLoopSound;

public:

	bool CanCreateIceSphere( const Vector &vecPos );
	icesphere_t *CreateIceSphere( const Vector &vecNewSpherePos, CIceSculpture *pSculpture, int *pnIndex = NULL );

	bool HasValidGesture();
	void BeginSculpture( const Vector &forward, const Vector &right, const Vector &vecPos, const Vector &vecNormal, int width );
	void GetSculptGestureDir( Vector &vecGestureDir );

	void ExtrudeSpherical( const Vector &start, const Vector &forward, const Vector &right, const Vector &vecPos, const Vector &vecNormal, int width );

	void LinkSpheresIntoSculpture( CUtlVector<int> &spheres, const Vector &vecDelta );

	void GetExtrudeDirPlanar( const Vector &start, const Vector &forward, const Vector &vecToRefPoint, Vector &vecSurfaceBuilderDir );
	void ExtrudePlanar( const Vector &vecDir );

	void FireProbe( const Vector &start, const Vector &forward, const Vector &right );
	ProbeState_t CheckProbe( const Vector &start, const Vector &forward, Vector &vecRefPoint, Vector &vecNormal );
	ProbeState_t HasProbeLanded( const Vector &forward, Vector &vecRefPoint, Vector &vecNormal );

	void FreezeObject( const Vector &start, const Vector &forward );

	void GetBuildVectors( const Vector &forward, const Vector &right, const Vector &vecNormal, Vector &vecBuildRight, Vector &vecBuildUp );

	bool IsPlayerIceSurfing( CIceSculpture *pSculpture = NULL );
	bool ShouldLimitSlope();

	void SubtractAmmo( int iType );
	void OnOutOfIceAmmo();
	bool UpdateIceAmmo();

public:

	CHandle<CIceProbe> m_hProbe;
	icesphere_t *m_pRefSphere;
	CUtlVector<int> m_PrevSpheres;

	SculptIceState_t m_eSculptState;
	EHANDLE m_hPrevExtrudePos;

	Vector m_vecProbeStart;
	Vector m_vecProbeForward;
	
	EHANDLE m_hLastGroundEnt;

	CHandle<CEnvQuadraticBeam> m_hSculptBeam;

	CHandle<CIceSculpture> m_hActiveSculpture;
	CHandle<CBaseAnimating> m_hFreezingObject;

	Vector m_vecGestureNormal;
	Vector m_vecGestureDelta;		// The offset of the gesture reference point from the original shoot position
	float m_flGestureDist;			// The length of m_vecGestureDelta. Ice is added at this distance from the shoot position.
	float m_flShatterTime;

	int m_nWarningLevel;			// Which low ammo warning beep have we played? 0=none

protected:

	// Because the icegun is a leaf class, we can use
	// static variables to store this information, and save some memory.
	// Should the icegun end up having inheritors, their activate may
	// stomp these numbers, in which case you should make these ordinary members
	// again.
	//
	// The icegun also caches some pose parameters in SetupGlobalModelData().
	static int m_poseActive;
	static bool m_sbStaticPoseParamsLoaded;
};

bool CWeaponIcegun::m_sbStaticPoseParamsLoaded = false;
int CWeaponIcegun::m_poseActive = 0;
static char s_szEmpty[1] = { '\0' };


static WeaponParams_t s_ShootIce =
{
	true, //bool bContinuous;				// Whether the user can press & hold to keep firing
	0.0f, //float flPrimaryAmmoBurnRate;	// Primary ammo consumed per shot while firing
	1.0f, //float flSecondaryAmmoBurnRate;	// Secondary ammo consumed per shot while firing
	&CWeaponIcegun::ShootIce,
};

static WeaponParams_t s_AddSculptSpheres =
{
	true, //bool bContinuous;				// Whether the user can press & hold to keep firing
	0.1f, //float flPrimaryAmmoBurnRate;	// Primary ammo consumed per shot while firing
	0.0f, //float flSecondaryAmmoBurnRate;	// Secondary ammo consumed per shot while firing
	&CWeaponIcegun::AddSculptSpheres,
};

IMPLEMENT_SERVERCLASS_ST(CWeaponIcegun, DT_WeaponIcegun)
	//SendPropBool( SENDINFO( m_bFiringSecondary ) ),
	//SendPropBool( SENDINFO( m_bFiringPrimary ) ),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_icegun, CWeaponIcegun );
PRECACHE_WEAPON_REGISTER( weapon_icegun );

BEGIN_DATADESC( CWeaponIcegun )

	DEFINE_FIELD( m_EffectState, FIELD_INTEGER ),

	DEFINE_FIELD( m_flNextPrimaryAnimationTime , FIELD_TIME ),
	DEFINE_FIELD( m_flNextSecondaryAnimationTime , FIELD_TIME ),
	DEFINE_FIELD( m_flNextAbsorbAnimationTime, FIELD_TIME ),
	DEFINE_FIELD( m_flSphereCreationSpeed, FIELD_FLOAT ),

	//DEFINE_SOUNDPATCH( m_pIcegunLoopSound ),
	//DEFINE_SOUNDPATCH( m_pIceLoopSound ),

	DEFINE_FIELD( m_flNextReloadTime, FIELD_TIME ),
	DEFINE_FIELD( m_eAttackState, FIELD_INTEGER ),

	DEFINE_FIELD( m_bFiringSecondary, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bFiringPrimary, FIELD_BOOLEAN ),

	DEFINE_FIELD( m_hLastGroundEnt, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hActiveSculpture, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hFreezingObject, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hPrevExtrudePos, FIELD_EHANDLE),
	DEFINE_FIELD( m_vecGestureNormal, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecGestureDelta, FIELD_VECTOR ),

	DEFINE_FIELD( m_vecProbeStart, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecProbeForward, FIELD_VECTOR ),
	
	DEFINE_FIELD( m_flGestureDist, FIELD_FLOAT ),
	DEFINE_FIELD( m_eSculptState, FIELD_INTEGER ),
	DEFINE_FIELD( m_bPrimaryAttackDebounce, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hProbe, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hSculptBeam, FIELD_EHANDLE ),
	
	DEFINE_FIELD( m_nWarningLevel, FIELD_INTEGER ),
	
	DEFINE_UTLVECTOR( m_PrevSpheres, FIELD_INTEGER ),
	
	DEFINE_FIELD( m_flShatterTime, FIELD_TIME ),

	DEFINE_INPUTFUNC( FIELD_VOID, "OnSculptureVaporized", InputOnSculptureVaporized ),

END_DATADESC()


enum
{
	EFFECT_NONE,
	EFFECT_READY,
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CWeaponIcegun::CWeaponIcegun( void )
{
	m_iClip1 = m_iClip2 = 0;
	m_EffectState = EFFECT_NONE;
	m_pIcegunLoopSound = NULL;
	m_pIceLoopSound = NULL;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CWeaponIcegun::~CWeaponIcegun()
{
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::Precache( void )
{
	PrecacheModel( ICEGUN_SCULPT_BEAM_SPRITE );
	//PrecacheModel( ICEGUN_MODEL );

	PrecacheScriptSound( "Weapon_IceGun.CryoDartShoot" );
	PrecacheScriptSound( "Weapon_IceGun.Active" );
	PrecacheScriptSound( "Weapon_IceGun.Deactivate" );
	PrecacheScriptSound( "Weapon_IceGun.IceLoop" );
	PrecacheScriptSound( "Weapon_Icegun.AmmoLow1" );
	PrecacheScriptSound( "Weapon_Icegun.AmmoLow2" );
	PrecacheScriptSound( "Weapon_Icegun.AmmoLow3" );

	PrecacheScriptSound( "Weapon_Icegun.SculpturePreShatter" );

	PrecacheParticleSystem(	"tube_water" );

	UTIL_PrecacheOther( "cryo_dart" );
	UTIL_PrecacheOther( "ice_probe" );
	UTIL_PrecacheOther( "ice_sculpture" );

	BaseClass::Precache();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::Spawn( void )
{
	BaseClass::Spawn();

	// Need to get close to pick it up
	CollisionProp()->UseTriggerBounds( false );

	m_eAttackState = ATTACK_POST;

	// Point entity for the positional ice building sound
	CBaseEntity *pPrevExtrudePos = static_cast<CPointEntity*>( CreateEntityByName( "info_target" ) );
	pPrevExtrudePos->AddSpawnFlags(0x01);
	DispatchSpawn( pPrevExtrudePos );
	m_hPrevExtrudePos = pPrevExtrudePos;

	Init();
}


void CWeaponIcegun::Think()
{
	BaseClass::Think();

	if ( icegun_simplified_collision_debug.GetBool() )
	{
		CIceSculpture *pSculpture = NULL;
		while ( ( pSculpture = (CIceSculpture *)gEntList.FindEntityByClassname( pSculpture, "ice_sculpture" ) ) != NULL )
		{
			pSculpture->DrawSimplifiedOBB();
		}
	}

	SetNextThink( gpGlobals->curtime + gpGlobals->frametime );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::OnRestore()
{
	BaseClass::OnRestore();

	// Icegun effects disappear through level transition, so
	// just recreate any effects here
	if ( m_EffectState != EFFECT_NONE )
	{
		DoEffect( m_EffectState, NULL );
	}

	Init();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::UpdateOnRemove(void)
{
	DestroyEffects( );

	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	if ( m_pIcegunLoopSound )
	{
		controller.SoundDestroy( m_pIcegunLoopSound );
		m_pIcegunLoopSound = NULL;
	}

	if ( m_pIceLoopSound )
	{
		controller.SoundDestroy( m_pIceLoopSound );
		m_pIceLoopSound = NULL;
	}

	UTIL_Remove( m_hPrevExtrudePos );

	BaseClass::UpdateOnRemove();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponIcegun::Deploy( void )
{
	DoEffect( EFFECT_READY );
	return BaseClass::Deploy();
}


//-----------------------------------------------------------------------------
// Shut down sculpture when we're holstered.
//-----------------------------------------------------------------------------
bool CWeaponIcegun::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	StopSculpting();
	return BaseClass::Holster( pSwitchingTo );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::ItemHolsterFrame( void )
{
	// Must be player held
	if ( GetOwner() && GetOwner()->IsPlayer() == false )
		return;

	// We can't be active
	if ( GetOwner()->GetActiveWeapon() == this )
		return;
}


//-----------------------------------------------------------------------------
// Make sure we set the right skin when equipped.
//-----------------------------------------------------------------------------
void CWeaponIcegun::Equip( CBaseCombatCharacter *pOwner )
{
	BaseClass::Equip( pOwner );
	
	if ( pOwner->IsPlayer() )
	{
		s_pIcegun = this;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::DryFire( void )
{
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	WeaponSound( EMPTY );

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner )
	{
		pOwner->RumbleEffect( RUMBLE_PISTOL, 0, RUMBLE_FLAG_RESTART );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::PrimaryFireEffect( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	
	if ( pOwner == NULL )
		return;

	pOwner->ViewPunch( QAngle(-6, random->RandomInt(-2,2) ,0) );
	
	color32 white = { 245, 245, 255, 32 };
	UTIL_ScreenFade( pOwner, white, 0.1f, 0.0f, FFADE_IN );

	WeaponSound( SINGLE );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::OnOutOfIceAmmo()
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;
	
	// HEV suit - indicate out of ammo condition
	pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 ); 

	m_bPrimaryAttackDebounce = true;
	StopSculpting();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponIcegun::UpdateIceAmmo()
{
	if ( icegun_recycle_ice.GetBool() && IsPlayerIceSurfing() )
	{
		m_iClip1 = MAX( icegun_max_fixed_ice.GetInt() - s_nIceSpheres, 0 );

		int iIceToMelt = m_iClip1 - ( icegun_max_fixed_ice.GetInt() * 0.1f );

		if ( iIceToMelt < 0 )
		{
			iIceToMelt *= -1;

			// Melt all the spheres that live in this sculpture
			CIceSculpture *pOldestSculpture = CIceSculpture::GetOldestIceSculpture();

			while ( iIceToMelt > 0 && pOldestSculpture )
			{
				while ( pOldestSculpture->m_IceSpheres.Count() && iIceToMelt )
				{
					pOldestSculpture->RemoveSphere( 0 );
					iIceToMelt--;
				}

				pOldestSculpture->m_eLifeStatus = ICE_SCULPTURE_LIFE_STATUS_MELTING;
				pOldestSculpture->RecenterAbsOrigin();

				if ( iIceToMelt > 0 )
				{
					pOldestSculpture->m_flCreationTime = FLT_MAX - 1.0f;
					pOldestSculpture = CIceSculpture::GetOldestIceSculpture();
				}
			}
		}

		return true;
	}

	CIceSculpture *pOldestSculpture = CIceSculpture::GetOldestIceSculpture();

	if ( ( m_iClip1 < icegun_max_fixed_ice.GetInt() * 0.1f ) && ( m_nWarningLevel < 3 ) )
	{
		m_nWarningLevel = 3;

		if ( IsPlayerIceSurfing( pOldestSculpture ) )
		{
			EmitSound( "Weapon_Icegun.AmmoLow3" );
		}
	}
	else if ( ( m_iClip1 < 200 ) && ( m_nWarningLevel < 2 ) )
	{
		m_nWarningLevel = 2;

		if ( IsPlayerIceSurfing( pOldestSculpture ) )
		{
			EmitSound( "Weapon_Icegun.AmmoLow2" );
		}
	}
	else if ( ( m_iClip1 < 300 ) && ( m_nWarningLevel < 1 ) )
	{
		m_nWarningLevel = 1;

		if ( IsPlayerIceSurfing( pOldestSculpture ) )
		{
			EmitSound( "Weapon_Icegun.AmmoLow1" );
		}
	}
	else if ( m_iClip1 >= 300 )
	{
		m_nWarningLevel = 0;
	}

	if ( icegun_auto_reload.GetBool() && ( m_iClip1 < icegun_max_fixed_ice.GetInt() * 0.1f ) )
	{
		Reload();
	}
	
	m_iClip1 = MAX( icegun_max_fixed_ice.GetInt() - s_nIceSpheres, 0 );
	if ( !m_iClip1 )
		return false;

	return true;
}
	

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::SubtractAmmo( int iType )
{
	// HACK: to test a fixed pool of ice ammo
	if ( ( iType == WEAPONIZER_PARAM_PRIMARY ) && ( !icegun_regen_ice.GetBool() ) )
		return;

	int i1 = m_iClip1;
	int i2 = m_iClip2;
	
	Assert( ( iType == WEAPONIZER_PARAM_PRIMARY ) || ( iType == WEAPONIZER_PARAM_SECONDARY ) || ( iType == WEAPONIZER_PARAM_FUSION ) );
	
	m_iClip1 = MAX( m_iClip1 - m_weaponParams[iType].flPrimaryAmmoBurnRate, 0 );
	m_iClip2 = MAX( m_iClip2 - m_weaponParams[iType].flSecondaryAmmoBurnRate, 0 );
	
	if ( !m_iClip1 && ( m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flPrimaryAmmoBurnRate != 0 ) )
	{
		m_bFiringPrimary = false;
	}
	
	if ( !m_iClip2 && ( m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flSecondaryAmmoBurnRate != 0 ) )
	{
		m_bFiringSecondary = false;
	}

	if ( ( i1 && !m_iClip1 ) || ( i2 && !m_iClip2 ) )
	{
		if ( i1 && !m_iClip1 )
		{
			OnOutOfIceAmmo();
		}

		if ( i2 && !m_iClip2 )
		{
		}
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::SetSkin( int skinNum )
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
// Cryo darts. They numb the enemy into submission!!
//-----------------------------------------------------------------------------
#define CRYODART_AIR_VELOCITY			4000

static const char *CRYODART_MODEL = "models/weapons/hunter_flechette.mdl";
static const char *s_szCryoDartBubbles = "CryoDartBubbles";
static const char *s_szCryoDartSpriteTrail = "sprites/bluelaser1.vmt";
static int s_nCryoDartImpact = -2;
static int s_nCryoDartFuseAttach = -1;

class CCryoDart : public CPhysicsProp
{
	DECLARE_CLASS( CCryoDart, CPhysicsProp );

public:

	CCryoDart();
	~CCryoDart();

	Class_T Classify() { return CLASS_NONE; }

public:

	void Spawn();
	void Activate();
	void Precache();
	void Shoot( Vector &vecVelocity, bool bBright );
	void SetSeekTarget( CBaseEntity *pTargetEntity );

	bool CreateVPhysics();

	unsigned int PhysicsSolidMaskForEntity() const;
	static CCryoDart *CryoDartCreate( const Vector &vecOrigin, const QAngle &angAngles, CBaseEntity *pentOwner = NULL );

protected:

	void SetupGlobalModelData();

	void StickTo( CBaseEntity *pOther, trace_t &tr );

	void BubbleThink();
	void DopplerThink();
	bool CreateSprites( bool bBright );
	void CryoDartTouch( CBaseEntity *pOther );

	DECLARE_DATADESC();
	//DECLARE_SERVERCLASS();
};

LINK_ENTITY_TO_CLASS( cryo_dart, CCryoDart );

BEGIN_DATADESC( CCryoDart )

DEFINE_THINKFUNC( BubbleThink ),
DEFINE_THINKFUNC( DopplerThink ),

DEFINE_ENTITYFUNC( CryoDartTouch ),
END_DATADESC()

//IMPLEMENT_SERVERCLASS_ST( CCryoDart, DT_CryoDart )
//END_SEND_TABLE()


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CCryoDart *CCryoDart::CryoDartCreate( const Vector &vecOrigin, const QAngle &angAngles, CBaseEntity *pentOwner )
{
	// Create a new entity with CCryoDart private data
	CCryoDart *pDart = (CCryoDart *)CreateEntityByName( "cryo_dart" );
	UTIL_SetOrigin( pDart, vecOrigin );
	pDart->SetAbsAngles( angAngles );
	pDart->Spawn();
	pDart->Activate();
	pDart->SetOwnerEntity( pentOwner );

	return pDart;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void ShootCryoDart( Vector vOrigin, QAngle angles, CBaseEntity *pOwner )
{
	//MDLCACHE_CRITICAL_SECTION();

	CBasePlayer *pPlayer = (CBasePlayer*)pOwner;

	CCryoDart *entity = CCryoDart::CryoDartCreate( vOrigin, angles, pPlayer );
	if ( entity )
	{
		entity->Precache();
		DispatchSpawn( entity );

		// Shoot the dart.
		Vector forward;
		pPlayer->EyeVectors( &forward );
		entity->Shoot( forward * CRYODART_AIR_VELOCITY, false );

		QAngle angles;

		VectorAngles( forward, angles );

		entity->SetAbsAngles( angles );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CCryoDart::CCryoDart()
{
	UseClientSideAnimation();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CCryoDart::~CCryoDart()
{
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CCryoDart::CreateVPhysics()
{
	// Create the object in the physics system
	VPhysicsInitNormal( SOLID_BBOX, FSOLID_NOT_STANDABLE, false );

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
unsigned int CCryoDart::PhysicsSolidMaskForEntity() const
{
	return ( BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_HITBOX ) & ~CONTENTS_GRATE;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CCryoDart::CreateSprites( bool bBright )
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
void CCryoDart::Spawn()
{
	Precache( );

	SetModel( CRYODART_MODEL );
	SetMoveType( MOVETYPE_FLY, MOVECOLLIDE_FLY_CUSTOM );
	UTIL_SetSize( this, -Vector(1,1,1), Vector(1,1,1) );
	SetSolid( SOLID_BBOX );
	SetCollisionGroup( COLLISION_GROUP_PROJECTILE );
	AddEffects( EF_NOSHADOW | EF_NORECEIVESHADOW );

	// Make sure we're updated if we're underwater
	UpdateWaterState();

	SetTouch( &CCryoDart::CryoDartTouch );

	// Make us glow until we've hit the wall
	m_nSkin = 1;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CCryoDart::Activate()
{
	BaseClass::Activate();
	SetupGlobalModelData();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CCryoDart::SetupGlobalModelData()
{
	if ( s_nCryoDartImpact == -2 )
	{
		s_nCryoDartImpact = LookupSequence( "impact" );
		s_nCryoDartFuseAttach = LookupAttachment( "attach_fuse" );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CCryoDart::Precache()
{
	PrecacheModel( CRYODART_MODEL );
	PrecacheModel( "sprites/light_glow02_noz.vmt" );

	PrecacheScriptSound( "Weapon_IceGun.CryoDartNearmiss" );
	PrecacheScriptSound( "Weapon_IceGun.CryoDartHitBody" );
	PrecacheScriptSound( "Weapon_IceGun.CryoDartHitWorld" );

	PrecacheParticleSystem( "hunter_flechette_trail_striderbuster" );
	PrecacheParticleSystem( "hunter_flechette_trail" );
	PrecacheParticleSystem( "ice_shatter" );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CCryoDart::StickTo( CBaseEntity *pOther, trace_t &tr )
{
	EmitSound( "Weapon_IceGun.CryoDartHitWorld" );

	// Bullseyes don't have valid bones so don't attach to them because it causes
	// a crash in bone setup.
	if ( !pOther->IsWorld() && ( pOther->Classify() != CLASS_BULLSEYE ) )
	{
		SetParent( pOther );
		SetSolid( SOLID_NONE );
		SetSolidFlags( FSOLID_NOT_SOLID );
		SetMoveType( MOVETYPE_NONE );
	}
	
	// TODO: impact effects & sound
	
	Vector vecVelocity = GetAbsVelocity();

	SetTouch( NULL );

	// We're no longer flying. Stop checking for water volumes.
	SetContextThink( NULL, 0, s_szCryoDartBubbles );

	// Play our impact animation.
	ResetSequence( s_nCryoDartImpact );

	static int s_nImpactCount = 0;
	s_nImpactCount++;
	if ( s_nImpactCount & 0x01 )
	{
		UTIL_ImpactTrace( &tr, DMG_BULLET );

		// Shoot some sparks
		if ( UTIL_PointContents( GetAbsOrigin(), CONTENTS_WATER ) != CONTENTS_WATER)
		{
			g_pEffects->Sparks( GetAbsOrigin() );
		}
	}

	SetAbsVelocity( vec3_origin );
	SetMoveType( MOVETYPE_NONE );
	SetSolid( SOLID_NONE );

	SetThink( &CCryoDart::SUB_FadeOut );
	SetNextThink( gpGlobals->curtime + cryodart_fade_time.GetFloat() );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CCryoDart::CryoDartTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet(FSOLID_VOLUME_CONTENTS | FSOLID_TRIGGER) )
	{
		// Some NPCs are triggers that can take damage (like antlion grubs). We should hit them.
		if ( ( pOther->m_takedamage == DAMAGE_NO ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) )
			return;
	}

	if ( FClassnameIs( pOther, "cryo_dart" ) )
		return;

	trace_t	tr;
	tr = BaseClass::GetTouchTrace();

	if ( pOther->m_takedamage != DAMAGE_NO )
	{
		Vector	vecNormalizedVel = GetAbsVelocity();

		ClearMultiDamage();
		VectorNormalize( vecNormalizedVel );

		float flDamage = sk_plr_dmg_cryodart.GetFloat();
		CBreakable *pBreak = dynamic_cast <CBreakable *>(pOther);
		if ( pBreak && ( pBreak->GetMaterialType() == matGlass ) )
		{
			flDamage = MAX( pOther->GetHealth(), flDamage );
		}

		CTakeDamageInfo	dmgInfo( this, GetOwnerEntity(), flDamage, DMG_DISSOLVE | DMG_NEVERGIB );
		CalculateMeleeDamageForce( &dmgInfo, vecNormalizedVel, tr.endpos, 0.7f );
		dmgInfo.SetDamagePosition( tr.endpos );
		pOther->DispatchTraceAttack( dmgInfo, vecNormalizedVel, &tr );
		
		ApplyMultiDamage();

		// Keep going through breakable glass.
		if ( pOther->GetCollisionGroup() == COLLISION_GROUP_BREAKABLE_GLASS )
			return;

		SetAbsVelocity( Vector( 0, 0, 0 ) );

		// play body "thwack" sound
		EmitSound( "Weapon_IceGun.CryoDartHitBody" );

		StopParticleEffects( this );

		Vector vForward;
		AngleVectors( GetAbsAngles(), &vForward );
		VectorNormalize ( vForward );

		trace_t	tr2;
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

				//DispatchEffect( "BoltImpact", data );
			}
		}

		if ( ( ( pOther->GetMoveType() == MOVETYPE_VPHYSICS ) || ( pOther->GetMoveType() == MOVETYPE_PUSH ) ) && ( ( pOther->GetHealth() > 0 ) || ( pOther->m_takedamage == DAMAGE_EVENTS_ONLY ) ) )
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
			SetTouch( NULL );
			SetThink( NULL );
			SetContextThink( NULL, 0, s_szCryoDartBubbles );

			UTIL_Remove( this );
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

			UTIL_Remove( this );
		}
	}
}


//-----------------------------------------------------------------------------
// Play a near miss sound as we travel past the player.
//-----------------------------------------------------------------------------
void CCryoDart::DopplerThink()
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
		EmitSound( "Weapon_IceGun.CryoDartNearMiss" );

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
void CCryoDart::BubbleThink()
{
	SetNextThink( gpGlobals->curtime + 0.1f, s_szCryoDartBubbles );

	if ( GetWaterLevel()  == 0 )
		return;

	UTIL_BubbleTrail( GetAbsOrigin() - GetAbsVelocity() * 0.1f, GetAbsOrigin(), 5 );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CCryoDart::Shoot( Vector &vecVelocity, bool bBrightFX )
{
	CreateSprites( bBrightFX );

	SetAbsVelocity( vecVelocity );

	SetThink( &CCryoDart::DopplerThink );
	SetNextThink( gpGlobals->curtime );

	SetContextThink( &CCryoDart::BubbleThink, gpGlobals->curtime + 0.1, s_szCryoDartBubbles );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::ShootIce( attackstate_t eState )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	// Kick the view a little
	QAngle viewPunch;
	viewPunch.x = random->RandomFloat( 0.25f, 0.5f );
	viewPunch.y = random->RandomFloat( -.6f, .6f );
	viewPunch.z = 0.0f;
	pPlayer->ViewPunch( viewPunch );

	SendWeaponAnim( ACT_VM_SECONDARYATTACK );

	pPlayer->GetActiveWeapon()->EmitSound( "Weapon_IceGun.CryoDartShoot" );

	QAngle angEye = pPlayer->EyeAngles();
	Vector vForward, vRight, vUp;
	pPlayer->EyeVectors( &vForward, &vRight, &vUp );

	Vector vOrigin = pPlayer->EyePosition(); //Weapon_ShootPosition() + vForward * 48.0f + vRight * 8 + vUp * -8.25f;

	ShootCryoDart( vOrigin, angEye, pPlayer );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponIcegun::HasValidGesture()
{
	return ( ( m_eSculptState == SCULPT_STATE_EXTRUDE ) && ( m_hActiveSculpture != NULL ) && ( m_hActiveSculpture->m_vecBaseOrigins.Count() > 0 ) );
}


//-----------------------------------------------------------------------------
// Figure out which direction the player is trying to extrude the original gesture shape.
//-----------------------------------------------------------------------------
void CWeaponIcegun::GetExtrudeDirPlanar( const Vector &start, const Vector &forward, const Vector &vecRefPoint, Vector &vecSurfaceBuilderDir )
{
	cplane_t plane;
	
	if ( HasValidGesture() )
	{
		// Generate the plane normal based on the surface normal and the gesture
		// direction. This gives us a plane which lies along the surface normal.
		Vector vecGestureDir;
		GetSculptGestureDir( vecGestureDir );
		
		plane.normal = CrossProduct( vecGestureDir, m_vecGestureNormal );
		VectorNormalize( plane.normal );
	}
	else
	{
		Assert( false );
		plane.normal = forward;
		plane.normal.z = 0;
		VectorNormalize( plane.normal );
	}
	
	// If the plane is too close to the eye we lose precision due to foreshortening, so 
	// eliminate the degree of freedom along the foreshortening.	
	float flDist = DotProduct( start, plane.normal ) - DotProduct( vecRefPoint, plane.normal );
	if ( fabs(flDist) < icegun_min_eye_dist.GetFloat() )
	{
		vecSurfaceBuilderDir = m_vecGestureNormal;
		return;
	}
	
	plane.dist = DotProduct( vecRefPoint, plane.normal );
	float flPlaneDist = IntersectRayWithPlane( start, forward, plane.normal, plane.dist );
	
	if ( icegun_debug.GetBool() )
	{
		debugoverlay->AddBoxOverlay( start + forward * flPlaneDist, Vector( -1, -1, -1 ), Vector( 1, 1, 1 ), QAngle( 0, 0, 0 ), 255, 255, 255, true, 0.1 );
		debugoverlay->AddLineOverlay( start + forward * flPlaneDist, start + forward * flPlaneDist + Vector( 0, 0, -100 ), 255, 255, 255, false, 0.1 );
	}
		
	vecSurfaceBuilderDir = ( start + forward * flPlaneDist ) - vecRefPoint;
	if ( vecSurfaceBuilderDir.Length() < 4.0f )
	{
		// near dead-center reflect back towards the muzzle
		//vecSurfaceBuilderDir = start - vecRefPoint;
	}	
	
	// If the player moved, add their motion vector to the build direction.
	static Vector vecPlayerPos = Vector( 0, 0, 0 );
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	if ( vecPlayerPos != vec3_origin )
	{
		if ( icegun_autoramp.GetBool() && ( pPlayer->m_afButtonLast & IN_FORWARD ) && ( DotProduct( m_vecGestureNormal, Vector( 0, 0, 1 ) ) > 0.8 ) )
		{
			Vector forward;
			pPlayer->GetVectors( &forward, NULL, NULL );
			
			VectorNormalize( vecSurfaceBuilderDir );
			vecSurfaceBuilderDir += forward;
			vecSurfaceBuilderDir.z = 0;
			VectorNormalize( vecSurfaceBuilderDir );
			vecSurfaceBuilderDir.z = 0.3;
		}
		else
		{
			Vector vecDelta = pPlayer->GetAbsOrigin() - vecPlayerPos;
			if ( vecDelta.LengthSqr() > 1 )
			{
				VectorNormalize( vecDelta );
				VectorNormalize( vecSurfaceBuilderDir );

				vecSurfaceBuilderDir += vecDelta;
			}
		}
	}
	vecPlayerPos = pPlayer->GetAbsOrigin();
	
	VectorNormalize( vecSurfaceBuilderDir );
}


//-----------------------------------------------------------------------------
// Add the next sphere at the appropriate position.
//-----------------------------------------------------------------------------
icesphere_t *CWeaponIcegun::CreateIceSphere( const Vector &vecNewSpherePos, CIceSculpture *pSculpture, int *pnIndex )
{
	//
	// Look for entities to glom onto.
	//
	Vector vecSphereRadius( ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS );
	const int SURFACE_SEARCH_SPHERES = 50;
	CBaseEntity *pEntities[SURFACE_SEARCH_SPHERES];
	int nCount = UTIL_EntitiesInBox( pEntities, SURFACE_SEARCH_SPHERES, vecNewSpherePos - vecSphereRadius, vecNewSpherePos + vecSphereRadius, 0 );

	for ( int i = 0; i < nCount; i++ )
	{
		if ( FClassnameIs( pEntities[i], "ice_sculpture" ) )
		{
			if ( pSculpture != pEntities[i] )
			{
				Ray_t ray;
				ray.Init( vecNewSpherePos, vecNewSpherePos, -vecSphereRadius, vecSphereRadius );

				trace_t tr;
				pEntities[i]->TestCollision( ray, MASK_SOLID, tr );

				if ( tr.DidHit() )
				{
					// Touched another sculpture, so let's merge them together
					CIceSculpture *pOldSculpture = static_cast<CIceSculpture*>( pEntities[i] );

					int iSphere = pSculpture->m_IceSpheres.Count();
					pSculpture->m_IceSpheres.AddMultipleToTail( pOldSculpture->m_IceSpheres.Count() );

					for ( int iOldSphere = pOldSculpture->m_IceSpheres.Count() - 1; iOldSphere >= 0; --iOldSphere, ++iSphere )
					{
						// Save off the sphere's important data
						icesphere_t *pOldIceSphere = &( pOldSculpture->m_IceSpheres[ iOldSphere ] );

						// Add the new sphere
						icesphere_t *pIceSphere = &( pSculpture->m_IceSpheres[ iSphere ] );

						// Copy over data from the old sphere
						pIceSphere->m_bRootedToWorld = pOldIceSphere->m_bRootedToWorld;
						pIceSphere->m_vecPos = pOldIceSphere->m_vecPos;
						pIceSphere->m_vecExtrudeDir = pOldIceSphere->m_vecExtrudeDir;
						pIceSphere->m_vecPrevDelta = pOldIceSphere->m_vecPrevDelta;
						pIceSphere->m_vecNextDelta = pOldIceSphere->m_vecNextDelta;
						pIceSphere->m_vecMid = pOldIceSphere->m_vecMid;
						pIceSphere->m_vecNextMid = pOldIceSphere->m_vecNextMid;
						pIceSphere->m_iHealth = pOldIceSphere->m_iHealth;
						pIceSphere->m_flDieTime = pOldIceSphere->m_flDieTime;

						// Do all the book keeping
						pOldSculpture->RemoveSphere( iOldSphere, true );
						pSculpture->AddSphere( iSphere );
					}

					pOldSculpture->m_IceSpheres.RemoveAll();
					UTIL_Remove( pOldSculpture );
					pSculpture->RecenterAbsOrigin();
				}
			}
		}
		else if ( !FClassnameIs( pEntities[i], "ice_probe" ) && !FClassnameIs( pEntities[i], "physics_prop_statue" ) )
		{
			// It's not an ice probe, so check if it's a physics prop or NPC
			if ( IsMovablePhysicsObject( pEntities[i] ) )
			{
				// Physics objects become embedded!
				IPhysicsObject *pPhys = pEntities[i]->VPhysicsGetObject();

				if ( pPhys )
				{
					pPhys->EnableMotion( false );

					CPhysicsProp *pObject = dynamic_cast<CPhysicsProp*>( pEntities[i] );
					if ( pObject )
					{
						pObject->Freeze( 0.5f, GetOwner() );
					}
				}
			}
			else if ( FClassnameIs( pEntities[i], "func_breakable" ) )
			{
				// If we ice surfed into a breakable while going fast enough, kill them!
				Vector vecVel = GetOwner()->GetAbsVelocity();
				if ( IsPlayerIceSurfing( pSculpture ) && ( vecVel.Length() > icegun_ramming_speed.GetFloat() ) )
				{
					CBreakable *pBreakable = dynamic_cast<CBreakable*>( pEntities[i] );
					pBreakable->Break( GetOwner() );
				}
			}
			else
			{
				CAI_BaseNPC *pNPC = dynamic_cast<CAI_BaseNPC*>( pEntities[i] );
				
				if ( pNPC && ( pNPC->Classify() != CLASS_BULLSEYE ) )
				{
					// If we ice surfed into an NPC while going fast enough, kill them!
					Vector vecVel = GetOwner()->GetAbsVelocity();
					if ( pNPC->VPhysicsGetObject() && IsPlayerIceSurfing( pSculpture ) && ( vecVel.Length() > icegun_ramming_speed.GetFloat() ) )
					{
						Vector vecForce = vecVel * icegun_ramming_force.GetFloat() * pNPC->VPhysicsGetObject()->GetMass();
						CTakeDamageInfo info( GetOwner(), GetOwner(), vecForce, vecNewSpherePos, pNPC->GetHealth(), DMG_VEHICLE );
						pNPC->TakeDamage( info );
					}
					else
					{
						// Our ramp is building near a freezable object, latch on it instead
						m_hFreezingObject = pNPC;

						if ( m_hProbe )
						{
							m_hProbe->Detach();
							m_hProbe = NULL;
						}

						m_eSculptState = SCULPT_STATE_FREEZE_OBJECT;

						return NULL;
					}
				}
			}
		}
	}

	//
	// See if there's a sphere already in the sculpture that's close enough
	// to do the job.
	//
	float flMinDistSq = icegun_mindistsq.GetFloat();
	nCount = pSculpture->GetSphereCount();
	for ( int i = 0; i < nCount; i++ )
	{
		icesphere_t *pSphere = pSculpture->GetSphere( i );
		float flDistSq = pSphere->m_vecPos.DistToSqr( vecNewSpherePos );
		if ( flDistSq < flMinDistSq )
		{
			// No need to create a sphere, just move the existing sphere.
			if ( icegun_debug.GetBool() )
			{
				Msg( "Too close, didn't create sphere: %x\n", pSphere );
			}

			m_pRefSphere = pSphere;

			if ( pnIndex )
			{
				*pnIndex = i;
			}
			
			return pSphere;
		}
	}
	
	//
	// Okay, we're going to actually create a new sphere.
	//
	int iSphere = pSculpture->m_IceSpheres.AddToTail();
	if ( pnIndex )
	{
		*pnIndex = iSphere;
	}
	
	icesphere_t *pIceSphere = &(pSculpture->m_IceSpheres[iSphere]);
	pIceSphere->m_vecPos = vecNewSpherePos;
	m_pRefSphere = pIceSphere;

	if ( m_pIceLoopSound == NULL )
	{
		// Positional ice growing sound
		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		CPASAttenuationFilter filter( m_hPrevExtrudePos.Get() );
		m_pIceLoopSound = controller.SoundCreate( filter, m_hPrevExtrudePos->entindex(), "Weapon_IceGun.IceLoop" );
		controller.Play( m_pIceLoopSound, 1.0, 90 );

		m_flSphereCreationSpeed = 0.15f;
	}
	else
	{
		CSoundEnvelopeController::GetController().SoundChangeVolume( m_pIceLoopSound, 1.0, 0.10 );
	}

	// Update the sound to know the player is creating spheres
	m_flSphereCreationSpeed = MIN( m_flSphereCreationSpeed + 0.1f, 1.0f );

	if ( icegun_debug.GetBool() )
	{
		Msg( "Created sphere #%d: %x\n", s_nIceSpheres, pIceSphere );
	}

	return pIceSphere;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::GetSculptGestureDir( Vector &vecGestureDir )
{
	m_hActiveSculpture->GetSculptureGestureDir( vecGestureDir );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponIcegun::CanCreateIceSphere( const Vector &vecPos )
{
	Vector vecSphereRadius( ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS );

	// Don't create spheres that will intersect the player.
	// FIXME: create a temp sphere and measure its bbox
	Vector vecMins = vecPos - vecSphereRadius;
	Vector vecMaxs = vecPos + vecSphereRadius;

	Vector vecPlayerMins;
	Vector vecPlayerMaxs;
	GetOwner()->CollisionProp()->WorldSpaceAABB( &vecPlayerMins, &vecPlayerMaxs );
	if ( IsBoxIntersectingBox(vecMins, vecMaxs,  vecPlayerMins, vecPlayerMaxs) )
		return false;
		
	//trace_t tr;
	//UTIL_TraceHull( vecPos, vecPos, -vecSphereRadius, vecSphereRadius, MASK_SOLID, NULL, &tr );
	//if ( tr.allsolid )
	//{
	//	debugoverlay->AddBoxOverlay( vecPos, -vecSphereRadius, vecSphereRadius, QAngle( 0, 0, 0 ), 255, 0, 0, 0, 10.0f );
	//	return false;
	//}
		
	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::GetBuildVectors( const Vector &forward, const Vector &right, const Vector &vecNormal, Vector &vecBuildRight, Vector &vecBuildUp )
{
	//
	// Derive build vectors from the view direction and surface normal so
	// that we build a line along the surface from view left to view right.
	//
	vecBuildUp = CrossProduct( vecNormal, right );
	VectorNormalize( vecBuildUp );

	vecBuildRight = CrossProduct( vecBuildUp, vecNormal );
	VectorNormalize( vecBuildRight );

	if ( fabs( vecNormal.z ) < 0.7 )
	{
		Vector vecTemp = vecBuildRight;
		vecBuildRight = vecBuildUp;
		vecBuildUp = vecTemp;
	}
}


//-----------------------------------------------------------------------------
// width - width in # spheres
//-----------------------------------------------------------------------------
void CWeaponIcegun::BeginSculpture( const Vector &forward, const Vector &right, const Vector &vecPos, const Vector &vecNormal, int width )
{
	Vector vecBuildRight;
	Vector vecBuildUp;
	GetBuildVectors( forward, right, vecNormal, vecBuildRight, vecBuildUp );

	// Create a new sculpture
	CIceSculpture *pSculpture = CIceSculpture::CreateIceSculpture( vecPos, vecNormal );
	if ( !pSculpture )
		return;
	
	m_hActiveSculpture = pSculpture;

	icesphere_t *pMiddleSphere = NULL;

	float flSpacing = icegun_sphere_spacing.GetFloat();
	Vector vecNewPos = vecPos - ( vecBuildRight * width * flSpacing );
	for ( int i = 0; i < width; i++ )
	{
		if ( !icegun_regen_ice.GetBool() )
		{
			if ( !UpdateIceAmmo() )
			{
				OnOutOfIceAmmo();
				return;
			}
		}

		if ( icegun_debug.GetBool() )
		{
			Msg( "BeginSculpture: %f %f %f\n", vecNewPos.x, vecNewPos.y, vecNewPos.z );
		}
		
		if ( !CanCreateIceSphere( vecNewPos ) )
			continue;
	
		int nIndex = -1;
		icesphere_t *pSphere = CreateIceSphere( vecNewPos, pSculpture, &nIndex );
		if ( !pSphere )
			continue;
		
		if ( i == ( width / 2 ) )
		{
			pMiddleSphere = pSphere;
		}
		
		pSculpture->AddBaseSphere( nIndex );

		vecNewPos += vecBuildRight * 2.0f * flSpacing;
	}

	pSculpture->RecenterAbsOrigin();

	if ( pMiddleSphere != NULL )
	{
		m_pRefSphere = pMiddleSphere;
	}

	m_hPrevExtrudePos->SetAbsOrigin( vecPos );
	m_eSculptState = SCULPT_STATE_EXTRUDE;
}


//-----------------------------------------------------------------------------
// Are we standing on the sculpture that we're building?
//-----------------------------------------------------------------------------
bool CWeaponIcegun::IsPlayerIceSurfing( CIceSculpture *pSculpture )
{
	if ( !GetOwner() )
		return false;

	CBaseEntity *pGroundEnt = GetOwner()->GetGroundEntity();
	CBaseEntity *pCurGroundEnt = pGroundEnt;

	if ( !pGroundEnt )
	{
		pGroundEnt = m_hLastGroundEnt;
	}
	else
	{
		m_hLastGroundEnt = pGroundEnt;
	}

	if ( !pSculpture )
	{
		// Not checking a specific sculpture, check the one that we're building
		pSculpture = m_hActiveSculpture;
	}
	
	if ( pGroundEnt && FClassnameIs( pGroundEnt, "ice_sculpture" ) )
	{
		// If we're standing on the sculpture, we're surfing
		CIceSculpture *pGroundSculpture = static_cast<CIceSculpture *>( pGroundEnt );
		if ( pGroundSculpture == pSculpture )
			return true;
		
		// If we're airborne and sculpting and were last standing on a sculpture,
		// we're probably surfing.
		// FIXME: handle jumping off the world and sculpting while airborne
		if ( !pCurGroundEnt && ( m_eSculptState != SCULPT_STATE_NONE ) )
			return true;
	}

	return false;		
}


//-----------------------------------------------------------------------------
// Weak heuristic for determining whether we should apply ramp limiting to the
// current sculpture.
//-----------------------------------------------------------------------------
bool CWeaponIcegun::ShouldLimitSlope()
{
	if ( IsPlayerIceSurfing() )
		return true;
		
	// If the player is walking forward assume they are trying to build a ramp.
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( pPlayer && pPlayer->m_afButtonLast & IN_FORWARD )
		return true;

	return false;		
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::GetMuzzlePos( Vector &vecMuzzle )
{
	// HACK: FIXME:
	Vector viewforward, viewright, viewup;
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	pPlayer->EyeVectors( &viewforward, &viewright, &viewup );
	vecMuzzle = pPlayer->Weapon_ShootPosition() + viewforward * 24.0f + viewright * 8 + viewup * -8.25f;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::LinkSpheresIntoSculpture( CUtlVector<int> &spheres, const Vector &vecDelta )
{
	// FIXME: PASS IN THE SCULPTURE?
	CIceSculpture *pSculpture = m_hActiveSculpture;
	if ( !pSculpture )
		return;

	int nIndex;

	int nCurCount = spheres.Count();
	if ( nCurCount > 0 )
	{
		// Find the middle of the spheres we just built
		nIndex = spheres.Element( spheres.Count() / 2 );
		icesphere_t *pMid = pSculpture->GetSphere( nIndex );

		// Set the the midpoint for the spheres we just built
		for ( int i = 0; i < nCurCount; i++ )
		{
			nIndex = spheres.Element( i );
			icesphere_t *pCurSphere = pSculpture->GetSphere( nIndex );
			
			//debugoverlay->AddBoxOverlay( pCurSphere->GetAbsOrigin(), Vector(-1,-1,-1), Vector(1,1,1), QAngle(0,0,0), 0, 0, 255, 255, 60 );
			pCurSphere->m_vecMid = pMid->m_vecPos;
		}

		int nPrevCount = m_PrevSpheres.Count();

		// Link the spheres we just built to the last spheres we built
		for ( int i = 0; i < nPrevCount; i++ )
		{
			nIndex = m_PrevSpheres.Element( i );
			icesphere_t *pPrevSphere = pSculpture->GetSphere( nIndex );

			if ( pPrevSphere )
			{
				pPrevSphere->m_vecNextDelta = vecDelta;

				if ( icegun_debug_flow.GetBool() )
				{
					debugoverlay->AddLineOverlay( pPrevSphere->m_vecPos, pPrevSphere->m_vecPos + pPrevSphere->m_vecNextDelta * 8, 255, 0, 255, 255, 60 );
				}

				pPrevSphere->m_vecNextMid = pMid->m_vecPos;
			}
		}

		// Replace prev spheres with the ones we just built
		m_PrevSpheres.RemoveAll();
		for ( int i = 0; i < nCurCount; i++ )
		{
			m_PrevSpheres.AddToTail( spheres.Element( i ) );
		}

		m_pRefSphere = pMid;
	}
}


//-----------------------------------------------------------------------------
// width - width in # spheres
//-----------------------------------------------------------------------------
void CWeaponIcegun::ExtrudeSpherical( const Vector &start, const Vector &forward, const Vector &right, const Vector &vecPos, const Vector &vecNormal, int width )
{
	Vector vecBuildRight;
	Vector vecBuildUp;
	GetBuildVectors( forward, right, vecNormal, vecBuildRight, vecBuildUp );

	if ( icegun_debug.GetBool() )
	{
		debugoverlay->AddBoxOverlay( vecPos, Vector(-4,-4,-4), Vector(4,4,4), QAngle(0,0,0), 255, 255, 255, 0, 10.0f );
		debugoverlay->AddLineOverlay( vecPos, vecPos + vecBuildRight * 50, 0, 255, 0, 0, 10.0f );
		debugoverlay->AddLineOverlay( vecPos, vecPos + vecBuildUp * 50, 0, 0, 255, 0, 10.0f );
	}

	Vector vecExtrudePos = vecPos;
	Vector vecPrevExtrudePos = m_hPrevExtrudePos->GetAbsOrigin();

	if ( IsPlayerIceSurfing() )
	{
		// Do ramp slope limiting when the player is surfing on the ice
		// FIXME: disallow folding the ramp back underneath the current ramp thusly:
		//
		//			___P__
		//				 /
		//				/___
		//
		//			P = player
		//
		Vector vecDelta = vecExtrudePos - vecPrevExtrudePos;
		float flDelta2D = vecDelta.Length2D();

		float flSlopeDeg = RAD2DEG( atan2( vecDelta.z, flDelta2D ) );
		float flMaxUpSlope = icegun_ramp_upslope.GetFloat();

		if ( flSlopeDeg > 0.0f )
		{
			// Prevent surfing into the ceiling
			trace_t tr;
			CTraceFilterSkipClassname filter( GetOwner(), "ice_sculpture", COLLISION_GROUP_NONE );

			// Trace strait up
			UTIL_TraceLine( vecExtrudePos, vecExtrudePos + Vector( 0.0f, 0.0f, 128.0f), (CONTENTS_SOLID|CONTENTS_MOVEABLE|CONTENTS_WINDOW|CONTENTS_GRATE), &filter, &tr );

			if ( tr.fraction != 1.0f )
			{
				// Stop going up! Start going down if we're running out of headroom
				flMaxUpSlope = ( 1.0f - tr.fraction ) * -flMaxUpSlope;
			}
		}

		if ( flSlopeDeg > flMaxUpSlope )
		{
			vecExtrudePos.z = vecPrevExtrudePos.z + tan( DEG2RAD( flMaxUpSlope ) ) * flDelta2D;
		}
		else if ( flSlopeDeg < icegun_ramp_downslope.GetFloat() )
		{
			vecExtrudePos.z = vecPrevExtrudePos.z + tan( DEG2RAD( icegun_ramp_downslope.GetFloat() ) ) * flDelta2D;
		}
		else if ( icegun_debug.GetBool() )
		{
			debugoverlay->AddLineOverlay( vecPrevExtrudePos, vecExtrudePos, 0, 255, 0, true, icegun_sphere_lifetime.GetFloat() );
		}
	}
	else if ( !GetOwner()->GetGroundEntity() )
	{
		// Jumping should not introduce vertical walls in sculptures
		vecExtrudePos.z = vecPrevExtrudePos.z;
	}

	UpdateSculptBeam( forward, vecExtrudePos );
	
	// Build ice from the previous extrude position to the current extrude position.
	Vector vecDelta = vecExtrudePos - vecPrevExtrudePos;
	float flDeltaLength = VectorNormalize( vecDelta );
	float flSpacing = icegun_sphere_spacing.GetFloat() * random->RandomFloat( 0.7, 1.3 );
	float flDeltaBuilt = MIN( flSpacing, flDeltaLength );

	// Indices into the sculpture's array of spheres
	CUtlVector<int> curSpheres;

	CIceSculpture *pSculpture = m_hActiveSculpture;
	if ( !pSculpture )
	{
		DestroySculptBeam();
		m_eSculptState = SCULPT_STATE_NONE;
		return;
	}

	trace_t tr;
	CTraceFilterSkipClassname filter( GetOwner(), "ice_sculpture", COLLISION_GROUP_NONE );

	int nTimesThrough = 0;
	int nIndex = -1;
	do
	{
		vecExtrudePos = vecPrevExtrudePos + vecDelta * flDeltaBuilt;

		Vector vecNewPos = vecExtrudePos - ( width * flSpacing * vecBuildRight );
		
		for ( int i = 0; i < width; i++ )
		{
			if ( !icegun_regen_ice.GetBool() )
			{
				if ( !UpdateIceAmmo() )
				{
					OnOutOfIceAmmo();
					return;
				}
			}

			UTIL_TraceLine( vecPrevExtrudePos + i * 2.0f * flSpacing * vecBuildRight, vecNewPos, MASK_SOLID_BRUSHONLY, &filter, &tr );
			if ( tr.fraction != 1.0 )
			{
				if ( tr.fraction < 0.05 )
				{
					UTIL_TraceLine( start, vecNewPos, MASK_SOLID_BRUSHONLY, &filter, &tr );
				}
				
				vecNewPos = tr.endpos;
			}

			if ( !CanCreateIceSphere( vecNewPos ) )
				continue;

			icesphere_t *pSphere = CreateIceSphere( vecNewPos, pSculpture, &nIndex );
			if ( !pSphere )
				return;

			// If this sphere touched the world mark it as rooted
			if ( tr.fraction < 1.0f )
			{
				pSphere->m_bRootedToWorld = true;
			}

			// Update looping sound
			if ( m_pIceLoopSound )
			{
				m_flSphereCreationSpeed = MAX( m_flSphereCreationSpeed - gpGlobals->frametime * 2.0f, 0.0f );

				CSoundEnvelopeController::GetController().SoundChangePitch( m_pIceLoopSound, m_flSphereCreationSpeed * 40.f + 80.0f, 0.10 );

				if ( m_flSphereCreationSpeed <= 0.0f )
				{
					CSoundEnvelopeController::GetController().SoundChangeVolume( m_pIceLoopSound, 0.0, 0.10 );
				}
			}
				
			// The pointer can move, so only hang onto the index
			curSpheres.AddToTail( nIndex );

			if ( pSphere->m_hSculpture == NULL )
			{
				pSculpture->AddSphere( nIndex );
			}

			pSphere->m_vecExtrudeDir = vecDelta;

			vecNewPos += vecBuildRight * 2.0f * flSpacing;
		}
		
		flDeltaBuilt += flSpacing;

		nTimesThrough++;

		// Link to the previous middle sphere in the sculpture.
		// This builds the flow map to keep players on the ice. Only do it if they are
		// trying to build a walkable ice sculpture.
		if ( ShouldLimitSlope() )
		{
			LinkSpheresIntoSculpture( curSpheres, vecDelta );
		}
		
		curSpheres.RemoveAll();

		if ( nTimesThrough > 1000 )
			Assert( false );

	} while ( flDeltaBuilt <= flDeltaLength );

	pSculpture->RecenterAbsOrigin();

	m_hPrevExtrudePos->SetAbsOrigin( vecExtrudePos );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::ExtrudePlanar( const Vector &vecDir )
{
	Assert( m_hActiveSculpture != NULL );

	Vector vecNewSpherePos;
	Vector vecMins;
	Vector vecMaxs;

	Vector vecPlayerMins;
	Vector vecPlayerMaxs;
	GetOwner()->CollisionProp()->WorldSpaceAABB( &vecPlayerMins, &vecPlayerMaxs );

	float flSpacing = icegun_sphere_spacing.GetFloat();

	icesphere_t *pMiddleSphere = NULL;

	CIceSculpture *pSculpture = m_hActiveSculpture;

	for ( int i = 0; i < pSculpture->m_vecBaseOrigins.Count(); i++ )
	{
		Vector vecStartFrom = m_hActiveSculpture->m_vecBaseOrigins.Element( i );
		vecNewSpherePos = vecStartFrom + vecDir * flSpacing * 2.0f;

		if ( !CanCreateIceSphere( vecNewSpherePos ) )
			continue;

		int nIndex = -1;
		icesphere_t *pSphere = CreateIceSphere( vecNewSpherePos, pSculpture, &nIndex );
		if ( !pSphere )
			continue;

		if ( !icegun_regen_ice.GetBool() )
		{		
			if ( !UpdateIceAmmo() )
			{
				OnOutOfIceAmmo();
				return;
			}
		}
		
		pSculpture->AddSphere( nIndex );

		// HACK: overwrite the base origins
		pSculpture->m_vecBaseOrigins.Element( i ) = vecNewSpherePos;
		
		pSphere->m_vecExtrudeDir = vecDir;

		if ( i >= ( pSculpture->m_vecBaseOrigins.Count() / 2 ) && ( pMiddleSphere == NULL ) )
		{
			pMiddleSphere = pSphere;
		}
	}

	pSculpture->RecenterAbsOrigin();

	if ( pMiddleSphere != NULL )
	{
		m_pRefSphere = pMiddleSphere;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::UpdateSculptBeam( const Vector &forward, const Vector &vecEndPos )
{
	if ( m_hSculptBeam.Get() )
	{
		Vector vecMuzzle;
		GetMuzzlePos( vecMuzzle );
		Vector vecControl = vecMuzzle + forward * 0.5 * m_flGestureDist;

		m_hSculptBeam->SetAbsOrigin( vecMuzzle );
		m_hSculptBeam->SetSpline( vecControl, vecEndPos );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::DestroySculptBeam()
{
	if ( m_hSculptBeam.Get() )
	{
		UTIL_Remove( m_hSculptBeam.Get() );
		m_hSculptBeam = NULL;
	}
}


//-----------------------------------------------------------------------------
// Launches a probe to find a surface to start accumulating ice on.
//-----------------------------------------------------------------------------
void CWeaponIcegun::FireProbe( const Vector &start, const Vector &forward, const Vector &right )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	CIceProbe *pSphere = static_cast<CIceProbe *>(CreateEntityByName( "ice_probe" ));
	if ( pSphere != NULL )
	{
		// Set the model
		pSphere->SetAbsOrigin( start + ( forward * 30.0f ) );
		pSphere->SetAbsAngles( QAngle( 0, 0, 0 ) );
		pSphere->Spawn();
		pSphere->SetOwnerEntity( GetOwner() );

		// Throw it
		pSphere->SetAbsVelocity( forward * ice_probe_velocity.GetFloat() );
	}
	
	if ( m_hProbe != NULL )
	{
		m_hProbe->Detach();
	}
	
	m_hProbe = pSphere;
	m_eSculptState = SCULPT_STATE_WAIT_PROBE;

	m_vecProbeStart = start;
	m_vecProbeForward = forward;

	DestroySculptBeam();

	// Create the beam
	Vector vecControl = 0.5 * ( start + pSphere->GetAbsOrigin() );
	Vector vecMuzzle;
	GetMuzzlePos( vecMuzzle );

	m_hSculptBeam = CreateQuadraticBeam( ICEGUN_SCULPT_BEAM_SPRITE, vecMuzzle, vecControl, pSphere->GetAbsOrigin(), 8.0f, this );
}


//-----------------------------------------------------------------------------
// Polls the probe to see if it has landed.
//-----------------------------------------------------------------------------
ProbeState_t CWeaponIcegun::CheckProbe( const Vector &start, const Vector &forward, Vector &vecRefPoint, Vector &vecNormal )
{
	Vector vecControl;
	Vector vecProbeOrigin;

	ProbeState_t eRet = PROBE_STATE_IN_FLIGHT;
	
	if ( m_hProbe )
	{
		// Probe in flight.
		vecProbeOrigin = m_hProbe->GetAbsOrigin();
		
		eRet = HasProbeLanded( m_vecProbeForward, vecRefPoint, vecNormal );

		if ( ( eRet == PROBE_STATE_LANDED ) || ( eRet == PROBE_STATE_LANDED_ON_SCULPTURE ) )
		{
			m_vecGestureDelta = vecRefPoint - m_vecProbeStart;
			m_flGestureDist = m_vecGestureDelta.Length();
		}
		
		UpdateSculptBeam( forward, vecProbeOrigin );
	}
	else
	{
		// Lost the probe. Probably out of range
		DestroySculptBeam();
		m_eSculptState = SCULPT_STATE_NONE;
		eRet = PROBE_STATE_LOST;
	}

	return eRet;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
ProbeState_t CWeaponIcegun::HasProbeLanded( const Vector &forward, Vector &vecRefPoint, Vector &vecNormal )
{
	if ( !m_hProbe.Get() )
		return PROBE_STATE_LOST;

	if ( m_hProbe->m_bLandedOnObject )
	{
		// If we landed on an object get rid of the probe and remember the object for later
		CBaseAnimating *pObject = dynamic_cast<CBaseAnimating*>( m_hProbe->m_hLandedEnt.Get() );
		m_hFreezingObject = pObject;

		if ( pObject )
		{
			vecRefPoint = pObject->WorldSpaceCenter();
			vecNormal = m_hProbe->m_vecNormal;
		}

		UTIL_Remove( m_hProbe.Get() );
		
		m_eSculptState = SCULPT_STATE_FREEZE_OBJECT;

		// Flew too far, start fading the probe out. When it removes itself
		// we'll detect it as lost.
		Vector vecDelta = m_hProbe->GetAbsOrigin() - GetOwner()->GetAbsOrigin();
		if ( vecDelta.Length() > icegun_max_build_dist.GetFloat() )
		{
			m_hProbe->StartFading();
			return PROBE_STATE_IN_FLIGHT;
		}

		// FIXME: differentiate this
		return PROBE_STATE_IN_FLIGHT;
	}

	if ( m_hProbe->m_bLanded )
	{
		vecRefPoint = m_hProbe->GetAbsOrigin();

		// Might change this below, but this is a good start
		vecNormal = m_hProbe->m_vecNormal;
		
		bool bHandled = false;

		if ( m_hProbe->m_hLandedEnt && FClassnameIs( m_hProbe->m_hLandedEnt, "ice_sculpture" ) )
		{
			//
			// We hit an ice sculpture. 
			//
			if ( icegun_debug.GetBool() )
			{
				Msg( "LANDED ON A SCULPTURE\n" );
			}

			CIceSculpture *pSculpture = assert_cast<CIceSculpture *>( m_hProbe->m_hLandedEnt.Get() );
			Assert( pSculpture != NULL );
			
			if ( !pSculpture )
				return PROBE_STATE_IN_FLIGHT;

			if ( icegun_melt_sculptures.GetInt() > 0 )
			{
				// Flew too far, start fading the probe out. When it removes itself
				// we'll detect it as lost.
				Vector vecDelta = m_hProbe->GetAbsOrigin() - GetOwner()->GetAbsOrigin();
				if ( vecDelta.Length() > icegun_max_melt_dist.GetFloat() )
				{
					m_hProbe->StartFading();
					return PROBE_STATE_IN_FLIGHT;
				}

				m_hActiveSculpture = pSculpture;
				m_hPrevExtrudePos->SetAbsOrigin( m_hProbe->GetAbsOrigin() );
				
				UTIL_Remove( m_hProbe );

				return PROBE_STATE_LANDED_ON_SCULPTURE;
			}
			else if ( !icegun_extrude_spherical.GetBool() )
			{
				bHandled = true;

				//
				// For planar extrusion, derive a better normal. This makes building on
				// existing ice sculptures more predictable.
				//
				// FIXFIX!!!
				DevMsg( "Need to get the proper extrude direction here! Fix this!\n" );
				Vector vecBaseNormal = ReplicateToVector(0);//pSphere->m_vecExtrudeDir;
				Vector vecGestureDir = pSculpture->m_vecBaseOrigins.Head() - pSculpture->m_vecBaseOrigins.Tail();
				VectorNormalize( vecGestureDir );
				
				vecNormal = CrossProduct( vecBaseNormal, vecGestureDir );
				
				if ( DotProduct( vecNormal, forward ) > 0 )
				{
					vecNormal *= -1;
				}
				
				Assert( m_hProbe->GetAbsOrigin().LengthSqr() != 0 );

				if ( icegun_debug.GetBool() )
				{				
					debugoverlay->AddLineOverlay( m_hProbe->GetAbsOrigin(), m_hProbe->GetAbsOrigin() + vecBaseNormal * 50.0f, 255, 0, 0, false, 10.0f );
					debugoverlay->AddLineOverlay( m_hProbe->GetAbsOrigin(), m_hProbe->GetAbsOrigin() + vecGestureDir * 50.0f, 0, 255, 0, false, 10.0f );
					debugoverlay->AddLineOverlay( m_hProbe->GetAbsOrigin(), m_hProbe->GetAbsOrigin() + vecNormal * 50.0f, 0, 0, 255, false, 10.0f );
				}
			}
		}
		
		if ( !bHandled )
		{
			if ( icegun_debug.GetBool() )
			{
				Msg( "Adjusting probe position\n" );
			}
			
			Vector vecForward = forward;
			VectorNormalize( vecForward );
			
			// Push it forward into the surface to make stepping into ramps easier.
			vecRefPoint -= m_hProbe->m_vecNormal * 2.0f;
		}

		// Flew too far, start fading the probe out. When it removes itself
		// we'll detect it as lost.
		Vector vecDelta = m_hProbe->GetAbsOrigin() - GetOwner()->GetAbsOrigin();
		if ( vecDelta.Length() > icegun_max_build_dist.GetFloat() )
		{
			m_hProbe->StartFading();
			return PROBE_STATE_IN_FLIGHT;
		}

		UTIL_Remove( m_hProbe );

		return PROBE_STATE_LANDED;
	}

	// Flew too far, start fading the probe out. When it removes itself
	// we'll detect it as lost.
	Vector vecDelta = m_hProbe->GetAbsOrigin() - GetOwner()->GetAbsOrigin();
	if ( vecDelta.Length() > icegun_max_melt_dist.GetFloat() )
	{
		m_hProbe->StartFading();
	}

	return PROBE_STATE_IN_FLIGHT;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::FreezeObject( const Vector &start, const Vector &forward )
{
	// We're adding ice to an NPC or physics object.
	CBaseAnimating *pFreezingObject = m_hFreezingObject.Get();
	if ( !pFreezingObject )
		return;

	if ( pFreezingObject->IsNPC() && GetOwner()->IRelationType( pFreezingObject ) == D_LI )
	{
		// Don't freeze NPCs that we like!
		return;
	}

	m_flGestureDist = start.DistTo( pFreezingObject->GetAbsOrigin() ) + 128.0f;

	// Find the position where we're hitting it
	Ray_t ray;
	ray.Init( start, start + forward * m_flGestureDist, ReplicateToVector( -5.0f ), ReplicateToVector( 5.0f ) );

	trace_t tr;
	UTIL_TraceRay( ray, MASK_NPCSOLID, GetOwner(), COLLISION_GROUP_NONE, &tr );

	if ( tr.m_pEnt != pFreezingObject )
	{
		// No longer pointing at the object
		m_hFreezingObject = NULL;
		StopSculpting();
		return;
	}

	UpdateSculptBeam( forward, tr.endpos );

	pFreezingObject->Freeze( icegun_sphere_freeze_amount.GetFloat(), GetOwnerEntity(), &ray );
}


//-----------------------------------------------------------------------------
// Clean up all our sculpture state.
//-----------------------------------------------------------------------------
void CWeaponIcegun::StopSculpting()
{
	EmitSound( "Weapon_IceGun.Deactivate" );

	if ( m_pIcegunLoopSound )
	{
		CSoundEnvelopeController::GetController().SoundFadeOut( m_pIcegunLoopSound, 0.25f, true );
		m_pIcegunLoopSound = NULL;
	}

	if ( m_pIceLoopSound )
	{
		CSoundEnvelopeController::GetController().SoundFadeOut( m_pIceLoopSound, 0.10f, true );
		m_pIceLoopSound = NULL;
	}

	DestroySculptBeam();

	if ( m_hProbe )
	{
		// We're no longer using this probe
		m_hProbe->Detach();
		m_hProbe = NULL;
	}

	if ( m_hFreezingObject )
	{
		m_hFreezingObject = NULL;
	}
	
	m_PrevSpheres.RemoveAll();

	m_pRefSphere = NULL;
	m_hActiveSculpture = NULL;
	m_eSculptState = SCULPT_STATE_NONE;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::MeltSculpture( const Vector &forward )
{
	UpdateSculptBeam( forward, m_hPrevExtrudePos->GetAbsOrigin() );

	if ( gpGlobals->curtime < m_flShatterTime )
		return;

	m_hActiveSculpture->Shatter( m_hPrevExtrudePos->GetAbsOrigin(), 0 );
	
	StopSculpting();

	// Don't do anything until they release the button.
	m_bPrimaryAttackDebounce = true;
}


//-----------------------------------------------------------------------------
// Manages the gun sounds while building ice.
//-----------------------------------------------------------------------------
void CWeaponIcegun::UpdateSculptSound()
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	if ( m_pIcegunLoopSound == NULL )
	{
		CPASAttenuationFilter filter( this );
		m_pIcegunLoopSound = controller.SoundCreate( filter, entindex(), "Weapon_IceGun.Active" );
		controller.Play( m_pIcegunLoopSound, 0.8, 90 );
	}
	else
	{
		// Ramp pitch and volume as they build ice
		float perc = ( m_flSphereCreationSpeed > 0 ) ? 1 : 0.9;
		float delay = ( m_flSphereCreationSpeed > 0 ) ? 0.1 : 1.5;
		
		controller.SoundChangeVolume( m_pIcegunLoopSound, perc, delay );
		controller.SoundChangePitch( m_pIcegunLoopSound, 100 * perc, delay );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::AddSculptSpheres( attackstate_t eState )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;
		
	if ( eState == ATTACK_POST )
	{
		if ( ( m_iClip1 == 0 ) && icegun_auto_reload.GetBool() )
		{
			Reload();
			
			// Reload debounces the primary attack button which we don't want
			m_bPrimaryAttackDebounce = false;
		}
		else
		{
			StopSculpting();
		}
		return;
	}

	UpdateSculptSound();

	if ( icegun_debug.GetBool() )
	{
		switch ( m_eSculptState )
		{
			case SCULPT_STATE_NONE:
				Msg( "SCULPT_STATE_NONE\n" ); break;
			case SCULPT_STATE_WAIT_PROBE:
				Msg( "SCULPT_STATE_WAIT_PROBE\n" ); break;
			case SCULPT_STATE_EXTRUDE:
				Msg( "SCULPT_STATE_EXTRUDE\n" ); break;
		}
	}
	
	Vector forward;
	Vector right;
	pOwner->EyeVectors( &forward, &right );
	Vector start = pOwner->Weapon_ShootPosition();

	// We're just firing the probe or waiting for it to land.
	if ( m_eSculptState == SCULPT_STATE_NONE )
	{
		FireProbe( start, forward, right );
		return;
	}

	// Waiting for the probe to land or go out of range
	if ( m_eSculptState == SCULPT_STATE_WAIT_PROBE )
	{
		Vector vecRefPoint;
		ProbeState_t eProbe = CheckProbe( start, forward, vecRefPoint, m_vecGestureNormal );

		if ( ( eProbe == PROBE_STATE_IN_FLIGHT ) || ( eProbe == PROBE_STATE_LOST ) )
			return;
			
		if ( ( eProbe == PROBE_STATE_LANDED_ON_SCULPTURE ) && ( icegun_melt_sculptures.GetInt() > 0 ) )
		{
			m_flShatterTime = gpGlobals->curtime + icegun_melt_delay.GetFloat();
			EmitSound( "Weapon_Icegun.SculpturePreShatter" );
			m_eSculptState = SCULPT_STATE_MELT_SCULPTURE;
			return;
		}
			
		Assert( icegun_fixed_width.GetInt() > 0 );

		BeginSculpture( forward, right, vecRefPoint, m_vecGestureNormal, icegun_fixed_width.GetInt() );
		return;
	}
	
	// Freezing an object or NPC
	if ( m_eSculptState == SCULPT_STATE_FREEZE_OBJECT )
	{
		FreezeObject( start, forward );
		return;
	}
	
	// Melting or shattering an existing sculpture.
	if ( m_eSculptState == SCULPT_STATE_MELT_SCULPTURE )
	{
		MeltSculpture( forward );
		return;
	}
	
	// Extruding an ice sculpture
	Assert( m_eSculptState == SCULPT_STATE_EXTRUDE );

	// Check if there's an NPC in the path of our beam
	Ray_t ray;
	ray.Init( start, start + forward * m_flGestureDist );

	trace_t tr;
	UTIL_TraceRay( ray, MASK_NPCSOLID, GetOwner(), COLLISION_GROUP_NONE, &tr );

	CAI_BaseNPC *pNPC = dynamic_cast<CAI_BaseNPC*>( tr.m_pEnt );

	if ( pNPC && ( pNPC->Classify() != CLASS_BULLSEYE ) )
	{
		// NPC is in the path of our beam, latch on it it instead
		m_hFreezingObject = pNPC;

		if ( m_hProbe )
		{
			m_hProbe->Detach();
			m_hProbe = NULL;
		}

		m_eSculptState = SCULPT_STATE_FREEZE_OBJECT;
		return;
	}

	if ( icegun_extrude_spherical.GetBool() )
	{
		// When surfing, keep the ice at an optimal distance ahead of the player. This
		// means the sculpture won't extrude until they are close enough to the edge.
		// It will also push the leading edge out if they are too close to it.
		if ( IsPlayerIceSurfing() )
		{
			float flDistToEnd = m_hPrevExtrudePos->GetAbsOrigin().DistTo( pOwner->GetAbsOrigin() );
			if ( flDistToEnd > icegun_extrude_dist.GetFloat() )
				return;
			
			m_flGestureDist = MAX( icegun_extrude_dist.GetFloat(), flDistToEnd );
		}

		// Spherical extrusion
		Vector vecPos = start + forward * m_flGestureDist;
		Vector vecDelta = forward * m_flGestureDist;

		if ( icegun_debug.GetBool() )
		{
			Msg( "ExtrudeSpherical: start=%f delta=%f pos=%f\n", start.z, vecDelta.z, vecPos.z );
		}

		//ExtrudeSpherical( start, forward, right, m_flGestureDist, m_vecGestureNormal, icegun_fixed_width.GetInt() );
		ExtrudeSpherical( start, forward, right, vecPos, m_vecGestureNormal, icegun_fixed_width.GetInt() );
	}
	else
	{
		Vector vecRefPoint;
		
		// Planar extrusion
		if ( m_pRefSphere )
		{
			vecRefPoint = m_pRefSphere->m_vecPos;

			if ( icegun_debug.GetBool() )
			{
				Msg( "Getting vecRefPoint from the reference sphere: %f %f %f\n", vecRefPoint.x, vecRefPoint.y, vecRefPoint.z );
			}

			if ( icegun_debug.GetBool() )
			{
				debugoverlay->AddBoxOverlay( m_pRefSphere->m_vecPos, Vector( -ICE_SPHERE_RADIUS, -ICE_SPHERE_RADIUS, -ICE_SPHERE_RADIUS ), Vector( ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS, ICE_SPHERE_RADIUS ), QAngle( 0, 0, 0 ), 0, 255, 0, true, 2.0 );
			}
		}

		// BUGBUG: possible to get here with an uninitialized vecRefPoint, but this code path is probably going away anyway

		Vector vecDir;
		GetExtrudeDirPlanar( start, forward, vecRefPoint, vecDir );
		
		//Msg( "dir = %f %f %f\n", vecDir.x, vecDir.y, vecDir.z );
		
		ExtrudePlanar( vecDir );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::Init()
{
	m_weaponParams[WEAPONIZER_PARAM_PRIMARY] = s_AddSculptSpheres;
	m_weaponParams[WEAPONIZER_PARAM_SECONDARY] = s_ShootIce;
	
	float flPrimaryAmmoRegenRate = icegun_ice_regen_rate.GetFloat();
	if ( flPrimaryAmmoRegenRate > 0 )
	{
		m_flNextPrimaryRegenTime = gpGlobals->curtime + flPrimaryAmmoRegenRate;
	}

	float flSecondaryAmmoRegenRate = icegun_cryodart_regen_rate.GetFloat();
	if ( flSecondaryAmmoRegenRate > 0 )
	{
		m_flNextSecondaryRegenTime = gpGlobals->curtime + flSecondaryAmmoRegenRate;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponIcegun::SculptStateRequiresAmmo()
{
	return ( ( m_eSculptState != SCULPT_STATE_NONE ) &&
			 ( m_eSculptState != SCULPT_STATE_MELT_SCULPTURE ) &&
			 ( m_eSculptState != SCULPT_STATE_WAIT_PROBE ) );
}


//-----------------------------------------------------------------------------
// Shoot ice which accumulates to form sculptures.
//-----------------------------------------------------------------------------
void CWeaponIcegun::PrimaryAttack( attackstate_t eState )
{
	if ( ( eState == ATTACK_RUN) && ( gpGlobals->curtime <= m_flNextPrimaryAttack ) )
		return;

	if ( SculptStateRequiresAmmo() &&
		 ( ( m_iClip1 < m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flPrimaryAmmoBurnRate ) ||
		   ( m_iClip2 < m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flSecondaryAmmoBurnRate ) ) )
	{
		//OnOutOfIceAmmo();
		 
		m_bPrimaryAttackDebounce = true;
		m_bFiringPrimary = false;
		return;
	}

	( this->*m_weaponParams[WEAPONIZER_PARAM_PRIMARY].funcShoot )( eState );

	SubtractAmmo( WEAPONIZER_PARAM_PRIMARY );

	// Keep pushing out the timer as long as we're firing.
	if ( icegun_regen_ice.GetBool() && ( icegun_ice_regen_rate.GetFloat() > 0 ) )
	{
		m_flNextPrimaryRegenTime = gpGlobals->curtime + icegun_ice_regen_rate.GetFloat();
	}

	if ( m_flNextPrimaryAnimationTime <= gpGlobals->curtime )
	{
		if ( m_weaponParams[WEAPONIZER_PARAM_PRIMARY].bContinuous )
		{
			SendWeaponAnim( ACT_VM_PRIMARYATTACK );
		}
		else
		{
			SendWeaponAnim( ACT_VM_SECONDARYATTACK );
		}

		m_flNextPrimaryAnimationTime = gpGlobals->curtime + SequenceDuration();
	}

	m_flNextPrimaryAttack = gpGlobals->curtime + icegun_ice_fire_rate.GetFloat();
}


//-----------------------------------------------------------------------------
// Shoots cryodarts.
//-----------------------------------------------------------------------------
void CWeaponIcegun::SecondaryAttack( attackstate_t eState )
{
	if ( ( eState == ATTACK_RUN) && gpGlobals->curtime <= m_flNextSecondaryAttack )
		return;

	if ( ( m_iClip1 < m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flPrimaryAmmoBurnRate ) ||
		( m_iClip2 < m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flSecondaryAmmoBurnRate ) )
	{	 
		m_bFiringSecondary = false;
		return;
	}

	( this->*m_weaponParams[WEAPONIZER_PARAM_SECONDARY].funcShoot )( eState );

	SubtractAmmo( WEAPONIZER_PARAM_SECONDARY );

	// Keep pushing out the timer as long as we're firing.
	if ( icegun_cryodart_regen_rate.GetFloat() > 0 )
	{
		m_flNextSecondaryRegenTime = gpGlobals->curtime + icegun_cryodart_regen_rate.GetFloat();
	}

	if ( m_flNextSecondaryAnimationTime <= gpGlobals->curtime )
	{
		if ( m_weaponParams[WEAPONIZER_PARAM_SECONDARY].bContinuous )
		{
			SendWeaponAnim( ACT_VM_PRIMARYATTACK );
			m_flNextSecondaryAnimationTime = gpGlobals->curtime + SequenceDuration();
		}
		else
		{
			SendWeaponAnim( ACT_VM_SECONDARYATTACK );
			m_flNextSecondaryAnimationTime = gpGlobals->curtime + icegun_cryodart_fire_rate.GetFloat();
		}
	}

	m_flNextSecondaryAttack = gpGlobals->curtime + icegun_cryodart_fire_rate.GetFloat();
}


//-----------------------------------------------------------------------------
// Dumps out our ammo, emptying the gun.
//-----------------------------------------------------------------------------
bool CWeaponIcegun::Reload()
{
	CIceSculpture *pOldestSculpture = CIceSculpture::GetOldestIceSculpture();

	if ( !icegun_regen_ice.GetBool() )
	{		
		if ( pOldestSculpture )
		{
			pOldestSculpture->Melt();
		}
	}

	m_nWarningLevel = 0;

	if ( IsPlayerIceSurfing( pOldestSculpture ) )
	{
		// Don't let the player continue to build on the melting sculpture
		if ( m_eSculptState != SCULPT_STATE_NONE )
		{
			m_bPrimaryAttackDebounce = true;
		}
		
		StopSculpting();
	}

	m_flNextReloadTime = gpGlobals->curtime + 1.0f;

	WeaponSound( SPECIAL3 );
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
		
	return false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::WeaponIdle( void )
{
	m_bFiringSecondary = false;
	m_bFiringPrimary = false;

}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponIcegun::IsAmmoFull( int iType )
{
	return false;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::ItemPreFrame()
{
	BaseClass::ItemPreFrame();

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	//CBaseViewModel *vm = pOwner->GetViewModel(); 
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::ItemPostFrame()
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	if ( ( pOwner->m_nButtons & IN_RELOAD ) && ( gpGlobals->curtime > m_flNextReloadTime ) )
	{
		Reload();
		return;
	}

	// HACK: clean up beam if we lost the probe
	if ( m_eSculptState == SCULPT_STATE_NONE )
	{	
		DestroySculptBeam();
	}

	bool bFired = false;

	bool bHasAmmo = false;
	if ( !icegun_regen_ice.GetBool() )
	{
		bHasAmmo = UpdateIceAmmo();
	}

	if ( ( pOwner->m_nButtons & IN_ATTACK ) && !m_bPrimaryAttackDebounce )
	{
		if ( !bHasAmmo && SculptStateRequiresAmmo() )
		{
			OnOutOfIceAmmo();
		}
		else
		{
			// FIXME: do we need two m_eAttackStates, one for primary, one for secondary?
			PrimaryAttack(  ATTACK_RUN );
			bFired = true;
		}
	}
	else
	{
		m_bFiringPrimary = false;
	}

	if ( pOwner->m_nButtons & IN_ATTACK2 )
	{
		SecondaryAttack( ATTACK_RUN );
		bFired = true;
	}
	else
	{
		m_bFiringSecondary = false;
	}

	if ( pOwner->m_afButtonReleased & IN_ATTACK )
	{
		if ( m_bPrimaryAttackDebounce )
		{
			m_bPrimaryAttackDebounce = false;
		}
		else
		{
			PrimaryAttack( ATTACK_POST );
		}
	}

	if ( pOwner->m_afButtonReleased & IN_ATTACK2 )
	{	
		SecondaryAttack( ATTACK_POST );
	}

	// Handle ammo regeneration here
	if ( icegun_regen_ice.GetBool() && m_flNextPrimaryRegenTime && ( gpGlobals->curtime > m_flNextPrimaryRegenTime ) )
	{
		m_iClip1++;
		if ( icegun_max_regen_ice.GetInt() > 0 )
		{
			m_iClip1 = MIN( m_iClip1, icegun_max_regen_ice.GetInt() );
		}
		
		m_flNextPrimaryRegenTime = gpGlobals->curtime + icegun_ice_regen_rate.GetFloat();
	}

	if ( m_flNextSecondaryRegenTime && ( gpGlobals->curtime > m_flNextSecondaryRegenTime ) )
	{
		m_iClip2++;
		if ( icegun_max_cryodart.GetInt() > 0 )
		{	
			m_iClip2 = MIN( m_iClip2, icegun_max_cryodart.GetInt() );
		}

		m_flNextSecondaryRegenTime = gpGlobals->curtime + icegun_cryodart_regen_rate.GetFloat();
	}

	if ( bFired == false ) 
	{
		WeaponIdle();
	}
}


//-----------------------------------------------------------------------------
// Shuts down sounds
//-----------------------------------------------------------------------------
void CWeaponIcegun::StopLoopingSounds()
{
	//if ( m_sndMotor != NULL )
	//{
	//	 (CSoundEnvelopeController::GetController()).SoundDestroy( m_sndMotor );
	//	 m_sndMotor = NULL;
	//}

	BaseClass::StopLoopingSounds();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::DestroyEffects( void )
{
	// TODO: destroy viewmodel effects
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponIcegun::StopEffects( bool stopSound )
{
	// Turn off our effect state
	DoEffect( EFFECT_NONE );

	// TODO: turn off viewmodel effects and sound loops
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::StartEffects( void )
{
	// TODO: Start viewmodel effects here

	//CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	//if ( pOwner == NULL )
	//	return;

	//CBaseEntity *pBeamEnt = pOwner->GetViewModel();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponIcegun::DoEffect( int effectType, Vector *pos )
{
	// Make sure we're active
	StartEffects();

	m_EffectState = effectType;

	switch ( effectType )
	{
		default:
		case EFFECT_NONE:
			//DoEffectNone();
			break;
	}
}


//-----------------------------------------------------------------------------
// Part of our sculpture was vaporized. Interrupt the surfing in progress.
//-----------------------------------------------------------------------------
void CWeaponIcegun::InputOnSculptureVaporized( inputdata_t &inputdata )
{
	if ( m_eSculptState != SCULPT_STATE_NONE )
	{
		m_bPrimaryAttackDebounce = true;
	}

	StopSculpting();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool Icegun_IsBuildingSculpture()
{
	if ( !s_pIcegun )
		return false;
		
	return ( s_pIcegun->m_eSculptState == SCULPT_STATE_EXTRUDE );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Icegun_GetBuildPosition( Vector &vecPos )
{
	Assert( s_pIcegun != NULL );
	if ( !s_pIcegun )
		return;

	Assert( s_pIcegun->m_eSculptState != SCULPT_STATE_NONE );

	vecPos = s_pIcegun->m_hPrevExtrudePos->GetAbsOrigin();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool Icegun_IsPlayerIceSurfing()
{
	if ( !s_pIcegun )
		return false;

	return s_pIcegun->IsPlayerIceSurfing();
}
