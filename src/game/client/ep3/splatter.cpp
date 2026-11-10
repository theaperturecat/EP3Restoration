// Copyright 2005 Turtle Rock Studios, Inc.

#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "hud_numericdisplay.h"
#include "iclientmode.h"
#include "C_BasePlayer.h"
#include "vguimatsurface/IMatSystemSurface.h"
#include "materialsystem/IMaterial.h"
#include "materialsystem/IMesh.h"
#include "materialsystem/imaterialvar.h"

#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include <KeyValues.h>
#include <vgui_controls/AnimationController.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const int NUM_SPLATTER_TEXTURES = 3;
const int NUM_VOMIT_TEXTURES = 2;

//-----------------------------------------------------------------------------
struct QueuedSplatter
{
	int count;
	float startTime;
};


//-----------------------------------------------------------------------------
struct Splatter
{
	int textureID;
	int x, y, w, h;
	float startTime;
};

ConVar ZombieSplatterAlpha( "z_splatteralpha", "200", FCVAR_REPLICATED );
ConVar ZombieSplatterHold( "z_splatterhold", "3", FCVAR_REPLICATED );
ConVar ZombieSplatterFade( "z_splatterfade", "1", FCVAR_REPLICATED );
ConVar ZombieVomitSlideRate( "z_vomit_slide_rate", "0.1", FCVAR_CHEAT | FCVAR_REPLICATED, "Percentage of screen height per second" );
ConVar ZombieVomitSlideMult( "z_vomit_slide_mult", "0.5", FCVAR_CHEAT | FCVAR_REPLICATED, "Multiplier for second texture slide rate" );
ConVar ZombieVomitFadeStart( "z_vomit_fade_start", "0.2", FCVAR_CHEAT | FCVAR_REPLICATED, "When the vomit starts to fade away" );
ConVar ZombieVomitFadeDuration( "z_vomit_fade_duration", "0.5", FCVAR_CHEAT | FCVAR_REPLICATED, "How long the fade takes" );
ConVar ZombieSplatterMinSize( "z_splatter_min_size", "350" );
ConVar ZombieSplatterMaxSize( "z_splatter_max_size", "350" );
ConVar ZombieSplatterDelay( "z_splatter_delay", "0.25" );
ConVar ZombieSplatterLimit( "z_splatter_limit", "10", FCVAR_NONE, "Max number of splatters on-screen at once" );


//-----------------------------------------------------------------------------
// Purpose: Draws the blood splatters
//-----------------------------------------------------------------------------
class CHudBlood : public vgui::Panel, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CHudBlood, vgui::Panel );

public:
	CHudBlood( const char *pElementName );
	virtual void Init( void );
	virtual void LevelInit( void );

	void MsgFunc_Splatter( bf_read &msg );
	void MsgFunc_AcidSplash( bf_read &msg );
	void MsgFunc_SplatterClear( bf_read &msg );

protected:
	virtual void ApplySchemeSettings(vgui::IScheme *scheme);
	virtual void Paint( void );

private:
	CHudTexture * m_bloodTextures[ NUM_SPLATTER_TEXTURES ];
	CHudTexture * m_vomitTextures[ NUM_VOMIT_TEXTURES ];

	void UpdateQueuedSplatter( void );
	CUtlVector< QueuedSplatter > m_queuedSplatter;
	CUtlVector< Splatter > m_splatter;
	int m_lastSplatterTextureID;
	float m_flAcidSplashStartTime;
	float m_flAcidSplashDuration;
};

DECLARE_HUDELEMENT_DEPTH( CHudBlood, 49 );
DECLARE_HUD_MESSAGE( CHudBlood, Splatter );
DECLARE_HUD_MESSAGE( CHudBlood, AcidSplash );
DECLARE_HUD_MESSAGE( CHudBlood, SplatterClear );

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudBlood::CHudBlood( const char *pElementName ) : CHudElement(pElementName), BaseClass(NULL, "HudBlood")
{
	vgui::Panel *pParent = GetClientMode()->GetViewport();
	SetParent( pParent );
	
	SetHiddenBits( 0 );

	int i;
	for ( i=0; i<NUM_SPLATTER_TEXTURES; ++i )
	{
		m_bloodTextures[i] = NULL;
	}

	for ( i=0; i<NUM_SPLATTER_TEXTURES; ++i )
	{
		m_bloodTextures[i] = NULL;
	}
}

