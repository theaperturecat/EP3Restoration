//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ====
//
// Purpose: 
//
//=============================================================================

#ifndef WEAPON_WEAPONIZER_H
#define WEAPON_WEAPONIZER_H
#ifdef _WIN32
#pragma once
#endif

#define WEAPONIZER_MODEL_SPHERE "models/weapons/w_ball_small.mdl"
#define WEAPONIZER_MODEL_MATTER "models/weapons/matter.mdl"

enum attackstate_t
{
	ATTACK_PRE,
	ATTACK_RUN,
	ATTACK_POST,
};


enum weaponparamstypes_t
{
	WEAPONIZER_PARAM_PRIMARY = 0,
	WEAPONIZER_PARAM_SECONDARY,
	WEAPONIZER_PARAM_FUSION,

	LAST_WEAPON_PARAM_COUNT,
};

#endif // WEAPON_WEAPONIZER_H

