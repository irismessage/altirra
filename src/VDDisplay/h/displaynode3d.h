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
class VDDisplayCommand3D;
struct VDDisplayMeshCommand3D;
struct VDDisplayDispatchCommand3D;
enum VDDPoolTextureIndex : uint16;
enum VDDPoolCommandIndex : uint16;
enum VDDPoolRenderViewId : uint8;

using VDDVertexTransformer = void (*)(void *, const void *, size_t, vdfloat2, vdfloat2);

struct VDDisplaySoftViewport {
	vdfloat2 mSize { 1, 1 };
	vdfloat2 mOffset { 0, 0 };

	bool operator==(const VDDisplaySoftViewport&) const = default;
};

struct VDDRenderView {
	IVDTSurface *mpTarget = nullptr;
	bool mbBypassSrgb = false;
	VDTViewport mViewport {};
	VDDisplaySoftViewport mSoftViewport;
};

class VDDisplayMeshBuilder3D {
public:
	void SetVertexFormat(IVDTVertexFormat *vf);
	void SetVertexProgram(IVDTVertexProgram *vp);
	void SetVertexProgram(VDTData vpdata);
	void SetFragmentProgram(IVDTFragmentProgram *fp);
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

	void *InitVertices(size_t vertexSize, uint32 vertexCount, VDDVertexTransformer vertexTransformer);

	template<typename T> requires(alignof(T) <= 4 && !(sizeof(T) & 3))
	T *InitVertices(uint32 vertexCount) {
		return (T *)InitVertex(sizeof(T), vertexCount);
	}

	void SetVertices(const void *data, size_t vertexSize, size_t vertexCount, VDDVertexTransformer vertexTransformer);

	template<typename T> requires(!(sizeof(T) & 3))
	void SetVertices(const T *vertices, size_t vertexCount) { 
		SetVertices(vertices, sizeof(T), vertexCount, TransformVerts<T>);
	}

	template<typename T, size_t N>
	void SetVertices(const T (&vertices)[N]) {
		SetVertices(vertices, N);
	}

	void SetTopology(const uint16 *indices, uint32 numTriangles);
	void SetTopologyImm(std::initializer_list<uint16> indices);
	void SetTopologyQuad();

	void SetQuad2T(const vdfloat2& duv1, const vdfloat2& duv2);
	void SetOffsetQuad2T(const vdfloat2& uv1, const vdfloat2& duv1, const vdfloat2& uv2, const vdfloat2& duv2);

	void InitTextures(uint32 numTextures);
	void InitTextures(std::initializer_list<VDDPoolTextureIndex> textures);
	void SetTexture(uint32 textureSlot, VDDPoolTextureIndex poolTextureIndex);

	void InitSamplers(uint32 numSamplers);
	void InitSamplers(std::initializer_list<IVDTSamplerState *> samplers);
	void SetSampler(uint32 samplerSlot, IVDTSamplerState *ss);

	void SetClear(uint32 clearColor);

	void SetBlendState(IVDTBlendState *blendState);

	void SetRenderView(VDDPoolRenderViewId id);
	VDDPoolRenderViewId SetRenderView(const VDDRenderView& renderView);
	VDDPoolRenderViewId SetRenderView(VDDPoolTextureIndex texture, uint32 mipLevel, bool bypassConversion);
	VDDPoolRenderViewId SetRenderView(VDDPoolTextureIndex texture, uint32 mipLevel, bool bypassConversion, const VDTViewport& viewport);

	VDDPoolCommandIndex GetCommandIndex() const { return mCommandIndex; }

private:
	friend class VDDisplayCommandList3D;

	VDDisplayMeshBuilder3D(VDDisplayNodeContext3D& dctx, VDDisplayCommandList3D& pool, VDDisplayMeshCommand3D& cmd, VDDPoolCommandIndex meshIndex);

