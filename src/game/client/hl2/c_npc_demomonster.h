//========= Copyright © 1996-2007 Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#ifndef C_NPC_DEMOMONSTER_H
#define C_NPC_DEMOMONSTER_H

#if defined( _WIN32 )
#pragma once
#endif

#include "c_npc_surface.h"


class C_NPC_BlobDemoMonster : public C_NPC_Surface
{
public:
	DECLARE_CLASS( C_NPC_BlobDemoMonster, C_NPC_Surface );
	DECLARE_CLIENTCLASS();
	DECLARE_INTERPOLATION();

	C_NPC_BlobDemoMonster();
	virtual			~C_NPC_BlobDemoMonster();

	// model specific
	virtual int DrawModel( int flags, const RenderableInstance_t &instance );

	virtual void SetUpParticleVector();
	virtual void TransformParticles(float angle, Vector& center, Vector& transformedCenter, Vector& transformedEye, VMatrix& rotationMatrix, VMatrix& invRotationMatrix);

	CUtlVector< float > m_flSurfaceV;
	CUtlVector< CInterpolatedVar< float > > m_iv_flSurfaceV;

private:
	C_NPC_BlobDemoMonster( const C_NPC_BlobDemoMonster & ); // not defined, not accessible
};


#endif // C_NPC_DEMOMONSTER