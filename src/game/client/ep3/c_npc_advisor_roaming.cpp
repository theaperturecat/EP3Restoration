//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "c_basehelicopter.h"
#include "fx_impact.h"
#include "ieffects.h"
#include "simple_keys.h"
#include "fx_envelope.h"
#include "fx_line.h"
#include "iefx.h"
#include "dlight.h"
#include "c_sprite.h"
#include "precache_register.h"
#include <bitbuf.h>
#include "npc_advisor_roaming_shareddefs.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


inline C_BaseEntity *IndexToEntity( int eindex )
{
	return ClientEntityList().GetBaseEntityFromHandle(ClientEntityList().EntIndexToHandle(eindex));
}



#define	ADVISOR_ROAMING_FX_BIG_SHOT_TIME			3.0f

PRECACHE_REGISTER_BEGIN( GLOBAL, PrecacheAdvisorRoamingFX )
PRECACHE( MATERIAL, "sprites/bluelaser1" )
PRECACHE_REGISTER_END()

//-----------------------------------------------------------------------------
// Big belly shot FX
//-----------------------------------------------------------------------------

class C_AdvisorRoamingFX : public C_EnvelopeFX
{
public:
	typedef C_EnvelopeFX	BaseClass;

	C_AdvisorRoamingFX();
	void			Update( C_BaseEntity *pOwner, const Vector &targetPos );

	// Returns the bounds relative to the origin (render bounds)
	virtual void	GetRenderBounds( Vector& mins, Vector& maxs )
	{
		ClearBounds( mins, maxs );
		AddPointToBounds( m_worldPosition, mins, maxs );
		AddPointToBounds( m_targetPosition, mins, maxs );
		mins -= GetRenderOrigin();
		maxs -= GetRenderOrigin();
	}

	virtual int	DrawModel( int flags );

	C_BaseEntity			*m_pOwner;
	Vector					m_targetPosition;
	Vector					m_beamEndPosition;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_AdvisorRoamingFX::C_AdvisorRoamingFX( void )
{
	m_pOwner = NULL;
}

enum 
{
	ADVISOR_ROAMING_FX_WARP_SCALE = 0,
	ADVISOR_ROAMING_FX_DARKNESS,
	ADVISOR_ROAMING_FX_FLARE_COLOR,
	ADVISOR_ROAMING_FX_FLARE_SIZE,

	ADVISOR_ROAMING_FX_NARROW_BEAM_COLOR,
	ADVISOR_ROAMING_FX_NARROW_BEAM_SIZE,
	
	ADVISOR_ROAMING_FX_WIDE_BEAM_COLOR,
	ADVISOR_ROAMING_FX_WIDE_BEAM_SIZE,

	ADVISOR_ROAMING_FX_AFTERGLOW_COLOR,

	ADVISOR_ROAMING_FX_WIDE_BEAM_LENGTH,

	// must be last
	ADVISOR_ROAMING_FX_PARAMETERS,
};

class CAdvisorRoamingFXEnvelope
{
public:
	CAdvisorRoamingFXEnvelope();

	void AddKey( int parameterIndex, const CSimpleKeyInterp &key )
	{
		Assert( parameterIndex >= 0 && parameterIndex < ADVISOR_ROAMING_FX_PARAMETERS );

		if ( parameterIndex >= 0 && parameterIndex < ADVISOR_ROAMING_FX_PARAMETERS )
		{
			m_parameters[parameterIndex].Insert( key );
		}

	}

