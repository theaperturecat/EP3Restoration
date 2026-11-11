//========= Copyright © 1996-2007, Valve Corporation, All rights reserved. ============//
//
// Purpose: Margarita Soldier NPC, mid-size (man-sized)
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
//#if 0
#include "ai_hull.h"
#include "ai_motor.h"
#include "ai_squad.h"
#include "npc_combines.h"
#include "bitstring.h"
#include "engine/IEngineSound.h"
#include "soundent.h"
#include "ndebugoverlay.h"
#include "npcevent.h"
#include "hl2/hl2_player.h"
#include "game.h"
#include "ammodef.h"
#include "explode.h"
#include "ai_memory.h"
#include "Sprite.h"
#include "soundenvelope.h"
#include "hl2_gamerules.h"
#include "gameweaponmanager.h"
#include "ai_tacticalservices.h"
#include "ai_behavior_assault.h"
#include "ai_behavior_follow.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"
#include "particle_parse.h"
#include "npc_playercompanion.h"
#include "physics_saverestore.h"
#include "ai_waypoint.h"
#include "ai_addon.h"
#include "ieffects.h"

//---------------------------------------------------------
// Interactions
//---------------------------------------------------------
extern int g_interactionAddOnShoot;
extern int g_interactionAddOnBeginShooting;

//---------------------------------------------------------
// These are gestures for making the soldier wobble when 
// an addon fires.
//---------------------------------------------------------
extern int ACT_RECOIL_LEFT_LIGHT;
extern int ACT_RECOIL_RIGHT_LIGHT;

//---------------------------------------------------------
// Facial gestures
//---------------------------------------------------------
extern int ACT_BARE_TEETH;

//theaperturecat - these are stubbed (we dont have margarita soldier ): )
int ACT_BARE_TEETH;
int ACT_RECOIL_LEFT_LIGHT;
int ACT_RECOIL_RIGHT_LIGHT;

ConVar sk_soldier_mid_snarl("sk_soldier_mid_snarl","4.0");

//-----------------------------------------------
// ConVars
//-----------------------------------------------
extern ConVar sk_soldier_mid_snarl;
extern ConVar g_debug_basescanner;

//-----------------------------------------------
// Anim Events
//-----------------------------------------------
extern int AE_SCANNER_CLOSED;


bool IsAddOnAttachmentAvailable( CAI_BaseNPC *pHost, char *pszAttachmentName );
int CountAddOns( CAI_BaseNPC *pHost );


//=============================================================================
//=============================================================================
class CNPC_CompositeCompanion : public CNPC_PlayerCompanion, public IMotionEvent
{
	DECLARE_CLASS( CNPC_CompositeCompanion, CNPC_PlayerCompanion );

public:
	CNPC_CompositeCompanion();
	~CNPC_CompositeCompanion();

public: 
	bool		CreateBehaviors();
	void		Spawn( void );
	void		Precache( void );
	virtual Class_T		Classify ( void ) { return CLASS_PLAYER_ALLY_VITAL; }

	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	virtual void Touch( CBaseEntity *pOther );

	bool OverrideMoveFacing( const AILocalMoveGoal_t &move, float flInterval );

	//------------------------------------
	// Conditions
	//------------------------------------
	virtual void GatherConditions();

	//------------------------------------
	// Schedule
	//------------------------------------
	virtual	void PrescheduleThink();
	virtual	int SelectSchedule();
	virtual int TranslateSchedule( int scheduleType );
	virtual void StartTask( const Task_t *pTask );

	//------------------------------------
	// Event notifiers
	//------------------------------------
	void HandleAnimEvent( animevent_t *pEvent );
	virtual void TranslateAddOnAttachment( char *pchAttachmentName, int iCount );
	Activity NPC_TranslateActivity( Activity eNewActivity );
	int OnTakeDamage( const CTakeDamageInfo &info );
	bool HandleInteraction( int interactionType, void *data, CBaseCombatCharacter* sourceEnt );

	//------------------------------------
	// Sounds
	//------------------------------------
	void PainSound( const CTakeDamageInfo &info );
	void DeathSound(  const CTakeDamageInfo &info  );

	//------------------------------------
	// Autoaim
	//------------------------------------
	float GetAutoAimRadius();

public:

	//------------------------------------
	// Flying
	//------------------------------------
	void			GetVelocity(Vector *vVelocity, AngularImpulse *vAngVelocity);
	virtual QAngle	BodyAngles();

	virtual bool	ShouldSavePhysics() { return true; }

	Vector	VelocityToAvoidObstacles(float flInterval);

	virtual void TurnHeadToTarget( float flInterval, const Vector &moveTarget );

	void MoveInDirection( float flInterval, const Vector &targetDir, float accelXY, float accelZ, float decay);
	void MoveToLocation( float flInterval, const Vector &target, float accelXY, float accelZ, float decay);
	void Decelerate( float flInterval, float decay );
	void AddNoiseToVelocity( float noiseScale = 1.0 );
	void LimitSpeed( float zLimit, float maxSpeed = -1 );

	AI_NavPathProgress_t ProgressFlyPath( float flInterval,
		const CBaseEntity *pNewTarget, 
		unsigned collisionMask, 
		bool bNewTrySimplify = true, 
		float strictPointTolerance = 32.0 );

	const Vector &GetCurrentVelocity() const		{ return m_vCurrentVelocity; }
	void SetCurrentVelocity(const Vector &vNewVel)	{ m_vCurrentVelocity = vNewVel; }

	const Vector &GetNoiseMod() const				{ return m_vNoiseMod; }
	void SetNoiseMod( float x, float y, float z )	{ m_vNoiseMod.Init( x, y, z ); }
	void SetNoiseMod( const Vector &noise )			{ m_vNoiseMod = noise; }

	void TranslateNavGoal( CBaseEntity *pTarget, Vector &chasePosition );

	virtual float GetHeadTurnRate( void ) { return 15.0f; }	// Degrees per second

	bool			CreateVPhysics( void );
	IMotionEvent::simresult_e Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular );

	virtual void ClampMotorForces( Vector &linear, AngularImpulse &angular );

	virtual float	GetMaxSpeed( void );

	inline CBaseEntity	*EntityToWatch( void );
	void				UpdateHead( float flInterval );

	// Movement
	virtual bool		OverridePathMove( CBaseEntity *pMoveTarget, float flInterval );
	virtual bool		OverrideMove( float flInterval );
	Vector				IdealGoalForMovement( const Vector &goalPos, const Vector &startPos, float idealRange, float idealHeight );
	virtual void		MoveToAttack(float flInterval);
	virtual void		MoveToTarget( float flInterval, const Vector &vecMoveTarget );
	virtual void		MoveExecute_Alive(float flInterval);
	virtual float		MinGroundDist( void );
	Vector				VelocityToEvade(CBaseCombatCharacter *pEnemy);
	virtual float		GetGoalDistance( void );
	bool				GetGoalDirection( Vector *vOut );

	// -------------------------------
	//  Movement vars
	// -------------------------------
	Vector			m_vCurrentVelocity;
	Vector			m_vCurrentBanking;
	Vector			m_vNoiseMod;
	float			m_fHeadYaw;
	Vector			m_vLastPatrolDir;
	IPhysicsMotionController	*m_pMotionController;
