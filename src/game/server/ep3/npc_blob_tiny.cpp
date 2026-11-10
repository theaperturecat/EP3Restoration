//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "npc_blob_tiny.h"
#include "physics_saverestore.h"
#include "vphysics/constraints.h"
#include "physics_collisionevent.h"
#include "ai_senses.h"
#include "physics_shared.h"
#include "ai_memory.h"
#include "hl2_shareddefs.h"
#include "saverestore_utlvector.h"
#include "particle_parse.h"
#include "props.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

						  
//-----------------------------------------------------------------------------
// CSimpleForceController implementation
//-----------------------------------------------------------------------------
BEGIN_SIMPLE_DATADESC( CSimpleForceController )
	DEFINE_FIELD( m_vecAngular, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecLinear, FIELD_VECTOR ),
	DEFINE_FIELD( m_fIsStopped, FIELD_BOOLEAN ),
END_DATADESC()

IMotionEvent::simresult_e CSimpleForceController::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
	if ( m_fIsStopped )
		return SIM_NOTHING;

	linear = m_vecLinear;
	angular = m_vecAngular;
	return IMotionEvent::SIM_GLOBAL_ACCELERATION;
}


//-----------------------------------------------------------------------------

static ConVar	sk_dmg_blob_tiny( "sk_dmg_blob_tiny","1" );
static ConVar	sk_blob_tiny_stun_delay( "sk_blob_tiny_stun_delay", "1");

static ConVar bt_no_sizzle( "bt_no_sizzle", "0" );
static ConVar bt_jump_angle( "bt_jump_angle", "60" );
static ConVar bt_jump_dist( "bt_jump_dist", "64" );
static ConVar bt_jump_delay( "bt_jump_delay", "0.6" );
static ConVar bt_jump_delay_fast( "bt_jump_delay_fast", "0.2" );
static ConVar bt_jump_raise( "bt_jump_raise", "1.0" );
static ConVar bt_jump_raise_crawl( "bt_jump_raise_crawl", "0.2" );
static ConVar bt_jump_impulse( "bt_jump_impulse", "750" );
static ConVar bt_jump_response_impulse( "bt_jump_response_impulse", "100" );
static ConVar bt_jump_version( "bt_jump_version", "0" );
static ConVar bt_jump_ceiling_chance( "bt_jump_ceiling_chance", "0.1" );

#define BLOB_TINY_MAX_TORQUE_FACTOR	5

#define BLOB_TINY_MASS			30.0f

#define BLOB_TINY_IDLE_SEE_DIST					2048
#define BLOB_TINY_NORMAL_SEE_DIST					2048
#define BLOB_TINY_WAKEUP_DIST						256
#define BLOB_TINY_SEE_VEHICLESONLY_BEYOND_IDLE		300		// See every other than vehicles upto this distance (i.e. old idle see dist)
#define BLOB_TINY_SEE_VEHICLESONLY_BEYOND_NORMAL	800		// See every other than vehicles upto this distance (i.e. old normal see dist)

#define BLOB_TINY_RETURN_TO_PLAYER_DIST			(200*200)

#define BLOB_TINY_MIN_ATTACK_DIST	1
#define BLOB_TINY_MAX_ATTACK_DIST	4096


#define BLOB_TINY_HOP_DELAY				2			// Don't allow hops faster than this

#define BLOB_TINY_FEAR_DISTANCE			(300*300)

#define BLOB_TINY_EXTRA_GRAVITY		3500.0f

//=========================================================
// Custom schedules
//=========================================================
enum
{
	SCHED_BLOB_TINY_RANGE_ATTACK1 = LAST_SHARED_SCHEDULE,
	SCHED_BLOB_TINY_CHASE_ENEMY,
	SCHED_BLOB_TINY_FLEE,
	SCHED_BLOB_TINY_ALERT_STAND,
	SCHED_BLOB_TINY_HOP_AWAY_FROM_DANGER_SOUND,
	SCHED_BLOB_TINY_SPAWN_TINY_BLOBS,
};

//=========================================================
// Custom tasks
//=========================================================
enum 
{
	TASK_BLOB_TINY_CHARGE_ENEMY = LAST_SHARED_TASK,
	TASK_BLOB_TINY_GET_PATH_TO_FLEE,
	TASK_BLOB_TINY_HOP_AWAY_FROM_DANGER_SOUND,
	TASK_BLOB_TINY_WAIT_TO_LAND,
	TASK_BLOB_TINY_SPAWN_TINY_BLOBS,
};

//=========================================================
// Conditions
//=========================================================
enum
{
	COND_BLOB_TINY_CAN_SPLIT = LAST_SHARED_CONDITION,
};


LINK_ENTITY_TO_CLASS( npc_blob_tiny, CNPC_BlobTiny );


//-----------------------------------------------------------------------------
// save/load
//-----------------------------------------------------------------------------
BEGIN_DATADESC( CNPC_BlobTiny )

	DEFINE_EMBEDDED( m_BlobController ),
	DEFINE_PHYSPTR( m_pMotionController ),

	DEFINE_FIELD( m_flActiveTime, FIELD_TIME ),
	DEFINE_FIELD( m_flGoIdleTime, FIELD_TIME ),
	DEFINE_FIELD( m_bHeld, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flNextHopTime, FIELD_TIME ),
	DEFINE_FIELD( m_flNextSplitTime, FIELD_TIME ),

	DEFINE_FIELD( m_hHitPlayer, FIELD_EHANDLE ),
	DEFINE_KEYFIELD( m_flRadius, FIELD_FLOAT, "radius" ),
	DEFINE_FIELD( m_bInFlight, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_vecImpactNormal, FIELD_VECTOR ),
	DEFINE_FIELD( m_vecImpactPosition, FIELD_POSITION_VECTOR ),
	DEFINE_UTLVECTOR( m_Constraints, FIELD_VECTOR ),
	DEFINE_KEYFIELD( m_bIsSplitter, FIELD_BOOLEAN, "splits" ),
	DEFINE_KEYFIELD( m_nMaxSplitCount, FIELD_INTEGER, "maxSplitCount" ),
	DEFINE_KEYFIELD( m_bIsFastBlob, FIELD_BOOLEAN, "fastJumper" ),
	DEFINE_FIELD( m_nSplitCount, FIELD_INTEGER ),
	DEFINE_FIELD( m_nTaskSplitCount, FIELD_INTEGER ),
	DEFINE_FIELD( m_flSplitOnceIgnoreTime, FIELD_TIME ),

	DEFINE_PHYSPTR( m_pConstraint ),

	// Function Pointers
	DEFINE_ENTITYFUNC( AcidSplashTouch ),

	DEFINE_INPUTFUNC( FIELD_FLOAT, "SplitOnce", InputSplitOnce ),

	DEFINE_OUTPUT( m_OnPhysGunDrop, "OnPhysGunDrop" ),
	DEFINE_OUTPUT( m_OnPhysGunPickup, "OnPhysGunPickup" ),

END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CNPC_BlobTiny, DT_BlobTiny )
	SendPropBool( SENDINFO( m_bInFlight ) ),
	SendPropBool( SENDINFO( m_bIsFastBlob ) ),
	SendPropBool( SENDINFO( m_bHeld ) ),
	SendPropBool( SENDINFO( m_bIsSplitter ) ),
	SendPropFloat( SENDINFO( m_flRadius ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_vecBlobVelocity ), -1, SPROP_NOSCALE|SPROP_CHANGES_OFTEN ), 
