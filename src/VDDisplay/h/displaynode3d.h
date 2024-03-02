#ifndef f_VD2_VDDISPLAY_DISPLAYNODE3D_H
#define f_VD2_VDDISPLAY_DISPLAYNODE3D_H

#include <vd2/system/vdstl.h>
#include <vd2/Tessa/Context.h>

struct VDPixmap;
class VDDisplayNode3D;

struct VDDisplayVertex3D {
	float x;
	float y;
	float z;
	float u;
	float v;
};

struct VDDisplayVertex2T3D {
	float x;
	float y;
	float z;
	float u0;
	float v0;
	float u1;
	float v1;
};

struct VDDisplayVertex3T3D {
	float x;
	float y;
	float z;
	float u0;
	float v0;
	float u1;
	float v1;
	float u2;
	float v2;
};

///////////////////////////////////////////////////////////////////////////

class VDDisplayNodeContext3D;
class VDDisplayMeshCommand3D;
enum VDDPoolTextureIndex : uint16;
enum VDDPoolMeshIndex : uint16;
enum VDDPoolRenderViewId : uint8;

struct VDDRenderView {
	IVDTSurface *mpTarget;
	bool mbBypassSrgb;
	VDTViewport mViewport;
};

class VDDisplayMeshBuilder3D {
public:
	void SetVertexFormat(IVDTVertexFormat *vf);
	void SetVertexProgram(VDTData vpdata);
	void SetFragmentProgram(VDTData fpdata);

	void SetVPConstData(const void *src, size_t len);

	template<typename T>
	void SetVPConstData(const T& obj) {
		static_assert(!(sizeof(obj) & 15));
		SetVPConstData(&obj, sizeof obj);
	}

	void SetVPConstDataReuse();
	void SetFPConstData(const void *src, size_t len);

	template<typename T>
	void SetFPConstData(const T& obj) {
		static_assert(!(sizeof(obj) & 15));

		SetFPConstData(&obj, sizeof obj);
	}

	void SetFPConstDataReuse();

	void *InitVertices(size_t vertexSize, uint32 vertexCount);

	template<typename T>
	T *InitVertices(uint32 vertexCount) {
		static_assert(alignof(T) <= 4, "Cannot direct map with vertex type that has greater than 32-bit alignment");
		static_assert(!(sizeof(T) & 3), "Cannot direct map with vertex type that is not 32-bit aligned in size");

		return (T *)InitVertex(sizeof(T), vertexCount);
	}

	void SetVertices(const void *data, size_t vertexSize, size_t vertexCount);

	template<typename T>
	void SetVertices(const T *vertices, size_t vertexCount) {
		static_assert(!(sizeof(T) & 3), "Cannot set vertex type that is not 32-bit aligned in size");

		SetVertices(vertices, sizeof(T), vertexCount);
	}

	template<typename T, size_t N>
	void SetVertices(const T (&vertices)[N]) {
		SetVertices(vertices, N);
	}

	void SetTopology(const uint16 *indices, uint32 numTriangles);
	void SetTopologyImm(std::initializer_list<uint16> indices);
	void SetTopologyQuad();

	void InitTextures(uint32 numTextures);
	void InitTextures(std::initializer_list<VDDPoolTextureIndex> textures);
	void SetTexture(uint32 textureSlot, VDDPoolTextureIndex poolTextureIndex);

	void InitSamplers(uint32 numSamplers);
	void InitSamplers(std::initializer_list<IVDTSamplerState *> samplers);
	void SetSampler(uint32 samplerSlot, IVDTSamplerState *ss);

	void SetRenderView(VDDPoolRenderViewId id);
	VDDPoolRenderViewId SetRenderView(const VDDRenderView& renderView);
	VDDPoolRenderViewId SetRenderView(VDDPoolTextureIndex texture, uint32 mipLevel, bool bypassConversion);
	VDDPoolRenderViewId SetRenderView(VDDPoolTextureIndex texture, uint32 mipLevel, bool bypassConversion, const VDTViewport& viewport);

	VDDPoolMeshIndex GetMeshIndex() const { return mMeshIndex; }

private:
	friend class VDDisplayMeshPool3D;

	VDDisplayMeshBuilder3D(VDDisplayNodeContext3D& dctx, VDDisplayMeshPool3D& pool, VDDisplayMeshCommand3D& cmd, VDDPoolMeshIndex meshIndex);

	VDDisplayNodeContext3D *mpDctx;
	VDDisplayMeshPool3D *mpPool;
	VDDisplayMeshCommand3D *mpCmd;
	VDDPoolMeshIndex mMeshIndex;
};