	CSimpleKeyList		m_parameters[ADVISOR_ROAMING_FX_PARAMETERS];
};

// NOTE: Beam widths are half-widths or radii, so this is a beam that represents a cylinder with 2" radius
const float NARROW_BEAM_WIDTH = 32;
const float WIDE_BEAM_WIDTH = 2;
const float FLARE_SIZE = 128;
const float	DARK_SIZE = 16;
const float AFTERGLOW_SIZE = 64;

CAdvisorRoamingFXEnvelope::CAdvisorRoamingFXEnvelope()
{
	// Glow flare
	AddKey( ADVISOR_ROAMING_FX_FLARE_COLOR, CSimpleKeyInterp( 0.0, KEY_LINEAR, 0 ) );
	AddKey( ADVISOR_ROAMING_FX_FLARE_COLOR, CSimpleKeyInterp( 0.5, KEY_SPLINE, 1 ) );
	AddKey( ADVISOR_ROAMING_FX_FLARE_COLOR, CSimpleKeyInterp( 2.9, KEY_LINEAR, 1 ) );
	AddKey( ADVISOR_ROAMING_FX_FLARE_COLOR, CSimpleKeyInterp( 3.2, KEY_DECELERATE, 0 ) );

	AddKey( ADVISOR_ROAMING_FX_FLARE_SIZE, CSimpleKeyInterp( 0.0, KEY_LINEAR, 0 ) );
	AddKey( ADVISOR_ROAMING_FX_FLARE_SIZE, CSimpleKeyInterp( 2.9, KEY_ACCELERATE, 1 ) );
	AddKey( ADVISOR_ROAMING_FX_FLARE_SIZE, CSimpleKeyInterp( 5.0, KEY_LINEAR, 1 ) );

	// Ground beam
	AddKey( ADVISOR_ROAMING_FX_NARROW_BEAM_COLOR, CSimpleKeyInterp( 0.0, KEY_LINEAR, 0.0 ) );
	AddKey( ADVISOR_ROAMING_FX_NARROW_BEAM_COLOR, CSimpleKeyInterp( 2.5, KEY_SPLINE, 1.0 ) );
	AddKey( ADVISOR_ROAMING_FX_NARROW_BEAM_COLOR, CSimpleKeyInterp( 3.0, KEY_LINEAR, 1 ) );
	AddKey( ADVISOR_ROAMING_FX_NARROW_BEAM_COLOR, CSimpleKeyInterp( 3.2, KEY_ACCELERATE, 0 ) );

	AddKey( ADVISOR_ROAMING_FX_NARROW_BEAM_SIZE, CSimpleKeyInterp( 0.0, KEY_LINEAR, 0 ) );
	AddKey( ADVISOR_ROAMING_FX_NARROW_BEAM_SIZE, CSimpleKeyInterp( 2.5, KEY_SPLINE, 0.25 ) );
	AddKey( ADVISOR_ROAMING_FX_NARROW_BEAM_SIZE, CSimpleKeyInterp( 2.8, KEY_ACCELERATE, 1 ) );
	AddKey( ADVISOR_ROAMING_FX_NARROW_BEAM_SIZE, CSimpleKeyInterp( 3.0, KEY_ACCELERATE, 4 ) );
	AddKey( ADVISOR_ROAMING_FX_NARROW_BEAM_SIZE, CSimpleKeyInterp( 3.2, KEY_DECELERATE, 0 ) );

	// Glow color on the ship
	AddKey( ADVISOR_ROAMING_FX_AFTERGLOW_COLOR, CSimpleKeyInterp( 0.0, KEY_LINEAR, 0 ) );
	AddKey( ADVISOR_ROAMING_FX_AFTERGLOW_COLOR, CSimpleKeyInterp( 3.0, KEY_LINEAR, 1 ) );
	AddKey( ADVISOR_ROAMING_FX_AFTERGLOW_COLOR, CSimpleKeyInterp( 5.0, KEY_SPLINE, 0 ) );
}

CAdvisorRoamingFXEnvelope g_AdvisorRoamingCannonEnvelope;

static void ScaleColor( color32 &out, const color32 &in, float scale )
{
	out.r = (byte)(int)((float)in.r * scale);
	out.g = (byte)(int)((float)in.g * scale);
	out.b = (byte)(int)((float)in.b * scale);
	out.a = (byte)(int)((float)in.a * scale);
}

static void DrawSpriteTangentSpace( const Vector &vecOrigin, float flWidth, float flHeight, color32 color )
{
	unsigned char pColor[4] = { color.r, color.g, color.b, color.a };

	// Generate half-widths
	flWidth *= 0.5f;
	flHeight *= 0.5f;

	// Compute direction vectors for the sprite
	Vector fwd, right( 1, 0, 0 ), up( 0, 1, 0 );
	VectorSubtract( CurrentViewOrigin(), vecOrigin, fwd );
	float flDist = VectorNormalize( fwd );
	if (flDist >= 1e-3)
	{
		CrossProduct( CurrentViewUp(), fwd, right );
		flDist = VectorNormalize( right );
		if (flDist >= 1e-3)
		{
			CrossProduct( fwd, right, up );
		}
		else
		{
			// In this case, fwd == g_vecVUp, it's right above or 
			// below us in screen space
			CrossProduct( fwd, CurrentViewRight(), up );
			VectorNormalize( up );
			CrossProduct( up, fwd, right );
		}
	}

	Vector left = -right;
	Vector down = -up;
	Vector back = -fwd;

	CMatRenderContextPtr pRenderContext( materials );
	
	CMeshBuilder meshBuilder;
	Vector point;
	IMesh* pMesh = pRenderContext->GetDynamicMesh( );

	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

	meshBuilder.Color4ubv (pColor);
	meshBuilder.TexCoord2f (0, 0, 1);
	VectorMA (vecOrigin, -flHeight, up, point);
	VectorMA (point, -flWidth, right, point);
	meshBuilder.TangentS3fv( left.Base() );
	meshBuilder.TangentT3fv( down.Base() );
	meshBuilder.Normal3fv( back.Base() );
	meshBuilder.Position3fv (point.Base());
	meshBuilder.AdvanceVertex();

	meshBuilder.Color4ubv (pColor);
	meshBuilder.TexCoord2f (0, 0, 0);
	VectorMA (vecOrigin, flHeight, up, point);
	VectorMA (point, -flWidth, right, point);
	meshBuilder.TangentS3fv( left.Base() );
	meshBuilder.TangentT3fv( down.Base() );
	meshBuilder.Normal3fv( back.Base() );
	meshBuilder.Position3fv (point.Base());
	meshBuilder.AdvanceVertex();

	meshBuilder.Color4ubv (pColor);
	meshBuilder.TexCoord2f (0, 1, 0);
	VectorMA (vecOrigin, flHeight, up, point);
	VectorMA (point, flWidth, right, point);
	meshBuilder.TangentS3fv( left.Base() );
	meshBuilder.TangentT3fv( down.Base() );
	meshBuilder.Normal3fv( back.Base() );
	meshBuilder.Position3fv (point.Base());
	meshBuilder.AdvanceVertex();

	meshBuilder.Color4ubv (pColor);
	meshBuilder.TexCoord2f (0, 1, 1);
	VectorMA (vecOrigin, -flHeight, up, point);
	VectorMA (point, flWidth, right, point);
	meshBuilder.TangentS3fv( left.Base() );
	meshBuilder.TangentT3fv( down.Base() );
	meshBuilder.Normal3fv( back.Base() );
	meshBuilder.Position3fv (point.Base());
	meshBuilder.AdvanceVertex();
	
	meshBuilder.End();
	pMesh->Draw();
}


void AdvisorRoaming_DrawSprite( const Vector &vecOrigin, float size, const color32 &color, bool glow )
{
	if ( glow )
	{
		pixelvis_queryparams_t params;
		params.Init( vecOrigin );
		if ( PixelVisibility_FractionVisible( params, NULL ) <= 0.0f )
			return;
	}

	DrawSpriteTangentSpace( vecOrigin, size, size, color );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : int - 
//-----------------------------------------------------------------------------
int	C_AdvisorRoamingFX::DrawModel( int )
{
	static color32 white = {255,255,255,255};
	Vector params[ADVISOR_ROAMING_FX_PARAMETERS];
	bool hasParam[ADVISOR_ROAMING_FX_PARAMETERS];

	if ( !m_active )
		return 1;

	C_BaseEntity *ent = cl_entitylist->GetEnt( m_entityIndex );
	if ( ent )
	{
		QAngle angles;
		ent->GetAttachment( m_attachment, m_worldPosition, angles );
	}

	Vector test;
	m_t += gpGlobals->frametime;
	if ( m_tMax > 0 )
	{
		m_t = clamp( m_t, 0, m_tMax );
		m_beamEndPosition = m_worldPosition;
	}
	float t = m_t;

	bool hasAny = false;
	memset( hasParam, 0, sizeof(hasParam) );
	for ( int i = 0; i < ADVISOR_ROAMING_FX_PARAMETERS; i++ )
	{
		hasParam[i] = g_AdvisorRoamingCannonEnvelope.m_parameters[i].Interp( params[i], t );
		hasAny = hasAny || hasParam[i];
	}

	// draw the narrow beam
	if ( hasParam[ADVISOR_ROAMING_FX_NARROW_BEAM_COLOR] && hasParam[ADVISOR_ROAMING_FX_NARROW_BEAM_SIZE] )
	{
		IMaterial *pMat = materials->FindMaterial( "sprites/bluelaser1", TEXTURE_GROUP_CLIENT_EFFECTS );
		float width = NARROW_BEAM_WIDTH * params[ADVISOR_ROAMING_FX_NARROW_BEAM_SIZE].x;
		color32 color;
		float bright = params[ADVISOR_ROAMING_FX_NARROW_BEAM_COLOR].x;
		ScaleColor( color, white, bright );

		//AdvisorRoaming_DrawLine( m_beamEndPosition, m_targetPosition, width, pMat, color );
		FX_DrawLine( m_beamEndPosition, m_targetPosition, width, pMat, color );
	}

	// glowy blue flare sprite
	if ( hasParam[ADVISOR_ROAMING_FX_FLARE_COLOR] && hasParam[ADVISOR_ROAMING_FX_FLARE_SIZE] )
	{
		IMaterial *pMat = materials->FindMaterial( "effects/blueblackflash", TEXTURE_GROUP_CLIENT_EFFECTS );
		float size = FLARE_SIZE * params[ADVISOR_ROAMING_FX_FLARE_SIZE].x;
		color32 color;
		float bright = params[ADVISOR_ROAMING_FX_FLARE_COLOR].x;
		ScaleColor( color, white, bright );
		color.a = (int)(255 * params[ADVISOR_ROAMING_FX_DARKNESS].x);
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->Bind( pMat, (IClientRenderable*)this );
		AdvisorRoaming_DrawSprite( m_worldPosition, size, color, true );
	}

	if ( hasParam[ADVISOR_ROAMING_FX_AFTERGLOW_COLOR] )
	{
		// Muzzle effect
		dlight_t *dl = effects->CL_AllocDlight( m_entityIndex );
		dl->origin = m_worldPosition;
		dl->color.r = 40*params[ADVISOR_ROAMING_FX_AFTERGLOW_COLOR].x;
		dl->color.g = 60*params[ADVISOR_ROAMING_FX_AFTERGLOW_COLOR].x;
		dl->color.b = 255*params[ADVISOR_ROAMING_FX_AFTERGLOW_COLOR].x;
		dl->color.exponent = 5;
		dl->radius = 128.0f;
		dl->die = gpGlobals->curtime + 0.001;
	}

	if ( m_t >= 4.0 && !hasAny )
	{
		EffectShutdown();
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pOwner - 
//			&targetPos - 
//-----------------------------------------------------------------------------
void C_AdvisorRoamingFX::Update( C_BaseEntity *pOwner, const Vector &targetPos )
{
	BaseClass::Update();

	m_pOwner = pOwner;
	
	if ( m_active )
	{
		m_targetPosition = targetPos;
	}
}

//-----------------------------------------------------------------------------
// AdvisorRoaming
//-----------------------------------------------------------------------------

class C_CombineAdvisorRoaming : public C_BaseHelicopter
{
	DECLARE_CLASS( C_CombineAdvisorRoaming, C_BaseHelicopter );
public:
	DECLARE_CLIENTCLASS();

	C_CombineAdvisorRoaming( void ) {}
	
	virtual ~C_CombineAdvisorRoaming( void )
	{
		m_cannonFX.EffectShutdown();
	}

	C_AdvisorRoamingFX	m_cannonFX;
	Vector		m_vecHitPos;

	//-----------------------------------------------------------------------------
	// Purpose: 
	// Input  : length - 
	//			*data - 
	// Output : 	void
	//-----------------------------------------------------------------------------
	void ReceiveMessage( int classID, bf_read &msg )
	{
		if ( classID != GetClientClass()->m_ClassID )
		{
			// message is for subclass
			BaseClass::ReceiveMessage( classID, msg );
			return;
		}

		int messageType = msg.ReadByte();
		switch( messageType )
		{
		case ADVISOR_ROAMING_MSG_STREAKS:
			{
				Vector	pos;
				msg.ReadBitVec3Coord( pos );
				m_cannonFX.SetRenderOrigin( pos );
				m_cannonFX.EffectInit( entindex(), LookupAttachment( "BellyGun" ) );
				m_cannonFX.LimitTime( ADVISOR_ROAMING_FX_BIG_SHOT_TIME );
			}
			break;

		case ADVISOR_ROAMING_MSG_BIG_SHOT:
			{
				Vector tmp;
				msg.ReadBitVec3Coord( tmp );
				m_cannonFX.SetTime( ADVISOR_ROAMING_FX_BIG_SHOT_TIME );
				m_cannonFX.LimitTime( 0 );
			}
			break;

		case ADVISOR_ROAMING_MSG_HIGHTLIGHT_ON:
			{
				int eidx = msg.ReadLong();
				HighlightOn( IndexToEntity( eidx ) );
			}
			break;

		case ADVISOR_ROAMING_MSG_HIGHTLIGHT_OFF:
			{
				HighlightOff();
			}
			break;

		case ADVISOR_ROAMING_MSG_HIGHTLIGHT_PULL_ON:
			{
				int eidx = msg.ReadLong();
				HighlightPullOn( IndexToEntity( eidx ) );
			}
			break;

		case ADVISOR_ROAMING_MSG_HIGHTLIGHT_PULL_OFF:
			{
				HighlightPullOff();
			}
			break;

// 		case ADVISOR_ROAMING_MSG_DEAD:
// 			{
// 				m_cannonFX.EffectShutdown();
// 			}
// 			break;
		}
	}

	void HighlightOn( C_BaseEntity *pEntity )
	{
		if ( CNewParticleEffect *pEffect = ParticleProp()->Create( "Advisor_Psychic_Beam", PATTACH_ABSORIGIN_FOLLOW ) )
		{
			ParticleProp()->AddControlPoint( pEffect, 1, pEntity, PATTACH_ABSORIGIN_FOLLOW );
		}
	}

	void HighlightOff()
	{
		ParticleProp()->StopParticlesNamed( "Advisor_Psychic_Beam" );
	}

	void HighlightPullOn( C_BaseEntity *pEntity )
	{
		if ( CNewParticleEffect *pEffect = ParticleProp()->Create( "Advisor_Pull_Beam", PATTACH_ABSORIGIN_FOLLOW ) )
		{
			ParticleProp()->AddControlPoint( pEffect, 1, pEntity, PATTACH_ABSORIGIN_FOLLOW );
		}
	}

	void HighlightPullOff()
	{
		ParticleProp()->StopParticlesNamed( "Advisor_Pull_Beam" );
	}

	void OnDataChanged( DataUpdateType_t updateType )
	{
		BaseClass::OnDataChanged( updateType );

		m_cannonFX.Update( this, m_vecHitPos );
	}

	/*virtual RenderGroup_t GetRenderGroup()
	{
		if ( hl2_episodic.GetBool() == true )
		{
			return RENDER_GROUP_TWOPASS;
		}
		else
		{
			return BaseClass::GetRenderGroup();
		}
	}*/

private:
	C_CombineAdvisorRoaming( const C_CombineAdvisorRoaming & ) {}
};

IMPLEMENT_CLIENTCLASS_DT( C_CombineAdvisorRoaming, DT_CombineAdvisorRoaming, CNPC_CombineAdvisorRoaming )
	RecvPropVector(RECVINFO(m_vecHitPos)),
END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Purpose: Handle advisor_roaming impacts
//-----------------------------------------------------------------------------
void ImpactAdvisorRoamingCallback( const CEffectData &data )
{
	trace_t tr;
	Vector vecOrigin, vecStart, vecShotDir;
	int iMaterial, iDamageType, iHitbox;
	short nSurfaceProp;
	C_BaseEntity *pEntity = ParseImpactData( data, &vecOrigin, &vecStart, &vecShotDir, nSurfaceProp, iMaterial, iDamageType, iHitbox );

	if ( !pEntity )
		return;

	// If we hit, perform our custom effects and play the sound
	if ( Impact( vecOrigin, vecStart, iMaterial, iDamageType, iHitbox, pEntity, tr ) )
	{
		// Check for custom effects based on the Decal index
		PerformCustomEffects( vecOrigin, tr, vecShotDir, iMaterial, 3 );
	}

	PlayImpactSound( pEntity, tr, vecOrigin, nSurfaceProp );
}

DECLARE_CLIENT_EFFECT( ImpactAdvisorRoaming, ImpactAdvisorRoamingCallback );

