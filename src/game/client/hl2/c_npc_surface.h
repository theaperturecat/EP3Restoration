//========= Copyright © 1996-2007, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#ifndef C_NPC_SURFACE_H
#define C_NPC_SURFACE_H

#if defined( _WIN32 )
#pragma once
#endif

#include "c_ai_basenpc.h"
#include "blob_networkbypass.h"

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

#define	MAX_SURFACE_ELEMENTS 750 //1000 Ilya: Reduced because I was hitting max prop buffer error //200

class C_NPC_Surface : public C_AI_BaseNPC
{
public:
	DECLARE_CLASS(C_NPC_Surface, C_AI_BaseNPC);
	DECLARE_CLIENTCLASS();
	DECLARE_INTERPOLATION();

	C_NPC_Surface();
	virtual			~C_NPC_Surface();

	// model specific
	virtual void GetRenderBounds(Vector& theMins, Vector& theMaxs);
	virtual const QAngle& GetRenderAngles();
	virtual RenderableTranslucencyType_t ComputeTranslucencyType();
	ShadowType_t	ShadowCastType() { return SHADOWS_NONE; }
	int GetRenderFlags(void);
	virtual int DrawModel(int flags, const RenderableInstance_t& instance);
	virtual const Vector& GetRenderOrigin(void);
	virtual bool ShouldDraw(void) { return true; }

	virtual void					GetRenderBoundsWorldspace(Vector& absMins, Vector& absMaxs);
	virtual const Vector& WorldAlignMins() const { ((C_NPC_Surface*)this)->GetRenderBoundsWorldspace(m_vCachedWorldMins, m_vCachedWorldMaxs); return m_vCachedWorldMins; }
	virtual const Vector& WorldAlignMaxs() const { ((C_NPC_Surface*)this)->GetRenderBoundsWorldspace(m_vCachedWorldMins, m_vCachedWorldMaxs); return m_vCachedWorldMaxs; }
	// TODO: This sucks.... I should use a renderCenter variable.
	virtual const Vector& WorldSpaceCenter() { GetRenderOrigin(); return m_vCachedRenderOrigin; }

	virtual void					OnDataChanged(DataUpdateType_t type);
	virtual void					OnPreDataChanged(DataUpdateType_t type);

	virtual bool	GetSoundSpatialization(SpatializationInfo_t& info);

	// Subclass overidable draw functions
	virtual void SetUpDrawLighting();
	virtual void SetUpParticleVector(bool bSkipOutOfWorldParticles = false);

	virtual void UpdateOutOfWorldParticleMask(void) const;
	const CVarBitVec& GetOutOfWorldParticleMask(void) const;

	IMaterial* m_pMaterial;

	// Networked variables
	//CUtlVector< Vector	> m_vecSurfacePos; // Now goes through the network bypass
	//CUtlVector< CInterpolatedVar< Vector > > m_iv_vecSurfacePos;
	CUtlVector< uint16 > m_iParticlePositionIndex;

	//CUtlVector< float > m_flSurfaceR; // Now goes through the network bypass
	//CUtlVector< CInterpolatedVar< float > > m_iv_flSurfaceR;

protected:
	// Non-networked variables
	int				m_nActiveParticles;
	float			m_flRadius;

	mutable Vector m_vCachedRenderOrigin;
	mutable Vector	m_vCachedWorldMins;
	mutable Vector	m_vCachedWorldMaxs;
	mutable int		m_iCachedWorldBoundsUpdateTick;
	mutable int		m_iCachedOutOfWorldParticleMaskTick;

	// Vector in which bits are set to identify particles which are outside of the world (relies on subclass to set)
	mutable CVarBitVec m_vOutOfWorldParticleMask;

private:
	C_NPC_Surface(const C_NPC_Surface&); // not defined, not accessible

	int m_nOldActiveParticles; //used to detect networked changes

	void UpdateRenderBoundsAndOriginWorldspace(void) const;
};

#endif // C_NPC_Surface
