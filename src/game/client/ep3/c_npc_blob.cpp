//========= Copyright © 1996-2007, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "dt_utlvector_recv.h"
#include "../game/shared/debugoverlay_shared.h"
#include "../public/mathlib/mathlib.h"
#include "view_scene.h"
#include "materialsystem/imaterialvar.h"
#include "c_npc_blob.h"
#include "model_types.h"
#include "functionproxy.h"
#include "c_npc_blob_tiny.h"
#include "proxyentity.h"
#include "c_surfacerender.h"

#include "blob_networkbypass.h"

#include "imaterialproxydict.h"
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


static ConVar cl_blob_draw_armature( "blob_draw_armature", "2", FCVAR_ARCHIVE, "Switch armature render modes (0..2)" );
static ConVar cl_blob_arm_tess_factor( "blob_arm_tess_factor", "4", FCVAR_ARCHIVE, "Tesselation factor for bezier armature" );
static ConVar cl_blob_arm_bend_factor( "blob_arm_bend_factor", "0.33", FCVAR_ARCHIVE, "Curviness for bezier armature" );
static ConVar cl_blob_draw_brain( "blob_draw_brain", "1", FCVAR_ARCHIVE, "Enable/disable brain rendering" );
static ConVar cl_blob_draw_backfaces( "blob_draw_backfaces", "0", FCVAR_ARCHIVE, "Enable disable blob back rendering" );
static ConVar cl_blob_draw_frontfaces( "blob_draw_frontfaces", "1", FCVAR_ARCHIVE, "Enable disable blob surface rendering" );
static ConVar cl_blob_arm_extend( "blob_arm_extend", "1.3", FCVAR_ARCHIVE, "Extend arms towards surface" );
static ConVar cl_blob_arm_pulse_speed( "blob_arm_pulse_speed", "2.0", FCVAR_ARCHIVE, "Speed of arm pulses" );
static ConVar cl_blob_arm_pulse_frequency( "blob_arm_pulse_frequency", "0.3", FCVAR_ARCHIVE, "Frequency of arm pulses" );

//-----------------------------------------------------------------------------
// Purpose: setup network receive table
//-----------------------------------------------------------------------------

IMPLEMENT_CLIENTCLASS_DT(C_NPC_Blob, DT_NPC_Blob, CNPC_Blob)

	RecvPropUtlVector(
		RECVINFO_UTLVECTOR( m_vecTypes ), 
		MAX_SURFACE_ELEMENTS,
		RecvPropInt(NULL, 0, sizeof( int ))),

	RecvPropUtlVector(
		RECVINFO_UTLVECTOR( m_vecParents ), 
		MAX_SURFACE_ELEMENTS,
		RecvPropInt(NULL, 0, sizeof( uint16 ))),
/*
	RecvPropUtlVector( 
		RECVINFO_UTLVECTOR( m_vecUVW ), 
		MAX_SURFACE_ELEMENTS,
		RecvPropVector(NULL, 0, sizeof( Vector ))),
*/
	RecvPropUtlVector(
		RECVINFO_UTLVECTOR( m_iFlags ), 
		MAX_SURFACE_ELEMENTS,
		RecvPropInt(NULL, 0, sizeof( uint16 ))),

	RecvPropBool( RECVINFO( m_bFrozen ) ),
	RecvPropEHandle( RECVINFO( m_hAutoSplitBlob ) ),
	RecvPropFloat( RECVINFO( m_fBrainExposure ) ),
	RecvPropTime( RECVINFO( m_flColorChangeStartTime ) ),
	RecvPropTime( RECVINFO( m_flColorChangeEndTime ) ),
	RecvPropVector( RECVINFO( m_vecStartColor ) ),
	RecvPropVector( RECVINFO( m_vecEndColor ) )

END_RECV_TABLE()


C_NPC_Blob::C_NPC_Blob() : m_hAutoSplitBlob( NULL )
{
	//m_coreFieldStrength.EnsureCount( MAX_SURFACE_ELEMENTS );
	m_blobParticles.EnsureCount( MAX_SURFACE_ELEMENTS );

	m_vecTypes.EnsureCount( MAX_SURFACE_ELEMENTS );
	m_vecParents.EnsureCount( MAX_SURFACE_ELEMENTS );

	m_vecUVW.EnsureCount( MAX_SURFACE_ELEMENTS );
	m_iv_vecUVW.EnsureCount( MAX_SURFACE_ELEMENTS );

	for (int i = 0; i < MAX_SURFACE_ELEMENTS; i++)
	{
		//m_coreFieldStrength[i] = 0.0f;
		m_blobParticles[i].vPulseColor.Init( 0.0f, 0.0f, 0.0f, -1.0f );

		IInterpolatedVar *pWatcher = &m_iv_vecUVW.Element( i );
		pWatcher->SetDebugName( "m_iv_vecUVW" );
		AddVar( &m_vecUVW.Element( i ), pWatcher, LATCH_ANIMATION_VAR );
	}

	memset( m_lightingBoxColors, 0, sizeof( m_lightingBoxColors ) );
}

C_NPC_Blob::~C_NPC_Blob()
{
}

void C_NPC_Blob::Activate( void )
{
	// These aren't saved
	memset( m_lightingBoxColors, 0, sizeof( m_lightingBoxColors ) );
}

void C_NPC_Blob::ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs )
{
	GetRenderBoundsWorldspace( *pMins, *pMaxs );
}

void C_NPC_Blob::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );

	if ( type == DATA_UPDATE_CREATED )
	{
		C_NPC_Blob* pParentBlob = m_hAutoSplitBlob.Get();

		if ( pParentBlob )
		{
			// Init our lighting environment with the parent's so that the there's no lighting pop for a newly split blob.
			memcpy( m_lightingBoxColors, pParentBlob->m_lightingBoxColors, sizeof( m_lightingBoxColors ) );
		}
	}
}

void C_NPC_Blob::UpdateOutOfWorldParticleMask( void ) const
{
	// Need to tell the C_NPC_Surface about particles that are (probably) outside the world, to avoid lighting issues
	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		m_vOutOfWorldParticleMask.Set( i, !!( m_iFlags[i] & BLOB_PARTICLE_STRAGGLER ) );
	}
}

