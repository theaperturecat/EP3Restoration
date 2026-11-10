//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "dust_cloud.h"
#include "dt_send.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define DUSTCLOUD_ENTITYNAME		"env_dustcloud"

//-----------------------------------------------------------------------------
//Data table
//-----------------------------------------------------------------------------
IMPLEMENT_SERVERCLASS_ST(DustCloud, DT_DustCloud)
	SendPropFloat(SENDINFO(m_SpawnRate), 8, 0, 1, 1024),
	SendPropVector(SENDINFO(m_Color), 8, 0, 0, 1),
	SendPropFloat(SENDINFO(m_ParticleLifetime), 16, SPROP_ROUNDUP, 0.1, 100),
	SendPropFloat(SENDINFO(m_StopEmitTime), 0, SPROP_NOSCALE),
	SendPropFloat(SENDINFO(m_MinSpeed), -1, SPROP_NOSCALE),
	SendPropFloat(SENDINFO(m_MaxSpeed), -1, SPROP_NOSCALE),
	SendPropFloat(SENDINFO(m_MinDirectedSpeed), -1, SPROP_NOSCALE),
	SendPropFloat(SENDINFO(m_MaxDirectedSpeed), -1, SPROP_NOSCALE),
	SendPropFloat(SENDINFO(m_StartSize), -1, SPROP_NOSCALE),
	SendPropFloat(SENDINFO(m_EndSize), -1, SPROP_NOSCALE),
	SendPropFloat(SENDINFO(m_SpawnRadius), -1, SPROP_NOSCALE),
	SendPropBool(SENDINFO(m_bEmit) ),
	SendPropFloat(SENDINFO(m_Opacity), -1, SPROP_NOSCALE),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( env_dustcloud, DustCloud);

BEGIN_DATADESC( DustCloud )

	DEFINE_FIELD( m_Color, FIELD_VECTOR ),
	DEFINE_KEYFIELD( m_Opacity, FIELD_FLOAT, "opacity" ),
	DEFINE_KEYFIELD( m_SpawnRate, FIELD_FLOAT, "spawnrate" ),
	DEFINE_KEYFIELD( m_ParticleLifetime, FIELD_FLOAT, "lifetime" ),
	DEFINE_FIELD( m_StopEmitTime, FIELD_TIME ),
	DEFINE_KEYFIELD( m_MinSpeed, FIELD_FLOAT, "minspeed" ),
	DEFINE_KEYFIELD( m_MaxSpeed, FIELD_FLOAT, "maxspeed" ),
	DEFINE_KEYFIELD( m_MinDirectedSpeed, FIELD_FLOAT, "mindirectedspeed" ),
	DEFINE_KEYFIELD( m_MaxDirectedSpeed, FIELD_FLOAT, "maxdirectedspeed" ),
	DEFINE_KEYFIELD( m_StartSize, FIELD_FLOAT, "startsize" ),
	DEFINE_KEYFIELD( m_EndSize, FIELD_FLOAT, "endsize" ),
	DEFINE_KEYFIELD( m_SpawnRadius, FIELD_FLOAT, "spawnradius" ),
	DEFINE_FIELD( m_bEmit, FIELD_BOOLEAN ),

END_DATADESC()


//-----------------------------------------------------------------------------
// Purpose: 
// Output : 
//-----------------------------------------------------------------------------
DustCloud::DustCloud()
{
	m_SpawnRate = 10;
	m_Color.GetForModify().Init(0.5, 0.5, 0.5);
	m_ParticleLifetime = 5;
	m_StopEmitTime = 0; // Don't stop emitting particles
	m_MinSpeed = 2;
	m_MaxSpeed = 4;
	m_MinDirectedSpeed = m_MaxDirectedSpeed = 0;
	m_StartSize = 35;
	m_EndSize = 55;
	m_SpawnRadius = 2;
	m_bEmit = true;
	m_Opacity = 0.5f;
}


//-----------------------------------------------------------------------------
// Parse data from a map file
//-----------------------------------------------------------------------------
bool DustCloud::KeyValue( const char *szKeyName, const char *szValue ) 
{
	if ( FStrEq( szKeyName, "color" ) )
	{
		color32 tmp;
		V_StringToColor32( &tmp, szValue );
		m_Color.GetForModify().Init( tmp.r / 255.0f, tmp.g / 255.0f, tmp.b / 255.0f );
		return true;
	}

	if ( FStrEq( szKeyName, "emittime" ) )
	{
		m_StopEmitTime = gpGlobals->curtime + atof( szValue );
		return true;
	}

	return BaseClass::KeyValue( szKeyName, szValue );
}


//-----------------------------------------------------------------------------
// Purpose :
// Input   :
// Output  :
//-----------------------------------------------------------------------------
void DustCloud::SetEmit(bool bVal)
{
	m_bEmit = bVal;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : DustCloud*
//-----------------------------------------------------------------------------
DustCloud* DustCloud::CreateDustCloud()
{
	CBaseEntity *pEnt = CreateEntityByName(DUSTCLOUD_ENTITYNAME);
	if(pEnt)
	{
		DustCloud *pDust = dynamic_cast<DustCloud*>(pEnt);
		if(pDust)
		{
			pDust->Activate();
			return pDust;
		}
		else
		{
			UTIL_Remove(pEnt);
		}
	}

	return NULL;
}
