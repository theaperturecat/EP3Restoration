//========= Copyright © 1996-2007, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_npc_fountain.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Purpose: setup network receive table
//-----------------------------------------------------------------------------

IMPLEMENT_CLIENTCLASS_DT(C_NPC_BlobFountain, DT_NPC_BlobFountain, CNPC_BlobFountain)

END_RECV_TABLE()


C_NPC_BlobFountain::C_NPC_BlobFountain()
: C_NPC_Surface()
{

}

C_NPC_BlobFountain::~C_NPC_BlobFountain()
{
	
}

/*
const Vector& C_NPC_BlobFountain::GetRenderOrigin( void )
{
	m_vCachedRenderOrigin.Init(-1980, -1792, 1);
	return m_vCachedRenderOrigin;
}
*/

int C_NPC_BlobFountain::DrawModel( int flags, const RenderableInstance_t &instance )
{
	return C_NPC_Surface::DrawModel( flags, instance );
}

void C_NPC_BlobFountain::SetUpDrawLighting()
{
	modelrender->SetupLighting(GetRenderOrigin());
}

void C_NPC_BlobFountain::SetUpParticleVector()
{
	Vector fountainOrigin = GetRenderOrigin();
	int n_particles = 0;
	m_particles.EnsureCapacity(m_nActiveParticles + 400);

	for (int i = 0 ; i < m_nActiveParticles; i++)
	{
		ImpParticleWithOneInterpolant* imp_particle = &m_particles[i];
		imp_particle->center = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[i] );
		imp_particle->setFieldScale( m_flSurfaceR[i] );
		imp_particle->interpolants1.set(0.0f, 0.0f, 0.0f);
		imp_particle->interpolants1[3] = 0.0f; //m_flSurfaceV[i];
		n_particles ++;
	}

	// This code adds a water surface to the fountain trough
	// using particles that oscillate up and down
	static float time = 0.0f;
	
	bool paused = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[0] ) == m_lastPoint0Pos;
	m_lastPoint0Pos = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[0] ); //((CAI_BaseNPC::m_nDebugBits & bits_debugDisableAI) != 0);
	if(!paused) time += 0.1f;

	for (int i = -7; i <= 7; i++)
	for (int j = -7; j <= 7; j++)
	{
			ImpParticleWithOneInterpolant* imp_particle = &m_particles[n_particles++];
			imp_particle->center = fountainOrigin + Vector(i * 2.0f * m_flRadius, j * 2.0f * m_flRadius, 15.0f);
			float dist = sqrtf(Sqr(imp_particle->center[0]) + Sqr(imp_particle->center[1]));
			
			imp_particle->center[2] += 2.0f*sin(2.0f*time + 2.0f*dist);
			imp_particle->setFieldScale( 1.0f );
			imp_particle->interpolants1[3] = 0.0f;
	}
	for (int i = -2; i <= 2; i++)
	for (int j = -2; j <= 2; j++)
	{
			ImpParticleWithOneInterpolant* imp_particle = &m_particles[n_particles++];
			imp_particle->center = fountainOrigin + Vector(i * 2.0f * m_flRadius, j * 2.0f * m_flRadius, 15.0f - 2.0f * m_flRadius);
			imp_particle->setFieldScale( 1.0f );
			imp_particle->interpolants1[3] = 0.0f;
	}
	ImpParticleWithOneInterpolant* imp_particle = &m_particles[n_particles++];
	imp_particle->center = fountainOrigin + Vector(0.0f, 0.0f, 15.0f - 4.0f * m_flRadius);
	imp_particle->setFieldScale( 1.0f );
	imp_particle->interpolants1[3] = 0.0f;

	m_particles.SetCount(n_particles);
}