int C_NPC_Blob::DrawModel( int flags, const RenderableInstance_t &instance )
{
	#if !defined( _GAMECONSOLE ) // X360TBD: Enable blobulator for EP3

	if ( flags & STUDIO_SHADOWTEXTURE )
	{
		DrawModelShadow();

		return 1;
	}

	if ( flags & STUDIO_SHADOWDEPTHTEXTURE )
	{
		DrawModelShadowDepth();
		return 1;
	}

	// TODO: Move this stuff into ClientThink
	SetUpDrawLighting();
	UpdateArmatureData();

	// Draw the brain
	if ( cl_blob_draw_brain.GetBool() )
	{
		SetUpDrawMaterial();
		SetUpCoreParticleVector();

		if ( !IsErrorMaterial( m_pMaterial ) )
		{
			Surface_Draw( GetClientRenderable(), GetRenderOrigin(), m_pMaterial, m_flRadius * 0.8f );
		}
	}

	// Draw the tentacles
	SetUpArmatureDrawMaterial();	
	DrawArmature( m_nTotalSegs );

	// Draw the back surface
	if ( cl_blob_draw_backfaces.GetBool() )
	{
		SetUpBackFaceDrawMaterial();
		SetUpParticleVector();

		// TODO: the back surface doesn't really need the refract buffer
		//       (it only needs to write alpha, but the engine doesn't support color write masking :o/ )
		if ( m_pMaterial->NeedsFullFrameBufferTexture() )
		{
			// Make sure we have a copy of the innards in an offscreen buffer for the next render pass
			UpdateScreenEffectTexture();
		}

		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->CullMode( MATERIAL_CULLMODE_CW );
		Surface_Draw( GetClientRenderable(), GetRenderOrigin(), m_pMaterial, m_flRadius );
		pRenderContext->CullMode( MATERIAL_CULLMODE_CCW );
	}

	// Draw the front surface
	if ( cl_blob_draw_frontfaces.GetBool() )
	{
		SetUpDrawMaterial();
		if ( m_pMaterial->NeedsFullFrameBufferTexture() )
		{
			// Make sure we have a copy of the innards in an offscreen buffer for the next render pass
			UpdateScreenEffectTexture();
		}
		SetUpParticleVector();

		// set up flashlight
		int nNumShadows = shadowmgr->GetNumShadowsOnModel( GetModelInstance() );	// This returns the total count of all types of shadows on the model
		if ( nNumShadows > 0 )
		{
			ShadowHandle_t* pShadowArray = static_cast<ShadowHandle_t*>( stackalloc( nNumShadows * sizeof( ShadowHandle_t ) ) );
			Assert( pShadowArray );

			nNumShadows = shadowmgr->GetShadowsOnModel( GetModelInstance(), pShadowArray, false, true );

			// nNumShadows now holds the actual number of shadows returned
			if ( nNumShadows > 1 )
			{
				DevWarning( "FIXME: Blob only supports a single flashlight" );
			}

			if ( nNumShadows > 0 )
			{
				shadowmgr->SetFlashlightRenderState( *pShadowArray );
			}
		}

		if ( !IsErrorMaterial( m_pMaterial ) )
		{
			Surface_Draw( GetClientRenderable(), GetRenderOrigin(), m_pMaterial, m_flRadius );
		}
	}

	// reset flashlight
	shadowmgr->SetFlashlightRenderState( SHADOW_HANDLE_INVALID );

	#endif // !_GAMECONSOLE

	/*
	Vector bbMin;
	Vector bbMax;
	ComputeWorldSpaceSurroundingBox( &bbMin, &bbMax );
	Vector center = 0.5 * bbMin + 0.5 * bbMax;
	NDebugOverlay::Box( center, bbMin - center, bbMax - center, 0, 255, 0, 32, 0 );
	GetRenderBounds( bbMin, bbMax );
	center = GetRenderOrigin();
	NDebugOverlay::Box( center, bbMin, bbMax, 0, 0, 255, 32, 0 );

	//NDebugOverlay::EntityBounds( this, 255, 0, 0, 32, 0 );
	*/
	return 1;
}

void C_NPC_Blob::DrawModelShadow()
{
	bool bSkipOutOfWorldParticles = true;
	BaseClass::SetUpParticleVector( bSkipOutOfWorldParticles );
	SetUpShadowDrawMaterial();

	// TODO: Draw screen-aligned quads for particles instead of isosurface (for perf)
	Surface_Draw( GetClientRenderable(), GetRenderOrigin(), m_pMaterial, m_flRadius * 2.0f, true );
}

void C_NPC_Blob::DrawModelShadowDepth()
{
	// flashlight shadow map rendering
	SetUpDrawMaterial();
	SetUpParticleVector();

	// FIXME: to avoid self-shadowing artifacts, particle radii may need adjusting here to take into account scaling
	//        of 'exposed brain' particles (use the max of the skin and core particle radii in that case)

	if( !IsErrorMaterial( m_pMaterial ) )
	{
		Surface_Draw( GetClientRenderable(), GetRenderOrigin(), m_pMaterial, m_flRadius, true );
	}
}


void C_NPC_Blob::SetUpParticleVector( bool bSkipOutOfWorldParticles )
{
	g_SurfaceRenderParticles.SetCount(m_nActiveParticles);

	// TODO: This doesn't have the logic that a particle with
	// radius below 0.1 doesn't get rendered
	int n_particles = 0;
	const CVarBitVec &particleMask = GetOutOfWorldParticleMask();
	for (int i = 0 ; i < m_nActiveParticles; i++)
	{
		if ( !bSkipOutOfWorldParticles || !particleMask.Get( i ) )
		{
			ImpParticleWithOneInterpolant* imp_particle = &g_SurfaceRenderParticles[n_particles];
			imp_particle->center = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[i] );

			// When Brain particles are exposed, their skin radius shrinks and their core radius grows
			float fieldStrength = BLOBPARTICLERADIUS_INTERP( m_iParticlePositionIndex[i] );
			if ( m_iFlags[ i ] & BLOB_PARTICLE_BRAIN )
				fieldStrength = Lerp( m_fBrainExposure, fieldStrength, 0.0f );
			imp_particle->setFieldScale( fieldStrength );

#if 0
			// Set color based on type
			switch( m_vecTypes[i] )
			{
				case 0: imp_particle->interpolants1.set(1.0f, 0.0f, 0.0f); break; // ROOT color is red
				case 1: imp_particle->interpolants1.set(1.0f, 1.0f, 0.0f); break; // CORE color is orange
				case 2: imp_particle->interpolants1.set(0.0f, 0.0f, 1.0f); break; // STICKY color is dark blue
				case 3: imp_particle->interpolants1.set(0.0f, 1.0f, 1.0f); break; // SKIN color is light blue
				case 4: imp_particle->interpolants1.set(0.0f, 1.0f, 0.0f); break; // TENTACLE color is green
				default: imp_particle->interpolants1.set(1.0f, 1.0f, 1.0f); break; // UNDEFINED color is white
			}
#endif

			float radius = BLOBPARTICLERADIUS_INTERP( m_iParticlePositionIndex[i] );
			radius = clamp( radius, 0, 1 ); // networkvar interpolation can extrapolate slightly out-of-range

			Vector glowColor = Vector(0,0,0);
			float  alpha	 = 1.0f;
			if ( m_iFlags[i] & BLOB_PARTICLE_DEATH )
			{
				// Particles glow light blue as they die, and fade out
				glowColor	 = 0.45f*sqrtf( 4*radius*( 1.0f - radius ) )*Vector( 0.0f, 0.15f, 1.0f );
				alpha		 = radius;
			}
			else if ( !( m_iFlags[i] & BLOB_PARTICLE_TENTACLE ) )
			{
				// Particles glow bright orange as they spawn in (so the blob's "regenerate" behaviour is noticeable)
				glowColor	 = powf( ( 1.0f - radius ), 0.25f )*Vector( 1.0f, 0.3f, 0.0f );
			}
			// Shader: Alpha is used as-is. RGB is luminance-squared, then scaled by VMT 'glowScale'
			imp_particle->interpolants1.set( glowColor.x, glowColor.y, glowColor.z );
			imp_particle->interpolants1[3] = alpha; // FIXME: this is evil. And will break without sse.
			n_particles++;
		}
	}

	g_SurfaceRenderParticles.SetCountNonDestructively(n_particles);
}

float C_NPC_Blob::GetPulseFrequency( void )
{
	return cl_blob_arm_pulse_frequency.GetFloat();
}

