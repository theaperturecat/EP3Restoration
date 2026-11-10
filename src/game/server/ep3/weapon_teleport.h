//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//


#ifndef EP3_WEAPON_TELEPORT_H
#define EP3_WEAPON_TELEPORT_H

#ifdef HL2_EP3

extern void ShootTeleportProjectile( Vector vOrigin, QAngle angles, CBaseEntity *pOwner, CBaseEntity *pGun );
extern CBaseEntity *ShootTeleportProjectileNPC( Vector vOrigin, Vector vTossVelocity, CBaseEntity *pOwner );

#else

inline void ShootTeleportProjectile( Vector vOrigin, QAngle angles, CBaseEntity *pOwner, CBaseEntity *pGun )
{
	return;
}

inline CBaseEntity *ShootTeleportProjectileNPC( Vector vOrigin, Vector vTossVelocity, CBaseEntity *pOwner )
{
	return NULL;
}

#endif


#endif // EP3_WEAPON_TELEPORT_H
