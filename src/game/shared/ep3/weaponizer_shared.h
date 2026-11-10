//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ====
//
// 
//
//=============================================================================

#ifndef WEAPONIZER_SHARED_H
#define WEAPONIZER_SHARED_H
#ifdef _WIN32
#pragma once
#endif


// Effects messages
#define WEAPONIZER_FX_ELECTRICAL_ZAP	0
#define WEAPONIZER_FX_ABSORB			1


// NOTE: If you change this enum, you must also update the selectParams
// array AND the s_AmmoParams array in weapon_weaponizer.cpp!!
enum WeaponizerMaterialTypes_t
{
	// Primary materials
	WEAPONIZER_MATERIAL_TYPE_NONE = 0,
	WEAPONIZER_MATERIAL_TYPE_FIRE,
	WEAPONIZER_MATERIAL_TYPE_ELECTRICAL,
	WEAPONIZER_MATERIAL_TYPE_CONCRETE,
	WEAPONIZER_MATERIAL_TYPE_METAL,

	// Must be between primary materials and secondary materials!
	WEAPONIZER_NUM_PRIMARY_MATERIALS,

	// Secondary materials
	WEAPONIZER_MATERIAL_TYPE_EXPLOSIVE,
	WEAPONIZER_MATERIAL_TYPE_WATER,

	// Must be last!
	WEAPONIZER_NUM_MATERIALS,
};


enum modifiertype_t
{
	WEAPONIZER_MODIFIER_NONE = 0,
	WEAPONIZER_MODIFIER_EXPLOSIVE,
	WEAPONIZER_MODIFIER_FLECHETTE,
};


#endif // WEAPONIZER_SHARED_H