void C_NPC_Blob::SetUpShadowDrawMaterial()
{
//	m_pMaterial = materials->FindMaterial( "models/blob/blob_shadow", TEXTURE_GROUP_OTHER, true );
	m_pMaterial = materials->FindMaterial( "engine/shadowbuild", TEXTURE_GROUP_OTHER, true );
}

void C_NPC_Blob::SetUpDrawMaterial()
{
 	//this is the blob's outer shell material
	m_pMaterial = materials->FindMaterial( "models/blob/blob_outer_material", TEXTURE_GROUP_OTHER, true );
	//change material when frozen
	if ( m_bFrozen )
		m_pMaterial = materials->FindMaterial( "models/blob/blob_frozen_material", TEXTURE_GROUP_OTHER, true );
}

void C_NPC_Blob::SetUpDrawLighting()
{
	if ( GetModelInstance() == MODEL_INSTANCE_INVALID )
	{
		// we need a model instance so that we can perform smooth lighting interpolation
		CreateModelInstance();
	}

	modelrender->SetupLightingEx( GetRenderOrigin(), GetModelInstance() );

	// We sometimes get an invalid ambient lightcube, this func smooths over that
	// FIXME: this is probably to do with sampling at invalid locations
	Surface_SafeLightCubeUpdate( GetRenderOrigin(), m_lightingBoxColors );

	// Set our manually interpolated lightcube
	if ( materials && materials->GetRenderContext() )
	{
		materials->GetRenderContext()->SetAmbientLightCube( m_lightingBoxColors );
	}
}

Vector C_NPC_Blob::GetRootNodePosition() const
{
	for ( int i = 0 ; i < m_nActiveParticles; i++ )
	{
		if ( m_vecTypes[i] == ROOT_PARTICLE )
		{
			return BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[i] );
		}
	}

	return Vector( 0.0f, 0.0f, 0.0f );
}

void C_NPC_Blob::SetUpBackFaceDrawMaterial()
{
	// FIXME: this material does not exist
	m_pMaterial = materials->FindMaterial( "models/blob/blob_back_material", TEXTURE_GROUP_OTHER, true );
	//m_pMaterial = materials->FindMaterial( "debug/debugvertexcolor", TEXTURE_GROUP_OTHER, true );
}

void C_NPC_Blob::SetUpCoreDrawMaterial()
{
	if ( m_bFrozen )
		m_pMaterial = materials->FindMaterial( "models/blob/blob_inner_material_frozen", TEXTURE_GROUP_OTHER, true );
	else
		m_pMaterial = materials->FindMaterial( "models/blob/blob_inner_material", TEXTURE_GROUP_OTHER, true );
	//m_pMaterial = materials->FindMaterial( "debug/debugvertexcolor", TEXTURE_GROUP_OTHER, true );
}

void C_NPC_Blob::SetUpCoreParticleVector()
{
	g_SurfaceRenderParticles.SetCount(m_nActiveParticles);

	int n_particles = 0;
	for (int i = 0 ; i < m_nActiveParticles; i++)
	{
		CCBlobParticle* b = &(m_blobParticles[i]);
		bool bContainsCore = ( m_vecTypes[i] == ROOT_PARTICLE ) || ( m_vecTypes[i] == CORE_PARTICLE ) ||
							 ( m_iFlags[ i ] & BLOB_PARTICLE_BRAIN );

		b->coreFieldStrength = Lerp( 0.025f, b->coreFieldStrength, ( bContainsCore ? 1.0f : 0.0f ) );
		b->vPulseColor.w -= cl_blob_arm_pulse_speed.GetFloat() * gpGlobals->frametime;
		b->vPulseColor.w = MAX( b->vPulseColor.w, -1.0f );

		// When Brain particles are exposed, their skin radius shrinks and their core radius grows
		float adjustedCoreFieldStrength = b->coreFieldStrength*0.75f;
		if ( m_iFlags[ i ] & BLOB_PARTICLE_BRAIN )
		{
			adjustedCoreFieldStrength = Lerp( m_fBrainExposure, 0.7f*adjustedCoreFieldStrength, 1.0f );

			// Make the brain shiver when exposed, due to the ABJECT TERROR.
			float speed   = 0.7f;
			float offset1 = 1 + sinf( ( i + gpGlobals->curtime )*17.1f*M_PI*speed );
			float offset2 = 1 + sinf( ( i + gpGlobals->curtime )*11.8f*M_PI*speed );
			float offset3 = 1 + sinf( ( i + gpGlobals->curtime )*6.90f*M_PI*speed );
			float offset  = 0.14f*m_fBrainExposure*offset1*offset2*offset3;
			adjustedCoreFieldStrength = clamp( (adjustedCoreFieldStrength + offset), 0, 1 );
		}

		if ( adjustedCoreFieldStrength > 0.075f )
		{
			ImpParticleWithOneInterpolant* imp_particle = &g_SurfaceRenderParticles[n_particles];
			imp_particle->center = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[i] );
			imp_particle->setFieldScale( adjustedCoreFieldStrength * BLOBPARTICLERADIUS_INTERP( m_iParticlePositionIndex[i] ) );
			// TODO: This code above makes the field a bit smaller. However, that is probably not the
			// best approach. A better thing to do might be to keep the field regular size, but to shrink
			// the particles so they are closer in around the root. This might cause other problems though,
			// for core particles that aren't touching the root. If we make the recoloring algorithm automatically
			// relabel core particles that are far away and not touching the root as skin particles, then this
			// problem goes away.

			Vector glowColor( 0, 0, 0 );
			float alpha = 1.0f;
			if ( m_bGlobalToLocalParticleIdxValid )
			{
				static const float fPulseWidth = 0.3f;
				float fArmPos = 0.5f;
				if ( m_pArmInfo[i].m_flArmLength > 0.001f )
				{
					fArmPos = m_pArmInfo[i].m_flDistToRoot / m_pArmInfo[i].m_flArmLength;
				}

				float fPulse = ( fArmPos - ( b->vPulseColor.w - fPulseWidth ) ) / fPulseWidth;
				fPulse = clamp( fPulse, 0.0f, 1.0f );
				float fTmp = 1.0f - ( (fArmPos - b->vPulseColor.w ) / fPulseWidth );
				fTmp = clamp( fTmp, 0.0f, 1.0f );
				fPulse *= fTmp;

				glowColor = sqrtf( fPulse )*Vector( b->vPulseColor.AsVector3D() );
			}
			// Shader: Alpha is used as-is. RGB is luminance-squared, then scaled by VMT 'glowScale'
			imp_particle->interpolants1.set( glowColor.x, glowColor.y, glowColor.z );
			imp_particle->interpolants1[3] = alpha; // FIXME: this is evil. And will break without sse.
			n_particles ++;
		}
	}

	g_SurfaceRenderParticles.SetCountNonDestructively(n_particles);
}

void C_NPC_Blob::SetUpArmatureDrawMaterial()
{
	if ( cl_blob_draw_armature.GetInt() == 2 )
	{
		if ( m_bFrozen )
			m_pMaterial = materials->FindMaterial( "models/blob/blob_inner_arms_frozen", TEXTURE_GROUP_OTHER, true );
		else
			m_pMaterial = materials->FindMaterial( "models/blob/blob_inner_arms", TEXTURE_GROUP_OTHER, true );
	}
	else
		m_pMaterial = materials->FindMaterial( "debug/debugvertexcolor", TEXTURE_GROUP_OTHER, true );
}