	template<typename T>
	static void TransformVerts(void *dst0, const void *src0, size_t n, vdfloat2 scale, vdfloat2 offset) {
		static_assert(sizeof(T) % sizeof(uint32) == 0);
		uint32 *VDRESTRICT dst = (uint32 *)dst0;
		const uint32 *VDRESTRICT src = (const uint32 *)src0;

		T vx;
		while(n--) {
			memcpy(&vx, src, sizeof(T));
			src = (const uint32 *)((const char *)src + sizeof(T));

			vx.x = vx.x * scale.x + offset.x;
			vx.y = vx.y * scale.y + offset.y;

			memcpy(dst, &vx, sizeof(T));
			dst = (uint32 *)((char *)dst + sizeof(T));
		}
	}

	VDDisplayNodeContext3D *mpDctx;
	VDDisplayCommandList3D *mpPool;
	VDDisplayMeshCommand3D *mpCmd;
	VDDPoolCommandIndex mCommandIndex;
};

class VDDisplayDispatchBuilder3D {
public:
	void SetComputeProgram(IVDTComputeProgram *fp);
	void SetComputeProgram(VDTData fpdata);

	void SetCPConstData(const void *src, size_t len);

	template<typename T>
	void SetCPConstData(const T& obj) {
		static_assert(!(sizeof(obj) & 15));
		SetCPConstData(&obj, sizeof obj);
	}

	void SetCPConstDataReuse();

	void InitTextures(uint32 numTextures);
	void InitTextures(std::initializer_list<VDDPoolTextureIndex> textures);
	void SetTexture(uint32 textureSlot, VDDPoolTextureIndex poolTextureIndex);

	void InitSamplers(uint32 numSamplers);
	void InitSamplers(std::initializer_list<IVDTSamplerState *> samplers);
	void SetSampler(uint32 samplerSlot, IVDTSamplerState *ss);

	void InitUAVs(uint32 numViews);
	void InitUAVs(std::initializer_list<IVDTUnorderedAccessView *> views);
	void SetUAV(uint32 viewSlot, IVDTUnorderedAccessView *views);

	void SetDimensions(uint32 x, uint32 y, uint32 z);

	VDDPoolCommandIndex GetCommandIndex() const { return mCommandIndex; }

private:
	friend class VDDisplayCommandList3D;

	VDDisplayDispatchBuilder3D(VDDisplayNodeContext3D& dctx, VDDisplayCommandList3D& pool, VDDisplayDispatchCommand3D& cmd, VDDPoolCommandIndex meshIndex);

	VDDisplayNodeContext3D *mpDctx;
	VDDisplayCommandList3D *mpPool;
	VDDisplayDispatchCommand3D *mpCmd;
	VDDPoolCommandIndex mCommandIndex;
};

class VDDisplayCommandList3D {
	VDDisplayCommandList3D(const VDDisplayCommandList3D&) = delete;
	VDDisplayCommandList3D& operator=(const VDDisplayCommandList3D&) = delete;

public:
	VDDisplayCommandList3D() = default;
	~VDDisplayCommandList3D();

	void Clear();

	void SetTexture(VDDPoolTextureIndex index, IVDTTexture *texture);
	VDDPoolTextureIndex RegisterTexture(IVDTTexture *texture);
	VDDPoolTextureIndex AddTempTexture(IVDTContext& ctx, uint32 width, uint32 height, VDTFormat format, uint32 mipcount);

	VDDPoolRenderViewId RegisterRenderView(const VDDRenderView& renderView);
	void SetRenderView(VDDPoolRenderViewId id, const VDDRenderView& renderView);
	void SetRenderViewWithSubrect(VDDPoolRenderViewId id, const VDDRenderView& renderView, float x, float y, float w, float h);
	void SetRenderViewFromCurrent(VDDPoolRenderViewId id, IVDTContext& ctx);

	VDDisplayMeshBuilder3D AddMesh(VDDisplayNodeContext3D& dctx);
	VDDisplayDispatchBuilder3D AddDispatch(VDDisplayNodeContext3D& dctx);

	void UpdateDispatchConstants(VDDPoolCommandIndex index, const void *data, size_t len);
	
	template<typename T>
	void UpdateDispatchConstants(VDDPoolCommandIndex index, const T& data) {
		UpdateDispatchConstants(index, &data, sizeof data);
	}

	void ReadDispatchConstants(VDDPoolCommandIndex index, void *data, size_t len) const;

	template<typename T>
	T ReadDispatchConstants(VDDPoolCommandIndex index) const {
		T data {};
		ReadDispatchConstants(index, &data, sizeof data);

		return data;
	}