//-----------------------------------------------------------------------------
void CHudBlood::Init( void )
{
	HOOK_HUD_MESSAGE( CHudBlood, Splatter );
	HOOK_HUD_MESSAGE( CHudBlood, AcidSplash );
	HOOK_HUD_MESSAGE( CHudBlood, SplatterClear );
}

//-----------------------------------------------------------------------------
void CHudBlood::LevelInit( void )
{
	m_queuedSplatter.RemoveAll();
	m_splatter.RemoveAll();
	m_lastSplatterTextureID = -1;
	m_flAcidSplashStartTime = 0.0f;
	m_flAcidSplashDuration = 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: sets scheme colors
//-----------------------------------------------------------------------------
void CHudBlood::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings(scheme);

	SetPaintBackgroundEnabled(false);
	SetPaintBorderEnabled(false);

	int screenWide, screenTall;
	surface()->GetScreenSize(screenWide, screenTall);
	SetBounds(0, 0, screenWide, screenTall);
}

//-----------------------------------------------------------------------------
void CHudBlood::MsgFunc_Splatter( bf_read &msg )
{
	if ( UTIL_IsLowViolence() )
		return;

	int numSplatters = msg.ReadByte();

	QueuedSplatter splatter = { numSplatters, gpGlobals->curtime + ZombieSplatterDelay.GetFloat() };
	m_queuedSplatter.AddToTail( splatter );
}

void CHudBlood::MsgFunc_AcidSplash( bf_read &msg )
{
	m_flAcidSplashStartTime = msg.ReadFloat();
	m_flAcidSplashDuration = msg.ReadFloat();
}

//-----------------------------------------------------------------------------
void CHudBlood::UpdateQueuedSplatter( void )
{
	if ( !m_queuedSplatter.Count() )
		return;

	const QueuedSplatter &splatter = m_queuedSplatter[0];
	if ( splatter.startTime > gpGlobals->curtime )
		return;

	int numSplatters = splatter.count;
	m_queuedSplatter.Remove( 0 );

	int screenWide, screenTall;
	surface()->GetScreenSize(screenWide, screenTall);

	for ( int i=0; i<numSplatters; ++i )
	{
		// Keep the damage (mapped to screen size) to 400 (640 covers the screen)
		int damage = RandomInt( ZombieSplatterMinSize.GetInt(), ZombieSplatterMaxSize.GetInt() );

		Splatter s;
		s.w = s.h = scheme()->GetProportionalScaledValue( damage );

		// Use sine to bias the distribution to the edges of the screen
		s.x = ( 1.0f - sinf( RandomInt( 0.0f, M_PI ) ) ) * screenWide - s.w/2;
		s.y = ( 1.0f - sinf( RandomInt( 0.0f, M_PI ) ) ) * screenTall - s.h/2;

		s.startTime = gpGlobals->curtime;
		s.textureID = RandomInt( 0, NUM_SPLATTER_TEXTURES-1 );
		while ( s.textureID == m_lastSplatterTextureID )
		{
			s.textureID = RandomInt( 0, NUM_SPLATTER_TEXTURES-1 );
		}
		m_lastSplatterTextureID = s.textureID;

		m_splatter.AddToTail( s );
	}

	while ( m_splatter.Count() > ZombieSplatterLimit.GetInt() )
	{
		m_splatter.Remove( 0 );
	}

	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if (player)
	{
		player->EmitSound( "Blood.Splat" );
	}
}

