//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose:		Blob - nasty bug
//
//===========================================================================//

#include "cbase.h"

#include "KeyValues.h"
#include "FileSystem.h"
						  
#include "engine/IEngineTrace.h"
#include "datacache/imdlcache.h"
#include "particle_parse.h"
#include "vphysics/constraints.h"
#include "physics_saverestore.h"
#include "vphysics/friction.h"
#include "physics_shared.h"
#include "collisionutils.h"
#include "ai_hint.h"
#include "saverestore_utlvector.h"
#include "dt_utlvector_send.h"
#include "blob_regenerator.h"

#include "movevars_shared.h"
#include "ai_node.h"
#include "ai_link.h"
#include "ai_dynamiclink.h"
#include "ai_network.h"
#include "ai_networkmanager.h"
#include "ai_route.h"
#include "ai_senses.h"
#include "ai_memory.h"
#include "ai_localnavigator.h"
#include "ai_squadslot.h"

#include "hl2_shareddefs.h"
#include "hl2_gamerules.h"
#include "soundenvelope.h"
#include "npcevent.h"
#include "fire.h"
#include "props.h"
#include <algorithm>

#include "npc_blob.h"
#include "unseen_movement.h"
#include "func_pipe_entrance.h"
#include "grenade_spit.h"
#include "npc_blob_tiny.h"
#include "basegrenade_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

														   
#define BLOB_MOVE_CAPS ( bits_CAP_MOVE_GROUND | bits_CAP_MOVE_CRAWL | bits_CAP_SKIP_NAV_GROUND_CHECK | bits_CAP_NO_LOCAL_NAV_CRAWL )

#define BLOB_ENVELOPE_STACK_MAX 32
#define BLOB_CLING_TIME 1.75f
#define BLOB_STUN_TIMEOUT 0.7
#define BLOB_MERGE_AFTER_SPLIT_TIMEOUT 2
#define BLOB_SPIT_GRAVITY 450
#define BLOB_MAIN_SPIT_HEIGHT 60.0f
#define BLOB_NEIGHBOR_FORCE_LERP_TIME 0.5f

#define BLOB_SWIPE_VERTICAL_BIAS 0.2f

// Fraction of total particle count to regenerate
#define BLOB_REGENERATION_AMOUNT 1.0f

static ConVar sv_blob_regeneration_time(			"blob_regeneration_time",		"15",	FCVAR_CHEAT, "Time after damage before blob autoregenerates");
#define BLOB_REGENERATION_TIME sv_blob_regeneration_time.GetFloat()

#define BIRTH_DURATION			2.0f
#define BIRTH_START_TIME_SPREAD	0.8f
#define BIRTH_START_RADIUS		12.0f
#define DEATH_PAUSE_TIME		1.5f
#define DEATH_MELT_TIME			1.5f

// Fraction of total particle count to blow off each time damage is done
#define BLOB_DAMAGE_AMOUNT 0.20f
#define BLOB_SPIT_MAX_RANGE (150*12)
#define BLOB_SPIT_MAX_RANGE_SQR ( BLOB_SPIT_MAX_RANGE * BLOB_SPIT_MAX_RANGE )


static ConVar sv_blob_debug_draw_particles(			"blob_debug_draw_particles",		"0",	FCVAR_CHEAT, "Draw particles with colored boxes");
static ConVar sv_blob_debug_draw_parents(			"blob_debug_draw_parents",			"0",	FCVAR_CHEAT, "Draw lines between parents");
static ConVar sv_blob_debug_draw_pgoals(			"blob_debug_draw_pgoals",			"0",	FCVAR_CHEAT, "Draw lines where there are forces applied between particles and their pgoals");
static ConVar sv_blob_debug_draw_contact_points(	"blob_debug_draw_contact_points",	"0",	FCVAR_CHEAT, "Draw contact points and vectors towards surface");
static ConVar sv_blob_debug_draw_goals(				"blob_debug_draw_goals",			"0",	FCVAR_CHEAT, "Draw the blob's various movement goals. Mode 2 = draw per-particle goals" );
static ConVar sv_blob_debug_draw_support(			"blob_debug_draw_support",			"0",	FCVAR_CHEAT, "Draw particle support values");
static ConVar sv_blob_debug_draw_forces(			"blob_debug_draw_forces",			"0",	FCVAR_CHEAT, "Draw particle support values");
static ConVar sv_blob_debug_draw_neighbor_forces(	"blob_debug_draw_neighbor_forces",	"0",	FCVAR_CHEAT, "Visualizes components of neighbor forces");
static ConVar sv_blob_debug_draw_goal_forces(		"blob_debug_draw_goal_forces",		"0",	FCVAR_CHEAT, "Visualize per-particle force towards the goal (along contact surfaces)");

static ConVar sv_blob_touch_damage(					"blob_touch_damage",				"4",	FCVAR_NONE, "The amount of (acid) damage done each time the blob touches the player." );
static ConVar sv_blob_leap_damage(					"blob_leap_damage",					"16",	FCVAR_NONE, "The amount of (acid) damage done each time the blob touches the player." );
static ConVar sv_blob_touch_damage_frequency(		"blob_touch_damage_frequency",		"0.25",	FCVAR_NONE, "How often the blob damages the player when touching them." );

static ConVar sv_blob_enable_stragglers(			"blob_enable_stragglers",			"1.5",	FCVAR_NONE, "Particles separated from the blob's main body for this many seconds are considered 'stragglers', and will have collision disabled so they can catch up (even if it means going through a wall). A time of zero disables stragglers." );
static ConVar sv_blob_straggler_speed(				"blob_straggler_speed",				"100",	FCVAR_NONE, "If blob_enable_stragglers, this sets the speed at which straggler particles move towards their goal." );

// Blob damage tweakage
static ConVar sv_blob_brain_health(					"blob_brain_health",				"100",	FCVAR_NONE, "How much health the blob's brain starts with" );
static ConVar sv_blob_health(						"blob_health",						"300",	FCVAR_NONE, "How much health the blob starts with" );
static ConVar sv_blob_leaper_health(				"blob_leaper_health",				"200",	FCVAR_NONE, "How much health the leaper blob starts with" );
static ConVar sv_blob_spawner_health(				"blob_spawner_health",				"200",	FCVAR_NONE, "How much health the spawner blob starts with" );
static ConVar sv_blob_stun_blast_threshold(			"blob_stun_blast_threshold",		"6000",	FCVAR_NONE, "The force which needs to be applied, by an explosion, to a blob in order to put it into the 'stunned' state." );

// Stalking movement tweakage
static ConVar sv_blob_stalk_goal_update_frequency(	"blob_stalk_goal_update_frequency",	".25",	FCVAR_NONE, "How often the blob should update its target position while stalking" );
static ConVar sv_blob_stalk_distance(				"blob_stalk_distance",				"160",	FCVAR_NONE, "How close the blob tries to get while stalking its target" );
static ConVar sv_blob_stalk_near_distance(			"blob_stalk_near_distance",			"400",	FCVAR_NONE, "How close the blob needs to be, while stalking its target, in order to initiate 'near' behaviours such as lunging or tentacle attacks" );
static ConVar sv_blob_stalk_predict_time(			"blob_stalk_predict_time",			"2.5",	FCVAR_NONE, "How far into the future the blob should extrapolate its target's position. Using a higher value can make the blob seem smarter (anticipating the target's movement), but can also cause it to overshoot too far and become less agile" );
static ConVar sv_blob_stalk_predict_max_dist(		"blob_stalk_predict_max_dist",		"250",	FCVAR_NONE, "The maximum distance ahead of the blob's target the blob should aim for when predicting its future motion" );

// Lunge movement tweakage
static ConVar sv_blob_lunge_timeout(				"blob_lunge_timeout",				"0.0",	FCVAR_NONE, "How frequently the blob may lunge (parallel to the target's motion) when stalking a target" );
static ConVar sv_blob_lunge_duration(				"blob_lunge_duration",				"0.5",	FCVAR_NONE, "How long each lunge a blob performs lasts for when stalking a target" );
static ConVar sv_blob_lunge_trigger_velocity(		"blob_lunge_trigger_velocity",		"35",	FCVAR_NONE, "While stalking a target, if it starts to move faster than this velocity, the blob will perform a lunge to catch up" );
static ConVar sv_blob_lunge_predict_time(			"blob_lunge_predict_time",			"2.5",	FCVAR_NONE, "When lunging, how far into the future the blob should extrapolate its target's position. Using a higher value can make the blob seem smarter (anticipating the target's movement), but can also cause it to overshoot too far and become less agile" );
static ConVar sv_blob_lunge_predict_max_dist(		"blob_lunge_predict_max_dist",		"250",	FCVAR_NONE, "When lunging, the maximum distance ahead of the blob's target the blob should aim for when predicting the target's future movement" );

// Brain tweakage
static ConVar sv_blob_brain_speed(					"blob_brain_speed",					"2.0",	FCVAR_NONE, "Scales the speed of the brain, when exposed, as it moves towards the rest of the blob." );
static ConVar sv_blob_brain_flail_if_exposed(		"blob_brain_flail_if_exposed",		"5",	FCVAR_NONE, "If the brain is exposed (more than blob_brain_danger_exposure_level), the blob will flail this many tentacles randomly" );

// Grenade tweakage
static ConVar sv_blob_capture_grenades(				"blob_capture_grenades",			"1",	FCVAR_NONE, "The blob will capture up to this many (live) grenades, as they collide with it." );

// Tentacle tweakage
static ConVar sv_blob_tentacle_defend(				"blob_tentacle_defend", 			"1",	FCVAR_NONE, "Makes the blob use tentacles defensively rather than offsensively" );
static ConVar sv_blob_tentacle_length(				"blob_tentacle_length",				"230",	FCVAR_NONE, "Length of the blob tentacle - blobs need to be this close to the player to use their tentacle attack." );
static ConVar sv_blob_max_tentacles(				"blob_max_tentacles",				"3",	FCVAR_NONE, "The maximum number of tentacles that the blob should have at the same time." );
static ConVar sv_blob_tentacle_damage_timeout(		"blob_tentacle_damage_timeout",		"0.5",	FCVAR_NONE, "If the blob hits with two tentacle attacks within this time of each other, the second only scores quarter damage (so hitting with two tentacles at once isn't a one-shot kill)." );
static ConVar sv_blob_tentacle_vulnerable_time( 	"blob_tentacle_vulnerable_time",	"2.5",	FCVAR_NONE, "The length of time, after missing its target with a tentacle attack, for which the blob is vulnerable to grenades (i.e. it will not spit them out during this time)" );
static ConVar sv_blob_tentacle_hit_then_haul(		"blob_tentacle_hit_then_haul",		"0",	FCVAR_NONE, "Whether the blob, at the end of a tentacle attack, should use the tentacle to pull itself to its target." );
static ConVar sv_blob_tentacle_hit_damage(			"blob_tentacle_hit_damage",			"15",	FCVAR_NONE, "Damage applied to a target hit by a blob's tentacle" );
static ConVar sv_blob_tentacle_hit_force(			"blob_tentacle_hit_force",			"300",	FCVAR_NONE, "Force applied to a target hit by a blob's tentacle" );
static ConVar sv_blob_tentacle_hit_tolerance(		"blob_tentacle_hit_tolerance",		"50",	FCVAR_NONE, "How close the tip of a blob tentacle needs to be to its target to hit it" );
static ConVar sv_blob_tentacle_miss_splash_force(	"blob_tentacle_miss_splash_force",	"3.5",	FCVAR_NONE, "How big a 'splash' force should be applied to the blob's particles in reaction to missing a tentacle attack (bigger force means bigger splash)" );
static ConVar sv_blob_tentacle_create_timeout(		"blob_tentacle_create_timeout",		"1.5",	FCVAR_NONE, "How long the blobs should wait between creating tentacles." );
static ConVar sv_blob_tentacle_extrude_time(		"blob_tentacle_extrude_time",		"1.5",	FCVAR_NONE, "Time spent in the first (extrude) phase of the blob tentacle attack." );
static ConVar sv_blob_tentacle_idle_time(			"blob_tentacle_wait_time",			"0.4",	FCVAR_NONE, "Time spent in the second (wait) phase of the blob tentacle attack." );
static ConVar sv_blob_tentacle_lunge_time(			"blob_tentacle_lunge_time",			"0.5",	FCVAR_NONE, "Time spent in the third (lunge) phase of the blob tentacle attack." );
static ConVar sv_blob_tentacle_recede_time(			"blob_tentacle_recede_time",		"0.75",	FCVAR_NONE, "Time spent in the fourth (recede) phase of the blob tentacle attack - only used if blob_tentacle_hit_then_haul is not set." );
static ConVar sv_blob_tentacle_haul_time(			"blob_tentacle_haul_time",			"1.5",	FCVAR_NONE, "Time spent in the fourth (haul) phase of the blob tentacle attack - only used if blob_tentacle_hit_then_haul is set." );

ConVar  sk_blob_spit_speed( "sk_blob_spit_speed", "0", FCVAR_NONE, "Speed at which an blob spit grenade travels." );

enum BlobSize_t
{
	BLOB_SIZE_TINY = 5,
	BLOB_SIZE_SMALL = 35,
	BLOB_SIZE_MEDIUM = 245,
	BLOB_SIZE_LARGE = 555,

	BLOB_SIZE_TOTAL
};

//-----------------------------------------------------------------------------
//	Squad slots
//-----------------------------------------------------------------------------
enum SquadSlot_t
{	
	SQUAD_SLOT_BLOB_LEAP = LAST_SHARED_SQUADSLOT,
	SQUAD_SLOT_BLOB_FLANK,
	SQUAD_SLOT_BLOB_SPIT,
};


struct BlobPhysicsParamState_t
{
	char szName[ 128 ];
	int iStateID;
	float pfProps[ BLOB_PHYSICS_PARAM_TOTAL ];

	BlobPhysicsParamState_t( void )
	{
		szName[ 0 ] = '\0';
		memset( pfProps, 0, sizeof( pfProps ) );
	}
};

CUtlVector<BlobPhysicsParamState_t> g_BlobPhysicsParamStates;

BlobPhysicsParamState_t* GetBlobPhysicsParameterStates( const char *pchName, bool warnIfNotExist = false)
{
	for ( int i = 0; i < g_BlobPhysicsParamStates.Count(); ++i )
	{
		BlobPhysicsParamState_t *pBlobPhysicsParamState = &(g_BlobPhysicsParamStates[ i ]);
		if ( Q_stricmp( pBlobPhysicsParamState->szName, pchName ) == 0 )
		{
			return pBlobPhysicsParamState;
		}
	}

	if(warnIfNotExist)
	{
		DevWarning( "Blob physics parameter state \"%s\" doesn't exist!\n", pchName );
	}
	return NULL;
}

BlobPhysicsParamState_t* GetBlobPhysicsParameterStates( int iStateID, bool warnIfNotExist = false)
{
	for ( int i = 0; i < g_BlobPhysicsParamStates.Count(); ++i )
	{
		BlobPhysicsParamState_t *pBlobPhysicsParamState = &(g_BlobPhysicsParamStates[ i ]);
		if ( pBlobPhysicsParamState->iStateID == iStateID )
		{
			return pBlobPhysicsParamState;
		}
	}

	if(warnIfNotExist)
	{
		DevWarning( "Blob physics parameter state ID \"%i\" doesn't exist!\n", iStateID );
	}
	return NULL;
}

const char *GetBlobPhysicsParameterStatesName( int iStateID )
{
	for ( int i = 0; i < g_BlobPhysicsParamStates.Count(); ++i )
	{
		BlobPhysicsParamState_t *pBlobPhysicsParamState = &(g_BlobPhysicsParamStates[ i ]);
		if ( pBlobPhysicsParamState->iStateID == iStateID )
		{
			return pBlobPhysicsParamState->szName;
		}
	}

	return NULL;
}

// This loads the physics parameter states from a file
// Can also be used to reload physics parameter states.
void LoadBlobPhysicsParameterStates( void )
{
	// First, clean out the list
	g_BlobPhysicsParamStates.Purge();

	KeyValues *pOptionKeys = new KeyValues( "blob_physics_parameter_states" );
	pOptionKeys->LoadFromFile( g_pFullFileSystem, "scripts/blob_physics_parameter_states.txt", NULL );
	KeyValues *pKey = NULL;

	BlobPhysicsParamState_t *pDefaultPhysicsParamState = GetBlobPhysicsParameterStates( "Default" );
	if(pDefaultPhysicsParamState == NULL)
	{
		int i = g_BlobPhysicsParamStates.AddToTail();
		pDefaultPhysicsParamState = &(g_BlobPhysicsParamStates[ i ]);
		Q_strcpy( pDefaultPhysicsParamState->szName, "Default" );
	}

	for ( pKey = pOptionKeys->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey() )
	{

		BlobPhysicsParamState_t *pNewPhysicsParamState = GetBlobPhysicsParameterStates( pKey->GetName() );
		if(pNewPhysicsParamState == NULL)
		{
			int i = g_BlobPhysicsParamStates.AddToTail();
			pNewPhysicsParamState = &(g_BlobPhysicsParamStates[ i ]);
			Q_strcpy( pNewPhysicsParamState->szName, pKey->GetName() );
		}

		pNewPhysicsParamState->iStateID = pKey->GetFloat( "state_id", -1 );

		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_GLOBAL_VISCOSITY ] = pKey->GetFloat( "global_viscosity", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_GLOBAL_VISCOSITY ] );

		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_VISCOSITY ] = pKey->GetFloat( "neighbor_viscosity", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_VISCOSITY ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_SCALE ] = pKey->GetFloat( "neighbor_dist_scale", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_SCALE ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_TOUCH ] = pKey->GetFloat( "neighbor_dist_touch", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_TOUCH ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_NEAR ] = pKey->GetFloat( "neighbor_dist_near", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_NEAR ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_STRAIN ] = pKey->GetFloat( "neighbor_dist_bond_strain", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_STRAIN ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_BREAK ] = pKey->GetFloat( "neighbor_dist_bond_break", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_BREAK ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_MAX ] = pKey->GetFloat( "neighbor_dist_max", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_MAX ] );

		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_ATTRACTION_MAX ] = pKey->GetFloat( "neighbor_attraction_max", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_ATTRACTION_MAX ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_REPULSION_MAX ] = pKey->GetFloat( "neighbor_repulsion_max", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_REPULSION_MAX ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_SURFACE_TENSION ] = pKey->GetFloat( "neighbor_lj_surface_tension", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_SURFACE_TENSION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_ATTRACTION ] = pKey->GetFloat( "neighbor_lj_attraction", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_ATTRACTION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_REPULSION ] = pKey->GetFloat( "neighbor_lj_repulsion", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_REPULSION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LINEAR_ATTRACTION ] = pKey->GetFloat( "neighbor_linear_attraction", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LINEAR_ATTRACTION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LINEAR_REPULSION ] = pKey->GetFloat( "neighbor_linear_repulsion", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_NEIGHBOR_LINEAR_REPULSION ] );

		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_ATTRACTION_MAX ] = pKey->GetFloat( "pgoal_attraction_max", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_ATTRACTION_MAX ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_REPULSION_MAX ] = pKey->GetFloat( "pgoal_repulsion_max", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_REPULSION_MAX ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LJ_SURFACE_TENSION ] = pKey->GetFloat( "pgoal_lj_surface_tension", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LJ_SURFACE_TENSION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LJ_ATTRACTION ] = pKey->GetFloat( "pgoal_lj_attraction", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LJ_ATTRACTION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LJ_REPULSION ] = pKey->GetFloat( "pgoal_lj_repulsion", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LJ_REPULSION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LINEAR_ATTRACTION ] = pKey->GetFloat( "pgoal_linear_attraction", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LINEAR_ATTRACTION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LINEAR_REPULSION ] = pKey->GetFloat( "pgoal_linear_repulsion", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_PGOAL_LINEAR_REPULSION ] );
																   
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_MAX_SPEED_TOWARDS_GOAL ] = pKey->GetFloat( "max_speed_towards_goal", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_MAX_SPEED_TOWARDS_GOAL ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_TIME_TO_MAX_SPEED_TOWARDS_GOAL ] = pKey->GetFloat( "time_to_max_speed_towards_goal", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_TIME_TO_MAX_SPEED_TOWARDS_GOAL ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_FORCE_TOWARDS_GOAL_CONVERGENCE ] = pKey->GetFloat( "force_towards_goal_convergence", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_FORCE_TOWARDS_GOAL_CONVERGENCE ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_TOWARDS_GOAL_BRAKE_TIME ] = pKey->GetFloat( "towards_goal_brake_time", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_TOWARDS_GOAL_BRAKE_TIME ] );

		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_ROOT_PARTICLES ] = pKey->GetFloat( "sticky_force_for_root_particles", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_ROOT_PARTICLES ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_CORE_PARTICLES ] = pKey->GetFloat( "sticky_force_for_core_particles", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_CORE_PARTICLES ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_STICKY_PARTICLES ] = pKey->GetFloat( "sticky_force_for_sticky_particles", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_STICKY_PARTICLES ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_SKIN_PARTICLES ] = pKey->GetFloat( "sticky_force_for_skin_particles", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_SKIN_PARTICLES ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_TENTACLE_PARTICLES ] = pKey->GetFloat( "sticky_force_for_tentacle_particles", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_TENTACLE_PARTICLES ] );

		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_NEIGHBOR_VISCOSITY ] = pKey->GetFloat( "brain_neighbor_viscosity", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_NEIGHBOR_VISCOSITY ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_ATTRACTION_MAX ] = pKey->GetFloat( "brain_attraction_max", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_ATTRACTION_MAX ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_REPULSION_MAX ] = pKey->GetFloat( "brain_repulsion_max", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_REPULSION_MAX ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LJ_SURFACE_TENSION ] = pKey->GetFloat( "brain_lj_surface_tension", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LJ_SURFACE_TENSION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LJ_ATTRACTION ] = pKey->GetFloat( "brain_lj_attraction", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LJ_ATTRACTION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LJ_REPULSION ] = pKey->GetFloat( "brain_lj_repulsion", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LJ_REPULSION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LINEAR_ATTRACTION ] = pKey->GetFloat( "brain_linear_attraction", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LINEAR_ATTRACTION ] );
		pNewPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LINEAR_REPULSION ] = pKey->GetFloat( "brain_linear_repulsion", pDefaultPhysicsParamState->pfProps[ BLOB_PHYSICS_PARAM_BRAIN_LINEAR_REPULSION ] );
	}
}

//Helpers
inline CBlobParticle *ClosestBlobParticle( const Vector &vTestPosition, CBlobParticle **pSearchArray, int iArrayCount )
{
	CBlobParticle* nearestMember = pSearchArray[0];
	float nearestMemberDistSqr = nearestMember->m_position.DistToSqr(vTestPosition);

	for(int i = 1; i != iArrayCount; ++i)
	{
		CBlobParticle* pParticle = pSearchArray[i];
		float distSqr = pParticle->m_position.DistToSqr(vTestPosition);
		if(distSqr < nearestMemberDistSqr)
		{
			nearestMember = pParticle;
			nearestMemberDistSqr = distSqr;
		}
	}

	return nearestMember;
}

inline int CNPC_Blob::ClosestBlobParticleIndices( const Vector &vTestPosition, CBlobParticle **pSearchArray, int iArrayCount, int *piIndicies, int iMaxIndicies, float fMaxDistance )
{
	int iNumIndicies = 0;

	float *pNearestMemberDist = new float[ iMaxIndicies ];

	for( int i = 0; i != iArrayCount; ++i )
	{
		CBlobParticle *pParticle = pSearchArray[ i ];
		int particleIndex = s_ExpandedParticleData[ pParticle->m_vphysParticle->GetGameIndex() ].iEntityIndex;

		float fDistance = pParticle->m_position.DistTo( vTestPosition );
		if ( fDistance < fMaxDistance )
		{
			if ( iNumIndicies < iMaxIndicies )
			{
				piIndicies[ iNumIndicies ] = particleIndex;
				pNearestMemberDist[ iNumIndicies ] = fDistance;
				++iNumIndicies;
			}
			else
			{
				for ( int j = 0; j < iNumIndicies; ++j )
				{
					if( fDistance < pNearestMemberDist[ j ] )
					{
						piIndicies[ j ] = particleIndex;
						pNearestMemberDist[ j ] = fDistance;
						break;
					}
				}
			}
		}
	}

	return iNumIndicies;
}

inline void ChangeParticleType( CBlobParticle *pParticle, BlobParticleType newType )
{
	CBlobGroup *pGroup = pParticle->m_group;
	Verify(pGroup->m_particlesByType[pParticle->m_type].FindAndRemove(pParticle));
	pParticle->m_type = newType;
	pGroup->m_particlesByType[newType].AddToTail(pParticle);
}

void BuildAABBForBlobGroup( const CBlobGroup *pBlobGroup, Vector &mins, Vector &maxs, float flRadius )
{
	// FIXME: cache the result, don't compute every call (put a dirty flag in the group)

	mins = pBlobGroup->m_members[ 0 ]->m_position;
	maxs = mins;
	for ( int i = 0; i < pBlobGroup->m_members.Count(); ++i )
	{
		VectorMin( pBlobGroup->m_members[ i ]->m_position, mins, mins );
		VectorMax( pBlobGroup->m_members[ i ]->m_position, maxs, maxs );
	}

	Vector vSurfaceRadius( flRadius, flRadius, flRadius );

	mins -= vSurfaceRadius;
	maxs += vSurfaceRadius;
}

void NearestSurfacePointAndNormalForBlobGroup( const CBlobGroup *pBlobGroup, const Vector &vInPoint, Vector &vOutPoint, Vector &vOutNormal, float flRadius )
{
	float fClosestDistanceSqr = FLT_MAX;

	for ( int i = 0; i < pBlobGroup->m_members.Count(); ++i )
	{
		CBlobParticle *pBlobParticle =  pBlobGroup->m_members[ i ];

		if ( pBlobParticle->m_nArm == 0 )
		{
			float fDistanceSqr = vInPoint.DistToSqr( pBlobParticle->m_position ) - ( flRadius * flRadius );
			if ( fDistanceSqr < fClosestDistanceSqr )
			{
				fClosestDistanceSqr = fDistanceSqr;
				vOutPoint = pBlobParticle->m_position;
				vOutNormal = vInPoint - pBlobParticle->m_position;
			}
		}
	}

	VectorNormalize( vOutNormal );
	vOutPoint = vOutPoint + vOutNormal * flRadius;
}


extern CBaseEntity *GetNextCommandEntity( CBasePlayer *pPlayer, const char *name, CBaseEntity *ent );


class CInfoBlobSpitTarget;

//-----------------------------------------------------------------------------
// Spit targets/paths
//-----------------------------------------------------------------------------
class CInfoBlobSpitPath : public CPointEntity
{
public:
	DECLARE_DATADESC();
	DECLARE_CLASS( CInfoBlobSpitPath, CPointEntity );

	virtual void Activate( );

	bool IsValidPath( CInfoBlobSpitTarget *pTarget ) const;
	CInfoBlobSpitPath *GetNextPath();

protected:
	CHandle< CInfoBlobSpitPath > m_hNextPath;

private:
	static int s_nCurrVisitId;
	int m_nVisitId;
	string_t m_iszNextPath;
};

static int s_nSpitTargetCount = 0;
class CInfoBlobSpitTarget : public CInfoBlobSpitPath
{
public:
	DECLARE_DATADESC();
	DECLARE_CLASS( CInfoBlobSpitTarget, CInfoBlobSpitPath );

	CInfoBlobSpitTarget() { ++s_nSpitTargetCount; }
	virtual ~CInfoBlobSpitTarget() { --s_nSpitTargetCount; }
	virtual void Activate( );
	CInfoBlobSpitPath *GetApproachPath();
	int GetApproachPathNodeCount() const;
	float ComputeApproachPathLength( const Vector &vecStartPos ) const;
	float GetSearchRange() const { return m_flSearchRange; }
	void GetSearchDirection( Vector *pSearchDir ) const;

private:
	void ValidateApproachPath();

	string_t m_iszApproachPath;
	float m_flSearchRange;
	CHandle< CInfoBlobSpitPath > m_hApproachPath;
};


//-----------------------------------------------------------------------------
// Spit path
//-----------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS( info_blob_spit_path, CInfoBlobSpitPath );

BEGIN_DATADESC( CInfoBlobSpitPath )
	DEFINE_KEYFIELD( m_iszNextPath, FIELD_STRING, "NextPath" ),
	DEFINE_FIELD( m_hNextPath, FIELD_EHANDLE ),
END_DATADESC();

int CInfoBlobSpitPath::s_nCurrVisitId = 0;

void CInfoBlobSpitPath::Activate(void)
{
	BaseClass::Activate();
	if ( m_iszNextPath != NULL_STRING )
	{
		m_hNextPath = dynamic_cast< CInfoBlobSpitPath* >( gEntList.FindEntityByName( NULL, m_iszNextPath ) );
	}
}

CInfoBlobSpitPath *CInfoBlobSpitPath::GetNextPath()
{
	return m_hNextPath.Get();
}

bool CInfoBlobSpitPath::IsValidPath( CInfoBlobSpitTarget *pTarget ) const
{
	++s_nCurrVisitId;
	for( CInfoBlobSpitPath *pNext = m_hNextPath.Get(); pNext != NULL; pNext = pNext->GetNextPath() )
	{
		if ( pNext->m_nVisitId == s_nCurrVisitId )
		{
			Warning( "Circular loop encountered in path for target %s!\n", const_cast< CInfoBlobSpitPath* >( this )->GetEntityNameAsCStr() );
			return false;
		};

		pNext->m_nVisitId = s_nCurrVisitId;
		if ( pNext == pTarget )
			return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// Spit target
//-----------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS( info_blob_spit_target, CInfoBlobSpitTarget );

BEGIN_DATADESC( CInfoBlobSpitTarget )
	DEFINE_KEYFIELD( m_iszApproachPath, FIELD_STRING, "ApproachPath" ),
	DEFINE_KEYFIELD( m_flSearchRange, FIELD_FLOAT, "SearchRange" ),
	DEFINE_FIELD( m_hApproachPath, FIELD_EHANDLE ),
END_DATADESC();

void CInfoBlobSpitTarget::Activate(void)
{
	BaseClass::Activate();
	if ( m_iszApproachPath != NULL_STRING )
	{
		m_hApproachPath = dynamic_cast< CInfoBlobSpitPath* >( gEntList.FindEntityByName( NULL, m_iszApproachPath ) );
	}

	// For safety; disallow this from having a next path
	m_hNextPath.Set( NULL );
}

void CInfoBlobSpitTarget::ValidateApproachPath()
{
	if ( m_hApproachPath.Get() )
	{
		if ( !m_hApproachPath->IsValidPath( this ) )
		{
			DevWarning( "Warning! Spit Target %s uses invalid path %s!\n", GetEntityNameAsCStr(), STRING( m_iszApproachPath ) );
			m_hApproachPath = NULL;
		}
	}
}

CInfoBlobSpitPath *CInfoBlobSpitTarget::GetApproachPath()
{
	ValidateApproachPath();
	return m_hApproachPath.Get();
}

int CInfoBlobSpitTarget::GetApproachPathNodeCount() const
{
	const_cast<CInfoBlobSpitTarget*>( this )->ValidateApproachPath();
	CInfoBlobSpitPath *pPath = m_hApproachPath.Get();
	int nCount = 0;
	for ( ; pPath != NULL; pPath = pPath->GetNextPath() )
	{
		++nCount;
	}
	return nCount;
}

float CInfoBlobSpitTarget::ComputeApproachPathLength( const Vector &vecStartPos ) const
{
	const_cast<CInfoBlobSpitTarget*>( this )->ValidateApproachPath();
	CInfoBlobSpitPath *pPath = m_hApproachPath.Get();
	if ( !pPath )
		return vecStartPos.DistTo( GetAbsOrigin() );

	float flLength = vecStartPos.DistTo( pPath->GetAbsOrigin() );
	CInfoBlobSpitPath *pPrev = pPath;
	pPath = pPath->GetNextPath();
	for ( ; pPath != NULL; pPrev = pPath, pPath = pPath->GetNextPath() )
	{
		flLength += pPath->GetAbsOrigin().DistTo( pPrev->GetAbsOrigin() );
	}
	return flLength;
}

void CInfoBlobSpitTarget::GetSearchDirection( Vector *pSearchDir ) const
{
	GetVectors( pSearchDir, NULL, NULL );
}


//-----------------------------------------------------------------------------
//
// CNPC_Blob
//									    

//Attack range definitions
#define	BLOB_MELEE1_RANGE		100.0f

// Animation events
int AE_BLOB_MELEE_HIT1;

//=========================================================
// Conditions
//=========================================================
enum
{
	COND_BLOB_SEE_PIPE_SHORTCUT = LAST_SHARED_CONDITION,
	COND_BLOB_SEE_NEW_SHAPE_TARGET,
	COND_BLOB_SEE_BUDDY_BLOB,
	COND_BLOB_TOUCHING_BUDDY_BLOB,
	COND_BLOB_FORCE_TRANSFER_THROUGH_PIPE,
	COND_BLOB_TRANSFERING_THROUGH_PIPE,
	COND_BLOB_AUTO_SPLIT_FROM_ANOTHER_BLOB,
	COND_BLOB_HEALTH_LOW,
	COND_BLOB_CAN_STALK_TARGET,
	COND_BLOB_CAN_SHAKE_OFF_BLOBS,
	COND_BLOB_RECEIVED_COMMAND,
	COND_BLOB_BRAIN_EXPOSED,
	COND_BLOB_REGENERATE_PARTICLES,
	COND_BLOB_CAN_LEAP,
	COND_BLOB_LEAP_SUPPRESSED_BY_SQUADMATE,
	COND_BLOB_SPIT_SUPPRESSED_BY_SQUADMATE,
	COND_BLOB_CAN_RUN_TO_REGENERATOR,
	COND_BLOB_CAN_SPAWN_REGENERATOR,
};

//=========================================================
// Schedules
//=========================================================
enum
{
	// Navigation
	SCHED_BLOB_ENTER_PIPE = LAST_SHARED_SCHEDULE,
	SCHED_BLOB_FORCE_ENTER_PIPE,
	SCHED_BLOB_TRANSFER_THROUGH_PIPE,
	SCHED_BLOB_REFORM,

	// Enemy
	SCHED_BLOB_STALK_TARGET,
	SCHED_BLOB_SHAKE_OFF_BLOBS,
	SCHED_BLOB_SHAKE_OFF_BLOBS_USE_HINTS,
	SCHED_BLOB_SPIT,
	SCHED_TAKE_COVER_FROM_ENEMY_USE_BRAIN_HINTS,
	SCHED_BLOB_STUNNED,
	SCHED_BLOB_FLAIL,
	SCHED_BLOB_FLEE_DANGER_SOUND,
	SCHED_BLOB_REGENERATE_PARTICLES,
	SCHED_BLOB_REGENERATE_PARTICLES_NO_LOS,
	SCHED_BLOB_TENTACLE_ATTACK,
	SCHED_BLOB_ESTABLISH_LINE_OF_FIRE,
	SCHED_BLOB_LEAP,
	SCHED_BLOB_RUN_TO_REGENERATOR,
	SCHED_BLOB_SPAWN_REGENERATOR,
};

//=========================================================
// Tasks
//=========================================================
enum
{
	// Navigation
	TASK_BLOB_GET_PATH_TO_PIPE = LAST_SHARED_TASK,
	TASK_BLOB_ENTER_PIPE,
	TASK_BLOB_EXIT_PIPE,
	TASK_BLOB_TRANSFER_THROUGH_PIPE,
	TASK_BLOB_GET_PATH_TO_BUDDY_BLOB,
	TASK_BLOB_SET_PHYS_PARAM_STATE,
	TASK_BLOB_TELEPORT_NPC_TO_PARTICLES,

	// Enemy 
	TASK_BLOB_GET_STALK_TARGET_PATH,
	TASK_WAIT_FOR_STALK_MOVEMENT,
	TASK_BLOB_SHAKE_OFF_BLOBS,
	TASK_BLOB_SPIT_BUILDUP,
	TASK_BLOB_SPIT_START,
	TASK_BLOB_SPIT,
	TASK_BLOB_WAIT_FOR_UNSTUN,
	TASK_BLOB_FLAIL,
	TASK_BLOB_REGENERATE_PARTICLES,
	TASK_BLOB_ANNOUNCE_REGENERATE_PARTICLES,
	TASK_BLOB_WAIT_UNTIL_ENEMY_LOS,
	TASK_BLOB_TENTACLE_ATTACK,
	TASK_BLOB_LEAP_BUILDUP,
	TASK_BLOB_LEAP,
	TASK_BLOB_LEAP_COLLECT,
	TASK_BLOB_LEAP_FINISHED,
	TASK_BLOB_GET_PATH_TO_REGENERATOR,
	TASK_BLOB_TOUCH_REGENERATOR,
	TASK_BLOB_SPAWN_REGENERATOR,
};


//---------------------------------------------------------
// Custom Client entity
//---------------------------------------------------------
IMPLEMENT_SERVERCLASS_ST(CNPC_Blob, DT_NPC_Blob)

	SendPropUtlVector(
		SENDINFO_UTLVECTOR( m_vecTypes ),
		MAX_SURFACE_ELEMENTS, // max elements
		SendPropInt( NULL, 0, sizeof( int ), 3, SPROP_UNSIGNED)), // Send 3 unsigned bits.

	SendPropUtlVector(
		SENDINFO_UTLVECTOR( m_vecParents ),
		MAX_SURFACE_ELEMENTS, // max elements
		SendPropInt( NULL, 0, sizeof( uint16 ), BLOB_MAX_LEVEL_PARTICLES_BITS, SPROP_UNSIGNED)),

	SendPropUtlVector(
		SENDINFO_UTLVECTOR( m_iFlags ),
		MAX_SURFACE_ELEMENTS, // max elements
		SendPropInt( NULL, 0, sizeof( uint16 ), 0, SPROP_UNSIGNED)),

	SendPropBool( SENDINFO( m_bFrozen ) ),
	SendPropEHandle( SENDINFO( m_hAutoSplitBlob ) ),
	SendPropFloat( SENDINFO( m_fBrainExposure ), 0, SPROP_NOSCALE ),
	SendPropTime( SENDINFO( m_flColorChangeStartTime ) ),
	SendPropTime( SENDINFO( m_flColorChangeEndTime ) ),
	SendPropVector( SENDINFO( m_vecStartColor ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_vecEndColor ), 0, SPROP_NOSCALE )

END_SEND_TABLE()

//-----------------------------------------------------------------------------
// Save/Restore
//-----------------------------------------------------------------------------
BEGIN_SIMPLE_DATADESC( BlobShapeTarget_t )

	DEFINE_FIELD( m_ShapeTargetName, FIELD_STRING ),
	DEFINE_FIELD( m_hBaseAnimating, FIELD_EHANDLE ),
	DEFINE_FIELD( m_vDirection, FIELD_VECTOR ),
	DEFINE_FIELD( m_bUseTargetPositionAngles, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bDisableCollision, FIELD_BOOLEAN ),

END_DATADESC();

BEGIN_SIMPLE_DATADESC( BlobCapturedGrenade_t )

	DEFINE_FIELD( m_hGrenade, FIELD_EHANDLE ),
	DEFINE_FIELD( m_fCaptureTime, FIELD_FLOAT ),

END_DATADESC();


//-----------------------------------------------------------------------------
// Save/Restore
//-----------------------------------------------------------------------------
BEGIN_DATADESC( CNPC_Blob )

	DEFINE_UTLVECTOR( m_ShapeTargets, FIELD_EMBEDDED ),

	DEFINE_FIELD( m_bPhysParamStateOverloaded, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_iBlobPhysParamState, FIELD_INTEGER ),
	//DEFINE_FIELD( m_pfBlobPhysParamState, FIELD_POINTER ),

	DEFINE_FIELD( m_BaseColor, FIELD_VECTOR ),
	DEFINE_FIELD( m_fLastAverageParticleNeighbors, FIELD_FLOAT ),
	DEFINE_FIELD( m_fLastInteractionScale, FIELD_FLOAT ),

	DEFINE_FIELD( m_fSpawnTimeStamp, FIELD_TIME ),
	DEFINE_FIELD( m_fLastSeenTime, FIELD_TIME ),
	DEFINE_FIELD( m_nNeighborForceMode, FIELD_INTEGER ),
	DEFINE_FIELD( m_flLastNeighborOutwardForceFactor, FIELD_FLOAT ),
	DEFINE_FIELD( m_flLastNeighborInwardForceFactor, FIELD_FLOAT ),
	
	DEFINE_KEYFIELD( m_bShakeFastBlobs, FIELD_BOOLEAN, "shakeFastBlobs" ),

	DEFINE_FIELD( m_hRegeneratorTarget, FIELD_EHANDLE ),
	DEFINE_FIELD( m_flNextSpawnRegeneratorTime, FIELD_TIME ),

	// FIXME: these really need saving+restoring, currently they're recreated on restore (w/ loss of state)
	//CUtlVector<CBlobParticle*>	m_blobParticles;
	//CUtlVector<CBlobGroup*>		m_blobGroups;
	//CBlobGroup					*m_pMainGroup;
	//int							m_iCacheNumLargeGroups;

	// These do not need saving/restoring
	//CUtlVector<CBlobParticle*>	unGroupedParticles;
	//CUtlVector<CBlobGroup*>		unUpdatedGroups;
	//CUtlVector<CBlobParticle*>	recursionStack;
	//Vector						m_randVectors[50];
	//int							m_randVectorIndex;

	// Doesn't need to get saved.. simply cached off for TASK_FIND_HINTNODE
	// DEFINE_FIELD( m_iDangerSounds, FIELD_INTEGER ),

	DEFINE_UTLVECTOR( m_vecTypes, FIELD_INTEGER ),
	DEFINE_UTLVECTOR( m_vecParents, FIELD_SHORT),
	DEFINE_UTLVECTOR( m_vecUVW,	FIELD_POSITION_VECTOR ),
	DEFINE_UTLVECTOR( m_iFlags, FIELD_CHARACTER ),

	DEFINE_FIELD( m_vecMoveGoal, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_vecLastMoveGoal, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_bIsMoveBlocked, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bPlayedBlockedSound, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flFirstMoveBlockTime, FIELD_TIME ),

	DEFINE_EMBEDDED_ARRAY( m_tentacles, CNPC_Blob::BLOB_MAX_TENTACLES ), // FIXME: change this to DEFINE_UTLVECTOR/FIELD_EMBEDDED
	DEFINE_FIELD( m_fTentacleTimeout, FIELD_TIME ),
	DEFINE_FIELD( m_fTentacleDamageTimeout, FIELD_TIME ),
	DEFINE_FIELD( m_fVulnerableTimeout, FIELD_TIME ),

	DEFINE_FIELD( m_fLastDamagedPlayer, FIELD_TIME ),
	DEFINE_FIELD( m_vecParticleImpulse, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecParticleImpulseSpread, FIELD_VECTOR ),

	DEFINE_KEYFIELD( m_bStartAsBrain, FIELD_BOOLEAN, "startAsBrain" ),
	DEFINE_KEYFIELD( m_nBrainParticleCount, FIELD_INTEGER, "brainParticleCount" ),
	DEFINE_FIELD( m_fBrainExposure, FIELD_FLOAT ),
	DEFINE_PHYSPTR( m_pBrainConstraintGroup ),

	DEFINE_FIELD( m_nMaxParticleCount, FIELD_INTEGER ),
	DEFINE_FIELD( m_flMaxArmorHealth, FIELD_FLOAT ),
	DEFINE_FIELD( m_flArmorHealth, FIELD_FLOAT ),

	DEFINE_FIELD( m_fStunStartTime, FIELD_TIME ),

	DEFINE_UTLVECTOR( m_capturedGrenades, FIELD_EMBEDDED ),

	DEFINE_FIELD( m_fBlockMergeUntilTime, FIELD_TIME ),

	DEFINE_FIELD( m_hPipeEntrance, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hPipeExit, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hPipeExitBlob, FIELD_EHANDLE ),
	// CUtlVector<BlobPipeParticle_t> m_QueuedPipeParticles; // Has custom save/restore code
	DEFINE_FIELD( m_bIsPipeTransferBlob, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bForcePipeTransfer, FIELD_BOOLEAN ),

	DEFINE_FIELD( m_fMinDistToTarget, FIELD_FLOAT ),
	DEFINE_FIELD( m_fLastNearerToTargetTime, FIELD_TIME ),
	DEFINE_FIELD( m_vClingPosition, FIELD_VECTOR ),
	DEFINE_FIELD( m_fClingTimeOut, FIELD_TIME ),

	DEFINE_FIELD( m_eBlobStalkState, FIELD_INTEGER ),
	DEFINE_FIELD( m_hStalkTarget, FIELD_EHANDLE	),
	DEFINE_FIELD( m_hNextStalkTarget, FIELD_EHANDLE	),
	DEFINE_FIELD( m_bInputStalkTarget, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecStalkGoal, FIELD_VECTOR ),
	DEFINE_FIELD( m_fLastStalkGoalUpdate, FIELD_TIME ),
	DEFINE_FIELD( m_fLastStalkLungeTime, FIELD_TIME ),
	DEFINE_FIELD( m_bStalkDefensive, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_nStalkCreatedTentacles, FIELD_INTEGER ),
	// AI_Waypoint_t *m_PrevBestWaypoint					// Not saved

	DEFINE_FIELD( m_nBlobsToShakeOff, FIELD_INTEGER ),
	DEFINE_FIELD( m_flNextBlobShakeSpawnTime, FIELD_TIME ),
	DEFINE_FIELD( m_flLastBlobSpawnTime, FIELD_TIME ),
	DEFINE_FIELD( m_flLastDamageEffectTime, FIELD_TIME ),
	DEFINE_FIELD( m_flNextShakeBlobTime, FIELD_TIME ),
	DEFINE_UTLVECTOR( m_hSpawnedBlobs, FIELD_EHANDLE ),

	DEFINE_FIELD( m_flLastPainSoundTime, FIELD_TIME ),

	DEFINE_FIELD( m_vecSaveSpitVelocity, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecSaveSpitTarget, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_hSaveSpitTentaclePos, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bSaveSpitUseTentacle, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_hSpitTentaclePos, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bSpitUseTentacle, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_nSpitCount, FIELD_INTEGER ),
	DEFINE_UTLVECTOR( m_hNearbySpitTargets, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hSpitVisibilityTestEyePos, FIELD_EHANDLE ),

//	DEFINE_FIELD( m_pSpitTentacle, CBlobTentacle* ), FIXME!

	DEFINE_FIELD( m_flNextLeapTime, FIELD_TIME ),
	DEFINE_FIELD( m_flLastLeapTime, FIELD_TIME ),
	DEFINE_FIELD( m_flLeapStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_vecLeapTarget, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_bLeapCompleted, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bSuppressMoveTowardGoal, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bSuppressStragglerCreation, FIELD_BOOLEAN ),

	DEFINE_FIELD( m_flColorChangeStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_flColorChangeEndTime, FIELD_TIME ),
	DEFINE_FIELD( m_vecStartColor, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecEndColor, FIELD_VECTOR ),

	DEFINE_FIELD( m_bCanShakeBlobs, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bCanTentacleAttack, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bCanSpit, FIELD_BOOLEAN ),
	DEFINE_KEYFIELD( m_bCanMerge, FIELD_BOOLEAN, "canMerge" ),
	DEFINE_FIELD( m_bCanRegenerate, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bCanLeap, FIELD_BOOLEAN ),

	DEFINE_FIELD( m_hAutoSplitBlob, FIELD_EHANDLE ),
	DEFINE_FIELD( m_fAutoSplitTimeOut, FIELD_TIME ),
	DEFINE_FIELD( m_hBuddyBlob, FIELD_EHANDLE ),

	DEFINE_SOUNDPATCH( m_pAmbientSound ),

	DEFINE_FIELD( m_bFrozen, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fUnfreezeTime, FIELD_TIME ),
	
	DEFINE_FIELD( m_flNextRegenerateTime, FIELD_TIME ),
	DEFINE_KEYFIELD( m_BlobType, FIELD_INTEGER, "blobType" ),

	// Inputs
	DEFINE_INPUTFUNC( FIELD_STRING, "MoveToPosition", InputMoveToPosition ),
	DEFINE_INPUTFUNC( FIELD_STRING, "TestSplit", InputTestSplit ),
	DEFINE_INPUTFUNC( FIELD_VOID, "UnseenTeleport", InputUnseenTeleport ),
	DEFINE_INPUTFUNC( FIELD_STRING, "OverloadPhysParamState", InputOverloadPhysParamState ),
	DEFINE_INPUTFUNC( FIELD_VOID, "RevertPhysParamState", InputRevertPhysParamState ),
	DEFINE_INPUTFUNC( FIELD_STRING, "ForceThroughPipe", InputForceThroughPipe ),
	DEFINE_INPUTFUNC( FIELD_STRING, "AddShapeTarget", InputAddShapeTarget ),
	DEFINE_INPUTFUNC( FIELD_STRING, "RemoveShapeTarget", InputRemoveShapeTarget ),
	DEFINE_INPUTFUNC( FIELD_STRING, "TentacleAttackEntity", InputTentacleAttackEntity ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "EnableTentacleAttack", InputEnableTentacleAttack ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "EnableShakeBlobs", InputEnableShakeBlobs ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "EnableSpit", InputEnableSpit ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "EnableLeap", InputEnableLeap ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "EnableSpawnRegenerators", InputEnableSpawnRegenerators ),

	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "EnableAutoRegeneration", InputEnableRegeneration ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "RegenerateParticles", InputRegenerateParticles ),

	DEFINE_OUTPUT( m_BlobSplitOutput, "BlobSplit" ),
	DEFINE_OUTPUT( m_BlobFrozenOutput, "BlobFrozen" ),
	DEFINE_OUTPUT( m_BlobUnfrozenOutput, "BlobUnfrozen" ),
	DEFINE_OUTPUT( m_OnBlobsShakenOff, "OnBlobsShakenOff" ),
	DEFINE_OUTPUT( m_OnBrainExposed, "OnBrainExposed" ),
	DEFINE_OUTPUT( m_OnBrainHidden, "OnBrainHidden" ),
	DEFINE_OUTPUT( m_OnThreeQuartersArmor, "OnThreeQuartersArmor" ),

END_DATADESC()


//-----------------------------------------------------------------------------
// Link to Hammer
//-----------------------------------------------------------------------------
LINK_ENTITY_TO_CLASS( npc_blob, CNPC_Blob );
LINK_ENTITY_TO_CLASS( npc_blob_small, CNPC_Blob );
LINK_ENTITY_TO_CLASS( npc_blob_medium, CNPC_Blob );
LINK_ENTITY_TO_CLASS( npc_blob_large, CNPC_Blob );


//-----------------------------------------------------------------------------
// Statics
//-----------------------------------------------------------------------------
bool CNPC_Blob::s_bInitializedRandVectors = false;
Vector CNPC_Blob::s_pRandVectors[BLOB_NUM_RAND_VECTORS];


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
CNPC_Blob::CNPC_Blob( void )
{
	m_BlobType = BLOB_TYPE_GENERIC;
	m_bCanShakeBlobs = true;
	m_bCanTentacleAttack = true;
	m_bCanSpit = true;
	m_bCanLeap = true;
	m_bCanSpawnRegenerators = false;
	m_bCanRegenerate = false;
	m_iDangerSounds = SOUNDLIST_EMPTY;
	m_nAITraceMask = MASK_NPCFLUID;
	m_iCacheNumLargeGroups = 0;
	m_nBrainParticleCount = 6; //default it to have a brain
	m_pBrainConstraintGroup = NULL;
	m_flNextRegenerateTime = 0.0f;
	m_BaseColor.Init( 1, 1, 1 );

	// Try loading physics states from file.
	if ( g_BlobPhysicsParamStates.Count() == 0 )
	{
		LoadBlobPhysicsParameterStates();
	}
	
	// Set the parameter state to default.
	SetPhysParamState( -1 );
	SetPhysParamState( "Default" );
}

CNPC_Blob::~CNPC_Blob( void )
{
	for(int i = 0; i < m_blobParticles.Count(); i++ )
	{
		delete m_blobParticles[i];
	}
	m_blobParticles.RemoveAll();

	for(int i = 0; i < m_blobGroups.Count(); i++ )
	{
		delete m_blobGroups[i];
	}
	m_blobGroups.RemoveAll();

#ifdef _DEBUG
	Assert( m_vecPhysParticles.Count() == 0 );
	m_nActiveParticles = 0;
	m_pMainGroup = NULL;
	AddSolidFlags( FSOLID_NOT_SOLID );
	DebugFullSanityCheck();
#endif
}


//-----------------------------------------------------------------------------
// Precache
//-----------------------------------------------------------------------------
void CNPC_Blob::Precache( void )
{
	BaseClass::Precache();

	UTIL_PrecacheOther( "npc_blob_tiny" );
	UTIL_PrecacheOther( "grenade_spit" );

	PrecacheParticleSystem( "blood_impact_yellow_01" );

	PrecacheScriptSound( "Blob.NavigationBusted" );
	PrecacheScriptSound( "Blob.Regenerate" );
	PrecacheScriptSound( "NPC_Antlion_Grub.Die" );
	PrecacheScriptSound( "NPC_Antlion_Grub.Squish" );
	PrecacheScriptSound( "NPC_Antlion.PoisonShoot" );
	PrecacheScriptSound( "Blob.Pain" );
	PrecacheScriptSound( "Blob.BrainPain" );
	PrecacheScriptSound( "Blob.BrainPainHeavy" );
	PrecacheScriptSound( "NPC_Strider.Death" );
	PrecacheScriptSound( "Blob.SpawnRegenerator" );

	PrecacheScriptSound( "HL2Player.BurnPain" );
	PrecacheScriptSound ( "ChainLink.ImpactHard" );
						  
	PrecacheScriptSound( "Blob.LeapImpact" );
	PrecacheScriptSound( "Blob.AmbientLoop" );
	PrecacheScriptSound( "Blob.Merge" );
	PrecacheScriptSound( "Blob.Split" );
	PrecacheScriptSound( "Blob.EnterPipe" );
	PrecacheScriptSound( "Blob.AnnounceLeap" );
	PrecacheScriptSound( "Blob.Leap" );
	PrecacheScriptSound( "Blob.AnnounceSpit" );
	PrecacheScriptSound( "Blob.AnnounceShake" );
	PrecacheScriptSound( "Weapon_BugBait.Splat" );
	PrecacheScriptSound( "NPC_FastZombie.AttackMiss" );
	PrecacheScriptSound( "NPC_Hunter.TackleHit" );
	PrecacheScriptSound( "GrenadeBugBait.Splat" );

	PrecacheParticleSystem( "GrubSquashBlood" );
	PrecacheParticleSystem( "blob_death" );	
	PrecacheParticleSystem( "blob_split" );
	PrecacheParticleSystem( "blob_impact" );
	PrecacheParticleSystem( "blob_impact_frozen" );
	PrecacheParticleSystem( "AntlionGib" );
	PrecacheParticleSystem( "blood_impact_antlion_worker_01" );
	PrecacheParticleSystem( "striderbuster_break" );


	PrecacheMaterial( "models/blob/blob_outer_material" ); 
	PrecacheMaterial( "models/blob/blob_inner_material" ); 
	PrecacheMaterial( "models/blob/blob_inner_arms" );
	PrecacheMaterial( "models/blob/blob_back_material" ); 
	PrecacheMaterial( "models/blob/blob_outer_material_frozen" ); 
	PrecacheMaterial( "models/blob/blob_inner_material_frozen" ); 
	PrecacheMaterial( "models/blob/blob_inner_arms_frozen" );
}


//-----------------------------------------------------------------------------
// Spawn
//-----------------------------------------------------------------------------
void CNPC_Blob::Spawn( void )
{
	m_flNextSpawnRegeneratorTime = gpGlobals->curtime;
	m_bLeapCompleted = true;
	m_bSuppressMoveTowardGoal = false;
	m_bSuppressStragglerCreation = false;
	m_nNeighborForceMode = NEIGHBOR_FORCE_NORMAL;
	m_flColorChangeStartTime = m_flColorChangeEndTime = gpGlobals->curtime;
	m_vecStartColor.Init( 1, 1, 1 );
	m_vecEndColor = m_vecStartColor;
	m_flLastLeapTime = gpGlobals->curtime;
	m_flNextLeapTime = gpGlobals->curtime;
	m_flLastNeighborOutwardForceFactor = 0.0f;
	m_flLastNeighborInwardForceFactor = 0.0f;
	SetViewOffset( Vector( 0, 0, 24.0f ) );

	// Deal with spawning blobs of specific size
	const char *pClassname = GetClassname();
	if ( Q_stricmp( pClassname, "npc_blob_small" ) == 0 )
	{		   
		SetClassname( "npc_blob" );
		m_nActiveParticles = ( BLOB_SIZE_SMALL + BLOB_SIZE_TINY ) / 2;
	}
	else if ( Q_stricmp( pClassname, "npc_blob_medium" ) == 0 )
	{
		SetClassname( "npc_blob" );
		m_nActiveParticles = ( BLOB_SIZE_MEDIUM + BLOB_SIZE_SMALL ) / 2;
	}
	else if ( Q_stricmp( pClassname, "npc_blob_large" ) == 0 )
	{
		SetClassname( "npc_blob" );
		m_nActiveParticles = ( BLOB_SIZE_LARGE + BLOB_SIZE_MEDIUM ) / 2;
	}

	m_nMaxParticleCount = m_nActiveParticles;

	BaseClass::Spawn( );

	switch ( m_BlobType )
	{
	case BLOB_TYPE_GENERIC:
	default:
		m_flMaxArmorHealth = m_flArmorHealth = sv_blob_health.GetFloat();
		break;

	case BLOB_TYPE_LEAPER:
		m_bCanLeap = true;
		m_bCanRegenerate = m_bCanSpit = m_bCanShakeBlobs = m_bCanTentacleAttack = m_bCanSpawnRegenerators = false;
		m_flMaxArmorHealth = m_flArmorHealth = sv_blob_leaper_health.GetFloat();
		break;

	case BLOB_TYPE_SPAWNER:
		m_bCanShakeBlobs = true;
		m_bCanRegenerate = m_bCanSpit = m_bCanLeap = m_bCanTentacleAttack = m_bCanSpawnRegenerators = false;
		m_flMaxArmorHealth = m_flArmorHealth = sv_blob_spawner_health.GetFloat();
		break;
		
	case BLOB_TYPE_SPITTER:
		m_bCanSpit = true;
		m_bCanRegenerate = m_bCanShakeBlobs = m_bCanLeap = m_bCanTentacleAttack = m_bCanSpawnRegenerators = false;
		m_flMaxArmorHealth = m_flArmorHealth = sv_blob_health.GetFloat();
		break;
	}
								 
	UpdateBaseColor();
	m_takedamage = DAMAGE_YES;

	SetHullType( HULL_TINY_FLUID );
	SetSolid( SOLID_VPHYSICS );
	SetMoveType( MOVETYPE_VPHYSICS );
	AddEffects( EF_NOSHADOW );

	// Gets DrawDebugGeometryOverlays called every tick:
	m_debugOverlays |= OVERLAY_PIVOT_BIT;

	SetHealth( sv_blob_brain_health.GetInt() );

	CapabilitiesClear();
	CapabilitiesAdd( BLOB_MOVE_CAPS );
	CapabilitiesAdd( bits_CAP_INNATE_MELEE_ATTACK1 | bits_CAP_INNATE_RANGE_ATTACK1 | bits_CAP_SQUAD );
	SetCollisionGroup( HL2COLLISION_GROUP_BLOB ); 

	GetEnemies()->SetEnemyDiscardTime( 15.0f );
	GetEnemies()->SetFreeKnowledgeDuration( 14.8f );

	m_fLastAverageParticleNeighbors = 1.0f;
	m_fLastInteractionScale = 1.0f;
	m_flLastPainSoundTime = -1000.0f;
	m_flLastDamageEffectTime = -1000.0f;
	m_fSpawnTimeStamp = gpGlobals->curtime;
	m_fLastSeenTime = gpGlobals->curtime;

	m_vecTypes.EnsureCount( MAX_SURFACE_ELEMENTS );
	m_vecParents.EnsureCount( MAX_SURFACE_ELEMENTS );
	m_vecUVW.EnsureCount( MAX_SURFACE_ELEMENTS );
	m_iFlags.EnsureCount( MAX_SURFACE_ELEMENTS );

	for (int i = 0; i < MAX_SURFACE_ELEMENTS; i++)
	{
		m_vecTypes[i] = 0;
		m_vecParents[i] = i; // parent == myself implies no parent
		m_vecUVW[i].Init();
		m_iFlags[i] = 0;
	}

	m_vecLastMoveGoal = GetAbsOrigin();			// Networked
	SetMoveGoal( GetAbsOrigin() );

	m_fBrainExposure = 0.0f;									// Networked
	m_fTentacleTimeout = 0;
	m_fTentacleDamageTimeout = 0;
	m_fVulnerableTimeout = 0;
	m_fLastDamagedPlayer = 0;
	m_vecParticleImpulse = vec3_origin;
	m_vecParticleImpulseSpread = vec3_origin;
	m_fStunStartTime = -1000.0f;
	m_fUnfreezeTime = 0;

	m_fMinDistToTarget = 0;
	m_fLastNearerToTargetTime = 0;

	m_eBlobStalkState = BLOB_STALK_NONE;
	m_bInputStalkTarget = false;
	m_fLastStalkGoalUpdate = 0;
	m_fLastStalkLungeTime = 0;
	m_nStalkCreatedTentacles = 0;
	m_PrevBestWaypoint = NULL;

	m_flNextShakeBlobTime = gpGlobals->curtime;
	m_nBlobsToShakeOff = 0;

	m_bFrozen = false;											// Networked
}


//-----------------------------------------------------------------------------
// Called when the NPC is removed
//-----------------------------------------------------------------------------
void CNPC_Blob::UpdateOnRemove()
{
	if ( m_pBrainConstraintGroup )
	{
		physenv->DestroyConstraintGroup( m_pBrainConstraintGroup );
		m_pBrainConstraintGroup = 0;
	}

	BaseClass::UpdateOnRemove();
}


//-----------------------------------------------------------------------------
// Activate
//-----------------------------------------------------------------------------
void CNPC_Blob::Activate( void )
{
	BaseClass::Activate();

	AddPipeEntrancesToSenses();

	m_PrevBestWaypoint = NULL; // Not saved

	CreateSounds();
}


//-----------------------------------------------------------------------------
// OnSave/OnRestore
//-----------------------------------------------------------------------------
void CNPC_Blob::OnSave( IEntitySaveUtils *pUtils )
{
	BaseClass::OnSave( pUtils );

	// Destroy tentacle groups/particles - they'll get reconstructed on restore
	for ( int i = 0; i < BLOB_MAX_TENTACLES; i++ )
	{
		TentacleDestroyParticles( m_tentacles[i] );
	}
}

void CNPC_Blob::OnRestore( void )
{
	BaseClass::OnRestore();

	bool bOverloaded = m_bPhysParamStateOverloaded;

	m_bPhysParamStateOverloaded = false;
	SetPhysParamState( -1 );
	SetPhysParamState( m_iBlobPhysParamState );
	m_bPhysParamStateOverloaded = bOverloaded;
}


//-----------------------------------------------------------------------------
// Looping sound create/destroy
//-----------------------------------------------------------------------------
void CNPC_Blob::CreateSounds()
{
	if (!m_pAmbientSound)
	{
		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		CPASAttenuationFilter filter( this );
		m_pAmbientSound = controller.SoundCreate( filter, entindex(), "Blob.AmbientLoop" );
		controller.Play( m_pAmbientSound, 0.5f, 100.0f, 1.0f );
	}
}

void CNPC_Blob::StopLoopingSounds()
{
	if ( m_pAmbientSound )
	{
		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
		controller.SoundDestroy( m_pAmbientSound );
		m_pAmbientSound = NULL;
	}

	BaseClass::StopLoopingSounds();
}


//-----------------------------------------------------------------------------
// This function creates a CBlobParticle along with an associated
// VPhysics particle. The game index is set to USHRT_MAX and collisions
// are turned off so the particle can be placed at any position.
//-----------------------------------------------------------------------------
CBlobParticle* CNPC_Blob::CreateParticle(const Vector &pos, const Vector &velocity, float mass)
{
	// Set up VPhysics object
	IPhysicsObject* v = CreateParticlePhysics();
	Assert(v != NULL);
	if(v == NULL) return NULL;

	v->SetGameData( NULL ); //particle isn't actually part of the blob yet
	v->EnableCollisions(false); // Do this before setting position, in case we penetrate something (can't do that w/ NULL gamedata!)
	v->SetPosition( pos, GetAbsAngles(), true );
	v->SetVelocity( &velocity, NULL );
	SetParticleEntityIndex( v, USHRT_MAX );
	v->SetMass( mass ); // TODO: Mass should be set based on density/volume.
	v->EnableGravity( false );
	v->EnableDrag( false );
	// v->SetContents( CONTENTS_GRATE );
	// v->EnableMotion( false );
	float flDamping = 0.0f; // was 0.5f 
	float flAngDamping = 0.0f; // was 0.5f
	v->SetDamping( &flDamping, &flAngDamping );
	// v->SetInertia( Vector( 1e30, 1e30, 1e30 ) );

	// Set up CBlobParticle
	CBlobParticle* p = new CBlobParticle();

	p->m_vphysParticle = v;
	p->m_mass = mass;
	p->m_support = 0.0f;
	p->m_position = pos;
	p->m_velocity = velocity;
	p->m_flBirthTime = gpGlobals->curtime;

	return p;
}


//-----------------------------------------------------------------------------
// Destroys a particle
//-----------------------------------------------------------------------------
void CNPC_Blob::DestroyParticle(CBlobParticle* p)
{
	// TODO: Assert that this particle is ready for deletion!!!
	DestroyParticlePhysics(p->m_vphysParticle);
	delete p;
}


//-----------------------------------------------------------------------------
// Adds a particle to a given group. This can be used to add a particle
// to a group in a different entity.
//-----------------------------------------------------------------------------
void CNPC_Blob::ActivateParticle(CBlobGroup* g, CBlobParticle* p)
{
	Assert( !IsMarkedForDeletion() );
	Assert(p != NULL && g != NULL);

	IPhysicsObject* v = p->m_vphysParticle;
	Assert(v != NULL);
	Assert(s_ExpandedParticleData[v->GetGameIndex()].iEntityIndex == USHRT_MAX); // Check that the particle is not inserted
	Assert(m_vecPhysParticles.Count() == m_blobParticles.Count()); // Make sure both arrays have the same size
	Assert(m_vecPhysParticles.Count() == m_nActiveParticles);
	Assert(p->m_group == NULL);
	Assert(p->m_parent == NULL);

	int vindex = m_vecPhysParticles.AddToTail(v);
	m_blobParticles.AddToTail(p);
	++m_nActiveParticles; //good idea to update this before changing solid flags

	v->SetGameData( this );
	SetParticleEntityIndex( v, vindex );
	v->EnableCollisions(true);
	v->EnableGravity( false );

	FreezeParticle( p );

	if( vindex == 0 )
	{
		VPhysicsSetObject( v );
		RemoveSolidFlags( FSOLID_NOT_SOLID );
	}

	if(p->m_type == ROOT_PARTICLE)
	{
		p->m_parent = NULL;
		Assert( g->m_root == NULL );
		if ( g->m_root == NULL )
		{
			g->m_root = p;
		}
	}
	else if(g->m_members.Count() == 0)
	{
		p->m_parent = NULL;
		p->m_type = ROOT_PARTICLE;
		g->m_root = p;
	}
	else
	{
		// NOTE: This just bonds particles to the nearest
		// particle in a given group. Many of these bonds
		// can subsequently break if the particle is too far
		// away to be attracted by the group.
		CBlobParticle* nearestMember = ClosestBlobParticle( p->m_position, g->m_members.Base(), g->m_members.Count() );

		Assert(nearestMember != NULL);
		p->m_parent = nearestMember;
	}

	p->m_group = g;
	p->m_pgoal = NULL;
	g->m_particlesByType[p->m_type].AddToTail(p);
	g->m_members.AddToTail(p);
	p->m_nWaypointTargetIndex = -1;

	DebugFullSanityCheck();
}

// Removes a particle from it's group and it's entity.
// Turns off simulation on the VPhysicsParticle.
void CNPC_Blob::DeactivateParticle(CBlobParticle* p)
{
	// FIXME: This method duplicates all the code in RemoveParticles! (see that method for details)

	Assert(p != NULL);
	Assert(p->m_group != NULL);

	IPhysicsObject* v = p->m_vphysParticle;
	Assert(v != NULL);

	int index = s_ExpandedParticleData[v->GetGameIndex()].iEntityIndex;

	UnfreezeParticle( p );


	Assert(m_blobParticles[index] == p);
	Assert(m_vecPhysParticles[index] == v);
	Assert(m_vecPhysParticles.Count() == m_blobParticles.Count()); // Make sure both arrays have the same size
	Assert(m_vecPhysParticles.Count() == m_nActiveParticles);

	m_vecPhysParticles.FastRemove(index);
	m_blobParticles.FastRemove(index);

	// If this particle was not the last element in the m_vecPhysParticles array, 
	// then FastRemove will swap in the last element in it's place. Thus, the last
	// elements index will change. Thus, we have to update the index in the GameData
	// field of the VPhysics object.
	if(index < m_vecPhysParticles.Count())
	{
		SetParticleEntityIndex( m_vecPhysParticles[index], index );
	}
	
	SetParticleEntityIndex( v, USHRT_MAX );
	v->EnableCollisions(false);
	v->EnableGravity( false );
	v->SetGameData( NULL );

	CBlobGroup* g = p->m_group;
	Verify(g->m_members.FindAndRemove(p));
	Verify(g->m_particlesByType[p->m_type].FindAndRemove(p));
	--m_nActiveParticles;

	// remove the particle from neighbor listings
	// NOTE: we have to iterate over all particles in the blob, since neighbour links can cross
	//       group boundaries (e.g. due to tentacles, which are each a separate group)
	for( int j = m_blobParticles.Count(); --j >= 0; )
	{
		SmartArray<BlobNeighbor_t> &neighbors = m_blobParticles[j]->m_neighbors;
		for( int k = neighbors.size; --k >= 0; )
		{
			if( neighbors[k].particle == p )
			{
				neighbors.remove( k );
				break;
			}
		}
	}

	p->m_neighbors.size = 0;
	

	if(g->m_members.Count() == 0)
	{
		// If group is empty, delete it.
		g->m_root = NULL;
		Verify(m_blobGroups.FindAndRemove(g));
		if( g == m_pMainGroup )
			m_pMainGroup = NULL;

		delete g;
	}
	else
	{
		CBlobParticle* parent = p->m_parent;

		// If the root is removed, try to find another particle
		// closeby to be the root and make that particle the root.
		if (parent == NULL || p->m_type == ROOT_PARTICLE)
		{
			Assert(p->m_type == ROOT_PARTICLE);

			parent = ClosestBlobParticle( p->m_position, g->m_members.Base(), g->m_members.Count() );
 			if ( parent->m_type != ROOT_PARTICLE )
			{
				ChangeParticleType( parent, ROOT_PARTICLE );
			}
			parent->m_parent = NULL;
			parent->m_pgoal = NULL;
			g->m_root = parent;
		}

		// For all children of this particle, reattach them to the
		// closest particle to this particle.
		// Any particle that was a child of the deleted particle
		// becomes a child of the deleted particle's parent.
		for(int i = 0; i < g->m_members.Count(); i++)
		{
			CBlobParticle* m = g->m_members[i];
			if(m != parent && m->m_parent == p)
			{
				m->m_parent = parent;
			}
		}
	}

	p->m_parent = NULL;
	p->m_group = NULL;
	p->m_pgoal = NULL;

	UpdateEntityPhysicsObject();
	DebugFullSanityCheck();
}

// This teleports a particle and can also change it's position.
// Note that this should only be called on a particle that has
// been removed from it's group.
void CNPC_Blob::TeleportParticle(CBlobParticle* p, const Vector &newPos, const Vector *pNewVel )
{
	Assert(p->m_group == NULL);
	Assert(s_ExpandedParticleData[p->m_vphysParticle->GetGameIndex()].iEntityIndex == USHRT_MAX);
	Assert(p->m_vphysParticle->IsCollisionEnabled() == false);

	p->m_vphysParticle->SetPosition(newPos, QAngle(0.0f, 0.0f, 0.0f), true);
	p->m_position = newPos;
	if(pNewVel)
	{
		p->m_vphysParticle->SetVelocity(pNewVel, NULL);
		p->m_velocity = *pNewVel;
	}
}

// Init a particle's physics state, based on whether/how the blob is frozen
void CNPC_Blob::FreezeParticle( CBlobParticle *pParticle )
{
	// UNDONE: move the freeze functionality to CNPC_Surface
	IPhysicsObject* pPhysObj = pParticle->m_vphysParticle;
	Assert( pPhysObj );
	if ( pPhysObj )
	{
#if defined( BLOB_RIGID_BODY_FREEZE )
		if ( m_bFrozen && ( pPhysObj != m_vecPhysParticles[0] ) )
		{
			pPhysObj->WeldToObject( m_vecPhysParticles[0] );
		}
		pPhysObj->EnableGravity( m_bFrozen ); //use gravity when frozen for rigid body coolness, supplying our own fake gravity when not frozen
#else
		pPhysObj->EnableMotion( !m_bFrozen );
#endif
	}
}

// Reset a particle to an unfrozen physics state
void CNPC_Blob::UnfreezeParticle( CBlobParticle *pParticle )
{
	// UNDONE: move the freeze functionality to CNPC_Surface
	IPhysicsObject* pPhysObj = pParticle->m_vphysParticle;
	Assert( pPhysObj );
	if ( pPhysObj )
	{
#if defined( BLOB_RIGID_BODY_FREEZE )
 		if ( m_bFrozen )
		{
			pPhysObj->RemoveAllWelds();
		}
		pPhysObj->EnableGravity( false );
#else
		pPhysObj->EnableMotion( true );
		pPhysObj->Wake();
#endif
	}
}

// Updates the entity's physics object to always be the first entry in m_vecPhysParticles
void CNPC_Blob::UpdateEntityPhysicsObject( void )
{
	if ( m_nActiveParticles == 0 )
	{
		// No particles -> no physobj
		VPhysicsSetObject( NULL );
		AddSolidFlags( FSOLID_NOT_SOLID );
	}
	else if ( VPhysicsGetObject() == NULL )
	{
		// Particles must just have been added to the blob, set the physobj
		// NOTE: calling code should call FreezeParticle for each added particle
		VPhysicsSetObject( m_vecPhysParticles[0] );
		RemoveSolidFlags( FSOLID_NOT_SOLID );
	}
	else if ( VPhysicsGetObject() != m_vecPhysParticles[0] )
	{
		// The first entry in the array must have changed, swap the physobj
		VPhysicsSwapObject( m_vecPhysParticles[0] );

#if defined( BLOB_RIGID_BODY_FREEZE )
		if ( m_bFrozen )
		{
			//crap, reset all freeze constraints for this blob
			for( int i = 0; i < m_nActiveParticles; ++i )
			{
				m_vecPhysParticles[i]->RemoveAllWelds();
			}
			for( int i = 1; i < m_nActiveParticles; ++i )
			{
				m_vecPhysParticles[i]->WeldToObject( m_vecPhysParticles[0] );
			}
		}
#endif
	}
}

void CNPC_Blob::TransferGroupToDifferentEntity(CBlobGroup* group, CNPC_Blob* other)
{
	if ( group == m_pMainGroup )
		return;

	Assert(group->m_members.Count() > 0); // Sanity check
	Assert(group->m_members.Count() + other->m_nActiveParticles <= MAX_SURFACE_ELEMENTS);

	// FIXME: for now, just destroy tentacle groups on transfer...
	if ( group->m_bIsTentacle )
	{
		// The group will be destroyed automatically once all of its particles are removed
		for ( int i = ( group->m_members.Count() - 1 ); i >= 0; i-- )
		{
			CBlobParticle *particle = group->m_members[ i ];
			DeactivateParticle( particle );
			DestroyParticle( particle );
		}
	}

	// Take from the rich...
	m_blobGroups.FindAndRemove(group);

	for(int i = 0; i < group->m_members.Count(); i++)
	{
		CBlobParticle* p = group->m_members[i];
		int index = s_ExpandedParticleData[p->m_vphysParticle->GetGameIndex()].iEntityIndex;
		UnfreezeParticle( p );
		m_blobParticles.FastRemove(index);
		m_vecPhysParticles.FastRemove(index);
		if(index < m_vecPhysParticles.Count())
		{
			other->m_iParticlePositionIndex[other->m_nActiveParticles + i] = m_iParticlePositionIndex[index];
			SetParticleEntityIndex( m_vecPhysParticles[index], index );
		}
	}

	m_nActiveParticles -= group->m_members.Count();
	UpdateEntityPhysicsObject();

	// ...and give to the poor:
	other->m_blobGroups.AddToTail(group);
	for(int i = 0; i < group->m_members.Count(); i++)
	{
		CBlobParticle* p = group->m_members[i];
		int index = other->m_blobParticles.AddToTail(p);
		other->m_vecPhysParticles.AddToTail(p->m_vphysParticle);
		other->SetParticleEntityIndex( p->m_vphysParticle, index );
		p->m_vphysParticle->SetGameData(other);
		other->FreezeParticle( p );
		p->m_nWaypointTargetIndex = -1;
	}

	other->m_nActiveParticles += group->m_members.Count(); //important to set this before changing solid state below
	other->UpdateEntityPhysicsObject();

	DebugFullSanityCheck();
	other->DebugFullSanityCheck();
}


// This function sanity checks groups and members of groups.
// It makes sure that:
// 1) There are no empty groups.
// 2) The particlesByType array in each group is properly updated.
// 3) The number of particles in particlesByType matches that of neighbors array.
// 4) The group pointer in each particle points to the group that holds that particle and vice versa.
// 5) The number of particles in all the groups matches the number of particles in the entity.
// 6) Every particle that is a ROOT has no parents
// 7) Every particle that is not a ROOT has parents
void CNPC_Blob::DebugSanityCheckGroups()
{
#if defined(_DEBUG)
	int totalParticles = 0;
	bool bFoundLargestGroup = (m_pMainGroup == NULL);
	for(int i = 0; i < m_blobGroups.Count(); i++)
	{
		CBlobGroup* group = m_blobGroups[i];
		AssertMsg( group->m_members.Count() > 0, "Group with 0 particles");
		
		bFoundLargestGroup |= (group == m_pMainGroup);
		
		int totalParticlesInGroup = 0;
		for(int j = 0; j < NUM_PARTICLE_TYPES; j++)
		{
			CUtlVector<CBlobParticle*> &particles = group->m_particlesByType[j];
			totalParticlesInGroup += particles.Count();
			for(int k = 0; k < particles.Count(); k++)
			{
				AssertMsg( particles[k]->m_type == j, "particleByTypes array has a particle of the wrong type" );
			}
		}

		AssertMsg(totalParticlesInGroup == group->m_members.Count(), "The total number of particles in group is not equal to the number of particles in particlesByType" );
		totalParticles += totalParticlesInGroup;

		for(int j = 0; j < group->m_members.Count(); j++)
		{
			AssertMsg( group->m_members[j]->m_group == group, "The group field of the particle did not point to a group that contained the particle" );

		}
	}

	AssertMsg( bFoundLargestGroup, "Largest particle group not owned by blob" );

	AssertMsg(totalParticles == m_blobParticles.Count(), "The total number of particles in all groups is not equal to the number of particles in this entity" );

	for(int i = 0; i < m_blobParticles.Count(); i++)
	{
		CBlobParticle* p = m_blobParticles[i];
		if(p->m_parent == NULL)
		{
			AssertMsg(p->m_type == ROOT_PARTICLE, "A particle with no parent should always be of type ROOT");
		}
		else
		{
			AssertMsg(p->m_type != ROOT_PARTICLE, "A particle with a parent can not be of type ROOT");
		}
	}
#endif
}

void CNPC_Blob::DebugSanityCheckVPhysics( void )
{
#if defined(_DEBUG)
	Assert( m_nActiveParticles == m_vecPhysParticles.Count() );

	for( int i = 0; i != m_nActiveParticles; ++i )
	{
		AssertMsg( m_vecPhysParticles[i]->GetGameData() == this, "Blob particle not owned by owning blob" );
	}

	int iNumPhysObjects = 0;
	const IPhysicsObject **pAllPhysObjects = physenv->GetObjectList( &iNumPhysObjects );

	for( int i = 0; i != iNumPhysObjects; ++i )
	{
		if( pAllPhysObjects[i]->GetGameData() == this )
		{
			//find it in our arrays
			int j;
			for( j = m_vecPhysParticles.Count(); --j >= 0; )
			{
				if( m_vecPhysParticles[j] == pAllPhysObjects[i] )
				{
					break;
				}
			}
			AssertMsg( j >= 0, "Physics has an object of this blob that the blob is not aware of" );
		}
	}

	if( m_nActiveParticles != 0 )
	{
		Assert( VPhysicsGetObject() == m_vecPhysParticles[0] );
		Assert( (GetSolidFlags() & FSOLID_NOT_SOLID) == 0 );
	}
	else
	{
		Assert( (GetSolidFlags() & FSOLID_NOT_SOLID) != 0 );
	}
#endif
}

static bool BlobParticlePositionIsValid( CBlobParticle *pParticle )
{
	int ignoreFlags = BLOB_PARTICLE_STRAGGLER | BLOB_PARTICLE_TENTACLE | BLOB_PARTICLE_BIRTH;
	return (	pParticle->m_position.IsValid() &&
				TEST_COORD( pParticle->m_position ) &&
				( ( pParticle->m_flags & ignoreFlags ) || !enginetrace->PointOutsideWorld( pParticle->m_position ) ) );
}

void CNPC_Blob::DebugFullSanityCheck( void )
{
#if defined(_DEBUG)
	DebugSanityCheckVPhysics();
	DebugSanityCheckGroups();

	for( int i = 0; i != m_nActiveParticles; ++i )
	{
		Assert( m_iParticlePositionIndex[i] == s_ExpandedParticleData[m_vecPhysParticles[i]->GetGameIndex()].iNetworkBypassIndex );
		Assert( BlobParticlePositionIsValid( m_blobParticles[i] ) );
	}
#endif
}


//-----------------------------------------------------------------------------
// Initializes random vectors
//-----------------------------------------------------------------------------
void CNPC_Blob::InitializeRandomVectors()
{
	if ( s_bInitializedRandVectors )
		return;
	s_bInitializedRandVectors = true;

	// It would probably be a good idea to apply a repulsion algorithm
	// to get these vectors to not have similar directions. Furthermore,
	// they should be hardcoded into a static array. It might be a useful
	// general feature to be able to get random vectors on a unit sphere
	// that are equally distributed in different directions.
	for(int i = 0; i < BLOB_NUM_RAND_VECTORS; i++)
	{
		s_pRandVectors[i] = RandomVector( -1.0f, 1.0f ).Normalized();
	}
}


//-----------------------------------------------------------------------------
// Does the blob have a brain?
//-----------------------------------------------------------------------------
bool CNPC_Blob::HasBrain() const
{
	return ( m_nBrainParticleCount > 0 );
}


//-----------------------------------------------------------------------------
// Creates brain particles
//-----------------------------------------------------------------------------
void CNPC_Blob::CreateBrain()
{
	if ( m_nBrainParticleCount <= 0 )
		return;

	// Make a new brain
	Assert( m_fBrainExposure == 0 );
	CBlobGroup *pBrainGroup = m_pMainGroup ? m_pMainGroup : m_blobGroups[0];
	if ( !pBrainGroup || !pBrainGroup->m_root )
	{
		m_nBrainParticleCount = 0;
		return;
	}
	int nCount = pBrainGroup->m_members.Count();
	if ( nCount < m_nBrainParticleCount )
	{
		m_nBrainParticleCount = nCount;
	}

	// The particles closest to the root particle will be identifed as the brain
	CBlobParticle **ppSortedParticles = (CBlobParticle **)stackalloc( nCount * sizeof(CBlobParticle *) );
	GetParticlesSortedByProximity( ppSortedParticles, pBrainGroup, pBrainGroup->m_root->m_position );
	
	CBlobParticle **ppBrainParticles = (CBlobParticle **)stackalloc( m_nBrainParticleCount * sizeof(CBlobParticle *) );
	int nBrainParticleCount = 0;
	for ( int i = 0; ( i < nCount ) && ( nBrainParticleCount < m_nBrainParticleCount ); ++i )
	{
		CBlobParticle *pParticle = ppSortedParticles[ i ];
		pParticle->m_flags |= BLOB_PARTICLE_BRAIN;
		ppBrainParticles[ nBrainParticleCount++ ] = pParticle;
	}
	m_nBrainParticleCount = nBrainParticleCount;

	// Brains will be constrained to each other

	// Create our constraint group
	constraint_groupparams_t group;
	group.Defaults();
	m_pBrainConstraintGroup = physenv->CreateConstraintGroup( group );

	// Create constraints to neighbors + particles across the list
	Vector vecPos1, vecPos2;
	int nOffset = m_nBrainParticleCount / 2;
	constraint_lengthparams_t info;
	info.Defaults();
	for ( int i = 0; i < m_nBrainParticleCount; ++i )
	{					  
		int iNext = (i+1) % m_nBrainParticleCount;
		IPhysicsObject* pThis = ppBrainParticles[i]->m_vphysParticle;
		IPhysicsObject* pNext = ppBrainParticles[iNext]->m_vphysParticle;
		info.Init( pThis, pNext, 1.732f * m_flRadius );
		info.minLength = m_flRadius * 0.4f;
		info.constraint.bodyMassScale[0] = ( ppBrainParticles[i] == pBrainGroup->m_root ) ? 1e10f : 1.0f;
		info.constraint.bodyMassScale[1] = ( ppBrainParticles[iNext] == pBrainGroup->m_root ) ? 1e10f : 1.0f;
		physenv->CreateLengthConstraint( pThis, pNext, m_pBrainConstraintGroup, info );

		if ( nOffset + i < m_nBrainParticleCount )
		{
			IPhysicsObject* pAcross = ppBrainParticles[ nOffset + i ]->m_vphysParticle;
			info.Init( pThis, pAcross, 1.732 * m_flRadius );
			info.minLength = m_flRadius * 0.4f;
			info.constraint.bodyMassScale[1] = ( ppBrainParticles[nOffset + i] == pBrainGroup->m_root ) ? 1e10f : 1.0f;
			physenv->CreateLengthConstraint( pThis, pAcross, m_pBrainConstraintGroup, info );
		}
	}
	m_pBrainConstraintGroup->Activate();

	// Free memory
	stackfree( ppSortedParticles );
	stackfree( ppBrainParticles );
}


//-----------------------------------------------------------------------------
// Called from npc_surface, spawns the initial blob particles
//-----------------------------------------------------------------------------
void CNPC_Blob::CreateBlobPhysics()
{
	SetMoveType( MOVETYPE_VPHYSICS );
	CollisionProp()->SetSurroundingBoundsType( USE_GAME_CODE );
	//SetCollisionGroup( HL2COLLISION_GROUP_BLOB );
	AddSolidFlags( FSOLID_NOT_SOLID ); //until taken back off
				   
	if ( m_nActiveParticles > 0 )
	{
		// I do this because adding a particle increase m_nActiveParticles
		int nParticlesToCreate = m_bStartAsBrain ? m_nBrainParticleCount : m_nActiveParticles;
		m_nActiveParticles = 0;

		DebugFullSanityCheck();

		CBlobGroup* newGroup = new CBlobGroup();
		m_pMainGroup = newGroup;
		m_blobGroups.AddToTail(newGroup);

		for(int i = 0; i < nParticlesToCreate; i++)
		{
			Vector vecParticleOrigin = Vector( RandomFloat( -1, 1 ), RandomFloat( -1, 1 ), RandomFloat( 0, 2 ) );
			Vector vecParticleVelocity = Vector( RandomFloat( -1, 1 ), RandomFloat( -1, 1 ), RandomFloat( -1, 1 ) ); 
			vecParticleOrigin += GetAbsOrigin();
			vecParticleVelocity *= 10.0f;
			CBlobParticle* pNewParticle = CreateParticle( vecParticleOrigin, vecParticleVelocity );
			Assert(pNewParticle);
			if ( pNewParticle == NULL )
				continue;
			pNewParticle->m_type = (i == 0) ? ROOT_PARTICLE : SKIN_PARTICLE;
			ActivateParticle( newGroup, pNewParticle );
		}

		CreateBrain();
		RecenterAbsOrigin( );
	}

	InitializeRandomVectors();
	m_randVectorIndex = 0;

	DebugFullSanityCheck();
}

static int BlobParticleRootDistSort( const void *a, const void *b )
{
	const BlobRootDistSortKey *pKeyA = (const BlobRootDistSortKey *)a;
	const BlobRootDistSortKey *pKeyB = (const BlobRootDistSortKey *)b;

	return ( pKeyA->distToRoot == pKeyB->distToRoot ) ? 0 : ( ( pKeyA->distToRoot < pKeyB->distToRoot ) ? -1 : +1 );
}

// This function tries to modify particle types in the least destructive way, in other words,
// demote or promote particles to a type of the closest rank.
void CNPC_Blob::OrganizeParticleStructure(CBlobGroup* group)
{
	int groupSize = group->m_members.Count();
	if( groupSize != 0 )
	{
		int desiredCountByType[NUM_PARTICLE_TYPES];

		// TODO (Ilya): Need to choose number of particles of each type more intelligently based
		// on size of blob, and what it's trying to do.
		int particlesLeft = groupSize;
		desiredCountByType[ROOT_PARTICLE] = 1;
		int iNonRootParticles = groupSize - desiredCountByType[ROOT_PARTICLE];
		particlesLeft -= desiredCountByType[ROOT_PARTICLE];
		desiredCountByType[CORE_PARTICLE] =		MIN( RoundInt(float(iNonRootParticles) * 0.05f), particlesLeft);
		particlesLeft -= desiredCountByType[CORE_PARTICLE];
		desiredCountByType[STICKY_PARTICLE] =	MIN( RoundInt(float(iNonRootParticles) * 0.50f), particlesLeft);
		particlesLeft -= desiredCountByType[STICKY_PARTICLE];
		desiredCountByType[SKIN_PARTICLE] =		MIN( RoundInt(float(iNonRootParticles) * 0.35f), particlesLeft);
		particlesLeft -= desiredCountByType[SKIN_PARTICLE];
		desiredCountByType[TENTACLE_PARTICLE] = MIN( RoundInt(float(iNonRootParticles) * 0.10f), particlesLeft);
		particlesLeft -= desiredCountByType[TENTACLE_PARTICLE];
		desiredCountByType[UNDEFINED_PARTICLE] = 0;
		particlesLeft -= desiredCountByType[UNDEFINED_PARTICLE];

		if(particlesLeft > 0)
		{
			desiredCountByType[SKIN_PARTICLE] += particlesLeft;
			particlesLeft = 0;
		}

		Assert(particlesLeft == 0);

		// Distribute types based on the distance (via parent links) to the root
		BlobRootDistSortKey *sortArray = (BlobRootDistSortKey *)stackalloc( groupSize*sizeof( BlobRootDistSortKey ) );
		Assert( sortArray );
		if ( !sortArray )
			return;

		for ( int i = 0; i < groupSize; i++ )
		{
			sortArray[ i ].particle   = group->m_members[ i ];
			sortArray[ i ].distToRoot = GetDistanceToParticleRoot( group->m_members[ i ] );
		}
		qsort( sortArray, groupSize, sizeof( BlobRootDistSortKey ), BlobParticleRootDistSort );

		// First entry should be the root
		Assert( ( sortArray[ 0 ].particle == group->m_root ) && ( sortArray[ 0 ].particle->m_type == ROOT_PARTICLE ) );

		int count = 0;
		for ( int i = 0; i < NUM_PARTICLE_TYPES; i++ )
		{
			group->m_particlesByType[ i ].RemoveAll();

			for ( int j = 0; j < desiredCountByType[ i ]; j++ )
			{
				sortArray[ count ].particle->m_type = (BlobParticleType)i;
				group->m_particlesByType[ i ].AddToTail( sortArray[ count ].particle );
				count++;
			}
		}

		if ( group->m_root->m_type != ROOT_PARTICLE )
		{
			if ( group->m_particlesByType[ROOT_PARTICLE].Count() > 0)
			{
				group->m_root = group->m_particlesByType[ROOT_PARTICLE].Head();
				Assert( group->m_particlesByType[ROOT_PARTICLE].Count() == 1 );
			}
			else
			{
				// This shouldn't happen! Must be a bug in the above code...
				Assert( false );
				group->m_root = NULL;
			}
		}
	}

	DebugSanityCheckGroups();
}

// UpdateParticleGoal attracts each particles to the nearest particle of the next
// type down - the result being that particle types form 'shells' around the root.
void CNPC_Blob::UpdateParticleGoal(CBlobGroup* group)
{
	int groupSize = group->m_members.Count();
	if( groupSize == 0 )
		return;

	// Particles are attracted strongly to their parents (trying to keep the blob more coherent, to aid navigtion)
	for ( int i = 0; i < groupSize; i++ )
	{
		CBlobParticle *b1 = group->m_members[ i ];
		b1->m_pgoal = b1->m_parent ? b1->m_parent : b1;
	}
}

bool CNPC_Blob::IsObstacleBetwParticles(CBlobParticle* p1, CBlobParticle* p2)
{
	trace_t tr;
	UTIL_TraceLine( p1->m_position, p2->m_position, GetAITraceMask(), this, COLLISION_GROUP_NONE, &tr );
	return (tr.fraction < 1.0f);
}

bool CNPC_Blob::IsChild(CBlobParticle* possibleParent, CBlobParticle* possibleChild)
{
	// TODO: Might not be a bad idea to put a counter here
	// just in case, so we don't get stuck in an infinite loop.
	while(possibleChild->m_parent != NULL)
	{
		Assert( possibleChild->m_parent != possibleChild ); // NOTE: If a node points to itself as a parent, we're screwed.
		if(possibleChild->m_parent == possibleParent) return true;
		else possibleChild = possibleChild->m_parent;
	}

	return false;
}

CBlobParticle* CNPC_Blob::GetParticleRoot( CBlobParticle* p )
{
	// TODO: Might not be a horrible idea to put a counter here
	// just in case, so we don't get stuck in an infinite loop.
	while(p->m_parent != NULL)
	{
		p = p->m_parent;
	}
	return p;
}

float CNPC_Blob::GetDistanceToParticleRoot( CBlobParticle* p )
{
	float distToRoot = 0;
	while ( p->m_parent != NULL )
	{
		distToRoot += ( p->m_parent->m_position - p->m_position ).Length();
		p = p->m_parent;
	}
	return distToRoot;
}

bool CNPC_Blob::IsDisconnectedFromRoot( CBlobParticle *pParticle, float bondBreakDistSqr )
{
	while( pParticle->m_parent )
	{
		if ( ( pParticle->m_position - pParticle->m_parent->m_position ).LengthSqr() > bondBreakDistSqr )
			return true;
		pParticle = pParticle->m_parent;
	}
	return false;
}

/////////////////////////////////////////////////////
// This "rebalances" the tree. However, it doesn't rebalance in the
// traditional sense of getting branches of the tree to be the same
// depth. Instead, it looks at the spatial relationsips of the particles
// and tries to reattach particles to other points on the tree given that
// those other points are closer to the particle and the root, and given
// that the reattachment does not break the structure of the tree
// (for example, you never want to reattach to a child because that
// would cause a cycle, and you don't want to reattach to a particle to
// which you don't have line of sight)
//
// The Algorithm Is:
// For each particle, check if there's another particle that you'd
// rather be attached to. If so, change your parent to that particle.
// It's that simple.
void CNPC_Blob::ImproveGroupTrees()
{
	// NOTE: It seems like the code in here is so similar to the FindGroupSplit
	// code that it is possible it could be done in the same routine.

	float bondStrainDist = m_flRadius * m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_SCALE ] * m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_STRAIN ];

	for(int i = 0; i < m_blobGroups.Count(); i ++)
	{
		CBlobGroup *	group = m_blobGroups[i];
		CBlobParticle *	pRoot = group->m_root;
		Assert( pRoot );

		for(int j = 0; j < group->m_members.Count(); j ++)
		{
			CBlobParticle* p = group->m_members[j];

			// Dying particles never switch parents
			if ( p->m_flags & BLOB_PARTICLE_DEATH )
				continue;

			// Ignore roots. They're not attached so they can't reattach.
			if ( p->m_parent == NULL )
			{
				Assert( ( p == pRoot ) && ( p->m_type == ROOT_PARTICLE ) );
				continue;
			}

			// FIXME: below reattachment code duplicates code in FindGroupSplits()

			// Find the parent with the best score
			CBlobParticle* bestReattachPoint = NULL;
			float bestReattachPointScore = FLT_MAX;
			for(int k = 0; k < p->m_neighbors.size; k++)
			{
				BlobNeighbor_t & neighbor = p->m_neighbors[k];
				CBlobParticle *  np = neighbor.particle;
				float dist = neighbor.dist;

				// Do quick tests first
				if ( np->m_group == p->m_group && dist < bondStrainDist && p->m_pgoal )
				{
					// Now do expensive tests

					float reattachPointScore = dist + np->m_position.DistTo( pRoot->m_position );
					if( reattachPointScore < bestReattachPointScore &&
						IsChild(p, np) == false && IsObstacleBetwParticles(p, np) == false)
					{
						bestReattachPoint = np;
						bestReattachPointScore = reattachPointScore;
					}
				}
			}

			if ( bestReattachPoint != NULL )
			{
				p->m_parent = bestReattachPoint;
			}
		}
	}

	DebugSanityCheckGroups();
}


//-----------------------------------------------------------------------------
// Visualization for contact points
//-----------------------------------------------------------------------------
void CNPC_Blob::DrawDebugContactPoints( CBlobGroup* pGroup )
{
	if( !sv_blob_debug_draw_contact_points.GetBool() )
		return;

	for(int i = 0; i < pGroup->m_members.Count(); i++)
	{
		CBlobParticle *b1 = pGroup->m_members[i];
		if ( b1->m_isInContact )
		{
			NDebugOverlay::Box( b1->m_position, -Vector(2,2,2), Vector(2,2,2), 0, 255, 0, 32, 0 );
		}
		NDebugOverlay::Line( b1->m_position, b1->m_position + b1->m_surface * 10, 0, 255, 0, true, 0 );
	}
}


//-----------------------------------------------------------------------------
// Visualization for support
//-----------------------------------------------------------------------------
void CNPC_Blob::DrawDebugSupport( CBlobGroup* pGroup )
{
	if( !sv_blob_debug_draw_support.GetBool() )
		return;

	for(int i = 0; i < pGroup->m_members.Count(); i++)
	{
		CBlobParticle *b1 = pGroup->m_members[i];
		Vector vecSupport( b1->m_support, b1->m_support, b1->m_support );
		Vector vecContact( b1->m_contact, b1->m_contact, b1->m_contact );
		NDebugOverlay::Box( b1->m_position, -vecSupport, vecSupport, 0, 255, 255, 32, 0 );
		NDebugOverlay::Box( b1->m_position, -vecContact, vecContact, 255, 0, 0, 32, 0 );
	}
}


//-----------------------------------------------------------------------------
// Visualization for support
//-----------------------------------------------------------------------------
void CNPC_Blob::DrawDebugGoals( CBlobGroup* pGroup )
{
	int nDrawMode = sv_blob_debug_draw_goals.GetInt();
	if ( nDrawMode == 0 )
		return;

	NDebugOverlay::Box( GetAbsOrigin(), Vector( -10, -10, -10 ), Vector( 10, 10, 10 ), 255, 0, 0, 100, 0 );
	NDebugOverlay::Box( m_vecMoveGoal, Vector( -3, -3, -3 ), Vector( 3, 3, 3 ), 0, 255, 0, 100, 0 );
	NDebugOverlay::Line( pGroup->m_root->m_position, m_vecMoveGoal, 0, 255, 0, true, 0 );

	if ( nDrawMode > 1 )
	{
		for( int i = 0; i < pGroup->m_members.Count(); i++ )
		{
			CBlobParticle* p = pGroup->m_members[i];
			NDebugOverlay::Box( p->m_position, Vector( -1, -1, -1 ), Vector( 1, 1, 1 ), 255, 0, 0, 32, 0 );
			NDebugOverlay::Line( p->m_position, p->m_vecMoveGoal, 255, 0, 0, true, 0 );
		}
	}
}


//-----------------------------------------------------------------------------
// Visualization for aggregate forces
//-----------------------------------------------------------------------------
void CNPC_Blob::DrawDebugForces( CBlobGroup* pGroup )
{
	int nDrawMode = sv_blob_debug_draw_forces.GetInt();
	if ( nDrawMode == 0 )
		return;

	for( int i = 0; i < pGroup->m_members.Count(); i++ )
	{
		CBlobParticle* p = pGroup->m_members[i];
		if ( p == pGroup->m_root )
		{
			NDebugOverlay::Box( p->m_position, Vector( -1, -1, -1 ), Vector( 1, 1, 1 ), 255, 0, 0, 32, 0 );
			NDebugOverlay::Line( p->m_position, p->m_position + 0.001f * p->m_force, 255, 0, 0, true, 0 );
		}
		else
		{
			NDebugOverlay::Box( p->m_position, Vector( -1, -1, -1 ), Vector( 1, 1, 1 ), 128, 0, 0, 32, 0 );
			NDebugOverlay::Line( p->m_position, p->m_position + 0.001f * p->m_force, 128, 0, 0, true, 0 );
		}
	}
}


void CNPC_Blob::DrawDebugVisualizations(CBlobGroup* group)
{
	for(int i = 0; i < group->m_members.Count(); i++)
	{
		CBlobParticle* p = group->m_members[i];
		if(sv_blob_debug_draw_particles.GetBool())
		{
			switch(p->m_type)
			{
				case ROOT_PARTICLE: 
					NDebugOverlay::Box( p->m_position, -Vector(1,1,1), Vector(1,1,1), 255, 0, 0, 32, 0 ); 
					break;
				case CORE_PARTICLE: 
					NDebugOverlay::Box( p->m_position, -Vector(1,1,1), Vector(1,1,1), 255, 255, 0, 32, 0 ); 
					break;
				case STICKY_PARTICLE:
					NDebugOverlay::Box( p->m_position, -Vector(1,1,1), Vector(1,1,1), 0, 0, 255, 32, 0 ); 
					break;
				case SKIN_PARTICLE: 
					NDebugOverlay::Box( p->m_position, -Vector(1,1,1), Vector(1,1,1), 0, 255, 255, 32, 0 ); 
					break;
				case TENTACLE_PARTICLE: 
					NDebugOverlay::Box( p->m_position, -Vector(1,1,1), Vector(1,1,1), 0, 255, 0, 32, 0 ); 
					break;
				case UNDEFINED_PARTICLE: 
					NDebugOverlay::Box( p->m_position, -Vector(1,1,1), Vector(1,1,1), 255, 255, 255, 32, 0 ); 
					break;
				NO_DEFAULT;
			}
		}
		if(sv_blob_debug_draw_parents.GetBool())
		{
			if(p->m_parent != NULL)
				NDebugOverlay::Line(p->m_position, p->m_parent->m_position, 255, 255, 0, true, 0);
		}
	}

	DrawDebugContactPoints( group );
	DrawDebugSupport( group );
	DrawDebugGoals( group );
	DrawDebugForces( group );
}


// Input handlers
void CNPC_Blob::InputMoveToPosition( inputdata_t &inputdata )
{
	if ( inputdata.value.Entity() != NULL )
	{
		SetTarget( inputdata.value.Entity() );
	}
	else if ( inputdata.value.String() != NULL )
	{
		CBaseEntity *pTargetEnt = gEntList.FindEntityByName( NULL, inputdata.value.String() );
		SetTarget( pTargetEnt );
	}
}

void CNPC_Blob::RecenterAbsOrigin( )
{
	Vector vecCentroid( 0, 0, 0 ), vecNewOrigin;
	float weightSum = 0;
	trace_t trace;

	Assert( m_pMainGroup );
	if ( !m_pMainGroup )
		return;

	// The main group's root particle now drives navigation, so use its position as the absorigin
	CBlobParticle *pRoot = m_pMainGroup->m_root;
	if ( pRoot )
	{
		Vector vecAbsOrigin = pRoot->m_position;
		vecAbsOrigin.z -= m_flRadius;
		if ( !enginetrace->PointOutsideWorld( vecAbsOrigin ) )
		{
			// Put the origin on the floor
			SetAbsOrigin( vecAbsOrigin );
			return;
		}
	}

	// If the root is somehow outside of the world or stuck in something, fall back to using the blob's centroid
	int nGroupCount = m_blobGroups.Count();
	for ( int i = 0; i < nGroupCount; i++)
	{
		CBlobGroup *pBlobGroup = m_blobGroups[ i ];
		int nParticleCount = pBlobGroup->m_members.Count();
		if ( nParticleCount && !pBlobGroup->m_bIsTentacle )
		{
			for ( int j = 0; j < nParticleCount; j++ )
			{
				vecCentroid += pBlobGroup->m_members[ j ]->m_position;
				weightSum  += 1.0f;
			}
		}
	}

	if ( !weightSum )
		return;

	vecCentroid /= weightSum;
	vecNewOrigin = vecCentroid;
	UTIL_TraceHull( vecNewOrigin, vecNewOrigin, WorldAlignMins(), WorldAlignMaxs(), GetAITraceMask(), this, COLLISION_GROUP_NONE, &trace );
	if ( trace.startsolid )
	{
		// If the blob cannot fit at the centroid, pick the nearest particle and put the origin there instead
		float flMinDistSqr = FLT_MAX;
		for ( int i = 0; i < nGroupCount; i++)
		{
			CBlobGroup *pBlobGroup = m_blobGroups[ i ];
			int nParticleCount = pBlobGroup->m_members.Count();
			if ( !nParticleCount || pBlobGroup->m_bIsTentacle )
				continue;

			for ( int j = 0; j < nParticleCount; j++ )
			{
				float flDistSqr = vecCentroid.DistToSqr( pBlobGroup->m_members[ j ]->m_position );
				if ( flDistSqr < flMinDistSqr )
				{
					flMinDistSqr = flDistSqr;
					vecNewOrigin = pBlobGroup->m_members[ j ]->m_position;
				}
			}
		}
		vecNewOrigin.z -= m_flRadius;
	}

	SetAbsOrigin( vecNewOrigin );
}

void CNPC_Blob::VPhysicsUpdate( IPhysicsObject *pPhysics )
{
	if( pPhysics != m_vecPhysParticles[0] )
		return;

	if ( gpGlobals->frametime == 0.0f )
		return;

	// NOTE: Specifically do not chain to base class;
	// we're handling the interesting cases here.

	// Copy position and velocity from vphysics particles to blob particles.
	Vector pos;
	QAngle ang;
	Vector vecVel;

	for( int i = 0; i < m_nActiveParticles; i++ )
	{
		CBlobParticle* p = m_blobParticles[i];
		p->m_vphysParticle->GetPosition( &pos, &ang );
		p->m_vphysParticle->GetVelocity( &vecVel, NULL );
		p->m_position = pos;
		p->m_velocity = vecVel;
		p->m_force.Init();
	}

	// Update tentacles (tentacle particle positions/velocities are currently derived from animation, not physics)
	for ( int i = 0; i < BLOB_MAX_TENTACLES; i++ )
	{
		TentacleUpdateState( m_tentacles[i] );
		TentacleProcessAnimation( m_tentacles[i] );
	}

	// Grenades get captured within the blob. DEAL with it.
	ProcessCapturedGrenades();

	DoPhysics();

	// Apply forces back to vphysics particles.
	float dt = physenv->GetSimulationTimestep();
	for(int i = 0; i < m_nActiveParticles; i++)
	{
		CBlobParticle* p = m_blobParticles[i];
		p->m_vphysParticle->ApplyForceCenter(p->m_force * dt);
	}

	// Update the blob's origin (without going outside the world, or inside a clip brush!)
	Vector prevOrigin = GetAbsOrigin();
	RecenterAbsOrigin( );

	PhysicsTouchTriggers( &prevOrigin );
	PhysicsRelinkChildren( gpGlobals->frametime );
}

CHandle<CBaseAnimating> g_hBlobShapeTarget;

void CC_BlobSelectShapeTarget( const CCommand &args )
{
	const char *pchEntityName = "";

	if ( args.ArgC() >= 2 )
	{
		pchEntityName = args[1];
	}

	bool bFoundOne = false;

	CBaseEntity *pEntity = NULL;
	while ( (pEntity = GetNextCommandEntity( UTIL_GetCommandClient(), pchEntityName, pEntity )) != NULL )
	{
		CBaseAnimating *pTarget = dynamic_cast<CBaseAnimating*>( pEntity );

		if ( pTarget )
		{
			NDebugOverlay::Box( pTarget->GetAbsOrigin(), pTarget->GetCollideable()->OBBMins(), pTarget->GetCollideable()->OBBMaxs(), 255, 0, 255, 128, 1.0f );
			g_hBlobShapeTarget = pTarget;
			bFoundOne = true;
		}
	}

	if ( !bFoundOne )
	{
		g_hBlobShapeTarget = NULL;
	}
}

static ConCommand blob_select_shape_target("blob_select_shape_target", CC_BlobSelectShapeTarget, "Select model for blob to mimic.", FCVAR_CHEAT);

void CC_BlobAddShapeTarget( const CCommand &args )
{
	const char *pchEntityName = "";

	if ( args.ArgC() >= 2 )
	{
		pchEntityName = args[1];
	}

	CBaseEntity *pEntity = NULL;
	while ( (pEntity = GetNextCommandEntity( UTIL_GetCommandClient(), pchEntityName, pEntity )) != NULL )
	{
		CNPC_Blob *pTarget = dynamic_cast<CNPC_Blob*>( pEntity );

		if ( pTarget )
		{
			if ( g_hBlobShapeTarget.Get() )
			{
				pTarget->AddShapeTarget( g_hBlobShapeTarget.Get(), Vector( 0.0f, 0.0f, 0.0f ) );
			}
			else
			{
				pTarget->m_ShapeTargets.RemoveAll();
			}
		}
	}
}

static ConCommand blob_add_shape_target("blob_add_shape_target", CC_BlobAddShapeTarget, "Set blob to mimic selected shape target.", FCVAR_CHEAT);

void CC_BlobAddShapeTargetRandomVector( const CCommand &args )
{
	const char *pchEntityName = "";

	if ( args.ArgC() >= 2 )
	{
		pchEntityName = args[1];
	}

	CBaseEntity *pEntity = NULL;
	while ( (pEntity = GetNextCommandEntity( UTIL_GetCommandClient(), pchEntityName, pEntity )) != NULL )
	{
		CNPC_Blob *pTarget = dynamic_cast<CNPC_Blob*>( pEntity );

		if ( pTarget )
		{
			if ( g_hBlobShapeTarget.Get() )
			{
				pTarget->AddShapeTarget( g_hBlobShapeTarget.Get(), Vector( RandomFloat( -1.0f, 1.0f ), RandomFloat( -1.0, 1.0f ), RandomFloat( 0.5f, 1.0f ) ) );
			}
			else
			{
				pTarget->m_ShapeTargets.RemoveAll();
			}
		}
	}
}


//-----------------------------------------------------------------------------
// NPCs can override this to tweak with how costly particular movements are
//-----------------------------------------------------------------------------
bool CNPC_Blob::MovementCost( int moveType, const Vector &vecStart, const Vector &vecEnd, float *pCost )
{
	// Blob movement requires we choose crawl nodes right on the corners
	// nearest them, otherwise we can get stuck. Therefore make the movement
	// cost of a crawl link very expensive if it has a large horizontal component.

	// Also, make vertical movement fairly cheap to incent seeing the blob crawl over stuff more
	if ( moveType == bits_CAP_MOVE_CRAWL )
	{
		float dz = fabs( vecEnd.z - vecStart.z );
		float flHorizontalDist = vecStart.AsVector2D().DistTo( vecEnd.AsVector2D() );
		float flCost = dz * 0.25f;
		if ( flHorizontalDist > 18.0f )
		{
			flCost += flHorizontalDist * 5.0f;
			if ( flHorizontalDist > dz )	// at 45 degree angle, dramatically increase cost
			{
				flCost *= 5.0f;
			}
		}
		*pCost = flCost;
		return true;
	}

	return BaseClass::MovementCost( moveType, vecStart, vecEnd, pCost );
}


//-----------------------------------------------------------------------------
// Main routine where forces to move blob particles occurs
//-----------------------------------------------------------------------------
void CNPC_Blob::DoPhysics()
{
	if( m_bFrozen )
		return;

	// NOTE: If I don't do FindNeighbors here, then the blob
	// looks like he is a lot more mean, and moves more erratically
	FindNeighbors();

	// TODO: These could actually operate on individual particles instead of on groups.
	// They could also happen in one function, but it is cleaner during prototyping to
	// do it this way.
	if ( m_pMainGroup )
	{
		ChooseGroupGoal( m_pMainGroup );
		FindContactPoints( m_pMainGroup );
		UpdateSupport( m_pMainGroup );
		MoveTowardsGoal( m_pMainGroup );

		// NOTE: ApplyNeighborForces needs to happen right after MoveTowardsGoal,
		// as it currently looks at m_force to determine current movement direction
		ApplyNeighborForces( m_pMainGroup );
		ApplyNeighborViscosity( m_pMainGroup );

		// Repel targets counter-act the forces from MoveTowardsGoal
		ApplyRepelTargetForces( m_pMainGroup );
		UpdateParticleGoal( m_pMainGroup );
		ApplyParticleGoalForces( m_pMainGroup );
		ApplySpitForces( m_pMainGroup );
		ApplyLeapForces( m_pMainGroup );

		ApplyGlobalViscosity( m_pMainGroup );
		ApplyGravity( m_pMainGroup );
		ApplyStickyForce( m_pMainGroup );
		ApplyRandomForce( m_pMainGroup );
	}

	// Apply special forces to 'brain' particles
	ApplyBrainForces();

	// Apply a one-off instantaneous force to all particles
	ApplyImpulse();

	// Update regenerating particles (which grow from nothing to full size)
	UpdateRegeneratingParticles();

	// Update dying particles (which melt and disappear)
	UpdateDyingParticles();

	// Draw debug visualizations
	for(int i = 0; i < m_blobGroups.Count(); i++)
	{
		CBlobGroup* group = m_blobGroups[i];
		DrawDebugVisualizations(group);
	}

	{
		int iParticle = 0;

		PurgeDeadShapeTargets();

		for ( int iShapeTarget = 0; iShapeTarget < m_ShapeTargets.Count(); ++iShapeTarget )
		{
			BlobShapeTarget_t *pShapeTarget = &(m_ShapeTargets[ iShapeTarget ]);

			static const bool bUseOldEnvelopeCode = false;
			if ( bUseOldEnvelopeCode )
			{
				// Old test envelope code
				DoEnvelopeOld( pShapeTarget, iParticle );
				continue;
			}

			const int BLOB_ENVELOPE_MAX_EDGES = 64;
			ShapeEdge_t edges[ BLOB_ENVELOPE_MAX_EDGES ];
			CUtlVector< ShapeEdge_t > edgeList( &edges[ 0 ], BLOB_ENVELOPE_MAX_EDGES, 0 );

			GetShapeTargetEdgeList( pShapeTarget, edgeList );
			DoEnvelope( edgeList, iParticle, ( iShapeTarget + 1 ), pShapeTarget->m_bDisableCollision );
		}

		while ( iParticle < m_nActiveParticles )
		{
			CBlobParticle *pParticle = m_blobParticles[ iParticle ];
			pParticle->m_nArm = 0;

			++iParticle;
		}
	}


	// NOTE: This code is back, because we still can not guarantee that there
	// isn't a level design error that will cause a particle to fall out of the world
	// (which will cause a crash).
	int numRemovedParticles = 0;
	for(int i = 0; i < m_blobParticles.Count(); i++)
	{
		CBlobParticle* b = m_blobParticles[i];
		if ( !BlobParticlePositionIsValid( b ) )
		{
			Assert(false);
			NDebugOverlay::Box( b->m_position, -1*Vector(1,1,1), +1*Vector(1,1,1), 255, 0, 0, 255, 5 );
			DeactivateParticle(b);
			// TODO: Verify that DestroyParticle works correctly and doesn't damage
			// Networking state.
			DestroyParticle(b);
			numRemovedParticles++;
		}
	}
	if ( numRemovedParticles )
	{
		// After destroying particles, must refind neighbors
		// so that neighbor lists are valid, and must update the
		// largest group.
		FindNeighbors();
	}


	DebugFullSanityCheck();


	// Prevent the same random vector from being applied to the same
	// particles in the case that the number of random vectors exactly
	// matches the number of particles being simulated.
	if(m_blobParticles.Count() == BLOB_NUM_RAND_VECTORS)
	{
		m_randVectorIndex = (m_randVectorIndex+1)%BLOB_NUM_RAND_VECTORS;
	}
}


//-----------------------------------------------------------------------------
// Computes visible spit targets
//-----------------------------------------------------------------------------
#pragma warning ( disable:4706 )
void CNPC_Blob::ComputeNearbySpitTargets( )
{
	m_hNearbySpitTargets.RemoveAll();

	if ( GetState() != NPC_STATE_COMBAT && GetState() != NPC_STATE_ALERT ) 
		return;

	const Vector &vecLookerOrigin = GetAbsOrigin();

	CInfoBlobSpitTarget *pTentacleSpitPos = NULL;
	while( pTentacleSpitPos = assert_cast<CInfoBlobSpitTarget*>( gEntList.FindEntityByClassname( pTentacleSpitPos, "info_blob_spit_target" ) ) )
	{
		CInfoBlobSpitPath *pApproachPath = pTentacleSpitPos->GetApproachPath();
		if ( !pApproachPath )
		{
			pApproachPath = pTentacleSpitPos;
		}

		// Don't use FVisible here, since it uses spit targets
		trace_t tr;
		CTraceFilterLOS traceFilter( this, COLLISION_GROUP_NONE, pApproachPath );
		UTIL_TraceLine( vecLookerOrigin, pApproachPath->GetAbsOrigin(), MASK_BLOCKLOS, &traceFilter, &tr );
		if ( tr.fraction != 1.0 || tr.startsolid )
		{
			if ( tr.m_pEnt != pApproachPath )
				continue;
		}

		m_hNearbySpitTargets.AddToTail( pTentacleSpitPos );
	}
}
#pragma warning ( default:4706 )


//-----------------------------------------------------------------------------
// Ticks the AI
//-----------------------------------------------------------------------------
void CNPC_Blob::RunAI( void )
{
	Assert( !IsMarkedForDeletion() || ( m_blobParticles.Count() == 0 ) );
	if ( IsMarkedForDeletion() && ( m_blobParticles.Count() == 0 ) )
		return;

	VPROF_BUDGET( "CNPC_Blob::RunAI", VPROF_BUDGETGROUP_NPCS );

	//FindNeighbors();
	ImproveGroupTrees();
	UpdateParticleFlags();
	ComputeNearbySpitTargets();

	//int iMove = RandomInt( 0, m_nActiveParticles - 1 );
	//m_vecPhysParticles[iMove]->SetPosition( GetAbsOrigin() + Vector( RandomFloat( -1, 1 ), RandomFloat( -1, 1 ), RandomFloat( -1, 1 ) ), GetAbsAngles(), true );

	for(int i = 0; i < m_blobGroups.Count(); i++)
	{
		CBlobGroup* group = m_blobGroups[i];
		OrganizeParticleStructure(group);
	}

	// Trigger repel targets by proximity
	TriggerRepelTargets();

	DebugFullSanityCheck();

	// Copy physics positions into networked array
	for (int i = 0; i < m_nActiveParticles; i++)
	{
		CBlobParticle* p = m_blobParticles[i];
		Assert( m_iParticlePositionIndex[i] == s_ExpandedParticleData[p->m_vphysParticle->GetGameIndex()].iNetworkBypassIndex );
		BLOBPARTICLEPOSITION( m_iParticlePositionIndex[i] ) = p->m_position;
		BLOBPARTICLECLOSESTSURFDIR( m_iParticlePositionIndex[i] ) = p->m_isInContact ? p->m_surface : 0.25f * p->m_surface;
		//m_vecUVW[ m_iParticlePositionIndex[i] ] = p->m_isInContact ? p->m_surface : 0.25f * p->m_surface;
		m_vecTypes[i] = p->m_type;
		if(p->m_parent == NULL)
			m_vecParents[i] = m_iParticlePositionIndex[i]; //s_ExpandedParticleData[p->m_vphysParticle->GetGameIndex()].iNetworkBypassIndex; // Return your own index to indicate that you are a root
		//else m_vecParents[i] = m_iParticlePositionIndex[s_ExpandedParticleData[p->m_parent->m_vphysParticle->GetGameIndex()].iEntityIndex]; // This version sometimes fails because we are writing particlePositionIndex in this loop
		else 
			m_vecParents[i] = s_ExpandedParticleData[p->m_parent->m_vphysParticle->GetGameIndex()].iNetworkBypassIndex;
		m_iFlags[i] = p->m_flags;
	}

	NetworkProp()->NetworkStateForceUpdate();

	//Check if this blob is still visible
	if( m_nActiveParticles != 0 )
	{
		int iPlayerEyeCluster = engine->GetClusterForOrigin( UTIL_GetLocalPlayer()->EyePosition() );
		unsigned char pvs[MAX_MAP_CLUSTERS/8];
		engine->GetPVSForCluster( iPlayerEyeCluster, sizeof( pvs ), pvs );

		//engine->CheckBoxInPVS( vTest - vBoxExtents, vTest + vBoxExtents, pvs, pvsSize );
		//engine->CheckOriginInPVS( vTest, pvs, pvsSize );

		int i;
		for( i = 0; i != m_nActiveParticles; ++i )
		{
			if( engine->CheckOriginInPVS( m_blobParticles[i]->m_position, pvs, sizeof( pvs ) ) )
			{
				m_fLastSeenTime = gpGlobals->curtime;
				break;
			}
		}

		//scary spawn time maybe, require the blob to be unseen for <m_nActiveParticles> seconds
		if( (i == m_nActiveParticles) && ((m_fLastSeenTime + m_nActiveParticles) < gpGlobals->curtime) )
		{
			//NDebugOverlay::Box( GetAbsOrigin(), - Vector( 20.0f, 20.0f, 20.0f ),  Vector( 20.0f, 20.0f, 20.0f ), 0, 0, 255, 100, 0 );
			m_fLastSeenTime = gpGlobals->curtime;
			TeleportToUnseenLocation();
		}
	}

	BaseClass::RunAI();
}


//-----------------------------------------------------------------------------
// Blur support values across nearby particles
//-----------------------------------------------------------------------------
void CNPC_Blob::BlurSupportAcrossNearbyParticles( CBlobGroup* pGroup, float flBlurRadius )
{
	struct UpdateSupportTemp_t
	{
		float m_flSupport;
		float m_flTotalWeight;
	};

	const int nCount = pGroup->m_members.Count();
	UpdateSupportTemp_t *pTempData = (UpdateSupportTemp_t *)stackalloc( sizeof( UpdateSupportTemp_t ) * nCount );
	for ( int i = nCount; --i >= 0; )
	{
		CBlobParticle *b1 = pGroup->m_members[i];
		b1->m_temp = &pTempData[i];
		pTempData[i].m_flSupport = b1->m_support;
		pTempData[i].m_flTotalWeight = 1.0f;
	}

	float flOORadiusSqr = ( flBlurRadius != 0.0f ) ? 1.0f / ( flBlurRadius * flBlurRadius ) : 1.0f;
	for ( int i = nCount; --i >= 0; )
	{
		CBlobParticle *b1 = pGroup->m_members[i];
		for(int j = b1->m_neighbors.size; --j >= 0; )
		{
			CBlobParticle *b2 = b1->m_neighbors[j].particle;
			if ( ( b2->m_group != pGroup ) || ( b1->m_neighbors[j].dist >= flBlurRadius ) )
				continue;

			// We're blurring by distance, using a gaussian blur.
			// We've selected the coefficients so that we get 1/e contribution
			// at a distance of blurRadius.
			UpdateSupportTemp_t* pTemp = (UpdateSupportTemp_t *)( b2->m_temp );
			float flDistSqr = b1->m_position.DistToSqr( b2->m_position );
			float flWeight = exp( -flDistSqr * flOORadiusSqr );
			pTemp->m_flSupport += flWeight * b1->m_support; 
			pTemp->m_flTotalWeight += flWeight;
		}
	}

	// Copy support back from supportTemp into support
	for(int i = 0; i < nCount; i++)
	{
		CBlobParticle *b1 = pGroup->m_members[i];
		UpdateSupportTemp_t *pTemp = (UpdateSupportTemp_t *)b1->m_temp;
		b1->m_support = pTemp->m_flSupport / pTemp->m_flTotalWeight;
		b1->m_temp = NULL;
	}

	stackfree( pTempData );
}


//-----------------------------------------------------------------------------
// Compute 'support', indicating how many particles this particle can hold over itself
//-----------------------------------------------------------------------------
void CNPC_Blob::UpdateSupport( CBlobGroup* pGroup )
{
	VPROF_BUDGET( "CNPC_Blob::UpdateSupport", VPROF_BUDGETGROUP_NPCS );

	// Halve the previous frame's support, and add
	// the contribution created by contact.
	for( int i = pGroup->m_members.Count(); --i >= 0; )
	{
		CBlobParticle *b1 = pGroup->m_members[i];

		// HARD-CODED: Each sticky or brain particle can support five others.
		// A non sticky particle can only support itself's weight
		float flMaxSupport = b1->isSticky() || ( b1->m_flags & BLOB_PARTICLE_BRAIN ) || ( b1->m_type == ROOT_PARTICLE ) ? 5.0f : 1.0f;

		// Lerp current support to new target
		b1->m_support = Lerp( 0.1f, b1->m_support, b1->m_contact * flMaxSupport );
	}

	// Apply a "blur" to the support
	BlurSupportAcrossNearbyParticles( pGroup, 2.5f * m_flRadius ); 

	// Regenerating particles need to move when in mid-air, so blat their support to 1.0
	for( int i = pGroup->m_members.Count(); --i >= 0; )
	{
		if ( pGroup->m_members[i]->m_flags & BLOB_PARTICLE_BIRTH )
			pGroup->m_members[i]->m_support = 1.0f;
	}
}

//-----------------------------------------------------------------------------
// Particle navigation code (each particle follows a point on the navigation path)
//-----------------------------------------------------------------------------
void CNPC_Blob::InitParticleNavigation( )
{
	// FIXME: Eventually, we don't want to do this
	m_pMainGroup->m_waypoints.RemoveAll();

	const int nCount = m_pMainGroup->m_members.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		m_pMainGroup->m_members[i]->m_nWaypointTargetIndex = -1;
	}

	if ( GetNavigator()->IsGoalActive() && ( GetNavigator()->GetGoalType() != GOALTYPE_NONE ) )
	{
		Vector vecWaypointPos;
		AI_Waypoint_t *pWaypoint = GetNavigator()->GetPath()->GetCurWaypoint();
		for ( ; pWaypoint; pWaypoint = pWaypoint->GetNext() )
		{
			// We need to raise the waypoints so they appear where the center 
			// of the particles would be waypoints are on the ground
			vecWaypointPos = pWaypoint->GetPos();
			vecWaypointPos.z += m_flRadius;
			m_pMainGroup->m_waypoints.AddToTail( vecWaypointPos );
		}
	}
}

/*
void CNPC_Blob::TranslateNavGoal( CBaseEntity *pEnemy, Vector &chasePosition )
{
	// HACK! There must be a better way to detect a change of path? (this deals w/ 'npc_go')
	ClearAllParticleNavigation();

	BaseClass::TranslateNavGoal( pEnemy, chasePosition );
}
*/

/*
void CNPC_Blob::UpdateParticleNavigationPosition( CBlobParticle *pParticle, AI_Waypoint_t *pCurWaypoint, float unitsToMove )
{
	// Move the particle's tracking point along the navigation path, in either direction

	if ( unitsToMove < 0 )
	{
		pParticle->m_distPastWaypoint += unitsToMove;

		while ( pParticle->m_distPastWaypoint < 0 )
		{
			AI_Waypoint_t *pPrevWaypoint = pCurWaypoint->GetPrev();

			if ( !pPrevWaypoint )
			{
				// We've reached the start of the path
				pParticle->m_distPastWaypoint = 0;
				break;
			}

			float length = pPrevWaypoint->GetPos().DistTo( pCurWaypoint->GetPos() );
			pParticle->m_distPastWaypoint += length;
			pParticle->m_curWaypoint--;
			pCurWaypoint = pPrevWaypoint;
		}
	}
	else if ( pCurWaypoint->GetNext() )
	{
		AI_Waypoint_t *pNextWaypoint = pCurWaypoint->GetNext();
		float length = pCurWaypoint->GetPos().DistTo( pNextWaypoint->GetPos() );

		pParticle->m_distPastWaypoint += unitsToMove;

		while ( pParticle->m_distPastWaypoint >= length )
		{
			pParticle->m_distPastWaypoint -= length;
			pParticle->m_curWaypoint++;
			pCurWaypoint  = pNextWaypoint;
			pNextWaypoint = pCurWaypoint->GetNext();
			if ( !pNextWaypoint )
			{
				// We've reached the end of the path
				pParticle->m_distPastWaypoint = 0;
				break;
			}
			length = pCurWaypoint->GetPos().DistTo( pNextWaypoint->GetPos() );
		}
	}
}
*/

void CNPC_Blob::SetupParticleNavigation( CBlobParticle *pParticle )
{
	if ( pParticle->m_nWaypointTargetIndex != -1 )
		return;

	pParticle->m_nWaypointTargetIndex = 0;
	pParticle->m_vecPrevWaypointPosition = pParticle->m_position;
	pParticle->m_bMovingTowardFinalGoal = false;
}

void CNPC_Blob::UpdateParticleNavigation( CBlobParticle *pParticle )
{
	SetupParticleNavigation( pParticle );

	int nWaypointCount = pParticle->m_group->m_waypoints.Count();
	if ( pParticle->m_nWaypointTargetIndex == nWaypointCount )
	{
		// Just move towards the root particle if we're at the end of the path
		// or if there's no path
		CBlobParticle *pRoot = m_pMainGroup ? m_pMainGroup->m_root : NULL;
		Assert( pRoot );
		pParticle->m_vecMoveGoal = pRoot ? pRoot->m_position : pParticle->m_position;
		pParticle->m_bMovingTowardFinalGoal = true;
		return;
	}

	const Vector &vecTargetWaypoint = pParticle->m_group->m_waypoints[ pParticle->m_nWaypointTargetIndex ];
	pParticle->m_vecMoveGoal = vecTargetWaypoint;

	float flReachedDist = ( pParticle->m_flags & BLOB_PARTICLE_NEAR_DANGER ) ? 12 : 64;
	if ( pParticle->m_position.DistToSqr( vecTargetWaypoint ) < flReachedDist * flReachedDist )
	{
		// FIXME: LOS test for non-root particles?
		if ( pParticle->m_type == ROOT_PARTICLE )
		{
			trace_t tr;
			AI_TraceLine( pParticle->m_position, vecTargetWaypoint, GetAITraceMask(), this, HL2COLLISION_GROUP_BLOB, &tr );
			if ( tr.fraction < 1.0f )
				return;
		}

		// Advance waypoints
		pParticle->m_vecPrevWaypointPosition = vecTargetWaypoint;
		++pParticle->m_nWaypointTargetIndex;
		pParticle->m_bMovingTowardFinalGoal = ( pParticle->m_nWaypointTargetIndex == nWaypointCount );
		return;
	}

	if ( pParticle->m_type == ROOT_PARTICLE )
		return;

	if ( pParticle->m_flags & BLOB_PARTICLE_NEAR_DANGER )
		return;

	// for shits + grins, see if we're close to another waypoint
	// (don't do this if we're near danger however)
	for ( int i = pParticle->m_nWaypointTargetIndex; i < nWaypointCount - 1; ++i )
	{
		Vector vClosest;
		const Vector &vecStartWaypoint = pParticle->m_group->m_waypoints[ i ];
		const Vector &vecEndWaypoint = pParticle->m_group->m_waypoints[ i+1 ];
		CalcClosestPointOnLineSegment( pParticle->m_position, vecStartWaypoint, vecEndWaypoint, vClosest );
		if ( vClosest.DistToSqr( pParticle->m_position ) < 32 * 32 )
		{
			pParticle->m_vecPrevWaypointPosition = vecStartWaypoint;
			pParticle->m_nWaypointTargetIndex = i+1;
			pParticle->m_bMovingTowardFinalGoal = ( pParticle->m_nWaypointTargetIndex == nWaypointCount );
			return;
		}
	}
}				 


//-----------------------------------------------------------------------------
// Selects a group goal for the specified group
//-----------------------------------------------------------------------------
void CNPC_Blob::ChooseGroupGoal( CBlobGroup* pGroup )
{
	VPROF_BUDGET( "CNPC_Blob::ChooseGroupGoal", VPROF_BUDGETGROUP_NPCS );

	// 'Convergence' is the degree to which all particles in the group should move like the root particle
	float flGoalConvergence = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_FORCE_TOWARDS_GOAL_CONVERGENCE ];

	// NOTE: m_vecMoveGoal/m_vecLastMoveGoal are based on waypoint positions, which are
	// on the ground. We need to move them up to account for the hull size.
	Vector vecRootToGoal, vecParticleToGoal, vecPredictedPos, vecActualGoal, vecPredictedToGoal;
	pGroup->m_root->m_vecMoveGoal = m_vecMoveGoal;
	pGroup->m_root->m_vecMoveGoal.z += m_flRadius;
	pGroup->m_root->m_vecPrevWaypointPosition = m_vecLastMoveGoal;
	pGroup->m_root->m_vecPrevWaypointPosition.z += m_flRadius;
	pGroup->m_root->m_bMovingTowardFinalGoal = true;
	pGroup->m_root->m_nWaypointTargetIndex = -1;
	const int nWaypointCount = pGroup->m_waypoints.Count();
	for ( int i = 0; i < nWaypointCount; ++i )
	{
		if ( pGroup->m_root->m_vecMoveGoal == pGroup->m_waypoints[i] )
		{
			pGroup->m_root->m_nWaypointTargetIndex = i;
			break;
		}
	}
	if ( GetNavigator()->IsGoalActive() && ( GetNavigator()->GetGoalType() != GOALTYPE_NONE ) )
	{
		pGroup->m_root->m_bMovingTowardFinalGoal = GetNavigator()->CurWaypointIsGoal();
	}

	// If we're within range of the goal, we're done
	VectorSubtract( pGroup->m_root->m_vecMoveGoal, pGroup->m_root->m_position, vecRootToGoal );
	const int nCount = pGroup->m_members.Count();
	for ( int i = 0; i < nCount; i++ )
	{
		CBlobParticle* pParticle = pGroup->m_members[i];
		if ( pParticle == pGroup->m_root )
			continue;

		if ( pParticle->m_flags & BLOB_PARTICLE_DEATH )
		{
			// Dead particles pick a dead 'pseudo-root' particle to follow
			CBlobParticle *pGoal = pParticle;
			while( pGoal->m_parent && ( pGoal->m_parent->m_flags & BLOB_PARTICLE_DEATH ) )
			{
				pGoal = pGoal->m_parent;
			}
			pParticle->m_vecMoveGoal = pGoal->m_position;
			continue;
		}

		// Stragglers and regenerating particles simply fly straight at the root particle
		if ( pParticle->m_flags & ( BLOB_PARTICLE_STRAGGLER | BLOB_PARTICLE_BIRTH ) )
		{
			pParticle->m_vecMoveGoal = pGroup->m_root->m_position;
			continue;
		}

		// Compute a goal by tracking along the current navigation path
		UpdateParticleNavigation( pParticle );

		// Compute an actual goal based on goal convergence logic
		VectorSubtract( pParticle->m_vecMoveGoal, pParticle->m_position, vecParticleToGoal );
		Vector vecDelta = vecParticleToGoal;
		if ( pParticle->m_vecMoveGoal == pGroup->m_root->m_vecMoveGoal )
		{
			vecDelta = Lerp( flGoalConvergence, vecParticleToGoal, vecRootToGoal );
		}
		VectorAdd( pParticle->m_position, vecDelta, pParticle->m_vecMoveGoal );
	}
}


//-----------------------------------------------------------------------------
// Selects the best contact surface for this particle
//-----------------------------------------------------------------------------
void CNPC_Blob::SelectBestContactSurface( CBlobParticle* pParticle )
{
	// Keep the surface from last frame
	if ( !pParticle->m_isInContact )
		return;

	int nCount = pParticle->m_surfaces.Count();
	if ( nCount == 1 )
	{
		pParticle->m_surface = pParticle->m_surfaces[0];
		return;
	}

	// Based on our particle's move goal, find the surface we can move along
	// which gets us closest to our goal the quickest.

	Vector vecDelta;
	VectorSubtract( pParticle->m_vecMoveGoal, pParticle->m_position, vecDelta );

	int nBestSurface = -1;
	float flBestProjection = FLT_MIN;
	float flSmallestInitialProjection = FLT_MAX;
	int nBestStuckSurface = -1;
	for ( int i = 0; i < nCount; ++i )
	{
		const Vector &vecSurfNormal = pParticle->m_surfaces[i];

		// First, project the goal direction along the surface normal
		Vector vecProjInSurfacePlane;
		float flDot = DotProduct( vecSurfNormal, vecDelta );
		VectorMA( vecDelta, -flDot, vecSurfNormal, vecProjInSurfacePlane );

		if ( flDot < flSmallestInitialProjection )
		{
			flSmallestInitialProjection = flDot;
			nBestStuckSurface = i;
		}

		// Next, subtract out the components of the projection if other
		// surfaces are blocking motion.
		for ( int j = 0; j < nCount; ++j )
		{
			if ( i == j )
				continue;

			float flDot = DotProduct( pParticle->m_surfaces[j], vecProjInSurfacePlane );
			if ( flDot >= 0.0f )
				continue;
			VectorMA( vecProjInSurfacePlane, -flDot, pParticle->m_surfaces[j], vecProjInSurfacePlane );
		}

		float flProjection = vecProjInSurfacePlane.LengthSqr();
		if ( flProjection > flBestProjection )
		{
			nBestSurface = i;
			flBestProjection = flProjection;
		}
	}

	if ( flBestProjection > 1e-4 )
	{
		pParticle->m_surface = pParticle->m_surfaces[nBestSurface];
	}
	else
	{
		// This can happen in a typical stuck case, where we are in a corner.
		// In this case, We want to pick the surface that has the smallest abs dot
		// with the original surface
		pParticle->m_surface = pParticle->m_surfaces[nBestStuckSurface];
	}
}


//-----------------------------------------------------------------------------
// Computes contact points and average contact normal for a particle
//-----------------------------------------------------------------------------
#define BLOB_CONTACT_RETAIN_TIME 0.1f

void CNPC_Blob::ComputeAverageContactPoint( CBlobParticle* pParticle )
{
	pParticle->m_fLastContactCountdown -= gpGlobals->frametime;
	pParticle->m_fLastContactCountdown = MAX( 0.0f, pParticle->m_fLastContactCountdown );

	IPhysicsFrictionSnapshot *pSnapshot = pParticle->m_vphysParticle->CreateFrictionSnapshot();
	while ( pSnapshot->IsValid() )
	{
		Vector vecPosition, vecNormal;
		pSnapshot->GetContactPoint( vecPosition );
		pSnapshot->GetSurfaceNormal( vecNormal );
						   
		// NOTE: surface normal points *away* from the particle, so it's in fact backward.
		vecNormal *= -1.0f;
		float flIntercept = DotProduct( vecNormal, vecPosition );

		bool bFoundMatch = false;
		int nCount = pParticle->m_surfaces.Count();
		for ( int i = 0; i < nCount; ++i )
		{
			if ( vecNormal == pParticle->m_surfaces[i] && flIntercept == pParticle->m_intercepts[i] )
			{
				bFoundMatch = true;
				pParticle->m_lastContactTime[i] = gpGlobals->curtime;
				break;
			}
		}

		if ( !bFoundMatch )
		{
			pParticle->m_surfaces.AddToTail( vecNormal );
			pParticle->m_intercepts.AddToTail( flIntercept );
			pParticle->m_lastContactTime.AddToTail( gpGlobals->curtime );
		}
		pSnapshot->NextFrictionData();
	}
	pParticle->m_vphysParticle->DestroyFrictionSnapshot( pSnapshot );

	// Retire old contacts
	int nCount = pParticle->m_surfaces.Count();
	for ( int i = nCount; --i >= 0; )
	{
		if ( gpGlobals->curtime - pParticle->m_lastContactTime[i] >= BLOB_CONTACT_RETAIN_TIME )
		{
			pParticle->m_surfaces.FastRemove(i);
			pParticle->m_intercepts.FastRemove(i);
			pParticle->m_lastContactTime.FastRemove(i);
		}
	}
	
	// Update contact flags
	if ( pParticle->m_surfaces.Count() > 0 )
	{
		if ( !pParticle->m_isInContact )
		{
			// new contact
			pParticle->m_fLastContactCountdown = 0.5f;
		}
		pParticle->m_isInContact = true;
		pParticle->m_contact = Lerp(0.5f, pParticle->m_contact, 1.0f);
	}
	else
	{
		pParticle->m_isInContact = false;
		pParticle->m_contact = Lerp(0.1f, pParticle->m_contact, 0.0f);
	}

	// Select best surface to move along
	SelectBestContactSurface( pParticle );

	// if this is a new contact, give the particle a chance to pulse
	if ( pParticle->m_fLastContactCountdown > 0.0f )
	{
		pParticle->m_flags |= BLOB_PARTICLE_TOUCHED_SURFACE;
	}
	else
	{
		pParticle->m_flags &= ~BLOB_PARTICLE_TOUCHED_SURFACE;
	}
}


//-----------------------------------------------------------------------------
// For particles that are not in contact, distribute surface
// directions from nearby particles.
//-----------------------------------------------------------------------------
void CNPC_Blob::ComputeContactSurfaceFromNeighbors( CBlobParticle* pParticle )
{
	Vector avgSurface = pParticle->m_surface;
	int nContribCount = 1;
	for ( int j = 0; j < pParticle->m_neighbors.size; j++ )
	{
		CBlobParticle *p2 = pParticle->m_neighbors[j].particle;
		if ( p2->m_group == pParticle->m_group )
		{
			avgSurface += p2->m_surface;
			nContribCount++;
		}
	}
	if ( nContribCount > 1 )
	{
		avgSurface.NormalizeInPlace();
		pParticle->m_surface = avgSurface;
	}
}


//-----------------------------------------------------------------------------
// Computes contact points and normals for all particles
//-----------------------------------------------------------------------------
void CNPC_Blob::FindContactPoints( CBlobGroup* pGroup )
{
	VPROF_BUDGET( "CNPC_Blob::FindContactPoints", VPROF_BUDGETGROUP_NPCS );

	// For each particle, update isInContact, direction
	// of surface, all surface vectors, and m_contact.
	const int nCount = pGroup->m_members.Count();
	for ( int i = 0; i < nCount; i++ )
	{
		ComputeAverageContactPoint( pGroup->m_members[i] );
	}

	// For particles that are not in contact, distribute surface
	// directions from nearby particles.
	// NOTE: this loop yields values which are dependent on the order in
	//   which particles are processed - this is valid only because we do
	//   not clear 'm_surface' values between sim ticks.
	for ( int i = 0; i < nCount; i++ )
	{
		CBlobParticle *pParticle = pGroup->m_members[i];
		if ( pParticle->m_isInContact ) 
			continue;

		ComputeContactSurfaceFromNeighbors( pParticle );
	}
}

int sortParticlesByDistance(const BlobNeighbor_t* p1, const BlobNeighbor_t* p2)
{
	// Sort by particle distance. If distance is the same,
	// sort by particle memory address.
	if(p1->dist > p2->dist) 
		return 1;
	if(p1->dist < p2->dist) 
		return -1;
	if(p1->particle > p2->particle) 
		return 1;
	return -1;
}

inline int *GetFloatSortOrder_Ascending( float *pFloats, int iFloatCount )
{
	static CUtlVector<int> s_ReturnArrayMemory;

	if( iFloatCount == 0 )
		return s_ReturnArrayMemory.Base();

	uint32 *pSort = (uint32 *)stackalloc( sizeof( uint32 ) * iFloatCount * 2 );
	uint32 *pDest = pSort + iFloatCount;
	
	s_ReturnArrayMemory.SetCount( iFloatCount );
	int *pSortIndices = s_ReturnArrayMemory.Base();
	int *pDestIndices = (int *)stackalloc( sizeof(int ) * iFloatCount );

	for( int i = 0; i != iFloatCount; ++i )
	{
		pSortIndices[i] = i;
	}
	
	//tamper with the floats a bit to make them radix sortable
	uint32 *pFloatsAsInts = (uint32 *)pFloats;
	for( int i = 0; i != iFloatCount; ++i )
	{
		if( pFloatsAsInts[i] & 0x80000000 ) //negative?
			pSort[i] = ~pFloatsAsInts[i]; //reverse sequence of negative numbers
		else
			pSort[i] = pFloatsAsInts[i] | 0x80000000; //flip sign
	}

	for( int radix = 0; radix != 4; ++radix )
	{
		int iCounts[256] = { 0 };
		int iBitOffset = radix * 8;
		for( int i = 0; i != iFloatCount; ++i )
		{
			uint8 val = (pSort[i] >> iBitOffset) & 0xFF;
			++iCounts[val];
		}

		int iOffsetTable[256];
		iOffsetTable[0] = 0;
		for( int i = 0; i != 255; ++i )
		{
			iOffsetTable[i + 1] = iOffsetTable[i] + iCounts[i];
		}

		for( int i = 0; i != iFloatCount; ++i )
		{
			uint8 val = (pSort[i] >> iBitOffset) & 0xFF;
			int iWriteIndex = iOffsetTable[val];
			pDest[iWriteIndex] = pSort[i];
			pDestIndices[iWriteIndex] = pSortIndices[i];
			++iOffsetTable[val];
		}

		//flip the inputs for the next pass
		{
			uint32 *pTemp = pSort;
			pSort = pDest;
			pDest = pTemp;
		}

		{
			int *pTemp = pSortIndices;
			pSortIndices = pDestIndices;
			pDestIndices = pTemp;
		}
	}

	return pSortIndices;
}


//-----------------------------------------------------------------------------
// Builds an axis-aligned bounding box around all particles from all groups
//-----------------------------------------------------------------------------
void CNPC_Blob::BuildAABB( Vector *pMins, Vector *pMaxs, float flRadius )
{
	if ( m_nActiveParticles == 0 )
	{
		*pMins = *pMaxs = GetAbsOrigin();
	}
	else
	{
		CBlobParticle **ppParticles = m_blobParticles.Base();
		*pMins = *pMaxs = ppParticles[0]->m_position;
		for( int i = 1; i != m_nActiveParticles; ++i )
		{
			VectorMin( *pMins, ppParticles[i]->m_position, *pMins );
			VectorMax( *pMaxs, ppParticles[i]->m_position, *pMaxs );
		}
		if ( !pMins->IsValid() || !pMaxs->IsValid() )
		{
			Warning( "Infinite bounds!\n" );
			*pMins = *pMaxs = GetAbsOrigin();
		}
	}
	Vector vecBloat( flRadius, flRadius, flRadius );
	*pMins -= vecBloat;
	*pMaxs += vecBloat;

}

//-----------------------------------------------------------------------------
// Computes the distance at which particles care about neighbor particles
//-----------------------------------------------------------------------------
#define BLOB_NEIGHBOR_GOAL 30.0f
float CNPC_Blob::ComputeNeighborInteractionDistance( )
{
	float flNearbyDistance = m_flRadius * 
		m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_SCALE ] * 
		m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_MAX ];
	float flLastAveNeighbors = ( m_fLastAverageParticleNeighbors > 0.0f ) ? m_fLastAverageParticleNeighbors : 1.0f;
	float fNewInteractionScale = m_fLastInteractionScale * ( BLOB_NEIGHBOR_GOAL / flLastAveNeighbors );
	m_fLastInteractionScale = (fNewInteractionScale * 0.05f) + (m_fLastInteractionScale * 0.95f); //shift scales slowly
	if( (m_fLastInteractionScale > 1.0f) || ( m_fLastInteractionScale <= 0.0f ) || !IsFinite( m_fLastInteractionScale ) )
	{
		m_fLastInteractionScale = 1.0f;
	}
	flNearbyDistance *= m_fLastInteractionScale;
	return flNearbyDistance;
}


//-----------------------------------------------------------------------------
// Class used to determine if particles reside in the same bucket
//-----------------------------------------------------------------------------
class CBlobNeighborHash
{
public:
	CBlobNeighborHash( int nParticleCount, float flVoxelSize, const Vector &vecMins, const Vector &vecMaxs );
	~CBlobNeighborHash();

	// Adds a particle to the hash
	void AddParticle( int nParticleIndex, const Vector &vecCenter );

	// Are two particles in the same bucket?
	bool AreParticlesNearby( int nParticleIndex1, int nParticleIndex2 ) const;

private:
	enum
	{
		BITCOLLISIONGRANULARITY = 4,
		ONE_MB_BITS = (8 * 1024 * 1024),
	};

	size_t m_iMemoryPerParticle;
	float m_flOOVoxelSize;
	Vector m_vecMinBound;
	int m_iBits[3];
	int m_iInts[3];
	int m_iBitOffset[3];
	int m_iIntOffset[3];
	static CUtlVector<uint8> s_CollisionMemory;

#ifdef _DEBUG
	int m_nMaxParticleCount;
#endif
};


//-----------------------------------------------------------------------------
// Static members
//-----------------------------------------------------------------------------
CUtlVector<uint8> CBlobNeighborHash::s_CollisionMemory;


//-----------------------------------------------------------------------------
// Constructor, destructor
//-----------------------------------------------------------------------------
CBlobNeighborHash::CBlobNeighborHash( int nParticleCount, float flVoxelSize, const Vector &vMins, const Vector &vMaxs )
{
#ifdef _DEBUG
	m_nMaxParticleCount = nParticleCount;
#endif
	m_vecMinBound = vMins;
	flVoxelSize /= ((float)BITCOLLISIONGRANULARITY);

	if ( !vMaxs.IsValid() || !vMins.IsValid() || !IsFinite( flVoxelSize ) )
	{
		Warning( "BlobNeighborHash given NANs to play with!! Crash imminent!\n" );
	}

	do 
	{
		m_flOOVoxelSize = ( flVoxelSize != 0.0f ) ? 1.0f / flVoxelSize : 1.0f;

		for ( int i = 0; i < 3; ++i )
		{
			m_iBits[i] = ceil((vMaxs[i] - vMins[i]) * m_flOOVoxelSize);
			m_iBits[i] = (m_iBits[i] + (BITS_PER_INT - 1)) & ~(BITS_PER_INT - 1); //round up to integer counts of bits
			m_iInts[i] = m_iBits[i] / BITS_PER_INT;

			m_iBitOffset[i] = ( i == 0 ) ? 0 : m_iBitOffset[i-1] + m_iBits[i-1];
			m_iIntOffset[i] = m_iBitOffset[i] / BITS_PER_INT;
		}

		int iCollisionInts = m_iInts[0] + m_iInts[1] + m_iInts[2];
		m_iMemoryPerParticle = iCollisionInts * sizeof( unsigned int );

		// Up voxel size for next time if we're consuming too much memory
		flVoxelSize = ( flVoxelSize != 0.0f ) ? flVoxelSize * 2.0f : 2.0f;
	} while ( m_iMemoryPerParticle >= (1024 * 1024) );

	Assert( m_iMemoryPerParticle < (1024 * 1024) );
	s_CollisionMemory.SetCount( m_iMemoryPerParticle * nParticleCount );
	uint8 *pCollisionMemory = s_CollisionMemory.Base();
	memset( pCollisionMemory, 0, m_iMemoryPerParticle * nParticleCount );
}

CBlobNeighborHash::~CBlobNeighborHash()
{
	s_CollisionMemory.RemoveAll();
}


//-----------------------------------------------------------------------------
// Adds a particle to the hash
//-----------------------------------------------------------------------------
void CBlobNeighborHash::AddParticle( int nParticleIndex, const Vector &vecCenter )
{
	Assert( nParticleIndex < m_nMaxParticleCount );

	// we set the bit for the boxes we reside in. And uniformly the 7 covering 
	// the +x, +y, +z directions. Since every particle does the same, 
	// bits will only overlap for two particles in adjoining squares. 
	// And since the squares are sized by our interaction distance, this 
	// will find all possible interactions with minimal false positives.
	uint8 *pParticleCollisionMemory = s_CollisionMemory.Base() + nParticleIndex * m_iMemoryPerParticle;
	CBitVec<ONE_MB_BITS> *pBitVec = (CBitVec<ONE_MB_BITS> *)pParticleCollisionMemory;
	for ( int i = 0; i < 3; ++i )
	{
		// Assert( ( vecCenter[i] > m_vecMinBound[i] ) && ( vecCenter[i]< m_vecMaxBound[i] ) );
		int u = trunc( (vecCenter[i] - m_vecMinBound[i]) * m_flOOVoxelSize );
		Assert( (u >= 0) && (u < m_iBits[i] - BITCOLLISIONGRANULARITY) );
		u = clamp( u, 0, m_iBits[i] - BITCOLLISIONGRANULARITY );
		for( int j = 0; j <= BITCOLLISIONGRANULARITY; ++j )
		{
			pBitVec->Set( m_iBitOffset[i] + u + j );
		}
	}
}


//-----------------------------------------------------------------------------
// Are two particles in the same bucket?
//-----------------------------------------------------------------------------
bool CBlobNeighborHash::AreParticlesNearby( int nParticleIndex1, int nParticleIndex2 ) const
{
	const uint8 *pBase = s_CollisionMemory.Base();
	const int *pParseMemory1 = (int*)( pBase + nParticleIndex1 * m_iMemoryPerParticle );
	const int *pParseMemory2 = (int*)( pBase + nParticleIndex2 * m_iMemoryPerParticle );
	for ( int i = 0; i < 3; ++i )
	{
		const int *pMem1 = pParseMemory1 + m_iIntOffset[i];
		const int *pMem2 = pParseMemory2 + m_iIntOffset[i];
		bool bFoundMatch = false;
		for( int j = 0; j < m_iInts[i]; ++j, ++pMem1, ++pMem2 )
		{
			if( ( *pMem1 & *pMem2 ) != 0 )
			{
				bFoundMatch = true;
				break;
			}
		}

		if ( !bFoundMatch )
			return false;
	}
	return true;
}


//-----------------------------------------------------------------------------
// Determines which particles are neighbors of other particles
//-----------------------------------------------------------------------------
#define BLOB_MAXNEIGHBORS 50
void CNPC_Blob::SortNeighbors()
{
	BlobNeighbor_t *pNeighborWorkMemory = (BlobNeighbor_t *)stackalloc( m_nActiveParticles * sizeof( BlobNeighbor_t ) );

	//we only need one or the other at a given time, and floats are smaller than neighbors
	float *pFloatWorkMemory = (float *)pNeighborWorkMemory; 
	for( int i = 0; i != m_nActiveParticles; ++i )
	{
		int iNeighborCount = m_blobParticles[i]->m_neighbors.size;
		if( iNeighborCount == 0 )
			continue;

		BlobNeighbor_t* pNeighbors = m_blobParticles[i]->m_neighbors.a;			

		for( int j = 0; j != iNeighborCount; ++j )
		{
			pFloatWorkMemory[j] = pNeighbors[j].dist;
		}

		int *pSorted = GetFloatSortOrder_Ascending( pFloatWorkMemory, iNeighborCount );

		// limit particle interactions to N nearest neighbors
		if( iNeighborCount > BLOB_MAXNEIGHBORS )
		{
			iNeighborCount = BLOB_MAXNEIGHBORS;
		}

		for( int j = 0; j != iNeighborCount; ++j )
		{
			pNeighborWorkMemory[j] = pNeighbors[pSorted[j]];
		}
		memcpy( pNeighbors, pNeighborWorkMemory, iNeighborCount * sizeof( BlobNeighbor_t ) );

		m_blobParticles[i]->m_neighbors.size = iNeighborCount; // in case we're limiting neighbor counts
	}
}


//-----------------------------------------------------------------------------
// Determines which particles are neighbors of other particles
//-----------------------------------------------------------------------------
void CNPC_Blob::FindNeighbors()
{
	if( m_nActiveParticles == 0 )
		return;

	VPROF_BUDGET( "CNPC_Blob::FindNeighbors", VPROF_BUDGETGROUP_NPCS );

	// Set up a hash to determine nearby particles quickly
	Vector vMins, vMaxs;
	float flNearbyDistance = ComputeNeighborInteractionDistance();
	BuildAABB( &vMins, &vMaxs, flNearbyDistance * 2.0f );
	CBlobNeighborHash hash( m_nActiveParticles, flNearbyDistance, vMins, vMaxs );

	// Add all particles to the hash
	CBlobParticle **ppParticles = m_blobParticles.Base();
	for( int i = 0; i != m_nActiveParticles; ++i )
	{
		hash.AddParticle( i, ppParticles[i]->m_position );
		ppParticles[i]->m_neighbors.size = 0;
	}

	// Use the hash to quickly determine particle nearness
	Vector vecDelta;
	float flNearbyDistanceSqr = flNearbyDistance * flNearbyDistance;
	int iTotalNeighborCount = 0;
	for( int i = 0; i < (m_nActiveParticles - 1); ++i )
	{
		for( int j = i + 1; j < m_nActiveParticles; ++j )
		{
			if ( !hash.AreParticlesNearby( i, j ) )
			{
				Assert( ppParticles[i]->m_position.DistToSqr( ppParticles[j]->m_position ) >= flNearbyDistanceSqr );
				continue;
			}

			// Do a true distance check, since we know they are near
			CBlobParticle* b1 = ppParticles[i];
			CBlobParticle* b2 = ppParticles[j];
			VectorSubtract( b2->m_position, b1->m_position, vecDelta );
			float flDistSqr = vecDelta.LengthSqr();
			if ( flDistSqr >= flNearbyDistanceSqr )
				continue;

			// indeed are neighbors
			float flDist = FastSqrt( flDistSqr );
			vecDelta *= 1.0f / ( flDist + FLT_EPSILON );

			BlobNeighbor_t* n1 = &(b1->m_neighbors.pushAutoSize());
			n1->dist = flDist;
			n1->particle = b2;
			n1->vec = vecDelta;

			BlobNeighbor_t* n2 = &(b2->m_neighbors.pushAutoSize());
			n2->dist = flDist;
			n2->particle = b1;
			n2->vec = -vecDelta;

			iTotalNeighborCount += 2;
		}
	}

	m_fLastAverageParticleNeighbors = ((float)iTotalNeighborCount) / ((float)m_nActiveParticles);
	//Msg( "Average Neighbors: %f\n", m_fLastAverageParticleNeighbors );

	//sort neighbors
	SortNeighbors();
}													    


void CNPC_Blob::ApplyNeighborViscosity(CBlobGroup* group)
{
	VPROF_BUDGET( "CNPC_Blob::ApplyNeighborViscosity", VPROF_BUDGETGROUP_NPCS );
	float neighborViscosity = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_VISCOSITY ];

	// We don't apply neighbour forces to the root particle (it should be focused purely on navigation)
	Vector rootForce = group->m_root->m_force;

	for(int i = group->m_members.Count(); --i >= 0; )
	{
		CBlobParticle *b1 = group->m_members[i];

		Vector avgNeighborVelocity(0.0f, 0.0f, 0.0f);
		int nNeighbors = 0;
		for(int j = b1->m_neighbors.size; --j >= 0; )
		{
			CBlobParticle *b2 = b1->m_neighbors[j].particle;
			if(b2->m_group == group)
			{
				avgNeighborVelocity += b2->m_velocity;
				++nNeighbors;	
			}
		}

		if(nNeighbors > 0)
		{
			avgNeighborVelocity *= 1.0f / nNeighbors;

			Vector vecVel = b1->m_velocity - avgNeighborVelocity;

			float vel = VectorNormalize( vecVel );
			float maxForce = b1->m_mass * vel; // We don't want a force greater than the velocity * mass... because it will make the particle go backwards.

			float dampForce = neighborViscosity * vel*vel;
			dampForce = MIN( dampForce, maxForce );

			Vector vecViscousForce;
			VectorMultiply( ReplicateToVector( -dampForce ), vecVel, vecViscousForce );
			b1->m_force += vecViscousForce;

			//NDebugOverlay::Line( b1->m_position, b1->m_position + 0.001f*vecViscousForce, 0, 64, 192, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		}
	}

	group->m_root->m_force = rootForce;
}


void CNPC_Blob::ApplyGlobalViscosity(CBlobGroup* group)
{
	VPROF_BUDGET( "CNPC_Blob::ApplyGlobalViscosity", VPROF_BUDGETGROUP_NPCS );
	float globalViscosity = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_GLOBAL_VISCOSITY ];

	Vector rootForce = group->m_root->m_force;

	for(int i = 0; i < group->m_members.Count(); i++)
	{
		CBlobParticle *b1 = group->m_members[i];

		Vector vecVel = b1->m_velocity;
		float vel = VectorNormalize( vecVel );
		float maxForce = b1->m_mass * vel; // We don't want a force greater than the velocity * mass... because it will make the particle go backwards.

		float v_scale = clamp(b1->m_support, 0.0f, 1.0f) * globalViscosity;
		// FIXME: none of the blob viscosity calcs take into account radius or mass, so the blob sim won't scale correctly
		float dampForce = v_scale * vel*vel;
		//float dampForce = 0.1f * vel;
		if( dampForce > maxForce)
		{
			dampForce = maxForce;
		}
		Vector vecViscousForce;
		VectorMultiply( ReplicateToVector( -dampForce ), vecVel, vecViscousForce );
		b1->m_force += vecViscousForce;

//		NDebugOverlay::Line( b1->m_position, b1->m_position + 0.001f*vecViscousForce, 0, 64, 192, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	}

	group->m_root->m_force = rootForce;
}


//-----------------------------------------------------------------------------
// Returns how much force to apply to particle 1 in the direction from particle 1->2
// (so, it should return a negative number to repel 1 from 2, positive to attract 1 to 2)
//-----------------------------------------------------------------------------
inline float CNPC_Blob::ComputeBondForce( const CBlobParticle *b1, 
	const CBlobParticle *b2, float flDist, const BondForceInfo_t &info )
{
	float flForce = 0.0f;

	// LJ Forces:
	// See http://en.wikipedia.org/wiki/Lennard-Jones_potential
	// When using the alternative expression with the force equation, you get
	// f = 12*e* ( rmin^12 / r^13 - rmin^6 / r^7 )
	// In our code, rmin = touchdistance (place of minimum potential), r = dist.
	// We'll not use exponents that high, as they would tend to be unstable.
	// Let's try rmin^4/r^5 - rmin^2/r^3
	float flOODist = 1.0f / ( flDist + 0.0001f );
	float p1 = info.m_flTouchDistance * flOODist;
	float p2 = p1 * p1;
	float p4 = p2 * p2;
//	FIXME: Enable later p2 *= flOODist;
//	FIXME: Enable later p4 *= flOODist;

	float surface_tension_modifier = ( ( 12.0f * info.m_flSurfaceTension ) / ( b1->m_neighbors.size + 0.1f ) ) + 1.0f;
	//float lennard_jones_force = fLennardJones * 2.0f * ( p2 - ( p4 * p4 ) );
	float lennard_jones_force = info.m_flLennardJonesAttraction * p2 - info.m_flLennardJonesRepulsion*p4;
	flForce += surface_tension_modifier * lennard_jones_force;

	// TODO: repulsion forces for a sticky particle in contact should only operate in the normal direction.

	// flDistFromGoal is positive if the particles are too far and need to be attracted together
	// it's negative if the particles are too close and need to be pushed apart.
	float flDistFromGoal = flDist - info.m_flTouchDistance;
	flForce += MAX( info.m_flLinearAttraction * flDistFromGoal, 0.0f );
	flForce += MIN( -info.m_flLinearRepulsion * flDistFromGoal, 0.0f );
	flForce = clamp( flForce, -info.m_flRepulsionMax, info.m_flAttractionMax );
	
	return flForce;
}


void CNPC_Blob::ApplyBrainForces( void )
{
	VPROF_BUDGET( "CNPC_Blob::ApplyBrainForces", VPROF_BUDGETGROUP_NPCS );

	if ( !HasBrain() )
		return;

	// Increase forces to improve brain cohesion in general:
	float neighborViscosity		=	m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_BRAIN_NEIGHBOR_VISCOSITY ];
	BondForceInfo_t info;
	SetupBondForceInfo( &info, BLOB_PHYSICS_PARAM_BRAIN_BOND_PARAM_START );

	// Get the set of current brain particles
	CBlobParticle **pBrainParticles = (CBlobParticle**)stackalloc( m_nBrainParticleCount * sizeof(CBlobParticle*) );
	CBlobGroup *pBrainGroup = NULL;
	int numBrainParticles = GetBrainParticles( pBrainParticles, pBrainGroup );

	if ( !numBrainParticles )
		return;

	Vector brainPosition, brainVelocity;
	ComputeBrainPosition( brainPosition, brainVelocity );

	for ( int i = 0; i < numBrainParticles; i++ )
	{
		CBlobParticle *b1 = pBrainParticles[ i ];

		// We don't apply this force to the root particle (it should be focused purely on navigation)
		if ( b1->m_type == ROOT_PARTICLE )
			continue;

		// Apply neighbor viscosity between brain particles
		Vector velocity	 = b1->m_velocity - brainVelocity;
		float  speed	 = VectorNormalize( velocity );
		float  dampForce = neighborViscosity*speed*speed;
		dampForce		 = MIN( dampForce, b1->m_mass*speed ); // Avoid oscillation
		b1->m_force		-= dampForce*velocity;

		// Apply attraction/repulsion forces between the brain particles (and towards the blob's centre)
		for ( int j = 0; j < numBrainParticles; j++ )
		{
			if ( j == i )
				continue;

			CBlobParticle *b2 = pBrainParticles[ j ];
			Vector dir = b2->m_position - b1->m_position;
			float dist = dir.NormalizeInPlace();
			b1->m_force += dir * ComputeBondForce( b1, b2, dist, info );
		}
	}
}


void CNPC_Blob::ApplyStickyForce(CBlobGroup* group)
{
	// Push particles away from surfaces they are in contact with

	VPROF_BUDGET( "CNPC_Blob::ApplyStickyForce", VPROF_BUDGETGROUP_NPCS );

	COMPILE_TIME_ASSERT( NUM_PARTICLE_TYPES == 6 );
	float pStickyForce[NUM_PARTICLE_TYPES];
	pStickyForce[ROOT_PARTICLE] = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_ROOT_PARTICLES ];
	pStickyForce[CORE_PARTICLE] = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_CORE_PARTICLES ];
	pStickyForce[STICKY_PARTICLE] = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_STICKY_PARTICLES ];
	pStickyForce[SKIN_PARTICLE] = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_SKIN_PARTICLES ];
	pStickyForce[TENTACLE_PARTICLE] = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_TENTACLE_PARTICLES ];
	pStickyForce[UNDEFINED_PARTICLE] = 0.0f;

	// We don't apply this force to the root particle (it should be focused purely on navigation)
	Vector rootForce = group->m_root->m_force;

	Vector vecStickyForce;
	for(int i = 0; i < group->m_members.Count(); i++)
	{
		CBlobParticle *p = group->m_members[i];

		if ( p->m_flags & ( BLOB_PARTICLE_BIRTH | BLOB_PARTICLE_DEATH ) )
			continue;

		Assert( p->m_type >= 0 && p->m_type < NUM_PARTICLE_TYPES );
		float flStickyForce = pStickyForce[ p->m_type ];
		float s_scale = clamp(p->m_support, 0.0f, 1.0f);
		VectorMultiply( p->m_surface, -flStickyForce * s_scale * p->m_mass, vecStickyForce );

		p->m_force += vecStickyForce;

//		NDebugOverlay::Line( p->m_position, p->m_position + 0.001f*vecStickyForce, 0, 255, 255, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	}

	group->m_root->m_force = rootForce;
}


void CNPC_Blob::ApplyGravity(CBlobGroup* group)
{
	VPROF_BUDGET( "CNPC_Blob::ApplyGravity", VPROF_BUDGETGROUP_NPCS );
	Vector m_gravity;
	physenv->GetGravity(&m_gravity);

	for(int i = 0; i < group->m_members.Count(); i++)
	{
		CBlobParticle* b = group->m_members[i];

		// Straggler and regenerating particles have their collisions disabled.
		// Don't apply gravity, we just want them to fly back towards the main blob body.
		if ( b->m_flags & ( BLOB_PARTICLE_STRAGGLER | BLOB_PARTICLE_BIRTH ) )
			continue;

		Vector vecGravity;
		float g_scale = 1.0f;
		if ( !m_bSuppressStragglerCreation )
		{
			g_scale = SimpleSplineRemapValClamped(1.0f - b->m_support, 0.0f, 1.0f, 0.0f, 1.6f);
		}
		VectorMultiply( m_gravity, g_scale * b->m_mass, vecGravity );
		b->m_force += vecGravity;

//		NDebugOverlay::Line( b->m_position, b->m_position + 0.001f*vecGravity, 255, 0, 255, true, 0 );
	}
}

void CNPC_Blob::ApplyRandomForce(CBlobGroup* group)
{
	VPROF_BUDGET( "CNPC_Blob::ApplyRandomForce", VPROF_BUDGETGROUP_NPCS );

	Vector vecRootForce = group->m_root->m_force;

	const int nCount = group->m_members.Count();
	for ( int i = 0; i < nCount; i++ )
	{
		if ( group->m_members[i]->m_flags & BLOB_PARTICLE_BIRTH )
			continue;

		// Apply a small random force (helps avoid particles getting stuck)
		group->m_members[i]->m_force += s_pRandVectors[ m_randVectorIndex ];
		m_randVectorIndex = ( m_randVectorIndex + 1 ) % BLOB_NUM_RAND_VECTORS;
	}

	group->m_root->m_force = vecRootForce;
}


void CNPC_Blob::ComputeGroupCenter( Vector *pCenter, CBlobGroup *pGroup )
{
	// Compute center of the largest group
	int nCount = pGroup->m_members.Count();
	*pCenter = vec3_origin;
	for ( int i = 0; i < nCount; ++i )
	{
		*pCenter += pGroup->m_members[i]->m_position;
	}
	*pCenter /= nCount;
}

void CNPC_Blob::ComputeMainGroupCenter( Vector *pCenter )
{
	if ( !m_pMainGroup )
	{
		*pCenter = WorldSpaceCenter();
		return;
	}

	ComputeGroupCenter( pCenter, m_pMainGroup );
}

static ConVar blob_spit_force( "blob_spit_force", "15000" );
void CNPC_Blob::ApplySpitForces( CBlobGroup* pGroup )
{
	if ( pGroup != m_pMainGroup )
		return;

	if ( m_nSpitCount <= 0 )
		return;

	// Apply random forces to particles
	float flSpitForce = blob_spit_force.GetFloat();
	int nCount = m_pMainGroup->m_members.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		CBlobParticle *pParticle = m_pMainGroup->m_members[i];
		if ( pParticle == m_pMainGroup->m_root )
			continue;
		if ( pParticle->m_flags & ( BLOB_PARTICLE_BIRTH | BLOB_PARTICLE_DEATH ) )
			continue;
		Vector vecForce = RandomVector( -flSpitForce, flSpitForce );
		pParticle->m_force += vecForce;
	}
}

static ConVar blob_leap_force( "blob_leap_force", "15000" );
void CNPC_Blob::ApplyLeapForces( CBlobGroup* pGroup )
{
	if ( pGroup != m_pMainGroup )
		return;

	if ( m_nNeighborForceMode != NEIGHBOR_FORCE_CONTRACT )
		return;

	// Apply inward forces to particles
	float flLeapForce = blob_leap_force.GetFloat();
	int nCount = m_pMainGroup->m_members.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		CBlobParticle *pParticle = m_pMainGroup->m_members[i];
		if ( pParticle == m_pMainGroup->m_root )
			continue;
		if ( pParticle->m_flags & ( BLOB_PARTICLE_BRAIN | BLOB_PARTICLE_BIRTH | BLOB_PARTICLE_DEATH ) )
			continue;
		Vector vecForce;
		VectorSubtract( pParticle->m_position, m_pMainGroup->m_root->m_position, vecForce );
		VectorNormalize( vecForce );
		vecForce *= -flLeapForce;
		pParticle->m_force += vecForce;
	}
}


void CNPC_Blob::ApplyImpulse( void )
{
	// Apply an instantaneous one-time impulse to all particles
	if ( m_vecParticleImpulse.IsZero() && m_vecParticleImpulseSpread.IsZero() )
		return;

	for ( int i = 0; i < m_blobGroups.Count(); i++ )
	{
		CBlobGroup *group = m_blobGroups[i];

		// We don't apply this force to the root particle (it should be focused purely on navigation)
		Vector rootForce = group->m_root->m_force;

		for ( int j = 0; j < group->m_members.Count(); j++ )
		{
			CBlobParticle *particle = group->m_members[j];

			if ( particle->m_flags & ( BLOB_PARTICLE_BIRTH | BLOB_PARTICLE_DEATH | BLOB_PARTICLE_STRAGGLER | BLOB_PARTICLE_TENTACLE ) )
				continue;

			Vector impulse = m_vecParticleImpulse + m_vecParticleImpulseSpread*Vector( RandomFloat(-1,+1), RandomFloat(-1,+1), RandomFloat(-1,+1) );
			particle->m_force += impulse*( particle->m_mass / gpGlobals->frametime );
		}

		group->m_root->m_force = rootForce;
	}
	m_vecParticleImpulse = vec3_origin;
	m_vecParticleImpulseSpread = vec3_origin;
}


//-----------------------------------------------------------------------------
// Sets up the bond force info struct
//-----------------------------------------------------------------------------
void CNPC_Blob::SetupBondForceInfo( BondForceInfo_t *pInfo, int nFirstStateVar )
{
	pInfo->m_flTouchDistance			=	m_flRadius*
		m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_SCALE ]*
		m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_TOUCH ];
	pInfo->m_flSurfaceTension			=	m_pfBlobPhysParamState[ (BlobPhysicsParamType_t)( nFirstStateVar + 2 ) ];
	pInfo->m_flLennardJonesAttraction	=	m_pfBlobPhysParamState[ (BlobPhysicsParamType_t)( nFirstStateVar + 3 ) ];
	pInfo->m_flLennardJonesRepulsion	=	m_pfBlobPhysParamState[ (BlobPhysicsParamType_t)( nFirstStateVar + 4 ) ];
	pInfo->m_flLinearAttraction			=	m_pfBlobPhysParamState[ (BlobPhysicsParamType_t)( nFirstStateVar + 5 ) ];
	pInfo->m_flLinearRepulsion			=	m_pfBlobPhysParamState[ (BlobPhysicsParamType_t)( nFirstStateVar + 6 ) ];
	pInfo->m_flAttractionMax			=	m_pfBlobPhysParamState[ (BlobPhysicsParamType_t)( nFirstStateVar ) ];
	pInfo->m_flRepulsionMax				=	m_pfBlobPhysParamState[ (BlobPhysicsParamType_t)( nFirstStateVar + 1 ) ];
}


//-----------------------------------------------------------------------------
// Applies neighbor forces
//-----------------------------------------------------------------------------
#define NEIGHBOR_DESIRED_COMPRESSION 0.2f
void CNPC_Blob::ApplyNeighborForces( CBlobGroup* pGroup )
{				 
	VPROF_BUDGET( "CNPC_Blob::ApplyNeighborForces", VPROF_BUDGETGROUP_NPCS );

	bool bDrawNeighborForces = sv_blob_debug_draw_neighbor_forces.GetBool();

	// We don't apply this force to the root particle (it should be focused purely on navigation)
	Vector rootForce = pGroup->m_root->m_force;

	float flRestDistance = m_flRadius * 
		m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_SCALE ] * 
		m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_TOUCH ];

	// We're going to define our neighbor spring constant based
	// on what it would take to make sure the spring compresses no more than 
	// 1/4 of the rest distance under the influence of gravity
	// NOTE: I'm doing the computation based on a quadratic spring ( F = kx^2 )
	// mg = k(x-xrest)^2 = k ( NEIGHBOR_DESIRED_COMPRESSION * rest ) ^2
	// We'll need to multiply by mass in the loop below because it's per-particle
	Vector vecGravity;
	physenv->GetGravity( &vecGravity );
	float flGravity = vecGravity.Length();
	float flDenom = NEIGHBOR_DESIRED_COMPRESSION * flRestDistance;
	flDenom *= flDenom;
	float flSpringConstant = flGravity / flDenom; 

	int nCount = pGroup->m_members.Count();

	struct NeighborForceInfo_t
	{
		Vector m_vecSpringForce;
		Vector m_vecTangentialForce;
		Vector m_vecGravityOpposingForce;
	};
	int nSizeInBytes = m_nActiveParticles * sizeof(NeighborForceInfo_t);
	NeighborForceInfo_t *pNeighborForceInfo = (NeighborForceInfo_t*)stackalloc( nSizeInBytes );
	memset( pNeighborForceInfo, 0, nSizeInBytes );
	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		m_blobParticles[i]->m_temp = &pNeighborForceInfo[i];
	}

	float flOutwardForceFactor, flInwardForceFactor;
	switch ( m_nNeighborForceMode )
	{
	case NEIGHBOR_FORCE_EXPLODE:
		m_flLastNeighborOutwardForceFactor = flOutwardForceFactor = 1.0f;
		m_flLastNeighborInwardForceFactor = flInwardForceFactor = 0.0f;
		break;

	default:			
		flOutwardForceFactor = ( m_nNeighborForceMode == NEIGHBOR_FORCE_CONTRACT ) ? 0.0f : 1.0f;
		flOutwardForceFactor = Lerp( gpGlobals->frametime / BLOB_NEIGHBOR_FORCE_LERP_TIME, m_flLastNeighborOutwardForceFactor, flOutwardForceFactor );
		m_flLastNeighborOutwardForceFactor = flOutwardForceFactor;
		flInwardForceFactor = Lerp( gpGlobals->frametime / BLOB_NEIGHBOR_FORCE_LERP_TIME, m_flLastNeighborInwardForceFactor, 1.0f );
		m_flLastNeighborInwardForceFactor = flInwardForceFactor;
		break;
	}
	flOutwardForceFactor = RemapValClamped( flOutwardForceFactor, 0.0f, 1.0f, 0.1f, 1.0f );
	flInwardForceFactor = RemapValClamped( flInwardForceFactor, 0.0f, 1.0f, 0.1f, 1.0f );

	for ( int i = 0; i < nCount; i++ )
	{
		CBlobParticle *b1 = pGroup->m_members[i];

		if ( b1->m_flags & BLOB_PARTICLE_BIRTH )
			continue;

		float flAvailableSupport = b1->m_support;
		for ( int j = 0; j < b1->m_neighbors.size; j++ )
		{
			const BlobNeighbor_t &neighbor = b1->m_neighbors[ j ];
			CBlobParticle *b2 = neighbor.particle;
			if ( b2 == pGroup->m_root )
				continue;

			NeighborForceInfo_t *pInfo = (NeighborForceInfo_t*)b2->m_temp;

			// First, use a quadratic spring to keep the neighbors at a specified distance apart
			Vector vecForce;
			float flSpringCompress = flRestDistance - neighbor.dist;
			if ( flSpringCompress > 0.0f )
			{
				flSpringCompress *= flOutwardForceFactor;
			}
			else
			{
				flSpringCompress *= flInwardForceFactor;
			}
			flSpringCompress *= flSpringCompress;
			float flForce = b2->m_mass * flSpringConstant * flSpringCompress; 
			VectorMultiply( neighbor.vec, flForce, vecForce );

			// Eliminate a portion of the spring force in the direction of movement
			Vector vecMoveDir = b2->m_force;
			if ( VectorNormalize( vecMoveDir ) > 1e-3 )
			{
				float flDot = DotProduct( vecMoveDir, vecForce );
				VectorMA( vecForce, -0.5f * flDot, vecMoveDir, vecForce );
			}

			b2->m_force += vecForce;
			pInfo->m_vecSpringForce += vecForce;

			// Next, if there is a support difference between this particle and
			// its neighbor and it's a equal or lesser type, try to push it tangentially
			// in the direction of the m_surface
			if ( b2->m_type < b1->m_type )
				continue;
			if ( ( b2->m_flags & BLOB_PARTICLE_BRAIN ) && ( !( b1->m_flags & BLOB_PARTICLE_BRAIN ) ) )
				continue;
			if ( flAvailableSupport < b2->m_support )
				continue;
			float flDot = DotProduct( b1->m_surface, neighbor.vec );
			float flDeltaSupport = flAvailableSupport - b2->m_support;
			flAvailableSupport -= flDeltaSupport;

			// Compute direction tangent to sphere around b1, pushing up in direction of b1->m_surface
			VectorMA( b1->m_surface, -flDot, neighbor.vec, vecForce );
			if ( VectorNormalize( vecForce ) < 1e-3 )
			{
				vecForce.Init( -b1->m_surface.y, b1->m_surface.x, 0.0f );
				if ( VectorNormalize( vecForce ) < 1e-3 )
				{
					vecForce.Init( -b1->m_surface.z, 0.0f, b1->m_surface.x );
					VectorNormalize( vecForce );
				}
			}						 
			flForce = RemapValClamped( flDot, -1.0f, 0.0f, 2.0f, 1.0f );
			flForce *= 2.0f * b2->m_mass * flGravity * flDeltaSupport;
			vecForce *= flForce;
			b2->m_force += vecForce;
			pInfo->m_vecTangentialForce += vecForce;

			// Additionally, help oppose gravity when the surface normal is against gravity
			vecForce = vecGravity;
			vecForce /= flGravity;
			flDot = DotProduct( b1->m_surface, vecForce );
			flDot += 1.0f;
			vecForce *= -b2->m_mass * flGravity * 0.75f * flDot * clamp( flDeltaSupport, 0.0f, 1.0f );
			b2->m_force += vecForce;
			pInfo->m_vecGravityOpposingForce += vecForce;
		}
	}

	if ( bDrawNeighborForces )
	{
		for ( int i = 0; i < nCount; ++i )
		{
			CBlobParticle* p = pGroup->m_members[i];
			NeighborForceInfo_t *pInfo = (NeighborForceInfo_t*)p->m_temp;
			NDebugOverlay::Box( p->m_position, Vector( -1, -1, -1 ), Vector( 1, 1, 1 ), 255, 0, 0, 32, 0 );
			NDebugOverlay::Line( p->m_position, p->m_position + 0.001f * pInfo->m_vecSpringForce, 255, 0, 0, true, 0 );
			NDebugOverlay::Line( p->m_position, p->m_position + 0.001f * pInfo->m_vecTangentialForce, 255, 255, 0, true, 0 );
			NDebugOverlay::Line( p->m_position, p->m_position + 0.001f * pInfo->m_vecGravityOpposingForce, 0, 255, 0, true, 0 );
			p->m_temp = NULL;
		}
	}

	stackfree( pNeighborForceInfo );

	// Restore the root's existing force
	pGroup->m_root->m_force = rootForce;
}


bool CNPC_Blob::GroupContainsBrain( CBlobGroup *pGroup )
{
	int nCount = pGroup->m_members.Count();
	for( int i = 0; i < nCount; i++ )
	{
		if ( pGroup->m_members[i]->m_flags & BLOB_PARTICLE_BRAIN )
			return true;
	}
	return false;
}


void CNPC_Blob::ApplyParticleGoalForces(CBlobGroup* pGroup)
{
	VPROF_BUDGET( "CNPC_Blob::ApplyParticleGoalForces", VPROF_BUDGETGROUP_NPCS );

	BondForceInfo_t info;
	SetupBondForceInfo( &info, BLOB_PHYSICS_PARAM_PGOAL_BOND_PARAM_START );

	float moveToGoalBrakeTime = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_TOWARDS_GOAL_BRAKE_TIME ];

	// We don't apply this force to the root particle (it should be focused purely on navigation)
	Vector rootForce = pGroup->m_root->m_force;

	int nCount = pGroup->m_members.Count();
	int nSizeInBytes = nCount * sizeof(Vector);
	Vector *pPGoalForces = (Vector*)stackalloc( nSizeInBytes );
	memset( pPGoalForces, 0, nSizeInBytes );
	for ( int i = 0; i < nCount; i++ )
	{
		pGroup->m_members[i]->m_temp = &pPGoalForces[i];
	}

	for(int i = 0; i < nCount; i++)
	{
		CBlobParticle *b1 = pGroup->m_members[i];
		CBlobParticle *b2 = b1->m_pgoal;

		if ( b1->m_flags & BLOB_PARTICLE_BIRTH )
			continue;

		// NOTE: Living and dead particles ignore each other here
		if ( b2 && ( b2->m_group == pGroup ) &&
			 !( ( b1->m_flags & BLOB_PARTICLE_DEATH ) ^ ( b2->m_flags & BLOB_PARTICLE_DEATH ) ) )
		{
			Vector dir = b2->m_position - b1->m_position;
			dir += moveToGoalBrakeTime*( b2->m_velocity - b1->m_velocity );
			float dist = VectorNormalize( dir );
			float force = ComputeBondForce( b1, b2, dist, info );

			// NOTE: This is now a bi-directional force (uni-directional pgoal forces caused global drift and had no real benefit)
			b1->m_force += dir * force;
			b2->m_force -= dir * force;

			Vector *pPGoal1 = (Vector*)b1->m_temp;
			Vector *pPGoal2 = (Vector*)b2->m_temp;
			VectorMA( *pPGoal1, force, dir, *pPGoal1 );
			VectorMA( *pPGoal2, -force, dir, *pPGoal2 );
		}
	}

	//	for ( int i = 0; i < nCount; ++i )
	//	{
	//		CBlobParticle* p = m_members[i];
	//		Vector *pInfo = (Vector*)p->m_temp;
	//		NDebugOverlay::Line( p->m_position, p->m_position + 0.001f * (*pInfo), 0, 0, 255, true, 0 );
	//		p->m_temp = NULL;
	//	}

	pGroup->m_root->m_force = rootForce;
}


//-----------------------------------------------------------------------------
// Compute clipped movement direction, return blockage amount
//-----------------------------------------------------------------------------
float CNPC_Blob::ClipMovementToContactSurfaces( CBlobParticle* pParticle, Vector &vecMoveDelta )
{
	// Then clip the movement direction to all surfaces with which the particle is in contact
	for ( int s = 0; s < pParticle->m_surfaces.Count(); s++ )
	{
		float flDot = DotProduct( pParticle->m_surfaces[ s ], vecMoveDelta );
		if ( flDot < 0.0f )
		{
			VectorMA( vecMoveDelta, -flDot, pParticle->m_surfaces[ s ], vecMoveDelta );
		}
	}

	// If moveDir is small after projection+clipping, then the particle is blocked - so push
	// upwards to escape (which is what the AI navigation code has done, in 'NAV_CRAWL' mode)
	float flDegreeBlocked = vecMoveDelta.Length();
	flDegreeBlocked = RemapValClamped( flDegreeBlocked, 0.0f, 0.2f, 1.0f, 0.0f );
	return flDegreeBlocked;
}


//-----------------------------------------------------------------------------
// Clip the ray to the surfaces; return false if the entire ray is clipped
// Return clipped endpoints in vecStart + vecEnd
//-----------------------------------------------------------------------------
bool CNPC_Blob::ClipRayToContactSurfaces( CBlobParticle* pParticle, Vector &vecStart, Vector &vecEnd )
{
//	bool bDrawDebug = ( pParticle->m_group->m_root == pParticle ); 
//	if ( bDrawDebug )
//	{
//		NDebugOverlay::Line( vecStart, vecEnd, 0, 255, 255, true, 0.25f );
//	}

	Vector vecDir;
	VectorSubtract( vecEnd, vecStart, vecDir );
	for ( int s = 0; s < pParticle->m_surfaces.Count(); s++ )
	{
		float flStartTest = DotProduct( vecStart, pParticle->m_surfaces[ s ] ) - pParticle->m_intercepts[ s ];
		bool bStartsInFront = ( flStartTest >= 0.0f );
		float t = IntersectRayWithPlane( vecStart, vecDir, pParticle->m_surfaces[ s ], pParticle->m_intercepts[ s ] );
		if ( !bStartsInFront )
		{
			if ( ( t <= 0.0f ) || ( t >= 1.0f ) )
				return false;
			vecStart += t * vecDir;
			VectorSubtract( vecEnd, vecStart, vecDir );

//			if ( bDrawDebug )
//			{
//				NDebugOverlay::Box( vecStart, Vector( -2, -2, -2 ), Vector( 2, 2, 2 ), 0, 128, 128, 20, 0.25);
//				NDebugOverlay::Line( vecStart, vecStart + 10.0f * pParticle->m_surfaces[ s ], 0, 128, 128, true, 0.25f );
//			}
		}
		else
		{
			if ( ( t > 0.0f ) && ( t < 1.0f ) )
			{
				VectorMA( vecStart, t, vecDir, vecEnd );
				VectorSubtract( vecEnd, vecStart, vecDir );

//				if ( bDrawDebug )
//				{
//					NDebugOverlay::Box( vecEnd, Vector( -2, -2, -2 ), Vector( 2, 2, 2 ), 0, 128, 128, 20, 0.25);
//					NDebugOverlay::Line( vecEnd, vecEnd + 10.0f * pParticle->m_surfaces[ s ], 0, 128, 128, true, 0.25f );
//				}
			}
		}
	}
	return true;
}

							 
//-----------------------------------------------------------------------------
// Generates forces which move the blob nearer to the goal
//-----------------------------------------------------------------------------
void CNPC_Blob::MoveTowardsGoal( CBlobGroup* group )
{
	if ( m_bSuppressMoveTowardGoal )
		return;

	// Push particles directly towards their current goal, with physics-driven movement
	// FIXME: it's easy for particles to get caught in local minima. Need a better system to deal with this than 'stragglers'
	//   ('breadcrumbs', strong parent bond constraints, intermediate goal points, randomized forces, particle teleport, etc)

	// NOTE: this is the main force that is applied to the root particle (it drives navigation)

	VPROF_BUDGET( "CNPC_Blob::MoveTowardsGoal", VPROF_BUDGETGROUP_NPCS );
	float maxSpeedTowardsGoal = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_MAX_SPEED_TOWARDS_GOAL ];
	float timeToMaxSpeedTowardsGoal = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_TIME_TO_MAX_SPEED_TOWARDS_GOAL ];
	if ( IsMostlyBrain() || ( m_BlobType == BLOB_TYPE_SPITTER ) || ( m_BlobType == BLOB_TYPE_SPAWNER ) )
	{
		float flSpeedFactor = IsCurSchedule( SCHED_TAKE_COVER_FROM_ENEMY_USE_BRAIN_HINTS ) ? 4.0f : 2.0f;
		maxSpeedTowardsGoal *= flSpeedFactor;
	}

	float accelTowardsGoal = maxSpeedTowardsGoal / timeToMaxSpeedTowardsGoal;
	float moveToGoalBrakeTime = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_TOWARDS_GOAL_BRAKE_TIME ];
	Vector gravityDir;
	physenv->GetGravity( &gravityDir );
	gravityDir.NormalizeInPlace();

	// Migrate stragglers towards the brain (which should contain the particle driving navigation).
	// This avoids stragglers cheating their way ahead of the blob (through clip brushes, etc).
	Vector brainPosition, brainVelocity;
	ComputeBrainPosition( brainPosition, brainVelocity );

	// push spheres around to meet position targets
	Vector vecDelta, vecPredictedPos, vecPredictedToGoal, vecParticlePosition;
	float dt = physenv->GetSimulationTimestep();
	for(int i = 0; i < group->m_members.Count(); i++)
	{
		CBlobParticle* pParticle = group->m_members[i];

		if ( pParticle->m_flags & BLOB_PARTICLE_STRAGGLER )
		{
			// Stragglers fly straight at the brain and 
			// match its velocity, ignoring everything else
			Vector goalDir = brainPosition - pParticle->m_position;
			Vector velocity = sv_blob_straggler_speed.GetFloat()*( goalDir / goalDir.Length() );
			if ( goalDir.Length() < m_flRadius )
				velocity = vec3_origin;
			velocity += brainVelocity;
			pParticle->m_vphysParticle->SetVelocityInstantaneous( &velocity, NULL );
			pParticle->m_force = vec3_origin;
			continue;
		}

		if ( pParticle->m_flags & BLOB_PARTICLE_BIRTH )
		{
			// Regenerating particles don't start moving until they've reached a certain size
			int particleIndex = s_ExpandedParticleData[ pParticle->m_vphysParticle->GetGameIndex() ].iNetworkBypassIndex;
			if ( BLOBPARTICLERADIUS( particleIndex ) < 0.3f )
				continue;
		}

		Vector particleDelta = pParticle->m_vecMoveGoal - pParticle->m_position;

		// Adjust movement direction to account for the particle's contact surfaces:

		// First project the movement direction onto the particle's chosen movement surface
		bool bBlocked = false;
		Vector moveDir = particleDelta;
		float flTotalLenSqr = particleDelta.LengthSqr();
		if ( flTotalLenSqr <= 1.0f )
		{
			moveDir.Init();
		}
		else
		{
			VectorNormalize( moveDir );
			if ( pParticle->m_isInContact )
			{
				float flDegreeBlocked = ClipMovementToContactSurfaces( pParticle, moveDir );
				if ( flDegreeBlocked != 0.0f )
				{
					bBlocked = true;

					// Look at the line segment between our current move target
					// and our previous one clip it to the contact surface planes
					// NOTE! I'm adding the step height here because vecStart + vecEnd
					// are typically going to be locations of nodes. The ray connecting
					// them often goes through solid because the ray can connect
					// anytime an NPC can step between the two endpoints using stepheight
					// logic. Upping the positions by stepheight in z will make
					// it far more likely the ray is not in solid.
					Vector vecStart = pParticle->m_vecPrevWaypointPosition;
					Vector vecEnd = pParticle->m_vecMoveGoal;
					vecStart.z += StepHeight(); vecEnd.z += StepHeight();
					if ( ClipRayToContactSurfaces( pParticle, vecStart, vecEnd ) )
					{
						// Clipping was successful. Let's try to move to that position now.
						VectorSubtract( vecEnd, vecParticlePosition, moveDir );
					}
					else
					{
						// Clipping wasn't successful. Try moving up for shits and grins
						VectorMA( moveDir, -flDegreeBlocked, gravityDir, moveDir );
					}
				}
				VectorNormalize( moveDir );
			}
		}

		// We want our velocity to be the max speed towards the goal. Apply the max accel force
		// in the direction needed to change our current velocity to that desired velocity:
		float flSpeedTowardsGoal = maxSpeedTowardsGoal;
		if ( group->m_root->m_nWaypointTargetIndex >= 0 )
		{
			int nDelta = pParticle->m_nWaypointTargetIndex - group->m_root->m_nWaypointTargetIndex;
			float flFactor = 1.0f;
			if ( nDelta >= 0 )
			{
				// Slow down particles which are ahead of the root
				flFactor = RemapValClamped( (float)nDelta, 0.0f, 3.0f, 1.0f, 0.6f );
			}
			else
			{
				// Speed up particles which are behind of the root
				flFactor = RemapValClamped( (float)nDelta, 0.0f, -3.0f, 1.0f, 3.0f );
			}

			flSpeedTowardsGoal *= flFactor;
		}

		Vector desiredVelocity	= flSpeedTowardsGoal*moveDir;
		
		if ( !bBlocked )
		{
			float flBrakeTime = moveToGoalBrakeTime;
			if ( pParticle->m_bMovingTowardFinalGoal )
			{
				flBrakeTime = 0.1f;
			}

			float flTotalLen = sqrt( flTotalLenSqr );
			float flTimeToDest = ( flTotalLen != 0.0f ) ? flTotalLen / flSpeedTowardsGoal : 0.0f;
			if ( flTimeToDest < flBrakeTime )
			{
				flTimeToDest = ( flBrakeTime > 0.0f ) ? flTimeToDest / flBrakeTime : 1.0f;
				desiredVelocity *= RemapVal( flTimeToDest, 0.0f, 1.0f, 0.1f, 1.0f );	
			}
		}

		Vector deltaVel	= desiredVelocity - pParticle->m_velocity;
		float flSpeedDelta = VectorNormalize( deltaVel ) / dt;
		if ( flSpeedDelta > accelTowardsGoal )
		{
			flSpeedDelta = accelTowardsGoal;
		}
		Vector force;
		VectorMultiply( deltaVel, flSpeedDelta * pParticle->m_mass, force );

		// Particles in contact with a surface have 'support' (traction - which allows them
		// to move parallel to the surface), which they also convey to nearby particles.
		// Generally particles in contact (and providing support) are 'sticky' particles.
		force					*= clamp( pParticle->m_support, 0.0f, 1.0f );

		// Stunned blobs move more slowly
		force					*= ( 1.0f - GetStunFraction() );

		// Death blobs move way more slowly
		if ( pParticle->m_flags & BLOB_PARTICLE_DEATH )
		{
			force *= 0.1f;
		}

		pParticle->m_force		+= force;

		if ( sv_blob_debug_draw_goal_forces.GetBool() )
		{
			NDebugOverlay::Line( pParticle->m_position, pParticle->m_position + 0.0002f*m_flRadius*force, 
				48, (pParticle->m_type == ROOT_PARTICLE ) ? 255 : 0, 48, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		}
	}
}


//---------------------------------------------------------
//---------------------------------------------------------

void CNPC_Blob::MeleeAttack( float distance, float damage, QAngle &viewPunch, Vector &shove )
{
	if ( GetEnemy() )
	{
		m_fClingTimeOut = gpGlobals->curtime + 0.5f;
		m_vClingPosition = GetEnemy()->GetAbsOrigin();

		MarkParticlesCloseToPosition( m_vClingPosition, BLOB_PARTICLE_PERFORMED_MELEE_ATTACK );

		// Play a random attack hit sound
		EmitSound( "HL2Player.BurnPain" );
	}
}

void CNPC_Blob::ForceTransferThroughPipe( CFunc_Pipe_Entrance *pEntrance, CFunc_Pipe_Entrance *pExit )
{
	if ( m_hPipeEntrance.Get() || m_hPipeExitBlob.Get() )
	{
		return;
	}

	if ( !pEntrance || !pExit || pEntrance->m_bDisabled || pExit->m_bDisabled )
	{
		return;
	}

	m_hPipeEntrance = pEntrance;
	m_hPipeExit = pExit;
	m_bForcePipeTransfer = true;	
}

void CNPC_Blob::SetPhysParamState( BlobPhysicsParamState_t *pPhysParamState )
{
	if ( m_bPhysParamStateOverloaded )
		return;

	if ( !pPhysParamState )
	{
		DevWarning( "Parameter set not found. Setting blob physics parameter state to Default.\n" );
		pPhysParamState = GetBlobPhysicsParameterStates( 0 );

		AssertMsg( pPhysParamState, "Default blob physics parameter set not found!" );

		if( !pPhysParamState )
		{
			DevWarning( "Default parameter set not found!\n" );
			memset( m_pfBlobPhysParamState, 0, sizeof( m_pfBlobPhysParamState ) );
			return;
		}

		memcpy( m_pfBlobPhysParamState, pPhysParamState->pfProps, sizeof( m_pfBlobPhysParamState ) );
		return;
	}

	memcpy( m_pfBlobPhysParamState, pPhysParamState->pfProps, sizeof( m_pfBlobPhysParamState ) );
}

void CNPC_Blob::SetPhysParamState( const char *pchName )
{
	if ( m_bPhysParamStateOverloaded )
		return;

	BlobPhysicsParamState_t *physParams = GetBlobPhysicsParameterStates( pchName );

	if ( physParams )
	{
		// Check if we're already in this state
		if ( m_iBlobPhysParamState == physParams->iStateID )
			return;

		m_iBlobPhysParamState = physParams->iStateID;
	}

	SetPhysParamState( physParams );
}

void CNPC_Blob::SetPhysParamState( int iStateID )
{
	if ( m_bPhysParamStateOverloaded )
		return;

	// Check if we're already in this state
	if ( m_iBlobPhysParamState == iStateID )
		return;

	m_iBlobPhysParamState = iStateID;

	// Special case to force reloading of data, but we don't want to spew the failure dev message, so bail
	if ( m_iBlobPhysParamState == -1 )
		return;

	BlobPhysicsParamState_t *physParams = GetBlobPhysicsParameterStates( iStateID );

	SetPhysParamState( physParams );
}

bool CNPC_Blob::HasGrenade( CBaseEntity *pGrenade )
{
	for ( int i = 0; i < m_capturedGrenades.Count(); i++ )
	{
		if ( m_capturedGrenades[i].m_hGrenade == pGrenade )
			return true;
	}
	return false;
}

void CNPC_Blob::ProcessCapturedGrenades( void )
{
	// Apply forces so grenades get captured inside the blob (makes them a more effective weapon)
	float dt = physenv->GetSimulationTimestep();
	CBaseGrenade *pGrenade = NULL;

	while ( m_capturedGrenades.Count() < sv_blob_capture_grenades.GetInt() )
	{
		pGrenade = (CBaseGrenade *)gEntList.FindEntityByClassnameWithin( pGrenade, "npc_grenade_frag", GetAbsOrigin(), 10*m_flRadius );
		if ( !pGrenade )
			break;

		IPhysicsObject *pGrenadePhysics = pGrenade->VPhysicsGetObject();
		if ( !pGrenadePhysics || !pGrenade->GetThrower() || HasGrenade( pGrenade ) )
			continue;

		Vector velocity, grenadePos[2];
		CBlobParticle *pCloseParticle;
		float grenadeRadius;
		int flagsToIgnore;

		// Check current AND future locations ( CGrenadeFrag::VPhysicsUpdate() goes to heroic lengths to bounce off
		// things that don't want to collide with it, so we look ahead two frames to pre-emptively capture grenades)
		//
		// FIXME: figure out a better way of doing this!
		//
		pGrenadePhysics->GetVelocity( &velocity, NULL );
		grenadePos[0]	= pGrenade->GetAbsOrigin();
		grenadePos[1]	= pGrenade->GetAbsOrigin() + 2*dt*velocity;
		grenadeRadius	= 0.5f*( pGrenade->WorldAlignMaxs() - pGrenade->WorldAlignMins() ).Length();

		for ( int i = 0; i < 2; i++ )
		{
			flagsToIgnore	= BLOB_PARTICLE_STRAGGLER | BLOB_PARTICLE_TENTACLE | BLOB_PARTICLE_BRAIN;
			pCloseParticle	= FindClosestParticleToPoint( grenadePos[i], flagsToIgnore );

			if ( pCloseParticle &&
				( pCloseParticle->m_group == m_pMainGroup ) &&
				( grenadePos[i].DistTo( pCloseParticle->m_position ) < ( m_flRadius + grenadeRadius ) ) )
			{
				m_capturedGrenades.AddToTail( BlobCapturedGrenade_t( pGrenade ) );
				break;
			}
		}
	}

	// Apply forces to our captured grenades
	Vector brainPosition, brainVelocity;
	ComputeBrainPosition( brainPosition, brainVelocity );
	for ( int i = 0; i < m_capturedGrenades.Count(); i++ )
	{
		CBaseEntity *pGrenade = m_capturedGrenades[ i ].m_hGrenade;
		if ( !pGrenade || !pGrenade->VPhysicsGetObject() )
		{
			// Remove dead grenades
			m_capturedGrenades.FastRemove( i );
			i--;
			continue;
		}

		// Pull the grenade to within some distance of the blob's centre (place it on the same
		// side as the brain, which we want the blast to separate from the rest of the blob)
		static const float goalDistance = 2.0f*m_flRadius;
		static const float timeToGoal   = 0.2f;
		Vector nearBrainPos = VectorLerp( pGrenade->GetAbsOrigin(), brainPosition, 0.8f );
		Vector delta = ( GetAbsOrigin() - nearBrainPos ).Normalized();
		Vector goal  = ( GetAbsOrigin() - delta*goalDistance );
		// Aim to get there in x seconds (be really harsh, don't lose hold of it)
		Vector velocity = ( goal - pGrenade->GetAbsOrigin() ) / timeToGoal;
		pGrenade->VPhysicsGetObject()->SetVelocity( &velocity, NULL );

		if ( shaper_draw_debug.GetInt() > 1 )
		{
			NDebugOverlay::EntityBounds( pGrenade, 96, 255, 96, 255, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		}
	}
}


// ------------------------------------------------------------------
// START SHAPETARGET CODE ----------------------------------------------------------
// ------------------------------------------------------------------

void CNPC_Blob::DoEnvelopeOld( const BlobShapeTarget_t *pShapeTarget, int &nStartParticle )
{
	CBaseAnimating *pBaseAnimating = pShapeTarget->m_hBaseAnimating.Get();

	if ( !pBaseAnimating )
		return;

	// Storage data for mins and maxs at various heights that are about as tall as a particle radius
	Vector pvMins[ BLOB_ENVELOPE_STACK_MAX ];
	Vector pvMaxs[ BLOB_ENVELOPE_STACK_MAX ];
	int iUsedStacks = 0;
	float fZStackSize = m_flRadius * 2.0f;

	// Compute a box in world space that surrounds this entity
	Vector vMins, vMaxs;

	int iHitbox = 0;

	while ( pBaseAnimating->ComputeHitboxSurroundingBox( iHitbox, &vMins, &vMaxs ) )
	{
		float fCurrentZTest = floorf( vMins.z / fZStackSize ) * fZStackSize;
		float fFinalZTest = floorf( vMaxs.z / fZStackSize ) * fZStackSize;

		// Modify or create stacks for each increment of Z
		while ( fCurrentZTest <= fFinalZTest )
		{
			int iStack;

			// Look through the existing stacks for a matching Z
			for ( iStack = 0; iStack < iUsedStacks; ++iStack )
			{
				if ( pvMins[ iStack ].z >= fCurrentZTest && pvMins[ iStack ].z < fCurrentZTest + fZStackSize )
				{
					// Found a suitable stack!
					break;
				}
			}

			// If we didn't find a stack we'll need to create it
			if ( iStack == iUsedStacks )
			{
				if ( iUsedStacks < BLOB_ENVELOPE_STACK_MAX )
				{
					// Put a new stack on the end
					pvMins[ iStack ].Init( FLT_MAX, FLT_MAX, FLT_MAX );
					pvMaxs[ iStack ].Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );

					++iUsedStacks;
				}
				else
				{
					// Used all the possible stacks, so lets throw away the lowest one
					float fLowestZ = FLT_MAX;
					int iLowestStack = 0;

					for ( iStack = 0; iStack < iUsedStacks; ++iStack )
					{
						if ( pvMins[ iStack ].z < fLowestZ )
						{
							fLowestZ = pvMins[ iStack ].z;
							iLowestStack = iStack;
						}
					}

					iStack = iLowestStack;
					pvMins[ iStack ].Init( FLT_MAX, FLT_MAX, FLT_MAX );
					pvMaxs[ iStack ].Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
				}
			}

			// Modify the extents of the stack
			VectorMin( pvMins[ iStack ], vMins, pvMins[ iStack ] );
			VectorMax( pvMaxs[ iStack ], vMaxs, pvMaxs[ iStack ] );
			pvMins[ iStack ].z = fCurrentZTest;
			pvMaxs[ iStack ].z = fCurrentZTest;

			fCurrentZTest += fZStackSize;
		}

		++iHitbox;
	}

	if ( iUsedStacks == 0 )
		return;

	// Sort the stacks
	for ( int i = 0; i < iUsedStacks; ++i )
	{
		for ( int j = 0; j < iUsedStacks - i - 1; ++j )
		{
			if ( pvMins[ j ].z > pvMins[ j + 1 ].z )
			{
				Vector vTemp = pvMins[ j ];
				pvMins[ j ] = pvMins[ j + 1 ];
				pvMins[ j + 1 ] = vTemp;

				vTemp = pvMaxs[ j ];
				pvMaxs[ j ] = pvMaxs[ j + 1 ];
				pvMaxs[ j + 1 ] = vTemp;
			}
		}
	}

	// Cap off the top
	while ( iUsedStacks < BLOB_ENVELOPE_STACK_MAX && pvMaxs[ iUsedStacks - 1 ].DistTo( pvMins[ iUsedStacks - 1 ] ) > fZStackSize * 2.0f )
	{
		pvMins[ iUsedStacks	] = pvMins[ iUsedStacks - 1 ] + Vector( fZStackSize, fZStackSize, fZStackSize );
		pvMaxs[ iUsedStacks	] = pvMaxs[ iUsedStacks - 1 ] + Vector( -fZStackSize, -fZStackSize, fZStackSize );
		++iUsedStacks;
	}

	// Move particles around stacks
	int iMaxParticlesToUse = ( 1.0f - ( ( m_fClingTimeOut - gpGlobals->curtime ) / ( BLOB_CLING_TIME ) ) ) * 4.0f * m_nActiveParticles;
	int nParticlesUsed = 0;

	for ( int iStack = 0; iStack < iUsedStacks; ++iStack )
	{
		// Get the mins and maxs of the current stack
		vMins = pvMins[ iStack ] + Vector( -m_flRadius, -m_flRadius, 0.0f );
		vMaxs = pvMaxs[ iStack ] + Vector( m_flRadius, m_flRadius, 0.0f );

		// Wrap around each side of the square
		for ( int i = 0; i < 4; ++i )
		{
			Vector vStartParticlePos = Vector( ( i < 2 ) ? ( vMins.x ) : ( vMaxs.x ), 
											   ( i == 0 || i == 3 ) ? ( vMins.y ) : ( vMaxs.y ), 
											   vMins.z );

			Vector vEndParticlePos = Vector( ( i == 0 || i == 3 ) ? ( vMins.x ) : ( vMaxs.x ), 
											 ( i < 2 ) ? ( vMaxs.y ) : ( vMins.y ), 
											 vMins.z );

			Vector vPrevToCurrent = vEndParticlePos - vStartParticlePos;

			while ( ( nStartParticle < m_nActiveParticles ) &&
					( nParticlesUsed < iMaxParticlesToUse ) &&
					( vPrevToCurrent.Length() > m_flRadius * 2.0f ) )
			{
				CBlobParticle *pParticle = m_blobParticles[ nStartParticle ];
				if ( ( pParticle->m_type != ROOT_PARTICLE ) && !( pParticle->m_flags & BLOB_PARTICLE_TENTACLE ) )
				{
					VectorNormalize( vPrevToCurrent );
					vPrevToCurrent *= m_flRadius * 2.0f;

					Vector vCurrentParticlePos;
					pParticle->m_vphysParticle->GetPosition( &vCurrentParticlePos, NULL );

					Vector vParticleVelocity;
					vParticleVelocity = (vStartParticlePos - vCurrentParticlePos) * 5.0f;
					pParticle->m_vphysParticle->SetVelocityInstantaneous( &vParticleVelocity, NULL );

					vStartParticlePos = vStartParticlePos + vPrevToCurrent;
					vPrevToCurrent = vEndParticlePos - vStartParticlePos;

					nParticlesUsed++;
				}

				++nStartParticle;
			}
		}
	}
}


void CNPC_Blob::DoEnvelope( const CUtlVector<ShapeEdge_t> &edgeList, int &nStartParticle, int nArm, bool bDisableCollision )
{
	// FIXME: need to address disconnected particles getting stuck behind walls when trying to attach to a shape
	//        (keep an array of particle pointers for DoEnvelope, shuffle them to skip blocked particles)

	for ( int iEdge = 0; iEdge < edgeList.Count(); iEdge++ )
	{
		const ShapeEdge_t &edge = edgeList[ iEdge ];

		Vector vEdgePos, vEdgeDelta, vParticlePos, vParticleVel;
		vEdgePos	= edge.startPos;
		vEdgeDelta	= edge.endPos - edge.startPos;
		if ( vEdgeDelta.LengthSqr() > m_flRadius*m_flRadius )
		{
			float edgeLength	= VectorNormalize( vEdgeDelta );
			float stepSize		= 2.0f*m_flRadius / edgeLength;
			float fractionLeft	= 1.0f;

			while ( ( nStartParticle < m_nActiveParticles ) && ( fractionLeft > 0 ) )
			{
				CBlobParticle *pParticle = m_blobParticles[ nStartParticle ];
				if ( ( pParticle->m_type != ROOT_PARTICLE ) && !( pParticle->m_flags & BLOB_PARTICLE_TENTACLE ) )
				{
					// Match position+velocity with this point on the edge
					// FIXME: there's still a bit of lag, even for constant-velocity targets...
					pParticle->m_vphysParticle->GetPosition( &vParticlePos, NULL );
					VectorLerp( edge.endVel, edge.startVel, fractionLeft, vParticleVel );
					vParticleVel += 5.0f*( vEdgePos - vParticlePos );
					pParticle->m_vphysParticle->SetVelocityInstantaneous( &vParticleVel, NULL );
					pParticle->m_nArm = nArm;

					// Disabling collision allows particles to get outside the world, which is 'not ok'
					// FIXME: Disable collision with just the target entity, if possible.
					//if ( bDisableCollision )
					//	pParticle->m_vphysParticle->EnableCollisions( false );

					vEdgePos += 2.0f*m_flRadius*vEdgeDelta;
					fractionLeft -= stepSize;
				}
				nStartParticle++;
			}
		}
	}
}


void CNPC_Blob::GetShapeTargetAttachmentTransform( const BlobShapeTarget_t *pBlobShapeTarget, matrix3x4_t &mBlobToWorld )
{
	// The returned matrix rotates the shape with the blob, and pushes its origin
	// out onto the 'surface' of the blob in the given attachment direction

	Vector vAttachmentOrigin, vAttachmentDirection, vDirectionPoint, vAttachmentNormal;
	QAngle vAttachmentAngles;
	matrix3x4_t mRot;

	// Find the origin and attachment direction in blob-local space
	vAttachmentOrigin = GetAbsOrigin();
	AngleMatrix( GetAbsAngles(), mRot );
	VectorTransform( pBlobShapeTarget->m_vDirection, mRot, vAttachmentDirection );
	vDirectionPoint = vAttachmentOrigin + vAttachmentDirection * 1000.0f;

	GetVectors( &vAttachmentNormal, NULL, NULL );
	if ( !pBlobShapeTarget->m_vDirection.IsZero() )
	{
		// Locate the shape on the blob's surface in the attachment direction
		NearestSurfacePointAndNormalForBlobGroup( m_pMainGroup, vDirectionPoint, vAttachmentOrigin, vAttachmentNormal, m_flRadius );
	}

	vAttachmentNormal.z = 0;
	VectorAngles( vAttachmentNormal, vAttachmentAngles );
	AngleMatrix( vAttachmentAngles, vAttachmentOrigin, mBlobToWorld );
}


void CNPC_Blob::GetShapeTargetEdgeListFromBaseAnimating( CBaseAnimating *pBaseAnimating, CUtlVector<ShapeEdge_t> &edgeList, bool bConvertToLocalSpace )
{
	// Extract edges from an animating model's bones
	if ( pBaseAnimating && pBaseAnimating->GetModelPtr()  )
	{
		matrix3x4_t mShapeToWorld = pBaseAnimating->EntityToWorldTransform();

		CStudioHdr *pStudioHdr = pBaseAnimating->GetModelPtr();
		for ( int iBone = 0; iBone < pStudioHdr->numbones(); ++iBone )
		{
			ShapeEdge_t edge;
			QAngle vAngle;

			mstudiobone_t *pStartBone = pStudioHdr->pBone( iBone );
			Assert( pStartBone );
			if ( pStartBone && ( pStartBone->parent >= 0 ) )
			{
				pBaseAnimating->GetBonePosition( iBone, edge.startPos, vAngle );
				pBaseAnimating->GetBonePosition( pStartBone->parent, edge.endPos, vAngle );

				// FIXME: need to get bone velocity (may have to compute it in here)
				//        ( see CreateServerRagdoll, it extracts positions at time t and (t-dt) to get velocity)
				edge.startVel = Vector(0,0,0);
				edge.endVel = Vector(0,0,0);

				if ( bConvertToLocalSpace )
				{
					// Transform from world-space back to the entity's local space
					VectorITransform(	edge.startPos, mShapeToWorld, edge.startPos );
					VectorITransform(	edge.endPos,   mShapeToWorld, edge.endPos );
					VectorIRotate(		edge.startVel, mShapeToWorld, edge.startVel );
					VectorIRotate(		edge.endVel,   mShapeToWorld, edge.endVel );
				}

				if ( edgeList.Count() < edgeList.NumAllocated() )
					edgeList.AddToTail( edge );
			}
		}
	}
}


void CNPC_Blob::GetShapeTargetEdgeList( const BlobShapeTarget_t *pBlobShapeTarget, CUtlVector<ShapeEdge_t> &edgeList )
{
	// Gets a set of edges to which the blob should move
	CShapeTarget   *pShapeTarget   = CShapeTarget::GetFirstTargetInShape( pBlobShapeTarget->m_ShapeTargetName );
	CBaseAnimating *pBaseAnimating = pBlobShapeTarget->m_hBaseAnimating;
	Assert( pShapeTarget || pBaseAnimating );

	bool bConvertToLocalSpace = !pBlobShapeTarget->m_bUseTargetPositionAngles;

	if ( pShapeTarget )
	{
		// Extract edges from a chain of CShapeTargets
		pShapeTarget->GetShapeEdgeList( edgeList, bConvertToLocalSpace );
	}
	else if ( pBaseAnimating && pBaseAnimating->GetModelPtr()  )
	{
		GetShapeTargetEdgeListFromBaseAnimating( pBaseAnimating, edgeList, bConvertToLocalSpace );
	}

	if ( !pBlobShapeTarget->m_bUseTargetPositionAngles )
	{
		// Transform the edges from shape target local space into world-space,
		// based on how the shape is attached to the surface of the blob
		matrix3x4_t mBlobToWorld;
		GetShapeTargetAttachmentTransform( pBlobShapeTarget, mBlobToWorld );

		for ( int i = 0; i < edgeList.Count(); i++ )
		{
			// Shape-local to world
			ShapeEdge_t &edge = edgeList[ i ];
			VectorTransform(	edge.startPos,	mBlobToWorld, edge.startPos );
			VectorTransform(	edge.endPos,	mBlobToWorld, edge.endPos );
			VectorRotate(		edge.startVel,	mBlobToWorld, edge.startVel );
			VectorRotate(		edge.endVel,	mBlobToWorld, edge.endVel );
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : Vector
//-----------------------------------------------------------------------------
Vector CNPC_Blob::EyePosition( void ) 
{
	// While spitting, the eye is at the tentacle endpoint
	Vector vecEyePosition;
	if ( m_hSpitVisibilityTestEyePos.Get() )
		return m_hSpitVisibilityTestEyePos->GetAbsOrigin();

	if ( m_pSpitTentacle )
		return m_pSpitTentacle->m_vecEyePosition;

	// Sorta faked, but it's better for cover purposes
	vecEyePosition = GetAbsOrigin();
	vecEyePosition += GetViewOffset();
	return vecEyePosition;
}


//-----------------------------------------------------------------------------
// Sound interests
//-----------------------------------------------------------------------------
int CNPC_Blob::GetSoundInterests( void )
{
	return BaseClass::GetSoundInterests() | SOUND_DANGER;
}


//-----------------------------------------------------------------------------
// Return true if this NPC should ignore the specified sound
//-----------------------------------------------------------------------------
bool CNPC_Blob::ShouldIgnoreSound( CSound *pSound )
{
	// These guys only pay attention to fire sounds
	if ( pSound->SoundTypeNoContext() & SOUND_DANGER )
	{
		if ( ( pSound->SoundContext() & SOUND_CONTEXT_FROM_FIRE ) == 0 )
			return true;
	}

	return BaseClass::ShouldIgnoreSound( pSound );
}

		    
int CNPC_Blob::BuildFireSoundList( )
{
	int iAudibleList = SOUNDLIST_EMPTY;
	int iSound = CSoundEnt::ActiveList();
	while ( iSound != SOUNDLIST_EMPTY )
	{
		CSound *pCurrentSound = CSoundEnt::SoundPointerForIndex( iSound );
		if ( !pCurrentSound )
			break;

		if ( ( SOUND_DANGER & pCurrentSound->SoundType() ) && !ShouldIgnoreSound( pCurrentSound ) )
		{
			// the npc cares about this sound, and it's close enough to hear.
			pCurrentSound->m_iNextAudible = iAudibleList;
			iAudibleList = iSound;
		}

		iSound = pCurrentSound->NextSound();
	}

	return iAudibleList;
}

extern ConVar shaper_repel_horizontal;

void CNPC_Blob::AddRepulsionForce( CBlobParticle *pParticle, const Vector &vecCenter, float flRadius, 
	float dt, float flVertFactor, float maxSpeedTowardsGoal, float fMaxRepelAccel, Vector *pForce )
{
	float flDistSqr = vecCenter.DistToSqr( pParticle->m_position );
	float flMinInfluenceRange = flRadius + m_flRadius;
	float flMaxInfluenceRange = 0.75f * flRadius + m_flRadius;

	if ( flDistSqr >= flMinInfluenceRange * flMinInfluenceRange )
		return;

	Vector vDelta = pParticle->m_position - vecCenter;

	// Horizontalize the force
	vDelta.z *= flVertFactor;

	float flDist = VectorNormalize( vDelta );
	float t = RemapValClamped( flDist, flMinInfluenceRange, flMaxInfluenceRange, 0.0f, 1.0f );
	vDelta *= maxSpeedTowardsGoal;

	// Compute the repulsion force, attenuating for particles already moving fast in that direction:
	Vector vecDesiredVel, vecDeltaVel;
	vecDeltaVel = vDelta - pParticle->m_velocity;
	float flSpeedDelta = VectorNormalize( vecDeltaVel ) / dt;
	if ( flSpeedDelta > fMaxRepelAccel )
	{
		flSpeedDelta = fMaxRepelAccel;
	}
	Vector vActualForce;
	VectorMultiply( vecDeltaVel, flSpeedDelta * pParticle->m_mass, vActualForce );

	// Also, add a force to cancel the particle's existing move-to-goal force:
	VectorMA( vActualForce,	-MIN( 0.0f, pParticle->m_force.Dot( vecDeltaVel ) ), vecDeltaVel, vActualForce );

	// Reduce forces by influence factor
	vActualForce *= t;

	*pForce += vActualForce;
}

void CNPC_Blob::GetFireRepelTargetForce( int iSound, CBlobParticle *pParticle, Vector *pForce )
{
	float maxSpeedTowardsGoal = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_MAX_SPEED_TOWARDS_GOAL ];
	float timeToMaxSpeedTowardsGoal = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_TIME_TO_MAX_SPEED_TOWARDS_GOAL ];
	float fMaxRepelAccel = shaper_repel_force.GetFloat()*( maxSpeedTowardsGoal / timeToMaxSpeedTowardsGoal );
	float dt = physenv->GetSimulationTimestep();

	float flVertFactor = 1.0f - shaper_repel_horizontal.GetFloat();
	pForce->Init();
	for ( CSound *pCurrentSound = NULL; iSound != SOUNDLIST_EMPTY; iSound = pCurrentSound->m_iNextAudible )
	{
		pCurrentSound = CSoundEnt::SoundPointerForIndex( iSound );
		Vector vecSound = pCurrentSound->GetSoundOrigin();
		AddRepulsionForce( pParticle, vecSound, pCurrentSound->Volume(), 
			dt, flVertFactor, maxSpeedTowardsGoal, fMaxRepelAccel, pForce );
	}
}

void CNPC_Blob::ComputeRegeneratorRepelForce( CBlobParticle *pParticle, int nCount, CBlobRegenerator **ppRegenerator, Vector *pForce )
{
	float maxSpeedTowardsGoal = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_MAX_SPEED_TOWARDS_GOAL ];
	float timeToMaxSpeedTowardsGoal = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_TIME_TO_MAX_SPEED_TOWARDS_GOAL ];
	float fMaxRepelAccel = shaper_repel_force.GetFloat()*( maxSpeedTowardsGoal / timeToMaxSpeedTowardsGoal );
	float dt = physenv->GetSimulationTimestep();

	pForce->Init();
	for ( int i = 0; i < nCount; ++i )
	{
		CBlobRegenerator *pRegenerator = ppRegenerator[i];
		const Vector &vecCenter = pRegenerator->GetAbsOrigin();
		float flMinDist = pRegenerator->BoundingRadius() + 32.0f;
		AddRepulsionForce( pParticle, vecCenter, flMinDist, 
			dt, 0.5f, maxSpeedTowardsGoal, fMaxRepelAccel, pForce );
	}
}

void CNPC_Blob::ApplyRepelTargetForces( CBlobGroup *pGroup )
{
	// Apply repulsion forces from nearby repel targets
	// UNDONE: optimize this w/ a sphere tree (sub-trees early-out when a zero force is returned)

	// NOTE: this force affects the root particle because it is *supposed* to interfere with navigation
	float maxSpeedTowardsGoal = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_MAX_SPEED_TOWARDS_GOAL ];
	float timeToMaxSpeedTowardsGoal = m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_TIME_TO_MAX_SPEED_TOWARDS_GOAL ];
	float fMaxRepelAccel = shaper_repel_force.GetFloat()*( maxSpeedTowardsGoal / timeToMaxSpeedTowardsGoal );
	int nFireSoundList = BuildFireSoundList( );
	for ( int i = 0; i < pGroup->m_members.Count(); i++ )
	{
		CBlobParticle *pParticle = pGroup->m_members[i];
		pParticle->m_flags &= ~BLOB_PARTICLE_NEAR_DANGER;

		if ( pParticle->m_flags & BLOB_PARTICLE_BIRTH )
			continue;

		Vector vForce(0,0,0), vFireForce(0,0,0), vActualForce(0,0,0);
		CShapeTarget::GetRepelTargetForce( pParticle->m_position, m_flRadius, vForce );
		if ( !vForce.IsZero() )
		{
			float fStrength = vForce.NormalizeInPlace();

			// Compute the repulsion force, attenuating for particles already moving fast in that direction:
			static const float MAX_FORCE_TIME = 0.85f;
			float fMaxVel		= 0.5f*fMaxRepelAccel*MAX_FORCE_TIME*MAX_FORCE_TIME;
			float fAttenuate	= 1.0f - clamp( ( vForce.Dot( pParticle->m_velocity ) / fMaxVel ), 0.0f, 1.0f );
			fAttenuate			= clamp( 3.0f*fAttenuate, 0.0f, 1.0f );
			vActualForce	= fStrength*fAttenuate*vForce*fMaxRepelAccel*pParticle->m_mass;

			// Also, add a force to cancel the particle's existing move-to-goal force:
			vActualForce		-= fStrength*vForce*( MIN( 0.0f, pParticle->m_force.Dot( vForce ) ) );
		}

		GetFireRepelTargetForce( nFireSoundList, pParticle, &vFireForce );
		vActualForce += vFireForce;

		if ( !vActualForce.IsZero() )
		{
			pParticle->m_flags |= BLOB_PARTICLE_NEAR_DANGER;
		}

		// Repulsion forces (and explosions) should leave the brains exposed:
		if ( pParticle->m_flags & BLOB_PARTICLE_BRAIN )
		{
			vActualForce *= 0.1f;
		}

		// Now push back in the repulsion-force direction
		pParticle->m_force += vActualForce;
	}

	int nCount = CBlobRegenerator::GetBlobRegeneratorCount();
	int nActiveCount = 0;
	CBlobRegenerator **ppRegenerators = (CBlobRegenerator**)stackalloc( nCount * sizeof(CBlobRegenerator*) );
	for ( int i = 0; i < nCount; ++i )
	{
		CBlobRegenerator *pRegenerator = CBlobRegenerator::GetBlobRegenerator( i );
		if ( !pRegenerator || !pRegenerator->IsBuilding() )
			continue;

		ppRegenerators[nActiveCount++] = pRegenerator;
	}

	if ( nActiveCount == 0 )
		return;

	for ( int i = 0; i < pGroup->m_members.Count(); i++ )
	{
		CBlobParticle *pParticle = pGroup->m_members[i];

		Vector vForce;
		ComputeRegeneratorRepelForce( pParticle, nActiveCount, ppRegenerators, &vForce );
		pParticle->m_force += vForce;
	}
}


void CNPC_Blob::TriggerRepelTargets( void )
{
	// See if this blob's particles proximity-trigger any repel targets
	// UNDONE: optimize this w/ a sphere tree (sub-trees use 'onlycheck' calls and early-out when it returns false)

	for ( int i = 0; i < m_blobParticles.Count(); i++ )
	{
		CBlobParticle *pParticle = m_blobParticles[i];
		bool bDontOnlyCheck = false;
		CShapeTarget::TriggerRepelTargets( pParticle->m_position, m_flRadius, bDontOnlyCheck );
	}
}


void CNPC_Blob::PurgeDeadShapeTargets( void )
{
	// Clear out invalidated shape targets
	for ( int i = ( m_ShapeTargets.Count() - 1 ); i >= 0; i-- )
	{
		BlobShapeTarget_t &shapeTarget = m_ShapeTargets[ i ];
		if ( ( shapeTarget.m_hBaseAnimating.Get() == NULL ) &&
			 ( CShapeTarget::GetFirstTargetInShape( shapeTarget.m_ShapeTargetName ) == NULL ) )
		{
			m_ShapeTargets.FastRemove( i );
		}
	}
}


void CNPC_Blob::RemoveShapeTarget( CBaseAnimating *pBaseAnimating )
{
	for ( int i = 0; i < m_ShapeTargets.Count(); i++ )
	{
		if ( m_ShapeTargets[ i ].m_hBaseAnimating == pBaseAnimating )
			m_ShapeTargets.FastRemove( i-- );
	}
}


void CNPC_Blob::AddShapeTarget( CBaseAnimating *pBaseAnimating, Vector vDirection, bool bUseTargetPositionAngles /*= false*/ )
{
	BlobShapeTarget_t &newShapeTarget = m_ShapeTargets[ m_ShapeTargets.AddToTail() ];
	newShapeTarget.m_hBaseAnimating = pBaseAnimating;
	newShapeTarget.m_vDirection = vDirection;
	newShapeTarget.m_bUseTargetPositionAngles = bUseTargetPositionAngles;
	newShapeTarget.m_bDisableCollision = bUseTargetPositionAngles;
}


void CNPC_Blob::RemoveShapeTarget( CShapeTarget *pShapeTarget )
{
	for ( int i = 0; i < m_ShapeTargets.Count(); i++ )
	{
		if ( m_ShapeTargets[ i ].m_ShapeTargetName == pShapeTarget->GetShapeName() )
			m_ShapeTargets.FastRemove( i-- );
	}
}


void CNPC_Blob::AddShapeTarget( CShapeTarget *pShapeTarget, bool bClearExistingTargets )
{
	// Ignore this command if chasing a parent blob - it means the source blob split,
	// so really just want the bigger part attaching to shape targets.
	// FIXME: we need to come up with a paradigm for filtering blob messages, to deal w/ blob splitting - perhaps blobs
	//        which accidentally split get renamed, so that the sub-blob never gets messages intended for its parent?
	//        I think we just need to be clear w.r.t "subsidiary blobs" and "independent blobs".
	if ( m_hAutoSplitBlob.Get() == NULL )
	{
		Assert( !! pShapeTarget->GetShapeName() );
		if ( !! pShapeTarget->GetShapeName() )
		{
			// Optionally, clear out existing shape targets
			if ( bClearExistingTargets )
				m_ShapeTargets.RemoveAll();

			// Avoid duplicates
			RemoveShapeTarget( pShapeTarget );

			BlobShapeTarget_t &newShapeTarget = m_ShapeTargets[ m_ShapeTargets.AddToTail() ];
			newShapeTarget.m_ShapeTargetName = pShapeTarget->GetShapeName();
			newShapeTarget.m_bUseTargetPositionAngles = true;
		}
	}
}


void CNPC_Blob::RemoveShapeTarget( CBaseEntity *pBaseEntity )
{
	CShapeTarget   *pShapeTarget   = dynamic_cast<CShapeTarget   *>( pBaseEntity );
	CBaseAnimating *pBaseAnimating = dynamic_cast<CBaseAnimating *>( pBaseEntity );

	if ( pShapeTarget )
	{
		RemoveShapeTarget( pShapeTarget );
	}
	else if ( pBaseAnimating )
	{
		RemoveShapeTarget( pBaseAnimating );
	}
}


bool CNPC_Blob::HasShapeTarget( CShapeTarget *pShapeTarget )
{
	if ( !! pShapeTarget->GetShapeName() )
	{
		for ( int i = 0; i < m_ShapeTargets.Count(); i++ )
		{
			if ( m_ShapeTargets[ i ].m_ShapeTargetName == pShapeTarget->GetShapeName() )
				return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------
// END SHAPETARGET CODE -------------------------------------------------------------
// ------------------------------------------------------------------


// ------------------------------------------------------------------
// START TENTACLES CODE -------------------------------------------------------------
// ------------------------------------------------------------------

void CNPC_Blob::TentacleCreateParticles( CBlobTentacle &tentacle )
{
	if ( tentacle.m_blobGroup == NULL )
	{
		tentacle.m_blobGroup = new CBlobGroup();
		m_blobGroups.AddToTail( tentacle.m_blobGroup );
		tentacle.m_blobGroup->m_bIsTentacle = true;

		for ( int i = 0; i < BLOB_NUM_TENTACLE_PARTICLES; i++ )
		{
			// FIXME: on the frame when new particles are created, they momentarily appear at the origin on the client!

			CBlobParticle* pParticle = CreateParticle( tentacle.m_pRoot->m_position, vec3_origin );
			pParticle->m_type = ( i == 0 ) ? ROOT_PARTICLE : SKIN_PARTICLE;
			ActivateParticle( tentacle.m_blobGroup, pParticle );

			pParticle->m_flags |= BLOB_PARTICLE_TENTACLE;
			pParticle->m_pgoal = tentacle.m_blobGroup->m_root;
			// For now, tentacle particles don't collide (we'll deal with that fun prospect later...)
			pParticle->m_vphysParticle->EnableCollisions( false );
		}
	}
}


CBlobTentacle *CNPC_Blob::TentacleCreate( EHANDLE target, BlobTentacleAction action, 
	CBlobParticle *pTentacleRoot, const Vector &targetOffset, float speedMultiplier )
{
	Assert( m_fTentacleTimeout < gpGlobals->curtime );
	AssertMsg( !!target, "ERROR: Blob cannot create a tentacle without a target entity..." );
	CBaseEntity *pEnemy = target;
	if ( !pEnemy )
		return NULL;

	// Find a free tentacle slot
	CBlobTentacle *result = NULL;
	for ( int i = 0; i < BLOB_MAX_TENTACLES; i++ )
	{
		if ( m_tentacles[i].m_nPhase == BLOB_TENTACLE_ACTION_NONE )
		{
			result = &m_tentacles[i];
			break;
		}
	}
	if ( !result )
		return NULL;

	if ( !pTentacleRoot )
	{
		pTentacleRoot = m_pMainGroup->m_root;
	}

	CBlobTentacle &tentacle = *result;

	// Figure out the direction of this tentacle from the action
	Vector vertical( 0, 0, 1 );
	Vector toTarget = ( pEnemy->BodyTarget( WorldSpaceCenter() ) - pTentacleRoot->m_position ).Normalized();
	Vector toLeft   = toTarget.Cross( vertical ).Normalized();
	Vector direction;
	switch( action )
	{
		case BLOB_TENTACLE_ACTION_ATTACK_LEFT:	direction = VectorLerp( vertical,  toLeft, 0.7f );	break;	// Hit from left
		case BLOB_TENTACLE_ACTION_ATTACK_RIGHT:	direction = VectorLerp( vertical, -toLeft, 0.7f );	break;	// Hit from right
		case BLOB_TENTACLE_ACTION_ATTACK_FRONT:	direction = vertical;								break;	// Hit from front/above
		case BLOB_TENTACLE_ACTION_SWIPE:
			{
				// FIXME: Choose the direction it has the most room in
				vertical = CrossProduct( toLeft, toTarget );
				VectorNormalize( vertical );

				Vector vecEnd;
				Vector vecDirection1 = VectorLerp( toLeft, vertical, BLOB_SWIPE_VERTICAL_BIAS );
				VectorMA( pTentacleRoot->m_position, sv_blob_tentacle_length.GetFloat(), vecDirection1, vecEnd );
				float flFraction1 = BlobParticleWorldTrace( pTentacleRoot->m_position, vecEnd );

				Vector vecDirection2 = VectorLerp( -toLeft, vertical, BLOB_SWIPE_VERTICAL_BIAS );
				VectorMA( pTentacleRoot->m_position, sv_blob_tentacle_length.GetFloat(), vecDirection2, vecEnd );
				float flFraction2 = BlobParticleWorldTrace( pTentacleRoot->m_position, vecEnd );

				tentacle.m_bSwipeLeftToRight = ( flFraction1 > flFraction2 );
				direction = tentacle.m_bSwipeLeftToRight ? vecDirection1 : vecDirection2;
			}
			break;
		case BLOB_TENTACLE_ACTION_SPIT:
			{
				if ( target.Get() != this )
				{
					direction = toTarget;
				}
				else
				{
					direction.Init( 0.0f, 0.0f, 1.0f );
				}
			}
			break;
		default: Assert( 0 ); return NULL;
	}

	// Find an entity in the map containing blob animations
	tentacle.m_hAnimationEnt = gEntList.FindEntityByModel( NULL, "models/blob_tentacle.mdl" );

	// Locate the tentacle on the blob's surface in the appropriate direction
	Vector attachmentPosition, attachmentNormal;
	tentacle.m_vecDirection = direction.Normalized();
	// FIXME: this doesn't work very well (need a more sophisticated surface attachment code which affects the physics sim)
	//	Vector attachmentOrigin = GetAbsOrigin() + 1000.0f*direction;
	//	NearestSurfacePointAndNormalForBlobGroup( m_pMainGroup, attachmentOrigin, attachmentPosition, attachmentNormal, m_flRadius );
	//	tentacle.m_vecBase = attachmentPosition;
	float flExtrudeTime = sv_blob_tentacle_extrude_time.GetFloat();
	if ( action == BLOB_TENTACLE_ACTION_SWIPE )
	{
		flExtrudeTime = 0.5f;
	}

	tentacle.m_SwipeHitTargets.RemoveAll();
	tentacle.m_pRoot = pTentacleRoot;
	tentacle.m_hTargetEnt = pEnemy;
	tentacle.m_fLength = sv_blob_tentacle_length.GetFloat();
	tentacle.m_fLastLength = 0.0f;
	tentacle.m_nAction = action;
	tentacle.m_fSpeedMultiplier = speedMultiplier;
	tentacle.m_fPhaseStart = gpGlobals->curtime;
	tentacle.m_fPhaseDuration = flExtrudeTime / speedMultiplier;
	tentacle.m_bPhaseIncomplete = false;
	tentacle.m_bHasSwooshed = false;
	tentacle.m_vecTargetOffset = targetOffset;
	tentacle.m_vecEyePosition = pTentacleRoot->m_position;
	if ( action != BLOB_TENTACLE_ACTION_SPIT )
	{
		tentacle.m_nPhase = BLOB_TENTACLE_STATE_EXTRUDE;
	}
	else
	{
		tentacle.m_nPhase = BLOB_TENTACLE_STATE_SPIT_EXTRUDE;
	}
	tentacle.m_vecLastValidSpitPos = TentacleComputeTargetPosition( tentacle, 0 );
	tentacle.m_vecTarget = tentacle.m_vecLastValidSpitPos - pTentacleRoot->m_position;

	// Tentacle announce
	EmitSound( "Weapon_BugBait.Splat", gpGlobals->curtime + 0.35f*tentacle.m_fPhaseDuration );

	// NOTE: the group/particles are created in TentacleProcessAnimation()
	return result;
}


void CNPC_Blob::TentacleDestroyParticles( CBlobTentacle &tentacle )
{
	Assert( !tentacle.m_nPhase == !tentacle.m_blobGroup );
	if ( tentacle.m_nPhase && tentacle.m_blobGroup )
	{
		// NOTE: The group will  automatically be destroyed when the last particle is removed:
		for ( int i = ( tentacle.m_blobGroup->m_members.Count() - 1 ); i >= 0; i-- )
		{
			CBlobParticle *particle = tentacle.m_blobGroup->m_members[ i ];
			DeactivateParticle( particle );
			DestroyParticle( particle );
		}
		tentacle.m_blobGroup = NULL;

		// Update neighbour lists
		// FIXME: should use a 'dirty' mechanism
		FindNeighbors();
	}
}


void CNPC_Blob::TentacleDestroy( CBlobTentacle *tentacle )
{
	TentacleDestroyParticles( *tentacle );
	tentacle->m_nPhase = BLOB_TENTACLE_STATE_NONE;
}


Vector CNPC_Blob::TentacleComputeTargetPosition( CBlobTentacle &tentacle, float predictionTime )
{														   
	Vector result = tentacle.m_vecTarget + tentacle.m_pRoot->m_position;

	CBaseEntity *pTargetEnt = tentacle.m_hTargetEnt;
	if ( pTargetEnt )
	{
		Vector predictedTargetPos;
		UTIL_PredictedPosition( pTargetEnt, predictionTime, &predictedTargetPos );

		result = predictedTargetPos + tentacle.m_vecTargetOffset + Vector( 0, 0, m_flRadius );

		float distToTarget = ( result - tentacle.m_pRoot->m_position ).Length();
		if ( distToTarget > tentacle.m_fLength )
		{
			float flFrac = tentacle.m_fLength / distToTarget;
			result = tentacle.m_pRoot->m_position + flFrac*(result - tentacle.m_pRoot->m_position );
		}
	}

	return result;
}

float CNPC_Blob::TentacleGetPathDistanceToTarget( EHANDLE hTarget, Vector targetOffset )
{
	float distanceToTarget = 0;

	Assert( !!hTarget );
	if ( !!hTarget )
	{
		if ( ( hTarget == m_hStalkTarget ) && GetNavigator() )
		{
			// We're stalking this target, get distance along the path (plus the distance to the target from the end of the path)
			distanceToTarget  = GetNavigator()->GetPathDistanceToGoal();
			distanceToTarget += GetNavigator()->GetGoalPos().DistTo( hTarget->GetAbsOrigin() + targetOffset );
		}
		else
		{
			// We're not stalking this target (secondary attack en-route, a door or barrel perchance), get direct distance
			distanceToTarget = GetAbsOrigin().DistTo( hTarget->GetAbsOrigin() + targetOffset );
			Assert(0); // Haven't enabled this behaviour yet!
		}
	}

	return distanceToTarget;
}


void CNPC_Blob::TentacleProcessAnimation_Authored(	CBlobTentacle &tentacle, int maxParticles,
													Vector *positions, Vector *velocities, float *radii )
{

	// FIXME: 
	//
	//   This is incomplete. TODO:
	//    - load the model and control its animations (use "CalcPoseSingle()" to update the animation smoothly, at the rate we want)
	//    - orient and scale the animation w.r.t the base+target (need anim's tentacle hit location, from the bbox or an attachment)
	//    - hook into anim events to get the moment the tentacle hits (when to apply melee damage)
	//
	// FIXME: 

	//float  distToTarget	= tentacle.m_vecTarget.Length();
	//float  length		= tentacle.m_fLength*clamp( ( distToTarget / tentacle.m_fLength ), 0.5f, 1.0f );
	Vector base         = tentacle.m_pRoot->m_position;
	Vector tentacleZDir	= tentacle.m_vecDirection;
	float  minRadius	= 0.5f;
	float  maxRadius	= 1.0f;
	int    nParticle	= 0;

	float phaseCompletion = ( gpGlobals->curtime - tentacle.m_fPhaseStart ) / tentacle.m_fPhaseDuration;
	// FIXME: because RunTask only gets run intermittently, we go slightly beyond 1.0f here... could cause slight visual pops
	//        what we want is to run tentacle animation entirely from here, not from RunTask()
	phaseCompletion = clamp( phaseCompletion, 0.0f, 1.0f );

	CBaseAnimating *pBaseAnimating = dynamic_cast<CBaseAnimating *>( tentacle.m_hAnimationEnt.Get() );
	if ( pBaseAnimating )
	{
		CStudioHdr *pStudioHdr = pBaseAnimating->GetModelPtr();

		if ( pStudioHdr && ( pStudioHdr->numbones() > 2 ) )
		{
			// Validate the topology:
			bool bValidTopology = true;
			for ( int iBone = 0; iBone < pStudioHdr->numbones(); ++iBone )
			{
				// Each bone should parent the next (NOTE: the root's parent should be -1)
				bValidTopology = bValidTopology && ( pStudioHdr->pBone( iBone )->parent == ( iBone - 1 ) );
			}

			if ( bValidTopology )
			{
				const int BLOB_TENTACLE_MAX_JOINTS = 64;
				ShapeEdge_t edges[ BLOB_TENTACLE_MAX_JOINTS ];
				CUtlVector< ShapeEdge_t > edgeList( &edges[ 0 ], BLOB_TENTACLE_MAX_JOINTS, 0 );
				bool bConvertToLocalSpace = true;
				GetShapeTargetEdgeListFromBaseAnimating( pBaseAnimating, edgeList, bConvertToLocalSpace );

				float scale = sv_blob_tentacle_length.GetFloat() / 125.0f; // FIXME: temp hack
				float tentacleLength = 0;
				for ( int i = ( edgeList.Count() - 1 ); i >= 0; i-- )
				{
					// Transform the animation to fit the current context
					ShapeEdge_t &edge = edgeList[ i ];
					edge.startPos	*= scale;
					edge.endPos		*= scale;
					edge.startVel	*= scale;
					edge.endVel		*= scale;
					tentacleLength	+= ( edgeList[ i ].endPos - edgeList[ i ].startPos ).Length();
				}

				// Iterate in reverse order, from the tip to the base
				float distanceAlongTentacle = 0, distanceAlongBone = 0;
				float baseStepSize = 2.3f*m_flRadius;
				for ( int i = ( edgeList.Count() - 1 ); i >= 0; i-- )
				{
					ShapeEdge_t &edge = edgeList[ i ];
					float  boneLength = ( edge.endPos - edge.startPos ).Length();

					while( distanceAlongBone < boneLength )
					{
						float fractionAlongBone		= distanceAlongBone / boneLength;
						float fractionAlongTentacle	= distanceAlongTentacle / tentacleLength;
						float radius				= Lerp( fractionAlongTentacle, minRadius, maxRadius );

						Assert( nParticle < maxParticles ); // Is BLOB_NUM_TENTACLE_PARTICLES too small or sv_blob_tentacle_length too large?
						if ( nParticle < maxParticles )
						{
							positions[	nParticle ] = VectorLerp( edge.startPos, edge.endPos, fractionAlongBone ) + base;
							velocities[	nParticle ] = VectorLerp( edge.startVel, edge.endVel, fractionAlongBone );
							radii[		nParticle ] = radius;
							nParticle++;
						}

						distanceAlongTentacle	+= radius*baseStepSize;
						distanceAlongBone		+= radius*baseStepSize;
					}

					distanceAlongBone -= boneLength;
				}
			}
			else
			{
				static int warnCount = 0;
				if ( warnCount < 5 )
				{
					Warning( "ERROR: blob tentacle animation model has incorrect joint topology..." );
					warnCount++;
				}
				Assert( bValidTopology );
			}
		}
	}

	while ( nParticle < maxParticles )
	{
		positions[	nParticle ] = base;
		velocities[	nParticle ] = vec3_origin;
		radii[		nParticle ] = 0.0f;
		nParticle++;
	}
}

#define BLOB_AIM_EXTRUDE_FACTOR 1.4f
#define BLOB_AIM_RETRACT_FACTOR 0.6f
#define BLOB_AIM_TEST_DISTANCE 72.0f
void CNPC_Blob::UpdateTentacleSpitPos( CBlobTentacle &tentacle )
{
	CBaseEntity *pEnemy = GetEnemy();
	if ( !pEnemy )
		return;

	Vector vecVel;
	Vector vecSpitPos = tentacle.m_vecLastValidSpitPos;
	const Vector &vecTargetPos = pEnemy->BodyTarget( vecSpitPos, false );
	if ( GetSpitVector( vecSpitPos, vecTargetPos, &vecVel ) )
	{
		tentacle.m_vecLastValidSpitPos = vecSpitPos;
		return;
	}

	// Try some areas near it. First try to extend or retract the tentacle
	float flLengthSqr = tentacle.m_vecTarget.LengthSqr();
	float flMaxLengthSqr = tentacle.m_fLength * tentacle.m_fLength;
	Vector vecTest;
	if ( flLengthSqr <= flMaxLengthSqr )
	{
		if ( flLengthSqr * BLOB_AIM_EXTRUDE_FACTOR * BLOB_AIM_EXTRUDE_FACTOR > flMaxLengthSqr )
		{
			vecTest = tentacle.m_vecTarget * sqrt( flMaxLengthSqr ) / sqrt( flLengthSqr );
		}
		else
		{
			vecTest = tentacle.m_vecTarget * BLOB_AIM_EXTRUDE_FACTOR;
		}
		vecSpitPos = vecTest + tentacle.m_pRoot->m_position;
		if ( GetSpitVector( vecSpitPos, vecTargetPos, &vecVel ) )
		{
			tentacle.m_vecLastValidSpitPos = vecSpitPos;
			return;
		}
	}

	float flMinLengthSqr = 0.1f * 0.1f * tentacle.m_fLength * tentacle.m_fLength;
	if ( flLengthSqr >= flMinLengthSqr )
	{
		if ( flLengthSqr * BLOB_AIM_RETRACT_FACTOR * BLOB_AIM_RETRACT_FACTOR < flMinLengthSqr )
		{
			vecTest = tentacle.m_vecTarget * sqrt( flMinLengthSqr ) / sqrt( flLengthSqr );
		}
		else
		{
			vecTest = tentacle.m_vecTarget * BLOB_AIM_RETRACT_FACTOR;
		}
		vecSpitPos = vecTest + tentacle.m_pRoot->m_position;
		if ( GetSpitVector( vecSpitPos, vecTargetPos, &vecVel ) )
		{
			tentacle.m_vecLastValidSpitPos = vecSpitPos;
			return;
		}
	}
}

static Vector s_pStandardTestDirs[] = 
{
	Vector( 1, 0, 0 ),
	Vector( 0, 1, 0 ),
	Vector( 0, 0, 1 ),
	Vector( 0, -1, 0 ),
	Vector( 0, 0, -1 ),
};

void CNPC_Blob::UpdateTentacleSpitPos_Hinted( CBlobTentacle &tentacle )
{
	CBaseEntity *pEnemy = GetEnemy();
	if ( !pEnemy )
		return;

	CInfoBlobSpitTarget *pTarget = dynamic_cast<CInfoBlobSpitTarget*>( tentacle.m_hTargetEnt.Get() );
	if ( !pTarget )
		return;

	// Is the previous position still ok?
	Vector vecVel;
	Vector vecSpitPos = tentacle.m_vecTarget;
	const Vector &vecTargetPos = pEnemy->BodyTarget( vecSpitPos, false );
	if ( GetSpitVector( vecSpitPos, vecTargetPos, &vecVel ) )
	{
		tentacle.m_vecLastValidSpitPos = vecSpitPos;
		return;
	}

	// How much length from the target point do we have?
	float flLengthRemaining = pTarget->ComputeApproachPathLength( tentacle.m_pRoot->m_position );
	flLengthRemaining = sv_blob_tentacle_length.GetFloat() - flLengthRemaining;
	flLengthRemaining = MIN( flLengthRemaining, pTarget->GetSearchRange() );
	if ( flLengthRemaining <= m_flRadius * 2.0f )
		return;

	const Vector &vecRootPos = pTarget->GetAbsOrigin();
	Vector vecCurDir;
	VectorSubtract( tentacle.m_vecTarget, vecRootPos, vecCurDir );

	// Try some areas near it. First try to extend or retract the tentacle
	float flExtrudeAmount = m_flRadius * 6.0f;
	float flLength = vecCurDir.Length();
	Vector vecTest;
	if ( ( flLength != 0.0f ) && ( flLength <= flLengthRemaining ) )
	{
		if ( flLength + flExtrudeAmount > flLengthRemaining )
		{
			vecTest = vecCurDir * flLengthRemaining / flLength;
		}
		else
		{								   
			vecTest = vecCurDir * ( flLength + flExtrudeAmount ) / flLength;
		}
		vecSpitPos = vecTest + vecRootPos;
		if ( GetSpitVector( vecSpitPos, vecTargetPos, &vecVel ) )
		{
			tentacle.m_vecLastValidSpitPos = vecSpitPos;
			return;
		}
	}

	if ( flLength > 0.0f )
	{
		if ( flLength < flExtrudeAmount )
		{
			vecTest = vecRootPos;
		}
		else
		{
			vecTest = vecCurDir * ( flLength - flExtrudeAmount ) / flLength;
		}
		vecSpitPos = vecTest + vecRootPos;
		if ( GetSpitVector( vecSpitPos, vecTargetPos, &vecVel ) )
		{
			tentacle.m_vecLastValidSpitPos = vecSpitPos;
			return;
		}
	}

	Vector vecTestPos;
	int nTestDirCount = ARRAYSIZE( s_pStandardTestDirs );
	int nTestDir = gpGlobals->framecount % nTestDirCount;
	VectorRotate( s_pStandardTestDirs[nTestDir], pTarget->EntityToWorldTransform(), vecTestPos );
	VectorMA( vecRootPos, flLengthRemaining, vecTestPos, vecTestPos );
	if ( GetSpitVector( vecTestPos, vecTargetPos, &vecVel ) )
	{
		tentacle.m_vecLastValidSpitPos = vecTestPos;
		return;
	}

	Vector vecForward;
	pTarget->GetSearchDirection( &vecForward );
	for ( int i = 0; i < 4; ++i )
	{
		RandomVectorInUnitSphere( &vecTestPos );
		if ( DotProduct( vecTestPos, vecForward ) < 0.0f )
		{
			vecTestPos *= -1.0f;
		}

		VectorMA( vecRootPos, flLengthRemaining, vecTestPos, vecTestPos );
		if ( GetSpitVector( vecTestPos, vecTargetPos, &vecVel ) )
		{
			tentacle.m_vecLastValidSpitPos = vecTestPos;
			return;
		}
	}
}

int CNPC_Blob::ComputeHintedPathNodeCount( CBlobTentacle &tentacle ) const
{
	CInfoBlobSpitTarget *pTarget = dynamic_cast<CInfoBlobSpitTarget*>( tentacle.m_hTargetEnt.Get() );
	return pTarget ? pTarget->GetApproachPathNodeCount() + 1 : 1;
}

int CNPC_Blob::ComputeHintedPath( HintedPath_t *pHintedPath, float *pTotalLength, CBlobTentacle &tentacle, float flFraction ) const
{
	CInfoBlobSpitTarget *pTarget = dynamic_cast<CInfoBlobSpitTarget*>( tentacle.m_hTargetEnt.Get() );
	if ( !pTarget || ( flFraction == 0.0f ) )
	{
		*pTotalLength = 0.0f;
		return 0;
	}

	Vector vecOffset, vecPt;
	int nCount = 0;
	pHintedPath[0].m_vecStartPos = tentacle.m_pRoot->m_position;
	float flTotalLength = 0.0f;
	for( CInfoBlobSpitPath *pPath = pTarget->GetApproachPath(); pPath; pPath = pPath->GetNextPath() )
	{
		vecPt = pPath->GetAbsOrigin();
		vecOffset.x = 4.0f * sin( 2.0f * M_PI * gpGlobals->curtime / 1.0f ); 
		vecOffset.y = 4.0f * sin( 2.0f * M_PI * ( gpGlobals->curtime + 0.33f ) / 1.2f ); 
		vecOffset.z = 4.0f * sin( 2.0f * M_PI * ( gpGlobals->curtime + 0.66f ) / 1.4f ); 
		vecPt += vecOffset;
		pHintedPath[nCount].m_vecEndPos = vecPt;
		float flLength = pHintedPath[nCount].m_vecStartPos.DistTo( vecPt );
		if ( flLength == 0.0f )
			continue;
		pHintedPath[nCount].m_flLength = flLength;
		pHintedPath[++nCount].m_vecStartPos = vecPt;
		flTotalLength += flLength;
	}
	pHintedPath[nCount].m_vecEndPos = tentacle.m_vecTarget;
	float flLength = pHintedPath[nCount].m_vecStartPos.DistTo( tentacle.m_vecTarget );
	pHintedPath[nCount].m_flLength = flLength;
	flTotalLength += flLength;
	++nCount;
	flTotalLength *= flFraction;
	*pTotalLength = flTotalLength;
	if ( flFraction == 1.0f )
		return nCount;

	// Lop off the end, based on fractional distance
	float flCurLength = 0.0f;
	float flNewLength;
	for ( int i = 0; i < nCount; ++i, flCurLength = flNewLength )
	{
		flNewLength = flCurLength + pHintedPath[i].m_flLength;
		if ( flNewLength <= flTotalLength )
			continue;

		float t = ( flTotalLength - flCurLength ) / pHintedPath[i].m_flLength;
		Vector vecDelta = pHintedPath[i].m_vecEndPos - pHintedPath[i].m_vecStartPos;
		VectorMA( pHintedPath[i].m_vecStartPos, t, vecDelta, pHintedPath[i].m_vecEndPos );
		pHintedPath[i].m_flLength *= t;
		return i+1;
	}

	return nCount;
}


void CNPC_Blob::TentacleProcessAnimation_Hinted( CBlobTentacle &tentacle, int nMaxParticles,
													Vector *positions, Vector *velocities, float *radii )
{
	float flPhaseCompletion = ( gpGlobals->curtime - tentacle.m_fPhaseStart ) / tentacle.m_fPhaseDuration;

	float flLengthFraction = 1.0f;
	switch( tentacle.m_nPhase )
	{
	default:
		flLengthFraction = 0.0f;
		break;

	case BLOB_TENTACLE_STATE_SPIT_EXTRUDE:
		tentacle.m_vecTarget = tentacle.m_vecLastValidSpitPos;
		flPhaseCompletion = 2.0f * flPhaseCompletion - powf( flPhaseCompletion, 3.0f );
		flLengthFraction = flPhaseCompletion;
		break;

	case BLOB_TENTACLE_STATE_SPIT_HOLD:
		UpdateTentacleSpitPos_Hinted( tentacle );
		VectorLerp( tentacle.m_vecTarget, tentacle.m_vecLastValidSpitPos, 0.05F, tentacle.m_vecTarget );
		break;

	case BLOB_TENTACLE_STATE_RECEDE:
		flLengthFraction = 1.0f - flPhaseCompletion;
		break;
	}

	float flLength;
	int nPathCount = ComputeHintedPathNodeCount( tentacle ); 
	HintedPath_t *pPath = (HintedPath_t*)stackalloc( nPathCount * sizeof(HintedPath_t) );
	int nCount = ComputeHintedPath( pPath, &flLength, tentacle, flLengthFraction );
	tentacle.m_fLastLength = flLength;
	
	if ( nCount > 0 )
	{
		tentacle.m_vecEyePosition = pPath[nCount-1].m_vecEndPos;
	}
	else
	{
		tentacle.m_vecEyePosition = tentacle.m_pRoot->m_position;
	}

	int nParticle = 0;
	float flLengthUntilParticle = 0.0f;
	float flLengthBuilt = 0.0f;
	float flLengthBetweenParticles = 2.3f * m_flRadius;
	float flMaxRadius = m_flRadius;
	float flMinRadius = m_flRadius * 0.6f;

	// Work backward along the tentacle..results in smoother movement
	Vector particlePos;
	while ( --nCount >= 0 )
	{
		Assert( nParticle < nMaxParticles );
		if ( nParticle >= nMaxParticles )
			break; // BLOB_NUM_TENTACLE_PARTICLES is too small or sv_blob_tentacle_length too large

		const HintedPath_t &path = pPath[nCount];
		float flLengthInSegment = path.m_flLength;
		float flStripAmount = MIN( flLengthUntilParticle, flLengthInSegment ); 
		flLengthInSegment -= flStripAmount;
		flLengthUntilParticle -= flStripAmount;
		while( flLengthInSegment >= 0.0f )
		{
			float tentacleParam = flLengthBuilt / flLength;
			float radius = Lerp( tentacleParam, flMinRadius, flMaxRadius );

			float t = flLengthInSegment / path.m_flLength; 
			VectorLerp( path.m_vecStartPos, path.m_vecEndPos, t, particlePos );

			positions[	nParticle ] = particlePos;
			velocities[	nParticle ] = vec3_origin; // FIXME: sample the position function twice to get this
			radii[		nParticle ] = radius;

			nParticle++;
			Assert( nParticle < nMaxParticles );
			if ( nParticle >= nMaxParticles )
				break; // BLOB_NUM_TENTACLE_PARTICLES is too small or sv_blob_tentacle_length too large

			flLengthInSegment -= flLengthBetweenParticles;
			flLengthBuilt += flLengthBetweenParticles;
		}
		flLengthUntilParticle = -flLengthInSegment;
	}

	while ( nParticle < nMaxParticles )
	{
		positions[	nParticle ] = tentacle.m_pRoot->m_position;
		velocities[	nParticle ] = vec3_origin;
		radii[		nParticle ] = 0.0f;
		nParticle++;
	}
}

						  
void CNPC_Blob::TentacleProcessAnimation_Procedural( CBlobTentacle &tentacle, int maxParticles,
													 Vector *positions, Vector *velocities, float *radii )
{
	if ( tentacle.m_nAction == BLOB_TENTACLE_ACTION_SPIT )
	{
		CInfoBlobSpitTarget *pTarget = dynamic_cast<CInfoBlobSpitTarget*>( tentacle.m_hTargetEnt.Get() );
		if ( pTarget && pTarget->GetApproachPathNodeCount() > 1 )
		{
			TentacleProcessAnimation_Hinted( tentacle, maxParticles, positions, velocities, radii );
			return;
		}
	}
								    
	static const float fPortionOfLungeToUpdateTarget = 0.55f;

	float distToTarget	= tentacle.m_vecTarget.Length();
	float length		= tentacle.m_fLength*clamp( ( distToTarget / tentacle.m_fLength ), 0.75f, 1.0f );
	Vector base         = tentacle.m_pRoot->m_position;
	Vector tentacleZDir	= tentacle.m_vecDirection;
	float curveDepth	= 0.1f;
	float tipOffset     = 0.0f;
	float rotation		= 0.0f;
	float minRadius		= 0.5f;
	float maxRadius		= 1.0f;

	float phaseCompletion = ( gpGlobals->curtime - tentacle.m_fPhaseStart ) / tentacle.m_fPhaseDuration;
	// FIXME: because RunTask only gets run intermittently, we go slightly beyond 1.0f here... could cause slight visual pops
	//        what we want is to run tentacle animation entirely from here, not from RunTask()
	phaseCompletion = clamp( phaseCompletion, 0.0f, 1.0f );

	float timeLeftInPhase = ( 1 - phaseCompletion )*tentacle.m_fPhaseDuration;

	switch( tentacle.m_nPhase )
	{
		case BLOB_TENTACLE_STATE_EXTRUDE:
		{
			if ( tentacle.m_nAction == BLOB_TENTACLE_ACTION_SWIPE )
			{
				Vector vertical( 0, 0, 1 );
				Vector toTarget = ( tentacle.m_hTargetEnt->BodyTarget( WorldSpaceCenter() ) - base ).Normalized();
				Vector toLeft   = toTarget.Cross( vertical ).Normalized();
				vertical = CrossProduct( toLeft, toTarget );
				VectorNormalize( vertical );
				tentacleZDir = VectorLerp( tentacle.m_bSwipeLeftToRight ? toLeft : -toLeft, vertical, BLOB_SWIPE_VERTICAL_BIAS );
				tentacle.m_vecDirection = tentacleZDir;
			}

			// The tentacle extrudes creepily from the blob's main body

			// Update base location to follow the main body of blob particles (makes the tentacle seem more connect to the blob)
			base = tentacle.m_pRoot->m_position;

			// Update target location until the end of the wait phase, then it's frozen in place
			float timeTillHit = sv_blob_tentacle_idle_time.GetFloat() + fPortionOfLungeToUpdateTarget*sv_blob_tentacle_lunge_time.GetFloat();
			timeTillHit = timeLeftInPhase + ( timeTillHit / tentacle.m_fSpeedMultiplier );
			tentacle.m_vecTarget = TentacleComputeTargetPosition( tentacle, timeTillHit ) - base;

			// Ease in+out:
			phaseCompletion = 0.5f*( 1 - cosf( M_PI*phaseCompletion ) );

			length    *= 0.5f*phaseCompletion;
			curveDepth = Lerp( phaseCompletion, 0.5f, 0.2f );
			rotation   = 4*M_PI*phaseCompletion;
			tipOffset  = -0.18f*phaseCompletion;
			// Thin out the tentacle as it stretches out
			//minRadius = Lerp( phaseCompletion, maxRadius, minRadius );

			break;
		}
		case BLOB_TENTACLE_STATE_SPIT_EXTRUDE:
		{
			// The tentacle extrudes creepily from the blob's main body

			// Update base location to follow the main body of blob particles (makes the tentacle seem more connect to the blob)
			base = tentacle.m_pRoot->m_position;
					    
			// Ease in+out:
			phaseCompletion = 2.0f * phaseCompletion - powf( phaseCompletion, 3.0f );
			tentacle.m_vecTarget = TentacleComputeTargetPosition( tentacle, 0.0f ) - base;
			tentacleZDir = tentacle.m_vecTarget;
			length = VectorNormalize( tentacleZDir );
			length    *= phaseCompletion;
			tentacle.m_fLastLength = length;
			curveDepth = Lerp( phaseCompletion, 0.5f, 0.0f );
			rotation   = 4*M_PI*phaseCompletion;
			tipOffset  = -0.18f*phaseCompletion;
			// Thin out the tentacle as it stretches out
			//minRadius = Lerp( phaseCompletion, maxRadius, minRadius );

			break;
		}

		case BLOB_TENTACLE_STATE_SPIT_HOLD:
		{
			// Update base location to follow the main body of blob particles (makes the tentacle seem more connect to the blob)
			base = tentacle.m_pRoot->m_position;
			UpdateTentacleSpitPos( tentacle );
								  
			Vector vecTarget;
			VectorSubtract( tentacle.m_vecLastValidSpitPos, base, vecTarget );
			VectorLerp( tentacle.m_vecTarget, vecTarget, 0.05f, tentacle.m_vecTarget );
			tentacleZDir = tentacle.m_vecTarget;
			length = VectorNormalize( tentacleZDir );
			tentacle.m_fLastLength = length;
			curveDepth = 0.0f; //Lerp( phaseCompletion, 0.2f, 0.0f );
			rotation   = Lerp( phaseCompletion, 0.0f, (float)( 0.5f * M_PI ) );
			tipOffset  = 0.0f;
			break;
		}
		break;

		case BLOB_TENTACLE_STATE_IDLE:
		{
			// The tentacle winds up for its attack

			// Update base location to follow the main body of blob particles (makes the tentacle seem more connect to the blob)
			base = tentacle.m_pRoot->m_position;

			// Update target location until the end of the wait phase, then it's frozen in place
			float timeTillHit = fPortionOfLungeToUpdateTarget*sv_blob_tentacle_lunge_time.GetFloat();
			timeTillHit = timeLeftInPhase + ( timeTillHit / tentacle.m_fSpeedMultiplier );
			tentacle.m_vecTarget = TentacleComputeTargetPosition( tentacle, timeTillHit ) - base;

			length    *= 0.5f;
			curveDepth = 0.2f;
			rotation   = 9*M_PI*( gpGlobals->curtime - tentacle.m_fPhaseStart );
			tipOffset  = -0.18f;

			// Slow down while vulnerable
			if ( m_fVulnerableTimeout > gpGlobals->curtime )
				tentacle.m_fPhaseStart += 0.5f*gpGlobals->frametime;

			// Keep idling until we're within in-range (and are not vulnerable)
			float distToTarget = TentacleGetPathDistanceToTarget( tentacle.m_hTargetEnt, tentacle.m_vecTargetOffset );
			tentacle.m_bPhaseIncomplete = ( m_fVulnerableTimeout > gpGlobals->curtime ) || ( distToTarget > tentacle.m_fLength );

			// Give up after a while, if doing a defensive attack:
			if ( m_bStalkDefensive && ( gpGlobals->curtime > ( tentacle.m_fPhaseStart + 3.0f ) ) )
				tentacle.m_bPhaseIncomplete = false;

			break;
		}
		case BLOB_TENTACLE_STATE_LUNGE:
		{
			// The tentacle slams down on the player (bending in the middle so the tip seems to lag)

			// Update base location to follow the main body of blob particles (makes the tentacle seem more connect to the blob)
			base = tentacle.m_pRoot->m_position;

			if ( phaseCompletion < fPortionOfLungeToUpdateTarget )
			{
				// Update target location until the lunge has visibly started, then it's frozen in place
				float timeTillHit = ( fPortionOfLungeToUpdateTarget - phaseCompletion )*sv_blob_tentacle_lunge_time.GetFloat();
				tentacle.m_vecTarget = TentacleComputeTargetPosition( tentacle, timeTillHit ) - base;
			}

			phaseCompletion = powf( phaseCompletion, 4.0f ); // Give it a bit o' zing!
			Vector tipStart = base + tentacleZDir*length;
			Vector tip = VectorLerp( tipStart, ( base + tentacle.m_vecTarget ), phaseCompletion );
			tentacleZDir = ( tip - base ).Normalized();

			if ( ( phaseCompletion > 0.5f ) && !tentacle.m_bHasSwooshed )
			{
				EmitSound( "NPC_FastZombie.AttackMiss" );
				tentacle.m_bHasSwooshed = true;
			}

			length *= Lerp( phaseCompletion, 0.5f, 1.0f );
			curveDepth = 0.5f*MIN( phaseCompletion, ( 1.0f - phaseCompletion ) );
			rotation = 0;

			break;
		}
		case BLOB_TENTACLE_STATE_SWIPE:
		{
			// Update base location to follow the main body of blob particles (makes the tentacle seem more connect to the blob)
			base = tentacle.m_pRoot->m_position;

			// Compute plane containing blob center + player
			Vector vertical( 0, 0, 1 );
			tentacle.m_vecTarget = tentacle.m_hTargetEnt->BodyTarget( WorldSpaceCenter() );
			Vector toTarget = ( tentacle.m_vecTarget - base ).Normalized();
			Vector toLeft   = toTarget.Cross( vertical ).Normalized();
			vertical = CrossProduct( toLeft, toTarget );
			VectorNormalize( vertical );

			phaseCompletion = powf( phaseCompletion, 4.0f ); // Give it a bit o' zing!
			float flAngle = RemapValClamped( phaseCompletion, 0.0f, 1.0f, 0.0f, 180.0f );
			if ( !tentacle.m_bSwipeLeftToRight )
			{
				flAngle *= -1.0f;
			}

			VMatrix rot;
			MatrixBuildRotationAboutAxis( rot, vertical, flAngle );
			Vector3DMultiply( rot, tentacle.m_vecDirection, tentacleZDir );

			Vector vecSwipeVel = CrossProduct( vertical, tentacleZDir );
			VectorNormalize( vecSwipeVel );
			if ( !tentacle.m_bSwipeLeftToRight )
			{
				vecSwipeVel *= -1.0f;
			}

			if ( ( phaseCompletion > 0.5f ) && !tentacle.m_bHasSwooshed )
			{
				EmitSound( "NPC_FastZombie.AttackMiss" );
				tentacle.m_bHasSwooshed = true;
			}

			length *= Lerp( phaseCompletion, 0.5f, 1.0f );
			float flFraction = BlobParticleWorldTrace( base, base + length * tentacleZDir );
			length *= flFraction;
			if ( length >= tentacle.m_fLastLength )
			{
				length = Lerp( 0.2f, tentacle.m_fLastLength, length );
			}
			tentacle.m_fLastLength = length;
			tentacle.m_vecLastDirection = tentacleZDir;
			curveDepth = 0.5f*MIN( phaseCompletion, ( 1.0f - phaseCompletion ) );
			rotation = 0;
			SwipeDamage( tentacle, base, base + length * tentacleZDir, vecSwipeVel );
			break;
		}
		case BLOB_TENTACLE_STATE_RECEDE:
		{
			// The tentacle gets pulled back into the blob

			// Update base location to follow the main body of blob particles (makes the tentacle seem more connect to the blob)
			base = tentacle.m_pRoot->m_position;

			switch( tentacle.m_nAction )
			{
			case BLOB_TENTACLE_ACTION_SWIPE:
				length = tentacle.m_fLastLength;
				break;

			case BLOB_TENTACLE_ACTION_SPIT:
				tentacleZDir = tentacle.m_vecTarget;
				length = tentacle.m_fLastLength;
				break;

			default:
				tentacleZDir = tentacle.m_vecTarget.Normalized();
				break;
			}

			// Wiggle wiggle!
			phaseCompletion = powf( phaseCompletion, 3.0f );
			length *= ( 1.0f - phaseCompletion );
			curveDepth = 0.25f*phaseCompletion*sinf( 8*M_PI*phaseCompletion );
			rotation = 0.5f*M_PI;
			break;
		}
		case BLOB_TENTACLE_STATE_HAUL:
		{
			// The blob pulls itself to where the tip of the tentacle landed

			// Make the blob move towards the tip of the tentacle and move the base of the tentacle with the rest
			// of the blob, figuring out phaseCompletion based on the distance travelled towards the target:
			// NOTE: since the blob's movement is physics-driven, we can't control the timing too well...
			float fTimeSinceStart = gpGlobals->curtime - tentacle.m_fPhaseStart;
			SetMoveGoal( VectorLerp( tentacle.m_pRoot->m_position, ( tentacle.m_pRoot->m_position + tentacle.m_vecTarget ), MIN( ( fTimeSinceStart / tentacle.m_fPhaseDuration ), 1.0f ) ) );

			base = GetAbsOrigin();
			phaseCompletion = ( base - tentacle.m_pRoot->m_position ).Length() / tentacle.m_vecTarget.Length();
			// End this phase once the blob reaches its goal (or if too much time passes - in case the blob gets stuck)
			tentacle.m_bPhaseIncomplete = ( phaseCompletion <= 0.98f ) && ( fTimeSinceStart < ( tentacle.m_fPhaseDuration + 5.0f ) );

			tentacleZDir = tentacle.m_vecTarget.Normalized();

			length *= ( 1.0f - phaseCompletion );
			rotation = M_PI;
			// Arc the tentacle up (like an inchworm), to emulate 'pulling'
			curveDepth = 0.2f*( 0.5f - 0.5f*cosf( 2*M_PI*phaseCompletion ) );
			// Thicken the tentacle as it bunches up
			minRadius = Lerp( phaseCompletion, minRadius, maxRadius );

			break;
		}
		default:
			Assert( 0 );
			return;
	}


	// UNDONE: do the animation via per-phase Callbacks( flTimeA, flTimeB, flDistanceFromTip ) which return a position
	//        (actually, probably want to pass "int nParticle", so we can adjust particle spacing if we want)
	//        sample twice to determine particle velocity


	Vector baseToTarget   = tentacle.m_vecTarget.Normalized();
	Vector lateralPerp    = baseToTarget.Cross( Vector( 0, 0, 1 ) );
	Vector tentacleYDir   = tentacleZDir.Cross( lateralPerp ).Normalized();
	Vector tentacleXDir   = tentacleYDir.Cross( tentacleZDir ).Normalized();
	Vector curveDir       = ( cosf( rotation )*tentacleYDir + sinf( rotation )*tentacleXDir ).Normalized();
	Vector maxCurveOffset = curveDepth*length*curveDir;
	tentacleZDir		  = ( tentacleZDir + tipOffset*curveDir ).Normalized();

	int nParticle = 0;
	Vector tentacleTipPos = base + length*tentacleZDir;
	tentacle.m_vecEyePosition = tentacleTipPos;
	Vector prevParticlePos = tentacleTipPos;
	float prevRadius = 0;
	float distFromTip = 0;
	float testDistFromTip = 0;
	float baseStepSize = 2.3f*m_flRadius;
	while ( distFromTip < length )
	{
		Assert( nParticle < maxParticles );
		if ( nParticle >= maxParticles )
			break; // BLOB_NUM_TENTACLE_PARTICLES is too small or sv_blob_tentacle_length too large

		float tentacleParam = testDistFromTip / length;
		float radius = Lerp( tentacleParam, minRadius, maxRadius );
		float curveParam = 1.0f - 4*( tentacleParam - 0.5f )*( tentacleParam - 0.5f );
		Vector curveOffset = curveParam*maxCurveOffset;

		// Ensure even spacing regardless of curvature (base parameter is "distance from tip, parallel to tentacleZDir")
		Vector particlePos = tentacleTipPos - testDistFromTip*tentacleZDir + curveOffset;
		Vector delta = ( particlePos - prevParticlePos ).Normalized()*0.5f*( prevRadius + radius )*baseStepSize;
		particlePos = prevParticlePos + delta;
		float stepSize = tentacleZDir.Dot( -delta );

		positions[	nParticle ] = particlePos;
		velocities[	nParticle ] = vec3_origin; // FIXME: sample the position function twice to get this
		radii[		nParticle ] = radius;

		distFromTip		+= stepSize;
		testDistFromTip  = clamp( ( distFromTip + baseStepSize ), 0, length );
		prevParticlePos	 = particlePos;
		prevRadius		 = radius;
		nParticle++;
	}

	while ( nParticle < maxParticles )
	{
		positions[	nParticle ] = base;
		velocities[	nParticle ] = vec3_origin;
		radii[		nParticle ] = 0.0f;
		nParticle++;
	}
}

void CNPC_Blob::TentacleProcessAnimation( CBlobTentacle &tentacle )
{
	if ( tentacle.m_nPhase == BLOB_TENTACLE_STATE_NONE )
		return;

	// If the tentacle loses its target, interrupt it
	if ( ( tentacle.m_nPhase != BLOB_TENTACLE_STATE_NONE ) &&
		 ( tentacle.m_nPhase != BLOB_TENTACLE_STATE_RECEDE ) &&
		 !tentacle.m_hTargetEnt )
	{
		TentacleInterrupt( tentacle );
	}

	// Create the group+particles on-demand (this makes save/restore work, since groups aren't saved)
	TentacleCreateParticles( tentacle );

	Vector positions[	BLOB_NUM_TENTACLE_PARTICLES ];
	Vector velocities[	BLOB_NUM_TENTACLE_PARTICLES ];
	float  radii[		BLOB_NUM_TENTACLE_PARTICLES ];
	if ( true ) // FIXME: tentacle.m_hAnimationEnt == NULL )
	{
		// Animating the tentacle via the power reticulated splines
		TentacleProcessAnimation_Procedural( tentacle, BLOB_NUM_TENTACLE_PARTICLES, positions, velocities, radii );
	}
	else
	{
		// Animating the tentacle via the power of Nick Maggiore
		TentacleProcessAnimation_Authored( tentacle, BLOB_NUM_TENTACLE_PARTICLES, positions, velocities, radii );
	}

	for ( int i = 0; i < BLOB_NUM_TENTACLE_PARTICLES; i++ )
	{
		CBlobParticle *pParticle = tentacle.m_blobGroup->m_members[ i ];
		int particleIndex = s_ExpandedParticleData[ pParticle->m_vphysParticle->GetGameIndex() ].iNetworkBypassIndex;
		BLOBPARTICLERADIUS( particleIndex )	= radii[ i ];
		pParticle->m_position				= positions[ i ];
		pParticle->m_velocity				= velocities[ i ];
		pParticle->m_force					= vec3_origin;
		pParticle->m_vphysParticle->SetPosition(  pParticle->m_position, vec3_angle, false );
		pParticle->m_vphysParticle->SetVelocity( &pParticle->m_velocity, NULL );
	}
}

void CNPC_Blob::ApplyTentacleDamage( CBlobTentacle &tentacle, CBaseEntity *pHurt, int iDamage, const Vector &vecDamagePos, const Vector &vecVelocityPunch )
{
	if ( !pHurt || ( pHurt == this ) )
		return;

	if ( !pHurt->ShouldCollide( GetCollisionGroup(), MASK_SOLID ) )
		return;

	if ( !g_pGameRules->ShouldCollide( GetCollisionGroup(), pHurt->GetCollisionGroup() ) )
		return;

	if ( ( pHurt->m_takedamage == DAMAGE_NO ) || !pHurt->IsAlive() )
		return;

	// Tentacle can hit stuff only once
	EHANDLE hHurt;
	hHurt.Set( pHurt );
	if ( tentacle.m_SwipeHitTargets.Find( hHurt ) != tentacle.m_SwipeHitTargets.InvalidIndex() )
		return;
	tentacle.m_SwipeHitTargets.AddToTail( hHurt );

	// Must hate the hit entity
	Ray_t ray;
	ray.Init( tentacle.m_pRoot->m_position, vecDamagePos );
	if ( IRelationType( pHurt ) == D_HT )
	{
		if ( iDamage > 0 )
		{
			CTakeDamageInfo info( this, this, iDamage, DMG_SLASH );
			CalculateMeleeDamageForce( &info, ray.m_Delta, ray.m_Start, 1.0f );
			pHurt->TakeDamage( info );
		}
	}

	EmitSound( "NPC_Hunter.TackleHit" );

	CBasePlayer *pPlayer = ToBasePlayer( pHurt );
	if ( pPlayer != NULL && !(pPlayer->GetFlags() & FL_GODMODE ) )
	{
		QAngle qaViewPunch = QAngle( 12.5, 15, -10 );
		pPlayer->ViewPunch( qaViewPunch );
		pPlayer->VelocityPunch( vecVelocityPunch );

		// Shake the screen
		UTIL_ScreenShake( pPlayer->GetAbsOrigin(), 100.0, 1.5, 1.0, 2, SHAKE_START );

		// Red damage indicator
		color32 red = { 128, 0, 0, 128 };
		UTIL_ScreenFade( pPlayer, red, 1.0f, 0.1f, FFADE_IN );
		return;
	}
	
	if ( pPlayer )
		return;

	bool bIsMovablePhysicsObject = pHurt && pHurt->GetMoveType() == MOVETYPE_VPHYSICS && pHurt->VPhysicsGetObject() && pHurt->VPhysicsGetObject()->IsMoveable();
	if ( bIsMovablePhysicsObject )
	{
		// If it's a vphysics object that's too heavy, crash into it too.
		IPhysicsObject *pPhysics = pHurt->VPhysicsGetObject();
		if ( pPhysics )
		{
			// If the object is being held by the player, break it or make them drop it.
			if ( pPhysics->GetGameFlags() & FVPHYSICS_PLAYER_HELD )
			{
				// If it's breakable, break it.
				if ( pHurt->m_takedamage == DAMAGE_YES )
				{
					CBreakableProp *pBreak = dynamic_cast<CBreakableProp*>(pHurt);
					if ( pBreak )
					{
						CTakeDamageInfo info( this, this, iDamage, DMG_SLASH );
						pBreak->Break( this, info );
					}
				}
			}
		}
	}

	if ( UTIL_ShouldShowBlood(pHurt->BloodColor()) )
	{
		// Hit an NPC. Bleed them!
		Vector vecBloodPos = vecDamagePos;

		trace_t tr;
		enginetrace->ClipRayToEntity( ray, MASK_ALL, pHurt, &tr );
		Vector vecDir = ray.m_Delta;
		VectorNormalize( vecDir );
		SpawnBlood( tr.endpos, vecDir, pHurt->BloodColor(), MIN( iDamage, 30 ) );
	}
}

void CNPC_Blob::SwipeDamage( CBlobTentacle &tentacle, const Vector &vecStart, const Vector &vecEnd, const Vector &vecTentacleVelocity )
{
	Vector vecVelocityPunch;
	VectorMultiply( vecTentacleVelocity, sv_blob_tentacle_hit_force.GetFloat(), vecVelocityPunch );
	vecVelocityPunch *= 2.0f;
	if ( vecVelocityPunch.z < 40 )
	{
		vecVelocityPunch.z = 40.0f;
	}
	int iDamage = sv_blob_tentacle_hit_damage.GetInt();

	// Get all entities that this tentacle is currently hitting
	CBaseEntity *list[128];
	float flBoxHalfWidth = 2.0f * m_flRadius; 
	Ray_t ray;
	ray.Init( vecStart, vecEnd,
		Vector(-flBoxHalfWidth, -flBoxHalfWidth, -flBoxHalfWidth),
		Vector(flBoxHalfWidth,flBoxHalfWidth,flBoxHalfWidth) );

	int nFoundCt = UTIL_EntitiesAlongRay( list, sizeof(list), ray, 0 );
	for( int i = 0; i < nFoundCt; i++ )
	{
		ApplyTentacleDamage( tentacle, list[i], iDamage, vecEnd, vecVelocityPunch );
	}
}


bool CNPC_Blob::TentacleMeleeAttack( CBlobTentacle &tentacle )
{
	// No more tentacles for a while after an attack
	m_fTentacleTimeout = gpGlobals->curtime + sv_blob_tentacle_create_timeout.GetFloat();


	Vector tentacleTip		= tentacle.m_pRoot->m_position + tentacle.m_vecTarget;
	Vector attackDir		= tentacle.m_vecTarget.Normalized();
	Vector attackStart		= tentacleTip - 0.7f*attackDir*sv_blob_tentacle_hit_tolerance.GetFloat();
	Vector attackEnd		= tentacleTip + 0.3f*attackDir*sv_blob_tentacle_hit_tolerance.GetFloat();
	int	iDamage				= sv_blob_tentacle_hit_damage.GetInt();

	// Figure out the direction of attack force
	Vector vertical( 0, 0, 1 );
	Vector toTarget = attackDir;
	Vector toLeft   = toTarget.Cross( vertical ).Normalized();
	Vector vecVelocityPunch;
	switch( tentacle.m_nAction )
	{
		case BLOB_TENTACLE_ACTION_ATTACK_LEFT:	vecVelocityPunch	= -toLeft;	break;	// Hit from left
		case BLOB_TENTACLE_ACTION_ATTACK_RIGHT:	vecVelocityPunch	=  toLeft;	break;	// Hit from right
		case BLOB_TENTACLE_ACTION_ATTACK_FRONT:	vecVelocityPunch	= toTarget;	break;	// Hit from front/above
		case BLOB_TENTACLE_ACTION_SWIPE:		vecVelocityPunch	= -toLeft;	break;	// Hit from left
		default: Assert( 0 ); return false;
	}
	vecVelocityPunch *= sv_blob_tentacle_hit_force.GetFloat();


	// Below cut'n'pasted from CNPC_Hunter::MeleeAttack:
	{
		/* FIXME: this makes the attack fail too easily (also there's no tentacle animation feedback)

		// Added test because claw attack anim sometimes used when for cases other than melee
		CBaseEntity *pEnemy = tentacle.m_hTargetEnt;
		if ( pEnemy )
		{
			trace_t	tr;
			AI_TraceHull( WorldSpaceCenter(), GetEnemy()->WorldSpaceCenter(), -Vector(8,8,8), Vector(8,8,8), MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );
			if ( tr.fraction < 1.0f )
				return;
		}*/

		//
		// Trace out a cubic section of our hull and see what we hit.
		//
		Vector vecMins = -m_flRadius*Vector(1,1,1);
		Vector vecMaxs = +m_flRadius*Vector(1,1,1);

		if ( gpGlobals->curtime < m_fTentacleDamageTimeout )
		{
			// Do a quarter damage if we only just scored damage (e.g. two tentacles hitting at once)
			iDamage = MAX( 1, ( iDamage / 4 ) );
		}
		else
		{
			m_fTentacleDamageTimeout = gpGlobals->curtime + sv_blob_tentacle_damage_timeout.GetFloat();
		}

		CBaseEntity *pHurt = CheckTraceHullAttack( attackStart, attackEnd, vecMins, vecMaxs, iDamage, DMG_SLASH );

		ApplyTentacleDamage( tentacle, pHurt, iDamage, tentacleTip, vecVelocityPunch );

		if ( ( pHurt == tentacle.m_hTargetEnt ) || ( tentacle.m_hTargetEnt == this ) )
		{
			// We hit our intended target
			return true;
		}
		else
		{
			// Attack-miss sound
			EmitSound( "GrenadeBugBait.Splat" );
			UTIL_ScreenShake( attackEnd, 400.0, 3.5, 1.0, 350.0, SHAKE_START );

			if ( gpGlobals->curtime > m_fVulnerableTimeout )
			{
				// Make the blob particles jump forwards towards the tentacle tip and enter a 'vulnerable' state
				Vector delta = attackEnd - attackStart;
				delta += delta.Length()*Vector(0,0,0.4f);
				m_vecParticleImpulse += delta*sv_blob_tentacle_miss_splash_force.GetFloat();
				m_vecParticleImpulseSpread = Vector(1,1,1)*0.3f*m_vecParticleImpulse.Length();
				m_fVulnerableTimeout = gpGlobals->curtime + sv_blob_tentacle_vulnerable_time.GetFloat();
			}

			return false;
		}
	}
}

void CNPC_Blob::TentacleUpdateState( CBlobTentacle &tentacle )
{
	// If the tentacle loses its target, interrupt it
	if ( ( tentacle.m_nPhase != BLOB_TENTACLE_STATE_NONE ) &&
		 ( tentacle.m_nPhase != BLOB_TENTACLE_STATE_RECEDE ) &&
		 !tentacle.m_hTargetEnt )
	{
		TentacleInterrupt( tentacle );
	}

	if ( ( tentacle.m_nPhase == BLOB_TENTACLE_STATE_NONE ) || 
		 tentacle.m_bPhaseIncomplete ||
		 ( gpGlobals->curtime < ( tentacle.m_fPhaseStart + tentacle.m_fPhaseDuration ) ) )
	{
		 return;
	}

	tentacle.m_fPhaseStart = gpGlobals->curtime;
	bool bHaul = false;
	switch( tentacle.m_nPhase )
	{
	case BLOB_TENTACLE_STATE_EXTRUDE:
		if ( tentacle.m_nAction == BLOB_TENTACLE_ACTION_SWIPE )
		{
			tentacle.m_nPhase			= BLOB_TENTACLE_STATE_SWIPE;
			tentacle.m_fPhaseDuration	= sv_blob_tentacle_lunge_time.GetFloat() / tentacle.m_fSpeedMultiplier;
		}
		else
		{
			tentacle.m_nPhase			= BLOB_TENTACLE_STATE_IDLE;
			tentacle.m_fPhaseDuration	= sv_blob_tentacle_idle_time.GetFloat() / tentacle.m_fSpeedMultiplier;
		}
		break;
	case BLOB_TENTACLE_STATE_SPIT_EXTRUDE:
		tentacle.m_nPhase			= BLOB_TENTACLE_STATE_SPIT_HOLD;
		tentacle.m_fPhaseDuration	= 4.0f / tentacle.m_fSpeedMultiplier;
		break;
	case BLOB_TENTACLE_STATE_SPIT_HOLD:
		tentacle.m_nPhase			= BLOB_TENTACLE_STATE_RECEDE;
		tentacle.m_fPhaseDuration	= sv_blob_tentacle_recede_time.GetFloat() / tentacle.m_fSpeedMultiplier;
		break;
	case BLOB_TENTACLE_STATE_IDLE:
		tentacle.m_nPhase			= BLOB_TENTACLE_STATE_LUNGE;
		tentacle.m_fPhaseDuration	= sv_blob_tentacle_lunge_time.GetFloat() / tentacle.m_fSpeedMultiplier;
		break;
	case BLOB_TENTACLE_STATE_LUNGE:
		bHaul						= !TentacleMeleeAttack( tentacle ) && sv_blob_tentacle_hit_then_haul.GetBool();
		tentacle.m_nPhase			=   bHaul ? BLOB_TENTACLE_STATE_HAUL				: BLOB_TENTACLE_STATE_RECEDE;
		tentacle.m_fPhaseDuration	= ( bHaul ? sv_blob_tentacle_haul_time.GetFloat()	: sv_blob_tentacle_recede_time.GetFloat() ) / tentacle.m_fSpeedMultiplier;
		break;
	case BLOB_TENTACLE_STATE_SWIPE:
		tentacle.m_nPhase			= BLOB_TENTACLE_STATE_RECEDE;
		tentacle.m_fPhaseDuration	= sv_blob_tentacle_recede_time.GetFloat() / tentacle.m_fSpeedMultiplier;
		tentacle.m_vecDirection		= tentacle.m_vecLastDirection;
		break;
	default:
		Assert(0);
	case BLOB_TENTACLE_STATE_RECEDE:
	case BLOB_TENTACLE_STATE_HAUL:
		TentacleDestroy( &tentacle );
		break;
	}
}

void CNPC_Blob::TentacleInterrupt( CBlobTentacle &tentacle )
{
	if ( ( tentacle.m_nPhase != BLOB_TENTACLE_STATE_NONE ) && ( tentacle.m_nPhase != BLOB_TENTACLE_STATE_RECEDE ) )
	{
		// FIXME: tentacles should store more intermediate state so that there's no animation pop entering the 'recede' state
		tentacle.m_nPhase = BLOB_TENTACLE_STATE_RECEDE;
		tentacle.m_fPhaseStart = gpGlobals->curtime;
		tentacle.m_fPhaseDuration = sv_blob_tentacle_recede_time.GetFloat() / tentacle.m_fSpeedMultiplier;
		tentacle.m_bPhaseIncomplete = false;
	}
}

void CNPC_Blob::TentaclesInterrupt( void )
{
	// Force all existing tentacles to recede
	for ( int i = 0; i < BLOB_MAX_TENTACLES; i++ )
	{
		TentacleInterrupt( m_tentacles[i] );
	}
}


void CNPC_Blob::StalkUpdateTentacles( void )
{
	int maxTentacles = m_bStalkDefensive ? 1 : sv_blob_max_tentacles.GetInt();
	int numActiveTentacles = 0;
	bool bActionUsage[4] = { false, false, false, false };
	for ( int i = 0; i < BLOB_MAX_TENTACLES; i++ )
	{
		if ( m_tentacles[i].m_nPhase != BLOB_TENTACLE_STATE_NONE )
		{
			bActionUsage[ m_tentacles[i].m_nAction ] = true;
			numActiveTentacles++;
		}
	}

	// Create a new tentacle if circumstances are favourable
	if (!!m_hStalkTarget &&
		  m_bCanTentacleAttack &&
		( gpGlobals->curtime > m_fTentacleTimeout ) &&
		( numActiveTentacles < maxTentacles ) &&
		( !m_bStalkDefensive || ( m_nStalkCreatedTentacles == 0 ) ) )
	{
		if ( TentacleGetPathDistanceToTarget( m_hStalkTarget ) < 1.5f*sv_blob_tentacle_length.GetFloat() )
		{
			// Create a tentacle with an unused action
			for ( int i = 0; i < 10; i++ )
			{
				BlobTentacleAction action = (BlobTentacleAction)( BLOB_TENTACLE_ACTION_ATTACK_FRONT + (int)RandomFloat( 0.0f, 2.99f ) );
				if ( bActionUsage[ action ] == false )
				{
					TentacleCreate( m_hStalkTarget, action );
					m_nStalkCreatedTentacles++;
					// Don't make another tentacle for N seconds
					m_fTentacleTimeout = gpGlobals->curtime + sv_blob_tentacle_create_timeout.GetFloat();
					break;
				}
			}
		}
	}

	if ( m_bStalkDefensive && m_nStalkCreatedTentacles )
	{
		int numActiveTentacles = 0;
		for ( int i = 0; i < BLOB_MAX_TENTACLES; i++ )
		{
			numActiveTentacles += ( m_tentacles[i].m_nPhase != BLOB_TENTACLE_STATE_NONE ) ? 1 : 0;
		}

		// Defensive stalking lasts for one attack only
		if ( numActiveTentacles == 0 )
			TaskComplete();
	}
}

bool CNPC_Blob::IsBigEnoughToCreateNewTentacle( void )
{
	// We should only create a new tentacle if we have more non-tentacle particles connected to the
	// brain than we (will) have tentacle particles (avoids the blob's volume growing implausbily):
	int numTentacleParticles = 0;
	for ( int i = 0; i < m_blobGroups.Count(); i++ )
	{
		if ( m_blobGroups[ i ]->m_bIsTentacle )
			numTentacleParticles += m_blobGroups[ i ]->m_members.Count();
	}

	int numRealParticles = GetNumParticlesConnectedToBrain( BLOB_PARTICLE_TENTACLE );
	static const float realToTentacleRatio = 0.5f;	

	return ( numRealParticles*realToTentacleRatio >= ( numTentacleParticles + BLOB_NUM_TENTACLE_PARTICLES ) );
}

// ------------------------------------------------------------------
// END TENTACLES CODE -------------------------------------------------------------
// ------------------------------------------------------------------


// ------------------------------------------------------------------
// START STALKING CODE ------------------------------------------------------------
// ------------------------------------------------------------------

bool CNPC_Blob::StalkGoalUpdate( void )
{
	// Update m_vecStalkGoal if needed (and if possible)

	// OPTIMIZATION: this path probably needs updating less often when the blob is far from the target (threshold on "pathError / pathLength")
	if ( gpGlobals->curtime < ( m_fLastStalkGoalUpdate + sv_blob_stalk_goal_update_frequency.GetFloat() ) )
		return true;

	if ( !!m_hStalkTarget )
	{
		// Move towards the target, along the existing path, if further than the tolerance distance from the target
		Vector prevWaypointPos = GetAbsOrigin();
		Vector nextWaypointPos = m_hStalkTarget->GetAbsOrigin();
		float  nextWaypointDistanceFromTarget = 0;

		// FIXME: Use of UpdateGoalPos() obviates this - keep this here until we know it's not needed
		/* static float validPathTolerance = 64;
		if ( !GetNavigator()->GetPath() ||
			 !GetNavigator()->IsGoalActive() ||
			( GetNavigator()->GetGoalType() == GOALTYPE_NONE ) ||
			( GetNavigator()->GetPath()->GetTarget() != m_hStalkTarget ) ||
			( ( GetNavigator()->GetGoalPos() - nextWaypointPos ).Length2D() > validPathTolerance ) )
		{
			// If the current path is not to the target entity, make it so
			Vector goalPos;
			bool bDrawDebugLines = true;
			if ( !ComputeTargetPosition( goalPos, m_hStalkTarget, vec3_origin, bDrawDebugLines ) )
			{
				Warning( "StalkGoalUpdate: Blob unable to create path to target\n" );
				Assert(0);
				return false;
			}

			AI_NavGoal_t goal( goalPos );
			goal.pTarget = m_hStalkTarget;
			if ( GetNavigator()->SetGoal( goal ) == false )
			{
				Warning( "StalkGoalUpdate: Blob unable to create path to target\n" );
				Assert(0);
				return false;
			}
		} */

		if ( !GetNavigator()->CurWaypointIsGoal() )
		{
			// Find the first waypoint which is nearer to the goal than the tolerance distance
			AI_Waypoint_t *pNextWaypoint = GetNavigator()->GetPath()->GetGoalWaypoint();
			Assert( pNextWaypoint );
			if ( pNextWaypoint )
			{
				// FIXME: since using SetGoalPos, the existing path now goes to m_vecStalkGoal, this probably needs re-thinking (though it seems to work ok thus far)
				//        the distance being measured is NOT to the target entity, but to the previous "target + tolerance + extrapolation" position
				//        It's not clear thatUpdateGoalPos() actually has any advantages (it too throws away all waypoints), so if this breaks then revert to SetGoal()

				// In case the existing path is not *quite* to the target entity, account for the extra distance
				// (ignore vertical separation, since waypoints get dropped to the floor and the player may jump over trenches with nodes in them)
				nextWaypointDistanceFromTarget += ( pNextWaypoint->GetPos() - nextWaypointPos ).Length2D();

				GetNavigator()->GetPath()->GetPathLength(); // Paranoia: make sure waypoint distances are initialized
				while( pNextWaypoint->GetPrev() &&
					   ( pNextWaypoint->GetPrev()->flPathDistGoal < sv_blob_stalk_distance.GetFloat() ) )
				{
					pNextWaypoint = pNextWaypoint->GetPrev();
					nextWaypointDistanceFromTarget = pNextWaypoint->flPathDistGoal;
				}

				nextWaypointPos = pNextWaypoint->GetPos();
				if ( pNextWaypoint->GetPrev() )
					prevWaypointPos = pNextWaypoint->GetPrev()->GetPos();
			}
		}

		// Move towards the next waypoint, if currently beyond the tolerance distance
		Vector toNextWaypoint	= nextWaypointPos - prevWaypointPos;
		float distToMove		= toNextWaypoint.NormalizeInPlace() + nextWaypointDistanceFromTarget;
		distToMove				= MAX( 0.0f, ( distToMove - sv_blob_stalk_distance.GetFloat() ) );

		Vector stalkGoal	 = prevWaypointPos + distToMove*toNextWaypoint;
		Vector targetOffset	 = stalkGoal - m_hStalkTarget->GetAbsOrigin();
		if ( ComputeTargetPosition( stalkGoal, m_hStalkTarget, targetOffset ) )
		{
			m_vecStalkGoal			= stalkGoal;
			m_fLastStalkGoalUpdate	= gpGlobals->curtime;

			if ( sv_blob_stalk_predict_time.GetFloat() > 0 )
			{
				// Now additionally take into account predicted target motion
				// NOTE: if sv_blob_stalk_predict_time is set high, stalking can be made to overshoot,
				//       so the blob will seem like it is anticipating the player to cut them off!
				Vector predictedTargetPos;
				UTIL_PredictedPosition( m_hStalkTarget, sv_blob_stalk_predict_time.GetFloat(), &predictedTargetPos );
				Vector extrapolation	= predictedTargetPos - m_hStalkTarget->GetAbsOrigin();
				extrapolation			= MIN( sv_blob_stalk_predict_max_dist.GetFloat(), extrapolation.Length() )*extrapolation.Normalized();

				// Ignore motion back along the blob's path (it should not retreat)
				// FIXME: try and avoid the player being able to force the blob to move diagonally backwards by curving around it...
				//        try updating blobPathDir only every second or so (get the blob to hold its course direction for a while, instead of curving to parallel motion)
				//        maybe try snapping to 45/90 degree angles? or only update blobPathDir when the player is not moving against it? (or gets past the blob)
				//        could time-smooth blobPathDir, updating it with a lower lerp amount when the player is moving faster
				Vector	blobPathDir	= toNextWaypoint;
				float distToCancel	 = -blobPathDir.Dot( extrapolation );
				// This makes the blob aggressive if you approach it (it pushes back in your direction), and avoids it overshooting into you when you back off
				// FIXME: tune these values better
				static const float toCancel = 1.1f, fromCancel = 0.6f;
				distToCancel		*= ( distToCancel > 0 ) ? toCancel : fromCancel;
				extrapolation		+= distToCancel*blobPathDir;

				// Don't let 'extrapolation' push us (further) within the tolerance distance of the target
				Vector testPos = stalkGoal + extrapolation;
				while ( testPos.DistTo( m_hStalkTarget->GetAbsOrigin() ) < ( sv_blob_stalk_distance.GetFloat() - 16 ) )
				{
					Vector dirToTestPos		 = ( testPos - prevWaypointPos ).Normalized();
					float  extrapolatedDist	 = clamp( extrapolation.Dot( dirToTestPos ), 0, 16 );
					if ( extrapolatedDist < 0.1 )
						break;
					extrapolation			-= extrapolatedDist*dirToTestPos;
					testPos					 = stalkGoal + extrapolation;
				}

				// Since the extrapolated position is likely to be through a wall, estimate how far we can really move
				extrapolation		*= BlobHullTrace( stalkGoal, stalkGoal + extrapolation, m_hStalkTarget );
				stalkGoal			+= extrapolation;

				targetOffset		 = stalkGoal - m_hStalkTarget->GetAbsOrigin();
				if ( ComputeTargetPosition( stalkGoal, m_hStalkTarget, targetOffset ) )
				{
					m_vecStalkGoal	 = stalkGoal;
				}
			}

			return true;
		}
	}

	return false;
}

void CNPC_Blob::StalkInitiateLunge( void )
{
	// Start a lunge if possible

	Assert( m_eBlobStalkState == BLOB_STALK_NEAR );
	if ( ( m_eBlobStalkState == BLOB_STALK_NEAR ) && m_hStalkTarget.Get() )
	{
		// FIXME: measure the player's walking speed, make sv_blob_lunge_trigger_velocity slightly more than that
		if ( m_hStalkTarget->GetSmoothedVelocity().Length() > sv_blob_lunge_trigger_velocity.GetFloat() )
		{
			// FIXME: set up a "lunge" physics state, modify "max_speed_towards_goal", "time_to_max_speed_towards_goal" and "towards_goal_brake_time"
			m_eBlobStalkState		= BLOB_STALK_LUNGING;
			m_fLastStalkLungeTime	= gpGlobals->curtime;
		}
	}
}

void CNPC_Blob::StalkUpdateState( void )
{
	// Update stalking state and m_vecStalkGoal

	if ( m_hStalkTarget == NULL )
	{
		// Not stalking, so reset persistent state
		m_eBlobStalkState		= BLOB_STALK_NONE;
		m_fLastStalkGoalUpdate	= 0;
		m_fLastStalkLungeTime	= 0;
		return;
	}

	float distanceToTarget = GetNavigator()->GetPathDistanceToGoal();
	if ( m_eBlobStalkState == BLOB_STALK_FAR )
	{
		if ( distanceToTarget < sv_blob_stalk_near_distance.GetFloat() )
		{
			m_eBlobStalkState = BLOB_STALK_NEAR;
			// Don't lunge immediately on getting close to the target (lunging should be a reaction to the target fleeing)
			m_fLastStalkLungeTime   = gpGlobals->curtime;
		}
	}

	if ( m_eBlobStalkState == BLOB_STALK_NEAR )
	{
		m_fLastNearerToTargetTime = gpGlobals->curtime; // Don't worry about getting closer if we're 'NEAR' already

		if ( distanceToTarget > sv_blob_stalk_near_distance.GetFloat() )
		{
			m_eBlobStalkState = BLOB_STALK_FAR;
		}
		else if ( gpGlobals->curtime > ( m_fLastStalkLungeTime + sv_blob_lunge_timeout.GetFloat() ) )
		{
			StalkInitiateLunge();
		}
	}

	if ( m_eBlobStalkState == BLOB_STALK_LUNGING )
	{
		if ( gpGlobals->curtime > ( m_fLastStalkLungeTime + sv_blob_lunge_duration.GetFloat() ) )
		{
			// Lunge complete
			m_eBlobStalkState = BLOB_STALK_NEAR;
		}
	}

	StalkGoalUpdate();
}

// ------------------------------------------------------------------
// END STALKING CODE --------------------------------------------------------------
// ------------------------------------------------------------------


int CNPC_Blob::GetBlobSize( void ) const
{
	int iNumParticles = m_blobParticles.Count();

	if ( iNumParticles <= BLOB_SIZE_TINY )
		return BLOB_SIZE_TINY;
	else if ( iNumParticles <= BLOB_SIZE_SMALL )
		return BLOB_SIZE_SMALL;
	else if ( iNumParticles <= BLOB_SIZE_MEDIUM )
		return BLOB_SIZE_MEDIUM;
	else
		return BLOB_SIZE_LARGE;
}

void CNPC_Blob::UpdatePipeParticles()
{
	if ( m_QueuedPipeParticles.Count() == 0 )
		return;

	//we have particles being added from a pipe entrance
	struct ParticleFlow_t
	{
		CFunc_Pipe_Entrance *pExit;
		int iCount;
		float fEarliestArrivalTime;
	};
	ParticleFlow_t *pFlows = (ParticleFlow_t *)stackalloc( sizeof(ParticleFlow_t) * m_QueuedPipeParticles.Count() );
	int iFlowCount = 0;

	for( int i = m_QueuedPipeParticles.Count(); --i >= 0; )
	{
		if( m_QueuedPipeParticles[i].fArrivalTime < gpGlobals->curtime )
		{
			int j;
			for( j = 0; j != iFlowCount; ++j )
			{
				if( pFlows[j].pExit == m_QueuedPipeParticles[i].hPipeExit )
				{
					++pFlows[j].iCount;
					if( m_QueuedPipeParticles[i].fArrivalTime < pFlows[j].fEarliestArrivalTime )
						pFlows[j].fEarliestArrivalTime = m_QueuedPipeParticles[i].fArrivalTime;
					break;
				}
			}

			if( j == iFlowCount )
			{
				Assert(m_QueuedPipeParticles[i].hPipeExit);
				pFlows[iFlowCount].pExit = m_QueuedPipeParticles[i].hPipeExit;
				pFlows[iFlowCount].iCount = 1;
				//in reality, we'd have to do arrival math on each individual particle, but screw that
				pFlows[iFlowCount].fEarliestArrivalTime = m_QueuedPipeParticles[i].fArrivalTime;
				++iFlowCount;
			}
		}
	}

	for( int i = 0; i != iFlowCount; ++i )
	{
		CFunc_Pipe_Entrance *pExit = pFlows[i].pExit;

		AssertMsg( pExit, "Why is this bad exit in our flow list!? THIS IS BAD!" );

		if ( pExit )
		{
			float fExitStart = MAX( pFlows[i].fEarliestArrivalTime, pExit->m_fFlowTimeStamp );
			int iTransport = floor((gpGlobals->curtime - fExitStart) * pExit->m_fFlowRate);

			if( iTransport > pFlows[i].iCount )
			{
				iTransport = pFlows[i].iCount;
			}

			if( iTransport <= 0 )
				continue;

			int iStart = m_nActiveParticles;
			AddParticles( iTransport );
			QAngle qAngles;
			for( int k = iStart; k != m_nActiveParticles; ++k )
			{
				Vector vPosition = pExit->GetAbsOrigin() + Vector( RandomFloat( -1.0f, 1.0f ), RandomFloat( -1.0f, 1.0f ), RandomFloat( 0.0f, 2.0f ) );
				m_vecPhysParticles[k]->SetPosition( vPosition, qAngles, true );
			}

			pExit->m_fFlowTimeStamp = fExitStart + (((float)iTransport) / pExit->m_fFlowRate);
			Assert( pExit->m_fFlowTimeStamp <= gpGlobals->curtime );

			//remove a sufficient number of particles from the queue
			for( int j = m_QueuedPipeParticles.Count(); --j >= 0; )
			{
				if( (m_QueuedPipeParticles[j].hPipeExit == pFlows[i].pExit) &&
					(m_QueuedPipeParticles[j].fArrivalTime < gpGlobals->curtime) )
				{
					m_QueuedPipeParticles.FastRemove( j );
					--iTransport;
					if( iTransport == 0 )
						break;
				}
			}
		}
	}
}

void CNPC_Blob::NPCThink( void )
{
	Vector vPos = GetAbsOrigin();

	BaseClass::NPCThink();

	Vector vecNPCMovement = GetAbsOrigin() - vPos;

	UpdatePipeParticles();


	if ( m_bFrozen )
	{
		if ( m_fUnfreezeTime <= gpGlobals->curtime )
		{
			Unfreeze();
		}

		if( GetMoveType() == MOVETYPE_VPHYSICS )
		{
			//unfreeze
			m_bFrozen = false;
			m_BlobUnfrozenOutput.FireOutput( this, this );

			for( int i = 0; i != m_nActiveParticles; ++i )
			{
				UnfreezeParticle( m_blobParticles[ i ] );
			}
		}
	}
	else
	{
		if( GetMoveType() == MOVETYPE_NONE )
		{
			//freeze
			m_bFrozen = true;
			m_BlobFrozenOutput.FireOutput( this, this );

			m_fUnfreezeTime = gpGlobals->curtime + 5.0f;

			//play a temp frozen sound
			EmitSound( "ChainLink.ImpactHard" );

			for( int i = 0; i != m_nActiveParticles; ++i )
			{
				FreezeParticle( m_blobParticles[ i ] );
			}
		}
	}

	if ( !m_pAmbientSound )
	{
		CreateSounds();
	}

	// Change physics state based one how it's moving
	// FIXME: this is NOT a robust test! It'll identify any kind of slope as a 'wall'
	if ( vecNPCMovement.Length() > 1.0f )
	{
		if ( vecNPCMovement.z != 0.0f )
		{
			SetPhysParamState( "wall_crawl" );
		}
		else
		{
			SetPhysParamState( "Default" );
		}
	}

	// Add attack ability based on size
	if ( GetBlobSize() < BLOB_SIZE_MEDIUM )
		CapabilitiesRemove( bits_CAP_INNATE_MELEE_ATTACK1 );
	else
		CapabilitiesAdd( bits_CAP_INNATE_MELEE_ATTACK1 );

	// Count the number of large blob groups to see if it's changed
	int iNumLargeGroups = 0;

	for ( int iGroup = 0; iGroup < m_blobGroups.Count(); ++iGroup )
	{
		if ( !m_blobGroups[ iGroup ]->m_bIsTentacle && ( m_blobGroups[ iGroup ]->m_members.Count() > 6 ) )
			++iNumLargeGroups;
	}

	if ( m_iCacheNumLargeGroups < iNumLargeGroups )
	{
		EmitSound( "Blob.Merge" );
		m_iCacheNumLargeGroups = iNumLargeGroups;
	}
	else if ( m_iCacheNumLargeGroups > iNumLargeGroups )
	{
		EmitSound( "Blob.Split" );
	    DispatchParticleEffect( "blob_split", GetAbsOrigin(), QAngle( 0.0f, 0.0f, 0.0f ) );
		m_iCacheNumLargeGroups = iNumLargeGroups;
	}

	// Extinguish nearby fires
	for ( int i = 0; i < m_blobGroups.Count(); ++i )
	{
		CBlobGroup *pBlobGroup = m_blobGroups[ i ];
		Assert( pBlobGroup->m_members.Count() );

		if ( pBlobGroup->m_bIsTentacle )
			continue;

		Vector mins, maxs;
		BuildAABBForBlobGroup( pBlobGroup, mins, maxs, m_flRadius );

		Vector vCenter = ( maxs + mins ) * 0.5f;
		float fRadius = mins.DistTo( maxs ) * 0.5f;

		// Get a rate between 0.5 (fast) and 0.75 (slow) where 0.25 is 100 or more particles
		float fRate = static_cast<float>( 100 - MIN( pBlobGroup->m_members.Count(), 100 ) ) / 200.0f + 0.25f;

		FireSystem_ExtinguishInRadius( vCenter, fRadius, fRate );
	}
}

float CNPC_Blob::MaxYawSpeed( void )
{
	return 180.0f;
}

bool CNPC_Blob::IsJumpLegal( const Vector &startPos, const Vector &apex, const Vector &endPos ) const
{
	const float MAX_JUMP_RISE		= 1000.0f;
	const float MAX_JUMP_DISTANCE	= 1000.0f;
	const float MAX_JUMP_DROP		= 1000.0f;

	return BaseClass::IsJumpLegal( startPos, apex, endPos, MAX_JUMP_RISE, MAX_JUMP_DROP, MAX_JUMP_DISTANCE );
}

void CNPC_Blob::HandleAnimEvent( animevent_t *pEvent )
{
	if ( pEvent->Event() == AE_BLOB_MELEE_HIT1)
	{
		MeleeAttack( BLOB_MELEE1_RANGE, sv_blob_tentacle_hit_damage.GetFloat(), QAngle( 20.0f, 0.0f, -12.0f ), Vector( -250.0f, 1.0f, 1.0f ) );
		return;
	}

	BaseClass::HandleAnimEvent( pEvent );
}

//-----------------------------------------------------------------------------
// Purpose: Allows for modification of the interrupt mask for the current schedule.
//			In the most cases the base implementation should be called first.
//-----------------------------------------------------------------------------
void CNPC_Blob::BuildScheduleTestBits( void )
{
	bool bBlobIsGoingSomewhere = false;

	bBlobIsGoingSomewhere = IsCurSchedule( SCHED_IDLE_WALK ) || 
		IsCurSchedule( SCHED_ALERT_WALK ) || 
		IsCurSchedule( SCHED_COMBAT_WALK ) || 
		IsCurSchedule( SCHED_CHASE_ENEMY ) || 
		IsCurSchedule( SCHED_TARGET_CHASE ) || 
		IsCurSchedule( SCHED_FORCED_GO ) || 
		IsCurSchedule( SCHED_FORCED_GO_RUN ) || 
		IsCurSchedule( SCHED_RUN_RANDOM ) || 
		IsCurSchedule( SCHED_INTERACTION_MOVE_TO_PARTNER ) || 
		IsCurSchedule( SCHED_BLOB_STALK_TARGET ) || 
		IsCurSchedule( SCHED_BLOB_SHAKE_OFF_BLOBS ) || 
		IsCurSchedule( SCHED_GET_HEALTHKIT ) || 
		IsCurSchedule( SCHED_BLOB_FLEE_DANGER_SOUND ) ||
		IsCurSchedule( SCHED_ESTABLISH_LINE_OF_FIRE ) ||
		IsCurSchedule( SCHED_TAKE_COVER_FROM_ENEMY ) ||
		IsCurSchedule( SCHED_BLOB_ESTABLISH_LINE_OF_FIRE ) ||
		IsCurSchedule( SCHED_BLOB_REFORM );

	bool bCanRangeAttack = ( ( m_BlobType == BLOB_TYPE_SPITTER ) || ( m_BlobType == BLOB_TYPE_GENERIC ) );
	if( bBlobIsGoingSomewhere )
	{
		SetCustomInterruptCondition( COND_BLOB_SEE_PIPE_SHORTCUT );
		if ( bCanRangeAttack )
		{
			SetCustomInterruptCondition( COND_CAN_MELEE_ATTACK1 );
		}
	}

	if ( m_BlobType == BLOB_TYPE_GENERIC )
	{
		if ( IsCurSchedule( SCHED_BLOB_SHAKE_OFF_BLOBS ) || IsCurSchedule( SCHED_BLOB_SHAKE_OFF_BLOBS_USE_HINTS ) )
		{
			SetCustomInterruptCondition( COND_BLOB_BRAIN_EXPOSED );
		}
	}

	if ( IsCurSchedule( SCHED_FAIL_TAKE_COVER ) )
	{
		SetCustomInterruptCondition( COND_HEAR_DANGER );
	}

	if ( IsCurSchedule( SCHED_TAKE_COVER_FROM_ENEMY ) || 
		 IsCurSchedule( SCHED_BLOB_ESTABLISH_LINE_OF_FIRE ) ||
		 IsCurSchedule( SCHED_FAIL_TAKE_COVER ) ||
		 IsCurSchedule( SCHED_IDLE_STAND ) )
	{
		if ( bCanRangeAttack )
		{
			SetCustomInterruptCondition( COND_CAN_MELEE_ATTACK1 );
			SetCustomInterruptCondition( COND_CAN_RANGE_ATTACK1 );
		}
		else if ( m_BlobType == BLOB_TYPE_LEAPER )
		{
			SetCustomInterruptCondition( COND_BLOB_CAN_LEAP );
		}
	}

	// Everything except transferring should be interrupted if we want to transfer through pipe
	if ( !IsCurSchedule( SCHED_BLOB_TRANSFER_THROUGH_PIPE ) )
	{
		SetCustomInterruptCondition( COND_BLOB_FORCE_TRANSFER_THROUGH_PIPE );
		SetCustomInterruptCondition( COND_BLOB_TRANSFERING_THROUGH_PIPE );
	}

	// Everything except splitting and pipe transfer should be interrupted if our health is low
	if ( !IsCurSchedule( SCHED_BLOB_TRANSFER_THROUGH_PIPE ) )
		SetCustomInterruptCondition( COND_BLOB_HEALTH_LOW );

	BaseClass::BuildScheduleTestBits();
}


//-----------------------------------------------------------------------------
// Purpose: Returns whether the enemy has been seen within the time period supplied
// Input  : flTime - Timespan we consider
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_Blob::SeenEnemyWithinTime( float flTime )
{
	float flLastSeenTime = GetEnemies()->LastTimeSeen( GetEnemy() );
	return ( flLastSeenTime != 0.0f && ( gpGlobals->curtime - flLastSeenTime ) < flTime );
}

						   
//-----------------------------------------------------------------------------
// Can we spit from a nearby tentacle spit position?
//-----------------------------------------------------------------------------
struct TentacleLOSInfo_t
{
	CBaseEntity *m_pEntity;
	Vector m_vecLOSCheck;
	float m_flDist;
};

static bool SortTentacleLOS( const TentacleLOSInfo_t &left, const TentacleLOSInfo_t &right )
{
	return left.m_flDist < right.m_flDist;
}

#pragma warning( disable : 4706 )
bool CNPC_Blob::CheckTentacleSpitLOS( const Vector &ownerPos, const Vector &targetPos )
{
	if ( IsMostlyBrain() )
		return false;

	if ( s_nSpitTargetCount == 0 )
		return false;

	float flTentacleLengthSqr = sv_blob_tentacle_length.GetFloat();
	flTentacleLengthSqr *= flTentacleLengthSqr;

	// Sort spit targets by closest to the player
	int nTargetCount = 0;
	TentacleLOSInfo_t *pTargets = (TentacleLOSInfo_t*)stackalloc( s_nSpitTargetCount * sizeof(TentacleLOSInfo_t) );
	CInfoBlobSpitTarget *pTentacleSpitPos = NULL;
	while( pTentacleSpitPos = assert_cast<CInfoBlobSpitTarget*>( gEntList.FindEntityByClassname( pTentacleSpitPos, "info_blob_spit_target" ) ) )
	{
		// The spit position must be within spit distance of the target
		// and within tentacle range of the blob
		const Vector& vecSpitPos = pTentacleSpitPos->GetAbsOrigin();
		if ( targetPos.DistToSqr( vecSpitPos ) > BLOB_SPIT_MAX_RANGE_SQR )
			continue;

		Vector vecLOSCheck;
		CInfoBlobSpitPath *pApproachPath = pTentacleSpitPos->GetApproachPath();
		if ( !pApproachPath )
		{
			if ( ownerPos.DistToSqr( vecSpitPos ) > flTentacleLengthSqr )
				continue;
			vecLOSCheck = ownerPos;
		}
		else
		{
			float flLength = pTentacleSpitPos->ComputeApproachPathLength( ownerPos );
			if ( flLength * flLength > flTentacleLengthSqr )
				continue;
			vecLOSCheck = pApproachPath->GetAbsOrigin();
		}

		Vector vecDelta, vecDelta2;
		VectorSubtract( targetPos, ownerPos, vecDelta );
		VectorSubtract( vecSpitPos, ownerPos, vecDelta2 );
		VectorNormalize( vecDelta );
		VectorNormalize( vecDelta2 );
		if ( DotProduct( vecDelta, vecDelta2 ) < -0.3f )
			continue;

		float flDistSq = targetPos.AsVector2D().DistToSqr( vecSpitPos.AsVector2D() );
		pTargets[nTargetCount].m_pEntity = pTentacleSpitPos;
		pTargets[nTargetCount].m_flDist = flDistSq;
		pTargets[nTargetCount].m_vecLOSCheck = vecLOSCheck;
		++nTargetCount;
	}

	if ( nTargetCount == 0 )
		return false;

	std::make_heap( pTargets, pTargets + nTargetCount, SortTentacleLOS ); 
	std::sort_heap( pTargets, pTargets + nTargetCount, SortTentacleLOS ); 

	// Sort list by distance to the player
	trace_t tr;
	CTraceFilterLOS traceFilter( this, HL2COLLISION_GROUP_BLOB );
	for ( int i = 0; i < nTargetCount; ++i )
	{
		AI_TraceLine( ownerPos, pTargets[i].m_vecLOSCheck, MASK_BLOCKLOS, &traceFilter, &tr );
		if ( tr.fraction != 1.0f || tr.startsolid )
			continue;

		const Vector& vecSpitPos = pTargets[i].m_pEntity->GetAbsOrigin();
		if ( GetSpitVector( vecSpitPos, targetPos, &m_vecSaveSpitVelocity ) )
		{
			m_bSaveSpitUseTentacle = true;
			m_hSaveSpitTentaclePos = pTargets[i].m_pEntity;
			m_vecSaveSpitTarget = targetPos;
			return true;
		}
	}
	return false;
}
#pragma warning( default : 4706 )


//=========================================================
// CanCheckAttacks - prequalifies a npc to do more fine
// checking of potential attacks.
//=========================================================
bool CNPC_Blob::FCanCheckAttacks( void )
{
	// Not allowed to check attacks while climbing or jumping
	// Otherwise schedule is interrupted while on ladder/etc
	// which is NOT a legal place to attack from
	if ( GetNavType() == NAV_CLIMB || GetNavType() == NAV_JUMP )
		return false;

	// We're allowed to check for attacks even if we can't see the enemy
	// owing to tentacle spit points
	return ( !HasCondition( COND_ENEMY_TOO_FAR ) );
}


//-----------------------------------------------------------------------------
// Purpose: Test whether this blob can hit the target
//-----------------------------------------------------------------------------
bool CNPC_Blob::InnateWeaponLOSCondition( const Vector &ownerPos, const Vector &targetPos, bool bSetConditions )
{
	if ( IsCurSchedule( SCHED_BLOB_SPIT ) )
		return true;
		  
	if ( GetNextAttack() > gpGlobals->curtime )
		return false;

	// If we can see the enemy, or we've seen them in the last few seconds
	// just try to lob in there
	m_bSaveSpitUseTentacle = false;
	if ( SeenEnemyWithinTime( 3.0f ) )
	{
		Vector vecSpitPos = ownerPos;
		if ( !IsMostlyBrain() )
		{
			vecSpitPos.z += BLOB_MAIN_SPIT_HEIGHT;
		}
		if ( GetSpitVector( vecSpitPos, targetPos, &m_vecSaveSpitVelocity ) )
		{
			m_vecSaveSpitTarget = targetPos;
			return true;
		}
	}

	// FIXME: Kinda expensive, would be nice to not do this all the time
	// but what the hell? This is a prototype! HAHAHAH!
	if ( CheckTentacleSpitLOS( ownerPos, targetPos ) )
		return true;

	return BaseClass::InnateWeaponLOSCondition( ownerPos, targetPos, bSetConditions );
}


//-----------------------------------------------------------------------------
// Attack conditions 
//-----------------------------------------------------------------------------
int CNPC_Blob::RangeAttack1Conditions( float flDot, float flDist )
{
	if ( !m_bCanSpit )
		return 0;

	if ( GetNextAttack() > gpGlobals->curtime )
		return COND_NOT_FACING_ATTACK;

	if ( flDist > BLOB_SPIT_MAX_RANGE )
		return COND_TOO_FAR_TO_ATTACK;

	// Really exposed brains will spit at close range
	if ( !IsMostlyBrain() )
	{
		if ( flDist < (10*12) )
			return COND_TOO_CLOSE_TO_ATTACK;
	}

	if ( IsStrategySlotRangeOccupied( SQUAD_SLOT_BLOB_SPIT, SQUAD_SLOT_BLOB_SPIT ) )
		return COND_BLOB_SPIT_SUPPRESSED_BY_SQUADMATE;

	return COND_CAN_RANGE_ATTACK1;
}

#define BLOB_MELEE_ATTACK_RANGE ( sv_blob_tentacle_length.GetFloat() * 0.8f )
#define BLOB_MELEE_ATTACK_RANGE_SQR ( BLOB_MELEE_ATTACK_RANGE * BLOB_MELEE_ATTACK_RANGE )
int CNPC_Blob::MeleeAttack1Conditions( float flDot, float flDist )
{
	if ( GetEnemy() == NULL )
		return 0;

	if ( !m_bCanSpit )
		return 0;

	if ( gpGlobals->curtime < m_fTentacleTimeout )
		return 0;

	if ( IsMostlyBrain() )
		return 0;

	int nRequiredParticleCount = BLOB_NUM_TENTACLE_PARTICLES / 3;
	if ( m_pMainGroup->m_members.Count() < BLOB_NUM_TENTACLE_PARTICLES )
	{
		nRequiredParticleCount = m_pMainGroup->m_members.Count() / 3;
	}

	Vector vecTarget = GetEnemy()->BodyTarget( GetAbsOrigin(), false );
	int nCloseParticles = 0;
	int nCount = m_pMainGroup->m_members.Count();
	for ( int i = 0; i < nCount; ++i )
	{					    
		if ( m_pMainGroup->m_members[i]->m_flags & BLOB_PARTICLE_DEATH )
			continue;

		if ( vecTarget.DistToSqr( m_pMainGroup->m_members[i]->m_position ) <= BLOB_MELEE_ATTACK_RANGE_SQR )
		{
			if ( ++nCloseParticles >= nRequiredParticleCount )
				break;
		}
	}

	if ( nCloseParticles < nRequiredParticleCount )
		return COND_TOO_FAR_TO_ATTACK;

	return COND_CAN_MELEE_ATTACK1;
}


//
//	FIXME: Create this in a better fashion!
//
static ConVar blob_debug_spit( "blob_debug_spit", "0" );
Vector CNPC_Blob::VecCheckThrowTolerance( CBaseEntity *pEdict, const Vector &vecSpot1, Vector vecSpot2, float flSpeed, float flTolerance )
{
	flSpeed = MAX( 1.0f, flSpeed );

	float flGravity = BLOB_SPIT_GRAVITY;

	Vector vecGrenadeVel = (vecSpot2 - vecSpot1);

	// throw at a constant time
	float flDist = vecGrenadeVel.Length( );
	float flTime = flDist / flSpeed;
	vecGrenadeVel *= ( flTime != 0.0f ) ? 1.0f / flTime : 1.0f;

	// adjust upward toss to compensate for gravity loss
	float flApexTime = flTime * 0.5f;
	vecGrenadeVel.z += flGravity * flApexTime;

	// Compute a position on the top of the arc to see if it's clear
	// Equation: z = zi + vz * t - .5 * g * t^2
	// NOTE: apex time is expected to be at time * .5
	// vz = flGravity * flTime * 0.5, since we've accounted for any other
	// z-based vel in the lerp for the initial apex computation
	// z = z1 + ( g * t ) - .5 * g * t^2 = g * t * ( 1.0f - 0.5f * t )
	Vector vecApex = vecSpot1 + (vecSpot2 - vecSpot1) * 0.5;
	vecApex.z += 0.5f * flGravity * flApexTime * flApexTime;

	trace_t tr;
	UTIL_TraceLine( vecSpot1, vecApex, 
		MASK_SHOT_BRUSHONLY, pEdict, COLLISION_GROUP_NONE, &tr );
	if (tr.fraction != 1.0)
	{
		// fail!
		if ( blob_debug_spit.GetBool() )
		{
			NDebugOverlay::Line( vecSpot1, vecApex, 255, 0, 0, true, 5.0 );
		}

		return vec3_origin;
	}

	if ( blob_debug_spit.GetBool() )
	{
		NDebugOverlay::Line( vecSpot1, vecApex, 0, 255, 0, true, 5.0 );
	}

	UTIL_TraceLine( vecApex, vecSpot2, 
		MASK_SHOT_BRUSHONLY, pEdict, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction != 1.0 )
	{
		bool bFail = true;

		// Didn't make it all the way there, but check if we're within our tolerance range
		if ( flTolerance > 0.0f )
		{
			float flNearness = ( tr.endpos - vecSpot2 ).LengthSqr();
			if ( flNearness < Square( flTolerance ) )
			{
				if ( blob_debug_spit.GetBool() )
				{
					NDebugOverlay::Sphere( tr.endpos, vec3_angle, flTolerance, 0, 255, 0, 0, true, 5.0 );
				}

				bFail = false;
			}
		}

		if ( bFail )
		{
			if ( blob_debug_spit.GetBool() )
			{
				NDebugOverlay::Line( vecApex, vecSpot2, 255, 0, 0, true, 5.0 );
				NDebugOverlay::Sphere( tr.endpos, vec3_angle, flTolerance, 255, 0, 0, 0, true, 5.0 );
			}
			return vec3_origin;
		}
	}

	if ( blob_debug_spit.GetBool() )
	{
		NDebugOverlay::Line( vecApex, vecSpot2, 0, 255, 0, true, 5.0 );
	}

	return vecGrenadeVel;
}

//-----------------------------------------------------------------------------
// Purpose: Get a toss direction that will properly lob spit to hit a target
// Input  : &vecStartPos - Where the spit will start from
//			&vecTarget - Where the spit is meant to land
//			*vecOut - The resulting vector to lob the spit
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_Blob::GetSpitVector( const Vector &vecStartPos, const Vector &vecTarget, Vector *vecOut )
{
	// Try the most direct route
	Vector vecToss = VecCheckThrowTolerance( this, vecStartPos, vecTarget, sk_blob_spit_speed.GetFloat(), 0.0f );

	// If this failed then try a little faster (flattens the arc)
	if ( vecToss == vec3_origin )
	{
		vecToss = VecCheckThrowTolerance( this, vecStartPos, vecTarget, sk_blob_spit_speed.GetFloat() * 1.5f, 0.0f );
		if ( vecToss == vec3_origin )
			return false;
	}

	// Save out the result
	if ( vecOut )
	{
		*vecOut = vecToss;
	}

	return true;
}



//-----------------------------------------------------------------------------
// Actually leaps
//-----------------------------------------------------------------------------
void CNPC_Blob::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	BaseClass::VPhysicsCollision( index, pEvent );

	if ( !m_bLeapCompleted )
	{
		if ( pEvent->pObjects[index] != m_pMainGroup->m_root->m_vphysParticle )
			return;
		EmitSound( "Blob.LeapImpact" );
		m_bLeapCompleted = true;
	}

	// Generate a touch event
	int otherIndex = !index;
	CBaseEntity *pOther = pEvent->pEntities[otherIndex];
	Touch( pOther );
}


//-----------------------------------------------------------------------------
// Actually leaps
//-----------------------------------------------------------------------------
static ConVar blob_leap_velocity( "blob_leap_velocity", "350" );

void CNPC_Blob::DoLeap()
{
	if ( !GetEnemy() )
		return;

	EmitSound( "Blob.Leap" );

	Vector groupDir = m_vecLeapTarget - GetAbsOrigin();
	float flVelocity = blob_leap_velocity.GetFloat();
	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		if ( m_blobParticles[i]->m_flags & ( BLOB_PARTICLE_DEATH | BLOB_PARTICLE_STRAGGLER ) )
			continue;

		m_blobParticles[i]->m_support = 0.0f;
		m_blobParticles[i]->m_contact = 0.0f;
		m_blobParticles[i]->m_isInContact = false;
		m_blobParticles[i]->m_nWaypointTargetIndex = -1;
		m_vecPhysParticles[i]->ApplyForceCenter( groupDir * m_vecPhysParticles[i]->GetMass() * flVelocity );
	}
	
	m_bSuppressMoveTowardGoal = true;
	m_bSuppressStragglerCreation = true;
	m_bLeapCompleted = false;
}


//------------------------------------------------------------------------------
// Forces the blob to find enemies when they are out of LOS
//------------------------------------------------------------------------------
bool CNPC_Blob::ShouldAlwaysThink()
{
	if ( BaseClass::ShouldAlwaysThink() )
		return true;

	return ( GetState() == NPC_STATE_ALERT );
}


//-----------------------------------------------------------------------------
// Actually spits
//-----------------------------------------------------------------------------
bool CNPC_Blob::DoSpit()
{
	if ( !GetEnemy() )
		return false;

	Vector vSpitPos;
	if ( m_pSpitTentacle )
	{
		vSpitPos = m_pSpitTentacle->m_vecEyePosition;
	}
	else
	{
		ComputeMainGroupCenter( &vSpitPos );
	}
	Vector	vTarget;

	// If our enemy is looking at us and far enough away, lead him
	if ( HasCondition( COND_ENEMY_FACING_ME ) && UTIL_DistApprox( GetAbsOrigin(), GetEnemy()->GetAbsOrigin() ) > (40*12) )
	{
		UTIL_PredictedPosition( GetEnemy(), 0.5f, &vTarget ); 
		vTarget.z = GetEnemy()->EyePosition().z;
	}
	else
	{
		// Otherwise he can't see us and he won't be able to dodge
		vTarget = GetEnemy()->BodyTarget( vSpitPos, true );
	}

	vTarget[2] += random->RandomFloat( 0.0f, 32.0f );

	// Try and spit at our target
	Vector	vecToss;				
	if ( GetSpitVector( vSpitPos, vTarget, &vecToss ) == false )
	{
		// Now try where they were
		if ( GetSpitVector( vSpitPos, m_vecSaveSpitTarget, &vecToss ) == false )
		{
			// Failing that, just shoot with the old velocity we calculated initially!
			vecToss = m_vecSaveSpitVelocity;
		}
	}

	m_vecSaveSpitVelocity = vecToss;

	// Find what our vertical theta is to estimate the time we'll impact the ground
	Vector vecToTarget = ( vTarget - vSpitPos );
	VectorNormalize( vecToTarget );
	float flVelocity = VectorNormalize( vecToss );
	float flCosTheta = DotProduct( vecToTarget, vecToss );
	float flTime = (vSpitPos-vTarget).Length2D() / ( flVelocity * flCosTheta );

	// Emit a sound where this is going to hit so that targets get a chance to act correctly
	CSoundEnt::InsertSound( SOUND_DANGER, vTarget, (15*12), flTime, this );

	// Don't fire again until this volley would have hit the ground (with some lag behind it)
	if ( !IsMostlyBrain() )
	{
		SetNextAttack( gpGlobals->curtime + random->RandomFloat( 0.5f, 1.0f ) );
	}
	else
	{
		SetNextAttack( gpGlobals->curtime + random->RandomFloat( 0.4f, 0.7f ) );
	}

	for ( int i = 0; i < 6; i++ )
	{
		CGrenadeSpit *pGrenade = (CGrenadeSpit*) CreateEntityByName( "grenade_spit" );
		pGrenade->SetAbsOrigin( vSpitPos );
		pGrenade->SetAbsAngles( vec3_angle );
		DispatchSpawn( pGrenade );
		pGrenade->SetGravity( UTIL_ScaleForGravity( BLOB_SPIT_GRAVITY ) );
		pGrenade->SetThrower( this );
		pGrenade->SetOwnerEntity( this );
		pGrenade->SpitDoesPoisonDamage( false );

		if ( i == 0 )
		{
			pGrenade->SetSpitSize( SPIT_LARGE );
			pGrenade->SetAbsVelocity( vecToss * flVelocity );
		}
		else
		{
			pGrenade->SetAbsVelocity( ( vecToss + RandomVector( -0.035f, 0.035f ) ) * flVelocity );
			pGrenade->SetSpitSize( random->RandomInt( SPIT_SMALL, SPIT_MEDIUM ) );
		}

		pGrenade->SetDamage( pGrenade->GetDamage() * 0.5f );

		// Tumble through the air
		pGrenade->SetLocalAngularVelocity(
			QAngle( random->RandomFloat( -250, -500 ),
			random->RandomFloat( -250, -500 ),
			random->RandomFloat( -250, -500 ) ) );
	}

	for ( int i = 0; i < 8; i++ )
	{
		DispatchParticleEffect( "blood_impact_yellow_01", vSpitPos + RandomVector( -12.0f, 12.0f ), RandomAngle( 0, 360 ) );
	}

	EmitSound( "NPC_Antlion.PoisonShoot" );
	return true;
}

bool CNPC_Blob::SpawnTinyBlob( CBlobGroup *pGroup )
{
	if ( !pGroup )
		return false;

	int nCount = pGroup->m_members.Count();
	if ( nCount == 0 )
		return false;

	CNPC_BlobTiny *pBlobTiny = (CNPC_BlobTiny*)CreateEntityByName( "npc_blob_tiny" );

	Vector vecVelocity = pGroup->m_root->m_velocity;
	vecVelocity.z += RandomFloat( 30.0f, 60.0f );
	if ( GetEnemy() )
	{
		Vector vecDelta;
		VectorSubtract( GetEnemy()->GetAbsOrigin(), pGroup->m_root->m_position, vecDelta );
		VectorNormalize( vecDelta );
		vecDelta *= RandomFloat( 75.0f, 100.0f );
		vecDelta.z = RandomFloat( 180.0f, 240.0f );
		vecVelocity += vecDelta;
	}
	bool bIsSplitter = ( RandomInt( 0, 100 ) <= 5 );
	pBlobTiny->SetAbsOrigin( pGroup->m_root->m_position );
	pBlobTiny->SetAbsVelocity( vecVelocity );
	pBlobTiny->SetRadius( m_flRadius * 1.5f );
	pBlobTiny->SetSplitterBlob( bIsSplitter, 3 );
	pBlobTiny->SetFastBlob( m_bShakeFastBlobs );
	pBlobTiny->Spawn();

	m_hSpawnedBlobs.AddToTail( pBlobTiny );

	return true;
}


//-----------------------------------------------------------------------------
// Computes conditions for taking pipe shortcuts
//-----------------------------------------------------------------------------
void CNPC_Blob::GatherPipeShortcutConditions( void )
{
	if( !ConditionInterruptsCurSchedule( COND_BLOB_SEE_PIPE_SHORTCUT ) && 
		!IsCustomInterruptConditionSet( COND_BLOB_SEE_PIPE_SHORTCUT ) )
		return;

	if ( GetNavigator()->GetGoalType() == GOALTYPE_NONE )
		return;

	Vector vTargetLocation = GetNavigator()->GetGoalPos();

	CFunc_Pipe_Entrance *pBestPipeEntrance = NULL;
	CFunc_Pipe_Entrance *pBestPipeExit = NULL;
	float fShortestPipeDistance = GetNavigator()->GetPathDistanceToGoal() * 0.6f;	// Should shortcut the distance by almost half to be considered

	// Get the node IDs so we can check if a path was built though a pipe
	int iCurPathCornerID = -1;
	int iNextPathCornerID = -1;

	AI_Waypoint_t *pCurWaypoint = GetNavigator()->GetPath()->GetCurWaypoint();
	if ( pCurWaypoint )
	{
		iCurPathCornerID = pCurWaypoint->iNodeID;

		AI_Waypoint_t *pNextWaypoint = pCurWaypoint->GetNext();

		if ( pNextWaypoint )
		{
			iNextPathCornerID = pNextWaypoint->iNodeID;
		}
	}

	// Check all the pipes to see if they make a shorter path to the target
	AISightIter_t iter = (AISightIter_t)(-1);
	CBaseEntity *pObject = GetSenses()->GetFirstSeenEntity( &iter, SEEN_MISC );

	while ( pObject )
	{
		CFunc_Pipe_Entrance *pPipe = dynamic_cast<CFunc_Pipe_Entrance *>( pObject );

		if ( pPipe )
		{
			if ( pPipe->m_bDisabled )
			{
				pObject = GetSenses()->GetNextSeenEntity( &iter );
				continue;
			}

			float fDistanceToEntrance = GetAbsOrigin().DistTo( pPipe->GetAbsOrigin() );

			// Bail if the entrance is farther than the path length
			if ( fDistanceToEntrance > fShortestPipeDistance )
			{
				pObject = GetSenses()->GetNextSeenEntity( &iter );
				continue;
			}

			// Find the exit closest to the goal using this entrance
			CFunc_Pipe_Entrance *pCloserPipeExit = NULL;

			float fNearestExitDistance = fShortestPipeDistance;

			for( int i = pPipe->m_LinkedEntrances.Count(); --i >= 0; )
			{
				CFunc_Pipe_Entrance *pExit = pPipe->m_LinkedEntrances[i].pPipeEntrance;

				if ( pExit->m_bDisabled )
					continue;

				float fDist = vTargetLocation.DistTo( pExit->GetAbsOrigin() );

				// If our next node is the pipe entrance then we're already pathing through it, set the distance very low
				int iLinkSrcID = pPipe->m_LinkedEntrances[i].pDynamicLink->m_nSrcID;
				int iLinkDestID = pPipe->m_LinkedEntrances[i].pDynamicLink->m_nDestID;

				if ( ( iLinkSrcID == iCurPathCornerID && iLinkDestID == iNextPathCornerID ) || ( iLinkDestID == iCurPathCornerID && iLinkSrcID == iNextPathCornerID ) )
					fDist = 1.0f;

				if( fDist < fNearestExitDistance )
				{
					fNearestExitDistance = fDist;
					pCloserPipeExit = pExit;
				}
			}

			if( pCloserPipeExit != NULL )
			{
				// See if this pipe connection is the closest
				float fTotalPipeDist = fDistanceToEntrance + fNearestExitDistance;
				if ( fTotalPipeDist < fShortestPipeDistance )
				{
					fShortestPipeDistance = fTotalPipeDist;
					pBestPipeEntrance = pPipe;
					pBestPipeExit = pCloserPipeExit;
				}
			}
		}

		pObject = GetSenses()->GetNextSeenEntity( &iter );
	}

	if ( pBestPipeEntrance && pBestPipeExit )
	{
		// We found a shorter path
		m_hPipeEntrance = pBestPipeEntrance;
		m_hPipeExit = pBestPipeExit;
		SetCondition( COND_BLOB_SEE_PIPE_SHORTCUT );
	}
}

void CNPC_Blob::GatherBuddyBlobConditions()
{
	//if ( !ConditionInterruptsCurSchedule( COND_BLOB_SEE_BUDDY_BLOB ) &&
	//		!IsCustomInterruptConditionSet( COND_BLOB_SEE_BUDDY_BLOB ) )
	//	return;
	if ( !m_bCanMerge )
		return;

	m_hBuddyBlob = NULL;

	// Non-brained blobs don't look as far as brained blobs
	float fClosestBuddyBlobDistanceSqr = HasBrain() ? 1024.0f : 192.0f;
	fClosestBuddyBlobDistanceSqr *= fClosestBuddyBlobDistanceSqr;
	CNPC_Blob *pClosestBuddyBlob = NULL;

	CBaseEntity *pObject = NULL;
	while ( ( pObject = gEntList.FindEntityByClassname( pObject, "npc_blob" ) ) != NULL )
	{
		if ( pObject == this )
			continue;

		CNPC_Blob *pBlob = static_cast<CNPC_Blob*>( pObject );

		float fDistanceSqr = GetAbsOrigin().DistToSqr( pObject->GetAbsOrigin() );
		if ( fDistanceSqr >= fClosestBuddyBlobDistanceSqr )
			continue;
		
		if ( ( GetBlobSize() < BLOB_SIZE_MEDIUM ) || !HasBrain() || ( m_nActiveParticles < pBlob->m_nActiveParticles ) )
		{
			fClosestBuddyBlobDistanceSqr = fDistanceSqr;
			pClosestBuddyBlob = pBlob;
		}
	}

	if ( pClosestBuddyBlob )
	{
		m_hBuddyBlob = pClosestBuddyBlob;
		SetCondition( COND_BLOB_SEE_BUDDY_BLOB );
	}
}


//-----------------------------------------------------------------------------
// Can the blob leap?
//-----------------------------------------------------------------------------
#define BLOB_LEAP_PROVOKE_DISTANCE 200.0f

void CNPC_Blob::GatherLeapConditions()
{
	if ( !m_bCanLeap )
		return;

	if ( IsMostlyBrain() && ( m_BlobType != BLOB_TYPE_LEAPER ) )
		return;

	if ( gpGlobals->curtime < m_flNextLeapTime )
		return;

	if ( IsCurSchedule( SCHED_BLOB_LEAP ) )
		return;

	CBaseEntity *pEnemy = GetEnemy();
	if ( !pEnemy )
		return;

	if ( m_BlobType != BLOB_TYPE_LEAPER )
	{
		if ( pEnemy->BodyTarget( GetAbsOrigin() ).DistToSqr( GetAbsOrigin() ) > BLOB_LEAP_PROVOKE_DISTANCE * BLOB_LEAP_PROVOKE_DISTANCE )
		{
			if ( m_flLastLeapTime < gpGlobals->curtime )
				return;
		}
	}

	if ( !IsCurSchedule( SCHED_BLOB_SPIT ) )
	{
		trace_t tr;
		UTIL_TraceHull( GetAbsOrigin(), pEnemy->BodyTarget( GetAbsOrigin() ), 
			Vector( -m_flRadius, -m_flRadius, -m_flRadius ),
			Vector( m_flRadius, m_flRadius, m_flRadius ),
			GetAITraceMask_BrushOnly(), pEnemy, HL2COLLISION_GROUP_BLOB, &tr );

		if ( tr.fraction == 1.0f )
		{
			if ( IsStrategySlotRangeOccupied( SQUAD_SLOT_BLOB_LEAP, SQUAD_SLOT_BLOB_LEAP ) )
				SetCondition( COND_BLOB_LEAP_SUPPRESSED_BY_SQUADMATE );
			else
				SetCondition( COND_BLOB_CAN_LEAP );
		}
	}
}


//-----------------------------------------------------------------------------
// Can the blob regenerate particles?
//-----------------------------------------------------------------------------
void CNPC_Blob::GatherRegenerateParticlesConditions()
{
	if ( !IsMostlyBrain() )
	{
		if ( m_bCanSpawnRegenerators )
		{
			if ( gpGlobals->curtime >= m_flNextSpawnRegeneratorTime )
			{
				SetCondition( COND_BLOB_CAN_SPAWN_REGENERATOR );
			}
		}
		return;
	}

	if ( m_bCanRegenerate )
	{
		if ( gpGlobals->curtime >= m_flNextRegenerateTime )
		{
			SetCondition( COND_BLOB_REGENERATE_PARTICLES );
		}
	}

	// Sort by nearest regenerator
	int nCount = CBlobRegenerator::GetBlobRegeneratorCount();
	for ( int i = 0; i < nCount; ++i )
	{
		CBlobRegenerator *pRegenerator = CBlobRegenerator::GetBlobRegenerator( i );
		if ( pRegenerator && !pRegenerator->IsInactive() )
		{
			SetCondition( COND_BLOB_CAN_RUN_TO_REGENERATOR );
			break;
		}
	}
}

void CNPC_Blob::GatherShakeBlobConditions()
{
	// Can we shake off the blobs?
	if ( !m_bCanShakeBlobs )
		return;

	if ( GetState() != NPC_STATE_COMBAT ) 
		return;

	if ( gpGlobals->curtime < m_flNextShakeBlobTime )
		return;

	if ( !m_pMainGroup )
		return;

	if ( IsMostlyBrain() && ( m_BlobType != BLOB_TYPE_SPAWNER ) ) 
		return;

	// Determine how many alive tiny blobs there are
	int nCount = m_hSpawnedBlobs.Count();
	for ( int i = nCount; --i >= 0; )
	{
		if ( !m_hSpawnedBlobs[i].Get() )
		{
			m_hSpawnedBlobs.FastRemove( i );
		}
	}

	// Can't spawn more than this many blobs at once
	if ( m_hSpawnedBlobs.Count() > 20 )
		return;

	// Need to have a large enough non-brain group    
	SetCondition( COND_BLOB_CAN_SHAKE_OFF_BLOBS );
}


//-----------------------------------------------------------------------------
// Indicates whether the blob cares about the hint
//-----------------------------------------------------------------------------
bool CNPC_Blob::FValidateHintType( CAI_Hint *pHint )
{
	if ( ( pHint->HintType() != HINT_EP3_BLOB_SHAKE_POSITION ) &&
		 ( pHint->HintType() != HINT_EP3_BLOB_FIRE_COVER_POSITION ) &&
		 ( pHint->HintType() != HINT_EP3_BLOB_BRAIN_COVER_POSITION ) &&
		 ( pHint->HintType() != HINT_EP3_BLOB_SPIT_POSITION ) &&
		 ( pHint->HintType() != HINT_EP3_BLOB_BRAIN_REGENERATE_POSITION ) &&
		 ( pHint->HintType() != HINT_EP3_BLOB_SPAWN_REGENERATOR_POSITION ) )
	{
		return false;
	}

	// Any regenerate position is ok
	if ( pHint->HintType() == HINT_EP3_BLOB_BRAIN_REGENERATE_POSITION )
		return true;

	// Get the hint position
	Vector vecHintPos;
	pHint->GetPosition( this, &vecHintPos );

	// Regenerator hints can't have regenerators near them
	if ( pHint->HintType() == HINT_EP3_BLOB_SPAWN_REGENERATOR_POSITION )
	{
		int nCount = CBlobRegenerator::GetBlobRegeneratorCount();
		for ( int i = 0; i < nCount; ++i )
		{
			CBlobRegenerator *pRegenerator = CBlobRegenerator::GetBlobRegenerator( i );
			if ( pRegenerator && vecHintPos.DistToSqr( pRegenerator->GetAbsOrigin() ) < ( 64*64 ) )
				return false;
		}
		return true;
	}

	// If this is a brain cover or spit hint position, don't let the player have LOS to it
	CBaseEntity *pEnemy = GetEnemy();
	if (( pHint->HintType() == HINT_EP3_BLOB_BRAIN_COVER_POSITION ) ||
		( pHint->HintType() == HINT_EP3_BLOB_SPIT_POSITION ) )
	{
		if ( pEnemy )
		{
			trace_t tr;
			AI_TraceLOS( pEnemy->EyePosition(), vecHintPos, NULL, &tr );
			if ( ( tr.fraction == 1.0f ) || tr.m_pEnt == pEnemy )
				return false;
		}
	}

	// If this is a spit hint position, look for info_blob_spit_targets which have LOS to the player
	if ( pHint->HintType() == HINT_EP3_BLOB_SPIT_POSITION )
	{
		if ( !CheckTentacleSpitLOS( vecHintPos, pEnemy->BodyTarget( vecHintPos ) ) )
			return false;
	}

	// Don't allow hints that are inside fire sounds
	// NOTE: m_iDangerSounds is set up in TASK_FIND_HINTNODE; we cache it off there
	CSound *pCurrentSound;
	for ( int iSound = m_iDangerSounds; iSound != SOUNDLIST_EMPTY; iSound = pCurrentSound->NextSound() )
	{						 
		pCurrentSound = CSoundEnt::SoundPointerForIndex( iSound );
		Vector vecSound = pCurrentSound->GetSoundOrigin();
		float flDistSqr = vecSound.DistToSqr( vecHintPos );
		if ( flDistSqr < ( pCurrentSound->Volume() * pCurrentSound->Volume() ) )
			return false;
	}
	return true;
}


//-----------------------------------------------------------------------------
// Computes conditions
//-----------------------------------------------------------------------------
void CNPC_Blob::GatherConditions( void )
{
	BaseClass::GatherConditions();

	static int conditionsToClear[] = 
	{
		COND_BLOB_SEE_PIPE_SHORTCUT,
		COND_BLOB_SEE_BUDDY_BLOB,
		COND_BLOB_TOUCHING_BUDDY_BLOB,
		COND_BLOB_TRANSFERING_THROUGH_PIPE,
		COND_BLOB_AUTO_SPLIT_FROM_ANOTHER_BLOB,
		COND_BLOB_HEALTH_LOW,
		COND_BLOB_CAN_STALK_TARGET,
		COND_BLOB_CAN_SHAKE_OFF_BLOBS,
		COND_BLOB_BRAIN_EXPOSED,
		COND_SEE_FEAR,
		COND_BLOB_REGENERATE_PARTICLES,
		COND_BLOB_CAN_LEAP,
		COND_BLOB_CAN_RUN_TO_REGENERATOR,
		COND_BLOB_CAN_SPAWN_REGENERATOR,
		COND_BLOB_LEAP_SUPPRESSED_BY_SQUADMATE,
		COND_BLOB_SPIT_SUPPRESSED_BY_SQUADMATE,
	};

	ClearConditions( conditionsToClear, ARRAYSIZE( conditionsToClear ) );

	// Handy debug variables:
	int	schedID;
	const char *pScheduleName;
	schedID			= GetCurSchedule() ? GetCurSchedule()->GetId() : 0;
	pScheduleName	= GetCurSchedule() ? GetCurSchedule()->GetName() : "none";

	GatherPipeShortcutConditions();
	GatherBuddyBlobConditions();
	GatherShakeBlobConditions();
	GatherRegenerateParticlesConditions();
	GatherLeapConditions();

	if( ConditionInterruptsCurSchedule( COND_BLOB_FORCE_TRANSFER_THROUGH_PIPE ) || IsCustomInterruptConditionSet( COND_BLOB_FORCE_TRANSFER_THROUGH_PIPE ) )
	{
		if ( m_bForcePipeTransfer )
		{
			m_bForcePipeTransfer = false;

			if ( m_hPipeEntrance.Get() && m_hPipeExit.Get() )
			{
				SetCondition( COND_BLOB_FORCE_TRANSFER_THROUGH_PIPE );
			}
		}
	}

	if ( ConditionInterruptsCurSchedule( COND_BLOB_TRANSFERING_THROUGH_PIPE ) || IsCustomInterruptConditionSet( COND_BLOB_TRANSFERING_THROUGH_PIPE ) )
	{
		if ( m_bIsPipeTransferBlob )
		{
			SetCondition( COND_BLOB_TRANSFERING_THROUGH_PIPE );
		}
	}

	//if ( ConditionInterruptsCurSchedule( COND_BLOB_AUTO_SPLIT_FROM_ANOTHER_BLOB ) || IsCustomInterruptConditionSet( COND_BLOB_AUTO_SPLIT_FROM_ANOTHER_BLOB ) )
	{
		if ( m_fAutoSplitTimeOut > 0.0f && m_fAutoSplitTimeOut < gpGlobals->curtime )
		{
			// Haven't met our blob try a scary spawn
			if ( gpGlobals->curtime - m_fLastSeenTime > 2.0f )
			{
				// Give it another 10 seconds
				m_fAutoSplitTimeOut = gpGlobals->curtime + 10.0f;
				TeleportToUnseenLocation();
			}
			else
			{
				// Ok, screw it
				m_hAutoSplitBlob = NULL;
				m_fAutoSplitTimeOut = 0.0f;
			}
		}

		CNPC_Blob *pAutoSplitBlob = m_hAutoSplitBlob.Get();

		if ( pAutoSplitBlob )
		{
			SetCondition( COND_BLOB_AUTO_SPLIT_FROM_ANOTHER_BLOB );
		}
	}

	if ( ConditionInterruptsCurSchedule( COND_BLOB_HEALTH_LOW ) || IsCustomInterruptConditionSet( COND_BLOB_HEALTH_LOW ) )
	{
		if ( GetBlobSize() > BLOB_SIZE_TINY && GetHealth() <= 1 )
		{
			SetCondition( COND_BLOB_HEALTH_LOW );
		}
	}

	if ( IsMostlyBrain() )
	{
		SetCondition( COND_BLOB_BRAIN_EXPOSED );
	}

	// Tentacle attacks
	if ( m_bInputStalkTarget )
	{
		// Input-specified tentacle attack
		SetCondition( COND_BLOB_CAN_STALK_TARGET );
		SetCondition( COND_PROVOKED ); // Interrupts lower-priority schedules like SCHED_IDLE_STAND
		Assert( !!m_hNextStalkTarget );
	}
	else if ( sv_blob_tentacle_defend.GetBool() && !HasCondition( COND_BLOB_BRAIN_EXPOSED ) )
	{
		// Automatic defensive tentacle attack (happens when the enemy is near)
		CBaseEntity *pEnemy = GetEnemy();
		m_hNextStalkTarget = NULL;

		if ( m_bCanTentacleAttack && IsBigEnoughToCreateNewTentacle() && pEnemy && ( gpGlobals->curtime > m_fTentacleTimeout ) )
		{
			float flDistanceToTarget = GetAbsOrigin().DistTo( pEnemy->BodyTarget( GetAbsOrigin() ) );
			if ( flDistanceToTarget < sv_blob_tentacle_length.GetFloat() )
			{
				SetCondition( COND_BLOB_CAN_STALK_TARGET );
				SetCondition( COND_PROVOKED ); // Interrupts lower-priority schedules like SCHED_IDLE_STAND
				m_hNextStalkTarget = pEnemy;
			}
		}
	}

	if ( IsStunned() )
	{
		// We've been hurt and are going to be shell-shocked for a while...
		SetCondition( COND_HEAVY_DAMAGE );
		SetCondition( COND_LIGHT_DAMAGE );
	}
}


//-----------------------------------------------------------------------------
// Changes color
//-----------------------------------------------------------------------------
void CNPC_Blob::ChangeColor( const Vector &vecColor, float flDuration )
{
	if ( gpGlobals->curtime >= m_flColorChangeEndTime )
	{
		// Don't bother changing state here
		if ( vecColor == m_vecEndColor )
			return;
		m_vecStartColor = m_vecEndColor;
	}
	else if ( gpGlobals->curtime > m_flColorChangeStartTime )
	{
		float t = ( gpGlobals->curtime - m_flColorChangeStartTime ) /
			( m_flColorChangeEndTime - m_flColorChangeStartTime );
		VectorLerp( m_vecStartColor, m_vecEndColor, t, m_vecStartColor.GetForModify() );
	}

	m_vecEndColor = vecColor;
	m_flColorChangeStartTime = gpGlobals->curtime;
	m_flColorChangeEndTime = gpGlobals->curtime + flDuration;
}


//-----------------------------------------------------------------------------
// Computes number of non-brain non-tentacle particles
//-----------------------------------------------------------------------------
int CNPC_Blob::GetNormalParticleCount() const
{
	if ( !m_pMainGroup )
		return 0;
	int nNonBrainParticleCount = 0;
	int nCount = m_pMainGroup->m_members.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		int flagsToIgnore = BLOB_PARTICLE_BRAIN | BLOB_PARTICLE_TENTACLE | BLOB_PARTICLE_BIRTH | BLOB_PARTICLE_DEATH;
		if ( ( m_pMainGroup->m_members[i]->m_flags & flagsToIgnore ) == 0 )
		{
			++nNonBrainParticleCount;
		}
	}
	return nNonBrainParticleCount;
}


//-----------------------------------------------------------------------------
// Computes ratio of brain particles to other blob particles
//-----------------------------------------------------------------------------
float CNPC_Blob::ComputeBrainParticleRatio() const
{
	int nNonBrainParticleCount = 0;
	int nBrainParticleCount = 0;

	for ( int i = 0; i < m_nActiveParticles; ++i )
	{
		if ( m_blobParticles[i]->m_flags & BLOB_PARTICLE_BRAIN )
			++nBrainParticleCount;
		else if ( !( m_blobParticles[i]->m_flags & ( BLOB_PARTICLE_TENTACLE | BLOB_PARTICLE_DEATH ) ) )
			++nNonBrainParticleCount;
	}

	return ( nBrainParticleCount / MAX( 1.0f, nNonBrainParticleCount ) );
}

int CNPC_Blob::SelectEnemySchedule( void )
{
	// Angry blob >:[
// UNDONE: flailing behaviour to be enabled soon
//		return SCHED_BLOB_FLAIL;
							   
	if ( HasCondition( COND_HEAR_DANGER ) )
		return SCHED_BLOB_FLEE_DANGER_SOUND;

	if ( !HasBrain() )
	{
		// If this blob doesn't have a brain, it just sits there unless it's close to another blob
		if ( HasCondition( COND_BLOB_SEE_BUDDY_BLOB ) )
			return SCHED_BLOB_REFORM;

// UNDONE: flailing behaviour to be enabled soon
//		return SCHED_BLOB_FLAIL;

		return SCHED_IDLE_STAND;
	}

	// Smack if we can!
	if ( HasCondition( COND_CAN_MELEE_ATTACK1 ) )
		return SCHED_BLOB_TENTACLE_ATTACK;

	// Regeneration takes priority if the blob can do it
	if ( HasCondition( COND_BLOB_REGENERATE_PARTICLES ) )
		return SCHED_BLOB_REGENERATE_PARTICLES;

	if ( HasCondition( COND_BLOB_CAN_RUN_TO_REGENERATOR ) )
	{
		if ( RandomFloat( 0.0f, 1.0f ) < 0.5f )
		{
			return SCHED_BLOB_RUN_TO_REGENERATOR;
		}
	}

	if ( HasCondition( COND_BLOB_CAN_LEAP ) )
		return SCHED_BLOB_LEAP;

	// Tentacle attacks (defensive, or ordered by an input event) take precedence over other activities
	if ( HasCondition( COND_BLOB_CAN_STALK_TARGET ) )
		return SCHED_BLOB_STALK_TARGET;

	if ( HasCondition( COND_BLOB_FORCE_TRANSFER_THROUGH_PIPE ) )
		return SCHED_BLOB_FORCE_ENTER_PIPE;

	if ( HasCondition( COND_BLOB_AUTO_SPLIT_FROM_ANOTHER_BLOB ) )
		return SCHED_BLOB_REFORM;

	if ( HasCondition( COND_BLOB_TRANSFERING_THROUGH_PIPE ) )
		return SCHED_BLOB_TRANSFER_THROUGH_PIPE;

	if ( HasCondition( COND_BLOB_SEE_PIPE_SHORTCUT ) )
		return SCHED_BLOB_ENTER_PIPE;

	float flBrainRatio = 5.0f * ComputeBrainParticleRatio();
	flBrainRatio = clamp( flBrainRatio, 0.5f, 5.0f );

	// Pick a behaviour at random
	float flShakeChance, flSpitChance, flTotalChance, flReformChance, flSpawnChance;
	flShakeChance	 = 0.0f          + ( HasCondition( COND_BLOB_CAN_SHAKE_OFF_BLOBS) ? 1.0f : 0.0f );
	flSpitChance	 = flShakeChance + ( HasCondition( COND_CAN_RANGE_ATTACK1		) ? 1.0f : 0.0f );
	flReformChance	 = flSpitChance  + ( HasCondition( COND_BLOB_SEE_BUDDY_BLOB		) ? flBrainRatio : 0.0f );
	flSpawnChance	 = flReformChance + ( HasCondition( COND_BLOB_CAN_SPAWN_REGENERATOR ) ? 0.4f : 0.0f );
	flTotalChance	 = flSpawnChance;
	if ( flTotalChance > 0.0f )
	{
		float flChoice = RandomFloat( 0.0f, flTotalChance );

		if ( flChoice <= flShakeChance )
			return SCHED_BLOB_SHAKE_OFF_BLOBS_USE_HINTS;

		if ( flChoice <= flSpitChance )
			return SCHED_BLOB_SPIT;

		if ( flChoice <= flReformChance )
			return SCHED_BLOB_REFORM;

		if ( flChoice <= flSpawnChance )
		{
			m_flNextSpawnRegeneratorTime = gpGlobals->curtime + RandomFloat( 10.0f, 15.0f );
			return SCHED_BLOB_SPAWN_REGENERATOR;
		}
	}

	if ( GetEnemy() != NULL )
	{		    
		if ( m_bCanSpit && ( gpGlobals->curtime > GetNextAttack() ) )
			return SCHED_BLOB_ESTABLISH_LINE_OF_FIRE;
		if ( IsMostlyBrain() )
			return SCHED_TAKE_COVER_FROM_ENEMY_USE_BRAIN_HINTS;
		if ( !HasCondition(COND_SEE_ENEMY) && m_bCanShakeBlobs )
			return SCHED_ESTABLISH_LINE_OF_FIRE;
		return SCHED_TAKE_COVER_FROM_ENEMY;
	}

	return SCHED_IDLE_STAND;
}


int CNPC_Blob::SelectLeaperSchedule( void )
{
	if ( HasCondition( COND_BLOB_CAN_RUN_TO_REGENERATOR ) )
	{
		if ( RandomFloat( 0.0f, 1.0f ) < 0.5f )
		{
			return SCHED_BLOB_RUN_TO_REGENERATOR;
		}
	}
				  
	bool bLeapSuppressed = HasCondition( COND_BLOB_LEAP_SUPPRESSED_BY_SQUADMATE );
	if ( HasCondition( COND_BLOB_CAN_LEAP ) )
	{
		if ( OccupyStrategySlot( SQUAD_SLOT_BLOB_LEAP ) )
			return SCHED_BLOB_LEAP;
		bLeapSuppressed = true;
	}

	if ( bLeapSuppressed )
	{
//		if ( OccupyStrategySlot( SQUAD_SLOT_BLOB_FLANK ) )
//			return XXX;
		if ( IsMostlyBrain() )
			return SCHED_TAKE_COVER_FROM_ENEMY_USE_BRAIN_HINTS;

		return SCHED_TAKE_COVER_FROM_ENEMY;
	}

	if ( GetEnemy() != NULL )
	{		    
		if ( IsMostlyBrain() )
			return SCHED_TAKE_COVER_FROM_ENEMY_USE_BRAIN_HINTS;
		if ( gpGlobals->curtime >= m_flNextLeapTime )
			return SCHED_ESTABLISH_LINE_OF_FIRE;
		return SCHED_TAKE_COVER_FROM_ENEMY;
	}

	return SCHED_IDLE_STAND;
}

								  
//-----------------------------------------------------------------------------
// Select schedule!
//-----------------------------------------------------------------------------
int CNPC_Blob::SelectSpitSchedule( void )
{
	if ( HasCondition( COND_BLOB_CAN_RUN_TO_REGENERATOR ) )
	{
		if ( RandomFloat( 0.0f, 1.0f ) < 0.5f )
		{
			return SCHED_BLOB_RUN_TO_REGENERATOR;
		}
	}

	bool bSpitSuppressed = HasCondition( COND_BLOB_SPIT_SUPPRESSED_BY_SQUADMATE );
	if ( HasCondition( COND_CAN_RANGE_ATTACK1 ) )
	{
		if ( OccupyStrategySlot( SQUAD_SLOT_BLOB_SPIT ) )
			return SCHED_BLOB_SPIT;
		bSpitSuppressed = true;
	}

	if ( bSpitSuppressed || ( GetEnemy() != NULL ) )
	{		    
		if ( gpGlobals->curtime > GetNextAttack() )
			return SCHED_BLOB_ESTABLISH_LINE_OF_FIRE;
		if ( IsMostlyBrain() )
			return SCHED_TAKE_COVER_FROM_ENEMY_USE_BRAIN_HINTS;
		return SCHED_TAKE_COVER_FROM_ENEMY;
	}

	return SCHED_IDLE_STAND;
}


//-----------------------------------------------------------------------------
// Select schedule!
//-----------------------------------------------------------------------------
int CNPC_Blob::SelectSchedule( void )
{
	int nSched = SCHED_NONE;
	switch ( m_BlobType )
	{
	case BLOB_TYPE_GENERIC:
	default:
		nSched = SelectEnemySchedule();
		break;

	case BLOB_TYPE_LEAPER:
		nSched = SelectLeaperSchedule();
		break;

	case BLOB_TYPE_SPITTER:
		nSched = SelectSpitSchedule();
		break;
	}

	if ( nSched == SCHED_NONE )
	{
		nSched = BaseClass::SelectSchedule();
	}
	return nSched;
}

void CNPC_Blob::OnScheduleChange( void )
{
	BaseClass::OnScheduleChange();

	m_hRegeneratorTarget = NULL;
	ChangeColor( m_BaseColor, 0.3f );
	m_nNeighborForceMode = NEIGHBOR_FORCE_NORMAL;
	m_bSuppressStragglerCreation = false;
	m_bSuppressMoveTowardGoal = false;

	if ( GetHintNode() )
	{
		GetHintNode()->Unlock();
		SetHintNode( NULL );
	}

	// If we have tentacles, our schedule has either been interrupted or completed normally (either way, kill the tentacles)
	TentaclesInterrupt();

	if ( IsCurSchedule( SCHED_BLOB_STALK_TARGET ) )
	{
		// Clear stalking state
		m_hStalkTarget = NULL;
		m_nStalkCreatedTentacles = 0;
		StalkUpdateState();
	}

	// Revert to the default physics parameters until the next schedule sets what it wants
	SetPhysParamState( "default" );

	// Clear this here since we may be exiting a 'navigate to' schedule
	m_fMinDistToTarget = 0;

	// Clear this here since it should only interrupt one schedule
	ClearCondition( COND_BLOB_RECEIVED_COMMAND );
}

bool CNPC_Blob::IsCurTaskContinuousMove( void )
{
	// Override this so we can say 'yes' for TASK_WAIT_FOR_STALK_MOVEMENT
	const Task_t* pTask = GetTask();
	if( pTask && ( pTask->iTask == TASK_WAIT_FOR_STALK_MOVEMENT ) )
		return true;

	return BaseClass::IsCurTaskContinuousMove();
}

bool CNPC_Blob::ComputeMeleeAttackPosition( CBlobParticle **ppTentacleRootParticle, Vector *pAttackPosition )
{
	Vector vecTarget = GetEnemy()->BodyTarget( GetAbsOrigin(), false );
	int nCount = m_pMainGroup->m_members.Count();

	// Compute centroid of particles near the target
	Vector vecCentroid = vec3_origin;
	int nCloseCount = 0;
	int flagsToIgnore = BLOB_PARTICLE_BIRTH | BLOB_PARTICLE_DEATH | BLOB_PARTICLE_TENTACLE | BLOB_PARTICLE_STRAGGLER;
	for ( int i = 0; i < nCount; ++i )
	{
		if ( m_pMainGroup->m_members[i]->m_flags & flagsToIgnore )
			continue;

		const Vector &vecParticle = m_pMainGroup->m_members[i]->m_position;
		if ( vecTarget.DistToSqr( vecParticle ) <= BLOB_MELEE_ATTACK_RANGE_SQR )
		{
			vecCentroid += vecParticle;
			++nCloseCount;
		}
	}

	if ( nCloseCount == 0 )
		return false;

	vecCentroid /= nCloseCount;

	// Choose a particle closest to the centroid
	// Hopefully this will result in the least number of particles moving far
	int nClosestParticle = 0;
	float flClosestDistSqr = FLT_MAX;
	for ( int i = 0; i < nCount; ++i )
	{
		if ( m_pMainGroup->m_members[i]->m_flags & flagsToIgnore )
			continue;

		const Vector &vecParticle = m_pMainGroup->m_members[i]->m_position;
		float flDistSqr = vecTarget.DistToSqr( vecParticle );
		if ( flDistSqr < flClosestDistSqr )
		{
			flClosestDistSqr = flDistSqr;
			nClosestParticle = i;
		}
	}

	*pAttackPosition = m_pMainGroup->m_members[nClosestParticle]->m_position;
	m_pMainGroup->m_members[nClosestParticle]->m_flags |= BLOB_PARTICLE_TENTACLE_ROOT;
	*ppTentacleRootParticle = m_pMainGroup->m_members[nClosestParticle];
	return true;
}

//-----------------------------------------------------------------------------
// Sortfunc for regenerator
//-----------------------------------------------------------------------------
static Vector s_vecRegeneratorCenter;
static bool RegeneratorLessFunc( const CBlobRegenerator *left, const CBlobRegenerator *right )
{
	float flLDistSqr = s_vecRegeneratorCenter.DistToSqr( left->GetAbsOrigin() );
	float flRDistSqr = s_vecRegeneratorCenter.DistToSqr( right->GetAbsOrigin() );
	return ( flLDistSqr < flRDistSqr );
}


bool CNPC_Blob::SetPathToRegenerator()
{
	int nRegenCount = 0;
	int nCount = CBlobRegenerator::GetBlobRegeneratorCount();
	CBlobRegenerator **ppBlobRegenerator = (CBlobRegenerator**)stackalloc( nCount * sizeof(CBlobRegenerator*) );
	for ( int i = 0; i < nCount; ++i )
	{
		CBlobRegenerator *pRegenerator = CBlobRegenerator::GetBlobRegenerator( i );
		if ( pRegenerator && !pRegenerator->IsInactive() )
		{
			ppBlobRegenerator[nRegenCount++] = pRegenerator;
		}
	}

	s_vecRegeneratorCenter = GetAbsOrigin();
	std::make_heap( ppBlobRegenerator, ppBlobRegenerator + nRegenCount, RegeneratorLessFunc ); 
	std::sort_heap( ppBlobRegenerator, ppBlobRegenerator + nRegenCount, RegeneratorLessFunc ); 

	for ( int i = 0; i < nRegenCount; ++i )
	{
		AI_NavGoal_t goal( GOALTYPE_TARGETENT,
			AIN_DEF_ACTIVITY, AIN_HULL_TOLERANCE, AIN_DEF_FLAGS, ppBlobRegenerator[i] );
		SetTarget( ppBlobRegenerator[i] );
		if ( GetNavigator()->SetGoal( goal ) )
		{
			m_hRegeneratorTarget = ppBlobRegenerator[i];
			return true;
		}

		ppBlobRegenerator[i]->MarkInactive();
	}
	SetTarget( NULL );
	return false;
}

void CNPC_Blob::StartTask( const Task_t *pTask )
{
	Assert( !IsMarkedForDeletion() || ( m_blobParticles.Count() == 0 ) );
	if ( IsMarkedForDeletion() && ( m_blobParticles.Count() == 0 ) )
		return;

	// Handy debug variables:
	int taskID, schedID;
	const char *pTaskName, *pScheduleName;
	schedID			= GetCurSchedule() ? GetCurSchedule()->GetId() : 0;
	pScheduleName	= GetCurSchedule() ? GetCurSchedule()->GetName() : "none";
	taskID			= pTask ? pTask->iTask : 0;
	pTaskName		= TaskName( taskID );

	switch( pTask->iTask )
	{
	case TASK_FACE_REASONABLE:
	case TASK_FACE_SAVEPOSITION:
	case TASK_FACE_LASTPOSITION:
	case TASK_FACE_TARGET:
	case TASK_FACE_AWAY_FROM_SAVEPOSITION:
	case TASK_FACE_HINTNODE:
	case TASK_FACE_ENEMY:
	case TASK_FACE_PLAYER:
	case TASK_FACE_PATH:
	case TASK_FACE_IDEAL:
		// This only applies to NPCs that aren't spheres with omnidirectional eyesight.
		TaskComplete();
		break;

	case TASK_IGNORE_OLD_ENEMIES:
		// Need to override this for the blob so it doesn't clear the enemy
		if ( GetEnemy() && GetEnemyLastTimeSeen() < gpGlobals->curtime )
		{
			CBaseEntity *pNewEnemy = BestEnemy();

			if ( pNewEnemy != GetEnemy() )
			{
				if( pNewEnemy != NULL )
				{
					// New enemy! Clear the timers and set conditions.
					SetEnemy( pNewEnemy );
					SetState( NPC_STATE_COMBAT );
				}
				else if ( !GetEnemies()->HasFreeKnowledgeOf( GetEnemy() ) )
				{
					SetEnemy( NULL );
					ClearAttackConditions();
				}
			}
		}
		TaskComplete();
		break;


	case TASK_STOP_MOVING:
		InitParticleNavigation();
		m_vecLastMoveGoal = GetAbsOrigin();
		SetMoveGoal( GetAbsOrigin() );
		break;

	case TASK_BLOB_LEAP_BUILDUP:
		if ( !GetEnemy() )
		{
			TaskFail( FAIL_NO_ENEMY );
			break;
		}
		EmitSound( "Blob.AnnounceLeap" );
		m_flLeapStartTime = gpGlobals->curtime;
		m_vecLeapTarget = GetEnemy()->BodyTarget( GetAbsOrigin() );
		ChangeColor( Vector( 1, .2, .2 ), pTask->flTaskData );
		m_nNeighborForceMode = NEIGHBOR_FORCE_CONTRACT;
		break;

	case TASK_BLOB_LEAP:
		DoLeap();
		break;

	case TASK_BLOB_LEAP_COLLECT:
		m_bSuppressMoveTowardGoal = false;
		m_nNeighborForceMode = NEIGHBOR_FORCE_NORMAL;
		TaskComplete();
		break;
						 
	case TASK_BLOB_LEAP_FINISHED:
		m_bSuppressStragglerCreation = false;
		TaskComplete();
		break;

	case TASK_BLOB_SPAWN_REGENERATOR:
		CBlobRegenerator::CreateFromBlob( GetAbsOrigin() );
		EmitSound( "Blob.SpawnRegenerator" );
		for ( int i = 0; i < m_nActiveParticles; i++ )
		{
			if ( m_blobParticles[i]->m_flags & ( BLOB_PARTICLE_DEATH | BLOB_PARTICLE_STRAGGLER ) )
				continue;

			m_blobParticles[i]->m_support = 0.0f;
			m_blobParticles[i]->m_contact = 0.0f;
			m_blobParticles[i]->m_isInContact = false;

			m_vecPhysParticles[i]->ApplyForceCenter( Vector( 0, 0, 500 ) * m_vecPhysParticles[i]->GetMass() );
		}
		m_flNextSpawnRegeneratorTime = gpGlobals->curtime + RandomFloat( 10.0f, 15.0f );
		TaskComplete();
		break;

	case TASK_BLOB_TENTACLE_ATTACK:
		{
			if ( !GetEnemy() )
			{
				TaskFail( FAIL_NO_ENEMY );
				break;
			}

			CBlobParticle *pTentacleRootParticle;
			Vector vecMoveGoal;
			if ( !ComputeMeleeAttackPosition( &pTentacleRootParticle, &vecMoveGoal ) )
			{
				TaskFail( FAIL_NO_ROUTE );
				break;
			}
			SetMoveGoal( vecMoveGoal );
			InitParticleNavigation();
			m_vecLastMoveGoal = GetAbsOrigin();
			TentacleCreate( GetEnemy(), BLOB_TENTACLE_ACTION_SWIPE, pTentacleRootParticle, vec3_origin, 1.0f );
			// No more tentacles for a while after an attack
			m_fTentacleTimeout = gpGlobals->curtime + pTask->flTaskData + 3.0f;
		}
		break;

	case TASK_WAIT_FOR_MOVEMENT:
		InitParticleNavigation();
		m_vecLastMoveGoal = GetAbsOrigin();
		break;

	case TASK_WAIT_FOR_STALK_MOVEMENT:
		InitParticleNavigation();
		m_vecLastMoveGoal = GetAbsOrigin();
		break;

	case TASK_BLOB_GET_PATH_TO_REGENERATOR:
		if ( SetPathToRegenerator() )
		{
			TaskComplete();
		}
		else
		{
			TaskFail( FAIL_NO_ROUTE );
		}
		break;

	case TASK_BLOB_TOUCH_REGENERATOR:
		if ( !IsMostlyBrain() || !m_hRegeneratorTarget.Get() || m_hRegeneratorTarget->IsInactive() )
		{
			TaskComplete();
			break;
		}

		InitParticleNavigation();
		m_vecLastMoveGoal = GetAbsOrigin();
		SetMoveGoal( m_hRegeneratorTarget->GetAbsOrigin() );
		break;

	case TASK_BLOB_WAIT_FOR_UNSTUN:
		break;

	case TASK_BLOB_FLAIL:
		break;

	case TASK_BLOB_WAIT_UNTIL_ENEMY_LOS:
		break;

	case TASK_BLOB_ANNOUNCE_REGENERATE_PARTICLES:
		EmitSound( "Blob.Regenerate" );
		TaskComplete();
		break;

	case TASK_BLOB_REGENERATE_PARTICLES:
		{
			// Regenerate particles
			RegenerateParticles( BLOB_REGENERATION_AMOUNT );
		}
		break;

	case TASK_BLOB_GET_PATH_TO_PIPE:
		{
			if ( m_hPipeEntrance == NULL )
			{
				TaskFail(FAIL_NO_ROUTE_GOAL);
			}
			else 
			{
				AI_NavGoal_t goal( static_cast<const Vector&>( m_hPipeEntrance->GetAbsOrigin() ) );
				goal.pTarget = m_hPipeEntrance;
				GetNavigator()->SetGoal( goal );
			}
		}
		break;

	case TASK_BLOB_ENTER_PIPE:
		{
			if ( !m_hPipeEntrance || !m_hPipeExit )
			{
				TaskFail( "Pipe entrance/exit does not exist." );
				break;
			}

			if ( m_hPipeEntrance->m_bDisabled || m_hPipeEntrance->m_bDisabled )
			{
				TaskFail( "Pipe entrance/exit is disabled." );
				break;
			}

			AssertMsg( m_QueuedPipeParticles.Count() == 0, "Blob starting a pipe transfer while being transferred to untested." );

			//We'd like to preserve this blob's AI as best as possible, so what we're going to do is spawn a dummy blob to sit here and enter the pipe while we teleport to the exit and grow.
			m_fBlockMergeUntilTime = gpGlobals->curtime + 10.0f;
			CNPC_Blob *pEntranceBlob = CreateBlob( 0 );
			pEntranceBlob->m_fBlockMergeUntilTime = m_fBlockMergeUntilTime;
			pEntranceBlob->m_hPipeExitBlob = this;
			//pEntranceBlob->m_fPipeFlowSpeed = m_hPipeEntrance->m_fFlowRate; //MIN( m_hPipeEntrance->m_fFlowRate, m_hPipeExit->m_fFlowRate );
			//m_fPipeFlowSpeed = m_hPipeExit->m_fFlowRate; 
			//pEntranceBlob->m_fNextTransportTime = gpGlobals->curtime;
			//m_fNextTransportTime = gpGlobals->curtime + (m_hPipeEntrance->GetAbsOrigin() - m_hPipeExit->GetAbsOrigin()).Length() / (500.0f);
			pEntranceBlob->m_hPipeEntrance = m_hPipeEntrance;
			pEntranceBlob->m_hPipeExit = m_hPipeExit;
			pEntranceBlob->m_bIsPipeTransferBlob = true;
			m_hPipeExitBlob = pEntranceBlob;

			// Move the real blob to the exit and the transfer blob to the entrance
			pEntranceBlob->SetAbsOrigin( m_hPipeEntrance->GetAbsOrigin() );
			SetAbsOrigin( m_hPipeExit->GetAbsOrigin() );

			// Give the transfer blob control of all the particles
			while( m_blobGroups.Count() != 0 )
			{
				Assert( !m_blobGroups[0]->m_bIsTentacle ); // FIXME: ensure this never happens, or transfer the tentacle or destroy it
				TransferGroupToDifferentEntity( m_blobGroups[0], pEntranceBlob );
			}

			TaskComplete();
		}
		break;

	case TASK_BLOB_EXIT_PIPE:
		{
			EmitSound( "Blob.EnterPipe" );
			TaskComplete();
			break;
		}

	case TASK_BLOB_TRANSFER_THROUGH_PIPE:
		{
			if ( !m_hPipeEntrance || !m_hPipeExit )
			{
				TaskFail( "No pipe entrance/exit." );
				break;
			}

			m_hPipeEntrance->m_fFlowTimeStamp = gpGlobals->curtime; //TODO: think of a better way to manage old timestamps that doesn't step on toes 
			
			//m_fPipeFlowSpeed = MIN( m_hPipeEntrance->m_fFlowRate, m_hPipeExit->m_fFlowRate );
			//m_fNextTransportTime = gpGlobals->curtime;
			EmitSound( "Blob.EnterPipe" );
		}
		break;

	case TASK_FIND_HINTNODE:
		{
			// Overloaded because we search over a lesser distance.
			m_iDangerSounds = BuildFireSoundList();
			Hint_e nHintType = (Hint_e)( (int)pTask->flTaskData );
			int nSearchFlags = bits_HINT_NODE_NEAREST;
			float flRange = 512.0f;
			CAI_Hint *pHint = NULL;
			switch( nHintType )
			{
			case HINT_EP3_BLOB_SHAKE_POSITION:
				if ( m_BlobType != BLOB_TYPE_SPAWNER )
				{
					nSearchFlags = bits_HINT_NODE_NEAREST | bits_HINT_HAS_LOS_TO_PLAYER;
				}
				else
				{
					nSearchFlags = bits_HINT_NOT_CLOSE_TO_ENEMY | bits_HINT_HAS_NO_EYEPOSITION_LOS_TO_ENEMY;
				}
				break;
			case HINT_EP3_BLOB_FIRE_COVER_POSITION:
				break;
			case HINT_EP3_BLOB_BRAIN_COVER_POSITION:
				{										 
					// When looking for cover, preferentially look for regen hints
					pHint = CAI_HintManager::FindHint( this, HINT_EP3_BLOB_BRAIN_REGENERATE_POSITION, bits_HINT_NODE_NEAREST, 1024.0f );
					nSearchFlags = bits_HINT_HAS_NO_EYEPOSITION_LOS_TO_ENEMY | bits_HINT_NOT_CLOSE_TO_ENEMY;
					flRange = 1024.0f;
				}							  
				break;
			case HINT_EP3_BLOB_SPIT_POSITION:
				nSearchFlags = bits_HINT_HAS_NO_EYEPOSITION_LOS_TO_ENEMY | bits_HINT_NODE_NEAREST;
				flRange = 1024.0f;
				break;
			case HINT_EP3_BLOB_SPAWN_REGENERATOR_POSITION:
				nSearchFlags = bits_HINT_HAS_NO_EYEPOSITION_LOS_TO_ENEMY | bits_HINT_NOT_CLOSE_TO_ENEMY;
				flRange = 1024.0f;
				break;
			}
			if ( !pHint )
			{
				pHint = CAI_HintManager::FindHint( this, nHintType, nSearchFlags, flRange );
			}
			m_iDangerSounds = SOUNDLIST_EMPTY;
			SetHintNode( pHint );
			if ( !pHint )
			{
				TaskFail(FAIL_NO_HINT_NODE);
				break;
			}
			TaskComplete();
			break;
		}

	case TASK_BLOB_GET_PATH_TO_BUDDY_BLOB:
		{
			CNPC_Blob *pBuddyBlob = m_hBuddyBlob.Get();

			if ( !pBuddyBlob )
				pBuddyBlob = m_hAutoSplitBlob.Get();

			if ( pBuddyBlob == NULL )
			{
				TaskFail(FAIL_NO_ROUTE_GOAL);
			}
			else 
			{
				AI_NavGoal_t goal( static_cast<const Vector&>( pBuddyBlob->GetAbsOrigin() ) );
				goal.pTarget = pBuddyBlob;
				if ( GetNavigator()->SetGoal( goal ) == false )
				{
					TaskFail(FAIL_NO_ROUTE_GOAL);
					break;
				}
			}
		}
		break;

	case TASK_BLOB_SHAKE_OFF_BLOBS:
		m_nBlobsToShakeOff = RandomInt( 3, 8 );
		m_flNextBlobShakeSpawnTime = gpGlobals->curtime + RandomFloat( 1.0f, 1.5f );
		m_flLastBlobSpawnTime = m_flNextBlobShakeSpawnTime; 
		ChangeColor( Vector( 0.0f, 1.0f, 0.0f ), 1.0f );
		EmitSound( "Blob.AnnounceShake" );
		break;

	case TASK_BLOB_SPIT_START:
		// Cache spit parameters off; los checks will continue and will overwrite this data
		m_bSpitUseTentacle = m_bSaveSpitUseTentacle;
		m_hSpitTentaclePos = m_hSaveSpitTentaclePos;
		TaskComplete();
		break;

	case TASK_BLOB_SPIT_BUILDUP:
		EmitSound( "Blob.AnnounceSpit" );
		m_nSpitCount = RandomInt( 2, 3 );
		SetPhysParamState( "spit_buildup" );
		m_pSpitTentacle = NULL;
		if ( m_bSpitUseTentacle && m_hSpitTentaclePos.Get() )
		{
			m_pSpitTentacle = TentacleCreate( m_hSpitTentaclePos, BLOB_TENTACLE_ACTION_SPIT, 
				m_pMainGroup->m_root, vec3_origin, 1.0f );
		}
		else if ( !IsMostlyBrain() )
		{
			m_pSpitTentacle = TentacleCreate( this, BLOB_TENTACLE_ACTION_SPIT, 
				m_pMainGroup->m_root, Vector( 0, 0, BLOB_MAIN_SPIT_HEIGHT ) );
		}
		break;

	case TASK_BLOB_SPIT:
		break;

	case TASK_BLOB_SET_PHYS_PARAM_STATE:
		{
			SetPhysParamState( (int)pTask->flTaskData );
			TaskComplete();
		}
		break;

	case TASK_BLOB_TELEPORT_NPC_TO_PARTICLES:
		{
			TeleportNPCToParticles();
			TaskComplete();
		}
		break;

	case TASK_BLOB_GET_STALK_TARGET_PATH:
		{
			// Move to within striking distance of the target

			if ( m_hNextStalkTarget == NULL )
			{
				TaskFail( FAIL_NO_ENEMY );
				break;
			}
			m_hStalkTarget = m_hNextStalkTarget;

			// Are we stalking defensively or offensively?
			m_bStalkDefensive = !m_bInputStalkTarget;
			m_bInputStalkTarget = false;

			// Set our initial goal position (TASK_WAIT_FOR_STALK_MOVEMENT will adjust it)
			bool bDrawDebugLines = true;
			if ( !ComputeTargetPosition( m_vecStalkGoal, m_hStalkTarget, vec3_origin, bDrawDebugLines ) )
			{
				TaskFail( FAIL_NO_ROUTE_GOAL );
				m_hStalkTarget = NULL;
				break;
			}

			// Compute the path
			AI_NavGoal_t goal( m_vecStalkGoal );
			goal.pTarget = m_hStalkTarget;
			if ( GetNavigator()->SetGoal( goal ) == false )
			{
				TaskFail(FAIL_NO_ROUTE_GOAL);
				m_hStalkTarget = NULL;
				break;
			}

			// Stalking successfully initiated!
			m_eBlobStalkState = BLOB_STALK_FAR;
			m_fLastStalkGoalUpdate = 0; // Get StalkGoalUpdate() called immediately in TASK_WAIT_FOR_STALK_MOVEMENT

			// Keep track of our progress towards our target (so we can tell if we get stuck en-route)
			m_fMinDistToTarget = FLT_MAX;
			m_fLastNearerToTargetTime = gpGlobals->curtime;

			TaskComplete();
		}
		break;

	default:
		BaseClass::StartTask( pTask );
		break;
	}
}


//-----------------------------------------------------------------------------
// Used for navigation, has the blob reached its waypoint?
//-----------------------------------------------------------------------------
bool CNPC_Blob::HasBlobReachedPoint( const Vector &vecOrigin, float flTolerance ) const
{
	// The root of the largest group is the 'driver' for the blob, so test its position
	Vector vecBlobOrigin = GetAbsOrigin();
	float dist = vecOrigin.DistTo( vecBlobOrigin ) - m_flRadius;
	if ( dist > flTolerance )
		return false;
										  
	// Must have LOS to the target from the abs origin to reach it
	trace_t tr;
	AI_TraceLine( vecBlobOrigin, vecOrigin, GetAITraceMask(), this, HL2COLLISION_GROUP_BLOB, &tr );
	return ( tr.fraction == 1.0 );	
}


//-----------------------------------------------------------------------------
// Wrapper to set m_vecMoveGoal.. makes it easy to set breakpoints
//-----------------------------------------------------------------------------
void CNPC_Blob::SetMoveGoal( const Vector &vecGoal )
{
	m_vecMoveGoal = vecGoal;
}


//-----------------------------------------------------------------------------
// Is our current path blocked by fire?
//-----------------------------------------------------------------------------
bool CNPC_Blob::IsCurPathBlockedByFire()
{
	return CAI_LocalNavigator::IsSegmentBlockedByGlobalObstacles( GetAbsOrigin(), m_vecMoveGoal );
}


//-----------------------------------------------------------------------------
// Executes tasks
//-----------------------------------------------------------------------------
void CNPC_Blob::RunTask( const Task_t *pTask )
{
	Assert( !IsMarkedForDeletion() || ( m_blobParticles.Count() == 0 ) );
	if ( IsMarkedForDeletion() && ( m_blobParticles.Count() == 0 ) )
		return;

	// Handy debug variables:
	int taskID, schedID;
	const char *pTaskName, *pScheduleName;
	schedID			= GetCurSchedule() ? GetCurSchedule()->GetId() : 0;
	pScheduleName	= GetCurSchedule() ? GetCurSchedule()->GetName() : "none";
	taskID			= pTask ? pTask->iTask : 0;
	pTaskName		= TaskName( taskID );

	switch( pTask->iTask )
	{
	case TASK_STOP_MOVING:
		{
			// Avoid random-walk drift, but also avoid springing backwards if the particles are in motion when this task starts
			static const float driftThreshold = 2.0f;
			if ( GetAbsOrigin().DistTo( m_vecMoveGoal ) > driftThreshold*m_flRadius )
			{
				SetMoveGoal( GetAbsOrigin() );
			}
								   
			if ( gpGlobals->curtime > ( GetTimeTaskStarted() + pTask->flTaskData ) )
			{
				TaskComplete();
			}
		}
		break;

	case TASK_WAIT_FOR_MOVEMENT:
		{
			// If we touch the regenerator in the process of moving, we're done
			if ( IsCurSchedule( SCHED_BLOB_RUN_TO_REGENERATOR ) && !IsMostlyBrain() )
			{
				TaskComplete();
				break;
			}

			bool bTimeExpired = ( pTask->flTaskData != 0 && pTask->flTaskData < gpGlobals->curtime - GetTimeTaskStarted() );

			// If the navigator is done, we're done
			SetMoveGoal( GetNavigator()->GetCurWaypointPos() );
			if ( bTimeExpired || !GetNavigator()->IsGoalActive() || ( GetNavigator()->GetGoalType() == GOALTYPE_NONE ) )
			{
				TaskComplete();
				break;
			}


			// If we're within range of the goal, we're done
			if ( GetNavigator()->CurWaypointIsGoal() )
			{
				float flTolerance  = GetNavigator()->GetGoalTolerance();
				if ( HasBlobReachedPoint( GetNavigator()->GetGoalPos(), flTolerance ) )
				{
					TaskComplete();
					break;
				}
			}

			// Check to see if the current path is blocked by fire. If so, rebuild a path
			// and fail if it's not going to fly
			if ( IsCurPathBlockedByFire() )
			{
				AI_NavGoal_t goal( 
					GetNavigator()->GetGoalType(),
					GetNavigator()->GetGoalPos(),
					AIN_DEF_ACTIVITY,
					GetNavigator()->GetGoalTolerance(),
					GetNavigator()->GetGoalFlags() | AIN_NO_PATH_TASK_FAIL | AIN_NO_LOCAL_NAVIGATION,
					GetNavigator()->GetGoalTarget()
					);
				if ( !GetNavigator()->SetGoal( goal ) )
				{	
					m_vecLastMoveGoal = m_vecMoveGoal = GetAbsOrigin();
					TaskFail( FAIL_NO_ROUTE_BLOCKED ); 
					break;
				}
				
				// Reset particle paths to account for the new path
				InitParticleNavigation();
			}

			// FIXME: we need to write and use CAI_BlobMotor, since CAI_Motor was removed
			//  - this code has broken 'crawl' navigation support, we need to use CheckStep( NAV_CRAWL ) to move m_vecMoveGoal up walls
			//  - this code doesn't limit speed (we should keep m_vecMoveGoal a fixed distance ahead of the blob unless it is exceeding the desired speed)
			//    we also need to change the physics (balance forces+viscosity) such that the blob particles tend to move at the desired NPC speed

			// Advance waypoints as we pass them (our origin is driven by particle simulation, and it acts as the motor)
			if ( !GetNavigator()->CurWaypointIsGoal() && HasBlobReachedPoint( m_vecMoveGoal, 64 ) )
			{
				m_vecLastMoveGoal = m_vecMoveGoal;
				GetNavigator()->AdvancePath();
				SetMoveGoal( GetNavigator()->GetCurWaypointPos() );
			}

			if ( m_pMainGroup )
			{
				float flSpeedSqr = m_vecLastPosition.DistToSqr( m_pMainGroup->m_root->m_position ) / physenv->GetSimulationTimestep();
				m_vecLastPosition = m_pMainGroup->m_root->m_position;
				bool bIsMoveBlocked = ( flSpeedSqr < 30.0f );
				if ( bIsMoveBlocked )
				{
					if ( !m_bIsMoveBlocked )
					{
						m_flFirstMoveBlockTime = gpGlobals->curtime;
						m_bPlayedBlockedSound = false;
					}
					else if ( gpGlobals->curtime - m_flFirstMoveBlockTime > 0.25f )
					{
						if ( !m_bPlayedBlockedSound )
						{
							DevMsg( "Blob nav stuck!\n" );
							if ( developer.GetInt() )
							{
								EmitSound( "Blob.NavigationBusted" );
								NDebugOverlay::Box( GetAbsOrigin(), Vector( -10, -10, -10 ), Vector( 10, 10, 10 ), 255, 0, 0, 0, 30 );
							}
							m_bPlayedBlockedSound = true;
						}
						TaskFail( FAIL_NO_ROUTE_BLOCKED ); 
						break;
					}
				}
				m_bIsMoveBlocked = bIsMoveBlocked;
			}
		}
		break;

	case TASK_BLOB_TOUCH_REGENERATOR:
		if ( !IsMostlyBrain() || !m_hRegeneratorTarget.Get() || m_hRegeneratorTarget->IsInactive() )
		{
			TaskComplete();
			break;
		}
		break;

	case TASK_WAIT_FOR_STALK_MOVEMENT:
		{
			StalkUpdateState();

			if ( m_hStalkTarget == NULL )
			{
				TaskFail( FAIL_NO_ENEMY );
				break;
			}

			if ( m_vecStalkGoal != GetNavigator()->GetGoalPos() )
			{
				// Update the navigator with the new goal
				//AI_NavGoal_t goal( m_vecStalkGoal );
				//goal.pTarget = m_hStalkTarget;
				//if ( GetNavigator()->SetGoal( goal ) == false )
				//
				// FIXME:
				// From Tom: "Derive from CAI_FollowBehavior, adding the [extrapolation] to the follow goal point"
				//           "You'll probably have to define your own 'formation'. Follow is not entirely flexibly data driven"
				// - check and see if that deals with the looping paths issue (new starting nodes being ones we've already processed recently), I'm not sure it does
				// - I think in fact the looping problem is to do with mis-placed nodes, ones which SHOULD connect but don't (implying an obstalce that only appears to be there from one angle)
				// - anyway, if that code does what StalkGoalUpdate does (with some minor changes in a derived class, or just a new 'formation') probably best to refactor to use it, after the milestone
				//
				// This seems to work (not noticeably better, though, since it also throws away existing waypoints - should probably revert it, else need to modify some stuff in StalkGoalUpdate)
				if ( !GetNavigator()->UpdateGoalPos( m_vecStalkGoal ) )
				{
					TaskFail( FAIL_NO_ROUTE_GOAL);
					m_hStalkTarget = NULL;
					break;
				}
			}

			// FIXME: implement proper lunging, change physics so the particles move faster (and preferably more coherently)
			static const float moveGoalDistanceNormal = 100, moveGoalDistanceLunge = 200;
			float moveGoalDistance = ( m_eBlobStalkState == BLOB_STALK_LUNGING ) ? moveGoalDistanceLunge : moveGoalDistanceNormal;

			// Advance the path if need be (and if not yet within the desired tolerance of the target)
			float distanceToCurWaypoint = GetAbsOrigin().DistTo( GetNavigator()->GetCurWaypointPos() );
			float distanceToGoal = GetNavigator()->GetPathDistanceToGoal();

			// FIXME: Endless looping can occur here
			//   [it's due to configurations where a node closer to the target doesn't connect to any other nodes closer to the target (only ones further away), so if
			//    the blob is near that node when we update our path, it becomes the first node but the blob then has to back-track due given its available connections]
			// We fix it here by skipping nodes we had already passed on our previous path, but we should
			// probably look into our node connection code (and at least spew if these error cases are detected)
			AI_Waypoint_t *pCurWaypoint = GetNavigator()->GetPath()->GetCurWaypoint()->GetNext();
			while( pCurWaypoint )
			{
				if ( pCurWaypoint == m_PrevBestWaypoint )
				{
					// Skip to this waypoint
					while ( GetNavigator()->GetPath()->GetCurWaypoint() != m_PrevBestWaypoint )
					{
						GetNavigator()->AdvancePath();
					}
					break;
				}
				pCurWaypoint = pCurWaypoint->GetNext();
			}

			if ( distanceToCurWaypoint <= moveGoalDistance )
			{
				GetNavigator()->AdvancePath();	
				distanceToCurWaypoint = GetAbsOrigin().DistTo( GetNavigator()->GetCurWaypointPos() );
				distanceToGoal = GetNavigator()->GetPathDistanceToGoal();
			}

			// Update the current 'best' waypoint for the above looping fix
			m_PrevBestWaypoint = GetNavigator()->GetPath()->GetCurWaypoint();

// FIXME: we need to write and use CAI_BlobMotor, since CAI_Motor was removed (see TASK_WAIT_FOR_MOVEMENT above)

			// Advance m_vecMoveGoal to remain 'moveGoalDistance' units ahead of the blob
			// FIXME: m_vecMoveGoal should really stick to the path (except in cases where it needlessly doubles back on itself)
			Vector waypointPos = GetNavigator()->GetCurWaypointPos();
			Vector delta = waypointPos - GetAbsOrigin();
			moveGoalDistance = MIN( moveGoalDistance, distanceToGoal );
			SetMoveGoal( GetAbsOrigin() + moveGoalDistance*delta.Normalized() );

			// Manage tentacles (this may override m_vecMoveGoal)
			StalkUpdateTentacles();
		}
		break;

	case TASK_BLOB_FLAIL:
		{
			// Spawn tentacles with random nearby targets, until the brain is no longer exposed

			if ( !IsMostlyBrain() )
			{
				m_fStunStartTime = 0;
				TaskComplete();
			}
			else
			{
				SetMoveGoal( GetAbsOrigin() );
				m_fStunStartTime = gpGlobals->curtime + BLOB_STUN_TIMEOUT;

				int numActiveTentacles = 0;
				for ( int i = 0; i < BLOB_MAX_TENTACLES; i++ )
				{
					numActiveTentacles += ( m_tentacles[i].m_nPhase == BLOB_TENTACLE_STATE_NONE ) ? 0 : 1;
				}

				// Create a new tentacle if it's time
				if ( numActiveTentacles < sv_blob_brain_flail_if_exposed.GetInt() )
				{
					// FIXME: create a new tentacle 'flail' phase, have tentacles flail from side to side
					//        (store start+end+current tentacle tip positions - use 'current' to improve 'recede' too)

					static int d = 0;
					Vector targetOffset = 0.5f*sv_blob_tentacle_length.GetFloat()*Vector( sinf( d*2.5f ), cosf( d*2.5f ), 0 );
					float speedMultiplier = 2; // Make fast tentacles
					TentacleCreate( this, BLOB_TENTACLE_ACTION_ATTACK_FRONT, NULL, targetOffset, speedMultiplier );
					m_fTentacleTimeout = gpGlobals->curtime + ( sv_blob_tentacle_create_timeout.GetFloat() / speedMultiplier );
					d++;
				}
			}
		}
		break;

	case TASK_BLOB_EXIT_PIPE:
		TaskComplete();
		break;

	case TASK_BLOB_WAIT_FOR_UNSTUN:
		if ( !IsStunned() )
		{
			TaskComplete();
		}
		break;

	case TASK_BLOB_TENTACLE_ATTACK:
		{
			if ( !GetEnemy() )
			{
				TaskComplete( );
				break;
			}

			bool bTimeExpired = ( pTask->flTaskData < gpGlobals->curtime - GetTimeTaskStarted() );
			if ( bTimeExpired )
			{
				TaskComplete();
				break;
			}
		}
		break;

	case TASK_BLOB_WAIT_UNTIL_ENEMY_LOS:
		{
			bool bTimeExpired = ( pTask->flTaskData != 0 && pTask->flTaskData < gpGlobals->curtime - GetTimeTaskStarted() );
			if ( bTimeExpired )
			{
				TaskComplete();
				break;
			}
			CBaseCombatCharacter *pEnemy = ToBaseCombatCharacter( GetEnemy() );
			if ( !pEnemy )
			{
				TaskComplete();
				break;
			}
			if ( pEnemy->FVisible( this ) )
			{
				Vector vTargetDir = GetAbsOrigin() - pEnemy->EyePosition();
				VectorNormalize( vTargetDir );

				float fDotPr = DotProduct( pEnemy->EyeDirection3D() ,vTargetDir );
				if ( fDotPr > 0.866F )
				{
					TaskComplete();
					break;
				}
			}
		}
		break;

	case TASK_BLOB_REGENERATE_PARTICLES:
		{
			// Wait for regenerating particles to become fully grown (or time out)
			int numRegenParticles = 0;
			for ( int i = 0; i < m_nActiveParticles; i++ )
			{
				if ( m_blobParticles[ i ]->m_flags & BLOB_PARTICLE_BIRTH )
					numRegenParticles++;
			}

			bool bTimeExpired = ( pTask->flTaskData != 0 && ( pTask->flTaskData < ( gpGlobals->curtime - GetTimeTaskStarted() ) ) );
			if ( ( numRegenParticles == 0 ) || bTimeExpired )
			{
				TaskComplete();
			}
		}
		break;

	case TASK_BLOB_TRANSFER_THROUGH_PIPE:
		{
			if ( !m_hPipeEntrance || !m_hPipeExit )
			{
				AssertMsg( 0, "Transfer blob lost it's pipe pointers! We should probably do some cleanup here!\n" );
				TaskFail( "Pipe entrance/exit does not exist." );
				break;
			}

			CNPC_Blob *pExitBlob = m_hPipeExitBlob.Get();
			if( !pExitBlob )
			{
				//something bad, but fixable
				float fClosestDistSqr = FLT_MAX;
				Vector vTestPos = m_hPipeExit->GetAbsOrigin();
				for( int i = 0; i != s_BlobInstances.Count(); ++i )
				{
					CNPC_Blob *pTransferBlob = dynamic_cast<CNPC_Blob *>(s_BlobInstances[i]);
					if( (pTransferBlob == NULL) || (pTransferBlob == this) )
						continue;

					float fDistSqr = (pTransferBlob->GetAbsOrigin() - vTestPos).LengthSqr();
					if( fDistSqr < fClosestDistSqr )
					{
						pExitBlob = pTransferBlob;
						fClosestDistSqr = fDistSqr;
					}
				}

				if( pExitBlob )
				{
					m_hPipeExitBlob = pExitBlob;
				}
				else
				{
					AssertMsg( 0, "Transfer blob lost it's exit blob pointer!\n" );
					TaskFail( "Exit blob doesn't exist." );
					break;
				}
			}

			if( m_hPipeExitBlob == this )
			{
				Assert( 0 ); //TODO: Fix it.
				TaskFail( "Blob somehow started piping to itself" );
			}

			if( m_nActiveParticles != 0 )
			{
				float fTimeStamp = m_hPipeEntrance->m_fFlowTimeStamp;
				//flows at 500 inches/sec in a straight line while in a pipe
				float fTravelTime = (m_hPipeEntrance->GetAbsOrigin() - m_hPipeExit->GetAbsOrigin()).Length() / (500.0f);
				float fOOFlowRate = 1.0f / m_hPipeEntrance->m_fFlowRate;
				
				int iTransport = floor((gpGlobals->curtime - fTimeStamp) * m_hPipeEntrance->m_fFlowRate);
				
				if( iTransport > m_nActiveParticles )
				{
					iTransport = m_nActiveParticles;
				}
				
				RemoveParticles( iTransport );

				Assert(m_hPipeExit);
				BlobPipeParticle_t queuedParticle;
				queuedParticle.hPipeExit = m_hPipeExit;
				queuedParticle.fArrivalTime = fTimeStamp + fTravelTime;

				for( int i = 0; i != iTransport; ++i )
				{
					pExitBlob->m_QueuedPipeParticles.AddToTail( queuedParticle );
					queuedParticle.fArrivalTime += fOOFlowRate;
					fTimeStamp += fOOFlowRate;
				}
				
				m_hPipeEntrance->m_fFlowTimeStamp = fTimeStamp;
			}				
			

			if( (m_nActiveParticles == 0) && (m_QueuedPipeParticles.Count() == 0) )
			{
				m_hPipeExitBlob = NULL;
				m_hPipeEntrance = NULL;
				m_hPipeExit = NULL;
				//TaskComplete(); //doing this before a UTIL_Remove() is a bad idea

				//Msg( "Transfer Complete\n" );
				DebugFullSanityCheck();
				UTIL_Remove( this );
			}
		}
		break;										    

	case TASK_BLOB_SHAKE_OFF_BLOBS:
		if ( m_flNextBlobShakeSpawnTime <= gpGlobals->curtime ) 
		{
			SpawnTinyBlob( m_pMainGroup );
			m_flLastBlobSpawnTime = gpGlobals->curtime;
			m_flNextBlobShakeSpawnTime = gpGlobals->curtime + 0.1f;
			if ( --m_nBlobsToShakeOff <= 0 )
			{
				m_flNextShakeBlobTime = gpGlobals->curtime + RandomFloat( 5.0f, 7.0f );
				m_OnBlobsShakenOff.FireOutput( this, this );
				TaskComplete();
				break;
			}
		}
		break;
	
	case TASK_BLOB_LEAP_BUILDUP:
		{
			if ( !GetEnemy() )
			{
				TaskFail( FAIL_NO_ENEMY );
				break;
			}

			// Update leap target
			trace_t tr;
			UTIL_TraceHull( GetAbsOrigin(), GetEnemy()->BodyTarget( GetAbsOrigin() ), 
				Vector( -m_flRadius, -m_flRadius, -m_flRadius ),
				Vector( m_flRadius, m_flRadius, m_flRadius ),
				GetAITraceMask_BrushOnly(), GetEnemy(), HL2COLLISION_GROUP_BLOB, &tr );

			if ( tr.fraction == 1.0f )
			{
				m_vecLeapTarget = GetEnemy()->BodyTarget( GetAbsOrigin() );
			}

			bool bTimeExpired = ( pTask->flTaskData < gpGlobals->curtime - GetTimeTaskStarted() );
			if ( bTimeExpired )
			{
				TaskComplete();
			}
		}
		break;

	case TASK_BLOB_LEAP:
		{
			bool bTimeExpired = ( pTask->flTaskData < gpGlobals->curtime - GetTimeTaskStarted() );
			if ( bTimeExpired || m_bLeapCompleted )
			{
				m_vecLastMoveGoal = GetAbsOrigin();
				SetMoveGoal( GetAbsOrigin() );
				m_bLeapCompleted = true;
				m_nNeighborForceMode = NEIGHBOR_FORCE_EXPLODE;
				m_flNextLeapTime = gpGlobals->curtime + RandomFloat( 5.0f, 7.0f );
				TaskComplete();
			}
		}
		break;

	case TASK_BLOB_SPIT_BUILDUP:
		{
			float flWaitTime = pTask->flTaskData;
			if ( IsMostlyBrain() )
			{
				flWaitTime *= 0.33f;
			}
			bool bTimeExpired = ( pTask->flTaskData != 0 && flWaitTime < gpGlobals->curtime - GetTimeTaskStarted() );
			if ( bTimeExpired )
			{
				if ( !m_pSpitTentacle || ( m_pSpitTentacle->m_nPhase != BLOB_TENTACLE_STATE_SPIT_EXTRUDE ) )
				{
					TaskComplete();
				}
			}
		}
		break;

	case TASK_BLOB_SPIT:
		if ( gpGlobals->curtime >= GetNextAttack() )
		{
			bool bDone = !DoSpit();
			if ( ( --m_nSpitCount <= 0 ) || bDone )
			{					 
				SetPhysParamState( "default" );
				TaskComplete();
				if ( m_pSpitTentacle && ( m_pSpitTentacle->m_nPhase == BLOB_TENTACLE_STATE_SPIT_HOLD ) )
				{
					// Force transition to the next state next update
					m_pSpitTentacle->m_fPhaseDuration = 0.0f;
				}
				m_pSpitTentacle = NULL;
				if ( !IsMostlyBrain() )
				{
					SetNextAttack( gpGlobals->curtime + random->RandomFloat( 2.0f, 3.0f ) );
				}
				else
				{
					SetNextAttack( gpGlobals->curtime + random->RandomFloat( 0.5f, 1.0f ) );
				}
			}
		}
		break;

	default:
		BaseClass::RunTask( pTask );
		break;
	}
}


//-----------------------------------------------------------------------------
// Enters a stunned state after being damaged
//-----------------------------------------------------------------------------
void CNPC_Blob::EnterStunnedState( void )
{
	// After certain types of damage, we act stunned (slowly gather particles together, but nothing else)
	m_fStunStartTime = gpGlobals->curtime;
	SetMoveGoal( GetAbsOrigin() );

	// Get rid of all tentacles
	TentaclesInterrupt();
}

float CNPC_Blob::GetStunFraction() const
{
	float flFraction = ( ( m_fStunStartTime + BLOB_STUN_TIMEOUT ) - gpGlobals->curtime ) / BLOB_STUN_TIMEOUT; 
	flFraction = clamp( flFraction, 0.0f, 1.0f );
	return flFraction;
}

bool CNPC_Blob::IsStunned( ) const
{
	return ( ( m_fStunStartTime + BLOB_STUN_TIMEOUT ) >= gpGlobals->curtime );
}


//-----------------------------------------------------------------------------
// Apply blast force to particles
//-----------------------------------------------------------------------------
void CNPC_Blob::ApplyBlastForceToParticles( const Vector &vecEpicenter, float flForceSize, float flForceRadius )
{
	Vector groupDir = GetAbsOrigin() - vecEpicenter;
	float flInvForceRadius = ( flForceRadius != 0.0f ) ? 1.0f / flForceRadius : 1.0f;

	// Apply forces on a per-particle basis so it looks right
	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		CBlobParticle *pParticle = m_blobParticles[ i ];
		if ( BLOBPARTICLERADIUS( m_iParticlePositionIndex[i] ) <= 0.0f )
			continue;

		// 'Brains' aren't affected, so explosions (and repulsion forces) leave the brains exposed!
		if ( pParticle->m_flags & BLOB_PARTICLE_BRAIN )
			continue;

		Vector delta = BLOBPARTICLEPOSITION( m_iParticlePositionIndex[i] ) - vecEpicenter;
		float flDistanceSqr = delta.Length() * flInvForceRadius;
		flDistanceSqr *= flDistanceSqr;
		if ( flDistanceSqr >= 1.0f )
			continue;

		// Reduce the spread, so particles don't scatter so much
		delta = VectorLerp( delta, groupDir, 0.9f ).Normalized();

		float flAttenuation = 1.0f - flDistanceSqr;
		m_vecPhysParticles[i]->ApplyForceCenter( flAttenuation*flForceSize*delta );
	}

}


//-----------------------------------------------------------------------------
// Apply damage force
//-----------------------------------------------------------------------------
void CNPC_Blob::ApplyDamageForce( const CTakeDamageInfo &info )
{
	// Apply force to the blob particles when taking damage

	// Compute radius of effect based on the magnitude of the damage force
	// FIXME: this needs a better algorithm for radiating the force
	Vector	forceDir	= info.GetDamageForce();
	float	forceSize	= 0.6f*forceDir.NormalizeInPlace();
	float	forceRadius	= 10*m_flRadius*MAX( 1.0, powf( ( forceSize / 1000 ), 0.25f ) );

	if ( ( info.GetDamageType() & DMG_BLAST ) == 0 )
	{
		// Apply non-blast damage in the direction of the force
		for ( int i = 0; i < m_nActiveParticles; i++ )
		{
			if ( BLOBPARTICLERADIUS( m_iParticlePositionIndex[i] ) > 0.0f )
			{
				// FIXME: this should apply force in a cylinder, not a sphere - 
				// try reducing the distance vector in the damage direction
				Vector delta = BLOBPARTICLEPOSITION( m_iParticlePositionIndex[i] ) - info.GetDamagePosition();
				float distance = delta.Length() / forceRadius;
				if ( distance < 1 )
				{
					float attenuation = 1.0f - distance*distance;

					// The flamethrower should expose the brain, so reduce the force it applies to brain particles
					if ( ( info.GetDamageType() & DMG_BURN ) && ( m_blobParticles[ i ]->m_flags & BLOB_PARTICLE_BRAIN ) )
						attenuation *= 0.1f;

					m_vecPhysParticles[i]->ApplyForceOffset( attenuation*forceSize*forceDir, info.GetDamagePosition() );
				}
			}
		}
		return;
	}

	// Reduce the force so that grenades/rockets aren't so destructive
	forceSize *= 0.35f;

	ApplyBlastForceToParticles( info.GetDamagePosition(), forceSize, forceRadius );
}

Vector CNPC_Blob::BodyTarget( const Vector &posSrc, bool bNoisy )
{
	// If we have a brain, return that, otherwise fall through to the base implementation
	int iShortest = -1;
	float flShortestDistSqr = FLT_MAX;
	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		if ( ( m_blobParticles[i]->m_flags & BLOB_PARTICLE_BRAIN ) &&
			 ( BLOBPARTICLERADIUS( m_iParticlePositionIndex[i] ) > 0.0f ) )
		{
			float flDistSqr = ( posSrc - BLOBPARTICLEPOSITION( m_iParticlePositionIndex[i] ) ).LengthSqr();
			if ( flDistSqr < flShortestDistSqr )
			{
				iShortest = i;
				flShortestDistSqr = flDistSqr;
			}
		}
	}

	if ( iShortest != -1 )
		return BLOBPARTICLEPOSITION( m_iParticlePositionIndex[ iShortest ] );

	// NDebugOverlay::Box(m_body[iShortest].vecPos, Vector( -2, -2, -2 ), Vector( 2, 2, 2 ), 0, 0, 255, 20, .1);

	return BaseClass::BodyTarget( posSrc, bNoisy );
}


#define DAMAGE_PER_PARTICLE_REMOVAL 30

//-----------------------------------------------------------------------------
// Defines the pain sound 
//-----------------------------------------------------------------------------
void CNPC_Blob::PainSound( const CTakeDamageInfo &info )
{
	bool bIsHeavyDamage = ( info.GetDamage() > 40 );
	if ( !bIsHeavyDamage && ( ( gpGlobals->curtime - m_flLastPainSoundTime ) < 0.5f ) )
		return;
	m_flLastPainSoundTime = gpGlobals->curtime;

	if ( IsMostlyBrain() )
	{
		if ( bIsHeavyDamage )
		{
			EmitSound( "Blob.BrainPainHeavy" );
		}
		else
		{
			EmitSound( "Blob.BrainPain" );
		}
	}
	else
	{
		EmitSound( "Blob.Pain" );
	}
}


//-----------------------------------------------------------------------------
// Computes the number of particle groups in addition to the brain we have
//-----------------------------------------------------------------------------
int CNPC_Blob::ComputeDamageGroupCount() const
{
	float flHealthFraction = m_flArmorHealth / m_flMaxArmorHealth;
	return (int)( flHealthFraction / BLOB_DAMAGE_AMOUNT );
}


//-----------------------------------------------------------------------------
// When damage is taken, and it's time to emit particles
//-----------------------------------------------------------------------------
#define BLOB_DAMAGE_FORCE 10000.0f
#define BLOB_DAMAGE_FORCE_UP_AMOUNT 0.6f
#define BLOB_DAMAGE_FORCE_AWAY_AMOUNT 0.4f

void CNPC_Blob::EmitNonBrainParticles( const CTakeDamageInfo &info )
{
	// Mark particles taking damage
	int nDamageGroupCount = ComputeDamageGroupCount();
	int nNormalParticleCount = GetNormalParticleCount();
	int nTargetParticleCount = ( m_nMaxParticleCount - m_nBrainParticleCount ) * nDamageGroupCount * BLOB_DAMAGE_AMOUNT;
	int nParticlesToRemove = nNormalParticleCount - nTargetParticleCount;
	if ( nParticlesToRemove <= 0 )
		return;

	bool bIsBlastDamage = ( info.GetDamageType() & DMG_BLAST ) != 0;
	int nCount = m_pMainGroup->m_members.Count();
	Vector vecCenter, vecForce, vecAway;
	ComputeMainGroupCenter( &vecCenter );
	if ( info.GetInflictor() )
		vecAway = ( vecCenter - info.GetInflictor()->GetAbsOrigin() ).Normalized();
	for ( int i = 0; i < nCount; i++ )
	{
		// Don't destroy the root particle; we need it for clean navigation.
		// The constaints holding the brain together result in noisy movement.
		CBlobParticle *pParticle = m_blobParticles[i];
		if ( pParticle->m_type == ROOT_PARTICLE )
			continue;

		if ( nDamageGroupCount != 0 )
		{
			if ( ( pParticle->m_flags & BLOB_PARTICLE_TENTACLE_ROOT ) != 0 )
				continue;
		}
		if ( ( pParticle->m_flags & ( BLOB_PARTICLE_BRAIN | BLOB_PARTICLE_DEATH ) ) != 0 )
			continue;

		pParticle->m_flags |= BLOB_PARTICLE_DEATH;
		pParticle->m_flDeathTime = gpGlobals->curtime;
		pParticle->m_support = 0.0f;
		pParticle->m_contact = 0.0f;
		if ( !bIsBlastDamage )
		{
			// Blast damage will automatically apply particles forces, so skip forces here 
			// if it's blast damage.
			// Blast outward from the center; add some upward motion also
			VectorSubtract( pParticle->m_position, vecCenter, vecForce ); 
			VectorNormalize( vecForce );
			vecForce = VectorLerp( vecForce, Vector( 0, 0, 1 ), BLOB_DAMAGE_FORCE_UP_AMOUNT ).Normalized();
			if ( info.GetInflictor() )
				vecForce = VectorLerp( vecForce, vecAway, BLOB_DAMAGE_FORCE_AWAY_AMOUNT ).Normalized();
			vecForce *= BLOB_DAMAGE_FORCE;
			pParticle->m_vphysParticle->ApplyForceOffset( vecForce, pParticle->m_position );
		}

		if ( --nParticlesToRemove <= 0 )
			break;
	}

	if ( nDamageGroupCount == 0 )
	{
		m_flNextRegenerateTime = gpGlobals->curtime + BLOB_REGENERATION_TIME;

		// Destroy tentacle groups/particles if we should be rid of all particles
		for ( int i = 0; i < BLOB_MAX_TENTACLES; i++ )
		{
			TentacleDestroy( &m_tentacles[i] );
		}
	}
}


//-----------------------------------------------------------------------------
// Only blast damage is heavy damage
//-----------------------------------------------------------------------------
bool CNPC_Blob::IsHeavyDamage( const CTakeDamageInfo &info )
{
	return ( ( info.GetDamageType() & DMG_BLAST ) != 0 ) && ( info.GetDamage() > 20 );
}


//-----------------------------------------------------------------------------
// Disables nearby cover hints
//-----------------------------------------------------------------------------
void CNPC_Blob::DisableNearbyBrainHints( float flRadius, float flDisableTime )
{
	if ( !IsMostlyBrain() )
		return;

	CHintCriteria hintCriteria;			
	hintCriteria.SetHintType( HINT_EP3_BLOB_BRAIN_COVER_POSITION );

	Vector vecHintPos;
	const Vector &vecOrigin = GetAbsOrigin();
	CUtlVector< CAI_Hint * > hints;
	CAI_HintManager::FindAllHints( this, vecOrigin, hintCriteria, &hints );
	int nCount = hints.Count();
	flRadius *= flRadius;
	for ( int i = 0; i < nCount; ++i )
	{
		hints[i]->GetPosition( this, &vecHintPos );
		if ( vecOrigin.DistToSqr( vecHintPos ) >= flRadius )
			continue;

		hints[i]->DisableForSeconds( flDisableTime );
	}
}


//-----------------------------------------------------------------------------
// Deals with damage taking
//-----------------------------------------------------------------------------
int CNPC_Blob::OnTakeDamage_Alive( const CTakeDamageInfo &info ) 
{
	CTakeDamageInfo damageInfo = info;

	CBaseGrenade *pGrenade = dynamic_cast<CBaseGrenade *>( damageInfo.GetInflictor() );
	if ( ( damageInfo.GetDamageType() == DMG_CRUSH ) && pGrenade )
	{
		// CGrenadeFrag::VPhysicsUpdate() goes to heroic lengths to bounce off things that don't *want* to
		// collide with it, so in order to capture grenades we need this MASSIVELY PAINFUL HACK... enjoy.
		return 0;
	}

	// Blobs don't take crushing or acid damage
	if ( damageInfo.GetDamageType() == DMG_CRUSH || damageInfo.GetDamageType() == DMG_ACID )
	{
		damageInfo.SetDamage( 0.0f );
	}

	// Blast damage blows away all particles that aren't the brain
	if ( damageInfo.GetDamageType() & DMG_BLAST )
	{
		float flMinDamage = GetHealth() - sv_blob_brain_health.GetInt();
		if ( damageInfo.GetDamage() < flMinDamage )
		{
			damageInfo.SetDamage( flMinDamage );
		}
	}

	// Buckshot doesn't deal as much damage to us
	if ( damageInfo.GetDamageType() & DMG_BUCKSHOT )
	{
		if ( !IsMostlyBrain() )
		{
			damageInfo.ScaleDamage( 0.25f );
		}
		else
		{
			damageInfo.ScaleDamage( 0.5f );
		}
	}

	// Apply force to the blob particles from the damage
	ApplyDamageForce( damageInfo );

	int iRetVal = BaseClass::OnTakeDamage_Alive( damageInfo );
	if ( iRetVal <= 0 )
		return iRetVal;

	if ( damageInfo.GetDamage() == 0.0f )
		return iRetVal;

	DisableNearbyBrainHints( 128.0f, 3.0f );

	// Can't regenerate after damage
	m_flNextRegenerateTime = gpGlobals->curtime + BLOB_REGENERATION_TIME;

	if ( damageInfo.GetDamageType() & DMG_BURN )
	{
		// The blob is afraid of flames, so interrupt schedules when it's being burninated
		SetCondition( COND_SEE_FEAR );
	}

	if ( !IsMostlyBrain() )
	{
		float flPrevArmor = m_flArmorHealth;
		m_flArmorHealth -= damageInfo.GetDamage();
		float flHealthThreshhold = 0.75f * GetMaxHealth();
		if ( ( flPrevArmor >= flHealthThreshhold ) && ( m_flArmorHealth < flHealthThreshhold ) )
		{
			m_OnThreeQuartersArmor.FireOutput( this, this );
		}
	}

	// The brain is only damageable if exposed, and only takes certain kinds of damage
	if ( gpGlobals->curtime > ( m_flLastDamageEffectTime + 0.5f ) )
	{
		m_flLastDamageEffectTime = gpGlobals->curtime;
		if ( damageInfo.GetDamage() > 40 )
		{
			DispatchParticleEffect( "AntlionGib", damageInfo.GetDamagePosition(), QAngle( 0.0f, 0.0f, 0.0f ) );
		}
		else
		{
			DispatchParticleEffect( "blood_impact_antlion_worker_01", damageInfo.GetDamagePosition(), QAngle( 0.0f, 0.0f, 0.0f ) );
		}
	}

	m_flLastLeapTime = gpGlobals->curtime + 2.0f;
	EmitNonBrainParticles( damageInfo );
	return iRetVal;
}

void CNPC_Blob::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );

	// Tell any shape targets we're touching that we're no longer doing so
	CShapeTarget::NotifyEndDeliberateTouchByBlob( this, NULL);

	EmitSound( "NPC_Strider.Death" );

	TentaclesInterrupt();

	DispatchParticleEffect( "striderbuster_break", GetAbsOrigin(), vec3_angle );

	// Go away
	DebugFullSanityCheck();

	
	if( m_QueuedPipeParticles.Count() != 0 )
	{
		//find some other blob to give our pipe particles to
		CNPC_Blob *pClosestBlob = NULL;
		float fClosestBlobDistSqr = FLT_MAX;
		
		Vector vTestPos = GetAbsOrigin();
		for( int i = 0; i != s_BlobInstances.Count(); ++i )
		{
			if( s_BlobInstances[i] == this )
				continue;

			CNPC_Blob *pTransferBlob = dynamic_cast<CNPC_Blob *>(s_BlobInstances[i]);
			if( pTransferBlob == NULL )
				continue;

			if( pTransferBlob->IsMarkedForDeletion() )
				continue;

			float fDistSqr = (vTestPos - pTransferBlob->GetAbsOrigin()).LengthSqr();
			if( fDistSqr < fClosestBlobDistSqr )
			{
				pClosestBlob = pTransferBlob;
				fClosestBlobDistSqr = fDistSqr;
			}
		}

		if( pClosestBlob )
		{
			pClosestBlob->m_QueuedPipeParticles.AddMultipleToTail( m_QueuedPipeParticles.Count(), m_QueuedPipeParticles.Base() );
			m_QueuedPipeParticles.RemoveAll();
		}
	}
	//UTIL_Remove( this );
	SetThink( &CBaseEntity::SUB_Remove );
	SetNextThink( gpGlobals->curtime + 0.1f );
}


//-----------------------------------------------------------------------------
// Merge two blobs together
//-----------------------------------------------------------------------------
void CNPC_Blob::Merge( CNPC_Blob *pOther )
{
	//MERGE!!!!
	// TODO (Ilya): When individual groups of two entities touch, we may
	// want to only merge those groups and make them part of one entity, instead
	// of merging both entities.

	// Only one should absorb the other. Go with whichever is oldest,
	// failing that, go with whichever has the earliest memory address
	if ( m_hPipeExitBlob.Get() || pOther->m_hPipeExitBlob.Get() )
		return;

	if( m_fBlockMergeUntilTime > gpGlobals->curtime )
		return;

	if( pOther->m_fBlockMergeUntilTime > gpGlobals->curtime )
		return;

	if( m_fSpawnTimeStamp > pOther->m_fSpawnTimeStamp )
		return;

	if( (m_fSpawnTimeStamp == pOther->m_fSpawnTimeStamp) && (this > pOther) )
		return;		

	// We're the winner, absorb the other blob.
	int iNumParticlesToTake = pOther->m_pMainGroup->m_members.Count();
	IPhysicsObject **pTakePhysics = (IPhysicsObject **)stackalloc( sizeof( IPhysicsObject * ) * iNumParticlesToTake );
	for( int i = 0; i != iNumParticlesToTake; ++i )
	{
		// For now we don't expect brain or tentacle particles to switch from one blob to another:
		Assert( !( pOther->m_blobParticles[ i ]->m_flags & ( BLOB_PARTICLE_TENTACLE | BLOB_PARTICLE_BRAIN ) ) );
		pTakePhysics[i] = pOther->m_vecPhysParticles[ i ];
	}

	pOther->RemoveParticles( iNumParticlesToTake, NULL, false );
	AddParticles( iNumParticlesToTake, pTakePhysics, true );

	DebugFullSanityCheck();
	pOther->DebugFullSanityCheck();

	//TODO: AI objectives merge somehow? Right now the merged blob does whatever the older blob was already doing.
	if ( pOther->m_nActiveParticles != 0 )
		return;

	// Move queued pipe particles over to this entity
	if( pOther->m_QueuedPipeParticles.Count() != 0 )
	{
		m_QueuedPipeParticles.AddMultipleToTail( pOther->m_QueuedPipeParticles.Count(), pOther->m_QueuedPipeParticles.Base() );
		pOther->m_QueuedPipeParticles.RemoveAll();
	}

	// If pipe particles were being pumped into dead entity, pump them into this entity
	for( int i = 0; i != s_BlobInstances.Count(); ++i )
	{
		CNPC_Blob *pTransferBlob = dynamic_cast<CNPC_Blob *>(s_BlobInstances[i]);
		if( pTransferBlob == NULL )
			continue;

		if( pTransferBlob->m_hPipeExitBlob == pOther )
		{
			pTransferBlob->m_hPipeExitBlob = this;
		}
	}

	// Find all blobs that split from the merging blob and change their split blob to the one it's merging into
	// TODO: Why isn't this done in the same loop as above?
	CBaseEntity *pObject = NULL;
	while ( ( pObject = gEntList.FindEntityByClassname( pObject, "npc_blob" ) ) != NULL )
	{
		CNPC_Blob *pBlob = dynamic_cast<CNPC_Blob*>( pObject );

		if ( pBlob->m_hAutoSplitBlob.Get() == pOther )
		{
			pBlob->m_hAutoSplitBlob = this;
		}
	}

	pOther->DebugFullSanityCheck();
	UTIL_Remove( pOther );
}


//-----------------------------------------------------------------------------
// Touch function
//-----------------------------------------------------------------------------
void CNPC_Blob::Touch( CBaseEntity *pOther )
{
	if ( pOther == this )
		return; // This can happen if something gets parented to the blob

	CNPC_Blob *pBlob = dynamic_cast<CNPC_Blob *>( pOther );
	if ( pBlob != NULL )
	{
		if ( m_bCanMerge )
		{
			Merge( pBlob );
		}
		return;
	}

	if ( m_bFrozen )
		return;

	// Stunned particles don't deal damage, nor do brainz
	if ( IsStunned() || IsMostlyBrain() )
		return;

	// FIXME: AbsOrigins are totally wrong for this - use the contact point (g_TouchTrace), and pOther's worldspace BBox centre
	Vector vecForceDir = (pOther->GetAbsOrigin() - GetAbsOrigin());
	CTakeDamageInfo info( this, this, sv_blob_touch_damage.GetFloat(), DMG_ACID );
	if ( !m_bLeapCompleted )
	{
		info.SetDamage( sv_blob_leap_damage.GetFloat() );
	}
	CalculateMeleeDamageForce( &info, vecForceDir, pOther->GetAbsOrigin() );

	if ( dynamic_cast<CAI_BaseNPC *>(pOther) != NULL )
	{
		// NPCs like not the caress of the blob
		pOther->TakeDamage( info );
		return;
	}
	
	if ( dynamic_cast<CBasePlayer *>(pOther) != NULL )
	{
		CBasePlayer *pPlayer = dynamic_cast<CBasePlayer *>(pOther);
		if ( !(pPlayer->GetFlags() & FL_GODMODE ) && ( pPlayer->GetMoveType() != MOVETYPE_NOCLIP ) && !( pPlayer->GetFlags() & FL_NOTARGET ) )
		{							 
			float flDamageFreq = sv_blob_touch_damage_frequency.GetFloat();
			bool bCanDamage = gpGlobals->curtime > ( m_fLastDamagedPlayer + flDamageFreq );
			if ( bCanDamage )
			{
				// FIXME: we should compute damage contacts ourselves, and only apply damage
				//        when a sufficiently large CBlobGroup touches the player
				m_fLastDamagedPlayer = gpGlobals->curtime;
				pPlayer->TakeDamage( info );
				pPlayer->ViewPunch( QAngle( 0.2f, 0.0f, -0.1f ) );
			}
		}
		return;
	}
}

//------------------------------------------------------------------------------
// Computes a world-aligned bounding box that surrounds everything in the entity
//------------------------------------------------------------------------------
void CNPC_Blob::ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs )
{
	if ( m_blobParticles.Count() == 0 )
	{
		*pMins = GetAbsOrigin();
		*pMaxs = *pMins;
		return;
	}

	// Update the blob's bounding box
	*pMins = m_blobParticles[0]->m_position;
	*pMaxs = *pMins;

	for ( int i = 0; i < m_blobGroups.Count(); ++i )
	{
		Vector groupMins, groupMaxs;
		Assert( m_blobGroups[ i ]->m_members.Count() );
		BuildAABBForBlobGroup( m_blobGroups[ i ], groupMins, groupMaxs, 0.0f );
		VectorMin( groupMins, *pMins, *pMins );
		VectorMax( groupMaxs, *pMaxs, *pMaxs );
	}

	Vector vSurfaceRadius( m_flRadius, m_flRadius, m_flRadius );
	*pMins -= vSurfaceRadius;
	*pMaxs += vSurfaceRadius;
}

bool CNPC_Blob::TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr )
{
	// FIXME: this needs an acceleration structure (box tree should be fine initially)
	// FIXME: this duplicates too much code from CNPC_Surface::TestCollision()
	trace_t traceTemp;

	int nGroupHit = -1;
	int nParticleHit = -1;

	if (ray.m_IsRay)
	{
		float d1, d2;

		for ( int i = 0; i < m_blobGroups.Count(); ++i )
		{
			CBlobGroup *pBlobGroup = m_blobGroups[ i ];
			Assert( pBlobGroup->m_members.Count() );

			float fFractionLeftSolid;
			Vector mins, maxs;
			BuildAABBForBlobGroup( pBlobGroup, mins, maxs, m_flRadius );

			if ( !IntersectRayWithBox( ray, mins, maxs, 0.0f, &traceTemp, &fFractionLeftSolid ) )
				continue;

			for ( int j = 0; j < pBlobGroup->m_members.Count(); j++ )
			{
				if ( !IntersectRayWithSphere( ray.m_Start, ray.m_Delta, pBlobGroup->m_members[ j ]->m_position, m_flRadius, &d1, &d2 ) )
					continue;

				if ( d1 < tr.fraction )
				{
					nGroupHit = i;
					nParticleHit = j;
					tr.fraction = d1;
				}
			}
		}

		if ( nParticleHit != -1 )
		{
			tr.m_pEnt = this;
			tr.startpos = ray.m_Start;
			tr.endpos = ray.m_Start + ray.m_Delta * tr.fraction; 
			tr.contents = CONTENTS_SOLID;
			tr.hitbox = 0;
			tr.hitgroup = HITGROUP_GENERIC;
			tr.plane.dist = tr.endpos.Length();
			tr.plane.normal = (tr.endpos - m_blobGroups[ nGroupHit ]->m_members[ nParticleHit ]->m_position ) * (1 / m_flRadius);
			tr.plane.type = 0;
			tr.physicsbone = 0;
			tr.surface.surfaceProps = physprops->GetSurfaceIndex( "blob" );
		}
	}
	else
	{
		tr.fraction = 1.0;

		for ( int i = 0; i < m_blobGroups.Count(); ++i )
		{
			CBlobGroup *pBlobGroup = m_blobGroups[ i ];
			Assert( pBlobGroup->m_members.Count() );

			Vector mins, maxs;
			BuildAABBForBlobGroup( pBlobGroup, mins, maxs, m_flRadius );

			if ( IntersectRayWithBox( ray, mins, maxs, 0.0f, &traceTemp ) )
			{
				for ( int j = 0; j < pBlobGroup->m_members.Count(); j++ )
				{
					if ( IntersectRayWithBox( ray, pBlobGroup->m_members[ j ]->m_position - Vector( m_flRadius, m_flRadius, m_flRadius ), pBlobGroup->m_members[ j ]->m_position + Vector( m_flRadius, m_flRadius, m_flRadius ), 0.0f, &traceTemp ) )
					{
						if ( traceTemp.fraction < tr.fraction )
						{
							tr = traceTemp;
							nParticleHit = j;
							nGroupHit = i;
						}
					}
				}
			}
		}

		if ( tr.fraction < 1.0 )
		{
			if ( fContentsMask == MASK_PLAYERSOLID )
			{
				// HACK: To get the player to walk on the blob without popping we force the fraction to be zero
				tr.fraction = 0.0f;
			}

			// TestCollision should just clip off the end of the ray:
			tr.startpos = ray.m_Start + ray.m_StartOffset;
			tr.endpos = tr.startpos + tr.fraction * ray.m_Delta;
			tr.fractionleftsolid = 0;

			tr.contents = CONTENTS_SOLID;
			tr.m_pEnt = this;
			tr.hitbox = 0;
			tr.hitgroup = HITGROUP_GENERIC;
			tr.physicsbone = 0;

			tr.plane.type = 0;
			tr.plane.dist = tr.endpos.Length();
			tr.plane.normal = (tr.endpos - m_blobGroups[ nGroupHit ]->m_members[ nParticleHit ]->m_position );
			VectorNormalize( tr.plane.normal );
			if ( tr.plane.normal.z > 0.0f && tr.plane.normal.z < 0.75f )
			{
				tr.plane.normal.z = 0;
				VectorNormalize( tr.plane.normal );
			}
			tr.surface.surfaceProps = physprops->GetSurfaceIndex( "blob" );
		}
	}

	return true;
}

CNPC_Blob *CNPC_Blob::CreateBlob( int iStartingParticleCount )
{
	CNPC_Blob *pNewBlob = NULL;
	
	pNewBlob = (CNPC_Blob *)CreateEntityByName( "npc_blob" );
	if( pNewBlob == NULL )
		return NULL;

	pNewBlob->m_nActiveParticles = iStartingParticleCount;
	DispatchSpawn( pNewBlob );

	pNewBlob->DebugSanityCheckGroups();

	return pNewBlob;
}

void CNPC_Blob::AddParticles( int iAddCount, IPhysicsObject **pUseExisting, bool bCopyOtherDataFromSourceBlobs )
{
	Assert( !IsMarkedForDeletion() );
	if( iAddCount == 0 )
		return;

	if ( m_pMainGroup == NULL )
	{
		m_pMainGroup = new CBlobGroup;
		m_blobGroups.AddToTail( m_pMainGroup );
	}

	if( pUseExisting != NULL )
	{
		for( int i = m_blobParticles.Count(); i < (m_nActiveParticles + iAddCount); ++i )
		{
			m_blobParticles.AddToTail(new CBlobParticle);
		}

		m_vecPhysParticles.AddMultipleToTail( iAddCount );

		QAngle qAngles;

		for( int i = 0; i != iAddCount; ++i )
		{
			int iAddPosition = m_nActiveParticles + i;
			m_vecPhysParticles[iAddPosition] = pUseExisting[i];
			CBlobParticle* pParticle = m_blobParticles[iAddPosition];

			if( bCopyOtherDataFromSourceBlobs )
			{
				CNPC_Blob *pOldBlob = dynamic_cast<CNPC_Blob *>((CBaseEntity *)pUseExisting[i]->GetGameData());
				if( pOldBlob )
				{
					int iOldIndex = s_ExpandedParticleData[pUseExisting[i]->GetGameIndex()].iEntityIndex;

					// Copy networked arrays
					m_iParticlePositionIndex[iAddPosition] = pOldBlob->m_iParticlePositionIndex[iOldIndex];
					m_vecTypes[iAddPosition] = pOldBlob->m_vecTypes[iOldIndex];
					m_iFlags[iAddPosition] = pOldBlob->m_iFlags[iOldIndex];
				}				
			}

			SetParticleEntityIndex( pUseExisting[i], iAddPosition );
			pParticle->m_vphysParticle = pUseExisting[i];
			pParticle->m_mass = pUseExisting[i]->GetMass();
			pParticle->m_support = 0.0f;
			pParticle->m_position = BLOBPARTICLEPOSITION( m_iParticlePositionIndex[iAddPosition] );
			Vector vDummyAngularImpulse;
			pUseExisting[i]->GetVelocity( &pParticle->m_velocity, &vDummyAngularImpulse );
			pParticle->m_group = m_pMainGroup;
			pParticle->m_parent = m_pMainGroup->m_root;
			pParticle->m_pgoal = NULL;
			pParticle->m_type = SKIN_PARTICLE;
			
			m_vecPhysParticles[iAddPosition] = pUseExisting[i];

			m_pMainGroup->m_particlesByType[SKIN_PARTICLE].AddToTail(pParticle);
			m_pMainGroup->m_members.AddToTail(pParticle);

			pUseExisting[i]->SetGameData( this );

			// Init physics freeze state for the new particles (they should come in here unfrozen)
			FreezeParticle( pParticle );
		}

		if( m_blobParticles[m_nActiveParticles]->m_parent == m_blobParticles[m_nActiveParticles] )
		{
			//we created a new group, small fixup needed
			m_blobParticles[m_nActiveParticles]->m_parent = NULL;
			ChangeParticleType( m_blobParticles[m_nActiveParticles], ROOT_PARTICLE );
		}
		m_nActiveParticles += iAddCount;
	}
	else
	{
		for(int i = 0; i < iAddCount; i++)
		{
			Vector vecParticleOrigin = GetAbsOrigin();
			Vector vecParticleVelocity = Vector( RandomFloat( -1, 1 ), RandomFloat( -1, 1 ), RandomFloat( -1, 1 ) ); 
			CBlobParticle* pNewParticle = CreateParticle( vecParticleOrigin, vecParticleVelocity );
			Assert(pNewParticle);
			if ( pNewParticle == NULL )
				continue;
			pNewParticle->m_type = ( m_pMainGroup->m_members.Count() == 0 ) ? ROOT_PARTICLE : SKIN_PARTICLE;
			ActivateParticle( m_pMainGroup, pNewParticle );
		}
	}
	
	UpdateEntityPhysicsObject();
	DebugFullSanityCheck();
	RecenterAbsOrigin();
}

void CNPC_Blob::RemoveParticles( int iRemoveCount, int *pRemoveIndices, bool bDeletePhysics )
{
	// FIXME: This method duplicates all the code in DeactivateParticle!
	//
	//        It should just sort the indices (process the highest ones first, so FastRemove doesn't
	//        invalidate the remaining indices) and then pass them to 'DeactivateParticles' which processes
	//        them in a loop (and only calls UpdateLargestGroup() and DebugFullSanityCheck() once).
	//        As of this checkin, these are the only differences between the two copies of the code:
	//         - this method destroys the removed particles, DeactivateParticle clears them
	//         - this method *optionally* destroys the physobj, DeactivateParticle clears them
	//         - BLOBPARTICLEPOSITION only gets updated in here (bug?)
	//         - there are some extra Asserts in DeactivateParticle


	if( iRemoveCount == 0 )
		return;

	Assert( iRemoveCount <= m_nActiveParticles );

	if( iRemoveCount > m_nActiveParticles )
		return;

	if( pRemoveIndices == NULL )
	{
		pRemoveIndices = (int *)stackalloc( sizeof( int ) * iRemoveCount );

		int iWriteIndex = 0;
		for( int i = m_nActiveParticles - iRemoveCount; i != m_nActiveParticles; ++i, ++iWriteIndex )
		{
			pRemoveIndices[iWriteIndex] = i;
		}
	}

	
	for( int i = 0; i != iRemoveCount; ++i )
	{
		int iIndex = pRemoveIndices[i];

		// Tentacle particles shouldn't be removed via this route (use DestroyTentacleParticles instead)
		Assert( !( m_blobParticles[ iIndex ]->m_flags & BLOB_PARTICLE_TENTACLE ) );

		UnfreezeParticle( m_blobParticles[ pRemoveIndices[i] ] );
		IPhysicsObject *v = m_vecPhysParticles[iIndex];
		if ( bDeletePhysics )
		{
			DestroyParticlePhysics( v );
		}
		else
		{
			SetParticleEntityIndex( v, USHRT_MAX );
			v->SetGameData( NULL );
		}
		m_vecPhysParticles[iIndex] = NULL;
	}

	m_nActiveParticles -= iRemoveCount;

	//now fill in the indices we carved out
	int iNullOffset = 0; //since some of the carved particles may have come from our reallocation space, we need to bob and weave around them.
	for( int i = 0; i != iRemoveCount; ++i )
	{
		int iIndex = pRemoveIndices[i];
		CBlobParticle *pRemovedParticle = m_blobParticles[iIndex];

		if( iIndex >= m_nActiveParticles )
		{
			--iNullOffset;
		}
		else
		{
			while( m_vecPhysParticles[m_nActiveParticles + i + iNullOffset] == NULL )
				++iNullOffset;

			int iCopyFrom = m_nActiveParticles + i + iNullOffset;
			m_blobParticles[iIndex] = m_blobParticles[iCopyFrom];
			m_vecPhysParticles[iIndex] = m_vecPhysParticles[iCopyFrom];
			SetParticleEntityIndex( m_vecPhysParticles[iIndex], iIndex );
		}

		//fixup groupings from the removed particle
		{
			CBlobGroup* pRemovedFromGroup = pRemovedParticle->m_group;
			Verify(pRemovedFromGroup->m_members.FindAndRemove(pRemovedParticle));
			Verify(pRemovedFromGroup->m_particlesByType[pRemovedParticle->m_type].FindAndRemove(pRemovedParticle));

			if(pRemovedFromGroup->m_members.Count() == 0)
			{
				// If group is empty, delete it.
				Verify(m_blobGroups.FindAndRemove(pRemovedFromGroup));
				if( pRemovedFromGroup == m_pMainGroup )
				{
					m_pMainGroup = NULL;
				}
				delete pRemovedFromGroup;
			}
			else
			{
				//remove the particle from neighbor listings
				for( int j = pRemovedFromGroup->m_members.Count(); --j >= 0; )
				{
					SmartArray<BlobNeighbor_t> &neighbors = pRemovedFromGroup->m_members[j]->m_neighbors;
					for( int k = neighbors.size; --k >= 0; )
					{
						if( neighbors[k].particle == pRemovedParticle )
						{
							neighbors.remove( k );
							break;
						}
					}
				}

				pRemovedParticle->m_neighbors.size = 0;

				CBlobParticle* parent = pRemovedParticle->m_parent;

				// If the root is removed, try to find another particle
				// closeby to be the root and make that particle the root.
				if(parent == NULL || pRemovedParticle->m_type == ROOT_PARTICLE)
				{
					Assert(pRemovedParticle->m_type == ROOT_PARTICLE);

					parent = ClosestBlobParticle( pRemovedParticle->m_position, pRemovedFromGroup->m_members.Base(), pRemovedFromGroup->m_members.Count() );
					if(parent->m_type != ROOT_PARTICLE)
					{
						ChangeParticleType( parent, ROOT_PARTICLE );
					}
					parent->m_parent = NULL;
					parent->m_pgoal = NULL;
					pRemovedFromGroup->m_root = parent;
				}

				// For all children of this particle, reattach them to the
				// closest particle to this particle.
				// Any particle that was a child of the deleted particle
				// becomes a child of the deleted particle's parent.
				for(int j = pRemovedFromGroup->m_members.Count(); --j >= 0; )
				{
					CBlobParticle* pCheck = pRemovedFromGroup->m_members[j];
					if( pCheck->m_parent == pRemovedParticle )
					{
						pCheck->m_parent = parent;
					}
				}
			}
		}

		delete pRemovedParticle;
	}

	m_blobParticles.RemoveMultiple( m_nActiveParticles, iRemoveCount );
	m_vecPhysParticles.RemoveMultiple( m_nActiveParticles, iRemoveCount );

	UpdateEntityPhysicsObject();
	DebugFullSanityCheck();
}

//splits off a list of iNumParticlesToTake particles
CNPC_Blob *CNPC_Blob::SplitBlob( int iNumParticlesToTake, int *pRemoveIndices )
{
	if ( !iNumParticlesToTake || !pRemoveIndices )
		return 0;

	IPhysicsObject **pTakePhysics = (IPhysicsObject **)stackalloc( sizeof( IPhysicsObject * ) * iNumParticlesToTake );
	for( int i = 0; i != iNumParticlesToTake; ++i )
	{
		// For now we don't expect brain or tentacle particles to switch from one blob to another:
		Assert( !( m_blobParticles[ pRemoveIndices[i] ]->m_flags & ( BLOB_PARTICLE_TENTACLE | BLOB_PARTICLE_BRAIN ) ) );
		pTakePhysics[i] = m_vecPhysParticles[ pRemoveIndices[i] ];
	}

	CNPC_Blob *pNewBlob = CreateBlob( 0 );
	pNewBlob->SetName( GetEntityName() );
	RemoveParticles( iNumParticlesToTake, pRemoveIndices, false );
	pNewBlob->AddParticles( iNumParticlesToTake, pTakePhysics );
	Vector vPos;
	QAngle qAngles;
	pNewBlob->m_vecPhysParticles[0]->GetPosition( &vPos, &qAngles );
	pNewBlob->SetAbsOrigin( vPos );
	pNewBlob->m_fBlockMergeUntilTime = gpGlobals->curtime + BLOB_MERGE_AFTER_SPLIT_TIMEOUT;
	pNewBlob->m_hAutoSplitBlob = this;
	pNewBlob->m_fAutoSplitTimeOut = gpGlobals->curtime + 15.0f;
	
	m_BlobSplitOutput.FireOutput( this, pNewBlob );

	DebugFullSanityCheck();
	pNewBlob->DebugFullSanityCheck();

	return pNewBlob;
}

//essentially splits off iNumParticlesToTake in a planar cut.
CNPC_Blob *CNPC_Blob::SplitBlob( Vector vNewBlobSplitDirection, int iNumParticlesToTake )
{
	if( iNumParticlesToTake == 0 )
		return NULL;

	if( iNumParticlesToTake >= m_nActiveParticles )
		return this; //it would take the whole blob.

	struct BlobParticleSort_t
	{
		float fDot;
		int iParticleIndex;
	};
	BlobParticleSort_t *pSort = (BlobParticleSort_t *)stackalloc( sizeof( BlobParticleSort_t ) * iNumParticlesToTake );
	for( int i = 0; i != iNumParticlesToTake; ++i )
	{
		pSort[i].fDot = -FLT_MAX;
		pSort[i].iParticleIndex = -1;
	}
	float fMinDot = -FLT_MAX;
	int iSortLast = iNumParticlesToTake - 1;
	int iSortMiddle = iNumParticlesToTake / 2;
	
	for( int i = 0; i != m_nActiveParticles; ++i )
	{
		float fDot = BLOBPARTICLEPOSITION( m_iParticlePositionIndex[i] ).Dot( vNewBlobSplitDirection );

		if( fDot > fMinDot )
		{
			//qualifies to be split until we find something better
			int iTest = iSortMiddle;
			int iHigh = iSortLast; //We already know the dot product is greater than the one at this index
			int iLow = 0;
			do 
			{
				if( pSort[iTest].fDot > fDot )
					iLow = iTest + 1;
				else if( pSort[iTest].fDot <= fDot )
					iHigh = iTest;

				iTest = (iLow + iHigh) / 2;
			} while( iHigh != iLow );

			int iInsert = iLow;

			for( int j = iSortLast; j > iInsert; --j )
			{
				pSort[j] = pSort[j - 1];
			}
			pSort[iInsert].fDot = fDot;
			pSort[iInsert].iParticleIndex = i;

			fMinDot = pSort[iSortLast].fDot;
		}
	}

	IPhysicsObject **pTakePhysics = (IPhysicsObject **)stackalloc( sizeof( IPhysicsObject * ) * iNumParticlesToTake );
	int *pRemoveIndices = (int *)stackalloc( sizeof( int ) * iNumParticlesToTake );
	for( int i = 0; i != iNumParticlesToTake; ++i )
	{
		int iIndex = pSort[i].iParticleIndex;
		pRemoveIndices[i] = iIndex;
		pTakePhysics[i] = m_vecPhysParticles[iIndex];
		//m_vecPhysParticles[iIndex] = NULL;
	}

	CNPC_Blob *pNewBlob = SplitBlob( iNumParticlesToTake, pRemoveIndices );

	return pNewBlob;
}

void CNPC_Blob::InputTestSplit( inputdata_t &inputdata )
{
	if( m_nActiveParticles <= 2 ) //we can still technically split with 2 particles. But to maintain naming behavior, we always keep the original blob 1 particle bigger than its split half
		return;

	CNPC_Blob *pNewBlob = SplitBlob( Vector( 1.0f, 0.0f, 0.0f ), ((int)ceil(m_nActiveParticles / 2.0f)) - 1 );
	Assert( pNewBlob != NULL );
	if( pNewBlob )
		pNewBlob->SetName( AllocPooledString( inputdata.value.String() ) );
}


void CC_AllBlobsSplit( const CCommand &args )
{
	for( int i = CNPC_Surface::s_BlobInstances.Count(); --i >= 0; )
	{
		CNPC_Blob* blob = (CNPC_Blob*)CNPC_Surface::s_BlobInstances[i];
		// TODO: It seems this check should see if the number of particles is >= 2
		if( blob->m_nActiveParticles != 0 )
		{
			blob->SplitBlob( Vector( 1.0f, 0.0f, 0.0f ), blob->m_nActiveParticles / 2 );
		}
	}
}

static ConCommand split_blobs("split_blobs", CC_AllBlobsSplit, "All blobs split in half.", FCVAR_CHEAT);


void CC_ReloadBlobPhysParamState( const CCommand &args )
{
	LoadBlobPhysicsParameterStates();

	// Reload all the blob's state values
	for( int i = CNPC_Surface::s_BlobInstances.Count(); --i >= 0; )
	{
		CNPC_Blob* blob = (CNPC_Blob*)CNPC_Surface::s_BlobInstances[i];

		int iCurrentState = blob->m_iBlobPhysParamState;
		bool bOverloaded = blob->m_bPhysParamStateOverloaded;

		blob->m_bPhysParamStateOverloaded = false;
		blob->SetPhysParamState( -1 );
		blob->SetPhysParamState( iCurrentState );
		blob->m_bPhysParamStateOverloaded = bOverloaded;
	}
}

static ConCommand blob_reload_phys_param_state("blob_reload_phys_param_state", CC_ReloadBlobPhysParamState, "Reloads the physics param state of all blobs from file.", FCVAR_CHEAT);


void CC_SetBlobPhysParamState( const CCommand &args )
{
	if ( args.ArgC() < 2 )
		return;

	const char *pchEntityName = "";
	const char *pchSetName = args[1];

	if ( args.ArgC() >= 3 )
	{
		pchEntityName = args[1];
		pchSetName = args[2];
	}

	CBaseEntity *pEntity = NULL;
	while ( (pEntity = GetNextCommandEntity( UTIL_GetCommandClient(), pchEntityName, pEntity )) != NULL )
	{
		CNPC_Blob *pBlob = dynamic_cast<CNPC_Blob*>( pEntity );

		if ( pBlob )
		{
			pBlob->m_bPhysParamStateOverloaded = false;
			pBlob->SetPhysParamState( pchSetName );
			pBlob->m_bPhysParamStateOverloaded = true;
		}
	}
}

static ConCommand blob_set_phys_param_state("blob_set_phys_param_state", CC_SetBlobPhysParamState, "Set the physics param state of all blobs.", FCVAR_CHEAT);


void CC_DumpBlobPhysParamState( const CCommand &args )
{
	CBaseEntity *pEntity = NULL;
	while ( (pEntity = GetNextCommandEntity( UTIL_GetCommandClient(), args[1], pEntity )) != NULL )
	{
		CNPC_Blob *pBlob = dynamic_cast<CNPC_Blob*>( pEntity );

		if ( pBlob )
		{
			DevMsg( "\t{\n" );

			DevMsg( "\t\t\"global_viscosity\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_GLOBAL_VISCOSITY ] );

			DevMsg( "\t\t\"neighbor_viscosity\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_VISCOSITY ] );
			DevMsg( "\t\t\"neighbor_dist_scale\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_SCALE ] );
			DevMsg( "\t\t\"neighbor_dist_touch\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_TOUCH ] );
			DevMsg( "\t\t\"neighbor_dist_near\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_NEAR ] );
			DevMsg( "\t\t\"neighbor_dist_bond_strain\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_STRAIN ] );
			DevMsg( "\t\t\"neighbor_dist_bond_break\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_BREAK ] );
			DevMsg( "\t\t\"neighbor_dist_max\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_MAX ] );

			DevMsg( "\t\t\"neighbor_attraction_max\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_ATTRACTION_MAX ] );
			DevMsg( "\t\t\"neighbor_repulsion_max\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_REPULSION_MAX ] );
			DevMsg( "\t\t\"neighbor_lj_surface_tension\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_SURFACE_TENSION ] );
			DevMsg( "\t\t\"neighbor_lj_attraction\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_ATTRACTION ] );
			DevMsg( "\t\t\"neighbor_lj_repulsion\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_REPULSION ] );
			DevMsg( "\t\t\"neighbor_linear_attraction\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_LINEAR_ATTRACTION ] );
			DevMsg( "\t\t\"neighbor_linear_repulsion\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_LINEAR_REPULSION ] );

			DevMsg( "\t\t\"pgoal_attraction_max\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_PGOAL_ATTRACTION_MAX ] );
			DevMsg( "\t\t\"pgoal_repulsion_max\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_PGOAL_REPULSION_MAX ] );
			DevMsg( "\t\t\"pgoal_lj_surface_tension\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_PGOAL_LJ_SURFACE_TENSION ] );
			DevMsg( "\t\t\"pgoal_lj_attraction\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_PGOAL_LJ_ATTRACTION ] );
			DevMsg( "\t\t\"pgoal_lj_repulsion\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_PGOAL_LJ_REPULSION ] );
			DevMsg( "\t\t\"pgoal_linear_attraction\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_PGOAL_LINEAR_ATTRACTION ] );
			DevMsg( "\t\t\"pgoal_linear_repulsion\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_PGOAL_LINEAR_REPULSION ] );

			DevMsg( "\t\t\"max_speed_towards_goal\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_MAX_SPEED_TOWARDS_GOAL ] );
			DevMsg( "\t\t\"time_to_max_speed_towards_goal\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_TIME_TO_MAX_SPEED_TOWARDS_GOAL ] );
			DevMsg( "\t\t\"force_towards_goal_convergence\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_FORCE_TOWARDS_GOAL_CONVERGENCE ] );
			DevMsg( "\t\t\"towards_goal_brake_time\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_TOWARDS_GOAL_BRAKE_TIME ] );

			DevMsg( "\t\t\"sticky_force_for_root_particles\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_ROOT_PARTICLES ] );
			DevMsg( "\t\t\"sticky_force_for_core_particles\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_CORE_PARTICLES ] );
			DevMsg( "\t\t\"sticky_force_for_sticky_particles\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_STICKY_PARTICLES ] );
			DevMsg( "\t\t\"sticky_force_for_skin_particles\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_SKIN_PARTICLES ] );
			DevMsg( "\t\t\"sticky_force_for_tentacle_particles\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_TENTACLE_PARTICLES ] );

			DevMsg( "\t\t\"brain_neighbor_viscosity\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_BRAIN_NEIGHBOR_VISCOSITY ] );
			DevMsg( "\t\t\"brain_attraction_max\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_BRAIN_ATTRACTION_MAX ] );
			DevMsg( "\t\t\"brain_repulsion_max\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_BRAIN_REPULSION_MAX ] );
			DevMsg( "\t\t\"brain_lj_surface_tension\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_BRAIN_LJ_SURFACE_TENSION ] );
			DevMsg( "\t\t\"brain_lj_attraction\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_BRAIN_LJ_ATTRACTION ] );
			DevMsg( "\t\t\"brain_lj_repulsion\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_BRAIN_LJ_REPULSION ] );
			DevMsg( "\t\t\"brain_linear_attraction\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_BRAIN_LINEAR_ATTRACTION ] );
			DevMsg( "\t\t\"brain_linear_repulsion\"\t\"%.1f\"\n", pBlob->m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_BRAIN_LINEAR_REPULSION ] );

			DevMsg( "\t}\n" );
		}
	}
}

static ConCommand blob_dump_phys_param_state("blob_dump_phys_param_state", CC_DumpBlobPhysParamState, "Dump the current ConVar physics param state for input into the script file.", FCVAR_CHEAT);

bool CNPC_Blob::ShouldCollide( int collisionGroup, int contentsMask ) const
{
	if ( m_bFrozen )
		return BaseClass::ShouldCollide( collisionGroup, contentsMask );
	else
		return ( ( (contentsMask & MASK_NPCFLUID) != 0) || (collisionGroup == COLLISION_GROUP_PROJECTILE) ) && ( m_bFrozen || ( (collisionGroup != COLLISION_GROUP_PLAYER) && (collisionGroup != COLLISION_GROUP_PLAYER_MOVEMENT) ) );
}

unsigned int CNPC_Blob::PhysicsSolidMaskForEntity( void ) const
{
	return MASK_NPCFLUID;
}

CNPC_Blob *CNPC_Blob::SpawnUnseenBlob( int iParticleCount )
{
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	Vector vPlayerEye = pPlayer->EyePosition();

	size_t iPossibleNodes = g_pAINetworkManager->GetNetwork()->NumNodes();
	CAI_Node **pPossibleNodes = (CAI_Node **)stackalloc( sizeof(CAI_Node *) * iPossibleNodes );
	Vector vBBox( 16.0f, 16.0f, 16.0f );
	Vector vNBBox = -vBBox;

	int iFoundNodes = FindUnseenNodes( pPossibleNodes, iPossibleNodes, vPlayerEye, NODE_GROUND, vBBox );

	if( iFoundNodes == 0 )
		return NULL;

	CNPC_Blob *pBlob = CreateBlob( iParticleCount );
	if (pBlob)
	{
		pBlob->Teleport( &pPossibleNodes[RandomInt(0, iFoundNodes - 1)]->GetOrigin(), NULL, NULL );
		UTIL_FindClosestPassableSpace( pBlob, Vector( 0.0f, 0.0f, 1.0f ), pBlob->GetAITraceMask() );
	}

	return pBlob;
}

void CNPC_Blob::TeleportToUnseenLocation( void )
{
	CBasePlayer *pPlayer = UTIL_GetLocalPlayer();
	Vector vPlayerEye = pPlayer->EyePosition();

	size_t iPossibleNodes = g_pAINetworkManager->GetNetwork()->NumNodes();
	CAI_Node **pPossibleNodes = (CAI_Node **)stackalloc( sizeof(CAI_Node *) * iPossibleNodes );
	
	//To generate a bounding box for visibility, we're going to compute the blob's total volume among all its particles. Then derive a radius as if it were one large sphere
	float fRadius = pow( (m_flRadius * m_flRadius * m_flRadius) * m_nActiveParticles, 1.0f / 3.0f );
	Vector vBBox( fRadius, fRadius, fRadius );
	
	int iFoundNodes = FindUnseenNodes( pPossibleNodes, iPossibleNodes, vPlayerEye, NODE_GROUND, vBBox );

	if( iFoundNodes == 0 )
		return;

	Teleport( &pPossibleNodes[RandomInt(0, iFoundNodes - 1)]->GetOrigin(), NULL, NULL );
	UTIL_FindClosestPassableSpace( this, Vector( 0.0f, 0.0f, 1.0f ), GetAITraceMask() );
}

void CNPC_Blob::TeleportNPCToParticles( void )
{
	CBlobGroup *pGroup = m_pMainGroup;

	if ( !pGroup || !pGroup->m_root )
	{
		if ( m_blobGroups.Count() > 0 )
		{
			pGroup = m_blobGroups[ 0 ];
		}
	}

	if ( pGroup && pGroup->m_root )
	{
		RecenterAbsOrigin( );
	}
}

void CNPC_Blob::AddPipeEntrancesToSenses( void )
{
	// Find all pipes entrances and add them to the list of objects we will sense
	CBaseEntity *pObject = NULL;
	while ( ( pObject = gEntList.FindEntityByClassname( pObject, "func_pipe_entrance" ) ) != NULL )
	{
		// Tell the AI sensing list that we want to consider this
		g_AI_SensedObjectsManager.AddEntity( pObject );
	}
}


//-----------------------------------------------------------------------------
// Used to produce lists of particles sorted by proximity to a point
//-----------------------------------------------------------------------------
static Vector gBlobParticleProximitySortCentre(0,0,0);
static int BlobParticleProximitySort( const void *a, const void *b )
{
	const CBlobParticle *pParticleA = *(const CBlobParticle **)a;
	const CBlobParticle *pParticleB = *(const CBlobParticle **)b;

	float distA = pParticleA->m_position.DistToSqr( gBlobParticleProximitySortCentre );
	float distB = pParticleB->m_position.DistToSqr( gBlobParticleProximitySortCentre );

	return ( distA == distB ) ? 0 : ( ( distA < distB ) ? -1 : +1 );
}

void CNPC_Blob::GetParticlesSortedByProximity( CBlobParticle **pSortedParticles, const Vector &sortCentre )
{
	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		pSortedParticles[ i ] = m_blobParticles[ i ];
	}

	gBlobParticleProximitySortCentre = sortCentre;
	qsort( pSortedParticles, m_nActiveParticles, sizeof( CBlobParticle * ), BlobParticleProximitySort );
}

void CNPC_Blob::GetParticlesSortedByProximity( CBlobParticle **ppSortedParticles, CBlobGroup *pGroup, const Vector &sortCentre )
{
	if ( !pGroup )
		return;

	int nCount = pGroup->m_members.Count();
	for ( int i = 0; i < nCount; i++ )
	{
		ppSortedParticles[ i ] = pGroup->m_members[ i ];
	}

	gBlobParticleProximitySortCentre = sortCentre;
	qsort( ppSortedParticles, nCount, sizeof( CBlobParticle * ), BlobParticleProximitySort );
}


//-----------------------------------------------------------------------------
// Finds the closest particle in the blob to a specified point
//-----------------------------------------------------------------------------
CBlobParticle *CNPC_Blob::FindClosestParticleToPoint( const Vector &vecTarget, int nFlagsToIgnore ) const
{
	CBlobParticle *pClosestParticle = NULL;
	float flClosestDistSqr = FLT_MAX;
	const int nCount = m_blobParticles.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		CBlobParticle *pParticle = m_blobParticles[i];
		if ( pParticle->m_flags & nFlagsToIgnore )
			continue;

		float flDistSqr = pParticle->m_position.DistToSqr( vecTarget );
		if ( flDistSqr < flClosestDistSqr )
		{
			pClosestParticle = pParticle;
			flClosestDistSqr = flDistSqr;
		}
	}
	
	return pClosestParticle;
}

void CNPC_Blob::MarkParticlesCloseToPosition( const Vector& pos, BlobParticleFlags_t flag )
{
	for ( int i = 0; i < m_blobGroups.Count(); ++i )
	{
		Assert( m_blobGroups[ i ]->m_members.Count() );
		CBlobGroup *pBlobGroup = m_blobGroups[i];

		if ( pBlobGroup->m_bIsTentacle )
			continue; // Ignore tentacles

		Vector groupBBMin, groupBBMax;
		BuildAABBForBlobGroup( m_blobGroups[ i ], groupBBMin, groupBBMax, 0.0f );
		Vector blobCenter = 0.5f * ( groupBBMin + groupBBMax );
		float flAvgDist = ( blobCenter - pos ).Length();
		float flAvgRadius = ( groupBBMax - groupBBMin ).Dot( Vector( 1.0f/6.0f, 1.0f/6.0f, 1.0f/6.0f ) );

		for ( int j = 0; j < pBlobGroup->m_members.Count(); ++j )
		{
			float flParticleDist = ( pBlobGroup->m_members[j]->m_position - pos ).Length();

			if ( flParticleDist < flAvgDist - 0.7f * flAvgRadius )
				pBlobGroup->m_members[j]->m_flags |= flag;
			else
				pBlobGroup->m_members[j]->m_flags &= ~flag;
		}
		//NDebugOverlay::HorzArrow( blobCenter, foodPos, 2.0f, 255, 0, 0, 128, false, 0.25f );
	}
}


void CNPC_Blob::MarkParticlesAsStragglers( void )
{
	// Find particles who are disconnected from the blob's main group and mark them as 'stragglers'.
	// TEMP HACK: disable their collisions (so they'll catch up). NOT the final solution - will use 'breadcrumbs' instead)
	float stragglerTimeout = sv_blob_enable_stragglers.GetFloat();

	// Find the closest group to the blob's target - check all other groups for being stragglers
	int flagsToIgnore = BLOB_PARTICLE_STRAGGLER | BLOB_PARTICLE_TENTACLE;
	CBlobParticle *pClosestParticle = FindClosestParticleToPoint( m_vecMoveGoal, flagsToIgnore );
	CBlobGroup *pClosestGroup = pClosestParticle ? pClosestParticle->m_group : NULL;

	float bondBreakDistSqr = m_flRadius * m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_SCALE ] * m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_BREAK ];
	bondBreakDistSqr *= bondBreakDistSqr;

	for ( int i = 0; i < m_blobGroups.Count(); ++i )
	{
		CBlobGroup *pBlobGroup = m_blobGroups[i];

		if ( pBlobGroup->m_bIsTentacle )
			continue; // Tentacles are outside of the normal simulation
	
		for ( int j = 0; j < pBlobGroup->m_members.Count(); ++j )
		{
			CBlobParticle *pParticle = pBlobGroup->m_members[j];

			if ( pParticle->m_flags & ( BLOB_PARTICLE_BIRTH | BLOB_PARTICLE_DEATH ) )
				continue;

			// We smooth particle velocity over time so we can ignore local jitters
			Vector vDirToTarget = ( m_vecMoveGoal - pParticle->m_position ).Normalized();
			VectorLerp( pParticle->m_smoothedVelocity, pParticle->m_velocity, 0.1f, pParticle->m_smoothedVelocity );
			float flVelocityToTarget = pParticle->m_smoothedVelocity.Dot( vDirToTarget );

			// Since we abolished bond-length-based group splitting, we need to measure
			// disconnection in terms of parent links (in the same way that FindGroupSplits did)
			// NOTE: this should mean that the driving root particle never becomes a straggler
			bool bDisconnectedFromRoot = ( pBlobGroup != pClosestGroup ) || IsDisconnectedFromRoot( pParticle, bondBreakDistSqr );

			// Stragglers (presumably stuck) are outside the closest group and not moving towards the target
			// UNDONE: this might not want to create stragglers in groups not targeting m_vecMoveGoal...
			static const float stragglerVel = 6; // FIXME: put this in the physics parameters
			bool bIsStraggler = pClosestGroup && bDisconnectedFromRoot && ( flVelocityToTarget < stragglerVel*m_flRadius );

			// When the brain is exposed (or the blob is stunned), we want the blob's brain
			// particles to sit there stranded and helpless, so don't make stragglers
			if ( IsStunned() || HasCondition( COND_BLOB_BRAIN_EXPOSED ) || m_bSuppressStragglerCreation )
			{
				bIsStraggler = false;
			}

			// Green(ok), Yellow(danger) Red(straggler)
			//int g = ( pParticle->m_flags & BLOB_PARTICLE_STRAGGLER ) ? 0 : 255;
			//int r = g ? ( bIsStraggler ? 255 : 0 ) : 255;
			//NDebugOverlay::Line( pParticle->m_position, pParticle->m_position + pParticle->m_smoothedVelocity, r, (255-r), 0, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );

			// Give particles 'timeout' seconds to shape up before flagging them as stragglers
			if ( !bIsStraggler || ( pParticle->m_fLastNonStraggler == 0 ) )
				pParticle->m_fLastNonStraggler = gpGlobals->curtime;
			bIsStraggler = stragglerTimeout ? ( ( gpGlobals->curtime - pParticle->m_fLastNonStraggler ) >= stragglerTimeout ) : false;



			// !!!HACK!!!
			{
				// FIXME:  when some particles are collision-disabled, IVP may randomly conclude that all blob
				//        particles are 'calm' (despite their velocity/forces), freeze them and never call VPhysicsUpdate again!
				if ( pParticle->m_vphysParticle->IsAsleep() )
					pParticle->m_vphysParticle->Wake();
			}



			if ( bIsStraggler && !( pParticle->m_flags & BLOB_PARTICLE_STRAGGLER ) )
			{
				// This is a straggler, disable collision so it can catch up
				pParticle->m_flags |= BLOB_PARTICLE_STRAGGLER;
				pParticle->m_vphysParticle->EnableCollisions( false );
			}
			else if ( !bIsStraggler && ( pParticle->m_flags & BLOB_PARTICLE_STRAGGLER ) )
			{
				// No longer a straggler - re-enable collisions once it's back in valid space
				// (and once it's not travelling straight up - avoids indefinite oscillation if stragglers fall through the floor)
				if ( ( vDirToTarget.z < 0.5f ) && !enginetrace->PointOutsideWorld( pParticle->m_position ) )
				{
					pParticle->m_flags &= ~BLOB_PARTICLE_STRAGGLER;
					pParticle->m_vphysParticle->EnableCollisions( true );
				}
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Returns the number of non-brain particles connected to the brain particles
//-----------------------------------------------------------------------------
int CNPC_Blob::GetNumParticlesConnectedToBrain( int flagsToIgnore )
{
	CBlobParticle **stack = (CBlobParticle **)stackalloc( m_nActiveParticles*sizeof(CBlobParticle *) );
	int stackSize = 0;

	// Iterate neighbour links, starting with the brain particles
	int numConnectedToBrain = 0;
	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		// Set the sieve flag, so we can track which particles haven't been processed
		m_blobParticles[ i ]->m_flags |= BLOB_PARTICLE_SIEVE;

		if ( m_blobParticles[ i ]->m_flags & BLOB_PARTICLE_BRAIN )
		{
			stack[ stackSize++ ] = m_blobParticles[ i ];	// PUSH!
			m_blobParticles[ i ]->m_flags &= ~BLOB_PARTICLE_SIEVE;
			numConnectedToBrain++;
		}
	}

	if ( numConnectedToBrain == 0 )
	{
		// If there's no brain, seed with the root of the largest group
		if ( m_pMainGroup && m_pMainGroup->m_root )
		{
			stack[ stackSize++ ] = m_pMainGroup->m_root;	// PUSH!
			m_pMainGroup->m_root->m_flags &= ~BLOB_PARTICLE_SIEVE;
			numConnectedToBrain++;
		}
	}

	while( stackSize )
	{
		CBlobParticle *particle = stack[ --stackSize ];		// POP!
		
		for ( int i = 0; i < particle->m_neighbors.size; i++ )
		{
			CBlobParticle *neighbour = particle->m_neighbors[ i ].particle;
			if ( neighbour->m_flags & BLOB_PARTICLE_SIEVE )
			{
				if ( !( neighbour->m_flags & flagsToIgnore ) )
				{
					stack[ stackSize++ ] = neighbour;		// PUSH!
					numConnectedToBrain++;
				}
				neighbour->m_flags &= ~BLOB_PARTICLE_SIEVE;
			}
		}
	}

	return numConnectedToBrain;
}


//-----------------------------------------------------------------------------
// Returns the average position + velocity of the brain
//-----------------------------------------------------------------------------
void CNPC_Blob::ComputeBrainPosition( Vector &brainPosition, Vector &brainVelocity )
{
	CBlobParticle **pBrainParticles = (CBlobParticle**)stackalloc( m_nBrainParticleCount * sizeof(CBlobParticle*) );
	CBlobGroup *pBrainGroup = NULL;
	int numBrainParticles = GetBrainParticles( pBrainParticles, pBrainGroup );

	if ( numBrainParticles )
	{
		brainPosition = vec3_origin;
		brainVelocity = vec3_origin;
		for ( int i = 0; i < numBrainParticles; i++ )
		{
			brainPosition += pBrainParticles[ i ]->m_position / numBrainParticles;
			brainVelocity += pBrainParticles[ i ]->m_velocity / numBrainParticles;
		}
	}
	else
	{
		brainPosition = GetAbsOrigin();
		brainVelocity = vec3_origin;
	}
}


//-----------------------------------------------------------------------------
// Returns the brain particles + brain group
//-----------------------------------------------------------------------------
int CNPC_Blob::GetBrainParticles( CBlobParticle **pBrainParticles, CBlobGroup * &pBrainGroup )
{
	int numBrainParticles = 0;
	pBrainGroup = NULL;

	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		if ( m_blobParticles[ i ]->m_flags & BLOB_PARTICLE_BRAIN )
		{
			Assert( numBrainParticles < m_nBrainParticleCount ); // Absolutely should not happen!
			if ( numBrainParticles >= m_nBrainParticleCount )
				return 0;

			pBrainParticles[ numBrainParticles ] = m_blobParticles[ i ];
			numBrainParticles++;
		}
	}

	// Find the group w/ the most brain particles in it
	int maxBrainParticlesInGroup = 0;
	int nStart = 0;
	while( nStart < numBrainParticles )
	{
		CBlobGroup *pGroup = pBrainParticles[ nStart ]->m_group;
		int numBrainParticlesInGroup = 0;
		int i = nStart;
		nStart = numBrainParticles;
		for ( ; i < numBrainParticles; i++ )
		{
			if ( pBrainParticles[ i ]->m_group == pGroup )
			{
				numBrainParticlesInGroup++;
			}
			else if ( nStart == numBrainParticles ) // Optimization: " && ( pBrainParticles[ i ]->m_group != pBrainGroup )"
			{
				nStart = i;
			}
		}
		if ( numBrainParticlesInGroup > maxBrainParticlesInGroup )
		{
			pBrainGroup = pGroup;
			maxBrainParticlesInGroup = numBrainParticlesInGroup;
		}
	}
	Assert( pBrainGroup || !numBrainParticles );

	return numBrainParticles;
}


//-----------------------------------------------------------------------------
// When it's mostly brain, new behaviors emerge!
//-----------------------------------------------------------------------------
bool CNPC_Blob::IsMostlyBrain() const
{
	return m_fBrainExposure > 0.9f;
}


//-----------------------------------------------------------------------------
// Computes how exposed the brain is
//-----------------------------------------------------------------------------
void CNPC_Blob::ComputeBrainExposure( void )
{
	if ( !HasBrain() )
		return;
	
	bool bIsMostlyBrain = IsMostlyBrain();

	// If it's not mostly brain, then we need to call damage functions in
	// AIBaseNPC::OnTakeDamage_Alive, but not actually subtract health
	m_takedamage = bIsMostlyBrain ? DAMAGE_YES : DAMAGE_EVENTS_ONLY;

	// The number of brain particles relative to the total determines exposure
	m_fBrainExposure = ComputeBrainParticleRatio();

	if ( IsMostlyBrain() != bIsMostlyBrain )
	{
		if ( IsMostlyBrain() )
		{
			m_OnBrainExposed.FireOutput( this, this );
		}
		else
		{
			m_OnBrainHidden.FireOutput( this, this );
		}
	}
}


//-----------------------------------------------------------------------------
// Regenerate new particles
//-----------------------------------------------------------------------------
void CNPC_Blob::RegenerateParticles( float flFraction )
{
	Assert( m_pMainGroup && m_pMainGroup->m_root );
	if ( !m_pMainGroup || !m_pMainGroup->m_root )
		return;

	int nNonBrainParticleCount = m_nMaxParticleCount - m_nBrainParticleCount;
	int nCountToAdd = (int)( nNonBrainParticleCount * flFraction + 0.5f );
	int nCurrParticleCount = m_pMainGroup->m_members.Count();
	int nNewParticleCount = nCurrParticleCount + nCountToAdd;
	if ( nNewParticleCount > m_nMaxParticleCount )
	{								    
		nNewParticleCount = m_nMaxParticleCount;
	}
	nCountToAdd = nNewParticleCount - nCurrParticleCount;
	int base = m_nActiveParticles;
	AddParticles( nCountToAdd, NULL, false );

	Vector brainPosition, brainVelocity;
	ComputeBrainPosition( brainPosition, brainVelocity );
	float startRadius = m_flRadius*( BIRTH_START_RADIUS + powf( nCurrParticleCount, 0.33f ) );

	for ( int i = 0; i < nCountToAdd; i++ )
	{
		// New particles start at zero radius some distance out from the blob, then grow and move inwards
		// (think of a japanese-style "power up!" effect)
		CBlobParticle *pNewParticle = m_blobParticles[ base + i ];

		pNewParticle->m_flags |= BLOB_PARTICLE_BIRTH;
		pNewParticle->m_flBirthTime = gpGlobals->curtime + RandomFloat( 0, BIRTH_DURATION*BIRTH_START_TIME_SPREAD );
		BLOBPARTICLERADIUS( m_iParticlePositionIndex[ base + i ] ) = 0.0f;
		Vector offset(0,0,0);
		for ( int j = 0; j < 64; j++ )
		{
			Vector testOffset = startRadius*Vector( RandomFloat(-1,+1), RandomFloat(-1,+1), RandomFloat(+0.2f,+1) ).Normalized();
			if ( enginetrace->PointOutsideWorld( brainPosition + testOffset ) )
				continue;
			offset = testOffset;
			break;
		}
		if ( offset.IsZero() )
		{
			// No room to spawn particles, fail
			Warning( "Blob has no room to regenerate particles!" );
			Assert( 0 );
			RemoveParticles( nCountToAdd );
			return;
		}
		pNewParticle->m_vphysParticle->SetPosition( ( brainPosition + offset ), vec3_angle, true );
		pNewParticle->m_vphysParticle->SetVelocity( &vec3_origin, NULL );
		pNewParticle->m_vecPrevWaypointPosition = brainPosition + offset;
		pNewParticle->m_vphysParticle->EnableCollisions( false );
	}

	m_flArmorHealth += m_flMaxArmorHealth *	flFraction;
	if ( m_flArmorHealth > m_flMaxArmorHealth )
	{
		m_flArmorHealth = m_flMaxArmorHealth;
	}
	m_flNextRegenerateTime = gpGlobals->curtime + BLOB_REGENERATION_TIME;
}


//-----------------------------------------------------------------------------
// Regenerating particles grow over time from nothing to full size
//-----------------------------------------------------------------------------
void CNPC_Blob::UpdateRegeneratingParticles( void )
{
	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		CBlobParticle *particle = m_blobParticles[ i ];
		float radius = clamp( ( gpGlobals->curtime - particle->m_flBirthTime ) / BIRTH_DURATION, 0, 1 );
		BLOBPARTICLERADIUS( m_iParticlePositionIndex[ i ] ) = powf( radius, 0.33f );
	}
}


//-----------------------------------------------------------------------------
// Make regenerating particles into regular ones, once fully grown and part of the herd
//-----------------------------------------------------------------------------
void CNPC_Blob::RetireRegeneratingParticles( void )
{
	Vector brainPosition, brainVelocity;
	ComputeBrainPosition( brainPosition, brainVelocity );

	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		CBlobParticle *particle = m_blobParticles[ i ];
		if ( !( particle->m_flags & BLOB_PARTICLE_BIRTH ) )
			continue;

		for ( int j = 0; j < particle->m_neighbors.size; j++ )
		{
			BlobNeighbor_t &neighbour = particle->m_neighbors[ j ];
			int flagsToIgnore = BLOB_PARTICLE_BIRTH | BLOB_PARTICLE_DEATH | BLOB_PARTICLE_STRAGGLER;
			if ( !( neighbour.particle->m_flags & flagsToIgnore ) &&
				 !enginetrace->PointOutsideWorld( particle->m_position ) )
			{
				// This particle is connected to the rest of the blob, so make it a real particle now
				particle->m_flags &= ~BLOB_PARTICLE_BIRTH;
				particle->m_vphysParticle->SetVelocity( &vec3_origin, NULL );
				particle->m_vphysParticle->EnableCollisions( true );
				// Inherit navigation state from the neighbour
				particle->m_vecPrevWaypointPosition = neighbour.particle->m_vecPrevWaypointPosition;
				particle->m_nWaypointTargetIndex = neighbour.particle->m_nWaypointTargetIndex;
				break;
			}
		}
	}
}


//-----------------------------------------------------------------------------
// Update dying particles (clumps of them 'melt' and disappear)
//-----------------------------------------------------------------------------
#define MELT_THRESHOLD 3.5f
#define MELT_THRESHOLD_BUFFER 1.0f
void CNPC_Blob::UpdateDyingParticles( void )
{
	// Dying particles should form into clumps - this code melts each clump from the outside in

	for ( int i = 0; i < m_nActiveParticles; ++i )
	{
		CBlobParticle *particle = m_blobParticles[i];

		if ( ( particle->m_flags & BLOB_PARTICLE_DEATH ) == 0 )
			continue;

		float timeElapsed = ( gpGlobals->curtime - m_blobParticles[i]->m_flDeathTime ) - DEATH_PAUSE_TIME;
		if ( timeElapsed < 0 )
			continue;

		Vector neighbourDir(0,0,0), neighbourDirMins(0,0,0), neighbourDirMaxs(0,0,0);
		for ( int j = 0; j < particle->m_neighbors.size; j++ )
		{
			Vector dir = particle->m_neighbors[ j ].vec;
			neighbourDir += dir;

			if ( particle->m_neighbors[ j ].particle->m_flags & BLOB_PARTICLE_DEATH )
			{
				// Determine which directions are 'covered' by neighbours
				float neighbourTimeElapsed = ( gpGlobals->curtime - particle->m_neighbors[ j ].particle->m_flDeathTime ) - DEATH_PAUSE_TIME;
				float neighbourCompletion = MIN( 1.0f, ( neighbourTimeElapsed / DEATH_MELT_TIME ) );

				// Treat partially melted neighbours as not covering us so much
				dir *= 1.0f - neighbourCompletion;

				VectorMin( dir, neighbourDirMins, neighbourDirMins );
				VectorMax( dir, neighbourDirMaxs, neighbourDirMaxs );
			}
		}

		// Ignore particles below us, but HATE ones above us (so we don't melt the underside of a clump)
		neighbourDirMaxs.z *= 2*neighbourDirMaxs.z;
		neighbourDirMins.z = 0;

		// Determine how covered we are by neighbour particles (the more covered we are, the less we melt):
		Vector surrounds = neighbourDirMaxs - neighbourDirMins;
		float surroundedness = surrounds.Dot( surrounds );
		surroundedness = 0.5f*( 1 + ( surroundedness - MELT_THRESHOLD ) / MELT_THRESHOLD_BUFFER );
		surroundedness = clamp( surroundedness, 0, 1 );

		m_blobParticles[i]->m_flDeathTime += surroundedness*gpGlobals->frametime;
		timeElapsed -= surroundedness*gpGlobals->frametime;

		// Visualise covered directions:
		//Vector p = particle->m_position;
		//NDebugOverlay::Line( p + 4*Vector( neighbourDirMins.x, 0, 0 ), p + 4*Vector( neighbourDirMaxs.x, 0, 0 ), 128,0,0, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		//NDebugOverlay::Line( p + 4*Vector( 0, neighbourDirMins.y, 0 ), p + 4*Vector( 0, neighbourDirMaxs.y, 0 ), 0,128,0, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		//NDebugOverlay::Line( p + 4*Vector( 0, 0, neighbourDirMins.z ), p + 4*Vector( 0, 0, neighbourDirMaxs.z ), 128,128,0, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );

		// Visualise surroundedness:
		//int r = 255*surroundedness;
		//int g = 255 - r;
		//NDebugOverlay::Box( particle->m_position, -1*Vector(1,1,1), +1*Vector(1,1,1), r,g,0,128, NDEBUG_PERSIST_TILL_NEXT_SERVER );

		if ( neighbourDir.LengthSqr() < ( 0.01f*m_flRadius*m_flRadius ) )
			neighbourDir = Vector( 0, 0, -1 );
		neighbourDir.NormalizeInPlace();

		float completion = MIN( 1.0f, ( timeElapsed / DEATH_MELT_TIME ) );
		float oldRadius  = BLOBPARTICLERADIUS( m_iParticlePositionIndex[ i ] );
		float newRadius	 = 1.0f - completion*completion*completion;
		BLOBPARTICLERADIUS( m_iParticlePositionIndex[ i ] ) = newRadius;

		// Shrink towards your neighbours
		Vector position = BLOBPARTICLEPOSITION( m_iParticlePositionIndex[ i ] );
		position += 0.9f*( oldRadius - newRadius )*neighbourDir;
		BLOBPARTICLEPOSITION( m_iParticlePositionIndex[ i ] ) = position;
	}
}

//-----------------------------------------------------------------------------
// Retire dying particles
//-----------------------------------------------------------------------------
void CNPC_Blob::RetireDyingParticles( void )
{
	int *pRemoveIndices = (int*)stackalloc( m_nActiveParticles * sizeof(int) );
	int nRemoveCount = 0;
	for ( int i = 0; i < m_nActiveParticles; ++i )
	{
		if ( ( m_blobParticles[ i ]->m_flags & BLOB_PARTICLE_DEATH ) &&
			 ( BLOBPARTICLERADIUS( m_iParticlePositionIndex[ i ] ) == 0.0f ) )
			pRemoveIndices[nRemoveCount++] = i;
	}

	if ( nRemoveCount > 0 )
	{
		RemoveParticles( nRemoveCount, pRemoveIndices, true );
	}

	stackfree( pRemoveIndices );
}


void CNPC_Blob::UpdateParticleFlags()
{
	ComputeBrainExposure();

	// Mark particles having trouble keeping up with the root particle
	MarkParticlesAsStragglers();

	// Retire damaged particles
	RetireDyingParticles();

	// Make regenerating particles into regular ones, once fully grown
	RetireRegeneratingParticles();
}

void CNPC_Blob::InputUnseenTeleport( inputdata_t &inputdata )
{
	TeleportToUnseenLocation();
}

void CNPC_Blob::InputOverloadPhysParamState( inputdata_t &inputdata )
{
	m_bPhysParamStateOverloaded = false;
	SetPhysParamState( inputdata.value.String() );
	m_bPhysParamStateOverloaded = true;
}

void CNPC_Blob::InputRevertPhysParamState( inputdata_t &inputdata )
{
	m_bPhysParamStateOverloaded = false;
}

void CNPC_Blob::InputForceThroughPipe( inputdata_t &inputdata )
{
	char szEntrancePipe[ 256 ];
	Q_strcpy( szEntrancePipe, inputdata.value.String() );

	char *pchExitPipe = const_cast<char *>( Q_strnchr( szEntrancePipe, ' ', sizeof( szEntrancePipe ) ) );

	if ( !pchExitPipe )
		return;

	pchExitPipe[ 0 ] = '\0';
	++pchExitPipe;

	CFunc_Pipe_Entrance *pEntrancePipe = NULL;
	CFunc_Pipe_Entrance *pExitPipe = NULL;

	CBaseEntity *pObject = NULL;
	while ( !( pEntrancePipe && pExitPipe ) && ( pObject = gEntList.FindEntityByClassname( pObject, "func_pipe_entrance" ) ) != NULL )
	{
		CFunc_Pipe_Entrance *pipe = static_cast<CFunc_Pipe_Entrance *>( pObject );

		if ( Q_strcmp( pipe->GetEntityName().ToCStr(), szEntrancePipe ) )
		{
			pEntrancePipe = pipe;
		}
		else if ( Q_strcmp( pipe->GetEntityName().ToCStr(), pchExitPipe ) )
		{
			pExitPipe = pipe;
		}
	}

	ForceTransferThroughPipe( pEntrancePipe, pExitPipe );
}

void CNPC_Blob::InputShapeTarget_Inner( inputdata_t &inputdata, bool bRemove )
{
	AssertMsg( ( inputdata.value.FieldType() == FIELD_STRING ), "CNPC_Blob::InputAddShapeTarget takes a string (entity name) as input!" );
	if ( inputdata.value.FieldType() == FIELD_STRING )
	{
		string_t shapeName = AllocPooledString( inputdata.value.String() );
		CShapeTarget *pShapeTarget = CShapeTarget::GetFirstTargetInShape( shapeName );
		if ( pShapeTarget )
		{
			bool bDontClearExistingTargets = false;
			if ( bRemove )
				RemoveShapeTarget( pShapeTarget );
			else
				AddShapeTarget( pShapeTarget, bDontClearExistingTargets );
		}
		else
		{
			CBaseEntity *pEntity = gEntList.FindEntityByName( NULL, inputdata.value.String() );
			AssertMsg( pEntity, "Could not match entity name passed to CNPC_Blob::InputAddShapeTarget" );
			if ( pEntity )
			{
				CBaseAnimating *pBaseAnimating = dynamic_cast<CBaseAnimating *>( pEntity );
				AssertMsg( pBaseAnimating, "ERROR: Non-animating entity passed as shape target to blog - ignored." );
				if ( pBaseAnimating )
				{
					bool bUseTargetPositionAngles = false;
					if ( bRemove )
						RemoveShapeTarget( pBaseAnimating );
					else
						AddShapeTarget( pBaseAnimating, vec3_origin, bUseTargetPositionAngles );
					AssertMsg( !gEntList.FindEntityByName( pEntity, inputdata.value.String() ), "Multiple entities match name passed to CNPC_Blob::InputAddShapeTarget - ignoring all except the first." );
				}
			}
		}
	}
}

void CNPC_Blob::InputAddShapeTarget( inputdata_t &inputdata )
{
	bool bDontRemove = false;
	InputShapeTarget_Inner( inputdata, bDontRemove );
}

void CNPC_Blob::InputRemoveShapeTarget( inputdata_t &inputdata )
{
	bool bRemove = true;
	InputShapeTarget_Inner( inputdata, bRemove );
}

void CNPC_Blob::InputTentacleAttackEntity( inputdata_t &inputdata )
{
	// Set the next tentacle target
	Assert( inputdata.value.FieldType() == FIELD_STRING );
	if ( inputdata.value.FieldType() == FIELD_STRING )
	{
		CBaseEntity *pTarget = gEntList.FindEntityByName( NULL, inputdata.value.String() );
		if ( pTarget )
		{
			// Stalk and attack this entity
			m_hNextStalkTarget  = pTarget;
			m_bInputStalkTarget = true;

			// Interrupt the current schedule
			SetCondition( COND_BLOB_RECEIVED_COMMAND );
		}
	}
}

void CNPC_Blob::UpdateBaseColor()
{
	switch( m_BlobType )
	{
	case BLOB_TYPE_GENERIC:
	default:
		m_BaseColor.Init( 1.0f, 1.0f, 1.0f );
		break;

	case BLOB_TYPE_LEAPER:
		m_BaseColor.Init( 1.0f, 0.6f, 0.6f );
		break;

	case BLOB_TYPE_SPITTER:
		m_BaseColor.Init( 0.2f, 0.2f, 1.0f );
		break;

	case BLOB_TYPE_SPAWNER:
		m_BaseColor.Init( 0.6f, 1.0f, 0.6f );
		break;
	}
}

void CNPC_Blob::InputEnableShakeBlobs( inputdata_t &inputdata )
{
	if ( m_BlobType != BLOB_TYPE_GENERIC )
		return;
	m_bCanShakeBlobs = inputdata.value.Bool();
}

void CNPC_Blob::InputEnableTentacleAttack( inputdata_t &inputdata )
{
	if ( m_BlobType != BLOB_TYPE_GENERIC )
		return;
	m_bCanTentacleAttack = inputdata.value.Bool();
}

void CNPC_Blob::InputEnableSpit( inputdata_t &inputdata )
{
	if ( m_BlobType != BLOB_TYPE_GENERIC )
		return;
	m_bCanSpit = inputdata.value.Bool();
}

void CNPC_Blob::InputEnableLeap( inputdata_t &inputdata )
{
	if ( m_BlobType != BLOB_TYPE_GENERIC )
		return;
	m_bCanLeap = inputdata.value.Bool();
}

void CNPC_Blob::InputEnableSpawnRegenerators( inputdata_t &inputdata )
{
	if ( m_BlobType != BLOB_TYPE_GENERIC )
		return;
	m_bCanSpawnRegenerators = inputdata.value.Bool();
}

void CNPC_Blob::InputEnableRegeneration( inputdata_t &inputdata )
{
	if ( m_BlobType != BLOB_TYPE_GENERIC )
		return;
	m_bCanRegenerate = inputdata.value.Bool();
}

void CNPC_Blob::InputRegenerateParticles( inputdata_t &inputdata )
{
	float flFraction = inputdata.value.Float();

	EmitSound( "Blob.Regenerate" );
	flFraction = clamp( flFraction, 0.0f, 1.0f );
	RegenerateParticles( flFraction );
}

float CNPC_Blob::BlobHullTrace( const Vector &vStart, const Vector &vEnd, const CBaseEntity *pIgnore )
{
	trace_t trace, trace2;
	CTraceFilterSkipTwoEntities filter( this, pIgnore, HL2COLLISION_GROUP_BLOB );
	UTIL_TraceHull( vStart, vEnd, WorldAlignMins(), WorldAlignMaxs(), GetAITraceMask(), &filter, &trace );

	if ( trace.startsolid && trace2.startsolid )
	{
		// Can't trace using the hull, so try a line
		UTIL_TraceLine( vStart, vEnd, GetAITraceMask(), &filter, &trace );
	}

	return ( trace.startsolid ? 0 : trace.fraction );
}

float CNPC_Blob::BlobParticleWorldTrace( const Vector &vStart, const Vector &vEnd )
{
	trace_t tr;

	// Choose a box that can fit inside a single particle
	float flBoxHalfWidth = m_flRadius / 1.732f; 
	Vector vecHullMins( -flBoxHalfWidth, -flBoxHalfWidth, -flBoxHalfWidth );
	Vector vecHullMaxs( flBoxHalfWidth, flBoxHalfWidth, flBoxHalfWidth );
	UTIL_TraceHull( vStart, vEnd, vecHullMins, vecHullMaxs, GetAITraceMask_BrushOnly(), NULL, HL2COLLISION_GROUP_BLOB, &tr );
	return tr.fraction;
}

bool CNPC_Blob::ComputeTargetPosition( Vector &result, const CBaseEntity *pTarget, const Vector &targetOffset, bool bDrawDebugLines )
{
	// Try to find a good position on the 'outside' of our target to move to.
	// We trace down (hopefully onto a 'floor'), from the middles of each of the
	// edges/faces of the target's BBox, then trace back in towards its centre,
	// and pick the result nearest to the blob.

	if ( pTarget && pTarget->CollisionProp() )
	{
		const CCollisionProperty *pCollide = pTarget->CollisionProp();
		Vector bboxMins = pCollide->OBBMins();
		Vector bboxMaxs = pCollide->OBBMaxs();
		matrix3x4_t bboxMatrix;
		AngleMatrix( pCollide->GetCollisionAngles(), bboxMatrix );
		Vector bboxOrigin = pCollide->GetCollisionOrigin() + targetOffset;

		Vector bboxCentre = 0.5f*( bboxMins + bboxMaxs );
		Vector testPositions[] =  { Vector( bboxMins.x,   bboxMins.y,   bboxCentre.z ),
									Vector( bboxMins.x,   bboxCentre.y, bboxCentre.z ),
									Vector( bboxMins.x,   bboxMaxs.y,   bboxCentre.z ),
									Vector( bboxCentre.x, bboxMaxs.y,   bboxCentre.z ),
									Vector( bboxMaxs.x,   bboxMaxs.y,   bboxCentre.z ),
									Vector( bboxMaxs.x,   bboxCentre.y, bboxCentre.z ),
									Vector( bboxMaxs.x,   bboxMins.y,   bboxCentre.z ),
									Vector( bboxCentre.x, bboxMins.y,   bboxCentre.z ) };
		float minLengthSqr = FLT_MAX;
		for ( int i = 0; i < ARRAYSIZE( testPositions ); i++ )
		{
			Vector start, end;
			VectorRotate( testPositions[i], bboxMatrix, start );
			start += bboxOrigin;

			// Trace down from the test position, so the target isn't floating in mid-air
			// FIXME: how far down should we trace? (could vary between 'floor' and 'wall-crawling' navigation modes)
			float dist = 1024;
			end = start - dist*Vector(0,0,1);
			dist = dist*BlobHullTrace( start, end, pTarget );
			if ( dist > 0 )
			{
				dist = MAX( 0, ( dist - CShapeTarget::PROJECTILE_PUSH_BACK_DIST ) ); // Don't place targets too close to the world
				end = start - dist*Vector(0,0,1);
				float lengthSqr = ( end - GetAbsOrigin() ).LengthSqr();
				if ( lengthSqr < minLengthSqr )
				{
					minLengthSqr = lengthSqr;

					if ( bDrawDebugLines && ( shaper_draw_debug.GetInt() > 1 ) )
						NDebugOverlay::Line( start, end, 240, 200, 255, true, 5 );

					// Now push back towards the target's origin (in the horizontal plane)
					start = end;
					end = Vector( bboxOrigin.x, bboxOrigin.y, end.z );
					dist = BlobHullTrace( start, end, pTarget );
					end = start + dist*( end - start );

					if ( bDrawDebugLines && ( shaper_draw_debug.GetInt() > 1 ) )
						NDebugOverlay::Line( start, end, 240, 200, 255, true, 5 );

					result = end;
				}
			}
		}

		if ( minLengthSqr != FLT_MAX )
		{
			if ( bDrawDebugLines && ( shaper_draw_debug.GetInt() > 1 ) )
				NDebugOverlay::Line( bboxOrigin, result, 215, 128, 255, true, 5 );
			return true;
		}
	}
	
	return false;
}

void CNPC_Blob::DrawDebugGeometryOverlays( void )
{			  
	if ( ( m_debugOverlays & ~OVERLAY_PIVOT_BIT ) != 0 )
	{
		m_debugOverlays &= ~OVERLAY_PIVOT_BIT;
		BaseClass::DrawDebugGeometryOverlays();
		m_debugOverlays |= OVERLAY_PIVOT_BIT;
	}

	if ( shaper_draw_debug.GetInt() < 2 )
		return;

	// Shape-target-related spew
	char tempstr[512];
	int offset = 0;

	// This behaviour keeps breaking in different ways...
	float textPersist = 0; // NDEBUG_PERSIST_TILL_NEXT_SERVER;
	float geomPersist = NDEBUG_PERSIST_TILL_NEXT_SERVER; // 0;

	if ( GetCurSchedule() )
	{
		const char *pStalkStates[4] = { "NOT_STALKING", "STALK_FAR", "STALK_NEAR", "STALK_LUNGE" };
		COMPILE_TIME_ASSERT( ARRAYSIZE( pStalkStates ) == BLOB_STALK_NUMSTATES );

		// Display the blob's current schedule and task
		const char *pScheduleName = GetCurSchedule()->GetName();
		const char *pTaskName = GetTask() ? TaskName( GetTask()->iTask ) : "";
		const char *pStalkState = pStalkStates[ clamp( m_eBlobStalkState, 0, BLOB_STALK_NUMSTATES ) ];
		Q_snprintf( tempstr, sizeof( tempstr ), "%s [%s] [%s]", pScheduleName, pTaskName, pStalkState );
		EntityText( offset++, tempstr, textPersist, 255, 255, 255 );
	}

	const char *physParamsName = GetBlobPhysicsParameterStatesName( m_iBlobPhysParamState );
	if ( physParamsName )
	{
		Q_snprintf( tempstr, sizeof( tempstr ), "Phys params: %s", physParamsName );
		EntityText( offset++, tempstr, textPersist, 128, 144, 128 );
	}

	{
		// Health/damage status
		Q_snprintf( tempstr, sizeof( tempstr ), "Particles: %d, Health: %2d %s",
					m_nActiveParticles, GetHealth(), ( IsStunned() ? "[STUNNED]" : "" ) );
		EntityText( offset++, tempstr, textPersist, 255, 96, 64 );
	}

	{
		// Brain status
		Q_snprintf( tempstr, sizeof( tempstr ), "%s [exposure: %.2f]", ( HasBrain() ? "HAS BRAIN" : "no brain" ), (float)m_fBrainExposure );
		EntityText( offset++, tempstr, textPersist, 96, 96, HasBrain() ? 255 : 96 );

		// Draw lines between all the brain particles
		CBlobParticle **pBrainParticles = ( CBlobParticle** )stackalloc( m_nBrainParticleCount * sizeof(CBlobParticle*) );
		CBlobGroup *pBrainGroup = NULL;
		int numBrainParticles = GetBrainParticles( pBrainParticles, pBrainGroup );
		for ( int i = 0; i < numBrainParticles; i++ )
		{
			Vector pos1 = pBrainParticles[ i ]->m_position;
			for ( int j = ( i + 1 ); j < numBrainParticles; j++ )
			{
				Vector pos2 = pBrainParticles[ j ]->m_position;
				NDebugOverlay::Line( pos1, pos2, 255, 200, 64, true, geomPersist );
			}
		}
	}

	for ( int i = 0; i < m_ShapeTargets.Count(); i++ )
	{
		CShapeTarget *pShapeTarget = CShapeTarget::GetFirstTargetInShape( m_ShapeTargets[i].m_ShapeTargetName );
		if ( pShapeTarget )
		{
			Q_snprintf( tempstr, sizeof( tempstr ), "Attached to shape: %s", m_ShapeTargets[i].m_ShapeTargetName.ToCStr() );
			EntityText( offset++, tempstr, textPersist, 128, 0, 255  );
			offset++;
		}
	}

	// Show our NPC's position
	{
		Color faceColor(255,255,160,96), edgeColor(200,200,200,144);
		debugoverlay->AddBoxOverlay2( GetAbsOrigin(), -2*Vector(1,1,1), +2*Vector(1,1,1), vec3_angle, faceColor, edgeColor, geomPersist );
	}

	// Show our NPC's target
	{
		Color faceColor(0,255,255,96), edgeColor(200,200,200,144);
		debugoverlay->AddBoxOverlay2( m_vecMoveGoal, -4*Vector(1,1,1), +4*Vector(1,1,1), vec3_angle, faceColor, edgeColor, geomPersist );
	}


	// FIXME: need to clean 'current target' stuff up! (make a 'CBlobTarget' class)
	CBaseEntity *pTargetEntity = m_hStalkTarget;
	Vector targetPosition;
	if ( pTargetEntity && ComputeTargetPosition( targetPosition, pTargetEntity ) )
	{
		Color faceColor(0,128,32,96), edgeColor(32,0,200,144), blank(0,0,0,0);

		// Show the target object's bounding box
		NDebugOverlay::EntityBounds( pTargetEntity, edgeColor.r(), edgeColor.g(), edgeColor.b(), edgeColor.a(), geomPersist );

		// Show the 'target position' computed from that bounding box (where the blob will actually go)
		debugoverlay->AddBoxOverlay2( targetPosition, -2*Vector(1,1,1), +2*Vector(1,1,1), vec3_angle, faceColor, blank, geomPersist );

		if ( m_eBlobStalkState != BLOB_STALK_NONE )
		{
			// Display the current stalk goal (leads the target entity using movement prediction)
			debugoverlay->AddBoxOverlay2( m_vecStalkGoal, -2*Vector(1,1,1), +2*Vector(1,1,1), vec3_angle, blank, faceColor, geomPersist );

			// Draw the current path
			Color wpColor(200,255,0,128);
			AI_Waypoint_t *pWaypoint = GetNavigator()->GetPath()->GetGoalWaypoint();
			while( pWaypoint && pWaypoint->GetPrev() )
			{
				debugoverlay->AddBoxOverlay2( pWaypoint->GetPos(), -1*Vector(1,1,1), +1*Vector(1,1,1), vec3_angle, wpColor, blank, geomPersist );
				debugoverlay->AddLineOverlayAlpha( pWaypoint->GetPos(), pWaypoint->GetPrev()->GetPos(), wpColor.r(), wpColor.g(), wpColor.b(), wpColor.a(), true, geomPersist );
				pWaypoint = pWaypoint->GetPrev();
				if ( pWaypoint == GetNavigator()->GetPath()->GetCurWaypoint() )
					wpColor = Color(100,128,0,128);
			}
			debugoverlay->AddBoxOverlay2( pWaypoint->GetPos(), -1*Vector(1,1,1), +1*Vector(1,1,1), vec3_angle, wpColor, blank, geomPersist );
		}

		if ( m_fMinDistToTarget > 0 )
		{
			float distToTarget = ( targetPosition - GetAbsOrigin() ).Length();
			if ( distToTarget < ( m_fMinDistToTarget - 48 ) )
			{
				m_fMinDistToTarget = distToTarget;
				m_fLastNearerToTargetTime = gpGlobals->curtime;
			}
			else if ( gpGlobals->curtime > ( m_fLastNearerToTargetTime + 3 ) )
			{
				// Haven't gotten closer to our target in three seconds...
				EntityText( offset++, "BLOB CANNOT REACH TARGET...", textPersist, 192, 0, 192 );
				NDebugOverlay::HorzArrow( GetAbsOrigin(), targetPosition, 16, 255, 32, 0, 128, true, geomPersist );
				if ( distToTarget > ( m_fMinDistToTarget + 48 ) )
				{
					// Back off min dist in case the blob temporarily stalls
					m_fMinDistToTarget = distToTarget;
				}
			}
		}
	}
}

int	CNPC_Blob::Save( ISave &save )
{
	if( !BaseClass::Save( save ) )
		return 0;

	save.StartBlock( "QueuedPipeParticles" );

	short iParticleCount = m_QueuedPipeParticles.Count();
	save.WriteShort( &iParticleCount );
	
	for( short i = 0; i != iParticleCount; ++i )
	{
		EHANDLE hHandle = m_QueuedPipeParticles[i].hPipeExit;
		save.WriteEHandle( &hHandle );
		save.WriteFloat( &m_QueuedPipeParticles[i].fArrivalTime );
	}

	save.EndBlock();

	return 1;
}

int	CNPC_Blob::Restore( IRestore &restore )
{
	if( !BaseClass::Restore( restore ) )
		return 0;

	char szBlockName[SIZE_BLOCK_NAME_BUF];
	restore.StartBlock( szBlockName );

	if( !FStrEq( szBlockName, "QueuedPipeParticles" ) )
		return 1;

	short iParticleCount;
	restore.ReadShort( &iParticleCount );
	m_QueuedPipeParticles.SetCount( iParticleCount );	

	if( iParticleCount != 0 )
	{
		BlobPipeParticle_t *pWrite = m_QueuedPipeParticles.Base();

		for( short i = 0; i != iParticleCount; ++i )
		{
			EHANDLE hHandle;
			restore.ReadEHandle( &hHandle );
			pWrite[i].hPipeExit = hHandle;
			restore.ReadFloat( &pWrite[i].fArrivalTime );
		}
	}

	restore.EndBlock();

	return 1;
}


void CC_Blob_Create_Hidden( const CCommand &args )
{
	MDLCACHE_CRITICAL_SECTION();

	bool allowPrecache = CBaseEntity::IsPrecacheAllowed();
	CBaseEntity::SetAllowPrecache( true );

	int iParticleCount;
	if( args.ArgC() >= 2 )
	{
		iParticleCount = atoi( args[1] );
		iParticleCount = MIN( iParticleCount, 256 );
		iParticleCount = MAX( iParticleCount, 1 );		
	}
	else
	{
		iParticleCount = 10;
	}

	CNPC_Blob::SpawnUnseenBlob( iParticleCount );

	CBaseEntity::SetAllowPrecache( allowPrecache );	
}

static ConCommand blob_create_hidden("blob_create_hidden", CC_Blob_Create_Hidden, "Spawn a blob in a nearby location the player can't see.", FCVAR_CHEAT);


//=============================================================================

AI_BEGIN_CUSTOM_NPC( npc_blob, CNPC_Blob )

DECLARE_CONDITION( COND_BLOB_SEE_PIPE_SHORTCUT )
DECLARE_CONDITION( COND_BLOB_SEE_NEW_SHAPE_TARGET )
DECLARE_CONDITION( COND_BLOB_SEE_BUDDY_BLOB )
DECLARE_CONDITION( COND_BLOB_TOUCHING_BUDDY_BLOB )
DECLARE_CONDITION( COND_BLOB_FORCE_TRANSFER_THROUGH_PIPE )
DECLARE_CONDITION( COND_BLOB_TRANSFERING_THROUGH_PIPE )
DECLARE_CONDITION( COND_BLOB_AUTO_SPLIT_FROM_ANOTHER_BLOB )
DECLARE_CONDITION( COND_BLOB_HEALTH_LOW )
DECLARE_CONDITION( COND_BLOB_CAN_STALK_TARGET )
DECLARE_CONDITION( COND_BLOB_CAN_SHAKE_OFF_BLOBS )
DECLARE_CONDITION( COND_BLOB_RECEIVED_COMMAND )
DECLARE_CONDITION( COND_BLOB_BRAIN_EXPOSED )
DECLARE_CONDITION( COND_BLOB_REGENERATE_PARTICLES )
DECLARE_CONDITION( COND_BLOB_CAN_LEAP )
DECLARE_CONDITION( COND_BLOB_LEAP_SUPPRESSED_BY_SQUADMATE )
DECLARE_CONDITION( COND_BLOB_SPIT_SUPPRESSED_BY_SQUADMATE )
DECLARE_CONDITION( COND_BLOB_CAN_RUN_TO_REGENERATOR )
DECLARE_CONDITION( COND_BLOB_CAN_SPAWN_REGENERATOR )

DECLARE_TASK( TASK_BLOB_GET_PATH_TO_PIPE )
DECLARE_TASK( TASK_BLOB_ENTER_PIPE )
DECLARE_TASK( TASK_BLOB_EXIT_PIPE )
DECLARE_TASK( TASK_BLOB_TRANSFER_THROUGH_PIPE )
DECLARE_TASK( TASK_BLOB_GET_PATH_TO_BUDDY_BLOB )
DECLARE_TASK( TASK_BLOB_SET_PHYS_PARAM_STATE )
DECLARE_TASK( TASK_BLOB_TELEPORT_NPC_TO_PARTICLES )
DECLARE_TASK( TASK_BLOB_GET_STALK_TARGET_PATH )
DECLARE_TASK( TASK_WAIT_FOR_STALK_MOVEMENT )
DECLARE_TASK( TASK_BLOB_SHAKE_OFF_BLOBS )
DECLARE_TASK( TASK_BLOB_SPIT_BUILDUP )
DECLARE_TASK( TASK_BLOB_SPIT_START )
DECLARE_TASK( TASK_BLOB_SPIT )
DECLARE_TASK( TASK_BLOB_WAIT_FOR_UNSTUN )
DECLARE_TASK( TASK_BLOB_FLAIL )
DECLARE_TASK( TASK_BLOB_REGENERATE_PARTICLES )
DECLARE_TASK( TASK_BLOB_ANNOUNCE_REGENERATE_PARTICLES )
DECLARE_TASK( TASK_BLOB_WAIT_UNTIL_ENEMY_LOS )
DECLARE_TASK( TASK_BLOB_TENTACLE_ATTACK )
DECLARE_TASK( TASK_BLOB_LEAP_BUILDUP )
DECLARE_TASK( TASK_BLOB_LEAP )
DECLARE_TASK( TASK_BLOB_LEAP_COLLECT )
DECLARE_TASK( TASK_BLOB_LEAP_FINISHED )
DECLARE_TASK( TASK_BLOB_GET_PATH_TO_REGENERATOR )
DECLARE_TASK( TASK_BLOB_TOUCH_REGENERATOR )
DECLARE_TASK( TASK_BLOB_SPAWN_REGENERATOR )

DECLARE_SQUADSLOT( SQUAD_SLOT_BLOB_LEAP )
DECLARE_SQUADSLOT( SQUAD_SLOT_BLOB_FLANK )
DECLARE_SQUADSLOT( SQUAD_SLOT_BLOB_SPIT )

DECLARE_ANIMEVENT( AE_BLOB_MELEE_HIT1 )

DEFINE_SCHEDULE
(
 SCHED_BLOB_ENTER_PIPE,

 "	Tasks"
 "		TASK_SET_TOLERANCE_DISTANCE	48"
 "		TASK_BLOB_GET_PATH_TO_PIPE	0"
 "		TASK_RUN_PATH				0"
 "		TASK_WAIT_FOR_MOVEMENT		0"
 "		TASK_BLOB_ENTER_PIPE		0"
 "		TASK_BLOB_EXIT_PIPE			0"
 ""
 "	Interrupts"
 "		COND_LIGHT_DAMAGE"
)

DEFINE_SCHEDULE
(
 SCHED_BLOB_FORCE_ENTER_PIPE,

 "	Tasks"
 "		TASK_BLOB_ENTER_PIPE		0"
 "		TASK_BLOB_EXIT_PIPE			0"
 ""
 "	Interrupts"
 "		COND_LIGHT_DAMAGE"
 )

DEFINE_SCHEDULE
(
 SCHED_BLOB_TRANSFER_THROUGH_PIPE,

 "	Tasks"
 "		TASK_BLOB_TRANSFER_THROUGH_PIPE		0"
 ""
 "	Interrupts"
 "		COND_LIGHT_DAMAGE"
)

DEFINE_SCHEDULE
(
 SCHED_BLOB_REFORM,

 "	Tasks"
 "		TASK_WAIT							0.5"
 "		TASK_BLOB_TELEPORT_NPC_TO_PARTICLES	0"
 "		TASK_SET_TOLERANCE_DISTANCE			10"
 "		TASK_BLOB_GET_PATH_TO_BUDDY_BLOB	0"
 "		TASK_RUN_PATH						0"
 "		TASK_WAIT_FOR_MOVEMENT				4"
 ""
 "	Interrupts"
 "		COND_LIGHT_DAMAGE"
 "		COND_BLOB_TOUCHING_BUDDY_BLOB"
 "		COND_BLOB_RECEIVED_COMMAND"
)

DEFINE_SCHEDULE
(
 SCHED_BLOB_STALK_TARGET,

 "	Tasks"
 "		TASK_BLOB_GET_STALK_TARGET_PATH		0"
 "		TASK_RUN_PATH						0"
 "		TASK_WAIT_FOR_STALK_MOVEMENT		0"
 ""
 "	Interrupts"
 "		COND_HEAVY_DAMAGE"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_BLOB_BRAIN_EXPOSED"
 "		COND_HEAR_DANGER"
)

DEFINE_SCHEDULE
(
 SCHED_BLOB_FLAIL,

 "	Tasks"
 "		TASK_BLOB_FLAIL						0"
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_HEAR_DANGER"
)

DEFINE_SCHEDULE
(
 SCHED_BLOB_SHAKE_OFF_BLOBS_USE_HINTS,

 "	Tasks"
 "		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_BLOB_SHAKE_OFF_BLOBS"
 "		TASK_FIND_HINTNODE				1200"	// 1200 is a blob shake hint node
 "		TASK_LOCK_HINTNODE				0"
 "		TASK_GET_PATH_TO_HINTNODE		1"
 "		TASK_WAIT_FOR_MOVEMENT			0"
 "		TASK_STOP_MOVING				1"
"		TASK_BLOB_SHAKE_OFF_BLOBS		0"
 ""
 "	Interrupts"
 "		COND_HEAVY_DAMAGE"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_BLOB_CAN_STALK_TARGET"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
)

 DEFINE_SCHEDULE
 (
 SCHED_BLOB_SHAKE_OFF_BLOBS,

 "	Tasks"
 "		TASK_STOP_MOVING				1"
 "		TASK_BLOB_SHAKE_OFF_BLOBS		0"
 ""
 "	Interrupts"
 "		COND_HEAVY_DAMAGE"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_BLOB_CAN_STALK_TARGET"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
)

 DEFINE_SCHEDULE
 (
 SCHED_BLOB_FLEE_DANGER_SOUND,

 "	Tasks"
 "		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_TAKE_COVER_FROM_ENEMY"
 "		TASK_FIND_HINTNODE				1201"	// 1201 is a blob fire cover hint node
 "		TASK_LOCK_HINTNODE				0"
 "		TASK_GET_PATH_TO_HINTNODE		1"
 "		TASK_RUN_PATH					0"
 "		TASK_WAIT_FOR_MOVEMENT			0"
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_NO_HEAR_DANGER"
 )

 DEFINE_SCHEDULE
 (
 SCHED_TAKE_COVER_FROM_ENEMY_USE_BRAIN_HINTS,

 "	Tasks"
 "		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_TAKE_COVER_FROM_ENEMY"
 "		TASK_FIND_HINTNODE				1202"	// 1202 is a blob brain cover hint node
 "		TASK_LOCK_HINTNODE				0"
 "		TASK_GET_PATH_TO_HINTNODE		1"
 "		TASK_RUN_PATH					0"
 "		TASK_WAIT_FOR_MOVEMENT			0"
 "		TASK_REMEMBER					MEMORY:INCOVER"
 "		TASK_SET_ACTIVITY				ACTIVITY:ACT_IDLE"	// Translated to cover
 "		TASK_BLOB_WAIT_UNTIL_ENEMY_LOS	3.0"
 "		TASK_WAIT						2.0"
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_NEW_ENEMY"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
 "		COND_LIGHT_DAMAGE"
 "		COND_HEAVY_DAMAGE"
 )

 DEFINE_SCHEDULE
 (
 SCHED_BLOB_RUN_TO_REGENERATOR,

 "	Tasks"
 "		TASK_BLOB_GET_PATH_TO_REGENERATOR	1"
 "		TASK_RUN_PATH						0"
 "		TASK_WAIT_FOR_MOVEMENT				0"
 "		TASK_BLOB_TOUCH_REGENERATOR			0"
 "		TASK_WAIT							1.0"
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
 )

 DEFINE_SCHEDULE
 (
 SCHED_BLOB_SPAWN_REGENERATOR,

 "	Tasks"
 "		TASK_FIND_HINTNODE				1205"	// 1205 is a blob regeneration node
 "		TASK_LOCK_HINTNODE				0"
 "		TASK_GET_PATH_TO_HINTNODE		1"
 "		TASK_RUN_PATH					0"
 "		TASK_WAIT_FOR_MOVEMENT			0"
 "		TASK_BLOB_SPAWN_REGENERATOR		0"
 "		TASK_SET_SCHEDULE				SCHEDULE:SCHED_TAKE_COVER_FROM_ENEMY"
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
 )


 DEFINE_SCHEDULE
 (
 SCHED_BLOB_ESTABLISH_LINE_OF_FIRE,

 "	Tasks"
 "		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_ESTABLISH_LINE_OF_FIRE"
 "		TASK_FIND_HINTNODE				1204"	// 1204 is a blob spit hint node
 "		TASK_LOCK_HINTNODE				0"
 "		TASK_GET_PATH_TO_HINTNODE		1"
 "		TASK_RUN_PATH					0"
 "		TASK_WAIT_FOR_MOVEMENT			0"
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_NEW_ENEMY"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
 )


 DEFINE_SCHEDULE
 (
 SCHED_BLOB_REGENERATE_PARTICLES,

 "	Tasks"
 "		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_BLOB_REGENERATE_PARTICLES_NO_LOS"
 "		TASK_GET_PATH_TO_ENEMY_LOS		0"
 "		TASK_WAIT_FOR_MOVEMENT			0"
 "		TASK_BLOB_ANNOUNCE_REGENERATE_PARTICLES	0"
 "		TASK_STOP_MOVING				1"
 "		TASK_BLOB_REGENERATE_PARTICLES	1.5"
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
 "		COND_HEAVY_DAMAGE"
 )

 DEFINE_SCHEDULE
 (
 SCHED_BLOB_REGENERATE_PARTICLES_NO_LOS,

 "	Tasks"
 "		TASK_BLOB_ANNOUNCE_REGENERATE_PARTICLES	0"
 "		TASK_STOP_MOVING				1"
 "		TASK_BLOB_REGENERATE_PARTICLES	1.5"
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
 "		COND_HEAVY_DAMAGE"
 )


 DEFINE_SCHEDULE
 (
 SCHED_BLOB_SPIT,

 "	Tasks"
 "		TASK_BLOB_SPIT_START		0"
 "		TASK_STOP_MOVING			0"
 "		TASK_BLOB_SPIT_BUILDUP		2.0"
 "		TASK_BLOB_SPIT				0"
 ""
 "	Interrupts"
 "		COND_HEAVY_DAMAGE"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_BLOB_CAN_STALK_TARGET"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
 "		COND_BLOB_CAN_LEAP"
)


DEFINE_SCHEDULE
(
 SCHED_BLOB_TENTACLE_ATTACK,

 "	Tasks"
 "		TASK_BLOB_TENTACLE_ATTACK	2"
 ""
 "	Interrupts"
 "		COND_HEAVY_DAMAGE"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_BLOB_CAN_STALK_TARGET"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
 )

 DEFINE_SCHEDULE
 (
 SCHED_BLOB_LEAP,

 "	Tasks"
 "		TASK_STOP_MOVING	0"
 "		TASK_BLOB_LEAP_BUILDUP	1.7"
 "		TASK_BLOB_LEAP		0.5"
 "		TASK_WAIT			2.0"
 "		TASK_BLOB_LEAP_COLLECT	0"
 "		TASK_STOP_MOVING	2"
 "		TASK_BLOB_LEAP_FINISHED 0 "
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 "		COND_SEE_FEAR"
 "		COND_HEAR_DANGER"
 )


 //===============================================
 //	> BLOB_STUNNED
 //===============================================
 DEFINE_SCHEDULE
 (
 SCHED_BLOB_STUNNED,

 "	Tasks"
 "		TASK_STOP_MOVING		1"
 "		TASK_SET_ACTIVITY		ACTIVITY:ACT_IDLE"
 "		TASK_BLOB_WAIT_FOR_UNSTUN	0"
 ""
 "	Interrupts"
 "		COND_BLOB_RECEIVED_COMMAND"
 );

 AI_END_CUSTOM_NPC()

 //=============================================================================
