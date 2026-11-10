//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "blob_regenerator.h"
#include "particle_parse.h"
#include "npc_blob.h"
#include "bone_setup.h"
					  
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#define BLOB_GROWTH_TIME					5.0f
#define BLOB_INITIAL_HEALTH					25.0f
#define BLOB_REGENERATOR_MINS				Vector( -30, -30, 0  )
#define BLOB_REGENERATOR_MAXS				Vector(  30,  30, 58 )
#define BLOB_REGENERATOR_INACTIVE_TIME		2.0f


LINK_ENTITY_TO_CLASS( blob_regenerator, CBlobRegenerator );

					    
//-----------------------------------------------------------------------------
// save/load
//-----------------------------------------------------------------------------
BEGIN_DATADESC( CBlobRegenerator )

	DEFINE_FIELD( m_bIsInactive, FIELD_BOOLEAN ),

	// Function Pointers
	DEFINE_ENTITYFUNC( RegenerateTouch ),
	DEFINE_THINKFUNC( ReactivateThink ),
	DEFINE_THINKFUNC( GrowThink ),
	DEFINE_THINKFUNC( FinishGrowThink ),

END_DATADESC()


//-----------------------------------------------------------------------------
// globals
//-----------------------------------------------------------------------------
CUtlVector< CHandle< CBlobRegenerator > > CBlobRegenerator::s_BlobRegenerators;

	
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static ConVar sk_blob_regenerator_health( "sk_blob_regenerator_health", "100" );
CBlobRegenerator::CBlobRegenerator( void )
{
	m_iHealth			= sk_blob_regenerator_health.GetInt();
	m_iMaxHealth		= m_iHealth;
	m_bIsInactive		 = false;
}

CBlobRegenerator::~CBlobRegenerator( void )
{
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CBlobRegenerator::Precache( void )
{
	BaseClass::Precache();
	PrecacheModel( "models/props_hive/nest_sm_flat.mdl" );
	PrecacheScriptSound( "NPC_Antlion_Grub.Squish" );
	PrecacheParticleSystem( "antlion_gib_01_juice" );	
}


//-----------------------------------------------------------------------------
// Purpose:
// Input  :
// Output :
//-----------------------------------------------------------------------------
void CBlobRegenerator::Spawn( void )
{
	Precache();							  

	SetModel( "models/props_hive/nest_sm_flat.mdl" );
					    
	SetSolid( SOLID_BBOX );
	UTIL_SetSize( this, BLOB_REGENERATOR_MINS, BLOB_REGENERATOR_MAXS );
	BaseClass::Spawn();
	SetTouch( &CBlobRegenerator::RegenerateTouch );
	m_takedamage = DAMAGE_YES;
	m_clrRender.Init( 128, 255, 128 );
	CreateVPhysics();
}


bool CBlobRegenerator::CreateVPhysics( void )
{
	VPhysicsInitShadow( true, false );
	IPhysicsObject *pPhysObj = VPhysicsGetObject();
	if ( pPhysObj )
	{
		pPhysObj->SetMass( 1e10 );
	}
	return true;
}


//-----------------------------------------------------------------------------
// Activate, remove
//-----------------------------------------------------------------------------
void CBlobRegenerator::Activate()
{
	BaseClass::Activate();
	CHandle< CBlobRegenerator > hRegenerator = this;
	s_BlobRegenerators.AddToTail( hRegenerator );
}

void CBlobRegenerator::UpdateOnRemove()
{
	CHandle< CBlobRegenerator > hRegenerator = this;
	s_BlobRegenerators.FindAndRemove( hRegenerator );
	BaseClass::UpdateOnRemove();
}


//-----------------------------------------------------------------------------
// Global regenerator list
//-----------------------------------------------------------------------------
int CBlobRegenerator::GetBlobRegeneratorCount()
{
	return s_BlobRegenerators.Count();
}

CBlobRegenerator *CBlobRegenerator::GetBlobRegenerator( int i )
{
	return s_BlobRegenerators[i];
}



void CBlobRegenerator::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );
	EmitSound( "NPC_Antlion_Grub.Squish" );
	DispatchParticleEffect( "antlion_gib_01_juice", GetAbsOrigin(), vec3_angle );
	UTIL_Remove( this );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBlobRegenerator::MarkInactive()
{
	m_bIsInactive = true;
	SetContextThink( &CBlobRegenerator::ReactivateThink, gpGlobals->curtime + BLOB_REGENERATOR_INACTIVE_TIME, "ReactivateContext" );
}

