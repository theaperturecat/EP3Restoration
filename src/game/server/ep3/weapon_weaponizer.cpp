//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ====
//
// Absorb objects, liquids, and electricity from the world and turn them into weapons! 
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
#include "coordsize.h"
#include "collisionutils.h"
#include "weaponizer_concrete.h"
#include "explode.h"
#include "npc_antlion.h"
#include "weaponizer_shared.h"

// IDBS: temp code, piggyback CShapeTarget off the weaponizer
#include "shape_target.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


class IGrabControllerNotifyTarget;


void CFlak_ShootFlak( CBaseEntity *pOwner, int iModifier );
void CFlak_ShootFlamingFlakProjectile( CBaseEntity *pOwner );
void CFlak_ShootZapFlak( CBaseEntity *pOwner );
void CFlak_ShootZapFlakBeam( CBaseEntity *pOwner, attackstate_t eState );
void CFlak_ShootFlakGrenade( CBaseEntity *pOwner );

void CMetal_ShootSphere( CBaseEntity *pOwner );
void CMetal_ShootFlamingSphere( CBaseEntity *pOwner );
void CMetal_ShootZapSphere( CBaseEntity *pOwner );

void CreateLiquid( Vector vOrigin, Vector vDirection, int iType, float flTime );

ConVar	g_debug_weaponizer( "g_debug_weaponizer", "0" );

ConVar weaponizer_minforce( "weaponizer_minforce", "700" );
ConVar weaponizer_maxforce( "weaponizer_maxforce", "1500" );
ConVar weaponizer_maxmass( "weaponizer_maxmass", "250" );
ConVar weaponizer_tracelength( "weaponizer_tracelength", "250" );
ConVar weaponizer_mega_tracelength( "weaponizer_mega_tracelength", "850" );
ConVar weaponizer_chargetime("weaponizer_chargetime", "2" );
ConVar weaponizer_pullforce( "weaponizer_pullforce", "4000" );
ConVar weaponizer_mega_pullforce( "weaponizer_mega_pullforce", "8000" );
ConVar weaponizer_cone( "weaponizer_cone", "0.97" );
ConVar weaponizer_ball_cone( "weaponizer_ball_cone", "0.997" );
ConVar weaponizer_punt_cone( "weaponizer_punt_cone", "0.997" );
ConVar weaponizer_player_throwforce( "weaponizer_player_throwforce", "1000" );
ConVar weaponizer_dmg_glass( "weaponizer_dmg_glass", "15" );
ConVar weaponizer_right_turrets( "weaponizer_right_turrets", "0" );
ConVar weaponizer_fusion_chargeup_time( "weaponizer_fusion_chargeup_time", "1.1" );
ConVar weaponizer_toggle_fusion( "weaponizer_toggle_fusion", "1" );
ConVar weaponizer_water_hose_force( "weaponizer_water_hose_force", "12000" );
ConVar weaponzier_hose_electrical_dmg( "weaponzier_hose_electrical_dmg", "5" );
ConVar weaponizer_grab_flechette_delay( "weaponizer_grab_flechette_delay", "0.4" );


extern ConVar hunter_flechette_delay;

extern ConVar hl2_normspeed;
extern ConVar hl2_walkspeed;


// Add an extra slot for type NONE
const int WEAPONIZER_NUM_SECONDARY_MATERIALS = ( WEAPONIZER_NUM_MATERIALS - WEAPONIZER_NUM_PRIMARY_MATERIALS ) + 1;


#define PHYSCANNON_BEAM_SPRITE "sprites/orangelight1.vmt"
#define PHYSCANNON_GLOW_SPRITE "sprites/glow04_noz.vmt"
#define PHYSCANNON_ENDCAP_SPRITE "sprites/orangeflare1.vmt"
#define PHYSCANNON_CENTER_GLOW "sprites/orangecore1.vmt"
#define PHYSCANNON_BLAST_SPRITE "sprites/orangecore2.vmt"
 
#define MEGACANNON_BEAM_SPRITE "sprites/lgtning_noz.vmt"
#define MEGACANNON_GLOW_SPRITE "sprites/blueflare1_noz.vmt"
#define MEGACANNON_ENDCAP_SPRITE "sprites/blueflare1_noz.vmt"
#define MEGACANNON_CENTER_GLOW "effects/fluttercore.vmt"
#define MEGACANNON_BLAST_SPRITE "effects/fluttercore.vmt"

#define MEGACANNON_RAGDOLL_BOOGIE_SPRITE "sprites/lgtning_noz.vmt"
#define WEAPONIZER_SURFACE_BUILDER_BEAM_SPRITE "sprites/physbeam.vmt"

#define	MEGACANNON_MODEL "models/weapons/v_superphyscannon.mdl"
#define	MEGACANNON_SKIN	1


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class CFuncElectricity : public CTriggerMultiple
{
	DECLARE_CLASS( CFuncElectricity, CTriggerMultiple );
public:

	DECLARE_DATADESC();

	void Spawn( void );
	bool IsEmitter( void ) { return m_bEmitter; }

	void InputEnableEmitter( inputdata_t &inputdata );
	void InputDisableEmitter( inputdata_t &inputdata );

private:

	bool	m_bEmitter;
};

BEGIN_DATADESC( CFuncElectricity )
	DEFINE_KEYFIELD( m_bEmitter, FIELD_BOOLEAN, "emitter" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "EnableEmitter", InputEnableEmitter ),
	DEFINE_INPUTFUNC( FIELD_VOID, "DisableEmitter", InputDisableEmitter ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( func_electricity, CFuncElectricity );

void CFuncElectricity::Spawn( void )
{
	BaseClass::Spawn();
}

void CFuncElectricity::InputEnableEmitter( inputdata_t &inputdata )
{
	m_bEmitter = true;
}

void CFuncElectricity::InputDisableEmitter( inputdata_t &inputdata )
{
	m_bEmitter = false;
}


//-----------------------------------------------------------------------------
// HACK: probably not shippable -- derive surfaces from materials or surfaceprops?
//
// Triggers that represent surfaces to the weaponizer.
//-----------------------------------------------------------------------------
class CFuncWeaponizerWater : public CTriggerMultiple
{
	DECLARE_CLASS( CFuncWeaponizerWater, CTriggerMultiple );
public:
	~CFuncWeaponizerWater();

	DECLARE_DATADESC();

	void Spawn( void );
	virtual int GetWeaponizerMaterialType( void ); 

	void InputWaterShock( inputdata_t &inputdata );
	void InputStartWaterShock( inputdata_t &inputdata );

	void InputWaterSparkEffect( inputdata_t &inputdata );
	void InputPlayLeakSound( inputdata_t &inputdata );

private:

	int	m_iDamage;
	float m_flDamageRate;
	float m_flDamageTime;
	float m_flDamageDuration;
	CSoundPatch *m_pLiquidLoop;
};

BEGIN_DATADESC( CFuncWeaponizerWater )
	DEFINE_KEYFIELD( m_iDamage, FIELD_INTEGER, "damage" ),
	DEFINE_KEYFIELD( m_flDamageRate, FIELD_FLOAT, "damagerate" ),
	DEFINE_KEYFIELD( m_flDamageDuration, FIELD_FLOAT, "damageduration" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "WaterShock", InputWaterShock ),
	DEFINE_INPUTFUNC( FIELD_VOID, "StartWaterShock", InputStartWaterShock ),
	DEFINE_INPUTFUNC( FIELD_VOID, "WaterSparkEffect", InputWaterSparkEffect ),
	DEFINE_INPUTFUNC( FIELD_VOID, "PlayLeakSound", InputPlayLeakSound ),
	DEFINE_FIELD( m_flDamageTime, FIELD_TIME ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( func_weaponizer_water, CFuncWeaponizerWater );

CFuncWeaponizerWater::~CFuncWeaponizerWater()
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	if ( m_pLiquidLoop )
	{
		controller.SoundDestroy( m_pLiquidLoop );
		m_pLiquidLoop = NULL;
	}
}

void CFuncWeaponizerWater::Spawn( void )
{
	BaseClass::Spawn();

	m_flDamageTime = 0.0f;

	if ( m_flDamageRate <= 0.0f )
	{
		m_flDamageRate = 1.0f;
	}
}

int CFuncWeaponizerWater::GetWeaponizerMaterialType( void )
{
	if ( m_flDamageTime > gpGlobals->curtime )
		return WEAPONIZER_MATERIAL_TYPE_ELECTRICAL;

	return WEAPONIZER_MATERIAL_TYPE_WATER;
}

void CFuncWeaponizerWater::InputPlayLeakSound( inputdata_t &inputdata )
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	CPASAttenuationFilter filter( this );
	m_pLiquidLoop = controller.SoundCreate( filter, entindex(), "d1_town.GasJet" );
	controller.Play( m_pLiquidLoop, 1.0, 100 );
}

void CFuncWeaponizerWater::InputWaterSparkEffect( inputdata_t &inputdata )
{
	if ( m_flDamageTime < gpGlobals->curtime )
		return;

	Vector vOrigin = WorldSpaceCenter();
	Vector vecSurroundMins, vecSurroundMaxs;
	CollisionProp()->WorldSpaceSurroundingBounds( &vecSurroundMins, &vecSurroundMaxs );

	float flZ = vecSurroundMaxs.z;

	if ( flZ < 0 )
	{
		flZ *= -1;
	}

	vOrigin.z += flZ;

	int iCount = BoundingRadius() / 10;

	for ( int i = 0; i < iCount; i++ )
	{
		Vector vSpawnPos = vOrigin;

		vSpawnPos.x = random->RandomInt( vecSurroundMins.x, vecSurroundMaxs.x );
		vSpawnPos.y = random->RandomInt( vecSurroundMins.y, vecSurroundMaxs.y );
		vSpawnPos.z = vecSurroundMaxs.z;

		DispatchParticleEffect( "water_volume_electrified", vSpawnPos, vec3_angle );
	}

	g_EventQueue.AddEvent( this, "WaterSparkEffect", random->RandomFloat( 1.0, 2.0f ), this, this );
}

void CFuncWeaponizerWater::InputStartWaterShock( inputdata_t &inputdata )
{
	g_EventQueue.AddEvent( this, "WaterShock", 0.0f, this, this );
	g_EventQueue.AddEvent( this, "WaterSparkEffect", 0.0f, this, this );

	m_flDamageTime = gpGlobals->curtime + m_flDamageDuration;
}

void CFuncWeaponizerWater::InputWaterShock( inputdata_t &inputdata )
{
	if ( m_flDamageTime < gpGlobals->curtime )
		return;

	// TODO: enable this behavior
	#if 0
	int iCount = GetNumTouchEntities();

	for ( int i = 0; i < iCount; i++ )
	{
		CBaseEntity *pEntity = GetTouchingEntity( i );

		if ( pEntity )
		{
			CTakeDamageInfo	dmgInfo( this, this, m_iDamage, DMG_SHOCK );
			CalculateMeleeDamageForce( &dmgInfo, Vector( 0, 0, 1 ), pEntity->GetAbsOrigin() );
			pEntity->TakeDamage( dmgInfo );
		}
	}
	#endif

	g_EventQueue.AddEvent( this, "WaterShock", m_flDamageRate, this, this );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CFuncWeaponizerFuel : public CTriggerMultiple
{
	DECLARE_CLASS( CFuncWeaponizerFuel, CTriggerMultiple );
public:

	DECLARE_DATADESC();
	~CFuncWeaponizerFuel();

	void InputPlayLeakSound( inputdata_t &inputdata );

	virtual int GetWeaponizerMaterialType(void) { return WEAPONIZER_MATERIAL_TYPE_EXPLOSIVE; };

private:

	CSoundPatch *m_pLiquidLoop;

};

BEGIN_DATADESC( CFuncWeaponizerFuel )
	DEFINE_INPUTFUNC( FIELD_VOID, "PlayLeakSound", InputPlayLeakSound ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( func_weaponizer_fuel, CFuncWeaponizerFuel );


CFuncWeaponizerFuel::~CFuncWeaponizerFuel()
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	if ( m_pLiquidLoop )
	{
		controller.SoundDestroy( m_pLiquidLoop );
		m_pLiquidLoop = NULL;
	}
}

void CFuncWeaponizerFuel::InputPlayLeakSound( inputdata_t &inputdata )
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	CPASAttenuationFilter filter( this );
	m_pLiquidLoop = controller.SoundCreate( filter, entindex(), "d1_town.GasJet" );
	controller.Play( m_pLiquidLoop, 1.0, 100 );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CFuncWeaponizerFire : public CTriggerMultiple
{
	DECLARE_CLASS( CFuncWeaponizerFire, CTriggerMultiple );
public:

	//DECLARE_DATADESC();

	virtual int GetWeaponizerMaterialType()
	{
		if ( m_bDisabled == false )
			return WEAPONIZER_MATERIAL_TYPE_FIRE;
	
		return WEAPONIZER_MATERIAL_TYPE_NONE;
	}

private:

};

LINK_ENTITY_TO_CLASS( func_weaponizer_fire, CFuncWeaponizerFire );


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


// -------------------------------------------------------------------------
//  Weaponizer trace filter to handle special cases
// -------------------------------------------------------------------------

class CTraceFilterWeaponizer : public CTraceFilterSimple
{
public:
	DECLARE_CLASS( CTraceFilterWeaponizer, CTraceFilterSimple );

	CTraceFilterWeaponizer( const IHandleEntity *passentity, int collisionGroup )
		: CTraceFilterSimple( NULL, collisionGroup ), m_pTraceOwner( passentity ) {	}

	// For this test, we only test against entities (then world brushes afterwards)
	virtual TraceType_t	GetTraceType() const { return TRACE_ENTITIES_ONLY; }

	bool HasContentsGrate( CBaseEntity *pEntity )
	{
		// FIXME: Move this into the GetModelContents() function in base entity

		// Find the contents based on the model type
		int nModelType = modelinfo->GetModelType( pEntity->GetModel() );
		if ( nModelType == mod_studio )
		{
			CBaseAnimating *pAnim = dynamic_cast<CBaseAnimating *>(pEntity);
			if ( pAnim != NULL )
			{
				CStudioHdr *pStudioHdr = pAnim->GetModelPtr();
				if ( pStudioHdr != NULL && (pStudioHdr->contents() & CONTENTS_GRATE) )
					return true;
			}
		}
		else if ( nModelType == mod_brush )
		{
			// Brushes poll their contents differently
			int contents = modelinfo->GetModelContents( pEntity->GetModelIndex() );
			if ( contents & CONTENTS_GRATE )
				return true;
		}

		return false;
	}

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		// Only skip ourselves (not things we own)
		if ( pHandleEntity == m_pTraceOwner )
			return false;

		// Get the entity referenced by this handle
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		if ( pEntity == NULL )
			return false;

		// Handle grate entities differently
		if ( HasContentsGrate( pEntity ) )
		{
			// See if it's a grabbable physics prop
			CPhysicsProp *pPhysProp = dynamic_cast<CPhysicsProp *>(pEntity);
			if ( pPhysProp != NULL )
				return pPhysProp->CanBePickedUpByPhyscannon();

			// See if it's a grabbable physics prop
			if ( FClassnameIs( pEntity, "prop_physics" ) )
			{
				CPhysicsProp *pPhysProp = dynamic_cast<CPhysicsProp *>(pEntity);
				if ( pPhysProp != NULL )
					return pPhysProp->CanBePickedUpByPhyscannon();

				// Somehow had a classname that didn't match the class!
				Assert(0);
			}
			else if ( FClassnameIs( pEntity, "func_physbox" ) )
			{
				// Must be a moveable physbox
				CPhysBox *pPhysBox = dynamic_cast<CPhysBox *>(pEntity);
				if ( pPhysBox )
					return pPhysBox->CanBePickedUpByPhyscannon();

				// Somehow had a classname that didn't match the class!
				Assert(0);
			}

			// Don't bother with any other sort of grated entity
			return false;
		}

		// Use the default rules
		return BaseClass::ShouldHitEntity( pHandleEntity, contentsMask );
	}

protected:
	const IHandleEntity *m_pTraceOwner;
};

// We want to test against brushes alone
class CTraceFilterOnlyBrushes : public CTraceFilterSimple
{
public:
	DECLARE_CLASS( CTraceFilterOnlyBrushes, CTraceFilterSimple );
	CTraceFilterOnlyBrushes( int collisionGroup ) : CTraceFilterSimple( NULL, collisionGroup ) {}
	virtual TraceType_t	GetTraceType() const { return TRACE_WORLD_ONLY; }
};

//-----------------------------------------------------------------------------
// this will hit skip the pass entity, but not anything it owns 
// (lets player grab own grenades)
class CTraceFilterNoOwnerTest : public CTraceFilterSimple
{
public:
	DECLARE_CLASS( CTraceFilterNoOwnerTest, CTraceFilterSimple );

	CTraceFilterNoOwnerTest( const IHandleEntity *passentity, int collisionGroup )
		: CTraceFilterSimple( NULL, collisionGroup ), m_pPassNotOwner(passentity)
	{
	}

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		if ( pHandleEntity != m_pPassNotOwner )
			return BaseClass::ShouldHitEntity( pHandleEntity, contentsMask );

		return false;
	}

protected:
	const IHandleEntity *m_pPassNotOwner;
};

int GetAllChildren( CBaseEntity *pParent, CUtlVector<CBaseEntity *> &list );

//-----------------------------------------------------------------------------
// Purpose: Trace a hull for the physcannon
//-----------------------------------------------------------------------------
void UTIL_WeaponizerTraceHull( const Vector &vecAbsStart, const Vector &vecAbsEnd, const Vector &vecAbsMins, const Vector &vecAbsMaxs, CBaseEntity *pTraceOwner, trace_t *pTrace )
{
	// Default to HL2 vanilla
	if ( hl2_episodic.GetBool() == false )
	{
		CTraceFilterNoOwnerTest filter( pTraceOwner, COLLISION_GROUP_NONE );
		UTIL_TraceHull( vecAbsStart, vecAbsEnd, vecAbsMins, vecAbsMaxs, (MASK_SHOT|CONTENTS_GRATE), &filter, pTrace );
		return;
	}

	// First, trace against entities
	CTraceFilterWeaponizer filter( pTraceOwner, COLLISION_GROUP_NONE );
	UTIL_TraceHull( vecAbsStart, vecAbsEnd, vecAbsMins, vecAbsMaxs, (MASK_SHOT|CONTENTS_GRATE), &filter, pTrace );

	// If we've hit something, test again to make sure no brushes block us
	if ( pTrace->m_pEnt != NULL )
	{
		trace_t testTrace;
		CTraceFilterOnlyBrushes brushFilter( COLLISION_GROUP_NONE );
		UTIL_TraceHull( pTrace->startpos, pTrace->endpos, vecAbsMins, vecAbsMaxs, MASK_SHOT, &brushFilter, &testTrace );

		// If we hit a brush, replace the trace with that result
		if ( testTrace.fraction < 1.0f || testTrace.startsolid || testTrace.allsolid )
		{
			*pTrace = testTrace;
		}
	}
}

static void MatrixOrthogonalize( matrix3x4_t &matrix, int column )
{
	Vector columns[3];
	int i;

	for ( i = 0; i < 3; i++ )
	{
		MatrixGetColumn( matrix, i, columns[i] );
	}

	int index0 = column;
	int index1 = (column+1)%3;
	int index2 = (column+2)%3;

	columns[index2] = CrossProduct( columns[index0], columns[index1] );
	columns[index1] = CrossProduct( columns[index2], columns[index0] );
	VectorNormalize( columns[index2] );
	VectorNormalize( columns[index1] );
	MatrixSetColumn( columns[index1], index1, matrix );
	MatrixSetColumn( columns[index2], index2, matrix );
}

#define SIGN(x) ( (x) < 0 ? -1 : 1 )

static QAngle AlignAngles( const QAngle &angles, float cosineAlignAngle )
{
	matrix3x4_t alignMatrix;
	AngleMatrix( angles, alignMatrix );

	// NOTE: Must align z first
	for ( int j = 3; --j >= 0; )
	{
		Vector vec;
		MatrixGetColumn( alignMatrix, j, vec );
		for ( int i = 0; i < 3; i++ )
		{
			if ( fabs(vec[i]) > cosineAlignAngle )
			{
				vec[i] = SIGN(vec[i]);
				vec[(i+1)%3] = 0;
				vec[(i+2)%3] = 0;
				MatrixSetColumn( vec, j, alignMatrix );
				MatrixOrthogonalize( alignMatrix, j );
				break;
			}
		}
	}

	QAngle out;
	MatrixAngles( alignMatrix, out );
	return out;
}


