//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: The client-side version of the gas pump
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "C_BaseAnimating.h"
#include "hl2_shareddefs.h"
#include "C_BasePlayer.h"
	    
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//=============================================================================
//
// Gas Pump Class
//
class C_GasPump : public C_BaseAnimating
{
	DECLARE_CLASS( C_GasPump, C_BaseAnimating );

public:

	DECLARE_CLIENTCLASS();

	C_GasPump( void );

	virtual void	UpdateOnRemove( void );
	virtual void	OnPreDataChanged( DataUpdateType_t updateType );
	virtual void	OnDataChanged( DataUpdateType_t updateType );

	// Check if the local player is attached
	bool IsLocalPlayerAttached( void );

private:
	typedef CHandle<C_BasePlayer> CPlayerHandle;
	CPlayerHandle	m_hPlayer;
	CPlayerHandle	m_hOldPlayer;

private:
	C_GasPump( const C_GasPump & ); // not defined, not accessible
};


IMPLEMENT_CLIENTCLASS_DT( C_GasPump, DT_GasPump, CGasPump )
	RecvPropEHandle( RECVINFO( m_hPlayer ) ),
END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_GasPump::C_GasPump( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Check if the local player is attached
//-----------------------------------------------------------------------------
bool C_GasPump::IsLocalPlayerAttached( void )
{
	C_BasePlayer *pLocalPlayer = C_BasePlayer::GetLocalPlayer();
	return ( m_hPlayer.Get() == pLocalPlayer );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_GasPump::UpdateOnRemove( void )
{
	if ( m_hPlayer.Get() )
	{
		// Stop the startup, in case it's still going
		StopSound( m_hPlayer->entindex(), "GasPump.Startup" );

		// Start the shutdown sound
		CPASAttenuationFilter filter( m_hPlayer, "GasPump.Shutdown" );
		EmitSound( filter, m_hPlayer->entindex(), "GasPump.Shutdown" );
	}

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_GasPump::OnPreDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnPreDataChanged( updateType );
	m_hOldPlayer = m_hPlayer;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_GasPump::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	// Did a player connect / disconnect?
	if ( m_hOldPlayer != m_hPlayer )
	{
		// Disconnected?
		if ( m_hOldPlayer )
		{
			// Stop the startup, in case it's still going
			StopSound( m_hOldPlayer->entindex(), "GasPump.Startup" );

			// Start the shutdown sound
			CPASAttenuationFilter filter( m_hOldPlayer, "GasPump.Shutdown" );
			EmitSound( filter, m_hOldPlayer->entindex(), "GasPump.Shutdown" );
		}

		if ( m_hPlayer )
		{
			// Start "buff" sound.
			CPASAttenuationFilter filter( m_hPlayer, "GasPump.Startup" );
			EmitSound( filter, m_hPlayer->entindex(), "GasPump.Startup" );
		}
	}
}
