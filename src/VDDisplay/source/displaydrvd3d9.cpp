//	VirtualDub - Video processing and capture application
//	A/V interface library
//	Copyright (C) 1998-2005 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
//
//	This file is the DirectX 9 driver for the video display subsystem.
//	It does traditional point sampled and bilinearly filtered upsampling
//	as well as a special multipass algorithm for emulated bicubic
//	filtering.
//

#include <vd2/system/vdtypes.h>

#define DIRECTDRAW_VERSION 0x0900
#define INITGUID
#include <d3d9.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/vdalloc.h>
#include <vd2/system/error.h>
#include <vd2/system/binary.h>
#include <vd2/system/refcount.h>
#include <vd2/system/math.h>
#include <vd2/system/seh.h>
#include <vd2/system/time.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/blitter.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/text.h>
#include <vd2/Kasumi/region.h>

#include <vd2/VDDisplay/direct3d.h>
#include <displaydrvd3d9.h>
#include <vd2/VDDisplay/compositor.h>
#include <vd2/VDDisplay/displaydrv.h>
#include <vd2/VDDisplay/renderer.h>
#include <vd2/VDDisplay/textrenderer.h>
#include <vd2/VDDisplay/internal/customshaderd3d9.h>

namespace nsVDDisplay {
	#include "displayd3d9_shader.inl"
}

#ifdef _MSC_VER
#pragma warning(disable: 4351)		// warning C4351: new behavior: elements of array 'VDVideoUploadContextD3D9::mpD3DImageTextures' will be default initialized
#endif

#define VDDEBUG_DX9DISP VDDEBUG
//#define VDDEBUG_DX9DISP (void)sizeof

#define D3D_DO(x) VDVERIFY(SUCCEEDED(mpD3DDevice->x))

using namespace nsVDD3D9;

bool VDCreateD3D9TextureGeneratorFullSizeRTT(IVDD3D9TextureGenerator **ppGenerator);
bool VDCreateD3D9TextureGeneratorFullSizeRTT16F(IVDD3D9TextureGenerator **ppGenerator);

///////////////////////////////////////////////////////////////////////////

class VDD3D9TextureGeneratorDither final : public vdrefcounted<IVDD3D9TextureGenerator> {
public:
	bool GenerateTexture(VDD3D9Manager *pManager, IVDD3D9Texture *pTexture) {
		static const uint8 dither[16][16]={
			{  35, 80,227,165, 64,199, 42,189,138, 74,238,111, 43,153, 13,211 },
			{ 197,135, 20, 99,244,  4,162,105, 25,210, 38,134,225, 78,242, 87 },
			{  63,249,126,192, 50,174, 82,251,116,148, 97,176, 19,167, 52,163 },
			{ 187, 30, 85,142,219, 71,194, 45,169, 11,241, 58,216,106,204,  5 },
			{  94,151,235,  9,112,155, 17,224, 91,206, 84,188,120, 36,132,233 },
			{ 177, 48,124,201, 40,239,125, 66,180, 51,160,  7,152,255, 89, 56 },
			{  16,209, 72,161,121, 59,208,150, 28,248, 75,229,101, 26,140,220 },
			{ 170,110,226, 22,252,139,  1,109,195,115,172, 39,200,114,191, 68 },
			{ 136, 34, 96,183, 44,175, 95,234, 81, 15,143,217, 62,164,  2,237 },
			{  57,245,154, 61,203, 70,213, 37,137,243, 98, 23,179, 86,198,103 },
			{ 184, 12,123,221,  6,129,156, 88,185, 53,127,228, 49,250, 31,130 },
			{  77,205, 83,145,107,247, 29,223, 10,212,159, 79,168, 73,146,232 },
			{ 173, 41,240, 24,190, 54,178,102,149,118, 33,202,  8,215,119, 18 },
			{  92,158, 67,166, 76,207,133, 47,254, 65,230,100,131,157, 69,193 },
			{ 253,  3,214,117,231, 14, 93,171, 21,182,144, 55,246, 27,222, 46 },
			{ 141,181,104, 32,147,113,236, 60,218,122,  0,196, 90,186,128,108 },
		};

		IDirect3DDevice9 *dev = pManager->GetDevice();
		vdrefptr<IVDD3D9InitTexture> tex;
		if (!pManager->CreateInitTexture(16, 16, 1, D3DFMT_A8R8G8B8, ~tex))
			return false;

		VDD3D9LockInfo lockInfo;
		if (!tex->Lock(0, lockInfo)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load horizontal even/odd texture.\n");
			return false;
		}

		char *dst = (char *)lockInfo.mpData;
		for(int y=0; y<16; ++y) {
			const uint8 *srcrow = dither[y];
			uint32 *dstrow = (uint32 *)dst;

			for(int x=0; x<16; ++x)
				dstrow[x] = 0x01010101 * srcrow[x];

			dst += lockInfo.mPitch;
		}

		tex->Unlock(0);

		return pTexture->Init(tex);
	}
};

class VDD3D9TextureGeneratorHEvenOdd final : public vdrefcounted<IVDD3D9TextureGenerator> {
public:
	bool GenerateTexture(VDD3D9Manager *pManager, IVDD3D9Texture *pTexture) {
		IDirect3DDevice9 *dev = pManager->GetDevice();
		vdrefptr<IVDD3D9InitTexture> tex;
		if (!pManager->CreateInitTexture(16, 1, 1, D3DFMT_A8R8G8B8, ~tex))
			return false;

		VDD3D9LockInfo lockInfo;
		if (!tex->Lock(0, lockInfo)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load horizontal even/odd texture.\n");
			return false;
		}

		for(int i=0; i<16; ++i)
			((uint32 *)lockInfo.mpData)[i] = (uint32)-(sint32)(i&1);

		tex->Unlock(0);

		return pTexture->Init(tex);
	}
};

class VDD3D9TextureGeneratorCubicFilter : public vdrefcounted<IVDD3D9TextureGenerator> {
public:
	VDD3D9TextureGeneratorCubicFilter(IVDVideoDisplayDX9Manager::CubicMode mode) : mCubicMode(mode) {}

	bool GenerateTexture(VDD3D9Manager *pManager, IVDD3D9Texture *pTexture) {
		IDirect3DDevice9 *dev = pManager->GetDevice();
		vdrefptr<IVDD3D9InitTexture> tex;
		if (!pManager->CreateInitTexture(256, 4, 1, D3DFMT_A8R8G8B8, ~tex))
			return false;

		VDD3D9LockInfo lr;
		if (!tex->Lock(0, lr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load cubic filter texture.\n");
			return false;
		}

		MakeCubic4Texture((uint32 *)lr.mpData, lr.mPitch, -0.75, mCubicMode);

		tex->Unlock(0);

		return pTexture->Init(tex);
	}

protected:
	IVDVideoDisplayDX9Manager::CubicMode mCubicMode;

	static void MakeCubic4Texture(uint32 *texture, ptrdiff_t pitch, double A, IVDVideoDisplayDX9Manager::CubicMode mode) {
		int i;

		uint32 *p0 = texture;
		uint32 *p1 = vdptroffset(texture, pitch);
		uint32 *p2 = vdptroffset(texture, pitch*2);
		uint32 *p3 = vdptroffset(texture, pitch*3);

		for(i=0; i<256; i++) {
			double d = (double)(i&63) / 64.0;
			int y1, y2, y3, y4, ydiff;

			// Coefficients for all four pixels *must* add up to 1.0 for
			// consistent unity gain.
			//
			// Two good values for A are -1.0 (original VirtualDub bicubic filter)
			// and -0.75 (closely matches Photoshop).

			double c1 =         +     A*d -       2.0*A*d*d +       A*d*d*d;
			double c2 = + 1.0             -     (A+3.0)*d*d + (A+2.0)*d*d*d;
			double c3 =         -     A*d + (2.0*A+3.0)*d*d - (A+2.0)*d*d*d;
			double c4 =                   +           A*d*d -       A*d*d*d;

			const int maxval = 255;
			double scale = maxval / (c1 + c2 + c3 + c4);

			y1 = (int)floor(0.5 + c1 * scale);
			y2 = (int)floor(0.5 + c2 * scale);
			y3 = (int)floor(0.5 + c3 * scale);
			y4 = (int)floor(0.5 + c4 * scale);

			ydiff = maxval - y1 - y2 - y3 - y4;

			int ywhole = ydiff<0 ? (ydiff-2)/4 : (ydiff+2)/4;
			ydiff -= ywhole*4;

			y1 += ywhole;
			y2 += ywhole;
			y3 += ywhole;
			y4 += ywhole;

			if (ydiff < 0) {
				if (y1<y4)
					y1 += ydiff;
				else
					y4 += ydiff;
			} else if (ydiff > 0) {
				if (y2 > y3)
					y2 += ydiff;
				else
					y3 += ydiff;
			}

			switch(mode) {
			case IVDVideoDisplayDX9Manager::kCubicUsePS1_4Path:
				p0[i] = (-y1 << 24) + (y2 << 16) + (y3 << 8) + (-y4);
				break;

			case IVDVideoDisplayDX9Manager::kCubicUsePS1_1Path:
				p0[i] = -y1 * 0x010101 + (-y4 << 24);
				p1[i] = y2 * 0x010101 + (y3<<24);

				p2[i] = -y1 * 0x010101 + (-y4 << 24);
				p3[i] = y2 * 0x010101 + (y3<<24);
				break;
			}
		}
	}
};

class VDD3D9TextureGeneratorCubicFilterPS1_1 final : public VDD3D9TextureGeneratorCubicFilter {
public:
	VDD3D9TextureGeneratorCubicFilterPS1_1() : VDD3D9TextureGeneratorCubicFilter(IVDVideoDisplayDX9Manager::kCubicUsePS1_1Path) {}
};

class VDD3D9TextureGeneratorCubicFilterPS1_4 final : public VDD3D9TextureGeneratorCubicFilter {
public:
	VDD3D9TextureGeneratorCubicFilterPS1_4() : VDD3D9TextureGeneratorCubicFilter(IVDVideoDisplayDX9Manager::kCubicUsePS1_4Path) {}
};

///////////////////////////////////////////////////////////////////////////

#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable: 4584)		// warning C4584: 'VDVideoDisplayDX9Manager' : base-class 'vdlist_node' is already a base-class of 'VDD3D9Client'
#endif

struct VDVideoDisplayDX9ManagerNode : public vdlist_node {};

class VDVideoDisplayDX9Manager final : public IVDVideoDisplayDX9Manager, public VDD3D9Client, public VDVideoDisplayDX9ManagerNode {
public:
	VDVideoDisplayDX9Manager(VDThreadID tid, HMONITOR hmonitor, bool use9ex);
	~VDVideoDisplayDX9Manager();

	int AddRef();
	int Release();

	bool Init();
	void Shutdown();

	CubicMode InitBicubic();
	void ShutdownBicubic();

	bool InitBicubicTempSurfaces(bool highPrecision);
	void ShutdownBicubicTempSurfaces(bool highPrecision);

	bool IsD3D9ExEnabled() const { return mbUseD3D9Ex; }
	bool Is16FEnabled() const { return mbIs16FEnabled; }
	bool IsPS11Enabled() const { return mbIsPS11Enabled; }
	bool IsPS20Enabled() const override { return mbIsPS20Enabled; }

	VDThreadID GetThreadId() const { return mThreadId; }
	HMONITOR GetMonitor() const { return mhMonitor; }

	IVDD3D9Texture	*GetTempRTT(int i) const { return mpRTTs[i]; }
	IVDD3D9Texture	*GetFilterTexture() const { return mpFilterTexture; }
	IVDD3D9Texture	*GetHEvenOddTexture() const { return mpHEvenOddTexture; }

	void		DetermineBestTextureFormat(int srcFormat, int& dstFormat, D3DFORMAT& dstD3DFormat);

	bool ValidateBicubicShader(CubicMode mode);

	bool RunEffect(const EffectContext& ctx, const nsVDDisplay::TechniqueInfo& technique, IDirect3DSurface9 *pRTOverride) override;

public:
	void OnPreDeviceReset() {}
	void OnPostDeviceReset() {}

protected:
	bool RunEffect2(const EffectContext& ctx, const nsVDDisplay::TechniqueInfo& technique, IDirect3DSurface9 *pRTOverride);
	bool InitEffect();
	void ShutdownEffect();

	VDD3D9Manager		*mpManager;
	vdrefptr<IVDD3D9Texture>	mpFilterTexture;
	vdrefptr<IVDD3D9Texture>	mpHEvenOddTexture;
	vdrefptr<IVDD3D9Texture>	mpDitherTexture;
	vdrefptr<IVDD3D9Texture>	mpRTTs[3];

	vdfastvector<IDirect3DVertexShader9 *>	mVertexShaders;
	vdfastvector<IDirect3DPixelShader9 *>	mPixelShaders;

	CubicMode			mCubicMode;
	int					mCubicRefCount;
	int					mCubicTempSurfacesRefCount[2];
	bool				mbIs16FEnabled;
	bool				mbIsPS11Enabled;
	bool				mbIsPS20Enabled;
	bool				mbUseD3D9Ex;

	const VDThreadID	mThreadId;
	const HMONITOR		mhMonitor;
	int					mRefCount;
};

#ifdef _MSC_VER
	#pragma warning(pop)
#endif

///////////////////////////////////////////////////////////////////////////

class VDFontRendererD3D9 final : public vdrefcounted<IVDFontRendererD3D9> {
public:
	VDFontRendererD3D9();

	bool Init(VDD3D9Manager *d3dmgr);
	void Shutdown();

	bool Begin();
	void DrawTextLine(int x, int y, uint32 textColor, uint32 outlineColor, const char *s);
	void End();

protected:
	VDD3D9Manager *mpD3DManager;
	vdrefptr<IDirect3DTexture9> mpD3DFontTexture;

	struct GlyphLayoutInfo {
		int		mGlyph;
		float	mX;
	};

	typedef vdfastvector<GlyphLayoutInfo> GlyphLayoutInfos;
	GlyphLayoutInfos mGlyphLayoutInfos;

	struct GlyphInfo {
		vdrect32f	mPos;
		vdrect32f	mUV;
		float		mAdvance;
	};

	GlyphInfo mGlyphInfo[256];
};

bool VDCreateFontRendererD3D9(IVDFontRendererD3D9 **pp) {
	*pp = new_nothrow VDFontRendererD3D9();
	if (*pp)
		(*pp)->AddRef();
	return *pp != NULL;
}

VDFontRendererD3D9::VDFontRendererD3D9()
	: mpD3DManager(NULL)
{
}

bool VDFontRendererD3D9::Init(VDD3D9Manager *d3dmgr) {
	mpD3DManager = d3dmgr;

	vdfastvector<uint32> tempbits(256*256, 0);
	VDPixmap temppx={0};
	temppx.data = tempbits.data();
	temppx.w = 256;
	temppx.h = 256;
	temppx.format = nsVDPixmap::kPixFormat_XRGB8888;
	temppx.pitch = 256*sizeof(uint32);

	VDTextLayoutMetrics metrics;
	VDPixmapPathRasterizer rast;

	VDPixmapRegion outlineRegion;
	VDPixmapRegion charRegion;
	VDPixmapRegion charOutlineRegion;
	VDPixmapCreateRoundRegion(outlineRegion, 16.0f);

	static const float kFontSize = 16.0f;

	int x = 1;
	int y = 1;
	int lineHeight = 0;
	GlyphInfo *pgi = mGlyphInfo;
	for(int c=0; c<256; ++c) {
		char s[2]={(char)c, 0};

		VDPixmapGetTextExtents(NULL, kFontSize, s, metrics);

		if (metrics.mExtents.valid()) {
			int x1 = VDCeilToInt(metrics.mExtents.left * 8.0f - 0.5f);
			int y1 = VDCeilToInt(metrics.mExtents.top * 8.0f - 0.5f);
			int x2 = VDCeilToInt(metrics.mExtents.right * 8.0f - 0.5f);
			int y2 = VDCeilToInt(metrics.mExtents.bottom * 8.0f - 0.5f);
			int ix1 = x1 >> 3;
			int iy1 = y1 >> 3;
			int ix2 = (x2 + 7) >> 3;
			int iy2 = (y2 + 7) >> 3;
			int w = (ix2 - ix1) + 4;
			int h = (iy2 - iy1) + 4;

			if (x + w > 255) {
				x = 1;
				y += lineHeight + 1;
				lineHeight = 0;
			}

			if (lineHeight < h) {
				lineHeight = h;
				VDASSERT(lineHeight+y < 255);
			}

			rast.Clear();
			VDPixmapConvertTextToPath(rast, NULL, kFontSize * 64.0f, (float)(-ix1*64), (float)(-iy1*64), s);
			rast.ScanConvert(charRegion);
			VDPixmapConvolveRegion(charOutlineRegion, charRegion, outlineRegion);

			VDPixmapFillRegionAntialiased8x(temppx, charOutlineRegion, x*8+16, y*8+16, 0x000000FF);
			VDPixmapFillRegionAntialiased8x(temppx, charRegion, x*8+16, y*8+16, 0xFFFFFFFF);

			pgi->mAdvance = metrics.mAdvance;
			pgi->mPos.set((float)(ix1 - 2), (float)(iy1 - 2), (float)(ix2 + 2), (float)(iy2 + 2));
			pgi->mUV.set((float)x, (float)y, (float)(x+w), (float)(y+h));
			pgi->mUV.scale(1.0f / 256.0f, 1.0f / 256.0f);

			x += w+1;
		} else {
			pgi->mAdvance = metrics.mAdvance;
			pgi->mPos.clear();
			pgi->mUV.clear();
		}
		++pgi;
	}

	// create texture
	IDirect3DDevice9 *dev = mpD3DManager->GetDevice();
	const bool useDefault = mpD3DManager->GetDeviceEx() != NULL;

	HRESULT hr = dev->CreateTexture(256, 256, 1, 0, D3DFMT_A8R8G8B8, useDefault ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, ~mpD3DFontTexture, NULL);
	if (FAILED(hr)) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to create font cache texture.\n");
		Shutdown();
		return false;
	}

	vdrefptr<IDirect3DTexture9> uploadtex;
	if (useDefault) {
		hr = dev->CreateTexture(256, 256, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, ~uploadtex, NULL);
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to create font cache texture.\n");
			Shutdown();
			return false;
		}
	} else {
		uploadtex = mpD3DFontTexture;
	}

	// copy into texture
	D3DLOCKED_RECT lr;
	hr = uploadtex->LockRect(0, &lr, NULL, 0);
	VDASSERT(SUCCEEDED(hr));
	if (FAILED(hr)) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load font cache texture.\n");
		Shutdown();
		return false;
	}
	
	uint32 *dst = (uint32 *)lr.pBits;
	const uint32 *src = tempbits.data();
	for(int y=0; y<256; ++y) {
		for(int x=0; x<256; ++x) {
			uint32 c = src[x];
			dst[x] = ((c >> 8) & 0xff) * 0x010101 + (c << 24);
		}

		src += 256;
		vdptrstep(dst, lr.Pitch);
	}

	VDVERIFY(SUCCEEDED(uploadtex->UnlockRect(0)));

	if (uploadtex != mpD3DFontTexture) {
		hr = dev->UpdateTexture(uploadtex, mpD3DFontTexture);
		if (FAILED(hr)) {
			Shutdown();
			return false;
		}
	}

	return true;
}

void VDFontRendererD3D9::Shutdown() {
	mpD3DFontTexture = NULL;
	mpD3DManager = NULL;
}

bool VDFontRendererD3D9::Begin() {
	if (!mpD3DManager)
		return false;

	IDirect3DDevice9 *dev = mpD3DManager->GetDevice();

	D3DVIEWPORT9 vp;
	HRESULT hr = dev->GetViewport(&vp);
	if (FAILED(hr))
		return false;

	const D3DMATRIX ident={
		{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}
	};

	dev->SetTransform(D3DTS_WORLD, &ident);
	dev->SetTransform(D3DTS_VIEW, &ident);

	const D3DMATRIX proj = {
		{
		2.0f / (float)vp.Width, 0.0f, 0.0f, 0.0f,
		0.0f, -2.0f / (float)vp.Height, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		-1.0f - 1.0f / (float)vp.Width, 1.0f + 1.0f / (float)vp.Height, 0.0f, 1.0f
		}
	};

	dev->SetTransform(D3DTS_PROJECTION, &proj);

	dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

	dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

	// Rite of passage for any 3D programmer:
	// "Why the *&#$ didn't it draw anything!?"
	dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	dev->SetRenderState(D3DRS_LIGHTING, FALSE);
	dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	dev->SetRenderState(D3DRS_ZENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);

	dev->SetVertexShader(NULL);
	dev->SetVertexDeclaration(mpD3DManager->GetVertexDeclaration());
	dev->SetPixelShader(NULL);
	dev->SetStreamSource(0, mpD3DManager->GetVertexBuffer(), 0, sizeof(nsVDD3D9::Vertex));
	dev->SetIndices(mpD3DManager->GetIndexBuffer());
	dev->SetTexture(0, mpD3DFontTexture);

	return mpD3DManager->BeginScene();
}

