//========= Copyright © 1996-2007, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

#include "c_blob_particle.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
//
// CCBlobParticle
//

CCBlobParticle::CCBlobParticle()
: connectedByArmatureTo(-1)
, coreFieldStrength(0.0f)
, vPulseColor( 0.0f, 0.0f, 0.0f, -1.0f )
{

}
