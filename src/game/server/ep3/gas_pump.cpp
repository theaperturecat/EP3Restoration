//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Medic's portable power generator
//
//===========================================================================//

#include "cbase.h"
#include "gas_pump.h"
#include "player.h"
#include "rope.h"
#include "rope_shared.h"
#include "entitylist.h"
#include "engine/IEngineSound.h"


//=============================================================================
//
// Console Variables
//
static ConVar gas_pump_fuel_rate( "gas_pump_fuel_rate", "50" );
static ConVar gas_pump_range( "gas_pump_range", "300" );


//-----------------------------------------------------------------------------
// Buff Station defines
//-----------------------------------------------------------------------------
#define GAS_PUMP_MINS							Vector( -30, -30, 0  )
#define GAS_PUMP_MAXS							Vector(  30,  30, 50 )

#define GAS_PUMP_MODEL							"models/gaspump/gaspump001a.mdl"
#define GAS_PUMP_CABLE_MATERIAL					"cable/cable_lit.vmt"//"cable/new_cable_lit"
#define GAS_PUMP_FUEL_PLAYER_THINK_CONTEXT		"BoostPlayerThink"
#define GAS_PUMP_FUEL_PLAYER_THINK_INTERVAL	0.1f

#define GAS_PUMP_FUEL_RANGE						( 600 * 600 )

//=============================================================================
//
// Data Description
//
BEGIN_DATADESC( CGasPump )

	DEFINE_FIELD( m_hPlayer, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hRope, FIELD_EHANDLE ),
	DEFINE_FIELD( m_flFractionalFuelAmount, FIELD_FLOAT ),
//	DEFINE_FIELD( m_iAttachPoint, FIELD_INTEGER ),

	DEFINE_THINKFUNC( BoostPlayerThink ),

	DEFINE_INPUTFUNC( FIELD_VOID, "PlayerSpawned", InputPlayerSpawned ),
	DEFINE_INPUTFUNC( FIELD_VOID, "PlayerAttachedToGenerator", InputPlayerAttachedToGenerator ),
	DEFINE_INPUTFUNC( FIELD_VOID, "PlayerEnteredVehicle", InputPlayerSpawned ),	// NJS: Detach player from buff pack.
END_DATADESC()


//=============================================================================
//
// Server Class
//
IMPLEMENT_SERVERCLASS_ST( CGasPump, DT_GasPump )
	SendPropEHandle( SENDINFO( m_hPlayer ) )
END_SEND_TABLE()

//=============================================================================
//
// Linking and Precache
//
LINK_ENTITY_TO_CLASS( gas_pump, CGasPump );


//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CGasPump::CGasPump()
{
	// Uses the client-side animation system.
	UseClientSideAnimation();
}

//-----------------------------------------------------------------------------
// Purpose: Spawn
//-----------------------------------------------------------------------------
void CGasPump::Spawn()
{
	Precache();
	BaseClass::Spawn();

	SetModel( GAS_PUMP_MODEL );
	SetSolid( SOLID_BBOX );

	UTIL_SetSize( this, GAS_PUMP_MINS, GAS_PUMP_MAXS );

	m_takedamage = DAMAGE_NO;
	m_flFractionalFuelAmount = 0.0f;

	// Thinking
	SetContextThink( &CGasPump::BoostPlayerThink, 1.0f, GAS_PUMP_FUEL_PLAYER_THINK_CONTEXT );
}

//-----------------------------------------------------------------------------
// Purpose: Precache model, vgui elements, and sound.
//-----------------------------------------------------------------------------
void CGasPump::Precache()
{
	BaseClass::Precache();

	PrecacheModel( GAS_PUMP_MODEL );
	PrecacheModel( GAS_PUMP_CABLE_MATERIAL );
	PrecacheScriptSound( "GasPump.Startup" );
	PrecacheScriptSound( "GasPump.Shutdown" );
}

void CGasPump::Activate()
{
	BaseClass::Activate();

	// Initialize gas pump attachment data.
	InitAttachmentData();
}


//-----------------------------------------------------------------------------
// Purpose: Remove this object from it's team and mark it for deletion.
//-----------------------------------------------------------------------------
void CGasPump::UpdateOnRemove( void )
{
	DetachPlayer( );

	// We shouldn't get any more messages
	g_pNotify->ClearEntity( this );
	BaseClass::UpdateOnRemove();
}


//-----------------------------------------------------------------------------
// Purpose: Attach to players who touch me
//-----------------------------------------------------------------------------
void CGasPump::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if ( useType == USE_TOGGLE )
	{
		// See if the activator is a player
		if ( !pActivator || !pActivator->IsPlayer() )
			return;

		CBasePlayer *pPlayer = static_cast<CBasePlayer*>(pActivator);

		// Always switch to the flamethrower
		CBaseCombatWeapon *pFlameThrower = pPlayer->Weapon_OwnsThisType( "weapon_flamethrower" );
		if ( pFlameThrower )
		{
			// Giving ammo makes it so we can switch to the flame thrower
			pPlayer->GiveAmmo( 1, "Fuel", true );
			pPlayer->Weapon_Switch( pFlameThrower );
			UpdatePlayerAttachment( pPlayer );
		}
	}

	BaseClass::Use( pActivator, pCaller, useType, value );
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGasPump::InitAttachmentData( void )
{
	m_iAttachPoint = LookupAttachment( "playercable" );
}


