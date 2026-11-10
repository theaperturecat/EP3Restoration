//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "baseanimating.h"
#include "ai_network.h"
#include "ai_default.h"
#include "ai_schedule.h"
#include "ai_hull.h"
#include "ai_node.h"
#include "ai_task.h"
#include "ai_motor.h"
#include "entitylist.h"
#include "basecombatweapon.h"
#include "soundenvelope.h"
#include "gib.h"
#include "gamerules.h"
#include "ammodef.h"
#include "cbasehelicopter.h"
#include "npcevent.h"
#include "ndebugoverlay.h"
#include "decals.h"
#include "explode.h" // temp (sjb)
#include "smoke_trail.h" // temp (sjb)
#include "IEffects.h"
#include "vstdlib/random.h"
#include "engine/IEngineSound.h"
#include "ar2_explosion.h"
#include "te_effect_dispatch.h"
#include "rope.h"
#include "effect_dispatch_data.h"
#include "trains.h"
#include "globals.h"
#include "physics_prop_ragdoll.h"
#include "iservervehicle.h"
#include "soundent.h"
#include "npc_citizen17.h"
#include "physics_saverestore.h"
#include "hl2_shareddefs.h"
#include "props.h"
#include "npc_attackchopper.h"
#include "citadel_effects_shared.h"
#include "eventqueue.h"
#include "beam_flags.h"
#include "ai_eventresponse.h"
#include "saverestore_utlvector.h"
#include "movevars_shared.h"
#include "npc_advisor_roaming_shareddefs.h"
#include "npc_advisor_roaming.h"
#include "con_nprint.h"
#include "npc_advisor_roaming_spit.h"
#include "explode.h"
#include "vphysics/constraints.h"
#include "particle_parse.h"
#include "props.h"
#include "physobj.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

int g_interactionAdvisorImmobilize = 0;
int g_interactionAdvisorRelease = 0;


static char const * GetAdvisorRoamingModelName()
{
	return "models/advisor.mdl";
}

extern int		g_sModelIndexFireball;		// holds the index for the fireball

int g_iAdvisorRoamingEffectIndex = -1;

#define ADVISOR_ROAMING_ACCEL_RATE 500

ConVar sk_advisor_roaming_burst_size("sk_advisor_roaming_burst_size", "15" );
ConVar sk_advisor_roaming_burst_min("sk_advisor_roaming_burst_min", "800" );
ConVar sk_advisor_roaming_burst_dist("sk_advisor_roaming_burst_dist", "768" );

//
// Navigation
//

// Max speed of the advisor
ConVar	sk_advisor_roaming_avoidance( "sk_advisor_roaming_avoidance", "0" );
ConVar	sk_advisor_roaming_speed_max( "sk_advisor_roaming_speed_max", "4096" );
ConVar	sk_advisor_roaming_enemydist_min( "sk_advisor_roaming_enemydist_min", "1024" );


//
// Levitation
//
// The concept is that objects directly under advisor are influenced at 100%
// and the influence  strength linearly falls off towards the edge.
//

// How many objects can levitate
ConVar sk_advisor_roaming_levitate_count( "sk_advisor_roaming_levitate_count", "10" );
ConVar sk_advisor_roaming_levitate_count_mass_min( "sk_advisor_roaming_levitate_count_mass_min", "10" );
ConVar sk_advisor_roaming_levitate_count_small( "sk_advisor_roaming_levitate_count_small", "20" );
// Mass range of objects that advisor can pick up
ConVar sk_advisor_roaming_levitate_mass_min( "sk_advisor_roaming_levitate_mass_min", "1.0" );
ConVar sk_advisor_roaming_levitate_mass_max( "sk_advisor_roaming_levitate_mass_max", "250.0" );
// Box defines the range in which advisor picks up stuff
ConVar sk_advisor_roaming_levitate_box( "sk_advisor_roaming_levitate_box", "512.0" );
// The objects within falloff_start distance from advisor are influenced at 100%
ConVar sk_advisor_roaming_levitate_falloff_start( "sk_advisor_roaming_levitate_falloff_start", "150" );
// The objects within clear radius will be moved outside for advisor to navigate properly
ConVar sk_advisor_roaming_levitate_clear_radius( "sk_advisor_roaming_levitate_clear_radius", "200" );
// The objects top levitation level will be range_top x advisor height
ConVar sk_advisor_roaming_levitate_range_top( "sk_advisor_roaming_levitate_range_top", "1.2" );
// The objects bottom levitation level will be range_bottom x advisor height
ConVar sk_advisor_roaming_levitate_range_bottom( "sk_advisor_roaming_levitate_range_bottom", "0.8" );
// The buffer when floating direction change is occurring
ConVar sk_advisor_roaming_levitate_range_buffer( "sk_advisor_roaming_levitate_range_buffer", "250" );
// How often advisor looks for more objects to levitate (depending on max speed it might need to update more often)
ConVar sk_advisor_roaming_levitate_think_update( "sk_advisor_roaming_levitate_think_update", "0.3" );
// Range for random vertical accelerations applied to levitating objects
ConVar sk_advisor_roaming_levitate_acc_min( "sk_advisor_roaming_levitate_acc_min", "0.1" );
ConVar sk_advisor_roaming_levitate_acc_max( "sk_advisor_roaming_levitate_acc_max", "0.7" );
// Range for random vertical spin applied to levitating objects
ConVar sk_advisor_roaming_levitate_spin_ang_min( "sk_advisor_roaming_levitate_spin_ang_min", "35" );
ConVar sk_advisor_roaming_levitate_spin_ang_max( "sk_advisor_roaming_levitate_spin_ang_max", "400" );
// Range for random around-z-axis rotations applied to levitating objects
ConVar sk_advisor_roaming_levitate_spin_rot_min( "sk_advisor_roaming_levitate_spin_rot_min", "15" );
ConVar sk_advisor_roaming_levitate_spin_rot_max( "sk_advisor_roaming_levitate_spin_rot_max", "45" );
// Tangential acceleration to levitating objects to create a "swirl" affect (0 means no swirl)
ConVar sk_advisor_roaming_levitate_swirl_acc( "sk_advisor_roaming_levitate_swirl_acc", "50" );
// Swirl_in acceleration maintains a swirl around advisor, increasing it will make advisor
// keep its objects as advisor moves
ConVar sk_advisor_roaming_levitate_swirl_in( "sk_advisor_roaming_levitate_swirl_in", "2.1" );
// Levitating objects form a shield in front of advisor - size
ConVar sk_advisor_roaming_levitate_shield( "sk_advisor_roaming_levitate_shield", "30" );
// How fast objects are drifting inside the shield
ConVar sk_advisor_roaming_levitate_shield_drift( "sk_advisor_roaming_levitate_shield_drift", "400" );
// Objects below the z-threshold don't drift too fast
ConVar sk_advisor_roaming_levitate_shield_drift_z( "sk_advisor_roaming_levitate_shield_drift_z", "128" );
// Coefficient to scale drifting outside the shield area
ConVar sk_advisor_roaming_levitate_shield_drift_k( "sk_advisor_roaming_levitate_shield_drift_k", "0.003" );
// How shield is getting predicted
ConVar sk_advisor_roaming_levitate_shield_predict( "sk_advisor_roaming_levitate_shield_predict", "0.1" );
// How much time need to wait for ammo pickup
ConVar sk_advisor_roaming_levitate_ammo_out_time( "sk_advisor_roaming_levitate_ammo_out_time", "1.0" );
// How long does the advisor stay vulnerable after blast damage
ConVar sk_advisor_roaming_levitate_drop_time( "sk_advisor_roaming_levitate_drop_time", "8.0" );
// How much explosive dmg is enough to drop shield
ConVar sk_advisor_roaming_levitate_drop_dmg_exp( "sk_advisor_roaming_levitate_drop_dmg_exp", "0.1" );
// How the levitating shield objects force non-sticking
ConVar sk_advisor_roaming_levitate_stick_mag( "sk_advisor_roaming_levitate_stick_mag", "300" );
ConVar sk_advisor_roaming_levitate_stick_rad( "sk_advisor_roaming_levitate_stick_rad", "50" );
ConVar sk_advisor_roaming_levitate_stick_frc( "sk_advisor_roaming_levitate_stick_frc", "300" );
// How the advisor is producing shield buzz
ConVar sk_advisor_roaming_levitate_buzz_vol( "sk_advisor_roaming_levitate_buzz_vol", "0.3" );

static char const *s_szThinkLevitate = "ThinkLevitate";


//
// Throwing objects
//
// The concept is that objects levitated by advisor are picked up and thrown at player.
// Advisor tries to pick up objects from different places in the swirl and of different
// mass to throw at once.
//

// Preferred distance within which advisor picks objects for throw
ConVar sk_advisor_roaming_throw_pick_dist( "sk_advisor_roaming_throw_pick_dist", "150" );
// Bucketing by distance - for each object to throw no more than "dist_c" objects
// to throw within "dist" from it
ConVar sk_advisor_roaming_throw_bucket_dist( "sk_advisor_roaming_throw_bucket_dist", "64" );
ConVar sk_advisor_roaming_throw_bucket_dist_c( "sk_advisor_roaming_throw_bucket_dist_c", "1" );
// Bucketing by mass - for each object to throw no more than "mass_c" objects
// to throw with mass in "mass" from it
ConVar sk_advisor_roaming_throw_bucket_mass( "sk_advisor_roaming_throw_bucket_mass", "200" );
ConVar sk_advisor_roaming_throw_bucket_mass_c( "sk_advisor_roaming_throw_bucket_mass_c", "1" );
// How many npcs can be thrown in one batch
ConVar sk_advisor_roaming_throw_bucket_npc_c( "sk_advisor_roaming_throw_bucket_npc_c", "1" );
// Objects should be at least "height" units above player's eyes
ConVar sk_advisor_roaming_throw_bucket_height( "sk_advisor_roaming_throw_bucket_height", "32" );
// Velocity to use when raising objects before throw
ConVar sk_advisor_roaming_throw_moveout_vel( "sk_advisor_roaming_throw_moveout_vel", "200" );
// Magnitude of a little explosion used for moveout
ConVar sk_advisor_roaming_throw_moveout_mag( "sk_advisor_roaming_throw_moveout_mag", "300" );
// Force of a little explosion used for moveout
ConVar sk_advisor_roaming_throw_moveout_force( "sk_advisor_roaming_throw_moveout_force", "300" );
// Initial velocity when throwing an object
ConVar sk_advisor_roaming_throw_vel( "sk_advisor_roaming_throw_vel", "8000" );
// Time between advisor starts telegraph and before throw is performed will be double of "time_warn"
ConVar sk_advisor_roaming_throw_time_warn( "sk_advisor_roaming_throw_time_warn", "0.4" );
// Time after the throw is performed that advisor is exhausted and doesn't consider throwing objects again
ConVar sk_advisor_roaming_throw_time( "sk_advisor_roaming_throw_time", "1.0" );
// Acceleration applied to object being thrown (this primarily defines the final velocity)
ConVar sk_advisor_roaming_throw_acc( "sk_advisor_roaming_throw_acc", "2500.0" );
// How often advisor updates its enemy position
ConVar sk_advisor_roaming_throw_laglead( "sk_advisor_roaming_throw_laglead", "0.9" );
// Acceleration will not be applied if object is traveling slower than "acc_cap"
ConVar sk_advisor_roaming_throw_acc_cap( "sk_advisor_roaming_throw_acc_cap", "1000.0" );
// Deadzone when advisor thinks that player is fleeing ( -0.5 = cos(-135 deg) )
ConVar sk_advisor_roaming_throw_deadzone( "sk_advisor_roaming_throw_deadzone", "-0.5" );
// Vertical adjustment in order to miss a fleeing player
ConVar sk_advisor_roaming_throw_deadzone_adj( "sk_advisor_roaming_throw_deadzone_adj", "120" );
// Horizontal adjustment in order to miss a fleeing player
ConVar sk_advisor_roaming_throw_deadzone_adjh( "sk_advisor_roaming_throw_deadzone_adjh", "70" );


//
// Health
//
// The concept is that advisor takes 1hp damage from anything that deals less than 50 damage.
// Everything above 50 damage will be scaled by "dmg_factor" and capped at "health"/"health_increments"
// In this case advisor starts with 100 health
//    bullets deal 1hp damage
//    blasts deal 100/4 = 25 hp damage
// "dmg_smoke" will emit smoke for that long when hurt
//

ConVar sk_advisor_roaming_dmg_min( "sk_advisor_roaming_dmg_min", "20" );
ConVar sk_advisor_roaming_dmg_factor( "sk_advisor_roaming_dmg_factor", "0.25" );
ConVar sk_advisor_roaming_dmg_smoke( "sk_advisor_roaming_dmg_smoke", "0.3" );
ConVar	sk_advisor_roaming_health( "sk_advisor_roaming_health", "105" );
ConVar	sk_advisor_roaming_health_increments( "sk_advisor_roaming_health_increments", "15" );
ConVar	sk_advisor_roaming_player_remap_mass_min( "sk_advisor_roaming_player_remap_mass_min", "0" );
ConVar	sk_advisor_roaming_player_remap_mass_max( "sk_advisor_roaming_player_remap_mass_max", "200" );
ConVar	sk_advisor_roaming_player_remap_dmg_min( "sk_advisor_roaming_player_remap_dmg_min", "5" );
ConVar	sk_advisor_roaming_player_remap_dmg_max( "sk_advisor_roaming_player_remap_dmg_max", "15" );



//
// Avalanche
//
// Avalanche blast is when advisor lowers down to the ground and punts all
// objects towards the player.
//

// Threshold to determine when advisor reached point of attack
ConVar sk_advisor_roaming_av_pos_threshold( "sk_advisor_roaming_av_pos_threshold", "96" );
// After advisor reached the ground-point it will give "warn_time" seconds
// for the player to take cover from avalanche
ConVar sk_advisor_roaming_av_warn_time( "sk_advisor_roaming_av_warn_time", "0.3" );
// How long avalanche descending can wait
ConVar sk_advisor_roaming_av_descend_timeout( "sk_advisor_roaming_av_descend_timeout", "3.0" );
// How often warning signals will beep
ConVar sk_advisor_roaming_av_warn_freq( "sk_advisor_roaming_av_warn_freq", "0.4" );
// Velocity of avalanche front
ConVar sk_advisor_roaming_av_vel( "sk_advisor_roaming_av_vel", "1000" );
// Acceleration objects gain to catch up with avalanche front
ConVar sk_advisor_roaming_av_acc( "sk_advisor_roaming_av_acc", "4000" );
// Delay the throwing of objects for a certain time after avalanche
ConVar sk_advisor_roaming_av_delaythrow( "sk_advisor_roaming_av_delaythrow", "8.0" );

static char const *s_szThinkAvalanche = "ThinkAvalanche";


//
// Spit
//
// Advisor can spit at enemy.
//

// Whether advisor spits when no shield
ConVar sk_advisor_roaming_spit_noshield( "sk_advisor_roaming_spit_noshield", "0" );
// Threshold to determine when advisor reached point of attack
ConVar sk_advisor_roaming_spit_spread( "sk_advisor_roaming_spit_spread", "0.05" );
// Delays between spits
ConVar sk_advisor_roaming_spit_delay_min( "sk_advisor_roaming_spit_delay_min", "0.5" );
ConVar sk_advisor_roaming_spit_delay_max( "sk_advisor_roaming_spit_delay_max", "2.5" );

static char const *s_szThinkSpit = "ThinkSpit";


//
// Pulling
//
// Advisor can pull enemy up to suck brain.
//

// 2D distance within which advisor can decide to pull
ConVar sk_advisor_roaming_pull_dist( "sk_advisor_roaming_pull_dist", "382" );
// Height from which advisor can decide to pull
ConVar sk_advisor_roaming_pull_height( "sk_advisor_roaming_pull_height", "400" );
// Velocity to pull the player with
ConVar sk_advisor_roaming_pull_vel( "sk_advisor_roaming_pull_vel", "150" );
// Max time of pulling
ConVar sk_advisor_roaming_pull_timeout( "sk_advisor_roaming_pull_timeout", "4.0" );
// 3D distance at which advisor starts sucking
ConVar sk_advisor_roaming_pull_suck_dist( "sk_advisor_roaming_pull_suck_dist", "200" );
// Delay after successful pull
ConVar sk_advisor_roaming_pull_delay_succ( "sk_advisor_roaming_pull_delay_succ", "15" );
// Delay after failed pull
ConVar sk_advisor_roaming_pull_delay_fail( "sk_advisor_roaming_pull_delay_fail", "5" );
// Screen shake when pulled
ConVar sk_advisor_roaming_pull_shake_amp( "sk_advisor_roaming_pull_shake_amp", "1000.0" );
ConVar sk_advisor_roaming_pull_shake_freq( "sk_advisor_roaming_pull_shake_freq", "150" );
ConVar sk_advisor_roaming_pull_shake_dur( "sk_advisor_roaming_pull_shake_dur", "3.0" );
ConVar sk_advisor_roaming_pull_shake_radius( "sk_advisor_roaming_pull_shake_radius", "512" );
// Tongue at full extension length
ConVar sk_advisor_roaming_pull_tongue_len( "sk_advisor_roaming_pull_tongue_len", "50.0" );
ConVar sk_advisor_roaming_pull_tongue_start( "sk_advisor_roaming_pull_tongue_start", "12.0" );
// Tongue awaiting for the suck
ConVar sk_advisor_roaming_pull_tongue_drool( "sk_advisor_roaming_pull_tongue_drool", "35.0" );
// Speed to extract the tongue
ConVar sk_advisor_roaming_pull_tongue_speed( "sk_advisor_roaming_pull_tongue_speed", "0.05" );

static char const *s_szThinkPull = "ThinkPull";


//
// Damage control
//

// Damage control interval
ConVar sk_advisor_roaming_dmgc_interval( "sk_advisor_roaming_dmgc_interval", "5" );
// Damage control threshold
ConVar sk_advisor_roaming_dmgc_threshold( "sk_advisor_roaming_dmgc_threshold", "8" );
// Invulnerable for couple of seconds after dmg threshold
ConVar sk_advisor_roaming_dmgc_invuln( "sk_advisor_roaming_dmgc_invuln", "3.0" );
// Come back to battle after so long...
ConVar sk_advisor_roaming_dmgc_comeback( "sk_advisor_roaming_dmgc_comeback", "10.0" );
// Retreat speed
ConVar sk_advisor_roaming_dmgc_retreat_speed( "sk_advisor_roaming_dmgc_retreat_speed", "512.0" );

static char const *s_szDmgcComeBack = "DmgcComeBack";




static void DbgDisplay( int nPos, char const *szKey, float flValue )
{
	con_nprint_s nx = {0};
	nx.time_to_live = 2;
	
	nx.color[0] = 1.0f;
	nx.color[1] = 1.0f;
	nx.color[2] = 1.0f;
	nx.fixed_width_font = true;

	nx.index = nPos;
	
	engine->Con_NXPrintf( &nx, "%-20.15s %5.3f", szKey, flValue );
}

static int IsExploitableByPlayer( CBaseEntity *pEntity )
{
	if ( !pEntity )
		return 0;

	if ( CPhysicsProp *p = dynamic_cast< CPhysicsProp * >( pEntity ) )
		return p->ExploitableByPlayer();

	if ( CPhysBox *p = dynamic_cast< CPhysBox * >( pEntity ) )
		return p->ExploitableByPlayer();

	if ( FClassnameIs( pEntity, "weapon_frag" ) )
		return 1;

	return 0;
}



/*

  Advisor_roaming should move its head according to flight model when the target is behind the advisor_roaming,
  or when the target is too far away to shoot at. Otherwise, the head should aim at the target.

	Negative angvelocity.y is a RIGHT turn.
	Negative angvelocity.x is UP

*/

#define ADVISOR_ROAMING_AP_MUZZLE	5

#define ADVISOR_ROAMING_MIN_ROCKET_DIST		1000.0f
#define ADVISOR_ROAMING_MAX_GUN_DIST		2000.0f
#define ADVISOR_ROAMING_ARRIVE_DIST			128.0f

#define ADVISOR_ROAMING_AE_THRUST			1

#define ADVISOR_ROAMING_HEAD_MAX_UP			-65
#define ADVISOR_ROAMING_HEAD_MAX_DOWN		60
#define ADVISOR_ROAMING_HEAD_MAX_LEFT		60
#define ADVISOR_ROAMING_HEAD_MAX_RIGHT		-60

#define	BASE_STITCH_VELOCITY		800		//Units per second
#define	MAX_STITCH_VELOCITY			1000	//Units per second

#define	ADVISOR_ROAMING_LEAD_DISTANCE		800.0f
#define	ADVISOR_ROAMING_AVOID_DIST			512.0f
#define	ADVISOR_ROAMING_STITCH_MIN			512.0f

#define	ADVISOR_ROAMING_MIN_CHASE_DIST_DIFF	128.0f	// Distance threshold used to determine when a target has moved enough to update our navigation to it

#define	MIN_GROUND_ATTACK_DIST			500.0f // Minimum distance a target has to be for the advisor_roaming to consider using the ground attack weapon
#define	MIN_GROUND_ATTACK_HEIGHT_DIFF	128.0f // Target's position and hit position must be within this threshold vertically

#define ADVISOR_ROAMING_WASH_ALTITUDE		1024.0f

#define ADVISOR_ROAMING_INNER_NAV_DIST			400.0f
#define ADVISOR_ROAMING_OUTER_NAV_DIST			800.0f

#define ADVISOR_ROAMING_BELLYBLAST_TARGET_HEIGHT	512.0		// Height above targets that the advisor_roaming wants to be when bellyblasting

#define ADVISOR_ROAMING_MISSILE_MAX_RESPONSE_TIME	0.4
#define ADVISOR_ROAMING_MAX_HITS_PER_BURST			5

#define ADVISOR_ROAMING_FLARE_IGNORE_TIME		6.0

//=====================================
// Custom activities
//=====================================
Activity ACT_ADVISOR_ROAMING_PATROL;
Activity ACT_ADVISOR_ROAMING_HOVER;
Activity ACT_ADVISOR_ROAMING_CRASH;

#define	ADVISOR_ROAMING_DEBUG_LEADING	1
#define	ADVISOR_ROAMING_DEBUG_PATH		2
#define	ADVISOR_ROAMING_DEBUG_STITCHING	3
#define ADVISOR_ROAMING_DEBUG_BELLYBLAST 4
#define ADVISOR_ROAMING_DEBUG_THROW 5

ConVar g_debug_advisor_roaming( "g_debug_advisor_roaming", "0", FCVAR_CHEAT );

//-----------------------------------------------------------------------------
// Purpose: Dying advisor_roaming ragdoll controller
//-----------------------------------------------------------------------------
class CAdvisorRoamingRagdollMotion : public IMotionEvent
{
	DECLARE_SIMPLE_DATADESC();
public:
	virtual simresult_e	Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
	{
		linear = Vector(0,0,400);
		angular = Vector(0,600,100);

		return SIM_GLOBAL_ACCELERATION;
	}
};

BEGIN_SIMPLE_DATADESC( CAdvisorRoamingRagdollMotion )
END_DATADESC()

//////////////////////////////////////////////////////////////////////////
// Purpose: levitate controller
//////////////////////////////////////////////////////////////////////////

class CAdvisorRoamingLevitate : public IMotionEvent
{
	DECLARE_SIMPLE_DATADESC();

public:

	virtual simresult_e	Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular );

	EHANDLE m_Advisor; ///< handle to the advisor roaming.
};

BEGIN_SIMPLE_DATADESC( CAdvisorRoamingLevitate )
	DEFINE_FIELD( m_Advisor, FIELD_EHANDLE ),
END_DATADESC()

//////////////////////////////////////////////////////////////////////////
// Purpose: pull controller
//////////////////////////////////////////////////////////////////////////

class CAdvisorRoamingPull : public IMotionEvent
{
	DECLARE_SIMPLE_DATADESC();

public:

	virtual simresult_e	Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular );

	EHANDLE m_Advisor; ///< handle to the advisor roaming.
};

BEGIN_SIMPLE_DATADESC( CAdvisorRoamingPull )
	DEFINE_FIELD( m_Advisor, FIELD_EHANDLE ),
END_DATADESC()



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CTargetAdvisorRoamingCrash : public CPointEntity
{
	DECLARE_CLASS( CTargetAdvisorRoamingCrash, CPointEntity );
public:
	DECLARE_DATADESC();	

	void	InputEnable( inputdata_t &inputdata )
	{
		m_bDisabled = false;
	}
	void	InputDisable( inputdata_t &inputdata )
	{
		m_bDisabled = true;
	}
	bool	IsDisabled( void )
	{
		return m_bDisabled;
	}
	void	AdvisorRoamingCrashedOnTarget( void )
	{
		m_OnCrashed.FireOutput( this, this );
	}

private:
	bool			m_bDisabled;

	COutputEvent	m_OnCrashed;
};

LINK_ENTITY_TO_CLASS( info_target_advisor_roaming_crash, CTargetAdvisorRoamingCrash );

BEGIN_DATADESC( CTargetAdvisorRoamingCrash )
	DEFINE_FIELD( m_bDisabled, FIELD_BOOLEAN ),

	// Inputs
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),

	// Outputs
	DEFINE_OUTPUT( m_OnCrashed,			"OnCrashed" ),
END_DATADESC()


//===================================================================
// AdvisorRoaming - the combine dugongic like attack vehicle.
//===================================================================
class CNPC_CombineAdvisorRoaming :
	public CBaseHelicopter,
	public IAdvisorCooperationHost
{
	friend class CAdvisorRoamingLevitate;

public:
	DECLARE_CLASS( CNPC_CombineAdvisorRoaming, CBaseHelicopter );

	CNPC_CombineAdvisorRoaming( void );
	~CNPC_CombineAdvisorRoaming( void );

	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();
	DEFINE_CUSTOM_AI;

	void	PlayPatrolLoop( void );
	void	PlayAngryLoop( void );

	void	Spawn( void );
	void	Activate( void );
	void	Precache( void );
	void	OnRestore( void );
	void	UpdateOnRemove( void );
	void	PrescheduleThink( void );
	void	HelicopterPostThink( void );
	void	StopLoopingSounds( void );

	bool	IsValidEnemy( CBaseEntity *pEnemy );
	void	GatherEnemyConditions( CBaseEntity *pEnemy );

	void	Flight( void );

	bool	FVisible( CBaseEntity *pEntity, int traceMask = MASK_BLOCKLOS, CBaseEntity **ppBlocker = NULL );
	int		OnTakeDamage_Alive( const CTakeDamageInfo &info );
	void	FireDamageOutputsUpto( int iDamageNumber );

	virtual float GetAcceleration( void ) { return 15; }

	virtual void	MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType );
	virtual void	DoImpactEffect( trace_t &tr, int nDamageType );

	void	MoveHead( void );
	void	UpdateDesiredPosition( void );
	void	DoCombat( void );
	bool	ChooseEnemy( void );
	void	DoMuzzleFlash( void );
	void	Ping( void );

	void	FireCannonRound( void );

	// AdvisorRoaming death process
	void	Event_Killed( const CTakeDamageInfo &info );
	void	BeginCrash( void );				// I'm going to go to a crash point and die there
	void	BeginDestruct( void );			// I want to die now, so create my ragdoll
	void	SelfDestruct( void );			// I'm now fully dead, so remove myself.
	void	CreateSmokeTrail( void );
	bool	FindNearestAdvisorRoamingCrash( void );
	
	int		BloodColor( void ) { return BLOOD_COLOR_YELLOW; }
	void	GibMonster( void );

	void	UpdateRotorSoundPitch( int iPitch );
	void	InitializeRotorSound( void );

	void	ApplyGeneralDrag( void );
	void	ApplySidewaysDrag( const Vector &vecRight );

	void	TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr );

	void	UpdateEnemyTarget( void );

	Vector	GetEnemyTarget( void );
	Vector	GetMissileTarget( void );

	float	GroundDistToPosition( const Vector &pos );

	void	FireWeapons( void );
	bool	FireGun( void );
	bool	IsTargettingMissile( void );
	bool	ShouldUseAvoidanceWhenTracking() { return sk_advisor_roaming_avoidance.GetBool(); }
	
	Class_T Classify( void ) { return CLASS_COMBINE_GUNSHIP; } // for now
	float	GetAutoAimRadius() { return 144.0f; }

	// Input functions
	void	InputSetPenetrationDepth( inputdata_t &inputdata );
	void	InputOmniscientOn( inputdata_t &inputdata );
	void	InputOmniscientOff( inputdata_t &inputdata );
	void	InputBlindfireOn( inputdata_t &inputdata );
	void	InputBlindfireOff( inputdata_t &inputdata );
	void	InputSelfDestruct( inputdata_t &inputdata );
	void	InputSetDockingBBox( inputdata_t &inputdata );
	void	InputSetNormalBBox( inputdata_t &inputdata );
	void	InputEnableGroundAttack( inputdata_t &inputdata );
	void	InputDisableGroundAttack( inputdata_t &inputdata );
	void	InputDoGroundAttack( inputdata_t &inputdata );
	void	InputDoAvalancheBlast( inputdata_t &inputdata );
	void	InputDoSpitAttack( inputdata_t &inputdata );
	void	InputSetMaxSpeed( inputdata_t &inputdata );
	void	InputDropShield( inputdata_t &inputdata );
	
	//NOTENOTE: I'm rather queasy about adding these, as they can lead to nasty bugs...
	void	InputBecomeInvulnerable( inputdata_t &inputdata );
	void	InputBecomeVulnerable( inputdata_t &inputdata );

	bool	PoseGunTowardTargetDirection( const Vector &vTargetDir );
	void	StartCannonBurst( int iBurstSize );
	void	StopCannonBurst( void );

	bool	CheckGroundAttack( void );
	void	StartGroundAttack( void );
	void	StopGroundAttack( bool bDoAttack );
	Vector	GetGroundAttackHitPosition( void );
	void	DoGroundAttackExplosion( void );
	void	DrawRotorWash( float flAltitude, const Vector &vecRotorOrigin );

	void	ManageWarningBeam( void );
	void	DoBellyBlastDamage( trace_t &tr, Vector vMins, Vector vMaxs );

	// Updates the facing direction
	void	UpdateFacingDirection( void );

	virtual float GetRotorVolume( void );

