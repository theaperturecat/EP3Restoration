//========= Copyright © 1996-2007 Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#ifndef C_NPC_FOUNTAIN_H
#define C_NPC_FOUNTAIN_H

#if defined( _WIN32 )
#pragma once
#endif

#include "c_npc_surface.h"


class C_NPC_BlobFountain : public C_NPC_Surface
{
public:
	DECLARE_CLASS( C_NPC_BlobFountain, C_NPC_Surface );
	DECLARE_CLIENTCLASS();
	DECLARE_INTERPOLATION();

	C_NPC_BlobFountain();
	virtual			~C_NPC_BlobFountain();

	// model specific
	//const Vector& GetRenderOrigin( void );
	virtual int DrawModel( int flags, const RenderableInstance_t &instance );

	virtual void SetUpDrawLighting();
	virtual void SetUpParticleVector();

private:
	Vector m_lastPoint0Pos;

	C_NPC_BlobFountain( const C_NPC_BlobFountain & ); // not defined, not accessible
};


#endif // C_NPC_FOUNTAIN