bool CBlobRegenerator::IsInactive() const
{
	return m_bIsInactive;
}

bool CBlobRegenerator::IsBuilding() const
{
	return m_bIsBuilding;
}

void CBlobRegenerator::ReactivateThink()
{
	m_bIsInactive = false;
	SetContextThink( NULL, gpGlobals->curtime, "ReactivateContext" );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBlobRegenerator::BeginGrowing()
{							   
	Vector vecOrigin = GetAbsOrigin();
	float flHeight = CollisionProp()->OBBMaxs().z + 5.0f;
	vecOrigin.z -= flHeight;
	SetAbsOrigin( vecOrigin );
	SetMoveType( MOVETYPE_PUSH );
	SetMoveDoneTime( BLOB_GROWTH_TIME );
	SetAbsVelocity( Vector( 0.0f, 0.0f, flHeight / BLOB_GROWTH_TIME ) );
	SetHealth( BLOB_INITIAL_HEALTH );
	m_bIsInactive = true;
	SetContextThink( &CBlobRegenerator::GrowThink, gpGlobals->curtime + 0.1f, "GrowContext" );
	SetContextThink( &CBlobRegenerator::FinishGrowThink, gpGlobals->curtime + BLOB_GROWTH_TIME, "FinishGrowContext" );
	m_bIsBuilding = true;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBlobRegenerator::GrowThink()
{
	TakeHealth( ( sk_blob_regenerator_health.GetInt() - BLOB_INITIAL_HEALTH ) * 0.1f / BLOB_GROWTH_TIME, DMG_GENERIC );
	SetContextThink( &CBlobRegenerator::GrowThink, gpGlobals->curtime + 0.1f, "GrowContext" );
}

void CBlobRegenerator::FinishGrowThink()
{
	SetMoveType( MOVETYPE_NONE );
	m_bIsInactive = false;
	m_bIsBuilding = false;
	SetContextThink( NULL, gpGlobals->curtime, "GrowContext" );
	SetContextThink( NULL, gpGlobals->curtime, "FinishGrowContext" );
}

									 
//-----------------------------------------------------------------------------
// Blobs can't hurt this 
//-----------------------------------------------------------------------------
int CBlobRegenerator::OnTakeDamage( const CTakeDamageInfo &info )
{
	if ( info.GetAttacker()->Classify() == CLASS_BLOB )
		return 0;

	int nRetval = BaseClass::OnTakeDamage( info );
	return nRetval;
}
			    

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBlobRegenerator::RegenerateTouch( CBaseEntity *pOther )
{
	CNPC_Blob *pBlob = dynamic_cast< CNPC_Blob* >( pOther );
	if ( !pBlob || !pBlob->IsMostlyBrain() )
		return;

	pBlob->RegenerateParticles( 1.0f );
	MarkInactive();
}


void CBlobRegenerator::CreateFromBlob( const Vector &vecSpawnPoint )
{				  
	Vector vecEndPt = vecSpawnPoint;
	vecEndPt.z -= 128.0f;
	trace_t tr;
	UTIL_TraceLine( vecSpawnPoint, vecEndPt, MASK_SOLID_BRUSHONLY, NULL, COLLISION_GROUP_NONE, &tr );
	if ( tr.fraction == 1.0f || tr.startsolid )
		return;
						    
	CBlobRegenerator *pRegenerator = (CBlobRegenerator*)CreateEntityByName( "blob_regenerator" );
	pRegenerator->SetAbsOrigin( tr.endpos );
	DispatchSpawn( pRegenerator );
	pRegenerator->Activate();
	pRegenerator->BeginGrowing();
}