END_SEND_TABLE()


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static ConVar sk_blob_tiny_health( "sk_blob_tiny_health", "1" );
CNPC_BlobTiny::CNPC_BlobTiny( void )
{
	m_flRadius			= 7.0f;
	m_flFieldOfView		= -1.0f;
	m_bloodColor		= BLOOD_COLOR_BLOB;
	m_iHealth			= sk_blob_tiny_health.GetInt();
	m_iMaxHealth		= m_iHealth;
	m_bIsSplitter		= false;
	m_nMaxSplitCount	= 1;
	m_nSplitCount		= 0;
	m_nTaskSplitCount	= 0;
}

CNPC_BlobTiny::~CNPC_BlobTiny( void )
{
	if ( m_pMotionController != NULL )
	{
		physenv->DestroyMotionController( m_pMotionController );
		m_pMotionController = NULL;
	}
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::Precache( void )
{
	BaseClass::Precache();
	PrecacheModel( "models/roller.mdl" );
	PrecacheMaterial( "models/blob/blob_material_tiny" );
	PrecacheMaterial( "models/blob/blob_material_tiny_fast" );
	PrecacheMaterial( "models/blob/blob_material_tiny_splitter" );
	PrecacheEffect( "WaterSurfaceExplosion" );
	PrecacheScriptSound( "NPC_Antlion_Grub.Squish" );
	PrecacheScriptSound( "NPC_BlobTiny.Held" );
	PrecacheScriptSound( "NPC_BlobTiny.Fear" );
	PrecacheScriptSound( "NPC_BlobTiny.Hate" );
	PrecacheParticleSystem( "acid_sizzle" );
	PrecacheParticleSystem( "blob_impact" );
	PrecacheParticleSystem( "blob_tiny_death" );	
	PrecacheParticleSystem( "antlion_gib_01_juice" );	
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::Spawn( void )
{
	Precache();

	SetModel( "models/roller.mdl" );

	SetSolid( SOLID_VPHYSICS );
	AddSolidFlags( FSOLID_FORCE_WORLD_ALIGNED | FSOLID_NOT_STANDABLE );

	BaseClass::Spawn();

	SetTouch( &CNPC_BlobTiny::AcidSplashTouch );

	SetImpactEnergyScale( 0.0f );
	CapabilitiesClear();
	CapabilitiesAdd( bits_CAP_MOVE_GROUND | bits_CAP_MOVE_CRAWL | bits_CAP_MOVE_JUMP | 
		bits_CAP_SKIP_NAV_GROUND_CHECK | bits_CAP_SQUAD );
	SetCollisionGroup( HL2COLLISION_GROUP_BLOB_TINY );

	m_vecImpactPosition	= GetAbsOrigin();
	m_vecBlobVelocity	= GetAbsVelocity();

	m_hHitPlayer		= NULL;
	m_bIsFleeing		= false;
	m_flNextHopTime		= gpGlobals->curtime;
	m_flNextSplitTime	= gpGlobals->curtime; 
	m_flSplitOnceIgnoreTime = gpGlobals->curtime;

	SetHullType( HULL_TINY_FLUID );

	SetHullSizeNormal();

	m_flActiveTime		= gpGlobals->curtime;

	NPCInit();

	m_bInFlight = ( GetFlags() & FL_ONGROUND ) == 0;
	m_takedamage = DAMAGE_YES;
	SetDistLook( BLOB_TINY_IDLE_SEE_DIST );

	//Suppress superfluous warnings from animation system
	m_flGroundSpeed = 20;
	m_NPCState		= NPC_STATE_NONE;

	m_pConstraint = NULL;

	//Set their yaw speed to 0 so the motor doesn't rotate them.
	GetMotor()->SetYawSpeed( 0.0f );
}

void CNPC_BlobTiny::SetSplitterBlob( bool bIsSplitter, int nMaxSplitCount ) 
{ 
	if ( nMaxSplitCount > 0 )
	{
		m_nMaxSplitCount = nMaxSplitCount;
	}
	m_bIsSplitter = bIsSplitter;
	if ( m_nMaxSplitCount <= m_nSplitCount )
	{
		m_bIsSplitter = false;
	}
}

void CNPC_BlobTiny::InputSplitOnce( inputdata_t &inputdata )
{ 
	if ( ( gpGlobals->curtime >= m_flSplitOnceIgnoreTime ) && !m_bIsSplitter )
	{
		float flDelay = inputdata.value.Float();
		m_nSplitCount = 0;
		SetSplitterBlob( true, 1 );
		m_flNextSplitTime = gpGlobals->curtime + flDelay;
		m_flSplitOnceIgnoreTime = gpGlobals->curtime + 5.0f;
	}
}

void CNPC_BlobTiny::DrawDebugGeometryOverlays()
{
	if (m_debugOverlays & OVERLAY_BBOX_BIT) 
	{
		float dist = GetSenses()->GetDistLook();
		Vector range(dist, dist, 64);
		NDebugOverlay::Box( GetAbsOrigin(), -range, range, 255, 0, 0, 0, 0 );
	}
	BaseClass::DrawDebugGeometryOverlays();
}

// UNDONE: Put this in the qc file!
Vector CNPC_BlobTiny::EyePosition() 
{
	// This takes advantage of the fact that the system knows
	// that the abs origin is at the center of the rollermine
	// and that the OBB is actually world-aligned despite the
	// fact that SOLID_VPHYSICS is being used
	Vector eye = CollisionProp()->GetCollisionOrigin();
	eye.z += CollisionProp()->OBBMaxs().z;
	return eye;
}

Class_T	CNPC_BlobTiny::Classify() 
{
	return CLASS_BLOB;
}

unsigned int CNPC_BlobTiny::PhysicsSolidMaskForEntity( void ) const
{
	return MASK_NPCFLUID;
}


bool CNPC_BlobTiny::ShouldGoToIdleState() 
{ 
	return gpGlobals->curtime > m_flGoIdleTime ? true : false;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::OnStateChange( NPC_STATE OldState, NPC_STATE NewState )
{
	if ( NewState == NPC_STATE_IDLE )
	{
		SetDistLook( BLOB_TINY_IDLE_SEE_DIST );
	}
	else
	{
		SetDistLook( BLOB_TINY_NORMAL_SEE_DIST );
		m_flNextSplitTime = gpGlobals->curtime + RandomFloat( 2.0f, 3.0f );
	}

	if ( NewState == NPC_STATE_COMBAT )
	{
		EmitSound( "NPC_BlobTiny.Hate" );
	}

	BaseClass::OnStateChange( OldState, NewState );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
NPC_STATE CNPC_BlobTiny::SelectIdealState( void )
{
	switch ( m_NPCState )
	{
		case NPC_STATE_COMBAT:
		{
			if ( HasCondition( COND_ENEMY_TOO_FAR ) )
			{
				ClearEnemyMemory();
				SetEnemy( NULL );
				m_flGoIdleTime = gpGlobals->curtime + 10;
				return NPC_STATE_ALERT;
			}
		}
		break;
	}

	return BaseClass::SelectIdealState();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::OnRestore()
{
	BaseClass::OnRestore();
	if ( m_pMotionController )
	{
		m_pMotionController->SetEventHandler( &m_BlobController );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CNPC_BlobTiny::CreateVPhysics()
{
	RemoveSolidFlags( FSOLID_NOT_SOLID );

	objectparams_t params = g_PhysDefaultObjectParams;
	params.pGameData = static_cast<void *>(this);

	int nMaterialIndex = physprops->GetSurfaceIndex( "acid" );

	Vector vPos = GetAbsOrigin();
	IPhysicsObject* p = physenv->CreateSphereObject( m_flRadius, nMaterialIndex, vPos, GetAbsAngles(), &params, false );
	Assert(p != NULL);

	p->SetVelocity( &m_vecBlobVelocity.Get(), NULL );
	PhysSetGameFlags( p, FVPHYSICS_NO_SELF_COLLISIONS ); // call collisionruleschanged if this changes dynamically

	p->SetMass( BLOB_TINY_MASS );
	p->EnableGravity( true );

	float flDrag = 5000.0f;
	p->EnableDrag( true );
	p->SetDragCoefficient( &flDrag, NULL );

	VPhysicsSetObject( p );

	// Extra gravity
	m_BlobController.m_vecLinear.Init( 0.0f, 0.0f, -BLOB_TINY_EXTRA_GRAVITY );
	m_pMotionController = physenv->CreateMotionController( &m_BlobController );
	m_pMotionController->AttachObject( p, true );
	m_BlobController.Off();

	SetMoveType( MOVETYPE_VPHYSICS );

	return true;
}	


void CNPC_BlobTiny::VPhysicsUpdate( IPhysicsObject *pPhysics )
{
	BaseClass::VPhysicsUpdate( pPhysics );
	IPhysicsObject *pPhysicsObject = VPhysicsGetObject();
	if ( pPhysicsObject != NULL )
	{
		pPhysicsObject->GetVelocity( &m_vecBlobVelocity.GetForModify(), NULL );
	}
}


void CNPC_BlobTiny::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );
	EmitSound( "NPC_Antlion_Grub.Squish" );
	DispatchParticleEffect( "blob_tiny_death", GetAbsOrigin(), vec3_angle );
	UTIL_Remove( this );
}


//-----------------------------------------------------------------------------
// Split the blob!
//-----------------------------------------------------------------------------
float CNPC_BlobTiny::GetJumpDelay() const
{
	if ( m_bIsFleeing )
		return 0.2f;
	float flDelay = m_bIsFastBlob ? bt_jump_delay_fast.GetFloat() : bt_jump_delay.GetFloat();
	flDelay *= RandomFloat( 0.5f, 2.0f );
	return flDelay;
}


//-----------------------------------------------------------------------------
// Split the blob!
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::Split()
{
	DispatchParticleEffect( "antlion_gib_01_juice", GetAbsOrigin(), vec3_angle );

	CNPC_BlobTiny *pBlobTiny = (CNPC_BlobTiny*)CreateEntityByName( "npc_blob_tiny" );

	pBlobTiny->SetAbsOrigin( GetAbsOrigin() );
	pBlobTiny->SetAbsVelocity( m_vecBlobVelocity );
	pBlobTiny->SetRadius( m_flRadius );
	pBlobTiny->m_bIsFastBlob = m_bIsFastBlob;
	pBlobTiny->m_flSplitOnceIgnoreTime = gpGlobals->curtime + 5.0f;
	pBlobTiny->Spawn();

	// NOTE: This has to be after spawn. We only split when we're on the ground
	pBlobTiny->m_bInFlight = m_bInFlight;
	pBlobTiny->m_vecImpactNormal = m_vecImpactNormal;
	pBlobTiny->m_Constraints.CopyArray( m_Constraints.Base(), m_Constraints.Count() );
	pBlobTiny->m_flNextHopTime = gpGlobals->curtime + GetJumpDelay();
}


//-----------------------------------------------------------------------------
// Add danger sounds
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::RunAI()
{
	if ( !m_bHeld )
	{
		CSoundEnt::InsertSound( ( SOUND_DANGER | SOUND_CONTEXT_REACT_TO_SOURCE | 
			SOUND_CONTEXT_DANGER_APPROACH ) , WorldSpaceCenter(), 120.0f, 0.2f, this,
			SOUNDENT_CHANNEL_REPEATED_DANGER );
	}

	BaseClass::RunAI();
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CNPC_BlobTiny::RangeAttack1Conditions( float flDot, float flDist )
{
	if( HasCondition( COND_SEE_ENEMY ) == false )
		return COND_NONE;

	if( flDist > BLOB_TINY_MAX_ATTACK_DIST )
		return COND_TOO_FAR_TO_ATTACK;
	
	if (flDist < BLOB_TINY_MIN_ATTACK_DIST )
		return COND_TOO_CLOSE_TO_ATTACK;

	return COND_CAN_RANGE_ATTACK1;
}


//-----------------------------------------------------------------------------
// Gathers conditions
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::GatherConditions()
{
	BaseClass::GatherConditions();

	static int conditionsToClear[] = 
	{
		COND_BLOB_TINY_CAN_SPLIT,
	};

	ClearConditions( conditionsToClear, ARRAYSIZE( conditionsToClear ) );

	if ( m_bIsSplitter && !m_bHeld && ( m_NPCState == NPC_STATE_COMBAT ) && 
		( m_nSplitCount < m_nMaxSplitCount ) && ( gpGlobals->curtime >= m_flNextSplitTime ) )
	{
		SetCondition( COND_BLOB_TINY_CAN_SPLIT );
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
int CNPC_BlobTiny::SelectSchedule( void )
{
	// We better not be attached if we're switching schedules...
	Detach();

	//If we're held, don't try and do anything
	if ( m_bHeld || !IsActive() )
		return SCHED_ALERT_STAND;

	// If we can see something we're afraid of, run from it
	if ( HasCondition( COND_SEE_FEAR ) )
		return SCHED_BLOB_TINY_FLEE;

	if ( HasCondition( COND_HEAR_DANGER ) )
	{
		CSound *pBestSound = GetBestSound( SOUND_DANGER );
		if ( pBestSound )
		{
			Assert( pBestSound->SoundContext() & SOUND_CONTEXT_FROM_FIRE );
			return SCHED_BLOB_TINY_HOP_AWAY_FROM_DANGER_SOUND;
		}
	}

	switch( m_NPCState )
	{
	case NPC_STATE_COMBAT:
		if ( !HasCondition( COND_HEAR_DANGER ) )
		{
			if ( HasCondition( COND_BLOB_TINY_CAN_SPLIT ) )
				return SCHED_BLOB_TINY_SPAWN_TINY_BLOBS;
			if ( HasCondition( COND_CAN_RANGE_ATTACK1 ) )
				return SCHED_BLOB_TINY_RANGE_ATTACK1;
			if ( GetEnemy() )
				return SCHED_BLOB_TINY_CHASE_ENEMY;
		}
		break;

	default:
		break;
	}
	// Rollermines never wait to fall to the ground
	ClearCondition( COND_FLOATING_OFF_GROUND );

	int nSched = BaseClass::SelectSchedule();
	return nSched;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNPC_BlobTiny::TranslateSchedule( int scheduleType )
{
	switch( scheduleType )
	{
	case SCHED_BLOB_TINY_RANGE_ATTACK1:
		if( HasCondition(COND_ENEMY_OCCLUDED) )
			return SCHED_BLOB_TINY_CHASE_ENEMY;
		break;
	}

	return scheduleType;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSightEnt - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_BlobTiny::QuerySeeEntity(CBaseEntity *pSightEnt, bool bOnlyHateOrFearIfNPC)
{
	if ( IRelationType( pSightEnt ) == D_FR )
	{
		// Only see feared objects up close
		float flDist = (WorldSpaceCenter() - pSightEnt->WorldSpaceCenter()).LengthSqr();
		if ( flDist > BLOB_TINY_FEAR_DISTANCE )
			return false;
	}

	return BaseClass::QuerySeeEntity(pSightEnt, bOnlyHateOrFearIfNPC);
}


//-----------------------------------------------------------------------------
// Notification that a new schedule is about to run its first task
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::OnStartSchedule( int scheduleType )
{
	BaseClass::OnStartSchedule( scheduleType );
	m_bIsFleeing = ( scheduleType == SCHED_BLOB_TINY_FLEE ) || 
		( scheduleType == SCHED_BLOB_TINY_HOP_AWAY_FROM_DANGER_SOUND );
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::StartTask( const Task_t *pTask )
{
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

	case TASK_STOP_MOVING:
		TaskComplete();
		break;

	case TASK_WAIT_FOR_MOVEMENT:
		break;

	case TASK_BLOB_TINY_WAIT_TO_LAND:
		break;

	case TASK_BLOB_TINY_SPAWN_TINY_BLOBS:
		m_nTaskSplitCount = RandomInt( 1, 2 );
		break;

	case TASK_BLOB_TINY_HOP_AWAY_FROM_DANGER_SOUND:
		break;

	case TASK_BLOB_TINY_CHARGE_ENEMY:
		break;

	case TASK_BLOB_TINY_GET_PATH_TO_FLEE:
		{
			// Find the nearest thing we're afraid of, and move away from it.
			float flNearest = BLOB_TINY_FEAR_DISTANCE;
			EHANDLE hNearestEnemy = NULL;
			AIEnemiesIter_t iter;
			for( AI_EnemyInfo_t *pEMemory = GetEnemies()->GetFirst( &iter ); pEMemory != NULL; pEMemory = GetEnemies()->GetNext( &iter ) )
			{
				CBaseEntity *pEnemy = pEMemory->hEnemy;
				if ( !pEnemy || !pEnemy->IsAlive() )
					continue;
				if ( IRelationType( pEnemy ) != D_FR )
					continue;			

				float flDist = (WorldSpaceCenter() - pEnemy->WorldSpaceCenter()).LengthSqr();
				if ( flDist < flNearest )
				{
					flNearest = flDist;
					hNearestEnemy = pEnemy;
				}
			}

			if ( !hNearestEnemy )
			{
				TaskFail("Couldn't find nearest feared object.");
				break;
			}

			GetMotor()->SetIdealYawToTarget( hNearestEnemy->WorldSpaceCenter() );
			ChainStartTask( TASK_MOVE_AWAY_PATH, pTask->flTaskData );
		}
		break;

	default:
		BaseClass::StartTask( pTask );
		break;
	}
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
#define BLOB_TINY_ATTACK_DIST 80
#define BLOB_TINY_ATTACK_DIST_SQR ( BLOB_TINY_ATTACK_DIST * BLOB_TINY_ATTACK_DIST )

bool CNPC_BlobTiny::ComputeCeilingTarget( Vector *pTarget, bool *pIsCeilingTarget )
{
	if ( GetNavType() == NAV_CRAWL )
		return false;

	if ( m_Constraints.Count() > 1 )
		return false;

	// Look for a straight up target
	Ray_t ray;
	trace_t trace;
	CTraceFilterSimple traceFilter( this, COLLISION_GROUP_NONE );
	if ( m_vecImpactNormal.z > 0.7f )
	{
		if ( RandomFloat( 0.0f, 1.0f ) < bt_jump_ceiling_chance.GetInt() )
		{
			// Check for a valid ceiling target
			Vector vecOrigin = GetAbsOrigin();
			ray.Init( vecOrigin, vecOrigin + 224 * Vector( 0, 0, 1 ) );
			enginetrace->TraceRay( ray, MASK_SOLID, &traceFilter, &trace );
			if ( trace.m_pEnt && trace.m_pEnt->IsWorld() && ( ( trace.surface.flags & SURF_SKY ) == 0 ) )
			{
				*pTarget = trace.endpos;
				*pIsCeilingTarget = true;
				return true;
			}
		}
	}
	return false;
}

void CNPC_BlobTiny::ComputeNearEnemyHopTarget( Vector *pTarget, bool *pIsCeilingTarget, const Vector &vecOriginalGoal, CBaseEntity *pEnemy )
{
	*pTarget = vecOriginalGoal;

	CBasePlayer *pPlayer = static_cast<CBasePlayer*>( pEnemy );
	Vector vecViewDir = pPlayer->EyeDirection2D();
	Vector vecDelta;
	Vector vecOrigin = GetAbsOrigin();
	VectorSubtract( vecOriginalGoal, vecOrigin, vecDelta );
	float flDistance = VectorNormalize( vecDelta );

	if ( flDistance > 80.0f )
	{
		if ( ComputeCeilingTarget( pTarget, pIsCeilingTarget ) )
			return;
	}

	float flDot = DotProduct( vecDelta, vecViewDir );
	if ( flDot > 0.7f )	  
	{					  
		if ( RandomFloat( 0.0f, 1.0f ) < 0.8f )
		{
			// Jump to the side
			Vector vecJumpDir;
			CrossProduct( m_vecImpactNormal, vecDelta, vecJumpDir );
			if ( VectorNormalize( vecJumpDir ) < 1e-3 )
			{
				vecJumpDir = vecDelta;
			}
			else
			{
				if ( DotProduct( vecJumpDir, vecDelta ) > 0.0f )
				{
					vecJumpDir *= -1.0f;
				}
			}
			VectorMA( vecOrigin, bt_jump_dist.GetFloat(), vecJumpDir, *pTarget );
		}
		return;
	}

	if ( flDot > -0.5f )
	{
		if ( RandomFloat( 0.0f, 1.0f ) < 0.7f )
		{
			// Try to jump in a place the player can see
			VectorMA( vecOrigin, bt_jump_dist.GetFloat(), vecViewDir, *pTarget );
		}
		return;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static ConVar bt_new_jump_dir( "bt_new_jump_dir", "1" );
void CNPC_BlobTiny::ConstrainJumpDirection( Vector *pVecJumpDir )
{
	if ( !m_bIsFleeing || bt_new_jump_dir.GetInt() )
	{
		ConstrainJumpDirectionV2( pVecJumpDir );
		return;
	}

	int nLoopCount = 0;
	bool bConstrained = false;
	do 
	{
		bConstrained = false;
		int nCount = m_Constraints.Count();
		for ( int i = 0; i < nCount; ++i )
		{
			float flDot = DotProduct( *pVecJumpDir, m_Constraints[i] );
			if ( flDot < 0.1f )
			{
				VectorMA( *pVecJumpDir, 0.1f - flDot, m_Constraints[i], *pVecJumpDir );
				bConstrained = true;
			}
		}
		++nLoopCount;
	} while ( bConstrained && ( nLoopCount < 5 ) );
	VectorNormalize( *pVecJumpDir );
}

void CNPC_BlobTiny::ConstrainJumpDirectionV2( Vector *pVecJumpDir )
{
	VectorNormalize( *pVecJumpDir );

	int nNegCount = 0;
	int nPosCount = 0;
	float pDotProduct[3];
	int pNegIndices[3];
	int pPosIndices[3];
	int nCount = MIN( 3, m_Constraints.Count() );
	for ( int i = 0; i < nCount; ++i )
	{
		pDotProduct[i] = DotProduct( *pVecJumpDir, m_Constraints[i] );
		if ( pDotProduct[i] < 1e-3 )
		{
			pNegIndices[ nNegCount++ ] = i;
		}
		else
		{
			pPosIndices[ nPosCount++ ] = i;
		}
	}

	switch ( nNegCount )
	{
	case 0:
	default:
		break;

	case 1:
		{
			const Vector &vecConstraint = m_Constraints[ pNegIndices[ 0 ] ];
			float flDot = pDotProduct[ pNegIndices[0] ]; 
			float flOffset = ( nCount == 1 ) ? 0.3f : 1e-3f;
			VectorMA( *pVecJumpDir, flOffset - flDot, vecConstraint, *pVecJumpDir );
			VectorNormalize( *pVecJumpDir );
		}
		break;

	case 2:
		{
			Vector vecEdge = CrossProduct( m_Constraints[ pNegIndices[0] ], m_Constraints[ pNegIndices[1] ] ); 
			VectorNormalize( vecEdge );
			float flFactor = ( DotProduct( vecEdge, *pVecJumpDir ) < 0.0f ) ? -1.0f : 1.0f;
			VectorMultiply( vecEdge, flFactor, *pVecJumpDir );
			if ( ( nCount == 3 ) && ( DotProduct( *pVecJumpDir, m_Constraints[ pPosIndices[0] ] ) < 0.0f ) )
			{
				*pVecJumpDir *= -1.0f;
			}
		}
		break;

	case 3:
		{
			// Choose the edge direction with the greatest positive dot product
			Vector vecEdge[3];
			float flMaxDot = -2.0f;
			int nMaxInd = -1;
			for ( int i = 0; i < 3; ++i )
			{
				vecEdge[i] = CrossProduct( m_Constraints[ pNegIndices[i] ], m_Constraints[ pNegIndices[(i+1)%3] ] ); 
				VectorNormalize( vecEdge[i] );
				float flDot = DotProduct( *pVecJumpDir, vecEdge[i] );
				if ( flDot < 0.0f )
				{
					vecEdge[i] *= -1.0f;
				}
				if ( flDot > flMaxDot )
				{
					flMaxDot = flDot;
					nMaxInd = i;
				}
			}
			*pVecJumpDir = vecEdge[ nMaxInd ];
		}
		break;
	}
}

void CNPC_BlobTiny::ComputeHopTarget( Vector *pTarget, bool *pIsCeilingTarget, const Vector &vecOriginalGoal, CBaseEntity *pEnemy )
{						  
	*pIsCeilingTarget = false;
	Vector vecOrigin = GetAbsOrigin();

	// Logic to make it so we don't always jump right at the player if we're behind him
	if ( pEnemy && pEnemy->IsPlayer() && !m_hHitPlayer.Get() && ( vecOriginalGoal.DistToSqr( vecOrigin ) <= BLOB_TINY_ATTACK_DIST_SQR ) )
	{
		ComputeNearEnemyHopTarget( pTarget, pIsCeilingTarget, vecOriginalGoal, pEnemy );
		return;
	}

	float flAngleFactor = 1.0f;
	float flDistFactor = 1.0f;
	Vector vecGoal = vecOriginalGoal;

	// If the blob was just attached to the player, then hop away from the player
	if ( m_hHitPlayer.Get() )
	{
		flAngleFactor = 0.5f;
		flDistFactor = RandomFloat( 1.5f, 3.0f );

		// Jump away from the impact normal
		VectorMA( vecOrigin, 200.0f, m_vecImpactNormal, vecGoal ); 
	}

	Vector vecUp, vecRight, vecNormDelta;
	VectorSubtract( vecGoal, vecOrigin, vecNormDelta );
	float flDistToGoal = VectorNormalize( vecNormDelta );

	// Compute a point within a cone of the target direction 
	VectorVectors( vecNormDelta, vecRight, vecUp );

	//		float flAngle = RandomFloat( 0.0f, 2.0f * M_PI );
	//		float flSinAngle, flCosAngle;

	Vector vecJumpDir;
	if ( ( GetNavigator()->CurWaypointRequiresPreciseMovement() || ( GetNavType() == NAV_CRAWL ) ) && !m_bIsFleeing )
	{
		vecJumpDir = vecNormDelta;
	}
	else
	{
		Vector2D vecCircle;
		RandomVectorInUnitCircle( &vecCircle );

		// Distribute more to the outside range when far from player
		float r = vecCircle.Length();
		Vector2DNormalize( vecCircle );
		float flPow = RemapValClamped( flDistToGoal, 128.0f, 384.0f, 2.0f, 5.0f );
		r = pow( r, 1.0f / flPow ); 
		vecCircle *= r;

		// Tighten the angle if we're running away
		if ( m_bIsFleeing ) 
		{
			flAngleFactor *= 0.5f;
		}
		vecCircle *= tan( DEG2RAD( bt_jump_angle.GetFloat() * flAngleFactor ) * 0.5f );

		VectorMA( vecNormDelta, vecCircle.x, vecRight, vecJumpDir );
		VectorMA( vecJumpDir, vecCircle.y, vecUp, vecJumpDir );
	}

	// Don't allow it to go back along the impact normal
	ConstrainJumpDirection( &vecJumpDir );

	if ( m_bIsFleeing ) 
	{
		flDistFactor *= 1.5f;
	}
	VectorMA( vecOrigin, flDistFactor * bt_jump_dist.GetFloat(), vecJumpDir, *pTarget );

	if ( m_hHitPlayer.Get() )
		return;

	if ( ComputeCeilingTarget( pTarget, pIsCeilingTarget ) )
		return;
}

void CNPC_BlobTiny::ComputeHopAwayTarget( Vector *pTarget, bool *pIsCeilingTarget, const Vector &vecFleeTarget, const Vector &vecFleeVelocity )
{
	// This is the same as the hop toward target, just make a fake target
	// away from the thing to flee from
	
	// We're going to compute two flee positions: One which is the current flee location,
	// one which is based on the point of closest approach of the thing to free and the blob
	// We'll lerp between these two based on how long it will take the thing to
	// reach the point of closest approach.
	
	Vector vecOrigin = GetAbsOrigin();
	Vector vecDelta;
	VectorSubtract( vecFleeTarget, vecOrigin, vecDelta );
	VectorNormalize( vecDelta );

	Vector vecFleeDir;
	VectorMultiply( vecDelta, -1.0f, vecFleeDir );

	/*
	// If the flee target is moving away, just use the initial position as a flee target
	if ( DotProduct( vecDelta, vecFleeVelocity ) < 0.0f )
	{
		// Compute point of closest approach
		Vector vecClosestApproach;
		CalcClosestPointOnLine( vecOrigin, vecFleeTarget, vecFleeVelocity, vecClosestApproach );
		float flDist = vecClosestApproach.DistTo( vecFleeTarget );
		float flSpeed = vecFleeVelocity.Length();
		float t = flDist / ( flSpeed + 1e-3f );
		
		Vector vecFleeDir2;
		VectorSubtract( vecOrigin, vecClosestApproach, vecFleeDir2 );
		VectorNormalize( vecFleeDir2 );
		t = clamp( t, 0.0f, 1.0f );
		vecFleeDir = VectorLerp( vecFleeDir2, vecFleeDir, t );
	}
	*/

	Vector vecGoal;
	VectorMA( vecOrigin, 300.0f, vecFleeDir, vecGoal );
	ComputeHopTarget( pTarget, pIsCeilingTarget, vecGoal, NULL );
}

void CNPC_BlobTiny::Hop( const Vector &vecHopTarget, bool bIsCeilingTarget, const Vector *pPreciseMovementPos )
{
	// Convert hop target into a linear force
	Vector vecImpulse;
	VectorSubtract( vecHopTarget, GetAbsOrigin(), vecImpulse );
	VectorNormalize( vecImpulse );
	if ( !bIsCeilingTarget )
	{
		bool bIsCrawling = ( GetNavType() == NAV_CRAWL ); 
		float flRaise = (!bIsCrawling) ? bt_jump_raise.GetFloat() : bt_jump_raise_crawl.GetFloat();
		if ( m_bIsFleeing )
		{
			flRaise *= 0.5f;
		}
		else if ( pPreciseMovementPos )
		{
			float flDistToTargetSq = pPreciseMovementPos->DistToSqr( GetAbsOrigin() );
			if ( flDistToTargetSq < 10000 )
			{
				float t = sqrt( flDistToTargetSq );
				t = RemapValClamped( t, 50.0f, 100.0f, 0.0f, 1.0f );
				flRaise = Lerp( t, 0.04f, 0.3f );
			}
		}
		vecImpulse.z += flRaise;
		ConstrainJumpDirection( &vecImpulse );
	}

	if ( m_hHitPlayer.Get() )
	{
		vecImpulse *= RandomFloat( 1.25f, 1.5f );
	}
	else
	{
		if ( bIsCeilingTarget )
		{
			vecImpulse *= 1.75f;
			m_BlobController.Off();
		}
	}

	if ( m_bIsFleeing )
	{
		vecImpulse *= RandomFloat( 1.25f, 1.5f );
	}

	// Break constraint holding us to that object
	Detach( &vecImpulse );

	IPhysicsObject *pPhysObj = VPhysicsGetObject();
	vecImpulse *= bt_jump_impulse.GetFloat() * pPhysObj->GetMass();
	pPhysObj->ApplyForceCenter( vecImpulse );

	m_bInFlight = true;
}

void CNPC_BlobTiny::PerformMovement( )
{
	// Don't bother doing anything if we're in flight
	if ( m_bInFlight || ( gpGlobals->curtime < m_flNextHopTime ) )
		return;

	Vector vecGoalPosition = GetNavigator()->GetCurWaypointPos(); 
	bool bIsGoal = GetNavigator()->CurWaypointIsGoal();

	// Need to see if we're close to our goal. If so, leap at it directly
	bool bSetHopTarget = false;
	bool bIsCeilingTarget = false;
	Vector vecHopTarget;

	float flToleranceSqr = BLOB_TINY_ATTACK_DIST_SQR;
	if ( GetNavigator()->CurWaypointRequiresPreciseMovement() )
	{
		flToleranceSqr = 625;
	}

	float flDistToGoalSqr = vecGoalPosition.DistToSqr( GetAbsOrigin() );
	if ( flDistToGoalSqr <= flToleranceSqr )
	{
		if ( bIsGoal )
		{
			vecHopTarget = vecGoalPosition;
			bSetHopTarget = true;
			TaskComplete();
		}
		else
		{
			GetNavigator()->AdvancePath();	
			vecGoalPosition = GetNavigator()->GetCurWaypointPos(); 
		}
	}

	if ( !bSetHopTarget )
	{
		ComputeHopTarget( &vecHopTarget, &bIsCeilingTarget, vecGoalPosition, NULL );
	}

	const Vector *pPreciseMovementPos = NULL;
	Vector vecPreciseMovementPos;
	if ( GetNavigator()->CurWaypointRequiresPreciseMovement() )
	{
		vecPreciseMovementPos = GetNavigator()->GetCurWaypointPos();
		pPreciseMovementPos = &vecPreciseMovementPos;
	}
	Hop( vecHopTarget, bIsCeilingTarget, pPreciseMovementPos );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::RunTask( const Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_BLOB_TINY_GET_PATH_TO_FLEE:
		{
			ChainRunTask( TASK_MOVE_AWAY_PATH, pTask->flTaskData );
		}
		break;

	case TASK_BLOB_TINY_HOP_AWAY_FROM_DANGER_SOUND:
		{
			if ( m_bHeld )
			{
				TaskFail( "Player interrupted by grabbing" );
				break;
			}
																					    
			// Don't bother doing anything if we're in flight
			if ( m_bInFlight || ( gpGlobals->curtime < m_flNextHopTime ) )
				return;

			CSound *pBestSound = GetBestSound( SOUND_DANGER );
			if ( !pBestSound )
			{
				TaskComplete();
				break;
			}

			Assert( pBestSound->SoundContext() & SOUND_CONTEXT_FROM_FIRE );
			CBaseEntity *pSoundEmitter = pBestSound->m_hOwner;
			Vector vecSoundPos = pBestSound->GetSoundReactOrigin();
			Vector vecSoundVel = pSoundEmitter ? pSoundEmitter->GetAbsVelocity() : vec3_origin;

			Vector vecHopTarget;
			bool bIsCeilingTarget;
			ComputeHopAwayTarget( &vecHopTarget, &bIsCeilingTarget, vecSoundPos, vecSoundVel );
			Hop( vecHopTarget, bIsCeilingTarget, NULL );
		}
		break;

	case TASK_WAIT_FOR_MOVEMENT:
		if ( m_bHeld )
		{
			TaskFail( "Player interrupted by grabbing" );
			break;
		}

		// If we were fleeing, but we've lost sight of the thing scaring us, stop
		if ( IsCurSchedule(SCHED_BLOB_TINY_FLEE) && !HasCondition( COND_SEE_FEAR ) )
		{
			TaskComplete();
			break;
		}

		if ( !GetNavigator()->IsGoalActive() )
		{
			TaskComplete();
			return;
		}

		PerformMovement( );
		break;

	case TASK_BLOB_TINY_CHARGE_ENEMY:
		{
			if ( !GetEnemy() )
			{
				TaskFail( FAIL_NO_ENEMY );
				break;
			}

			if ( m_bHeld )
			{
				TaskComplete();
				break;
			}

			// Don't bother doing anything if we're in flight
			if ( m_bInFlight || ( gpGlobals->curtime < m_flNextHopTime ) )
				return;

			CBaseEntity *pEnemy = GetEnemy();
			Vector vecTargetPosition;
			pEnemy->CollisionProp()->CalcNearestPoint( GetAbsOrigin(), &vecTargetPosition );

			// Need to see if we're close to our goal. If so, leap at it directly
			Vector vecHopTarget;
			bool bIsCeilingTarget = false;
			ComputeHopTarget( &vecHopTarget, &bIsCeilingTarget, vecTargetPosition, pEnemy );
			Hop( vecHopTarget, bIsCeilingTarget, NULL );
		}
		break;

	case TASK_BLOB_TINY_WAIT_TO_LAND:
		if ( !m_bInFlight )
		{
			TaskComplete();
		}
		break;

	case TASK_BLOB_TINY_SPAWN_TINY_BLOBS:
		if ( gpGlobals->curtime >= m_flNextSplitTime )
		{
			Split();
			if ( --m_nTaskSplitCount <= 0 )
			{
				m_flNextSplitTime = gpGlobals->curtime + RandomFloat( 3.0f, 5.0f );
				if ( ++m_nSplitCount >= m_nMaxSplitCount )
				{
					m_bIsSplitter = false;
				}
				TaskComplete();
			}
			else
			{
				m_flNextSplitTime = gpGlobals->curtime + 0.4f;
			}
		}
		break;

	default:
		BaseClass::RunTask( pTask );
		break;
	}
}


//-----------------------------------------------------------------------------
// Stun
//-----------------------------------------------------------------------------
float CNPC_BlobTiny::GetStunDelay()
{
	return sk_blob_tiny_stun_delay.GetFloat();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::AcidSplashTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet(FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS) )
		return;

	if ( m_bHeld )
		return;

	Disposition_t disp = IRelationType(pOther);

	// Ignore anyone that I'm friendly or neutral to.
	if( disp != D_HT && disp != D_FR)
		return;

	if ( pOther->IsPlayer() )
	{
		// Acid splash effect
		CRecipientFilter filter;
		filter.MakeReliable();
		filter.AddRecipient( static_cast< CBasePlayer*>( pOther ) );

		UserMessageBegin( filter, "AcidSplash" );
		WRITE_FLOAT( gpGlobals->curtime );
		WRITE_FLOAT( 0.5f );
		MessageEnd();
	}

	// Apply damage to target
	CTakeDamageInfo	info( this, this, sk_dmg_blob_tiny.GetFloat(), 
		DMG_ACID | DMG_PREVENT_PHYSICS_FORCE );
	pOther->TakeDamage( info );

	// Pop!
	CTakeDamageInfo	dmgInfo( this, this, GetHealth(), DMG_CRUSH | DMG_PREVENT_PHYSICS_FORCE );
	TakeDamage( dmgInfo );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::Attach( CBaseEntity *pOther )
{
	Detach();

	IPhysicsObject *pPhysics = VPhysicsGetObject();
	if ( !pPhysics )
		return;

	// Slam orientation to match impact normal
	Vector vecPosition;
	QAngle angles;
	VectorAngles( m_vecImpactNormal, angles );
	pPhysics->GetPosition( &vecPosition, NULL );
	pPhysics->SetPosition( vecPosition, angles, false );
	SetAbsAngles( angles );

	// Slam velocity to zero
	pPhysics->SetVelocity( &vec3_origin, NULL );

	m_vecBlobVelocity = vec3_origin;

	IPhysicsObject *pOtherPhysics = pOther->VPhysicsGetObject();
	if ( !pOtherPhysics )
	{
		pOtherPhysics = g_PhysWorldObject;
	}

	if ( pOther->IsPlayer() )
	{
		SetOwnerEntity( pOther );
		pPhysics->RecheckContactPoints();
		m_hHitPlayer = static_cast< CBasePlayer* >( pOther );
	}
		    
	CBreakableProp *pProp = dynamic_cast<CBreakableProp*>( pOther );
	if ( pProp && pProp->GetExplosiveDamage() > 0.0f )
	{
		pProp->Ignite( 5.0f, false );
	}
						 
	constraint_fixedparams_t fixed;
	fixed.Defaults();
	fixed.InitWithCurrentObjectState( pOtherPhysics, pPhysics );
	fixed.constraint.Defaults();
	fixed.constraint.forceLimit	= ImpulseScale( pPhysics->GetMass(), 200 );
	fixed.constraint.torqueLimit = ImpulseScale( pPhysics->GetMass(), 800 );
	m_pConstraint = physenv->CreateFixedConstraint( pOtherPhysics, pPhysics, NULL, fixed );
	m_pConstraint->SetGameData( (void *)this );

	m_BlobController.Off();

	if ( !bt_no_sizzle.GetInt() )
	{
		DispatchParticleEffect( "acid_sizzle", PATTACH_ABSORIGIN_FOLLOW, this );
	}
}

bool CNPC_BlobTiny::Detach( Vector *pLeapDir )
{
	m_hHitPlayer = NULL;

	if ( m_pConstraint )
	{
		IPhysicsObject *pMe = m_pConstraint->GetAttachedObject( );
		IPhysicsObject *pAttached = m_pConstraint->GetReferenceObject( );
		CBaseEntity *pAttachedEnt = pAttached ? (CBaseEntity*)pAttached->GetGameData() : NULL;

		if ( pAttachedEnt )
		{
			if ( pLeapDir )
			{ 															
				if ( !pAttachedEnt->IsPlayer() )
				{
					Vector vecImpulse = *pLeapDir;
					VectorNormalize( vecImpulse );
					vecImpulse *= -bt_jump_response_impulse.GetFloat() * pMe->GetMass();
					pAttached->ApplyForceOffset( vecImpulse, GetAbsOrigin() );
				}
			}

			if ( pAttachedEnt->IsPlayer() )
			{
				SetOwnerEntity( NULL );
				pMe->RecheckContactPoints();
			}
		}

		physenv->DestroyConstraint( m_pConstraint );
		m_pConstraint = NULL;
	}

	m_BlobController.On();
	return true;
}


static void BlobTiny_Attach( CNPC_BlobTiny *pBlob, CBaseEntity *pOther )
{
	pBlob->Attach( pOther );
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	// Explode if thrown by the phys cannon
	if ( pEvent->pObjects[index]->GetGameFlags() & FVPHYSICS_WAS_THROWN )
	{
		CTakeDamageInfo	dmgInfo( this, this, GetHealth(), DMG_CRUSH | DMG_PREVENT_PHYSICS_FORCE );
		TakeDamage( dmgInfo );
		BaseClass::VPhysicsCollision( index, pEvent );
		return;
	}

	// Make sure we don't keep hitting the same entity
	int otherIndex = !index;
	CBaseEntity *pOther = pEvent->pEntities[otherIndex];

	if ( pEvent->deltaCollisionTime < 0.5f && (pOther == this) )
		return;
										  
	// This test ensures no sticking on the sky
	// (unfortunately, ladders also use 'X')
	const surfacedata_t *pHit = physprops->GetSurfaceData( pEvent->surfaceProps[otherIndex] );
	if ( pHit->game.material == 'X' )
	{
		BaseClass::VPhysicsCollision( index, pEvent );
		return;
	}

	// Don't stick to things that are slamming against me
	Vector vecVelDelta;
	VectorSubtract( pEvent->preVelocity[0], pEvent->preVelocity[1], vecVelDelta );
	if ( ( pEvent->preVelocity[otherIndex].LengthSqr() >= 200 * 200 ) && !pOther->IsPlayer() )
	{
		BaseClass::VPhysicsCollision( index, pEvent );
		return;
	}

	Vector vecLastImpactPosition = m_vecImpactPosition;
	pEvent->pInternalData->GetContactPoint( m_vecImpactPosition );
	pEvent->pInternalData->GetSurfaceNormal( m_vecImpactNormal );
	if ( index == 0 )
	{
		m_vecImpactNormal *= -1.0f;
	}

	// Deal with list of constraint directions. Do not store parallel constraints
	if ( vecLastImpactPosition.DistToSqr( m_vecImpactPosition ) > 600 )
	{
		m_Constraints.RemoveAll();
	}

	bool bFoundMatch = false;
	int nCount = m_Constraints.Count();
	for ( int i = 0; i < nCount; ++i )
	{
		if ( DotProduct( m_Constraints[i], m_vecImpactNormal ) > ( 1.0f - 1e-3f ) )
		{
			bFoundMatch = true;
			break;
		}
	}
	if ( !bFoundMatch )
	{
		m_Constraints.AddToTail( m_vecImpactNormal );
	}

	// Stick to what we hit
	float flDelay = GetJumpDelay();
	m_flNextHopTime = gpGlobals->curtime + flDelay;
	m_bInFlight = false;

	g_PostSimulationQueue.QueueCall( BlobTiny_Attach, this, pOther );

	BaseClass::VPhysicsCollision( index, pEvent );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysGunUser - 
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason )
{
	Detach();
	m_BlobController.Off();

	// Are we just being punted?
	if ( reason == PUNTED_BY_CANNON )
	{
		// Be stunned
		m_bInFlight = true;
		m_flNextHopTime = gpGlobals->curtime + GetStunDelay();
		return;
	}

	m_OnPhysGunPickup.FireOutput( pPhysGunUser, this );
	m_bHeld = true;
	m_bInFlight = false;

	// Eliminate extra gravity
	EmitSound( "NPC_BlobTiny.Held" );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysGunUser - 
//-----------------------------------------------------------------------------
void CNPC_BlobTiny::OnPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t Reason )
{
	m_bHeld = false;
	m_bInFlight = true;
	m_flNextHopTime = gpGlobals->curtime + GetStunDelay();

	m_OnPhysGunDrop.FireOutput( pPhysGunUser, this );

	if ( Reason == LAUNCHED_BY_CANNON )
	{
		PhysSetGameFlags( VPhysicsGetObject(), FVPHYSICS_WAS_THROWN );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEnemy - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_BlobTiny::IsValidEnemy( CBaseEntity *pEnemy )
{
	// If the enemy's over the vehicle detection range, and it's not a player in a vehicle, ignore it
	if ( pEnemy )
	{
		float flDistance = GetAbsOrigin().DistTo( pEnemy->GetAbsOrigin() );
		if ( flDistance >= BLOB_TINY_IDLE_SEE_DIST )
			return false;

		// Never pick something I fear
		if ( IRelationType( pEnemy ) == D_FR )
			return false;

		// Don't attack flying things.
		if ( pEnemy->GetMoveType() == MOVETYPE_FLY )
			return false;
	}

	return BaseClass::IsValidEnemy( pEnemy );
}


//-----------------------------------------------------------------------------
//
// Schedules
//
//-----------------------------------------------------------------------------
AI_BEGIN_CUSTOM_NPC( npc_blob_tiny, CNPC_BlobTiny )

	// Conditions
	DECLARE_CONDITION( COND_BLOB_TINY_CAN_SPLIT )

	//Tasks
	DECLARE_TASK( TASK_BLOB_TINY_CHARGE_ENEMY )
	DECLARE_TASK( TASK_BLOB_TINY_GET_PATH_TO_FLEE )
	DECLARE_TASK( TASK_BLOB_TINY_HOP_AWAY_FROM_DANGER_SOUND )
	DECLARE_TASK( TASK_BLOB_TINY_SPAWN_TINY_BLOBS )
	DECLARE_TASK( TASK_BLOB_TINY_WAIT_TO_LAND )

	//Schedules

	DEFINE_SCHEDULE
	(
	SCHED_BLOB_TINY_RANGE_ATTACK1,

		"	Tasks"
		"		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_CHASE_ENEMY"
		"		TASK_BLOB_TINY_CHARGE_ENEMY	0"
		"	"
		"	Interrupts"
		"		COND_ENEMY_DEAD"
		"		COND_NEW_ENEMY"
		"		COND_ENEMY_OCCLUDED"
		"		COND_ENEMY_TOO_FAR"
		"		COND_HEAR_DANGER"
		"		COND_BLOB_TINY_CAN_SPLIT"
	)
	
	DEFINE_SCHEDULE
	(
	SCHED_BLOB_TINY_CHASE_ENEMY,

		"	Tasks"
		"		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_BLOB_TINY_RANGE_ATTACK1"
		"		TASK_SET_TOLERANCE_DISTANCE		48"
		"		TASK_GET_PATH_TO_ENEMY			0"
		"		TASK_RUN_PATH					0"
		"		TASK_WAIT_FOR_MOVEMENT			0"
		"	"
		"	Interrupts"
		"		COND_ENEMY_DEAD"
		"		COND_ENEMY_UNREACHABLE"
		"		COND_ENEMY_TOO_FAR"
		"		COND_CAN_RANGE_ATTACK1"
		"		COND_TASK_FAILED"
		"		COND_SEE_FEAR"
		"		COND_HEAR_DANGER"
		"		COND_BLOB_TINY_CAN_SPLIT"
	)

	DEFINE_SCHEDULE
	(
	SCHED_BLOB_TINY_FLEE,

		"	Tasks"
		"		TASK_SET_FAIL_SCHEDULE				SCHEDULE:SCHED_IDLE_STAND"
		"		TASK_BLOB_TINY_GET_PATH_TO_FLEE	300"
		"		TASK_RUN_PATH						0"
		"		TASK_STOP_MOVING					0"
		"	"
		"	Interrupts"
		"		COND_NEW_ENEMY"
		"		COND_TASK_FAILED"
	)

	DEFINE_SCHEDULE
	(
	SCHED_BLOB_TINY_ALERT_STAND,

	"	Tasks"
	"		TASK_STOP_MOVING			0"
	"		TASK_FACE_REASONABLE		0"
	"		TASK_SET_ACTIVITY			ACTIVITY:ACT_IDLE"
	"		TASK_WAIT					2"
	""
	"	Interrupts"
	"		COND_NEW_ENEMY"
	"		COND_SEE_ENEMY"
	"		COND_SEE_FEAR"
	"		COND_LIGHT_DAMAGE"
	"		COND_HEAVY_DAMAGE"
	"		COND_PROVOKED"
	"		COND_SMELL"
	"		COND_HEAR_COMBAT"		// sound flags
	"		COND_HEAR_WORLD"
	"		COND_HEAR_PLAYER"
	"		COND_HEAR_DANGER"
	"		COND_HEAR_BULLET_IMPACT"
	"		COND_IDLE_INTERRUPT"
	"		COND_BLOB_TINY_CAN_SPLIT"
	)

	DEFINE_SCHEDULE
	(
	SCHED_BLOB_TINY_HOP_AWAY_FROM_DANGER_SOUND,

	"	Tasks"
	"		TASK_BLOB_TINY_HOP_AWAY_FROM_DANGER_SOUND	0"
	""
	"	Interrupts"
	"		COND_NO_HEAR_DANGER"
	)

	DEFINE_SCHEDULE
	(
	SCHED_BLOB_TINY_SPAWN_TINY_BLOBS,

	"	Tasks"
	"		TASK_BLOB_TINY_WAIT_TO_LAND	0"
	"		TASK_BLOB_TINY_SPAWN_TINY_BLOBS	0"
	)

AI_END_CUSTOM_NPC()
