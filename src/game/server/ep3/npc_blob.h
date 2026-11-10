//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef NPC_BLOB_H
#define NPC_BLOB_H
#ifdef _WIN32
#pragma once
#endif


#include "npc_surface.h"
#include "blob_particle.h"
#include "blob_shareddefs.h"
#include "func_pipe_entrance.h"
#include "shape_target.h"

class CBlobRegenerator;

//#define BLOB_RIGID_BODY_FREEZE //comment out to use immovable particles (more stable). Uncomment to use rigid body physics (not perfected yet)

enum BlobStalkTargetState_t
{
	BLOB_STALK_NONE = 0,			// Not currently stalking a target
	BLOB_STALK_FAR,				// Target is distant, move towards it (predicting position with dead-reckoning)
	BLOB_STALK_NEAR,				// Target is near, move slowly to keep separation constant, and maybe lunge to get ahead of it if it moves
	BLOB_STALK_LUNGING,		// Target is near and moving, currently lunging to get ahead of it

	BLOB_STALK_NUMSTATES
};

#define BLOB_PARTICLE_TO_NPC_FAR_DIST		(48.0f)
#define BLOB_PARTICLE_TO_NPC_VERY_FAR_DIST	(150.0f)

enum NeighborForceMode_t
{
	NEIGHBOR_FORCE_NORMAL = 0,
	NEIGHBOR_FORCE_CONTRACT,
	NEIGHBOR_FORCE_EXPLODE,
};

enum BlobPhysicsParamType_t
{
	BLOB_PHYSICS_PARAM_GLOBAL_VISCOSITY = 0,

	BLOB_PHYSICS_PARAM_NEIGHBOR_VISCOSITY,
	BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_SCALE,
	BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_TOUCH,
	BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_NEAR,
	BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_STRAIN,
	BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_BOND_BREAK,
	BLOB_PHYSICS_PARAM_NEIGHBOR_DIST_MAX,

	BLOB_PHYSICS_PARAM_NEIGHBOR_BOND_PARAM_START,
	BLOB_PHYSICS_PARAM_NEIGHBOR_ATTRACTION_MAX = BLOB_PHYSICS_PARAM_NEIGHBOR_BOND_PARAM_START,
	BLOB_PHYSICS_PARAM_NEIGHBOR_REPULSION_MAX,
	BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_SURFACE_TENSION,
	BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_ATTRACTION,
	BLOB_PHYSICS_PARAM_NEIGHBOR_LJ_REPULSION,
	BLOB_PHYSICS_PARAM_NEIGHBOR_LINEAR_ATTRACTION,
	BLOB_PHYSICS_PARAM_NEIGHBOR_LINEAR_REPULSION,

	BLOB_PHYSICS_PARAM_PGOAL_BOND_PARAM_START,
	BLOB_PHYSICS_PARAM_PGOAL_ATTRACTION_MAX = BLOB_PHYSICS_PARAM_PGOAL_BOND_PARAM_START,
	BLOB_PHYSICS_PARAM_PGOAL_REPULSION_MAX,
	BLOB_PHYSICS_PARAM_PGOAL_LJ_SURFACE_TENSION,
	BLOB_PHYSICS_PARAM_PGOAL_LJ_ATTRACTION,
	BLOB_PHYSICS_PARAM_PGOAL_LJ_REPULSION,
	BLOB_PHYSICS_PARAM_PGOAL_LINEAR_ATTRACTION,
	BLOB_PHYSICS_PARAM_PGOAL_LINEAR_REPULSION,

	BLOB_PHYSICS_PARAM_MAX_SPEED_TOWARDS_GOAL,
	BLOB_PHYSICS_PARAM_TIME_TO_MAX_SPEED_TOWARDS_GOAL,
	BLOB_PHYSICS_PARAM_FORCE_TOWARDS_GOAL_CONVERGENCE,
	BLOB_PHYSICS_PARAM_TOWARDS_GOAL_BRAKE_TIME,

	BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_ROOT_PARTICLES,
	BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_CORE_PARTICLES,
	BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_STICKY_PARTICLES,
	BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_SKIN_PARTICLES,
	BLOB_PHYSICS_PARAM_STICKY_FORCE_FOR_TENTACLE_PARTICLES,

	BLOB_PHYSICS_PARAM_BRAIN_NEIGHBOR_VISCOSITY,