public:
	// Because the combine advisor_roaming is a leaf class, we can use
	// static variables to store this information, and save some memory.
	// Should the advisor_roaming end up having inheritors, their activate may
	// stomp these numbers, in which case you should make these ordinary members
	// again.
	static int m_poseFlex_Horz, m_poseFlex_Vert, m_posePitch, m_poseYaw, m_poseArm_Accel, m_poseArm_Sway;
	static int m_poseCamera_Pitch, m_poseCamera_Yaw;
	static int m_poseTongue_Length;
	static int m_bodyTongue;
	static int m_attachMouth, m_attachSuckTgt;

	static bool m_sbStaticPoseParamsLoaded;
	virtual void	PopulatePoseParameters( void );

private:
	// Outputs
	COutputEvent	m_OnFireCannon;
	COutputEvent	m_OnCrashed;

	COutputEvent	m_OnFirstDamage;	// First damage tick
	COutputEvent	m_OnSecondDamage;
	COutputEvent	m_OnThirdDamage;
	COutputEvent	m_OnFourthDamage;
	// Keep track of which damage outputs we've fired. This is necessary
	// to ensure that the game doesn't break if a mapmaker has outputs that
	// must be fired on advisor_roamings, and the player switches skill levels 
	// midway through a advisor_roaming battle.
	bool			m_bDamageOutputsFired[ADVISOR_ROAMING_NUM_DAMAGE_OUTPUTS];	

	float	m_flNextGroundAttack;	// Time to wait before the next ground attack
	bool	m_bIsGroundAttacking;	// Denotes that we are ground attacking
	bool	m_bCanGroundAttack;		// Denotes whether we can ground attack or not
	float	m_flGroundAttackTime;	// Delay before blast happens from ground attack

	CHandle<SmokeTrail>	m_pSmokeTrail;
	EHANDLE			m_hGroundAttackTarget;

	CSoundPatch		*m_pAirExhaustSound;
	CSoundPatch		*m_pAirBlastSound;
	CSoundPatch		*m_pCannonSound;

	CBaseEntity		*m_pRotorWashModel;
	QAngle			m_vecAngAcceleration;

	float			m_flEndDestructTime;

	int				m_iDoSmokePuff;
	int				m_iAmmoType;
	int				m_iBurstSize;
	
	bool			m_fBlindfire;
	bool			m_fOmniscient;
	bool			m_bIsFiring;
	int				m_iBurstHits;
	bool			m_bPreFire;
	bool			m_bInvulnerable;

	float			m_flTimeNextPing;
	float			m_flPenetrationDepth; 
	float			m_flDeltaT;
	float			m_flTimeNextAttack;
	float			m_flNextSeeEnemySound;
	float			m_flNextRocket;
	float			m_flBurstDelay;

	Vector			m_vecAttackPosition;
	Vector			m_vecAttackVelocity;

	// Used when the advisor_roamings using the chopper model
	Vector			m_angGun;

	// For my death throes
	IPhysicsMotionController		*m_pCrashingController;
	CAdvisorRoamingRagdollMotion			m_crashCallback;
	EHANDLE							m_hRagdoll;
	CHandle<CTargetAdvisorRoamingCrash>	m_hCrashTarget;
	float							m_flNextAdvisorRoamingCrashFind;

	CNetworkVector( m_vecHitPos );

	// If true, playing patrol loop.
	// Else, playing angry.
	bool			m_fPatrolLoopPlaying;

	
	// Damage control
public:
	CUtlVector< float > m_dmgc_times;
	CUtlVector< float > m_dmgc_dmgs;
	float			m_dmgc_flTimeInvuln;
	EHANDLE			m_dmgc_hPathRetreat;
	EHANDLE			m_dmgc_hPathBattle;
	COutputEvent	m_dmgc_onDmgReached;

	void DamageControl_RecordDmg( float flDmg );
	void DamageControl_OnDmgReached( float flCumulativeDmg );
	void DamageControl_SetRetreatTrack( inputdata_t &inputdata );
	void DamageControl_ScheduleComeBackToBattle();
	void DamageControl_ComeBackToBattle();
	virtual void OnNewCurrentTarget( CPathTrack *pTrackNew, CPathTrack *pTrackOld );

	// Advisor pull functions
public:
	enum PullStage_t
	{
		PULL_NONE,
		PULL_PREPARING,
		PULL_PULLING,
		PULL_SUCK,
		PULL_DROP,
	};
	PullStage_t m_pull_eStage;
	IPhysicsObject *m_pull_pHook;
	IPhysicsConstraint *m_pull_pConstraint;
	IPhysicsMotionController *m_pull_pController;
	CAdvisorRoamingPull m_pull_fnCallback;
	float m_pull_flTime;
	float m_pull_flTongue, m_pull_flTongueTime;

	void Pull_Update();
	void Pull_Prepare();
	void Pull_Pulling();
	void Pull_Suck();
	void Pull_Drop();

	void Pull_Clear();
	void Pull_DelayUpdate( bool bSuccessfulPull = false );
	bool Pull_CheckCondition();
	void Pull_SimulateTongue();
	void Pull_SetPoseParam();

	// Advisor spit functions
public:
	enum SpitStage_t
	{
		SPIT_NONE,
		SPIT_PREPARING,
		SPIT_THROW,
		SPIT_FLYING,
		SPIT_HIT,
	};
	SpitStage_t m_spit_eStage;
	float m_spit_flTime;
	Vector m_spit_vel;
	EHANDLE m_spit_ball;

	void Spit_Update();
	void Spit_Prepare();
	void Spit_Throw();
	void Spit_Flying();
	void Spit_Hit();

	void Spit_DelayAttack();

	// Advisor control functions
public:
	enum AdvisorAttack_t
	{
		ATK_NONE,
		ATK_THROW,
		ATK_AVALANCHE,
		ATK_SPIT,
		ATK_PULL,
	};

	void Control_ReturnToPath();
	void Control_AttackCompleted();
	AdvisorAttack_t Control_GetOtherAttackInProgress( AdvisorAttack_t eWantToStartAttack );

	// Avalanche blast
public:
	enum AvalancheStage_t
	{
		AS_NONE,
		AS_PREPARING,		// Advisor is waiting for conditions to be satisfied
		AS_DESCENDING,		// Advisor is descending for a big blast
		AS_WARNING,			// Advisor is warning before the blast
		AS_BLAST,			// Advisor is initiating the blast
		AS_SHOCKWAVE,		// Advisor shockwave is propagating
	};

	AvalancheStage_t m_avalanche_eStage;
	float m_avalanche_flTime;
	float m_avalanche_front;

	void Avalanche_Update();
	void Avalanche_Prepare();
	void Avalanche_Descend();
	void Avalanche_Warning();
	void Avalanche_DoBlast();
	void Avalanche_ShockWave();
	void Avalanche_Finished();

	// Advisor cooperation data
public:
	CUtlVector< EHANDLE > m_coop_npcs;

	virtual CBaseEntity * AdvisorHost_GetEntity() { return this; }
	virtual void AdvisorHost_ExitCoopMode( IAdvisorCooperation *pCoop );
	virtual void AdvisorHost_ExitLevitationMode( IAdvisorCooperation *pCoop );

	// Levitation data
public:
	IPhysicsMotionController *m_levitate_pPhysController;
	CAdvisorRoamingLevitate m_levitate_fnCallback;

	CUtlVector<EHANDLE>	m_levitate_lstObjects;
	CUtlVector<Vector>	m_levitate_orgObjects;

	float				m_levitate_flTimeAmmoCondition;

	COutputEvent		m_levitate_outEnoughAmmo;
	COutputEvent		m_levitate_outLowAmmo;
	COutputEvent		m_levitate_outNoAmmo;

	void Levitate_FindObjects();
	void Levitate_Start();
	void Levitate_Update();
	void Levitate_AttachObjectsToController();
	void Levitate_Stop( float flSeconds = 0.0 );
	bool Levitate_EntityFilter( CBaseEntity *pEntity );

	// Throwing objects at player
public:
	enum ThrowStage_t
	{
		TS_CLEAR,
		TS_FIND,
		TS_MOVEOUT,
		TS_THROW
	};
	ThrowStage_t m_throw_eStage;
	CUtlVector< EHANDLE > m_throw_lstObjects;
	CUtlVector< EHANDLE > m_throw_lstThrown;
	int m_throw_iObject;
	float m_throw_flNextTime;
	Vector m_throw_lastThrowTgt;
	float m_throw_flLastTgtTime;
	int m_throw_numTimes;

	void Throw_Update();
	void Throw_Clear();
	void Throw_FindObjects();
	bool Throw_EntityFilter( CBaseEntity *pEntity );
	void Throw_MoveObjectsOutOfWay( CBaseEntity *pEntity, Vector const &tgt );
	void Throw_PerformThrow( CBaseEntity *pEntity, Vector const &tgt );
	Vector Throw_GetTarget();
	void Throw_SetThrown( CBaseEntity *pEntity );
	bool Throw_WasThrown( CBaseEntity *pEntity );

	// Client communications
public:
	void Client_HighlightOn( CBaseEntity *pEntity );
	void Client_HighlightOff( void );
	void Client_HighlightPullOn( CBaseEntity *pEntity );
	void Client_HighlightPullOff( void );
};

LINK_ENTITY_TO_CLASS( npc_combine_advisor_roaming, CNPC_CombineAdvisorRoaming );

IMPLEMENT_SERVERCLASS_ST( CNPC_CombineAdvisorRoaming, DT_CombineAdvisorRoaming )
	SendPropVector(SENDINFO(m_vecHitPos), -1, SPROP_COORD),
END_SEND_TABLE()