void VDFontRendererD3D9::DrawTextLine(int x, int y, uint32 textColor, uint32 outlineColor, const char *s) {
	const uint32 kMaxQuads = nsVDD3D9::kVertexBufferSize / 4;
	size_t len = strlen(s);

	mGlyphLayoutInfos.clear();
	mGlyphLayoutInfos.reserve(len);

	float xpos = (float)x;
	for(size_t i=0; i<len; ++i) {
		char c = *s++;
		const GlyphInfo& gi = mGlyphInfo[(int)c & 0xff];

		if (!gi.mPos.empty()) {
			mGlyphLayoutInfos.push_back();
			GlyphLayoutInfo& gli = mGlyphLayoutInfos.back();
			gli.mGlyph = (int)c & 0xff;
			gli.mX = xpos;
		}

		xpos += gi.mAdvance;
	}

	float ypos = (float)y;

	IDirect3DDevice9 *dev = mpD3DManager->GetDevice();
	for(int i=0; i<2; ++i) {
		uint32 vertexColor;

		switch(i) {
		case 0:
			vertexColor = outlineColor;
			dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
			dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE);
			dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
			dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			break;
		case 1:
			vertexColor = textColor;
			dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
			dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
			dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
			dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
			break;
		}

		uint32 glyphCount = (uint32)mGlyphLayoutInfos.size();
		uint32 glyphStart = 0;
		const GlyphLayoutInfo *pgli = mGlyphLayoutInfos.data();
		while(glyphStart < glyphCount) {
			uint32 glyphsToRender = glyphCount - glyphStart;
			if (glyphsToRender > kMaxQuads)
				glyphsToRender = kMaxQuads;

			nsVDD3D9::Vertex *vx = mpD3DManager->LockVertices(glyphsToRender * 4);
			if (!vx)
				break;

			for(uint32 i=0; i<glyphsToRender; ++i) {
				const GlyphInfo& gi = mGlyphInfo[pgli->mGlyph];

				new(vx  ) nsVDD3D9::Vertex(pgli->mX + gi.mPos.left,  ypos + gi.mPos.top,    vertexColor, gi.mUV.left,  gi.mUV.top   );
				new(vx+1) nsVDD3D9::Vertex(pgli->mX + gi.mPos.left,  ypos + gi.mPos.bottom, vertexColor, gi.mUV.left,  gi.mUV.bottom);
				new(vx+2) nsVDD3D9::Vertex(pgli->mX + gi.mPos.right, ypos + gi.mPos.bottom, vertexColor, gi.mUV.right, gi.mUV.bottom);
				new(vx+3) nsVDD3D9::Vertex(pgli->mX + gi.mPos.right, ypos + gi.mPos.top,    vertexColor, gi.mUV.right, gi.mUV.top   );
				vx += 4;
				++pgli;
			}

			mpD3DManager->UnlockVertices();

			uint16 *idx = mpD3DManager->LockIndices(glyphsToRender * 6);
			if (!idx)
				break;

			uint32 vidx = 0;
			for(uint32 i=0; i<glyphsToRender; ++i) {
				idx[0] = vidx;
				idx[1] = vidx+1;
				idx[2] = vidx+2;
				idx[3] = vidx;
				idx[4] = vidx+2;
				idx[5] = vidx+3;
				vidx += 4;
				idx += 6;
			}

			mpD3DManager->UnlockIndices();

			mpD3DManager->DrawElements(D3DPT_TRIANGLELIST, 0, 4*glyphsToRender, 0, 2*glyphsToRender);

			glyphStart += glyphsToRender;
		}
	}
}

void VDFontRendererD3D9::End() {
	IDirect3DDevice9 *dev = mpD3DManager->GetDevice();

	dev->SetTexture(0, NULL);
}

///////////////////////////////////////////////////////////////////////////

VDDisplayCachedImageD3D9::VDDisplayCachedImageD3D9() {
	mListNodePrev = NULL;
	mListNodeNext = NULL;
}

VDDisplayCachedImageD3D9::~VDDisplayCachedImageD3D9() {
	if (mListNodePrev)
		vdlist_base::unlink(*this);
}

void *VDDisplayCachedImageD3D9::AsInterface(uint32 iid) {
	if (iid == kTypeID)
		return this;

	return NULL;
}

bool VDDisplayCachedImageD3D9::Init(VDD3D9Manager *mgr, void *owner, const VDDisplayImageView& imageView) {
	const VDPixmap& px = imageView.GetImage();
	int w = px.w;
	int h = px.h;

	if (!mgr->AdjustTextureSize(w, h, true))
		return false;

	IDirect3DDevice9 *dev = mgr->GetDevice();
	HRESULT hr = dev->CreateTexture(w, h, 1, 0, D3DFMT_X8R8G8B8, mgr->GetDeviceEx() ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, ~mpD3DTexture, NULL);
	if (FAILED(hr))
		return false;

	mWidth = px.w;
	mHeight = px.h;
	mTexWidth = w;
	mTexHeight = h;
	mpOwner = owner;
	mUniquenessCounter = imageView.GetUniquenessCounter() - 2;

	Update(imageView);
	return true;
}

void VDDisplayCachedImageD3D9::Shutdown() {
	mpD3DTexture.clear();
	mpOwner = NULL;
}

void VDDisplayCachedImageD3D9::Update(const VDDisplayImageView& imageView) {
	uint32 newCounter = imageView.GetUniquenessCounter();
	bool partialUpdateOK = ((mUniquenessCounter + 1) == newCounter);

	mUniquenessCounter = newCounter;

	if (mpD3DTexture) {
		const VDPixmap& px = imageView.GetImage();

		const uint32 numRects = imageView.GetDirtyListSize();

		D3DLOCKED_RECT lr;

		if (partialUpdateOK && numRects) {
			const vdrect32 *rects = imageView.GetDirtyList();

			if (SUCCEEDED(mpD3DTexture->LockRect(0, &lr, NULL, D3DLOCK_NO_DIRTY_UPDATE))) {
				VDPixmap dst = {};
				dst.format = nsVDPixmap::kPixFormat_XRGB8888;
				dst.w = mTexWidth;
				dst.h = mTexHeight;
				dst.pitch = lr.Pitch;
				dst.data = lr.pBits;

				for(uint32 i=0; i<numRects; ++i) {
					const vdrect32& r = rects[i];

					VDPixmapBlt(dst, r.left, r.top, px, r.left, r.top, r.width(), r.height());
				}

				mpD3DTexture->UnlockRect(0);

				for(uint32 i=0; i<numRects; ++i) {
					const vdrect32& r = rects[i];

					RECT r2 = {r.left, r.top, r.right, r.bottom};
					mpD3DTexture->AddDirtyRect(&r2);
				}
			}
		} else {
			if (SUCCEEDED(mpD3DTexture->LockRect(0, &lr, NULL, 0))) {
				VDPixmap dst = {};
				dst.format = nsVDPixmap::kPixFormat_XRGB8888;
				dst.w = mTexWidth;
				dst.h = mTexHeight;
				dst.pitch = lr.Pitch;
				dst.data = lr.pBits;

				VDPixmapBlt(dst, px);

				mpD3DTexture->UnlockRect(0);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////

static VDCriticalSection g_csVDDisplayDX9Managers;
static vdlist<VDVideoDisplayDX9ManagerNode> g_VDDisplayDX9Managers;

bool VDInitDisplayDX9(HMONITOR hmonitor, bool use9ex, VDVideoDisplayDX9Manager **ppManager) {
	VDVideoDisplayDX9Manager *pMgr = NULL;
	bool firstClient = false;

	vdsynchronized(g_csVDDisplayDX9Managers) {
		vdlist<VDVideoDisplayDX9ManagerNode>::iterator it(g_VDDisplayDX9Managers.begin()), itEnd(g_VDDisplayDX9Managers.end());

		VDThreadID tid = VDGetCurrentThreadID();

		for(; it != itEnd; ++it) {
			VDVideoDisplayDX9Manager *mgr = static_cast<VDVideoDisplayDX9Manager *>(*it);

			if (mgr->GetThreadId() == tid && mgr->GetMonitor() == hmonitor && mgr->IsD3D9ExEnabled() == use9ex) {
				pMgr = mgr;
				break;
			}
		}

		if (!pMgr) {
			pMgr = new_nothrow VDVideoDisplayDX9Manager(tid, hmonitor, use9ex);
			if (!pMgr)
				return false;

			g_VDDisplayDX9Managers.push_back(pMgr);
			firstClient = true;
		}

		pMgr->AddRef();
	}

	if (firstClient) {
		if (!pMgr->Init()) {
			vdsynchronized(g_csVDDisplayDX9Managers) {
				g_VDDisplayDX9Managers.erase(pMgr);
			}
			pMgr->Release();
			return NULL;
		}
	}

	*ppManager = pMgr;
	return true;
}

VDVideoDisplayDX9Manager::VDVideoDisplayDX9Manager(VDThreadID tid, HMONITOR hmonitor, bool use9ex)
	: mpManager(NULL)
	, mCubicRefCount(0)
	, mThreadId(tid)
	, mhMonitor(hmonitor)
	, mRefCount(0)
	, mbIs16FEnabled(false)
	, mbIsPS20Enabled(false)
	, mbUseD3D9Ex(use9ex)
{
	mCubicTempSurfacesRefCount[0] = 0;
	mCubicTempSurfacesRefCount[1] = 0;
}

VDVideoDisplayDX9Manager::~VDVideoDisplayDX9Manager() {
	VDASSERT(!mRefCount);
	VDASSERT(!mCubicRefCount);
	VDASSERT(!mCubicTempSurfacesRefCount[0]);
	VDASSERT(!mCubicTempSurfacesRefCount[1]);

	vdsynchronized(g_csVDDisplayDX9Managers) {
		g_VDDisplayDX9Managers.erase(this);
	}
}

int VDVideoDisplayDX9Manager::AddRef() {
	return ++mRefCount;
}

int VDVideoDisplayDX9Manager::Release() {
	int rc = --mRefCount;
	if (!rc) {
		Shutdown();
		delete this;
	}
	return rc;
}

bool VDVideoDisplayDX9Manager::Init() {
	VDASSERT(!mpManager);
	mpManager = VDInitDirect3D9(this, mhMonitor, mbUseD3D9Ex);
	if (!mpManager)
		return false;

	// Check for 16F capability.
	//
	// We need:
	//	* Vertex and pixel shader 2.0.
	//	* 16F texture support.
	//	* 16F blending.

	const D3DCAPS9& caps = mpManager->GetCaps();

	mbIs16FEnabled = false;

	mbIsPS11Enabled = false;
	if (caps.VertexShaderVersion >= D3DVS_VERSION(1, 1) &&
		caps.PixelShaderVersion >= D3DPS_VERSION(1, 1))
	{
		mbIsPS11Enabled = true;
	}

	if (caps.VertexShaderVersion >= D3DVS_VERSION(2, 0) &&
		caps.PixelShaderVersion >= D3DPS_VERSION(2, 0))
	{
		mbIsPS20Enabled = true;

		if (mpManager->CheckResourceFormat(0, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F) &&
			mpManager->CheckResourceFormat(D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F))
		{
			mbIs16FEnabled = true;
		}
	}

	if (!mpManager->CreateSharedTexture<VDD3D9TextureGeneratorDither>("dither", ~mpDitherTexture)) {
		Shutdown();
		return false;
	}

	if (!mpManager->CreateSharedTexture<VDD3D9TextureGeneratorHEvenOdd>("hevenodd", ~mpHEvenOddTexture)) {
		Shutdown();
		return false;
	}

	if (!InitEffect()) {
		Shutdown();
		return false;
	}

	return true;
}

void VDVideoDisplayDX9Manager::Shutdown() {
	VDASSERT(!mCubicRefCount);
	VDASSERT(!mCubicTempSurfacesRefCount[0]);
	VDASSERT(!mCubicTempSurfacesRefCount[1]);

	mpDitherTexture = NULL;
	mpHEvenOddTexture = NULL;

	ShutdownEffect();

	if (mpManager) {
		VDDeinitDirect3D9(mpManager, this);
		mpManager = NULL;
	}
}

bool VDVideoDisplayDX9Manager::InitEffect() {
	using namespace nsVDDisplay;

	IDirect3DDevice9 *pD3DDevice = mpManager->GetDevice();
	const D3DCAPS9& caps = mpManager->GetCaps();

	// initialize vertex shaders
	if (g_effect.mVertexShaderCount && mVertexShaders.empty()) {
		mVertexShaders.resize(g_effect.mVertexShaderCount, NULL);
		for(uint32 i=0; i<g_effect.mVertexShaderCount; ++i) {
			const uint32 *pVertexShaderData = g_shaderData + g_effect.mVertexShaderOffsets[i];

			if ((pVertexShaderData[0] & 0xffff) > (caps.VertexShaderVersion & 0xffff))
				continue;

			HRESULT hr = pD3DDevice->CreateVertexShader((const DWORD *)pVertexShaderData, &mVertexShaders[i]);
			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Unable to create vertex shader #%d.\n", i+1);
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Vertex shader version is: %x.\n", pVertexShaderData[0]);
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Supported vertex shader version is: %x.\n", caps.VertexShaderVersion);
				return false;
			}
		}
	}

	// initialize pixel shaders
	if (g_effect.mPixelShaderCount && mPixelShaders.empty()) {
		mPixelShaders.resize(g_effect.mPixelShaderCount, NULL);
		for(uint32 i=0; i<g_effect.mPixelShaderCount; ++i) {
			const uint32 *pPixelShaderData = g_shaderData + g_effect.mPixelShaderOffsets[i];

			if ((pPixelShaderData[0] & 0xffff) > (caps.PixelShaderVersion & 0xffff))
				continue;

			HRESULT hr = pD3DDevice->CreatePixelShader((const DWORD *)pPixelShaderData, &mPixelShaders[i]);
			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Unable to create pixel shader #%d.\n", i+1);
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Pixel shader version is: %x.\n", pPixelShaderData[0]);
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Supported pixel shader version is: %x.\n", caps.PixelShaderVersion);
				return false;
			}
		}
	}

	return true;
}

void VDVideoDisplayDX9Manager::ShutdownEffect() {
	while(!mPixelShaders.empty()) {
		IDirect3DPixelShader9 *ps = mPixelShaders.back();
		mPixelShaders.pop_back();

		if (ps)
			ps->Release();
	}

	while(!mVertexShaders.empty()) {
		IDirect3DVertexShader9 *vs = mVertexShaders.back();
		mVertexShaders.pop_back();

		if (vs)
			vs->Release();
	}
}

VDVideoDisplayDX9Manager::CubicMode VDVideoDisplayDX9Manager::InitBicubic() {
	VDASSERT(mRefCount > 0);
	VDASSERT(mCubicRefCount >= 0);

	if (++mCubicRefCount > 1)
		return mCubicMode;

	mCubicMode = (CubicMode)kMaxCubicMode;
	while(mCubicMode > kCubicNotPossible) {
		if (ValidateBicubicShader(mCubicMode))
			break;
		mCubicMode = (CubicMode)(mCubicMode - 1);
	}

	if (mCubicMode == kCubicNotPossible)
		ShutdownBicubic();

	return mCubicMode;
}

void VDVideoDisplayDX9Manager::ShutdownBicubic() {
	VDASSERT(mCubicRefCount > 0);
	if (--mCubicRefCount)
		return;

	mpFilterTexture = NULL;
}

bool VDVideoDisplayDX9Manager::InitBicubicTempSurfaces(bool highPrecision) {
	VDASSERT(mRefCount > 0);
	VDASSERT(mCubicTempSurfacesRefCount[highPrecision] >= 0);

	if (++mCubicTempSurfacesRefCount[highPrecision] > 1)
		return true;

	if (highPrecision) {
		if (!mbIs16FEnabled) {
			ShutdownBicubicTempSurfaces(highPrecision);
			return false;
		}

		if (!mpManager->CreateSharedTexture("rtt3", VDCreateD3D9TextureGeneratorFullSizeRTT16F, ~mpRTTs[2])) {
			ShutdownBicubicTempSurfaces(highPrecision);
			return false;
		}
	} else {
		// create horizontal resampling texture
		if (!mpManager->CreateSharedTexture("rtt1", VDCreateD3D9TextureGeneratorFullSizeRTT, ~mpRTTs[0])) {
			ShutdownBicubicTempSurfaces(highPrecision);
			return false;
		}

		// create vertical resampling texture
		if (mCubicMode < kCubicUsePS1_1Path) {
			if (!mpManager->CreateSharedTexture("rtt2", VDCreateD3D9TextureGeneratorFullSizeRTT, ~mpRTTs[1])) {
				ShutdownBicubicTempSurfaces(highPrecision);
				return false;
			}
		}
	}

	return true;
}

void VDVideoDisplayDX9Manager::ShutdownBicubicTempSurfaces(bool highPrecision) {
	VDASSERT(mCubicTempSurfacesRefCount[highPrecision] > 0);
	if (--mCubicTempSurfacesRefCount[highPrecision])
		return;

	if (highPrecision) {
		mpRTTs[2] = NULL;
	} else {
		mpRTTs[1] = NULL;
		mpRTTs[0] = NULL;
	}
}

namespace {
	D3DFORMAT GetD3DTextureFormatForPixmapFormat(int format) {
		using namespace nsVDPixmap;

		switch(format) {
			case nsVDPixmap::kPixFormat_XRGB1555:
				return D3DFMT_X1R5G5B5;

			case nsVDPixmap::kPixFormat_RGB565:
				return D3DFMT_R5G6B5;

			case nsVDPixmap::kPixFormat_XRGB8888:
				return D3DFMT_X8R8G8B8;

			case nsVDPixmap::kPixFormat_Y8_FR:
				return D3DFMT_L8;

			default:
				return D3DFMT_UNKNOWN;
		}
	}
}

void VDVideoDisplayDX9Manager::DetermineBestTextureFormat(int srcFormat, int& dstFormat, D3DFORMAT& dstD3DFormat) {
	using namespace nsVDPixmap;

	// Try direct format first. If that doesn't work, try a fallback (in practice, we
	// only have one).

	dstFormat = srcFormat;
	for(int i=0; i<2; ++i) {
		dstD3DFormat = GetD3DTextureFormatForPixmapFormat(dstFormat);
		if (dstD3DFormat && mpManager->IsTextureFormatAvailable(dstD3DFormat)) {
			dstFormat = srcFormat;
			return;
		}

		// fallback
		switch(dstFormat) {
			case kPixFormat_XRGB1555:
				dstFormat = kPixFormat_RGB565;
				break;

			case kPixFormat_RGB565:
				dstFormat = kPixFormat_XRGB1555;
				break;

			default:
				goto fail;
		}
	}
fail:

	// Just use X8R8G8B8. We always know this works (we reject the device if it doesn't).
	dstFormat = kPixFormat_XRGB8888;
	dstD3DFormat = D3DFMT_X8R8G8B8;
}

bool VDVideoDisplayDX9Manager::ValidateBicubicShader(CubicMode mode) {
	using namespace nsVDDisplay;

	const TechniqueInfo *pTechInfo;
	switch(mode) {
		case kCubicUsePS1_4Path:
			pTechInfo = &g_technique_bicubic1_4;
			break;																														
		case kCubicUsePS1_1Path:																				
			pTechInfo = &g_technique_bicubic1_1;
			break;																														
		default:
			return false;
	}

	// Validate caps bits.
	const D3DCAPS9& caps = mpManager->GetCaps();
	if ((caps.PrimitiveMiscCaps & pTechInfo->mPrimitiveMiscCaps) != pTechInfo->mPrimitiveMiscCaps)
		return false;
	if (caps.MaxSimultaneousTextures < pTechInfo->mMaxSimultaneousTextures)
		return false;
	if (caps.MaxTextureBlendStages < pTechInfo->mMaxSimultaneousTextures)
		return false;
	if ((caps.SrcBlendCaps & pTechInfo->mSrcBlendCaps) != pTechInfo->mSrcBlendCaps)
		return false;
	if ((caps.DestBlendCaps & pTechInfo->mDestBlendCaps) != pTechInfo->mDestBlendCaps)
		return false;
	if ((caps.TextureOpCaps & pTechInfo->mTextureOpCaps) != pTechInfo->mTextureOpCaps)
		return false;
	if (pTechInfo->mPSVersionRequired) {
		if (caps.PixelShaderVersion < pTechInfo->mPSVersionRequired)
			return false;
		if (caps.PixelShader1xMaxValue < pTechInfo->mPixelShader1xMaxValue * 0.95f)
			return false;
	}

	// Validate shaders.
	IDirect3DDevice9 *pDevice = mpManager->GetDevice();
	HRESULT hr = pDevice->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2);
	if (FAILED(hr))
		return false;

	const PassInfo *pPasses = pTechInfo->mpPasses;
	for(uint32 stage = 0; stage < pTechInfo->mPassCount; ++stage) {
		const PassInfo& pi = *pPasses++;

		const uint32 stateStart = pi.mStateStart, stateEnd = pi.mStateEnd;
		for(uint32 stateIdx = stateStart; stateIdx != stateEnd; ++stateIdx) {
			uint32 token = g_states[stateIdx];
			uint32 tokenIndex = (token >> 12) & 0xFFF;
			uint32 tokenValue = token & 0xFFF;

			if (tokenValue == 0xFFF)
				tokenValue = g_states[++stateIdx];

			hr = S_OK;
			switch(token >> 28) {
				case 0:		// render state
					hr = pDevice->SetRenderState((D3DRENDERSTATETYPE)tokenIndex, tokenValue);
					break;
				case 1:		// texture stage state
					hr = pDevice->SetTextureStageState((token >> 24)&15, (D3DTEXTURESTAGESTATETYPE)tokenIndex, tokenValue);
					break;
				case 2:		// sampler state
					hr = pDevice->SetSamplerState((token >> 24)&15, (D3DSAMPLERSTATETYPE)tokenIndex, tokenValue);
					break;
				case 3:		// texture
				case 8:		// vertex bool constant
				case 9:		// vertex int constant
				case 10:	// vertex float constant
				case 12:	// pixel bool constant
				case 13:	// pixel int constant
				case 14:	// pixel float constant
					// ignore.
					break;
			}

			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set state! hr=%08x\n", hr);
				return false;
			}
		}

		HRESULT hr = pDevice->SetVertexShader(pi.mVertexShaderIndex >= 0 ? mVertexShaders[pi.mVertexShaderIndex] : NULL);
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Couldn't set vertex shader! hr=%08x\n", hr);
			return false;
		}

		hr = pDevice->SetPixelShader(pi.mPixelShaderIndex >= 0 ? mPixelShaders[pi.mPixelShaderIndex] : NULL);
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Couldn't set pixel shader! hr=%08x\n", hr);
			return false;
		}

		DWORD passes;
		hr = pDevice->ValidateDevice(&passes);

		if (FAILED(hr))
			return false;
	}

	return true;
}