//-----------------------------------------------------------------------------
// Render a line
//-----------------------------------------------------------------------------
void C_NPC_Blob::RenderLine( const Vector& v1, const Vector& v2, Color c, bool bZBuffer )
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( m_pMaterial );
	//pRenderContext->Bind( bZBuffer ? s_pWireframe : s_pWireframeIgnoreZ );

	unsigned char chRed = c.r();
	unsigned char chGreen = c.g();
	unsigned char chBlue = c.b();
	unsigned char chAlpha = c.a();

	IMesh *pMesh = pRenderContext->GetDynamicMesh( );
	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_LINES, 1 );

	meshBuilder.Position3fv( v1.Base() );
	meshBuilder.Color4ub( chRed, chGreen, chBlue, chAlpha );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( v2.Base() );
	meshBuilder.Color4ub( chRed, chGreen, chBlue, chAlpha );
	meshBuilder.AdvanceVertex();

	meshBuilder.End();
	pMesh->Draw();
}

int C_NPC_Blob::UpdateArmatureData()
{
	// TODO: This hard-coded 4.0f here is bad. I should network over the bond break distance
	// and use that instead
	float bondBreakDist = 4.0f * m_flRadius;
	float bondBreakDistSq = Sqr(bondBreakDist);
	int nTotalSegs = 0;

	for(int i = 0; i < m_nActiveParticles; i++)
	{
		CCBlobParticle* p = &(m_blobParticles[i]);

		if(m_vecParents[i] == m_iParticlePositionIndex[i])
		{
			// If I am a root particle, I don't need an armature segment to connect
			// me to a parent. So I make sure I set that I am not connected by an armature
			// here. This lets all segments going to me shrink.
			p->connectedByArmatureTo = -1;
		}

		Vector myPosition = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[i] );
		CUtlVector<ArmatureSegment_t>* segments = &(p->armatureSegments);

		bool haveSegmentToParent = false;
#if 0
		for(int j = 0; j < segments->Count(); j++)
		{
			ArmatureSegment_t* seg = &(segments->Element(j));

			// If a segment is too long, shrink it fast. If the particle is connected by that
			// segment, disconnect it. If that segment is a parent segment and it doesn't get
			// deleted here, mark haveSegmentToParent as true.
			if( myPosition.DistToSqr( BLOBPARTICLEPOS_INTERP( seg->startParticle )) >= bondBreakDistSq)
			{
				// When there is fast motion, we want to break bonds that get over length four.
				// Broken bonds get shrunk really fast
				// TODO: Force broken bonds to never be longer than bondBreakDist. This can be done
				// in the code that actually renders the connection.
				if(seg->startParticle == p->connectedByArmatureTo)
				{
					p->connectedByArmatureTo = -1;
				}

				seg->len = MAX(seg->len - 0.5f, 0.0f); // TODO: Scale interpolation by timescale
				if(seg->len == 0.0f)
				{
					segments->FastRemove(j);
					j--;
				}
				else if(seg->startParticle == m_vecParents[i])
				{
					haveSegmentToParent = true;
				}
			}
			else if(seg->startParticle == m_vecParents[i])
			{
				haveSegmentToParent = true;

				if(p->connectedByArmatureTo == -1)
				{
					seg->len = MIN(seg->len + 0.5f, 1.0f); // TODO: Scale interpolation by timescale
				}
				else
				{
					seg->len = MIN(seg->len + 0.2f, 1.0f); // TODO: Scale interpolation by timescale
				}	

				if(seg->len == 1.0f)
				{
					p->connectedByArmatureTo = seg->startParticle;
				}		
			}
			else if(seg->startParticle != p->connectedByArmatureTo)
			{
				seg->len = MAX(seg->len - 0.2f, 0.0f); // TODO: Scale interpolation by timescale
				if(seg->len == 0.0f)
				{
					segments->FastRemove(j);
					j--;
				}
			}
			//else if(seg->startParticle == p->connectedByArmatureTo) don't do anything to that segment
		}
#else
		for(int j = 0; j < segments->Count(); j++)
		{
			ArmatureSegment_t* seg = &(segments->Element(j));

			seg->vPulseColor.w -= cl_blob_arm_pulse_speed.GetFloat() * gpGlobals->frametime;
			seg->vPulseColor.w = MAX( seg->vPulseColor.w, -1.0f );

			float distSq = myPosition.DistToSqr( BLOBPARTICLEPOS_INTERP( seg->startParticle ));

			if(distSq >= bondBreakDistSq)
			{
				// If segment is over bondBreakDist, shrink it whether or not it is connected to parent.
				//seg->len -= 0.5f; // TODO: Scale interpolation by timescale
				// Tweak the length to make sure that even with shrinking, len * dist < bondBreakDist
				seg->len = MIN(bondBreakDist / sqrtf(distSq), seg->len - 5.5f * gpGlobals->frametime); // TODO: Use * reciprocal sqrt instead of /sqrt
			}
			else if(seg->startParticle == m_vecParents[i])
			{
				// Lengthen the segment if it is not longer than bondBreakDist and is connected to the parent.
				if(p->connectedByArmatureTo == -1)
				{
					seg->len += 5.5f * gpGlobals->frametime;
				}
				else
				{
					seg->len += 2.2f * gpGlobals->frametime;
				}	
			}
			else if(seg->startParticle != p->connectedByArmatureTo)
			{
				// Shrink the segment is not forming an armature connection and is not connected to the parent
				seg->len -= 2.2f * gpGlobals->frametime;
			}


			if(seg->len <= 0.0f)
			{
				if(seg->startParticle == p->connectedByArmatureTo) p->connectedByArmatureTo = -1;
				segments->FastRemove(j);
				j--;
			}
			else
			{
				if(seg->len >= 1.0f)
				{
					seg->len = 1.0f;
					//if(seg->startParticle == m_vecParents[i])
					//{
					p->connectedByArmatureTo = seg->startParticle;
					//}
				}
				else // length is between 0 and 1
				{
					if(seg->startParticle == p->connectedByArmatureTo) p->connectedByArmatureTo = -1;
				}

				// Have to do this at the end because segments may be deleted
				if(seg->startParticle == m_vecParents[i])
				{
					haveSegmentToParent = true;
				}

				// NOTE: I can draw line directly in here.
				// The advantage of doing it here is that I can reuse the length
				// calculation that is done at the beginning of the loop.
			}		
		}
#endif

		// If I couldn't find a segment growing from the parent, and I am not a root
		// particle, create a new armature segment growing from
		// the parent to myself.
		if (	!haveSegmentToParent &&
				m_vecParents[i] != m_iParticlePositionIndex[i] &&
				!( m_iFlags[i] & ( BLOB_PARTICLE_BIRTH | BLOB_PARTICLE_DEATH ) ) &&
				myPosition.DistToSqr( BLOBPARTICLEPOS_INTERP( m_vecParents[i] )) < bondBreakDistSq
			)
		{
			ArmatureSegment_t seg;
			seg.startParticle = m_vecParents[i];
			seg.len = 0.0f;
			seg.vPulseColor.Init( 0.0f, 0.0f, 0.0f, -1.0f );

#if 0
			// If I am not connected at all to anything. Connect immediately
			// to my parent.
			if(p->connectedByArmatureTo == -1)
			{
				seg.len = MIN(0.5f, 1.0f); // TODO: Scale interpolation by timescale
			}
			else
			{
				seg.len = 0.0f;
			}

			if(seg.len == 1.0f)
			{
				p->connectedByArmatureTo = seg.startParticle;
			}
#endif

			segments->AddToTail(seg);
		}

		nTotalSegs += segments->Count();
	}



	// Walk armature graph to compute armature width and such
	if ( UpdateGlobalToLocalParticleIdx() == false )
	{
		return nTotalSegs;
	}

	// init arm info here
	{
		ArmatureNodeInfo emptyNode;
		for(int i = 0; i < m_nActiveParticles; i++)
		{
			m_pArmInfo[i] = emptyNode;
		}
	}

	int nVisitID = 1;

	for(int i = 0; i < m_nActiveParticles; i++)
	{
		WalkGraph( i, m_pArmInfo, nVisitID++ );
	}

	for(int i = 0; i < m_nActiveParticles; i++)
	{
		if( m_pArmInfo[i].m_vAvgTangent.LengthSqr() < 0.0001f )
		{
			m_pArmInfo[i].m_vAvgTangent = Vector( 0.0f, 0.0f, 0.0f );
		}
		else
		{
			m_pArmInfo[i].m_vAvgTangent.NormalizeInPlace();
		}

		if ( m_pArmInfo[i].m_bLeafNode == true )
		{
			// figure out if this arm will fire a pulse this frame
			Vector4D vPulseColor = GetArmPulse( i );

			PropagateArmLength( m_pArmInfo[i].m_flMinDistToRoot, m_pArmInfo[i].m_flMaxDistToRoot, m_pArmInfo[i].m_flDistToFirstCoreParticle, vPulseColor, i, m_pArmInfo, nVisitID++ );
		}
	}

	m_nTotalSegs = nTotalSegs;

	return nTotalSegs;
}

