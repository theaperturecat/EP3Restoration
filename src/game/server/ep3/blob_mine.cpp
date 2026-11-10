//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "blob_mine.h"
#include "physics_saverestore.h"
#include "vphysics/constraints.h"
#include "physics_collisionevent.h"
#include "physics_shared.h"
#include "hl2_shareddefs.h"
#include "saverestore_utlvector.h"
#include "particle_parse.h"
#include "props.h"
#include "grenade_spit.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

						 
#define BLOB_MINE_MASS 50.0f


LINK_ENTITY_TO_CLASS( blob_mine, CBlobMine );


//-----------------------------------------------------------------------------
// save/load
//-----------------------------------------------------------------------------
BEGIN_DATADESC( CBlobMine )

	DEFINE_FIELD( m_flDamageFactor, FIELD_FLOAT ),
	DEFINE_FIELD( m_bHeld, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_bTimerStarted, FIELD_BOOLEAN ),
	DEFINE_KEYFIELD( m_flRadius, FIELD_FLOAT, "radius" ),
	DEFINE_EMBEDDED( m_Controller ),
	DEFINE_PHYSPTR( m_pConstraint ),

	// Function Pointers
	DEFINE_ENTITYFUNC( AcidSplashTouch ),
	DEFINE_THINKFUNC( Detonate ),

	DEFINE_OUTPUT( m_OnPhysGunDrop, "OnPhysGunDrop" ),
	DEFINE_OUTPUT( m_OnPhysGunPickup, "OnPhysGunPickup" ),

END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CBlobMine, DT_BlobMine )
	SendPropBool( SENDINFO( m_bHeld ) ),
	SendPropFloat( SENDINFO( m_flRadius ), 0, SPROP_NOSCALE ),
END_SEND_TABLE()


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
static ConVar sk_dmg_blob_mine( "sk_dmg_blob_mine", "20" );
static ConVar sk_blob_mine_health( "sk_blob_mine_health", "5" );
static ConVar sk_blob_mine_detonation_time( "sk_blob_mine_detonation_time", "2" );
CBlobMine::CBlobMine( void )
{
	m_flRadius			= BLOB_MINE_RADIUS;
	m_iHealth			= sk_blob_mine_health.GetInt();
	m_iMaxHealth		= m_iHealth;
	m_bTimerStarted		= false;
	m_flDamageFactor	= 1.0f;
}