//-----------------------------------------------------------------------------
void CHudBlood::MsgFunc_SplatterClear( bf_read &msg )
{
	m_queuedSplatter.RemoveAll();
	m_splatter.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: draws the blood effects
//-----------------------------------------------------------------------------
void CHudBlood::Paint( void )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	
	if ( player == NULL )
	{
		return;
	}

	UpdateQueuedSplatter();

	int i;
	for ( i=0; i<NUM_SPLATTER_TEXTURES; ++i )
	{
		if ( !m_bloodTextures[i] )
		{
			m_bloodTextures[i] = HudIcons().GetIcon( VarArgs( "BloodSplatter%2.2d", i+1 ) );
		}
	}

	for ( i=0; i<NUM_VOMIT_TEXTURES; ++i )
	{
		if ( !m_vomitTextures[i] )
		{
			m_vomitTextures[i] = HudIcons().GetIcon( VarArgs( "Vomit%2.2d", i+1 ) );
		}
	}

	float now = gpGlobals->curtime;

	for ( i=m_splatter.Count()-1; i>=0; --i )
	{
		const Splatter& s = m_splatter[i];
		if ( !m_bloodTextures[s.textureID] )
		{
			continue;
		}

		if ( now > s.startTime + ZombieSplatterHold.GetFloat() + ZombieSplatterFade.GetFloat() )
		{
			m_splatter.Remove( i );
			continue;
		}

		int alpha = ZombieSplatterAlpha.GetInt();
		float fadeStart = s.startTime + ZombieSplatterHold.GetFloat();
		if ( now > fadeStart )
		{
			// fade out
			float fadeEnd = fadeStart + ZombieSplatterFade.GetFloat();
			float mult = 1.0f - (now - fadeStart) / ( fadeEnd - fadeStart );
			alpha = (int)((float)alpha * mult);
		}
		alpha = clamp( alpha, 0, 255 );

		vgui::surface()->DrawSetColor( 255, 255, 255, alpha );
		vgui::surface()->DrawSetTexture( m_bloodTextures[s.textureID]->textureId );
		vgui::surface()->DrawTexturedRect( s.x, s.y, s.x + s.w, s.y + s.h );
	}

	bool doVomit = true;
	float vomitStart = m_flAcidSplashStartTime;
	float vomitFadeStart = m_flAcidSplashStartTime + ZombieVomitFadeStart.GetFloat();

	if ( vomitStart == 0.0f )
	{
		doVomit = false;
	}

	float fadeStart = vomitFadeStart;
	float fadeEnd = fadeStart + ZombieVomitFadeDuration.GetFloat();
	if ( now > fadeEnd )
	{
		vomitStart = 0.0f;
		doVomit = false;
	}

	int screenWide, screenTall;
	surface()->GetScreenSize(screenWide, screenTall);

	if( doVomit )
	{
		for ( i=0; i<NUM_VOMIT_TEXTURES; ++i )
		{

			float slideMult = powf( ZombieVomitSlideMult.GetFloat(), i );
			float slideRate = ZombieVomitSlideRate.GetFloat() * slideMult;

			int alpha = 255;
			if ( now > fadeStart )
			{
				// fade out
				float mult = 1.0f - (now - fadeStart) / ( fadeEnd - fadeStart );
				alpha = (int)((float)alpha * mult);
			}
			alpha = clamp( alpha, 0, 255 );

			int w = screenWide;
			int h = screenWide * 2;
			int x = 0;
			int y = screenTall - h;

			y += screenTall * (now-vomitStart) * slideRate;
			while ( y > screenTall )
			{
				// we're completely below the screen, so move up
				y -= h;
			}

			vgui::surface()->DrawSetColor( 255, 255, 255, alpha );
			vgui::surface()->DrawSetTexture( m_vomitTextures[i]->textureId );
			vgui::surface()->DrawTexturedRect( x, y, x + w, y + h );

			if ( y > 0 )
			{
				// we started drawing below the top of the screen, so draw a second pass above to fill in.
				// this assumes wraparound textures.
				while ( y > 0 )
				{
					y -= h;
				}
				vgui::surface()->DrawTexturedRect( x, y, x + w, y + h );
			}
		}
	}
}
