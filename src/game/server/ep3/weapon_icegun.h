//========= Copyright © 1996-2008, Valve Corporation, All rights reserved. ====
//
// A weapon that sprays ice allowing the player to create ice walls and bridges.
// Secondary fire shoots cryodarts that violently shatter the ice!
//
//=============================================================================

#include "cbase.h"
#include "props.h"
#include "physics_prop_statue.h"


class CWeaponIcegun;
class CIceSculpture;
struct icesphere_t;


#define ICE_SPHERE_MAX_NEIGHBORS 9


enum IceSculptureLifeStatus
{
	ICE_SCULPTURE_LIFE_STATUS_HEALTHY,
	ICE_SCULPTURE_LIFE_STATUS_MELTING,
	ICE_SCULPTURE_LIFE_STATUS_SHATTERING,

	ICE_SCULPTURE_LIFE_STATUS_TOTAL
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
struct icesphere_t
{
	DECLARE_CLASS_NOBASE( icesphere_t );
	DECLARE_SIMPLE_DATADESC();

	icesphere_t();
	~icesphere_t();
	
	CHandle<CIceSculpture>	m_hSculpture;	// The sculpture that we belong to
	IPhysicsObject			*m_pPhysObject;	// Associated physics object

	short		m_iNeighborSpheres[ ICE_SPHERE_MAX_NEIGHBORS ];
	bool		m_bRootedToWorld;			// Sphere was built directly onto the world
	bool		m_bPeeked;					// Prevents infinite recursion when looking for world roots

	Vector	m_vecPos;

	Vector	m_vecExtrudeDir;				// The direction that we were extruded from the sculpture

	Vector	m_vecPrevDelta;					// The offset of the previous sphere in the sculpture
	Vector	m_vecNextDelta;					// The offset of the next sphere in the sculpture

	Vector	m_vecMid;						// The position of the current midpoint in the sculpture
	Vector	m_vecNextMid;					// The position of the next midpoint in the sculpture

	int		m_iHealth;
	float	m_flDieTime;					// Time when this sphere will melt

	short	m_iOBB;							// Sphere is contained in this OBB

#ifdef PAINT
	Color	m_color;
	Vector	m_vecSurfNormal;				// surface normal of the containing box
#endif
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class CIceSculpture : public CBaseAnimating
{
public:
	DECLARE_SERVERCLASS();
	DECLARE_SHAREDCLASS();
	DECLARE_CLASS( CIceSculpture, CBaseAnimating );
	DECLARE_DATADESC();

	static CIceSculpture *CreateIceSculpture( const Vector &vecPos, const Vector &vecNormal );
	static CIceSculpture *GetOldestIceSculpture( void );

	CIceSculpture();

	virtual void	Spawn();
	virtual void	Precache();
	virtual bool	CreateVPhysics();

	bool			ShouldSavePhysics()	{ return false; }
	virtual void	OnRestore();
	
	virtual int		VPhysicsGetObjectList( IPhysicsObject **pList, int listMax );
	virtual void	VPhysicsDestroyObject();
	virtual void	VPhysicsUpdate( IPhysicsObject *pPhysics );

	virtual int		OnTakeDamage( const CTakeDamageInfo &info );
	virtual bool	TestCollision( const Ray_t &ray, unsigned int fContentsMask, trace_t& tr );
	virtual void	ComputeWorldSpaceSurroundingBox( Vector *pMins, Vector *pMaxs );
	icesphere_t		*GetTouchSphere( const Ray_t& ray, trace_t& tr );

	inline int GetSphereCount();
	inline icesphere_t *GetSphere( int nIndex );

	void DumpSpheres();

	void RecenterAbsOrigin( void );

	void AddNeighborSphereLinks( int iSphere );
	void RemoveNeighborSphereLinks( int iSphere );
	bool BuildIceChunk( CUtlVector<short> &chunk, short iSphere, 
						const Vector *pvPlaneNormal = NULL, const Vector *pvPlanePoint = NULL );
	void CheckForNonRootedIceChunks( void );

	void RemoveFromOBB( int iSphere );
	void RebuildCollisionOBB( short iOBB );
	void RebuildSimplifiedCollision( bool bCleanRebuild = false );

	void GetSculptureGestureDir( Vector &vecDir );

	void AddBaseSphere( int iSphere );
	void AddSphere( int iSphere );
	void AddPhysicsObjectForSphere( icesphere_t *pSphere );
	void RemoveSphere( int iSphere, bool bFast = false );
	int GetIceSphere( Vector vPosition, icesphere_t **ppIceSphere = NULL );

	void Melt( void );
	void Shatter( Vector vecPos, float flDelay );
	void RemoveSphereThink( void );

	bool PlayerIsOnSculpture( CBasePlayer *pPlayer, icesphere_t *pLastGround );

	void DrawSimplifiedOBB();

public:
	static CUtlVector<IPhysicsObject*>	m_PhysicsObjects;
	static int m_iPhysicsObjectsRef;

	CNetworkVar( bool,				m_bOriginsRelativeToCenter );	// Used when spheres are relative to the origin (for moving sculptures)
	CUtlVector<Vector>				m_vecBaseOrigins;				// The origins of the spheres that define the base gesture of this sculpture
	CSharedUtlVector( Vector,		m_vecOrigins );					// The origins of the spheres that define this entire sculpture
	CUtlVector<icesphere_t>			m_IceSpheres;					// All the individual sphere data

	CUtlVector<outer_collision_obb_t>	m_CollisionOBBs;	// Simplified OBB collision data

	IceSculptureLifeStatus	m_eLifeStatus;

	Vector	m_vecNormal;					// The normal of the surface that this sculpture lies on
	float	m_flCreationTime;				// When the sculture was created
	float m_flNextShatterSoundTime;			// To keep shatter sounds from stacking up

private:

	Vector	m_vCachedWorldMins;
	Vector	m_vCachedWorldMaxs;
	int		m_iCachedWorldBoundsUpdateTick;
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CIceSculpture::GetSphereCount()
{
	return m_IceSpheres.Count();
}

icesphere_t *CIceSculpture::GetSphere( int nIndex )
{
	return &m_IceSpheres.Element( nIndex );
}


bool Icegun_IsBuildingSculpture();
void Icegun_GetBuildPosition( Vector &vecPos );
bool Icegun_IsPlayerIceSurfing();