void C_NPC_Blob::DrawArmature( int nTotalSegs )
{
	if ( cl_blob_draw_armature.GetInt() == 0 )
	{
		return;
	}

	if ( cl_blob_draw_armature.GetInt() & 1 )
	{
		m_pMaterial = materials->FindMaterial( "debug/debugvertexcolor", TEXTURE_GROUP_OTHER, true );
		// Old armature drawing code
		for(int i = 0; i < m_nActiveParticles; i++)
		{
			Vector myPosition = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[i] );
			CCBlobParticle* p = &(m_blobParticles[i]);
			CUtlVector<ArmatureSegment_t>* segments = &(p->armatureSegments);

			// TODO: Force broken bonds to never be longer than bondBreakDist. This can be done
			// in the code that actually renders the connection.
			for(int j = 0; j < segments->Count(); j++)
			{
				ArmatureSegment_t* seg = &(segments->Element(j));
				Vector startParticlePos = BLOBPARTICLEPOS_INTERP( seg->startParticle );
				RenderLine(startParticlePos, Lerp(seg->len, startParticlePos, myPosition), Color( 255, 255, 0, 255), true);
			}
		}
		//return;
	}
	else

	if ( (cl_blob_draw_armature.GetInt() & 2) == 0 )
	{
		return;
	}

	// Walk armature graph to compute armature width and such
	if ( m_bGlobalToLocalParticleIdxValid == false )
	{
		Warning ( "Skipping armature rendering due to inconsistent data\n" );
		return;
	}

	// Generate VB for bezier segments and render in a single draw call
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->Bind( m_pMaterial, GetClientRenderable() );

	IMesh *pMesh = pRenderContext->GetDynamicMesh();
	CMeshBuilder meshBuilder;

	int nMaxVertsPerBatch = 0;
	int nMaxIndicesPerBatch = 0;
	pRenderContext->GetMaxToRender( pMesh, false, &nMaxVertsPerBatch, &nMaxIndicesPerBatch );

	int nVertsPerSegment = 2 * ( cl_blob_arm_tess_factor.GetInt() + 1 );
	int nIndicesPerSegment = 6 * cl_blob_arm_tess_factor.GetInt();
	int nVertsLeftToRender = nVertsPerSegment * nTotalSegs;
	int nIndicesLeftToRender = nIndicesPerSegment * nTotalSegs;
	int nVertsRenderedInThisBatch = 0;
	int nIndicesRenderedInThisBatch = 0;
	int nVBSize = MIN( nMaxVertsPerBatch, nVertsLeftToRender );
	int nIBSize = MIN( nMaxIndicesPerBatch, nIndicesLeftToRender );

	meshBuilder.Begin(	pMesh, MATERIAL_TRIANGLES, nVBSize, nIBSize );

	for(int i = 0; i < m_nActiveParticles; i++)
	{
		CCBlobParticle* p = &(m_blobParticles[i]);
		CUtlVector<ArmatureSegment_t>* pSegments = &(p->armatureSegments);

		// compute segment end point params
		Vector myPosition = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[i] );
		float flChildWidth = 0.5f;
		if ( m_pArmInfo[i].m_flArmLength > 0.001f )
		{
			flChildWidth = 1.0f - m_pArmInfo[i].m_flDistToRoot / m_pArmInfo[i].m_flArmLength;
		}
		float flChildArmLength = m_pArmInfo[i].m_flArmLength;

		{
			// offset location in direction of tangent to get armature further out towards the surface
			float flNormalizedDistToCore = 0.0f;
			if ( m_pArmInfo[i].m_flLengthToFirstCoreParticle > 0.001f )
			{
				flNormalizedDistToCore = m_pArmInfo[i].m_flDistToFirstCoreParticle / m_pArmInfo[i].m_flLengthToFirstCoreParticle;
			}
			myPosition -= flNormalizedDistToCore * cl_blob_arm_extend.GetFloat() * m_flRadius * m_pArmInfo[i].m_vAvgTangent;
		}

		Color c2 = GetDebugArmColor( i, m_pArmInfo, false );
		float flChildU = m_pArmInfo[ i ].m_flDistToRoot;	// * blob_arm_uv_scale.GetFloat();


		for ( int j = 0; j < pSegments->Count(); j++ )
		{
			ArmatureSegment_t* pSeg = &(pSegments->Element(j));

			if ( m_nGlobalToLocalParticleIdx[pSeg->startParticle] < 0 )
			{
				// wonky start particle index, skip segment
				//Warning( "skipping arm segment due to invalid startParticle index\n" );
				continue;
			}

			Vector v1 = BLOBPARTICLEPOS_INTERP( pSeg->startParticle );

			float flParentWidth = 0.5f;
			if ( m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_flArmLength > 0.001f )
			{
				flParentWidth = 1.0f - m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_flDistToRoot /
					m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_flArmLength;
			}
			//float flParentArmLength = pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_flArmLength;

			float flParentU = m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_flDistToRoot;	// * blob_arm_uv_scale.GetFloat();

			float flSegChildWidth = flChildWidth;
			float flSegChildU = flChildU;
			float flSegChildArmLength = flChildArmLength;

			if ( pSeg->len < 0.99f )
			{
				// lerp child node data based on seg length
				flSegChildWidth = 0.0f;
				flSegChildU = Lerp( pSeg->len, flParentU, flChildU );
				flSegChildArmLength = flSegChildU;//Lerp( pSeg->len, flParentArmLength, pArmInfo[i].m_flDistToRoot );
			}

			{
				float flNormalizedDistToCore = 0.0f;
				if ( m_pArmInfo[m_nGlobalToLocalParticleIdx[pSeg->startParticle]].m_flLengthToFirstCoreParticle > 0.001f )
				{
					flNormalizedDistToCore = m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_flDistToFirstCoreParticle /
						m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_flLengthToFirstCoreParticle;
				}
				// offset location in direction of tangent to get armature further out towards the surface
				v1 -= flNormalizedDistToCore * cl_blob_arm_extend.GetFloat() * m_flRadius * m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_vAvgTangent;
			}


			Vector v2 = Lerp( pSeg->len, v1, myPosition );
			Vector lineDir = v2 - v1;
			float  flSegLength = lineDir.Length();

			if ( flSegLength > 0.01f )
			{
				Assert( m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_bLeafNode == false );
				Assert( m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_vAvgTangent.Length() <= 1.1f );
				Assert( m_pArmInfo[ i ].m_vAvgTangent.Length() <= 1.1f );

				float flTanScale = cl_blob_arm_bend_factor.GetFloat() * flSegLength;
				Vector bezierCtrl0 = v1;
				Vector bezierCtrl1 = v1 - flTanScale * m_pArmInfo[ m_nGlobalToLocalParticleIdx[pSeg->startParticle] ].m_vAvgTangent;
				Vector bezierCtrl2 = v2 + flTanScale * m_pArmInfo[ i ].m_vAvgTangent;
				Vector bezierCtrl3 = v2;

				Color c1 = GetDebugArmColor( m_nGlobalToLocalParticleIdx[pSeg->startParticle], m_pArmInfo, true );

				// flush dynamic VB if necessary
				if ( nVertsRenderedInThisBatch + nVertsPerSegment > nVBSize ||
					 nIndicesRenderedInThisBatch + nIndicesPerSegment > nIBSize )
				{
					meshBuilder.End();
					pMesh->Draw();

					nVertsRenderedInThisBatch = 0;
					nIndicesRenderedInThisBatch = 0;
					nVBSize = MIN( nMaxVertsPerBatch, nVertsLeftToRender );
					nIBSize = MIN( nMaxIndicesPerBatch, nIndicesLeftToRender );
					meshBuilder.Begin(	pMesh, MATERIAL_TRIANGLES, nVBSize, nIBSize );
				}

				int nCurrVertIdx = meshBuilder.GetCurrentVertex();		// store off current vert index for IB creation

				// Verts
				for ( int k = 0; k <= cl_blob_arm_tess_factor.GetInt(); k++ )
				{
					float t1 = float( k ) / float( cl_blob_arm_tess_factor.GetInt() );
					float w1 = Lerp( t1, flParentWidth, flSegChildWidth );
					float u1 = Lerp( t1, flParentU, flSegChildU );

					meshBuilder.TexCoord4f( 0, u1, 0.0f, w1, t1 );
					meshBuilder.TexCoord4f( 1, bezierCtrl0.x, bezierCtrl0.y, bezierCtrl0.z, bezierCtrl3.x );
					meshBuilder.TexCoord4f( 2, bezierCtrl1.x, bezierCtrl1.y, bezierCtrl1.z, bezierCtrl3.y );
					meshBuilder.TexCoord4f( 3, bezierCtrl2.x, bezierCtrl2.y, bezierCtrl2.z, bezierCtrl3.z );
					meshBuilder.TexCoord4f( 4, pSeg->vPulseColor.x, pSeg->vPulseColor.y, pSeg->vPulseColor.z, pSeg->vPulseColor.w );
					meshBuilder.AdvanceVertex();

					meshBuilder.TexCoord4f( 0, u1, 1.0f, w1, t1 );
					meshBuilder.TexCoord4f( 1, bezierCtrl0.x, bezierCtrl0.y, bezierCtrl0.z, bezierCtrl3.x );
					meshBuilder.TexCoord4f( 2, bezierCtrl1.x, bezierCtrl1.y, bezierCtrl1.z, bezierCtrl3.y );
					meshBuilder.TexCoord4f( 3, bezierCtrl2.x, bezierCtrl2.y, bezierCtrl2.z, bezierCtrl3.z );
					meshBuilder.TexCoord4f( 4, pSeg->vPulseColor.x, pSeg->vPulseColor.y, pSeg->vPulseColor.z, pSeg->vPulseColor.w );
					meshBuilder.AdvanceVertex();
				}

				// indices
				for ( int k = 0; k < cl_blob_arm_tess_factor.GetInt(); k++ )
				{
					int nBase = nCurrVertIdx + 2*k;
					meshBuilder.FastIndex2( nBase, nBase+1 );
					meshBuilder.FastIndex2( nBase + 3, nBase );
					meshBuilder.FastIndex2( nBase + 3, nBase +2 );
				}
				nVertsRenderedInThisBatch += nVertsPerSegment;
				nIndicesRenderedInThisBatch += nIndicesPerSegment;
				nVertsLeftToRender -= nVertsPerSegment;
				nIndicesLeftToRender -= nIndicesPerSegment;
			}
		}	
	}

	//Assert( nIndicesLeftToRender == 0 );
	//Assert( nVertsLeftToRender == 0 );
	meshBuilder.End();
	pMesh->Draw();
}


