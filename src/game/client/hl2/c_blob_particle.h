//========= Copyright © 1996-2007, Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

#ifndef NPC_CBLOB_PARTICLE_H
#define NPC_CBLOB_PARTICLE_H

#if defined( _WIN32 )
#pragma once
#endif

#include "utlvector.h"
#include "../common/blobulator/Implicit/ImpParticle.h"

// THIS SHOULD BE THE SAME AS IN THE SERVER (OR WE SHOULD INCLUDE STUFF FROM SERVER)
#define NUM_PARTICLE_TYPES 6
enum BlobParticleType
{
	// NOTE: The order here matters for transformation purposes.
	ROOT_PARTICLE = 0,
	CORE_PARTICLE = 1,
	STICKY_PARTICLE = 2,
	SKIN_PARTICLE = 3,
	TENTACLE_PARTICLE = 4,
	UNDEFINED_PARTICLE = 5,
};

class CCBlobParticle;

struct ArmatureSegment_t
{
	int startParticle;
	float len;
	Vector4D vPulseColor;
};

class CCBlobParticle
{
public:
	CCBlobParticle();

	float coreFieldStrength;
	int connectedByArmatureTo;
	Vector4D vPulseColor;
	CUtlVector<ArmatureSegment_t> armatureSegments;
};

#endif
