//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ====
//
// Purpose: 
//
//=============================================================================

#include "cbase.h"
#include "c_weapon__stubs.h"
#include "c_basehlcombatweapon.h"
#include "fx.h"
#include "particles_localspace.h"
#include "view.h"
#include "particles_attractor.h"
#include "in_buttons.h"
#include "c_physicsprop.h"
#include "dt_utlvector_recv.h"
#include "props_shared.h"
#include "weaponizer_shared.h"


// HACK: needs to be shared
#define ZAP_FLAK_MAX_ENTS 256


class C_WeaponWeaponizer: public C_BaseHLCombatWeapon
{
	DECLARE_CLASS( C_WeaponWeaponizer, C_BaseHLCombatWeapon );
public:
	C_WeaponWeaponizer( void );

	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

	virtual void OnDataChanged( DataUpdateType_t updateType );
	virtual void OnPreDataChanged( DataUpdateType_t type );

	virtual void ReceiveMessage( int classID, bf_read &msg ) ;

	void StartAttackParticleEffect( bool bFlameThrower );
	void StopAttackParticleEffect( bool bFlameThrower  );
	virtual void ItemPostFrame( void );

	void ManageAmmoParticleEffects( void );
	void TurnOffAllEffects( void );

	C_BaseViewModel *GetEffectViewModel( void );
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo );

	bool Simulate( void );

private:

	bool m_bFlameEffects;
	bool m_bFiringSecondary;
	bool m_bFiringPrimary;
	
	int m_iModifierType;
	
	CUtlReference<CNewParticleEffect> m_pHoseEffect;
	CUtlReference<CNewParticleEffect> m_pFireEffect;
	CUtlReference<CNewParticleEffect> m_pTubePrimaryAmmo;
	CUtlReference<CNewParticleEffect> m_pTubeSecondaryAmmo;
	CUtlReference<CNewParticleEffect> m_pTubeSecondaryModifier;

	int	m_iCachedPrimaryAmmo;
	int	m_iCachedSecondaryAmmo;
	bool m_bResetAmmo;
};

STUB_WEAPON_CLASS_IMPLEMENT( weapon_weaponizer, C_WeaponWeaponizer );

IMPLEMENT_CLIENTCLASS_DT( C_WeaponWeaponizer, DT_WeaponWeaponizer, CWeaponWeaponizer )
	RecvPropBool( RECVINFO( m_bFiringSecondary ) ),
	RecvPropBool( RECVINFO( m_bFiringPrimary ) ),
	RecvPropInt( RECVINFO( m_iModifierType ) ),
END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
C_WeaponWeaponizer::C_WeaponWeaponizer( void )
{
	m_bFlameEffects = false;
	m_pHoseEffect = NULL;
	m_pFireEffect = NULL;
	m_pTubePrimaryAmmo = NULL;
	m_pTubeSecondaryAmmo = NULL;
	m_pTubeSecondaryModifier = NULL;
}

