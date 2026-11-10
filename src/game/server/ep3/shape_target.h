//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: Targets which, in combination, specify shapes for the blob/swarm to follow.
//          Can also act as blob-repellants.
//
//=============================================================================//

#ifndef	SHAPE_TARGET_H
#define	SHAPE_TARGET_H

//-----------------------------------------------------------------------------
//
// CLASS: CShapeTarget
//
//-----------------------------------------------------------------------------

struct ShapeEdge_t
{
	Vector startPos, endPos;
	Vector startVel, endVel;
};

class CShapeTarget : public CBaseAnimating
{
	DECLARE_CLASS( CShapeTarget, CBaseAnimating );

public:

	CShapeTarget();
	~CShapeTarget();

	DECLARE_DATADESC();

	virtual void Precache( void );
	virtual bool CreateVPhysics( void );
	virtual void Spawn( void );
	virtual void StartTouch( CBaseEntity *pOther );
	virtual void DrawDebugGeometryOverlays( void );


	// Util func to shoot a shape target from a player's eyes:
	static CShapeTarget *ShootShapeTarget(	CBasePlayer *pPlayer, const char *shapeName,
											int shapeSize = DEFAULT_SHAPE_SIZE, int sequenceNumber = INVALID_SEQUENCE_NUMBER );
	// Util func to shoot a repel target from a player's eyes:
	static CShapeTarget *ShootRepelTarget(	CBasePlayer *pPlayer, float fRadius = INVALID_RADIUS, float fDuration = INVALID_REPEL_DURATION );

	// Is this entity tagged with a player-placed child shape target?
	// (bIsTaggedInMap is true if there's a child, Hammer-placed 'blob_trigger' shape target)
	static bool IsPlayerTaggedEntity( CBaseEntity *pEntity, bool *pIsTaggedInMap );

	// Temp methods for piggybacking off the weaponizer
	static float WeaponPrimary( CBasePlayer *pPlayer );
	static float WeaponSecondary( CBasePlayer *pPlayer );
	static void  WeaponReload( void );

	// Enabled targets are linked into a shape (chain of targets)
	void InputEnable( inputdata_t &inputdata );
	// Unlink an activated target from its shape (if the param is a bool 'false', fade it out like debris)
	void InputDisable( inputdata_t &inputdata );

	// Allow iteration along the targets in a shape
	const string_t &GetShapeName( void ) { return m_ShapeName; };
	static CShapeTarget * GetFirstTargetInShape( const string_t &shapeName );
	CShapeTarget * GetNextTarget( void );
	CShapeTarget * GetPrevTarget( void );
	void GetShapeEdgeList( CUtlVector<ShapeEdge_t> &edgeList, bool bConvertToLocalSpace );
	float GetRadius( void ) { return m_fRadius; };

	// Destroy all the targets in a given shape
	static void DestroyShape( const char *pShapeName );

	// "Show me the nearest tasty treat!", insists the blob (player-placed or non).
	static CShapeTarget *GetNearestBlobTarget( const Vector &searchPos, bool bGetPlayerPlacedTarget = false );

	// Called by a blob that says it's attaching to this entity.
	static void NotifyStartDeliberateTouchByBlob( CBaseEntity *pBlob, CBaseEntity *pEntity );
	// Called by a blob that says it's detaching from this entity (NULL if that entity was destroyed).
	static void NotifyEndDeliberateTouchByBlob( CBaseEntity *pBlob, CBaseEntity *pEntity );

	// Trigger any repel targets overlapping the given sphere (returns true if there are any)
	static bool TriggerRepelTargets( const Vector &vBlobOrigin, float fBlobRadius, bool bOnlyCheck );
	// Return a force vector (length between zero and one) due to active repel targets overlapping the given sphere
	static void GetRepelTargetForce( const Vector &vBlobOrigin, float fBlobRadius, Vector &vForce );
	// Returns FALSE if any active repel targets block a given AI node link
	static bool ValidateAINodeLink( const Vector &vStart, const Vector &vEnd );

	static const int INVALID_SHAPE_SIZE			= ~0;
	static const int DEFAULT_SHAPE_SIZE			= 4;
	static const int MAX_SHAPE_SIZE				= 64;
	static const int INVALID_SEQUENCE_NUMBER	= ~0;
	static const int INVALID_RADIUS				= -1;
	static const int DEFAULT_RADIUS				= 8;
	static const int INVALID_REPEL_DURATION		= -1;
	static const int INFINITE_REPEL_DURATION	= 0;
	static const int DEFAULT_REPEL_DURATION		= 5;
	static float	 PROJECTILE_PUSH_BACK_DIST;	// How far shape targets need to be from the world to be reachable by the blob
	static Vector	 PROJECTILE_DIMENSIONS;		// Size of a shape target's OBB when acting as a projectile

private:

	// Link up a new target with others based on their 'shape name'
	void LinkShapeTarget(	void );
	// Unlink a deactivated target from the other targets in its shape (optionally destroy the target)
	void UnlinkShapeTarget(	bool bExplode = false );
	// Check that this target was placed at a valid position, such that the blob can navigate to it
	void ValidateShapeTargetLocation( void );
	// Determine if this target can link to another (something blocking line-of-sight? too far apart?)
	bool ValidateShapeTargetLink( CShapeTarget *pOtherTarget );
	// Insert a target before this one in this shape chain
	void InsertTargetBefore( CShapeTarget *pShapeTarget );
	// Insert a target after this one in this shape chain
	void InsertTargetAfter(  CShapeTarget *pShapeTarget );

	// Start repelling the blob
	void ActivateRepulsion( void );
	// Stop repelling the blob
	void TerminateRepulsion( void );
	// Validate surface orientation for repel target placement
	bool ValidateRepelTargetPlacement( void );
	// Destroy existing repel targets overlapping a newly-placed target
	void DestroyOverlappingRepelTargets( void );
	// Called once the repulsion period expires
	void RepelEndThink( void );
	// Called once the target's trigger delay expires
	void RepelActivateThink( void );
	// Deferred call to check shape target placement
	void ValidateShapeTargetLocationThink( void );

	// Get rid of this shape target, in spectacular fasion!
	void Explode( void );
	// Sets up and returns the entity's VPhysics object as an OBB of the given size
	IPhysicsObject *InitVPhysics( const Vector &dimensions );
	// Stop moving (occurs on init, or on impact after flying through the world)
	void BecomeStationary( void );
	// Become a projectile, flying through the world
	void BecomeProjectile( void );
	// Become triggerable by the blob, via VPhysics touches
	void BecomeBlobTrigger( void );

	// Helper for GetNearestBlobTarget (does this target look tasty to a blob?)
	bool IsBlobTarget( void );

	// Helpers for Shoot[Shape|Repel]Target:
	static void ShootTarget( CBasePlayer *pPlayer, CShapeTarget *pShapeTarget );
	static const string_t &WeaponizerShapeTargetName( void );
	static const string_t &WeaponizerRepelTargetName( void );

	// Attach debugoverlay error text to this shape target
	void ErrorText( int type, const char *format, ... );


	// Outputs
	COutputEvent m_OnStartTouchByBlob;
	COutputEvent m_OnStartDeliberateTouchByBlob;
	COutputEvent m_OnEndDeliberateTouchByBlob;


	bool						m_bActive;						// Is this active? (part of a shape or repelling the blob)
	bool						m_bStartEnabled;				// To be activated on spawn (settable in Hammer)
	bool						m_bInFlight;					// Flying, will be activated on impact (Default: false)
	bool						m_bPendingThink;				// A think function is scheduled (so ignore touches)
	bool						m_bInvisible;					// No model, ignored by blob (useful for hidden blob-touch triggers)
	EHANDLE						m_hTouchingBlob;				// Set to the triggering blob when m_OnStartDeliberateTouchByBlob is fired,
																// cleared when m_OnEndDeliberateTouchByBlob is fired
	float						m_fRadius;						// Radius of influence for blob repulsion (if m_bRepel) or
																// for blob-touch triggering.

	string_t					m_ShapeName;					// CShapeTargets with the same shape name are part of the same shape
	int							m_nShapeSize;					// How many active targets should be in this shape?
	int							m_nSequenceNumber;				// The target's position relative to others in this shape
	CHandle<CShapeTarget>		m_hNextTarget;					// Chain to the next CShapeTarget in the shape
	CHandle<CShapeTarget>		m_hPrevTarget;					// Chain to the previous CShapeTarget in the shape

	bool						m_bRepel;						// This target repels blobs within a given radius, does not form Shapes
	float						m_fRepelDuration;				// How long this repels the blob
	float						m_fRepelEndTime;				// When this should finish repelling the blob

	EHANDLE						m_hErrorTextEnt;				// Debug error text attached to this shape target

	static CUtlVector<CShapeTarget *> g_Targets;				// All extant CShapeTargets
	static int					s_nLightningSprite;				// Repel target shockwave texture index
	static castable_string_t	s_WeaponizerShapeTargetName;	// The name of weaponizer shape targets
	static castable_string_t	s_WeaponizerRepelTargetName;	// The name of weaponizer repel targets
};

//-----------------------------------------------------------------------------

// IDBS: temp code, piggyback CShapeTarget off the weaponizer
extern ConVar shaper_use_weaponizer;
extern ConVar shaper_repel_force;
extern ConVar shaper_draw_debug;

//-----------------------------------------------------------------------------

#endif // SHAPE_TARGET_H