bool VDVideoDisplayDX9Manager::RunEffect(const EffectContext& ctx, const nsVDDisplay::TechniqueInfo& technique, IDirect3DSurface9 *pRTOverride) {
	mpManager->BeginScope(L"RunEffect");
	bool success = RunEffect2(ctx, technique, pRTOverride);
	mpManager->EndScope();

	return success;
}

bool VDVideoDisplayDX9Manager::RunEffect2(const EffectContext& ctx, const nsVDDisplay::TechniqueInfo& technique, IDirect3DSurface9 *pRTOverride) {
	using namespace nsVDDisplay;

	const int firstRTTIndex = ctx.mbHighPrecision ? 2 : 0;

	IDirect3DTexture9 *const textures[15]={
		NULL,
		ctx.mpSourceTexture1,
		ctx.mpSourceTexture2,
		ctx.mpSourceTexture3,
		ctx.mpSourceTexture4,
		ctx.mpSourceTexture5,
		ctx.mpPaletteTexture,
		mpRTTs[firstRTTIndex] ? mpRTTs[firstRTTIndex]->GetD3DTexture() : NULL,
		mpRTTs[1] ? mpRTTs[1]->GetD3DTexture() : NULL,
		mpFilterTexture ? mpFilterTexture->GetD3DTexture() : NULL,
		mpHEvenOddTexture ? mpHEvenOddTexture->GetD3DTexture() : NULL,
		mpDitherTexture ? mpDitherTexture->GetD3DTexture() : NULL,
		ctx.mpInterpFilterH,
		ctx.mpInterpFilterV,
		ctx.mpInterpFilter
	};

	const D3DDISPLAYMODE& dmode = mpManager->GetDisplayMode();
	int clippedWidth = std::min<int>(ctx.mOutputX + ctx.mOutputW, dmode.Width);
	int clippedHeight = std::min<int>(ctx.mOutputY + ctx.mOutputH, dmode.Height);

	if (clippedWidth <= 0 || clippedHeight <= 0)
		return true;

	struct StdParamData {
		float vpsize[4];			// (viewport size)			vpwidth, vpheight, 1/vpheight, 1/vpwidth
		float cvpsize[4];			// (clipped viewport size)	cvpwidth, cvpheight, 1/cvpheight, 1/cvpwidth
		float texsize[4];			// (texture size)			texwidth, texheight, 1/texheight, 1/texwidth
		float tex2size[4];			// (texture2 size)			tex2width, tex2height, 1/tex2height, 1/tex2width
		float srcsize[4];			// (source size)			srcwidth, srcheight, 1/srcheight, 1/srcwidth
		float tempsize[4];			// (temp rtt size)			tempwidth, tempheight, 1/tempheight, 1/tempwidth
		float temp2size[4];			// (temp2 rtt size)			tempwidth, tempheight, 1/tempheight, 1/tempwidth
		float vpcorrect[4];			// (viewport correction)	2/vpwidth, 2/vpheight, -1/vpheight, 1/vpwidth
		float vpcorrect2[4];		// (viewport correction)	2/vpwidth, -2/vpheight, 1+1/vpheight, -1-1/vpwidth
		float tvpcorrect[4];		// (temp vp correction)		2/tvpwidth, 2/tvpheight, -1/tvpheight, 1/tvpwidth
		float tvpcorrect2[4];		// (temp vp correction)		2/tvpwidth, -2/tvpheight, 1+1/tvpheight, -1-1/tvpwidth
		float t2vpcorrect[4];		// (temp2 vp correction)	2/tvpwidth, 2/tvpheight, -1/tvpheight, 1/tvpwidth
		float t2vpcorrect2[4];		// (temp2 vp correction)	2/tvpwidth, -2/tvpheight, 1+1/tvpheight, -1-1/tvpwidth
		float time[4];				// (time)
		float interphtexsize[4];	// (cubic htex interp info)
		float interpvtexsize[4];	// (cubic vtex interp info)
		float interptexsize[4];		// (interp tex info)
		float fieldinfo[4];			// (field information)		fieldoffset, -fieldoffset/4, und, und
		float chromauvscale[4];		// (chroma UV scale)		U scale, V scale, und, und
		float chromauvoffset[4];	// (chroma UV offset)		U offset, V offset, und, und
		float pixelsharpness[4];	// (pixel sharpness)		X factor, Y factor, ?, ?
	};

	static const struct StdParam {
		int offset;
	} kStdParamInfo[]={
		offsetof(StdParamData, vpsize),
		offsetof(StdParamData, cvpsize),
		offsetof(StdParamData, texsize),
		offsetof(StdParamData, tex2size),
		offsetof(StdParamData, srcsize),
		offsetof(StdParamData, tempsize),
		offsetof(StdParamData, temp2size),
		offsetof(StdParamData, vpcorrect),
		offsetof(StdParamData, vpcorrect2),
		offsetof(StdParamData, tvpcorrect),
		offsetof(StdParamData, tvpcorrect2),
		offsetof(StdParamData, t2vpcorrect),
		offsetof(StdParamData, t2vpcorrect2),
		offsetof(StdParamData, time),
		offsetof(StdParamData, interphtexsize),
		offsetof(StdParamData, interpvtexsize),
		offsetof(StdParamData, interptexsize),
		offsetof(StdParamData, fieldinfo),
		offsetof(StdParamData, chromauvscale),
		offsetof(StdParamData, chromauvoffset),
		offsetof(StdParamData, pixelsharpness),
	};

	StdParamData data;

	data.vpsize[0] = (float)ctx.mOutputW;
	data.vpsize[1] = (float)ctx.mOutputH;
	data.vpsize[2] = 1.0f / (float)ctx.mOutputH;
	data.vpsize[3] = 1.0f / (float)ctx.mOutputW;
	data.cvpsize[0] = (float)clippedWidth;
	data.cvpsize[0] = (float)clippedHeight;
	data.cvpsize[0] = 1.0f / (float)clippedHeight;
	data.cvpsize[0] = 1.0f / (float)clippedWidth;
	data.texsize[0] = (float)(int)ctx.mSourceTexW;
	data.texsize[1] = (float)(int)ctx.mSourceTexH;
	data.texsize[2] = 1.0f / (float)(int)ctx.mSourceTexH;
	data.texsize[3] = 1.0f / (float)(int)ctx.mSourceTexW;
	data.tex2size[0] = 1.f;
	data.tex2size[1] = 1.f;
	data.tex2size[2] = 1.f;
	data.tex2size[3] = 1.f;
	data.srcsize[0] = (float)(int)ctx.mSourceW;
	data.srcsize[1] = (float)(int)ctx.mSourceH;
	data.srcsize[2] = 1.0f / (float)(int)ctx.mSourceH;
	data.srcsize[3] = 1.0f / (float)(int)ctx.mSourceW;
	data.tempsize[0] = 1.f;
	data.tempsize[1] = 1.f;
	data.tempsize[2] = 1.f;
	data.tempsize[3] = 1.f;
	data.temp2size[0] = 1.f;
	data.temp2size[1] = 1.f;
	data.temp2size[2] = 1.f;
	data.temp2size[3] = 1.f;
	data.vpcorrect[0] = 2.0f / (float)clippedWidth;
	data.vpcorrect[1] = 2.0f / (float)clippedHeight;
	data.vpcorrect[2] = -1.0f / (float)clippedHeight;
	data.vpcorrect[3] = 1.0f / (float)clippedWidth;
	data.vpcorrect2[0] = 2.0f / (float)clippedWidth;
	data.vpcorrect2[1] = -2.0f / (float)clippedHeight;
	data.vpcorrect2[2] = 1.0f + 1.0f / (float)clippedHeight;
	data.vpcorrect2[3] = -1.0f - 1.0f / (float)clippedWidth;
	data.tvpcorrect[0] = 2.0f;
	data.tvpcorrect[1] = 2.0f;
	data.tvpcorrect[2] = -1.0f;
	data.tvpcorrect[3] = 1.0f;
	data.tvpcorrect2[0] = 2.0f;
	data.tvpcorrect2[1] = -2.0f;
	data.tvpcorrect2[2] = 0.f;
	data.tvpcorrect2[3] = 2.0f;
	data.t2vpcorrect[0] = 2.0f;
	data.t2vpcorrect[1] = 2.0f;
	data.t2vpcorrect[2] = -1.0f;
	data.t2vpcorrect[3] = 1.0f;
	data.t2vpcorrect2[0] = 2.0f;
	data.t2vpcorrect2[1] = -2.0f;
	data.t2vpcorrect2[2] = 0.f;
	data.t2vpcorrect2[3] = 2.0f;
	data.interphtexsize[0] = (float)ctx.mInterpHTexW;
	data.interphtexsize[1] = (float)ctx.mInterpHTexH;
	data.interphtexsize[2] = ctx.mInterpHTexH ? 1.0f / (float)ctx.mInterpHTexH : 0.0f;
	data.interphtexsize[3] = ctx.mInterpHTexW ? 1.0f / (float)ctx.mInterpHTexW : 0.0f;
	data.interpvtexsize[0] = (float)ctx.mInterpVTexW;
	data.interpvtexsize[1] = (float)ctx.mInterpVTexH;
	data.interpvtexsize[2] = ctx.mInterpVTexH ? 1.0f / (float)ctx.mInterpVTexH : 0.0f;
	data.interpvtexsize[3] = ctx.mInterpVTexW ? 1.0f / (float)ctx.mInterpVTexW : 0.0f;
	data.interptexsize[0] = (float)ctx.mInterpTexW;
	data.interptexsize[1] = (float)ctx.mInterpTexH;
	data.interptexsize[2] = ctx.mInterpTexH ? 1.0f / (float)ctx.mInterpTexH : 0.0f;
	data.interptexsize[3] = ctx.mInterpTexW ? 1.0f / (float)ctx.mInterpTexW : 0.0f;
	data.fieldinfo[0] = ctx.mFieldOffset;
	data.fieldinfo[1] = ctx.mFieldOffset * -0.25f;
	data.fieldinfo[2] = 0.0f;
	data.fieldinfo[3] = 0.0f;
	data.chromauvscale[0] = ctx.mChromaScaleU;
	data.chromauvscale[1] = ctx.mChromaScaleV;
	data.chromauvscale[2] = 0.0f;
	data.chromauvscale[3] = 0.0f;
	data.chromauvoffset[0] = ctx.mChromaOffsetU;
	data.chromauvoffset[1] = ctx.mChromaOffsetV;
	data.chromauvoffset[2] = 0.0f;
	data.chromauvoffset[3] = 0.0f;
	data.pixelsharpness[0] = ctx.mPixelSharpnessX;
	data.pixelsharpness[1] = ctx.mPixelSharpnessY;
	data.pixelsharpness[2] = 0.0f;
	data.pixelsharpness[3] = 0.0f;

	uint32 t = VDGetAccurateTick();
	data.time[0] = (t % 1000) / 1000.0f;
	data.time[1] = (t % 5000) / 5000.0f;
	data.time[2] = (t % 10000) / 10000.0f;
	data.time[3] = (t % 30000) / 30000.0f;

	if (ctx.mpSourceTexture2) {
		D3DSURFACE_DESC desc;

		HRESULT hr = ctx.mpSourceTexture2->GetLevelDesc(0, &desc);
		if (FAILED(hr))
			return false;

		float w = (float)desc.Width;
		float h = (float)desc.Height;

		data.tex2size[0] = w;
		data.tex2size[1] = h;
		data.tex2size[2] = 1.0f / h;
		data.tex2size[3] = 1.0f / w;
	}

	if (mpRTTs[firstRTTIndex]) {
		data.tempsize[0] = (float)mpRTTs[firstRTTIndex]->GetWidth();
		data.tempsize[1] = (float)mpRTTs[firstRTTIndex]->GetHeight();
		data.tempsize[2] = 1.0f / data.tempsize[1];
		data.tempsize[3] = 1.0f / data.tempsize[0];
		data.tvpcorrect[0] = 2.0f * data.tempsize[3];
		data.tvpcorrect[1] = 2.0f * data.tempsize[2];
		data.tvpcorrect[2] = -data.tempsize[2];
		data.tvpcorrect[3] = data.tempsize[3];
		data.tvpcorrect2[0] = 2.0f * data.tempsize[3];
		data.tvpcorrect2[1] = -2.0f * data.tempsize[2];
		data.tvpcorrect2[2] = 1.0f + data.tempsize[2];
		data.tvpcorrect2[3] = -1.0f - data.tempsize[3];
	}

	if (mpRTTs[1]) {
		data.temp2size[0] = (float)mpRTTs[1]->GetWidth();
		data.temp2size[1] = (float)mpRTTs[1]->GetHeight();
		data.temp2size[2] = 1.0f / data.temp2size[1];
		data.temp2size[3] = 1.0f / data.temp2size[0];
		data.t2vpcorrect[0] = 2.0f * data.tempsize[3];
		data.t2vpcorrect[1] = 2.0f * data.tempsize[2];
		data.t2vpcorrect[2] = -data.tempsize[2];
		data.t2vpcorrect[3] = data.tempsize[3];
		data.t2vpcorrect2[0] = 2.0f * data.tempsize[3];
		data.t2vpcorrect2[1] = -2.0f * data.tempsize[2];
		data.t2vpcorrect2[2] = 1.0f + data.tempsize[2];
		data.t2vpcorrect2[3] = -1.0f - data.tempsize[3];
	}

	enum { kStdParamCount = sizeof kStdParamInfo / sizeof kStdParamInfo[0] };

	uint32 nPasses = technique.mPassCount;
	const PassInfo *pPasses = technique.mpPasses;
	IDirect3DDevice9 *dev = mpManager->GetDevice();
	bool rtmain = true;

	while(nPasses--) {
		const PassInfo& pi = *pPasses++;

		// bind vertex and pixel shaders
		HRESULT hr = dev->SetVertexShader(pi.mVertexShaderIndex >= 0 ? mVertexShaders[pi.mVertexShaderIndex] : NULL);
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Couldn't set vertex shader! hr=%08x\n", hr);
			return false;
		}

		hr = dev->SetPixelShader(pi.mPixelShaderIndex >= 0 ? mPixelShaders[pi.mPixelShaderIndex] : NULL);
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Couldn't set pixel shader! hr=%08x\n", hr);
			return false;
		}

		// set states
		const uint32 stateStart = pi.mStateStart, stateEnd = pi.mStateEnd;
		for(uint32 stateIdx = stateStart; stateIdx != stateEnd; ++stateIdx) {
			uint32 token = g_states[stateIdx];
			uint32 tokenIndex = (token >> 12) & 0xFFF;
			uint32 tokenValue = token & 0xFFF;

			if (tokenValue == 0xFFF)
				tokenValue = g_states[++stateIdx];

			HRESULT hr;
			switch(token >> 28) {
				case 0:		// render state
					hr = dev->SetRenderState((D3DRENDERSTATETYPE)tokenIndex, tokenValue);
					break;
				case 1:		// texture stage state
					switch((D3DTEXTURESTAGESTATETYPE)tokenIndex) {
					case D3DTSS_BUMPENVMAT00:
					case D3DTSS_BUMPENVMAT01:
					case D3DTSS_BUMPENVMAT10:
					case D3DTSS_BUMPENVMAT11:
						{
							union {
								uint32 i;
								float f;
							} converter = {tokenValue};

							if (pi.mBumpEnvScale) {
								const float *param = (const float *)&data + 4*(pi.mBumpEnvScale - 1);

								switch((D3DTEXTURESTAGESTATETYPE)tokenIndex) {
								case D3DTSS_BUMPENVMAT00:
								case D3DTSS_BUMPENVMAT10:
									converter.f *= param[3];
									break;
								case D3DTSS_BUMPENVMAT01:
								case D3DTSS_BUMPENVMAT11:
								default:
									converter.f *= param[2];
									break;
								}
							}
						
							hr = dev->SetTextureStageState((token >> 24)&15, (D3DTEXTURESTAGESTATETYPE)tokenIndex, converter.i);
						}
						break;
					default:
						hr = dev->SetTextureStageState((token >> 24)&15, (D3DTEXTURESTAGESTATETYPE)tokenIndex, tokenValue);
						break;
					}
					break;
				case 2:		// sampler state
					hr = dev->SetSamplerState((token >> 24)&15, (D3DSAMPLERSTATETYPE)tokenIndex, tokenValue);
					break;
				case 3:		// texture
					VDASSERT(tokenValue < sizeof(textures)/sizeof(textures[0]));
					hr = dev->SetTexture(tokenIndex, textures[tokenValue]);
					break;
				case 8:		// vertex bool constant
					hr = dev->SetVertexShaderConstantB(tokenIndex, (const BOOL *)((char *)&data + kStdParamInfo[tokenValue].offset), 1);
					break;
				case 9:		// vertex int constant
					hr = dev->SetVertexShaderConstantI(tokenIndex, (const INT *)((char *)&data + kStdParamInfo[tokenValue].offset), 1);
					break;
				case 10:	// vertex float constant
					hr = dev->SetVertexShaderConstantF(tokenIndex, (const float *)((char *)&data + kStdParamInfo[tokenValue].offset), 1);
					break;
				case 12:	// pixel bool constant
					hr = dev->SetPixelShaderConstantB(tokenIndex, (const BOOL *)((char *)&data + kStdParamInfo[tokenValue].offset), 1);
					break;
				case 13:	// pixel int constant
					hr = dev->SetPixelShaderConstantI(tokenIndex, (const INT *)((char *)&data + kStdParamInfo[tokenValue].offset), 1);
					break;
				case 14:	// pixel float constant
					hr = dev->SetPixelShaderConstantF(tokenIndex, (const float *)((char *)&data + kStdParamInfo[tokenValue].offset), 1);
					break;
			}

			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set state! hr=%08x\n", hr);
				return false;
			}
		}

		// change render target
		if (pi.mRenderTarget >= 0) {
			if (!mpManager->EndScene())
				return false;

			HRESULT hr = E_FAIL;
			rtmain = false;

			switch(pi.mRenderTarget) {
				case 0:
					hr = dev->SetRenderTarget(0, pRTOverride ? pRTOverride : mpManager->GetRenderTarget());
					rtmain = true;
					break;
				case 1:
					if (mpRTTs[firstRTTIndex]) {
						IDirect3DSurface9 *pSurf;
						hr = mpRTTs[firstRTTIndex]->GetD3DTexture()->GetSurfaceLevel(0, &pSurf);
						if (SUCCEEDED(hr)) {
							hr = dev->SetRenderTarget(0, pSurf);
							pSurf->Release();
						}
					}
					break;
				case 2:
					if (mpRTTs[1]) {
						IDirect3DSurface9 *pSurf;
						hr = mpRTTs[1]->GetD3DTexture()->GetSurfaceLevel(0, &pSurf);
						if (SUCCEEDED(hr)) {
							hr = dev->SetRenderTarget(0, pSurf);
							pSurf->Release();
						}
					}
					break;
			}

			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set render target! hr=%08x\n", hr);
				return false;
			}
		}

		// change viewport
		D3DVIEWPORT9 vp;
		if (pi.mViewportW | pi.mViewportH) {
			HRESULT hr;

			IDirect3DSurface9 *rt;
			hr = dev->GetRenderTarget(0, &rt);
			if (SUCCEEDED(hr)) {
				D3DSURFACE_DESC desc;
				hr = rt->GetDesc(&desc);
				if (SUCCEEDED(hr)) {
					const DWORD hsizes[4]={ desc.Width, ctx.mSourceW, (DWORD)clippedWidth, (DWORD)ctx.mOutputW };
					const DWORD vsizes[4]={ desc.Height, ctx.mSourceH, (DWORD)clippedHeight, (DWORD)ctx.mOutputH };

					vp.X = rtmain ? ctx.mViewportX : 0;
					vp.Y = rtmain ? ctx.mViewportY : 0;
					vp.Width = hsizes[pi.mViewportW];
					vp.Height = vsizes[pi.mViewportH];
					vp.MinZ = 0;
					vp.MaxZ = 1;

					hr = dev->SetViewport(&vp);
				}
				rt->Release();
			}

			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to set viewport! hr=%08x\n", hr);
				return false;
			}
		} else {
			vp.X = ctx.mViewportX;
			vp.Y = ctx.mViewportY;
			vp.Width = ctx.mViewportW;
			vp.Height = ctx.mViewportH;
			vp.MinZ = 0;
			vp.MaxZ = 1;

			HRESULT hr = dev->SetViewport(&vp);
			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to retrieve viewport! hr=%08x\n", hr);
				return false;
			}
		}

		// clear target
		if (pi.mbRTDoClear) {
			hr = dev->Clear(0, NULL, D3DCLEAR_TARGET, pi.mRTClearColor, 0, 0);
			if (FAILED(hr)) {
				VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to clear viewport! hr=%08x\n", hr);
				return false;
			}
		}

		// render!
		bool validDraw = true;

		const float ustep = 1.0f / (float)(int)ctx.mSourceTexW * ctx.mDefaultUVScaleCorrectionX;
		const float vstep = 1.0f / (float)(int)ctx.mSourceTexH * ctx.mDefaultUVScaleCorrectionY;
		const float voffset = (pi.mbClipPosition ? ctx.mFieldOffset * vstep * -0.25f : 0.0f);
		const float u0 = ctx.mSourceArea.left   * ustep;
		const float v0 = ctx.mSourceArea.top    * vstep + voffset;
		const float u1 = ctx.mSourceArea.right  * ustep;
		const float v1 = ctx.mSourceArea.bottom * vstep + voffset;

		const float invVpW = 1.f / (float)vp.Width;
		const float invVpH = 1.f / (float)vp.Height;
		const float xstep =  2.0f * invVpW;
		const float ystep = -2.0f * invVpH;

		const float x0 = -1.0f - invVpW + ctx.mOutputX * xstep;
		const float y0 =  1.0f + invVpH + ctx.mOutputY * ystep;
		const float x1 = pi.mbClipPosition ? x0 + ctx.mOutputW * xstep : 1.f - invVpW;
		const float y1 = pi.mbClipPosition ? y0 + ctx.mOutputH * ystep : -1.f + invVpH;

		if (pi.mTileMode == 0) {
			if (Vertex *pvx = mpManager->LockVertices(4)) {
				vd_seh_guard_try {
					pvx[0].SetFF2(x0, y0, 0xFFFFFFFF, u0, v0, 0, 0);
					pvx[1].SetFF2(x1, y0, 0xFFFFFFFF, u1, v0, 1, 0);
					pvx[2].SetFF2(x0, y1, 0xFFFFFFFF, u0, v1, 0, 1);
					pvx[3].SetFF2(x1, y1, 0xFFFFFFFF, u1, v1, 1, 1);
				} vd_seh_guard_except {
					validDraw = false;
				}

				mpManager->UnlockVertices();
			}
		} else if (pi.mTileMode == 1) {
			if (Vertex *pvx = mpManager->LockVertices(12)) {
				vd_seh_guard_try {
					pvx[ 0].SetFF2(x0, y1, 0xFFFFFFFF,  0.0f, 0, 0, 1);
					pvx[ 1].SetFF2(x0, y0, 0xFFFFFFFF,  0.0f, 0, 0, 0);
					pvx[ 2].SetFF2(x0, y1, 0xFFFFFFFF, +0.5f, 0, 0, 1);
					pvx[ 3].SetFF2(x0, y0, 0xFFFFFFFF, +0.5f, 0, 0, 0);
					pvx[ 4].SetFF2(x0, y1, 0xFFFFFFFF, +2.0f, 0, 0, 1);
					pvx[ 5].SetFF2(x0, y0, 0xFFFFFFFF, +2.0f, 0, 0, 0);
					pvx[ 6].SetFF2(x1, y1, 0xFFFFFFFF, -2.0f, 0, 1, 1);
					pvx[ 7].SetFF2(x1, y0, 0xFFFFFFFF, -2.0f, 0, 1, 0);
					pvx[ 8].SetFF2(x1, y1, 0xFFFFFFFF, -0.5f, 0, 1, 1);
					pvx[ 9].SetFF2(x1, y0, 0xFFFFFFFF, -0.5f, 0, 1, 0);
					pvx[10].SetFF2(x1, y1, 0xFFFFFFFF,  0.0f, 0, 1, 1);
					pvx[11].SetFF2(x1, y0, 0xFFFFFFFF,  0.0f, 0, 1, 0);
				} vd_seh_guard_except {
					validDraw = false;
				}

				mpManager->UnlockVertices();
			}
		} else if (pi.mTileMode == 2) {
			if (Vertex *pvx = mpManager->LockVertices(12)) {
				vd_seh_guard_try {
					pvx[ 0].SetFF2(x1, y0, 0xFFFFFFFF, 0,  0.0f, 1, 0);
					pvx[ 1].SetFF2(x0, y0, 0xFFFFFFFF, 0,  0.0f, 0, 0);
					pvx[ 2].SetFF2(x1, y0, 0xFFFFFFFF, 0,  0.5f, 1, 0);
					pvx[ 3].SetFF2(x0, y0, 0xFFFFFFFF, 0,  0.5f, 0, 0);
					pvx[ 4].SetFF2(x1, y0, 0xFFFFFFFF, 0,  2.0f, 1, 0);
					pvx[ 5].SetFF2(x0, y0, 0xFFFFFFFF, 0,  2.0f, 0, 0);
					pvx[ 6].SetFF2(x1, y1, 0xFFFFFFFF, 0, -2.0f, 1, 1);
					pvx[ 7].SetFF2(x0, y1, 0xFFFFFFFF, 0, -2.0f, 0, 1);
					pvx[ 8].SetFF2(x1, y1, 0xFFFFFFFF, 0, -0.5f, 1, 1);
					pvx[ 9].SetFF2(x0, y1, 0xFFFFFFFF, 0, -0.5f, 0, 1);
					pvx[10].SetFF2(x1, y1, 0xFFFFFFFF, 0,  0.0f, 1, 1);
					pvx[11].SetFF2(x0, y1, 0xFFFFFFFF, 0,  0.0f, 0, 1);
				} vd_seh_guard_except {
					validDraw = false;
				}

				mpManager->UnlockVertices();
			}
		}

		if (!validDraw) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Invalid vertex buffer lock detected -- bailing.\n");
			return false;
		}

		if (!mpManager->BeginScene())
			return false;

		if (pi.mTileMode) {
			hr = mpManager->DrawArrays(D3DPT_TRIANGLESTRIP, 0, 10);
		} else {
			hr = mpManager->DrawArrays(D3DPT_TRIANGLESTRIP, 0, 2);
		}

		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to draw primitive! hr=%08x\n", hr);
			return false;
		}
	}

	// NVPerfHUD 3.1 draws a bit funny if we leave this set to REVSUBTRACT, even
	// with alpha blending off....
	dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);

	return true;
}

