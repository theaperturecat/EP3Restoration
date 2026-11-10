//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//
#include "cbase.h"
#include "c_smoke_trail.h"
#include "c_dust_cloud.h"
#include "fx.h"
#include "engine/IVDebugOverlay.h"
#include "engine/ienginesound.h"
#include "c_te_effect_dispatch.h"
#include "glow_overlay.h"
#include "fx_explosion.h"
#include "tier1/keyvalues.h"
#include "toolframework_client.h"
#include "view.h"
#include "precache_register.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Datatable.. this can have all the smokecloud parameters when we need it to.
IMPLEMENT_CLIENTCLASS_DT(C_DustCloud, DT_DustCloud, DustCloud)
	RecvPropFloat(RECVINFO(m_SpawnRate)),
	RecvPropVector(RECVINFO(m_Color)),
	RecvPropFloat(RECVINFO(m_ParticleLifetime)),
	RecvPropFloat(RECVINFO(m_StopEmitTime)),
	RecvPropFloat(RECVINFO(m_MinSpeed)),
	RecvPropFloat(RECVINFO(m_MaxSpeed)),
	RecvPropFloat(RECVINFO(m_MinDirectedSpeed)),
	RecvPropFloat(RECVINFO(m_MaxDirectedSpeed)),
	RecvPropFloat(RECVINFO(m_StartSize)),
	RecvPropFloat(RECVINFO(m_EndSize)),
	RecvPropFloat(RECVINFO(m_SpawnRadius)),
	RecvPropInt(RECVINFO(m_bEmit)),
	RecvPropFloat(RECVINFO(m_Opacity)),
END_RECV_TABLE()


// ------------------------------------------------------------------------- //
// ParticleMovieExplosion
// ------------------------------------------------------------------------- //
C_DustCloud::C_DustCloud()
{
	for (int i = 0; i < DUSTCLOUD_MATERIALS; i++)
	{
        m_MaterialHandle[i] = NULL;
	}

	m_SpawnRate = 10;
	m_ParticleSpawn.Init(10);
	m_Color.Init(0.5, 0.5, 0.5);
	m_ParticleLifetime = 5;
	m_StartEmitTime = gpGlobals->curtime;
	m_StopEmitTime = 0;	// No end time
	m_MinSpeed = 2;
	m_MaxSpeed = 4;
	m_MinDirectedSpeed = m_MaxDirectedSpeed = 0;
	m_StartSize = 35;
	m_EndSize = 55;
	m_SpawnRadius = 2;
	m_VelocityOffset.Init();
	m_Opacity = 0.5f;

	m_bEmit = true;

	m_pDustEmitter = NULL;
	m_pParticleMgr	= NULL;
}