CBlobMine::~CBlobMine( void )
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
void CBlobMine::Precache( void )
{
	BaseClass::Precache();
	PrecacheModel( "models/roller.mdl" );
	PrecacheMaterial( "models/blob/blob_mine_material" );
	PrecacheScriptSound( "NPC_Antlion_Grub.Squish" );
	PrecacheScriptSound( "NPC_BlobTiny.Held" );
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
void CBlobMine::Spawn( void )
{
	Precache();

	SetModel( "models/roller.mdl" );

	SetSolid( SOLID_VPHYSICS );
	AddSolidFlags( FSOLID_FORCE_WORLD_ALIGNED | FSOLID_NOT_STANDABLE );

	BaseClass::Spawn();
	CreateVPhysics();
	SetTouch( &CBlobMine::AcidSplashTouch );

	SetCollisionGroup( HL2COLLISION_GROUP_BLOB_TINY );

	m_takedamage = DAMAGE_YES;
	m_pConstraint = NULL;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBlobMine::OnRestore()
{
	BaseClass::OnRestore();
	if ( m_pMotionController )
	{
		m_pMotionController->SetEventHandler( &m_Controller );
	}
}


unsigned int CBlobMine::PhysicsSolidMaskForEntity( void ) const
{
	return MASK_NPCFLUID;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CBlobMine::CreateVPhysics()
{
	RemoveSolidFlags( FSOLID_NOT_SOLID );

	objectparams_t params = g_PhysDefaultObjectParams;
	params.pGameData = static_cast<void *>(this);

	int nMaterialIndex = physprops->GetSurfaceIndex( "acid" );

	Vector vPos = GetAbsOrigin();
	IPhysicsObject* p = physenv->CreateSphereObject( m_flRadius, nMaterialIndex, vPos, GetAbsAngles(), &params, false );
	Assert(p != NULL);

	const Vector &vecVelocity = GetAbsVelocity();
	p->SetVelocity( &vecVelocity, NULL );
	PhysSetGameFlags( p, FVPHYSICS_NO_SELF_COLLISIONS | FVPHYSICS_NO_IMPACT_DMG ); // call collisionruleschanged if this changes dynamically

	p->SetMass( BLOB_MINE_MASS );
	p->EnableGravity( false );
	p->EnableDrag( false );

	VPhysicsSetObject( p );
					 
	// Gravity modification
	Vector vecGravity;
	physenv->GetGravity( &vecGravity );
	vecGravity *= GetGravity();
	m_Controller.m_vecLinear = vecGravity;
	m_pMotionController = physenv->CreateMotionController( &m_Controller );
	m_pMotionController->AttachObject( p, true );
	m_Controller.On();

	SetMoveType( MOVETYPE_VPHYSICS );
	return true;
}	


void CBlobMine::SetDamageFactor( float flFactor )
{
	m_flDamageFactor = flFactor;
}


void CBlobMine::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );
	EmitSound( "NPC_Antlion_Grub.Squish" );
	DispatchParticleEffect( "blob_tiny_death", GetAbsOrigin(), vec3_angle );
	UTIL_Remove( this );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBlobMine::AcidSplashTouch( CBaseEntity *pOther )
{
	if ( pOther->IsSolidFlagSet(FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS) )
		return;

	if ( m_bHeld )
		return;

	CBaseEntity *pOwner = GetOwnerEntity();
	if ( pOwner )
	{
		CBaseCombatCharacter *pOwnerChar = pOwner->MyCombatCharacterPointer();
		if ( pOwnerChar )
		{
			Disposition_t disp = pOwnerChar->IRelationType(pOther);

			// Ignore anyone that I'm friendly or neutral to.
			if( disp != D_HT && disp != D_FR)
				return;
		}
	}

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
	CTakeDamageInfo	info( this, this, sk_dmg_blob_mine.GetFloat() * m_flDamageFactor, 
		DMG_ACID | DMG_PREVENT_PHYSICS_FORCE );
	pOther->TakeDamage( info );

	// Pop!
	CTakeDamageInfo	dmgInfo( this, this, GetHealth(), DMG_CRUSH | DMG_PREVENT_PHYSICS_FORCE );
	TakeDamage( dmgInfo );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBlobMine::Attach( CBaseEntity *pOther )
{
	Detach();

	IPhysicsObject *pPhysics = VPhysicsGetObject();
	if ( !pPhysics )
		return;

	// Slam orientation to match impact normal
	Vector vecPosition;
	QAngle angles = GetAbsAngles();
	pPhysics->GetPosition( &vecPosition, NULL );
	pPhysics->SetPosition( vecPosition, angles, false );
	SetAbsAngles( angles );

	// Slam velocity to zero
	pPhysics->SetVelocity( &vec3_origin, NULL );

	IPhysicsObject *pOtherPhysics = pOther->VPhysicsGetObject();
	if ( !pOtherPhysics )
	{
		pOtherPhysics = g_PhysWorldObject;
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

	DispatchParticleEffect( "acid_sizzle", PATTACH_ABSORIGIN_FOLLOW, this );

	if ( !m_bTimerStarted )
	{
		m_bTimerStarted = true;
		SetContextThink( &CBlobMine::Detonate, gpGlobals->curtime + sk_blob_mine_detonation_time.GetFloat(), "DetonateContext" );
	}
}

void CBlobMine::Detach( )
{
	if ( m_pConstraint )
	{
		physenv->DestroyConstraint( m_pConstraint );
		m_pConstraint = NULL;
	}
}


static void BlobTiny_Attach( CBlobMine *pBlob, CBaseEntity *pOther )
{
	pBlob->Attach( pOther );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CBlobMine::VPhysicsCollision( int index, gamevcollisionevent_t *pEvent )
{
	// Only stick once
	if ( m_pConstraint )
		return;
									  
	// Explode if thrown by the phys cannon
	if ( pEvent->pObjects[index]->GetGameFlags() & FVPHYSICS_WAS_THROWN )
	{
		CTakeDamageInfo	dmgInfo( this, this, GetHealth(), DMG_CRUSH | DMG_PREVENT_PHYSICS_FORCE );
		TakeDamage( dmgInfo );
		BaseClass::VPhysicsCollision( index, pEvent );
		return;
	}

	// Don't stick to characters
	int otherIndex = !index;
	CBaseEntity *pOther = pEvent->pEntities[otherIndex];
	if ( pOther->MyCombatCharacterPointer() )
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

	g_PostSimulationQueue.QueueCall( BlobTiny_Attach, this, pOther );
	BaseClass::VPhysicsCollision( index, pEvent );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysGunUser - 
//-----------------------------------------------------------------------------
void CBlobMine::OnPhysGunPickup( CBasePlayer *pPhysGunUser, PhysGunPickup_t reason )
{
	Detach();

	// Are we just being punted?
	if ( reason == PUNTED_BY_CANNON )
		return;

	m_OnPhysGunPickup.FireOutput( pPhysGunUser, this );
	m_bHeld = true;
	EmitSound( "NPC_BlobTiny.Held" );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pPhysGunUser - 
//-----------------------------------------------------------------------------
void CBlobMine::OnPhysGunDrop( CBasePlayer *pPhysGunUser, PhysGunDrop_t Reason )
{
	m_bHeld = false;
	m_OnPhysGunDrop.FireOutput( pPhysGunUser, this );

	if ( Reason == LAUNCHED_BY_CANNON )
	{
		PhysSetGameFlags( VPhysicsGetObject(), FVPHYSICS_WAS_THROWN );
	}
}


//-----------------------------------------------------------------------------
// Detonates the blob
//-----------------------------------------------------------------------------
#define BLOB_SPIT_GRAVITY 300
#define BLOB_SPIT_VELOCITY 300
void CBlobMine::Detonate()
{
	// Pop!
	int nCount = RandomInt( 10, 14 );
	for ( int i = 0; i < nCount; i++ )
	{
		CGrenadeSpit *pGrenade = (CGrenadeSpit*) CreateEntityByName( "grenade_spit" );
		pGrenade->SetAbsOrigin( GetAbsOrigin() );
		pGrenade->SetAbsAngles( vec3_angle );
		DispatchSpawn( pGrenade );
		pGrenade->SetGravity( UTIL_ScaleForGravity( BLOB_SPIT_GRAVITY ) );
		CBaseEntity *pOwner = GetOwnerEntity();
		if ( pOwner )
		{
			pGrenade->SetThrower( pOwner->MyCombatCharacterPointer() );
			pGrenade->SetOwnerEntity( pOwner->MyCombatCharacterPointer() );
		}
		pGrenade->SpitDoesPoisonDamage( false );
		Vector vecDir = RandomVector( -1.0f, 1.0f );
		vecDir.AsVector2D().NormalizeInPlace();
		vecDir.z = 0.7f * fabs( vecDir.z ) + 0.3f;
		pGrenade->SetAbsVelocity( vecDir * BLOB_SPIT_VELOCITY );
		pGrenade->SetSpitSize( random->RandomInt( SPIT_SMALL, SPIT_LARGE ) );
		pGrenade->SetDamage( pGrenade->GetDamage() * m_flDamageFactor );

		// Tumble through the air
		pGrenade->SetLocalAngularVelocity(
			QAngle( random->RandomFloat( -250, -500 ),
			random->RandomFloat( -250, -500 ),
			random->RandomFloat( -250, -500 ) ) );
	}

	CTakeDamageInfo	dmgInfo( this, this, GetHealth(), DMG_CRUSH | DMG_PREVENT_PHYSICS_FORCE );
	TakeDamage( dmgInfo );
}