C_BaseViewModel *C_WeaponWeaponizer::GetEffectViewModel( void )
{
	// Start the effect on the viewmodel if our owner is the local player
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( C_BasePlayer::IsLocalPlayer( pOwner ) )
	{
		ACTIVE_SPLITSCREEN_PLAYER_GUARD_ENT( pOwner );
		return pOwner->GetViewModel();
	}

	return NULL;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void C_WeaponWeaponizer::OnPreDataChanged( DataUpdateType_t type )
{
	m_iCachedPrimaryAmmo = m_iPrimaryAmmoType;
	m_iCachedSecondaryAmmo = m_iSecondaryAmmoType;

	BaseClass::OnPreDataChanged( type );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool C_WeaponWeaponizer::Simulate( void )
{
	if ( IsEffectActive( EF_NODRAW ) )
	{
		StopAttackParticleEffect( true );
		StopAttackParticleEffect( false );
		TurnOffAllEffects();

		m_bResetAmmo = true;

		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void C_WeaponWeaponizer::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );
	SetNextClientThink( CLIENT_THINK_ALWAYS );
	
	if ( IsEffectActive( EF_NODRAW ) == false )
	{
		if ( m_bFiringPrimary == true )
		{
			if ( m_iPrimaryAmmoType == WEAPONIZER_MATERIAL_TYPE_FIRE )
			{
				StartAttackParticleEffect( true );
			}
		}
		else
		{
			StopAttackParticleEffect( true );
		}

		if ( m_bFiringSecondary == true )
		{
			StartAttackParticleEffect( false );
		}
		else
		{
			StopAttackParticleEffect( false );
		}

		ManageAmmoParticleEffects();
	}
}

bool C_WeaponWeaponizer::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	return BaseClass::Holster( pSwitchingTo );
}

const char *p_szAmmoEffectName[] = 
{
	" ",
	"tube_fire",
	"tube_electricity",
	"tube_matter",
	"tube_matter",
	" ",
	"tube_fuel",
	"tube_water",
};

void C_WeaponWeaponizer::TurnOffAllEffects( void )
{
	if ( m_pTubePrimaryAmmo ) 
	{
		GetEffectViewModel()->ParticleProp()->StopEmission( m_pTubePrimaryAmmo );
		m_pTubePrimaryAmmo = NULL;
	}

	if ( m_pTubeSecondaryAmmo )
	{
		GetEffectViewModel()->ParticleProp()->StopEmission( m_pTubeSecondaryAmmo );
		m_pTubeSecondaryAmmo = NULL;
	}

	if ( m_pTubeSecondaryModifier )
	{
		GetEffectViewModel()->ParticleProp()->StopEmission( m_pTubeSecondaryModifier );
		m_pTubeSecondaryModifier = NULL;
	}
}


void C_WeaponWeaponizer::ManageAmmoParticleEffects( void )
{
	// FIXME: particle effects are broken, disabling for now
	//return;

	if ( m_iPrimaryAmmoType != m_iCachedPrimaryAmmo || m_bResetAmmo == true )
	{
		m_iCachedPrimaryAmmo = m_iPrimaryAmmoType;

		if ( m_pTubePrimaryAmmo ) 
		{
			GetEffectViewModel()->ParticleProp()->StopEmission( m_pTubePrimaryAmmo );
			m_pTubePrimaryAmmo = NULL;
		}

		if ( m_iPrimaryAmmoType > 0 )
		{
			if ( !m_pTubePrimaryAmmo.IsValid() )
			{
				if ( GetEffectViewModel() )
				{
					m_pTubePrimaryAmmo = GetEffectViewModel()->ParticleProp()->Create( p_szAmmoEffectName[m_iPrimaryAmmoType], PATTACH_POINT_FOLLOW, "ltube_pattach_base" );
					if (m_pTubePrimaryAmmo)//
						GetEffectViewModel()->ParticleProp()->AddControlPoint(m_pTubePrimaryAmmo, 1, GetEffectViewModel(), PATTACH_POINT_FOLLOW, "ltube_pattach_end");
					else
						Warning("m_pTubePrimaryAmmo is NULL, this probably means weaponizer is missing a particle effect!!!");
				}
			}
		}
	}

	if ( m_iSecondaryAmmoType != m_iCachedSecondaryAmmo || m_bResetAmmo == true )
	{
		m_iCachedSecondaryAmmo = m_iSecondaryAmmoType;

		if ( m_pTubeSecondaryAmmo )
		{
			GetEffectViewModel()->ParticleProp()->StopEmission( m_pTubeSecondaryAmmo );
			m_pTubeSecondaryAmmo = NULL;
		}

		if ( m_iSecondaryAmmoType > 0 )
		{
			if ( !m_pTubeSecondaryAmmo.IsValid() )
			{
				if ( GetEffectViewModel() )
				{
					
					m_pTubeSecondaryAmmo = GetEffectViewModel()->ParticleProp()->Create( p_szAmmoEffectName[m_iSecondaryAmmoType], PATTACH_POINT_FOLLOW, "rtube_pattach_base" );
					if (m_pTubeSecondaryAmmo)//
					GetEffectViewModel()->ParticleProp()->AddControlPoint( m_pTubeSecondaryAmmo, 1, GetEffectViewModel(), PATTACH_POINT_FOLLOW, "rtube_pattach_end" );
					else
						Warning("m_pTubeSecondaryAmmo is NULL, this probably means weaponizer is missing a particle effect!!!");
				}
			}
		}
	}

	// Modifiers are layered atop other effects.
	if ( m_iModifierType == WEAPONIZER_MODIFIER_EXPLOSIVE )
	{
		if ( !m_pTubeSecondaryModifier.IsValid() )
		{
			m_pTubeSecondaryModifier = GetEffectViewModel()->ParticleProp()->Create( "tube_modifier_explosive", PATTACH_POINT_FOLLOW, "rtube_pattach_base" );
			//GetEffectViewModel()->ParticleProp()->AddControlPoint( m_pTubeSecondaryModifier, 1, GetEffectViewModel(), PATTACH_POINT_FOLLOW, "rtube_pattach_end" );
		}
	}
	if ( m_iModifierType == WEAPONIZER_MODIFIER_FLECHETTE )
	{
		if ( !m_pTubeSecondaryModifier.IsValid() )
		{
			m_pTubeSecondaryModifier = GetEffectViewModel()->ParticleProp()->Create( "tube_flechette", PATTACH_POINT_FOLLOW, "rtube_pattach_base" );
			GetEffectViewModel()->ParticleProp()->AddControlPoint( m_pTubeSecondaryModifier, 1, GetEffectViewModel(), PATTACH_POINT_FOLLOW, "rtube_pattach_end" );
		}
	}
	else if ( m_pTubeSecondaryModifier )
	{
		GetEffectViewModel()->ParticleProp()->StopEmission( m_pTubeSecondaryModifier );
		m_pTubeSecondaryModifier = NULL;
	}

	m_bResetAmmo = false;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_WeaponWeaponizer::ItemPostFrame( void )
{
	BaseClass::ItemPostFrame();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_WeaponWeaponizer::StartAttackParticleEffect( bool bFlameThrower )
{
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	if ( bFlameThrower == true )
	{
		if ( !m_pFireEffect.IsValid() )
		{
			// Start the appropriate particle effect
			const char *pszParticleEffect = "flamethrower";

			if ( GetEffectViewModel() )
			{
				m_pFireEffect = GetEffectViewModel()->ParticleProp()->Create( pszParticleEffect, PATTACH_POINT_FOLLOW, "ltube_muzzle" );
			}
		}
	}
	else
	{
		// Start the appropriate particle effect
		const char *pszParticleEffect = "fuel_hose";

		if ( m_iSecondaryAmmoType == WEAPONIZER_MATERIAL_TYPE_WATER )
		{
			pszParticleEffect = "water_hose";
		}

		if ( GetEffectViewModel() )
		{
				if ( !m_pHoseEffect.IsValid() )
				{
					m_pHoseEffect = GetEffectViewModel()->ParticleProp()->Create( pszParticleEffect, PATTACH_POINT_FOLLOW, "rtube_muzzle" );
				}
		}
	}

	m_bFlameEffects = true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_WeaponWeaponizer::StopAttackParticleEffect( bool bFlameThrower )
{
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	if ( bFlameThrower == true )
	{
		if ( m_pFireEffect )
		{
			if ( GetEffectViewModel() )
			{
				GetEffectViewModel()->ParticleProp()->StopEmission( m_pFireEffect );
				m_pFireEffect = NULL;
				
			}
		}
	}
	else
	{
		if ( m_pHoseEffect )
		{
			if ( GetEffectViewModel() )
			{
				GetEffectViewModel()->ParticleProp()->StopEmission( m_pHoseEffect );
				m_pHoseEffect = NULL;
			}
		}
	}

	m_bFlameEffects = false;
}



void C_WeaponWeaponizer::ReceiveMessage( int classID, bf_read &msg )
{
	C_BasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner )
		return;

	// Is the message for a sub-class?
	if ( classID != GetClientClass()->m_ClassID )
	{
		BaseClass::ReceiveMessage( classID, msg );
		return;
	}

	int messageType = msg.ReadByte();
	switch( messageType )
	{
	default:
		case WEAPONIZER_FX_ELECTRICAL_ZAP:
		{
			// Get our attachment position
			Vector vecStart;
			QAngle vecAngles;
			int iAttachment = LookupAttachment( "ltube_muzzle" );

			GetAttachment( iAttachment, vecStart, vecAngles );

			char szEffectName[256];
			msg.ReadString( szEffectName, sizeof(szEffectName) );

			// Get the final position we'll strike
			Vector vecEndPos;
			msg.ReadBitVec3Coord( vecEndPos );

			if ( GetEffectViewModel() )
			{
				// Place a beam between the two points
				CNewParticleEffect *pEffect = GetEffectViewModel()->ParticleProp()->Create( szEffectName, PATTACH_POINT_FOLLOW, iAttachment );
				if ( pEffect )
				{
					pEffect->SetControlPoint( 0, vecStart );
					pEffect->SetControlPoint( 1, vecEndPos );
				}
			}

			break;
		}
		
		case WEAPONIZER_FX_ABSORB:
			{
				// Get our attachment position
				Vector vecStart;
				QAngle vecAngles;
				char szMuzzle[256];
				msg.ReadString( szMuzzle, sizeof(szMuzzle) );

				int iAttachment = LookupAttachment( szMuzzle );

				GetAttachment( iAttachment, vecStart, vecAngles );

				// Get the final position we'll strike
				Vector vecEndPos;
				msg.ReadBitVec3Coord( vecEndPos );

				if ( GetEffectViewModel() )
				{ 
					char szString[256];
					msg.ReadString( szString, sizeof(szString) );

					// Place a beam between the two points
					CNewParticleEffect *pEffect = GetEffectViewModel()->ParticleProp()->Create( szString, PATTACH_POINT_FOLLOW, iAttachment );
					if ( pEffect )
					{
						pEffect->SetControlPoint( 0, vecStart );
						pEffect->SetControlPoint( 1, vecEndPos );
					}
				}

				break;
			}
	}
}


class C_MetalZapSphere: public C_PhysicsProp
{
	DECLARE_CLASS( C_MetalZapSphere, C_PhysicsProp );
public:

	DECLARE_CLIENTCLASS();

	void ReceiveMessage( int classID, bf_read &msg );
};


IMPLEMENT_CLIENTCLASS_DT( C_MetalZapSphere, DT_MetalZapSphere, CMetalZapSphere )

END_RECV_TABLE()

void C_MetalZapSphere::ReceiveMessage( int classID, bf_read &msg )
{
	// Is the message for a sub-class?
	if ( classID != GetClientClass()->m_ClassID )
	{
		BaseClass::ReceiveMessage( classID, msg );
		return;
	}

	int messageType = msg.ReadByte();
	switch( messageType )
	{
	default:
	case WEAPONIZER_FX_ELECTRICAL_ZAP:
		{
			// Get the final position we'll strike
			Vector vecEndPos;
			msg.ReadBitVec3Coord( vecEndPos );

			// Place a beam between the two points
			CNewParticleEffect *pEffect = ParticleProp()->Create( "electricity_attack", PATTACH_ABSORIGIN_FOLLOW );
			if ( pEffect )
			{
				pEffect->SetControlPoint( 0, GetAbsOrigin() );
				pEffect->SetControlPoint( 1, vecEndPos );
			}
		}
		break;
	}
}

class C_MetalFlameSphere: public C_PhysicsProp
{
	DECLARE_CLASS( C_MetalFlameSphere, C_PhysicsProp );
public:

	DECLARE_CLIENTCLASS();

	void OnDataChanged( DataUpdateType_t updateType );

	bool	m_bActive;
};


IMPLEMENT_CLIENTCLASS_DT( C_MetalFlameSphere, DT_MetalFlameSphere, CMetalFlameSphere )
	RecvPropBool( RECVINFO( m_bActive ) ),
END_RECV_TABLE()

void C_MetalFlameSphere::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	if ( m_bActive == true )
	{
		ParticleProp()->Create( "fusion_flamethrower", PATTACH_ABSORIGIN_FOLLOW );
	}
}


class C_ZapFlak: public C_BaseAnimating
{
	DECLARE_CLASS( C_ZapFlak, C_BaseAnimating );
public:

	void OnDataChanged( DataUpdateType_t updateType );

	Vector m_beamEndPoints[8];
	CUtlReference<CNewParticleEffect> m_pBeamEffect[8];

	bool m_bActive;
	bool m_bWasActive;

	DECLARE_CLIENTCLASS();
};


IMPLEMENT_CLIENTCLASS_DT( C_ZapFlak, DT_ZapFlak, CZapFlak )
	RecvPropVector(RECVINFO(m_beamEndPoints[0])),
	RecvPropVector(RECVINFO(m_beamEndPoints[1])),
	RecvPropVector(RECVINFO(m_beamEndPoints[2])),
	RecvPropVector(RECVINFO(m_beamEndPoints[3])),
	RecvPropVector(RECVINFO(m_beamEndPoints[4])),
	RecvPropVector(RECVINFO(m_beamEndPoints[5])),
	RecvPropVector(RECVINFO(m_beamEndPoints[6])),
	RecvPropVector(RECVINFO(m_beamEndPoints[7])),
	RecvPropBool( RECVINFO( m_bActive )),
END_RECV_TABLE()


void C_ZapFlak::OnDataChanged( DataUpdateType_t updateType )
{
	// HACK: use a network message instead
	if ( m_bActive && !m_bWasActive )
	{	
		m_bWasActive = true;
	
		// HACK: share NUM_BEAMS
		// Place a beam between the two points
		for ( int i = 0; i < 8; i++ )
		{
			CNewParticleEffect *pEffect = ParticleProp()->Create( "flaktricity", PATTACH_ABSORIGIN_FOLLOW );
			if ( pEffect )
			{
				pEffect->SetControlPoint( 0, GetAbsOrigin() );
				pEffect->SetControlPoint( 1, m_beamEndPoints[i] );
			}
			
			m_pBeamEffect[i] = pEffect;
		}
	}
	else if ( m_bActive )
	{
		for ( int i = 0; i < 8; i++ )
		{
			m_pBeamEffect[i]->SetControlPoint( 1, m_beamEndPoints[i] );
		}
	}
	
	BaseClass::OnDataChanged( updateType );
}


class C_ZapFlakBeam: public C_BaseEntity
{
	DECLARE_CLASS( C_ZapFlakBeam, C_BaseEntity );
public:

	void Spawn();
	void OnDataChanged( DataUpdateType_t updateType );

	Vector m_beamEndPoints[8];
	CUtlReference<CNewParticleEffect> m_pBeamEffect[8];

	DECLARE_CLIENTCLASS();

	int m_nEntCount;
	CUtlVector<EHANDLE> m_hEnts;
	CUtlVector<int> m_nFromIndex;
};


IMPLEMENT_CLIENTCLASS_DT( C_ZapFlakBeam, DT_ZapFlakBeam, CZapFlakBeam )
	RecvPropUtlVector( 
				RECVINFO_UTLVECTOR( m_hEnts ), 
				ZAP_FLAK_MAX_ENTS,
				RecvPropEHandle( NULL, 0, 0 ) ),
	RecvPropUtlVector( 
				RECVINFO_UTLVECTOR( m_nFromIndex ), 
				ZAP_FLAK_MAX_ENTS,
				RecvPropInt( NULL, 0, sizeof(int) ) ),
END_RECV_TABLE()


void C_ZapFlakBeam::Spawn()
{
	BaseClass::Spawn();
}


void C_ZapFlakBeam::OnDataChanged( DataUpdateType_t updateType )
{
	for ( int i = m_nEntCount; i < m_hEnts.Count(); i++ )
	{
		CNewParticleEffect *pEffect = ParticleProp()->Create( "flaktricity", PATTACH_ABSORIGIN_FOLLOW );
		if ( pEffect )
		{
			pEffect->SetControlPointEntity( 0, m_hEnts.Element( i ) );
			pEffect->SetControlPointEntity( 1, m_hEnts.Element( m_nFromIndex.Element( i ) ) );
		}

		m_pBeamEffect[i] = pEffect;
	
		m_nEntCount = m_hEnts.Count();
	}

	BaseClass::OnDataChanged( updateType );
}

