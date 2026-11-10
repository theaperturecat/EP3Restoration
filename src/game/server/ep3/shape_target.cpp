//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ============//
//
// Purpose: See shape_target.h
//
//=============================================================================//

#include "cbase.h"

#include "datacache/imdlcache.h"
#include "particle_parse.h"

#include "collisionutils.h"

#include "beam_shared.h"
#include "te_effect_dispatch.h"
#include "hl2_player.h"

#include "shape_target.h"
#include "ep3/npc_blob.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

ConVar shaper_use_weaponizer(	"shaper_use_weaponizer",	"0",	FCVAR_CHEAT,
								"Force the weaponizer to shoot shape targets for guiding the blob ( 0 - off, 1 - primary fire places "
								"shape target chains, secondary fire places repel targets, 2 - primary fire places a target for the "
								"blob to follow, secondary fire cancels the current target" );
ConVar shaper_line_of_sight(	"shaper_line_of_sight",		"1",	FCVAR_CHEAT, "If enabled, shape targets only collide with line-of-sight-blocking surfaces." );
ConVar shaper_draw_debug(		"shaper_draw_debug",		"0",	FCVAR_CHEAT, "Display shaper target debug lines ( 0 - draw nothing, 1 - draw debug lines, 2 - draw debug lines+text )" );

ConVar shaper_repel_radius(		"shaper_repel_radius",		"128",	FCVAR_CHEAT, "The default radius of influence of a shaper repel target" );
ConVar shaper_repel_duration(	"shaper_repel_duration",	"5",	FCVAR_CHEAT, "The default duration of a shaper repel target" );
ConVar shaper_repel_delay(		"shaper_repel_delay",		"0",	FCVAR_CHEAT, "Delay on activation for shaper repel targets" );
ConVar shaper_repel_force(		"shaper_repel_force",		"1",	FCVAR_CHEAT, "Force applied to blob particles by shaper repel targets" );
ConVar shaper_repel_proximity(	"shaper_repel_proximity",	"0",	FCVAR_CHEAT, "Enable to have shaper repel targets triggered when a blob enters their region of influence" );
ConVar shaper_repel_overlap(	"shaper_repel_overlap",		"1",	FCVAR_CHEAT, "Proportion of overlap allowed between nearby shaper repel targets ( 0 - no overlap allowed, 1 - complete overlap allowed)" );
ConVar shaper_repel_horizontal(	"shaper_repel_horizontal",	"1",	FCVAR_CHEAT, "Shaper repel targets should only be placeable on horizontal surfaces, and should only apply horizontal forces to the blob ( 0 - any surface orientation is ok, 1 - only exactly horizontal surfaces are ok, 0.X - partially tilted surfaces are ok )" );

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

CUtlVector<CShapeTarget *> CShapeTarget::g_Targets;
int CShapeTarget::s_nLightningSprite = 0;
Vector CShapeTarget::PROJECTILE_DIMENSIONS( 1, 1, 1 );
float  CShapeTarget::PROJECTILE_PUSH_BACK_DIST = 10;
castable_string_t CShapeTarget::s_WeaponizerShapeTargetName( "weaponizer_shape" );
castable_string_t CShapeTarget::s_WeaponizerRepelTargetName( "weaponizer_repel" );


#define SHAPE_TARGET_MODEL "models/weapons/hunter_flechette.mdl"


LINK_ENTITY_TO_CLASS( shape_target,			CShapeTarget );
LINK_ENTITY_TO_CLASS( blob_shape_target,	CShapeTarget );
LINK_ENTITY_TO_CLASS( blob_trigger,			CShapeTarget );
LINK_ENTITY_TO_CLASS( blob_repeller,		CShapeTarget );
BEGIN_DATADESC( CShapeTarget )

	// Keys
	DEFINE_KEYFIELD(	m_bStartEnabled,	FIELD_BOOLEAN,	"startenabled" ),
	DEFINE_KEYFIELD(	m_bInvisible,		FIELD_BOOLEAN,	"invisible" ),
	DEFINE_KEYFIELD( 	m_fRadius,			FIELD_FLOAT,	"radius" ),

	DEFINE_KEYFIELD(	m_ShapeName, 		FIELD_STRING,	"shapename" ),
	DEFINE_KEYFIELD( 	m_nShapeSize,		FIELD_INTEGER,  "shapesize" ),
	DEFINE_KEYFIELD( 	m_nSequenceNumber,	FIELD_INTEGER,  "shapesequence" ),

	DEFINE_KEYFIELD( 	m_bRepel,			FIELD_BOOLEAN,  "repel" ),
	DEFINE_KEYFIELD( 	m_fRepelDuration,	FIELD_FLOAT,	"repelduration" ),

	// Non-key, saved members
	DEFINE_FIELD(		m_bActive,			FIELD_BOOLEAN ),
	DEFINE_FIELD( 		m_bInFlight,		FIELD_BOOLEAN ),

	DEFINE_FIELD( 		m_hNextTarget,		FIELD_EHANDLE ),
	DEFINE_FIELD( 		m_hPrevTarget,		FIELD_EHANDLE ),

	DEFINE_FIELD( 		m_fRepelEndTime,	FIELD_FLOAT ),

	// Inputs
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable",  InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable),

	// Outputs
	DEFINE_OUTPUT( m_OnStartTouchByBlob, "OnStartTouchByBlob" ),
	DEFINE_OUTPUT( m_OnStartDeliberateTouchByBlob, "OnStartDeliberateTouchByBlob" ),
	DEFINE_OUTPUT( m_OnEndDeliberateTouchByBlob, "OnEndDeliberateTouchByBlob" ),

	// Function Pointers
	DEFINE_FUNCTION( RepelEndThink ),
	DEFINE_FUNCTION( RepelActivateThink ),
	DEFINE_FUNCTION( ValidateShapeTargetLocationThink ),


END_DATADESC()


// TODO: when needed, implement a client class and add a server table (for now, C_BaseAnimating is enough)
//IMPLEMENT_SERVERCLASS_ST( CShapeTarget, DT_ShapeTarget )
//END_SEND_TABLE()


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// This class allows us to output ent text for errors that occur during map spawn and for entities which have died
class CErrorTextEnt : public CBaseEntity
{
	DECLARE_CLASS( CErrorTextEnt, CBaseEntity );

public:
	enum ErrorType_t
	{
		ERROR_POSITION = 0,		// Shape target is too close to/far from the nearest surface
		ERROR_LINE_OF_SIGHT,	// Shape target cannot be linked to the others in its shape due to line-of-sight blockage
		ERROR_NO_NAME,			// Shape target of type 'blob_shape_target' has to have a shape name
		ERROR_BAD_TYPE,			// Shape targets should be spawned as 'blob_shape_target', 'blob_trigger' or 'blog_repeller'
		ERROR_NUM_ERRORS
	};

	CErrorTextEnt( void )
	{
		memset( &( m_pErrors[ 0 ] ), 0, sizeof( m_pErrors ) );
	}

	~CErrorTextEnt( void )
	{
		for ( int i = 0; i < ERROR_NUM_ERRORS; i++ )
		{
			if ( m_pErrors[ i ] )
				delete m_pErrors[ i ];
		}
	}

	virtual void Spawn( void )
	{
		Precache();
		BaseClass::Spawn();
		AddEffects( EF_NODRAW );
		AddEffects( EF_NOSHADOW );
	}