	BLOB_PHYSICS_PARAM_BRAIN_BOND_PARAM_START,
	BLOB_PHYSICS_PARAM_BRAIN_ATTRACTION_MAX = BLOB_PHYSICS_PARAM_BRAIN_BOND_PARAM_START,
	BLOB_PHYSICS_PARAM_BRAIN_REPULSION_MAX,
	BLOB_PHYSICS_PARAM_BRAIN_LJ_SURFACE_TENSION,
	BLOB_PHYSICS_PARAM_BRAIN_LJ_ATTRACTION,
	BLOB_PHYSICS_PARAM_BRAIN_LJ_REPULSION,
	BLOB_PHYSICS_PARAM_BRAIN_LINEAR_ATTRACTION,
	BLOB_PHYSICS_PARAM_BRAIN_LINEAR_REPULSION,

	BLOB_PHYSICS_PARAM_TOTAL
};

struct BlobPhysicsParamState_t;

struct BlobPipeParticle_t
{
	CHandle<CFunc_Pipe_Entrance> hPipeExit;
	float fArrivalTime;
};

struct BlobShapeTarget_t
{
	BlobShapeTarget_t( void )
	 :	m_ShapeTargetName( MAKE_STRING( NULL ) ),
		m_hBaseAnimating( NULL ),
		m_vDirection( 0, 0, 0 ),
		m_bUseTargetPositionAngles( false ),
		m_bDisableCollision( false ) {}

	string_t					m_ShapeTargetName;
	CHandle<CBaseAnimating>		m_hBaseAnimating;
	Vector						m_vDirection;
	bool						m_bUseTargetPositionAngles;
	bool						m_bDisableCollision;

	DECLARE_SIMPLE_DATADESC();
};

struct BlobCapturedGrenade_t
{
	BlobCapturedGrenade_t( void ) :	m_hGrenade( NULL ),	m_fCaptureTime( 0 )	{}
	BlobCapturedGrenade_t( CBaseEntity *pGrenade ) : m_hGrenade( pGrenade ), m_fCaptureTime( gpGlobals->curtime ) {}
	EHANDLE	m_hGrenade;
	float	m_fCaptureTime;

	DECLARE_SIMPLE_DATADESC();
};

struct BlobRootDistSortKey
{
	CBlobParticle *particle;
	float distToRoot;
};


class CNPC_Blob : public CNPC_Surface
{
	DECLARE_CLASS( CNPC_Blob, CNPC_Surface );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	DEFINE_CUSTOM_AI;

public:
	CNPC_Blob( void );
	virtual ~CNPC_Blob( void );

	virtual void	Precache( void );
	virtual void	CreateSounds( void );
	virtual void	StopLoopingSounds( void );
	virtual void	Spawn( void );
	virtual void	UpdateOnRemove();
	virtual void	Activate( void );
	virtual void	OnSave( IEntitySaveUtils *pUtils );
	virtual void	OnRestore( void );
	virtual int		Save( ISave &save );
	virtual int		Restore( IRestore &restore );
	virtual void	DrawDebugGeometryOverlays( void );
	virtual bool	FValidateHintType( CAI_Hint *pHint );
	virtual void	PainSound( const CTakeDamageInfo &info );
	virtual void	VPhysicsCollision( int index, gamevcollisionevent_t *pEvent );

	virtual int		GetSoundInterests( void );
	virtual bool	ShouldIgnoreSound( CSound *pSound );
	virtual Vector	EyePosition( void );
	virtual bool	FCanCheckAttacks( void );

	virtual void	CreateBlobPhysics();
	virtual int		OnTakeDamage_Alive( const CTakeDamageInfo &info );
	virtual Vector	BodyTarget( const Vector &posSrc, bool bNoisy );
	//virtual void PostPhysFrame(float deltaTime);

	// NPCs can override this to tweak with how costly particular movements are
	virtual	bool	MovementCost( int moveType, const Vector &vecStart, const Vector &vecEnd, float *pCost );

	virtual void	RunAI( void );
	virtual void	Touch( CBaseEntity *pOther );

