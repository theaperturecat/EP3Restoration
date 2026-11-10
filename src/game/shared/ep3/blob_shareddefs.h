//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#ifndef PORTAL_SHAREDDEFS_H
#define PORTAL_SHAREDDEFS_H
#ifdef _WIN32
#pragma once
#endif


enum BlobEmotion_t
{
	BLOB_EMOTION_CONTENT = 0,
	BLOB_EMOTION_CURIOUS,
	BLOB_EMOTION_HUNGRY,
	BLOB_EMOTION_ANGRY,
	BLOB_EMOTION_AFRAID,
	BLOB_EMOTION_EXHAUSTED,

	BLOB_EMOTION_TOTAL
};

enum BlobParticleFlags_t
{
	BLOB_PARTICLE_BIRTH						= 0x01, // This particle is in the process of being born (doesn't simulate normally, grows from a point)
	BLOB_PARTICLE_DEATH						= 0x02, // This particle is in the process of dying (doesn't simulate normally, shrinks to a point)

	BLOB_PARTICLE_TOUCHED_SURFACE			= 0x04,
	BLOB_PARTICLE_PERFORMED_MELEE_ATTACK	= 0x08,
	BLOB_PARTICLE_STRAGGLER					= 0x10, // Particle fell behind main blob group (TEMP HACK: collision is disabled to
													// allow catchup. NOT the final solution - will use 'breadcrumbs' instead)
	BLOB_PARTICLE_TENTACLE					= 0x20, // This particle is part of a tentacle
	BLOB_PARTICLE_BRAIN						= 0x40, // This particle is part of the damageable 'Brain' of the blob
	BLOB_PARTICLE_SIEVE						= 0x80, // Utility flag to mark particles as 'processed' during any non-trivial
													// iteration over the particle graph
	BLOB_PARTICLE_TENTACLE_ROOT				= 0x100,// Is a tentacle attached to this particle?
	BLOB_PARTICLE_NEAR_DANGER				= 0x200,
};

#endif // PORTAL_SHAREDDEFS_H