///////////////////////////////////////////////////////////////////////////

class VDVideoUploadContextD3D9 final : public vdrefcounted<IVDVideoUploadContextD3D9>, public VDD3D9Client {
public:
	VDVideoUploadContextD3D9();
	~VDVideoUploadContextD3D9();

	IDirect3DTexture9 *GetD3DTexture(int i = 0) {
		return !mpD3DConversionTextures.empty() ? mpD3DConversionTextures[i] : mpD3DImageTextures[i];
	}

	IDirect3DTexture9 *const *GetD3DTextures() {
		return !mpD3DConversionTextures.empty() ? mpD3DConversionTextures.data() : mpD3DImageTextures.data();
	}

	bool Init(void *hmonitor, bool use9ex, const VDPixmap& source, bool allowConversion, bool highPrecision, int buffers);
	void Shutdown();

	void SetBufferCount(uint32 buffers);
	bool Update(const VDPixmap& source, int fieldMask);

protected:
	bool Lock(IDirect3DTexture9 *tex, IDirect3DTexture9 *upload, D3DLOCKED_RECT *lr);
	bool Unlock(IDirect3DTexture9 *tex, IDirect3DTexture9 *upload);

	void OnPreDeviceReset() override;
	void OnPostDeviceReset() override;

	bool ReinitImageTextures();
	bool ReinitVRAMTextures();
	void ClearImageTexture(uint32 i);

	VDD3D9Manager	*mpManager = nullptr;
	vdrefptr<VDVideoDisplayDX9Manager> mpVideoManager;

	enum UploadMode {
		kUploadModeNormal,
		kUploadModeDirect8,
		kUploadModeDirect8Laced,
		kUploadModeDirect16,
		kUploadModeDirectV210,
		kUploadModeDirectNV12
	} mUploadMode = {};

	int mBufferCount = 0;
	int	mConversionTexW = 0;
	int	mConversionTexH = 0;
	bool mbHighPrecision = false;
	bool mbPaletteTextureValid = false;
	bool mbPaletteTextureIdentity = false;

	int mSourceFmt = 0;
	D3DFORMAT mTexD3DFmt = {};
	VDPixmap			mTexFmt;
	VDPixmapCachedBlitter mCachedBlitter;

	vdfastvector<IDirect3DTexture9 *> mpD3DImageTextures;
	IDirect3DTexture9	*mpD3DImageTextureUpload = nullptr;
	IDirect3DTexture9	*mpD3DPaletteTexture = nullptr;
	IDirect3DTexture9	*mpD3DPaletteTextureUpload = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2a = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2aUpload = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2b = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2bUpload = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2c = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2cUpload = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2d = nullptr;
	IDirect3DTexture9	*mpD3DImageTexture2dUpload = nullptr;
	vdfastvector<IDirect3DTexture9 *> mpD3DConversionTextures;

	vdblock<uint32> mLastPalette;
};

bool VDCreateVideoUploadContextD3D9(IVDVideoUploadContextD3D9 **ppContext) {
	return VDRefCountObjectFactory<VDVideoUploadContextD3D9, IVDVideoUploadContextD3D9>(ppContext);
}

VDVideoUploadContextD3D9::VDVideoUploadContextD3D9() {
}

VDVideoUploadContextD3D9::~VDVideoUploadContextD3D9() {
	Shutdown();
}

bool VDVideoUploadContextD3D9::Init(void *hmonitor, bool use9ex, const VDPixmap& source, bool allowConversion, bool highPrecision, int buffers) {
	mCachedBlitter.Invalidate();

	mBufferCount = buffers;
	mbHighPrecision = highPrecision;

	VDASSERT(!mpManager);
	mpManager = VDInitDirect3D9(this, (HMONITOR)hmonitor, use9ex);
	if (!mpManager)
		return false;

	if (!VDInitDisplayDX9((HMONITOR)hmonitor, use9ex, ~mpVideoManager)) {
		Shutdown();
		return false;
	}

	// check capabilities
	const D3DCAPS9& caps = mpManager->GetCaps();

	if (caps.MaxTextureWidth < (uint32)source.w || caps.MaxTextureHeight < (uint32)source.h) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: source image is larger than maximum texture size\n");
		Shutdown();
		return false;
	}

	// high precision requires VS/PS 2.0
	if (caps.VertexShaderVersion < D3DVS_VERSION(2, 0) || caps.PixelShaderVersion < D3DPS_VERSION(2, 0)) {
		mbHighPrecision = false;
	}

	// create source texture
	int texw = source.w;
	int texh = source.h;

	mpManager->AdjustTextureSize(texw, texh);

	memset(&mTexFmt, 0, sizeof mTexFmt);
	mTexFmt.format		= nsVDPixmap::kPixFormat_XRGB8888;

	mSourceFmt = source.format;

	HRESULT hr;
	D3DFORMAT d3dfmt;
	IDirect3DDevice9 *dev = mpManager->GetDevice();

	mUploadMode = kUploadModeNormal;

	const bool useDefault = (mpManager->GetDeviceEx() != NULL);
	const D3DPOOL texPool = useDefault ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;

	switch(source.format) {
		case nsVDPixmap::kPixFormat_YUV420i_Planar:
		case nsVDPixmap::kPixFormat_YUV420i_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420i_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420i_Planar_709_FR:
			if (mpManager->IsTextureFormatAvailable(D3DFMT_L8) && caps.PixelShaderVersion >= D3DPS_VERSION(2, 0)) {
				mUploadMode = kUploadModeDirect8Laced;
				d3dfmt = D3DFMT_L8;

				int subw = (source.w + 1) >> 1;
				int subh = (source.h + 3) >> 2;

				if (subw < 1)
					subw = 1;
				if (subh < 1)
					subh = 1;

				if (!mpManager->AdjustTextureSize(subw, subh)) {
					Shutdown();
					return false;
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2a, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2b, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2c, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2d, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				if (mpManager->GetDeviceEx()) {
					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2aUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}

					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2bUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}

					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2cUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}

					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2dUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}
				}
			}
			break;

		case nsVDPixmap::kPixFormat_YUV410_Planar_709:
		case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709:
		case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709:
		case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420it_Planar:
		case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
			if (mpManager->IsTextureFormatAvailable(D3DFMT_L8) && caps.PixelShaderVersion >= D3DPS_VERSION(2, 0)) {
				mUploadMode = kUploadModeDirect8;
				d3dfmt = D3DFMT_L8;

				uint32 subw = texw;
				uint32 subh = texh;

				switch(source.format) {
					case nsVDPixmap::kPixFormat_YUV444_Planar:
					case nsVDPixmap::kPixFormat_YUV444_Planar_709:
					case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar:
					case nsVDPixmap::kPixFormat_YUV422_Planar_709:
					case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
						subw >>= 1;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar:
					case nsVDPixmap::kPixFormat_YUV420_Planar_709:
					case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
					case nsVDPixmap::kPixFormat_YUV420it_Planar:
					case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
					case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
					case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
						subw >>= 1;
						subh >>= 1;
						break;
					case nsVDPixmap::kPixFormat_YUV410_Planar:
					case nsVDPixmap::kPixFormat_YUV410_Planar_709:
					case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
					case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
						subw >>= 2;
						subh >>= 2;
						break;
				}

				if (subw < 1)
					subw = 1;
				if (subh < 1)
					subh = 1;

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2a, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2b, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				if (useDefault) {
					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2aUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}

					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2bUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}
				}
			}
			break;

		case nsVDPixmap::kPixFormat_Pal8:
		case nsVDPixmap::kPixFormat_YUV410_Planar:
		case nsVDPixmap::kPixFormat_YUV420_Planar:
		case nsVDPixmap::kPixFormat_YUV422_Planar:
		case nsVDPixmap::kPixFormat_YUV444_Planar:
			if (mpManager->IsTextureFormatAvailable(D3DFMT_L8) && caps.PixelShaderVersion >= D3DPS_VERSION(1, 1)) {
				mUploadMode = kUploadModeDirect8;
				d3dfmt = D3DFMT_L8;

				uint32 subw = texw;
				uint32 subh = texh;

				switch(source.format) {
					case nsVDPixmap::kPixFormat_Pal8:
					case nsVDPixmap::kPixFormat_YUV444_Planar:
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar:
						subw >>= 1;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar:
					case nsVDPixmap::kPixFormat_YUV420i_Planar:
						subw >>= 1;
						subh >>= 1;
						break;
					case nsVDPixmap::kPixFormat_YUV410_Planar:
						subw >>= 2;
						subh >>= 2;
						break;
				}

				if (subw < 1)
					subw = 1;
				if (subh < 1)
					subh = 1;

				if (source.format == nsVDPixmap::kPixFormat_Pal8) {
					mbPaletteTextureValid = false;
					mLastPalette.resize(256);

					hr = dev->CreateTexture(256, 1, 1, 0, D3DFMT_X8R8G8B8, texPool, &mpD3DPaletteTexture, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}

					if (useDefault) {
						hr = dev->CreateTexture(256, 1, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &mpD3DPaletteTextureUpload, NULL);
						if (FAILED(hr)) {
							Shutdown();
							return false;
						}
					}
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2a, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, texPool, &mpD3DImageTexture2b, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				if (useDefault) {
					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2aUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}

					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2bUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}
				}
			}
			break;

		case nsVDPixmap::kPixFormat_YUV420_NV12:
			if (mpManager->IsTextureFormatAvailable(D3DFMT_L8) &&
				mpManager->IsTextureFormatAvailable(D3DFMT_A8L8) &&
				caps.PixelShaderVersion >= D3DPS_VERSION(1, 1))
			{
				mUploadMode = kUploadModeDirectNV12;
				d3dfmt = D3DFMT_L8;

				uint32 subw = (texw + 1) >> 1;
				uint32 subh = (texh + 1) >> 1;

				if (subw < 1)
					subw = 1;
				if (subh < 1)
					subh = 1;

				hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_A8L8, texPool, &mpD3DImageTexture2a, NULL);
				if (FAILED(hr)) {
					Shutdown();
					return false;
				}

				if (useDefault) {
					hr = dev->CreateTexture(subw, subh, 1, 0, D3DFMT_A8L8, D3DPOOL_SYSTEMMEM, &mpD3DImageTexture2aUpload, NULL);
					if (FAILED(hr)) {
						Shutdown();
						return false;
					}
				}
			}
			break;

		case nsVDPixmap::kPixFormat_YUV422_V210:
			if (mpManager->IsTextureFormatAvailable(D3DFMT_A2B10G10R10) && caps.PixelShaderVersion >= D3DPS_VERSION(2, 0)) {
				mUploadMode = kUploadModeDirectV210;
				d3dfmt = D3DFMT_A2B10G10R10;
			}
			break;
	}
	
	if (mUploadMode == kUploadModeNormal) {
		mpVideoManager->DetermineBestTextureFormat(source.format, mTexFmt.format, d3dfmt);

		if (source.format != mTexFmt.format) {
			if ((source.format == nsVDPixmap::kPixFormat_YUV422_UYVY ||
				 source.format == nsVDPixmap::kPixFormat_YUV422_YUYV ||
				 source.format == nsVDPixmap::kPixFormat_YUV422_UYVY_709 ||
				 source.format == nsVDPixmap::kPixFormat_YUV422_YUYV_709 ||
				 source.format == nsVDPixmap::kPixFormat_YUV422_UYVY_FR ||
				 source.format == nsVDPixmap::kPixFormat_YUV422_YUYV_FR ||
				 source.format == nsVDPixmap::kPixFormat_YUV422_UYVY_709_FR ||
				 source.format == nsVDPixmap::kPixFormat_YUV422_YUYV_709_FR
				 )
				&& caps.PixelShaderVersion >= D3DPS_VERSION(1,1))
			{
				if (mpManager->IsTextureFormatAvailable(D3DFMT_A8R8G8B8)) {
					mUploadMode = kUploadModeDirect16;
					d3dfmt = D3DFMT_A8R8G8B8;
				}
			} else if (!allowConversion) {
				Shutdown();
				return false;
			}
		}
	}

	if (mUploadMode != kUploadModeNormal) {
		mpD3DImageTextures.resize(1, nullptr);
		mpD3DConversionTextures.resize(buffers, nullptr);
	} else {
		mpD3DImageTextures.resize(buffers, nullptr);
	}

	mConversionTexW = texw;
	mConversionTexH = texh;
	if (!ReinitVRAMTextures()) {
		Shutdown();
		return false;
	}

	if (mUploadMode == kUploadModeDirectV210) {
		texw = ((source.w + 5) / 6) * 4;
		texh = source.h;
		mpManager->AdjustTextureSize(texw, texh);
	} else if (mUploadMode == kUploadModeDirect16) {
		texw = (source.w + 1) >> 1;
		texh = source.h;
		mpManager->AdjustTextureSize(texw, texh);
	}

	mTexFmt.w			= texw;
	mTexFmt.h			= texh;
	mTexD3DFmt = d3dfmt;

	if (!ReinitImageTextures()) {
		Shutdown();
		return false;
	}

	if (useDefault) {
		hr = dev->CreateTexture(texw, texh, 1, 0, d3dfmt, D3DPOOL_SYSTEMMEM, &mpD3DImageTextureUpload, nullptr);
		if (FAILED(hr)) {
			Shutdown();
			return false;
		}
	}

	// clear source textures
	for(uint32 i=0; i<mpD3DImageTextures.size(); ++i) {
		ClearImageTexture(i);
	}

	VDDEBUG_DX9DISP("VideoDisplay/DX9: Init successful for %dx%d source image (%s -> %s); monitor=%p\n", source.w, source.h, VDPixmapGetInfo(source.format).name, VDPixmapGetInfo(mTexFmt.format).name, hmonitor);
	return true;
}