	void AddErrorText( CBaseEntity *pSourceEnt, ErrorType_t errorType, const char *format, ... )
	{
		Assert( pSourceEnt );
		Assert( ( m_hSourceEnt == NULL ) || ( pSourceEnt == m_hSourceEnt ) );
		Assert( ( errorType >= 0 ) && ( errorType < ERROR_NUM_ERRORS ) );
		if ( pSourceEnt &&
			( ( m_hSourceEnt == NULL ) || ( pSourceEnt == m_hSourceEnt ) ) &&
			( ( errorType >= 0 ) && ( errorType < ERROR_NUM_ERRORS ) ) )
		{
			const int MAX_TEXT_SIZE = 496;
			m_pErrors[ errorType ] = new char[ MAX_TEXT_SIZE ];
			Assert( m_pErrors[ errorType ] );
			if ( m_pErrors[ errorType ] )
			{
				va_list m_argptr;
				va_start( m_argptr, format );
				Q_vsnprintf( m_pErrors[ errorType ], MAX_TEXT_SIZE, format, m_argptr );
				va_end( m_argptr );

				m_hSourceEnt = pSourceEnt;
				SetAbsOrigin( pSourceEnt->GetAbsOrigin() );

				// Gets DrawDebugGeometryOverlays called every tick:
				m_debugOverlays |= OVERLAY_PIVOT_BIT;
			}
		}
	}

	virtual void DrawDebugGeometryOverlays( void )
	{
		if ( shaper_draw_debug.GetBool() )
		{
			int offset = ( m_hSourceEnt.Get() && ( shaper_draw_debug.GetInt() > 1 ) ) ? 1 : 0;

			if ( m_hSourceEnt.Get() )
			{
				// Follow our source entity
				SetAbsOrigin( m_hSourceEnt->GetAbsOrigin() );
			}
			else
			{
				// Remain wherever our source entity died, and show that it's dead
				debugoverlay->AddTextOverlayRGB( GetAbsOrigin(), -1, 0, 128,128,128,255, "[Shape target dead]" );
			}

			Color faceColor( 0, 0, 128, 64 ), edgeColor = Color( 0, 0, 0, 0 );
			debugoverlay->AddBoxOverlay2( GetAbsOrigin(), Vector( -1, -1, -1 ), Vector( +1, +1, +1 ), vec3_angle, faceColor, edgeColor, NDEBUG_PERSIST_TILL_NEXT_SERVER );

			if ( m_pErrors[ ERROR_POSITION ] )
				debugoverlay->AddTextOverlayRGB( GetAbsOrigin(), offset++, 0,  255,  0,  0,255, m_pErrors[ ERROR_POSITION ] );
			if ( m_pErrors[ ERROR_LINE_OF_SIGHT ] )
				debugoverlay->AddTextOverlayRGB( GetAbsOrigin(), offset++, 0,    0,255,  0,255, m_pErrors[ ERROR_LINE_OF_SIGHT ] );
			if ( m_pErrors[ ERROR_NO_NAME ] )
				debugoverlay->AddTextOverlayRGB( GetAbsOrigin(), offset++, 0,    0,128,128,255, m_pErrors[ ERROR_NO_NAME ] );
			if ( m_pErrors[ ERROR_BAD_TYPE ] )
				debugoverlay->AddTextOverlayRGB( GetAbsOrigin(), offset++, 0,  128,  0, 96,255, m_pErrors[ ERROR_BAD_TYPE ] );
		}
	}

private:

	EHANDLE m_hSourceEnt;

