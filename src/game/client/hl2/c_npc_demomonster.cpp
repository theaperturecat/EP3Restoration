//========= Copyright © 1996-2007, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "dt_utlvector_recv.h"
#include "bone_setup.h"
#include "c_npc_demomonster.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Purpose: setup network receive table
//-----------------------------------------------------------------------------

IMPLEMENT_CLIENTCLASS_DT(C_NPC_BlobDemoMonster, DT_NPC_BlobDemoMonster, CNPC_BlobDemoMonster)
	RecvPropUtlVector( 
		RECVINFO_UTLVECTOR( m_flSurfaceV ), 
		MAX_SURFACE_ELEMENTS,
		RecvPropFloat(NULL, 0, sizeof( float ))),
END_RECV_TABLE()


C_NPC_BlobDemoMonster::C_NPC_BlobDemoMonster()
: C_NPC_Surface()
{
	m_flSurfaceV.EnsureCount( MAX_SURFACE_ELEMENTS );
	m_iv_flSurfaceV.EnsureCount( MAX_SURFACE_ELEMENTS );

	for (int i = 0; i < MAX_SURFACE_ELEMENTS; i++)
	{
		IInterpolatedVar *pWatcher = &m_iv_flSurfaceV.Element( i );
		pWatcher->SetDebugName( "m_iv_flSurfaceV" );
		AddVar( &m_flSurfaceV.Element( i ), pWatcher, LATCH_ANIMATION_VAR );
	}
}

C_NPC_BlobDemoMonster::~C_NPC_BlobDemoMonster()
{

}

int C_NPC_BlobDemoMonster::DrawModel( int flags, const RenderableInstance_t &instance )
{
	return C_NPC_Surface::DrawModel( flags, instance );
}

void C_NPC_BlobDemoMonster::SetUpParticleVector()
{
	if(sv_surface_debug_testshape.GetBool())
	{
		m_particles.EnsureCapacity(21);
		m_particles.SetCount(21);

		for (int i = -10; i <= 10; i++)
		{
			ImpParticle* imp_particle = &m_particles[i+10];
			imp_particle->center.set(i*2.0f*m_flRadius, 0.0f, 0.0f);
		}
	}
	else
	{
		m_particles.EnsureCapacity(m_nActiveParticles);

		int n_particles = 0;

		for (int i = 0; i < m_nActiveParticles; i++)
		{
			ImpParticleWithOneInterpolant* imp_particle = &m_particles[n_particles];
			if(m_flSurfaceR[i] <= 0.1f) continue; // Don't draw tiny particles (Maybe not a great default behavior)
			imp_particle->center = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[i] );
			imp_particle->setFieldScale( m_flSurfaceR[i] );
			imp_particle->interpolants1.set(1.0f, 1.0f, 1.0f);
			imp_particle->interpolants1[3] = m_flSurfaceV[i];
			n_particles ++;
		}

		m_particles.SetCountNonDestructively(n_particles);
	}
}

void C_NPC_BlobDemoMonster::TransformParticles(float angle, Vector& center, Vector& transformedCenter, Vector& transformedEye, VMatrix& rotationMatrix, VMatrix& invRotationMatrix)
{
	for (int i = 0; i < m_particles.Count(); i++)
	{
		ImpParticleWithFourInterpolants* imp_particle = &m_particles[i];

		Vector vParticle = imp_particle->center.AsVector();
		//vParticle.Init(imp_particle->center[0], imp_particle->center[1], imp_particle->center[2]);
		Vector transformedParticle = vParticle-center;

		//if(sv_surface_rotate.GetBool())
		//{
		transformedParticle = rotationMatrix.ApplyRotation(transformedParticle);
		//}

		Point3D pParticle = transformedParticle; 
		Point3D pCenter = transformedCenter;
		Point3D vec = (pParticle - pCenter);

		imp_particle->center = pParticle;
		//imp_particle->setFieldScale( max(1.2f - vec.length()/30.0f, 0.25f) );

		// interpolants1[0..2] is the color. interpolants1[3] is the v coordinate
		// imp_particle->interpolants1[3] = min(max(1.4f - vec.length()/17.0f, 0.0f), 1.0f);
		// interpolants2[0..2] is the tangent vector.
		// interpolants3[0..2] and interpolants4[0..2] are the normal and
		// binormal which are used to generate a u coordinate
		imp_particle->interpolants2 = vec.unit();
		imp_particle->interpolants4.set(0.0f, 0.0f, -1.0f);
		imp_particle->interpolants3 = imp_particle->interpolants2.crossProduct(imp_particle->interpolants4);
		imp_particle->interpolants3.normalize();
		imp_particle->interpolants4 = imp_particle->interpolants2.crossProduct(imp_particle->interpolants3);
		imp_particle->interpolants4.normalize();
	}
}