void VDVideoUploadContextD3D9::Shutdown() {
	for(IDirect3DTexture9 *&tex : mpD3DConversionTextures)
		vdsaferelease <<= tex;

	for(IDirect3DTexture9 *&tex : mpD3DImageTextures)
		vdsaferelease <<= tex;

	vdsaferelease <<= mpD3DPaletteTexture;
	vdsaferelease <<= mpD3DPaletteTextureUpload;
	vdsaferelease <<= mpD3DImageTexture2d;
	vdsaferelease <<= mpD3DImageTexture2dUpload;
	vdsaferelease <<= mpD3DImageTexture2c;
	vdsaferelease <<= mpD3DImageTexture2cUpload;
	vdsaferelease <<= mpD3DImageTexture2b;
	vdsaferelease <<= mpD3DImageTexture2bUpload;
	vdsaferelease <<= mpD3DImageTexture2a;
	vdsaferelease <<= mpD3DImageTexture2aUpload;
	vdsaferelease <<= mpD3DImageTextureUpload;

	mpVideoManager = NULL;
	if (mpManager) {
		VDDeinitDirect3D9(mpManager, this);
		mpManager = NULL;
	}
}

void VDVideoUploadContextD3D9::SetBufferCount(uint32 buffers) {
	if (buffers == 0)
		buffers = 1;

	if (mpD3DConversionTextures.empty()) {
		while(mpD3DImageTextures.size() > buffers) {
			vdsaferelease <<= mpD3DImageTextures.back();
			mpD3DImageTextures.pop_back();
		}

		mpD3DImageTextures.resize(buffers, nullptr);

		ReinitImageTextures();
	} else {
		while(mpD3DConversionTextures.size() > buffers) {
			vdsaferelease <<= mpD3DConversionTextures.back();
			mpD3DConversionTextures.pop_back();
		}

		mpD3DConversionTextures.resize(buffers, nullptr);

		ReinitVRAMTextures();
	}
}

bool VDVideoUploadContextD3D9::Update(const VDPixmap& source, int fieldMask) {
	using namespace nsVDDisplay;

	if (mpD3DConversionTextures.size() > 1)
		std::rotate(mpD3DConversionTextures.begin(), mpD3DConversionTextures.end() - 1, mpD3DConversionTextures.end());

	if (mpD3DImageTextures.size() > 1)
		std::rotate(mpD3DImageTextures.begin(), mpD3DImageTextures.end() - 1, mpD3DImageTextures.end());

	D3DLOCKED_RECT lr;
	HRESULT hr;

	if (mpD3DPaletteTexture) {
		bool paletteValid = mbPaletteTextureValid;
		if (paletteValid) {
			if (source.palette) {
				if (memcmp(mLastPalette.data(), source.palette, sizeof(uint32)*256))
					paletteValid = false;
			} else {
				if (!mbPaletteTextureIdentity)
					paletteValid = false;
			}
		}

		if (!paletteValid) {
			if (!Lock(mpD3DPaletteTexture, mpD3DPaletteTextureUpload, &lr))
				return false;

			if (source.palette) {
				memcpy(mLastPalette.data(), source.palette, 256*4);

				mbPaletteTextureIdentity = false;
			} else {
				uint32 *dst = mLastPalette.data();
				uint32 v = 0;
				for(uint32 i=0; i<256; ++i) {
					*dst++ = v;
					v += 0x010101;
				}

				mbPaletteTextureIdentity = true;
			}

			memcpy(lr.pBits, mLastPalette.data(), 256*4);

			VDVERIFY(Unlock(mpD3DPaletteTexture, mpD3DPaletteTextureUpload));

			mbPaletteTextureValid = true;
		}
	}
	
	if (!Lock(mpD3DImageTextures[0], mpD3DImageTextureUpload, &lr))
		return false;

	mTexFmt.data		= lr.pBits;
	mTexFmt.pitch		= lr.Pitch;

	VDPixmap dst(mTexFmt);
	VDPixmap src(source);

	if (fieldMask == 1) {
		dst = VDPixmapExtractField(mTexFmt, false);
		src = VDPixmapExtractField(source, false);
	} else if (fieldMask == 2) {
		dst = VDPixmapExtractField(mTexFmt, true);
		src = VDPixmapExtractField(source, true);
	}

	if (mUploadMode == kUploadModeDirectV210) {
		VDMemcpyRect(dst.data, dst.pitch, src.data, src.pitch, ((src.w + 5) / 6) * 16, src.h);
	} else if (mUploadMode == kUploadModeDirect16) {
		VDMemcpyRect(dst.data, dst.pitch, src.data, src.pitch, src.w * 2, src.h);
	} else if (mUploadMode == kUploadModeDirect8 || mUploadMode == kUploadModeDirect8Laced || mUploadMode == kUploadModeDirectNV12) {
		VDMemcpyRect(dst.data, dst.pitch, src.data, src.pitch, src.w, src.h);
	} else {
		if (dst.w > src.w)
			dst.w = src.w;
		
		if (dst.h > src.h)
			dst.h = src.h;

		mCachedBlitter.Blit(dst, src);
	}

	VDVERIFY(Unlock(mpD3DImageTextures[0], mpD3DImageTextureUpload));

	if (mUploadMode == kUploadModeDirectNV12) {
		uint32 subw = (source.w + 1) >> 1;
		uint32 subh = (source.h + 1) >> 1;

		if (subw < 1)
			subw = 1;
		if (subh < 1)
			subh = 1;

		// upload chroma plane
		if (!Lock(mpD3DImageTexture2a, mpD3DImageTexture2aUpload, &lr))
			return false;

		VDMemcpyRect(lr.pBits, lr.Pitch, source.data2, source.pitch2, subw*2, subh);

		VDVERIFY(Unlock(mpD3DImageTexture2a, mpD3DImageTexture2aUpload));

	} else if (mUploadMode == kUploadModeDirect8Laced) {
		uint32 subw = (source.w + 1) >> 1;
		uint32 subh = (source.h + 1) >> 1;
		sint32 subh1 = (subh + 1) >> 1;
		sint32 subh2 = subh >> 1;

		if (fieldMask & 1) {
			const VDPixmap srcPlane1(VDPixmapExtractField(source, false));

			// upload Cb plane
			if (!Lock(mpD3DImageTexture2a, mpD3DImageTexture2aUpload, &lr))
				return false;

			VDMemcpyRect(lr.pBits, lr.Pitch, srcPlane1.data2, srcPlane1.pitch2, subw, subh1);

			VDVERIFY(Unlock(mpD3DImageTexture2a, mpD3DImageTexture2aUpload));

			// upload Cr plane
			if (!Lock(mpD3DImageTexture2b, mpD3DImageTexture2bUpload, &lr))
				return false;

			VDMemcpyRect(lr.pBits, lr.Pitch, srcPlane1.data3, srcPlane1.pitch3, subw, subh1);

			VDVERIFY(Unlock(mpD3DImageTexture2b, mpD3DImageTexture2bUpload));
		}

		if (fieldMask & 2) {
			const VDPixmap srcPlane2(VDPixmapExtractField(source, true));

			// upload Cb plane
			if (!Lock(mpD3DImageTexture2c, mpD3DImageTexture2cUpload, &lr))
				return false;

			VDMemcpyRect(lr.pBits, lr.Pitch, srcPlane2.data2, srcPlane2.pitch2, subw, subh2);

			VDVERIFY(Unlock(mpD3DImageTexture2c, mpD3DImageTexture2cUpload));

			// upload Cr plane
			if (!Lock(mpD3DImageTexture2d, mpD3DImageTexture2dUpload, &lr))
				return false;

			VDMemcpyRect(lr.pBits, lr.Pitch, srcPlane2.data3, srcPlane2.pitch3, subw, subh2);

			VDVERIFY(Unlock(mpD3DImageTexture2d, mpD3DImageTexture2dUpload));
		}
	} else if (mUploadMode == kUploadModeDirect8) {
		uint32 subw = source.w;
		uint32 subh = source.h;

		switch(source.format) {
			case nsVDPixmap::kPixFormat_YUV410_Planar:
			case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV410_Planar_709:
			case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
				subw >>= 2;
				subh >>= 2;
				break;
			case nsVDPixmap::kPixFormat_YUV420_Planar:
			case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV420_Planar_709:
			case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
			case nsVDPixmap::kPixFormat_YUV420it_Planar:
			case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
			case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
			case nsVDPixmap::kPixFormat_YUV420ib_Planar:
			case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
			case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
				subw >>= 1;
				subh >>= 1;
				break;
			case nsVDPixmap::kPixFormat_YUV422_Planar:
			case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV422_Planar_709:
			case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
				subw >>= 1;
				break;
			case nsVDPixmap::kPixFormat_YUV444_Planar:
			case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
			case nsVDPixmap::kPixFormat_YUV444_Planar_709:
			case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
				break;
		}

		if (subw < 1)
			subw = 1;
		if (subh < 1)
			subh = 1;

		if (source.format != nsVDPixmap::kPixFormat_Pal8) {
			// upload Cb plane
			if (!Lock(mpD3DImageTexture2a, mpD3DImageTexture2aUpload, &lr))
				return false;

			VDMemcpyRect(lr.pBits, lr.Pitch, source.data2, source.pitch2, subw, subh);

			VDVERIFY(Unlock(mpD3DImageTexture2a, mpD3DImageTexture2aUpload));

			// upload Cr plane
			if (!Lock(mpD3DImageTexture2b, mpD3DImageTexture2bUpload, &lr))
				return false;

			VDMemcpyRect(lr.pBits, lr.Pitch, source.data3, source.pitch3, subw, subh);

			VDVERIFY(Unlock(mpD3DImageTexture2b, mpD3DImageTexture2bUpload));
		}
	}

	if (mUploadMode != kUploadModeNormal) {
		IDirect3DDevice9 *dev = mpManager->GetDevice();
		vdrefptr<IDirect3DSurface9> rtsurface;

		hr = mpD3DConversionTextures[0]->GetSurfaceLevel(0, ~rtsurface);
		if (FAILED(hr))
			return false;

		hr = dev->SetStreamSource(0, mpManager->GetVertexBuffer(), 0, sizeof(Vertex));
		if (FAILED(hr))
			return false;

		hr = dev->SetIndices(mpManager->GetIndexBuffer());
		if (FAILED(hr))
			return false;

		hr = dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2);
		if (FAILED(hr))
			return false;

		hr = dev->SetRenderTarget(0, rtsurface);
		if (FAILED(hr))
			return false;

		static const uint32 kRenderStates[][2]={
			{	D3DRS_LIGHTING,			FALSE				},
			{	D3DRS_CULLMODE,			D3DCULL_NONE		},
			{	D3DRS_ZENABLE,			FALSE				},
			{	D3DRS_ALPHATESTENABLE,	FALSE				},
			{	D3DRS_ALPHABLENDENABLE,	FALSE				},
			{	D3DRS_STENCILENABLE,	FALSE				},
		};

		for(int i=0; i<sizeof(kRenderStates)/sizeof(kRenderStates[0]); ++i) {
			const uint32 (&rs)[2] = kRenderStates[i];

			hr = dev->SetRenderState((D3DRENDERSTATETYPE)rs[0], rs[1]);
			if (FAILED(hr))
				return false;
		}

		bool success = false;
		if (mpManager->BeginScene()) {
			success = true;

			D3DVIEWPORT9 vp = { 0, 0, (DWORD)source.w, (DWORD)source.h, 0, 1 };
			hr = dev->SetViewport(&vp);
			if (FAILED(hr))
				success = false;

			if (success) {
				VDVideoDisplayDX9Manager::EffectContext ctx;

				ctx.mpSourceTexture1 = mpD3DImageTextures[0];
				ctx.mpSourceTexture2 = mpD3DImageTexture2a;
				ctx.mpSourceTexture3 = mpD3DImageTexture2b;
				ctx.mpSourceTexture4 = mpD3DImageTexture2c;
				ctx.mpSourceTexture5 = mpD3DImageTexture2d;
				ctx.mpPaletteTexture = mpD3DPaletteTexture;
				ctx.mpInterpFilterH = NULL;
				ctx.mpInterpFilterV = NULL;
				ctx.mpInterpFilter = NULL;
				ctx.mSourceW = source.w;
				ctx.mSourceH = source.h;
				ctx.mSourceTexW = mTexFmt.w;
				ctx.mSourceTexH = mTexFmt.h;
				ctx.mSourceArea.set(0.0f, 0.0f, (float)source.w, (float)source.h);
				ctx.mInterpHTexW = 0;
				ctx.mInterpHTexH = 0;
				ctx.mInterpVTexW = 0;
				ctx.mInterpVTexH = 0;
				ctx.mInterpTexW = 0;
				ctx.mInterpTexH = 0;
				ctx.mViewportX = 0;
				ctx.mViewportY = 0;
				ctx.mViewportW = source.w;
				ctx.mViewportH = source.h;
				ctx.mOutputX = 0;
				ctx.mOutputY = 0;
				ctx.mOutputW = source.w;
				ctx.mOutputH = source.h;
				ctx.mDefaultUVScaleCorrectionX = 1.0f;
				ctx.mDefaultUVScaleCorrectionY = 1.0f;
				ctx.mChromaScaleU = 1.0f;
				ctx.mChromaScaleV = 1.0f;
				ctx.mChromaOffsetU = 0.0f;
				ctx.mChromaOffsetV = 0.0f;
				ctx.mbHighPrecision = mbHighPrecision;
				ctx.mFieldOffset = 0.0f;
				ctx.mPixelSharpnessX = 0.0f;
				ctx.mPixelSharpnessY = 0.0f;

				switch(source.format) {
					case nsVDPixmap::kPixFormat_YUV422_V210:
						if (!mpVideoManager->RunEffect(ctx, g_technique_v210_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_UYVY_FR:
						ctx.mDefaultUVScaleCorrectionX = 0.5f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_uyvy601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_UYVY_709_FR:
						ctx.mDefaultUVScaleCorrectionX = 0.5f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_uyvy709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_YUYV_709:
						ctx.mDefaultUVScaleCorrectionX = 0.5f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_yuyv709_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_YUYV_FR:
						ctx.mDefaultUVScaleCorrectionX = 0.5f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_yuyv601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_YUYV_709_FR:
						ctx.mDefaultUVScaleCorrectionX = 0.5f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_yuyv709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV444_Planar_709:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar_709:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar_709:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;
					case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = -0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420i_Planar:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr420i_601_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420i_Planar_709:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr420i_709_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420i_Planar_FR:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr420i_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420i_Planar_709_FR:
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr420i_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					// 4:2:0 interlaced field

					case nsVDPixmap::kPixFormat_YUV420it_Planar:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = +0.25f;
						ctx.mChromaOffsetV = +0.125f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = +0.25f;
						ctx.mChromaOffsetV = +0.125f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = +0.25f;
						ctx.mChromaOffsetV = +0.125f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = +0.25f;
						ctx.mChromaOffsetV = +0.125f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420ib_Planar:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = +0.25f;
						ctx.mChromaOffsetV = -0.125f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = +0.25f;
						ctx.mChromaOffsetV = -0.125f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = +0.25f;
						ctx.mChromaOffsetV = -0.125f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
						ctx.mChromaScaleU = 0.5f;
						ctx.mChromaScaleV = 0.5f;
						ctx.mChromaOffsetU = +0.25f;
						ctx.mChromaOffsetV = -0.125f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					// 4:1:0

					case nsVDPixmap::kPixFormat_YUV410_Planar_709:
						ctx.mChromaScaleU = 0.25f;
						ctx.mChromaScaleV = 0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
						ctx.mChromaScaleU = 0.25f;
						ctx.mChromaScaleV = 0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
						ctx.mChromaScaleU = 0.25f;
						ctx.mChromaScaleV = 0.25f;
						if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_709fr_to_rgb_2_0, rtsurface))
							success = false;
						break;

					case nsVDPixmap::kPixFormat_Pal8:
						if (mpVideoManager->IsPS20Enabled()) {
							if (!mpVideoManager->RunEffect(ctx, g_technique_pal8_to_rgb_2_0, rtsurface))
								success = false;
						} else {
							if (!mpVideoManager->RunEffect(ctx, g_technique_pal8_to_rgb_1_1, rtsurface))
								success = false;
						}
						break;

					default:
						if (mbHighPrecision) {
							switch(source.format) {
								case nsVDPixmap::kPixFormat_YUV422_UYVY:
									ctx.mDefaultUVScaleCorrectionX = 0.5f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_uyvy601_to_rgb_2_0, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV422_UYVY_709:
									ctx.mDefaultUVScaleCorrectionX = 0.5f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_uyvy709_to_rgb_2_0, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV422_YUYV:
									ctx.mDefaultUVScaleCorrectionX = 0.5f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_yuyv601_to_rgb_2_0, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV444_Planar:
									if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601_to_rgb_2_0, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV422_Planar:
									ctx.mChromaScaleU = 0.5f;
									ctx.mChromaOffsetU = -0.25f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601_to_rgb_2_0, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV420_Planar:
									ctx.mChromaScaleU = 0.5f;
									ctx.mChromaScaleV = 0.5f;
									ctx.mChromaOffsetU = -0.25f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601_to_rgb_2_0, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV410_Planar:
									ctx.mChromaScaleU = 0.25f;
									ctx.mChromaScaleV = 0.25f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_601_to_rgb_2_0, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV420_NV12:
									if (!mpVideoManager->RunEffect(ctx, g_technique_nv12_to_rgb_2_0, rtsurface))
										success = false;
									break;
							}
						} else {
							switch(source.format) {
								case nsVDPixmap::kPixFormat_YUV422_UYVY:
									ctx.mDefaultUVScaleCorrectionX = 0.5f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_uyvy_to_rgb_1_1, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV422_UYVY_709:
									ctx.mDefaultUVScaleCorrectionX = 0.5f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_hdyc_to_rgb_1_1, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV422_YUYV:
									ctx.mDefaultUVScaleCorrectionX = 0.5f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_yuy2_to_rgb_1_1, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV444_Planar:
									if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_to_rgb_1_1, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV422_Planar:
									ctx.mChromaScaleU = 0.5f;
									ctx.mChromaOffsetU = -0.25f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_to_rgb_1_1, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV420_Planar:
									ctx.mChromaScaleU = 0.5f;
									ctx.mChromaScaleV = 0.5f;
									ctx.mChromaOffsetU = -0.25f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_to_rgb_1_1, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV410_Planar:
									ctx.mChromaScaleU = 0.25f;
									ctx.mChromaScaleV = 0.25f;
									if (!mpVideoManager->RunEffect(ctx, g_technique_ycbcr_to_rgb_1_1, rtsurface))
										success = false;
									break;

								case nsVDPixmap::kPixFormat_YUV420_NV12:
									if (!mpVideoManager->RunEffect(ctx, g_technique_nv12_to_rgb_1_1, rtsurface))
										success = false;
									break;
							}
						}
						break;
				}
			}

			if (!mpManager->EndScene())
				success = false;
		}

		dev->SetRenderTarget(0, mpManager->GetRenderTarget());

		return success;
	}

	return true;
}

