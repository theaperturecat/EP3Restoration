//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//
// This defines the client-side SmokeCloud entity. It can also be used without
// an entity, in which case you must pass calls to it and set its position each frame.

#ifndef PARTICLE_SMOKECLOUD_H
#define PARTICLE_SMOKECLOUD_H

#include "particlemgr.h"
#include "particle_prototype.h"
#include "particle_util.h"
#include "particles_simple.h"
#include "c_baseentity.h"
#include "baseparticleentity.h"

#include "fx_trail.h"


//==================================================
// C_DustCloud
//==================================================

class C_DustCloud : public C_BaseParticleEntity, public IPrototypeAppEffect
{
public:
	DECLARE_CLASS( C_DustCloud, C_BaseParticleEntity );
	DECLARE_CLIENTCLASS();
	
					C_DustCloud();
	virtual			~C_DustCloud();

public:

	// Enable/disable emission.
	void			SetEmit(bool bEmit);

	// Change the spawn rate.
	void			SetSpawnRate(float rate);


// C_BaseEntity.
public:
	virtual	void	OnDataChanged(DataUpdateType_t updateType);

	virtual void	CleanupToolRecordingState( KeyValues *msg );

// IPrototypeAppEffect.
public:
	virtual void	Start(CParticleMgr *pParticleMgr, IPrototypeArgAccess *pArgs);

// IParticleEffect.
public:
	virtual void	Update(float fTimeDelta);
	virtual void RenderParticles( CParticleRenderIterator *pIterator );
	virtual void SimulateParticles( CParticleSimulateIterator *pIterator );


public:
	// Effect parameters. These will assume default values but you can change them.
	float			m_SpawnRate;			// How many particles per second.

	Vector			m_Color;
	float			m_Opacity;

	float			m_ParticleLifetime;		// How long do the particles live?
	float			m_StartEmitTime;		// When did I start emitting particles?
	float			m_StopEmitTime;			// When do I stop emitting particles? (-1 = never)
	
	float			m_MinSpeed;				// Speed range.
	float			m_MaxSpeed;
	
	float			m_MinDirectedSpeed;		// Directed speed range.
	float			m_MaxDirectedSpeed;

	float			m_StartSize;			// Size ramp.
	float			m_EndSize;

	float			m_SpawnRadius;

	Vector			m_VelocityOffset;		// Emit the particles in a certain direction.

	bool			m_bEmit;				// Keep emitting particles?

private:
	C_DustCloud( const C_DustCloud & );

#define DUSTCLOUD_MATERIALS 16
	PMaterialHandle	m_MaterialHandle[DUSTCLOUD_MATERIALS];
	TimedEvent		m_ParticleSpawn;

	CParticleMgr	*m_pParticleMgr;
	CSmartPtr<CSimpleEmitter> m_pDustEmitter;
};

#endif