public:

	bool AddOn_Equip( CBaseEntity *pOther );
	void Recoil( CBaseEntity *pAddOn );
	bool CanSnarl();
	void Snarl( bool bMakeSound );

	void InputSnarl( inputdata_t &data )	{ Snarl( true ); }


	CAI_AssaultBehavior m_AssaultBehavior;
	CAI_FollowBehavior m_FollowBehavior;

	float			m_flPainTime;
	float			m_flTimeNextSnarl;

	int				m_nPoseFaceVert;
	int				m_nPoseFaceHoriz;

	enum 
	{
		SCHED_COMPOSITE_COMPANION_FLEE_ENEMY = BaseClass::NEXT_SCHEDULE,

		NEXT_SCHEDULE,
	};

	enum 
	{
		COND_SOLDIER_MID_ON_NPC = BaseClass::NEXT_CONDITION,

		NEXT_CONDITION,
	};

	enum
	{
		TASK_COMPOSITE_COMPANION_GET_ENEMY_ESCAPE_PATH = BaseClass::NEXT_TASK,

		NEXT_TASK,
	};

	DEFINE_CUSTOM_AI;
	DECLARE_DATADESC();
};

LINK_ENTITY_TO_CLASS( npc_composite_companion, CNPC_CompositeCompanion );

BEGIN_DATADESC(CNPC_CompositeCompanion)

	DEFINE_FIELD( m_vCurrentVelocity,		FIELD_VECTOR),
	DEFINE_FIELD( m_vCurrentBanking,		FIELD_VECTOR),
	DEFINE_FIELD( m_vNoiseMod,				FIELD_VECTOR),
	DEFINE_FIELD( m_fHeadYaw,				FIELD_FLOAT),
	DEFINE_FIELD( m_vLastPatrolDir,			FIELD_VECTOR),
	DEFINE_FIELD( m_nPoseFaceVert,			FIELD_INTEGER ),
	DEFINE_FIELD( m_nPoseFaceHoriz,			FIELD_INTEGER ),

	DEFINE_PHYSPTR( m_pMotionController ),

	DEFINE_INPUTFUNC( FIELD_VOID, "snarl", InputSnarl ),

END_DATADESC()

