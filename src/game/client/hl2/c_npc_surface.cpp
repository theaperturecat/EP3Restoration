//========= Copyright © 1996-2007, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "dt_utlvector_recv.h"
#include "bone_setup.h"

#include "tier0/vprof.h"
#include "soundinfo.h"
#include "hud_macros.h"

#include "c_npc_surface.h"
#include "c_surfacerender.h"

#include "../../common/blobulator/implicit/impdefines.h"
#include "../../common/blobulator/implicit/imprenderer.h"
#include "../../common/blobulator/implicit/imptiler.h"
#include "../../common/blobulator/implicit/userfunctions.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Purpose: setup network receive table
//-----------------------------------------------------------------------------

IMPLEMENT_CLIENTCLASS_DT(C_NPC_Surface, DT_NPC_Surface, CNPC_Surface)
/* Now goes through the network bypass
RecvPropUtlVector(
	RECVINFO_UTLVECTOR( m_vecSurfacePos ),
	MAX_SURFACE_ELEMENTS,
	RecvPropVector(NULL, 0, sizeof( Vector ))),*/
	RecvPropUtlVector(
		RECVINFO_UTLVECTOR(m_iParticlePositionIndex),
		MAX_SURFACE_ELEMENTS,
		RecvPropInt(NULL, 0, sizeof(uint16))),
	/* Now goes through the network bypass
	RecvPropUtlVector(
		RECVINFO_UTLVECTOR( m_flSurfaceR ),
		MAX_SURFACE_ELEMENTS,
		RecvPropFloat(NULL, 0, sizeof( float ))),*/

	RecvPropInt(RECVINFO(m_nActiveParticles)),
	RecvPropFloat(RECVINFO(m_flRadius)),
	END_RECV_TABLE()

	//-----------------------------------------------------------------------------
	// Purpose: link networked elements to local data
	//-----------------------------------------------------------------------------

	C_NPC_Surface::C_NPC_Surface()
	: m_vCachedRenderOrigin(ReplicateToVector(0.0f))
	, m_vCachedWorldMins(ReplicateToVector(0.0f))
	, m_vCachedWorldMaxs(ReplicateToVector(0.0f))
	, m_iCachedWorldBoundsUpdateTick(0)
	, m_iCachedOutOfWorldParticleMaskTick(0)
{
	CreateModelInstance();

	m_pMaterial = materials->FindMaterial("models/blob/blob_outer_material", TEXTURE_GROUP_OTHER);

	m_iParticlePositionIndex.EnsureCount(MAX_SURFACE_ELEMENTS);
	//m_iv_vecSurfacePos.EnsureCount( MAX_SURFACE_ELEMENTS );
	//m_flSurfaceR.EnsureCount( MAX_SURFACE_ELEMENTS );
	//m_iv_flSurfaceR.EnsureCount( MAX_SURFACE_ELEMENTS );

	for (int i = 0; i < MAX_SURFACE_ELEMENTS; i++)
	{
		/*IInterpolatedVar *pWatcher; */

		/*pWatcher = &m_iv_vecSurfacePos.Element( i );
		pWatcher->SetDebugName( "m_iv_vecSurfacePos" );
		AddVar( &m_vecSurfacePos.Element( i ), pWatcher, LATCH_ANIMATION_VAR );*/

		/*pWatcher = &m_iv_flSurfaceR.Element( i );
		pWatcher->SetDebugName( "m_iv_flSurfaceR" );
		AddVar( &m_flSurfaceR.Element( i ), pWatcher, LATCH_ANIMATION_VAR );*/
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

C_NPC_Surface::~C_NPC_Surface()
{
	DestroyModelInstance();
}


//-----------------------------------------------------------------------------
// Purpose: Custom model rendering
//-----------------------------------------------------------------------------

void C_NPC_Surface::UpdateOutOfWorldParticleMask(void) const
{
	// A subclass with more particle information will need to override this
	m_vOutOfWorldParticleMask.ClearAll();
}

const CVarBitVec& C_NPC_Surface::GetOutOfWorldParticleMask(void) const
{
	if (m_iCachedOutOfWorldParticleMaskTick != gpGlobals->tickcount)
	{
		m_iCachedOutOfWorldParticleMaskTick = gpGlobals->tickcount;

		m_vOutOfWorldParticleMask.Resize(m_nActiveParticles);
		UpdateOutOfWorldParticleMask();
	}

	return m_vOutOfWorldParticleMask;
}

const Vector& C_NPC_Surface::GetRenderOrigin(void)
{
	UpdateRenderBoundsAndOriginWorldspace();
	return m_vCachedRenderOrigin;
}

void C_NPC_Surface::UpdateRenderBoundsAndOriginWorldspace(void) const
{
	// TODO: Consider networking bounds in from the server

	if (m_iCachedWorldBoundsUpdateTick != gpGlobals->tickcount)
	{
		m_iCachedWorldBoundsUpdateTick = gpGlobals->tickcount;

		m_vCachedWorldMins = BLOBPARTICLEPOS_INTERP(m_iParticlePositionIndex[0]);
		m_vCachedWorldMaxs = BLOBPARTICLEPOS_INTERP(m_iParticlePositionIndex[0]);
		m_vCachedRenderOrigin = BLOBPARTICLEPOS_INTERP(m_iParticlePositionIndex[0]);

		// To avoid lighting issues, ignore particles which are outside of the world
		const CVarBitVec& particleMask = GetOutOfWorldParticleMask();

		int n_particles = 1;
		for (int i = 1; i < m_nActiveParticles; i++)
		{
			if (!particleMask.IsBitSet(i))
			{
				VectorMin(BLOBPARTICLEPOS_INTERP(m_iParticlePositionIndex[i]), m_vCachedWorldMins, m_vCachedWorldMins);
				VectorMax(BLOBPARTICLEPOS_INTERP(m_iParticlePositionIndex[i]), m_vCachedWorldMaxs, m_vCachedWorldMaxs);

				// compute centroid of particle cloud
				m_vCachedRenderOrigin += BLOBPARTICLEPOS_INTERP(m_iParticlePositionIndex[i]);
				n_particles++;
			}
		}
		m_vCachedRenderOrigin /= n_particles;

		Vector vRadius = ReplicateToVector(m_flRadius * 3.0f + 0.1f); // TODO: not sure if this is exactly what we want!
		m_vCachedWorldMins -= vRadius;
		m_vCachedWorldMaxs += vRadius;
	}
}

void C_NPC_Surface::GetRenderBoundsWorldspace(Vector& absMins, Vector& absMaxs)
{
	UpdateRenderBoundsAndOriginWorldspace();
	absMins = m_vCachedWorldMins;
	absMaxs = m_vCachedWorldMaxs;
}

void C_NPC_Surface::GetRenderBounds(Vector& theMins, Vector& theMaxs)
{
	UpdateRenderBoundsAndOriginWorldspace();

	theMins = m_vCachedWorldMins - m_vCachedRenderOrigin;
	theMaxs = m_vCachedWorldMaxs - m_vCachedRenderOrigin;

	// Msg( "origin  %.2f %.2f %.2f : mins %.2f %.2f %.2f  : maxs %.2f %.2f %.2f\n", GetRenderOrigin().x, GetRenderOrigin().y, GetRenderOrigin().z, theMins.x, theMins.y, theMins.z, theMaxs.x, theMaxs.y, theMaxs.z );

	//debugoverlay->AddBoxOverlay( GetRenderOrigin(), theMins, theMaxs, QAngle( 0, 0, 0 ), 0, 255, 0, 0, 0 );
}

const QAngle& C_NPC_Surface::GetRenderAngles()
{
	return g_SurfaceRenderAnglesAngles;
}


RenderableTranslucencyType_t C_NPC_Surface::ComputeTranslucencyType()
{
	// TODO: Fix this
	return RENDERABLE_IS_TRANSLUCENT;
}


int C_NPC_Surface::GetRenderFlags(void)
{
	int nRet = 0;
	if (m_pMaterial)
	{
		if (m_pMaterial->NeedsPowerOfTwoFrameBufferTexture())
			nRet |= ERENDERFLAGS_NEEDS_POWER_OF_TWO_FB;
		if (m_pMaterial->NeedsFullFrameBufferTexture())
			nRet |= ERENDERFLAGS_NEEDS_FULL_FB;
	}
	return nRet;
}

__forceinline float sqr(float a) { return a * a; }


int C_NPC_Surface::DrawModel(int flags, const RenderableInstance_t& instance)
{
#if !defined( _X360 ) // X360TBD: Enable blobulator for EP3

	SetUpDrawLighting();

	SetUpParticleVector();

	Surface_Draw(GetClientRenderable(), GetRenderOrigin(), m_pMaterial, m_flRadius);

#endif // !_X360

	return 1;
}

void C_NPC_Surface::SetUpDrawLighting()
{
	if (GetModelInstance() == MODEL_INSTANCE_INVALID)
	{
		// we need a model instance so that we can perform smooth lighting interpolation
		CreateModelInstance();
	}

	modelrender->SetupLightingEx(GetRenderOrigin(), GetModelInstance());
	//modelrender->SetupLighting( Vector(0,0,300) );
}

void C_NPC_Surface::SetUpParticleVector(bool bSkipOutOfWorldParticles)
{
	g_SurfaceRenderParticles.SetCount(m_nActiveParticles);

	const CVarBitVec& particleMask = GetOutOfWorldParticleMask();

	int n_particles = 0;
	for (int i = 0; i < m_nActiveParticles; i++)
	{
		if (!bSkipOutOfWorldParticles || !particleMask.Get(i))
		{
			ImpParticleWithOneInterpolant* imp_particle = &g_SurfaceRenderParticles[n_particles];
			if (BLOBPARTICLERADIUS_INTERP(m_iParticlePositionIndex[i]) <= 0.1f)
				continue; // Don't draw tiny particles (Maybe not a great default behavior)
			imp_particle->center = BLOBPARTICLEPOS_INTERP(m_iParticlePositionIndex[i]);
			imp_particle->setFieldScale(BLOBPARTICLERADIUS_INTERP(m_iParticlePositionIndex[i]));
			imp_particle->interpolants1.set(1.0f, 1.0f, 1.0f);
			imp_particle->interpolants1[3] = 0.0f; //m_flSurfaceV[i];
			n_particles++;
		}
	}

	g_SurfaceRenderParticles.SetCountNonDestructively(n_particles);
}

//-----------------------------------------------------------------------------
// Purpose: move the sound source to a point near the player
//-----------------------------------------------------------------------------

bool C_NPC_Surface::GetSoundSpatialization(SpatializationInfo_t& info)
{
	bool bret = BaseClass::GetSoundSpatialization(info);
	// Default things it's audible, put it at a better spot?
	if (bret)
	{
		// TODO:  Note, this is where you could override the sound position and orientation and use
		//  an attachment points position as the sound source
		// You might have to issue C_BaseAnimating::AllowBoneAccess( true, false ); to allow
		//  bone setup during sound spatialization if you run into asserts...
	}

	return bret;
}

void C_NPC_Surface::OnPreDataChanged(DataUpdateType_t type)
{
	m_nOldActiveParticles = m_nActiveParticles;

	// NOTE: The blob is always moving (even if its AbsOrigin remains still), this marks shadow projection + client leaf dirty
	// UNDONE: could skip this if the blob becomes frozen
	CollisionProp()->MarkPartitionHandleDirty();

	BaseClass::OnPreDataChanged(type);
}

void C_NPC_Surface::OnDataChanged(DataUpdateType_t type)
{
	BaseClass::OnDataChanged(type);
}