class VDDisplayMeshPool3D {
	VDDisplayMeshPool3D(const VDDisplayMeshPool3D&) = delete;
	VDDisplayMeshPool3D& operator=(const VDDisplayMeshPool3D&) = delete;

public:
	VDDisplayMeshPool3D() = default;
	~VDDisplayMeshPool3D();

	void Clear();

	void SetTexture(VDDPoolTextureIndex index, IVDTTexture *texture);
	VDDPoolTextureIndex RegisterTexture(IVDTTexture *texture);
	VDDPoolTextureIndex AddTempTexture(IVDTContext& ctx, uint32 width, uint32 height, VDTFormat format, uint32 mipcount);

	VDDPoolRenderViewId RegisterRenderView(const VDDRenderView& renderView);
	void SetRenderView(VDDPoolRenderViewId id, const VDDRenderView& renderView);
	void SetRenderViewFromCurrent(VDDPoolRenderViewId id, IVDTContext& ctx);

	VDDisplayMeshBuilder3D AddMesh(VDDisplayNodeContext3D& dctx);
	void DrawAllMeshes(IVDTContext& ctx, VDDisplayNodeContext3D& dctx);
	void DrawMeshes(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, VDDPoolMeshIndex start, uint32 count);

	operator bool() const { return !mbError; }

private:
	friend class VDDisplayMeshBuilder3D;

	vdfastvector<uint32> mVertexData;
	vdfastvector<uint16> mIndexData;
	vdfastvector<IVDTSamplerState *> mpSamplers;
	vdfastvector<IVDTTexture *> mpTextures;
	vdfastvector<VDDPoolTextureIndex> mTextureIndices;
	vdfastvector<char, vdaligned_alloc<char, 16>> mConstData;
	vdfastvector<VDDisplayMeshCommand3D> mMeshes;
	vdfastvector<IVDTTexture *> mpAllocatedTextures;

	vdfastvector<VDDRenderView> mRenderViews;
	bool mbError = false;
};

class VDDisplayMeshCommand3D {
private:
	friend class VDDisplayMeshBuilder3D;
	friend class VDDisplayMeshPool3D;

	IVDTVertexFormat *mpVF = nullptr;
	IVDTVertexProgram *mpVP = nullptr;
	IVDTFragmentProgram *mpFP = nullptr;

	uint32 mSamplerStart = 0;
	uint16 mTextureStart = 0;
	uint8 mSamplerCount = 0;
	uint8 mTextureCount = 0;
	VDDPoolRenderViewId mRenderView {};

	// Vertex/fragment program constant data. Special case: len=0, offset>0
	// means to reuse existing set data.
	uint32 mVPConstOffset = 0;
	uint32 mFPConstOffset = 0;
	uint16 mVPConstCount = 0;
	uint16 mFPConstCount = 0;

	uint32 mVertexSourceOffset = 0;
	uint32 mVertexSourceLen = 0;
	uint16 mVertexSize = 0;
	uint16 mVertexCount = 0;
	uint32 mIndexSourceOffset = 0;
	uint32 mIndexSourceCount = 0;

	uint32 mVertexCachedOffset = 0;
	uint32 mVertexCachedGeneration = 0;
	uint32 mIndexCachedOffset = 0;
	uint32 mIndexCachedGeneration = 0;
};

class VDDisplayNodeContext3D {
	VDDisplayNodeContext3D(const VDDisplayNodeContext3D&);
	VDDisplayNodeContext3D& operator=(const VDDisplayNodeContext3D&);
public:
	VDDisplayNodeContext3D();
	~VDDisplayNodeContext3D();

	bool Init(IVDTContext& ctx, bool preferLinear);
	void Shutdown();

	VDDRenderView CaptureRenderView() const;
	void ApplyRenderView(const VDDRenderView& targetView);
	void ApplyRenderViewWithSubrect(const VDDRenderView& targetView, sint32 x, sint32 y, sint32 w, sint32 h);

	IVDTVertexProgram *InitVP(VDTData vpdata);
	IVDTFragmentProgram *InitFP(VDTData ipdata);
	bool CacheVB(const void *data, uint32 len, uint32& offset, uint32& generation);
	bool CacheIB(const uint16 *data, uint32 count, uint32& offset, uint32& generation);

public:
	IVDTContext *mpContext = nullptr;
	IVDTVertexFormat *mpVFTexture = nullptr;
	IVDTVertexFormat *mpVFTexture2T = nullptr;
	IVDTVertexFormat *mpVFTexture3T = nullptr;
	IVDTVertexProgram *mpVPTexture = nullptr;
	IVDTVertexProgram *mpVPTexture2T = nullptr;
	IVDTVertexProgram *mpVPTexture3T = nullptr;
	IVDTFragmentProgram *mpFPBlit = nullptr;
	IVDTSamplerState *mpSSPoint = nullptr;
	IVDTSamplerState *mpSSBilinear = nullptr;
	IVDTSamplerState *mpSSBilinearRepeatMip = nullptr;