	virtual void	ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs );
	virtual bool	TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );

	CBlobParticle* CreateParticle( const Vector &pos, const Vector &velocity, float mass = 10.0f);
	void		DestroyParticle(CBlobParticle* p);
	void		ActivateParticle(CBlobGroup* g, CBlobParticle* p);
	void		DeactivateParticle(CBlobParticle* p);
	void		TeleportParticle(CBlobParticle* p, const Vector &newPos, const Vector *pNewVel = NULL );
	void		TransferGroupToDifferentEntity(CBlobGroup* group, CNPC_Blob* other);
	void		DebugSanityCheckGroups();
	void		DebugSanityCheckVPhysics( void );
	void		DebugFullSanityCheck( void );

	virtual void	VPhysicsUpdate( IPhysicsObject *pPhysics );

	void		OrganizeParticleStructure(CBlobGroup* group); //called after particle counts have changed
	void		ImproveGroupTrees();

	// Simple helper functions used by particle physics
	FORCEINLINE bool IsObstacleBetwParticles(CBlobParticle* p1, CBlobParticle* p2);
	bool		IsChild(CBlobParticle* possibleParent, CBlobParticle* possibleChild);
	CBlobParticle* GetParticleRoot( CBlobParticle* p );
	float GetDistanceToParticleRoot( CBlobParticle* p );
	bool IsDisconnectedFromRoot( CBlobParticle *pParticle, float bondBreakDistSqr );

	// Input handlers
	void InputMoveToPosition( inputdata_t &inputdata );
	void InputTestSplit( inputdata_t &inputdata );
	void InputUnseenTeleport( inputdata_t &inputdata );
	void InputOverloadPhysParamState( inputdata_t &inputdata );
	void InputRevertPhysParamState( inputdata_t &inputdata );
	void InputForceThroughPipe( inputdata_t &inputdata );
	void InputAddShapeTarget( inputdata_t &inputdata );
	void InputRemoveShapeTarget( inputdata_t &inputdata );
	void InputTentacleAttackEntity( inputdata_t &inputdata );
	void InputEnableShakeBlobs( inputdata_t &inputdata );
	void InputEnableTentacleAttack( inputdata_t &inputdata );
	void InputEnableSpit( inputdata_t &inputdata );
	void InputEnableLeap( inputdata_t &inputdata );
	void InputEnableRegeneration( inputdata_t &inputdata );
	void InputEnableSpawnRegenerators( inputdata_t &inputdata );
	void InputRegenerateParticles( inputdata_t &inputdata );

	// Attack
	void MeleeAttack( float distance, float damage, QAngle& viewPunch, Vector& shove );
	bool ComputeMeleeAttackPosition( CBlobParticle **ppTentacleRootParticle, Vector *pAttackPosition );

	void	ForceTransferThroughPipe( CFunc_Pipe_Entrance *pEntrance, CFunc_Pipe_Entrance *pExit );

	void SetPhysParamState( BlobPhysicsParamState_t *pPhysParamState );
	void SetPhysParamState( const char *pchName );
	void SetPhysParamState( int iStateID );

	// Regenerate new particles
	void RegenerateParticles( float flFraction );

	// CBaseAnimating shape targets
	void AddShapeTarget( CBaseAnimating *pBaseAnimating, Vector vDirection, bool bUseTargetPoistionAngles = false );
	bool HasShapeTarget( CBaseAnimating *pBaseAnimating );
	void RemoveShapeTarget( CBaseAnimating *pBaseAnimating );
	// CShapeTarget shape targets
	void AddShapeTarget( CShapeTarget *pShapeTarget, bool bClearExistingTargets );
	bool HasShapeTarget( CShapeTarget *pShapeTarget );
	void RemoveShapeTarget( CShapeTarget *pShapeTarget );


	int GetBlobSize( void ) const;

	virtual void	NPCThink( void );
	virtual float	MaxYawSpeed( void );
	virtual bool	IsJumpLegal( const Vector &startPos, const Vector &apex, const Vector &endPos ) const;
	virtual void	HandleAnimEvent( animevent_t *pEvent );
	virtual void	BuildScheduleTestBits( void );
	virtual void	GatherConditions( void );
	virtual int		SelectSchedule( void );
	virtual void	OnScheduleChange( void );
	virtual void	StartTask( const Task_t *pTask );
	virtual void	RunTask( const Task_t *pTask );
	virtual bool	IsCurTaskContinuousMove( void );
	virtual void	Event_Killed( const CTakeDamageInfo &info );
	virtual	bool	IsHeavyDamage( const CTakeDamageInfo &info );
	virtual bool	ShouldAlwaysThink();

	virtual bool	ShouldCollide( int collisionGroup, int contentsMask ) const;
	virtual unsigned int PhysicsSolidMaskForEntity( void ) const;

	static CNPC_Blob *CreateBlob( int iStartingParticleCount = 0 ); //helps recycle dormant blobs to use this
	void AddParticles( int iAddCount, IPhysicsObject **pUseExisting = NULL, bool bCopyOtherDataFromSourceBlobs = false );
	void RemoveParticles( int iRemoveCount, int *pRemoveIndices = NULL, bool bDeletePhysics = true ); //if pRemoveIndices is provided, those specific particles will be removed. Otherwise we just remove particles at the end of our arrays

	CNPC_Blob *SplitBlob( int iNumParticlesToTake, int *pRemoveIndices );
	CNPC_Blob *SplitBlob( Vector vNewBlobSplitDirection, int iNumParticlesToTake );

	static CNPC_Blob *SpawnUnseenBlob( int iParticleCount );
	void TeleportToUnseenLocation( void );
	void TeleportNPCToParticles( void );

	virtual bool OnlySeeAliveEntities( void ) { return false; }

	// When it's mostly brain, new behaviors emerge!
	bool IsMostlyBrain() const;