C_DustCloud::~C_DustCloud()
{
	if ( ToolsEnabled() && clienttools->IsInRecordingMode() && m_pDustEmitter.IsValid() && m_pDustEmitter->GetToolParticleEffectId() != TOOLPARTICLESYSTEMID_INVALID )
	{
		KeyValues *msg = new KeyValues( "OldParticleSystem_ActivateEmitter" );
		msg->SetInt( "id", m_pDustEmitter->GetToolParticleEffectId() );
		msg->SetInt( "emitter", 0 );
		msg->SetInt( "active", false );
		msg->SetFloat( "time", gpGlobals->curtime );
		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}

	if ( m_pParticleMgr )
	{
		m_pParticleMgr->RemoveEffect( &m_ParticleEffect );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bEmit - 
//-----------------------------------------------------------------------------
void C_DustCloud::SetEmit(bool bEmit)
{
	m_bEmit = bEmit;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : rate - 
//-----------------------------------------------------------------------------
void C_DustCloud::SetSpawnRate(float rate)
{
	m_SpawnRate = rate;
	m_ParticleSpawn.Init(rate);
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : bnewentity - 
//-----------------------------------------------------------------------------
void C_DustCloud::OnDataChanged(DataUpdateType_t updateType)
{
	C_BaseEntity::OnDataChanged(updateType);

	if ( updateType == DATA_UPDATE_CREATED )
	{
		Start( ParticleMgr(), NULL );
	}
}


// FIXME: These all have to be moved out of this old system and into the new to leverage art assets!
PRECACHE_REGISTER_BEGIN( GLOBAL, PrecacheEffectDustcloud )
PRECACHE( MATERIAL, "particle/smokesprites_0001" )
/*
PRECACHE( MATERIAL, "particle/smokesprites_0002" )
PRECACHE( MATERIAL, "particle/smokesprites_0003" )
PRECACHE( MATERIAL, "particle/smokesprites_0004" )
PRECACHE( MATERIAL, "particle/smokesprites_0005" )
PRECACHE( MATERIAL, "particle/smokesprites_0006" )
PRECACHE( MATERIAL, "particle/smokesprites_0007" )
PRECACHE( MATERIAL, "particle/smokesprites_0008" )
PRECACHE( MATERIAL, "particle/smokesprites_0009" )
PRECACHE( MATERIAL, "particle/smokesprites_0010" )
PRECACHE( MATERIAL, "particle/smokesprites_0011" )
PRECACHE( MATERIAL, "particle/smokesprites_0012" )
PRECACHE( MATERIAL, "particle/smokesprites_0013" )
PRECACHE( MATERIAL, "particle/smokesprites_0014" )
PRECACHE( MATERIAL, "particle/smokesprites_0015" )
PRECACHE( MATERIAL, "particle/smokesprites_0016" )
*/
PRECACHE_REGISTER_END()


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pParticleMgr - 
//			*pArgs - 
//-----------------------------------------------------------------------------
void C_DustCloud::Start( CParticleMgr *pParticleMgr, IPrototypeArgAccess *pArgs )
{
	if(!pParticleMgr->AddEffect( &m_ParticleEffect, this ))
		return;

	m_pParticleMgr	= pParticleMgr;
	m_pDustEmitter = CDustFollower::Create("DustCloud");
	
	if ( !m_pDustEmitter )
	{
		Assert( false );
		return;
	}

	m_pDustEmitter->SetSortOrigin( GetAbsOrigin() );
	m_pDustEmitter->SetNearClip( 64.0f, 128.0f );

	for (int i = 0; i < DUSTCLOUD_MATERIALS; i++)
	{
		//char name[256];
		//Q_snprintf( name, sizeof( name ), "particle/smokesprites_%04d", i + 1 );
		m_MaterialHandle[i] = m_pDustEmitter->GetPMaterial( "particle/smokesprites_0001" );
	}
	
	m_ParticleSpawn.Init( m_SpawnRate );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : fTimeDelta - 
//-----------------------------------------------------------------------------
void C_DustCloud::Update( float fTimeDelta )
{
	if ( !m_pDustEmitter )
		return;

	Vector	offsetColor;

	// Add new particles
	if ( !m_bEmit )
		return;

	if ( ( m_StopEmitTime != 0 ) && ( m_StopEmitTime <= gpGlobals->curtime ) )
		return;

	float tempDelta = fTimeDelta;

	SimpleParticle	*pParticle;
	Vector			offset;

	Vector vecOrigin;
	VectorMA( GetAbsOrigin(), -fTimeDelta, GetAbsVelocity(), vecOrigin );

	Vector vecForward;
	GetVectors( &vecForward, NULL, NULL );

	while( m_ParticleSpawn.NextEvent( tempDelta ) )
	{
		float fldt = fTimeDelta - tempDelta;

		offset.Random( -m_SpawnRadius, m_SpawnRadius );
		offset += vecOrigin;
		VectorMA( offset, fldt, GetAbsVelocity(), offset );

		//if ( random->RandomFloat( 0.f, 5.0f ) > GetAbsVelocity().Length())
		//	continue;

		pParticle = (SimpleParticle *) m_pDustEmitter->AddParticle( sizeof( SimpleParticle ), m_MaterialHandle[random->RandomInt(0,0)], offset ); // FIXME: the other sprites look bad

		if ( pParticle == NULL )
			continue;

		pParticle->m_flLifetime		= 0.0f;
		pParticle->m_flDieTime		= m_ParticleLifetime;

		pParticle->m_vecVelocity.Random( -1.0f, 1.0f );
		pParticle->m_vecVelocity *= random->RandomFloat( m_MinSpeed, m_MaxSpeed );

		pParticle->m_vecVelocity = pParticle->m_vecVelocity + GetAbsVelocity();
		
		float flDirectedVel = random->RandomFloat( m_MinDirectedSpeed, m_MaxDirectedSpeed );
		VectorMA( pParticle->m_vecVelocity, flDirectedVel, vecForward, pParticle->m_vecVelocity );

		offsetColor = m_Color;
		float flMaxVal = MAX( m_Color[0], m_Color[1] );
		if ( flMaxVal < m_Color[2] )
		{
			flMaxVal = m_Color[2];
		}
		offsetColor /= flMaxVal;

		offsetColor *= random->RandomFloat( -0.2f, 0.2f );
		offsetColor += m_Color;

		offsetColor[0] = clamp( offsetColor[0], 0.0f, 1.0f );
		offsetColor[1] = clamp( offsetColor[1], 0.0f, 1.0f );
		offsetColor[2] = clamp( offsetColor[2], 0.0f, 1.0f );

		pParticle->m_uchColor[0]	= offsetColor[0]*255.0f;
		pParticle->m_uchColor[1]	= offsetColor[1]*255.0f;
		pParticle->m_uchColor[2]	= offsetColor[2]*255.0f;
		
		pParticle->m_uchStartSize	= m_StartSize;
		pParticle->m_uchEndSize		= m_EndSize;
		
		float alpha = random->RandomFloat( m_Opacity*0.75f, m_Opacity*1.25f );
		alpha = clamp( alpha, 0.0f, 1.0f );

		if ( m_StopEmitTime != 0 && m_StopEmitTime > m_StartEmitTime )
		{
			alpha *= sqrt( (m_StopEmitTime - gpGlobals->curtime) /(m_StopEmitTime - m_StartEmitTime) );
		}

		pParticle->m_uchStartAlpha	= alpha * 255; 
		pParticle->m_uchEndAlpha	= 0;
			
		pParticle->m_flRoll			= random->RandomInt( 0, 360 );
		pParticle->m_flRollDelta	= random->RandomFloat( -1.0f, 1.0f );
    }
}


void C_DustCloud::RenderParticles( CParticleRenderIterator *pIterator )
{
}


void C_DustCloud::SimulateParticles( CParticleSimulateIterator *pIterator )
{
}


//-----------------------------------------------------------------------------
// This is called after sending this entity's recording state
//-----------------------------------------------------------------------------

void C_DustCloud::CleanupToolRecordingState( KeyValues *msg )
{
	if ( !ToolsEnabled() )
		return;

	BaseClass::CleanupToolRecordingState( msg );

	// Generally, this is used to allow the entity to clean up
	// allocated state it put into the message, but here we're going
	// to use it to send particle system messages because we
	// know the grenade has been recorded at this point
	if ( !clienttools->IsInRecordingMode() || !m_pDustEmitter.IsValid() )
		return;
	
	// For now, we can't record Dustclouds that don't have a moveparent
	C_BaseEntity *pEnt = GetMoveParent();
	if ( !pEnt )
		return;

	bool bEmitterActive = m_bEmit && ( ( m_StopEmitTime == 0 ) || ( m_StopEmitTime > gpGlobals->curtime ) );

	// NOTE: Particle system destruction message will be sent by the particle effect itself.
	if ( m_pDustEmitter->GetToolParticleEffectId() == TOOLPARTICLESYSTEMID_INVALID )
	{
		int nId = m_pDustEmitter->AllocateToolParticleEffectId();

		KeyValues *msg = new KeyValues( "OldParticleSystem_Create" );
		msg->SetString( "name", "C_DustCloud" );
		msg->SetInt( "id", nId );
		msg->SetFloat( "time", gpGlobals->curtime );

		KeyValues *pEmitter = msg->FindKey( "DmeSpriteEmitter", true );
		pEmitter->SetString( "material", "particle/smokesprites_0001" );
		pEmitter->SetInt( "count", m_SpawnRate );	// particles per second, when duration is < 0
		pEmitter->SetFloat( "duration", -1 ); // FIXME
		pEmitter->SetInt( "active", bEmitterActive );

		KeyValues *pInitializers = pEmitter->FindKey( "initializers", true );

		// FIXME: Until we can interpolate ent logs during emission, this can't work
		KeyValues *pPosition = pInitializers->FindKey( "DmePositionPointToEntityInitializer", true );
		pPosition->SetPtr( "entindex", (void*)pEnt->entindex() );
		pPosition->SetInt( "attachmentIndex", GetParentAttachment() );
		pPosition->SetFloat( "randomDist", m_SpawnRadius );
		pPosition->SetFloat( "startx", pEnt->GetAbsOrigin().x );
		pPosition->SetFloat( "starty", pEnt->GetAbsOrigin().y );
		pPosition->SetFloat( "startz", pEnt->GetAbsOrigin().z );

		KeyValues *pVelocity = pInitializers->FindKey( "DmeDecayVelocityInitializer", true );
		pVelocity->SetFloat( "velocityX", pEnt->GetAbsVelocity().x );
		pVelocity->SetFloat( "velocityY", pEnt->GetAbsVelocity().y );
		pVelocity->SetFloat( "velocityZ", pEnt->GetAbsVelocity().z );
		pVelocity->SetFloat( "decayto", 0.5 );
		pVelocity->SetFloat( "decaytime", 0.3 );

		KeyValues *pLifetime = pInitializers->FindKey( "DmeRandomLifetimeInitializer", true );
		pLifetime->SetFloat( "minLifetime", m_ParticleLifetime );
 		pLifetime->SetFloat( "maxLifetime", m_ParticleLifetime );

		KeyValues *pRoll = pInitializers->FindKey( "DmeRandomRollInitializer", true );
		pRoll->SetFloat( "minRoll", 0.0f );
 		pRoll->SetFloat( "maxRoll", 360.0f );

		KeyValues *pRollSpeed = pInitializers->FindKey( "DmeRandomRollSpeedInitializer", true );
		pRollSpeed->SetFloat( "minRollSpeed", -1.0f );
 		pRollSpeed->SetFloat( "maxRollSpeed", 1.0f );

		KeyValues *pColor = pInitializers->FindKey( "DmeRandomValueColorInitializer", true );
		Color c( 
			clamp( m_Color.x * 255.0f, 0, 255 ),
			clamp( m_Color.y * 255.0f, 0, 255 ),
			clamp( m_Color.z * 255.0f, 0, 255 ), 255 );
		pColor->SetColor( "startColor", c );
		pColor->SetFloat( "minStartValueDelta", 0.0f );
 		pColor->SetFloat( "maxStartValueDelta", 0.0f );
		pColor->SetColor( "endColor", c );

		KeyValues *pAlpha = pInitializers->FindKey( "DmeRandomAlphaInitializer", true );
		int nMinAlpha = 255 * m_Opacity * 0.75f;
		int nMaxAlpha = 255 * m_Opacity * 1.25f;
		pAlpha->SetInt( "minStartAlpha", clamp( nMinAlpha, 0, 255 ) );
		pAlpha->SetInt( "maxStartAlpha", clamp( nMaxAlpha, 0, 255 ) );
		pAlpha->SetInt( "minEndAlpha", clamp( nMinAlpha, 0, 255 ) );
		pAlpha->SetInt( "maxEndAlpha", clamp( nMaxAlpha, 0, 255 ) );

		KeyValues *pSize = pInitializers->FindKey( "DmeRandomSizeInitializer", true );
		pSize->SetFloat( "minStartSize", m_StartSize );
		pSize->SetFloat( "maxStartSize", m_StartSize );
		pSize->SetFloat( "minEndSize", m_EndSize );
		pSize->SetFloat( "maxEndSize", m_EndSize );

		KeyValues *pUpdaters = pEmitter->FindKey( "updaters", true );
		pUpdaters->FindKey( "DmePositionVelocityDecayUpdater", true );
		pUpdaters->FindKey( "DmeRollUpdater", true );

		KeyValues *pRollSpeedUpdater = pUpdaters->FindKey( "DmeRollSpeedAttenuateUpdater", true );
		pRollSpeedUpdater->SetFloat( "attenuation", 1.0f - 8.0f / 30.0f );
		pRollSpeedUpdater->SetFloat( "attenuationTme", 1.0f / 30.0f );
		pRollSpeedUpdater->SetFloat( "minRollSpeed", 0.5f );

		pUpdaters->FindKey( "DmeAlphaSineRampUpdater", true );
		pUpdaters->FindKey( "DmeColorUpdater", true );
		pUpdaters->FindKey( "DmeSizeUpdater", true );

		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}
	else 
	{
		KeyValues *msg = new KeyValues( "OldParticleSystem_ActivateEmitter" );
		msg->SetInt( "id", m_pDustEmitter->GetToolParticleEffectId() );
		msg->SetInt( "emitter", 0 );
		msg->SetInt( "active", bEmitterActive );
		msg->SetFloat( "time", gpGlobals->curtime );
		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}
}