int C_NPC_Blob::WalkGraph( int nParticleID, ArmatureNodeInfo* pArmInfo, int nVisitID, bool bPassedCoreParticle )
{
	if ( nParticleID < 0 || nParticleID >= m_nActiveParticles )
	{
		Warning( "invalid global to local particle idx\n" );
		return -1;
	}

	if ( pArmInfo[nParticleID].m_flMinDistToRoot >= 0.0f )
	{
		// we have valid data already, don't redundantly recurse up
		return 0;
	}

	if ( pArmInfo[nParticleID].m_nVisited == 1 )
	{
		// cycle detected
		Warning( "cycle\n" );
		return -1;
	}

	pArmInfo[nParticleID].m_nVisited = 1;

	CCBlobParticle* p = &(m_blobParticles[nParticleID]);
	CUtlVector<ArmatureSegment_t>* segments = &(p->armatureSegments);

	Vector myPosition = BLOBPARTICLEPOS_INTERP( m_iParticlePositionIndex[nParticleID] );

	pArmInfo[nParticleID].m_flMinDistToRoot = FLT_MAX;
	pArmInfo[nParticleID].m_flMaxDistToRoot = -FLT_MAX;
	pArmInfo[nParticleID].m_flDistToFirstCoreParticle = FLT_MAX;

	if ( m_vecTypes[ nParticleID ] == CORE_PARTICLE || m_vecTypes[ nParticleID ] == ROOT_PARTICLE || bPassedCoreParticle )
	{
		pArmInfo[nParticleID].m_flDistToFirstCoreParticle = 0.0f;
		bPassedCoreParticle = true;
	}

	int nSegs = 0;

	for ( int j = 0; j < segments->Count(); j++ )
	{
		ArmatureSegment_t* seg = &(segments->Element(j));

		//if ( seg->len < 0.99f )
		//	continue;

		if ( m_nGlobalToLocalParticleIdx[seg->startParticle] < 0 )	// temporary fix for out-of bounds particle links
			continue;

		/*
		bool bSegmentGrowing = seg->len < 0.99f;
		
		if ( bSegmentGrowing )
		{
			
			Vector startParticlePos = BLOBPARTICLEPOS_INTERP( seg->startParticle );

			pArmInfo[nParticleID].m_vAvgTangent += seg->len * ( startParticlePos - myPosition );
			pArmInfo[ m_nGlobalToLocalParticleIdx[seg->startParticle] ].m_vAvgTangent += seg->len * ( startParticlePos - myPosition );
			continue;	// no recursive graph walk up growing links
		}
		*/

		nSegs++;

		pArmInfo[ m_nGlobalToLocalParticleIdx[seg->startParticle] ].m_bLeafNode = false;

		if( WalkGraph( m_nGlobalToLocalParticleIdx[seg->startParticle], pArmInfo, nVisitID, bPassedCoreParticle ) == -1 )
		{
			// cycle detected, make seg->startparticle a root node for this arm
			pArmInfo[nParticleID].m_flMinDistToRoot = 0.0f;
			pArmInfo[nParticleID].m_flMaxDistToRoot = 0.0f;
			pArmInfo[nParticleID].m_flDistToFirstCoreParticle = 0.0f;
			pArmInfo[nParticleID].m_bRootNode = true;
		}
		else
		{
			// get segment length
			Vector startParticlePos = BLOBPARTICLEPOS_INTERP( seg->startParticle );
			float flSegLength = myPosition.DistTo( startParticlePos );

			// only consider segment if it's connected
			pArmInfo[nParticleID].m_flMinDistToRoot = MIN( pArmInfo[nParticleID].m_flMinDistToRoot,
				pArmInfo[ m_nGlobalToLocalParticleIdx[seg->startParticle] ].m_flMinDistToRoot + flSegLength );
			pArmInfo[nParticleID].m_flMaxDistToRoot = MAX( pArmInfo[nParticleID].m_flMinDistToRoot,
				pArmInfo[ m_nGlobalToLocalParticleIdx[seg->startParticle] ].m_flMaxDistToRoot + flSegLength );

			if ( !bPassedCoreParticle )
			{
				pArmInfo[nParticleID].m_flDistToFirstCoreParticle = MIN( pArmInfo[nParticleID].m_flDistToFirstCoreParticle,
					pArmInfo[ m_nGlobalToLocalParticleIdx[seg->startParticle] ].m_flDistToFirstCoreParticle + flSegLength );
			}

			pArmInfo[nParticleID].m_vAvgTangent += seg->len * ( startParticlePos - myPosition );
			pArmInfo[ m_nGlobalToLocalParticleIdx[seg->startParticle] ].m_vAvgTangent += seg->len * ( startParticlePos - myPosition );
		}
	} // end for( each segment )

	if ( nSegs == 0 )
	{
		pArmInfo[nParticleID].m_flMinDistToRoot = 0.0f;
		pArmInfo[nParticleID].m_flMaxDistToRoot = 0.0f;
		pArmInfo[nParticleID].m_bRootNode = true;
	}

	// fill in for now. These need to be propagated from the leaves
	pArmInfo[nParticleID].m_flMinArmLength = pArmInfo[nParticleID].m_flMinDistToRoot;
	pArmInfo[nParticleID].m_flMaxArmLength = pArmInfo[nParticleID].m_flMaxDistToRoot;
	pArmInfo[nParticleID].m_flLengthToFirstCoreParticle = pArmInfo[nParticleID].m_flDistToFirstCoreParticle;

	pArmInfo[nParticleID].m_flDistToRoot = Lerp( 0.0, pArmInfo[nParticleID].m_flMinDistToRoot, pArmInfo[nParticleID].m_flMaxDistToRoot );

	return 0;
}