	char *m_pErrors[ ERROR_NUM_ERRORS ];
};
LINK_ENTITY_TO_CLASS( error_text_ent, CErrorTextEnt );


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CShapeTarget::CShapeTarget( void )
 :	m_bActive( false ),
	m_bStartEnabled( false ),
	m_bInFlight( false ),
	m_bInvisible( false ),
	m_fRadius( INVALID_RADIUS ),
	m_nShapeSize( INVALID_SHAPE_SIZE ),
	m_nSequenceNumber( INVALID_SEQUENCE_NUMBER ),
	m_bRepel( false ),
	m_fRepelDuration( INVALID_REPEL_DURATION ),
	m_fRepelEndTime( -1 )
{
	g_Targets.AddToTail( this );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CShapeTarget::~CShapeTarget( void )
{
	if ( m_hTouchingBlob.ToInt() != INVALID_EHANDLE_INDEX )
	{
		// This trigger target died while the blob was attached to it (or its parent)
		m_OnEndDeliberateTouchByBlob.FireOutput( m_hTouchingBlob.Get(), this );
	}

	if ( m_bActive )
	{
		// Cancel any remaining pending Think calls
		m_bPendingThink = false;

		if ( m_bRepel )
		{
			TerminateRepulsion();
		}
		else
		{
			UnlinkShapeTarget();
		}
	}
	g_Targets.FindAndFastRemove( this );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::Precache()
{
	PrecacheModel( SHAPE_TARGET_MODEL );
	PrecacheScriptSound( "NPC_Hunter.FlechetteHitWorld" );
	PrecacheParticleSystem( "hunter_projectile_explosion_1" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteExplode" );
	s_nLightningSprite = PrecacheModel( "sprites/lgtning.vmt" );
	PrecacheEffect( "VortDispel" );
	PrecacheScriptSound( "NPC_CombineGunship.SearchPing" );

	BaseClass::Precache();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::InputEnable( inputdata_t &inputdata )
{
	// Ignore multiple enable calls
	if ( !m_bActive )
	{
		// If there's a pending Think call, cancel it
		m_bPendingThink = false;

		if ( m_bRepel )
		{
			ActivateRepulsion();
		}
		else
		{
			LinkShapeTarget();
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::InputDisable( inputdata_t &inputdata )
{
	// Ignore multiple disable calls
	if ( m_bActive )
	{
		// If there's a pending Think call, cancel it:
		m_bPendingThink = false;

		if ( m_bRepel )
		{
			TerminateRepulsion();
		}
		else
		{
			bool bExplode = inputdata.value.Bool();
			UnlinkShapeTarget( bExplode );
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CShapeTarget::CreateVPhysics( void )
{
	// Create the object in the physics system (shadowing means it
	// moves with this entity - important for parenting to work)
	// UNDONE: figure out how to make a long_thin, orientable projectile (move type fly only work's with AABBs)
	SetSolid( SOLID_BBOX );
	VPhysicsInitShadow( false, false );
	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::ValidateShapeTargetLocationThink( void )
{
	// Check that this was placed at a valid position, such that the blob can navigate to it
	trace_t bigBoxTrace, smallBoxTrace;
	UTIL_TraceHull( GetLocalOrigin(), GetLocalOrigin(), -2.0*PROJECTILE_PUSH_BACK_DIST*Vector(1,1,1), 2.0*PROJECTILE_PUSH_BACK_DIST*Vector(1,1,1), MASK_SOLID, this, COLLISION_GROUP_PROJECTILE, &bigBoxTrace );
	UTIL_TraceHull( GetLocalOrigin(), GetLocalOrigin(), -0.5*PROJECTILE_PUSH_BACK_DIST*Vector(1,1,1), 0.5*PROJECTILE_PUSH_BACK_DIST*Vector(1,1,1), MASK_SOLID, this, COLLISION_GROUP_PROJECTILE, &smallBoxTrace );
	if ( smallBoxTrace.startsolid )
		ErrorText( CErrorTextEnt::ERROR_POSITION, "ERROR: shape target placed too close to the world - blob will not be able to navigate to it correctly" );
	else if ( !bigBoxTrace.startsolid )
		ErrorText( CErrorTextEnt::ERROR_POSITION, "ERROR: shape target placed too far from the world - blob will not be able to navigate to it correctly" );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::Spawn( void )
{
	Precache();
	SetModel( SHAPE_TARGET_MODEL );
	BaseClass::Spawn();


	// Deal with Hammer/FGD specialisations of shape_target
	const char *pClassname = GetClassname();
	if ( Q_stricmp( pClassname, "blob_shape_target" ) == 0 )
	{
		// 'blob_shape_target' is used to provide a single/chained target for the blob
		if ( !m_ShapeName )
			ErrorText( CErrorTextEnt::ERROR_NO_NAME, "ERROR: 'blob_shape_target' does not have a shape name" );
		m_bRepel = false;
		if ( !m_bInFlight )
		{
			// We can't do traces in Spawn (since other entities won't have their models set up yet), so schedule a think
			SetThink( &CShapeTarget::ValidateShapeTargetLocationThink );
			SetNextThink( gpGlobals->curtime );
		}
	}
	else if ( Q_stricmp( pClassname, "blob_trigger" ) == 0 )
	{
		// 'blob_trigger' is used to fire an output when touched by the blob
		Assert( !m_bRepel && !m_bInFlight );
		Assert( ( m_fRadius == INVALID_RADIUS ) || ( m_fRadius > 1.0f ) );
		m_bRepel		= false;
		m_bInFlight		= false;
		m_bInvisible	= true;	// Useful?
		if ( !m_ShapeName )
		{
			// Auto-generate a unique name, so the mapper needn't
			char temp[ 16 ];
			static int count = 1;
			Q_snprintf( temp, sizeof( temp ), "}st_bt_%d{", count++ );
			m_ShapeName = AllocPooledString( temp );
		}
	}
	else if ( Q_stricmp( pClassname, "blob_repeller" ) == 0 )
	{
		// 'blob_trigger' is used to fire an output when touched by the blob
		Assert( ( m_fRadius == INVALID_RADIUS ) || ( m_fRadius > 1.0f ) );
		Assert( ( m_fRepelDuration == INFINITE_REPEL_DURATION ) || ( m_fRepelDuration == INVALID_REPEL_DURATION ) || ( m_fRepelDuration > 0.5f ) );
		m_bRepel = true;
	}
	else
	{
		ErrorText( CErrorTextEnt::ERROR_BAD_TYPE, "WARNING: shape target not using one of the new, preferred sub-types (blob_shape_target, blob_trigger or blob_repeller)..." );
	}
	SetClassname( "shape_target" );


	if ( m_bInvisible )
		AddEffects( EF_NODRAW );

	// No need for shadows
	AddEffects( EF_NOSHADOW );

	// Gets DrawDebugGeometryOverlays called every tick:
	m_debugOverlays |= OVERLAY_PIVOT_BIT;


	// Init member values (after keyvalue parsing)
	m_fRadius = ( m_fRadius == INVALID_RADIUS ) ? DEFAULT_RADIUS : m_fRadius;
	if ( m_bRepel )
	{
		m_fRepelDuration = ( m_fRepelDuration == INVALID_REPEL_DURATION ) ? DEFAULT_REPEL_DURATION : m_fRepelDuration;
	}
	else
	{
		m_nShapeSize = ( m_nShapeSize == INVALID_SHAPE_SIZE ) ? MAX_SHAPE_SIZE : m_nShapeSize;
		Assert( m_nShapeSize <= MAX_SHAPE_SIZE );
		m_nShapeSize = MIN( m_nShapeSize, MAX_SHAPE_SIZE );
	}

	if ( m_bInFlight )
	{
		// Turn on projectile flight+collision physics
		BecomeProjectile();
	}
	else
	{
		// Turn off motion and solidity
		BecomeStationary();
		// FIXME: Need to check+spew for unreachable shape target positions on map spawn.
	}

	if ( m_bStartEnabled )
	{
		// Activated immediately on spawn
		if ( m_bRepel )
		{
			ActivateRepulsion();
		}
		else
		{
			LinkShapeTarget();
		}
	}
	else
	{
		// Activated on impact (if m_bInFlight), by an input, or by blob proximity
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
IPhysicsObject *CShapeTarget::InitVPhysics( const Vector &dimensions )
{
	UTIL_SetSize( this, -0.5f*dimensions, +0.5f*dimensions );
	if ( !VPhysicsGetObject() )
	{
		CreateVPhysics();
		Assert( VPhysicsGetObject() );
	}
	return VPhysicsGetObject();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::BecomeStationary( void )
{
	Assert( !m_bInFlight );
	if ( !m_bInFlight )
	{
		Vector velocity = GetLocalVelocity();
		if ( velocity.Length() > 0.1 )
		{
			// Projectiles need to push back from the surface they impact, so the blob can access them easier
			trace_t impactTrace;
			UTIL_TraceLine( GetLocalOrigin(), GetLocalOrigin() + velocity*100, MASK_SOLID, this, COLLISION_GROUP_PROJECTILE, &impactTrace );
			Assert( impactTrace.fraction < 1 );
			Assert( !GetParent() ); // Shouldn't be parented yet
			if ( ( impactTrace.fraction < 1 ) && !GetParent() )
			{
				Vector normal = impactTrace.plane.normal;
				SetLocalOrigin( impactTrace.endpos + PROJECTILE_PUSH_BACK_DIST*normal );
				SetLocalAngles( QAngle( UTIL_VecToPitch( -normal ), UTIL_VecToYaw( -normal ), 0 ) );
			}
		}
		SetLocalVelocity( vec3_origin );
		SetSolidFlags( FSOLID_NOT_SOLID );
		SetMoveType( MOVETYPE_NONE );
		SetGravity( 0 );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::BecomeProjectile( void )
{
	Assert( m_bInFlight );
	if ( m_bInFlight )
	{
		SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM );
		SetGravity( 0.05f );
		SetCollisionGroup( COLLISION_GROUP_PROJECTILE );
		InitVPhysics( PROJECTILE_DIMENSIONS );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::BecomeBlobTrigger( void )
{
	Assert( m_bActive && !m_bInFlight && !m_bRepel && !IsMarkedForDeletion() );
	if ( m_bActive && !m_bInFlight && !m_bRepel && !IsMarkedForDeletion() )
	{
		IPhysicsObject *pPhysObj = InitVPhysics( 2*m_fRadius*Vector(1,1,1) );
		if ( pPhysObj )
		{
			pPhysObj->BecomeTrigger();
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CShapeTarget *CShapeTarget::GetFirstTargetInShape( const string_t & shapeName ) // Static method
{
	for ( int i = 0; i < g_Targets.Count(); i++ )
	{
		CShapeTarget *pShapeTarget = g_Targets[ i ];
		if ( pShapeTarget->m_bActive && !pShapeTarget->m_bRepel &&
			( pShapeTarget->GetPrevTarget() == NULL ) && ( pShapeTarget->m_ShapeName == shapeName ) )
		{
			return pShapeTarget;
		}
	}

	return NULL;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::DestroyShape( const char *pShapeName ) // Static method
{
	string_t shapeName = AllocPooledString( pShapeName );
	CShapeTarget *pTarget;
	while( ( pTarget = GetFirstTargetInShape( shapeName ) ) != NULL )
	{
		bool bExplode = true;
		pTarget->m_bPendingThink = false; // Cancel any Thinks queued for this target before it's explosified
		pTarget->UnlinkShapeTarget( bExplode );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CShapeTarget *CShapeTarget::GetNextTarget( void )
{
	Assert( m_bActive && !m_bRepel );
	if ( m_bActive && !m_bRepel )
	{
		return m_hNextTarget;
	}
	Assert( ( m_hNextTarget == NULL ) && ( m_hPrevTarget == NULL ) );
	return NULL;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CShapeTarget *CShapeTarget::GetPrevTarget( void )
{
	Assert( m_bActive && !m_bRepel );
	if ( m_bActive && !m_bRepel )
	{
		return m_hPrevTarget;
	}
	Assert( ( m_hNextTarget == NULL ) && ( m_hPrevTarget == NULL ) );
	return NULL;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::InsertTargetBefore( CShapeTarget *pShapeTarget )
{
	Assert( m_bActive && !m_bRepel );
	if ( m_bActive && !m_bRepel )
	{
		if ( m_hPrevTarget != NULL )
		{
			m_hPrevTarget->m_hNextTarget = pShapeTarget;
		}
		pShapeTarget->m_hPrevTarget = m_hPrevTarget;
		pShapeTarget->m_hNextTarget = this;
		m_hPrevTarget = pShapeTarget;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::InsertTargetAfter( CShapeTarget *pShapeTarget )
{
	Assert( m_bActive && !m_bRepel );
	if ( m_bActive && !m_bRepel )
	{
		if ( m_hNextTarget != NULL )
		{
			m_hNextTarget->m_hPrevTarget = pShapeTarget;
		}
		pShapeTarget->m_hPrevTarget = this;
		pShapeTarget->m_hNextTarget = m_hNextTarget;
		m_hNextTarget = pShapeTarget;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CShapeTarget::ValidateShapeTargetLink( CShapeTarget *pOtherTarget )
{
	// For now, just test to see if static geometry is blocking line-of-sight to the new target
	// (don't test target entities placed in the map, i.e. ones with sequence numbers)
	if ( ( m_nSequenceNumber == INVALID_SEQUENCE_NUMBER ) && pOtherTarget )
	{
		trace_t trace;
		unsigned int nAITraceMask = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_OPAQUE | CONTENTS_MONSTERCLIP;
		UTIL_TraceLine( GetAbsOrigin(), pOtherTarget->GetAbsOrigin(), nAITraceMask, NULL, COLLISION_GROUP_NONE, &trace );
		if ( trace.fraction < 1.0f )
		{
			return false;
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::LinkShapeTarget( void )
{
	Assert( !m_bPendingThink && !m_bActive && !m_bRepel && !!m_ShapeName );
	if ( !m_bPendingThink && !m_bActive && !m_bRepel && !!m_ShapeName )
	{
		const string_t & shapeName = m_ShapeName;

		// Find the first target in the new target's shape:
		CShapeTarget *pFirstTarget = GetFirstTargetInShape( shapeName );
		AssertMsg( pFirstTarget != this, "ERROR: new target should not be in a shape list yet!" );
		if ( pFirstTarget == NULL )
		{
			// This target will form a new shape
		}
		else if ( pFirstTarget && ( pFirstTarget != this ) )
		{
			// This shape already has active targets in it - add at the appropriate position in the sequence (default: put at end)

			AssertMsg( pFirstTarget->m_nShapeSize == m_nShapeSize, "ERROR: two shape targets with the same shape name have different shape sizes..." );
			m_nShapeSize = pFirstTarget->m_nShapeSize;

			int numTargetsInShape = 0;
			if ( m_nShapeSize == 1 )
			{
				// Just replace the existing target in this shape (don't want to call ValidateShapeTargetLink)
				numTargetsInShape = 2;
			}
			else
			{
				CShapeTarget *pOtherTarget = pFirstTarget;
				bool bHasLOS = true;
				bool bAdded = false;
				while ( pOtherTarget )
				{
					numTargetsInShape++;

					if ( !bAdded )
					{
						if ( ( m_nSequenceNumber != INVALID_SEQUENCE_NUMBER ) && ( m_nSequenceNumber < pOtherTarget->m_nSequenceNumber ) )
						{
							bHasLOS = bHasLOS && ( ValidateShapeTargetLink( pOtherTarget ) && ValidateShapeTargetLink( pOtherTarget->GetPrevTarget() ) );
							if ( bHasLOS )
							{
								pOtherTarget->InsertTargetBefore( this );
								bAdded = true;
							}
						}
						else if ( pOtherTarget->m_hNextTarget == NULL )
						{
							bHasLOS = bHasLOS && ValidateShapeTargetLink( pOtherTarget );
							if ( bHasLOS )
							{
								pOtherTarget->InsertTargetAfter( this );
								bAdded = true;
							}
						}
					}
					pOtherTarget = pOtherTarget->m_hNextTarget;
				}

				if ( !bAdded )
				{
					// This target couldn't be added to the shape, so destroy it
					if ( !bHasLOS && ( GetEntityName() == s_WeaponizerShapeTargetName ) )
					{
						// This is allowed behaviour for weaponizer targets (which are dynamically placed by the player)
						Explode();
					}
					else
					{
						// This is a map error
						ErrorText( CErrorTextEnt::ERROR_LINE_OF_SIGHT,
									!bHasLOS ?	"ERROR: Line-of-sight blocked! Cannot link this to other targets in the same shape." :
												"ERROR: unknown issue linking this target into its shape, tell a programmer." );
						Explode();
					}
					return;
				}
			}

			// Kill the oldest target if the shape is full
			if ( numTargetsInShape > pFirstTarget->m_nShapeSize )
			{
				AssertMsg( numTargetsInShape == ( pFirstTarget->m_nShapeSize + 1 ), "ERROR: only one shape target should be added to a shape at a time... code is in a broken state (save/restore issue?)" );
				bool bExplode = true;
				pFirstTarget->UnlinkShapeTarget( bExplode );
			}
		}

		// The target is now active in a shape
		m_bActive = true;

		// Receive VPhysics touches from the blob
		BecomeBlobTrigger();
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::UnlinkShapeTarget( bool bExplode )
{
	Assert( !m_bPendingThink && m_bActive && !m_bRepel && !!m_ShapeName );
	if ( !m_bPendingThink && m_bActive && !m_bRepel && !!m_ShapeName )
	{
		if ( m_hPrevTarget != NULL )
		{
			m_hPrevTarget->m_hNextTarget = m_hNextTarget;
		}
		if ( m_hNextTarget != NULL )
		{
			m_hNextTarget->m_hPrevTarget = m_hPrevTarget;
		}

		// The target is no longer part of a shape
		m_hNextTarget = NULL;
		m_hPrevTarget = NULL;
		m_bActive = false;

		if ( bExplode )
		{
			// Optionally get rid of the target
			Explode();
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::GetShapeEdgeList( CUtlVector<ShapeEdge_t> &edgeList, bool bConvertToLocalSpace )
{
	// Return a list of edges describing the shape containing this target
	Assert( m_bActive && !m_bRepel );
	if ( m_bActive && !m_bRepel )
	{
		CShapeTarget *pShapeTarget = this;
		while( pShapeTarget->GetPrevTarget() )
		{
			// Skip back to the start of the chain
			pShapeTarget = pShapeTarget->GetPrevTarget();
		}

		// Optionally convert to 'local' coordinates (treat the first shape target as the shape's origin)
		Vector localOffset = bConvertToLocalSpace ? pShapeTarget->GetAbsOrigin() : vec3_origin;

		while( pShapeTarget && pShapeTarget->GetNextTarget() )
		{
			ShapeEdge_t edge;
			edge.startPos = pShapeTarget->GetAbsOrigin() - localOffset;
			edge.startVel = pShapeTarget->GetAbsVelocity();
			pShapeTarget = pShapeTarget->GetNextTarget();
			edge.endPos = pShapeTarget->GetAbsOrigin() - localOffset;
			edge.endVel = pShapeTarget->GetAbsVelocity();
			
			if ( edgeList.Count() < edgeList.NumAllocated() )
				edgeList.AddToTail( edge );
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::StartTouch( CBaseEntity *pOther )
{
	CNPC_Blob *pBlob = dynamic_cast<CNPC_Blob *>( pOther );

	// Ignore touches while m_bPendingAction is set
	if ( !m_bPendingThink && !IsMarkedForDeletion() )
	{
		if ( !m_bActive )
		{
			// Inactive target

			if ( m_bInFlight )
			{
				// This target is in flight, not yet active
				if ( pBlob )
				{
					if ( !m_bRepel )
					{
						if ( pBlob->HasShapeTarget( this ) )
						{
							// The blob's already following this shape, so stop it
							pBlob->RemoveShapeTarget( this );
						}
						else if ( m_nShapeSize > 1 )
						{
							// Attract the touched blob to this target's shape
							bool bDontClearExistingTargets = false;
							pBlob->AddShapeTarget( this, bDontClearExistingTargets );
						}
					}
					UTIL_Remove( this );
				}
				else
				{
					// FIXME: this doesn't work (the target stops moving even if we early-out from here...)
					/*if ( shaper_line_of_sight.GetBool() )
					{
						// Ignore collisions with see-through objects
						trace_t losTrace;
						Vector vForward = GetAbsVelocity();
						vForward.NormalizeInPlace();
						UTIL_TraceLine( GetAbsOrigin(),	GetAbsOrigin() + 2000*vForward, MASK_BLOCKLOS, this, COLLISION_GROUP_PROJECTILE, &losTrace );
						pOther->VPhysicsGetObject()->
						if ( ( losTrace.fraction >= 1 ) || ( losTrace.m_pEnt != pOther ) || ( ( GetAbsOrigin() - losTrace.endpos ).Length() > 2 ) )
							return;
					}*/

					// Stop moving
					m_bInFlight = false;
					BecomeStationary();
					ValidateShapeTargetLocationThink();
					EmitSound( "NPC_Hunter.FlechetteHitWorld" );

					// Bullseyes don't have valid bones so don't attach to them because it causes
					// a crash in bone setup. Also avoid parenting to the owner (i.e. the player)
					if ( !pOther->IsWorld() && ( pOther != GetOwnerEntity() ) && ( pOther->Classify() != CLASS_BULLSEYE ) )
					{
						SetParent( pOther );
					}

					// Enable the target on impact
					if ( m_bRepel )
					{
						if ( ValidateRepelTargetPlacement() )
						{
							// Initiate after the appropriate delay
							SetThink( &CShapeTarget::RepelActivateThink );
							SetNextThink( gpGlobals->curtime + shaper_repel_delay.GetFloat() );
							m_fRepelEndTime = ( m_fRepelDuration == INFINITE_REPEL_DURATION ) ? 0 : ( gpGlobals->curtime + shaper_repel_delay.GetFloat() + m_fRepelDuration );
							// A think call is pending (ignore further touches 'till then)
							m_bPendingThink = true;
						}
						else
						{
							// Can't place the target here
							Explode();
						}
					}
					else
					{
						// Link the shape target (defer it to avoid changing physics properties in a touch function)
						LinkShapeTarget();
					}
				}
			}
		}
		else if ( pBlob && !m_bRepel )
		{
			// Active shape target

			// Accidental touch (deliberate touch is fired by the blob on reaching its goal)
			m_OnStartTouchByBlob.FireOutput( pBlob, this );
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::NotifyStartDeliberateTouchByBlob( CBaseEntity *pBlob, CBaseEntity *pEntity ) // Static method
{
	// Called by a blob that says it's attaching to an entity. By parenting trigger shape targets
	// to it, we can effectively decorate any entity with an 'on blob start touch' output.

	Assert( pEntity );
	if ( pEntity )
	{
		CShapeTarget *pShapeTarget = dynamic_cast<CShapeTarget *>( pEntity );
		if ( pShapeTarget )
		{
			// The blob is directly touching a shape target
			// Don't bother firing weaponizer shape target outputs, and ignore multiple start-touches
			if ( ( pShapeTarget->GetEntityName() != s_WeaponizerShapeTargetName ) && !pShapeTarget->m_hTouchingBlob )
			{
				pShapeTarget->m_OnStartDeliberateTouchByBlob.FireOutput( pBlob, pShapeTarget );
				pShapeTarget->m_hTouchingBlob = pBlob;
			}
		}
		else
		{
			// The blob is touching another type of entity - does it have any shape target children?
			CBaseEntity *pChild = pEntity->FirstMoveChild();
			while ( pChild )
			{
				CShapeTarget *pChildShapeTarget = dynamic_cast<CShapeTarget *>( pChild );
				if ( pChildShapeTarget && pChildShapeTarget->m_bActive && !pChildShapeTarget->m_bRepel &&
					 ( pChildShapeTarget->GetEntityName() != s_WeaponizerShapeTargetName ) )
				{
					if ( !pChildShapeTarget->m_hTouchingBlob ) // Ignore multiple start-touches
					{
						pChildShapeTarget->m_OnStartDeliberateTouchByBlob.FireOutput( pBlob, pChildShapeTarget );
						pChildShapeTarget->m_hTouchingBlob = pBlob;
					}
				}
				pChild = pChild->NextMovePeer();
			}
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::NotifyEndDeliberateTouchByBlob( CBaseEntity *pBlob, CBaseEntity *pEntity ) // Static method
{
	// Called by a blob that says it's detaching from an entity. By parenting trigger shape
	// targets to it, we can effectively decorate any entity with an 'on blob end touch' output.
	// NOTE: if pEntity is NULL, we need to look for shape targets with a 'recently deceased' parent

	if ( pEntity )
	{
		CShapeTarget *pShapeTarget = dynamic_cast<CShapeTarget *>( pEntity );
		if ( pShapeTarget )
		{
			// The blob was directly touching a shape target
			// Don't bother firing weaponizer shape target outputs, and ignore multiple end-touches
			if ( ( pShapeTarget->GetEntityName() != s_WeaponizerShapeTargetName ) && !!pShapeTarget->m_hTouchingBlob )
			{
				pShapeTarget->m_OnEndDeliberateTouchByBlob.FireOutput( pBlob, pShapeTarget );
				pShapeTarget->m_hTouchingBlob = NULL;
			}
		}
		else
		{
			CBaseEntity *pChild = pEntity->FirstMoveChild();
			while ( pChild )
			{
				CShapeTarget *pChildShapeTarget = dynamic_cast<CShapeTarget *>( pChild );
				if ( pChildShapeTarget && pChildShapeTarget->m_bActive &&
					!pChildShapeTarget->m_bRepel && ( pChildShapeTarget->GetEntityName() != s_WeaponizerShapeTargetName ) )
				{
					if ( !!pChildShapeTarget->m_hTouchingBlob ) // Ignore multiple end-touches
					{
						pChildShapeTarget->m_OnEndDeliberateTouchByBlob.FireOutput( pBlob, pChildShapeTarget );
						pChildShapeTarget->m_hTouchingBlob = NULL;
					}
				}
				pChild = pChild->NextMovePeer();
			}
		}
	}
	else
	{
		// Presumably, the blob's target was destroyed - find shape targets with a dead parent
		for ( int i = 0; i < g_Targets.Count(); i++ )
		{
			// FIXME: This would do the wrong thing if a blob had multiple simultaneous (non-parented) targets...
			CShapeTarget *pShapeTarget = g_Targets[i];
			if ( ( pShapeTarget->m_hTouchingBlob == pBlob ) &&
				 ( !pShapeTarget->GetParent() || pShapeTarget->GetParent()->IsMarkedForDeletion() ) )
			{
				pShapeTarget->m_OnEndDeliberateTouchByBlob.FireOutput( pBlob, pShapeTarget );
				pShapeTarget->m_hTouchingBlob = NULL;
			}
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::Explode( void )
{
	Assert( !m_bActive && !m_bPendingThink );
	if ( !m_bActive && !m_bPendingThink )
	{
		// // Treat the target like debris (dies once offscreen)
		//SUB_StartFadeOutInstant();

		// Explode like a flechette
		DispatchParticleEffect( "hunter_projectile_explosion_1", GetAbsOrigin(), GetAbsAngles(), NULL );
		EmitSound( "NPC_Hunter.FlechetteExplode" );
		PhysCallbackRemove( this->NetworkProp() );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::DestroyOverlappingRepelTargets( void )
{
	float fAllowedOverlap = shaper_repel_overlap.GetFloat();
	if ( fAllowedOverlap < 1.0f )
	{
		Vector vOrigin = GetAbsOrigin();
		for ( int i = 0; i < g_Targets.Count(); i++ )
		{
			CShapeTarget *pOther = g_Targets[ i ];
			if ( pOther && ( pOther != this ) && pOther->m_bActive && pOther->m_bRepel )
			{
				Vector vDelta = pOther->GetAbsOrigin() - vOrigin;
				float fOverlap = ( m_fRadius + pOther->m_fRadius ) - vDelta.Length();
				float fOverlapFraction = MIN( 1.0f, fOverlap / MIN( m_fRadius, pOther->m_fRadius ) );
				if ( fOverlapFraction > fAllowedOverlap )
				{
					pOther->m_bPendingThink = false; // Cancel any Thinks queued for the other target - it's done for!
					pOther->TerminateRepulsion();
					pOther->Explode();
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CShapeTarget::ValidateRepelTargetPlacement( void )
{
	// Targets can attach to NPCs at any angle
	CAI_BaseNPC *pNPCParent = dynamic_cast<CAI_BaseNPC *>( GetParent() );
	if ( !pNPCParent )
	{
		// Check we were placed on a surface with a valid orientation
		float fRequiredHorizontality = shaper_repel_horizontal.GetFloat();
		if ( fRequiredHorizontality > 0.0f )
		{
			// TODO: check surface orientation (compare normal.Z to shaper_repel_horizontal)
			//       trace straight down (or along the target's up axis), get the attached surface's normal
			Vector vNormal = Vector(0,0,1);
			float fHorizontality = fabsf( vNormal.z );
			if ( fHorizontality < fRequiredHorizontality )
			{
				return false;
			}
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::ActivateRepulsion( void )
{
	Assert( !m_bPendingThink && !m_bActive && !IsMarkedForDeletion() );
	if ( !m_bPendingThink && !m_bActive && !IsMarkedForDeletion() )
	{
		// Destroy other overlapping repel targets
		DestroyOverlappingRepelTargets();

		m_bActive = true;
		SetMoveType( MOVETYPE_NONE );
		m_fRepelEndTime = ( m_fRepelDuration == INFINITE_REPEL_DURATION ) ? 0 : gpGlobals->curtime + m_fRepelDuration;
		if ( m_fRepelDuration != INFINITE_REPEL_DURATION )
		{
			SetThink( &CShapeTarget::RepelEndThink );
			SetNextThink( m_fRepelEndTime );
		}
		// A think call is pending (ignore further touches 'till then)
		m_bPendingThink = true;

		// Create some snazzy FX:
		CBroadcastRecipientFilter filter2;
		Vector vOrigin( GetAbsOrigin() );

		// TODO: better particle effects (like houndeye rings)

		CEffectData	data;
		data.m_vOrigin = vOrigin;
		DispatchEffect( "VortDispel", data );
		
		// Audioze
		EmitSound( "NPC_CombineGunship.SearchPing" );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::TerminateRepulsion( void )
{
	Assert( !m_bPendingThink && m_bActive );
	if ( !m_bPendingThink && m_bActive )
	{
		m_bActive = false;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::RepelActivateThink( void )
{
	// m_bPendingThink can be cleared to cancel this think (e.g. due to an input event)
	if ( m_bPendingThink )
	{
		m_bPendingThink = false;
		Assert( !m_bActive && m_bRepel && !IsMarkedForDeletion() );
		if ( !m_bActive && m_bRepel && !IsMarkedForDeletion() )
		{
			ActivateRepulsion();
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::RepelEndThink( void )
{
	// m_bPendingThink can be cleared to cancel this think (e.g. due to an input event)
	if ( m_bPendingThink )
	{
		m_bPendingThink = false;
		Assert( m_bActive && m_bRepel );
		if ( m_bActive && m_bRepel )
		{
			TerminateRepulsion();
			Explode();
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CShapeTarget::TriggerRepelTargets( const Vector &vBlobOrigin, float fBlobRadius, bool bOnlyCheck ) // Static method
{
	// UNDONE: optimize this for large numbers of repel targets

	bool bDidTrigger = false;
	if ( shaper_repel_proximity.GetBool() )
	{
		for ( int i = 0; i < g_Targets.Count(); i++ )
		{
			CShapeTarget *pTarget = g_Targets[ i ];
			if ( pTarget && pTarget->m_bRepel && !pTarget->m_bActive && !pTarget->m_bInFlight )
			{
				// Make sure this target isn't already doing something (activating/exploding)
				if ( !pTarget->m_bPendingThink && !pTarget->IsMarkedForDeletion() )
				{
					Vector vDelta = vBlobOrigin - pTarget->GetAbsOrigin();
					float fDist = vDelta.Length() - fBlobRadius;
					if ( fDist < pTarget->m_fRadius )
					{
						bDidTrigger = true;
						if ( !bOnlyCheck )
						{
							pTarget->ActivateRepulsion();
						}
					}
				}
			}
		}
	}

	return bDidTrigger;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::GetRepelTargetForce( const Vector &vBlobOrigin, float fBlobRadius, Vector &vForce ) // Static method
{
	// UNDONE: optimize this for large numbers of repel targets

	// TODO: link together repel targets ala shape targets (repel w/ capsules instead of circles - math's almost as simple)
	CShapeTarget *pStrongestRepeller = NULL;
	float fMaxStrength = -1;
	for ( int i = 0; i < g_Targets.Count(); i++ )
	{
		CShapeTarget *pTarget = g_Targets[ i ];
		if ( pTarget && pTarget->m_bActive && pTarget->m_bRepel )
		{
			Vector vDelta = vBlobOrigin - pTarget->GetAbsOrigin();
			float fDist = vDelta.Length() - fBlobRadius;
			if ( fDist < pTarget->m_fRadius )
			{
				float STRENGTH_RAMP = 8.0f;
				float fStrength = STRENGTH_RAMP*( 1.0f - ( fDist / pTarget->m_fRadius ) );
				if ( fStrength > fMaxStrength )
				{
					fMaxStrength = fStrength;
					pStrongestRepeller = pTarget;
				}
			}
		}
	}

	if ( pStrongestRepeller )
	{
		Vector vDelta = vBlobOrigin - pStrongestRepeller->GetAbsOrigin();
		if ( !vDelta.IsZero() )
			vDelta.NormalizeInPlace();

		// Horizontalize the force
		vDelta.z *= ( 1.0f - shaper_repel_horizontal.GetFloat() );
		if ( !vDelta.IsZero() )
			vDelta.NormalizeInPlace();

		fMaxStrength = clamp( fMaxStrength, 0.0f, 1.0f );
		vForce = fMaxStrength*vDelta;
	}
	else
	{
		vForce = vec3_origin;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CShapeTarget::ValidateAINodeLink( const Vector &vStart, const Vector &vEnd )
{
	// TODO: finish this code
	return true;
/*
	AssertMsg( 0, "Code not run yet (step through it)." );
	// Return FALSE if any active repel targets block a given AI node link
	Vector vDelta = vEnd - vStart;
	for ( int i = 0; i < g_Targets.Count(); i++ )
	{
		CShapeTarget *pTarget = g_Targets[ i ];
		if ( pTarget && pTarget->m_bActive && pTarget->m_bRepel )
		{
			if ( IntersectRayWithSphere( vStart, vDelta, pTarget->GetAbsOrigin(), pTarget->m_fRadius, NULL, NULL ) )
			{
				return false;
			}
		}
	}
	return true;
*/
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::ErrorText( int type, const char *format, ... )
{
	if ( m_hErrorTextEnt == NULL )
	{
		m_hErrorTextEnt = CreateEntityByName( "error_text_ent" );
		DispatchSpawn( (CBaseEntity *)m_hErrorTextEnt );
	}

	CErrorTextEnt *pErrorTextEnt = static_cast<CErrorTextEnt *>( m_hErrorTextEnt.Get() );
	Assert( pErrorTextEnt );
	if ( pErrorTextEnt )
	{
		// FIXME: va_list pass-through works with Q_vsnprint, but not AddErrorText... weird
		va_list arg_ptr;
		va_start( arg_ptr, format );
		char pString[512];
		Q_vsnprintf( pString, sizeof( pString ), format, arg_ptr );
		va_end( arg_ptr );
		pErrorTextEnt->AddErrorText( this, (CErrorTextEnt::ErrorType_t)type, pString );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CShapeTarget::IsBlobTarget( void )
{
	// Active, visible, non-repel shape targets are blob food!
	return ( m_bActive && !m_bInvisible && !m_bInFlight && !m_bRepel && !IsMarkedForDeletion() );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CShapeTarget *CShapeTarget::GetNearestBlobTarget( const Vector &searchPos, bool bGetPlayerPlacedTarget ) // Static method
{
	CShapeTarget *pNearestEdibleTarget = NULL;
	float fNearestDistSqr = 0;

	for ( int i = 0; i < g_Targets.Count(); i++ )
	{
		CShapeTarget *pShapeTarget = g_Targets[i];
		if ( pShapeTarget->IsBlobTarget() &&
			 ( ( pShapeTarget->GetEntityName() == s_WeaponizerShapeTargetName ) == bGetPlayerPlacedTarget ) )
		{
			float fDistSqr = searchPos.DistToSqr( pShapeTarget->GetAbsOrigin() );
			if ( !pNearestEdibleTarget || ( fDistSqr < fNearestDistSqr ) )
			{
				pNearestEdibleTarget = pShapeTarget;
				fNearestDistSqr = fDistSqr;
			}
		}
	}

	return pNearestEdibleTarget;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CShapeTarget::IsPlayerTaggedEntity( CBaseEntity *pEntity, bool *pIsTaggedInMap ) // Static method
{
	bool bHasWeaponizerChild = false;

	if ( pEntity )
	{
		// If player-tagged, the entity should have a child which is a (non-repel) weaponizer shape target
		CBaseEntity *pChild = pEntity->FirstMoveChild();
		while ( pChild )
		{
			CShapeTarget *pChildShapeTarget = dynamic_cast<CShapeTarget *>( pChild );
			if ( pChildShapeTarget && pChildShapeTarget->m_bActive && !pChildShapeTarget->m_bRepel )
			{
				bHasWeaponizerChild = bHasWeaponizerChild || ( pChildShapeTarget->GetEntityName() == s_WeaponizerShapeTargetName );
				if ( pIsTaggedInMap )
				{
					// Also check to see if pEntity has a child which is a non-weaponizer (map-placed 'blob_trigger') shape target
					*pIsTaggedInMap = *pIsTaggedInMap || ( pChildShapeTarget->GetEntityName() != s_WeaponizerShapeTargetName );
				}
			}
			pChild = pChild->NextMovePeer();
		}
	}

	return ( bHasWeaponizerChild );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::ShootTarget( CBasePlayer *pPlayer, CShapeTarget *pTarget ) // Static method
{
	pPlayer = pPlayer ? pPlayer : UTIL_GetLocalPlayer();
	if ( pPlayer )
	{
		if ( pPlayer->GetActiveWeapon() )
		{
			pPlayer->GetActiveWeapon()->EmitSound( "NPC_Hunter.FlechetteShoot" );
		}

		pTarget->m_bInFlight = true;		// The target will act like a projectile, and be enabled on impact
		DispatchSpawn( pTarget );
		pTarget->SetOwnerEntity( pPlayer );	// The target shouldn't collide with its owner

		// Shoot the target from the player with the appropriate physics properties
		Vector	vForward, vRight, vUp, vOrigin, vDir;
		pPlayer->EyeVectors( &vForward, &vRight, &vUp );
		vDir = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );	
		vOrigin = pPlayer->Weapon_ShootPosition() + vForward * ( 12.0f + PROJECTILE_DIMENSIONS.z );
		UTIL_SetOrigin( pTarget, vOrigin );
		pTarget->SetAbsAngles( pPlayer->EyeAngles() );
		pTarget->SetAbsVelocity( vDir * 2000 );

		return;
	}

	UTIL_Remove( pTarget );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CShapeTarget *CShapeTarget::ShootShapeTarget( CBasePlayer *pPlayer, const char *shapeName, int shapeSize, int sequenceNumber ) // Static method
{
	CShapeTarget *pTarget = (CShapeTarget *)CreateEntityByName( "blob_shape_target" );
	if ( pTarget )
	{
		// Spawn a shape target, set up its shape values and shoot it
		pTarget->m_ShapeName		= AllocPooledString( shapeName );
		pTarget->m_nShapeSize		= shapeSize;
		pTarget->m_nSequenceNumber	= sequenceNumber;
		pTarget->SetName( s_WeaponizerShapeTargetName );
		pTarget->SetClassname( "blob_shape_target" );
		ShootTarget( pPlayer, pTarget );
	}
	return pTarget;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CShapeTarget *CShapeTarget::ShootRepelTarget( CBasePlayer *pPlayer, float fRadius, float fDuration ) // Static method
{
	CShapeTarget *pTarget = (CShapeTarget *)CreateEntityByName( "blob_repeller" );
	if ( pTarget )
	{
		if ( fRadius == INVALID_RADIUS )
			fRadius = shaper_repel_radius.GetFloat();
		if ( fDuration == INVALID_REPEL_DURATION )
			fDuration = shaper_repel_duration.GetFloat();

		// Spawn a shape target, set up its repel values and shoot it
		pTarget->m_bRepel			= true;
		pTarget->m_fRadius			= fRadius;
		pTarget->m_fRepelDuration	= fDuration;
		pTarget->SetName( s_WeaponizerRepelTargetName );
		pTarget->SetClassname( "blob_repeller" );
		ShootTarget( pPlayer, pTarget );
	}
	return pTarget;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
float CShapeTarget::WeaponPrimary( CBasePlayer *pPlayer ) // Static method
{
	if ( shaper_use_weaponizer.GetInt() == 2 )
	{
		ShootShapeTarget( pPlayer, "weaponizer", 1 );
		return 0.3f;
	}
	else
	{
		ShootShapeTarget( pPlayer, "weaponizer", 6 );
		return 0.3f;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
float CShapeTarget::WeaponSecondary( CBasePlayer *pPlayer ) // Static method
{
	if ( shaper_use_weaponizer.GetInt() == 2 )
	{
		DestroyShape( "weaponizer" );
		return 0.75f;
	}
	else
	{
		ShootRepelTarget( pPlayer );
		return 0.75f;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::WeaponReload( void ) // Static method
{
	// Reload resets the player's shape
	DestroyShape( "weaponizer" );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CON_COMMAND( shaper_shoot_target, "Fire a shaper target from the player's eyes (you may optionally specify shape name and sequence number)" )
{
	MDLCACHE_CRITICAL_SECTION();
	bool allowPrecache = CBaseEntity::IsPrecacheAllowed();
	CBaseEntity::SetAllowPrecache( true );

	const char *pShapeName = "blob_shoot_shape_target";
	int nSequenceNumber = CShapeTarget::INVALID_SEQUENCE_NUMBER;
	for ( int i = 1; i < args.ArgC(); i++ )
	{
		int seq;
		if ( 1 == sscanf( args.Arg( i ), "%d", &seq ) )
		{
			nSequenceNumber = seq; // Would it be useful for sequence number to actually be a float?
		}
		else
		{
			pShapeName = args.Arg( i );
		}
	}

	CShapeTarget::ShootShapeTarget( NULL, pShapeName, CShapeTarget::MAX_SHAPE_SIZE, nSequenceNumber );

	CBaseEntity::SetAllowPrecache( allowPrecache );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CON_COMMAND( shaper_shoot_repel, "Fire a blob-repelling target from the player's eyes (you may optionally specify radius and duration, in that order)" )
{
	MDLCACHE_CRITICAL_SECTION();
	bool allowPrecache = CBaseEntity::IsPrecacheAllowed();
	CBaseEntity::SetAllowPrecache( true );

	float fRadius	= CShapeTarget::INVALID_RADIUS;
	float fDuration	= CShapeTarget::INVALID_REPEL_DURATION;
	bool bFirst = true;
	for ( int i = 1; i < args.ArgC(); i++ )
	{
		float f;
		if ( 1 == sscanf( args.Arg( i ), "%f", &f ) )
		{
			if ( bFirst )
			{
				fRadius = f;
				bFirst = false;
			}
			else
			{
				fDuration = f;
				break;
			}
		}
	}

	CShapeTarget::ShootRepelTarget( NULL, fRadius, fDuration );

	CBaseEntity::SetAllowPrecache( allowPrecache );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CShapeTarget::DrawDebugGeometryOverlays( void )
{
	Vector vOrigin = GetAbsOrigin();
	bool bNoDepthTest = true;

	if ( shaper_draw_debug.GetInt() >= 1 )
	{
		if ( m_bActive )
		{
			if ( m_bRepel )
			{
				// Draw a circle of the appropriate size
				float fTimeElapsed = ( m_fRepelDuration == INFINITE_REPEL_DURATION ) ? 0 : ( m_fRepelEndTime - gpGlobals->curtime ) / m_fRepelDuration;
				fTimeElapsed = clamp( ( 1.0f - fTimeElapsed ), 0.0f, 1.0f );
				float fRadius = m_fRadius*( 1.0f - pow( fTimeElapsed, 16.0f ) );

				Vector vOrigin( GetAbsOrigin() );
				float c = 2*M_PI_F / 32;
				for ( int i = 0; i < 32; )
				{
					Vector v1( vOrigin + fRadius*( sin( i*c )*Vector(1,0,0) + cos( i*c )*Vector(0,1,0) ) ); i++;
					Vector v2( vOrigin + fRadius*( sin( i*c )*Vector(1,0,0) + cos( i*c )*Vector(0,1,0) ) );
					debugoverlay->AddLineOverlay( v1, v2, 200, 0, 0, bNoDepthTest, NDEBUG_PERSIST_TILL_NEXT_SERVER );
				}
			}
			else
			{
				CShapeTarget *pNextTarget = GetNextTarget();
				if ( pNextTarget )
				{
					Vector vTarget = pNextTarget->GetAbsOrigin();
					debugoverlay->AddLineOverlay( vOrigin, vTarget, 64, 255, 128, bNoDepthTest, NDEBUG_PERSIST_TILL_NEXT_SERVER );
				}
			}
		}

		Color faceColor( 0, 0, 0, 0 ), edgeColor = m_bActive ? Color( 255, 128, 64, 255 ) : Color( 128, 64, 32, 255 );
		debugoverlay->AddBoxOverlay2( vOrigin, Vector( -1, -1, -1 ), Vector( +1, +1, +1 ), vec3_angle, faceColor, edgeColor, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	}

	if ( ( shaper_draw_debug.GetInt() >= 2 ) && ( !( m_debugOverlays & OVERLAY_TEXT_BIT ) ) )
	{
		char tempstr[512];
		if ( m_bRepel )
		{
			Q_snprintf( tempstr, sizeof(tempstr), "%s [Repel|%s], Pos (%.0f,%.0f,%.0f), Radius %.0f",
						GetEntityName(), ( m_bActive ? "Enabled" : "Disabled" ),
						vOrigin.x, vOrigin.y, vOrigin.z, m_fRadius );
		}
		else
		{
			Q_snprintf( tempstr, sizeof(tempstr), "%s [Shape|%s|%s%s|%d], Pos (%.0f,%.0f,%.0f), Radius %.0f",
						GetEntityName(), ( m_bActive ? "Enabled" : "Disabled" ), ( m_bInvisible ? "|Invisible" : "" ), m_ShapeName.ToCStr(),
						m_nSequenceNumber, vOrigin.x, vOrigin.y, vOrigin.z, m_fRadius );
		}
		EntityText( 0, tempstr, 0 );
	}
}