static void TraceCollideAgainstBBox( const CPhysCollide *pCollide, const Vector &start, const Vector &end, const QAngle &angles, const Vector &boxOrigin, const Vector &mins, const Vector &maxs, trace_t *ptr )
{
	physcollision->TraceBox( boxOrigin, boxOrigin + (start-end), mins, maxs, pCollide, start, angles, ptr );

	if ( ptr->DidHit() )
	{
		ptr->endpos = start * (1-ptr->fraction) + end * ptr->fraction;
		ptr->startpos = start;
		ptr->plane.dist = -ptr->plane.dist;
		ptr->plane.normal *= -1;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Finds the nearest ragdoll sub-piece to a location and returns it
// Input  : *pTarget - entity that is the potential ragdoll
//			&position - position we're testing against
// Output : IPhysicsObject - sub-object (if any)
//-----------------------------------------------------------------------------
IPhysicsObject *Weaponizer_GetRagdollChildAtPosition( CBaseEntity *pTarget, const Vector &position )
{
	// Check for a ragdoll
	if ( dynamic_cast<CRagdollProp*>( pTarget ) == NULL )
		return NULL;

	// Get the root
	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pTarget->VPhysicsGetObjectList( pList, ARRAYSIZE( pList ) );
	
	IPhysicsObject *pBestChild = NULL;
	float			flBestDist = 99999999.0f;
	float			flDist;
	Vector			vPos;

	// Find the nearest child to where we're looking
	for ( int i = 0; i < count; i++ )
	{
		pList[i]->GetPosition( &vPos, NULL );
		
		flDist = ( position - vPos ).LengthSqr();

		if ( flDist < flBestDist )
		{
			pBestChild = pList[i];
			flBestDist = flDist;
		}
	}

	// Make this our base now
	pTarget->VPhysicsSwapObject( pBestChild );

	return pTarget->VPhysicsGetObject();
}

//-----------------------------------------------------------------------------
// Purpose: Computes a local matrix for the player clamped to valid carry ranges
//-----------------------------------------------------------------------------
// when looking level, hold bottom of object 8 inches below eye level
#define PLAYER_HOLD_LEVEL_EYES	-8

// when looking down, hold bottom of object 0 inches from feet
#define PLAYER_HOLD_DOWN_FEET	2

// when looking up, hold bottom of object 24 inches above eye level
#define PLAYER_HOLD_UP_EYES		24

// use a +/-30 degree range for the entire range of motion of pitch
#define PLAYER_LOOK_PITCH_RANGE	30

// player can reach down 2ft below his feet (otherwise he'll hold the object above the bottom)
#define PLAYER_REACH_DOWN_DISTANCE	24

static void ComputePlayerMatrix( CBasePlayer *pPlayer, matrix3x4_t &out )
{
	if ( !pPlayer )
		return;

	QAngle angles = pPlayer->EyeAngles();
	Vector origin = pPlayer->EyePosition();
	
	// 0-360 / -180-180
	//angles.x = init ? 0 : AngleDistance( angles.x, 0 );
	//angles.x = clamp( angles.x, -PLAYER_LOOK_PITCH_RANGE, PLAYER_LOOK_PITCH_RANGE );
	angles.x = 0;

	float feet = pPlayer->GetAbsOrigin().z + pPlayer->WorldAlignMins().z;
	float eyes = origin.z;
	float zoffset = 0;
	// moving up (negative pitch is up)
	if ( angles.x < 0 )
	{
		zoffset = RemapVal( angles.x, 0, -PLAYER_LOOK_PITCH_RANGE, PLAYER_HOLD_LEVEL_EYES, PLAYER_HOLD_UP_EYES );
	}
	else
	{
		zoffset = RemapVal( angles.x, 0, PLAYER_LOOK_PITCH_RANGE, PLAYER_HOLD_LEVEL_EYES, PLAYER_HOLD_DOWN_FEET + (feet - eyes) );
	}
	origin.z += zoffset;
	angles.x = 0;
	AngleMatrix( angles, origin, out );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
// derive from this so we can add save/load data to it
struct game_weaponizer_shadowcontrol_params_t : public hlshadowcontrol_params_t
{
	DECLARE_SIMPLE_DATADESC();
};

BEGIN_SIMPLE_DATADESC( game_weaponizer_shadowcontrol_params_t )
	
	DEFINE_FIELD( targetPosition,		FIELD_POSITION_VECTOR ),
	DEFINE_FIELD( targetRotation,		FIELD_VECTOR ),
	DEFINE_FIELD( maxAngular, FIELD_FLOAT ),
	DEFINE_FIELD( maxDampAngular, FIELD_FLOAT ),
	DEFINE_FIELD( maxSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( maxDampSpeed, FIELD_FLOAT ),
	DEFINE_FIELD( dampFactor, FIELD_FLOAT ),
	DEFINE_FIELD( teleportDistance,	FIELD_FLOAT ),

END_DATADESC()

//-----------------------------------------------------------------------------
class CWeaponizerGrabController : public IMotionEvent
{
	DECLARE_SIMPLE_DATADESC();

public:

	CWeaponizerGrabController( void );
	~CWeaponizerGrabController( void );
	void AttachEntity( CBasePlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhys, bool bIsMegaWeaponizer, const Vector &vGrabPosition, bool bUseGrabPosition );
	void DetachEntity( bool bClearVelocity );
	void OnRestore();
	void SetGrabNotifyTarget( IGrabControllerNotifyTarget *pTarget );

	bool UpdateObject( CBasePlayer *pPlayer, float flError );

	void SetTargetPosition( const Vector &target, const QAngle &targetOrientation );
	float ComputeError();
	float GetLoadWeight( void ) const { return m_flLoadWeight; }
	void SetAngleAlignment( float alignAngleCosine ) { m_angleAlignment = alignAngleCosine; }
	void SetIgnorePitch( bool bIgnore ) { m_bIgnoreRelativePitch = bIgnore; }
	QAngle TransformAnglesToPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer );
	QAngle TransformAnglesFromPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer );

	CBaseEntity *GetAttached() { return (CBaseEntity *)m_attachedEntity; }

	IMotionEvent::simresult_e Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular );
	float GetSavedMass( IPhysicsObject *pObject );

	bool IsObjectAllowedOverhead( CBaseEntity *pEntity );

private:

	IGrabControllerNotifyTarget *m_pNotifyTarget;

	// Compute the max speed for an attached object
	void ComputeMaxSpeed( CBaseEntity *pEntity, IPhysicsObject *pPhysics );

	game_weaponizer_shadowcontrol_params_t	m_shadow;
	float			m_timeToArrive;
	float			m_errorTime;
	float			m_error;
	float			m_contactAmount;
	float			m_angleAlignment;
	bool			m_bCarriedEntityBlocksLOS;
	bool			m_bIgnoreRelativePitch;
	float			m_flGrabTime;

	float			m_flLoadWeight;
	float			m_savedRotDamping[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	float			m_savedMass[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	EHANDLE			m_attachedEntity;
	QAngle			m_vecPreferredCarryAngles;
	bool			m_bHasPreferredCarryAngles;
	float			m_flDistanceOffset;

	QAngle			m_attachedAnglesPlayerSpace;
	Vector			m_attachedPositionObjectSpace;

	IPhysicsMotionController *m_controller;

	bool			m_bAllowObjectOverhead; // Can the player hold this object directly overhead? (Default is NO)

	friend class CWeaponWeaponizer;
};

BEGIN_SIMPLE_DATADESC( CWeaponizerGrabController )

	DEFINE_EMBEDDED( m_shadow ),

	DEFINE_FIELD( m_timeToArrive,		FIELD_FLOAT ),
	DEFINE_FIELD( m_errorTime,			FIELD_FLOAT ),
	DEFINE_FIELD( m_error,				FIELD_FLOAT ),
	DEFINE_FIELD( m_contactAmount,		FIELD_FLOAT ),
	DEFINE_AUTO_ARRAY( m_savedRotDamping,	FIELD_FLOAT ),
	DEFINE_AUTO_ARRAY( m_savedMass,	FIELD_FLOAT ),
	DEFINE_FIELD( m_flLoadWeight,		FIELD_FLOAT ),
	DEFINE_FIELD( m_bCarriedEntityBlocksLOS, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bIgnoreRelativePitch, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_attachedEntity,	FIELD_EHANDLE ),
	DEFINE_FIELD( m_angleAlignment, FIELD_FLOAT ),
	DEFINE_FIELD( m_vecPreferredCarryAngles, FIELD_VECTOR ),
	DEFINE_FIELD( m_bHasPreferredCarryAngles, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flDistanceOffset, FIELD_FLOAT ),
	DEFINE_FIELD( m_attachedAnglesPlayerSpace, FIELD_VECTOR ),
	DEFINE_FIELD( m_attachedPositionObjectSpace, FIELD_VECTOR ),
	DEFINE_FIELD( m_bAllowObjectOverhead, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flGrabTime, FIELD_TIME ),

	// Physptrs can't be inside embedded classes
	// DEFINE_PHYSPTR( m_controller ),

END_DATADESC()

const float DEFAULT_MAX_ANGULAR = 360.0f * 10.0f;
const float REDUCED_CARRY_MASS = 1.0f;

CWeaponizerGrabController::CWeaponizerGrabController( void )
{
	m_shadow.dampFactor = 1.0;
	m_shadow.teleportDistance = 0;
	m_errorTime = 0;
	m_error = 0;
	// make this controller really stiff!
	m_shadow.maxSpeed = 1000;
	m_shadow.maxAngular = DEFAULT_MAX_ANGULAR;
	m_shadow.maxDampSpeed = m_shadow.maxSpeed*2;
	m_shadow.maxDampAngular = m_shadow.maxAngular;
	m_attachedEntity = NULL;
	m_vecPreferredCarryAngles = vec3_angle;
	m_bHasPreferredCarryAngles = false;
	m_flDistanceOffset = 0;
}

CWeaponizerGrabController::~CWeaponizerGrabController( void )
{
	DetachEntity( false );
}

void CWeaponizerGrabController::OnRestore()
{
	if ( m_controller )
	{
		m_controller->SetEventHandler( this );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponizerGrabController::SetGrabNotifyTarget( IGrabControllerNotifyTarget *pTarget )
{
	m_pNotifyTarget = pTarget;
}

void CWeaponizerGrabController::SetTargetPosition( const Vector &target, const QAngle &targetOrientation )
{
	m_shadow.targetPosition = target;
	m_shadow.targetRotation = targetOrientation;

	m_timeToArrive = gpGlobals->frametime;

	CBaseEntity *pAttached = GetAttached();
	if ( pAttached )
	{
		IPhysicsObject *pObj = pAttached->VPhysicsGetObject();
		
		if ( pObj != NULL )
		{
			pObj->Wake();
		}
		else
		{
			DetachEntity( false );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CWeaponizerGrabController::ComputeError()
{
	if ( m_errorTime <= 0 )
		return 0;

	CBaseEntity *pAttached = GetAttached();
	if ( pAttached )
	{
		Vector pos;
		IPhysicsObject *pObj = pAttached->VPhysicsGetObject();
		
		if ( pObj )
		{	
			pObj->GetShadowPosition( &pos, NULL );

			float error = (m_shadow.targetPosition - pos).Length();
			if ( m_errorTime > 0 )
			{
				if ( m_errorTime > 1 )
				{
					m_errorTime = 1;
				}
				float speed = error / m_errorTime;
				if ( speed > m_shadow.maxSpeed )
				{
					error *= 0.5;
				}
				m_error = (1-m_errorTime) * m_error + error * m_errorTime;
			}
		}
		else
		{
			DevMsg( "Object attached to Weaponizer has no physics object\n" );
			DetachEntity( false );
			return 9999; // force detach
		}
	}
	
	if ( pAttached->IsEFlagSet( EFL_IS_BEING_LIFTED_BY_BARNACLE ) )
	{
		m_error *= 3.0f;
	}

	m_errorTime = 0;

	return m_error;
}


#define MASS_SPEED_SCALE	60
#define MAX_MASS			40

void CWeaponizerGrabController::ComputeMaxSpeed( CBaseEntity *pEntity, IPhysicsObject *pPhysics )
{
	m_shadow.maxSpeed = 1000;
	m_shadow.maxAngular = DEFAULT_MAX_ANGULAR;

	// Compute total mass...
	float flMass = PhysGetEntityMass( pEntity );
	float flMaxMass = weaponizer_maxmass.GetFloat();
	if ( flMass <= flMaxMass )
		return;

	float flLerpFactor = clamp( flMass, flMaxMass, 500.0f );
	flLerpFactor = SimpleSplineRemapVal( flLerpFactor, flMaxMass, 500.0f, 0.0f, 1.0f );

	float invMass = pPhysics->GetInvMass();
	float invInertia = pPhysics->GetInvInertia().Length();

	float invMaxMass = 1.0f / MAX_MASS;
	float ratio = invMaxMass / invMass;
	invMass = invMaxMass;
	invInertia *= ratio;

	float maxSpeed = invMass * MASS_SPEED_SCALE * 200;
	float maxAngular = invInertia * MASS_SPEED_SCALE * 360;

	m_shadow.maxSpeed = Lerp( flLerpFactor, m_shadow.maxSpeed, maxSpeed );
	m_shadow.maxAngular = Lerp( flLerpFactor, m_shadow.maxAngular, maxAngular );
}


QAngle CWeaponizerGrabController::TransformAnglesToPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer )
{
	if ( m_bIgnoreRelativePitch )
	{
		matrix3x4_t test;
		QAngle angleTest = pPlayer->EyeAngles();
		angleTest.x = 0;
		AngleMatrix( angleTest, test );
		return TransformAnglesToLocalSpace( anglesIn, test );
	}
	return TransformAnglesToLocalSpace( anglesIn, pPlayer->EntityToWorldTransform() );
}

QAngle CWeaponizerGrabController::TransformAnglesFromPlayerSpace( const QAngle &anglesIn, CBasePlayer *pPlayer )
{
	if ( m_bIgnoreRelativePitch )
	{
		matrix3x4_t test;
		QAngle angleTest = pPlayer->EyeAngles();
		angleTest.x = 0;
		AngleMatrix( angleTest, test );
		return TransformAnglesToWorldSpace( anglesIn, test );
	}
	return TransformAnglesToWorldSpace( anglesIn, pPlayer->EntityToWorldTransform() );
}


void CWeaponizerGrabController::AttachEntity( CBasePlayer *pPlayer, CBaseEntity *pEntity, IPhysicsObject *pPhys, bool bIsMegaWeaponizer, const Vector &vGrabPosition, bool bUseGrabPosition )
{
	// play the impact sound of the object hitting the player
	// used as feedback to let the player know he picked up the object
	int hitMaterial = pPhys->GetMaterialIndex();
	int playerMaterial = pPlayer->VPhysicsGetObject() ? pPlayer->VPhysicsGetObject()->GetMaterialIndex() : hitMaterial;
	PhysicsImpactSound( pPlayer, pPhys, CHAN_STATIC, hitMaterial, playerMaterial, 1.0, 64 );
	Vector position;
	QAngle angles;
	pPhys->GetPosition( &position, &angles );
	// If it has a preferred orientation, use that instead.
	Pickup_GetPreferredCarryAngles( pEntity, pPlayer, pPlayer->EntityToWorldTransform(), angles );

//	ComputeMaxSpeed( pEntity, pPhys );

	// If we haven't been killed by a grab, we allow the gun to grab the nearest part of a ragdoll
	if ( bUseGrabPosition )
	{
		IPhysicsObject *pChild = Weaponizer_GetRagdollChildAtPosition( pEntity, vGrabPosition );
		
		if ( pChild )
		{
			pPhys = pChild;
		}
	}

	// Carried entities can never block LOS
	m_bCarriedEntityBlocksLOS = pEntity->BlocksLOS();
	pEntity->SetBlocksLOS( false );
	m_controller = physenv->CreateMotionController( this );
	m_controller->AttachObject( pPhys, true );
	// Don't do this, it's causing trouble with constraint solvers.
	//m_controller->SetPriority( IPhysicsMotionController::HIGH_PRIORITY );

	pPhys->Wake();
	PhysSetGameFlags( pPhys, FVPHYSICS_PLAYER_HELD );
	SetTargetPosition( position, angles );
	m_attachedEntity = pEntity;
	IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	int count = pEntity->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
	m_flLoadWeight = 0;
	float damping = 10;
	float flFactor = count / 7.5f;
	if ( flFactor < 1.0f )
	{
		flFactor = 1.0f;
	}
	for ( int i = 0; i < count; i++ )
	{
		float mass = pList[i]->GetMass();
		pList[i]->GetDamping( NULL, &m_savedRotDamping[i] );
		m_flLoadWeight += mass;
		m_savedMass[i] = mass;

		// reduce the mass to prevent the player from adding crazy amounts of energy to the system
		pList[i]->SetMass( REDUCED_CARRY_MASS / flFactor );
		pList[i]->SetDamping( NULL, &damping );
	}
	
	// Give extra mass to the phys object we're actually picking up
	pPhys->SetMass( REDUCED_CARRY_MASS );
	pPhys->EnableDrag( false );

	m_errorTime = bIsMegaWeaponizer ? -1.5f : -1.0f; // 1 seconds until error starts accumulating
	m_error = 0;
	m_contactAmount = 0;

	m_attachedAnglesPlayerSpace = TransformAnglesToPlayerSpace( angles, pPlayer );
	if ( m_angleAlignment != 0 )
	{
		m_attachedAnglesPlayerSpace = AlignAngles( m_attachedAnglesPlayerSpace, m_angleAlignment );
	}

	// Ragdolls don't offset this way
	if ( dynamic_cast<CRagdollProp*>(pEntity) )
	{
		m_attachedPositionObjectSpace.Init();
	}
	else
	{
		VectorITransform( pEntity->WorldSpaceCenter(), pEntity->EntityToWorldTransform(), m_attachedPositionObjectSpace );
	}

	// If it's a prop, see if it has desired carry angles
	CPhysicsProp *pProp = dynamic_cast<CPhysicsProp *>(pEntity);
	if ( pProp )
	{
		m_bHasPreferredCarryAngles = pProp->GetPropDataAngles( "preferred_carryangles", m_vecPreferredCarryAngles );
		m_flDistanceOffset = pProp->GetCarryDistanceOffset();
	}
	else
	{
		m_bHasPreferredCarryAngles = false;
		m_flDistanceOffset = 0;
	}

	m_bAllowObjectOverhead = IsObjectAllowedOverhead( pEntity );
	
	// Flechettes are small and hard to see, so to clarity add a delay before absorbing them.
	m_flGrabTime = -1;
}

static void ClampPhysicsVelocity( IPhysicsObject *pPhys, float linearLimit, float angularLimit )
{
	Vector vel;
	AngularImpulse angVel;
	pPhys->GetVelocity( &vel, &angVel );
	float speed = VectorNormalize(vel) - linearLimit;
	float angSpeed = VectorNormalize(angVel) - angularLimit;
	speed = speed < 0 ? 0 : -speed;
	angSpeed = angSpeed < 0 ? 0 : -angSpeed;
	vel *= speed;
	angVel *= angSpeed;
	pPhys->AddVelocity( &vel, &angVel );
}

void CWeaponizerGrabController::DetachEntity( bool bClearVelocity )
{
	Assert(!PhysIsInCallback());
	CBaseEntity *pEntity = GetAttached();
	if ( pEntity )
	{
		// Restore the LS blocking state
		pEntity->SetBlocksLOS( m_bCarriedEntityBlocksLOS );
		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = pEntity->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
		for ( int i = 0; i < count; i++ )
		{
			IPhysicsObject *pPhys = pList[i];
			if ( !pPhys )
				continue;

			// on the odd chance that it's gone to sleep while under anti-gravity
			pPhys->EnableDrag( true );
			pPhys->Wake();
			pPhys->SetMass( m_savedMass[i] );
			pPhys->SetDamping( NULL, &m_savedRotDamping[i] );
			PhysClearGameFlags( pPhys, FVPHYSICS_PLAYER_HELD );
			if ( bClearVelocity )
			{
				PhysForceClearVelocity( pPhys );
			}
			else
			{
				ClampPhysicsVelocity( pPhys, hl2_normspeed.GetFloat() * 1.5f, 2.0f * 360.0f );
			}

		}
	}

	m_attachedEntity = NULL;
	physenv->DestroyMotionController( m_controller );
	m_controller = NULL;
}

static bool InContactWithHeavyObject( IPhysicsObject *pObject, float heavyMass )
{
	bool contact = false;
	IPhysicsFrictionSnapshot *pSnapshot = pObject->CreateFrictionSnapshot();
	while ( pSnapshot->IsValid() )
	{
		IPhysicsObject *pOther = pSnapshot->GetObject( 1 );
		if ( !pOther->IsMoveable() || pOther->GetMass() > heavyMass )
		{
			contact = true;
			break;
		}
		pSnapshot->NextFrictionData();
	}
	pObject->DestroyFrictionSnapshot( pSnapshot );
	return contact;
}

IMotionEvent::simresult_e CWeaponizerGrabController::Simulate( IPhysicsMotionController *pController, IPhysicsObject *pObject, float deltaTime, Vector &linear, AngularImpulse &angular )
{
	game_weaponizer_shadowcontrol_params_t shadowParams = m_shadow;
	if ( InContactWithHeavyObject( pObject, GetLoadWeight() ) )
	{
		m_contactAmount = Approach( 0.1f, m_contactAmount, deltaTime*2.0f );
	}
	else
	{
		m_contactAmount = Approach( 1.0f, m_contactAmount, deltaTime*2.0f );
	}
	shadowParams.maxAngular = m_shadow.maxAngular * m_contactAmount * m_contactAmount * m_contactAmount;
	m_timeToArrive = pObject->ComputeShadowControl( shadowParams, m_timeToArrive, deltaTime );
	
	// Slide along the current contact points to fix bouncing problems
	Vector velocity;
	AngularImpulse angVel;
	pObject->GetVelocity( &velocity, &angVel );
	PhysComputeSlideDirection( pObject, velocity, angVel, &velocity, &angVel, GetLoadWeight() );
	pObject->SetVelocityInstantaneous( &velocity, NULL );

	linear.Init();
	angular.Init();
	m_errorTime += deltaTime;

	return SIM_LOCAL_ACCELERATION;
}

float CWeaponizerGrabController::GetSavedMass( IPhysicsObject *pObject )
{
	CBaseEntity *pHeld = m_attachedEntity;
	if ( pHeld )
	{
		if ( pObject->GetGameData() == (void*)pHeld )
		{
			IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
			int count = pHeld->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
			for ( int i = 0; i < count; i++ )
			{
				if ( pList[i] == pObject )
					return m_savedMass[i];
			}
		}
	}
	return 0.0f;
}

//-----------------------------------------------------------------------------
// Is this an object that the player is allowed to lift to a position 
// directly overhead? The default behavior prevents lifting objects directly
// overhead, but there are exceptions for gameplay purposes.
//-----------------------------------------------------------------------------
bool CWeaponizerGrabController::IsObjectAllowedOverhead( CBaseEntity *pEntity )
{
	// Allow combine balls overhead 
	if( UTIL_IsCombineBallDefinite(pEntity) )
		return true;

	// Allow props that are specifically flagged as such
	CPhysicsProp *pPhysProp = dynamic_cast<CPhysicsProp *>(pEntity);
	if ( pPhysProp != NULL && pPhysProp->HasInteraction( PROPINTER_PHYSGUN_ALLOW_OVERHEAD ) )
		return true;

	// String checks are fine here, we only run this code one time- when the object is picked up.
	if( pEntity->ClassMatches("grenade_helicopter") )
		return true;

	if( pEntity->ClassMatches("weapon_striderbuster") )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Player pickup controller
//-----------------------------------------------------------------------------
class CWeaponizerPlayerPickupController : public CBaseEntity
{
	DECLARE_DATADESC();
	DECLARE_CLASS( CWeaponizerPlayerPickupController, CBaseEntity );
public:
	void Init( CBasePlayer *pPlayer, CBaseEntity *pObject );
	void Shutdown( bool bThrown = false );
	bool OnControls( CBaseEntity *pControls ) { return true; }
	void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	void OnRestore()
	{
		m_grabController.OnRestore();
	}
	void VPhysicsUpdate( IPhysicsObject *pPhysics ){}
	void VPhysicsShadowUpdate( IPhysicsObject *pPhysics ) {}

	bool IsHoldingEntity( CBaseEntity *pEnt );
	CWeaponizerGrabController &GetGrabController() { return m_grabController; }

private:
	CWeaponizerGrabController		m_grabController;
	CBasePlayer			*m_pPlayer;
};

LINK_ENTITY_TO_CLASS( weaponizer_player_pickup, CWeaponizerPlayerPickupController );

//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CWeaponizerPlayerPickupController )

	DEFINE_EMBEDDED( m_grabController ),

	// Physptrs can't be inside embedded classes
	DEFINE_PHYSPTR( m_grabController.m_controller ),

	DEFINE_FIELD( m_pPlayer,		FIELD_CLASSPTR ),
	
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPlayer - 
//			*pObject - 
//-----------------------------------------------------------------------------
void CWeaponizerPlayerPickupController::Init( CBasePlayer *pPlayer, CBaseEntity *pObject )
{
	// Holster player's weapon
	if ( pPlayer->GetActiveWeapon() )
	{
		if ( !pPlayer->GetActiveWeapon()->CanHolster() || !pPlayer->GetActiveWeapon()->Holster() )
		{
			Shutdown();
			return;
		}
	}

	CHL2_Player *pOwner = (CHL2_Player *)ToBasePlayer( pPlayer );
	if ( pOwner )
	{
		pOwner->EnableSprint( false );
	}

	// If the target is debris, convert it to non-debris
	if ( pObject->GetCollisionGroup() == COLLISION_GROUP_DEBRIS )
	{
		// Interactive debris converts back to debris when it comes to rest
		pObject->SetCollisionGroup( COLLISION_GROUP_INTERACTIVE_DEBRIS );
	}

	// done so I'll go across level transitions with the player
	SetParent( pPlayer );
	m_grabController.SetIgnorePitch( true );
	m_grabController.SetAngleAlignment( DOT_30DEGREE );
	m_pPlayer = pPlayer;
	IPhysicsObject *pPhysics = pObject->VPhysicsGetObject();
	
	Pickup_OnPhysGunPickup( pObject, m_pPlayer, PICKED_UP_BY_PLAYER );
	
	m_grabController.AttachEntity( pPlayer, pObject, pPhysics, false, vec3_origin, false );
	
	m_pPlayer->m_Local.m_iHideHUD |= HIDEHUD_WEAPONSELECTION;
	m_pPlayer->SetUseEntity( this );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bool - 
//-----------------------------------------------------------------------------
void CWeaponizerPlayerPickupController::Shutdown( bool bThrown )
{
	CBaseEntity *pObject = m_grabController.GetAttached();

	bool bClearVelocity = false;
	if ( !bThrown && pObject && pObject->VPhysicsGetObject() && pObject->VPhysicsGetObject()->GetContactPoint(NULL,NULL) )
	{
		bClearVelocity = true;
	}

	m_grabController.DetachEntity( bClearVelocity );

	if ( pObject != NULL )
	{
		Pickup_OnPhysGunDrop( pObject, m_pPlayer, bThrown ? THROWN_BY_PLAYER : DROPPED_BY_PLAYER );
	}

	if ( m_pPlayer )
	{
		CHL2_Player *pOwner = (CHL2_Player *)ToBasePlayer( m_pPlayer );
		if ( pOwner )
		{
			pOwner->EnableSprint( true );
		}

		m_pPlayer->SetUseEntity( NULL );
		if ( m_pPlayer->GetActiveWeapon() )
		{
			if ( !m_pPlayer->GetActiveWeapon()->Deploy() )
			{
				// We tried to restore the player's weapon, but we couldn't.
				// This usually happens when they're holding an empty weapon that doesn't
				// autoswitch away when out of ammo. Switch to next best weapon.
				m_pPlayer->SwitchToNextBestWeapon( NULL );
			}
		}

		m_pPlayer->m_Local.m_iHideHUD &= ~HIDEHUD_WEAPONSELECTION;
	}
	Remove();
}


void CWeaponizerPlayerPickupController::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if ( ToBasePlayer(pActivator) == m_pPlayer )
	{
		CBaseEntity *pAttached = m_grabController.GetAttached();

		// UNDONE: Use vphysics stress to decide to drop objects
		// UNDONE: Must fix case of forcing objects into the ground you're standing on (causes stress) before that will work
		if ( !pAttached || useType == USE_OFF || (m_pPlayer->m_nButtons & IN_ATTACK2) || m_grabController.ComputeError() > 12 )
		{
			Shutdown();
			return;
		}
		
		//Adrian: Oops, our object became motion disabled, let go!
		IPhysicsObject *pPhys = pAttached->VPhysicsGetObject();
		if ( pPhys && pPhys->IsMoveable() == false )
		{
			Shutdown();
			return;
		}

#if STRESS_TEST
		vphysics_objectstress_t stress;
		CalculateObjectStress( pPhys, pAttached, &stress );
		if ( stress.exertedStress > 250 )
		{
			Shutdown();
			return;
		}
#endif
		// +ATTACK will throw phys objects
		if ( m_pPlayer->m_nButtons & IN_ATTACK )
		{
			Shutdown( true );
			Vector vecLaunch;
			m_pPlayer->EyeVectors( &vecLaunch );
			// JAY: Scale this with mass because some small objects really go flying
			float massFactor = clamp( pPhys->GetMass(), 0.5, 15 );
			massFactor = RemapVal( massFactor, 0.5, 15, 0.5, 4 );
			vecLaunch *= weaponizer_player_throwforce.GetFloat() * massFactor;

			pPhys->ApplyForceCenter( vecLaunch );
			AngularImpulse aVel = RandomAngularImpulse( -10, 10 ) * massFactor;
			pPhys->ApplyTorqueCenter( aVel );
			return;
		}

		if ( useType == USE_SET )
		{
			// update position
			m_grabController.UpdateObject( m_pPlayer, 12 );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pEnt - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponizerPlayerPickupController::IsHoldingEntity( CBaseEntity *pEnt )
{
	return ( m_grabController.GetAttached() == pEnt );
}

void Weaponizer_PlayerPickupObject( CBasePlayer *pPlayer, CBaseEntity *pObject )
{
	//Don't pick up if we don't have a phys object.
	if ( pObject->VPhysicsGetObject() == NULL )
		 return;

	CWeaponizerPlayerPickupController *pController = (CWeaponizerPlayerPickupController *)CBaseEntity::Create( "weaponizer_player_pickup", pObject->GetAbsOrigin(), vec3_angle, pPlayer );
	
	if ( !pController )
		return;

	pController->Init( pPlayer, pObject );
}

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Weaponizer
//-----------------------------------------------------------------------------

#define	NUM_BEAMS	4
#define	NUM_SPRITES	6

struct weaponizer_thrown_objects_t
{
	float				fTimeThrown;
	EHANDLE				hEntity;

	DECLARE_SIMPLE_DATADESC();
};

BEGIN_SIMPLE_DATADESC( weaponizer_thrown_objects_t )
	DEFINE_FIELD( fTimeThrown, FIELD_TIME ),
	DEFINE_FIELD( hEntity,	FIELD_EHANDLE	),
END_DATADESC()

class IGrabControllerNotifyTarget
{
public:
	virtual void OnEntityFullyGrabbed( CBaseEntity *pEntity ) = 0;
};


typedef void (CWeaponWeaponizer::*WeaponizerShootFunc_t)( attackstate_t eAttackState );

struct WeaponParams_t
{
	bool bContinuous;					// Whether the user can press & hold to keep firing
	int nMinProjectilesPerShot;			// At least this many flying out bits fly out per shot
	int nMaxProjectilesPerShot;			// No more than this many flying out bits fly out per shot
	float flRateOfFire;					// Seconds per shot
	float flPrimaryAmmoBurnRate;		// Primary ammo consumed per shot while firing
	float flSecondaryAmmoBurnRate;		// Secondary ammo consumed per shot while firing
	float flPrimaryAmmoRegenRate;		// Seconds per round of primary ammo regenerated while not firing
	float flSecondaryAmmoRegenRate;		// Seconds per round of secondary ammo regenerated while not firing
	int nPrimaryAmmoMax;				// Max ammo the gun can hold for primary fire
	int nSecondaryAmmoMax;				// Max ammo for gun can hold for secondary fire
	bool bFusion;						// Is this weapon a fusion?
	const char *absorbsound;			// Sound effect to play when absorbing this
	const char *absorbeffect;			// Effect to play when absorbing this
	const char *absorbattach;			// Attachment to use for absorb effects
	WeaponizerShootFunc_t funcShoot;	// Function to call from PrimaryAttack
};


struct AmmoParams_t
{
	int iMaxAmmo;				// The maximum carry for this ammo type

	bool bMassBased;			// Whether we absorb based on mass or 
	float flAmmoPerAbsorb;		// Either ammo per kg or ammo per absorb, depending on the above

	bool bRemoveOnAbsorb;		// Whether to remove the entity after absorbing the ammo
};


class CWeaponWeaponizer : public CBaseHLCombatWeapon, IGrabControllerNotifyTarget
{
public:
	DECLARE_CLASS( CWeaponWeaponizer, CBaseHLCombatWeapon );

	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CWeaponWeaponizer( void );
	~CWeaponWeaponizer( void );

	void Drop( const Vector &vecVelocity );
	void Precache();
	void Equip( CBaseCombatCharacter *pOwner );

	virtual void	Spawn();
	virtual void	OnRestore();
	virtual void	StopLoopingSounds();
	virtual void	UpdateOnRemove(void);

	bool Reload();
	void	PrimaryAttack( attackstate_t eState );
	void	SecondaryAttack( attackstate_t eState );
	void	WeaponIdle();
	void	ItemPreFrame();
	void	ItemPostFrame();
	void	ItemBusyFrame();

	void	AbsorbScan();
	int		GetAmmoCount( int iMaterial );

	virtual float GetMaxAutoAimDeflection() { return 0.90f; }

	void AbsorbEntity( CBaseEntity *pEntity, Vector vTracePos );
	void OnEntityFullyGrabbed( CBaseEntity *pEntity );
	void SelectWeapon();
	void SetSkin( int skinNum );

	void CheckWaterLevel( CBasePlayer *pOwner );

	void	ForceDrop( void );
	bool	DropIfEntityHeld( CBaseEntity *pTarget );	// Drops its held entity if it matches the entity passed in
	CWeaponizerGrabController &GetGrabController() { return m_grabController; }

	bool	CanHolster( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo = NULL );
	bool	Deploy( void );

	bool	HasAnyAmmo( void ) { return true; }

	void	InputBecomeMegaCannon( inputdata_t &inputdata );

	virtual const char *GetShootSound( int iIndex ) const;
	void	RecordThrownObject( CBaseEntity *pObject );
	void	PurgeThrownObjects();
	bool	IsAccountableForObject( CBaseEntity *pObject );
	
	bool	ShouldDisplayHUDHint() { return true; }

	void ShootDefault( attackstate_t eState );
	void ShootSphere( attackstate_t eState );//theaperturecat unused?
	void ShootFlak( attackstate_t eState );
	void ShootFlakGrenade( attackstate_t eState );//theaperturecat unused?
	void ShootZapFlak( attackstate_t eState );//theaperturecat unused?
	void ShootZapFlakBeam( attackstate_t eState );//theaperturecat unused?
	void ShootFlamingFlak( attackstate_t eState );//theaperturecat unused?
	void ShootFlames( attackstate_t eState );
	void ShootElectrical( attackstate_t eState );
	void ShootFlamingSphere( attackstate_t eState ); //Fusion test //theaperturecat unused?
	void ShootZapSphere( attackstate_t eState ); //theaperturecat unused?
	void ShootLiquidFuel( attackstate_t eState );
	void ShootLiquidWater( attackstate_t eState );

protected:
	enum FindObjectResult_t
	{
		OBJECT_FOUND = 0,
		OBJECT_NOT_FOUND,
		OBJECT_BEING_DETACHED,
	};

	void	DoMegaEffect( int effectType, Vector *pos = NULL );
	void	DoEffect( int effectType, Vector *pos = NULL );

	void	OpenElements( void );
	void	CloseElements( void );

	// Pickup and throw objects.
	bool	CanPickupObject( CBaseEntity *pTarget );
	void	CheckForTarget( void );
	FindObjectResult_t		FindObject( void );
	void					FindObjectTrace( CBasePlayer *pPlayer, trace_t *pTraceResult );
	CBaseEntity *MegaWeaponizerFindObjectInCone( const Vector &vecOrigin, const Vector &vecDir, float flCone, float flCombineBallCone, bool bOnlyCombineBalls );
	bool	AttachObject( CBaseEntity *pObject, const Vector &vPosition );
	void	UpdateObject( void );
	void	DetachObject( bool playSound = true, bool wasLaunched = false );
	void	LaunchObject( const Vector &vecDir, float flForce );
	void	StartEffects( void );	// Initialize all sprites and beams
	void	StopEffects( bool stopSound = true );	// Hide all effects temporarily
	void	DestroyEffects( void );	// Destroy all sprites and beams

	// Punt objects - this is pointing at an object in the world and applying a force to it.
	void	PuntNonVPhysics( CBaseEntity *pEntity, const Vector &forward, trace_t &tr );
	void	PuntVPhysics( CBaseEntity *pEntity, const Vector &forward, trace_t &tr );
	void	PuntRagdoll( CBaseEntity *pEntity, const Vector &forward, trace_t &tr );

	// Velocity-based throw common to punt and launch code.
	void	ApplyVelocityBasedForce( CBaseEntity *pEntity, const Vector &forward, const Vector &vecHitPos, PhysGunForce_t reason );

	// Physgun effects
	void	DoEffectClosed( void );
	void	DoMegaEffectClosed( void );
	
	void	DoEffectReady( void );
	void	DoMegaEffectReady( void );

	void	DoMegaEffectHolding( void );
	void	DoEffectHolding( void );

	void	DoMegaEffectLaunch( Vector *pos );
	void	DoEffectLaunch( Vector *pos );

	void	DoEffectNone( void );

	void ItemHolsterFrame( void );

	// Trace length
	float	TraceLength();

	// Do we have the super-phys gun?
	inline bool	IsMegaWeaponizer()
	{
		return true;
	}

	// Sprite scale factor 
	float	SpriteScaleFactor();

	float			GetLoadPercentage();
	CSoundPatch		*GetMotorSound( void );

	void	DryFire( void );
	void	PrimaryFireEffect( void );

	// What happens when the physgun picks up something 
	void	Physgun_OnPhysGunPickup( CBaseEntity *pEntity, CBasePlayer *pOwner, PhysGunPickup_t reason );

	bool	EntityAllowsPunts( CBaseEntity *pEntity );

	bool IsAmmoFull( int iType );

	bool	m_bOpen;
	bool	m_bActive;
	int		m_nChangeState;			//For delayed state change of elements
	float	m_flCheckSuppressTime;	//Amount of time to suppress the checking for targets
	bool	m_flLastDenySoundPlayed;	//Debounce for deny sound
	int		m_nAttackDebounce;

	CNetworkVar( bool, m_bIsCurrentlyUpgrading );
	CNetworkVar( float, m_flTimeForceView );

	float	m_flElementDebounce;
	float	m_flElementPosition;
	float	m_flElementDestination;

	CHandle<CBeam>		m_hBeams[NUM_BEAMS];
	CHandle<CSprite>	m_hGlowSprites[NUM_SPRITES];
	CHandle<CSprite>	m_hEndSprites[2];
	float				m_flEndSpritesOverride[2];
	CHandle<CSprite>	m_hCenterSprite;
	CHandle<CSprite>	m_hBlastSprite;

	CSoundPatch			*m_sndMotor;		// Whirring sound for the gun
	
	CWeaponizerGrabController		m_grabController;
	
	int					m_EffectState;		// Current state of the effects on the gun

	bool				m_bWeaponizerState;

	// A list of the objects thrown or punted recently, and the time done so.
	CUtlVector< weaponizer_thrown_objects_t >	m_ThrownEntities;

	float				m_flTimeNextObjectPurge;
	float				m_flAmmoRemainder; // Holds fractional part of ammo for accumulation

	CNetworkVar( bool, m_bFiringSecondary );
	CNetworkVar( bool, m_bFiringPrimary );

	attackstate_t		m_eAttackState;

	bool				m_bFusionMode;
	bool				m_bChargedFusion;
	float				m_flFusionChargeTime;
	float				m_flNextReloadTime;

	float				m_flNextPrimaryRegenTime;
	float				m_flNextSecondaryRegenTime;
	float				m_flNextPrimaryAnimationTime;
	float				m_flNextSecondaryAnimationTime;
	float				m_flNextAbsorbAnimationTime;

	float				m_flNextFlechetteFireTime;
	int					m_iQueuedFlechettes;
	
	WeaponParams_t m_weaponParams[LAST_WEAPON_PARAM_COUNT];

	void SubtractAmmo( int iType );

	float m_flNextAbsorbTime;
	float m_flNextWarningSound;

	CSoundPatch *m_pFiringLoop;
	CSoundPatch *m_pLiquidLoop;

	int	  m_iModifierCount;

	CNetworkVar( int, m_iModifierType );
private:

	WeaponizerMaterialTypes_t GetWeaponizerMaterialType( CBaseEntity *pEntity );

	void UTIL_WeaponizerTraceLine( const Vector &vecAbsStart, const Vector &vecAbsEnd, CBaseEntity *pTraceOwner, CBaseCombatWeapon *pPhyscannon, trace_t *pTrace );

protected:

	// Because the physcannon is a leaf class, we can use
	// static variables to store this information, and save some memory.
	// Should the physcannon end up having inheritors, their activate may
	// stomp these numbers, in which case you should make these ordinary members
	// again.
	//
	// The physcannon also caches some pose parameters in SetupGlobalModelData().
	static int m_poseActive;
	static bool m_sbStaticPoseParamsLoaded;
};

bool CWeaponWeaponizer::m_sbStaticPoseParamsLoaded = false;
int CWeaponWeaponizer::m_poseActive = 0;
static char s_szEmpty[1] = { '\0' };

static WeaponParams_t s_Default =
{
	true, //bool bContinuous;				// Whether the user can press & hold to keep firing
	0, //int nMinProjectilesPerShot;		// At least this many flying out bits fly out per shot
	0, //int nMaxProjectilesPerShot;		// No more than this many flying out bits fly out per shot
	1.0f, //float flRateOfFire;				// Seconds per shot
	0.0f, //float flPrimaryAmmoBurnRate;	// Primary ammo consumed per shot while firing
	0.0f, //float flSecondaryAmmoBurnRate;	// Secondary ammo consumed per shot while firing
	0.0f, //float flPrimaryAmmoRegenRate;	// Seconds to regenerate a round of primary ammo while not firing
	0.0f, //float flSecondaryAmmoRegenRate;	// Seconds to regenerate a round of secondary ammo while not firing
	-1, // int nPrimaryAmmoMax				// Maximum allowed primary ammo
	-1, // int nSecondaryAmmoMax			// Maximum allowed secondary ammo
	false,
	s_szEmpty,
	s_szEmpty,
	s_szEmpty,
	&CWeaponWeaponizer::ShootDefault,
};


static WeaponParams_t s_Flak =
{
	false, //bool bContinuous;				// Whether the user can press & hold to keep firing
	8, //int nMinProjectilesPerShot;		// At least this many flying out bits fly out per shot
	12, //int nMaxProjectilesPerShot;		// No more than this many flying out bits fly out per shot
	0.45f, //float flRateOfFire;				// Seconds per shot
	1.0f, //float flPrimaryAmmoBurnRate;	// Primary ammo consumed per shot while firing
	0.0f, //float flSecondaryAmmoBurnRate;	// Secondary ammo consumed per shot while firing
	0.0f, //float flPrimaryAmmoRegenRate;	// Seconds to regenerate a round of primary ammo while not firing
	0.0f, //float flSecondaryAmmoRegenRate;	// Seconds to regenerate a round of secondary ammo while not firing
	-1, // int nPrimaryAmmoMax				// Maximum allowed primary ammo
	-1, // int nSecondaryAmmoMax			// Maximum allowed secondary ammo
	false,
	s_szEmpty,
	s_szEmpty,
	s_szEmpty,
	&CWeaponWeaponizer::ShootFlak,
};


static WeaponParams_t s_Flamethrower =
{
	true, //bool bContinuous;				// Whether the user can press & hold to keep firing
	1, //int nMinProjectilesPerShot;		// At least this many flying out bits fly out per shot
	1, //int nMaxProjectilesPerShot;		// No more than this many flying out bits fly out per shot
	0.1f, //float flRateOfFire;				// Seconds per shot
	1.0f, //float flPrimaryAmmoBurnRate;	// Primary ammo consumed per shot while firing
	0.0f, //float flSecondaryAmmoBurnRate;	// Secondary ammo consumed per shot while firing
	0.0f, //float flPrimaryAmmoRegenRate;	// Seconds to regenerate a round of primary ammo while not firing
	0.0f, //float flSecondaryAmmoRegenRate;	// Seconds to regenerate a round of secondary ammo while not firing
	-1, // int nPrimaryAmmoMax				// Maximum allowed primary ammo
	-1, // int nSecondaryAmmoMax			// Maximum allowed secondary ammo
	false,
	"Weaponizer_AbsorbFire",
	"absorb_fire",
	"ltube_muzzle",
	&CWeaponWeaponizer::ShootFlames,
};

static WeaponParams_t s_Electrical =
{
	false, //bool bContinuous;				// Whether the user can press & hold to keep firing
	1, //int nMinProjectilesPerShot;		// At least this many flying out bits fly out per shot
	1, //int nMaxProjectilesPerShot;		// No more than this many flying out bits fly out per shot
	0.5f, //float flRateOfFire;				// Seconds per shot
	2.0f, //float flPrimaryAmmoBurnRate;	// Primary ammo consumed per shot while firing
	0.0f, //float flSecondaryAmmoBurnRate;	// Secondary ammo consumed per shot while firing
	0.0f, //float flPrimaryAmmoRegenRate;	// Seconds to regenerate a round of primary ammo while not firing
	0.0f, //float flSecondaryAmmoRegenRate;	// Seconds to regenerate a round of secondary ammo while not firing
	-1, // int nPrimaryAmmoMax				// Maximum allowed primary ammo
	-1, // int nSecondaryAmmoMax				// Maximum allowed secondary ammo
	false,
	"Weaponizer_AbsorbElectrical",
	"absorb_electricity",
	"ltube_muzzle",
	&CWeaponWeaponizer::ShootElectrical,
};

static WeaponParams_t s_LiquidFuel =
{
	true, //bool bContinuous;				// Whether the user can press & hold to keep firing
	1, //int nMinProjectilesPerShot;		// At least this many flying out bits fly out per shot
	1, //int nMaxProjectilesPerShot;		// No more than this many flying out bits fly out per shot
	0.01f, //float flRateOfFire;				// Seconds per shot
	0.0f, //float flPrimaryAmmoBurnRate;	// Primary ammo consumed per shot while firing
	1.0f, //float flSecondaryAmmoBurnRate;	// Secondary ammo consumed per shot while firing
	0.0f, //float flPrimaryAmmoRegenRate;	// Seconds to regenerate a round of primary ammo while not firing
	0.0f, //float flSecondaryAmmoRegenRate;	// Seconds to regenerate a round of secondary ammo while not firing
	-1, // int nPrimaryAmmoMax				// Maximum allowed primary ammo
	-1, // int nSecondaryAmmoMax				// Maximum allowed secondary ammo
	false,
	s_szEmpty,
	"absorb_fuel",
	"rtube_muzzle",
	&CWeaponWeaponizer::ShootLiquidFuel,
};

static WeaponParams_t s_LiquidWater =
{
	true, //bool bContinuous;				// Whether the user can press & hold to keep firing
	1, //int nMinProjectilesPerShot;		// At least this many flying out bits fly out per shot
	1, //int nMaxProjectilesPerShot;		// No more than this many flying out bits fly out per shot
	0.01f, //float flRateOfFire;			// Seconds per shot
	0.0f, //float flPrimaryAmmoBurnRate;	// Primary ammo consumed per shot while firing
	1.0f, //float flSecondaryAmmoBurnRate;	// Secondary ammo consumed per shot while firing
	0.0f, //float flPrimaryAmmoRegenRate;	// Seconds to regenerate a round of primary ammo while not firing
	0.0f, //float flSecondaryAmmoRegenRate;	// Seconds to regenerate a round of secondary ammo while not firing
	-1, // int nPrimaryAmmoMax				// Maximum allowed primary ammo
	-1, // int nSecondaryAmmoMax			// Maximum allowed secondary ammo
	false,
	s_szEmpty,
	"absorb_water",
	"rtube_muzzle",
	&CWeaponWeaponizer::ShootLiquidWater,
};

static AmmoParams_t s_AmmoParams[WEAPONIZER_NUM_MATERIALS] =
{
	// iMaxAmmo, bMassBased, flAmmoPer, bRemove
	{ 0, false, 0, false }, //WEAPONIZER_MATERIAL_TYPE_NONE
	{ 40, false, 5, false }, //WEAPONIZER_MATERIAL_TYPE_FIRE
	{ 40, false, 10, false }, //WEAPONIZER_MATERIAL_TYPE_ELECTRICAL
	{ 30, true, 8.0f, true }, //WEAPONIZER_MATERIAL_TYPE_CONCRETE
	{ 30, true, 8.0f, true }, //WEAPONIZER_MATERIAL_TYPE_METAL
	{ 0, false, 0, false }, //WEAPONIZER_NUM_PRIMARY_MATERIALS
	{ 300, false, 60, false }, //WEAPONIZER_MATERIAL_TYPE_EXPLOSIVE
	{ 300, false, 60, false }, //WEAPONIZER_MATERIAL_TYPE_WATER
};

IMPLEMENT_SERVERCLASS_ST(CWeaponWeaponizer, DT_WeaponWeaponizer)
	SendPropBool( SENDINFO( m_bFiringSecondary ) ),
	SendPropBool( SENDINFO( m_bFiringPrimary ) ),
	SendPropInt( SENDINFO( m_iModifierType ), 32, SPROP_UNSIGNED ),
//	SendPropFloat( SENDINFO( m_flTimeForceView ) ),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_weaponizer, CWeaponWeaponizer );
PRECACHE_WEAPON_REGISTER( weapon_weaponizer );

BEGIN_DATADESC( CWeaponWeaponizer )

	DEFINE_FIELD( m_bOpen, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bActive, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_iQueuedFlechettes, FIELD_INTEGER ),
	DEFINE_FIELD( m_flNextFlechetteFireTime, FIELD_TIME ),
	DEFINE_FIELD( m_nChangeState, FIELD_INTEGER ),
	DEFINE_FIELD( m_flCheckSuppressTime, FIELD_TIME ),
	DEFINE_FIELD( m_flElementDebounce, FIELD_TIME ),
	DEFINE_FIELD( m_flElementPosition, FIELD_FLOAT ),
	DEFINE_FIELD( m_flElementDestination, FIELD_FLOAT ),
	DEFINE_FIELD( m_nAttackDebounce, FIELD_INTEGER ),
	DEFINE_FIELD( m_bIsCurrentlyUpgrading, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flTimeForceView, FIELD_TIME ),
	DEFINE_FIELD( m_EffectState, FIELD_INTEGER ),

	DEFINE_AUTO_ARRAY( m_hBeams, FIELD_EHANDLE ),
	DEFINE_AUTO_ARRAY( m_hGlowSprites, FIELD_EHANDLE ),
	DEFINE_AUTO_ARRAY( m_hEndSprites, FIELD_EHANDLE ),
	DEFINE_AUTO_ARRAY( m_flEndSpritesOverride, FIELD_TIME ),
	DEFINE_FIELD( m_hCenterSprite, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hBlastSprite, FIELD_EHANDLE ),
	DEFINE_FIELD( m_flLastDenySoundPlayed, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bFusionMode, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bWeaponizerState, FIELD_BOOLEAN ),
	DEFINE_SOUNDPATCH( m_sndMotor ),

	DEFINE_FIELD( m_flNextPrimaryAnimationTime , FIELD_TIME ),
	DEFINE_FIELD( m_flNextSecondaryAnimationTime , FIELD_TIME ),
	DEFINE_FIELD( m_flNextAbsorbAnimationTime, FIELD_TIME ),

	DEFINE_FIELD( m_iModifierCount, FIELD_INTEGER ),
	DEFINE_FIELD( m_iModifierType, FIELD_INTEGER ),

	DEFINE_EMBEDDED( m_grabController ),

	// Physptrs can't be inside embedded classes
	DEFINE_PHYSPTR( m_grabController.m_controller ),

	DEFINE_UTLVECTOR( m_ThrownEntities, FIELD_EMBEDDED ),

	DEFINE_FIELD( m_flTimeNextObjectPurge, FIELD_TIME ),
	DEFINE_FIELD( m_flFusionChargeTime, FIELD_TIME ),
	DEFINE_FIELD( m_flNextReloadTime, FIELD_TIME ),
	DEFINE_FIELD( m_flAmmoRemainder, FIELD_FLOAT ),
	DEFINE_FIELD( m_eAttackState, FIELD_INTEGER ),

	DEFINE_FIELD( m_bFiringSecondary, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bFiringPrimary, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_flNextAbsorbTime, FIELD_TIME ),
	DEFINE_FIELD( m_flNextWarningSound, FIELD_TIME ),

END_DATADESC()


enum
{
	ELEMENT_STATE_NONE = -1,
	ELEMENT_STATE_OPEN,
	ELEMENT_STATE_CLOSED,
};

enum
{
	EFFECT_NONE,
	EFFECT_CLOSED,
	EFFECT_READY,
	EFFECT_HOLDING,
	EFFECT_LAUNCH,
};


//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CWeaponWeaponizer::CWeaponWeaponizer( void )
{
	m_iClip1 = m_iClip2 = 0;

	m_flElementPosition		= 0.0f;
	m_flElementDestination	= 0.0f;
	m_bOpen					= false;
	m_nChangeState			= ELEMENT_STATE_NONE;
	m_flCheckSuppressTime	= 0.0f;
	m_EffectState			= EFFECT_NONE;
	m_flLastDenySoundPlayed	= false;

	m_flEndSpritesOverride[0] = 0.0f;
	m_flEndSpritesOverride[1] = 0.0f;

	m_bWeaponizerState = false;
	m_iPrimaryAmmoType = m_iSecondaryAmmoType = 0;
}

CWeaponWeaponizer::~CWeaponWeaponizer( void )
{
	CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

	if ( m_pFiringLoop )
	{
		controller.SoundDestroy( m_pFiringLoop );
		m_pFiringLoop = NULL;
	}

	if ( m_pLiquidLoop )
	{
		controller.SoundDestroy( m_pLiquidLoop );
		m_pLiquidLoop = NULL;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Precache
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::Precache( void )
{
	PrecacheModel( PHYSCANNON_BEAM_SPRITE );
	PrecacheModel( PHYSCANNON_GLOW_SPRITE );
	PrecacheModel( PHYSCANNON_ENDCAP_SPRITE );
	PrecacheModel( PHYSCANNON_CENTER_GLOW );
	PrecacheModel( PHYSCANNON_BLAST_SPRITE );

	PrecacheModel( MEGACANNON_BEAM_SPRITE );
	PrecacheModel( MEGACANNON_GLOW_SPRITE );
	PrecacheModel( MEGACANNON_ENDCAP_SPRITE );
	PrecacheModel( MEGACANNON_CENTER_GLOW );
	PrecacheModel( MEGACANNON_BLAST_SPRITE );

	PrecacheModel( MEGACANNON_RAGDOLL_BOOGIE_SPRITE );

	PrecacheModel( WEAPONIZER_MODEL_SPHERE );
	PrecacheModel( WEAPONIZER_MODEL_MATTER );

	// Precache our alternate model
	PrecacheModel( MEGACANNON_MODEL );
	
	PrecacheModel( WEAPONIZER_SURFACE_BUILDER_BEAM_SPRITE );

	PrecacheModel("models/spitball_small.mdl"); 

	PrecacheScriptSound( "Weapon_Weaponizer.HoldSound" );
	PrecacheScriptSound( "Weapon_Physgun.Off" );
	PrecacheScriptSound( "Weaponizer_FusionToggle" );

	PrecacheScriptSound( "Weapon_MegaWeaponizer.HoldSound");
	PrecacheScriptSound( "Weapon_MegaWeaponizer.ChargeZap");
	PrecacheScriptSound( "Weaponizer_FusionChargeup" );

	PrecacheScriptSound( "Weaponizer_AbsorbFire" );
	PrecacheScriptSound( "Weaponizer_AbsorbElectrical" );
	PrecacheScriptSound( "NPC_Hunter.FlechetteShoot" );

	PrecacheScriptSound( "Weaponizer_DischargeElectrical" );
	PrecacheScriptSound( "Weaponizer_FireElectricalFail" );
	PrecacheScriptSound( "Weaponizer_FireElectrical" );
	PrecacheScriptSound( "Weaponizer_FlameThrower.FireLoop" );
	PrecacheScriptSound( "Weaponizer_FlameThrower.FireStart" );
	PrecacheScriptSound( "Weaponizer_AmmoFull" );

	PrecacheParticleSystem( "water" );
	PrecacheParticleSystem( "water_puddle_electrified" );

	PrecacheScriptSound( "Weaponizer_WaterLiquid.FireLoop" );
	PrecacheScriptSound( "Weaponizer_WaterLiquid.FireStart" );
	PrecacheScriptSound( "Weaponizer_FuelLiquid.FireLoop" );
	PrecacheScriptSound( "d1_town.GasJet" );

	PrecacheParticleSystem( "flamethrower" );
	PrecacheParticleSystem( "vortigaunt_beam" );
	PrecacheParticleSystem( "vortigaunt_beam_charge" );
	PrecacheParticleSystem( "electricity_attack" );
	PrecacheParticleSystem( "npc_stun_electricity" );
	PrecacheParticleSystem( "absorb_electricity" );
	PrecacheParticleSystem( "absorb_fire" );
	PrecacheParticleSystem( "fire_jet_01" );
	PrecacheParticleSystem( "fusion_flamethrower" );
	PrecacheParticleSystem( "burning_character_weaponizer" );
	PrecacheParticleSystem( "water_hose" );
	PrecacheParticleSystem( "fuel_hose" );
	PrecacheParticleSystem( "water_hose_droplets" );
	PrecacheParticleSystem( "absorb_water" );
	PrecacheParticleSystem( "absorb_fuel" );
	PrecacheParticleSystem( "water_volume_electrified" );

	PrecacheParticleSystem( "tube_fire" );
	PrecacheParticleSystem(	"tube_electricity" );
	PrecacheParticleSystem( "tube_matter" );
	PrecacheParticleSystem(	"tube_fuel" );
	PrecacheParticleSystem(	"tube_water" );
	PrecacheParticleSystem(	"tube_modifier_explosive" );
	PrecacheParticleSystem(	"tube_flechette" );

	PrecacheParticleSystem( "world_water_source" );
	PrecacheParticleSystem( "world_fuel_source" );

	PrecacheParticleSystem( "electricity_attack_fail" );

	PrecacheModel( "sprites/vortring1.vmt" );
	
	UTIL_PrecacheOther( "prop_flak" );
	UTIL_PrecacheOther( "prop_flaming_flak_projectile" );
	UTIL_PrecacheOther( "prop_flak_grenade" );
	UTIL_PrecacheOther( "prop_metal_sphere" );
	UTIL_PrecacheOther( "prop_blob_sphere" );
	UTIL_PrecacheOther( "hunter_flechette" );

	PrecacheModel( "models/weapons/w_ball_small_chunk_01.mdl" );
	PrecacheModel( "models/weapons/w_ball_small_chunk_02.mdl" );
	PrecacheModel( "models/weapons/w_ball_small_chunk_03.mdl" );

	// IDBS: temp code, piggyback CShapeTarget off the weaponizer
	UTIL_PrecacheOther( "shape_target" );

	BaseClass::Precache();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::Spawn( void )
{
	BaseClass::Spawn();

	// Need to get close to pick it up
	CollisionProp()->UseTriggerBounds( false );

	m_bWeaponizerState = IsMegaWeaponizer();
	
	m_grabController.SetGrabNotifyTarget( this );

	// The megacannon uses a different skin
	m_nSkin = MEGACANNON_SKIN;

	m_flTimeForceView = -1;

	m_iPrimaryAmmoType = m_iSecondaryAmmoType = 0;
	
	m_eAttackState = ATTACK_POST;

	SelectWeapon();
}


//-----------------------------------------------------------------------------
// Purpose: Restore
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::OnRestore()
{
	BaseClass::OnRestore();
	m_grabController.OnRestore();
	m_grabController.SetGrabNotifyTarget( this );

	m_bWeaponizerState = IsMegaWeaponizer();

	// Tracker 8106:  Weaponizer effects disappear through level transition, so
	//  just recreate any effects here
	if ( m_EffectState != EFFECT_NONE )
	{
		DoEffect( m_EffectState, NULL );
	}

	SelectWeapon();
}

//-----------------------------------------------------------------------------
// On Remove
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::UpdateOnRemove(void)
{
	DestroyEffects( );
	BaseClass::UpdateOnRemove();
}


//-----------------------------------------------------------------------------
// Sprite scale factor 
//-----------------------------------------------------------------------------
inline float CWeaponWeaponizer::SpriteScaleFactor() 
{
	return 1.5f;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::Deploy( void )
{
	CloseElements();
	DoEffect( EFFECT_READY );

	// Unbloat our bounds
	if ( IsMegaWeaponizer() )
	{
		CollisionProp()->UseTriggerBounds( false );
	}

	m_flTimeNextObjectPurge = gpGlobals->curtime;

	return BaseClass::Deploy();
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ItemHolsterFrame( void )
{
	// Must be player held
	if ( GetOwner() && GetOwner()->IsPlayer() == false )
		return;

	// We can't be active
	if ( GetOwner()->GetActiveWeapon() == this )
		return;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	CheckWaterLevel( pOwner );
}


//-----------------------------------------------------------------------------
// Purpose: Force the cannon to drop anything it's carrying
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ForceDrop( void )
{
	CloseElements();
	DetachObject();
	StopEffects();
}


//-----------------------------------------------------------------------------
// Purpose: Drops its held entity if it matches the entity passed in
// Input  : *pTarget - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::DropIfEntityHeld( CBaseEntity *pTarget )
{
	if ( pTarget == NULL )
		return false;

	CBaseEntity *pHeld = m_grabController.GetAttached();
	
	if ( pHeld == NULL )
		return false;

	if ( pHeld == pTarget )
	{
		ForceDrop();
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Make sure we set the right skin when equipped.
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::Equip( CBaseCombatCharacter *pOwner )
{
	BaseClass::Equip( pOwner );
	SelectWeapon();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::Drop( const Vector &vecVelocity )
{
	ForceDrop();
	BaseClass::Drop( vecVelocity );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::CanHolster( void ) 
{ 
	//Don't holster this weapon if we're holding onto something
	if ( m_bActive )
		return false;

	if ( m_iQueuedFlechettes > 0 )
		return false;

	return BaseClass::CanHolster();
};

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	//Don't holster this weapon if we're holding onto something
	if ( m_bActive )
	{
		if ( !pSwitchingTo ||
			( m_grabController.GetAttached() == pSwitchingTo && 
			GetOwner()->Weapon_OwnsThisType( pSwitchingTo->GetClassname(), pSwitchingTo->GetSubType()) ) )
		{
		
		}
		else
		{
			return false;
		}
	}

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner )
	{
		pOwner->RumbleEffect( RUMBLE_PHYSCANNON_OPEN, 0, RUMBLE_FLAG_STOP );
	}

	ForceDrop();

	m_iQueuedFlechettes = 0;

	return BaseClass::Holster( pSwitchingTo );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DryFire( void )
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
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::PrimaryFireEffect( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	
	if ( pOwner == NULL )
		return;

	pOwner->ViewPunch( QAngle(-6, random->RandomInt(-2,2) ,0) );
	
	color32 white = { 245, 245, 255, 32 };
	UTIL_ScreenFade( pOwner, white, 0.1f, 0.0f, FFADE_IN );

	WeaponSound( SINGLE );
}

#define	MAX_KNOCKBACK_FORCE	128

//-----------------------------------------------------------------------------
// Punt non-physics
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::PuntNonVPhysics( CBaseEntity *pEntity, const Vector &forward, trace_t &tr )
{
	float flDamage = 1.0f;
	if ( FClassnameIs( pEntity, "func_breakable" ) )
	{
		CBreakable *pBreak = dynamic_cast <CBreakable *>(pEntity);
		if ( pBreak && ( pBreak->GetMaterialType() == matGlass ) )
		{
			flDamage = weaponizer_dmg_glass.GetFloat();
		}
	}

	CTakeDamageInfo	info;
	
	info.SetAttacker( GetOwner() );
	info.SetInflictor( this );
	info.SetDamage( flDamage );
	info.SetDamageType( DMG_CRUSH | DMG_PHYSGUN );
	info.SetDamageForce( forward );	// Scale?
	info.SetDamagePosition( tr.endpos );

	pEntity->DispatchTraceAttack( info, forward, &tr );

	ApplyMultiDamage();
	
	//Explosion effect
	DoEffect( EFFECT_LAUNCH, &tr.endpos );

	PrimaryFireEffect();
	SendWeaponAnim( ACT_VM_SECONDARYATTACK );

	m_nChangeState = ELEMENT_STATE_CLOSED;
	m_flElementDebounce = gpGlobals->curtime + 0.5f;
	m_flCheckSuppressTime = gpGlobals->curtime + 0.25f;
}


//-----------------------------------------------------------------------------
// What happens when the physgun picks up something 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::Physgun_OnPhysGunPickup( CBaseEntity *pEntity, CBasePlayer *pOwner, PhysGunPickup_t reason )
{
	// If the target is debris, convert it to non-debris
	if ( pEntity->GetCollisionGroup() == COLLISION_GROUP_DEBRIS )
	{
		// Interactive debris converts back to debris when it comes to rest
		pEntity->SetCollisionGroup( COLLISION_GROUP_INTERACTIVE_DEBRIS );
	}

	float mass = 0.0f;
	if( pEntity->VPhysicsGetObject() )
	{
		mass = pEntity->VPhysicsGetObject()->GetMass();
	}

	if( reason == PUNTED_BY_CANNON )
	{
		pOwner->RumbleEffect( RUMBLE_357, 0, RUMBLE_FLAGS_NONE );
		RecordThrownObject( pEntity );
	}

	// Warn Alyx if the player is punting a car around.
	if( hl2_episodic.GetBool() && mass > 250.0f )
	{
		CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
		int nAIs = g_AI_Manager.NumAIs();

		for ( int i = 0; i < nAIs; i++ )
		{
			if( ppAIs[ i ]->Classify() == CLASS_PLAYER_ALLY_VITAL )
			{
				ppAIs[ i ]->DispatchInteraction( g_interactionPlayerPuntedHeavyObject, pEntity, pOwner );
			}
		}
	}

	Pickup_OnPhysGunPickup( pEntity, pOwner, reason );
}


//-----------------------------------------------------------------------------
// Punt vphysics
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::PuntVPhysics( CBaseEntity *pEntity, const Vector &vecForward, trace_t &tr )
{
	CTakeDamageInfo	info;

	Vector forward = vecForward;

	info.SetAttacker( GetOwner() );
	info.SetInflictor( this );
	info.SetDamage( 0.0f );
	info.SetDamageType( DMG_PHYSGUN );
	pEntity->DispatchTraceAttack( info, forward, &tr );
	ApplyMultiDamage();

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( Pickup_OnAttemptPhysGunPickup( pEntity, pOwner, PUNTED_BY_CANNON ) )
	{
		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int listCount = pEntity->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
		if ( !listCount )
		{
			//FIXME: Do we want to do this if there's no physics object?
			Physgun_OnPhysGunPickup( pEntity, pOwner, PUNTED_BY_CANNON );
			DryFire();
			return;
		}
				
		if( forward.z < 0 )
		{
			//reflect, but flatten the trajectory out a bit so it's easier to hit standing targets
			forward.z *= -0.65f;
		}
		
		// NOTE: Do this first to enable motion (if disabled) - so forces will work
		// Tell the object it's been punted
		Physgun_OnPhysGunPickup( pEntity, pOwner, PUNTED_BY_CANNON );

		// don't push vehicles that are attached to the world via fixed constraints
		// they will just wiggle...
		if ( (pList[0]->GetGameFlags() & FVPHYSICS_CONSTRAINT_STATIC) && pEntity->GetServerVehicle() )
		{
			forward.Init();
		}

		if ( !IsMegaWeaponizer() && !Pickup_ShouldPuntUseLaunchForces( pEntity, PHYSGUN_FORCE_PUNTED ) )
		{
			int i;

			// limit mass to avoid punting REALLY huge things
			float totalMass = 0;
			for ( i = 0; i < listCount; i++ )
			{
				totalMass += pList[i]->GetMass();
			}
			float maxMass = 250;
			IServerVehicle *pVehicle = pEntity->GetServerVehicle();
			if ( pVehicle )
			{
				maxMass *= 2.5;	// 625 for vehicles
			}
			float mass = MIN(totalMass, maxMass); // MAX 250kg of additional force

			// Put some spin on the object
			for ( i = 0; i < listCount; i++ )
			{
				const float hitObjectFactor = 0.5f;
				const float otherObjectFactor = 1.0f - hitObjectFactor;
  				// Must be light enough
				float ratio = pList[i]->GetMass() / totalMass;
				if ( pList[i] == pEntity->VPhysicsGetObject() )
				{
					ratio += hitObjectFactor;
					ratio = MIN(ratio,1.0f);
				}
				else
				{
					ratio *= otherObjectFactor;
				}
				pList[i]->ApplyForceCenter( forward * 15000.0f * ratio );
				pList[i]->ApplyForceOffset( forward * mass * 600.0f * ratio, tr.endpos );
			}
		}
		else
		{
			ApplyVelocityBasedForce( pEntity, vecForward, tr.endpos, PHYSGUN_FORCE_PUNTED );
		}
	}

	// Add recoil
	QAngle	recoil = QAngle( random->RandomFloat( 1.0f, 2.0f ), random->RandomFloat( -1.0f, 1.0f ), 0 );
	pOwner->ViewPunch( recoil );

	//Explosion effect
	DoEffect( EFFECT_LAUNCH, &tr.endpos );

	PrimaryFireEffect();
	SendWeaponAnim( ACT_VM_SECONDARYATTACK );

	m_nChangeState = ELEMENT_STATE_CLOSED;
	m_flElementDebounce = gpGlobals->curtime + 0.5f;
	m_flCheckSuppressTime = gpGlobals->curtime + 0.25f;

	// Don't allow the gun to regrab a thrown object!!
	m_flNextAbsorbTime = m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;
}

//-----------------------------------------------------------------------------
// Purpose: Applies velocity-based forces to throw the entity. This code is
//			called from both punt and launch carried code.
//			ASSUMES: that pEntity is a vphysics entity.
// Input  : - 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ApplyVelocityBasedForce( CBaseEntity *pEntity, const Vector &forward, const Vector &vecHitPos, PhysGunForce_t reason )
{
	// Get the launch velocity
	Vector vVel = Pickup_PhysGunLaunchVelocity( pEntity, forward, reason );
	
	// Get the launch angular impulse
	AngularImpulse aVel = Pickup_PhysGunLaunchAngularImpulse( pEntity, reason );
		
	// Get the physics object (MUST have one)
	IPhysicsObject *pPhysicsObject = pEntity->VPhysicsGetObject();
	if ( pPhysicsObject == NULL )
	{
		Assert( 0 );
		return;
	}

	// Affect the object
	CRagdollProp *pRagdoll = dynamic_cast<CRagdollProp*>( pEntity );
	if ( pRagdoll == NULL )
	{
#ifdef HL2_EPISODIC
		// The jeep being punted needs special force overrides
		if ( reason == PHYSGUN_FORCE_PUNTED && pEntity->GetServerVehicle() )
		{
			// We want the point to emanate low on the vehicle to move it along the ground, not to twist it
			Vector vecFinalPos = vecHitPos;
			vecFinalPos.z = pEntity->GetAbsOrigin().z;
			pPhysicsObject->ApplyForceOffset( vVel, vecFinalPos );
		}
		else
		{
			pPhysicsObject->AddVelocity( &vVel, &aVel );
		}
#else

		pPhysicsObject->AddVelocity( &vVel, &aVel );

#endif // HL2_EPISODIC
	}
	else
	{
		Vector	vTempVel;
		AngularImpulse vTempAVel;

		ragdoll_t *pRagdollPhys = pRagdoll->GetRagdoll( );
		for ( int j = 0; j < pRagdollPhys->listCount; ++j )
		{
			pRagdollPhys->list[j].pObject->AddVelocity( &vVel, &aVel ); 
		}
	}
}


//-----------------------------------------------------------------------------
// Punt non-physics
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::PuntRagdoll( CBaseEntity *pEntity, const Vector &vecForward, trace_t &tr )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	Pickup_OnPhysGunDrop( pEntity, pOwner, LAUNCHED_BY_CANNON );

	CTakeDamageInfo	info;

	Vector forward = vecForward;
	info.SetAttacker( GetOwner() );
	info.SetInflictor( this );
	info.SetDamage( 0.0f );
	info.SetDamageType( DMG_PHYSGUN );
	pEntity->DispatchTraceAttack( info, forward, &tr );
	ApplyMultiDamage();

	if ( Pickup_OnAttemptPhysGunPickup( pEntity, pOwner, PUNTED_BY_CANNON ) )
	{
		Physgun_OnPhysGunPickup( pEntity, pOwner, PUNTED_BY_CANNON );

		if( forward.z < 0 )
		{
			//reflect, but flatten the trajectory out a bit so it's easier to hit standing targets
			forward.z *= -0.65f;
		}
		
		Vector			vVel = forward * 1500;
		AngularImpulse	aVel = Pickup_PhysGunLaunchAngularImpulse( pEntity, PHYSGUN_FORCE_PUNTED );

		CRagdollProp *pRagdoll = dynamic_cast<CRagdollProp*>( pEntity );
		ragdoll_t *pRagdollPhys = pRagdoll->GetRagdoll( );

		int j;
		for ( j = 0; j < pRagdollPhys->listCount; ++j )
		{
			pRagdollPhys->list[j].pObject->AddVelocity( &vVel, NULL ); 
		}
	}
	
	// Add recoil
	QAngle	recoil = QAngle( random->RandomFloat( 1.0f, 2.0f ), random->RandomFloat( -1.0f, 1.0f ), 0 );
	pOwner->ViewPunch( recoil );

	//Explosion effect
	DoEffect( EFFECT_LAUNCH, &tr.endpos );

	PrimaryFireEffect();
	SendWeaponAnim( ACT_VM_SECONDARYATTACK );

	m_nChangeState = ELEMENT_STATE_CLOSED;
	m_flElementDebounce = gpGlobals->curtime + 0.5f;
	m_flCheckSuppressTime = gpGlobals->curtime + 0.25f;

	// Don't allow the gun to regrab a thrown object!!
	m_flNextAbsorbTime = m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;
}


//-----------------------------------------------------------------------------
// Trace length
//-----------------------------------------------------------------------------
float CWeaponWeaponizer::TraceLength()
{
	return weaponizer_tracelength.GetFloat();
}

//-----------------------------------------------------------------------------
// If there's any special rejection code you need to do per entity then do it here
// This is kinda nasty but I'd hate to move more physcannon related stuff into CBaseEntity
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::EntityAllowsPunts( CBaseEntity *pEntity )
{
	if ( pEntity->HasSpawnFlags( SF_PHYSBOX_NEVER_PUNT ) )
	{
		CPhysBox *pPhysBox = dynamic_cast<CPhysBox*>(pEntity);

		if ( pPhysBox != NULL )
		{
			if ( pPhysBox->HasSpawnFlags( SF_PHYSBOX_NEVER_PUNT ) )
			{
				return false;
			}
		}
	}

	if ( pEntity->HasSpawnFlags( SF_WEAPON_NO_PHYSCANNON_PUNT ) )
	{
		CBaseCombatWeapon *pWeapon = dynamic_cast<CBaseCombatWeapon*>(pEntity);

		if ( pWeapon != NULL )
		{
			if ( pWeapon->HasSpawnFlags( SF_WEAPON_NO_PHYSCANNON_PUNT ) )
			{
				return false;
			}
		}
	}

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootFlak( attackstate_t eState )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	if ( m_iModifierType == WEAPONIZER_MODIFIER_FLECHETTE )
	{
		if ( m_iQueuedFlechettes <= 0 )
		{
			m_flNextFlechetteFireTime = gpGlobals->curtime + hunter_flechette_delay.GetFloat();
			m_iQueuedFlechettes = 4;
		}

		m_iQueuedFlechettes--;
	}

	// Kick the view a little
	QAngle	viewPunch;
	viewPunch.x = random->RandomFloat( 0.25f, 0.5f );
	viewPunch.y = random->RandomFloat( -.6f, .6f );
	viewPunch.z = 0.0f;
	pPlayer->ViewPunch( viewPunch );

	SendWeaponAnim( ACT_VM_SECONDARYATTACK );
		
	CFlak_ShootFlak( pPlayer, m_iModifierType );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootFlakGrenade( attackstate_t eState )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	CFlak_ShootFlakGrenade( pPlayer );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootFlamingFlak( attackstate_t eState )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	PrimaryFireEffect();
	WeaponSound( SINGLE );
	
	CFlak_ShootFlamingFlakProjectile( pPlayer );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootZapFlak( attackstate_t eState )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	PrimaryFireEffect();
	WeaponSound( SINGLE );

	CFlak_ShootZapFlak( pPlayer );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootDefault( attackstate_t eState )
{
	DryFire();
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootSphere( attackstate_t eState )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;
	
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	PrimaryFireEffect();
	WeaponSound( SINGLE );

	CMetal_ShootSphere( pPlayer );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootZapSphere( attackstate_t eState )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	PrimaryFireEffect();
	WeaponSound( SINGLE );

	CMetal_ShootZapSphere( pPlayer );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootFlamingSphere( attackstate_t eState )
{
	// Only the player fires this way so we can cast
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	PrimaryFireEffect();
	WeaponSound( SINGLE );

	CMetal_ShootFlamingSphere( pPlayer );
}


ConVar weaponizer_fire_damage( "weaponizer_fire_damage", "2" );

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootFlames( attackstate_t eState )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	//Don't fire fire underwater.
	if ( pOwner->GetWaterLevel() == 3 )
		return;

	m_bFiringPrimary = true;

	trace_t tr;
	Vector forward;
	pOwner->EyeVectors( &forward );

	// Setup our positions
	Vector	start = pOwner->Weapon_ShootPosition();
	float	testLength = 256;
	Vector	end = start + forward * testLength;

	UTIL_TraceHull( start, end, Vector( -2, -2, -2 ), Vector( 2, 2, 2 ), MASK_SHOT, pOwner, COLLISION_GROUP_NONE, &tr );

	if ( m_flNextWarningSound <= gpGlobals->curtime )
	{
		CSoundEnt::InsertSound ( SOUND_THUMPER, pOwner->GetAbsOrigin(), 512, 1.0f, this );
		m_flNextWarningSound = gpGlobals->curtime + 1.0f;
	}
	
	if ( m_pFiringLoop == NULL )
	{
		EmitSound( "Weaponizer_FlameThrower.FireStart" );

		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		CPASAttenuationFilter filter( this );
		m_pFiringLoop = controller.SoundCreate( filter, entindex(), "Weaponizer_FlameThrower.FireLoop" );
		controller.Play( m_pFiringLoop, 0.0, 100 );
		controller.SoundChangeVolume( m_pFiringLoop, 1.0, 3.4 );
	}

	if ( tr.m_pEnt != NULL )
	{
		tr.m_pEnt->TakeDamage( CTakeDamageInfo( this, pOwner, weaponizer_fire_damage.GetInt(), DMG_BURN | DMG_DIRECT ) );

		CBaseAnimating *pEnt = tr.m_pEnt->GetBaseAnimating();

		CAI_BaseNPC *pNPC = pEnt->MyNPCPointer();
		if ( pNPC && pNPC->AllowedToIgnite() )
		{
			float flMod = 1.0f;
						
			if ( pEnt->IsNPC() == true )
			{
				// WEAPONIZER TODO: burning objects
				//if ( pEnt->ShouldBurnLonger() == false )
				//{
				//	flMod = random->RandomFloat( 0.15f, 0.25f );
				//}
			}
	
			float flLength = (float)(pEnt->GetMaxHealth() * flMod) / FLAME_DIRECT_DAMAGE_PER_SEC;

			// WEAPONIZER TODO: burning objects
			//if ( pEnt->ShouldBurnLonger() == true && flLength < 15 )
			//{
			//	flLength = 15;
			//}

			pEnt->Ignite( flLength, false );
		}
	}
}


ConVar weaponizer_liquid_max_travel_time( "weaponizer_liquid_max_travel_time", "0.75" );

//-----------------------------------------------------------------------------
// this will hit skip the pass entity, but not anything it owns 
// (lets player grab own grenades)
class CTraceFilterNoLiquid : public CTraceFilterSimple
{
public:
	DECLARE_CLASS( CTraceFilterNoLiquid, CTraceFilterSimple );

	CTraceFilterNoLiquid( IHandleEntity *passentity, int collisionGroup )
		: CTraceFilterSimple( NULL, collisionGroup ), m_pIgnoreEnt(passentity)
	{
	}


	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		CBaseEntity *pIgnore = EntityFromEntityHandle( m_pIgnoreEnt );

		if ( pEntity == NULL )
			return false;

		if ( pHandleEntity == m_pIgnoreEnt )
			return false;

		if ( pEntity == pIgnore->GetOwnerEntity() )
			return false;

		if ( pEntity->GetCollisionGroup() == HL2COLLISION_GROUP_SPIT )
			return false;

		return BaseClass::ShouldHitEntity( pHandleEntity, contentsMask );
	}

protected:
	IHandleEntity *m_pIgnoreEnt;
};

ConVar weaponizer_liquid_velocity( "weaponizer_liquid_velocity", "900" );
ConVar weaponizer_liquid_debug( "weaponizer_liquid_debug", "0" );

void ShootLiquid( CBasePlayer *pPlayer, int iType )
{
	Vector	vForward, vRight, vUp;

	pPlayer->EyeVectors( &vForward, &vRight, &vUp );

	bool bCollided = false;
	bool bInSky = false;
	float gravity = -gpGlobals->frametime * 800;

	if ( iType == WEAPONIZER_MATERIAL_TYPE_WATER )
	{
		gravity = -gpGlobals->frametime * 300;
	}

	Vector vOrigin = pPlayer->Weapon_ShootPosition() + vForward * 12.0f + vRight * 3.25 + vUp * -2.25f;
	Vector vVelocity = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT ) * weaponizer_liquid_velocity.GetInt();
	CBaseEntity *pEntity = NULL;
	trace_t pm;

	float flDistance = 0.0f;

	int iFailSafe = 0;

	while ( bCollided == false && iFailSafe < 1000 )
	{
		Vector vOldOrigin = vOrigin;
		vOrigin = vOrigin + vVelocity * gpGlobals->frametime;

		if ( weaponizer_liquid_debug.GetBool() == true )
		{
			NDebugOverlay::SweptBox( vOldOrigin, vOrigin, Vector( -8, -8, -8 ), Vector( 8, 8, 8 ), pPlayer->GetAbsAngles(), 255, 0, 0, 255, 5 );
		}

		flDistance += (vOrigin - vOldOrigin).Length();

		float flTravelTime = flDistance / vVelocity.Length();

		if ( flTravelTime > weaponizer_liquid_max_travel_time.GetFloat() )
		{
			flTravelTime = weaponizer_liquid_max_travel_time.GetFloat();

			if ( weaponizer_liquid_debug.GetBool() == true )
			{
				NDebugOverlay::Box( vOrigin, Vector( 16, 16, 16 ), Vector( -16, -16, -16 ), 255, 0, 0, 0, 5 );
			}

			break;
		}


		CTraceFilterNoLiquid filter( pPlayer->GetActiveWeapon(), COLLISION_GROUP_NONE );
		
		if ( iType == WEAPONIZER_MATERIAL_TYPE_WATER )
		{
			UTIL_TraceHull( vOldOrigin, vOrigin, Vector( -8, -8, -8 ), Vector( 8, 8, 8 ), MASK_SOLID, &filter, &pm );
		}
		else
		{
			UTIL_TraceLine( vOldOrigin, vOrigin, MASK_SOLID, &filter, &pm );
		}

		int iContent = UTIL_PointContents( pm.endpos, CONTENTS_WATER );

		if ( pm.surface.flags & SURF_SKY || pm.allsolid == true || iContent & CONTENTS_WATER ) 
		{
			bInSky = true;
			break;
		}

		if ( pm.fraction != 1.0f )
		{
			bCollided = true;
			vOrigin = pm.endpos;
			pEntity = pm.m_pEnt;

			if ( weaponizer_liquid_debug.GetBool() == true )
			{
				NDebugOverlay::Box( vOrigin, Vector( 16, 16, 16 ), Vector( -16, -16, -16 ), 0, 255, 0, 0, 5 );
			}
			break;
		}
		else
		{
			vVelocity[2] += gravity;
		}
	}


	float flTravelTime = flDistance / vVelocity.Length();

	if ( bCollided )
	{
		if ( pEntity && pEntity->IsWorld() == false && iType == WEAPONIZER_MATERIAL_TYPE_EXPLOSIVE )
		{
			// WEAPONIZER TODO: burning objects
			//pEntity->SetAllowedToIgnite( true );
			//pEntity->SetLongerBurnState( true );

			DispatchParticleEffect( "water_hose_droplets", pm.endpos, vec3_angle );
		}
		else if ( pEntity && pEntity->IsWorld() == false && iType == WEAPONIZER_MATERIAL_TYPE_WATER )
		{
			if ( pEntity->GetBaseAnimating() && pEntity->GetBaseAnimating()->IsOnFire() )
			{
				if ( pEntity->GetEffectEntity() )
				{
					variant_t variant;
					variant.SetFloat( 0.5f );

					g_EventQueue.AddEvent( pEntity->GetEffectEntity(), "Extinguish", variant, 0.0f, pPlayer, pPlayer );
				}
			}

			if ( pEntity->VPhysicsGetObject() )
			{
				if ( !pEntity->VPhysicsGetObject()->IsMoveable() )
				{
					if ( FClassnameIs( pEntity, "prop_physics" ) ||
						 FClassnameIs( pEntity, "prop_ragdoll" ) ||
						 FClassnameIs( pEntity, "func_physbox" ) )
					{
						if ( pEntity->HasSpawnFlags( SF_PHYSPROP_ENABLE_ON_PHYSCANNON ) )
						{
							pEntity->VPhysicsGetObject()->EnableMotion( true );
						}
					}
				}

				VectorNormalize( vVelocity );
				pEntity->VPhysicsGetObject()->ApplyForceOffset( vVelocity * weaponizer_water_hose_force.GetFloat(), pm.endpos );
			}
			
			CAI_BaseNPC *pNPC = pEntity->MyNPCPointer();
			if ( pNPC )
			{
				// WEAPONIZER TODO: support shocking NPCs?
				#if 0
				if ( pNPC->IsShocked() )
				{
					CTakeDamageInfo info( pPlayer, pPlayer, weaponzier_hose_electrical_dmg.GetInt(), DMG_GENERIC ); // dvs: DMG_SHOCK will stun em?
					pNPC->TakeDamage( info );
				}
				#endif
				
				CNPC_Antlion *pAntlion = dynamic_cast<CNPC_Antlion *>(pNPC);
				if ( pAntlion )
				{
					// Turn them over

					vForward.z = 0;
					Vector vNewVelocity = pAntlion->GetAbsVelocity();
					vNewVelocity += vForward * 200;
					pAntlion->SetAbsVelocity( vNewVelocity );

					pAntlion->Flip( false, vForward * 200 );
				}
			}
		}
		else
		{
			CBaseEntity *pEntity = NULL;

			while ( (pEntity  = gEntList.FindEntityInSphere( pEntity, pm.endpos, 8 )) != NULL )
			{
				if ( pEntity->GetCollisionGroup() == HL2COLLISION_GROUP_SPIT )
					return;

				variant_t variant;
				variant.SetFloat( 0.5f );

				g_EventQueue.AddEvent( pEntity, "Extinguish", variant, 0.0f, pPlayer, pPlayer );
			}

			trace_t tr;
			CTraceFilterNoLiquid filter( pPlayer->GetActiveWeapon(), COLLISION_GROUP_NONE );
			UTIL_TraceLine( pm.endpos, pm.endpos + vVelocity * 64, MASK_SOLID, &filter, &tr );

			if ( tr.fraction != 1.0f )
			{
				VectorNormalize( vVelocity );
				CreateLiquid( tr.endpos, vVelocity, iType, flTravelTime );
			}
		}
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootLiquidFuel( attackstate_t eState )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	m_bFiringSecondary = true;

	ShootLiquid( pOwner, WEAPONIZER_MATERIAL_TYPE_EXPLOSIVE );

	if ( m_pLiquidLoop == NULL )
	{
		EmitSound( "Weaponizer_WaterLiquid.FireStart" );

		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		CPASAttenuationFilter filter( this );
		m_pLiquidLoop = controller.SoundCreate( filter, entindex(), "Weaponizer_FuelLiquid.FireLoop" );
		controller.Play( m_pLiquidLoop, 0.0, 100 );
		controller.SoundChangeVolume( m_pLiquidLoop, 1.0, 3.4 );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootLiquidWater( attackstate_t eState )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	m_bFiringSecondary = true;

	ShootLiquid( pOwner, WEAPONIZER_MATERIAL_TYPE_WATER );

	if ( m_pLiquidLoop == NULL )
	{
		EmitSound( "Weaponizer_WaterLiquid.FireStart" );

		CSoundEnvelopeController &controller = CSoundEnvelopeController::GetController();

		CPASAttenuationFilter filter( this );
		m_pLiquidLoop = controller.SoundCreate( filter, entindex(), "Weaponizer_WaterLiquid.FireLoop" );
		controller.Play( m_pLiquidLoop, 0.0, 100 );
		controller.SoundChangeVolume( m_pLiquidLoop, 1.0, 3.4 );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

ConVar weaponizer_electrical_damage( "weaponizer_electrical_damage", "5" );
ConVar weaponizer_electrical_shock_time( "weaponizer_electrical_shock_time", "4.0" );

void CWeaponWeaponizer::ShootElectrical( attackstate_t eState )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	trace_t tr;
	Vector forward;
	pOwner->EyeVectors( &forward );

	// Setup our positions
	Vector	start = pOwner->Weapon_ShootPosition();
	float	testLength = 512;
	Vector	end = start + forward * testLength;

	UTIL_TraceLine( start, end, MASK_SHOT & ~(CONTENTS_HITBOX), pOwner, COLLISION_GROUP_NONE, &tr );

	const char *pEffectName = "electricity_attack_fail";

	if ( tr.fraction != 1.0f )
	{
		pEffectName = "electricity_attack";
		EmitSound( "Weaponizer_FireElectrical" );
	}
	else
	{
		EmitSound( "Weaponizer_FireElectricalFail" );
	}

	EntityMessageBegin( this, true );
		WRITE_BYTE( WEAPONIZER_FX_ELECTRICAL_ZAP );
		WRITE_STRING( pEffectName );
		WRITE_VEC3COORD( tr.endpos );
	MessageEnd();

	CBaseEntity *list[128];
	Ray_t ray;

	ray.Init( start, tr.endpos,
		Vector(-1,-1,-1),
		Vector( 1, 1, 1) );

	int nFoundCt = UTIL_EntitiesAlongRay( list, 128, ray, 0 );

	for( int i = 0; i < nFoundCt; i++ )
	{
		CBaseEntity *pEntityFound = list[i];

		if ( GetWeaponizerMaterialType( pEntityFound ) == WEAPONIZER_MATERIAL_TYPE_ELECTRICAL )
		{
			CFuncElectricity *pElectrical =	dynamic_cast< CFuncElectricity*>( pEntityFound );

			if ( pElectrical && pElectrical->m_bDisabled == false )
			{
				// WEAPONIZER TODO: support electrified objects
				//pElectrical->m_OnElectrified.FireOutput( this, this, 0 );
			}

			return;
		}
		else if ( GetWeaponizerMaterialType( pEntityFound ) == WEAPONIZER_MATERIAL_TYPE_WATER )
		{
			CFuncWeaponizerWater *pWater =	dynamic_cast< CFuncWeaponizerWater*>( pEntityFound );

			if ( pWater && pWater->m_bDisabled == false )
			{
				// WEAPONIZER TODO: support electrified objects
				//pWater->m_OnElectrified.FireOutput( this, this, 0 );
				g_EventQueue.AddEvent( pWater, "StartWaterShock", 0.0f, this, this );
				return;
			}
		}
	}

	if ( tr.m_pEnt == NULL )
		return;

	CPhysicsProp *pPhysProp = dynamic_cast<CPhysicsProp *>(tr.m_pEnt);

	int iDamage = weaponizer_electrical_damage.GetInt();

	if ( pPhysProp )
	{
		iDamage = pPhysProp->GetHealth() + ( iDamage * 2 );
	}

	CTakeDamageInfo info( this, pOwner, iDamage, DMG_SHOCK | DMG_DIRECT );
	CalculateMeleeDamageForce( &info, forward, tr.endpos );

	tr.m_pEnt->TakeDamage( info );
	
	// WEAPONIZER TODO: support OnElectrified output
	#if 0
	tr.m_pEnt->m_OnElectrified.FireOutput( this, this, 0 ); 

	// WEAPONIZER TODO: support burning objects
	if ( tr.m_pEnt->ShouldBurnLonger() == true )
	{
		tr.m_pEnt->SetLongerBurnState( false );	

		float xSize = CollisionProp()->OBBMaxs().x - CollisionProp()->OBBMins().x;
		float ySize = CollisionProp()->OBBMaxs().y - CollisionProp()->OBBMins().y;

		float size = ( xSize + ySize );

		ExplosionCreate( tr.m_pEnt->GetAbsOrigin(), tr.m_pEnt->GetAbsAngles(), pOwner, size * 2.0f, size * 2.0f, 0, 150.0f, tr.m_pEnt );

		tr.m_pEnt->SetLongerBurnState( false );
	}
	#endif
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ShootZapFlakBeam( attackstate_t eState )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	CFlak_ShootZapFlakBeam( pOwner, eState );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::SubtractAmmo( int iType )
{
	int i1 = m_iClip1;
	int i2 = m_iClip2;
	
	Assert( ( iType == WEAPONIZER_PARAM_PRIMARY ) || ( iType == WEAPONIZER_PARAM_SECONDARY ) || ( iType == WEAPONIZER_PARAM_FUSION ) );
	
	m_iClip1 = MAX( m_iClip1 - m_weaponParams[iType].flPrimaryAmmoBurnRate, 0 );
	m_iClip2 = MAX( m_iClip2 - m_weaponParams[iType].flSecondaryAmmoBurnRate, 0 );
	
	if ( !m_iClip1 && ( m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flPrimaryAmmoBurnRate != 0 ) )
	{
		m_iPrimaryAmmoType = WEAPONIZER_MATERIAL_TYPE_NONE;
		m_bFiringPrimary = false;

		m_iModifierType = WEAPONIZER_MODIFIER_NONE;
		m_iQueuedFlechettes = 0;
	}
	
	if ( !m_iClip2 && ( m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flSecondaryAmmoBurnRate != 0 ) )
	{
		m_iSecondaryAmmoType = WEAPONIZER_MATERIAL_TYPE_NONE;
		m_bFiringSecondary = false;
	}

	if ( ( i1 && !m_iClip1 ) || ( i2 && !m_iClip2 ) )
	{
		// We ran out of ammo. See if we need to switch weapons.
		SelectWeapon();
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::SetSkin( int skinNum )
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
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::SelectWeapon()
{
	static WeaponParams_t *selectParams[WEAPONIZER_NUM_MATERIALS] =
	{
		&s_Default,			// NONE
		&s_Flamethrower,	// FIRE
		&s_Electrical,		// ELECTRICAL
		&s_Flak,			// CONCRETE	
		&s_Flak,			// METAL
		&s_Default,			// --- blank ---
		&s_LiquidFuel,		// EXPLOSIVE
		&s_LiquidWater,		// WATER
	};

	Assert( ( m_iPrimaryAmmoType >= 0 ) && ( m_iPrimaryAmmoType < WEAPONIZER_NUM_PRIMARY_MATERIALS ) );

	int iSecondary = m_iSecondaryAmmoType;
	if ( iSecondary <= WEAPONIZER_NUM_PRIMARY_MATERIALS )
	{
		Assert( iSecondary == 0 );
		iSecondary = WEAPONIZER_NUM_PRIMARY_MATERIALS;
	}
	iSecondary -= WEAPONIZER_NUM_PRIMARY_MATERIALS;

	Assert( ( iSecondary >= 0 ) && ( iSecondary < WEAPONIZER_NUM_SECONDARY_MATERIALS ) );

	m_weaponParams[WEAPONIZER_PARAM_PRIMARY] = *selectParams[m_iPrimaryAmmoType];
	m_weaponParams[WEAPONIZER_PARAM_SECONDARY] = *selectParams[m_iSecondaryAmmoType];
	//m_weaponParams[WEAPONIZER_PARAM_FUSION] = *selectParams[iSecondary][m_iPrimaryAmmoType];

	if ( m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flPrimaryAmmoRegenRate > 0 )
	{
		m_flNextPrimaryRegenTime = gpGlobals->curtime + m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flPrimaryAmmoRegenRate;
	}

	if ( m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flSecondaryAmmoRegenRate > 0 )
	{
		m_flNextSecondaryRegenTime = gpGlobals->curtime + m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flSecondaryAmmoRegenRate;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CWeaponWeaponizer::GetAmmoCount( int iMaterial )
{
	if ( iMaterial < WEAPONIZER_NUM_PRIMARY_MATERIALS )
		return m_iClip1;

	if ( iMaterial > WEAPONIZER_NUM_PRIMARY_MATERIALS )
		return m_iClip2;

	return 0;
}


//-----------------------------------------------------------------------------
// Shoot the weaponizer's primary weapon!
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::PrimaryAttack( attackstate_t eState )
{
	if ( ( eState == ATTACK_RUN) && ( gpGlobals->curtime <= m_flNextPrimaryAttack ) )
		return;

	if ( shaper_use_weaponizer.GetBool() )
	{
		if ( eState != ATTACK_POST )
		{
			// IDBS: temp code, piggyback CShapeTarget off the weaponizer
			m_flNextPrimaryAttack = gpGlobals->curtime + CShapeTarget::WeaponPrimary( ToBasePlayer( GetOwner() ) );
		}
		return;
	}


	if ( ( m_iClip1 < m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flPrimaryAmmoBurnRate ) ||
		 ( m_iClip2 < m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flSecondaryAmmoBurnRate ) )
	{	 
		m_bFiringPrimary = false;
		return;
	}

	( this->*m_weaponParams[WEAPONIZER_PARAM_PRIMARY].funcShoot )( eState );

	SubtractAmmo( WEAPONIZER_PARAM_PRIMARY );

	// Keep pushing out the timer as long as we're firing.
	if ( m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flPrimaryAmmoRegenRate )
	{
		m_flNextPrimaryRegenTime = gpGlobals->curtime + m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flPrimaryAmmoRegenRate;
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

	m_flNextPrimaryAttack = gpGlobals->curtime + m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flRateOfFire;
}


//-----------------------------------------------------------------------------
// Shoot the weaponizer's secondary weapon!
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::SecondaryAttack( attackstate_t eState )
{
	if ( ( eState == ATTACK_RUN) && gpGlobals->curtime <= m_flNextSecondaryAttack )
		return;

	if ( shaper_use_weaponizer.GetBool() )
	{
		if ( eState != ATTACK_POST )
		{
			// IDBS: temp code, piggyback CShapeTarget off the weaponizer
			m_flNextSecondaryAttack = gpGlobals->curtime + CShapeTarget::WeaponSecondary( ToBasePlayer( GetOwner() ) );
		}
		return;
	}


	if ( ( m_iClip1 < m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flPrimaryAmmoBurnRate ) ||
		( m_iClip2 < m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flSecondaryAmmoBurnRate ) )
	{	 
		m_bFiringSecondary = false;
		return;
	}

	( this->*m_weaponParams[WEAPONIZER_PARAM_SECONDARY].funcShoot )( eState );

	SubtractAmmo( WEAPONIZER_PARAM_SECONDARY );

	// Keep pushing out the timer as long as we're firing.
	if ( m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flSecondaryAmmoRegenRate )
	{
		m_flNextSecondaryRegenTime = gpGlobals->curtime + m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flSecondaryAmmoRegenRate;
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
			m_flNextSecondaryAnimationTime = gpGlobals->curtime + m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flRateOfFire;
		}
	}

	m_flNextSecondaryAttack = gpGlobals->curtime + m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flRateOfFire;
}


//-----------------------------------------------------------------------------
// Purpose: Click secondary attack whilst holding an object to hurl it.
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::AbsorbScan()
{
	if ( m_flNextAbsorbTime > gpGlobals->curtime )
		return;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	
	if ( pOwner == NULL )
		return;

	// See if we should drop a held item
	if ( ( m_bActive ) && ( pOwner->m_afButtonPressed & IN_ATTACK2 ) )
	{
		// Drop the held object
		m_flNextPrimaryAttack = gpGlobals->curtime + 0.5;
		m_flNextAbsorbTime = gpGlobals->curtime + 0.5;

		DetachObject();

		DoEffect( EFFECT_READY );

		SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	}
	else
	{
		// Otherwise pick it up
		FindObjectResult_t result = FindObject();
		switch ( result )
		{
		case OBJECT_FOUND:
			WeaponSound( SPECIAL1 );
			SendWeaponAnim( ACT_VM_PRIMARYATTACK );
			m_flNextAbsorbTime = gpGlobals->curtime + 0.5f;

			break;

		case OBJECT_NOT_FOUND:
			m_flNextAbsorbTime = gpGlobals->curtime + 0.1f;
			CloseElements();

			if ( m_flNextAbsorbAnimationTime <= gpGlobals->curtime )
			{
				SendWeaponAnim( ACT_VM_DRYFIRE );
				m_flNextAbsorbAnimationTime = gpGlobals->curtime + SequenceDuration();
			}
			WeaponSound( EMPTY );
			break;

		case OBJECT_BEING_DETACHED:
			m_flNextAbsorbTime = gpGlobals->curtime + 0.01f;
			break;
		}

		DoEffect( EFFECT_HOLDING );
	}
}	


//-----------------------------------------------------------------------------
// Dumps out our ammo, emptying the gun.
// TODO: poop out a nugget of refined ammo?
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::Reload()
{
	if ( shaper_use_weaponizer.GetBool() )
	{
		CShapeTarget::WeaponReload();
		return false;
	}

	if ( weaponizer_toggle_fusion.GetBool() )
	{
		EmitSound( "Weaponizer_FusionToggle" );
		SendWeaponAnim( ACT_VM_PRIMARYATTACK );

		m_bFusionMode = !m_bFusionMode;

		m_flNextReloadTime = gpGlobals->curtime + 0.5f;

		return false;
	}

	m_flNextReloadTime = gpGlobals->curtime + 1.0f;

	WeaponSound( SPECIAL3 );
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );

	m_iPrimaryAmmoType = WEAPONIZER_MATERIAL_TYPE_NONE;
	m_iClip1 = 0;
	
	m_iSecondaryAmmoType = WEAPONIZER_MATERIAL_TYPE_NONE;
	m_iClip2 = 0;

	SelectWeapon();
		
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::WeaponIdle( void )
{
	m_bFiringSecondary = false;
	m_bFiringPrimary = false;

}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pObject - 
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::AttachObject( CBaseEntity *pObject, const Vector &vPosition )
{
	if ( m_bActive )
		return false;

	if ( CanPickupObject( pObject ) == false )
		return false;

	m_grabController.SetIgnorePitch( false );
	m_grabController.SetAngleAlignment( 0 );

	bool bKilledByGrab = false;

	IPhysicsObject *pPhysics = pObject->VPhysicsGetObject();

	// Must be valid
	if ( !pPhysics )
		return false;

	CHL2_Player *pOwner = (CHL2_Player *)ToBasePlayer( GetOwner() );

	m_bActive = true;
	if( pOwner )
	{
#ifdef HL2_EPISODIC
		CBreakableProp *pProp = dynamic_cast< CBreakableProp * >( pObject );

		if ( pProp && pProp->HasInteraction( PROPINTER_PHYSGUN_CREATE_FLARE ) )
		{
			pOwner->FlashlightTurnOff();
		}
#endif

		// NOTE: This can change the mass; so it must be done before max speed setting
		Physgun_OnPhysGunPickup( pObject, pOwner, PICKED_UP_BY_CANNON );
	}

	// NOTE :This must happen after OnPhysGunPickup because that can change the mass
	m_grabController.AttachEntity( pOwner, pObject, pPhysics, false, vPosition, (!bKilledByGrab) );

	if( pOwner )
	{
		pOwner->EnableSprint( false );

		float	loadWeight = ( 1.0f - GetLoadPercentage() );
		float	maxSpeed = hl2_walkspeed.GetFloat() + ( ( hl2_normspeed.GetFloat() - hl2_walkspeed.GetFloat() ) * loadWeight );

		//Msg( "Load perc: %f -- Movement speed: %f/%f\n", loadWeight, maxSpeed, hl2_normspeed.GetFloat() );
		pOwner->SetMaxSpeed( maxSpeed );
	}

	// Don't drop again for a slight delay, in case they were pulling objects near them
	m_flNextAbsorbTime = gpGlobals->curtime + 0.4f;

	DoEffect( EFFECT_HOLDING );
	OpenElements();

	if ( GetMotorSound() )
	{
		(CSoundEnvelopeController::GetController()).Play( GetMotorSound(), 0.0f, 50 );
		(CSoundEnvelopeController::GetController()).SoundChangePitch( GetMotorSound(), 100, 0.5f );
		(CSoundEnvelopeController::GetController()).SoundChangeVolume( GetMotorSound(), 0.8f, 0.5f );
	}

#if defined(HL2_DLL)
	if( weaponizer_right_turrets.GetBool() && pObject->ClassMatches("npc_turret_floor") )
	{
		// We just picked up a turret. Is it already upright?
		Vector vecUp;
		Vector vecTrueUp(0,0,1);
		pObject->GetVectors( NULL, NULL, &vecUp );
		float flDot = DotProduct( vecUp, vecTrueUp );

		if( flDot < 0.5f )
		{
			// The turret is NOT upright, so have the client help us by raising up the player's view.
			m_flTimeForceView = gpGlobals->curtime + 1.0f;
		}
	}
#endif

	return true;
}

void CWeaponWeaponizer::FindObjectTrace( CBasePlayer *pPlayer, trace_t *pTraceResult )
{
	Vector forward;
	pPlayer->EyeVectors( &forward );

	// Setup our positions
	Vector	start = pPlayer->Weapon_ShootPosition();
	float	testLength = TraceLength() * 4.0f;
	Vector	end = start + forward * testLength;

	if( IsMegaWeaponizer() && hl2_episodic.GetBool() )
	{
		Vector vecAutoAimDir = pPlayer->GetAutoaimVector( 1.0f, testLength );
		end = start + vecAutoAimDir * testLength;
	}

	// Try to find an object by looking straight ahead
	UTIL_WeaponizerTraceLine( start, end, pPlayer, this, pTraceResult );

	// Try again with a hull trace
	if ( !pTraceResult->DidHitNonWorldEntity() )
	{
		UTIL_WeaponizerTraceHull( start, end, -Vector(4,4,4), Vector(4,4,4), pPlayer, pTraceResult );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::IsAmmoFull( int iType )
{
	if ( iType == m_iPrimaryAmmoType || iType == m_iSecondaryAmmoType ) 
	{
		if ( s_AmmoParams[iType].iMaxAmmo == GetAmmoCount( iType ) )
			return true;
	}

	return false;
}


CWeaponWeaponizer::FindObjectResult_t CWeaponWeaponizer::FindObject( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	
	Assert( pPlayer );
	if ( pPlayer == NULL )
		return OBJECT_NOT_FOUND;
	
	trace_t tr;
	FindObjectTrace( pPlayer, &tr );
	CBaseEntity *pEntity = tr.m_pEnt ? tr.m_pEnt->GetRootMoveParent() : NULL;
	bool	bAttach = false;
	bool	bPull = false;


	// If we hit something, pick it up or pull it
	if ( ( tr.fraction != 1.0f ) && ( tr.m_pEnt ) && ( tr.m_pEnt->IsWorld() == false ) )
	{
		// Attempt to attach if within range
		if ( tr.fraction <= 0.25f )
		{
			bAttach = true;
		}
		else if ( tr.fraction > 0.25f )
		{
			bPull = true;
		}
	}
	
	Vector forward;
	pPlayer->EyeVectors( &forward );

	// Setup our positions
	Vector	start = pPlayer->Weapon_ShootPosition();
	float	testLength = TraceLength() * 4.0f;

	// Find anything within a general cone in front
	CBaseEntity *pConeEntity = MegaWeaponizerFindObjectInCone( start, forward, weaponizer_cone.GetFloat(), weaponizer_ball_cone.GetFloat(), bAttach || bPull );
	if ( pConeEntity )
	{
		pEntity = pConeEntity;

		// If the object is near, grab it. Else, pull it a bit.
		if ( pEntity->WorldSpaceCenter().DistToSqr( start ) <= (testLength * testLength) )
		{
			bAttach = true;
		}
		else
		{
			bPull = true;
		}
	}

	if ( !pEntity )
		return OBJECT_NOT_FOUND;
		
	int iType = GetWeaponizerMaterialType( pEntity );
	if ( iType == WEAPONIZER_MATERIAL_TYPE_NONE )
		return OBJECT_NOT_FOUND;

	// FIXME: need a way of disallowing absorption
	//if ( pEntity->CanBeAbsorbed() == false )
	//	return OBJECT_NOT_FOUND;

	if ( IsAmmoFull( iType ) )
	{
		EmitSound( "Weaponizer_AmmoFull" );
		return OBJECT_NOT_FOUND;
	}

	// This means we don't have a physics object but we can be absorbed (like fire or electricity)
	if ( pEntity && ( s_AmmoParams[iType].bRemoveOnAbsorb == false ) )
	{
		AbsorbEntity( pEntity, tr.endpos );
		return OBJECT_FOUND;
	}
	
	if ( CanPickupObject( pEntity ) == false )
	{
		CBaseEntity *pNewObject = Pickup_OnFailedPhysGunPickup( pEntity, start );

		if ( pNewObject && CanPickupObject( pNewObject ) )
		{
			pEntity = pNewObject;
		}
		else
		{
			// Make a noise to signify we can't pick this up
			if ( !m_flLastDenySoundPlayed )
			{
				m_flLastDenySoundPlayed = true;
				WeaponSound( SPECIAL3 );
			}

			return OBJECT_NOT_FOUND;
		}
	}

	// Check to see if the object is constrained + needs to be ripped off...
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( FClassnameIs( pEntity, "hunter_flechette" ) == true )
	{
		if ( pEntity->GetParent() )
		{
			pEntity->SetParent( NULL );

			pEntity->CreateVPhysics();

			if ( pEntity->VPhysicsGetObject() )
			{
				pEntity->SetMoveType( MOVETYPE_VPHYSICS );
				pEntity->VPhysicsGetObject()->EnableMotion( false );
				pEntity->AddSpawnFlags( SF_PHYSPROP_ENABLE_ON_PHYSCANNON );
				UTIL_SetSize( pEntity, -Vector(8,8,8), Vector(8,8,8) );
			}
		}
	}

	if ( !Pickup_OnAttemptPhysGunPickup( pEntity, pOwner, PICKED_UP_BY_CANNON ) )
		return OBJECT_BEING_DETACHED;

	if ( bAttach )
	{
		return AttachObject( pEntity, tr.endpos ) ? OBJECT_FOUND : OBJECT_NOT_FOUND;
	}

	if ( !bPull )
		return OBJECT_NOT_FOUND;

	// FIXME: This needs to be run through the CanPickupObject logic
	IPhysicsObject *pObj = pEntity->VPhysicsGetObject();
	if ( !pObj )
		return OBJECT_NOT_FOUND;

	// If we're too far, simply start to pull the object towards us
	Vector	pullDir = start - pEntity->WorldSpaceCenter();
	VectorNormalize( pullDir );
	pullDir *= IsMegaWeaponizer() ? weaponizer_mega_pullforce.GetFloat() : weaponizer_pullforce.GetFloat();
	
	float mass = PhysGetEntityMass( pEntity );
	if ( mass < 50.0f )
	{
		pullDir *= (mass + 0.5) * (1/50.0f);
	}

	// Nudge it towards us
	pObj->ApplyForceCenter( pullDir );
	return OBJECT_NOT_FOUND;
}

ConVar weaponizer_smart_absorb( "weaponizer_smart_absorb", "0" );

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
CBaseEntity *CWeaponWeaponizer::MegaWeaponizerFindObjectInCone( const Vector &vecOrigin, 
   const Vector &vecDir, float flCone, float flCombineBallCone, bool bOnlyCombineBalls )
{
	// Find the nearest physics-based item in a cone in front of me.
	CBaseEntity *list[1024];
	float flMaxDist = TraceLength() + 1.0;
	float flNearestDist = flMaxDist;
	Vector mins = vecOrigin - Vector( flNearestDist, flNearestDist, flNearestDist );
	Vector maxs = vecOrigin + Vector( flNearestDist, flNearestDist, flNearestDist );

	CBaseEntity *pNearest = NULL;

	int count = UTIL_EntitiesInBox( list, 1024, mins, maxs, 0 );
	for( int i = 0 ; i < count ; i++ )
	{
		if ( !list[ i ]->VPhysicsGetObject() )
			continue;
			
		WeaponizerMaterialTypes_t eType = GetWeaponizerMaterialType( list[i] );
		if ( eType == WEAPONIZER_MATERIAL_TYPE_NONE )
			continue;

		// Closer than other objects
		Vector los;
		VectorSubtract( list[ i ]->WorldSpaceCenter(), vecOrigin, los );
		float flDist = VectorNormalize( los );

		bool bUseCone = true;

		// Closer than other objects
		if( flDist >= flNearestDist )
			continue;

		if ( weaponizer_smart_absorb.GetBool() == true )
		{
			if ( eType < WEAPONIZER_NUM_PRIMARY_MATERIALS )
			{
				if ( m_iPrimaryAmmoType > 0 && m_iPrimaryAmmoType != eType )
				{
					bUseCone = false;
				}
			}
			else
			{
				if ( m_iSecondaryAmmoType > 0 && m_iSecondaryAmmoType != eType )
				{
					bUseCone = false;
				}
			}
		}

		// Cull to the cone
		if ( bUseCone == true )
		{
			if ( DotProduct( los, vecDir ) <= flCone )
				continue;
		}
		else
		{
			trace_t tr;
			UTIL_WeaponizerTraceLine( vecOrigin, vecOrigin + vecDir * TraceLength(), GetOwner(), this, &tr );

			if ( tr.m_pEnt != list[i] )
				continue;
		}

		if ( eType == m_iPrimaryAmmoType || eType == m_iSecondaryAmmoType )
		{
			if ( s_AmmoParams[eType].iMaxAmmo == GetAmmoCount( eType ) )
				continue;
		}
	
		// Make sure it isn't occluded!
		trace_t tr;
		UTIL_WeaponizerTraceLine( vecOrigin, list[ i ]->WorldSpaceCenter(), GetOwner(), this, &tr );
		if( tr.m_pEnt == list[ i ] )
		{
			flNearestDist = flDist;
			pNearest = list[ i ];
		}
	}

	return pNearest;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponizerGrabController::UpdateObject( CBasePlayer *pPlayer, float flError )
{
 	CBaseEntity *pEntity = GetAttached();
	if ( !pEntity || ComputeError() > flError || pPlayer->GetGroundEntity() == pEntity || !pEntity->VPhysicsGetObject() )
	{
		return false;
	}

	//Adrian: Oops, our object became motion disabled, let go!
	IPhysicsObject *pPhys = pEntity->VPhysicsGetObject();
	if ( pPhys && pPhys->IsMoveable() == false )
	{
		return false;
	}

	Vector forward, right, up;
	QAngle playerAngles = pPlayer->EyeAngles();
	AngleVectors( playerAngles, &forward, &right, &up );
	
	float pitch = AngleDistance(playerAngles.x,0);

	if( !m_bAllowObjectOverhead )
	{
		playerAngles.x = clamp( pitch, -75, 75 );
	}
	else
	{
		playerAngles.x = clamp( pitch, -90, 75 );
	}

	
	
	// Now clamp a sphere of object radius at end to the player's bbox
	Vector radial = physcollision->CollideGetExtent( pPhys->GetCollide(), vec3_origin, pEntity->GetAbsAngles(), -forward );
	Vector player2d = pPlayer->CollisionProp()->OBBMaxs();
	float playerRadius = player2d.Length2D();
	float radius = playerRadius + fabs(DotProduct( forward, radial ));

	float distance = 24 + ( radius * 2.0f );

	// Add the prop's distance offset
	distance += m_flDistanceOffset;

	Vector start = pPlayer->Weapon_ShootPosition();
	Vector end = start + ( forward * distance );

	trace_t	tr;
	CTraceFilterSkipTwoEntities traceFilter( pPlayer, pEntity, COLLISION_GROUP_NONE );
	Ray_t ray;
	ray.Init( start, end );
	enginetrace->TraceRay( ray, MASK_SOLID_BRUSHONLY, &traceFilter, &tr );

	if ( tr.fraction < 0.5 )
	{
		end = start + forward * (radius*0.5f);
	}
	else if ( tr.fraction <= 1.0f )
	{
		end = start + forward * ( distance - radius );
	}
	Vector playerMins, playerMaxs, nearest;
	pPlayer->CollisionProp()->WorldSpaceAABB( &playerMins, &playerMaxs );
	Vector playerLine = pPlayer->CollisionProp()->WorldSpaceCenter();
	CalcClosestPointOnLine( end, playerLine+Vector(0,0,playerMins.z), playerLine+Vector(0,0,playerMaxs.z), nearest, NULL );

	if( !m_bAllowObjectOverhead )
	{
		Vector delta = end - nearest;
		float len = VectorNormalize(delta);
		if ( len < radius )
		{
			end = nearest + radius * delta;
		}
	}

	//Show overlays of radius
	if ( g_debug_weaponizer.GetBool() )
	{
		NDebugOverlay::Box( end, -Vector( 2,2,2 ), Vector(2,2,2), 0, 255, 0, true, 0 );

		NDebugOverlay::Box( GetAttached()->WorldSpaceCenter(), 
							-Vector( radius, radius, radius), 
							Vector( radius, radius, radius ),
							255, 0, 0,
							true,
							0.0f );
	}

	QAngle angles = TransformAnglesFromPlayerSpace( m_attachedAnglesPlayerSpace, pPlayer );
	
	// If it has a preferred orientation, update to ensure we're still oriented correctly.
	Pickup_GetPreferredCarryAngles( pEntity, pPlayer, pPlayer->EntityToWorldTransform(), angles );

	// We may be holding a prop that has preferred carry angles
	if ( m_bHasPreferredCarryAngles )
	{
		matrix3x4_t tmp;
		ComputePlayerMatrix( pPlayer, tmp );
		angles = TransformAnglesToWorldSpace( m_vecPreferredCarryAngles, tmp );
	}

	matrix3x4_t attachedToWorld;
	Vector offset;
	AngleMatrix( angles, attachedToWorld );
	VectorRotate( m_attachedPositionObjectSpace, attachedToWorld, offset );

	Vector vecTargetPos = end - offset;

	SetTargetPosition( vecTargetPos, angles );
	
	if ( ( pEntity->GetAbsOrigin() - vecTargetPos ).Length() < 10.0f )
	{
		if ( m_flGrabTime == -1 )
		{
			m_flGrabTime = gpGlobals->curtime;
			if ( FClassnameIs( pEntity, "hunter_flechette" ) )
			{
				m_flGrabTime += weaponizer_grab_flechette_delay.GetFloat();
			}
		}

		if ( m_pNotifyTarget && ( gpGlobals->curtime >= m_flGrabTime ) )
		{
			m_pNotifyTarget->OnEntityFullyGrabbed( pEntity );
		}
	}

	return true;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::OnEntityFullyGrabbed( CBaseEntity *pEntity )
{
	AbsorbEntity( pEntity, vec3_origin );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
ConVar weaponizer_mass_ratio( "weaponizer_mass_ratio", "15.0" );

void CWeaponWeaponizer::AbsorbEntity( CBaseEntity *pEntity, Vector vTracePos )
{
	int iType = GetWeaponizerMaterialType( pEntity );

	// Changing ammo types, so reset ammo count.
	if ( ( iType < WEAPONIZER_NUM_PRIMARY_MATERIALS ) && ( iType != m_iPrimaryAmmoType ) )
	{
		m_iClip1 = 0;
	}
	else if ( ( iType > WEAPONIZER_NUM_PRIMARY_MATERIALS ) && ( iType != m_iSecondaryAmmoType ) )
	{
		m_iClip2 = 0;
	}

	if ( s_AmmoParams[iType].bMassBased )
	{
		// Ammo is added based on the mass of the object absorbed
		// Absorbing a physical material. Convert to ammo based on mass absorbed.
		float massTotal = 0;

		IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
		int count = pEntity->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
		for ( int i = 0; i < count; i++ )
		{
			massTotal += m_grabController.m_savedMass[i];
		}

		if ( s_AmmoParams[iType].flAmmoPerAbsorb != 0 )
		{
			massTotal /= s_AmmoParams[iType].flAmmoPerAbsorb;
		}

	
		int iNewModType = WEAPONIZER_MODIFIER_NONE;

		if ( FClassnameIs( pEntity, "hunter_flechette" ) == true )
		{
			m_iModifierCount = 6;
			iNewModType = WEAPONIZER_MODIFIER_FLECHETTE;
		}

		// Allow props that are specifically flagged as such
		CPhysicsProp *pPhysProp = dynamic_cast<CPhysicsProp *>(pEntity);
		if ( pPhysProp != NULL && ( pPhysProp->HasInteraction( PROPINTER_PHYSGUN_BREAK_EXPLODE ) || pPhysProp->HasInteraction( PROPINTER_FIRE_FLAMMABLE ) ) )
		{
			m_iModifierCount = 3;
			iNewModType = WEAPONIZER_MODIFIER_EXPLOSIVE;
		}

		if ( iNewModType != m_iModifierType )
		{
			m_iClip1 = 0;
		}

		m_iModifierType = iNewModType;

		m_flAmmoRemainder += massTotal;
		m_iClip1 = MIN( s_AmmoParams[iType].iMaxAmmo, m_iClip1 + ( int )m_flAmmoRemainder );
		m_flAmmoRemainder -= ( int )m_flAmmoRemainder;

	}
	else
	{
		// Ammo is added based on rules in the ammo params 
		if ( iType < WEAPONIZER_NUM_PRIMARY_MATERIALS )
		{
			m_iClip1 = MIN( s_AmmoParams[iType].iMaxAmmo, m_iClip1 + (int)s_AmmoParams[iType].flAmmoPerAbsorb );
		}
		else if ( iType > WEAPONIZER_NUM_PRIMARY_MATERIALS )
		{
			m_iClip2 = MIN( s_AmmoParams[iType].iMaxAmmo, m_iClip2 + (int)s_AmmoParams[iType].flAmmoPerAbsorb );
		}
	}

	// Set the ammo type that we're using now
	if ( iType < WEAPONIZER_NUM_PRIMARY_MATERIALS )
	{
		m_iPrimaryAmmoType = iType;
	}
	else
	{
		m_iSecondaryAmmoType = iType;
	}
	
	// FIXME: we will need OnAbsorb eventually
	//pEntity->m_OnAbsorb.FireOutput( this, this, 0 );

	// Special case for fire.
	// If we absorbed fire from an entity then extinguish them
	if ( iType == WEAPONIZER_MATERIAL_TYPE_FIRE )
	{
		if ( pEntity->GetEffectEntity() && GetWeaponizerMaterialType( pEntity->GetEffectEntity() ) == WEAPONIZER_MATERIAL_TYPE_FIRE )
		{
			g_EventQueue.AddEvent( pEntity->GetEffectEntity(), "Extinguish", 1.0f, this, this );
		}
	}

	SelectWeapon();

	WeaponParams_t params;
	
	if ( iType < WEAPONIZER_NUM_PRIMARY_MATERIALS )
	{
		params = m_weaponParams[WEAPONIZER_PARAM_PRIMARY];
	}
	else
	{
		params = m_weaponParams[WEAPONIZER_PARAM_SECONDARY];
	}

	if ( Q_strlen( params.absorbsound ) > 0 )
	{
		EmitSound( params.absorbsound );
	}

	if ( Q_strlen( params.absorbeffect ) > 0 )
	{
		if ( iType == WEAPONIZER_MATERIAL_TYPE_ELECTRICAL )
		{
			vTracePos = pEntity->WorldSpaceCenter();
		}

		EntityMessageBegin( this, true );
			WRITE_BYTE( WEAPONIZER_FX_ABSORB );
			WRITE_STRING( params.absorbattach );
			WRITE_VEC3COORD( vTracePos );
			WRITE_STRING( params.absorbeffect );
		MessageEnd();
	}

	if ( s_AmmoParams[iType].bRemoveOnAbsorb )
	{
		UTIL_Remove( pEntity );
	}
}


void CWeaponWeaponizer::UpdateObject( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	Assert( pPlayer );

	float flError = IsMegaWeaponizer() ? 18 : 12;
	if ( !m_grabController.UpdateObject( pPlayer, flError ) )
	{
		DetachObject();
		return;
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DetachObject( bool playSound, bool wasLaunched )
{
	if ( m_bActive == false )
		return;

	CHL2_Player *pOwner = (CHL2_Player *)ToBasePlayer( GetOwner() );
	if( pOwner != NULL )
	{
		pOwner->EnableSprint( true );
		pOwner->SetMaxSpeed( hl2_normspeed.GetFloat() );
		
		if( wasLaunched )
		{
			pOwner->RumbleEffect( RUMBLE_357, 0, RUMBLE_FLAG_RESTART );
		}
	}

	CBaseEntity *pObject = m_grabController.GetAttached();

	m_grabController.DetachEntity( wasLaunched );

	if ( pObject != NULL )
	{
		Pickup_OnPhysGunDrop( pObject, pOwner, wasLaunched ? LAUNCHED_BY_CANNON : DROPPED_BY_CANNON );
	}

	// Stop our looping sound
	if ( GetMotorSound() )
	{
		(CSoundEnvelopeController::GetController()).SoundChangeVolume( GetMotorSound(), 0.0f, 1.0f );
		(CSoundEnvelopeController::GetController()).SoundChangePitch( GetMotorSound(), 50, 1.0f );
	}

	m_bActive = false;

	if ( playSound )
	{
		//Play the detach sound
		WeaponSound( MELEE_MISS );
	}

	RecordThrownObject( pObject );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ItemPreFrame()
{
	BaseClass::ItemPreFrame();

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	m_flElementPosition = UTIL_Approach( m_flElementDestination, m_flElementPosition, 0.1f );

	CBaseViewModel *vm = pOwner->GetViewModel(); 
	
	if ( vm != NULL )
	{
		// This has to happen here because of how the SetModel interacts with the caching at startup
		if ( m_sbStaticPoseParamsLoaded == false )
		{
			m_poseActive = LookupPoseParameter( "active" );
			m_sbStaticPoseParamsLoaded = true;
		}

		vm->SetPoseParameter( m_poseActive, m_flElementPosition );
	}

	// Update the object if the weapon is switched on.
	if( m_bActive )
	{
		UpdateObject();
	}

	if( gpGlobals->curtime >= m_flTimeNextObjectPurge )
	{
		PurgeThrownObjects();
		m_flTimeNextObjectPurge = gpGlobals->curtime + 0.5f;
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::CheckForTarget( void )
{
	//See if we're suppressing this
	if ( m_flCheckSuppressTime > gpGlobals->curtime )
		return;

	// holstered
	if ( IsEffectActive( EF_NODRAW ) )
		return;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	if ( m_bActive )
		return;

	trace_t	tr;
	FindObjectTrace( pOwner, &tr );

	if ( ( tr.fraction != 1.0f ) && ( tr.m_pEnt != NULL ) )
	{
		float dist = (tr.endpos - tr.startpos).Length();
		if ( dist <= TraceLength() )
		{
			// FIXME: Try just having the elements always open when pointed at a physics object
			if ( CanPickupObject( tr.m_pEnt ) || Pickup_ForcePhysGunOpen( tr.m_pEnt, pOwner ) )
			// if ( ( tr.m_pEnt->VPhysicsGetObject() != NULL ) && ( tr.m_pEnt->GetMoveType() == MOVETYPE_VPHYSICS ) )
			{
				m_nChangeState = ELEMENT_STATE_NONE;
				OpenElements();
				return;
			}
		}
	}

	// Close the elements after a delay to prevent overact state switching
	if ( ( m_flElementDebounce < gpGlobals->curtime ) && ( m_nChangeState == ELEMENT_STATE_NONE ) )
	{
		m_nChangeState = ELEMENT_STATE_CLOSED;
		m_flElementDebounce = gpGlobals->curtime + 0.5f;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Update our idle effects even when deploying
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ItemBusyFrame( void )
{
	BaseClass::ItemBusyFrame();
}

ConVar weaponizer_discharge_damage( "weaponizer_discharge_damage", "10" );

void CWeaponWeaponizer::CheckWaterLevel( CBasePlayer *pOwner )
{
	if ( pOwner->GetWaterLevel() == 0 )
		return;

	if ( m_iPrimaryAmmoType == WEAPONIZER_MATERIAL_TYPE_ELECTRICAL )
	{
		// Empty clip and force a weapon switch 
		m_iClip1 = 0;
		SubtractAmmo( WEAPONIZER_PARAM_PRIMARY );
		SelectWeapon();

		color32 shock = { 0, 64, 128, 128 };
		UTIL_ScreenFade( pOwner, shock, 1.0f, 0.1f, FFADE_IN );
		EmitSound( "Weaponizer_DischargeElectrical" );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::ItemPostFrame()
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	
	if ( pOwner == NULL )
	{
		return;
	}

	if ( ( pOwner->m_nButtons & IN_RELOAD ) && ( gpGlobals->curtime > m_flNextReloadTime ) )
	{
		Reload();
		return;
	}

	CheckWaterLevel( pOwner );

	//Check for object in pickup range
	if ( m_bActive == false )
	{
		CheckForTarget();

		if ( ( m_flElementDebounce < gpGlobals->curtime ) && ( m_nChangeState != ELEMENT_STATE_NONE ) )
		{
			if ( m_nChangeState == ELEMENT_STATE_OPEN )
			{
				OpenElements();
			}
			else if ( m_nChangeState == ELEMENT_STATE_CLOSED )
			{
				CloseElements();
			}

			m_nChangeState = ELEMENT_STATE_NONE;
		}
	}

	if ( pOwner->m_nButtons & IN_ALT1 )
	{
		AbsorbScan();
	}
	else
	{
		// Reset our debouncer
		m_flLastDenySoundPlayed = false;

		if ( m_bActive == false )
		{
			DoEffect( EFFECT_READY );
		}
	}
	
	if ( m_iQueuedFlechettes > 0 && m_flNextFlechetteFireTime <= gpGlobals->curtime && m_iClip1 > 0 )
	{
		ShootFlak( ATTACK_RUN );
	}

	bool bFired = false;

	if ( pOwner->m_nButtons & IN_ATTACK )
	{
		// FIXME: do we need two m_eAttackStates, one for primary, one for secondary?
		PrimaryAttack(  ATTACK_RUN );
		bFired = true;
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
		PrimaryAttack( ATTACK_POST );
	}

	if ( pOwner->m_afButtonReleased & IN_ATTACK2 )
	{	
		SecondaryAttack( ATTACK_POST );
	}

	m_nAttackDebounce &= ~pOwner->m_afButtonReleased;

	// Handle ammo regeneration here
	if ( m_flNextPrimaryRegenTime && ( gpGlobals->curtime > m_flNextPrimaryRegenTime ) )
	{
		m_iClip1++;
		if ( m_weaponParams[WEAPONIZER_PARAM_PRIMARY].nPrimaryAmmoMax > 0 )
		{
			m_iClip1 = MIN( m_iClip1, m_weaponParams[WEAPONIZER_PARAM_PRIMARY].nPrimaryAmmoMax );
		}
		
		m_flNextPrimaryRegenTime = gpGlobals->curtime + m_weaponParams[WEAPONIZER_PARAM_PRIMARY].flPrimaryAmmoRegenRate;
	}

	if ( m_flNextSecondaryRegenTime && ( gpGlobals->curtime > m_flNextSecondaryRegenTime ) )
	{
		m_iClip2++;
		if ( m_weaponParams[WEAPONIZER_PARAM_SECONDARY].nSecondaryAmmoMax > 0 )
		{	
			m_iClip2 = MIN( m_iClip2, m_weaponParams[WEAPONIZER_PARAM_SECONDARY].nSecondaryAmmoMax );
		}

		m_flNextSecondaryRegenTime = gpGlobals->curtime + m_weaponParams[WEAPONIZER_PARAM_SECONDARY].flSecondaryAmmoRegenRate;
	}

	if ( bFired == false ) 
	{
		WeaponIdle();
	}

	if ( m_bFiringPrimary == false && m_pFiringLoop )
	{
		StopSound( "Weaponizer_FlameThrower.FireStart" );

		CSoundEnvelopeController::GetController().SoundDestroy( m_pFiringLoop );
		m_pFiringLoop = NULL;
	}

	if ( m_bFiringSecondary == false && m_pLiquidLoop )
	{
		StopSound( "Weaponizer_WaterLiquid.FireStart" );

		CSoundEnvelopeController::GetController().SoundFadeOut( m_pLiquidLoop, 0.5f, true );
		m_pLiquidLoop = NULL;
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
#define PHYSCANNON_DANGER_SOUND_RADIUS 128

void CWeaponWeaponizer::LaunchObject( const Vector &vecDir, float flForce )
{
	// FIRE!!!
	if( m_grabController.GetAttached() )
	{
		CBaseEntity *pObject = m_grabController.GetAttached();

		gamestats->Event_Punted( pObject );

		DetachObject( false, true );

		// Trace ahead a bit and make a chain of danger sounds ahead of the phys object
		// to scare potential targets
		trace_t	tr;
		Vector	vecStart = pObject->GetAbsOrigin();
		Vector	vecSpot;
		int		iLength;
		int		i;

		UTIL_TraceLine( vecStart, vecStart + vecDir * flForce, MASK_SHOT, pObject, COLLISION_GROUP_NONE, &tr );
		iLength = ( tr.startpos - tr.endpos ).Length();
		vecSpot = vecStart + vecDir * PHYSCANNON_DANGER_SOUND_RADIUS;

		for( i = PHYSCANNON_DANGER_SOUND_RADIUS ; i < iLength ; i += PHYSCANNON_DANGER_SOUND_RADIUS )
		{
			CSoundEnt::InsertSound( SOUND_PHYSICS_DANGER, vecSpot, PHYSCANNON_DANGER_SOUND_RADIUS, 0.5, pObject );
			vecSpot = vecSpot + ( vecDir * PHYSCANNON_DANGER_SOUND_RADIUS );
		}
				
		// Launch
		ApplyVelocityBasedForce( pObject, vecDir, tr.endpos, PHYSGUN_FORCE_LAUNCHED );

		// Don't allow the gun to regrab a thrown object!!
		m_flNextAbsorbTime = m_flNextPrimaryAttack = gpGlobals->curtime + 0.5;
		
		Vector	center = pObject->WorldSpaceCenter();

		//Do repulse effect
		DoEffect( EFFECT_LAUNCH, &center );
	}

	// Stop our looping sound
	if ( GetMotorSound() )
	{
		(CSoundEnvelopeController::GetController()).SoundChangeVolume( GetMotorSound(), 0.0f, 1.0f );
		(CSoundEnvelopeController::GetController()).SoundChangePitch( GetMotorSound(), 50, 1.0f );
	}

	//Close the elements and suppress checking for a bit
	m_nChangeState = ELEMENT_STATE_CLOSED;
	m_flElementDebounce = gpGlobals->curtime + 0.1f;
	m_flCheckSuppressTime = gpGlobals->curtime + 0.25f;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pTarget - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::CanPickupObject( CBaseEntity *pTarget )
{
	if ( pTarget == NULL )
		return false;

	if ( pTarget->GetBaseAnimating() && pTarget->GetBaseAnimating()->IsDissolving() )
		return false;

	if ( pTarget->HasSpawnFlags( SF_PHYSBOX_ALWAYS_PICK_UP ) || pTarget->HasSpawnFlags( SF_PHYSBOX_NEVER_PICK_UP ) )
	{
		// It may seem strange to check this spawnflag before we know the class of this object, since the 
		// spawnflag only applies to func_physbox, but it can act as a filter of sorts to reduce the number 
		// of irrelevant entities that fall through to this next casting check, which is slower.
		CPhysBox *pPhysBox = dynamic_cast<CPhysBox*>(pTarget);

		if ( pPhysBox != NULL )
		{
			if ( pTarget->HasSpawnFlags( SF_PHYSBOX_NEVER_PICK_UP ) )
                return false;
			else
				return true;
		}
	}

	if ( pTarget->HasSpawnFlags(SF_PHYSPROP_ALWAYS_PICK_UP) )
	{
		// It may seem strange to check this spawnflag before we know the class of this object, since the 
		// spawnflag only applies to func_physbox, but it can act as a filter of sorts to reduce the number 
		// of irrelevant entities that fall through to this next casting check, which is slower.
		CPhysicsProp *pPhysProp = dynamic_cast<CPhysicsProp*>(pTarget);
		if ( pPhysProp != NULL )
			return true;
	}

	if ( pTarget->IsEFlagSet( EFL_NO_PHYSCANNON_INTERACTION ) )
		return false;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner && pOwner->GetGroundEntity() == pTarget )
		return false;

	if ( FClassnameIs( pTarget, "hunter_flechette" ) == true )
		return true;

	// Allow props that are specifically flagged as such
	CPhysicsProp *pPhysProp = dynamic_cast<CPhysicsProp *>(pTarget);
	if ( pPhysProp == NULL )
		return false;
	
	if ( pPhysProp->HasInteraction( PROPINTER_PHYSGUN_BREAK_EXPLODE ) == false && pPhysProp->HasInteraction( PROPINTER_FIRE_FLAMMABLE ) == false )
		return false;

	//if ( !IsMegaWeaponizer() )
	{
		if ( pTarget->VPhysicsIsFlesh( ) )
			return false;
		return CBasePlayer::CanPickupObject( pTarget, weaponizer_maxmass.GetFloat(), 0 );
	}

	//if ( pTarget->IsNPC() && pTarget->MyNPCPointer()->CanBecomeRagdoll() )
	//	return true;
	//
	//if ( dynamic_cast<CRagdollProp*>(pTarget) )
	//	return true;
	//
	//return CBasePlayer::CanPickupObject( pTarget, 0, 0 );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::OpenElements( void )
{
	if ( m_bOpen )
		return;

	WeaponSound( SPECIAL2 );

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	if( !IsMegaWeaponizer() )
	{
		pOwner->RumbleEffect( RUMBLE_PHYSCANNON_OPEN, 0, RUMBLE_FLAG_RESTART );
	}

	if ( m_flElementPosition < 0.0f )
		m_flElementPosition = 0.0f;

	m_flElementDestination = 1.0f;

	SendWeaponAnim( ACT_VM_IDLE );

	m_bOpen = true;

	DoEffect( EFFECT_READY );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::CloseElements( void )
{
	// The mega cannon cannot be closed!
	if ( IsMegaWeaponizer() )
	{
		OpenElements();
		return;
	}

	if ( m_bOpen == false )
		return;

	WeaponSound( MELEE_HIT );

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner == NULL )
		return;

	pOwner->RumbleEffect(RUMBLE_PHYSCANNON_OPEN, 0, RUMBLE_FLAG_STOP);

	if ( m_flElementPosition > 1.0f )
		m_flElementPosition = 1.0f;

	m_flElementDestination = 0.0f;

	SendWeaponAnim( ACT_VM_IDLE );

	m_bOpen = false;

	if ( GetMotorSound() )
	{
		(CSoundEnvelopeController::GetController()).SoundChangeVolume( GetMotorSound(), 0.0f, 1.0f );
		(CSoundEnvelopeController::GetController()).SoundChangePitch( GetMotorSound(), 50, 1.0f );
	}
	
	DoEffect( EFFECT_CLOSED );
}

#define	PHYSCANNON_MAX_MASS		500


//-----------------------------------------------------------------------------
// Purpose: 
// Output : float
//-----------------------------------------------------------------------------
float CWeaponWeaponizer::GetLoadPercentage( void )
{
	float loadWeight = m_grabController.GetLoadWeight();
	loadWeight /= weaponizer_maxmass.GetFloat();	
	loadWeight = clamp( loadWeight, 0.0f, 1.0f );
	return loadWeight;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : CSoundPatch
//-----------------------------------------------------------------------------
CSoundPatch *CWeaponWeaponizer::GetMotorSound( void )
{
	if ( m_sndMotor == NULL )
	{
		CPASAttenuationFilter filter( this );
		
		if ( IsMegaWeaponizer() )
		{
			m_sndMotor = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_MegaWeaponizer.HoldSound", ATTN_NORM );
		}
		else
		{
			m_sndMotor = (CSoundEnvelopeController::GetController()).SoundCreate( filter, entindex(), CHAN_STATIC, "Weapon_Weaponizer.HoldSound", ATTN_NORM );
		}
	}

	return m_sndMotor;
}


//-----------------------------------------------------------------------------
// Shuts down sounds
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::StopLoopingSounds()
{
	if ( m_sndMotor != NULL )
	{
		 (CSoundEnvelopeController::GetController()).SoundDestroy( m_sndMotor );
		 m_sndMotor = NULL;
	}

	BaseClass::StopLoopingSounds();
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DestroyEffects( void )
{
	//Turn off main glow
	if ( m_hCenterSprite != NULL )
	{
		UTIL_Remove( m_hCenterSprite );
		m_hCenterSprite = NULL;
	}

	if ( m_hBlastSprite != NULL )
	{
		UTIL_Remove( m_hBlastSprite );
		m_hBlastSprite = NULL;
	}

	// Turn off beams
	for ( int i = 0; i < NUM_BEAMS; i++ )
	{
		if ( m_hBeams[i] != NULL )
		{
			UTIL_Remove( m_hBeams[i] );
			m_hBeams[i] = NULL;
		}
	}

	// Turn off sprites
	for ( int i = 0; i < NUM_SPRITES; i++ )
	{
		if ( m_hGlowSprites[i] != NULL )
		{
			UTIL_Remove( m_hGlowSprites[i] );
			m_hGlowSprites[i] = NULL;
		}
	}

	for ( int i = 0; i < 2; i++ )
	{
		if ( m_hEndSprites[i] != NULL )
		{
			UTIL_Remove( m_hEndSprites[i] );
			m_hEndSprites[i] = NULL;
		}
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::StopEffects( bool stopSound )
{
	// Turn off our effect state
	DoEffect( EFFECT_NONE );

	//Turn off main glow
	if ( m_hCenterSprite != NULL )
	{
		m_hCenterSprite->TurnOff();
	}

	if ( m_hBlastSprite != NULL )
	{
		m_hBlastSprite->TurnOff();
	}

	//Turn off beams
	for ( int i = 0; i < NUM_BEAMS; i++ )
	{
		if ( m_hBeams[i] != NULL )
		{
			m_hBeams[i]->SetBrightness( 0 );
		}
	}

	//Turn off sprites
	for ( int i = 0; i < NUM_SPRITES; i++ )
	{
		if ( m_hGlowSprites[i] != NULL )
		{
			m_hGlowSprites[i]->TurnOff();
		}
	}

	for ( int i = 0; i < 2; i++ )
	{
		if ( m_hEndSprites[i] != NULL )
		{
			m_hEndSprites[i]->TurnOff();
		}
	}

	//Shut off sounds
	if ( stopSound && GetMotorSound() != NULL )
	{
		(CSoundEnvelopeController::GetController()).SoundFadeOut( GetMotorSound(), 0.1f );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::StartEffects( void )
{
	return;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	bool bIsMegaCannon = IsMegaWeaponizer();

	int i;
	float flScaleFactor = SpriteScaleFactor();
	CBaseEntity *pBeamEnt = pOwner->GetViewModel();

	// Create the beams
	for ( i = 0; i < NUM_BEAMS; i++ )
	{
		if ( m_hBeams[i] )
			continue;

		const char *beamAttachNames[] = 
		{
			"fork1t",
			"fork2t",
			"fork1t",
			"fork2t",
			"fork1t",
			"fork2t",
		};

		m_hBeams[i] = CBeam::BeamCreate( 
			bIsMegaCannon ? MEGACANNON_BEAM_SPRITE : PHYSCANNON_BEAM_SPRITE, 1.0f );
		m_hBeams[i]->EntsInit( pBeamEnt, pBeamEnt );

		int	startAttachment = LookupAttachment( beamAttachNames[i] );
		int endAttachment	= 1;

		m_hBeams[i]->FollowEntity( pBeamEnt );

		m_hBeams[i]->AddSpawnFlags( SF_BEAM_TEMPORARY );	
		m_hBeams[i]->SetStartAttachment( startAttachment );
		m_hBeams[i]->SetEndAttachment( endAttachment );
		m_hBeams[i]->SetNoise( random->RandomFloat( 8.0f, 16.0f ) );
		m_hBeams[i]->SetColor( 255, 255, 255 );
		m_hBeams[i]->SetScrollRate( 25 );
		m_hBeams[i]->SetBrightness( 128 );
		m_hBeams[i]->SetWidth( 0 );
		m_hBeams[i]->SetEndWidth( random->RandomFloat( 2, 4 ) );
	}

	//Create the glow sprites
	for ( i = 0; i < NUM_SPRITES; i++ )
	{
		if ( m_hGlowSprites[i] )
			continue;

		const char *attachNames[] = 
		{
			"fork1b",
			"fork1m",
			"fork1t",
			"fork2b",
			"fork2m",
			"fork2t"
		};

		m_hGlowSprites[i] = CSprite::SpriteCreate( 
			bIsMegaCannon ? MEGACANNON_GLOW_SPRITE : PHYSCANNON_GLOW_SPRITE, 
			GetAbsOrigin(), false );

		m_hGlowSprites[i]->SetAsTemporary();

		m_hGlowSprites[i]->SetAttachment( pOwner->GetViewModel(), LookupAttachment( attachNames[i] ) );
		
		if ( bIsMegaCannon )
		{
			m_hGlowSprites[i]->SetTransparency( kRenderTransAdd, 255, 255, 255, 128, kRenderFxNone );
		}
		else
		{
			m_hGlowSprites[i]->SetTransparency( kRenderTransAdd, 255, 128, 0, 64, kRenderFxNoDissipation );
		}

		m_hGlowSprites[i]->SetBrightness( 255, 0.2f );
		m_hGlowSprites[i]->SetScale( 0.25f * flScaleFactor, 0.2f );
	}

	//Create the endcap sprites
	for ( i = 0; i < 2; i++ )
	{
		if ( m_hEndSprites[i] == NULL )
		{
			const char *attachNames[] = 
			{
				"fork1t",
				"fork2t"
			};

			m_hEndSprites[i] = CSprite::SpriteCreate( 
				bIsMegaCannon ? MEGACANNON_ENDCAP_SPRITE : PHYSCANNON_ENDCAP_SPRITE, 
				GetAbsOrigin(), false );

			m_hEndSprites[i]->SetAsTemporary();
			m_hEndSprites[i]->SetAttachment( pOwner->GetViewModel(), LookupAttachment( attachNames[i] ) );
			m_hEndSprites[i]->SetTransparency( kRenderTransAdd, 255, 255, 255, 255, kRenderFxNoDissipation );
			m_hEndSprites[i]->SetBrightness( 255, 0.2f );
			m_hEndSprites[i]->SetScale( 0.25f * flScaleFactor, 0.2f );
			m_hEndSprites[i]->TurnOff();
		}
	}

	//Create the center glow
	if ( m_hCenterSprite == NULL )
	{
		m_hCenterSprite = CSprite::SpriteCreate( 
			bIsMegaCannon ? MEGACANNON_CENTER_GLOW : PHYSCANNON_CENTER_GLOW, 
			GetAbsOrigin(), false );

		m_hCenterSprite->SetAsTemporary();
		m_hCenterSprite->SetAttachment( pOwner->GetViewModel(), 1 );
		m_hCenterSprite->SetTransparency( kRenderTransAdd, 255, 255, 255, 255, kRenderFxNone );
		m_hCenterSprite->SetBrightness( 255, 0.2f );
		m_hCenterSprite->SetScale( 0.1f, 0.2f );
	}

	//Create the blast sprite
	if ( m_hBlastSprite == NULL )
	{
		m_hBlastSprite = CSprite::SpriteCreate( 
			bIsMegaCannon ? MEGACANNON_BLAST_SPRITE : PHYSCANNON_BLAST_SPRITE, 
			GetAbsOrigin(), false );

		m_hBlastSprite->SetAsTemporary();
		m_hBlastSprite->SetAttachment( pOwner->GetViewModel(), 1 );
		m_hBlastSprite->SetTransparency( kRenderTransAdd, 255, 255, 255, 255, kRenderFxNone );
		m_hBlastSprite->SetBrightness( 255, 0.2f );
		m_hBlastSprite->SetScale( 0.1f, 0.2f );
		m_hBlastSprite->TurnOff();
	}
}

//-----------------------------------------------------------------------------
// Closing effects
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoEffectClosed( void )
{
	float flScaleFactor = SpriteScaleFactor();

	// Turn off the center sprite
	if ( m_hCenterSprite != NULL )
	{
		m_hCenterSprite->SetBrightness( 0.0, 0.1f );
		m_hCenterSprite->SetScale( 0.0f, 0.1f );
		m_hCenterSprite->TurnOff();
	}

	// Turn off the end-caps
	for ( int i = 0; i < 2; i++ )
	{
		if ( m_hEndSprites[i] != NULL )
		{
			m_hEndSprites[i]->TurnOff();
		}
	}

	// Turn off the lightning
	for ( int i = 0; i < NUM_BEAMS; i++ )
	{
		if ( m_hBeams[i] != NULL )
		{
			m_hBeams[i]->SetBrightness( 0 );
		}
	}

	// Turn on the glow sprites
	for ( int i = 0; i < NUM_SPRITES; i++ )
	{
		if ( m_hGlowSprites[i] != NULL )
		{
			m_hGlowSprites[i]->TurnOn();
			m_hGlowSprites[i]->SetBrightness( 16.0f, 0.2f );
			m_hGlowSprites[i]->SetScale( 0.3f * flScaleFactor, 0.2f );
		}
	}
	
	// Prepare for scale down
	if ( m_hBlastSprite != NULL )
	{
		m_hBlastSprite->TurnOn();
		m_hBlastSprite->SetScale( 1.0f, 0.0f );
		m_hBlastSprite->SetBrightness( 0, 0.0f );
	}
}

//-----------------------------------------------------------------------------
// Closing effects
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoMegaEffectClosed( void )
{
	float flScaleFactor = SpriteScaleFactor();

	// Turn off the center sprite
	if ( m_hCenterSprite != NULL )
	{
		m_hCenterSprite->SetBrightness( 0.0, 0.1f );
		m_hCenterSprite->SetScale( 0.0f, 0.1f );
		m_hCenterSprite->TurnOff();
	}

	// Turn off the end-caps
	for ( int i = 0; i < 2; i++ )
	{
		if ( m_hEndSprites[i] != NULL )
		{
			m_hEndSprites[i]->TurnOff();
		}
	}

	// Turn off the lightning
	for ( int i = 0; i < NUM_BEAMS; i++ )
	{
		if ( m_hBeams[i] != NULL )
		{
			m_hBeams[i]->SetBrightness( 0 );
		}
	}

	// Turn on the glow sprites
	for ( int i = 0; i < NUM_SPRITES; i++ )
	{
		if ( m_hGlowSprites[i] != NULL )
		{
			m_hGlowSprites[i]->TurnOn();
			m_hGlowSprites[i]->SetBrightness( 16.0f, 0.2f );
			m_hGlowSprites[i]->SetScale( 0.3f * flScaleFactor, 0.2f );
		}
	}
	
	// Prepare for scale down
	if ( m_hBlastSprite != NULL )
	{
		m_hBlastSprite->TurnOn();
		m_hBlastSprite->SetScale( 1.0f, 0.0f );
		m_hBlastSprite->SetBrightness( 0, 0.0f );
	}
}

//-----------------------------------------------------------------------------
// Ready effects
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoEffectReady( )
{
	float flScaleFactor = SpriteScaleFactor();

	//Turn on the center sprite
	if ( m_hCenterSprite != NULL )
	{
		m_hCenterSprite->SetBrightness( 128, 0.2f );
		m_hCenterSprite->SetScale( 0.15f, 0.2f );
		m_hCenterSprite->TurnOn();
	}

	//Turn off the end-caps
	for ( int i = 0; i < 2; i++ )
	{
		if ( m_hEndSprites[i] != NULL )
		{
			m_hEndSprites[i]->TurnOff();
		}
	}

	//Turn off the lightning
	for ( int i = 0; i < NUM_BEAMS; i++ )
	{
		if ( m_hBeams[i] != NULL )
		{
			m_hBeams[i]->SetBrightness( 0 );
		}
	}

	//Turn on the glow sprites
	for ( int i = 0; i < NUM_SPRITES; i++ )
	{
		if ( m_hGlowSprites[i] != NULL )
		{
			m_hGlowSprites[i]->TurnOn();
			m_hGlowSprites[i]->SetBrightness( 32.0f, 0.2f );
			m_hGlowSprites[i]->SetScale( 0.4f * flScaleFactor, 0.2f );
		}
	}

	//Scale down
	if ( m_hBlastSprite != NULL )
	{
		m_hBlastSprite->TurnOn();
		m_hBlastSprite->SetScale( 0.1f, 0.2f );
		m_hBlastSprite->SetBrightness( 255, 0.1f );
	}
}


//-----------------------------------------------------------------------------
// Holding effects
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoEffectHolding( )
{
	float flScaleFactor = SpriteScaleFactor();

	// Turn off the center sprite
	if ( m_hCenterSprite != NULL )
	{
		m_hCenterSprite->SetBrightness( 255, 0.1f );
		m_hCenterSprite->SetScale( 0.2f, 0.2f );
		m_hCenterSprite->TurnOn();
	}

	// Turn off the end-caps
	for ( int i = 0; i < 2; i++ )
	{
		if ( m_hEndSprites[i] != NULL )
		{
			m_hEndSprites[i]->TurnOn();
		}
	}

	// Turn off the lightning
	for ( int i = 0; i < NUM_BEAMS; i++ )
	{
		if ( m_hBeams[i] != NULL )
		{
			m_hBeams[i]->SetBrightness( 128 );
		}
	}

	// Turn on the glow sprites
	for ( int i = 0; i < NUM_SPRITES; i++ )
	{
		if ( m_hGlowSprites[i] != NULL )
		{
			m_hGlowSprites[i]->TurnOn();
			m_hGlowSprites[i]->SetBrightness( 64.0f, 0.2f );
			m_hGlowSprites[i]->SetScale( 0.5f * flScaleFactor, 0.2f );
		}
	}

	// Prepare for scale up
	if ( m_hBlastSprite != NULL )
	{
		m_hBlastSprite->TurnOff();
		m_hBlastSprite->SetScale( 0.1f, 0.0f );
		m_hBlastSprite->SetBrightness( 0, 0.0f );
	}
}


//-----------------------------------------------------------------------------
// Launch effects
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoEffectLaunch( Vector *pos )
{
	return;


	Assert( pos );
	if ( pos == NULL )
		return;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	Vector	endpos = *pos;

	// Check to store off our view model index
	CBeam *pBeam = CBeam::BeamCreate( IsMegaWeaponizer() ? MEGACANNON_BEAM_SPRITE : PHYSCANNON_BEAM_SPRITE, 8 );

	if ( pBeam != NULL )
	{
		pBeam->PointEntInit( endpos, this );
		pBeam->SetEndAttachment( 1 );
		pBeam->SetWidth( 6.4 );
		pBeam->SetEndWidth( 12.8 );
		pBeam->SetBrightness( 255 );
		pBeam->SetColor( 255, 255, 255 );
		pBeam->LiveForTime( 0.1f );
		pBeam->RelinkBeam();
		pBeam->SetNoise( 2 );
	}

	Vector	shotDir = ( endpos - pOwner->Weapon_ShootPosition() );
	VectorNormalize( shotDir );

	//End hit
	//FIXME: Probably too big
	CPVSFilter filter( endpos );
	te->GaussExplosion( filter, 0.0f, endpos - ( shotDir * 4.0f ), RandomVector(-1.0f, 1.0f), 0 );
	
	if ( m_hBlastSprite != NULL )
	{
		m_hBlastSprite->TurnOn();
		m_hBlastSprite->SetScale( 2.0f, 0.1f );
		m_hBlastSprite->SetBrightness( 0.0f, 0.1f );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pos - 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoMegaEffectLaunch( Vector *pos )
{

	return;

	Assert( pos );
	if ( pos == NULL )
		return;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner == NULL )
		return;

	Vector	endpos = *pos;

	// Check to store off our view model index
	CBaseViewModel *vm = pOwner->GetViewModel();
	
	int numBeams = random->RandomInt( 1, 2 );

	CBeam *pBeam = CBeam::BeamCreate( IsMegaWeaponizer() ? MEGACANNON_BEAM_SPRITE : PHYSCANNON_BEAM_SPRITE, 0.8 );

	if ( pBeam != NULL )
	{
		pBeam->PointEntInit( endpos, vm );
		pBeam->SetEndAttachment( 1 );
		pBeam->SetWidth( 2 );
		pBeam->SetEndWidth( 12 );
		pBeam->SetBrightness( 255 );
		pBeam->SetColor( 255, 255, 255 );
		pBeam->LiveForTime( 0.1f );
		pBeam->RelinkBeam();
		pBeam->SetNoise( 0 );
	}

	for ( int i = 0; i < numBeams; i++ )
	{
		pBeam = CBeam::BeamCreate( IsMegaWeaponizer() ? MEGACANNON_BEAM_SPRITE : PHYSCANNON_BEAM_SPRITE, 0.8 );

		if ( pBeam != NULL )
		{
			pBeam->PointEntInit( endpos, vm );
			pBeam->SetEndAttachment( 1 );
			pBeam->SetWidth( 2 );
			pBeam->SetEndWidth( random->RandomInt( 1, 2 ) );
			pBeam->SetBrightness( 255 );
			pBeam->SetColor( 255, 255, 255 );
			pBeam->LiveForTime( 0.1f );
			pBeam->RelinkBeam();
			pBeam->SetNoise( random->RandomInt( 8, 12 ) );
		}
	}
	
	Vector	shotDir = ( endpos - pOwner->Weapon_ShootPosition() );
	VectorNormalize( shotDir );

	//End hit
	//FIXME: Probably too big
	CPVSFilter filter( endpos );
	te->GaussExplosion( filter, 0.0f, endpos - ( shotDir * 4.0f ), RandomVector(-1.0f, 1.0f), 0 );
}

//-----------------------------------------------------------------------------
// Holding effects
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoMegaEffectHolding( void )
{
	float flScaleFactor = SpriteScaleFactor();

	// Turn off the center sprite
	if ( m_hCenterSprite != NULL )
	{
		m_hCenterSprite->SetBrightness( 255, 0.1f );
		m_hCenterSprite->SetScale( 0.2f, 0.2f );
		m_hCenterSprite->TurnOn();
	}

	// Turn off the end-caps
	for ( int i = 0; i < 2; i++ )
	{
		if ( m_hEndSprites[i] != NULL )
		{
			m_hEndSprites[i]->TurnOn();
		}
	}

	// Turn off the lightning
	for ( int i = 0; i < NUM_BEAMS; i++ )
	{
		if ( m_hBeams[i] != NULL )
		{
			m_hBeams[i]->SetBrightness( 128 );
		}
	}

	// Turn on the glow sprites
	for ( int i = 0; i < NUM_SPRITES; i++ )
	{
		if ( m_hGlowSprites[i] != NULL )
		{
			m_hGlowSprites[i]->TurnOn();
			m_hGlowSprites[i]->SetBrightness( 32.0f, 0.2f );
			m_hGlowSprites[i]->SetScale( 0.25f * flScaleFactor, 0.2f );
		}
	}
}

//-----------------------------------------------------------------------------
// Ready effects
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoMegaEffectReady( void )
{
	float flScaleFactor = SpriteScaleFactor();

	//Turn on the center sprite
	if ( m_hCenterSprite != NULL )
	{
		m_hCenterSprite->SetBrightness( 128, 0.2f );
		m_hCenterSprite->SetScale( 0.15f, 0.2f );
		m_hCenterSprite->TurnOn();
	}

	//Turn off the end-caps
	for ( int i = 0; i < 2; i++ )
	{
		if ( m_hEndSprites[i] != NULL )
		{
			if ( m_flEndSpritesOverride[i] < gpGlobals->curtime )
			{
				m_hEndSprites[i]->TurnOff();
			}
		}
	}

	//Turn off the lightning
	for ( int i = 0; i < NUM_BEAMS; i++ )
	{
		if ( m_hBeams[i] != NULL )
		{
			m_hBeams[i]->SetBrightness( 0 );
		}
	}

	//Turn on the glow sprites
	for ( int i = 0; i < NUM_SPRITES; i++ )
	{
		if ( m_hGlowSprites[i] != NULL )
		{
			m_hGlowSprites[i]->TurnOn();
			m_hGlowSprites[i]->SetBrightness( 24.0f, 0.2f );
			m_hGlowSprites[i]->SetScale( 0.2f * flScaleFactor, 0.2f );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Shutdown for the weapon when it's holstered
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoEffectNone( void )
{
	if ( m_hBlastSprite != NULL )
	{
		// Become small
		m_hBlastSprite->SetScale( 0.001f );
	}	
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : effectType - 
//			*pos - 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoMegaEffect( int effectType, Vector *pos )
{
	switch( effectType )
	{
	case EFFECT_CLOSED:
		DoMegaEffectClosed();
		break;

	case EFFECT_READY:
		DoMegaEffectReady();
		break;

	case EFFECT_HOLDING:
		DoMegaEffectHolding();
		break;

	case EFFECT_LAUNCH:
		DoMegaEffectLaunch( pos );
		break;

	default:
	case EFFECT_NONE:
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : effectType - 
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::DoEffect( int effectType, Vector *pos )
{
	// Make sure we're active
	StartEffects();

	m_EffectState = effectType;

	// Do different effects when upgraded
	if ( IsMegaWeaponizer() )
	{
		DoMegaEffect( effectType, pos );
		return;
	}

	switch( effectType )
	{
	case EFFECT_CLOSED:
		DoEffectClosed( );
		break;

	case EFFECT_READY:
		DoEffectReady( );
		break;

	case EFFECT_HOLDING:
		DoEffectHolding();
		break;

	case EFFECT_LAUNCH:
		DoEffectLaunch( pos );
		break;

	default:
	case EFFECT_NONE:
		DoEffectNone();
		break;
	}
}

//-----------------------------------------------------------------------------
// Override weapon sounds here
//-----------------------------------------------------------------------------
const char *CWeaponWeaponizer::GetShootSound( int iIndex ) const
{
	return BaseClass::GetShootSound( iIndex );
}

//-----------------------------------------------------------------------------
// Purpose: Adds the specified object to the list of objects that have been
//			propelled by this physgun, along with a timestamp of when the 
//			object was added to the list. This list is checked when a physics
//			object strikes another entity, to resolve whether the player is 
//			accountable for the impact.
//
// Input  : pObject - pointer to the object being thrown by the physcannon.
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::RecordThrownObject( CBaseEntity *pObject )
{
	weaponizer_thrown_objects_t thrown;
	thrown.hEntity = pObject;
	thrown.fTimeThrown = gpGlobals->curtime;

	// Get rid of stale and dead objects in the list.
	PurgeThrownObjects();

	// See if this object is already in the list.
	int count = m_ThrownEntities.Count();

	for( int i = 0 ; i < count ; i++ )
	{
		if( m_ThrownEntities[i].hEntity == pObject )
		{
			// Just update the time.
			//Msg("++UPDATING: %s (%d)\n", m_ThrownEntities[i].hEntity->GetClassname(), m_ThrownEntities[i].hEntity->entindex() );
			m_ThrownEntities[i] = thrown;
			return;
		}
	}

	//Msg("++ADDING: %s (%d)\n", pObject->GetClassname(), pObject->entindex() );

	m_ThrownEntities.AddToTail(thrown);
}

//-----------------------------------------------------------------------------
// Purpose: Go through the objects in the thrown objects list and discard any
//			objects that have gone 'stale'. (Were thrown several seconds ago), or
//			have been destroyed or removed.
//
//-----------------------------------------------------------------------------
#define PHYSCANNON_THROWN_LIST_TIMEOUT	10.0f
void CWeaponWeaponizer::PurgeThrownObjects()
{
	bool bListChanged;

	// This is bubble-sorty, but the list is also very short.
	do
	{
		bListChanged = false;

		int count = m_ThrownEntities.Count();
		for( int i = 0 ; i < count ; i++ )
		{
			bool bRemove = false;

			if( !m_ThrownEntities[i].hEntity.Get() )
			{
				bRemove = true;
			}
			else if( gpGlobals->curtime > (m_ThrownEntities[i].fTimeThrown + PHYSCANNON_THROWN_LIST_TIMEOUT) )
			{
				bRemove = true;
			}
			else
			{
				IPhysicsObject *pObject = m_ThrownEntities[i].hEntity->VPhysicsGetObject();

				if( pObject && pObject->IsAsleep() )
				{
					bRemove = true;
				}
			}

			if( bRemove )
			{
				//Msg("--REMOVING: %s (%d)\n", m_ThrownEntities[i].hEntity->GetClassname(), m_ThrownEntities[i].hEntity->entindex() );
				m_ThrownEntities.Remove(i);
				bListChanged = true;
				break;
			}
		}
	} while( bListChanged );
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CWeaponWeaponizer::IsAccountableForObject( CBaseEntity *pObject )
{
	// Clean out the stale and dead items.
	PurgeThrownObjects();

	// Now if this object is in the list, the player is accountable for it striking something.
	int count = m_ThrownEntities.Count();

	for( int i = 0 ; i < count ; i++ )
	{
		if( m_ThrownEntities[i].hEntity == pObject )
		{
			return true;
		}
	}

	return false;
}


//-----------------------------------------------------------------------------
// Purpose: Trace a line the special weaponizer way!
//-----------------------------------------------------------------------------
void CWeaponWeaponizer::UTIL_WeaponizerTraceLine( const Vector &vecAbsStart, const Vector &vecAbsEnd, CBaseEntity *pTraceOwner, CBaseCombatWeapon *pPhyscannon, trace_t *pTrace )
{
	// Default to HL2 vanilla
	if ( hl2_episodic.GetBool() == false )
	{
		CTraceFilterNoOwnerTest filter( pTraceOwner, COLLISION_GROUP_NONE );
		UTIL_TraceLine( vecAbsStart, vecAbsEnd, (MASK_SHOT|CONTENTS_GRATE), &filter, pTrace );
		return;
	}

	// First, trace against entities
	CTraceFilterWeaponizer filter( pTraceOwner, COLLISION_GROUP_NONE );
	UTIL_TraceLine( vecAbsStart, vecAbsEnd, (MASK_SHOT|CONTENTS_GRATE), &filter, pTrace );

	// If we've hit something, test again to make sure no brushes block us
	if ( pTrace->m_pEnt != NULL )
	{
		trace_t testTrace;
		CTraceFilterOnlyBrushes brushFilter( COLLISION_GROUP_NONE );
		UTIL_TraceLine( pTrace->startpos, pTrace->endpos, MASK_SHOT, &brushFilter, &testTrace );

		// If we hit a brush, replace the trace with that result
		if ( testTrace.fraction < 1.0f || testTrace.startsolid || testTrace.allsolid )
		{
			*pTrace = testTrace;
		}
		else
		{
			CUtlVector<CBaseEntity *> childrenList;
			GetAllChildren( pTrace->m_pEnt, childrenList );
			if ( childrenList.Count() )
			{
				for ( int i = childrenList.Count()-1; i >= 0; --i )
				{
					if ( FClassnameIs( childrenList[i], "hunter_flechette" ) == true )
					{
						pTrace->m_pEnt = childrenList[i];
						break;
					}
				}
			}
		}
	}

	CBaseEntity *list[128];
	Ray_t ray;

	ray.Init( vecAbsStart, pTrace->endpos );

	int nFoundCt = UTIL_EntitiesAlongRay( list, 128, ray, 0 );

	for( int i = 0; i < nFoundCt; i++ )
	{
		CBaseEntity *pEntityFound = list[i];
		WeaponizerMaterialTypes_t eType = GetWeaponizerMaterialType( pEntityFound );

		if ( ( pEntityFound->VPhysicsGetObject() == NULL ) && ( eType != WEAPONIZER_MATERIAL_TYPE_NONE ) )
		{
			if ( eType == WEAPONIZER_MATERIAL_TYPE_ELECTRICAL )
			{
				CFuncElectricity *pElectrical =	dynamic_cast< CFuncElectricity*>( pEntityFound );

				if ( pElectrical && pElectrical->IsEmitter() == false )
					continue;
			}

			float flLength = (pEntityFound->WorldSpaceCenter() - vecAbsStart).Length();

			if ( flLength > weaponizer_tracelength.GetFloat() * 2 )
				continue;

			Vector vecAbsMins, vecAbsMaxs;
			pEntityFound->CollisionProp()->WorldSpaceAABB( &vecAbsMins, &vecAbsMaxs );

			trace_t tr;
			if ( IntersectRayWithBox( ray, vecAbsMins, vecAbsMaxs, DIST_EPSILON, &tr ) )
			{
				pTrace->endpos = tr.endpos;
				pTrace->m_pEnt = pEntityFound;
				return;
			}
		}
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
WeaponizerMaterialTypes_t CWeaponWeaponizer::GetWeaponizerMaterialType( CBaseEntity *pEntity )
{
	

	//-----------------------------------------------------------------------------
	// CBaseAnimating
	//{
	//	if ( IsOnFire() )
	//		return WEAPONIZER_MATERIAL_TYPE_FIRE;
	//
	//	return BaseClass;
	//}

	//-----------------------------------------------------------------------------
	// CBaseProp
	//{
	//	if ( !GetModelPtr() )
	//		return WEAPONIZER_MATERIAL_TYPE_CONCRETE;

	//	return WEAPONIZER_MATERIAL_TYPE_CONCRETE;
	//}

	//-----------------------------------------------------------------------------
	// CRagdollProp
	//{
	//	if ( !GetModelPtr() )
	//		return WEAPONIZER_MATERIAL_TYPE_CONCRETE;
	//
	//	int surfaceProp = physprops->GetSurfaceIndex( GetModelPtr()->pszSurfaceProp() );
	//	surfacedata_t *psurf = physprops->GetSurfaceData( surfaceProp );
	//
	//	if ( !psurf )
	//		return WEAPONIZER_MATERIAL_TYPE_NONE;
	//
		//#define CHAR_TEX_CONCRETE	'C'
		//#define CHAR_TEX_METAL		'M'
		//#define CHAR_TEX_DIRT		'D'
		//#define CHAR_TEX_VENT		'V'
		//#define CHAR_TEX_GRATE		'G'
		//#define CHAR_TEX_TILE		'T'
		//#define CHAR_TEX_SLOSH		'S'
		//#define CHAR_TEX_WOOD		'W'
		//#define CHAR_TEX_COMPUTER		'P'
		//#define CHAR_TEX_GLASS		'Y'
		//#define CHAR_TEX_FLESH		'F'
		//#define CHAR_TEX_BLOODYFLESH	'B'
		//#define CHAR_TEX_CLIP		'I'
		//#define CHAR_TEX_ANTLION		'A'
		//#define CHAR_TEX_ALIENFLESH	'H'
		//#define CHAR_TEX_FOLIAGE		'O'
		//#define CHAR_TEX_SAND		'N'
		//#define CHAR_TEX_PLASTIC		'L'
		//#define CHAR_TEX_WARPSHIELD	'Z' ///< wierd-looking jello effect for advisor shield.
		//#define CHAR_TEX_EGGSHELL		'E'	
	//
	//	if ( ( psurf->game.material == 'F' ) ||
	//		( psurf->game.material == 'B' ) ||
	//		( psurf->game.material == 'A' ) ||
	//		( psurf->game.material == 'H' ) ||
	//		( psurf->game.material == 'O' ) )
	//		return WEAPONIZER_MATERIAL_TYPE_NONE;
	//
	//	return WEAPONIZER_MATERIAL_TYPE_CONCRETE;
	//}

	//-----------------------------------------------------------------------------
	// CFire
	//{ 
	//	if ( IsBurning() == true )
	//		return WEAPONIZER_MATERIAL_TYPE_FIRE; 
	//
	//	return WEAPONIZER_MATERIAL_TYPE_NONE;
	//}

	//-----------------------------------------------------------------------------
	// CHunterFlechette
	//virtual int	GetWeaponizerMaterialType( void ) { return WEAPONIZER_MATERIAL_TYPE_CONCRETE; }

	//-----------------------------------------------------------------------------
	// CFuncElectricity
	if ( FClassnameIs( pEntity, "func_electricity" ) )
	{
		CFuncElectricity *pFunc = dynamic_cast<CFuncElectricity *>( pEntity );
		if ( pFunc->IsEmitter() )
			return WEAPONIZER_MATERIAL_TYPE_ELECTRICAL;

		return WEAPONIZER_MATERIAL_TYPE_NONE;
	}

	//-----------------------------------------------------------------------------
	// CFuncWeaponizerFuel
	//virtual int GetWeaponizerMaterialType( void ) { return WEAPONIZER_MATERIAL_TYPE_EXPLOSIVE; }

	//-----------------------------------------------------------------------------
	// CFuncWeaponizerFire
	//{
	//	if ( m_bDisabled == false )
	//		return WEAPONIZER_MATERIAL_TYPE_FIRE;
	//
	//	return WEAPONIZER_MATERIAL_TYPE_NONE;
	//}

	return (WeaponizerMaterialTypes_t)pEntity->GetWeaponizerMaterialType();

	return WEAPONIZER_MATERIAL_TYPE_NONE;
}