//---------------------------------------------------------
//---------------------------------------------------------
CNPC_CompositeCompanion::CNPC_CompositeCompanion()
{
#ifdef _DEBUG
	m_vCurrentVelocity.Init();
	m_vCurrentBanking.Init();
	m_vLastPatrolDir.Init();
#endif

	SetModelName( MAKE_STRING("models/shield_scanner.mdl") );
	//SetModelName( MAKE_STRING("models/soldier_mid.mdl") );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CNPC_CompositeCompanion::~CNPC_CompositeCompanion( void )
{
	physenv->DestroyMotionController( m_pMotionController );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CompositeCompanion::CreateBehaviors()
{
	AddBehavior( &m_AssaultBehavior );
	AddBehavior( &m_FollowBehavior );

	return BaseClass::CreateBehaviors();
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNPC_CompositeCompanion::Precache()
{
	PrecacheModel( STRING( GetModelName() ) );
	PrecacheScriptSound("NPC_Strider.Footstep");

	PrecacheScriptSound( "NPC_SScanner.DeployMine" );
	PrecacheScriptSound( "Soldier_Mid.StepRight" );
	PrecacheScriptSound( "Soldier_Mid.StepLeft" );
	PrecacheScriptSound( "Soldier_Mid.Pain" );
	PrecacheScriptSound( "Soldier_Mid.Die" );
	PrecacheScriptSound( "Soldier_Mid.StartBite" );
	PrecacheScriptSound( "Soldier_Mid.Snarl" );

	PrecacheScriptSound( "NPC_Barnacle.FinalBite" );

	UTIL_PrecacheOther( "ai_addon_shield" );
	UTIL_PrecacheOther( "ai_addon_brain" );
	UTIL_PrecacheOther( "ai_addon_saw" );
	UTIL_PrecacheOther( "ai_addon_basecombatweapon" );
	UTIL_PrecacheOther( "ai_addon_thrownprojectile" );

	PrecacheParticleSystem("soldier_dust_thud");

	BaseClass::Precache();
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNPC_CompositeCompanion::PrescheduleThink()
{
	BaseClass::PrescheduleThink();

	SetViewOffset( Vector( 0, 0, 36) );


	if( GetActivity() == ACT_GLIDE && GetEnemy() != NULL && !HasCondition(COND_ENEMY_OCCLUDED) )
	{
		Vector vecEnemy = GetEnemy()->WorldSpaceCenter();

		GetMotor()->SetIdealYawToTarget( vecEnemy );
		GetMotor()->UpdateYaw();
	}
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNPC_CompositeCompanion::GatherConditions()
{
	BaseClass::GatherConditions();

	if( GetGroundEntity() && GetGroundEntity()->IsNPC() )
	{
		SetCondition( COND_SOLDIER_MID_ON_NPC );
	}
	else
	{
		ClearCondition( COND_SOLDIER_MID_ON_NPC );
	}
}

//---------------------------------------------------------
//---------------------------------------------------------
int CNPC_CompositeCompanion::SelectSchedule()
{
	if( HasCondition(COND_SOLDIER_MID_ON_NPC) )
	{
		return SCHED_MOVE_OFF_OF_NPC;
	}

	if ( BehaviorSelectSchedule() )
	{
		return BaseClass::SelectSchedule();
	}

	return BaseClass::SelectSchedule();
}

//---------------------------------------------------------
//---------------------------------------------------------
int CNPC_CompositeCompanion::TranslateSchedule( int scheduleType )
{
	switch( scheduleType )
	{
	case SCHED_RANGE_ATTACK1:
	{
		Vector vecEnemyPos = GetEnemy()->GetAbsOrigin();
		vecEnemyPos.z = GetAbsOrigin().z;

		if ( vecEnemyPos.DistTo( GetAbsOrigin() ) < 100.0f )
		{
			return SCHED_COMPOSITE_COMPANION_FLEE_ENEMY;
		}
		return SCHED_RANGE_ATTACK1;
		break;
	}

	default:
		return BaseClass::TranslateSchedule( scheduleType );
		break;
	}
}

//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CNPC_CompositeCompanion::StartTask( const Task_t *pTask )
{
	switch (pTask->iTask)
	{	
		// Skip as done via bone controller
	case TASK_FACE_ENEMY:
		{
			TaskComplete();
			break;
		}
		// Activity is just idle (have no run)
	case TASK_RUN_PATH:
		{
			GetNavigator()->SetMovementActivity(ACT_IDLE);
			TaskComplete();
			break;
		}
		// Don't check for run/walk activity
	case TASK_SCRIPT_RUN_TO_TARGET:
	case TASK_SCRIPT_WALK_TO_TARGET:
		{
			if (GetTarget() == NULL)
			{
				TaskFail(FAIL_NO_TARGET);
			}
			else 
			{
				if (!GetNavigator()->SetGoal( GOALTYPE_TARGETENT ) )
				{
					TaskFail(FAIL_NO_ROUTE);
					GetNavigator()->ClearGoal();
				}
			}
			TaskComplete();
			break;
		}
		// Override to get more to get a directional path
	case TASK_GET_PATH_TO_RANDOM_NODE:  
		{
			if ( GetNavigator()->SetRandomGoal( pTask->flTaskData, m_vLastPatrolDir ) )
				TaskComplete();
			else
				TaskFail(FAIL_NO_REACHABLE_NODE);
			break;
		}
	case TASK_IGNORE_OLD_ENEMIES:
		{
			TaskComplete();
			break;
		}
	case TASK_COMPOSITE_COMPANION_GET_ENEMY_ESCAPE_PATH:
		{
			Vector vecEnemyPos = GetEnemy()->GetAbsOrigin();
			vecEnemyPos.z = GetAbsOrigin().z;

			Vector vecFleeDir = GetAbsOrigin() - vecEnemyPos;
			VectorNormalize( vecFleeDir );

			//Make us offset this a little at least
			float flFleeYaw = VecToYaw( vecFleeDir ) + random->RandomInt( -20, 20 );

			vecFleeDir = UTIL_YawToVector( flFleeYaw );

			// Move us to the outer radius of the noise (with some randomness)
			Vector vecFleeGoal = vecEnemyPos + vecFleeDir * 250.0f;

			trace_t tr;
			UTIL_TraceLine( GetAbsOrigin(), vecFleeGoal, MASK_SOLID_BRUSHONLY, this, COLLISION_GROUP_NONE, &tr );

			if ( tr.fraction < 0.5 )
			{
				// If we're backing into a wall, just go the other way
				vecFleeGoal = vecEnemyPos - vecFleeDir * 250.0f;
			}

			// Find a route to that position
			AI_NavGoal_t goal( vecFleeGoal + Vector( 0, 0, 8 ) );

			if ( GetNavigator()->SetGoal( goal ) )
			{
				GetNavigator()->SetArrivalDirection( -vecFleeDir );
			}
			else
			{
				TaskFail( "Couldn't create enemy flee path." );
			}

			TaskComplete();
			break;
		}
	default:
		{
			BaseClass::StartTask(pTask);
		}
	}
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNPC_CompositeCompanion::HandleAnimEvent( animevent_t *pEvent )
{
	if( pEvent->Event() == AE_SCANNER_CLOSED)
	{
		SetActivity( ACT_IDLE );
		return;
	}

	BaseClass::HandleAnimEvent( pEvent );
}

void CNPC_CompositeCompanion::TranslateAddOnAttachment( char *pchAttachmentName, int iCount )
{
	if ( Q_strcmp( pchAttachmentName, "addon_baseshooter" ) == 0 )
	{
		switch ( iCount )
		{
		case 0:
			Q_strcpy( pchAttachmentName, "addon_side1" );
			return;

		case 1:
			Q_strcpy( pchAttachmentName, "addon_side2" );
			return;

		default:
			Q_strcpy( pchAttachmentName, "" );
			return;
		}
	}

	if ( Q_strcmp( pchAttachmentName, "addon_head" ) == 0 )
	{
		if ( iCount == 0 )
		{
			return;
		}

		Q_strcpy( pchAttachmentName, "" );
		return;

	}

	if ( Q_strcmp( pchAttachmentName, "addon_front" ) == 0 )
	{
		if ( iCount == 0 )
		{
			Q_strcpy( pchAttachmentName, "addon_head" );
			return;
		}

		Q_strcpy( pchAttachmentName, "" );
		return;

	}

	// Everything else can go on the bottom for now
	if ( iCount == 0 )
	{
		Q_strcpy( pchAttachmentName, "addon_bottom" );
		return;
	}

	Q_strcpy( pchAttachmentName, "" );
	return;

	// TODO: Use addon_back eventually
	//Q_strcpy( pchAttachmentName, "addon_back" );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : idealActivity - 
//			*pIdealWeaponActivity - 
// Output : int
//-----------------------------------------------------------------------------
Activity CNPC_CompositeCompanion::NPC_TranslateActivity( Activity eNewActivity )
{
	// The claw scanner came along a little late and doesn't have the activities
	// of the city scanner. So Just pick between these three
	if( eNewActivity == ACT_DISARM )
	{
		// Closing up.
		return eNewActivity;
	}

	char szAttachmentName[ 256 ];
	Q_strcpy( szAttachmentName, "addon_bottom" );


	if( IsAddOnAttachmentAvailable( this, szAttachmentName ) )
	{
		return ACT_IDLE;
	}
	else
	{
		return ACT_IDLE_ANGRY;		
	}
}

void CNPC_CompositeCompanion::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	CBasePlayer *pPlayer = ToBasePlayer( pActivator );

	if ( pPlayer == NULL )
		return;

	// Check if we're actually trying to use an addon
	Vector forward, up;
	pPlayer->EyeVectors( &forward, NULL, &up );

	trace_t tr;
	Vector searchCenter = pPlayer->EyePosition();

	// NOTE: Some debris objects are useable too, so hit those as well
	// A button, etc. can be made out of clip brushes, make sure it's +useable via a traceline, too.
	CTraceFilterSkipTwoEntities filter( this, pPlayer, COLLISION_GROUP_NONE );
	int useableContents = MASK_SOLID | CONTENTS_DEBRIS | CONTENTS_PLAYERCLIP;

	UTIL_TraceLine( searchCenter, searchCenter + forward * 1024, useableContents, &filter, &tr );

	if ( tr.m_pEnt )
	{
		CAI_AddOn *pAddOn = dynamic_cast<CAI_AddOn*>( tr.m_pEnt );

		if ( pAddOn && pAddOn->GetNPCHost() == this )
		{
			pAddOn->Use( pActivator, pCaller, useType, value );
			return;
		}
	}

	// Give it our active weapon if possible
	CBaseCombatWeapon *pWeapon = pPlayer->GetActiveWeapon();
	if ( pWeapon && pWeapon->GetAIAddOn().ToCStr()[ 0 ] != '\0' )
	{
		pPlayer->Weapon_Drop( pWeapon, NULL, NULL );

		if ( !AddOn_Equip( pWeapon ) )
		{
			// Didn't work out... give it back
			pPlayer->Weapon_Equip( pWeapon );
		}
	}

	// Drop through
	BaseClass::Use( pActivator, pCaller, useType, value );
}

void CNPC_CompositeCompanion::Touch(CBaseEntity *pOther)
{
	IPhysicsObject *pPhys = pOther->VPhysicsGetObject();
	if ( !pPhys || !( pPhys->GetGameFlags() & FVPHYSICS_PLAYER_HELD ) )
		return;

	AddOn_Equip( pOther );
}

//---------------------------------------------------------
//---------------------------------------------------------
bool CNPC_CompositeCompanion::OverrideMoveFacing( const AILocalMoveGoal_t &move, float flInterval )
{
	if ( GetEnemy() && HasCondition( COND_SEE_ENEMY ) )
	{
		AddFacingTarget( GetEnemy(), GetEnemy()->WorldSpaceCenter(), 1.0f, 0.2f );
	}

	return BaseClass::OverrideMoveFacing( move, flInterval );
}

//---------------------------------------------------------
//---------------------------------------------------------
int	CNPC_CompositeCompanion::OnTakeDamage( const CTakeDamageInfo &info )
{
	if( info.GetAttacker() != this && info.GetAttacker()->MyNPCPointer() && GetSquad() && 
		GetSquad()->GetSquadIndex( static_cast<CAI_BaseNPC *>( info.GetAttacker() ) ) != -1 )
	{
		return 0;
	}

	CTakeDamageInfo newInfo = info;

	newInfo.SetDamageType( info.GetDamageType() & ~DMG_DISSOLVE );

	return BaseClass::OnTakeDamage( newInfo );
}

//------------------------------------------------------------------------------
// Purpose : Override to return correct velocity
// Input   :
// Output  :
//------------------------------------------------------------------------------
void CNPC_CompositeCompanion::GetVelocity(Vector *vVelocity, AngularImpulse *vAngVelocity)
{
	Assert( GetMoveType() == MOVETYPE_VPHYSICS );
	if ( VPhysicsGetObject() )
	{
		VPhysicsGetObject()->GetVelocity( vVelocity, vAngVelocity );
	}
	else
	{
		if ( vVelocity )
		{
			vVelocity->Init();
		}
		if ( vAngVelocity )
		{
			vAngVelocity->Init();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Turn head yaw into facing direction
// Input  :
// Output :
//-----------------------------------------------------------------------------
QAngle CNPC_CompositeCompanion::BodyAngles()
{
	return QAngle(0,m_fHeadYaw,0);
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CNPC_CompositeCompanion::TurnHeadToTarget(float flInterval, const Vector &MoveTarget )
{
	Vector vecOriginToTarget = MoveTarget - GetLocalOrigin();

	float desYaw = UTIL_AngleDiff(VecToYaw(vecOriginToTarget), 0 );

	m_fHeadYaw = desYaw;
}

//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
float CNPC_CompositeCompanion::MinGroundDist( void )
{
	return 65.0f;
}

//------------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//------------------------------------------------------------------------------
Vector CNPC_CompositeCompanion::VelocityToAvoidObstacles(float flInterval)
{
	// --------------------------------
	//  Avoid banging into stuff
	// --------------------------------
	trace_t tr;
	Vector vTravelDir = m_vCurrentVelocity*flInterval;
	Vector endPos = GetAbsOrigin() + vTravelDir;
	AI_TraceEntity( this, GetAbsOrigin(), endPos, GetAITraceMask()|CONTENTS_WATER, &tr);
	if (tr.fraction != 1.0)
	{	
		// Bounce off in normal 
		Vector vBounce = tr.plane.normal * 0.5 * m_vCurrentVelocity.Length();
		return (vBounce);
	}

	// --------------------------------
	// Try to remain above the ground.
	// --------------------------------
	float flMinGroundDist = MinGroundDist();
	AI_TraceLine(GetAbsOrigin(), GetAbsOrigin() + Vector(0, 0, -flMinGroundDist), 
		GetAITraceMask_BrushOnly()|CONTENTS_WATER, this, COLLISION_GROUP_NONE, &tr);
	if (tr.fraction < 1)
	{
		// Clamp veloctiy
		if (tr.fraction < 0.1)
		{
			tr.fraction = 0.1;
		}

		return Vector(0, 0, 50/tr.fraction);
	}
	return vec3_origin;
}

//------------------------------------------------------------------------------

AI_NavPathProgress_t CNPC_CompositeCompanion::ProgressFlyPath( 
	float flInterval,
	const CBaseEntity *pNewTarget, 
	unsigned collisionMask, 
	bool bNewTrySimplify, 
	float strictPointTolerance)
{
	AI_ProgressFlyPathParams_t params( collisionMask );
	params.strictPointTolerance = strictPointTolerance;
	params.SetCurrent( pNewTarget, bNewTrySimplify );

	AI_NavPathProgress_t progress = GetNavigator()->ProgressFlyPath( params );

	switch ( progress )
	{
	case AINPP_NO_CHANGE:
	case AINPP_ADVANCED:
		{
			MoveToTarget(flInterval, GetNavigator()->GetCurWaypointPos());
			break;
		}

	case AINPP_COMPLETE:
		{
			TaskMovementComplete();
			break;
		}

	case AINPP_BLOCKED: // function is not supposed to test blocking, just simple path progression
	default:
		{
			AssertMsg( 0, ( "Unexpected result" ) );
			break;
		}
	}

	return progress;
}

//-----------------------------------------------------------------------------
// Purpose: 
//
//
//-----------------------------------------------------------------------------
bool CNPC_CompositeCompanion::CreateVPhysics( void )
{
	// Create the object in the physics system
	IPhysicsObject *pPhysicsObject = VPhysicsInitNormal( SOLID_BBOX, FSOLID_NOT_STANDABLE, false );

	m_pMotionController = physenv->CreateMotionController( this );
	m_pMotionController->AttachObject( pPhysicsObject, true );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pTarget - 
//			&chasePosition - 
//-----------------------------------------------------------------------------
void CNPC_CompositeCompanion::TranslateNavGoal( CBaseEntity *pTarget, Vector &chasePosition )
{
	Assert( pTarget != NULL );

	if ( pTarget == NULL )
	{
		chasePosition = vec3_origin;
		return;
	}

	// Chase their eyes
	chasePosition = pTarget->GetAbsOrigin() + pTarget->GetViewOffset();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pController - 
//			*pObject - 
//			deltaTime - 
//			&linear - 
//			&angular - 
// Output : IMotionEvent::simresult_e
//-----------------------------------------------------------------------------
IMotionEvent::simresult_e CNPC_CompositeCompanion::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
	static int count;

	IPhysicsObject *pPhysicsObject = VPhysicsGetObject();
	// Assert( pPhysicsObject );
	if (!pPhysicsObject) 
		return SIM_NOTHING;

	// move
	Vector actualVelocity;
	AngularImpulse actualAngularVelocity;
	pPhysicsObject->GetVelocity( &actualVelocity, &actualAngularVelocity );
	linear = (m_vCurrentVelocity - actualVelocity) * (0.1 / deltaTime) * 10.0;

	/*
	DevMsg("Sim %d : %5.1f %5.1f %5.1f\n", count++,
	m_vCurrentVelocity.x - actualVelocity.x, 
	m_vCurrentVelocity.y - actualVelocity.y, 
	m_vCurrentVelocity.z - actualVelocity.z );
	*/

	// do angles.
	Vector actualPosition;
	QAngle actualAngles;
	pPhysicsObject->GetPosition( &actualPosition, &actualAngles ); 

	// FIXME: banking currently disabled, forces simple upright posture
	angular.x = (UTIL_AngleDiff( m_vCurrentBanking.z, actualAngles.z ) - actualAngularVelocity.x) * (1 / deltaTime);
	angular.y = (UTIL_AngleDiff( m_vCurrentBanking.x, actualAngles.x ) - actualAngularVelocity.y) * (1 / deltaTime);

	// turn toward target
	angular.z = UTIL_AngleDiff( m_fHeadYaw, actualAngles.y + actualAngularVelocity.z * 0.1 ) * (1 / deltaTime);

	// angular = m_vCurrentAngularVelocity - actualAngularVelocity;

	// DevMsg("Sim %d : %.1f %.1f %.1f (%.1f)\n", count++, actualAngles.x, actualAngles.y, actualAngles.z, m_fHeadYaw );

	// FIXME: remove the stuff from MoveExecute();
	// FIXME: check MOVE?

	ClampMotorForces( linear, angular );

	return SIM_GLOBAL_ACCELERATION; // on my local axis.   SIM_GLOBAL_ACCELERATION
}

void CNPC_CompositeCompanion::MoveInDirection( float flInterval, const Vector &targetDir, float accelXY, float accelZ, float decay )
{
	decay = ExponentialDecay( decay, 1.0, flInterval );
	accelXY *= flInterval;
	accelZ  *= flInterval;

	m_vCurrentVelocity.x = ( decay * m_vCurrentVelocity.x + accelXY * targetDir.x );
	m_vCurrentVelocity.y = ( decay * m_vCurrentVelocity.y + accelXY * targetDir.y );
	m_vCurrentVelocity.z = ( decay * m_vCurrentVelocity.z + accelZ  * targetDir.z );
}

void CNPC_CompositeCompanion::MoveToLocation( float flInterval, const Vector &target, float accelXY, float accelZ, float decay)
{
	Vector targetDir = target - GetLocalOrigin();
	VectorNormalize(targetDir);

	MoveInDirection(flInterval, targetDir, accelXY, accelZ, decay);
}

void CNPC_CompositeCompanion::Decelerate( float flInterval, float decay )
{
	decay *= flInterval;
	decay = 0.75f;
	m_vCurrentVelocity.x = (decay * m_vCurrentVelocity.x);
	m_vCurrentVelocity.y = (decay * m_vCurrentVelocity.y);
	m_vCurrentVelocity.z = (decay * m_vCurrentVelocity.z);
}

void CNPC_CompositeCompanion::AddNoiseToVelocity( float noiseScale )
{
	if( m_vNoiseMod.x )
	{
		m_vCurrentVelocity.x += noiseScale*sin(m_vNoiseMod.x * gpGlobals->curtime + m_vNoiseMod.x);
	}

	if( m_vNoiseMod.y )
	{
		m_vCurrentVelocity.y += noiseScale*cos(m_vNoiseMod.y * gpGlobals->curtime + m_vNoiseMod.y);
	}

	if( m_vNoiseMod.z )
	{
		m_vCurrentVelocity.z -= noiseScale*cos(m_vNoiseMod.z * gpGlobals->curtime + m_vNoiseMod.z);
	}
}

void CNPC_CompositeCompanion::LimitSpeed( float zLimit, float maxSpeed )
{
	if ( maxSpeed == -1 )
		maxSpeed = m_flSpeed;
	if (m_vCurrentVelocity.Length() > maxSpeed)
	{
		VectorNormalize(m_vCurrentVelocity);
		m_vCurrentVelocity *= maxSpeed;
	}
	// Limit fall speed
	if (zLimit > 0 && m_vCurrentVelocity.z < -zLimit)
	{
		m_vCurrentVelocity.z = -zLimit;
	}
}

void CNPC_CompositeCompanion::ClampMotorForces( Vector &linear, AngularImpulse &angular )
{ 
	// limit reaction forces
	linear.x = clamp( linear.x, -3000, 3000 );
	linear.y = clamp( linear.y, -3000, 3000 );
	linear.z = clamp( linear.z, -3000, 3000 );

	// add in weightlessness
	//linear.z += 800;
}

float CNPC_CompositeCompanion::GetMaxSpeed( void )
{
	return 250.0f;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
inline CBaseEntity *CNPC_CompositeCompanion::EntityToWatch( void )
{
	return ( GetTarget() != NULL ) ? GetTarget() : GetEnemy();	// Okay if NULL
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flInterval - 
//-----------------------------------------------------------------------------
void CNPC_CompositeCompanion::UpdateHead( float flInterval )
{
	float yaw = GetPoseParameter( m_nPoseFaceHoriz );
	float pitch = GetPoseParameter( m_nPoseFaceVert );

	CBaseEntity *pTarget = EntityToWatch();

	Vector	vLookPos;

	if ( !HasCondition( COND_IN_PVS ) || GetAttachment( "eyes", vLookPos ) == false )
	{
		vLookPos = EyePosition();
	}

	if ( pTarget != NULL )
	{
		Vector	lookDir = pTarget->EyePosition() - vLookPos;
		VectorNormalize( lookDir );

		if ( DotProduct( lookDir, BodyDirection3D() ) < 0.0f )
		{
			SetPoseParameter( m_nPoseFaceHoriz,	UTIL_Approach( 0, yaw, 10 ) );
			SetPoseParameter( m_nPoseFaceVert, UTIL_Approach( 0, pitch, 10 ) );

			return;
		}

		float facingYaw = VecToYaw( BodyDirection3D() );
		float yawDiff = VecToYaw( lookDir );
		yawDiff = UTIL_AngleDiff( yawDiff, facingYaw + yaw );

		float facingPitch = UTIL_VecToPitch( BodyDirection3D() );
		float pitchDiff = UTIL_VecToPitch( lookDir );
		pitchDiff = UTIL_AngleDiff( pitchDiff, facingPitch + pitch );

		SetPoseParameter( m_nPoseFaceHoriz, UTIL_Approach( yaw + yawDiff, yaw, 50 ) );
		SetPoseParameter( m_nPoseFaceVert, UTIL_Approach( pitch + pitchDiff, pitch, 50 ) );
	}
	else
	{
		SetPoseParameter( m_nPoseFaceHoriz,	UTIL_Approach( 0, yaw, 10 ) );
		SetPoseParameter( m_nPoseFaceVert, UTIL_Approach( 0, pitch, 10 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Handles movement towards the last move target.
// Input  : flInterval - 
//-----------------------------------------------------------------------------
bool CNPC_CompositeCompanion::OverridePathMove( CBaseEntity *pMoveTarget, float flInterval )
{
	// Save our last patrolling direction
	Vector lastPatrolDir = GetNavigator()->GetCurWaypointPos() - GetAbsOrigin();

	// Continue on our path
	if ( ProgressFlyPath( flInterval, pMoveTarget, (GetAITraceMask()|CONTENTS_WATER), false, 64 ) == AINPP_COMPLETE )
	{
		/*if ( IsCurSchedule( SCHED_COMPOSITE_COMPANION_STAND ) )
		{
			m_vLastPatrolDir = lastPatrolDir;
			VectorNormalize(m_vLastPatrolDir);
		}*/

		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flInterval - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CompositeCompanion::OverrideMove( float flInterval )
{
	// ----------------------------------------------
	//	If dive bombing
	// ----------------------------------------------
	/*if (m_nFlyMode == SCANNER_FLY_DIVE)
	{
		MoveToDivebomb( flInterval );
	}
	else*/
	{
		Vector vMoveTargetPos(0,0,0);
		CBaseEntity *pMoveTarget = NULL;

		if ( !GetNavigator()->IsGoalActive() || ( GetNavigator()->GetCurWaypointFlags() | bits_WP_TO_PATHCORNER ) )
		{
			// Select move target 
			if ( GetTarget() != NULL )
			{
				pMoveTarget = GetTarget();
			}
			else if ( GetEnemy() != NULL )
			{
				pMoveTarget = GetEnemy();
			}

			// Select move target position 
			if ( GetEnemy() != NULL )
			{
				vMoveTargetPos = GetEnemy()->GetAbsOrigin();
			}
		}
		else
		{
			vMoveTargetPos = GetNavigator()->GetCurWaypointPos();
		}

		//ClearCondition( COND_SCANNER_FLY_CLEAR );
		//ClearCondition( COND_SCANNER_FLY_BLOCKED );

		// See if we can fly there directly
		if ( pMoveTarget )
		{
			trace_t tr;
			AI_TraceHull( GetAbsOrigin(), vMoveTargetPos, GetHullMins(), GetHullMaxs(), GetAITraceMask_BrushOnly(), this, COLLISION_GROUP_NONE, &tr );

			float fTargetDist = (1.0f-tr.fraction)*(GetAbsOrigin() - vMoveTargetPos).Length();

			if ( ( tr.m_pEnt == pMoveTarget ) || ( fTargetDist < 50 ) )
			{
				if ( g_debug_basescanner.GetBool() )
				{
					NDebugOverlay::Line(GetLocalOrigin(), vMoveTargetPos, 0,255,0, true, 0);
					NDebugOverlay::Cross3D(tr.endpos,Vector(-5,-5,-5),Vector(5,5,5),0,255,0,true,0.1);
				}

				//SetCondition( COND_SCANNER_FLY_CLEAR );
			}
			else		
			{
				//HANDY DEBUG TOOL	
				if ( g_debug_basescanner.GetBool() )
				{
					NDebugOverlay::Line(GetLocalOrigin(), vMoveTargetPos, 255,0,0, true, 0);
					NDebugOverlay::Cross3D(tr.endpos,Vector(-5,-5,-5),Vector(5,5,5),255,0,0,true,0.1);
				}

				//SetCondition( COND_SCANNER_FLY_BLOCKED );
			}
		}

		// If I have a route, keep it updated and move toward target
		if ( GetNavigator()->IsGoalActive() )
		{
			if ( OverridePathMove( pMoveTarget, flInterval ) )
			{
				return true;
			}
		}	
		// ----------------------------------------------
		//	If attacking
		// ----------------------------------------------
		/*else if (m_nFlyMode == SCANNER_FLY_ATTACK)
		{
			MoveToAttack( flInterval );
		}*/
		// -----------------------------------------------------------------
		// If I don't have a route, just decelerate
		// -----------------------------------------------------------------
		else if (!GetNavigator()->IsGoalActive())
		{
			float	myDecay	 = 9.5;
			Decelerate( flInterval, myDecay);

			// Face targets
			m_fHeadYaw = GetMotor()->GetIdealYaw();
		}
	}

	MoveExecute_Alive( flInterval );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &goalPos - 
//			&startPos - 
//			idealRange - 
//			idealHeight - 
// Output : Vector
//-----------------------------------------------------------------------------
Vector CNPC_CompositeCompanion::IdealGoalForMovement( const Vector &goalPos, const Vector &startPos, float idealRange, float idealHeightDiff )
{
	Vector	vMoveDir;

	if ( GetGoalDirection( &vMoveDir ) == false )
	{
		vMoveDir = ( goalPos - startPos );
		vMoveDir.z = 0;
		VectorNormalize( vMoveDir );
	}

	// Move up from the position by the desired amount
	Vector vIdealPos = goalPos + Vector( 0, 0, idealHeightDiff ) + ( vMoveDir * -idealRange );

	// Trace down and make sure we can fit here
	trace_t	tr;
	AI_TraceHull( vIdealPos, vIdealPos - Vector( 0, 0, MinGroundDist() ), GetHullMins(), GetHullMaxs(), GetAITraceMask(), this, COLLISION_GROUP_NONE, &tr );

	// Move up otherwise
	if ( tr.fraction < 1.0f )
	{
		vIdealPos.z += ( MinGroundDist() * ( 1.0f - tr.fraction ) );
	}

	//FIXME: We also need to make sure that we fit here at all, and if not, chose a new spot

	// Debug tools
	if ( g_debug_basescanner.GetBool() )
	{
		NDebugOverlay::Cross3D( goalPos, -Vector(8,8,8), Vector(8,8,8), 255, 255, 0, true, 0.1f );
		NDebugOverlay::Cross3D( startPos, -Vector(8,8,8), Vector(8,8,8), 255, 0, 255, true, 0.1f );
		NDebugOverlay::Cross3D( vIdealPos, -Vector(8,8,8), Vector(8,8,8), 255, 255, 255, true, 0.1f );
		NDebugOverlay::Line( startPos, goalPos, 0, 255, 0, true, 0.1f );

		NDebugOverlay::Cross3D( goalPos + ( vMoveDir * -idealRange ), -Vector(8,8,8), Vector(8,8,8), 255, 255, 255, true, 0.1f );
		NDebugOverlay::Line( goalPos, goalPos + ( vMoveDir * -idealRange ), 255, 255, 0, true, 0.1f );
		NDebugOverlay::Line( goalPos + ( vMoveDir * -idealRange ), vIdealPos, 255, 255, 0, true, 0.1f );
	}

	return vIdealPos;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : flInterval - 
//-----------------------------------------------------------------------------
void CNPC_CompositeCompanion::MoveToAttack(float flInterval)
{
	if (GetEnemy() == NULL)
		return;

	if ( flInterval <= 0 )
		return;

	Vector vTargetPos = GetEnemyLKP();

	//float flDesiredDist = m_flAttackNearDist + ( ( m_flAttackFarDist - m_flAttackNearDist ) / 2 );

	Vector idealPos = IdealGoalForMovement( vTargetPos, GetAbsOrigin(), GetGoalDistance(), 150.0f );

	MoveToTarget( flInterval, idealPos );

	//FIXME: Re-implement?

	/*
	// ---------------------------------------------------------
	//  Add evasion if I have taken damage recently
	// ---------------------------------------------------------
	if ((m_flLastDamageTime + SCANNER_EVADE_TIME) > gpGlobals->curtime)
	{
	vFlyDirection = vFlyDirection + VelocityToEvade(GetEnemyCombatCharacterPointer());
	}
	*/
}

//-----------------------------------------------------------------------------
// Purpose: Accelerates toward a given position.
// Input  : flInterval - Time interval over which to move.
//			vecMoveTarget - Position to move toward.
//-----------------------------------------------------------------------------
void CNPC_CompositeCompanion::MoveToTarget( float flInterval, const Vector &vecMoveTarget )
{
	// Look at our inspection target if we have one
	if ( GetEnemy() != NULL )
	{
		// Otherwise at our enemy
		TurnHeadToTarget( flInterval, GetEnemy()->EyePosition() );
	}
	else
	{
		// Otherwise face our motion direction
		TurnHeadToTarget( flInterval, vecMoveTarget );
	}

	// -------------------------------------
	// Move towards our target
	// -------------------------------------
	float myAccel;
	float myZAccel = 400.0f;
	float myDecay  = 0.15f;

	Vector vecCurrentDir;

	// Get the relationship between my current velocity and the way I want to be going.
	vecCurrentDir = GetCurrentVelocity();
	VectorNormalize( vecCurrentDir );

	Vector targetDir = vecMoveTarget - GetAbsOrigin();
	float flDist = VectorNormalize(targetDir);

	float flDot;
	flDot = DotProduct( targetDir, vecCurrentDir );

	if( flDot > 0.25 )
	{
		// If my target is in front of me, my flight model is a bit more accurate.
		myAccel = 250;
	}
	else
	{
		// Have a harder time correcting my course if I'm currently flying away from my target.
		myAccel = 128;
	}

	if ( myAccel > flDist / flInterval )
	{
		myAccel = flDist / flInterval;
	}

	if ( myZAccel > flDist / flInterval )
	{
		myZAccel = flDist / flInterval;
	}

	MoveInDirection( flInterval, targetDir, myAccel, myZAccel, myDecay );

	// calc relative banking targets
	Vector forward, right, up;
	GetVectors( &forward, &right, &up );

	m_vCurrentBanking.x	= targetDir.x;
	m_vCurrentBanking.z	= 120.0f * DotProduct( right, targetDir );
	m_vCurrentBanking.y	= 0;

	float speedPerc = SimpleSplineRemapVal( GetCurrentVelocity().Length(), 0.0f, GetMaxSpeed(), 0.0f, 1.0f );

	speedPerc = clamp( speedPerc, 0.0f, 1.0f );

	m_vCurrentBanking *= speedPerc;
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CNPC_CompositeCompanion::MoveExecute_Alive(float flInterval)
{
	// Amount of noise to add to flying
	float noiseScale = 3.0f;

	// -------------------------------------------
	//  Avoid obstacles, unless I'm dive bombing
	// -------------------------------------------
	SetCurrentVelocity( GetCurrentVelocity() + VelocityToAvoidObstacles(flInterval) );

	IPhysicsObject *pPhysics = VPhysicsGetObject();

	if ( pPhysics && pPhysics->IsAsleep() )
	{
		pPhysics->Wake();
	}

	// Add time-coherent noise to the current velocity so that it never looks bolted in place.
	AddNoiseToVelocity( noiseScale );

	float maxSpeed = GetEnemy() ? ( GetMaxSpeed() * 2.0f ) : GetMaxSpeed();

	// Limit fall speed
	LimitSpeed( maxSpeed );

	// Update what we're looking at
	UpdateHead( flInterval );

	// Control the tail based on our vertical travel
	float tailPerc = clamp( GetCurrentVelocity().z, -150, 250 );
	tailPerc = SimpleSplineRemapVal( tailPerc, -150, 250, -25, 80 );

	// TODO: fix poses
	//SetPoseParameter( m_nPoseTail, tailPerc );

	// Spin the dynamo based upon our speed
	/*float flCurrentDynamo = GetPoseParameter( m_nPoseDynamo );
	float speed	= GetCurrentVelocity().Length();
	float flDynamoSpeed = (maxSpeed > 0 ? speed / maxSpeed : 1.0) * 60;
	flCurrentDynamo -= flDynamoSpeed;
	if ( flCurrentDynamo < -180.0 )
	{
		flCurrentDynamo += 360.0;
	}
	SetPoseParameter( m_nPoseDynamo, flCurrentDynamo );*/

	// TODO: fix sound
	//PlayFlySound();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
Vector CNPC_CompositeCompanion::VelocityToEvade(CBaseCombatCharacter *pEnemy)
{
	if (pEnemy)
	{
		// -----------------------------------------
		//  Keep out of enemy's shooting position
		// -----------------------------------------
		Vector vEnemyFacing = pEnemy->BodyDirection2D( );
		Vector	vEnemyDir   = pEnemy->EyePosition() - GetLocalOrigin();
		VectorNormalize(vEnemyDir);
		float  fDotPr		= DotProduct(vEnemyFacing,vEnemyDir);

		if (fDotPr < -0.9)
		{
			Vector vDirUp(0,0,1);
			Vector vDir;
			CrossProduct( vEnemyFacing, vDirUp, vDir);

			Vector crossProduct;
			CrossProduct(vEnemyFacing, vEnemyDir, crossProduct);
			if (crossProduct.y < 0)
			{
				vDir = vDir * -1;
			}
			return (vDir);
		}
		else if (fDotPr < -0.85)
		{
			Vector vDirUp(0,0,1);
			Vector vDir;
			CrossProduct( vEnemyFacing, vDirUp, vDir);

			Vector crossProduct;
			CrossProduct(vEnemyFacing, vEnemyDir, crossProduct);
			if (random->RandomInt(0,1))
			{
				vDir = vDir * -1;
			}
			return (vDir);
		}
	}
	return vec3_origin;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CNPC_CompositeCompanion::GetGoalDistance( void )
{
	// TODO: Attack dist?
	//switch ( m_nFlyMode )
	//{
	//case SCANNER_FLY_ATTACK:
	//	{
	//		float goalDist = ( m_flAttackNearDist + ( ( m_flAttackFarDist - m_flAttackNearDist ) / 2 ) );
	//		if( IsEnemyPlayerInSuit() )
	//		{
	//			goalDist *= 0.5;
	//		}
	//		return goalDist;
	//	}
	//	break;
	//}

	return 128.0f;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &vOut - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_CompositeCompanion::GetGoalDirection( Vector *vOut )
{
	CBaseEntity *pTarget = GetTarget();

	if ( pTarget == NULL )
		return false;

	if ( FClassnameIs( pTarget, "info_hint_air" ) || FClassnameIs( pTarget, "info_target" ) )
	{
		AngleVectors( pTarget->GetAbsAngles(), vOut );
		return true;
	}

	return false;
}

bool CNPC_CompositeCompanion::AddOn_Equip( CBaseEntity *pOther )
{
	CAI_AddOn *pAddOn = dynamic_cast<CAI_AddOn*>( pOther );

	if ( !pAddOn )
	{
		char szAIAddOnName[ 256 ];
		Q_strcpy( szAIAddOnName, pOther->GetAIAddOn().ToCStr() );

		if ( szAIAddOnName[ 0 ] == '\0' )
			return false;

		// Spawn the addon
		CBaseEntity *pItem = (CBaseEntity *)CreateEntityByName( szAIAddOnName );

		if ( !pItem )
			return false;

		pAddOn = dynamic_cast<CAI_AddOn*>( pItem );

		if ( !pAddOn )
		{
			UTIL_Remove( pItem );
			return false;
		}

		pAddOn->SetPhysReplacement( pOther );

		DispatchSpawn( pAddOn );

		// Set the position to where the phys prop was
		IPhysicsObject *pAddonPhys = pAddOn->VPhysicsGetObject();

		if ( pAddonPhys )
		{
			pAddonPhys->SetPosition( pOther->GetAbsOrigin(), pOther->GetAbsAngles(), true );
		}

		pAddOn->SetAbsOrigin( pOther->GetAbsOrigin() );
		pAddOn->SetAbsAngles( pOther->GetAbsAngles() );
	}

	bool bInstalled = pAddOn->Install( this, false );

	if ( bInstalled )
	{
		EmitSound( "NPC_SScanner.DeployMine" );
		g_pEffects->Sparks( pAddOn->GetAbsOrigin(), 2, 2 );
	}

	return bInstalled;
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNPC_CompositeCompanion::Recoil( CBaseEntity *pAddOn )
{
	Vector vecRight;
	Vector vecToAddOn;

	GetVectors( NULL, &vecRight, NULL );
	vecToAddOn = pAddOn->GetAbsOrigin() - WorldSpaceCenter();

	// Check side-to-side.
	vecRight.z = 0;
	vecToAddOn.z = 0;
	VectorNormalize( vecRight );
	VectorNormalize( vecToAddOn );

	float flDot = DotProduct( vecRight, vecToAddOn );

	if( flDot > 0.0f )
	{
		if( !IsPlayingGesture((Activity)ACT_RECOIL_RIGHT_LIGHT) )
		{
			RestartGesture( (Activity)ACT_RECOIL_RIGHT_LIGHT );
		}
	}
	else
	{
		if( !IsPlayingGesture((Activity)ACT_RECOIL_LEFT_LIGHT) )
		{
			RestartGesture( (Activity)ACT_RECOIL_LEFT_LIGHT );
		}
	}
}

//---------------------------------------------------------
//---------------------------------------------------------
bool CNPC_CompositeCompanion::CanSnarl()
{
	// Is it time to snarl again?
	if( gpGlobals->curtime < m_flTimeNextSnarl )
		return false;
	
	// Are we close enough to the player for them to see it?
	CBasePlayer *pPlayer = AI_GetSinglePlayer();
	if( pPlayer && GetAbsOrigin().DistToSqr( pPlayer->GetAbsOrigin() ) > Square( 12 * 75 ) )
		return false;


	return true;
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNPC_CompositeCompanion::Snarl( bool bMakeSound )
{
	if( !IsPlayingGesture((Activity)ACT_BARE_TEETH) )
	{
		m_flTimeNextSnarl = gpGlobals->curtime + sk_soldier_mid_snarl.GetFloat() + RandomFloat( 0, 2 );
		RestartGesture( (Activity)ACT_BARE_TEETH );

		if( bMakeSound )
		{
			EmitSound( "NPC_SScanner.DeployMine" );
		}
	}
}

//---------------------------------------------------------
//---------------------------------------------------------
bool CNPC_CompositeCompanion::HandleInteraction( int interactionType, void *data, CBaseCombatCharacter* sourceEnt )
{
	if( interactionType == g_interactionAddOnBeginShooting )
	{
		if( CanSnarl() )
		{
			Snarl( true );
		}
		return true;
	}

	if( interactionType == g_interactionAddOnShoot )
	{
		Recoil( (CBaseEntity *)data );
		return true;
	}

	return BaseClass::HandleInteraction( interactionType, data, sourceEnt );
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNPC_CompositeCompanion::PainSound( const CTakeDamageInfo &info )
{
	if ( gpGlobals->curtime > m_flPainTime )
	{
		EmitSound( "Soldier_Mid.Pain" );
		m_flPainTime = gpGlobals->curtime + 0.5f;
	}
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNPC_CompositeCompanion::DeathSound( const CTakeDamageInfo &info )
{
	EmitSound( "Soldier_Mid.Die" );
}

//---------------------------------------------------------
//---------------------------------------------------------
float CNPC_CompositeCompanion::GetAutoAimRadius()
{
	float radius = BaseClass::GetAutoAimRadius();

	if( GetActivity() == ACT_GLIDE )
		radius *= 2.0f;

	return radius;
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNPC_CompositeCompanion::Spawn()
{
	Precache();

	BaseClass::Spawn();

	SetModel( STRING(GetModelName()) );

	SetHullType( HULL_TINY_CENTERED );
	SetHullSizeNormal();

	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );

	SetMoveType( MOVETYPE_VPHYSICS );

	SetBloodColor( DONT_BLEED );
	SetViewOffset( Vector(0, 0, 10) );		// Position of the eyes relative to NPC's origin.
	m_NPCState	= NPC_STATE_NONE;

	SetNavType( NAV_FLY );
	AddFlag( FL_FLY );

	// This entity cannot be dissolved by the combine balls,
	// nor does it get killed by the mega physcannon.
	AddEFlags( EFL_NO_DISSOLVE | EFL_NO_MEGAPHYSCANNON_RAGDOLL );

	m_iHealth = 100;

	SetCollisionGroup( COLLISION_GROUP_NPC );

	CapabilitiesClear();
	CapabilitiesAdd( bits_CAP_MOVE_FLY | bits_CAP_SQUAD | bits_CAP_TURN_HEAD | bits_CAP_SKIP_NAV_GROUND_CHECK );

	NPCInit();

	// Soldiers will always pursue
	m_flDistTooFar = FLT_MAX;

	// set flight speed
	m_flSpeed = GetMaxSpeed();

	m_nPoseFaceVert = LookupPoseParameter( "flex_vert" );
	m_nPoseFaceHoriz = LookupPoseParameter( "flex_horz" );
}

AI_BEGIN_CUSTOM_NPC( npc_composite_companion, CNPC_CompositeCompanion )

DECLARE_ANIMEVENT( AE_SCANNER_CLOSED )

DECLARE_ACTIVITY( ACT_RECOIL_LEFT_LIGHT )
DECLARE_ACTIVITY( ACT_RECOIL_RIGHT_LIGHT )
DECLARE_ACTIVITY( ACT_BARE_TEETH )

DECLARE_CONDITION( COND_SOLDIER_MID_ON_NPC )

DECLARE_TASK( TASK_COMPOSITE_COMPANION_GET_ENEMY_ESCAPE_PATH )

DEFINE_SCHEDULE
 (
 SCHED_COMPOSITE_COMPANION_FLEE_ENEMY,

 "	Tasks"
 "		TASK_SET_FAIL_SCHEDULE					SCHEDULE:SCHED_IDLE_STAND"
 "		TASK_COMPOSITE_COMPANION_GET_ENEMY_ESCAPE_PATH		0"
 "		TASK_RUN_PATH_TIMED						3"
 "		TASK_STOP_MOVING						0"
 ""
 "	Interrupts"
 "		COND_TASK_FAILED"
 )

AI_END_CUSTOM_NPC()
//#endif