private:
	struct BondForceInfo_t
	{
		float m_flTouchDistance;
		float m_flSurfaceTension;
		float m_flLennardJonesAttraction;
		float m_flLennardJonesRepulsion;
		float m_flLinearAttraction;
		float m_flLinearRepulsion;
		float m_flAttractionMax;
		float m_flRepulsionMax;
	};

	struct HintedPath_t
	{
		Vector m_vecStartPos;
		Vector m_vecEndPos;
		float m_flLength;
	};

	// Particle Physics Routines
	void		DoPhysics();
	void		ChooseGroupGoal(CBlobGroup* group);
	void		MoveTowardsGoal(CBlobGroup* group);
	void		UpdateParticleGoal(CBlobGroup* group);
	void		SetupBondForceInfo( BondForceInfo_t *pInfo, int nFirstStateVar );
	float		ComputeBondForce( const CBlobParticle *b1, const CBlobParticle *b2, float flDist, const BondForceInfo_t& info );
	void		ApplyParticleGoalForces(CBlobGroup* group);
	void		ApplyNeighborForces(CBlobGroup* group);
	void		ApplyGravity(CBlobGroup* group);
	void		ApplyRandomForce(CBlobGroup* group);
	void		ApplyNeighborViscosity(CBlobGroup* group);
	void		ApplyGlobalViscosity(CBlobGroup* group);
	void		ApplyStickyForce(CBlobGroup* group);
	void		ApplySpitForces( CBlobGroup* pGroup );
	void		ApplyBrainForces( void );
	void		ApplyLeapForces( CBlobGroup* pGroup );
	void		ApplyImpulse( void );

	int	BuildFireSoundList( );
	void GetFireRepelTargetForce( int iSound, CBlobParticle *pParticle, Vector *pForce );

	int SelectEnemySchedule( void );
	int SelectLeaperSchedule( void );
	int SelectSpitSchedule( void );

	// Methods related to gathering conditions
	void GatherPipeShortcutConditions( void );
	void GatherBuddyBlobConditions();
	void GatherShakeBlobConditions();
	void GatherRegenerateParticlesConditions();
	void GatherLeapConditions();

	bool SetPathToRegenerator();

	void AddPipeEntrancesToSenses( void );
	
	void UpdateParticleFlags();
	void MarkParticlesCloseToPosition( const Vector& pos, BlobParticleFlags_t flag );
	void MarkParticlesAsStragglers( void );
	// Utility method for MarkParticlesAsStragglers, finds the closest particle to a point
	// (optionally ignores particles with given flags)
	CBlobParticle *FindClosestParticleToPoint( const Vector &target, int ignoreFlags ) const;
	// Utility method for UpdateParticleFlags, returns a list of particles sorted by proximity to a point (closest first)
	void GetParticlesSortedByProximity( CBlobParticle **pSortedParticles, const Vector &sortCentre );
	void GetParticlesSortedByProximity( CBlobParticle **pSortedParticles, CBlobGroup *pGroup, const Vector &sortCentre );
	// Utility method for OnTakeDamage_Alive, takes a list of particles and returns indices
	// (blob, not group indices) of those which lie within a given sphere
	int ClosestBlobParticleIndices( const Vector &vTestPosition, CBlobParticle **pSearchArray, int iArrayCount, int *piIndicies, int iMaxIndicies, float fMaxDistance );

	// Creates brain particles
	void CreateBrain();
	// This figures out how exposed the brain is (how much it is covered by non-brain particles). When exposed, it is damageable by the player.
	void ComputeBrainExposure( void );
	// Get a list of particles flagged with BLOB_PARTICLE_BRAIN.
	// Returns the number of particles (and the the group with the most brain particles)
	int GetBrainParticles( CBlobParticle **pBrainParticles, CBlobGroup * &pBrainGroup );
	// Get the brain's position+velocity
	void ComputeBrainPosition( Vector &brainPosition, Vector &brainVelocity );
	// Count how many particles are connected to the brain (including the brain particles)
	int GetNumParticlesConnectedToBrain( int flagsToIgnore );
	bool HasBrain() const;

	// Updates the entity's physics object to always be the first entry in m_vecPhysParticles
	void UpdateEntityPhysicsObject( void );

	// Apply force to the blob particles when taking damage
	void ApplyDamageForce( const CTakeDamageInfo &info );
	void ApplyBlastForceToParticles( const Vector &vecEpicenter, float flForceSize, float flForceRadius );

	// After certain types of damage, we act stunned 	
	// (slowly gather particles together, but nothing else)
	void EnterStunnedState( void );
	float GetStunFraction() const;
	bool IsStunned( void ) const;

	void RecenterAbsOrigin( );

	// Freeze methods
	void FreezeParticle( CBlobParticle *pParticle );
	void UnfreezeParticle( CBlobParticle *pParticle );

	// This returns a (hopefully) reachable location near the target entity (e.g. on its bounding box)
	bool ComputeTargetPosition( Vector &result, const CBaseEntity *pTarget, const Vector &targetOffset = vec3_origin, bool bDrawDebugLines = false );
	// Utility used by ComputeTargetPosition to trace blob-passable spaces
	float BlobHullTrace( const Vector &vStart, const Vector &vEnd, const CBaseEntity *pIgnore );
	float BlobParticleWorldTrace( const Vector &vStart, const Vector &vEnd );

	// Private SHAPE TARGET methods
	void DoEnvelope( const CUtlVector<ShapeEdge_t> &edgeList, int &nStartParticle, int nArm, bool bDisableCollision );
	void DoEnvelopeOld( const BlobShapeTarget_t *pBlobShapeTarget, int &nStartParticle );
	void GetShapeTargetEdgeListFromBaseAnimating( CBaseAnimating *pBaseAnimating, CUtlVector<ShapeEdge_t> &edgeList, bool bConvertToLocalSpace );
	void GetShapeTargetEdgeList( const BlobShapeTarget_t *pBlobShapeTarget, CUtlVector<ShapeEdge_t> &edgeList );
	void GetShapeTargetAttachmentTransform( const BlobShapeTarget_t *pBlobShapeTarget, matrix3x4_t &mBlobToWorld );
	void InputShapeTarget_Inner( inputdata_t &inputdata, bool bRemove );
	void ApplyRepelTargetForces( CBlobGroup *pGroup );
	void TriggerRepelTargets( void );
	void PurgeDeadShapeTargets( void );
	void RemoveShapeTarget( CBaseEntity *pBaseEntity );

	// Private TENTACLE methods
	CBlobTentacle *TentacleCreate( EHANDLE target, BlobTentacleAction action, CBlobParticle *pTentacleRoot = NULL, const Vector &targetOffset = vec3_origin, float speedMultiplier = 1.0f );
	void	TentacleDestroy( CBlobTentacle *tentacle );
	void	TentacleCreateParticles( CBlobTentacle &tentacle );
	void	TentacleDestroyParticles( CBlobTentacle &tentacle );
	void	TentacleProcessAnimation( CBlobTentacle &tentacle );
	void	TentacleProcessAnimation_Authored( CBlobTentacle &tentacle, int maxParticles, Vector *positions, Vector *velocities, float *radii );
	void	TentacleProcessAnimation_Procedural( CBlobTentacle &tentacle, int maxParticles, Vector *positions, Vector *velocities, float *radii );
	void	TentacleProcessAnimation_Hinted( CBlobTentacle &tentacle, int maxParticles, Vector *positions, Vector *velocities, float *radii );
	Vector	TentacleComputeTargetPosition( CBlobTentacle &tentacle, float predictionTime );
	float	TentacleGetPathDistanceToTarget( EHANDLE hTarget, Vector targetOffset = vec3_origin );
	bool	TentacleMeleeAttack( CBlobTentacle &tentacle );
	void	TentacleUpdateState( CBlobTentacle &tentacle );
	void	TentaclesInterrupt( void );
	void	TentacleInterrupt( CBlobTentacle &tentacle );
	void	StalkUpdateTentacles( void );
	bool	IsBigEnoughToCreateNewTentacle( void );

	// Private STALKING methods
	void StalkUpdateState( void );					// Called each AI update when stalking, updates m_vecStalkGoal and switches between stalking states (e.g. start a lunge)
	bool StalkGoalUpdate( void );					// Called from StalkUpdateState
	void StalkInitiateLunge( void );				// Called from StalkUpdateState

	// Apply forces so grenades get captured inside the blob (makes them a more effective weapon)
	void ProcessCapturedGrenades( void );
	bool HasGrenade( CBaseEntity *pGrenade );

	// Causes the ai motor code to be skipped entirely
	virtual bool OverrideMove( float flInterval ) { return true; }

	// Spawns a tiny blob, returns false if it couldn't
	bool SpawnTinyBlob( CBlobGroup *pGroup );

	// Methods related to spitting
	Vector VecCheckThrowTolerance( CBaseEntity *pEdict, const Vector &vecSpot1, Vector vecSpot2, float flSpeed, float flTolerance );
	virtual bool InnateWeaponLOSCondition( const Vector &ownerPos, const Vector &targetPos, bool bSetConditions );
	virtual int RangeAttack1Conditions( float flDot, float flDist );
	virtual int MeleeAttack1Conditions( float flDot, float flDist );
	int ComputeHintedPathNodeCount( CBlobTentacle &tentacle ) const;
	int ComputeHintedPath( HintedPath_t *pSpitPath, float *pTotalLength, CBlobTentacle &tentacle, float flFraction ) const;

	virtual float	InnateRange1MinRange( void ) { return 128; }
	virtual float	InnateRange1MaxRange( void ) { return 150*12; }
	bool GetSpitVector( const Vector &vecStartPos, const Vector &vecTarget, Vector *vecOut );
	bool DoSpit();
	bool SeenEnemyWithinTime( float flTime );
	void DoLeap();
	void ChangeColor( const Vector &vecColor, float flDuration );

	void ComputeGroupCenter( Vector *pCenter, CBlobGroup *pGroup );
	void ComputeMainGroupCenter( Vector *pCenter );

	bool GroupContainsBrain( CBlobGroup *pGroup );
	void UpdatePipeParticles();
	float ComputeBrainParticleRatio() const;
	int GetNormalParticleCount() const;
	void Merge( CNPC_Blob *pOther );

	// Update dying particles
	void UpdateDyingParticles( void );
	// Retire dying particles
	void RetireDyingParticles( void );
	// Update regenerating particles
	void UpdateRegeneratingParticles( void );
	// Make regenerating particles into regular ones, once fully grown
	void RetireRegeneratingParticles( void );

	// When damage is taken, and it's time to emit particles
	void EmitNonBrainParticles( const CTakeDamageInfo &info );

	// Methods related to navigation
	void InitParticleNavigation( );
	void SetupParticleNavigation( CBlobParticle *pParticle );
	void UpdateParticleNavigation( CBlobParticle *pParticle );

	// Compute clipped movement direction, return blockage amount
	float ClipMovementToContactSurfaces( CBlobParticle* pParticle, Vector &vecMoveDelta );
	bool ClipRayToContactSurfaces( CBlobParticle* pParticle, Vector &vecStart, Vector &vecEnd );

	// Used for navigation, has the blob reached its waypoint?
	bool HasBlobReachedPoint( const Vector &vecOrigin, float flTolerance ) const;

	// Builds an axis-aligned bounding box around all particles from all groups
	void BuildAABB( Vector *pMins, Vector *pMaxs, float flRadius );

	// Initializes random vectors
	static void InitializeRandomVectors();

	// Methods related to neighbors

	// Computes the distance at which particles care about neighbor particles
	float ComputeNeighborInteractionDistance( );
	void FindNeighbors();
	void SortNeighbors();

	// Methods related to contact

	void FindContactPoints( CBlobGroup* group );

	// Computes contact points and average contact normal for a particle
	void ComputeAverageContactPoint( CBlobParticle* pParticle );

	// For particles that are not in contact, distribute surface directions from nearby particles.
	void ComputeContactSurfaceFromNeighbors( CBlobParticle* pParticle );

	// Selects the best contact surface for this particle
	void SelectBestContactSurface( CBlobParticle* pParticle );

	// Methods related to support
	void UpdateSupport( CBlobGroup* pGroup );
	void BlurSupportAcrossNearbyParticles( CBlobGroup* pGroup, float flBlurRadius );

	// Computes the number of particle groups in addition to the brain we have
	int ComputeDamageGroupCount() const;

	// Methods related to debug visualization
	void DrawDebugVisualizations( CBlobGroup* pGroup );
	void DrawDebugContactPoints( CBlobGroup* pGroup );
	void DrawDebugSupport( CBlobGroup* pGroup );
	void DrawDebugGoals( CBlobGroup* pGroup );
	void DrawDebugForces( CBlobGroup* pGroup );

	void SwipeDamage( CBlobTentacle &tentacle, const Vector &vecStart, const Vector &vecEnd, const Vector &vecTentacleVelocity );
	void ApplyTentacleDamage( CBlobTentacle &tentacle, CBaseEntity *pHurt, int iDamage, const Vector &vecDamagePos, const Vector &vecVelocityPunch );

	// Can we spit from a nearby tentacle spit position?
	bool CheckTentacleSpitLOS( const Vector &ownerPos, const Vector &targetPos );

	void UpdateTentacleSpitPos( CBlobTentacle &tentacle );
	void UpdateTentacleSpitPos_Hinted( CBlobTentacle &tentacle );
	void ComputeNearbySpitTargets( );

	void SetMoveGoal( const Vector &vecGoal );

	// Disables nearby cover hints
	void DisableNearbyBrainHints( float flRadius, float flDisableTime );

	// Is our current path blocked by fire?
	bool IsCurPathBlockedByFire();

	void ComputeRegeneratorRepelForce( CBlobParticle *pParticle, int nCount, CBlobRegenerator **ppRegenerator, Vector *pForce );

	void AddRepulsionForce( CBlobParticle *pParticle, const Vector &vecCenter, float flRadius, 
		float dt, float flVertFactor, float maxSpeedTowardsGoal, float fMaxRepelAccel, Vector *pForce );

	// Update base color
	void UpdateBaseColor();