	void UpdateDispatchUAV(VDDPoolCommandIndex cmdIndex, uint32 viewIndex, IVDTUnorderedAccessView *view);

	void ExecuteAll(IVDTContext& ctx, VDDisplayNodeContext3D& dctx);
	void Execute(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, VDDPoolCommandIndex start, uint32 count);
	void ExecuteRange(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, VDDPoolCommandIndex start, VDDPoolCommandIndex end);

	operator bool() const { return !mbError; }
	VDDPoolCommandIndex GetNextCommandIndex() const;

private:
	friend class VDDisplayMeshBuilder3D;
	friend class VDDisplayDispatchBuilder3D;

	vdfastvector<uint32> mVertexData;
	vdfastvector<uint32> mVertexTransformedData;
	vdfastvector<uint16> mIndexData;
	vdfastvector<IVDTSamplerState *> mpSamplers;
	vdfastvector<IVDTTexture *> mpTextures;
	vdfastvector<VDDPoolTextureIndex> mTextureIndices;
	vdfastvector<char, vdaligned_alloc<char, 16>> mConstData;
	vdfastvector<VDDisplayCommand3D> mMeshes;
	vdfastvector<IVDTTexture *> mpAllocatedTextures;
	vdfastvector<IVDTUnorderedAccessView *> mpUAVs;

	vdfastvector<VDDRenderView> mRenderViews;
	bool mbError = false;
};

struct VDDisplayMeshCommand3D {
	IVDTVertexFormat *mpVF = nullptr;
	IVDTVertexProgram *mpVP = nullptr;
	IVDTFragmentProgram *mpFP = nullptr;
	IVDTBlendState *mpBlendState = nullptr;

	uint32 mSamplerStart = 0;
	uint16 mTextureStart = 0;
	uint8 mSamplerCount = 0;
	uint8 mTextureCount = 0;

	bool mbRenderClear = false;
	uint32 mRenderClearColor = 0;
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

	VDDisplaySoftViewport mLastSoftViewport {
		vdfloat2{0,0},
		vdfloat2{0,0}
	};
	VDDVertexTransformer mVertexTransformer = nullptr;
};

struct VDDisplayDispatchCommand3D {
	IVDTComputeProgram *mpCP = nullptr;

	uint32 mSamplerStart = 0;
	uint16 mTextureStart = 0;
	uint16 mUAVStart = 0;
	uint8 mSamplerCount = 0;
	uint8 mTextureCount = 0;
	uint8 mUAVCount = 0;

	// Vertex/fragment program constant data. Special case: len=0, offset>0
	// means to reuse existing set data.
	uint32 mCPConstOffset = 0;
	uint16 mCPConstCount = 0;

	uint32 mSizeX = 0;
	uint32 mSizeY = 0;
	uint32 mSizeZ = 0;
};

class VDDisplayCommand3D {
public:
	VDDisplayCommand3D() {}

private:
	friend class VDDisplayMeshBuilder3D;
	friend class VDDisplayCommandList3D;

	bool mbIsCompute = false;
	union {
		VDDisplayMeshCommand3D mMeshCommand;
		VDDisplayDispatchCommand3D mDispatchCommand;
	};
};

class VDDisplayNodeContext3D {
	VDDisplayNodeContext3D(const VDDisplayNodeContext3D&);
	VDDisplayNodeContext3D& operator=(const VDDisplayNodeContext3D&);
public:
	VDDisplayNodeContext3D();
	~VDDisplayNodeContext3D();

	bool Init(IVDTContext& ctx, bool preferLinear);
	void Shutdown();

	void BeginScene(bool traceRTs);
	void ClearTrace();

	const vdvector<vdrefptr<IVDTSurface>>& GetTracedRTs() const { return mTracedRTs; }

	VDDRenderView CaptureRenderView() const;
	void ApplyRenderView(const VDDRenderView& targetView);
	void ApplyRenderViewWithSubrect(const VDDRenderView& targetView, float x, float y, float w, float h);

	const VDDisplaySoftViewport& GetCurrentSoftViewport() const {
		return mSoftViewport;
	}