	VDTFormat mBGRAFormat {};
	VDTFormat mBGRASRGBFormat {};
	VDTFormat mHDRFormat {};
	bool mbRenderLinear {};
	float mSDRBrightness {};

	static constexpr uint32 kCacheVBBytes = 262144;
	static constexpr uint32 kCacheIBIndices = 16384;

	IVDTVertexBuffer *mpVertexCache = nullptr;
	IVDTIndexBuffer *mpIndexCache = nullptr;
	uint32 mVBCacheBytePos = 0;
	uint32 mVBCacheGeneration = 0;	// always odd when active
	uint32 mIBCacheIdxPos = 0;
	uint32 mIBCacheGeneration = 0;	// always odd when active

	struct VDTDataHashPred {
		size_t operator()(const VDTData& x) const;
		bool operator()(const VDTData& x, const VDTData& y) const;
	};

	vdhashmap<VDTData, IVDTVertexProgram *, VDTDataHashPred, VDTDataHashPred> mVPCache;
	vdhashmap<VDTData, IVDTFragmentProgram *, VDTDataHashPred, VDTDataHashPred> mFPCache;
};

struct VDDisplaySourceTexMapping {
	float mUSize;
	float mVSize;
	uint32 mWidth;
	uint32 mHeight;
	uint32 mTexWidth;
	uint32 mTexHeight;

	void Init(uint32 w, uint32 h, uint32 texw, uint32 texh) {
		mUSize = (float)w / (float)texw;
		mVSize = (float)h / (float)texh;
		mWidth = w;
		mHeight = h;
		mTexWidth = texw;
		mTexHeight = texh;
	}
};

///////////////////////////////////////////////////////////////////////////

enum class VDDImageEncoding : uint8 {
	sRGB,
	sRGBExtended,
	Linear,
};

///////////////////////////////////////////////////////////////////////////
// VDDisplaySourceNode3D
//
// Source nodes supply images through textures. They can either be
// regular 2D textures, or they can be render target textures produced by
// recursive renders.
//
class VDDisplaySourceNode3D : public vdrefcount {
public:
	virtual ~VDDisplaySourceNode3D();

	// Obtain the texture mapping for the source. This must be valid after
	// node init and before Draw(). It cannot change between Draw() calls
	// as its result may be used in a derived node's init.
	virtual VDDisplaySourceTexMapping GetTextureMapping() const = 0;

	// Update and return the current texture. The texture may change with
	// each call, and only the last returned texture is valid.
	virtual IVDTTexture2D *Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx) = 0;
};

///////////////////////////////////////////////////////////////////////////
// VDDisplayTextureSourceNode3D
//
class VDDisplayTextureSourceNode3D : public VDDisplaySourceNode3D {
public:
	VDDisplayTextureSourceNode3D();
	~VDDisplayTextureSourceNode3D();

	bool Init(IVDTTexture2D *tex, const VDDisplaySourceTexMapping& mapping);
	void Shutdown();

	VDDisplaySourceTexMapping GetTextureMapping() const;
	IVDTTexture2D *Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx);

private:
	IVDTTexture2D *mpImageTex;
	VDDisplaySourceTexMapping mMapping;
};

///////////////////////////////////////////////////////////////////////////
// VDDisplayImageSourceNode3D
//
// Image source nodes provide static or animated images through a texture.
// They can only support one image or video per texture and only those
// that can be uploaded in native format or through a red/blue flip.
//
class VDDisplayImageSourceNode3D : public VDDisplaySourceNode3D {
public:
	VDDisplayImageSourceNode3D();
	~VDDisplayImageSourceNode3D();

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 w, uint32 h, uint32 formaty);
	void Shutdown();

	void Load(const VDPixmap& px);

	VDDisplaySourceTexMapping GetTextureMapping() const;
	IVDTTexture2D *Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx);

private:
	IVDTTexture2D *mpImageTex = nullptr;
	VDDisplaySourceTexMapping mMapping {};
	bool mbTextureInited = false;
};