BEGIN_DATADESC( CNPC_CombineAdvisorRoaming )

	DEFINE_ENTITYFUNC( FlyTouch ),

	DEFINE_FIELD( m_flNextGroundAttack,FIELD_TIME ),
	DEFINE_FIELD( m_bIsGroundAttacking,FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bCanGroundAttack,	FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flGroundAttackTime,FIELD_TIME ),
	DEFINE_FIELD( m_pRotorWashModel,	FIELD_CLASSPTR ),
	DEFINE_FIELD( m_pSmokeTrail,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_hGroundAttackTarget, FIELD_EHANDLE ),
	DEFINE_SOUNDPATCH( m_pAirExhaustSound ),
	DEFINE_SOUNDPATCH( m_pAirBlastSound ),
	DEFINE_SOUNDPATCH( m_pCannonSound ),
	DEFINE_FIELD( m_vecAngAcceleration,FIELD_VECTOR ),
	DEFINE_FIELD( m_flDeltaT,			FIELD_FLOAT ),
	DEFINE_FIELD( m_flTimeNextAttack,	FIELD_TIME ),
	DEFINE_FIELD( m_flNextSeeEnemySound,	FIELD_TIME ),
	DEFINE_FIELD( m_flEndDestructTime,	FIELD_TIME ),
	DEFINE_FIELD( m_flNextRocket,		FIELD_TIME ),
	DEFINE_FIELD( m_iDoSmokePuff,		FIELD_INTEGER ),
	DEFINE_FIELD( m_iAmmoType,			FIELD_INTEGER ),
	DEFINE_FIELD( m_iBurstSize,		FIELD_INTEGER ),
	DEFINE_FIELD( m_flBurstDelay,		FIELD_FLOAT ),
	DEFINE_FIELD( m_fBlindfire,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fOmniscient,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bIsFiring,			FIELD_BOOLEAN ),
	DEFINE_FIELD( m_iBurstHits,			FIELD_INTEGER ),
	DEFINE_FIELD( m_flTimeNextPing,	FIELD_TIME ),
	DEFINE_FIELD( m_flPenetrationDepth,FIELD_FLOAT ),
	DEFINE_FIELD( m_vecAttackPosition,	FIELD_VECTOR ),
	DEFINE_FIELD( m_vecAttackVelocity,	FIELD_VECTOR ),
	DEFINE_FIELD( m_angGun,				FIELD_VECTOR ),
	DEFINE_PHYSPTR( m_pCrashingController ),
	DEFINE_EMBEDDED( m_crashCallback ),
	DEFINE_FIELD( m_hRagdoll,			FIELD_EHANDLE ),
	DEFINE_FIELD( m_hCrashTarget,		FIELD_EHANDLE ),
	DEFINE_FIELD( m_vecHitPos,			FIELD_VECTOR ),
	DEFINE_FIELD( m_fPatrolLoopPlaying,FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bPreFire,			FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bInvulnerable,		FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flNextAdvisorRoamingCrashFind, FIELD_TIME ),

	DEFINE_ARRAY( m_bDamageOutputsFired, FIELD_BOOLEAN, ADVISOR_ROAMING_NUM_DAMAGE_OUTPUTS ),

	// Function pointers
	DEFINE_INPUTFUNC( FIELD_VOID, "OmniscientOn", InputOmniscientOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "OmniscientOff", InputOmniscientOff ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetPenetrationDepth", InputSetPenetrationDepth ),
	DEFINE_INPUTFUNC( FIELD_VOID, "BlindfireOn", InputBlindfireOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "BlindfireOff", InputBlindfireOff ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SelfDestruct", InputSelfDestruct ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SetDockingBBox", InputSetDockingBBox ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SetNormalBBox", InputSetNormalBBox ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableGroundAttack", InputEnableGroundAttack ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableGroundAttack", InputDisableGroundAttack ),
	DEFINE_INPUTFUNC( FIELD_STRING, "DoGroundAttack", InputDoGroundAttack ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DoAvalancheBlast", InputDoAvalancheBlast ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DoSpitAttack", InputDoSpitAttack ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetMaxSpeed", InputSetMaxSpeed ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "DropShield", InputDropShield ),

	DEFINE_OUTPUT( m_OnFireCannon,		"OnFireCannon" ),
	DEFINE_OUTPUT( m_OnFirstDamage,	"OnFirstDamage" ),
	DEFINE_OUTPUT( m_OnSecondDamage,	"OnSecondDamage" ),
	DEFINE_OUTPUT( m_OnThirdDamage,	"OnThirdDamage" ),
	DEFINE_OUTPUT( m_OnFourthDamage,	"OnFourthDamage" ),
	DEFINE_OUTPUT( m_OnCrashed,			"OnCrashed" ),

//
// Damage control data
//
	DEFINE_UTLVECTOR( m_dmgc_times, FIELD_TIME ),
	DEFINE_UTLVECTOR( m_dmgc_dmgs, FIELD_FLOAT ),
	DEFINE_FIELD( m_dmgc_flTimeInvuln, FIELD_TIME ),
	DEFINE_FIELD( m_dmgc_hPathRetreat, FIELD_EHANDLE ),
	DEFINE_FIELD( m_dmgc_hPathBattle, FIELD_EHANDLE ),
	DEFINE_THINKFUNC( DamageControl_ComeBackToBattle ),
	DEFINE_OUTPUT( m_dmgc_onDmgReached, "OnDmgReached" ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetRetreatTrack", DamageControl_SetRetreatTrack ),

//
// Pull data
//
	DEFINE_FIELD( m_pull_eStage, FIELD_INTEGER ),
	DEFINE_FIELD( m_pull_flTime, FIELD_TIME ),
	DEFINE_FIELD( m_pull_flTongueTime, FIELD_TIME ),
	DEFINE_FIELD( m_pull_flTongue, FIELD_FLOAT ),
	DEFINE_PHYSPTR( m_pull_pHook ),
	DEFINE_PHYSPTR( m_pull_pConstraint ),
	DEFINE_PHYSPTR( m_pull_pController ),
	DEFINE_EMBEDDED( m_pull_fnCallback ),
	DEFINE_THINKFUNC( Pull_Update ),

//
// Spit data
//
	DEFINE_FIELD( m_spit_eStage, FIELD_INTEGER ),
	DEFINE_FIELD( m_spit_flTime, FIELD_TIME ),
	DEFINE_FIELD( m_spit_vel, FIELD_VECTOR ),
	DEFINE_FIELD( m_spit_ball, FIELD_EHANDLE ),
	DEFINE_THINKFUNC( Spit_Update ),

//
// Avalanche data
//
	DEFINE_FIELD( m_avalanche_eStage, FIELD_INTEGER ),
	DEFINE_FIELD( m_avalanche_flTime, FIELD_TIME ),
	DEFINE_FIELD( m_avalanche_front, FIELD_FLOAT ),
	DEFINE_THINKFUNC( Avalanche_Update ),

//
// NPC cooperation data
//
	DEFINE_UTLVECTOR( m_coop_npcs, FIELD_EHANDLE ),

//
// Levitation data
//
	DEFINE_PHYSPTR( m_levitate_pPhysController ),
	DEFINE_EMBEDDED( m_levitate_fnCallback ),
	DEFINE_UTLVECTOR( m_levitate_lstObjects, FIELD_EHANDLE ),
	DEFINE_UTLVECTOR( m_levitate_orgObjects, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_levitate_flTimeAmmoCondition, FIELD_TIME ),
	
	DEFINE_THINKFUNC( Levitate_Update ),
	
	DEFINE_OUTPUT( m_levitate_outEnoughAmmo, "OnEnoughPhysAmmo" ),
	DEFINE_OUTPUT( m_levitate_outLowAmmo, "OnLowPhysAmmo" ),
	DEFINE_OUTPUT( m_levitate_outNoAmmo, "OnOutOfPhysAmmo" ),

//
// Throwing data
//
	DEFINE_UTLVECTOR( m_throw_lstObjects, FIELD_EHANDLE ),
	DEFINE_UTLVECTOR( m_throw_lstThrown, FIELD_EHANDLE ),
	DEFINE_FIELD( m_throw_iObject, FIELD_INTEGER ),
	DEFINE_FIELD( m_throw_flNextTime, FIELD_TIME ),
	DEFINE_FIELD( m_throw_eStage, FIELD_INTEGER ),
	DEFINE_FIELD( m_throw_lastThrowTgt, FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( m_throw_flLastTgtTime, FIELD_TIME ),
	DEFINE_FIELD( m_throw_numTimes, FIELD_INTEGER ),

END_DATADESC()

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CNPC_CombineAdvisorRoaming::CNPC_CombineAdvisorRoaming( void )
{ 
	m_hGroundAttackTarget = NULL;
	m_pSmokeTrail	= NULL;
	m_iAmmoType		= -1; 
	m_pCrashingController = NULL;
	m_hRagdoll = NULL;
	m_hCrashTarget = NULL;
}


//------------------------------------------------------------------------------
// Purpose:
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::Spawn( void )
{
	Precache( );

	SetModel( GetAdvisorRoamingModelName() );
	
	ExtractBbox( SelectHeaviestSequence( ACT_ADVISOR_ROAMING_PATROL ), m_cullBoxMins, m_cullBoxMaxs ); 
	BaseClass::Spawn();

	InitPathingData( ADVISOR_ROAMING_ARRIVE_DIST, ADVISOR_ROAMING_MIN_CHASE_DIST_DIFF,
		sk_advisor_roaming_enemydist_min.GetFloat() );
	AddEFlags( EFL_NO_DISSOLVE | EFL_NO_MEGAPHYSCANNON_RAGDOLL | EFL_NO_PHYSCANNON_INTERACTION );

	m_takedamage = DAMAGE_YES;

	SetHullType(HULL_LARGE_CENTERED);
	SetHullSizeNormal();

	m_iMaxHealth = m_iHealth = sk_advisor_roaming_health.GetInt();

	m_flFieldOfView = 0; // 180 degrees total ( +- 90 degrees cone )

	m_fHelicopterFlags |= BITS_HELICOPTER_GUN_ON;

	InitBoneControllers();

	InitCustomSchedules();

	SetActivity( (Activity)ACT_ADVISOR_ROAMING_PATROL );
	SetCollisionGroup( HL2COLLISION_GROUP_GUNSHIP );

	m_flMaxSpeed = sk_advisor_roaming_speed_max.GetFloat();
	m_flMaxSpeedFiring = sk_advisor_roaming_speed_max.GetFloat();

	m_flTimeNextAttack = gpGlobals->curtime;
	m_flNextSeeEnemySound = gpGlobals->curtime;

	// Init the pose parameters
	SetPoseParameter( "flex_horz", 0 );
	SetPoseParameter( "flex_vert", 0 );
	SetPoseParameter( "arm_accel", 0 );
	SetPoseParameter( "arm_sway", 0 );

	if( m_iAmmoType == -1 )
	{
		// Since there's no weapon to index the ammo type,
		// do it manually here.
		m_iAmmoType = GetAmmoDef()->Index("CombineCannon"); 
	}

	//!!!HACKHACK
	// This tricks the AI code that constantly complains that the advisor_roaming has no schedule.
	SetSchedule( SCHED_IDLE_STAND );

	AddRelationship( "env_flare D_LI 9",	NULL );
	AddRelationship( "rpg_missile D_HT 99", NULL );

	m_flTimeNextPing = gpGlobals->curtime + 2;

	m_flPenetrationDepth = 24;
	m_flBurstDelay = 2.0f;

	// Blindfire default to off
	m_fBlindfire		= false;
	m_fOmniscient		= true;		// Advisor defaults to omniscience
	m_bIsFiring			= false;
	m_bPreFire			= false;
	m_bInvulnerable		= false;
	
	// See if we should start being able to attack
	m_bCanGroundAttack	= true;

	m_flEndDestructTime = 0;

	m_iBurstSize = 0;
	m_iBurstHits = 0;

	// Do not dissolve
	AddEFlags( EFL_NO_DISSOLVE );

	for ( int i = 0; i < ADVISOR_ROAMING_NUM_DAMAGE_OUTPUTS; i++ )
	{
		m_bDamageOutputsFired[i] = false;
	}

	CapabilitiesAdd( bits_CAP_SQUAD);

	// Allows autoaim to help attack the advisor_roaming.
	if( g_pGameRules->GetAutoAimMode() == AUTOAIM_ON_CONSOLE )
	{
		AddFlag( FL_AIMTARGET );
	}

	// The advisor cannot spit when it spawns
	Spit_DelayAttack();
}

void CNPC_CombineAdvisorRoaming::Activate()
{
	BaseClass::Activate();

	m_levitate_fnCallback.m_Advisor = this;
	m_pull_fnCallback.m_Advisor = this;

	Levitate_Start();
	Pull_Update();
}

void CNPC_CombineAdvisorRoaming::OnRestore( void )
{
	BaseClass::OnRestore();

	if ( m_pCrashingController )
	{
		m_pCrashingController->SetEventHandler( &m_crashCallback );
	}

	Levitate_Start();
}

void CNPC_CombineAdvisorRoaming::UpdateOnRemove()
{
	Levitate_Stop();

	BaseClass::UpdateOnRemove();
}

//------------------------------------------------------------------------------
// Purpose:
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::Precache( void )
{
	PrecacheModel( GetAdvisorRoamingModelName() );

	PrecacheModel("sprites/lgtning.vmt");

	PrecacheMaterial( "effects/ar2ground2" );
	PrecacheMaterial( "effects/blueblackflash" );
	
	PrecacheScriptSound( "NPC_CombineGunship.SearchPing" );
	PrecacheScriptSound( "NPC_CombineGunship.PatrolPing" );
	PrecacheScriptSound( "NPC_Strider.Charge" );
	PrecacheScriptSound( "NPC_Strider.Shoot" );
	PrecacheScriptSound( "NPC_CombineGunShip.SeeEnemy" );
	PrecacheScriptSound( "NPC_CombineGunship.CannonStartSound" );
	PrecacheScriptSound( "NPC_CombineGunship.Explode");
	PrecacheScriptSound( "NPC_CombineGunship.Pain" );
	PrecacheScriptSound( "NPC_CombineGunship.CannonStopSound" );

	PrecacheScriptSound( "NPC_CombineGunship.DyingSound" );
	PrecacheScriptSound( "NPC_CombineGunship.CannonSound" );
	PrecacheScriptSound( "NPC_CombineGunship.RotorSound" );
	PrecacheScriptSound( "NPC_CombineGunship.ExhaustSound" );
	PrecacheScriptSound( "NPC_CombineGunship.RotorBlastSound" );

	PrecacheScriptSound( "NPC_CombineBall.WhizFlyby" );

	PrecacheParticleSystem( "Advisor_Psychic_Beam" );
	PrecacheParticleSystem( "Advisor_Pull_Beam" );
	PrecacheParticleSystem( "advisor_object_charge" );
	PrecacheParticleSystem( "blood_impact_synth_01" );
	PrecacheParticleSystem( "blood_impact_synth_01_arc_parent" );

	PrecacheEffect( "ImpactGunship" );
	PrecacheEffect( "AR2Explosion" );
	PrecacheEffect( "GunshipMuzzleFlash" );
	PrecacheEffect( "GunshipTracer" );

	if ( hl2_episodic.GetBool() == true )
	{
		UTIL_PrecacheOther( "env_citadel_energy_core" );
		g_iAdvisorRoamingEffectIndex = PrecacheModel( "sprites/physbeam.vmt" );
	}

	PropBreakablePrecacheAll( MAKE_STRING( GetAdvisorRoamingModelName() ) );

	UTIL_PrecacheOther( "advisor_grenade_spit" );

	BaseClass::Precache();
}

//-----------------------------------------------------------------------------
// Purpose: Cache whatever pose parameters we intend to use
//-----------------------------------------------------------------------------
bool CNPC_CombineAdvisorRoaming::m_sbStaticPoseParamsLoaded = false;
int CNPC_CombineAdvisorRoaming::m_poseFlex_Horz = 0; 
int CNPC_CombineAdvisorRoaming::m_poseFlex_Vert = 0; 
int CNPC_CombineAdvisorRoaming::m_posePitch = 0; 
int CNPC_CombineAdvisorRoaming::m_poseYaw = 0; 
int CNPC_CombineAdvisorRoaming::m_poseArm_Accel = 0; 
int CNPC_CombineAdvisorRoaming::m_poseArm_Sway = 0; 
int CNPC_CombineAdvisorRoaming::m_poseCamera_Pitch = 0; 
int CNPC_CombineAdvisorRoaming::m_poseCamera_Yaw = 0;
int CNPC_CombineAdvisorRoaming::m_bodyTongue = 0;
int CNPC_CombineAdvisorRoaming::m_poseTongue_Length = 0;
int CNPC_CombineAdvisorRoaming::m_attachMouth = 0;
int CNPC_CombineAdvisorRoaming::m_attachSuckTgt = 0;
void	CNPC_CombineAdvisorRoaming::PopulatePoseParameters( void )
{
	if (!m_sbStaticPoseParamsLoaded)
	{
		m_poseFlex_Horz		= LookupPoseParameter( "flex_horz");
		m_poseFlex_Vert			= LookupPoseParameter( "flex_vert" );
		m_posePitch  = LookupPoseParameter( "head_pitch" );
		m_poseYaw   = LookupPoseParameter( "head_yaw" );
		m_poseArm_Accel   = LookupPoseParameter( "arm_accel" );
		m_poseArm_Sway   = LookupPoseParameter( "arm_sway" );

		m_poseCamera_Pitch		= LookupPoseParameter( "camera_pitch" );
		m_poseCamera_Yaw		= LookupPoseParameter( "camera_yaw" );

		m_poseTongue_Length		= LookupPoseParameter( "tongue_length" );

		m_bodyTongue		= FindBodygroupByName( "tongue" );

		m_attachMouth		= LookupAttachment( "attach_mouth" );
		m_attachSuckTgt		= LookupAttachment( "tongue_target" );

		m_sbStaticPoseParamsLoaded = true;
	}

	BaseClass::PopulatePoseParameters();
}


//------------------------------------------------------------------------------
// Purpose : 
//------------------------------------------------------------------------------
CNPC_CombineAdvisorRoaming::~CNPC_CombineAdvisorRoaming(void)
{
	StopLoopingSounds();

	if ( m_pCrashingController )
	{
		physenv->DestroyMotionController( m_pCrashingController );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::Ping( void )
{
	if( IsCrashing() )
		return;

	if( GetEnemy() != NULL )
	{
		if( !HasCondition(COND_SEE_ENEMY) && gpGlobals->curtime > m_flTimeNextPing )
		{
			EmitSound( "NPC_CombineGunship.SearchPing" );
			m_flTimeNextPing = gpGlobals->curtime + 3;
		}
	}
	else
	{
		if( gpGlobals->curtime > m_flTimeNextPing )
		{
			EmitSound( "NPC_CombineGunship.PatrolPing" );
			m_flTimeNextPing = gpGlobals->curtime + 3;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &pos - 
// Output : float
//-----------------------------------------------------------------------------
float CNPC_CombineAdvisorRoaming::GroundDistToPosition( const Vector &pos )
{
	Vector vecDiff;
	VectorSubtract( GetAbsOrigin(), pos, vecDiff );

	// Only interested in the 2d dist
	vecDiff.z = 0;

	return vecDiff.Length();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::PlayPatrolLoop( void )
{
	m_fPatrolLoopPlaying = true;
	/*
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	controller.SoundChangeVolume( m_pPatrolSound, 1.0, 1.0 );
	controller.SoundChangeVolume( m_pAngrySound, 0.0, 1.0 );
	*/
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::PlayAngryLoop( void )
{
	m_fPatrolLoopPlaying = false;
	/*
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	controller.SoundChangeVolume( m_pPatrolSound, 0.0, 1.0 );
	controller.SoundChangeVolume( m_pAngrySound, 1.0, 1.0 );
	*/
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::HelicopterPostThink( void )
{
	// After HelicopterThink()
	if ( HasCondition( COND_ENEMY_DEAD ) )
	{
		if ( m_bIsFiring )
		{
			// Fire more shots at the dead body for effect
			if ( m_iBurstSize > 8 )
			{
				m_iBurstSize = 8;
			}
		}

		// Fade out search sound, fade in patrol sound.
		PlayPatrolLoop();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Vector
//-----------------------------------------------------------------------------
Vector CNPC_CombineAdvisorRoaming::GetGroundAttackHitPosition( void )
{
	trace_t	tr;
	Vector	vecShootPos, vecShootDir;

	GetAttachment( "BellyGun", vecShootPos, &vecShootDir, NULL, NULL );

	AI_TraceLine( vecShootPos, vecShootPos + Vector( 0, 0, -MAX_TRACE_LENGTH ), MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );

	if ( m_hGroundAttackTarget )
	{
		return Vector( tr.endpos.x, tr.endpos.y, m_hGroundAttackTarget->WorldSpaceCenter().z );
	}
	return tr.endpos;	
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CombineAdvisorRoaming::CheckGroundAttack( void )
{
	if ( m_bCanGroundAttack == false )
		return false;

	if ( m_bIsGroundAttacking )
		return false;

	// Must have an enemy
	if ( GetEnemy() == NULL )
		return false;

	// Must not have done it too recently
	if ( m_flNextGroundAttack > gpGlobals->curtime )
		return false;

	Vector	predPos, predDest;
	
	// Find where the enemy is most likely to be in two seconds
	UTIL_PredictedPosition( GetEnemy(), 1.0f, &predPos );
	UTIL_PredictedPosition( this, 1.0f, &predDest );

	Vector	predGap = ( predDest - predPos );
	predGap.z = 0;

	float	predDistance = predGap.Length();

	// Must be within distance
	if ( predDistance > MIN_GROUND_ATTACK_DIST )
		return false;

	// Can't ground attack missiles
	if ( IsTargettingMissile() )
		return false;

	//FIXME: Check to make sure we're not firing too far above or below the target
	if ( fabs( GetGroundAttackHitPosition().z - GetEnemy()->WorldSpaceCenter().z ) > MIN_GROUND_ATTACK_HEIGHT_DIFF )
		return false;

	//FIXME: Check for ground movement capabilities?

	//TODO: Check for friendly-fire

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::StartGroundAttack( void )
{
	Warning( "CNPC_CombineAdvisorRoaming::StartGroundAttack\n" );
	return;

	// Mark us as attacking
	m_bIsGroundAttacking = true;
	m_flGroundAttackTime = gpGlobals->curtime + 3.0f;

	// Setup the attack effects
	Vector	vecShootPos;

	GetAttachment( "BellyGun", vecShootPos );

	EntityMessageBegin( this, true );
		WRITE_BYTE( ADVISOR_ROAMING_MSG_STREAKS );
		WRITE_VEC3COORD( vecShootPos );
	MessageEnd();

	CPASAttenuationFilter filter2( this, "NPC_Strider.Charge" );
	EmitSound( filter2, entindex(), "NPC_Strider.Charge" );

	Vector	endpos = GetGroundAttackHitPosition();
	
	CSoundEnt::InsertSound ( SOUND_DANGER, endpos, 1024, 0.5f );
}

#define ADVISOR_ROAMING_BELLY_BLAST_RADIUS 256.0f
#define BELLY_BLAST_MAX_PUNCH 5

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::ManageWarningBeam( void )
{
	Vector vecSrc, vecShootDir;
	GetAttachment( "BellyGun", vecSrc, NULL, NULL, NULL );

	trace_t	tr;
	CTraceFilterSkipTwoEntities filter( m_hGroundAttackTarget, this, COLLISION_GROUP_NONE );

	UTIL_TraceLine( vecSrc, m_vecHitPos, MASK_SOLID, &filter, &tr );

	int iPunch = 0;

	while ( tr.endpos != m_vecHitPos )
	{
		iPunch++;

		if ( iPunch > BELLY_BLAST_MAX_PUNCH )
			break;

		if ( tr.fraction != 1.0 )
		{
			if ( tr.m_pEnt )
			{
				CTakeDamageInfo	info( this, this, 1.0f, DMG_ENERGYBEAM );

				Vector vTargetDir = tr.m_pEnt->BodyTarget( tr.endpos, false ) - tr.endpos;

				VectorNormalize( vTargetDir );

				info.SetDamagePosition( tr.endpos + ( tr.plane.normal * 64.0f ) );
				info.SetDamageForce( vTargetDir * 100 );

				if ( tr.m_pEnt->m_takedamage != DAMAGE_NO )
				{
					// Deal damage
					tr.m_pEnt->TakeDamage( info );
				}
			}

			Vector vDir = m_vecHitPos - vecSrc;
			VectorNormalize( vDir );

			Vector vStartPunch = tr.endpos + vDir * 1;

			UTIL_TraceLine( vStartPunch, m_vecHitPos, MASK_SOLID, &filter, &tr );

			if ( tr.startsolid )
			{
				float flLength = (vStartPunch - tr.endpos).Length();

				Vector vEndPunch = vStartPunch + vDir * ( flLength * tr.fractionleftsolid );

				UTIL_TraceLine( vEndPunch, m_vecHitPos, MASK_SOLID, &filter, &tr );

				trace_t tr2;
				UTIL_TraceLine( vEndPunch, vEndPunch - vDir * 2, MASK_SOLID, &filter, &tr2 );

				if ( (m_flGroundAttackTime - gpGlobals->curtime) <= 2.0f )
				{
					g_pEffects->EnergySplash( tr2.endpos + vDir * 8, tr2.plane.normal, true );
				}

				g_pEffects->Sparks( tr2.endpos, 3.0f - (m_flGroundAttackTime-gpGlobals->curtime), 3.5f - (m_flGroundAttackTime-gpGlobals->curtime), &tr2.plane.normal );

			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::DoBellyBlastDamage( trace_t &tr, Vector vMins, Vector vMaxs )
{
	Warning( "CNPC_CombineAdvisorRoaming::DoBellyBlastDamage\n" );
	return;

	CBaseEntity*	pList[100];

	if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_BELLYBLAST )
	{
		NDebugOverlay::Box( tr.endpos, vMins, vMaxs, 255, 255, 0, true, 5.0f );
	}

	int count = UTIL_EntitiesInBox( pList, 100, tr.endpos + vMins, tr.endpos + vMaxs, 0 );

	for ( int i = 0; i < count; i++ )
	{
		CBaseEntity *pEntity = pList[i];

		if ( pEntity == this )
			continue;

		if ( pEntity->m_takedamage == DAMAGE_NO )
			continue;

		float damage = 150;

		if ( pEntity->IsPlayer() )
		{
			float damageDist = ( pEntity->GetAbsOrigin() - tr.endpos ).Length();
			damage = RemapValClamped( damageDist, 0, 300, 200, 0 );
		}

		CTakeDamageInfo	info( this, this, damage, DMG_DISSOLVE );

		Vector vTargetDir = pEntity->BodyTarget( tr.endpos, false ) - tr.endpos;

		VectorNormalize( vTargetDir );

		info.SetDamagePosition( tr.endpos + ( tr.plane.normal * 64.0f ) );
		info.SetDamageForce( vTargetDir * 25000 );

		// Deal damage
		pEntity->TakeDamage( info );

		trace_t	groundTrace;
		UTIL_TraceLine( pEntity->GetAbsOrigin(), pEntity->GetAbsOrigin() - Vector( 0, 0, 256 ), MASK_SOLID, pEntity, COLLISION_GROUP_NONE, &groundTrace );

		if ( tr.fraction < 1.0f )
		{
			CEffectData	data;

			// Find the floor and add a dissolve explosion at that point
			data.m_flRadius = ADVISOR_ROAMING_BELLY_BLAST_RADIUS * 0.5f;
			data.m_vNormal	= groundTrace.plane.normal;
			data.m_vOrigin	= groundTrace.endpos;

			DispatchEffect( "AR2Explosion", data );
		}

		// If the creature was killed, then dissolve it
		if ( pEntity->GetHealth() <= 0.0f )
		{
			if ( pEntity->GetBaseAnimating() != NULL && !pEntity->IsEFlagSet( EFL_NO_DISSOLVE ) )
			{
				pEntity->GetBaseAnimating()->Dissolve( NULL, gpGlobals->curtime );
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::DoGroundAttackExplosion( void )
{
	Warning( "CNPC_CombineAdvisorRoaming::DoGroundAttackExplosion\n" );
	return;

	// Fire the bullets
	Vector vecSrc, vecShootDir;
	Vector vecAttachmentOrigin;
	GetAttachment( "BellyGun", vecAttachmentOrigin, &vecShootDir, NULL, NULL );

	vecSrc = vecAttachmentOrigin;

	if ( m_hGroundAttackTarget )
	{
		vecSrc = m_hGroundAttackTarget->GetAbsOrigin();
	}

	Vector impactPoint = vecSrc + ( Vector( 0, 0, -1 ) * MAX_TRACE_LENGTH );

	trace_t	tr;
	UTIL_TraceLine( vecSrc, impactPoint, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );
	UTIL_DecalTrace( &tr, "Scorch" );

	if ( hl2_episodic.GetBool() == true )
	{
		g_pEffects->EnergySplash( tr.endpos, tr.plane.normal );

		CBroadcastRecipientFilter filter;
		te->BeamRingPoint( filter, 0.0, 
			tr.endpos,							//origin
			0,									//start radius
			ADVISOR_ROAMING_BELLY_BLAST_RADIUS,			//end radius
			g_iAdvisorRoamingEffectIndex,				//texture
			0,									//halo index
			0,									//start frame
			0,									//framerate
			0.2,								//life
			10,									//width
			0,									//spread
			0,									//amplitude
			255,								//r
			255,								//g
			255,								//b
			50,									//a
			0,									//speed
			FBEAM_FADEOUT
			);
	}

	// Send the effect over
	CEffectData	data;

	// Do an extra effect if we struck the world
	if ( tr.m_pEnt && tr.m_pEnt->IsWorld() )
	{
		data.m_flRadius = ADVISOR_ROAMING_BELLY_BLAST_RADIUS;
		data.m_vNormal	= tr.plane.normal;
		data.m_vOrigin	= tr.endpos;
		
		DispatchEffect( "AR2Explosion", data );
	}

	float flZLength = vecAttachmentOrigin.z - tr.endpos.z;

	Vector vBeamMins = Vector( -16, -16, 0 );
	Vector vBeamMaxs = Vector( 16, 16, flZLength );

	DoBellyBlastDamage( tr, vBeamMins, vBeamMaxs );

	Vector vBlastMins = Vector( -ADVISOR_ROAMING_BELLY_BLAST_RADIUS, -ADVISOR_ROAMING_BELLY_BLAST_RADIUS, 0 );
	Vector vBlastMaxs = Vector( ADVISOR_ROAMING_BELLY_BLAST_RADIUS, ADVISOR_ROAMING_BELLY_BLAST_RADIUS, 96 );

	DoBellyBlastDamage( tr, vBlastMins, vBlastMaxs );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::StopGroundAttack( bool bDoAttack )
{
	return;

	if ( !m_bIsGroundAttacking )
		return;

	// Mark us as no longer attacking
	m_bIsGroundAttacking = false;
	m_flNextGroundAttack = gpGlobals->curtime + 4.0f;
	m_flTimeNextAttack	 = gpGlobals->curtime + 2.0f;

	Vector	hitPos = GetGroundAttackHitPosition();

	// tell the client side effect to complete
	EntityMessageBegin( this, true );
		WRITE_BYTE( ADVISOR_ROAMING_MSG_BIG_SHOT );
		WRITE_VEC3COORD( hitPos );
	MessageEnd();

	// Only attack if told to
	if ( bDoAttack )
	{
		CPASAttenuationFilter filter2( this, "NPC_Strider.Shoot" );
		EmitSound( filter2, entindex(), "NPC_Strider.Shoot");

		ApplyAbsVelocityImpulse( Vector( 0, 0, 200.0f ) );

		//ExplosionCreate( hitPos, QAngle( 0, 0, 1 ), this, 500, 500, true );
		DoGroundAttackExplosion();
	}

	// If we were attacking a target, revert to our previous target
	if ( m_hGroundAttackTarget )
	{
		m_hGroundAttackTarget = NULL;
		if ( GetDestPathTarget() )
		{
			// Return to our old path
			SetupNewCurrentTarget( GetDestPathTarget() );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::DrawRotorWash( float flAltitude, const Vector &vecRotorOrigin )
{
	// If we have a ragdoll, we want the wash under that, not me
	if ( m_hRagdoll )
	{
		BaseClass::DrawRotorWash( flAltitude, m_hRagdoll->GetAbsOrigin() );
		return;
	}

	// Don't use rotor wash
	// BaseClass::DrawRotorWash( flAltitude, vecRotorOrigin );
}

//------------------------------------------------------------------------------
// Purpose : Override the desired position if your derived helicopter is doing something special
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::UpdateDesiredPosition( void )
{
	if ( m_hCrashTarget )
	{
		SetDesiredPosition( m_hCrashTarget->WorldSpaceCenter() + Vector(0,0,128) );
	}
	else if ( m_hGroundAttackTarget )
	{
		SetDesiredPosition( m_hGroundAttackTarget->GetAbsOrigin() + Vector(0,0,ADVISOR_ROAMING_BELLYBLAST_TARGET_HEIGHT) );
	}
	else if ( m_avalanche_eStage >= AS_DESCENDING )
	{
		Vector pos = GetAbsOrigin();
		Vector dir( 0, 0, -2048 );
		trace_t tr;
		UTIL_TraceLine( pos, pos + dir, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
		SetDesiredPosition( pos + tr.fraction * dir );
	}
	else if ( m_pull_pHook && m_pull_eStage >= PULL_PULLING )
	{
 		Vector pos;
		if ( CBaseEntity *pEnemy = GetEnemy() )
			pos = pEnemy->EyePosition();
		else
 			m_pull_pHook->GetPosition( &pos, NULL );

		Vector posOrig = GetAbsOrigin();
		Vector posSuckTgt;
		GetAttachment( m_attachSuckTgt, posSuckTgt );

		pos.z = posOrig.z;
		
		pos += ( posOrig - posSuckTgt );

		SetDesiredPosition( pos );
	}
}

//-----------------------------------------------------------------------------
// Purpose: do all of the stuff related to having an enemy, attacking, etc.
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::DoCombat( void )
{
	// Check for enemy change-overs
	if ( HasEnemy() )
	{
		if ( HasCondition( COND_NEW_ENEMY ) )
		{
			if ( GetEnemy() && GetEnemy()->IsPlayer() && m_flNextSeeEnemySound < gpGlobals->curtime )
			{
				m_flNextSeeEnemySound = gpGlobals->curtime + 5.0;

				EmitSound( "NPC_CombineGunship.SeeEnemy" );
			}

			// If we're shooting at a missile, do it immediately!
			if ( IsTargettingMissile() )
			{
				EmitSound( "NPC_CombineGunship.SeeMissile" );

				// Allow the advisor_roaming to attack again immediately
				if ( ( m_flTimeNextAttack > gpGlobals->curtime ) && ( ( m_flTimeNextAttack - gpGlobals->curtime ) > ADVISOR_ROAMING_MISSILE_MAX_RESPONSE_TIME ) )
				{
					m_flTimeNextAttack = gpGlobals->curtime + ADVISOR_ROAMING_MISSILE_MAX_RESPONSE_TIME;
					m_iBurstSize = sk_advisor_roaming_burst_size.GetInt();
				}
			}

			// Fade in angry sound, fade out patrol sound.
			PlayAngryLoop();
		}
	}

	// Do we have a belly blast target?
	if ( m_hGroundAttackTarget && !m_bIsGroundAttacking )
	{
		// If we're over it, blast. Can't use GetDesiredPosition() because it's not updated yet.
		Vector vecTarget = m_hGroundAttackTarget->GetAbsOrigin() + Vector(0,0,ADVISOR_ROAMING_BELLYBLAST_TARGET_HEIGHT);
		Vector vecToTarget = (vecTarget - GetAbsOrigin());
		float flDistance = vecToTarget.Length();

		// Get the difference between our velocity & the target's velocity
		Vector vec2DVelocity = GetAbsVelocity();
		Vector vec2DTargetVelocity = m_hGroundAttackTarget->GetAbsVelocity();
		vec2DVelocity.z = vec2DTargetVelocity.z = 0;
		float flVelocityDiff = (vec2DVelocity - vec2DTargetVelocity).Length();
		if ( flDistance < 100 && flVelocityDiff < 200 )
		{
			StartGroundAttack();
		}
	}

	// Update our firing
	if ( m_bIsFiring )
	{
		// Fire if we have rounds remaining in this burst
		if ( ( m_iBurstSize > 0 ) && ( gpGlobals->curtime > m_flTimeNextAttack ) )
		{
			UpdateEnemyTarget();
			FireCannonRound();
		}
		else if ( m_iBurstSize < 1 )
		{
			// We're done firing
			StopCannonBurst();
			
			if ( IsTargettingMissile() )
			{
				m_flTimeNextAttack = gpGlobals->curtime + 0.5f;
			}
		}
	}
	else
	{
		// If we're not firing, look at the enemy
		if ( GetEnemy() )
		{
			m_vecAttackPosition = GetEnemy()->EyePosition();
		}

#ifdef BELLYBLAST
		// Check for a ground attack
		if ( CheckGroundAttack() )
		{
			StartGroundAttack();
		}
#endif

		// See if we're attacking
		if ( m_bIsGroundAttacking )
		{
			m_vecHitPos = GetGroundAttackHitPosition();

			ManageWarningBeam();

			// If our time is up, fire the blast and be done
			if ( m_flGroundAttackTime < gpGlobals->curtime )
			{
				// Fire!
				StopGroundAttack( true );
			}
		}
	}

	// If we're using the chopper model, align the gun towards the target
	{
		Vector vGunPosition;
		GetAttachment( "muzzle", vGunPosition );
		Vector vecToAttackPos = (m_vecAttackPosition - vGunPosition);
		PoseGunTowardTargetDirection( vecToAttackPos );
	}

	// Forget flares once I've seen them for a while
	float flDeltaSeen = m_flLastSeen - m_flPrevSeen;
	if ( GetEnemy() != NULL && GetEnemy()->Classify() == CLASS_FLARE && flDeltaSeen > ADVISOR_ROAMING_FLARE_IGNORE_TIME )
	{
		AddEntityRelationship( GetEnemy(), D_NU, 5 );

		PlayPatrolLoop();

		// Forget the flare now.
		SetEnemy( NULL );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CombineAdvisorRoaming::ChooseEnemy( void )
{
	// If we're firing, don't switch enemies. This stops the advisor_roaming occasionally 
	// stopping a burst before he's really fired at all, which makes him look indecisive.
	if ( m_bIsFiring )
		return true;

	return BaseClass::ChooseEnemy();
}

//-----------------------------------------------------------------------------
// Purpose: There's a lot of code in here now. We should consider moving 
//			helicopters and such to scheduled AI. (sjb)
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::MoveHead( void )
{
	float flYaw = GetPoseParameter( m_poseFlex_Horz );
	float flPitch = GetPoseParameter( m_poseFlex_Vert );

/*
	This head-turning code will cause the head to POP when switching from looking at the enemy
	to looking according to the flight model. I will fix this later. Right now I'm turning
	the code over to Ken for some aiming fixups. (sjb)
*/

	if ( m_pull_eStage >= PULL_PULLING )
	{
		flYaw = UTIL_Approach( 0.0f, flYaw, 2.0 * 10 * m_flDeltaT );	
		flPitch = UTIL_Approach( 0.0f, flPitch, 2.0 * 10 * m_flDeltaT );	
	}
	else
	{
		float flDot = 0.0f;
		
		Vector vecToEnemy, vecAimDir;
		Vector vTargetPos, vGunPosition;
		Vector vecTargetOffset;
		QAngle vGunAngles;
		
		if ( GetEnemy() != NULL )
		{
			GetAttachment( "attach_mouth", vGunPosition, vGunAngles );

			vTargetPos = GetEnemyTarget();

			VectorSubtract( vTargetPos, vGunPosition, vecToEnemy );
			VectorNormalize( vecToEnemy );
			
			// get angles relative to body position
			AngleVectors( GetAbsAngles(), &vecAimDir );
			flDot = DotProduct( vecAimDir, vecToEnemy );
		}

		// Look at Enemy!!
		if ( flDot > 0.3f )
		{
			float flDiff;

			float flDesiredYaw = VecToYaw(vTargetPos - vGunPosition);
			flDiff = UTIL_AngleDiff( flDesiredYaw, vGunAngles.y ) * 0.90;
			flYaw = UTIL_Approach( flYaw + flDiff, flYaw, 5.0 );	

			float flDesiredPitch = UTIL_VecToPitch(vTargetPos - vGunPosition);
			flDiff = UTIL_AngleDiff( flDesiredPitch, vGunAngles.x ) * 0.90;
			flPitch = UTIL_Approach( flPitch + flDiff, flPitch, 5.0 );	
		}
		else
		{
			// Look where going!
			// old way- look according to rotational velocity
			flYaw = UTIL_Approach( GetLocalAngularVelocity().y, flYaw, 2.0 * 10 * m_flDeltaT );	
			flPitch = UTIL_Approach( GetLocalAngularVelocity().x, flPitch, 2.0 * 10 * m_flDeltaT );	
		}
	}

	// Set the body flexes
	SetPoseParameter( m_poseFlex_Vert, flPitch );
	SetPoseParameter( m_poseFlex_Horz, flYaw );

	// DbgDisplay( 11, "flex_vert", flPitch );
	// DbgDisplay( 12, "flex_horz", flYaw );
}


//-----------------------------------------------------------------------------
// Purpose: There's a lot of code in here now. We should consider moving 
//			helicopters and such to scheduled AI. (sjb)
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::PrescheduleThink( void )
{
	m_flDeltaT = gpGlobals->curtime - GetLastThink();

	// Are we crashing?
	if ( m_flEndDestructTime && gpGlobals->curtime > m_flEndDestructTime )
	{
		// We're dead, remove ourselves
		SelfDestruct();
		return;
	}

	if( m_lifeState == LIFE_ALIVE )
	{
		// Chopper doesn't ping
		Ping();

		DoCombat();
		MoveHead();
	}
	else if( m_lifeState == LIFE_DYING )
	{
		// Increase the number of explosions as he gets closer to death
		bool bCreateExplosion = false;
		float flTimeLeft = m_flEndDestructTime - gpGlobals->curtime;
		if ( flTimeLeft > 1.5 )
		{
			bCreateExplosion = (random->RandomInt( 0, 3 ) == 0);
		}
		else 
		{
			bCreateExplosion = (random->RandomInt( 0, 2 ) == 0);
		}

		if ( bCreateExplosion )
		{
			Vector explodePoint;
			if ( m_hRagdoll )
			{
				m_hRagdoll->CollisionProp()->RandomPointInBounds( Vector(0.25,0.25,0.25), Vector(0.75,0.75,0.75), &explodePoint );
			}
			else
			{
				CollisionProp()->RandomPointInBounds( Vector(0.25,0.25,0.25), Vector(0.75,0.75,0.75), &explodePoint );

				// Knock the advisor_roaming a little, but not if we're trying to fly to a point
				if ( !m_hCrashTarget )
				{
					Vector vecPush = (GetAbsOrigin() - explodePoint);
					VectorNormalize( vecPush );
					ApplyAbsVelocityImpulse( vecPush * 128 );
				}
			}

			ExplosionCreate( explodePoint, QAngle(0,0,1), this, 100, 128, false );
		}

		// Have we reached our crash point?
		if ( m_flNextAdvisorRoamingCrashFind && !m_hRagdoll )
		{
			// Update nearest crash point. The RPG that killed us may have knocked us
			// closer to a different point than the one we were near when we first died.
			if ( m_flNextAdvisorRoamingCrashFind < gpGlobals->curtime )
			{
				FindNearestAdvisorRoamingCrash();
			}

			if ( m_hCrashTarget )
			{
				MoveHead();

				UpdateDesiredPosition();

				// If we're over it, destruct
				Vector vecToTarget = (GetDesiredPosition() - GetAbsOrigin());
				if ( vecToTarget.LengthSqr() < (384 * 384) )
				{
					BeginDestruct();
					m_OnCrashed.FireOutput( this, this );
					m_hCrashTarget->AdvisorRoamingCrashedOnTarget();
					return;
				}
			}
		}
	}

	BaseClass::PrescheduleThink();

// 	SetPoseParameter( m_posePitch, random->RandomFloat( ADVISOR_ROAMING_HEAD_MAX_LEFT, ADVISOR_ROAMING_HEAD_MAX_RIGHT ) );
// 	SetPoseParameter( m_poseYaw, random->RandomFloat( ADVISOR_ROAMING_HEAD_MAX_UP, ADVISOR_ROAMING_HEAD_MAX_DOWN ) );
}

//------------------------------------------------------------------------------
// Purpose :	If the enemy is in front of the gun, load up a burst. 
//				Actual gunfire is handled in PrescheduleThink
// Input   : 
// Output  : 
//------------------------------------------------------------------------------
bool CNPC_CombineAdvisorRoaming::FireGun( void )
{
	CNPC_CombineAdvisorRoaming::FireWeapons();
	return false;

	if ( m_lifeState != LIFE_ALIVE )
		return false;

	if ( m_bIsGroundAttacking )
		return false;

	if ( false && GetEnemy() && !m_bIsFiring && gpGlobals->curtime > m_flTimeNextAttack )
		// Shooting bullets
	{
		// We want to decelerate to attack
		if (m_flGoalSpeed > GetMaxSpeedFiring() )
		{
			m_flGoalSpeed = GetMaxSpeedFiring();
		}

		bool bTargetingMissile = IsTargettingMissile();
		if ( !bTargetingMissile && !m_bPreFire )
		{
			m_bPreFire = true;
			m_flTimeNextAttack = gpGlobals->curtime + 0.5f;
			
			EmitSound( "NPC_CombineGunship.CannonStartSound" );
			return false;
		}

		//TODO: Emit the danger noise and wait until it's finished

		// Don't fire at an occluded enemy unless blindfire is on.
		if ( HasCondition( COND_ENEMY_OCCLUDED ) && ( m_fBlindfire == false ) )
			return false;

		// Don't shoot if the enemy is too close
// 		if ( !bTargetingMissile && GroundDistToPosition( GetEnemy()->GetAbsOrigin() ) < ADVISOR_ROAMING_STITCH_MIN )
// 			return false;

		Vector vecAimDir, vecToEnemy;
		Vector vecMuzzle, vecEnemyTarget;

		GetAttachment( "muzzle", vecMuzzle, &vecAimDir, NULL, NULL );
		vecEnemyTarget = GetEnemyTarget();

		// Aim with the muzzle's attachment point.
		VectorSubtract( vecEnemyTarget, vecMuzzle, vecToEnemy );

		VectorNormalize( vecToEnemy );
		VectorNormalize( vecAimDir );

		if ( DotProduct( vecToEnemy, vecAimDir ) > 0.9 )
		{
			StartCannonBurst( sk_advisor_roaming_burst_size.GetInt() );
			return true;
		}

		return false;
	}

	return false;
}

void CNPC_CombineAdvisorRoaming::FireWeapons()
{
	// Called every think time

	Throw_Update();

	// If we don't have the shield for a while then spit
	if ( sk_advisor_roaming_spit_noshield.GetBool() &&
		 m_levitate_lstObjects.Count() <= 0 &&
		 !Control_GetOtherAttackInProgress( ATK_SPIT ) &&
		 gpGlobals->curtime > m_spit_flTime )
	{
		Spit_Prepare();
	}
}

//------------------------------------------------------------------------------
// Purpose: Fire a round from the cannon
// Notes:	Only call this if you have an enemy.
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::FireCannonRound( void )
{
	Vector vecPenetrate;
	trace_t tr;

	Vector vecToEnemy, vecEnemyTarget;
	Vector vecMuzzle;
	Vector vecAimDir;

	GetAttachment( "muzzle", vecMuzzle, &vecAimDir );
	vecEnemyTarget = GetEnemyTarget();
	
	// Aim with the muzzle's attachment point.
	VectorSubtract( vecEnemyTarget, vecMuzzle, vecToEnemy );
	VectorNormalize( vecToEnemy );

	// If the gun is wildly off target, stop firing!
	// FIXME  - this should use a vector pointing 
	// to the enemy's location PLUS the stitching 
	// error! (sjb) !!!BUGBUG

	if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_STITCHING )
	{
		QAngle vecAimAngle;
		Vector	vForward, vRight, vUp;
		GetAttachment( "muzzle", vecMuzzle, &vForward, &vRight, &vUp );
		AngleVectors( vecAimAngle, &vForward, &vRight, &vUp );
		NDebugOverlay::Line( vecMuzzle, vecEnemyTarget, 255, 255, 0, true, 1.0f );

		NDebugOverlay::Line( vecMuzzle, vecMuzzle + ( vForward * 64.0f ), 255, 0, 0, true, 1.0f );
		NDebugOverlay::Line( vecMuzzle, vecMuzzle + ( vRight * 32.0f ), 0, 255, 0, true, 1.0f );
		NDebugOverlay::Line( vecMuzzle, vecMuzzle + ( vUp * 32.0f ), 0, 0, 255, true, 1.0f );
	}

	// Robin: Check the dotproduct to the enemy, NOT to the offsetted firing angle
	// Fixes problems firing at close enemies, where the enemy is valid but
	// the offset firing stitch isn't.
	Vector vecDotCheck = vecToEnemy;
	if ( GetEnemy() )
	{
		VectorSubtract( GetEnemy()->GetAbsOrigin(), vecMuzzle, vecDotCheck );
		VectorNormalize( vecDotCheck );
	}

	if ( DotProduct( vecDotCheck, vecAimDir ) < 0.8f )
	{
		StopCannonBurst();
		return;
	}

	DoMuzzleFlash();

	m_OnFireCannon.FireOutput( this, this, 0 );

	m_flTimeNextAttack = gpGlobals->curtime + 0.05f;

	float flPrevHealth = 0;
	if ( GetEnemy() )
	{
		flPrevHealth = GetEnemy()->GetHealth();
	}

	// Make sure we hit missiles
	if ( IsTargettingMissile() )
	{
		// Fire a fake shot
		FireBullets( 1, vecMuzzle, vecToEnemy, VECTOR_CONE_5DEGREES, 8192, m_iAmmoType, 1 );

		CBaseEntity *pMissile = GetEnemy();

		Vector	missileDir, threatDir;

		AngleVectors( pMissile->GetAbsAngles(), &missileDir );

		threatDir = ( WorldSpaceCenter() - pMissile->GetAbsOrigin() );
		float	threatDist = VectorNormalize( threatDir );

		// Check that the target is within some threshold
		if ( ( DotProduct( threatDir, missileDir ) > 0.95f ) && ( threatDist < 1024.0f ) )
		{
			if ( random->RandomInt( 0, 1 ) == 0 )
			{
				CTakeDamageInfo info( this, this, 200, DMG_MISSILEDEFENSE );
				CalculateBulletDamageForce( &info, m_iAmmoType, -threatDir, WorldSpaceCenter() );
				GetEnemy()->TakeDamage( info );
			}
		}
		else
		{
			//FIXME: Some other metric
		}
	}
	else
	{
		m_iBurstSize--;

		// Fire directly at the target
		FireBulletsInfo_t info( 1, vecMuzzle, vecToEnemy, vec3_origin, MAX_COORD_RANGE, m_iAmmoType );
		info.m_iTracerFreq = 1;
		CAmmoDef *pAmmoDef = GetAmmoDef();
		info.m_flPlayerDamage = pAmmoDef->PlrDamage( m_iAmmoType );

		// If we've already hit the player, do 0 damage. This ensures we don't hit the
		// player multiple times during a single burst.
		if ( m_iBurstHits >= ADVISOR_ROAMING_MAX_HITS_PER_BURST )
		{
			info.m_flPlayerDamage = 1.0f;
		}

		FireBullets( info );

		if ( GetEnemy() && flPrevHealth != GetEnemy()->GetHealth() )
		{
			m_iBurstHits++;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::DoMuzzleFlash( void )
{
	BaseClass::DoMuzzleFlash();
	
	CEffectData data;

	data.m_nAttachmentIndex = LookupAttachment( "muzzle" );
	data.m_nEntIndex = entindex();
	DispatchEffect( "GunshipMuzzleFlash", data );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CombineAdvisorRoaming::FVisible( CBaseEntity *pEntity, int traceMask, CBaseEntity **ppBlocker )
{
	bool fReturn = BaseClass::FVisible( pEntity, traceMask, ppBlocker );

	if( m_fOmniscient )
	{
		if( !fReturn )
		{
			// Set this condition so that we can check it later and know that the 
			// enemy truly is occluded, but the advisor_roaming regards it as visible due 
			// to omniscience.
			SetCondition( COND_ENEMY_OCCLUDED );
		}
		else
		{
			ClearCondition( COND_ENEMY_OCCLUDED );
		}

		return true;
	}

	if( fReturn )
	{
		ClearCondition( COND_ENEMY_OCCLUDED );
	}
	else
	{
		SetCondition( COND_ENEMY_OCCLUDED );
	}

	return fReturn;
}


//-----------------------------------------------------------------------------
// Purpose: Change the depth that advisor_roaming bullets can penetrate through solids
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputSetPenetrationDepth( inputdata_t &inputdata )
{
	m_flPenetrationDepth = inputdata.value.Float();
}

void CNPC_CombineAdvisorRoaming::InputSetMaxSpeed( inputdata_t &inputdata )
{
	m_flMaxSpeed = inputdata.value.Float();
	m_flMaxSpeedFiring = m_flMaxSpeed;
}

void CNPC_CombineAdvisorRoaming::InputDropShield( inputdata_t &inputdata )
{
	float flSeconds = inputdata.value.Float();
	
	if ( m_levitate_lstObjects.Count() > 0 )
	{
		Levitate_Stop( flSeconds );
	}
}


//-----------------------------------------------------------------------------
// Purpose: Allow the advisor_roaming to sense its enemy's location even when enemy
//			is hidden from sight.
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputOmniscientOn( inputdata_t &inputdata )
{
	m_fOmniscient = true;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the advisor_roaming to its default requirement that it see the 
//			enemy to know its current position
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputOmniscientOff( inputdata_t &inputdata )
{
	m_fOmniscient = false;
}


//-----------------------------------------------------------------------------
// Purpose: Allows the advisor_roaming to fire at an unseen enemy. The advisor_roaming is relying
//			on hitting the target with bullets that will punch through the 
//			cover that the enemy is hiding behind. (Such as the Depot lighthouse)
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputBlindfireOn( inputdata_t &inputdata )
{
	m_fBlindfire = true;
}


//-----------------------------------------------------------------------------
// Purpose: Returns the advisor_roaming to default rules for attacking the enemy. The
//			enemy must be seen to be fired at.
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputBlindfireOff( inputdata_t &inputdata )
{
	m_fBlindfire = false;
}

//-----------------------------------------------------------------------------
// Purpose: Set the advisor_roaming's paddles flailing!
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::Event_Killed( const CTakeDamageInfo &info )
{
	m_takedamage = DAMAGE_NO;

	StopCannonBurst();

	// Replace the rotor sound with broken engine sound.
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	controller.SoundDestroy( m_pRotorSound );

	// BUGBUG: Isn't this sound just going to get stomped when the base class calls StopLoopingSounds() ??
	CPASAttenuationFilter filter2( this );
	m_pRotorSound = controller.SoundCreate( filter2, entindex(), "NPC_CombineGunship.DyingSound" );
	controller.Play( m_pRotorSound, 1.0, 100 );

	m_OnDeath.FireOutput( info.GetAttacker(), this );
	SendOnKilledGameEvent( info );

	BeginCrash();

	// we deliberately do not call BaseClass::EventKilled
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::BeginCrash( void )
{
	m_lifeState = LIFE_DYING;
	StopGroundAttack( false );

	// Increase our smoke trail
	CreateSmokeTrail();
	if ( m_pSmokeTrail )
	{
		m_pSmokeTrail->SetLifetime( -1 );
		m_pSmokeTrail->m_StartSize = 64;
		m_pSmokeTrail->m_EndSize = 128;
		m_pSmokeTrail->m_Opacity = 0.5f;
	}

	if ( !FindNearestAdvisorRoamingCrash() )
	{
		// We couldn't find a crash target, so just die right here.
		BeginDestruct();
		return;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CNPC_CombineAdvisorRoaming::FindNearestAdvisorRoamingCrash( void )
{
	// Find the nearest crash point. If we find one, we'll try to fly to it and die.
	// If we can't find one, we'll die right here.
	bool bFoundAnyCrashTargets = false;
 	float flNearest = MAX_TRACE_LENGTH * MAX_TRACE_LENGTH;
	CTargetAdvisorRoamingCrash *pNearest = NULL;
	CBaseEntity *pEnt = NULL;
	while( (pEnt = gEntList.FindEntityByClassname(pEnt, "info_target_advisor_roaming_crash")) != NULL )
	{
		CTargetAdvisorRoamingCrash *pCrashTarget = assert_cast<CTargetAdvisorRoamingCrash*>(pEnt);
		if ( pCrashTarget->IsDisabled() )
			continue;

		bFoundAnyCrashTargets = true;

		float flDist = ( pEnt->WorldSpaceCenter() - WorldSpaceCenter() ).LengthSqr();
		if( flDist < flNearest )
		{
			trace_t tr;
			UTIL_TraceLine( WorldSpaceCenter(), pEnt->WorldSpaceCenter(), MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
			if( tr.fraction == 1.0 )
			{
				pNearest = pCrashTarget;
				flNearest = flDist;
			}
			else if ( g_debug_advisor_roaming.GetInt() )
			{
				NDebugOverlay::Line( WorldSpaceCenter(), tr.endpos, 255,0,0, true, 99);
			}
		}
	}

	if ( !pNearest )
	{
		// If we found a advisor_roaming crash, but none near enough, claim we did find one, so that we
		// don't blow up yet. This will give us 3 seconds to attempt to find one before dying.
		if ( !m_hCrashTarget && bFoundAnyCrashTargets )
		{
			m_flNextAdvisorRoamingCrashFind = gpGlobals->curtime + 0.5;
			m_flEndDestructTime = gpGlobals->curtime + 3.0;
			return true;
		}

		return false;
	}

	// Fly to the crash point and destruct there
  	m_hCrashTarget = pNearest;
	m_flNextAdvisorRoamingCrashFind = gpGlobals->curtime + 0.5;
	m_flEndDestructTime = 0;

	if ( g_debug_advisor_roaming.GetInt() )
	{
		NDebugOverlay::Line(GetAbsOrigin(), m_hCrashTarget->GetAbsOrigin(), 0,255,0, true, 0.5);
		NDebugOverlay::Box( m_hCrashTarget->GetAbsOrigin(), -Vector(200,200,200), Vector(200,200,200), 0,255,0, 128, 0.5 );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: I'm now ready to die. Create my ragdoll & hide myself.
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::BeginDestruct( void )
{
	m_flEndDestructTime = gpGlobals->curtime + 3.0;

	// Clamp velocity
	if( hl2_episodic.GetBool() && GetAbsVelocity().Length() > 700.0f )
	{
		Vector vecVelocity = GetAbsVelocity(); 
		VectorNormalize( vecVelocity );
		SetAbsVelocity( vecVelocity * 700.0f );
	}

	CTakeDamageInfo info;
	info.SetDamage( 40000 );
	CalculateExplosiveDamageForce( &info, GetAbsVelocity(), GetAbsOrigin() );

	// Don't create a ragdoll if we're going to explode into gibs
	if ( !m_hCrashTarget )
		return;

	// Switch to damaged skin
	m_nSkin = 1;

	// Create the ragdoll
	m_hRagdoll = CreateServerRagdoll( this, 0, info, COLLISION_GROUP_NONE );
	if ( !m_hRagdoll )
	{
		// Failed, just explode
		SelfDestruct();
		return;
	}

	m_hRagdoll->SetName( AllocPooledString( UTIL_VarArgs("%s_ragdoll", GetEntityName()) ) );

	// Tell the smoke trail to follow the ragdoll
	CreateSmokeTrail();
	if ( m_pSmokeTrail )
	{
		// Force the smoke trail to stay on, and tell it to follow the ragdoll
		m_pSmokeTrail->SetLifetime( -1 );
		m_pSmokeTrail->FollowEntity( m_hRagdoll );
		
		m_pSmokeTrail->m_StartSize = 64;
		m_pSmokeTrail->m_EndSize = 128;
		m_pSmokeTrail->m_Opacity = 0.5f;
	}

	/* 
	// ROBIN: Disabled this for now.
	//
	// Create the crashing controller and attach it to the ragdoll physics objects
	m_pCrashingController = physenv->CreateMotionController( &m_crashCallback );
	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = m_hRagdoll->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
	for ( int i = 0; i < count; i++ )
	{
		m_pCrashingController->AttachObject( pList[i], false );
	}
	*/

	// Hide myself, because the ragdoll's now taken my place
	AddEffects( EF_NODRAW );
	AddSolidFlags( FSOLID_NOT_SOLID );
}

//-----------------------------------------------------------------------------
// Purpose: Create a smoke trail
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::CreateSmokeTrail( void )
{
	if ( m_pSmokeTrail )
		return;

	m_pSmokeTrail = SmokeTrail::CreateSmokeTrail();
	
	if ( m_pSmokeTrail )
	{
		m_pSmokeTrail->m_SpawnRate			= 48;
		m_pSmokeTrail->m_ParticleLifetime	= 2.5f;
		
		m_pSmokeTrail->m_StartColor.Init( 0.25f, 0.25f, 0.25f );
		m_pSmokeTrail->m_EndColor.Init( 0.0, 0.0, 0.0 );
		
		m_pSmokeTrail->m_StartSize		= 24;
		m_pSmokeTrail->m_EndSize		= 128;
		m_pSmokeTrail->m_SpawnRadius	= 4;
		m_pSmokeTrail->m_MinSpeed		= 8;
		m_pSmokeTrail->m_MaxSpeed		= 64;
		m_pSmokeTrail->m_Opacity		= 0.2f;

		m_pSmokeTrail->SetLifetime( -1 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::ApplyGeneralDrag( void )
{
	Vector vecNewVelocity = GetAbsVelocity();
	
	// See if we need to stop more quickly
	if ( m_bIsGroundAttacking )
	{
		vecNewVelocity *= 0.95f;
	}
	else
	{
		vecNewVelocity *= 0.995;
	}

	SetAbsVelocity( vecNewVelocity );
}

//-----------------------------------------------------------------------------
// Purpose:	
//-----------------------------------------------------------------------------

void CNPC_CombineAdvisorRoaming::Flight( void )
{
	if( GetFlags() & FL_ONGROUND )
	{
		//This would be really bad.
		SetGroundEntity( NULL );
	}

	if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_PATH )
	{
		NDebugOverlay::Line(GetLocalOrigin(), GetDesiredPosition(), 0,0,255, true, 0.1);
	}

	// calc desired acceleration
	float dt = 1.0f;

	Vector	accel;
	float	accelRate = ADVISOR_ROAMING_ACCEL_RATE;
	float	maxSpeed = GetMaxSpeed(); 

	if ( m_lifeState == LIFE_DYING && m_hCrashTarget != NULL )
	{
		// AdvisorRoaming can fly faster to the place where it's supposed to crash, but
		// maintain normal speeds if we haven't found a place to crash.
		accelRate *= 2.0;
		maxSpeed *= 4.0;
	}

	float flCurrentSpeed = GetAbsVelocity().Length();
	float flDist = MIN( flCurrentSpeed + accelRate, maxSpeed );

	Vector deltaPos;
	if ( m_lifeState == LIFE_DYING || m_hGroundAttackTarget )
	{
		// Move directly to the target point
		deltaPos = GetDesiredPosition();
	}
	else
	{
		ComputeActualTargetPosition( flDist, dt, 0.0f, &deltaPos );
	}
	deltaPos -= GetAbsOrigin();

	// calc goal linear accel to hit deltaPos in dt time.
	accel.x = 2.0 * (deltaPos.x - GetAbsVelocity().x * dt) / (dt * dt);
	accel.y = 2.0 * (deltaPos.y - GetAbsVelocity().y * dt) / (dt * dt);
	accel.z = 2.0 * (deltaPos.z - GetAbsVelocity().z * dt + 0.5 * 384 * dt * dt) / (dt * dt);
	
	float flDistFromPath = 0.0f;
	Vector vecPoint, vecDelta;
	if ( m_lifeState != LIFE_DYING && IsOnPathTrack() )
	{
		// Also, add in a little force to get us closer to our current line segment if we can
		ClosestPointToCurrentPath( &vecPoint );
		VectorSubtract( vecPoint, GetAbsOrigin(), vecDelta );
 		flDistFromPath = VectorNormalize( vecDelta );
		if ( flDistFromPath > ADVISOR_ROAMING_OUTER_NAV_DIST )
		{
			// Strongly constrain to an n unit pipe around the current path
			// by damping out all impulse forces that would push us further from the pipe
			float flAmount = (flDistFromPath - ADVISOR_ROAMING_OUTER_NAV_DIST) / 200.0f;
			flAmount = clamp( flAmount, 0, 1 );
			VectorMA( accel, flAmount * 200.0f, vecDelta, accel );
		}
	}

	Vector vecAvoidForce;
	CAvoidSphere::ComputeAvoidanceForces( this, 350.0f, 2.0f, &vecAvoidForce );
	accel += vecAvoidForce;
	CAvoidBox::ComputeAvoidanceForces( this, 350.0f, 2.0f, &vecAvoidForce );
	accel += vecAvoidForce;
	
	if ( m_lifeState != LIFE_DYING || m_hCrashTarget == NULL )
	{
		// don't fall faster than 0.2G or climb faster than 2G
		accel.z = clamp( accel.z, 384 * 0.2, 384 * 2.0 );
	}

	Vector forward, right, up;
	GetVectors( &forward, &right, &up );

	Vector goalUp = accel;
	VectorNormalize( goalUp );

	// calc goal orientation to hit linear accel forces
	float goalPitch = RAD2DEG( asin( DotProduct( forward, goalUp ) ) );
	float goalYaw = UTIL_VecToYaw( m_vecDesiredFaceDir );
	float goalRoll = RAD2DEG( asin( DotProduct( right, goalUp ) ) );

	// clamp goal orientations
	goalPitch = clamp( goalPitch, -45, 60 );
	goalRoll = clamp( goalRoll, -45, 45 );

	// calc angular accel needed to hit goal pitch in dt time.
	dt = 0.6;
	QAngle goalAngAccel;
	goalAngAccel.x = 2.0 * (AngleDiff( goalPitch, AngleNormalize( GetLocalAngles().x ) ) - GetLocalAngularVelocity().x * dt) / (dt * dt);
	goalAngAccel.y = 2.0 * (AngleDiff( goalYaw, AngleNormalize( GetLocalAngles().y ) ) - GetLocalAngularVelocity().y * dt) / (dt * dt);
	goalAngAccel.z = 2.0 * (AngleDiff( goalRoll, AngleNormalize( GetLocalAngles().z ) ) - GetLocalAngularVelocity().z * dt) / (dt * dt);

	goalAngAccel.x = clamp( goalAngAccel.x, -300, 300 );
	//goalAngAccel.y = clamp( goalAngAccel.y, -60, 60 );
	goalAngAccel.y = clamp( goalAngAccel.y, -120, 120 );
	goalAngAccel.z = clamp( goalAngAccel.z, -300, 300 );

	// limit angular accel changes to similate mechanical response times
	dt = 0.1;
	QAngle angAccelAccel;
	angAccelAccel.x = (goalAngAccel.x - m_vecAngAcceleration.x) / dt;
	angAccelAccel.y = (goalAngAccel.y - m_vecAngAcceleration.y) / dt;
	angAccelAccel.z = (goalAngAccel.z - m_vecAngAcceleration.z) / dt;

	angAccelAccel.x = clamp( angAccelAccel.x, -1000, 1000 );
	angAccelAccel.y = clamp( angAccelAccel.y, -1000, 1000 );
	angAccelAccel.z = clamp( angAccelAccel.z, -1000, 1000 );

	m_vecAngAcceleration += angAccelAccel * 0.1;

	// DevMsg( "pitch %6.1f (%6.1f:%6.1f)  ", goalPitch, GetLocalAngles().x, m_vecAngVelocity.x );
	// DevMsg( "roll %6.1f (%6.1f:%6.1f) : ", goalRoll, GetLocalAngles().z, m_vecAngVelocity.z );
	// DevMsg( "%6.1f %6.1f %6.1f  :  ", goalAngAccel.x, goalAngAccel.y, goalAngAccel.z );
	// DevMsg( "%6.0f %6.0f %6.0f\n", angAccelAccel.x, angAccelAccel.y, angAccelAccel.z );

	ApplySidewaysDrag( right );
	ApplyGeneralDrag();
	
	QAngle angVel = GetLocalAngularVelocity();
	angVel += m_vecAngAcceleration * 0.1;

	//angVel.y = clamp( angVel.y, -60, 60 );
	//angVel.y = clamp( angVel.y, -120, 120 );
	angVel.y = clamp( angVel.y, -120, 120 );

	SetLocalAngularVelocity( angVel );

	m_flForce = m_flForce * 0.8 + (accel.z + fabs( accel.x ) * 0.1 + fabs( accel.y ) * 0.1) * 0.1 * 0.2;

	Vector vecImpulse = m_flForce * up;
	
	if ( !m_hCrashTarget && m_lifeState == LIFE_DYING && !hl2_episodic.GetBool() )
	{
		// Force advisor_roaming to the ground if it doesn't have a specific place to crash.
		// EXCEPT In episodic, where forcing it to the ground means it crashes where the player can't see (attic showdown) (sjb)
		vecImpulse.z = -10;
	}
	else
	{
		vecImpulse.z -= 38.4;  // 32ft/sec	
	}
	
	// Find our current velocity
	Vector vecVelDir = GetAbsVelocity();
	VectorNormalize( vecVelDir );

	if ( flDistFromPath > ADVISOR_ROAMING_INNER_NAV_DIST )
	{
		// Strongly constrain to an n unit pipe around the current path
		// by damping out all impulse forces that would push us further from the pipe
		float flDot = DotProduct( vecImpulse, vecDelta );
		if ( flDot < 0.0f )
		{
			VectorMA( vecImpulse, -flDot * 0.1f, vecDelta, vecImpulse );
		}

		// Also apply an extra impulse to compensate for the current velocity
		flDot = DotProduct( vecVelDir, vecDelta );
		if ( flDot < 0.0f )
		{
			VectorMA( vecImpulse, -flDot * 0.1f, vecDelta, vecImpulse );
		}
	}
	
	// Find our acceleration direction
	Vector	vecAccelDir = vecImpulse;
	VectorNormalize( vecAccelDir );

	// Level out our plane of movement
	vecAccelDir.z	= 0.0f;
	vecVelDir.z		= 0.0f;
	forward.z		= 0.0f;
	right.z			= 0.0f;

	// Find out how "fast" we're moving in relation to facing and acceleration
	float speed = m_flForce * DotProduct( vecVelDir, vecAccelDir );// * DotProduct( forward, vecVelDir );

	// Apply the acceleration blend to the fins
	float finAccelBlend = SimpleSplineRemapVal( speed, -60, 60, -1, 1 );
	float curFinAccel = GetPoseParameter( m_poseArm_Accel );
	
	curFinAccel = UTIL_Approach( finAccelBlend, curFinAccel, 0.5f );
	SetPoseParameter( m_poseArm_Accel, curFinAccel );
	// DbgDisplay( 13, "arm_accel", curFinAccel );

	speed = m_flForce * DotProduct( vecVelDir, right );

	// Apply the spin sway to the fins
	float finSwayBlend = SimpleSplineRemapVal( speed, -60, 60, -1, 1 );
	float curFinSway = GetPoseParameter( m_poseArm_Sway );

	curFinSway = UTIL_Approach( finSwayBlend, curFinSway, 0.5f );
	SetPoseParameter( m_poseArm_Sway, curFinSway );
	// DbgDisplay( 14, "arm_sway", curFinSway );

	if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_PATH )
	{
		NDebugOverlay::Line(GetLocalOrigin(), GetLocalOrigin() + vecImpulse, 255,0,0, true, 0.1);
	}

	// Add in our velocity pulse for this frame
	ApplyAbsVelocityImpulse( vecImpulse );

	// Check if the advisor has an enemy and needs to stay away
	if ( 0 && GetEnemy() )
	{
		float flDistFromEnemy = ( Throw_GetTarget() - GetAbsOrigin() ).Length2D();
		if ( flDistFromEnemy < sk_advisor_roaming_enemydist_min.GetFloat() )
		{
			static float flTimeSpew = 0;
			if ( gpGlobals->curtime > flTimeSpew )
			{
				flTimeSpew = gpGlobals->curtime + 1.0;
				Msg( "Advisor is too close (%f)\n", flDistFromEnemy );
			}
			UseFarthestPathPoint( true );
			Control_ReturnToPath();
		}
	}
}

//------------------------------------------------------------------------------
// Updates the facing direction
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::UpdateFacingDirection( void )
{
	if ( GetEnemy() )
	{
		if ( !IsCrashing() && m_flLastSeen + 5 > gpGlobals->curtime )
		{
			// If we've seen the target recently, face the target.
			//Msg( "Facing Target \n" );
			m_vecDesiredFaceDir = m_vecTargetPosition - GetAbsOrigin();
		}
		else
		{
			// Remain facing the way you were facing...
		}
	}
	else
	{
		// Face our desired position.
		if ( GetDesiredPosition().DistToSqr( GetAbsOrigin() ) > 1 )
		{
			m_vecDesiredFaceDir = GetDesiredPosition() - GetAbsOrigin();
		}
		else
		{
			GetVectors( &m_vecDesiredFaceDir, NULL, NULL );
		}
	}
	VectorNormalize( m_vecDesiredFaceDir ); 
}

//------------------------------------------------------------------------------
// Purpose : Fire up the AdvisorRoamings 'second' rotor sound. The Search sound.
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InitializeRotorSound( void )
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	
	CPASAttenuationFilter filter( this );

	m_pCannonSound		= controller.SoundCreate( filter, entindex(), "NPC_CombineGunship.CannonSound" );
 	m_pRotorSound		= controller.SoundCreate( filter, entindex(), "NPC_CombineGunship.RotorSound" );
 	m_pAirExhaustSound	= controller.SoundCreate( filter, entindex(), "NPC_CombineGunship.ExhaustSound" );
 	m_pAirBlastSound	= controller.SoundCreate( filter, entindex(), "NPC_CombineGunship.RotorBlastSound" );
// 	m_pRotorSound		= controller.SoundCreate( filter, entindex(), "BaseCombatCharacter.StopWeaponSounds" );
// 	m_pAirExhaustSound	= controller.SoundCreate( filter, entindex(), "BaseCombatCharacter.StopWeaponSounds" );
// 	m_pAirBlastSound	= controller.SoundCreate( filter, entindex(), "BaseCombatCharacter.StopWeaponSounds" );
	
	controller.Play( m_pCannonSound, 0.0, 100 );
	controller.Play( m_pAirExhaustSound, 0.0, 100 );
	controller.Play( m_pAirBlastSound, 0.0, 100 );

	BaseClass::InitializeRotorSound();
}


//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::UpdateRotorSoundPitch( int iPitch )
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	// Apply the pitch to both sounds. 
	controller.SoundChangePitch( m_pAirExhaustSound, iPitch, 0.1 );

	// FIXME: Doesn't work in multiplayer
	CBaseEntity *pPlayer = UTIL_PlayerByIndex(1);
	if (pPlayer)
	{
		Vector pos;
		Vector up;
		GetAttachment( "rotor", pos, NULL, NULL, &up );

		float flDistance = (pPlayer->WorldSpaceCenter() - pos).Length2DSqr();

		// Fade in exhaust when we're far from the player
		float flVolume = clamp( RemapVal( flDistance, (900*900), (1800*1800), 1, 0 ), 0, 1 );
		controller.SoundChangeVolume( m_pAirExhaustSound, flVolume * GetRotorVolume(), 0.1 );

		// Fade in the blast when it's close to the player (in 2D)
		flVolume = clamp( RemapVal( flDistance, (600*600), (700*700), 1, 0 ), 0, 1 );
		controller.SoundChangeVolume( m_pAirBlastSound, flVolume * GetRotorVolume(), 0.1 );
	}

	BaseClass::UpdateRotorSoundPitch( iPitch );
}

float CNPC_CombineAdvisorRoaming::GetRotorVolume( void )
{
	int numObjects = m_levitate_lstObjects.Count();
	if ( numObjects <= 0 )
		return 0.0f;

	float flBase = BaseClass::GetRotorVolume();
	flBase *= sk_advisor_roaming_levitate_buzz_vol.GetFloat();

	if ( numObjects <= 2 )
		flBase /= 2.0f;

	return flBase;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  :
// Output : 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::ApplySidewaysDrag( const Vector &vecRight )
{
	Vector vecVelocity = GetAbsVelocity();
	if( m_lifeState == LIFE_ALIVE )
	{
		vecVelocity.x *= (1.0 - fabs( vecRight.x ) * 0.04);
		vecVelocity.y *= (1.0 - fabs( vecRight.y ) * 0.04);
		vecVelocity.z *= (1.0 - fabs( vecRight.z ) * 0.04);
	}
	else
	{
		vecVelocity.x *= (1.0 - fabs( vecRight.x ) * 0.03);
		vecVelocity.y *= (1.0 - fabs( vecRight.y ) * 0.03);
		vecVelocity.z *= (1.0 - fabs( vecRight.z ) * 0.09);
	}
	SetAbsVelocity( vecVelocity );
}

//------------------------------------------------------------------------------
// Purpose: Explode the advisor_roaming.
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::SelfDestruct( void )
{
	SetThink( NULL );
	m_lifeState = LIFE_DEAD;
	
	StopLoopingSounds();
	StopCannonBurst();

	Vector vecVelocity = GetAbsVelocity();
	vecVelocity.z = 0.0; // stop falling.
	SetAbsVelocity( vecVelocity );

	CBaseEntity *pBreakEnt = this;

	// If we've ragdolled, play the explosions on the ragdoll instead
	Vector vecOrigin;
	if ( m_hRagdoll )
	{
		m_hRagdoll->EmitSound( "NPC_CombineGunship.Explode" );
		vecOrigin = m_hRagdoll->GetAbsOrigin();
		pBreakEnt = m_hRagdoll;
	}
	else
	{
		EmitSound( "NPC_CombineGunship.Explode" );
		vecOrigin = GetAbsOrigin();
	}

	// Create some explosions on the advisor_roaming body
	Vector vecDelta;
	for( int i = 0 ; i < 6 ; i++ )
	{
		vecDelta = RandomVector( -200,200 );
		ExplosionCreate( vecOrigin + vecDelta, QAngle( -90, 0, 0 ), this, 10, 10, false );
	}

	AR2Explosion *pExplosion = AR2Explosion::CreateAR2Explosion( vecOrigin );
	if ( pExplosion )
	{
		pExplosion->SetLifetime( 10 );
	}

	// If we don't have a crash target, explode into chunks
	if ( !m_hCrashTarget )
	{
		Vector angVelocity;
		QAngleToAngularImpulse( pBreakEnt->GetLocalAngularVelocity(), angVelocity );
		PropBreakableCreateAll( pBreakEnt->GetModelIndex(), pBreakEnt->VPhysicsGetObject(), pBreakEnt->GetAbsOrigin(), pBreakEnt->GetAbsAngles(), pBreakEnt->GetAbsVelocity(), angVelocity, 1.0, 800, COLLISION_GROUP_NPC, pBreakEnt );

		// Throw out some small chunks too
		CPVSFilter filter( GetAbsOrigin() );
	 	for ( int i = 0; i < 20; i++ )
		{
			Vector gibVelocity = RandomVector(-100,100) * 10;
			int iModelIndex = modelinfo->GetModelIndex( g_PropDataSystem.GetRandomChunkModel( "MetalChunks" ) );	
			te->BreakModel( filter, 0.0, GetAbsOrigin(), vec3_angle, Vector(40,40,40), gibVelocity, iModelIndex, 400, 1, 2.5, BREAK_METAL );
		}

		if ( m_hRagdoll )
		{
			UTIL_Remove( m_hRagdoll );
		}
	}
	else
	{
		if ( m_pSmokeTrail )
		{
			// If we have a ragdoll, let it smoke for a few more seconds
			if ( m_hRagdoll )
			{
				m_pSmokeTrail->SetLifetime(3.0f);
			}
			else
			{
				m_pSmokeTrail->SetLifetime(0.1f);
			}
			m_pSmokeTrail = NULL;
		}
	}

	UTIL_Remove( this );

	// Record this so a nearby citizen can respond.
	if ( GetCitizenResponse() )
	{
		GetCitizenResponse()->AddResponseTrigger( CR_PLAYER_KILLED_GUNSHIP );
	}

#ifdef HL2_EPISODIC
	NPCEventResponse()->TriggerEvent( "TLK_CITIZEN_RESPONSE_KILLED_GUNSHIP", false, false );
#endif
}


//------------------------------------------------------------------------------
// Purpose : Explode the advisor_roaming.
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputSelfDestruct( inputdata_t &inputdata )
{
	BeginCrash();
}

//------------------------------------------------------------------------------
// Purpose : Shrink the advisor_roaming's bbox so that it fits in docking bays
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputSetDockingBBox( inputdata_t &inputdata )
{
	Vector vecSize( 32, 32, 32 );

	UTIL_SetSize( this, vecSize * -1, vecSize );
}

//------------------------------------------------------------------------------
// Purpose : Set the advisor_roaming BBox to normal size
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputSetNormalBBox( inputdata_t &inputdata )
{
	Vector vecBBMin, vecBBMax;

	ExtractBbox( SelectHeaviestSequence( ACT_ADVISOR_ROAMING_PATROL ), vecBBMin, vecBBMax ); 

	// Trim the bounding box a bit. It's huge.
#define ADVISOR_ROAMING_TRIM_BOX 38
	vecBBMin.x += ADVISOR_ROAMING_TRIM_BOX;
	vecBBMax.x -= ADVISOR_ROAMING_TRIM_BOX;
	vecBBMin.y += ADVISOR_ROAMING_TRIM_BOX;
	vecBBMax.y -= ADVISOR_ROAMING_TRIM_BOX;

	UTIL_SetSize( this, vecBBMin, vecBBMax );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputEnableGroundAttack( inputdata_t &inputdata )
{
	m_bCanGroundAttack = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputDisableGroundAttack( inputdata_t &inputdata )
{
	m_bCanGroundAttack = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &inputdata - 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputDoGroundAttack( inputdata_t &inputdata )
{
	// Was a target node specified?
	CBaseEntity *pEntity = gEntList.FindEntityByName( NULL, inputdata.value.StringID(), NULL, inputdata.pActivator, inputdata.pCaller );
	if ( pEntity )
	{
		// Mapmaker wants us to ground attack a specific target
		m_hGroundAttackTarget = pEntity;
	}
	else
	{
		StartGroundAttack();
	}
}

void CNPC_CombineAdvisorRoaming::InputDoAvalancheBlast( inputdata_t &inputdata )
{
	// Was a target node specified?
	// CBaseEntity *pEntity = gEntList.FindEntityByName( NULL, inputdata.value.StringID(), NULL, inputdata.pActivator, inputdata.pCaller );
	Avalanche_Prepare();
}

void CNPC_CombineAdvisorRoaming::InputDoSpitAttack( inputdata_t &inputdata )
{
	// Was a target node specified?
	// CBaseEntity *pEntity = gEntList.FindEntityByName( NULL, inputdata.value.StringID(), NULL, inputdata.pActivator, inputdata.pCaller );
	Spit_Prepare();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &vGunPosition - 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::UpdateEnemyTarget( void )
{
	Vector	vGunPosition;

	GetAttachment( "muzzle", vGunPosition );

	// Follow mode
	Vector	enemyPos;
	bool	bTargettingPlayer;
	
	if ( GetEnemy() != NULL )
	{
		CBaseCombatCharacter *pCCEnemy = GetEnemy()->MyCombatCharacterPointer();
		if ( pCCEnemy != NULL && pCCEnemy->IsInAVehicle() )
		{
			// Update against a driving target
			enemyPos = GetEnemy()->WorldSpaceCenter();
		}
		else 
		{
			enemyPos = GetEnemy()->EyePosition();
		}
		bTargettingPlayer = GetEnemy()->IsPlayer();
	}
	else
	{
		enemyPos = m_vecAttackPosition;
		bTargettingPlayer = false;
	}

	// Direction towards the enemy
	Vector	targetDir = enemyPos - m_vecAttackPosition;
	VectorNormalize( targetDir );

	// Direction from the advisor_roaming to the enemy
	Vector	enemyDir = enemyPos - vGunPosition;
	VectorNormalize( enemyDir );

	float	lastSpeed = VectorNormalize( m_vecAttackVelocity );
	QAngle	chaseAngles, lastChaseAngles;

	VectorAngles( targetDir, chaseAngles );
	VectorAngles( m_vecAttackVelocity, lastChaseAngles );

	// Debug info
	if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_STITCHING )
	{
		// Final position
		NDebugOverlay::Cross3D( m_vecAttackPosition, -Vector(2,2,2), Vector(2,2,2), 0, 0, 255, true, 4.0f );
	}

	float yawDiff = UTIL_AngleDiff( lastChaseAngles[YAW], chaseAngles[YAW] );

	int	maxYaw;
	if ( bTargettingPlayer )
	{
		maxYaw = 6;
	}
	else
	{
		maxYaw = 30;
	}	

	yawDiff = clamp( yawDiff, -maxYaw, maxYaw );

	chaseAngles[PITCH]	= 0.0f;
	chaseAngles[ROLL]	= 0.0f;

	bool bMaxHits = ( m_iBurstHits >= ADVISOR_ROAMING_MAX_HITS_PER_BURST || (GetEnemy() && !GetEnemy()->IsAlive()) );

	if ( bMaxHits )
	{
		// We've hit our target. Stop chasing, and return to max speed.
		chaseAngles[YAW] = lastChaseAngles[YAW];
		lastSpeed = BASE_STITCH_VELOCITY;
	}
	else
	{
		// Move towards the target yaw
		chaseAngles[YAW] = UTIL_AngleMod( lastChaseAngles[YAW] - yawDiff );
	}

	// If we've hit the target already, or we're not close enough to it, then just stitch along
	if ( bMaxHits || ( m_vecAttackPosition - enemyPos ).LengthSqr() > (64 * 64) )
	{
		AngleVectors( chaseAngles, &targetDir );

		// Update our new velocity
		m_vecAttackVelocity = targetDir * lastSpeed;

		if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_STITCHING )
		{
			NDebugOverlay::Line( m_vecAttackPosition, m_vecAttackPosition + (m_vecAttackVelocity * 0.1), 255, 0, 0, true, 4.0f );
		}

		// Move along that velocity for this step in time
		m_vecAttackPosition += ( m_vecAttackVelocity * 0.1f );
		m_vecAttackPosition.z = enemyPos.z;
	}
	else
	{
		// Otherwise always continue to hit an NPC when close enough
		m_vecAttackPosition = enemyPos;
	}
}

//------------------------------------------------------------------------------
// Purpose: Utility function to aim the helicopter gun at the direction
//------------------------------------------------------------------------------
bool CNPC_CombineAdvisorRoaming::PoseGunTowardTargetDirection( const Vector &vTargetDir )
{
	Vector vecOut;
	VectorIRotate( vTargetDir, EntityToWorldTransform(), vecOut );

	QAngle angles;
	VectorAngles(vecOut, angles);
	angles.y = AngleNormalize( angles.y );
	angles.x = AngleNormalize( angles.x );

	if (angles.x > m_angGun.x)
	{
		m_angGun.x = MIN( angles.x, m_angGun.x + 12 );
	}
	if (angles.x < m_angGun.x)
	{
		m_angGun.x = MAX( angles.x, m_angGun.x - 12 );
	}
	if (angles.y > m_angGun.y)
	{
		m_angGun.y = MIN( angles.y, m_angGun.y + 12 );
	}
	if (angles.y < m_angGun.y)
	{
		m_angGun.y = MAX( angles.y, m_angGun.y - 12 );
	}

	SetPoseParameter( m_poseCamera_Pitch, -m_angGun.x );
	SetPoseParameter( m_poseCamera_Yaw, m_angGun.y );

	// DbgDisplay( 15, "camera_pitch", -m_angGun.x );
	// DbgDisplay( 16, "camera_yaw", m_angGun.y );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Vector
//-----------------------------------------------------------------------------
Vector CNPC_CombineAdvisorRoaming::GetMissileTarget( void )
{
	return GetEnemy()->GetAbsOrigin();
}

//------------------------------------------------------------------------------
// Purpose : Get the target position for the enemy- the position we fire upon.
//			 this is often modified by m_flAttackOffset to provide the 'stitching'
//			 behavior that's so popular with the kids these days (sjb)
//
// Input   : vGunPosition - location of advisor_roaming's muzzle
//		   : pTarget = vector to paste enemy target into.
// Output  :
//------------------------------------------------------------------------------
Vector CNPC_CombineAdvisorRoaming::GetEnemyTarget( void )
{
	// Make sure we have an enemy
	if ( GetEnemy() == NULL )
		return m_vecAttackPosition;

	// If we're locked onto a missile, use special code to try and destroy it
	if ( IsTargettingMissile() )
		return GetMissileTarget();

	return m_vecAttackPosition;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &tr - 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::DoImpactEffect( trace_t &tr, int nDamageType )
{
	UTIL_ImpactTrace( &tr, nDamageType, "ImpactGunship" );

	// These glow effects don't sort properly, so they're cut for E3 2003 (sjb)
#if 0 
	CEffectData data;

	data.m_vOrigin = tr.endpos;
	data.m_vNormal = vec3_origin;
	data.m_vAngles = vec3_angle;

	DispatchEffect( "GunshipImpact", data );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Make the advisor_roaming's signature blue tracer!
// Input  : &vecTracerSrc - 
//			&tr - 
//			iTracerType - 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType )
{
	switch ( iTracerType )
	{
	case TRACER_LINE:
		{
			float flTracerDist;
			Vector vecDir;
			Vector vecEndPos;

			vecDir = tr.endpos - vecTracerSrc;

			flTracerDist = VectorNormalize( vecDir );

			UTIL_Tracer( vecTracerSrc, tr.endpos, 0, TRACER_DONT_USE_ATTACHMENT, 8000, true, "GunshipTracer" );
		}
		break;

	default:
		BaseClass::MakeTracer( vecTracerSrc, tr, iTracerType );
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &info - 
//			&vecDir - 
//			*ptr - 
// Output : int
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr )
{
	QAngle vecAngles;
	VectorAngles( ptr->plane.normal, vecAngles );

	DispatchParticleEffect( "blood_impact_synth_01", ptr->endpos, vecAngles );
	DispatchParticleEffect( "blood_impact_synth_01_arc_parent", PATTACH_POINT_FOLLOW, this, m_attachMouth );

	float flFactor = RemapValClamped( GetHealth(), GetMaxHealth(), 0.0f, 1.0f, sk_advisor_roaming_health_increments.GetFloat() );
	int numBloodSpills = flFactor;
	for ( int k = 1; k < numBloodSpills; ++ k )
	{
		DispatchParticleEffect( "blood_impact_synth_01",
			ptr->endpos + RandomVector( -1, 1 ),
			vecAngles + RandomAngle( -30, 30 ) );
	}

	BaseClass::BaseClass::TraceAttack( info, vecDir, ptr );
}

//-----------------------------------------------------------------------------
// Purpose: This is necessary to ensure that the game doesn't break if a mapmaker has outputs that
//			must be fired on advisor_roamings, and the player switches skill levels 
//			midway through a advisor_roaming battle.
// Input  : iDamageNumber - 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::FireDamageOutputsUpto( int iDamageNumber )
{
	for ( int i = 0; i <= iDamageNumber; i++ )
	{
		if ( !m_bDamageOutputsFired[i] )
		{
			m_bDamageOutputsFired[i] = true;

			switch ( i )
			{
			case 0:
				//Msg("Fired first\n");
				m_OnFirstDamage.FireOutput( this, this );
				break;
			
			case 1:
				//Msg("Fired second\n");
				m_OnSecondDamage.FireOutput( this, this );
				break;
			
			case 2:
				//Msg("Fired third\n");
				m_OnThirdDamage.FireOutput( this, this );
				break;
			
			case 3:
				//Msg("Fired fourth\n");
				m_OnFourthDamage.FireOutput( this, this );
				break;
			}
		}
	}
}

//------------------------------------------------------------------------------
// Damage control
//------------------------------------------------------------------------------

void CNPC_CombineAdvisorRoaming::DamageControl_RecordDmg( float flDmg )
{
	// Check if within recent interval the dmg threshold was exceeded
	float flTimeStart = gpGlobals->curtime - sk_advisor_roaming_dmgc_interval.GetFloat();
	float flRecentDmg = flDmg;
	int idxRetire = -1;
	for ( int k = 0; k < m_dmgc_times.Count(); ++ k )
	{
		if ( m_dmgc_times[k] >= flTimeStart )
		{
			flRecentDmg += m_dmgc_dmgs[k];
		}
		else
		{
			idxRetire = k;
		}
	}

	if ( flRecentDmg >= sk_advisor_roaming_dmgc_threshold.GetFloat() )
	{
		m_dmgc_times.RemoveAll();
		m_dmgc_dmgs.RemoveAll();

		DamageControl_OnDmgReached( flRecentDmg );
	}
	else
	{
		if ( idxRetire >= 0 )
		{
			m_dmgc_times.RemoveMultipleFromHead( idxRetire + 1 );
			m_dmgc_dmgs.RemoveMultipleFromHead( idxRetire + 1 );
		}

		m_dmgc_times.AddToTail( gpGlobals->curtime );
		m_dmgc_dmgs.AddToTail( flDmg );
	}
}

void CNPC_CombineAdvisorRoaming::DamageControl_OnDmgReached( float flCumulativeDmg )
{
	// Fire our control output
	m_dmgc_onDmgReached.FireOutput( this, this );

	// Restore the shield
	Levitate_Update();

	// Send the advisor onto the retreat track
	CPathTrack *pCurPath = GetDestPathTarget();
	CPathTrack *pRetreat = dynamic_cast<CPathTrack *>( m_dmgc_hPathRetreat.Get() );
	if ( pRetreat && ( !pCurPath || !IsOnSameTrack( pRetreat, pCurPath ) ) )
	{
		MoveToTrackPoint( pRetreat );
		MoveTopSpeed( sk_advisor_roaming_dmgc_retreat_speed.GetFloat() );
	}

	m_dmgc_flTimeInvuln = gpGlobals->curtime + sk_advisor_roaming_dmgc_invuln.GetFloat();

	// Schedule to come back
	DamageControl_ScheduleComeBackToBattle();
}

void CNPC_CombineAdvisorRoaming::DamageControl_SetRetreatTrack( inputdata_t &inputdata )
{
	string_t strTrackName = MAKE_STRING( inputdata.value.String() );

	CBaseEntity *pGoalEnt = gEntList.FindEntityByName( NULL, strTrackName );
	if ( pGoalEnt == NULL )
	{
		DevWarning( "%s: Could not find path_track '%s'!\n", GetClassname(), STRING( strTrackName ) );
		return;
	}

	// Ignore this input if it's not a path_track.
	CPathTrack *pTrack = dynamic_cast<CPathTrack *>(pGoalEnt);
	if ( !pTrack )
	{
		DevWarning( "%s: Specified entity '%s' must be a path_track!\n", GetClassname(), STRING( strTrackName ) );
		return;
	}

	// Otherwise remember that path track as retreat track
	m_dmgc_hPathRetreat = pGoalEnt;
}

void CNPC_CombineAdvisorRoaming::OnNewCurrentTarget( CPathTrack *pTrackNew, CPathTrack *pTrackOld )
{
	if ( !pTrackNew )
		return;

	CPathTrack *pRetreat = dynamic_cast<CPathTrack *>( m_dmgc_hPathRetreat.Get() );
	if ( !pRetreat )
		return;

	if ( IsOnSameTrack( pTrackNew, pRetreat ) )
		return;

	// Otherwise remember that track as our battle track
	m_dmgc_hPathBattle = pTrackNew;
}

void CNPC_CombineAdvisorRoaming::DamageControl_ScheduleComeBackToBattle()
{
	SetContextThink( &CNPC_CombineAdvisorRoaming::DamageControl_ComeBackToBattle,
		gpGlobals->curtime + sk_advisor_roaming_dmgc_comeback.GetFloat(),
		s_szDmgcComeBack );
}

void CNPC_CombineAdvisorRoaming::DamageControl_ComeBackToBattle()
{
	// This is thinking every so often
	CPathTrack *pCurPath = GetDestPathTarget();
	CPathTrack *pBattle = dynamic_cast<CPathTrack *>( m_dmgc_hPathBattle.Get() );

	if ( pBattle && ( !pCurPath || !IsOnSameTrack( pBattle, pCurPath ) ) )
	{
		MoveToTrackPoint( pBattle );
	}
}


//------------------------------------------------------------------------------
// Damage filtering
//------------------------------------------------------------------------------
int	CNPC_CombineAdvisorRoaming::OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo )
{
	CTakeDamageInfo info = inputInfo;
	int iHealthIncrements = sk_advisor_roaming_health_increments.GetInt();

	if ( g_debug_advisor_roaming.GetInt() > 0 )
	{
		Msg( "CNPC_CombineAdvisorRoaming::OnTakeDamage_Alive( %f )\n", info.GetDamage() );
	}

	if ( info.GetDamage() > sk_advisor_roaming_levitate_drop_dmg_exp.GetFloat() )
	{
		bool bDropShield = ( ( info.GetDamageType() & DMG_BLAST ) != 0 );
		if ( !bDropShield )
		{
			// bDropShield = ( RandomInt( 0, 10 ) == 0 );	// 10% of cases the advisor will drop shield
		}
		if ( bDropShield )
		{
			bool bHadObjects = m_levitate_lstObjects.Count() > 0;

			// Drop shield when taking blast damage
			Levitate_Stop();

			if ( bHadObjects )
				// Initial explosion just makes us drop the shield
				return 0;
		}
	}

	// Allow npc_kill to kill me
	if ( info.GetDamageType() != DMG_GENERIC )
	{
		if ( info.GetDamage() < 1.0f )
			return 0;

		// Set min damage
		if ( info.GetDamage() < sk_advisor_roaming_dmg_min.GetInt() )
			info.SetDamage( 1.0f );
		else
			info.SetDamage( info.GetDamage() * sk_advisor_roaming_dmg_factor.GetFloat() );

		if ( info.GetDamage() > ( GetMaxHealth() / (float)iHealthIncrements ) )
			info.SetDamage( GetMaxHealth() / (float)iHealthIncrements );
	}

	// Make a pain sound
	EmitSound( "NPC_CombineGunship.Pain" );

	Vector	damageDir = info.GetDamageForce();
	VectorNormalize( damageDir );

	// Blast damage knocks me off and gives damage in increments
	if ( info.GetDamageType() & DMG_BLAST )
	{
		ApplyAbsVelocityImpulse( damageDir * 200.0f );

		info.SetDamage( ( GetMaxHealth() / iHealthIncrements ) + 1 );
	}
	
	if ( m_bInvulnerable == false )
	{
		// Find out which "stage" we're at in our health
		int healthIncrement = iHealthIncrements - ( GetHealth() / (float)(( GetMaxHealth() / (float)iHealthIncrements )) );
		FireDamageOutputsUpto( healthIncrement );

		// Start smoking when we're almost dead
		if ( GetHealth() < GetMaxHealth() / 2.0f )
		{
			CreateSmokeTrail();

			if ( m_pSmokeTrail )
			{
				if ( GetHealth() > ( GetMaxHealth() / (float)iHealthIncrements ) )
				{
					float flSmokeTime = sk_advisor_roaming_dmg_smoke.GetFloat();
					float flFactor = RemapValClamped( GetHealth(), GetMaxHealth(), 0.0f, 1.0f, 30.0f );
					flSmokeTime *= flFactor;
					m_pSmokeTrail->SetLifetime( flSmokeTime );
				}

				m_pSmokeTrail->FollowEntity( this, "attach_mouth" );
			}
		}

// 		// Move with the target
// 		Vector	gibVelocity = GetAbsVelocity() + (-damageDir * 200.0f);
// 
// 		// Dump out metal gibs
// 		CPVSFilter filter( GetAbsOrigin() );
// 	 	for ( int i = 0; i < 10; i++ )
// 		{
// 			int iModelIndex = modelinfo->GetModelIndex( g_PropDataSystem.GetRandomChunkModel( "MetalChunks" ) );	
// 			te->BreakModel( filter, 0.0, GetAbsOrigin(), vec3_angle, Vector(40,40,40), gibVelocity, iModelIndex, 400, 1, 2.5, BREAK_METAL );
// 		}
	}

	SpawnBlood( info.GetDamagePosition(), info.GetDamageForce(), BloodColor(), info.GetDamage() );

	if ( m_pull_eStage > PULL_PREPARING && m_pull_eStage < PULL_DROP )
	{
		// Make every damage drop from pull
		Pull_Drop();
	}

	if ( gpGlobals->curtime < m_dmgc_flTimeInvuln && gpGlobals->curtime > m_dmgc_flTimeInvuln - sk_advisor_roaming_dmgc_invuln.GetFloat() )
	{
		// Advisor is invulnerable from a previous big chunk of damage
		info.SetDamage( 0.0f );
	}
	else
	{
		DamageControl_RecordDmg( info.GetDamage() );
	}

	return BaseClass::OnTakeDamage_Alive( info );
}


//------------------------------------------------------------------------------
// Purpose : The proper way to begin the advisor_roaming cannon firing at the enemy.
// Input   : iBurstSize - the size of the burst, in rounds.
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::StartCannonBurst( int iBurstSize )
{
	m_iBurstSize = iBurstSize;
	m_iBurstHits = 0;

	m_flTimeNextAttack = gpGlobals->curtime;

	// Start up the cannon sound.
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();
	controller.SoundChangeVolume( m_pCannonSound, 1.0, 0 );

	m_bIsFiring = true;

	// Setup the initial position of the burst
	if ( GetEnemy() )
	{
		// Follow mode
		Vector	enemyPos;
		UTIL_PredictedPosition( GetEnemy(), 2.0f, &enemyPos );

		QAngle offsetAngles;
		Vector offsetDir = ( WorldSpaceCenter() - enemyPos );
		VectorNormalize( offsetDir );
		VectorAngles( offsetDir, offsetAngles );

		int angleOffset = random->RandomInt( 15, 30 );
		if ( random->RandomInt( 0, 1 ) )
		{
			angleOffset *= -1;
		}
		offsetAngles[YAW] += angleOffset;
		offsetAngles[PITCH] = 0;
		offsetAngles[ROLL] = 0;

		AngleVectors( offsetAngles, &offsetDir );

		float stitchOffset;
		float enemyDist = GroundDistToPosition( GetEnemy()->GetAbsOrigin() );
		if ( enemyDist < ( sk_advisor_roaming_burst_dist.GetFloat() + ADVISOR_ROAMING_STITCH_MIN ) )
		{
			stitchOffset = ADVISOR_ROAMING_STITCH_MIN;
		}
		else
		{
			stitchOffset = sk_advisor_roaming_burst_dist.GetFloat();
		}

		// Move out to the start of our stitch run
		m_vecAttackPosition = enemyPos + ( offsetDir * stitchOffset );
		m_vecAttackPosition.z = enemyPos.z;

		// Point at our target
		m_vecAttackVelocity = -offsetDir * BASE_STITCH_VELOCITY;

		CSoundEnt::InsertSound( SOUND_DANGER | SOUND_CONTEXT_REACT_TO_SOURCE, enemyPos, 512, 0.2f, this );
	}
}


//------------------------------------------------------------------------------
// Purpose : The proper way to cease the advisor_roaming cannon firing. 
//------------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::StopCannonBurst( void )
{
	m_iBurstHits = 0;
	m_bIsFiring = false;
	m_bPreFire = false;

	// Reduce the burst time when we get lower in health
	float flPerc = (float)GetHealth() / (float)GetMaxHealth();
	float flDelay = clamp( flPerc * m_flBurstDelay, 0.5, m_flBurstDelay );

	// If we didn't finish the burst, don't wait so long
	flPerc = 1.0 - (m_iBurstSize / sk_advisor_roaming_burst_size.GetFloat());
	flDelay *= flPerc;

	m_flTimeNextAttack = gpGlobals->curtime + flDelay;
	m_iBurstSize = 0;

	// Stop the cannon sound.
	if ( m_pCannonSound != NULL )
	{
		CSoundEnvelopeController::GetController().SoundChangeVolume( m_pCannonSound, 0.0, 0.05 );
	}

	EmitSound( "NPC_CombineGunship.CannonStopSound" );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::StopLoopingSounds( void )
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	if ( m_pCannonSound )
	{
		controller.SoundDestroy( m_pCannonSound );
		m_pCannonSound = NULL;
	}

	if ( m_pRotorSound )
	{
		controller.SoundDestroy( m_pRotorSound );
		m_pRotorSound = NULL;
	}

	if ( m_pAirExhaustSound )
	{
		controller.SoundDestroy( m_pAirExhaustSound );
		m_pAirExhaustSound = NULL;
	}

	if ( m_pAirBlastSound )
	{
		controller.SoundDestroy( m_pAirBlastSound );
		m_pAirBlastSound = NULL;
	}

	BaseClass::StopLoopingSounds();
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEnemy - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CombineAdvisorRoaming::IsValidEnemy( CBaseEntity *pEnemy )
{
	// Always track missiles
	if ( pEnemy->IsAlive() && !pEnemy->MyNPCPointer() && FClassnameIs( pEnemy, "rpg_missile" ) )
		return true;

	// If we're shooting off a burst, don't pick up a new enemy
	if ( ( m_bIsFiring ) && ( ( GetEnemy() == NULL ) || ( GetEnemy() != pEnemy ) ) )
		return false;

	return BaseClass::IsValidEnemy( pEnemy );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::GatherEnemyConditions( CBaseEntity *pEnemy )
{
	BaseClass::GatherEnemyConditions(pEnemy);

	// If we can't see the enemy for a few seconds, consider him unreachable
	if ( !HasCondition(COND_SEE_ENEMY) )
	{
		if ( gpGlobals->curtime - GetEnemyLastTimeSeen() >= 3.0f )
		{
			MarkEnemyAsEluded();
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: Tells us whether or not we're targetting an incoming missile
//-----------------------------------------------------------------------------
bool CNPC_CombineAdvisorRoaming::IsTargettingMissile( void )
{
	if ( GetEnemy() == NULL )
		return false;

	if ( FClassnameIs( GetEnemy(), "rpg_missile" ) == false )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputBecomeInvulnerable( inputdata_t &input )
{
	m_bInvulnerable = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_CombineAdvisorRoaming::InputBecomeVulnerable( inputdata_t &input )
{
	m_bInvulnerable = false;
}


//////////////////////////////////////////////////////////////////////////
//
//	NPC cooperation functions
//
//////////////////////////////////////////////////////////////////////////

void CNPC_CombineAdvisorRoaming::AdvisorHost_ExitCoopMode( IAdvisorCooperation *pCoop )
{
	CBaseEntity *pEnt = pCoop->Advisor_GetCoopEntity();
	m_coop_npcs.FindAndFastRemove( pEnt );
}

void CNPC_CombineAdvisorRoaming::AdvisorHost_ExitLevitationMode( IAdvisorCooperation *pCoop )
{
	if ( !m_levitate_pPhysController )
		return;

	IPhysicsObject *pPhys = pCoop->Advisor_RequestLevitationMode();
	if ( !pPhys )
		return;
	
	m_levitate_pPhysController->DetachObject( pPhys );
}


//////////////////////////////////////////////////////////////////////////
//
//	Default implementation of NPC cooperation
//
//////////////////////////////////////////////////////////////////////////

CAdvisorCooperationDefaultImpl::CAdvisorCooperationDefaultImpl() :
	m_advisor_bCooperate( false ),
	m_advisor_bLevitating( false ),
	m_advisor_hEnt( NULL )
{
	;
}

IAdvisorCooperationHost * CAdvisorCooperationDefaultImpl::Advisor_GetHost()
{
	CBaseEntity *pEnt = m_advisor_hEnt;
	if ( !pEnt )
		return NULL;
	return dynamic_cast< IAdvisorCooperationHost * >( pEnt );
}

CBaseEntity * CAdvisorCooperationDefaultImpl::Advisor_GetCoopEntity()
{
	return dynamic_cast< CBaseEntity * >( this );
}

bool CAdvisorCooperationDefaultImpl::Advisor_RequestEnterCoopMode( IAdvisorCooperationHost *pHost )
{
	if ( !m_advisor_bCooperate )
		return false;

	if ( !pHost )
		return false;

	CBaseEntity *pEnt = pHost->AdvisorHost_GetEntity();
	if ( !pEnt )
		return false;

	CBaseEntity *pMyAdvisor = m_advisor_hEnt;
	if ( pMyAdvisor && pMyAdvisor != pEnt )
		// Prevent another advisor controlling me
		return false;

	// Accept coop request from this advisor
	m_advisor_hEnt = pEnt;
	return true;
}

void CAdvisorCooperationDefaultImpl::Advisor_ExitCoopMode( IAdvisorCooperationHost *pHost )
{
	Advisor_ExitLevitationMode();
	m_advisor_hEnt = NULL;
}

IPhysicsObject * CAdvisorCooperationDefaultImpl::Advisor_RequestLevitationMode()
{
	IPhysicsObject *pPhysObject = Advisor_GetCoopEntity()->VPhysicsGetObject();
	if ( !Advisor_IsLevitating() && pPhysObject )
	{
		m_advisor_bLevitating = true;
		Advisor_RememberShadowControllerSettings( pPhysObject->GetShadowController() );
		pPhysObject->RemoveShadowController();
		Advisor_OnStartedLevitating();
	}
	return pPhysObject;
}

void CAdvisorCooperationDefaultImpl::Advisor_ExitLevitationMode()
{
	if ( !Advisor_IsLevitating() )
		return;
	
	m_advisor_bLevitating = false;

	Advisor_RestoreShadowControllerSettings( Advisor_GetCoopEntity()->VPhysicsGetObject() );

	Advisor_OnFinishedLevitating();
}


void CAdvisorCooperationDefaultImpl::Advisor_RememberShadowControllerSettings( IPhysicsShadowController *c )
{
	m_shadow_flags = 0;
	if ( !c )
		return;

	m_shadow_flags = SF_CONTROLLER |
		( c->AllowsTranslation() ? SF_TRANSLATION : 0 ) |
		( c->AllowsRotation() ? SF_ROTATION : 0 ) |
		( c->IsPhysicallyControlled() ? SF_PHYSCTRL : 0 );

	m_shadow_flTeleportDist = c->GetTeleportDistance();
	c->GetMaxSpeed( &m_shadow_maxSpeed, &m_shadow_maxAngSpeed );
}

void CAdvisorCooperationDefaultImpl::Advisor_RestoreShadowControllerSettings( IPhysicsObject *p )
{
	if ( !p )
		return;

	if ( m_shadow_flags & SF_CONTROLLER )
	{
#pragma warning ( disable : 4800 ) // forcing value to bool 'true' or 'false' (performance warning)
		p->SetShadow( m_shadow_maxSpeed, m_shadow_maxAngSpeed,
			( m_shadow_flags & SF_TRANSLATION ) != 0, ( m_shadow_flags & SF_ROTATION ) != 0 );
		
		if ( IPhysicsShadowController *c = p->GetShadowController() )
		{
			c->SetTeleportDistance( m_shadow_flTeleportDist );
			c->SetPhysicallyControlled( ( m_shadow_flags & SF_PHYSCTRL ) != 0 );
		}
#pragma warning ( default : 4800 )
	}
}




//////////////////////////////////////////////////////////////////////////
//
//	Levitation functions
//
//////////////////////////////////////////////////////////////////////////

void CNPC_CombineAdvisorRoaming::Levitate_FindObjects()
{
	m_levitate_lstObjects.RemoveAll();
	m_levitate_orgObjects.RemoveAll();

	CBaseEntity *ents[128];

	class CEnumEnts : public CFlaggedEntitiesEnum
	{
	public:
		CEnumEnts( CNPC_CombineAdvisorRoaming *pAdvisor, CBaseEntity **pList, int nMaxListElems ) :
		  CFlaggedEntitiesEnum( pList, nMaxListElems, 0 ),
		  m_pAdvisor( pAdvisor ) {}

		virtual IterationRetval_t EnumElement( IHandleEntity *phEnt )
		{
			CBaseEntity *pEnt = gEntList.GetBaseEntity( phEnt->GetRefEHandle() );
			if ( m_pAdvisor->Levitate_EntityFilter( pEnt ) )
				return CFlaggedEntitiesEnum::EnumElement( phEnt );
			else
				return ITERATION_CONTINUE;
		}

		CNPC_CombineAdvisorRoaming *m_pAdvisor;
	}
	entsEnumHelper( this, ents, ARRAYSIZE( ents ) );

	Vector vecBoxDimensions( sk_advisor_roaming_levitate_box.GetFloat(), sk_advisor_roaming_levitate_box.GetFloat(), sk_advisor_roaming_levitate_box.GetFloat() * 8 );
	int numEnts = UTIL_EntitiesInBox( GetAbsOrigin() - vecBoxDimensions, GetAbsOrigin() + vecBoxDimensions, &entsEnumHelper );

	int numMassCount = sk_advisor_roaming_levitate_count.GetInt();
	int numMassSmallCount = sk_advisor_roaming_levitate_count_small.GetInt();
	float flMassCountMin = sk_advisor_roaming_levitate_count_mass_min.GetFloat();

	for ( int k = 0; k < numEnts; ++ k )
	{
		if ( IPhysicsObject *pPhys = ents[k]->VPhysicsGetObject() )
		{
			float flMass = pPhys->GetMass();
			if ( pPhys->GetGameFlags() & FVPHYSICS_WAS_THROWN )
			{
				// object was thrown by player, keep it
				;
			}
			else if ( int iExploitableType = IsExploitableByPlayer( ents[k] ) )
			{
				// object can be exploited, keep it
				;
			}
			else if ( flMass > flMassCountMin )
			{
				-- numMassCount;
				if ( numMassCount < 0 )
					continue;
			}
			else
			{
				-- numMassSmallCount;
				if ( numMassSmallCount < 0 )
					continue;
			}
		}

		m_levitate_lstObjects.AddToTail( ents[ k ] );

		Vector vecOrgObj = ents[ k ]->GetAbsOrigin();
		vecOrgObj.x = vecOrgObj.y = 0;
		m_levitate_orgObjects.AddToTail( vecOrgObj );
	}
}

void CNPC_CombineAdvisorRoaming::Levitate_Start()
{
	StopRotorWash();

	Levitate_FindObjects();

	Levitate_AttachObjectsToController();

	SetContextThink( &CNPC_CombineAdvisorRoaming::Levitate_Update,
		gpGlobals->curtime + sk_advisor_roaming_levitate_think_update.GetFloat(),
		s_szThinkLevitate );
}

void CNPC_CombineAdvisorRoaming::Levitate_Update()
{
	SetContextThink( &CNPC_CombineAdvisorRoaming::Levitate_Update,
		gpGlobals->curtime + sk_advisor_roaming_levitate_think_update.GetFloat(),
		s_szThinkLevitate );

	if ( m_avalanche_eStage != AS_NONE )
		return;

	CUtlVector< EHANDLE > lstAttachedObjects;
	lstAttachedObjects.AddMultipleToTail( m_levitate_lstObjects.Count(), m_levitate_lstObjects.Base() );

	CUtlVector< Vector > lstOriginalPos;
	lstOriginalPos.AddMultipleToTail( m_levitate_orgObjects.Count(), m_levitate_orgObjects.Base() );

	Levitate_FindObjects();

	// Now for the objects that existed before we should retain the original pos
	// TODO: this is very un-optimal, will re-write the data storage for objects and pos later
	for ( int k = 0; k < m_levitate_lstObjects.Count(); ++ k )
	{
		for ( int j = 0; j < lstAttachedObjects.Count(); ++ j )
		{
			CBaseEntity *pEntOld = lstAttachedObjects[j].Get();
			CBaseEntity *pEntNew = m_levitate_lstObjects[k].Get();
			if ( !pEntOld || pEntOld != pEntNew )
				continue;

			m_levitate_orgObjects[ k ] = lstOriginalPos[ j ];
			break;
		}
	}

	// Now notify NPC's that are no longer in the zone of influence
	for ( int k = 0; k < lstAttachedObjects.Count(); ++ k )
	{
		CBaseEntity *pEntOld = lstAttachedObjects[k].Get();
		if ( !pEntOld || !pEntOld->IsNPC() )
			continue;
		if ( m_levitate_lstObjects.HasElement( pEntOld ) )
			continue;

		IAdvisorCooperation *pCoop = dynamic_cast< IAdvisorCooperation * >( pEntOld );
		if ( !pCoop )
			continue;
		pCoop->Advisor_ExitLevitationMode();
		pCoop->Advisor_ExitCoopMode( this );
		m_coop_npcs.FindAndFastRemove( pEntOld );
	}

	Levitate_AttachObjectsToController();
}

void CNPC_CombineAdvisorRoaming::Levitate_AttachObjectsToController()
{
	if ( !m_levitate_pPhysController )
	{
		m_levitate_pPhysController = physenv->CreateMotionController( &m_levitate_fnCallback );
	}

	m_levitate_pPhysController->ClearObjects();

	int numHeavyObjects = 0;
	float flHeavyMass = sk_advisor_roaming_levitate_count_mass_min.GetFloat();

	for ( int i = 0; i < m_levitate_lstObjects.Count(); i++ )
	{
		CBaseEntity *pEnt = m_levitate_lstObjects.Element( i );
		if ( !pEnt )
			continue;

		IPhysicsObject *pPhys = NULL;

		if ( pEnt->IsNPC() )
		{
			if ( IAdvisorCooperation *pCoop = dynamic_cast< IAdvisorCooperation * >( pEnt ) )
				pPhys = pCoop->Advisor_RequestLevitationMode();
		}
		else
		{
			pPhys = pEnt->VPhysicsGetObject();
			pEnt->SetOwnerEntity( this );	// declare advisor as owner
		}
		
		if ( pPhys && pPhys->IsMoveable() )
		{
			m_levitate_pPhysController->AttachObject( pPhys, false );
			pPhys->Wake();

			float flMass = pPhys->GetMass();
			if ( flMass >= flHeavyMass )
			{
				++ numHeavyObjects;

				// Trigger a little force around each heavy object
				// to prevent objects from sticking to each other
				Vector pos;
				QAngle ang;
				pPhys->GetPosition( &pos, &ang );

				EHANDLE hEntIgnore = pEnt;

				ExplosionCreate( pos, ang,
					this,
					sk_advisor_roaming_levitate_stick_mag.GetFloat(),
					sk_advisor_roaming_levitate_stick_rad.GetFloat(),
					SF_ENVEXPLOSION_NODAMAGE |
					SF_ENVEXPLOSION_NOFIREBALL |
					SF_ENVEXPLOSION_NOSMOKE |
					SF_ENVEXPLOSION_NODECAL |
					SF_ENVEXPLOSION_NOSPARKS |
					SF_ENVEXPLOSION_NOSOUND |
					SF_ENVEXPLOSION_NOFIREBALLSMOKE |
					SF_ENVEXPLOSION_NOPARTICLES |
					SF_ENVEXPLOSION_NODLIGHTS,
					sk_advisor_roaming_levitate_stick_frc.GetFloat(),
					NULL, -1,
					&hEntIgnore,
					CLASS_NONE );
			}
		}
	}

	int const threshAmmo = 2;

	//
	// We used to be low/out on ammo
	//
	if ( m_levitate_flTimeAmmoCondition )
	{
		if ( numHeavyObjects > threshAmmo )
		{
			// Now we have enough ammo
			m_levitate_flTimeAmmoCondition = 0;
			m_levitate_outEnoughAmmo.FireOutput( this, this );
		}
		else if ( m_levitate_flTimeAmmoCondition + sk_advisor_roaming_levitate_ammo_out_time.GetFloat() < gpGlobals->curtime )
		{
			// We have had the condition for a while by now
			m_levitate_flTimeAmmoCondition = 0;
			if ( !numHeavyObjects )
				m_levitate_outNoAmmo.FireOutput( this, this );
			else
				m_levitate_outLowAmmo.FireOutput( this, this );
		}
	}
	//
	// We used to have enough ammo
	//
	else if ( numHeavyObjects <= threshAmmo )
	{
		m_levitate_flTimeAmmoCondition = gpGlobals->curtime;
	}
}

void CNPC_CombineAdvisorRoaming::Levitate_Stop( float flSeconds )
{
	if ( flSeconds <= 0.0f )
		flSeconds = sk_advisor_roaming_levitate_drop_time.GetFloat();

	// SetContextThink( NULL, 0, s_szThinkLevitate );
	SetContextThink( &CNPC_CombineAdvisorRoaming::Levitate_Update,
		gpGlobals->curtime + flSeconds,
		s_szThinkLevitate );

	if ( m_levitate_pPhysController )
	{
		physenv->DestroyMotionController( m_levitate_pPhysController );
		m_levitate_pPhysController = NULL;
	}

	m_levitate_lstObjects.RemoveAll();
	m_levitate_orgObjects.RemoveAll();

	Throw_Clear();
}

bool CNPC_CombineAdvisorRoaming::Levitate_EntityFilter( CBaseEntity *pEntity )
{
	if ( !pEntity )
		return false;

	if ( FClassnameIs( pEntity, "teleport_projectile" ) )	// allow teleporting in the swirl
		return false;

	if ( pEntity->IsNPC() )
	{
		// Check if NPC can cooperate
		IAdvisorCooperation *pCoop = dynamic_cast< IAdvisorCooperation * >( pEntity );
		if ( !pCoop )
			return false;

		// Check if we are co-operating already
		if ( !m_coop_npcs.HasElement( pEntity ) )
		{
			// Enter coop mode
			if ( !pCoop->Advisor_RequestEnterCoopMode( this ) )
				return false;

			m_coop_npcs.AddToTail( pEntity );
		}

		// Request levitation phys object
		IPhysicsObject *pPhys = pCoop->Advisor_RequestLevitationMode();
		return pPhys != NULL;
	}

	if ( pEntity->GetMoveType() != MOVETYPE_VPHYSICS )
		return false;

// 	if ( Throw_WasThrown( pEntity ) )
// 		return false;

	IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
	if ( !pPhys )
		return false;

	if ( !pPhys->IsMoveable() )
		return false;

	float flMass = pPhys->GetMass();
	return ( flMass >= sk_advisor_roaming_levitate_mass_min.GetFloat() &&
		flMass <= sk_advisor_roaming_levitate_mass_max.GetFloat() );
}



//////////////////////////////////////////////////////////////////////////
//
//	Throwing objects functions
//
//////////////////////////////////////////////////////////////////////////

void CNPC_CombineAdvisorRoaming::Throw_Update()
{
	if ( GetEnemy() &&
		gpGlobals->curtime > m_throw_flNextTime )
		// Throwing objects
	{
		m_throw_flNextTime = gpGlobals->curtime;

		switch ( m_throw_eStage )
		{
		case TS_CLEAR:
			{
				// Don't throw if other attack is going on
				if ( Control_GetOtherAttackInProgress( ATK_THROW ) )
				{
					m_throw_flNextTime += sk_advisor_roaming_throw_time.GetFloat();
					break;
				}

				Throw_Clear();
				m_throw_lstThrown.RemoveAll();
				m_throw_eStage = TS_FIND;
			}
			break;

		case TS_FIND:
			{
				Throw_FindObjects();
				if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_THROW )
				{
					Msg( "Throw_Find = %d...\n", m_throw_lstObjects.Count() );
				}

				if ( !m_throw_lstObjects.Count() )
				{
					m_throw_eStage = TS_CLEAR;
					m_throw_flNextTime += sk_advisor_roaming_throw_time.GetFloat();
					break;
				}

				m_throw_eStage = TS_MOVEOUT;
				m_throw_iObject = 0;
				m_throw_flNextTime += sk_advisor_roaming_throw_time_warn.GetFloat();

				// EmitSound( "NPC_CombineGunship.SeeEnemy" );
			}
			break;

		case TS_MOVEOUT:
			{
				if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_THROW )
				{
					Msg( "Throw_MoveOut( %d )...\n", m_throw_iObject );
				}

				if ( m_throw_iObject >= m_throw_lstObjects.Count() )
				{
					m_throw_iObject = 0;
					m_throw_eStage = TS_THROW;
					m_throw_flNextTime += sk_advisor_roaming_throw_time_warn.GetFloat();
					break;
				}

				CBaseEntity *pEntity = m_throw_lstObjects[ m_throw_iObject ++ ];
				Throw_MoveObjectsOutOfWay( pEntity, Throw_GetTarget() );
			}
			break;

		case TS_THROW:
			{
				if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_THROW )
				{
					Msg( "Throw_Throw( %d )...\n", m_throw_iObject );
				}

				if ( m_throw_iObject >= m_throw_lstObjects.Count() )
				{
					Control_AttackCompleted();
					m_throw_flNextTime += sk_advisor_roaming_throw_time.GetFloat();
					++ m_throw_numTimes;
					break;
				}

				CBaseEntity *pEntity = m_throw_lstObjects[ m_throw_iObject ++ ];
				Throw_PerformThrow( pEntity, Throw_GetTarget() );
			}
			break;
		}
	}
}

void CNPC_CombineAdvisorRoaming::Throw_Clear()
{
	m_throw_lstObjects.RemoveAll();
	m_throw_iObject = 0;
	m_throw_flNextTime = gpGlobals->curtime;
	m_throw_eStage = TS_CLEAR;

	Client_HighlightOff();
}

struct AdvisorThrowFindObjects_t
{
	Vector pos;				// object position
	float mass;				// object mass
	float distFromAdvisor;	// sq. dist from advisor
	bool bSeenByPlayer;		// true if object in player's fov
	bool bIsNpc;			// true if object is an NPC
};

void CNPC_CombineAdvisorRoaming::Throw_FindObjects()
{
	CBasePlayer *pPlayer = ToBasePlayer( GetEnemy() );
	if ( !pPlayer )
		return;

	// Check that we have objects levitating
	int const numObjects = m_levitate_lstObjects.Count();
	if ( !numObjects )
		return;

	// Player's position and look direction
	Vector eyes, lookDirection;
	eyes = pPlayer->EyePosition();
	pPlayer->EyeVectors( &lookDirection );

	// Positions of objects selected
	CUtlVector< AdvisorThrowFindObjects_t > objs;

	// Check constants
	float flThresholdPos = sk_advisor_roaming_throw_bucket_dist.GetFloat();
	flThresholdPos *= flThresholdPos;
	float flThresholdMass = sk_advisor_roaming_throw_bucket_mass.GetFloat();
	float flThresholdHeight = sk_advisor_roaming_throw_bucket_height.GetFloat();

	// Search randomizer
	int const iPrime = 523;
	int iRandomOffset = RandomInt( 0, numObjects );

	float flPreferPickDist = sk_advisor_roaming_throw_pick_dist.GetFloat();
	flPreferPickDist *= flPreferPickDist;

	int numNpcsThreshold = sk_advisor_roaming_throw_bucket_npc_c.GetInt();
	if ( m_throw_numTimes % 3 != 0 )
		numNpcsThreshold = 0;

	// Iterate looking for objects
	for ( int k = 0; k < numObjects; ++ k )
	{
		int idx = ( iPrime * ( k + iRandomOffset ) ) % numObjects;
		CBaseEntity *pEnt = m_levitate_lstObjects[idx];
		if ( !pEnt )
			continue;

		if ( !Throw_EntityFilter( pEnt ) )
			continue;

		IPhysicsObject *pEntPhys = pEnt->VPhysicsGetObject();
		if ( !pEntPhys )
			continue;

		AdvisorThrowFindObjects_t x;

		x.pos = pEnt->GetAbsOrigin();
		x.mass = pEntPhys->GetMass();
		x.distFromAdvisor = 0.0f;
		x.bSeenByPlayer = true;
		x.bIsNpc = pEnt->IsNPC();

		// Height test
		if ( x.pos.z - eyes.z < flThresholdHeight )
			continue;

		// NPC allowed test
		if ( x.bIsNpc )
		{
			if ( numNpcsThreshold --<= 0 )
				continue;
		}

		// LOS test
		Vector eyeToEnt = x.pos - eyes;
		if ( eyeToEnt.AsVector2D().Dot( lookDirection.AsVector2D() ) < 0.85 ) // cos 32 ( = fov 64 )
			x.bSeenByPlayer = false;

		// Distance threshold test
		int numAllowDist = sk_advisor_roaming_throw_bucket_dist_c.GetInt();
		for ( int cc = 0; numAllowDist >= 0 && cc < objs.Count(); ++ cc )
		{
			if ( objs[ cc ].bIsNpc != x.bIsNpc )
				continue;

			if ( ( objs[ cc ].pos - x.pos ).LengthSqr() < flThresholdPos )
				-- numAllowDist;
		}
		if ( numAllowDist < 0 )
			continue;

		x.distFromAdvisor = ( x.pos - GetAbsOrigin() ).LengthSqr();

		// Mass threshold test
		int numAllowMass = sk_advisor_roaming_throw_bucket_mass_c.GetInt();
		for ( int cc = 0; numAllowMass >= 0 && cc < objs.Count(); ++ cc )
		{
			if ( objs[ cc ].bIsNpc != x.bIsNpc )
				continue;

			if ( fabs( objs[ cc ].mass - x.mass ) > flThresholdMass )
				continue;

			bool bReplaceObject = false;

			if ( x.bSeenByPlayer && !objs[ cc ].bSeenByPlayer )
				// we found an object in player's fov that is similar mass as
				// another object not seen by player, use the visible object
				// instead
				bReplaceObject = true;

			if ( 0 && ( objs[ cc ].distFromAdvisor > flPreferPickDist ) &&
				 ( x.distFromAdvisor < objs[ cc ].distFromAdvisor ) )
				 // we had an object of similar mass far out of preferred pick
				 // radius and now we found an object closer to advisor
				 // use the closer-to-advisor object instead
				 bReplaceObject = true;

			if ( fabs( objs[ cc ].distFromAdvisor - flPreferPickDist ) >
				 fabs( x.distFromAdvisor - objs[ cc ].distFromAdvisor ) )
				 // use the closer-to-pick-circle object instead
				 bReplaceObject = true;

			if ( x.mass > objs[ cc ].mass )
				// use the object of a bigger mass
				bReplaceObject = true;
			else
				bReplaceObject = false;

			if ( bReplaceObject )
			{
				objs[ cc ] = x;
				m_throw_lstObjects[ cc ] = pEnt;

				numAllowMass = -1; // break out
			}
			-- numAllowMass;
		}
		if ( numAllowMass < 0 )
			continue;

		// It's a good candidate entity
		objs.AddToTail( x );
		m_throw_lstObjects.AddToTail( pEnt );
	}

	// Mark the objects as getting thrown
	for ( int cc = 0; cc < m_throw_lstObjects.Count(); ++ cc )
	{
		Throw_SetThrown( m_throw_lstObjects[ cc ] );
	}
}

bool CNPC_CombineAdvisorRoaming::Throw_EntityFilter( CBaseEntity *pEntity )
{
// 	if ( pEntity->IsNPC() )
// 		return false;

	if ( int iExploitableType = IsExploitableByPlayer( pEntity ) )
	{
		// player can exploit it, don't throw
		return false;
	}

	return true;
}

void CNPC_CombineAdvisorRoaming::Throw_MoveObjectsOutOfWay( CBaseEntity *pEntity, Vector const &tgt )
{
	if ( !pEntity )
		return;

	// Apply a cheap solution - bump the entity up in the air
	if ( 0 )
	{
		IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
		if ( pPhys )
		{
			Vector vel( 0, 0, sk_advisor_roaming_throw_moveout_vel.GetFloat() );
			pPhys->AddVelocity( &vel, NULL );
		}
	}

	// Apply a cheap solution - bump the entity towards the enemy
	if ( 1 )
	{
		IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
		if ( pPhys )
		{
			Vector posTgt = Throw_GetTarget();
			Vector dirTgt = posTgt - GetAbsOrigin();
			dirTgt.NormalizeInPlace();
			dirTgt *= sk_advisor_roaming_throw_moveout_vel.GetFloat();
			pPhys->SetVelocity( &dirTgt, NULL );
		}
	}

	// Apply a cheap solution - create a little explosion force where the object is
	if ( 1 )
	{
		IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
		if ( pPhys )
		{
			m_levitate_pPhysController->WakeObjects();

			Vector pos;
			QAngle ang;
			pPhys->GetPosition( &pos, &ang );

			EHANDLE hEntIgnore = pEntity;
			
			ExplosionCreate( pos, ang,
				this,
				sk_advisor_roaming_throw_moveout_mag.GetFloat(),
				sk_advisor_roaming_levitate_shield.GetFloat(),
				SF_ENVEXPLOSION_NODAMAGE |
				SF_ENVEXPLOSION_NOFIREBALL |
				SF_ENVEXPLOSION_NOSMOKE |
				SF_ENVEXPLOSION_NODECAL |
				SF_ENVEXPLOSION_NOSPARKS |
				SF_ENVEXPLOSION_NOSOUND |
				SF_ENVEXPLOSION_NOFIREBALLSMOKE |
				SF_ENVEXPLOSION_NOPARTICLES |
				SF_ENVEXPLOSION_NODLIGHTS,
				sk_advisor_roaming_throw_moveout_force.GetFloat(),
				NULL, -1,
				&hEntIgnore,
				CLASS_NONE );
		}
	}

	return;

	// look for objects in the way
	CBaseEntity *list[128];
	Ray_t ray;

	float boundingRadius = pEntity->BoundingRadius();
	Vector boundingVector( boundingRadius, boundingRadius, boundingRadius);

	ray.Init( pEntity->GetAbsOrigin(), tgt,
		- boundingVector, boundingVector );

	if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_THROW )
	{
		NDebugOverlay::Box( pEntity->GetAbsOrigin(), -boundingVector, boundingVector,
			0, 255, 0, 255, 1.0f );
	}

	int numInTheWay = UTIL_EntitiesAlongRay( list, ARRAYSIZE( list ), ray, 0 );
	for ( int k = 0; k < numInTheWay; ++ k )
	{
		CBaseEntity *obstruction = list[ k ];
		
		if ( obstruction == pEntity )
			continue;

		if ( !m_levitate_lstObjects.HasElement( obstruction ) )
			// not levitating
			continue;

		IPhysicsObject *pPhys = obstruction->VPhysicsGetObject();
		if ( !pPhys )
			continue;

		Vector physVel, physAng;
		pPhys->GetVelocity( &physVel, &physAng );

		if ( physVel.z > 0.9 * sk_advisor_roaming_throw_moveout_vel.GetFloat() )
			// object already getting out of the way
			continue;

		Vector vel( 0, 0, sk_advisor_roaming_throw_moveout_vel.GetFloat() );
		pPhys->AddVelocity( &vel, NULL );

		if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_THROW )
		{
			NDebugOverlay::Box( obstruction->GetAbsOrigin(), -boundingVector, boundingVector,
				200, 0, 0, 255, 1.0f );
		}
	}
}

void CNPC_CombineAdvisorRoaming::Throw_PerformThrow( CBaseEntity *pEntity, Vector const &tgt )
{
	if ( !pEntity )
		return;

	IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
	if ( !pPhys )
		return;

	if ( g_debug_advisor_roaming.GetInt() == ADVISOR_ROAMING_DEBUG_THROW )
	{
		NDebugOverlay::Box( pEntity->GetAbsOrigin(), pEntity->CollisionProp()->OBBMins(), pEntity->CollisionProp()->OBBMaxs(),
			0, 0, 255, 255, 1.0f );
	}

// 	if ( m_levitate_pPhysController )
// 	{
// 		m_levitate_pPhysController->DetachObject( pPhys );
// 	}

	Vector posEnt = pEntity->CollisionProp()->WorldSpaceCenter();
	
	Vector throwLine = tgt - posEnt;
// 	float flThrowDist = throwLine.Length();

	float flVel = sk_advisor_roaming_throw_vel.GetFloat();
	Vector vecThrowVel( throwLine );
	vecThrowVel.NormalizeInPlace();
	vecThrowVel *= flVel;

	// float flDrag = 0.0f;
	pPhys->SetVelocity( &vecThrowVel, NULL );

	pEntity->SetOwnerEntity( this );	// declare advisor as owner

	// Emit the throw sound
	pEntity->EmitSound( "NPC_CombineBall.WhizFlyby" );
}

Vector CNPC_CombineAdvisorRoaming::Throw_GetTarget()
{
	CBasePlayer *pPlayer;
	if ( ( gpGlobals->curtime > sk_advisor_roaming_throw_laglead.GetFloat() + m_throw_flLastTgtTime ) &&
		 ( pPlayer = ToBasePlayer( GetEnemy() ) ) != NULL )
		// Update player's target every 0.1 sec
	{
		m_throw_lastThrowTgt = pPlayer->EyePosition();
// 		UTIL_PredictedPosition( pPlayer, sk_advisor_roaming_throw_laglead.GetFloat(),
// 			&m_throw_lastThrowTgt );
// 		m_throw_lastThrowTgt += pPlayer->GetEyeOffset();

		// Check if player is looking at advisor
		Vector lookDirection;
		pPlayer->EyeVectors( &lookDirection );

		Vector dirAtAdvisor = GetAbsOrigin() - m_throw_lastThrowTgt;
		
		Vector2D lookDirection2D = lookDirection.AsVector2D();
		lookDirection2D.NormalizeInPlace();

		Vector2D dirAtAdvisor2D = dirAtAdvisor.AsVector2D();
		dirAtAdvisor2D.NormalizeInPlace();

		if ( lookDirection2D.Dot( dirAtAdvisor2D ) < sk_advisor_roaming_throw_deadzone.GetFloat() )
		{
			float flAdjustment = sk_advisor_roaming_throw_deadzone_adj.GetFloat();
			float flAdjustmentH = sk_advisor_roaming_throw_deadzone_adjh.GetFloat();
			// Player is fleeing from the advisor, toss above player's head
			m_throw_lastThrowTgt.z += flAdjustment;
			
			// Also miss the player laterally
			Vector2D dirOffTarget;
			dirOffTarget.x = - dirAtAdvisor.y;
			dirOffTarget.y = dirAtAdvisor.x;
			dirOffTarget.NormalizeInPlace();

			dirOffTarget *= RandomFloat( -flAdjustmentH, flAdjustmentH );

			m_throw_lastThrowTgt.x += dirOffTarget.x;
			m_throw_lastThrowTgt.y += dirOffTarget.y;
		}
	}
	return m_throw_lastThrowTgt;
}

void CNPC_CombineAdvisorRoaming::Throw_SetThrown( CBaseEntity *pEntity )
{
	if ( !pEntity )
		return;

	m_throw_lstThrown.AddToTail( pEntity );

	IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
	pPhys->SetVelocity( &vec3_origin, NULL );

	Client_HighlightOn( pEntity );
}

bool CNPC_CombineAdvisorRoaming::Throw_WasThrown( CBaseEntity *pEntity )
{
	return m_throw_lstThrown.HasElement( pEntity );
}


//////////////////////////////////////////////////////////////////////////
//
//	Client communications
//
//////////////////////////////////////////////////////////////////////////

void CNPC_CombineAdvisorRoaming::Client_HighlightOn( CBaseEntity *pEntity )
{
	EntityMessageBegin( this, true );
		WRITE_BYTE( ADVISOR_ROAMING_MSG_HIGHTLIGHT_ON );
		WRITE_LONG( pEntity->entindex() );
	MessageEnd();
}

void CNPC_CombineAdvisorRoaming::Client_HighlightOff( void )
{
	EntityMessageBegin( this, true );
		WRITE_BYTE( ADVISOR_ROAMING_MSG_HIGHTLIGHT_OFF );
	MessageEnd();
}

void CNPC_CombineAdvisorRoaming::Client_HighlightPullOn( CBaseEntity *pEntity )
{
	EntityMessageBegin( this, true );
		WRITE_BYTE( ADVISOR_ROAMING_MSG_HIGHTLIGHT_PULL_ON );
		WRITE_LONG( pEntity->entindex() );
	MessageEnd();
}

void CNPC_CombineAdvisorRoaming::Client_HighlightPullOff( void )
{
	EntityMessageBegin( this, true );
		WRITE_BYTE( ADVISOR_ROAMING_MSG_HIGHTLIGHT_PULL_OFF );
	MessageEnd();
}




//==============================================================================================
// MOTION CALLBACK
//==============================================================================================
CAdvisorRoamingLevitate::simresult_e
CAdvisorRoamingLevitate::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
   	// this function can be optimized to minimize branching if necessary (PPE branch prediction) 
	CNPC_CombineAdvisorRoaming *pAdvisor = static_cast<CNPC_CombineAdvisorRoaming *>(m_Advisor.Get());
	Assert(pAdvisor);
	
	// Advisor settings
	Vector posAdvisor = pAdvisor->GetAbsOrigin();
	float flDistFalloffStart = sk_advisor_roaming_levitate_falloff_start.GetFloat();
	float flDistFalloffEnd = sk_advisor_roaming_levitate_box.GetFloat();
	float flDistClear = sk_advisor_roaming_levitate_clear_radius.GetFloat();

	// Independent movement of all objects
	Vector vel; AngularImpulse angvel;
	pObject->GetVelocity(&vel,&angvel);
	Vector pos;
	pObject->GetPosition(&pos,NULL);
	bool bMovingUp = vel.z > 0;

	// Determine this objects original position
	// TODO: optimize this discovery
	Vector *pvecOrigObj = NULL;
	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());
	int iExploitable = IsExploitableByPlayer( pEntity );
	float flOrigZ = 0.0f;

	int idx = pAdvisor->m_levitate_lstObjects.Find( pEntity );
	if ( idx != pAdvisor->m_levitate_lstObjects.InvalidIndex() )
	{
		pvecOrigObj = &pAdvisor->m_levitate_orgObjects[idx];
		flOrigZ = pvecOrigObj->z;
	}

	// Now determine what the floating range would be taking falloff into account
	float flRangeUp = MAX( posAdvisor.z - flOrigZ, 0.0f );
	float flRangeBuffer = sk_advisor_roaming_levitate_range_buffer.GetFloat();

	// 2D distance from advisor
	Vector vecFromAdvisorRadial = pos - posAdvisor;
	Vector vecAdvisorEnemy = pAdvisor->Throw_GetTarget();
	Vector vecFromAdvisorToEnemy = vecAdvisorEnemy - posAdvisor;
	float flDistFromAdvisor2D = vecFromAdvisorRadial.Length2D();
	float flFalloffCoef = 1.0f;
	if ( flDistFromAdvisor2D > flDistFalloffStart )
	{
		flFalloffCoef = 1.0f - ( flDistFromAdvisor2D - flDistFalloffStart ) / ( flDistFalloffEnd - flDistFalloffStart );
		flFalloffCoef = MAX( 0.0f, flFalloffCoef );
	}

	// Adjust the range according to falloff
	flRangeUp *= flFalloffCoef;

	float flNowZ = pos.z - flOrigZ;

	float flRangeBottom = flRangeUp * sk_advisor_roaming_levitate_range_bottom.GetFloat();
	flRangeUp *= sk_advisor_roaming_levitate_range_top.GetFloat();

	float flRandom = RandomFloat();

#define RndConVarValue( cvarname ) ( flRandom * cvarname##_max.GetFloat() + ( 1 - flRandom ) * cvarname##_min.GetFloat() )

	float flSpeed = RndConVarValue( sk_advisor_roaming_levitate_acc );
	float flSpinAng = RndConVarValue( sk_advisor_roaming_levitate_spin_ang );
	float flSpinRot = RndConVarValue( sk_advisor_roaming_levitate_spin_rot );

	flSpinAng *= flFalloffCoef;
	flSpinRot *= MAX( flSpeed, flFalloffCoef );

	// Determine the linear acceleration on the tangent of a swirl
	Vector vecSwirlTangent;
	vecSwirlTangent.x = -vecFromAdvisorRadial.y;
	vecSwirlTangent.y = vecFromAdvisorRadial.x;
	vecSwirlTangent.z = 0;
	float flSwirlLength = vecSwirlTangent.Length();
	float flShieldSize = sk_advisor_roaming_levitate_shield.GetFloat();
	if ( flShieldSize > 0.0f )
	{
		float flDrift = sk_advisor_roaming_levitate_shield_drift.GetFloat();
		float flDriftZ = sk_advisor_roaming_levitate_shield_drift_z.GetFloat();
		float flDriftK = sk_advisor_roaming_levitate_shield_drift_k.GetFloat();
		float flKeepCloseFactor = 1.0f;

		if ( iExploitable )
		{
			// Keep it in front of the advisor behind the shield
			flShieldSize /= 4.0f;
			flKeepCloseFactor = 0.75f;
			flDriftK *= 2;
		}

		Vector dirToEnemy = vecFromAdvisorToEnemy;
		dirToEnemy.NormalizeInPlace();

		Vector velAdvisor;
		pAdvisor->GetVelocity( &velAdvisor, NULL );

		Vector posShield = posAdvisor + dirToEnemy * flRangeBuffer * flKeepCloseFactor +
			sk_advisor_roaming_levitate_shield_predict.GetFloat() * velAdvisor;
		Vector dirUp( 0, 0, 1 );
		Vector dirRight = dirToEnemy.Cross( dirUp );
		dirRight.NormalizeInPlace();
		dirUp = dirRight.Cross( dirToEnemy );

		Vector vecDrift;
		if ( iExploitable )
			vecDrift = RandomVector( -flShieldSize/100, flShieldSize/100 );
		else
			vecDrift = RandomVector( -flShieldSize, flShieldSize );

		Vector vecDest = posShield +
			( vecDrift.x + flShieldSize ) * dirToEnemy +
			vecDrift.y * dirRight +
			vecDrift.z * dirUp;
		
		vecSwirlTangent = vecDest - pos;
		float flDistFromDest = vecSwirlTangent.NormalizeInPlace();
		
		if ( pos.z > flDriftZ && flDistFromDest > flShieldSize )
		{
			// flDrift *= ( 1 +
			flDrift *= pow( 2.0f,
				( pos.z - flDriftZ ) * flDriftK +
				( flDistFromDest - flShieldSize ) * flDriftK
				);
		}

		vecSwirlTangent *= flDrift;
	}
	else if ( flSwirlLength < 10.0f )
	{
		vecSwirlTangent.x = vecSwirlTangent.y = 0.0f;
	}
	else
	{
		flSwirlLength = sk_advisor_roaming_levitate_swirl_acc.GetFloat() / flSwirlLength;
		vecSwirlTangent.x *= flSwirlLength;
		vecSwirlTangent.y *= flSwirlLength;

		Vector vecToAdvisor( -vecSwirlTangent.y, vecSwirlTangent.x, 0.0f );

		float flSwirlIn = sk_advisor_roaming_levitate_swirl_in.GetFloat();

		if ( vecFromAdvisorToEnemy.AsVector2D().Dot( vecFromAdvisorRadial.AsVector2D() ) < 0 )
		{
			Vector vecPush = vecFromAdvisorToEnemy;
			vecPush.z = 0;
			vecPush.NormalizeInPlace();

			vecSwirlTangent += flSwirlIn * vecToAdvisor.Length() * vecPush;
		}
		else if ( flDistFromAdvisor2D < flDistClear )
		{
			vecSwirlTangent -= flSwirlIn * 2.0 * vecToAdvisor;
		}
		else
		{
			vecSwirlTangent += flSwirlIn * vecToAdvisor;
		}
	}

	if ( pAdvisor->m_avalanche_eStage == CNPC_CombineAdvisorRoaming::AS_SHOCKWAVE )
	{
		// Don't kill the player in the end
		if ( ( vecAdvisorEnemy - pos ).LengthSqr() < 128.0f * 128.0f )
			return SIM_NOTHING;

		// Advisor is doing a shockwave make all the objects target the player
		pAdvisor->Avalanche_ShockWave(); // Simulate the shock wave front

		Vector dir = vecFromAdvisorToEnemy;
		dir.z = 0.0f;
		dir.NormalizeInPlace();

		float flDistFwd = dir.Dot( vecFromAdvisorRadial );
		if ( flDistFwd < pAdvisor->m_avalanche_front )
		{
			linear = sk_advisor_roaming_av_acc.GetFloat() * dir;
			linear.z = sv_gravity.GetFloat();
			angular = Vector( flSpinAng, - flSpinAng, 0 );
		}
		else
		{
			return SIM_NOTHING;
		}
	}
	else
	if ( pAdvisor->Throw_WasThrown( pEntity ) )
	{
		Vector dir( 0, 0, 0 );
		
		if ( vel.Length() > sk_advisor_roaming_throw_acc_cap.GetFloat() )
		{
			dir = vecAdvisorEnemy - pos;
			float flDistFromEnemy = dir.NormalizeInPlace();

			if ( flDistFromEnemy < 128.0f )
				return SIM_NOTHING;

			Vector velNorm = vel;
			velNorm.NormalizeInPlace();

			dir *= sk_advisor_roaming_throw_acc.GetFloat();
		}
		else
		{
			dir = vecFromAdvisorToEnemy;
			dir.NormalizeInPlace();
			dir *= sk_advisor_roaming_throw_moveout_vel.GetFloat();

			pObject->SetVelocity( &dir, NULL );

			dir = vec3_origin;
			dir.z += sv_gravity.GetFloat();
		}

		linear = dir;
		angular = Vector( flSpinAng, - flSpinAng, 0 );
	}
	else
	if ( flShieldSize > 0.0f )
	{
		linear = Vector( vecSwirlTangent.x, vecSwirlTangent.y,
			vecSwirlTangent.z + sv_gravity.GetFloat() );
		angular = Vector( flSpinAng, - flSpinAng, 0 );
	}
	else
	// if above top limit and moving up, move down. if below bottom limit and moving down, move up.
	if (bMovingUp)
	{
		if ( flNowZ > flRangeUp )
		{
			// turn around move down
			linear = Vector( vecSwirlTangent.x, vecSwirlTangent.y, Square((1.0f - flSpeed)) * sv_gravity.GetFloat() );
			angular = Vector( flSpinAng, - flSpinAng, 0 );
			vel.z = 0;
			pObject->SetVelocity( &vel, NULL );
		}
		else
		{	// keep moving up 
			if ( flNowZ > flRangeUp - flRangeBuffer )
				flSpeed = -flSpeed;
			linear = Vector( vecSwirlTangent.x, vecSwirlTangent.y, (1.0f + flSpeed) * sv_gravity.GetFloat() );
			angular = Vector( 0, flSpeed * 20, flSpinRot );
		}
	}
	else
	{
		if ( flNowZ < flRangeBottom )
		{
			// turn around move up
			linear = Vector( vecSwirlTangent.x, vecSwirlTangent.y, Square((1.0f + flSpeed)) * sv_gravity.GetFloat() );
			angular = Vector( flSpinAng, flSpinAng, 0 );
			vel.z = 0;
			pObject->SetVelocity( &vel, NULL );
		}
		else
		{	// keep moving down
			if ( flNowZ < flRangeBottom + flRangeBuffer )
				flSpeed = -flSpeed;
			linear = Vector( vecSwirlTangent.x, vecSwirlTangent.y, (1.0f - flSpeed) * sv_gravity.GetFloat() );
			angular = Vector( 0, - flSpeed * 20, flSpinRot );
		}
	}
	
	return SIM_GLOBAL_ACCELERATION;
}

CAdvisorRoamingPull::simresult_e
CAdvisorRoamingPull::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
	CNPC_CombineAdvisorRoaming *pAdvisor = static_cast<CNPC_CombineAdvisorRoaming *>(m_Advisor.Get());
	if ( !pAdvisor )
		return SIM_NOTHING;

	pAdvisor->Pull_SimulateTongue();

	CBaseEntity *pEntity = static_cast<CBaseEntity *>(pObject->GetGameData());

	Vector pos;
	QAngle ang;
	pObject->GetPosition( &pos, &ang );
	
	Vector eyes = pEntity ? pEntity->EyePosition() : pos;
	Vector posDelta = pos - eyes;

	Vector posSuckTgt;
	pAdvisor->GetAttachment( pAdvisor->m_attachSuckTgt, posSuckTgt );

	Vector toAdvisor = posSuckTgt - eyes;
	float flCurrentDist = toAdvisor.NormalizeInPlace();
	float flPullSpeed = sk_advisor_roaming_pull_vel.GetFloat();
	toAdvisor *= flPullSpeed;

	if ( flCurrentDist > flPullSpeed * deltaTime )
	{
		// new proposed position
		eyes += toAdvisor * deltaTime;
	}
	else
	{
		eyes = posSuckTgt;
	}
	pos = eyes + posDelta;

	// Trace if the new position is ok
	trace_t tr;
	UTIL_TraceHull( pos, pos,
		VEC_HULL_MIN, VEC_HULL_MAX,
		MASK_PLAYERSOLID, pEntity,
		COLLISION_GROUP_NONE, &tr );
	
	if ( tr.startsolid )
	{
		// Player cannot fit there, try a different direction
	}
	else
	{
		pObject->SetPosition( pos, ang, true );

		if ( flCurrentDist > flPullSpeed * deltaTime )
			pObject->SetVelocity( &toAdvisor, &vec3_origin );
		else
			pObject->SetVelocity( &vec3_origin, &vec3_origin );
	}

// 	if ( CBaseEntity *pEnemy = pAdvisor->GetEnemy() )
// 	{
// 		pEnemy->SetAbsVelocity( vec3_origin );
// 	}

	linear = toAdvisor;
	angular = vec3_origin;

	return SIM_GLOBAL_ACCELERATION;
}

//////////////////////////////////////////////////////////////////////////
//
//	Advisor control implementation
//
//////////////////////////////////////////////////////////////////////////

void CNPC_CombineAdvisorRoaming::Control_ReturnToPath()
{
	// Return to path
	if ( GetDestPathTarget() )
	{
		SetupNewCurrentTarget( GetDestPathTarget() );
	}

}

void CNPC_CombineAdvisorRoaming::Control_AttackCompleted()
{
	float flDelayThrow = sk_advisor_roaming_throw_time.GetFloat();

	// Stop avalanche if we aren't planning to start avalanche
	if ( m_avalanche_eStage > AS_PREPARING )
	{
		flDelayThrow = sk_advisor_roaming_av_delaythrow.GetFloat();
		m_avalanche_eStage = AS_NONE;
	}

	// Clear our throw status
	if ( m_throw_eStage >= TS_THROW )
	{
		Throw_Clear();
	}

	// Clear out spit status
	if ( m_spit_eStage >= SPIT_THROW )
	{
		m_spit_eStage = SPIT_NONE;
	}

	// Clear out pull status
	if ( m_pull_eStage > PULL_PREPARING )
	{
		Pull_Clear();
	}

	// Adjust throw objects timer to cool down after attack
	m_throw_flNextTime = gpGlobals->curtime + flDelayThrow;
}

CNPC_CombineAdvisorRoaming::AdvisorAttack_t
CNPC_CombineAdvisorRoaming::Control_GetOtherAttackInProgress( AdvisorAttack_t eWantToStartAttack )
{
	if ( ( eWantToStartAttack != ATK_THROW ) &&
		 ( m_throw_eStage > TS_CLEAR ) )
	{
		return ATK_THROW;
	}

	if ( ( eWantToStartAttack != ATK_AVALANCHE ) &&
		 ( m_avalanche_eStage > AS_PREPARING ) )
	{
		return ATK_AVALANCHE;
	}

	if ( ( eWantToStartAttack != ATK_SPIT ) &&
		 ( m_spit_eStage > SPIT_PREPARING ) )
	{
		return ATK_SPIT;
	}

	if ( ( eWantToStartAttack != ATK_PULL ) &&
		 ( m_pull_eStage > PULL_PREPARING ) )
	{
		return ATK_PULL;
	}

	return ATK_NONE;
}


//////////////////////////////////////////////////////////////////////////
//
//	Spit implementation
//
//////////////////////////////////////////////////////////////////////////

void CNPC_CombineAdvisorRoaming::Spit_Update()
{
	if ( m_spit_eStage == SPIT_NONE )
	{
		SetContextThink( NULL, 0, s_szThinkSpit );
		return;
	}

	SetContextThink( &CNPC_CombineAdvisorRoaming::Spit_Update,
		gpGlobals->curtime + 0.1,
		s_szThinkSpit );

	switch( m_spit_eStage )
	{
	case SPIT_PREPARING:
		Spit_Prepare();
		break;

	case SPIT_THROW:
		Spit_Throw();
		break;

	case SPIT_FLYING:
		Spit_Flying();
		break;

	case SPIT_HIT:
		Spit_Hit();
		break;
	}
}

void CNPC_CombineAdvisorRoaming::Spit_Prepare()
{
	switch ( m_spit_eStage )
	{
	case SPIT_NONE:
		m_spit_eStage = SPIT_PREPARING;
		Spit_Update();
		break;

	case SPIT_PREPARING:
		// Make sure that other attacks are completed
		if ( Control_GetOtherAttackInProgress( ATK_SPIT ) )
			return;
		m_spit_eStage = SPIT_THROW;
		break;
	}
}

void CNPC_CombineAdvisorRoaming::Spit_Throw()
{
	// Get the mouth
	Vector vecPos;
	QAngle qAng;
	GetAttachment( "attach_mouth", vecPos, qAng );
	
	// Get the throw target
	Vector vecEnemy = Throw_GetTarget();

	// Get direction to enemy
	Vector dirToEnemy = vecEnemy - vecPos;
	dirToEnemy.NormalizeInPlace();

	// Check if we can spit
	if ( !CAdvisorGrenadeSpit::ComputeSpitParameters( this, vecPos, vecEnemy, m_spit_vel ) )
	{
		Control_AttackCompleted();
		return;
	}

	float flVelMagnitude = m_spit_vel.Length();
	float flVelSpread = sk_advisor_roaming_spit_spread.GetFloat();

	//
	// Okay, we can spit
	//
	for ( int i = 0; i < 6; i++ )
	{
		CAdvisorGrenadeSpit *pGrenade = (CAdvisorGrenadeSpit*) CreateEntityByName( "advisor_grenade_spit" );
		pGrenade->SetAbsOrigin( vecPos );
		pGrenade->SetAbsAngles( qAng );
		DispatchSpawn( pGrenade );
		pGrenade->SetThrower( this );
		pGrenade->SetOwnerEntity( this );

		if ( i == 0 )
		{
			pGrenade->SetSpitSize( CAdvisorGrenadeSpit::SPIT_LARGE );
			pGrenade->SetAbsVelocity( m_spit_vel );
			m_spit_ball = pGrenade;
		}
		else
		{
			pGrenade->SetAbsVelocity( m_spit_vel + RandomVector( -flVelSpread, flVelSpread ) * flVelMagnitude );
			pGrenade->SetSpitSize( random->RandomInt( CAdvisorGrenadeSpit::SPIT_SMALL, CAdvisorGrenadeSpit::SPIT_MEDIUM ) );
		}

		// Tumble through the air
		pGrenade->SetLocalAngularVelocity(
			QAngle( random->RandomFloat( -250, -500 ),
			random->RandomFloat( -250, -500 ),
			random->RandomFloat( -250, -500 ) ) );
	}

	m_spit_eStage = SPIT_FLYING;
}

void CNPC_CombineAdvisorRoaming::Spit_Flying()
{
	// Wait while the largest spit is in the air
	CBaseEntity *pEnt = m_spit_ball;
	if ( pEnt )
		return;

	m_spit_eStage = SPIT_HIT;
}

void CNPC_CombineAdvisorRoaming::Spit_Hit()
{
	m_spit_flTime = gpGlobals->curtime +
		RandomFloat( sk_advisor_roaming_spit_delay_min.GetFloat(), sk_advisor_roaming_spit_delay_max.GetFloat() );

	Control_AttackCompleted();
}

void CNPC_CombineAdvisorRoaming::Spit_DelayAttack()
{
	m_spit_flTime = gpGlobals->curtime + 10.0f;
}



//////////////////////////////////////////////////////////////////////////
//
//	Avalanche implementation
//
//////////////////////////////////////////////////////////////////////////

void CNPC_CombineAdvisorRoaming::Avalanche_Update()
{
	if ( m_avalanche_eStage == AS_NONE )
	{
		SetContextThink( NULL, 0, s_szThinkAvalanche );
		return;
	}

	SetContextThink( &CNPC_CombineAdvisorRoaming::Avalanche_Update,
		gpGlobals->curtime + 0.1,
		s_szThinkAvalanche );

	switch( m_avalanche_eStage )
	{
	case AS_PREPARING:
		Avalanche_Prepare();
		break;

	case AS_DESCENDING:
		Avalanche_Descend();
		break;

	case AS_WARNING:
		Avalanche_Warning();
		break;

	case AS_BLAST:
		Avalanche_DoBlast();
		break;

	case AS_SHOCKWAVE:
		Avalanche_ShockWave();
		break;
	}
}

void CNPC_CombineAdvisorRoaming::Avalanche_Prepare()
{
	switch ( m_avalanche_eStage )
	{
	case AS_NONE:
		m_avalanche_eStage = AS_PREPARING;
		Avalanche_Update();
		break;
	
	case AS_PREPARING:
		// Make sure that other attacks are completed
		if ( Control_GetOtherAttackInProgress( ATK_AVALANCHE ) )
			return;
		
		// Check the LOS to the player from the point where we will lower down
		if ( m_levitate_lstObjects.Count() > 0 )
		{
			Vector posAdvisor = GetAbsOrigin();
			trace_t tr;

			// Find the spot on the ground
			Vector dir( 0, 0, -2048 );
			UTIL_TraceLine( posAdvisor, posAdvisor + dir, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );
			posAdvisor += tr.fraction * dir;
			posAdvisor.z += 64;
			
			// Trace to the enemy
			if ( CBaseEntity *pEnemy = GetEnemy() )
			{
				Vector posEnemy = pEnemy->GetAbsOrigin();
				UTIL_TraceLine( posAdvisor, posEnemy, MASK_BLOCKLOS, this, COLLISION_GROUP_NONE, &tr );
				if ( tr.fraction > 0.8 )
				{
					// We can hit the player or stuff nearby
					m_avalanche_eStage = AS_DESCENDING;
					m_avalanche_flTime = gpGlobals->curtime;
				}
			}
		}

		// We don't have stuff or no good los
		if ( m_avalanche_eStage != AS_DESCENDING )
		{
			m_avalanche_eStage = AS_NONE;
		}

		break;
	}
}

void CNPC_CombineAdvisorRoaming::Avalanche_Descend()
{
	// Advisor is descending for the avalanche blast
	// wait till we are close enough to the desired position
	Vector dst = GetDesiredPosition();
	Vector pos = GetAbsOrigin();

	if ( ( pos - dst ).Length() > sk_advisor_roaming_av_pos_threshold.GetFloat() )
	{
		if ( ( gpGlobals->curtime - m_avalanche_flTime > sk_advisor_roaming_av_descend_timeout.GetFloat() ) ||
			 ( m_levitate_lstObjects.Count() <= 0 ) )
		{
			// Timed out when descending or lost shield stuff
			Avalanche_Finished();
		}

		return; // not close enough yet
	}

	// We are ready to do the blast
	m_avalanche_eStage = AS_WARNING;
	m_avalanche_flTime = gpGlobals->curtime;
}

void CNPC_CombineAdvisorRoaming::Avalanche_Warning()
{
	EmitSound( "NPC_CombineGunship.Explode" );

	if ( gpGlobals->curtime < m_avalanche_flTime + sk_advisor_roaming_av_warn_time.GetFloat() )
	{
		// Warn less often
		SetContextThink( &CNPC_CombineAdvisorRoaming::Avalanche_Update,
			gpGlobals->curtime + sk_advisor_roaming_av_warn_freq.GetFloat(),
			s_szThinkAvalanche );
		return;
	}

	m_avalanche_eStage = AS_BLAST;
	m_avalanche_flTime = gpGlobals->curtime;
}

void CNPC_CombineAdvisorRoaming::Avalanche_DoBlast()
{
	EmitSound( "NPC_CombineGunship.Explode" );

	m_avalanche_front = 0.0f;
	m_avalanche_flTime = gpGlobals->curtime;

	m_avalanche_eStage = AS_SHOCKWAVE;
}

void CNPC_CombineAdvisorRoaming::Avalanche_ShockWave()
{
	// This function can be called multiple times
	// during the same frame, simulation done once only
	if ( gpGlobals->curtime <= m_avalanche_flTime )
		return;

	// Simulate the current shockwave edge
	m_avalanche_front += ( gpGlobals->curtime - m_avalanche_flTime ) * sk_advisor_roaming_av_vel.GetFloat();
	m_avalanche_flTime = gpGlobals->curtime;

	// Check avalanche front vs player
	Vector tgt = Throw_GetTarget();
	Vector org = GetAbsOrigin();
	float flTgtDist = ( org.AsVector2D() - tgt.AsVector2D() ).Length();

	if ( m_avalanche_front > flTgtDist * 1.2 )
	{
		Avalanche_Finished();
	}
}

void CNPC_CombineAdvisorRoaming::Avalanche_Finished()
{
	Control_AttackCompleted();
	Control_ReturnToPath();
}


//////////////////////////////////////////////////////////////////////////
//
//	Pull implementation
//
//////////////////////////////////////////////////////////////////////////

void CNPC_CombineAdvisorRoaming::Pull_Update()
{
	SetContextThink( &CNPC_CombineAdvisorRoaming::Pull_Update,
		gpGlobals->curtime + 0.1,
		s_szThinkPull );

	CBaseEntity *pEnemy = GetEnemy();
	if ( !pEnemy && m_pull_eStage > PULL_PREPARING )
	{
		Pull_Drop();
		Control_AttackCompleted();
		Control_ReturnToPath();
	}

	switch( m_pull_eStage )
	{
	case PULL_NONE:
		if ( Pull_CheckCondition() )
		{
			m_pull_eStage = PULL_PREPARING;
		}
		else
		{
			SetContextThink( &CNPC_CombineAdvisorRoaming::Pull_Update,
				gpGlobals->curtime + random->RandomFloat( 1.0, 2.0 ),	// keep checking every second or so
				s_szThinkPull );
		}
		break;

	case PULL_PREPARING:
		Pull_Prepare();
		break;

	case PULL_PULLING:
		Pull_Pulling();
		break;

	case PULL_SUCK:
		Pull_Suck();
		break;

	case PULL_DROP:
		Pull_Drop();
		break;
	}
}

void CNPC_CombineAdvisorRoaming::Pull_Prepare()
{
	if ( Control_GetOtherAttackInProgress( ATK_PULL ) )
		return;
	
	CBaseEntity *pEnemy = GetEnemy();
	CBaseCombatCharacter *pBCC = dynamic_cast<CBaseCombatCharacter *>( pEnemy );

	// Re-check the condition
	if ( pBCC && Pull_CheckCondition() )
	{
		// Can start pulling
		m_pull_eStage = PULL_PULLING;

		m_pull_pHook = pEnemy->VPhysicsGetObject();
		// 		m_pull_pHook = PhysModelCreateBox( this,
		// 			VEC_HULL_MIN, VEC_HULL_MAX, pEnemy->GetAbsOrigin(),
		// 			false );
		// 		m_pull_pHook->EnableCollisions( false );

		// Create player constraint
		// 		IPhysicsObject *pObjEnemy = pEnemy->VPhysicsGetObject();
		// 		
		// 		constraint_fixedparams_t def;
		// 		def.Defaults();
		// 		def.InitWithCurrentObjectState( m_pull_pHook, pObjEnemy );
		// 		def.constraint.Defaults();
		// 		
		// 		m_pull_pConstraint = physenv->CreateFixedConstraint( m_pull_pHook, pObjEnemy, NULL, def );

		// Create the controller
		m_pull_pController = physenv->CreateMotionController( &m_pull_fnCallback );
		m_pull_pController->AttachObject( m_pull_pHook, false );

		// Immobilize the player
		pBCC->HandleInteraction( g_interactionAdvisorImmobilize, this, this );

		// Set the highlight
		Client_HighlightPullOn( pEnemy );

		// Do the pull effect
		UTIL_ScreenShake( pEnemy->EyePosition(), // pEnemy->GetAbsOrigin()
			sk_advisor_roaming_pull_shake_amp.GetFloat(),
			sk_advisor_roaming_pull_shake_freq.GetFloat(),
			sk_advisor_roaming_pull_shake_dur.GetFloat(),
			sk_advisor_roaming_pull_shake_radius.GetFloat(),
			SHAKE_START, true );

		// Drop the shield
		float flShieldTimeout = sk_advisor_roaming_pull_timeout.GetFloat();
		m_pull_flTime = gpGlobals->curtime + flShieldTimeout;
		Levitate_Stop( flShieldTimeout + 2.0f );

		EmitSound( "NPC_CombineGunship.Explode" );
	}
	else
	{
		// Abort the pulling attempt
		m_pull_eStage = PULL_NONE;
	}
}

void CNPC_CombineAdvisorRoaming::Pull_Pulling()
{
	{
		static float flSpewMsg = 0;
		if ( gpGlobals->curtime > flSpewMsg )
		{
			flSpewMsg = gpGlobals->curtime + 0.5f;
			Msg( "***** PULLING...\n" );
		}
	}

	CBaseEntity *pEnemy = GetEnemy();

	if ( pEnemy )
		m_pull_pHook = pEnemy->VPhysicsGetObject();

	if ( !pEnemy || !m_pull_pHook || gpGlobals->curtime > m_pull_flTime )
	{
		Pull_Drop();
		return;
	}

	// Simulate the tongue
	if ( !GetBodygroup( m_bodyTongue ) )
	{
		m_pull_flTongue = sk_advisor_roaming_pull_tongue_start.GetFloat();
		m_pull_flTongueTime = gpGlobals->curtime;
		Pull_SetPoseParam();
		SetBodygroup( m_bodyTongue, 1 );
	}

	Pull_SimulateTongue();

	// Now we have to keep pulling the player towards us
	Vector posEnemy = pEnemy->GetAbsOrigin();
	Vector posSuckTgt;
	GetAttachment( m_attachSuckTgt, posSuckTgt );
	Vector toAdvisor = ( posSuckTgt - posEnemy );
	float flDistToAdvisor = toAdvisor.NormalizeInPlace();
	if ( posEnemy.z > sk_advisor_roaming_pull_height.GetFloat() )
	{
		Pull_Drop();
	}
	else if ( flDistToAdvisor < sk_advisor_roaming_pull_suck_dist.GetFloat() )
	{
		// It's time to suck!
		m_pull_pHook->SetVelocity( &vec3_origin, &vec3_origin );
		m_pull_eStage = PULL_SUCK;
	}
	else
	{
		// Keep pulling
		toAdvisor *= sk_advisor_roaming_pull_vel.GetFloat();
		m_pull_pHook->SetVelocity( &toAdvisor, &vec3_origin );
	}
}

void CNPC_CombineAdvisorRoaming::Pull_Suck()
{
	CBaseEntity *pEnemy = GetEnemy();
	if ( pEnemy )
		m_pull_pHook = pEnemy->VPhysicsGetObject();

	if ( !pEnemy || !m_pull_pHook )
	{
		Pull_Drop();
		return;
	}

	{
		static float flSpewMsg = 0;
		if ( gpGlobals->curtime > flSpewMsg )
		{
			flSpewMsg = gpGlobals->curtime + 0.5f;
			Msg( "***** SUCKING...\n" );
		}
	}

	m_pull_pHook->SetVelocity( &vec3_origin, &vec3_origin );

	Pull_SimulateTongue();

	Msg( "***** TONGUE... (%f / %f)\n", m_pull_flTongue, sk_advisor_roaming_pull_tongue_len.GetFloat() );
	if ( m_pull_flTongue < sk_advisor_roaming_pull_tongue_len.GetFloat() )
		return;

	float flDmg = pEnemy->GetHealth() / 2.0f;
	CTakeDamageInfo dmg;
	dmg.Set( this, this, flDmg,
		DMG_SHOCK | DMG_PREVENT_PHYSICS_FORCE | DMG_NEVERGIB );

	pEnemy->TakeDamage( dmg );

	if ( CBasePlayer *pPlayer = ToBasePlayer( pEnemy ) )
	{
		Vector vecForward;
		pPlayer->EyeVectors( &vecForward );
		vecForward.Negate();
		
		SpawnBlood( pPlayer->EyePosition(), vecForward, pPlayer->BloodColor(), flDmg );
		// UTIL_ScreenFade();
	}

	m_pull_eStage = PULL_DROP;
}

void CNPC_CombineAdvisorRoaming::Pull_SimulateTongue()
{
	if ( !GetBodygroup( m_bodyTongue ) )
		return;

	float flDeltaTime = gpGlobals->curtime - m_pull_flTongueTime;
	if ( flDeltaTime <= 0 )
		return;

	float flLength = sk_advisor_roaming_pull_tongue_speed.GetFloat() * flDeltaTime;
	m_pull_flTongue += flLength;

	if ( m_pull_eStage != PULL_SUCK )
	{
		float flDrool = sk_advisor_roaming_pull_tongue_drool.GetFloat();
		if ( m_pull_flTongue > flDrool )
			m_pull_flTongue = -flDrool + 0.01;
	}
	else if ( m_pull_flTongue < 0.0f )
	{
		m_pull_flTongue = - m_pull_flTongue;
	}

	Pull_SetPoseParam();
}

void CNPC_CombineAdvisorRoaming::Pull_SetPoseParam()
{
	float flMinTongue = sk_advisor_roaming_pull_tongue_start.GetFloat();
	if ( fabs( m_pull_flTongue ) < flMinTongue )
		m_pull_flTongue = flMinTongue;

	float flPoseParam = 100.0f * fabs( m_pull_flTongue ) / sk_advisor_roaming_pull_tongue_len.GetFloat();
	if ( flPoseParam > 100.0f )
		flPoseParam = 100.0f;

	SetPoseParameter( m_poseTongue_Length, flPoseParam );
}

void CNPC_CombineAdvisorRoaming::Pull_Drop()
{
	Msg( "***** DROPPING...\n" );

	SetPoseParameter( m_poseTongue_Length, 0.0f );
	SetBodygroup( m_bodyTongue, 0 );

	Control_AttackCompleted();
	Control_ReturnToPath();

	Pull_DelayUpdate( true );
	Spit_DelayAttack();
}

void CNPC_CombineAdvisorRoaming::Pull_DelayUpdate( bool bSuccessfulPull /* = false */ )
{
	float flDelay = bSuccessfulPull ?
		sk_advisor_roaming_pull_delay_succ.GetFloat() :
		sk_advisor_roaming_pull_delay_fail.GetFloat();
	SetContextThink( &CNPC_CombineAdvisorRoaming::Pull_Update,
		gpGlobals->curtime + flDelay,
		s_szThinkPull );
}

bool CNPC_CombineAdvisorRoaming::Pull_CheckCondition()
{
	CBaseEntity *pEnemy = GetEnemy();
	if ( !pEnemy )
		return false;

	Vector posAdvisor = GetAbsOrigin();
	if ( posAdvisor.z > sk_advisor_roaming_pull_height.GetFloat() )
		return false;	// advisor is too high in the air

	Vector posEnemy = pEnemy->GetAbsOrigin();
	Vector toEnemy = ( posEnemy - posAdvisor );

	float flDist2D = toEnemy.Length2D();
	return ( flDist2D <= sk_advisor_roaming_pull_dist.GetFloat() );
}

void CNPC_CombineAdvisorRoaming::Pull_Clear()
{
	physenv->DestroyMotionController( m_pull_pController );
	m_pull_pController = NULL;

	physenv->DestroyConstraint( m_pull_pConstraint );
	m_pull_pConstraint = NULL;

	// physenv->DestroyObject( m_pull_pHook );
	m_pull_pHook = NULL;

	m_pull_eStage = PULL_NONE;

	if ( CBaseCombatCharacter *pBCC = dynamic_cast<CBaseCombatCharacter *>( GetEnemy() ) )
	{
		pBCC->HandleInteraction( g_interactionAdvisorRelease, this, this );
	}

	Client_HighlightPullOff();
}

bool AdvisorRoaming_IsPullingPlayer( CBaseEntity *pPlayer )
{
	for ( CBaseEntity *pEnt = NULL;
		  ( pEnt = gEntList.FindEntityByClassname( pEnt, "npc_combine_advisor_roaming" ) ) != NULL;
		  )
	{
		if ( AdvisorRoaming_IsPullingPlayer( pPlayer, pEnt ) )
			return true;
	}
	return false;
}

bool AdvisorRoaming_IsPullingPlayer( CBaseEntity *pPlayer, CBaseEntity *pAdvisor )
{
	if ( CNPC_CombineAdvisorRoaming *pNpcAdvisor = dynamic_cast< CNPC_CombineAdvisorRoaming * >( pAdvisor ) )
	{
		return
			( pNpcAdvisor->GetEnemy() == pPlayer ) &&
			( pNpcAdvisor->m_pull_eStage > CNPC_CombineAdvisorRoaming::PULL_PREPARING );
	}
	return false;
}

bool AdvisorRoaming_RemapPlayerPhysDmg( CBaseEntity *pPlayer, CTakeDamageInfo const &info, CTakeDamageInfo &infoRemapped )
{
	if ( g_debug_advisor_roaming.GetInt() > 0 )
	{
		Msg( "==== Player [in] phys dmg (%f) = %f : %d\n",
			gpGlobals->curtime, info.GetDamage(), info.GetDamageType() );
	}

	// Tracking to clamp the damage done to player
	static float s_lastdmg_time = 0.0f;
	static float s_lastdmg_dmg = 0.0f;
	static const float s_lastdmg_keep = 0.2f;

	// Determine damage information
	if ( ( info.GetDamageType() & DMG_CRUSH ) == 0 )
		return false;

	CBaseEntity *pAttacker = info.GetAttacker();
	if ( !pAttacker )
		return false;
	
	IPhysicsObject *pPhysObj = pAttacker->VPhysicsGetObject();
	if ( !pPhysObj )
		return false;

	bool bAdvisorSubsequentDmg =
		( gpGlobals->curtime >= s_lastdmg_time &&
		  gpGlobals->curtime < s_lastdmg_time + s_lastdmg_keep );

	CBaseEntity *pOwner = pAttacker->GetOwnerEntity();
	bool bAdvisorInitialDmg = pOwner && FClassnameIs( pOwner, "npc_combine_advisor_roaming" );

	if ( bAdvisorInitialDmg || bAdvisorSubsequentDmg )
	{
		float flRangeMass[2] = { sk_advisor_roaming_player_remap_mass_min.GetFloat(), sk_advisor_roaming_player_remap_mass_max.GetFloat() };
		float flRangeDmg[2] = { sk_advisor_roaming_player_remap_dmg_min.GetFloat(), sk_advisor_roaming_player_remap_dmg_max.GetFloat() };

		float flMass = pPhysObj->GetMass();
		flMass = MAX( flMass, flRangeMass[1] );

		float flDmg = ( flMass - flRangeMass[0] )/( flRangeMass[1] - flRangeMass[0] );
		flDmg = ( 1.0f - flDmg ) * flRangeDmg[0] + flDmg * flRangeDmg[1];

		// Also when getting hit from behind, half dmg and don't allow to be killed
		Vector vel, ang;
		pPhysObj->GetVelocity( &vel, &ang );

		Vector fwd;
		ToBasePlayer( pPlayer )->EyeVectors( &fwd );

		fwd.z = 0;
		vel.z = 0;

		if ( fwd.Dot( vel ) > 0 )	// object coming from the back
		{
			flDmg /= 2.0f;

			float flPlayerHealth = pPlayer->GetHealth();
			if ( flDmg > flPlayerHealth &&
				flPlayerHealth > 3.0f )
			{
				flDmg = flPlayerHealth;
				flDmg -= RandomFloat( 1.0f, 3.0f );
			}
		}

		// Check if player would have received less dmg
		if ( info.GetDamage() < flDmg )
		{
			flDmg = info.GetDamage();
		}

		// Check if this is new damage or continuation of old one
		if ( gpGlobals->curtime >= s_lastdmg_time &&
			 gpGlobals->curtime < s_lastdmg_time + s_lastdmg_keep )
		{
			// Receiving damage from the same advisor hit
			float flCanDoDmg = flRangeDmg[1] - s_lastdmg_dmg;

			if ( flDmg > flCanDoDmg )
				flDmg = flCanDoDmg;

			s_lastdmg_dmg += flDmg;
		}
		else
		{
			// Dealing new damage from advisor hit
			s_lastdmg_time = gpGlobals->curtime;
			s_lastdmg_dmg = flDmg;
		}

		if ( g_debug_advisor_roaming.GetInt() > 0 )
		{
			Msg( "==== Player [remap] phys dmg (%f) = %f\n", gpGlobals->curtime, flDmg );
		}

		if ( info.GetDamage() > flDmg )
		{
			// Clamp the damage
			infoRemapped = info;
			infoRemapped.SetDamage( flDmg );

			return true;
		}
	}

	return false;
}




//////////////////////////////////////////////////////////////////////////
//
//	NPC Schedule table
//
//////////////////////////////////////////////////////////////////////////

AI_BEGIN_CUSTOM_NPC( npc_combine_advisor_roaming, CNPC_CombineAdvisorRoaming )

	DECLARE_INTERACTION( g_interactionAdvisorImmobilize );
	DECLARE_INTERACTION( g_interactionAdvisorRelease );

//	DECLARE_TASK(  )

	DECLARE_ACTIVITY( ACT_ADVISOR_ROAMING_PATROL );
	DECLARE_ACTIVITY( ACT_ADVISOR_ROAMING_HOVER );
	DECLARE_ACTIVITY( ACT_ADVISOR_ROAMING_CRASH );

	//DECLARE_CONDITION( COND_ )

	//=========================================================
//	DEFINE_SCHEDULE
//	(
//		SCHED_DUMMY,
//
//		"	Tasks"
//		"		TASK_FACE_ENEMY			0"
//		"	"
//		"	Interrupts"
//	)


AI_END_CUSTOM_NPC()