bool VDVideoUploadContextD3D9::Lock(IDirect3DTexture9 *tex, IDirect3DTexture9 *upload, D3DLOCKED_RECT *lr) {
	HRESULT hr = (upload ? upload : tex)->LockRect(0, lr, NULL, 0);

	return SUCCEEDED(hr);
}

bool VDVideoUploadContextD3D9::Unlock(IDirect3DTexture9 *tex, IDirect3DTexture9 *upload) {
	HRESULT hr;

	if (upload) {
		hr = upload->UnlockRect(0);
		if (FAILED(hr))
			return false;

		hr = mpManager->GetDevice()->UpdateTexture(upload, tex);
	} else {
		hr = tex->UnlockRect(0);
	}

	return SUCCEEDED(hr);
}

void VDVideoUploadContextD3D9::OnPreDeviceReset() {
	for(IDirect3DTexture9 *&tex : mpD3DConversionTextures)
		vdsaferelease <<= tex;
}

void VDVideoUploadContextD3D9::OnPostDeviceReset() {
	ReinitVRAMTextures();
}

bool VDVideoUploadContextD3D9::ReinitImageTextures() {
	for(IDirect3DTexture9 *& tex : mpD3DImageTextures) {
		if (!tex) {
			const bool useDefault = (mpManager->GetDeviceEx() != NULL);
			const D3DPOOL texPool = useDefault ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;

			HRESULT hr = mpManager->GetDevice()->CreateTexture(mTexFmt.w, mTexFmt.h, 1, 0, mTexD3DFmt, texPool, &tex, NULL);
			if (FAILED(hr))
				return false;
		}
	}

	return true;
}

bool VDVideoUploadContextD3D9::ReinitVRAMTextures() {
	if (mUploadMode != kUploadModeNormal) {
		IDirect3DDevice9 *dev = mpManager->GetDevice();

		for(IDirect3DTexture9 *&tex : mpD3DConversionTextures) {
			if (!tex) {
				HRESULT hr = dev->CreateTexture(mConversionTexW, mConversionTexH, 1, D3DUSAGE_RENDERTARGET, mbHighPrecision ? D3DFMT_A16B16G16R16F : D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &tex, NULL);
				if (FAILED(hr))
					return false;

				mpManager->ClearRenderTarget(tex);
			}
		}
	}

	return true;
}

void VDVideoUploadContextD3D9::ClearImageTexture(uint32 i) {
	if (!mpD3DImageTextures[i])
		return;

	uint32 texw = mTexFmt.w;
	uint32 texh = mTexFmt.h;

	IDirect3DTexture9 *pDstTex = mpD3DImageTextures[i];
	IDirect3DTexture9 *pSrcTex = mpD3DImageTextureUpload;

	if (!pSrcTex)
		pSrcTex = pDstTex;
		
	D3DLOCKED_RECT lr;
	if (FAILED(pSrcTex->LockRect(0, &lr, NULL, 0)))
		return;

	switch(mSourceFmt) {
		case nsVDPixmap::kPixFormat_YUV422_UYVY:
		case nsVDPixmap::kPixFormat_YUV422_UYVY_709:
			VDMemset32Rect(lr.pBits, lr.Pitch, 0x80108010, (texw + 1) >> 1, texh);
			break;
		case nsVDPixmap::kPixFormat_YUV422_UYVY_FR:
		case nsVDPixmap::kPixFormat_YUV422_UYVY_709_FR:
			VDMemset32Rect(lr.pBits, lr.Pitch, 0x80008000, (texw + 1) >> 1, texh);
			break;
		case nsVDPixmap::kPixFormat_YUV422_YUYV:
		case nsVDPixmap::kPixFormat_YUV422_YUYV_709:
			VDMemset32Rect(lr.pBits, lr.Pitch, 0x10801080, (texw + 1) >> 1, texh);
			break;
		case nsVDPixmap::kPixFormat_YUV422_YUYV_FR:
		case nsVDPixmap::kPixFormat_YUV422_YUYV_709_FR:
			VDMemset32Rect(lr.pBits, lr.Pitch, 0x00800080, (texw + 1) >> 1, texh);
			break;
		case nsVDPixmap::kPixFormat_YUV444_Planar:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709:
		case nsVDPixmap::kPixFormat_YUV422_Planar:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420_Planar:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420i_Planar:
		case nsVDPixmap::kPixFormat_YUV420i_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420it_Planar:
		case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
		case nsVDPixmap::kPixFormat_YUV411_Planar:
		case nsVDPixmap::kPixFormat_YUV411_Planar_709:
		case nsVDPixmap::kPixFormat_YUV410_Planar:
		case nsVDPixmap::kPixFormat_YUV410_Planar_709:
		case nsVDPixmap::kPixFormat_YUV420_NV12:
			VDMemset8Rect(lr.pBits, lr.Pitch, 0x10, texw, texh);
			break;
		case nsVDPixmap::kPixFormat_YUV422_V210:
			VDMemset32Rect(lr.pBits, lr.Pitch, 0x20080200, ((texw + 5) / 6) * 4, texh);
			break;
		case nsVDPixmap::kPixFormat_XRGB1555:
		case nsVDPixmap::kPixFormat_RGB565:
			VDMemset16Rect(lr.pBits, lr.Pitch, 0, texw, texh);
			break;
		case nsVDPixmap::kPixFormat_YUV444_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV444_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV422_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420i_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420i_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV411_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV411_Planar_709_FR:
		case nsVDPixmap::kPixFormat_YUV410_Planar_FR:
		case nsVDPixmap::kPixFormat_YUV410_Planar_709_FR:
		case nsVDPixmap::kPixFormat_Pal8:
		case nsVDPixmap::kPixFormat_Y8:
		case nsVDPixmap::kPixFormat_Y8_FR:
			VDMemset8Rect(lr.pBits, lr.Pitch, 0, texw, texh);
			break;
		default:
			VDMemset32Rect(lr.pBits, lr.Pitch, 0, texw, texh);
			break;
	}

	pSrcTex->UnlockRect(0);

	if (pSrcTex != pDstTex)
		mpManager->GetDevice()->UpdateTexture(pSrcTex, pDstTex);
}

///////////////////////////////////////////////////////////////////////////

class VDVideoDisplayMinidriverDX9 final : public VDVideoDisplayMinidriver, public IVDDisplayCompositionEngine, protected VDD3D9Client {
public:
	VDVideoDisplayMinidriverDX9(bool clipToMonitor, bool use9ex);
	~VDVideoDisplayMinidriverDX9();

protected:
	bool Init(HWND hwnd, HMONITOR hmonitor, const VDVideoDisplaySourceInfo& info) override;
	void Shutdown() override;

	bool ModifySource(const VDVideoDisplaySourceInfo& info) override;

	bool IsValid() override;
	bool IsFramePending() override { return mbSwapChainPresentPending; }
	void SetFilterMode(FilterMode mode) override;
	void SetFullScreen(bool fs, uint32 w, uint32 h, uint32 refresh) override;

	bool Tick(int id) override;
	void Poll() override;
	bool Resize(int w, int h) override;
	bool Invalidate() override;
	void PresentQueued() { Poll(); }
	bool Update(UpdateMode) override;
	void Refresh(UpdateMode) override;
	bool Paint(HDC hdc, const RECT& rClient, UpdateMode mode) override;

	void SetLogicalPalette(const uint8 *pLogicalPalette) override;
	bool AreVSyncTicksNeeded() const override { return (mbSwapChainVsync && !mbSwapChainVsyncEvent) || (mbFullScreen && mbSwapChainPresentPolling); }
	float GetSyncDelta() const override { return mSyncDelta; }

	IVDDisplayCompositionEngine *GetDisplayCompositionEngine() override { return this; }

public:
	void LoadCustomEffect(const wchar_t *path) override;

protected:
	void OnPreDeviceReset() override;
	void OnPostDeviceReset() override {}

	void OnVsyncEvent(IVDVideoDisplayMinidriverCallback *cb);

	void InitBicubic();
	void ShutdownBicubic();
	bool InitBicubicPS2Filters(int w, int h);
	void ShutdownBicubicPS2Filters();
	bool InitBoxlinearPS11Filters(int w, int h, float facx, float facy);
	void ShutdownBoxlinearPS11Filters();

	bool UpdateBackbuffer(const RECT& rClient, UpdateMode updateMode);
	bool UpdateScreen(const RECT& rClient, UpdateMode updateMode, bool polling);

	void DrawDebugInfo(FilterMode mode, const RECT& rClient);

	HWND				mhwnd = nullptr;
	VDD3D9Manager		*mpManager = nullptr;
	vdrefptr<VDVideoDisplayDX9Manager>	mpVideoManager;
	IDirect3DDevice9	*mpD3DDevice = nullptr;			// weak ref
	vdrefptr<IDirect3DTexture9>	mpD3DInterpFilterTextureH;
	vdrefptr<IDirect3DTexture9>	mpD3DInterpFilterTextureV;
	vdrefptr<IDirect3DTexture9>	mpD3DInterpFilterTexture;
	int					mInterpFilterHSize = 0;
	int					mInterpFilterHTexSize = 0;
	int					mInterpFilterVSize = 0;
	int					mInterpFilterVTexSize = 0;
	int					mInterpFilterTexSizeW = 0;
	int					mInterpFilterTexSizeH = 0;
	float				mInterpFilterFactorX = 0;
	float				mInterpFilterFactorY = 0;

	vdrefptr<VDVideoUploadContextD3D9>	mpUploadContext;
	vdrefptr<IVDFontRendererD3D9>	mpFontRenderer;
	vdrefptr<IVDDisplayRendererD3D9>	mpRenderer;

	vdrefptr<IVDD3D9SwapChain>	mpSwapChain;
	int					mSwapChainW = 0;
	int					mSwapChainH = 0;
	bool				mbSwapChainImageValid = false;
	bool				mbSwapChainPresentPending = false;
	bool				mbSwapChainPresentPolling = false;
	bool				mbSwapChainVsync = false;
	bool				mbSwapChainVsyncEvent = false;
	VDAtomicBool		mbSwapChainVsyncEventPending = false;
	bool				mbFirstPresent = true;
	bool				mbFullScreen = false;
	bool				mbFullScreenSet = false;
	uint32				mFullScreenWidth = 0;
	uint32				mFullScreenHeight = 0;
	uint32				mFullScreenRefreshRate = 0;
	const bool			mbClipToMonitor;

	VDVideoDisplayDX9Manager::CubicMode	mCubicMode;
	bool				mbCubicInitialized = false;
	bool				mbCubicAttempted = false;
	bool				mbCubicUsingHighPrecision = false;
	bool				mbCubicTempSurfacesInitialized = false;
	bool				mbBoxlinearCapable11 = false;
	const bool			mbUseD3D9Ex;

	FilterMode			mPreferredFilter = kFilterAnySuitable;
	float				mSyncDelta = 0;
	VDD3DPresentHistory	mPresentHistory;

	VDPixmap					mTexFmt;

	VDVideoDisplaySourceInfo	mSource;

	VDStringA		mFormatString;
	VDStringA		mDebugString;
	VDStringA		mErrorString;

	vdautoptr<IVDDisplayCustomShaderPipelineD3D9> mpCustomPipeline;
};

IVDVideoDisplayMinidriver *VDCreateVideoDisplayMinidriverDX9(bool clipToMonitor, bool use9ex) {
	return new VDVideoDisplayMinidriverDX9(clipToMonitor, use9ex);
}

VDVideoDisplayMinidriverDX9::VDVideoDisplayMinidriverDX9(bool clipToMonitor, bool use9ex)
	: mbClipToMonitor(clipToMonitor)
	, mbUseD3D9Ex(use9ex)
{
}

VDVideoDisplayMinidriverDX9::~VDVideoDisplayMinidriverDX9() {
}

bool VDVideoDisplayMinidriverDX9::Init(HWND hwnd, HMONITOR hmonitor, const VDVideoDisplaySourceInfo& info) {
	VDASSERT(!mpManager);
	mhwnd = hwnd;
	mSource = info;

	// attempt to initialize D3D9
	mbFullScreenSet = false;
	mpManager = VDInitDirect3D9(this, hmonitor, mbUseD3D9Ex);
	if (!mpManager) {
		Shutdown();
		return false;
	}

	if (!VDInitDisplayDX9(hmonitor, mbUseD3D9Ex, ~mpVideoManager)) {
		Shutdown();
		return false;
	}

	if (mbFullScreen && !mbFullScreenSet) {
		mbFullScreenSet = true;
		mpManager->AdjustFullScreen(true, mFullScreenWidth, mFullScreenHeight, mFullScreenRefreshRate);
	}

	mpD3DDevice = mpManager->GetDevice();

	if (!VDCreateDisplayRendererD3D9(~mpRenderer)) {
		Shutdown();
		return false;
	}

	mpRenderer->Init(mpManager, mpVideoManager);

	mpUploadContext = new_nothrow VDVideoUploadContextD3D9;
	const uint32 bufferCount = mpCustomPipeline ? mpCustomPipeline->GetMaxPrevFrames() + 1 : 1;
	if (!mpUploadContext || !mpUploadContext->Init(hmonitor, mbUseD3D9Ex, info.pixmap, info.bAllowConversion, mbHighPrecision && mpVideoManager->Is16FEnabled(), bufferCount)) {
		Shutdown();
		return false;
	}

	mSyncDelta = 0.0f;
	mbFirstPresent = true;

	mbBoxlinearCapable11 = mpVideoManager->IsPS11Enabled();

	mErrorString.clear();

	return true;
}

void VDVideoDisplayMinidriverDX9::LoadCustomEffect(const wchar_t *path) {
	mErrorString.clear();

	if (path && *path) {
		try {
			mpCustomPipeline = VDDisplayParseCustomShaderPipeline(mpManager, path);

			if (mpUploadContext)
				mpUploadContext->SetBufferCount(mpCustomPipeline->GetMaxPrevFrames() + 1);
		} catch(const MyError& e) {
			mErrorString = e.c_str();

			if (mpUploadContext)
				mpUploadContext->SetBufferCount(1);
		}
	} else {
		mpCustomPipeline.reset();

		if (mpUploadContext)
			mpUploadContext->SetBufferCount(1);
	}
}

void VDVideoDisplayMinidriverDX9::OnPreDeviceReset() {
	ShutdownBicubic();
	ShutdownBicubicPS2Filters();
	ShutdownBoxlinearPS11Filters();
	mpSwapChain = NULL;
	mSwapChainW = 0;
	mSwapChainH = 0;
	mbSwapChainImageValid = false;
	mbSwapChainVsync = false;
	mbSwapChainVsyncEvent = false;
}

void VDVideoDisplayMinidriverDX9::OnVsyncEvent(IVDVideoDisplayMinidriverCallback *cb) {
	if (mbSwapChainVsyncEventPending.xchg(false))
		mSource.mpCB->QueuePresent();
}

void VDVideoDisplayMinidriverDX9::InitBicubic() {
	if (mbCubicInitialized || mbCubicAttempted)
		return;

	mbCubicAttempted = true;

	mCubicMode = mpVideoManager->InitBicubic();

	if (mCubicMode == VDVideoDisplayDX9Manager::kCubicNotPossible)
		return;

	VDASSERT(!mbCubicTempSurfacesInitialized);
	mbCubicUsingHighPrecision = mbHighPrecision;
	mbCubicTempSurfacesInitialized = mpVideoManager->InitBicubicTempSurfaces(mbCubicUsingHighPrecision);
	if (!mbCubicTempSurfacesInitialized) {
		mpVideoManager->ShutdownBicubic();
		mCubicMode = VDVideoDisplayDX9Manager::kCubicNotPossible;
		return;
	}

	VDDEBUG_DX9DISP("VideoDisplay/DX9: Bicubic initialization complete.\n");
	if (mCubicMode == VDVideoDisplayDX9Manager::kCubicUsePS1_4Path)
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Using pixel shader 1.4, 5 texture (RADEON 8xxx+ / GeForceFX+) pixel path.\n");
	else
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Using pixel shader 1.1, 4 texture (GeForce3/4) pixel path.\n");

	mbCubicInitialized = true;
}

void VDVideoDisplayMinidriverDX9::ShutdownBicubic() {
	if (mbCubicInitialized) {
		mbCubicInitialized = mbCubicAttempted = false;

		if (mbCubicTempSurfacesInitialized) {
			mbCubicTempSurfacesInitialized = false;

			mpVideoManager->ShutdownBicubicTempSurfaces(mbCubicUsingHighPrecision);
		}

		mpVideoManager->ShutdownBicubic();
	}
}

///////////////////////////////////////////////////////////////////////////

namespace {
	int GeneratePS2CubicTexture(VDD3D9Manager *pManager, int w, int srcw, vdrefptr<IDirect3DTexture9>& pTexture, int existingTexW, bool mode1_4) {
		IDirect3DDevice9 *dev = pManager->GetDevice();

		// Round up to next multiple of 128 pixels to reduce reallocation.
		int texw = (w + 127) & ~127;
		int texh = 1;
		pManager->AdjustTextureSize(texw, texh);

		// If we can't fit the texture, bail.
		if (texw < w)
			return -1;

		// Check if we need to reallocate the texture.
		HRESULT hr;
		D3DFORMAT format = mode1_4 ? D3DFMT_A8R8G8B8 : D3DFMT_X8L8V8U8;
		const bool useDefault = (pManager->GetDeviceEx() != NULL);

		if (!pTexture || existingTexW != texw) {
			hr = dev->CreateTexture(texw, texh, 1, 0, format, useDefault ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, ~pTexture, NULL);
			if (FAILED(hr))
				return -1;
		}

		vdrefptr<IDirect3DTexture9> uploadtex;
		if (useDefault) {
			hr = dev->CreateTexture(texw, texh, 1, 0, format, D3DPOOL_SYSTEMMEM, ~uploadtex, NULL);
			if (FAILED(hr))
				return -1;
		} else {
			uploadtex = pTexture;
		}

		// Fill the texture.
		D3DLOCKED_RECT lr;
		hr = uploadtex->LockRect(0, &lr, NULL, 0);
		VDASSERT(SUCCEEDED(hr));
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load bicubic texture.\n");
			return -1;
		}

		double dudx = (double)srcw / (double)w;
		double u = dudx * 0.5;
		double u0 = 0.5;
		double ud0 = 1.5;
		double ud1 = (double)srcw - 1.5;
		double u1 = (double)srcw - 0.5;
		uint32 *p0 = (uint32 *)lr.pBits;