///////////////////////////////////////////////////////////////////////////
// VDDisplayBufferSourceNode3D
//
// This node collects the output of a subrender tree and makes it available
// as a source node.
//
class VDDisplayBufferSourceNode3D : public VDDisplaySourceNode3D {
public:
	VDDisplayBufferSourceNode3D();
	~VDDisplayBufferSourceNode3D();

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 w, uint32 h, bool hdr, VDDisplayNode3D *child);
	void Shutdown();

	VDDisplaySourceTexMapping GetTextureMapping() const;
	IVDTTexture2D *Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx);

private:
	struct Vertex;

	IVDTTexture2D *mpRTT;
	VDDisplayNode3D *mpChildNode;
	VDDisplaySourceTexMapping mMapping;
};

///////////////////////////////////////////////////////////////////////////
// VDDisplayNode3D
//
// Display nodes draw items to the current render target. They make up
// the primary composition tree.
//
class VDDisplayNode3D : public vdrefcount {
public:
	virtual ~VDDisplayNode3D();

	virtual void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView) = 0;
};

///////////////////////////////////////////////////////////////////////////

class VDDisplaySequenceNode3D : public VDDisplayNode3D {
public:
	VDDisplaySequenceNode3D();
	~VDDisplaySequenceNode3D();

	void Shutdown();

	void AddNode(VDDisplayNode3D *node);

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView);

protected:
	typedef vdfastvector<VDDisplayNode3D *> Nodes;
	Nodes mNodes;
};

///////////////////////////////////////////////////////////////////////////

class VDDisplayClearNode3D : public VDDisplayNode3D {
public:
	VDDisplayClearNode3D();
	~VDDisplayClearNode3D();

	void SetClearColor(uint32 c);

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView);

protected:
	uint32 mColor;
};

///////////////////////////////////////////////////////////////////////////

class VDDisplayImageNode3D : public VDDisplayNode3D {
public:
	VDDisplayImageNode3D();
	~VDDisplayImageNode3D();

	bool CanStretch() const;
	void SetBilinear(bool enabled) { mbBilinear = enabled; }
	void SetDestArea(sint32 x, sint32 y, uint32 w, uint32 h) { mDstX = x; mDstY = y; mDstW = w; mDstH = h; }

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 w, uint32 h, uint32 format);
	void Shutdown();

	void Load(const VDPixmap& px);

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView);

private:
	enum RenderMode {
		kRenderMode_Blit,
		kRenderMode_BlitY,
		kRenderMode_BlitYCbCr,
		kRenderMode_BlitPal8,
		kRenderMode_BlitUYVY,
		kRenderMode_BlitRGB16,
		kRenderMode_BlitRGB16Direct,
		kRenderMode_BlitRGB24
	};

	IVDTTexture2D *mpImageTex[3];
	IVDTTexture2D *mpPaletteTex;
	IVDTVertexFormat *mpVF;
	IVDTVertexProgram *mpVP;
	IVDTFragmentProgram *mpFP;
	IVDTVertexBuffer *mpVB;

	RenderMode	mRenderMode;
	bool	mbRender2T;
	bool	mbBilinear;
	sint32	mDstX;
	sint32	mDstY;
	uint32	mDstW;
	uint32	mDstH;
	uint32	mTexWidth;
	uint32	mTexHeight;
	uint32	mTex2Width;
	uint32	mTex2Height;

	uint32	mLastPalette[256] {};
};

///////////////////////////////////////////////////////////////////////////
// VDDisplayBlitNode3D
//
// This node does a simple render of a source node using point, bilinear,
// or sharp bilinear filtering.
//
class VDDisplayBlitNode3D : public VDDisplayNode3D {
public:
	VDDisplayBlitNode3D();
	~VDDisplayBlitNode3D();

	void SetDestArea(sint32 x, sint32 y, uint32 w, uint32 h) { mDstX = x; mDstY = y; mDstW = w; mDstH = h; }

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 w, uint32 h, bool linear, float sharpnessX, float sharpnessY, VDDisplaySourceNode3D *source);
	void Shutdown();

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView);

private:
	struct Vertex;

	IVDTVertexBuffer *mpVB;
	IVDTFragmentProgram *mpFP;
	VDDisplaySourceNode3D *mpSourceNode;
	bool mbLinear;
	float mSharpnessX;
	float mSharpnessY;
	sint32	mDstX;
	sint32	mDstY;
	uint32	mDstW;
	uint32	mDstH;
	VDDisplaySourceTexMapping mMapping;
};

///////////////////////////////////////////////////////////////////////////

