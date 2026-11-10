//========= Copyright © 1996-2007 Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#ifndef C_NPC_BLOB_H
#define C_NPC_BLOB_H

#if defined( _WIN32 )
#pragma once
#endif
#include "cbase.h"
#include "dt_utlvector_recv.h"
#include "bone_setup.h"
#include "c_ai_basenpc.h"
#include "engine/IVDebugOverlay.h"
#include "c_surfacerender.h"

#include "IVRenderView.h"
#include "view_shared.h"
#include "iviewrender.h"

#include "tier0/vprof.h"
#include "soundinfo.h"
#include "c_npc_surface.h"
#include "c_blob_particle.h"
#include "blob_shareddefs.h"

class C_NPC_Blob : public C_NPC_Surface
{
public:
	DECLARE_CLASS( C_NPC_Blob, C_NPC_Surface );
	DECLARE_CLIENTCLASS();
	DECLARE_INTERPOLATION();

	C_NPC_Blob();
	virtual			~C_NPC_Blob();

	virtual void Activate( void );

	void		ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs );

	// model specific
	virtual IClientModelRenderable* GetClientModelRenderable() { return NULL; }
	virtual int DrawModel( int flags, const RenderableInstance_t &instance );
	virtual ShadowType_t ShadowCastType() { return IsEffectActive( EF_NOSHADOW ) ? SHADOWS_NONE : SHADOWS_RENDER_TO_TEXTURE_DYNAMIC_CUSTOM; }
	virtual RenderableTranslucencyType_t ComputeTranslucencyType() { return RENDERABLE_IS_OPAQUE; }
	virtual int GetRenderFlags( void ) { return 0; }		// entity handles framebuffer texture update manually

	virtual void OnDataChanged( DataUpdateType_t type );

	virtual void SetUpParticleVector( bool bSkipOutOfWorldParticles = false );

	virtual void UpdateOutOfWorldParticleMask( void ) const;

	float GetPulseFrequency( void );
	Vector GetEmotionColor() const;

	void SetUpCoreDrawMaterial();
	void SetUpCoreParticleVector();

	void RenderLine( const Vector& v1, const Vector& v2, Color c, bool bZBuffer );
	void SetUpArmatureDrawMaterial();
	void DrawArmature( int nTotalSegs );

	void SetUpBackFaceDrawMaterial();
	void SetUpShadowDrawMaterial();

	virtual void SetUpDrawLighting();
	virtual void SetUpDrawMaterial();

public:

	CUtlVector<CCBlobParticle> m_blobParticles;
	//CUtlVector<float> m_coreFieldStrength;

	// Networked variables
	CUtlVector< int > m_vecTypes;
	CUtlVector< uint16 > m_vecParents;
	//CUtlVector< CInterpolatedVar< int > > m_iv_iType;
	CUtlVector< Vector > m_vecUVW;
	CUtlVector< CInterpolatedVar< Vector > > m_iv_vecUVW;
	CUtlVector< uint16 > m_iFlags;	// per-particle flags
	float			m_flColorChangeStartTime;
	float			m_flColorChangeEndTime;
	Vector			m_vecStartColor;
	Vector			m_vecEndColor;

	bool			m_bFrozen;
	CHandle<C_NPC_Blob> m_hAutoSplitBlob;		// handle to blob this one split from
	float			m_fBrainExposure;			// How 'exposed' the brain is (affects how we render brain particles)

private:
	friend class CBlobLightingMaterialProxy;	// material proxies gets access to blob internals
	friend class CBlobMaterialProxy;

	C_NPC_Blob( const C_NPC_Blob & ); // not defined, not accessible

	Vector GetRootNodePosition() const;

	struct ArmatureNodeInfo
	{
		float m_flDistToRoot;
		float m_flArmLength;
		float m_flMinDistToRoot;
		float m_flMaxDistToRoot;
		float m_flMinArmLength;
		float m_flMaxArmLength;
		float m_flDistToFirstCoreParticle;
		float m_flLengthToFirstCoreParticle;
		Vector m_vAvgTangent;
		int   m_nNumChildren;
		bool  m_bLeafNode;
		bool  m_bRootNode;
		int   m_nVisited;

		ArmatureNodeInfo() :
			m_flDistToRoot( -1.0f ),
			m_flMinDistToRoot( -1.0f ),
			m_flMaxDistToRoot( -1.0f ),
			m_flArmLength( -1.0f ),
			m_flMinArmLength( -1.0f ),
			m_flMaxArmLength( -1.0f ),
			m_flDistToFirstCoreParticle( -1.0f ),
			m_flLengthToFirstCoreParticle( -1.0f ),
			m_vAvgTangent( 0.0f, 0.0f, 0.0f ),
			m_nNumChildren( 0 ),
			m_bLeafNode( true ),
			m_bRootNode( false ),
			m_nVisited( INT_MAX ) {}
	};

	ArmatureNodeInfo m_pArmInfo[ MAX_SURFACE_ELEMENTS ];
	int m_nGlobalToLocalParticleIdx[ MAX_SURFACE_ELEMENTS ];
	int m_nTotalSegs;		// total number of arm segments
	int m_bGlobalToLocalParticleIdxValid;
	
	Vector4D m_lightingBoxColors[6];	// WORK IN PROGRESS

	void DrawModelShadow();
	void DrawModelShadowDepth();

	bool UpdateGlobalToLocalParticleIdx();

	// Internal methods for armature rendering
	int UpdateArmatureData();
	int WalkGraph( int nParticleID, ArmatureNodeInfo* pArmInfo, int nVisitID, bool bPassedCoreParticle = false );
	void PropagateArmLength( float fMinLen, float fMaxLen, float fCoreParticleLen, Vector4D vPulseColor, int nParticleID, ArmatureNodeInfo* pArmInfo, int nVisitID );
	Color GetDebugArmColor( int nParticleID, ArmatureNodeInfo* pArmInfo, bool bStartNode );
	Vector4D GetArmPulse( int nParticleID );
};


#endif // C_NPC_BLOB