//-----------------------------------------------------------------------------
// Purpose: Create "Buff Station" cable (rope).
//-----------------------------------------------------------------------------
CRopeKeyframe *CGasPump::CreateRope( CBasePlayer *pPlayer, int iAttachPoint )
{
	CRopeKeyframe *pRope = CRopeKeyframe::Create( this, pPlayer, iAttachPoint, 0, 2, GAS_PUMP_CABLE_MATERIAL );
	if ( pRope )
	{
		pRope->m_RopeLength = gas_pump_range.GetFloat();
		pRope->m_Slack = 0.0f;
		pRope->m_nSegments = ROPE_MAX_SEGMENTS;
		pRope->m_RopeFlags |= ROPE_COLLIDE;
		pRope->EnablePlayerWeaponAttach( true );
		pRope->ActivateStartDirectionConstraints( true );
	}

	return pRope;
}


//-----------------------------------------------------------------------------
// Purpose: Is a particular player attached?
//-----------------------------------------------------------------------------
bool CGasPump::IsPlayerAttached( CBasePlayer *pPlayer )
{
	return ( m_hPlayer.Get() == pPlayer );
}


//-----------------------------------------------------------------------------
// Purpose: Attach the player to the "Buff Station."
//-----------------------------------------------------------------------------
void CGasPump::AttachPlayer( CBasePlayer *pPlayer )
{
	// Player shouldn't already be attached.
	Assert( !IsPlayerAttached( pPlayer ) );

	// Check to see if the player is alive and on the correct team.
	if ( !pPlayer->IsAlive() )
		return;

	// This will disconnect the player from other Buff Stations, and keep track of important player events.
	g_pNotify->ReportNamedEvent( pPlayer, "PlayerAttachedToGenerator" );
	g_pNotify->AddEntity( this, pPlayer );

	// Connect player.
	m_hPlayer.Set( pPlayer );
	m_hRope = CreateRope( pPlayer, m_iAttachPoint );

	// Tell the player to constrain his movement.
	pPlayer->ActivateMovementConstraint( this, GetAbsOrigin(), gas_pump_range.GetFloat(), 75.0f, 0.15f );

	// Update think.
	SetNextThink( gpGlobals->curtime + GAS_PUMP_FUEL_PLAYER_THINK_INTERVAL, GAS_PUMP_FUEL_PLAYER_THINK_CONTEXT );
}


//-----------------------------------------------------------------------------
// Purpose: Detach the player from the "Buff Station."
//-----------------------------------------------------------------------------
void CGasPump::DetachPlayer( )
{
	// Get the player.
	CBasePlayer *pPlayer = m_hPlayer.Get();
	if ( !pPlayer )
		return;

	// Remove the rope (cable).
	if ( m_hRope.Get() )
	{
		m_hRope->DetachPoint( 1 );
		m_hRope->DieAtNextRest();
	}

	// Unconstrain the player movement.
	pPlayer->DeactivateMovementConstraint();

	// Keep track of player events.
	g_pNotify->RemoveEntity( this, pPlayer );

	m_hPlayer.Set( NULL );
}

																		  
//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CGasPump::UpdatePlayerAttachment( CBasePlayer *pPlayer )
{
	// Valid player?
	if ( !pPlayer )
		return;

	// Attach/Detach (toggle).
	if ( IsPlayerAttached( pPlayer ) )
	{
		DetachPlayer( );
	}
	else
	{
		AttachPlayer( pPlayer );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Input handler
//-----------------------------------------------------------------------------
void CGasPump::InputPlayerSpawned( inputdata_t &inputdata )
{
	if ( inputdata.pActivator->IsPlayer() )
	{
		CBasePlayer *pPlayer = static_cast<CBasePlayer*>( inputdata.pActivator );
		if ( IsPlayerAttached( pPlayer ) )
		{
			DetachPlayer( );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Input handler
//-----------------------------------------------------------------------------
void CGasPump::InputPlayerAttachedToGenerator( inputdata_t &inputdata )
{
	if ( inputdata.pActivator->IsPlayer() )
	{
		CBasePlayer *pPlayer = static_cast<CBasePlayer*>( inputdata.pActivator );
		if ( IsPlayerAttached( pPlayer ) )
		{
			DetachPlayer( );
		}
	}
}

//-----------------------------------------------------------------------------
// Boost those attached to me as long as I'm not EMPed
//-----------------------------------------------------------------------------
void CGasPump::BoostPlayerThink( void )
{
	// Get range (squared = faster test).
	float flMaxRangeSq = gas_pump_range.GetFloat();
	flMaxRangeSq *= flMaxRangeSq;

	// Boost all attached players and objects.

	// Clean up dangling pointers + dead players, subversion, disconnection
	CBasePlayer *pPlayer = m_hPlayer.Get();
	if ( !pPlayer || !pPlayer->IsAlive() )
	{
		DetachPlayer( );
		return;
	}

	CBaseCombatWeapon* pActiveWeapon = pPlayer->GetActiveWeapon();
	if ( !FClassnameIs( pActiveWeapon, "weapon_flamethrower" ) )
	{
		DetachPlayer();
		return;
	}

	// Check for out of range.
	float flDistSq = GetAbsOrigin().DistToSqr( pPlayer->GetAbsOrigin() ); 
	if ( flDistSq > flMaxRangeSq )
	{
		DetachPlayer( );
		return;
	}

	float flFuelAmount = gas_pump_fuel_rate.GetFloat() * GAS_PUMP_FUEL_PLAYER_THINK_INTERVAL;
	m_flFractionalFuelAmount += flFuelAmount;
	int nFuelAmount = (int)m_flFractionalFuelAmount;
	m_flFractionalFuelAmount -= nFuelAmount;
	if ( nFuelAmount > 0 )
	{
		pPlayer->GiveAmmo( flFuelAmount, "Fuel", true );
	}

	// Set next think time.
	SetNextThink( gpGlobals->curtime + GAS_PUMP_FUEL_PLAYER_THINK_INTERVAL, 
		          GAS_PUMP_FUEL_PLAYER_THINK_CONTEXT );
}
