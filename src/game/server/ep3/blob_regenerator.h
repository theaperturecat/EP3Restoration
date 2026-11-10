//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
//===========================================================================//

#ifndef BLOB_REGENERATOR_H
#define BLOB_REGENERATOR_H
#ifdef _WIN32
#pragma once
#endif


#include "baseanimating.h"


//-----------------------------------------------------------------------------
// Object that regenerates a blob
//-----------------------------------------------------------------------------
class CBlobRegenerator : public CBaseAnimating
{
	DECLARE_CLASS( CBlobRegenerator, CBaseAnimating );

public:
	CBlobRegenerator( void );
	virtual ~CBlobRegenerator( void );

	virtual void	Spawn( void );
	virtual void	Activate();
	virtual bool	CreateVPhysics( void );
	virtual void	UpdateOnRemove();
	virtual int		OnTakeDamage( const CTakeDamageInfo &info );
	void	RegenerateTouch( CBaseEntity *pOther );
	void	Precache( void );
	void	Event_Killed( const CTakeDamageInfo &info );
	void	MarkInactive();
	bool	IsInactive() const;
	bool	IsBuilding() const;

	static void CreateFromBlob( const Vector &vecSpawnPoint );

	static int GetBlobRegeneratorCount();
	static CBlobRegenerator *GetBlobRegenerator( int i );

protected:
	DECLARE_DATADESC();

	void	ReactivateThink();
	void	GrowThink();
	void	FinishGrowThink();
	void	BeginGrowing();

	bool	m_bIsInactive;
	bool	m_bIsBuilding;

	static CUtlVector< CHandle< CBlobRegenerator > > s_BlobRegenerators;
};


#endif // BLOB_REGENERATOR_H