	IVDTVertexProgram *InitVP(VDTData vpdata);
	IVDTFragmentProgram *InitFP(VDTData ipdata);
	IVDTComputeProgram *InitCP(VDTData ipdata);
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
	IVDTBlendState *mpBSOver = nullptr;

	// General purpose RGBA format. Recommended for render targets since
	// RGBA is needed for widest compatibility with compute.
	VDTFormat mRGBAFormat {};

	// General purpose RGBA format with implicit linear <-> sRGB conversion.
	// Recommended for render targets where sRGB conversion may be needed.
	VDTFormat mRGBASRGBFormat {};

	VDTFormat mRGBAGammaToSRGBFormat {};

	// BGRA-specific layout formats. Used for textures where we need to upload
	// BGRA data and don't want to swizzle it on upload.
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

	VDDisplaySoftViewport mSoftViewport;

	struct VDTDataHashPred {
		size_t operator()(const VDTData& x) const;
		bool operator()(const VDTData& x, const VDTData& y) const;
	};

	vdhashmap<VDTData, IVDTVertexProgram *, VDTDataHashPred, VDTDataHashPred> mVPCache;
	vdhashmap<VDTData, IVDTFragmentProgram *, VDTDataHashPred, VDTDataHashPred> mFPCache;
	vdhashmap<VDTData, IVDTComputeProgram *, VDTDataHashPred, VDTDataHashPred> mCPCache;

	bool mbTraceRTs = false;
	vdvector<vdrefptr<IVDTSurface>> mTracedRTs;
};

struct VDDisplaySourceTexMapping {
	vdfloat2 mUVOffset;
	vdfloat2 mTexelOffset;
	vdfloat2 mUVSize;
	vdfloat2 mTexelSize;
	uint32 mTexWidth;
	uint32 mTexHeight;

	void Init(uint32 w, uint32 h, uint32 texw, uint32 texh) {
		mUVOffset = vdfloat2{0, 0};
		mTexelOffset = vdfloat2{0, 0};
		mUVSize.set((float)w / (float)texw, (float)h / (float)texh);
		mTexelSize.set(w, h);
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

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, float outx, float outy, float outw, float outh, uint32 w, uint32 h, bool format, VDDisplayNode3D *child);
	bool InitWithFormat(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, float outx, float outy, float outw, float outh, uint32 w, uint32 h, VDTFormat format, VDDisplayNode3D *child);
	void Shutdown();

	VDDisplaySourceTexMapping GetTextureMapping() const;
	IVDTTexture2D *Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx);

private:
	struct Vertex;

	float mDestX = 0;
	float mDestY = 0;
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
	void SetDestArea(float x, float y, float w, float h) { mDstX = x; mDstY = y; mDstW = w; mDstH = h; }

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

	RenderMode	mRenderMode;
	bool	mbRender2T;
	bool	mbBilinear;
	float	mDstX;
	float	mDstY;
	float	mDstW;
	float	mDstH;
	uint32	mTexWidth;
	uint32	mTexHeight;
	uint32	mTex2Width;
	uint32	mTex2Height;

	VDDisplayCommandList3D mMeshPool;

	uint32	mLastPalette[256] {};
};

///////////////////////////////////////////////////////////////////////////
// VDDisplayBlitNode3D
//
// This node does a simple render of a source node using point, bilinear,
// or sharp bilinear filtering.
//
class VDDisplayBlitNode3D final : public VDDisplayNode3D {
public:
	VDDisplayBlitNode3D();
	~VDDisplayBlitNode3D();

	void SetDestArea(float x, float y, float w, float h) { mDstX = x; mDstY = y; mDstW = w; mDstH = h; }

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 w, uint32 h, bool linear, float sharpnessX, float sharpnessY, VDDisplaySourceNode3D *source);
	void Shutdown();

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView);

private:
	struct Vertex;

	VDDisplaySourceNode3D *mpSourceNode = nullptr;
	float	mDstX = 0;
	float	mDstY = 0;
	float	mDstW = 1;
	float	mDstH = 1;
	VDDisplaySourceTexMapping mMapping;
	VDDisplayCommandList3D mMeshPool;
	VDDPoolTextureIndex mSourceTextureIndex {};
};

///////////////////////////////////////////////////////////////////////////

