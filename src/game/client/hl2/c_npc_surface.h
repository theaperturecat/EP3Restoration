#define			MAX_SURFACE_ELEMENTS 750 // Ilya: Reduced because I was getting max prop buffer error 1000 // 200
#define			MAX_SURFACE_ELEMENTS_BITS 10 // How many bits are needed to send a number from 0 to MAX_SURFACE_ELEMENTS-1

__forceinline float sqr(float a) { return a * a; }

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class C_NPC_Surface : public C_AI_BaseNPC
{
public:
	DECLARE_CLASS(C_NPC_Surface, C_AI_BaseNPC);
	DECLARE_CLIENTCLASS();
	DECLARE_INTERPOLATION();

	C_NPC_Surface();
	virtual			~C_NPC_Surface();

	// model specific
	virtual void GetRenderBounds(Vector& theMins, Vector& theMaxs);
	virtual bool IsTransparent(void);
	ShadowType_t	ShadowCastType() { return SHADOWS_NONE; }
	bool UsesPowerOfTwoFrameBufferTexture(void);
	RenderableTranslucencyType_t ComputeTranslucencyType();
	bool UsesFullFrameBufferTexture(void);
	int DrawModel(int flags, const RenderableInstance_t& instance) override;

	virtual bool	GetSoundSpatialization(SpatializationInfo_t& info);

	mutable CVarBitVec m_vOutOfWorldParticleMask;//kinda guessing this

	virtual void SetupParticles();

	CVarBitVec& GetOutOfWorldParticleMask() { return m_vOutOfWorldParticleMask; };
	void SetUpParticleVector(bool) { };

	//#define	MAX_SURFACE_ELEMENTS 1000//BLOB_MAX_LEVEL_PARTICLES //200

	IMaterial* m_pMaterial;

	//CUtlVector< Vector	> m_vecSurfacePos;
	//CUtlVector< CInterpolatedVar< Vector > > m_iv_vecSurfacePos;

	//CUtlVector< float > m_flSurfaceV;
	//CUtlVector< CInterpolatedVar< float > > m_iv_flSurfaceV;

	//CUtlVector< float > m_flSurfaceR;
	//CUtlVector< CInterpolatedVar< float > > m_iv_flSurfaceR;

	CUtlVector<uint16> m_iParticlePositionIndex;


	int				m_nActiveParticles;
	float			m_flRadius;

private:
	C_NPC_Surface(const C_NPC_Surface&); // not defined, not accessible
};