public:
	CUtlVector<BlobShapeTarget_t>			m_ShapeTargets;
	bool		m_bPhysParamStateOverloaded;
	int			m_iBlobPhysParamState;
	float		m_pfBlobPhysParamState[ BLOB_PHYSICS_PARAM_TOTAL ];

private:
	enum BlobType_t
	{
		BLOB_TYPE_GENERIC = 0,
		BLOB_TYPE_LEAPER,
		BLOB_TYPE_SPITTER,
		BLOB_TYPE_SPAWNER,

		BLOB_TYPE_COUNT
	};

	static const int BLOB_NUM_TENTACLE_PARTICLES = 48;
	static const int BLOB_MAX_TENTACLES = 8;

	float	m_fLastAverageParticleNeighbors;
	float	m_fLastInteractionScale;

	float	m_fSpawnTimeStamp;
	float	m_fLastSeenTime;

	CUtlVector<CBlobParticle*>	m_blobParticles;
	CUtlVector<CBlobGroup*>		m_blobGroups;

	CBlobGroup		*m_pMainGroup;
	int				m_iCacheNumLargeGroups;

	// These vectors are only used by the blob grouping algorithm
	// outside of the algorithm, they should be empty.
	CUtlVector<CBlobParticle*> unGroupedParticles;
	CUtlVector<CBlobGroup*> unUpdatedGroups;
	CUtlVector<CBlobParticle*> recursionStack;
	
	#define BLOB_NUM_RAND_VECTORS 64
	static bool s_bInitializedRandVectors;
	static Vector s_pRandVectors[BLOB_NUM_RAND_VECTORS];
	int m_randVectorIndex;

	// Network variables
	CUtlVector< int > m_vecTypes;
	CUtlVector< uint16 > m_vecParents;
	CUtlVector< Vector > m_vecUVW;
	CUtlVector< uint16 > m_iFlags;	// per-particle flags
   
	CUtlVector< EHANDLE > m_hNearbySpitTargets;

	BlobType_t	m_BlobType;
	Vector		m_BaseColor;
	Vector		m_vecMoveGoal;
	Vector		m_vecLastMoveGoal;
	Vector		m_vecLastPosition;
	float		m_flFirstMoveBlockTime;
	bool		m_bIsMoveBlocked;
	bool		m_bPlayedBlockedSound;
	int			m_iDangerSounds;
	float		m_flLastPainSoundTime;
	float		m_flLastDamageEffectTime;
	float		m_flMaxArmorHealth;
	float		m_flArmorHealth;
	float		m_flNextSpawnRegeneratorTime;

	// Members related to TENTACLES
	CBlobTentacle				m_tentacles[BLOB_MAX_TENTACLES];// This blob's current tentacles (may not all be active)
	float						m_fTentacleTimeout;				// When we can next do a tentacle attack
	float						m_fTentacleDamageTimeout;		// The next time the blob can score damage with a tentacle attack
																// (until then, tentacle hits score quarter damage)
	float						m_fVulnerableTimeout;			// When the blob attacks and misses with a tentacle, it is momentarily
																// vulnerable to grenade attacks (this is the timeout for that state)

	float						m_fLastDamagedPlayer;			// Used so we can damage the player at a fixed rate
	Vector						m_vecParticleImpulse;			// Used to apply one-off impulse to the blob particles
	Vector						m_vecParticleImpulseSpread;		// Added to m_vecParticleImpulse, multiplied by a per-particle random Vector( [-1,+1] )
	float						m_flLastNeighborOutwardForceFactor;
	float						m_flLastNeighborInwardForceFactor;
	NeighborForceMode_t			m_nNeighborForceMode;

	// Members related to BRAINS
	int							m_nBrainParticleCount;			// Whether this blob has a coherent core of brain particles
	CNetworkVar( float, 		m_fBrainExposure );				// How 'exposed' is this brain? (no other particles covering it)
	IPhysicsConstraintGroup		*m_pBrainConstraintGroup;

	// Members related to blob regeneration
	int							m_nMaxParticleCount;

	// Non-brain health
	float						m_flHealthUntilParticlesRemoved;	// Health needed to be taken to remove the next chunk of particles

	float						m_fStunStartTime;				// At what time did I start being stunned

	CUtlVector<BlobCapturedGrenade_t>	m_capturedGrenades;		// Live grenades that the blob has captured (and may spit back out)

	float						m_fBlockMergeUntilTime; 		// Hackish way to prevent a split blob from immediately re-merging

	COutputEvent 				m_BlobSplitOutput;
	COutputEvent 				m_BlobFrozenOutput;
	COutputEvent 				m_BlobUnfrozenOutput;
	COutputEvent 				m_OnBlobsShakenOff;
	COutputEvent 				m_OnBrainExposed;
	COutputEvent 				m_OnBrainHidden;
	COutputEvent 				m_OnThreeQuartersArmor;

	CHandle<CFunc_Pipe_Entrance> m_hPipeEntrance;
	CHandle<CFunc_Pipe_Entrance> m_hPipeExit;
	CHandle<CNPC_Blob>			m_hPipeExitBlob;
	CUtlVector<BlobPipeParticle_t> m_QueuedPipeParticles;		// Unsorted particles going through a pipe to be added to this blob.
	bool						m_bIsPipeTransferBlob;
	bool						m_bForcePipeTransfer;

	// Members related to the current TARGET
	float						m_fMinDistToTarget;				// Distance to the current target (zero if at the target or no target)
	float						m_fLastNearerToTargetTime;		// Last time m_fMinDistToTarget decreased
	Vector						m_vClingPosition;
	float						m_fClingTimeOut;

	// Members related to SCHED_BLOB_STALK_TARGET
	BlobStalkTargetState_t		m_eBlobStalkState;				// Current stalking state (see BlobStalkTargetState_t)
	EHANDLE						m_hStalkTarget;					// Currently stalked entity
	EHANDLE						m_hNextStalkTarget;				// Entity to stalk next time we start stalking
	bool						m_bInputStalkTarget;			// The entity has been told to stalk m_hNextStalkTarget via an input
	Vector						m_vecStalkGoal;					// Where the stalking blob is headed (used in RunTask->TASK_BLOB_GET_STALK_TARGET_PATH)
	float						m_fLastStalkGoalUpdate;			// Last time m_vecStalkGoal was updated
	float						m_fLastStalkLungeTime;			// Last time the blob lunged while stalking
	bool						m_bStalkDefensive;				// Whether the current stalking is defensive (vs offensive)
	bool						m_bNextStalkDefensive;			// Whether the next stalk should be defensive (vs offensive)
	int							m_nStalkCreatedTentacles;		// The number of tentacles created by this the current stalk schedule
	AI_Waypoint_t *				m_PrevBestWaypoint;				// Used to avoid infinite loops in path selection (see TASK_WAIT_FOR_STALK_MOVEMENT)

	// Members related to TASK_BLOB_SHAKE_OFF_BLOBS
	float						m_flNextShakeBlobTime;
	float						m_flLastBlobSpawnTime;
	int							m_nBlobsToShakeOff;
	float						m_flNextBlobShakeSpawnTime;
	bool						m_bShakeFastBlobs;
	CUtlVector< EHANDLE >		m_hSpawnedBlobs;

	// Members related to TASK_BLOB_REGENERATE_PARTICLES
	float						m_flNextRegenerateTime;

	// Members related to LEAPING
	float						m_flNextLeapTime;
	float						m_flLastLeapTime;
	Vector						m_vecLeapTarget;
	float						m_flLeapStartTime;
	bool						m_bLeapCompleted;
	bool						m_bSuppressMoveTowardGoal;
	bool						m_bSuppressStragglerCreation;

	// Members related to color change
	CNetworkVar( float,			m_flColorChangeStartTime );
	CNetworkVar( float,			m_flColorChangeEndTime );
	CNetworkVector(				m_vecStartColor );
	CNetworkVector(				m_vecEndColor );

	// Members related to SPITTING
	int							m_nSpitCount;
	Vector						m_vecSaveSpitTarget;
	Vector						m_vecSaveSpitVelocity;
	EHANDLE						m_hSaveSpitTentaclePos;
	bool						m_bSaveSpitUseTentacle;
	EHANDLE						m_hSpitTentaclePos;
	bool						m_bSpitUseTentacle;
	CBlobTentacle				*m_pSpitTentacle;
	EHANDLE						m_hSpitVisibilityTestEyePos;

	bool						m_bCanSpawnRegenerators;
	bool						m_bCanShakeBlobs;
	bool						m_bCanTentacleAttack;
	bool						m_bCanSpit;
	bool						m_bCanRegenerate;
	bool						m_bStartAsBrain;
	bool						m_bCanMerge;
	bool						m_bCanLeap;

	CNetworkVar( CHandle<CNPC_Blob>, m_hAutoSplitBlob );
	float						m_fAutoSplitTimeOut;

	CHandle<CNPC_Blob>			m_hBuddyBlob;
	CSoundPatch					*m_pAmbientSound;
	CHandle<CBlobRegenerator>	m_hRegeneratorTarget;
	CNetworkVar( bool,			m_bFrozen );
	float						m_fUnfreezeTime;
};


#endif // NPC_BLOB_H