		if (mode1_4) {
			for(int x = 0; x < texw; ++x) {
				double ut = u;
				if (ut < u0)
					ut = u0;
				else if (ut > u1)
					ut = u1;
				int ix = VDFloorToInt(ut - 0.5);
				double d = ut - ((double)ix + 0.5);

				static const double m = -0.75;
				double c0 = (( (m    )*d - 2.0*m    )*d +   m)*d;
				double c1 = (( (m+2.0)*d -     m-3.0)*d      )*d + 1.0;
				double c2 = ((-(m+2.0)*d + 2.0*m+3.0)*d -   m)*d;
				double c3 = ((-(m    )*d +     m    )*d      )*d;

				double c03		= c0+c3;
				double k1 = d < 0.5 ? d < 1e-5 ? -m : c2 / d : d > 1-1e-5 ? -m : c1 / (1-d);
				double kx = d < 0.5 ? c1 - k1*(1-d) : c2 - k1*d;

				if (ut < ud0 || ut > ud1) {
					c0 = 0;
					k1 = 1.0;
					kx = 0.0;
					c3 = 0;
				}

				double blue		= -c0*4;
				double green	= k1 - 1.0 + 128.0f/255.0f;
				double red		= kx * 2;
				double alpha	= -c3*4;

				blue = fabs(c0 + c3) > 1e-9 ? c0 / (c0 + c3) : 0;
				green = fabs(green + red) > 1e-9 ? green / (green + red) : 0;
				red = fabs(c0 + c3);

				// The rounding here is a bit tricky. Here's how we use the values:
				//	r = p2 * (g + 0.5) + p3 * (r / 2) - p1 * (b / 4) - p4 * (a / 4)
				//
				// Which means we need:
				//	g + 0.5 + r/2 - b/4 - a/4 = 1
				//	g + r/2 - b/4 - a/4 = 0.5
				//	g*4 + r*2 - (b + a) = 2 (510 / 1020)

				uint8 ib = VDClampedRoundFixedToUint8Fast((float)blue);
				uint8 ig = VDClampedRoundFixedToUint8Fast((float)green);
				uint8 ir = VDClampedRoundFixedToUint8Fast((float)red);
				uint8 ia = VDClampedRoundFixedToUint8Fast((float)alpha);

				p0[x] = (uint32)ib + ((uint32)ig << 8) + ((uint32)ir << 16) + ((uint32)ia << 24);

				u += dudx;
			}
		} else {
			for(int x = 0; x < texw; ++x) {
				int ix = VDFloorToInt(u - 0.5);
				double d = u - ((double)ix + 0.5);

				static const double m = -0.75;
				double c0 = (( (m    )*d - 2.0*m    )*d +   m)*d;
				double c1 = (( (m+2.0)*d -     m-3.0)*d      )*d + 1.0;
				double c2 = ((-(m+2.0)*d + 2.0*m+3.0)*d -   m)*d;
				double c3 = ((-(m    )*d +     m    )*d      )*d;

				double k0 = d*(1-d)*m;
				double k2 = d*(1-d)*m;

				double c1bi = d*k0;
				double c2bi = (1-d)*k2;
				double c1ex = c1-c1bi;
				double c2ex = c2-c2bi;

				double o1 = c2ex/(c1ex+c2ex)-d;

				double blue		= d;							// bilinear offset - p0 and p3
				double green	= o1*4;							// bilinear offset - p1 and p2
				double red		= (d*(1-d))*4;					// shift factor between the two
				double alpha	= d;							// lerp constant between p0 and p3

				uint8 ib = VDClampedRoundFixedToUint8Fast((float)blue * 127.0f/255.0f + 128.0f/255.0f) ^ 0x80;
				uint8 ig = VDClampedRoundFixedToUint8Fast((float)green * 127.0f/255.0f + 128.0f/255.0f) ^ 0x80;
				uint8 ir = VDClampedRoundFixedToUint8Fast((float)red);
				uint8 ia = VDClampedRoundFixedToUint8Fast((float)alpha);

				p0[x] = (uint32)ib + ((uint32)ig << 8) + ((uint32)ir << 16) + ((uint32)ia << 24);

				u += dudx;
			}
		}

		VDVERIFY(SUCCEEDED(uploadtex->UnlockRect(0)));

		if (useDefault) {
			hr = pManager->GetDevice()->UpdateTexture(uploadtex, pTexture);
			if (FAILED(hr)) {
				pTexture.clear();
				return -1;
			}
		}

		return texw;
	}

	std::pair<int, int> GeneratePS11BoxlinearTexture(VDD3D9Manager *pManager, int w, int h, int srcw, int srch, float facx, float facy, vdrefptr<IDirect3DTexture9>& pTexture, int existingTexW, int existingTexH) {
		IDirect3DDevice9 *dev = pManager->GetDevice();

		// Round up to next multiple of 128 pixels to reduce reallocation.
		int texw = (w + 127) & ~127;
		int texh = (h + 127) & ~127;
		pManager->AdjustTextureSize(texw, texh);

		// If we can't fit the texture, bail.
		if (texw < w || texh < h)
			return std::pair<int, int>(-1, -1);

		// Check if we need to reallocate the texture.
		HRESULT hr;
		D3DFORMAT format = D3DFMT_V8U8;
		const bool useDefault = (pManager->GetDeviceEx() != NULL);

		if (!pTexture || existingTexW != texw || existingTexH != texh) {
			hr = dev->CreateTexture(texw, texh, 1, 0, D3DFMT_V8U8, useDefault ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, ~pTexture, NULL);
			if (FAILED(hr))
				return std::pair<int, int>(-1, -1);
		}

		vdrefptr<IDirect3DTexture9> uploadtex;
		if (useDefault) {
			hr = dev->CreateTexture(texw, texh, 1, 0, D3DFMT_V8U8, D3DPOOL_SYSTEMMEM, ~uploadtex, NULL);
			if (FAILED(hr))
				return std::pair<int, int>(-1, -1);
		} else {
			uploadtex = pTexture;
		}

		// Fill the texture.
		D3DLOCKED_RECT lr;
		hr = uploadtex->LockRect(0, &lr, NULL, 0);
		VDASSERT(SUCCEEDED(hr));
		if (FAILED(hr)) {
			VDDEBUG_DX9DISP("VideoDisplay/DX9: Failed to load boxlinear texture.\n");
			return std::pair<int, int>(-1, -1);
		}

		double dudx = (double)srcw / (double)w;
		double dvdy = (double)srch / (double)h;
		double u = dudx * 0.5;
		double v = dvdy * 0.5;

		vdfastvector<sint8> hfilter(texw);
		for(int x = 0; x < texw; ++x) {
			double edgePos = floor(u + 0.5);
			double snappedPos = edgePos - fmax(-0.5f, fmin((edgePos - u) * facx, 0.5));
			hfilter[x] = (sint8)floor(0.5 + 127.0 * (snappedPos - u));
			u += dudx;
		}

		vdfastvector<sint8> vfilter(texh);
		for(int y = 0; y < texh; ++y) {
			double edgePos = floor(v + 0.5);
			double snappedPos = edgePos - fmax(-0.5f, fmin((edgePos - v) * facy, 0.5));
			vfilter[y] = (sint8)floor(0.5 + 127.0 * (snappedPos - v));
			v += dvdy;
		}

		sint8 *p0 = (sint8 *)lr.pBits;

		for(int y = 0; y < texh; ++y) {
			sint8 *p = p0;
			sint8 vc = vfilter[y];

			for(int x = 0; x < texw; ++x) {
				p[0] = hfilter[x];
				p[1] = vc;
				p += 2;
			}

			p0 += lr.Pitch;
		}

		VDVERIFY(SUCCEEDED(uploadtex->UnlockRect(0)));

		if (useDefault) {
			hr = pManager->GetDevice()->UpdateTexture(uploadtex, pTexture);
			if (FAILED(hr)) {
				pTexture.clear();
				return std::pair<int,int>(texw, texh);
			}
		}

		return std::pair<int,int>(texw, texh);
	}
}

bool VDVideoDisplayMinidriverDX9::InitBicubicPS2Filters(int w, int h) {
	// requires PS2.0 path
	if (mCubicMode != VDVideoDisplayDX9Manager::kCubicUsePS1_1Path && mCubicMode != VDVideoDisplayDX9Manager::kCubicUsePS1_4Path)
		return false;

	bool mode1_4 = (mCubicMode == VDVideoDisplayDX9Manager::kCubicUsePS1_4Path);

	// update horiz filter
	if (!mpD3DInterpFilterTextureH || mInterpFilterHSize != w) {
		int newtexw = GeneratePS2CubicTexture(mpManager, w, mSource.pixmap.w, mpD3DInterpFilterTextureH, mInterpFilterHSize, mode1_4);
		if (newtexw < 0)
			return false;

		mInterpFilterHSize = w;
		mInterpFilterHTexSize = newtexw;
	}

	// update vert filter
	if (!mpD3DInterpFilterTextureV || mInterpFilterVSize != h) {
		int newtexw = GeneratePS2CubicTexture(mpManager, h, mSource.pixmap.h, mpD3DInterpFilterTextureV, mInterpFilterVSize, mode1_4);
		if (newtexw < 0)
			return false;

		mInterpFilterVSize = h;
		mInterpFilterVTexSize = newtexw;
	}
	return true;
}

void VDVideoDisplayMinidriverDX9::ShutdownBicubicPS2Filters() {
	mpD3DInterpFilterTextureH = NULL;
	mpD3DInterpFilterTextureV = NULL;
	mInterpFilterHSize = 0;
	mInterpFilterHTexSize = 0;
	mInterpFilterVSize = 0;
	mInterpFilterVTexSize = 0;
}

bool VDVideoDisplayMinidriverDX9::InitBoxlinearPS11Filters(int w, int h, float facx, float facy) {
	// update horiz filter
	if (!mpD3DInterpFilterTexture
		|| mInterpFilterHSize != w
		|| mInterpFilterVSize != h
		|| mInterpFilterFactorX != facx
		|| mInterpFilterFactorY != facy)
	{
		auto newtexsize = GeneratePS11BoxlinearTexture(mpManager, w, h, mSource.pixmap.w, mSource.pixmap.h, facx, facy, mpD3DInterpFilterTexture, mInterpFilterHSize, mInterpFilterVSize);
		if (newtexsize.first < 0)
			return false;

		mInterpFilterHSize = w;
		mInterpFilterVSize = h;
		mInterpFilterTexSizeW = newtexsize.first;
		mInterpFilterTexSizeH = newtexsize.second;
		mInterpFilterFactorX = facx;
		mInterpFilterFactorY = facy;
	}

	return true;
}

void VDVideoDisplayMinidriverDX9::ShutdownBoxlinearPS11Filters() {
	mpD3DInterpFilterTexture.clear();
	mInterpFilterTexSizeW = 0;
	mInterpFilterTexSizeH = 0;
}

void VDVideoDisplayMinidriverDX9::Shutdown() {
	mpCustomPipeline.reset();

	mpUploadContext = NULL;

	if (mpFontRenderer) {
		mpFontRenderer->Shutdown();
		mpFontRenderer.clear();
	}

	if (mpRenderer) {
		mpRenderer->Shutdown();
		mpRenderer.clear();
	}

	ShutdownBicubic();
	ShutdownBicubicPS2Filters();
	ShutdownBoxlinearPS11Filters();

	mpSwapChain = NULL;
	mbSwapChainVsync = false;
	mbSwapChainVsyncEvent = false;
	mSwapChainW = 0;
	mSwapChainH = 0;

	mpVideoManager = NULL;

	if (mpManager) {
		if (mbFullScreenSet) {
			mbFullScreenSet = false;
			mpManager->AdjustFullScreen(false, 0, 0, 0);
		}

		VDDeinitDirect3D9(mpManager, this);
		mpManager = NULL;
	}

	mbCubicAttempted = false;
}

bool VDVideoDisplayMinidriverDX9::ModifySource(const VDVideoDisplaySourceInfo& info) {
	bool fastPath = false;

	if (mSource.pixmap.w == info.pixmap.w && mSource.pixmap.h == info.pixmap.h) {
		const int prevFormat = mSource.pixmap.format;
		const int nextFormat = info.pixmap.format;

		if (prevFormat != nextFormat) {
			// Check for compatible formats.
			switch(prevFormat) {
				case nsVDPixmap::kPixFormat_YUV420it_Planar:
					if (nextFormat == nsVDPixmap::kPixFormat_YUV420ib_Planar)
						fastPath = true;
					break;

				case nsVDPixmap::kPixFormat_YUV420it_Planar_FR:
					if (nextFormat == nsVDPixmap::kPixFormat_YUV420ib_Planar_FR)
						fastPath = true;
					break;

				case nsVDPixmap::kPixFormat_YUV420it_Planar_709:
					if (nextFormat == nsVDPixmap::kPixFormat_YUV420ib_Planar_709)
						fastPath = true;
					break;

				case nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR:
					if (nextFormat == nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR)
						fastPath = true;
					break;

				case nsVDPixmap::kPixFormat_YUV420ib_Planar:
					if (nextFormat == nsVDPixmap::kPixFormat_YUV420it_Planar)
						fastPath = true;
					break;

				case nsVDPixmap::kPixFormat_YUV420ib_Planar_FR:
					if (nextFormat == nsVDPixmap::kPixFormat_YUV420it_Planar_FR)
						fastPath = true;
					break;

				case nsVDPixmap::kPixFormat_YUV420ib_Planar_709:
					if (nextFormat == nsVDPixmap::kPixFormat_YUV420it_Planar_709)
						fastPath = true;
					break;

				case nsVDPixmap::kPixFormat_YUV420ib_Planar_709_FR:
					if (nextFormat == nsVDPixmap::kPixFormat_YUV420it_Planar_709_FR)
						fastPath = true;
					break;
			}
		} else {
			fastPath = true;
		}
	}

	if (!fastPath) {
		ShutdownBoxlinearPS11Filters();

		mpUploadContext.clear();

		mpUploadContext = new_nothrow VDVideoUploadContextD3D9;
		const uint32 bufferCount = mpCustomPipeline ? mpCustomPipeline->GetMaxPrevFrames() + 1 : 1;
		if (!mpUploadContext || !mpUploadContext->Init(mpManager->GetMonitor(), mbUseD3D9Ex, info.pixmap, info.bAllowConversion, mbHighPrecision && mpVideoManager->Is16FEnabled(), bufferCount)) {
			mpUploadContext.clear();
			return false;
		}
	}

	mSource = info;
	return true;
}

bool VDVideoDisplayMinidriverDX9::IsValid() {
	return mpD3DDevice != 0;
}

void VDVideoDisplayMinidriverDX9::SetFilterMode(FilterMode mode) {
	mPreferredFilter = mode;

	if (mode != kFilterBicubic && mode != kFilterAnySuitable) {
		ShutdownBicubicPS2Filters();

		if (mbCubicInitialized)
			ShutdownBicubic();
	}
}

void VDVideoDisplayMinidriverDX9::SetFullScreen(bool fs, uint32 w, uint32 h, uint32 refresh) {
	if (mbFullScreen != fs) {
		mbFullScreen = fs;
		mFullScreenWidth = w;
		mFullScreenHeight = h;
		mFullScreenRefreshRate = refresh;

		if (mpManager) {
			if (mbFullScreenSet != fs) {
				mbFullScreenSet = fs;
				mpManager->AdjustFullScreen(fs, w, h, refresh);
			}
		}
	}
}

bool VDVideoDisplayMinidriverDX9::Tick(int id) {
	return true;
}

void VDVideoDisplayMinidriverDX9::Poll() {
	if (mbSwapChainPresentPending) {
		RECT rClient = { mClientRect.left, mClientRect.top, mClientRect.right, mClientRect.bottom };
		if (!UpdateScreen(rClient, kModeVSync, true))
			mSource.mpCB->RequestNextFrame();
	}
}

bool VDVideoDisplayMinidriverDX9::Resize(int w, int h) {
	mbSwapChainImageValid = false;
	return VDVideoDisplayMinidriver::Resize(w, h);
}

bool VDVideoDisplayMinidriverDX9::Invalidate() {
	mbSwapChainImageValid = false;
	return false;
}

bool VDVideoDisplayMinidriverDX9::Update(UpdateMode mode) {
	if (!mpManager->CheckDevice())
		return false;

	int fieldMask = 3;

	switch(mode & kModeFieldMask) {
		case kModeEvenField:
			fieldMask = 1;
			break;

		case kModeOddField:
			fieldMask = 2;
			break;

		case kModeAllFields:
			break;
	}

	if (!mpUploadContext->Update(mSource.pixmap, fieldMask))
		return false;

	mbSwapChainImageValid = false;

	return true;
}

void VDVideoDisplayMinidriverDX9::Refresh(UpdateMode mode) {
	if (mClientRect.right > 0 && mClientRect.bottom > 0) {
		RECT rClient = { mClientRect.left, mClientRect.top, mClientRect.right, mClientRect.bottom };

		if (!Paint(NULL, rClient, mode)) {
			VDDEBUG_DX9DISP("Refresh() failed in Paint()\n");
		}
	}
}

bool VDVideoDisplayMinidriverDX9::Paint(HDC, const RECT& rClient, UpdateMode updateMode) {
	return (mbSwapChainImageValid || UpdateBackbuffer(rClient, updateMode)) && UpdateScreen(rClient, updateMode, 0 != (updateMode & kModeVSync));
}

void VDVideoDisplayMinidriverDX9::SetLogicalPalette(const uint8 *pLogicalPalette) {
}