void C_NPC_Blob::PropagateArmLength( float fMinLen, float fMaxLen, float fCoreParticleLen, Vector4D vPulseColor, int nParticleID, ArmatureNodeInfo* pArmInfo, int nVisitID )
{
	if ( nParticleID < 0 || nParticleID >= m_nActiveParticles )
	{
		//Warning( "invalid global to local particle idx\n" );
		return;
	}

	if( pArmInfo[nParticleID].m_nVisited == nVisitID )
	{
		// cycle found
		return;
	}

	pArmInfo[nParticleID].m_nVisited = nVisitID;
	pArmInfo[nParticleID].m_flMaxArmLength = MAX( fMaxLen, pArmInfo[nParticleID].m_flMaxArmLength );
	pArmInfo[nParticleID].m_flMinArmLength = MAX( fMinLen, pArmInfo[nParticleID].m_flMinArmLength );
	pArmInfo[nParticleID].m_flArmLength = Lerp( 0.0, pArmInfo[nParticleID].m_flMinArmLength, pArmInfo[nParticleID].m_flMaxArmLength );
	pArmInfo[nParticleID].m_flLengthToFirstCoreParticle = MAX( fCoreParticleLen, pArmInfo[nParticleID].m_flLengthToFirstCoreParticle );

	CCBlobParticle* p = &(m_blobParticles[nParticleID]);
	if ( vPulseColor.w > p->vPulseColor.w )
	{
		p->vPulseColor = vPulseColor;
	}

	CUtlVector<ArmatureSegment_t>* segments = &(p->armatureSegments);

	for ( int j = 0; j < segments->Count(); j++ )
	{
		ArmatureSegment_t* seg = &(segments->Element(j));

		if ( vPulseColor.w > seg->vPulseColor.w )
		{
			seg->vPulseColor = vPulseColor;
		}

		if( seg->len >= 0.99f )
		{
			PropagateArmLength( fMinLen, fMaxLen, fCoreParticleLen, vPulseColor, m_nGlobalToLocalParticleIdx[seg->startParticle], pArmInfo, nVisitID );
		}
	}
}


Color C_NPC_Blob::GetDebugArmColor( int nParticleID, ArmatureNodeInfo* pArmInfo, bool bStartNode )
{
	int r, g, b;
	r = bStartNode ? 255 : 0;
	g = pArmInfo[nParticleID].m_bLeafNode ? 255 : 0;
	b = pArmInfo[nParticleID].m_bRootNode ? 255 : 0;
	return Color( r, g, b, 255 );
}

bool C_NPC_Blob::UpdateGlobalToLocalParticleIdx()
{
	for ( int i = 0; i < MAX_SURFACE_ELEMENTS; i++ )
	{
		m_nGlobalToLocalParticleIdx[ i ] = -1;
	}

	for ( int i = 0; i < m_nActiveParticles; i++ )
	{
		if ( m_iParticlePositionIndex[i] >= 0 && m_iParticlePositionIndex[i] < MAX_SURFACE_ELEMENTS )
		{
			m_nGlobalToLocalParticleIdx[ m_iParticlePositionIndex[i] ] = i;
		}
		else
		{
			Assert( !"inconsistent position index data" );
			m_bGlobalToLocalParticleIdxValid = false;
			return false;
		}
	}

	m_bGlobalToLocalParticleIdxValid = true;
	return true;
}

Vector4D C_NPC_Blob::GetArmPulse( int nParticleID )
{
	float flPulse = 0.0f;
	Vector4D vColor( 1.0f, 1.0f, 1.0f, 0.0f );

	if ( m_blobParticles[nParticleID].vPulseColor.w < 0.3f )		// if we didn't just recently fire a pulse
	{
		float flPulseFrequency = 0.0f;
		
		if ( m_iFlags[nParticleID] & BLOB_PARTICLE_PERFORMED_MELEE_ATTACK )
		{
			flPulseFrequency = 3.0f;
			vColor.Init( 1.0f, 1.0f, 0.0f, 0.0f );
		}
		else if ( m_iFlags[nParticleID] & BLOB_PARTICLE_DEATH )
		{
			flPulseFrequency = 3.0f;
			vColor.Init( 1.0f, 0.0f, 1.0f, 0.0f );
		}
		else if ( m_iFlags[nParticleID] & BLOB_PARTICLE_TOUCHED_SURFACE )
		{
			flPulseFrequency = 0.5f;
			vColor.Init( 0.1f, 0.1f, 1.0f, 0.0f );
		}
		else
		{
			flPulseFrequency = 0.2 * GetPulseFrequency();	// default frequency based on mood
		}

		flPulse = ( random->RandomFloat( 0.0f, 1.0f ) < gpGlobals->frametime * flPulseFrequency ) ? 1.0f : 0.0f;
		flPulse = m_bFrozen ? 0.0f : flPulse;	// no pulsing when frozen
	}

	vColor.w = flPulse;
	return vColor;
}