class VDDisplayStretchBicubicNode3D final : public VDDisplayNode3D {
public:
	VDDisplayStretchBicubicNode3D();
	~VDDisplayStretchBicubicNode3D();

	bool Init(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, uint32 srcw, uint32 srch, uint32 dstw, uint32 dsth, float outx, float outy, float outw, float outh, VDDisplaySourceNode3D *child);
	void Shutdown();

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView) override;

private:
	struct Vertex;

	IVDTVertexFormat *mpVF = nullptr;

	IVDTTexture2D *mpRTTHoriz = nullptr;
	IVDTTexture2D *mpFilterTex = nullptr;
	vdrefptr<VDDisplaySourceNode3D> mpSourceNode;

	uint32	mSrcW = 0;
	uint32	mSrcH = 0;
	uint32	mDstW = 0;
	uint32	mDstH = 0;
	float	mOutX = 0;
	float	mOutY = 0;
	float	mOutW = 0;
	float	mOutH = 0;
	VDDPoolTextureIndex mSourceTex {};
	VDDPoolRenderViewId mOutputView {};

	VDDisplayCommandList3D mMeshPool;
};

///////////////////////////////////////////////////////////////////////////

class VDDisplayScreenFXNode3D : public VDDisplayNode3D {
public:
	VDDisplayScreenFXNode3D();
	~VDDisplayScreenFXNode3D();

	const vdrect32 GetDestArea() const;

	struct Params {
		uint32 mSrcH;
		float mDstX;
		float mDstY;
		float mDstW;
		float mDstH;
		sint32 mClipDstX;
		sint32 mClipDstY;
		uint32 mClipDstW;
		uint32 mClipDstH;
		bool mbLinear;
		float mOutputGamma;
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

	IVDTTexture2D *mpGammaRampTex = nullptr;
	IVDTTexture2D *mpScanlineMaskTex = nullptr;
	vdrefptr<VDDisplaySourceNode3D> mpSourceNode;

	VDDisplaySourceTexMapping mMapping {};
	Params mParams {};
	float mScanlineMaskVBase = 0;
	float mScanlineMaskVScale = 0;
	
	VDDPoolTextureIndex mSourceTextureIndex {};
	VDDPoolRenderViewId mOutputViewId {};

	VDDisplayCommandList3D mMeshPool;

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

	vdrefptr<VDDisplaySourceNode3D> mpSourceNode;
	VDDisplayCommandList3D mMeshPool;
	VDDPoolTextureIndex mSourceTextureIndex {};

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
		sint32 mClipX;
		sint32 mClipY;
		uint32 mClipW;
		uint32 mClipH;
		float mThreshold;
		float mBlurBaseRadius;
		float mBlurAdjustRadius;
		float mDirectIntensity;
		float mIndirectIntensity;
		bool mbRenderLinear;
	};

	bool InitV1(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const Params& params, VDDisplaySourceNode3D *child);
	bool InitV2(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const Params& params, bool inputLinear, VDDisplaySourceNode3D *child);
	void Shutdown();

	void Draw(IVDTContext& ctx, VDDisplayNodeContext3D& dctx, const VDDRenderView& renderView) override;

private:
	bool RemakeV2(IVDTContext& ctx, VDDisplayNodeContext3D& dctx);

	struct Vertex;
	struct CpBloomV2Final;

	Params mParams {};
	uint32 mBlurW = 0;
	uint32 mBlurH = 0;
	uint32 mBlurW2 = 0;
	uint32 mBlurH2 = 0;
	bool mbPrescale2x = false;
	uint32 mChangeCounter = 0;

	VDDPoolCommandIndex mCommandIndexStart {};
	VDDPoolCommandIndex mCommandIndexDown {};
	VDDPoolCommandIndex mCommandIndexUp {};
	VDDPoolCommandIndex mCommandIndexFinal {};

	VDDisplaySourceNode3D *mpSourceNode = nullptr;
	bool mbSourceLinear = false;

	VDDPoolTextureIndex mSourceTextureIndex;
	VDDPoolRenderViewId mOutputViewId;

	VDDisplayCommandList3D mMeshPool;

	VDDisplaySourceTexMapping mMapping {};
};

#endif