bool VDVideoDisplayMinidriverDX9::UpdateBackbuffer(const RECT& rClient0, UpdateMode updateMode) {
	using namespace nsVDDisplay;

	const D3DDISPLAYMODE& displayMode = mpManager->GetDisplayMode();
	int rtw = displayMode.Width;
	int rth = displayMode.Height;
	RECT rClient = rClient0;
	if (mbFullScreen) {
		rClient.right = rtw;
		rClient.bottom = rth;
	}

	RECT rClippedClient={0,0,std::min<int>(rClient.right, rtw), std::min<int>(rClient.bottom, rth)};

	// Make sure the device is sane.
	if (!mpManager->CheckDevice())
		return false;

	// Check if we need to create or resize the swap chain.
	if (!mbFullScreen) {
		if (mpManager->GetDeviceEx()) {
			if (mSwapChainW != rClippedClient.right || mSwapChainH != rClippedClient.bottom) {
				mpSwapChain = NULL;
				mbSwapChainVsync = false;
				mbSwapChainVsyncEvent = false;
				mSwapChainW = 0;
				mSwapChainH = 0;
				mbSwapChainImageValid = false;
			}

			if (!mpSwapChain || mSwapChainW != rClippedClient.right || mSwapChainH != rClippedClient.bottom) {
				int scw = std::min<int>(rClippedClient.right, rtw);
				int sch = std::min<int>(rClippedClient.bottom, rth);

				if (!mpManager->CreateSwapChain(mhwnd, scw, sch, mbClipToMonitor, ~mpSwapChain))
					return false;

				mSwapChainW = scw;
				mSwapChainH = sch;
			}
		} else {
			if (mSwapChainW >= rClippedClient.right + 128 || mSwapChainH >= rClippedClient.bottom + 128) {
				mpSwapChain = NULL;
				mbSwapChainVsync = false;
				mbSwapChainVsyncEvent = false;
				mSwapChainW = 0;
				mSwapChainH = 0;
				mbSwapChainImageValid = false;
			}

			if (!mpSwapChain || mSwapChainW < rClippedClient.right || mSwapChainH < rClippedClient.bottom) {
				int scw = std::min<int>((rClippedClient.right + 127) & ~127, rtw);
				int sch = std::min<int>((rClippedClient.bottom + 127) & ~127, rth);

				if (!mpManager->CreateSwapChain(mhwnd, scw, sch, mbClipToMonitor, ~mpSwapChain))
					return false;

				mSwapChainW = scw;
				mSwapChainH = sch;
			}
		}
	}

	VDDisplayCompositeInfo compInfo = {};

	if (mpCompositor) {
		compInfo.mWidth = rClient.right;
		compInfo.mHeight = rClient.bottom;

		mpCompositor->PreComposite(compInfo);
	}

	// Do we need to switch bicubic modes?
	FilterMode mode = mPreferredFilter;

	if (mode == kFilterAnySuitable)
		mode = kFilterBicubic;

	// bicubic modes cannot clip
	if (rClient.right != rClippedClient.right || rClient.bottom != rClippedClient.bottom)
		mode = kFilterBilinear;

	if (mode != kFilterBicubic && mbCubicInitialized)
		ShutdownBicubic();
	else if (mode == kFilterBicubic && !mbCubicInitialized && !mbCubicAttempted)
		InitBicubic();

	if (mpD3DInterpFilterTexture && mPixelSharpnessX <= 1 && mPixelSharpnessY <= 1)
		ShutdownBoxlinearPS11Filters();

	static const D3DMATRIX ident={
		{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}
	};

	D3D_DO(SetTransform(D3DTS_WORLD, &ident));
	D3D_DO(SetTransform(D3DTS_VIEW, &ident));
	D3D_DO(SetTransform(D3DTS_PROJECTION, &ident));

	D3D_DO(SetStreamSource(0, mpManager->GetVertexBuffer(), 0, sizeof(Vertex)));
	D3D_DO(SetIndices(mpManager->GetIndexBuffer()));
	D3D_DO(SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2));
	D3D_DO(SetRenderState(D3DRS_LIGHTING, FALSE));
	D3D_DO(SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
	D3D_DO(SetRenderState(D3DRS_ZENABLE, FALSE));
	D3D_DO(SetRenderState(D3DRS_ALPHATESTENABLE, FALSE));
	D3D_DO(SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
	D3D_DO(SetRenderState(D3DRS_STENCILENABLE, FALSE));
	D3D_DO(SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0));
	D3D_DO(SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1));
	D3D_DO(SetTextureStageState(2, D3DTSS_TEXCOORDINDEX, 2));

	vdrefptr<IDirect3DSurface9> pRTMain;

	mpManager->SetSwapChainActive(NULL);

	if (mpSwapChain) {
		IDirect3DSwapChain9 *sc = mpSwapChain->GetD3DSwapChain();
		HRESULT hr = sc->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, ~pRTMain);
		if (FAILED(hr))
			return false;
	} else {
		mpManager->SetSwapChainActive(NULL);
		mpD3DDevice->GetRenderTarget(0, ~pRTMain);
	}

	mbSwapChainImageValid = false;

	bool bSuccess = false;

	const D3DDISPLAYMODE& dispMode = mpManager->GetDisplayMode();
	bool needBlit = true;
	if (mColorOverride) {
		mpManager->SetSwapChainActive(mpSwapChain);

		D3DRECT rClear;
		rClear.x1 = rClient.left;
		rClear.y1 = rClient.top;
		rClear.x2 = rClient.right;
		rClear.y2 = rClient.bottom;
		HRESULT hr = mpD3DDevice->Clear(1, &rClear, D3DCLEAR_TARGET, mColorOverride, 0.0f, 0);

		bSuccess = SUCCEEDED(hr);
		needBlit = false;
	} 

	IDirect3DTexture9 *srcTex = mpUploadContext->GetD3DTexture();
	uint32 srcWidth = mSource.pixmap.w;
	uint32 srcHeight = mSource.pixmap.h;

	if (needBlit && mpCustomPipeline) {
		needBlit = false;

		try {
			vdsize32 viewportSize(rClient.right, rClient.bottom);

			if (mbDestRectEnabled)
				viewportSize = mDestRect.size();

			mpCustomPipeline->Run(mpUploadContext->GetD3DTextures(), vdsize32(srcWidth, srcHeight), vdsize32(mSource.pixmap.w, mSource.pixmap.h), viewportSize);

			if (mpCustomPipeline->ContainsFinalBlit()) {
				mpManager->SetSwapChainActive(mpSwapChain);

				vdrefptr<IDirect3DSurface9> rt;
				HRESULT hr = mpD3DDevice->GetRenderTarget(0, ~rt);
				if (SUCCEEDED(hr)) {
					D3DSURFACE_DESC desc;
					hr = rt->GetDesc(&desc);
					if (SUCCEEDED(hr)) {
						uint32 clippedWidth = std::min<uint32>(srcWidth, rClient0.right);
						uint32 clippedHeight = std::min<uint32>(srcHeight, rClient0.bottom);

						D3DVIEWPORT9 vp = {};
						vp.X = 0;
						vp.Y = 0;
						vp.Width = (DWORD)rClippedClient.right;
						vp.Height = (DWORD)rClippedClient.bottom;
						vp.MinZ = 0;
						vp.MaxZ = 1;

						hr = mpD3DDevice->SetViewport(&vp);
						if (SUCCEEDED(hr)) {

							D3DRECT r;
							r.x1 = 0;
							r.y1 = 0;
							r.x2 = rClippedClient.right;
							r.y2 = rClippedClient.bottom;

							hr = mpD3DDevice->Clear(1, &r, D3DCLEAR_TARGET, mBackgroundColor, 0.0f, 0);
							if (FAILED(hr))
								return false;

							const float invw = 1.0f / (float)rClippedClient.right;
							const float invh = 1.0f / (float)rClippedClient.bottom;

							vdrect32f dstRect(0.0f, 0.0f, (float)rClient0.right, (float)rClient0.bottom);

							if (mbDestRectEnabled)
								dstRect = vdrect32f((float)mDestRect.left, (float)mDestRect.top, (float)mDestRect.right, (float)mDestRect.bottom);

							mpCustomPipeline->RunFinal(
								dstRect, viewportSize
							);

							bSuccess = true;
						}
					}
				}
			} else {
				needBlit = true;

				srcTex = mpCustomPipeline->GetFinalOutput(srcWidth, srcHeight);
			}
		} catch(const MyError&) {
		}
	}
	
	if (needBlit) {
		bSuccess = false;
		needBlit = false;

		D3DRECT rects[4];
		D3DRECT *nextRect = rects;
		RECT rDest = rClippedClient;

		if (mbDestRectEnabled) {
			// clip client rect to dest rect
			if (rDest.left < mDestRect.left)
				rDest.left = mDestRect.left;

			if (rDest.top < mDestRect.top)
				rDest.top = mDestRect.top;

			if (rDest.right > mDestRect.right)
				rDest.right = mDestRect.right;

			if (rDest.bottom > mDestRect.bottom)
				rDest.bottom = mDestRect.bottom;

			// fix rect in case dest rect lies entirely outside of client rect
			if (rDest.left > rClippedClient.right)
				rDest.left = rClippedClient.right;

			if (rDest.top > rClippedClient.bottom)
				rDest.top = rClippedClient.bottom;

			if (rDest.right < rDest.left)
				rDest.right = rDest.left;

			if (rDest.bottom < rDest.top)
				rDest.bottom = rDest.top;
		}

		if (rDest.right <= rDest.left || rDest.bottom <= rDest.top) {
			mpManager->SetSwapChainActive(mpSwapChain);

			D3DRECT r;
			r.x1 = rClippedClient.left;
			r.y1 = rClippedClient.top;
			r.x2 = rClippedClient.right;
			r.y2 = rClippedClient.bottom;

			HRESULT hr = mpD3DDevice->Clear(1, &r, D3DCLEAR_TARGET, mBackgroundColor, 0.0f, 0);
			if (FAILED(hr))
				return false;
		} else {
			if (rDest.top > rClippedClient.top) {
				nextRect->x1 = rClippedClient.left;
				nextRect->y1 = rClippedClient.top;
				nextRect->x2 = rClippedClient.right;
				nextRect->y2 = rDest.top;
				++nextRect;
			}

			if (rDest.left > rClippedClient.left) {
				nextRect->x1 = rClippedClient.left;
				nextRect->y1 = rDest.top;
				nextRect->x2 = rDest.left;
				nextRect->y2 = rDest.bottom;
				++nextRect;
			}

			if (rDest.right < rClippedClient.right) {
				nextRect->x1 = rDest.right;
				nextRect->y1 = rDest.top;
				nextRect->x2 = rClippedClient.right;
				nextRect->y2 = rDest.bottom;
				++nextRect;
			}

			if (rDest.bottom < rClippedClient.bottom) {
				nextRect->x1 = rClippedClient.left;
				nextRect->y1 = rDest.bottom;
				nextRect->x2 = rClippedClient.right;
				nextRect->y2 = rClippedClient.bottom;
				++nextRect;
			}

			HRESULT hr;
			if (nextRect > rects) {
				mpManager->SetSwapChainActive(mpSwapChain);

				hr = mpD3DDevice->Clear(nextRect - rects, rects, D3DCLEAR_TARGET, mBackgroundColor, 0.0f, 0);
				if (FAILED(hr))
					return false;
			}

			VDVideoDisplayDX9Manager::EffectContext ctx;

			ctx.mpSourceTexture1 = srcTex;
			ctx.mpSourceTexture2 = NULL;
			ctx.mpSourceTexture3 = NULL;
			ctx.mpSourceTexture4 = NULL;
			ctx.mpSourceTexture5 = NULL;
			ctx.mpInterpFilterH = NULL;
			ctx.mpInterpFilterV = NULL;
			ctx.mpInterpFilter = NULL;
			ctx.mSourceW = srcWidth;
			ctx.mSourceH = srcHeight;
			ctx.mSourceArea.set(0, 0, (float)ctx.mSourceW, (float)ctx.mSourceH);

			D3DSURFACE_DESC desc;

			hr = ctx.mpSourceTexture1->GetLevelDesc(0, &desc);
			if (FAILED(hr))
				return false;

			ctx.mSourceTexW = desc.Width;
			ctx.mSourceTexH = desc.Height;
			ctx.mInterpHTexW = 1;
			ctx.mInterpHTexH = 1;
			ctx.mInterpVTexW = 1;
			ctx.mInterpVTexH = 1;
			ctx.mInterpTexW = 1;
			ctx.mInterpTexH = 1;
			ctx.mViewportX = rDest.left;
			ctx.mViewportY = rDest.top;
			ctx.mViewportW = rDest.right - rDest.left;
			ctx.mViewportH = rDest.bottom - rDest.top;
			ctx.mOutputX = 0;
			ctx.mOutputY = 0;
			ctx.mOutputW = rDest.right - rDest.left;
			ctx.mOutputH = rDest.bottom - rDest.top;
			ctx.mFieldOffset = 0.0f;
			ctx.mDefaultUVScaleCorrectionX = 1.0f;
			ctx.mDefaultUVScaleCorrectionY = 1.0f;
			ctx.mChromaScaleU = 1.0f;
			ctx.mChromaScaleV = 1.0f;
			ctx.mChromaOffsetU = 0.0f;
			ctx.mChromaOffsetV = 0.0f;
			ctx.mPixelSharpnessX = mPixelSharpnessX;
			ctx.mPixelSharpnessY = mPixelSharpnessY;
			ctx.mbHighPrecision = mbHighPrecision;

			if (updateMode & kModeBobEven)
				ctx.mFieldOffset = -1.0f;
			else if (updateMode & kModeBobOdd)
				ctx.mFieldOffset = +1.0f;

			if (mbCubicInitialized &&
				(uint32)rClient.right <= dispMode.Width &&
				(uint32)rClient.bottom <= dispMode.Height &&
				(uint32)mSource.pixmap.w <= dispMode.Width &&
				(uint32)mSource.pixmap.h <= dispMode.Height
				)
			{
				int cubicMode = mCubicMode;

				if (cubicMode == VDVideoDisplayDX9Manager::kCubicUsePS1_1Path || cubicMode == VDVideoDisplayDX9Manager::kCubicUsePS1_4Path) {
					if (!InitBicubicPS2Filters(ctx.mViewportW, ctx.mViewportH))
						cubicMode = VDVideoDisplayDX9Manager::kCubicNotPossible;
					else {
						ctx.mpInterpFilterH = mpD3DInterpFilterTextureH;
						ctx.mpInterpFilterV = mpD3DInterpFilterTextureV;
						ctx.mInterpHTexW = mInterpFilterHTexSize;
						ctx.mInterpHTexH = 1;
						ctx.mInterpVTexW = mInterpFilterVTexSize;
						ctx.mInterpVTexH = 1;
					}
				}

				if (mbHighPrecision && mpVideoManager->Is16FEnabled() && cubicMode == mCubicMode) {
					bSuccess = mpVideoManager->RunEffect(ctx, g_technique_bicubic_2_0, pRTMain);
				} else {
					switch(cubicMode) {
					case VDVideoDisplayDX9Manager::kCubicUsePS1_4Path:
						bSuccess = mpVideoManager->RunEffect(ctx, g_technique_bicubic1_4, pRTMain);
						break;
					case VDVideoDisplayDX9Manager::kCubicUsePS1_1Path:
						bSuccess = mpVideoManager->RunEffect(ctx, g_technique_bicubic1_1, pRTMain);
						break;
					default:
						bSuccess = mpVideoManager->RunEffect(ctx, g_technique_bilinear_ff, pRTMain);
						break;
					}
				}
			} else {
				if (mbHighPrecision && mpVideoManager->Is16FEnabled()) {
					if (mPreferredFilter == kFilterPoint)
						bSuccess = mpVideoManager->RunEffect(ctx, g_technique_point_2_0, pRTMain);
					else
						bSuccess = mpVideoManager->RunEffect(ctx, g_technique_bilinear_2_0, pRTMain);
				} else {
					if (mPreferredFilter == kFilterPoint)
						bSuccess = mpVideoManager->RunEffect(ctx, g_technique_point_ff, pRTMain);
					else if ((mPixelSharpnessX > 1 || mPixelSharpnessY > 1) && mpVideoManager->IsPS20Enabled())
						bSuccess = mpVideoManager->RunEffect(ctx, g_technique_boxlinear_2_0, pRTMain);
					else if ((mPixelSharpnessX > 1 || mPixelSharpnessY > 1) && mbBoxlinearCapable11) {
						if (!InitBoxlinearPS11Filters(ctx.mViewportW, ctx.mViewportH, mPixelSharpnessX, mPixelSharpnessY))
							mbBoxlinearCapable11 = false;
						else {
							ctx.mpInterpFilter = mpD3DInterpFilterTexture;
							ctx.mInterpTexW = mInterpFilterTexSizeW;
							ctx.mInterpTexH = mInterpFilterTexSizeH;
						}

						if (ctx.mpInterpFilter)
							bSuccess = mpVideoManager->RunEffect(ctx, g_technique_boxlinear_1_1, pRTMain);
						else
							bSuccess = mpVideoManager->RunEffect(ctx, g_technique_bilinear_ff, pRTMain);
					} else
						bSuccess = mpVideoManager->RunEffect(ctx, g_technique_bilinear_ff, pRTMain);
				}
			}
		}
	}

	pRTMain = NULL;

	if (mpCompositor) {
		D3DVIEWPORT9 vp;

		vp.X = 0;
		vp.Y = 0;
		vp.Width = rClippedClient.right;
		vp.Height = rClippedClient.bottom;
		vp.MinZ = 0;
		vp.MaxZ = 1;
		mpD3DDevice->SetViewport(&vp);

		if (mpRenderer->Begin()) {
			mpCompositor->Composite(*mpRenderer, compInfo);
			mpRenderer->End();
		}
	}

	if (mbDisplayDebugInfo || !mErrorString.empty() || (mpCustomPipeline && mpCustomPipeline->HasTimingInfo())) {
		D3DVIEWPORT9 vp;

		vp.X = 0;
		vp.Y = 0;
		vp.Width = rClippedClient.right;
		vp.Height = rClippedClient.bottom;
		vp.MinZ = 0;
		vp.MaxZ = 1;
		mpD3DDevice->SetViewport(&vp);
		DrawDebugInfo(mode, rClient);
	}

	if (bSuccess && !mpManager->EndScene())
		bSuccess = false;

	if (updateMode & kModeVSync)
		mpManager->Flush();

	mpManager->SetSwapChainActive(NULL);

	if (!bSuccess) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Render failed -- applying boot to the head.\n");

		if (!mpManager->Reset())
			return false;

	} else {
		mbSwapChainImageValid = true;
		mbSwapChainPresentPending = true;
		mbSwapChainPresentPolling = false;
	}

	return bSuccess;
}

bool VDVideoDisplayMinidriverDX9::UpdateScreen(const RECT& rClient, UpdateMode updateMode, bool polling) {
	if (!mbSwapChainImageValid)
		return false;

	HRESULT hr;
	if (mbFullScreen) {
		hr = mpManager->PresentFullScreen(!polling && !(updateMode & kModeDoNotWait));

		if (!polling || !mbSwapChainPresentPolling) {
			mPresentHistory.mPresentStartTime = VDGetPreciseTick();
		}

		if (hr == S_OK) {
			mPresentHistory.mAveragePresentTime += ((VDGetPreciseTick() - mPresentHistory.mPresentStartTime)*VDGetPreciseSecondsPerTick() - mPresentHistory.mAveragePresentTime) * 0.01f;
		}

		if (hr == S_FALSE && polling) {
			++mPresentHistory.mPollCount;
			mPresentHistory.mbPresentPending = true;
		} else {
			mPresentHistory.mbPresentPending = false;
		}

	} else {
		bool vsync = (updateMode & kModeVSync) != 0;

		if (mbSwapChainVsync != vsync) {
			mbSwapChainVsync = vsync;

			if (vsync) {
				const auto refreshRate = mpManager->GetDisplayMode().RefreshRate;
				uint32 delay = refreshRate == 0 ? 5 : refreshRate > 50 ? 500 / refreshRate : 10;

				auto *cb = mSource.mpCB;
				mbSwapChainVsyncEvent = mpSwapChain->SetVsyncCallback(mpManager->GetMonitor(), [this, cb]{ OnVsyncEvent(cb); }, delay);
			} else {
				mpSwapChain->SetVsyncCallback(nullptr, {}, 0);
				mbSwapChainVsyncEvent = false;
			}
		}

		if (mbSwapChainVsyncEvent) {
			if (!mbSwapChainPresentPolling) {
				mpSwapChain->RequestVsyncCallback();
				mbSwapChainVsyncEventPending = true;
				mbSwapChainPresentPolling = true;
				mpManager->Flush();
				return true;
			} else if (mbSwapChainVsyncEventPending) {
				return true;
			} else {
				hr = mpManager->PresentSwapChain(mpSwapChain, &rClient, mhwnd, false, true, false, mSyncDelta, mPresentHistory);
			}
		} else {
			hr = mpManager->PresentSwapChain(mpSwapChain, &rClient, mhwnd, vsync, !polling || !mbSwapChainPresentPolling, polling || (updateMode & kModeDoNotWait) != 0, mSyncDelta, mPresentHistory);
		}
	}

	if (hr == S_FALSE && polling) {
		mbSwapChainPresentPolling = true;
		return true;
	}

	// Workaround for Windows Vista DWM composition chain not updating.
	if (!mbFullScreen && mbFirstPresent) {
		SetWindowPos(mhwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED);
		mbFirstPresent = false;
	}

	mbSwapChainPresentPending = false;
	mbSwapChainPresentPolling = false;
	VDASSERT(!mPresentHistory.mbPresentPending);

	if (FAILED(hr)) {
		VDDEBUG_DX9DISP("VideoDisplay/DX9: Render failed in UpdateScreen() -- applying boot to the head.\n");

		// TODO: Need to free all DEFAULT textures before proceeding

		if (!mpManager->Reset())
			return false;
	}

	mSource.mpCB->RequestNextFrame();
	return true;
}

void VDVideoDisplayMinidriverDX9::DrawDebugInfo(FilterMode mode, const RECT& rClient) {
	if (!mpFontRenderer) {
		// init font renderer
		if (!VDCreateFontRendererD3D9(~mpFontRenderer))
			return;

		mpFontRenderer->Init(mpManager);		// we explicitly allow this to fail
	}

	if (!mpManager->BeginScene())
		return;
	
	if (!mpFontRenderer->Begin())
		return;

	if (mbDisplayDebugInfo) {
		const char *modestr = "point";

		switch(mode) {
			case kFilterBilinear:
				modestr = "bilinear";
				break;
			case kFilterBicubic:
				modestr = "bicubic";
				break;
		}

		GetFormatString(mSource, mFormatString);
		mDebugString.sprintf("Direct3D9%s minidriver - %s (%s%s)  Average present time: %6.2fms"
			, mpManager->GetDeviceEx() ? "Ex" : ""
			, mFormatString.c_str()
			, modestr
			, mbHighPrecision && mpVideoManager->Is16FEnabled() ? "-16F" : ""
			, mPresentHistory.mAveragePresentTime * 1000.0);

		mpFontRenderer->DrawTextLine(10, rClient.bottom - 40, 0xFFFFFF00, 0, mDebugString.c_str());

		mDebugString.sprintf("Target scanline: %7.2f  Average bracket [%7.2f,%7.2f]  Last bracket [%4d,%4d]  Poll count %5d"
				, mPresentHistory.mScanlineTarget
				, mPresentHistory.mAverageStartScanline
				, mPresentHistory.mAverageEndScanline
				, mPresentHistory.mLastBracketY1
				, mPresentHistory.mLastBracketY2
				, mPresentHistory.mPollCount);
		mPresentHistory.mPollCount = 0;
		mpFontRenderer->DrawTextLine(10, rClient.bottom - 20, 0xFFFFFF00, 0, mDebugString.c_str());
	}

	if (!mErrorString.empty())
		mpFontRenderer->DrawTextLine(10, rClient.bottom - 60, 0xFFFF4040, 0, mErrorString.c_str());

	if (mpCustomPipeline && mpCustomPipeline->HasTimingInfo()) {
		uint32 numTimings = 0;
		const auto *passInfos = mpCustomPipeline->GetPassTimings(numTimings);

		if (passInfos) {
			for(uint32 i=0; i<numTimings; ++i) {
				if (i + 1 == numTimings)
					mDebugString.sprintf("Total: %7.2fms %ux%u", passInfos[i].mTiming * 1000.0f, mbDestRectEnabled ? mDestRect.width() : rClient.right, mbDestRectEnabled ? mDestRect.height() : rClient.bottom);
				else
					mDebugString.sprintf("Pass #%-2u: %7.2fms %ux%u%s", i + 1, passInfos[i].mTiming * 1000.0f, passInfos[i].mOutputWidth, passInfos[i].mOutputHeight, passInfos[i].mbOutputFloat ? " float" : "");
				mpFontRenderer->DrawTextLine(10, 10 + 14*i, 0xFFFFFF00, 0, mDebugString.c_str());
			}
		}
	}

	mpFontRenderer->End();
}