Vector C_NPC_Blob::GetEmotionColor() const
{
	if ( gpGlobals->curtime >= m_flColorChangeEndTime )
		return m_vecEndColor;
	if ( gpGlobals->curtime <= m_flColorChangeStartTime )
		return m_vecStartColor;
	float t = ( gpGlobals->curtime - m_flColorChangeStartTime ) / ( m_flColorChangeEndTime - m_flColorChangeStartTime );
	Vector c;
	VectorLerp( m_vecStartColor, m_vecEndColor, t, c );
	return c;
}

// Blob material proxy for the color representing the blob's emotion
// 
// Example use:
// "Proxies"
// {
//		"BlobEmotionColor"
//		{
//			"resultVar" "$interiorColor"
//			"scale"	"0.5"			// scale emotion color
//		}
// }

class CBlobEmotionColorMaterialProxy : public CResultProxy
{
public:
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues );
	virtual void OnBind( void *pC_BaseEntity );

private:
	CFloatInput m_scale;
};

bool CBlobEmotionColorMaterialProxy::Init( IMaterial *pMaterial, KeyValues *pKeyValues )
{
	if ( !CResultProxy::Init( pMaterial, pKeyValues ) )
	{
		return false;
	}

	m_scale.Init( pMaterial, pKeyValues, "scale", 1.0f );
	return true;
}

void CBlobEmotionColorMaterialProxy::OnBind( void* pC_BaseEntity )
{
	if ( !pC_BaseEntity )
		return;

	C_BaseEntity *pEntity = BindArgToEntity( pC_BaseEntity );
	C_NPC_Blob* pBlob = dynamic_cast<C_NPC_Blob*>( pEntity );
	if ( !pBlob )
		return;

	Vector col = m_scale.GetFloat() * pBlob->GetEmotionColor();
	SetVecResult( col.x, col.y, col.z, 1.0f );
}

EXPOSE_MATERIAL_PROXY( CBlobEmotionColorMaterialProxy, BlobEmotionColor );

//EXPOSE_MATERIAL_PROXY( CBlobLightingMaterialProxy, BlobLighting );

// Blob material proxy for all internal blob shader parameters
// This proxy must be in any VMT used on the blob.
// 
// Example use:
// "Proxies"
// {
//		"Blob"
//		{
//		}
// }

class CBlobMaterialProxy : public CEntityMaterialProxy
{
public:
	CBlobMaterialProxy();
	virtual bool Init( IMaterial* pMaterial, KeyValues* pKeyValues );
	virtual IMaterial* GetMaterial();

protected:
	virtual void OnBind( C_BaseEntity *pC_BaseEntity );

private:
	IMaterialVar* m_pMatVarBBMin;
	IMaterialVar* m_pMatVarBBMax;
	IMaterialVar* m_pMatVarUVProjOffset;
};

CBlobMaterialProxy::CBlobMaterialProxy()
: m_pMatVarBBMin( NULL ),
  m_pMatVarBBMax( NULL ),
  m_pMatVarUVProjOffset( NULL )
{
}

bool CBlobMaterialProxy::Init( IMaterial* pMaterial, KeyValues* pKeyValues )
{
	Assert( pMaterial );

	bool bFound = true;
	m_pMatVarBBMin = pMaterial->FindVar( "$BBMin", &bFound );
	if ( !bFound )
		return false;
	m_pMatVarBBMax = pMaterial->FindVar( "$BBMax", &bFound );
	if ( !bFound )
		return false;
	m_pMatVarUVProjOffset = pMaterial->FindVar( "$UVProjOffset", &bFound );
	if ( !bFound )
		return false;

	return true;
}

IMaterial* CBlobMaterialProxy::GetMaterial()
{
	Assert( m_pMatVarBBMin );
	return m_pMatVarBBMin ? m_pMatVarBBMin->GetOwningMaterial() : NULL;
}

void CBlobMaterialProxy::OnBind( C_BaseEntity* pC_BaseEntity )
{
	if ( !pC_BaseEntity )
		return;

	C_NPC_Blob* pBlob = dynamic_cast<C_NPC_Blob*>( pC_BaseEntity );

	Vector pos;
	if ( pBlob )
	{
		pos = pBlob->GetRootNodePosition();
	}
	else
	{
		pos = pC_BaseEntity->GetRenderOrigin();
	}
	m_pMatVarUVProjOffset->SetVecValue( pos.Base(), 3 );

	Vector bbMin;
	Vector bbMax;
	pC_BaseEntity->GetRenderBoundsWorldspace( bbMin, bbMax );
	m_pMatVarBBMin->SetVecValue( bbMin.Base(), 3 );
	m_pMatVarBBMax->SetVecValue( bbMax.Base(), 3 );
}

EXPOSE_MATERIAL_PROXY( CBlobMaterialProxy, Blob );

// Super-hacky material proxy that can set up a lighting cube. This only works with the blob particle render op.
// 
// Example use:
// "Proxies"
// {
//		"LightCube"
//		{
//		}
// }

class CLightCubeMaterialProxy : public IMaterialProxy
{
public:
	CLightCubeMaterialProxy();
	virtual bool Init( IMaterial* pMaterial, KeyValues* pKeyValues );
	virtual IMaterial* GetMaterial();
	virtual void OnBind( void* pData );
	virtual void Release();

private:
	IMaterialVar* m_pMatVarLightCube[6];
};

CLightCubeMaterialProxy::CLightCubeMaterialProxy()
{
	memset( m_pMatVarLightCube, 0, sizeof( m_pMatVarLightCube ) );
}

bool CLightCubeMaterialProxy::Init( IMaterial* pMaterial, KeyValues* pKeyValues )
{
	Assert( pMaterial );

	bool bFound = true;
	char tmpStr[64];
	for ( int i = 0; i < 6; i++ )
	{
		sprintf( tmpStr, "$lightCube%d", i );
		m_pMatVarLightCube[i] = pMaterial->FindVar( tmpStr, &bFound );
		if ( !bFound )
			return false;
	}

	return true;
}

IMaterial* CLightCubeMaterialProxy::GetMaterial()
{
	Assert( m_pMatVarLightCube[0] );
	return m_pMatVarLightCube[0]->GetOwningMaterial();
}

void CLightCubeMaterialProxy::OnBind( void* pData )
{
	if ( !pData )
		return;

	// FIXME: super-hack! We assume the data is a Vector ptr. Only works for the blob particle render op.
	Vector* pPos = reinterpret_cast< Vector* >( pData );

	// work in progress
	Vector boxColors[6];
	engine->ComputeLightingCube( *pPos, false, boxColors );

	for ( int i = 0; i < 6; i++ )
	{
		m_pMatVarLightCube[i]->SetVecValue( boxColors[i].Base(), 3 );
	}
}

void CLightCubeMaterialProxy::Release()
{
	delete this; 
}

EXPOSE_MATERIAL_PROXY( CLightCubeMaterialProxy, LightCube );