class VDDisplayStretchBicubicNode3D final : public VDDisplayNode3D {
public:
	VDDisplayStretchBicubicNode3D();
	~VDDisplayStretchBicubicNode3D();

	const vdrect32 GetDestArea() const;

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 srcw, uint32 srch, sint32 dstx, sint32 dsty, uint32 dstw, uint32 dsth, VDDisplaySourceNode3D *child);
	void Shutdown();

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView) override;

private:
	struct Vertex;

	IVDTVertexProgram *mpVP;
	IVDTFragmentProgram *mpFP;
	IVDTVertexFormat *mpVF;
	IVDTVertexBuffer *mpVB;

	IVDTTexture2D *mpRTTHoriz;
	IVDTTexture2D *mpFilterTex;
	VDDisplaySourceNode3D *mpSourceNode;

	uint32	mSrcW;
	uint32	mSrcH;
	sint32	mDstX;
	sint32	mDstY;
	uint32	mDstW;
	uint32	mDstH;
};

///////////////////////////////////////////////////////////////////////////

class VDDisplayScreenFXNode3D : public VDDisplayNode3D {
public:
	VDDisplayScreenFXNode3D();
	~VDDisplayScreenFXNode3D();

	const vdrect32 GetDestArea() const;

	struct Params {
		sint32 mDstX;
		sint32 mDstY;
		sint32 mDstW;
		sint32 mDstH;
		bool mbLinear;
		bool mbUseAdobeRGB;
		bool mbRenderLinear;
		bool mbSignedInput;
		float mHDRScale;
		float mSharpnessX;
		float mSharpnessY;
		float mDistortionX;
		float mDistortionYRatio;
		float mScanlineIntensity;
		float mGamma;
		float mPALBlendingOffset;
		float mColorCorrectionMatrix[3][3];
	};

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const Params& initParams, VDDisplaySourceNode3D *child);
	void Shutdown();

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView);

private:
	struct Vertex;

	IVDTVertexProgram *mpVP = nullptr;
	IVDTFragmentProgram *mpFP = nullptr;
	IVDTVertexFormat *mpVF = nullptr;
	IVDTVertexBuffer *mpVB = nullptr;
	IVDTIndexBuffer *mpIB = nullptr;

	IVDTTexture2D *mpGammaRampTex = nullptr;
	IVDTTexture2D *mpScanlineMaskTex = nullptr;
	VDDisplaySourceNode3D *mpSourceNode = nullptr;

	uint32 mFPMode = 0;
	float mCachedGamma = 0;
	bool mbCachedGammaHasSrgb = false;
	bool mbCachedGammaHasAdobeRGB = false;

	VDDisplaySourceTexMapping mMapping {};
	Params mParams {};
	float mScanlineMaskNormH = 0;

	static const uint32 kTessellation;
};

///////////////////////////////////////////////////////////////////////////

class VDDisplayArtifactingNode3D final : public VDDisplayNode3D {
public:
	VDDisplayArtifactingNode3D();
	~VDDisplayArtifactingNode3D();

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, float dy, bool extendedOutput, VDDisplaySourceNode3D *child);
	void Shutdown();

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView) override;

private:
	struct Vertex;

	IVDTFragmentProgram *mpFP = nullptr;
	IVDTVertexBuffer *mpVB = nullptr;

	VDDisplaySourceNode3D *mpSourceNode = nullptr;

	VDDisplaySourceTexMapping mMapping {};
};

///////////////////////////////////////////////////////////////////////////

class VDDisplayBloomNode3D final : public VDDisplayNode3D {
public:
	VDDisplayBloomNode3D();
	~VDDisplayBloomNode3D();

	struct Params {
		float mDstX;
		float mDstY;
		float mDstW;
		float mDstH;
		float mThreshold;
		float mBlurRadius;
		float mDirectIntensity;
		float mIndirectIntensity;
		bool mbRenderLinear;
	};

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const Params& params, VDDisplaySourceNode3D *child);
	void Shutdown();

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView) override;

private:
	struct Vertex;

	Params mParams {};
	uint32 mBlurW = 0;
	uint32 mBlurH = 0;
	uint32 mBlurW2 = 0;
	uint32 mBlurH2 = 0;
	bool mbPrescale2x = false;

	VDDisplaySourceNode3D *mpSourceNode = nullptr;

	VDDPoolTextureIndex mSourceTextureIndex;
	VDDPoolRenderViewId mOutputViewId;

	VDDisplayMeshPool3D mMeshPool;

	VDDisplaySourceTexMapping mMapping {};
};

#endif
