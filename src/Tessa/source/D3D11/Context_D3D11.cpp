//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#define D3D11_NO_HELPERS
#define INITGUID
#include <guiddef.h>
#include <windows.h>
#include <synchapi.h>
#include <dwmapi.h>

#include <dxgi1_3.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>

#include <d3d11.h>
#include <d3d11_1.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/strutil.h>
#include <vd2/system/time.h>
#include <vd2/system/w32assist.h>
#include <vd2/Tessa/Config.h>
#include <vd2/Tessa/Context.h>
#include <vd2/Tessa/Format.h>
#include <vd2/Tessa/Options.h>
#include "D3D11/Context_D3D11.h"
#include "D3D11/FenceManager_D3D11.h"
#include "Program.h"

namespace {
	DXGI_FORMAT GetSurfaceFormatD3D11(VDTFormat format) {
		switch(format) {
			case kVDTF_B8G8R8A8:
				return DXGI_FORMAT_B8G8R8A8_UNORM;

			case kVDTF_B8G8R8A8_sRGB:
				return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

			case kVDTF_R8G8B8A8:
				return DXGI_FORMAT_R8G8B8A8_UNORM;

			case kVDTF_U8V8:
				return DXGI_FORMAT_R8G8_SNORM;

			case kVDTF_R8G8:
				return DXGI_FORMAT_R8G8_UNORM;

			case kVDTF_R8:
				return DXGI_FORMAT_R8_UNORM;

			case kVDTF_R16G16B16A16F:
				return DXGI_FORMAT_R16G16B16A16_FLOAT;

			default:
				return DXGI_FORMAT_UNKNOWN;
		}
	}

	VDTFormat GetSurfaceFormatFromD3D11(DXGI_FORMAT format) {
		switch(format) {
			case DXGI_FORMAT_B8G8R8A8_UNORM:
				return kVDTF_B8G8R8A8;

			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				return kVDTF_B8G8R8A8_sRGB;

			case DXGI_FORMAT_R8G8B8A8_UNORM:
				return kVDTF_R8G8B8A8;

			case DXGI_FORMAT_R8G8_SNORM:
				return kVDTF_U8V8;

			case DXGI_FORMAT_R8G8_UNORM:
				return kVDTF_R8G8;

			case DXGI_FORMAT_R8_UNORM:
				return kVDTF_R8;

			case DXGI_FORMAT_R16G16B16A16_FLOAT:
				return kVDTF_R16G16B16A16F;

			default:
				return kVDTF_Unknown;
		}
	}
}

///////////////////////////////////////////////////////////////////////////

class VDD3D11Holder : public vdrefcounted<IVDRefUnknown> {
public:
	VDD3D11Holder();
	~VDD3D11Holder();

	void *AsInterface(uint32 iid);

	typedef HRESULT (APIENTRY *CreateDeviceFn)(IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels, UINT SDKVersion, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel, ID3D11DeviceContext **ppImmediateContext);
	typedef HRESULT (APIENTRY *CreateDXGIFactoryFn)(REFIID riid, void **ppFactory);
	typedef HRESULT (APIENTRY *CreateDXGIFactory2Fn)(UINT flags, REFIID riid, void **ppFactory);

	CreateDeviceFn GetCreateDeviceFn() const { return mpCreateDeviceFn; }
	CreateDXGIFactoryFn GetCreateDXGIFactoryFn() const { return mpCreateDXGIFactoryFn; }
	CreateDXGIFactory2Fn GetCreateDXGIFactory2Fn() const { return mpCreateDXGIFactory2Fn; }

	HMODULE GetD3D11() const { return mhmodD3D11; }

	bool Init();
	void Shutdown();

protected:
	HMODULE mhmodDXGI;
	HMODULE mhmodD3D11;
	CreateDXGIFactoryFn mpCreateDXGIFactoryFn;
	CreateDXGIFactory2Fn mpCreateDXGIFactory2Fn;
	CreateDeviceFn mpCreateDeviceFn;
};

///////////////////////////////////////////////////////////////////////////////

VDD3D11Holder::VDD3D11Holder()
	: mhmodDXGI(NULL)
	, mhmodD3D11(NULL)
	, mpCreateDXGIFactoryFn(NULL)
	, mpCreateDXGIFactory2Fn(NULL)
	, mpCreateDeviceFn(NULL)
{
}

VDD3D11Holder::~VDD3D11Holder() {
	Shutdown();
}

void *VDD3D11Holder::AsInterface(uint32 iid) {
	return NULL;
}

bool VDD3D11Holder::Init() {
	if (!mhmodDXGI) {
		if (VDTGetLibraryOverridesEnabled())
			mhmodDXGI = VDLoadSystemLibraryWithAllowedOverrideW32("dxgi");
		else
			mhmodDXGI = VDLoadSystemLibraryW32("dxgi");

		if (!mhmodDXGI) {
			Shutdown();
			return false;
		}
	}

	if (!mpCreateDXGIFactoryFn) {
		mpCreateDXGIFactoryFn = (CreateDXGIFactoryFn)GetProcAddress(mhmodDXGI, "CreateDXGIFactory1");

		if (!mpCreateDXGIFactoryFn) {
			Shutdown();
			return false;
		}
	}
	
	if (!mpCreateDXGIFactory2Fn)
		mpCreateDXGIFactory2Fn = (CreateDXGIFactory2Fn)GetProcAddress(mhmodDXGI, "CreateDXGIFactory2");

	if (!mhmodD3D11) {
		if (VDTGetLibraryOverridesEnabled())
			mhmodD3D11 = VDLoadSystemLibraryWithAllowedOverrideW32("D3D11");
		else
			mhmodD3D11 = VDLoadSystemLibraryW32("D3D11");

		if (!mhmodD3D11) {
			Shutdown();
			return false;
		}
	}

	if (!mpCreateDeviceFn) {
		mpCreateDeviceFn = (CreateDeviceFn)GetProcAddress(mhmodD3D11, "D3D11CreateDevice");
		if (!mpCreateDeviceFn) {
			Shutdown();
			return false;
		}
	}

	return true;
}

void VDD3D11Holder::Shutdown() {
	mpCreateDeviceFn = NULL;
	mpCreateDXGIFactoryFn = nullptr;
	mpCreateDXGIFactory2Fn = nullptr;

	if (mhmodD3D11) {
		FreeLibrary(mhmodD3D11);
		mhmodD3D11 = NULL;
	}
}

///////////////////////////////////////////////////////////////////////////////

VDTResourceD3D11::VDTResourceD3D11() {
	mListNodePrev = NULL;
	mpParent = NULL;
}

VDTResourceD3D11::~VDTResourceD3D11() {
}

void VDTResourceD3D11::Shutdown() {
	if (mListNodePrev)
		mpParent->RemoveResource(this);
}

void VDTResourceManagerD3D11::AddResource(VDTResourceD3D11 *res) {
	VDASSERT(!res->mListNodePrev);

	mResources.push_back(res);
	res->mpParent = this;
}

void VDTResourceManagerD3D11::RemoveResource(VDTResourceD3D11 *res) {
	VDASSERT(res->mListNodePrev);

	mResources.erase(res);
	res->mListNodePrev = NULL;
}

void VDTResourceManagerD3D11::ShutdownAllResources() {
	while(!mResources.empty()) {
		VDTResourceD3D11 *res = mResources.back();
		mResources.pop_back();

		res->mListNodePrev = NULL;
		res->Shutdown();
	}
}

///////////////////////////////////////////////////////////////////////////////

VDTReadbackBufferD3D11::VDTReadbackBufferD3D11()
	: mpSurface(NULL)
{
}

VDTReadbackBufferD3D11::~VDTReadbackBufferD3D11() {
	Shutdown();
}

bool VDTReadbackBufferD3D11::Init(VDTContextD3D11 *parent, uint32 width, uint32 height, VDTFormat format) {
	ID3D11Device *dev = parent->GetDeviceD3D11();

	D3D11_TEXTURE2D_DESC desc;
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;

	HRESULT hr = dev->CreateTexture2D(&desc, NULL, &mpSurface);

	parent->AddResource(this);
	return SUCCEEDED(hr);
}

void VDTReadbackBufferD3D11::Shutdown() {
	if (mpSurface) {
		mpSurface->Release();
		mpSurface = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTReadbackBufferD3D11::Lock(VDTLockData2D& lockData) {
	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();

	D3D11_MAPPED_SUBRESOURCE info;
	HRESULT hr = devctx->Map(mpSurface, 0, D3D11_MAP_READ, 0, &info);

	if (FAILED(hr)) {
		lockData.mpData = NULL;
		lockData.mPitch = 0;
		return false;
	}

	lockData.mpData = info.pData;
	lockData.mPitch = info.RowPitch;
	return true;
}

void VDTReadbackBufferD3D11::Unlock() {
	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();

	devctx->Unmap(mpSurface, 0);
}

bool VDTReadbackBufferD3D11::Restore() {
	return true;
}

///////////////////////////////////////////////////////////////////////////////

VDTSurfaceD3D11::VDTSurfaceD3D11()
	: mpTexture(NULL)
	, mpTextureSys(NULL)
	, mpRTView(NULL)
{
}

VDTSurfaceD3D11::~VDTSurfaceD3D11() {
	Shutdown();
}

bool VDTSurfaceD3D11::Init(VDTContextD3D11 *parent, uint32 width, uint32 height, VDTFormat format, VDTUsage usage) {
	DXGI_FORMAT dxgiFormat = GetSurfaceFormatD3D11(format);

	if (!dxgiFormat)
		return false;

	ID3D11Device *dev = parent->GetDeviceD3D11();
	HRESULT hr;
	
	mDesc.mWidth = width;
	mDesc.mHeight = height;
	mDesc.mFormat = format;

	D3D11_TEXTURE2D_DESC desc;
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = dxgiFormat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	switch(usage) {
		case kVDTUsage_Default:
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			break;

		case kVDTUsage_Render:
			desc.BindFlags = D3D11_BIND_RENDER_TARGET;
			break;

		default:
			break;
	}

	hr = dev->CreateTexture2D(&desc, NULL, &mpTexture);
	if (FAILED(hr))
		return false;

	if (usage == kVDTUsage_Render) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvdesc = {};
		rtvdesc.Format = dxgiFormat;
		rtvdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvdesc.Texture2D.MipSlice = 0;

		hr = dev->CreateRenderTargetView(mpTexture, &rtvdesc, &mpRTView);
		if (FAILED(hr)) {
			Shutdown();
			return false;
		}

		DXGI_FORMAT dxgiNonSrgbFormat = dxgiFormat;

		// if this is an inherently sRGB based format, create a second render target view
		// for non-sRGB pass through writes
		if (dxgiNonSrgbFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
			dxgiNonSrgbFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		else if (dxgiNonSrgbFormat == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
			dxgiNonSrgbFormat = DXGI_FORMAT_B8G8R8X8_UNORM;
		else if (dxgiNonSrgbFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
			dxgiNonSrgbFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

		if (dxgiNonSrgbFormat != dxgiFormat)
		{
			rtvdesc.Format = dxgiNonSrgbFormat;

			hr = dev->CreateRenderTargetView(mpTexture, &rtvdesc, &mpRTViewNoSrgb);
			if (FAILED(hr)) {
				Shutdown();
				return false;
			}
		}
	}

	parent->AddResource(this);

	return SUCCEEDED(hr);
}

bool VDTSurfaceD3D11::Init(VDTContextD3D11 *parent, IVDTTexture *parentTexture, ID3D11Texture2D *tex, ID3D11Texture2D *texsys, uint32 mipLevel, bool rt, bool onlyMip, bool forceSRGB) {
	D3D11_TEXTURE2D_DESC desc = {};

	tex->GetDesc(&desc);

	mpParentTexture = parentTexture;

	mMipLevel = mipLevel;
	mbOnlyMip = onlyMip;

	mDesc.mWidth = desc.Width;
	mDesc.mHeight = desc.Height;
	mDesc.mFormat = GetSurfaceFormatFromD3D11(desc.Format);

	if (rt) {
		D3D11_RENDER_TARGET_VIEW_DESC rtvdesc = {};
		rtvdesc.Format = desc.Format;

		if (forceSRGB) {
			switch(rtvdesc.Format) {
				case DXGI_FORMAT_R8G8B8A8_UNORM:
					rtvdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
					break;

				case DXGI_FORMAT_B8G8R8A8_UNORM:
					rtvdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
					break;

				case DXGI_FORMAT_B8G8R8X8_UNORM:
					rtvdesc.Format = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
					break;
			}
		}

		rtvdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		rtvdesc.Texture2D.MipSlice = mipLevel;

		HRESULT hr = parent->GetDeviceD3D11()->CreateRenderTargetView(tex, &rtvdesc, &mpRTView);
		if (FAILED(hr))
			return false;
	} else if (texsys) {
		ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();

		D3D11_MAPPED_SUBRESOURCE info;
		HRESULT hr = devctx->Map(texsys, mMipLevel, D3D11_MAP_WRITE, 0, &info);
		if (FAILED(hr))
			return false;

		const uint32 bpr = VDTGetBytesPerBlockRow(mDesc.mFormat, mDesc.mWidth);
		const uint32 bh = VDTGetNumBlockRows(mDesc.mFormat, mDesc.mHeight);
		VDMemset8Rect(info.pData, info.RowPitch, 0, bpr, bh);

		devctx->Unmap(texsys, mMipLevel);
	}

	parent->AddResource(this);

	mpTexture = tex;
	mpTexture->AddRef();

	mpTextureSys = texsys;
	if (mpTextureSys)
		mpTextureSys->AddRef();
	return true;
}

void VDTSurfaceD3D11::Shutdown() {
	if (mpParent)
		static_cast<VDTContextD3D11 *>(mpParent)->UnsetRenderTarget(this);

	vdsaferelease <<= mpRTView, mpRTViewNoSrgb, mpTextureSys, mpTexture;

	VDTResourceD3D11::Shutdown();
}

bool VDTSurfaceD3D11::Restore() {
	return true;
}

bool VDTSurfaceD3D11::Readback(IVDTReadbackBuffer *target) {
	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();
	VDTReadbackBufferD3D11 *targetD3D11 = static_cast<VDTReadbackBufferD3D11 *>(target);

	devctx->CopyResource(targetD3D11->mpSurface, mpTexture);
	return true;
}

void VDTSurfaceD3D11::Load(uint32 dx, uint32 dy, const VDTInitData2D& srcData, uint32 w, uint32 h) {
	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();

	D3D11_MAPPED_SUBRESOURCE info;
	HRESULT hr = devctx->Map(mpTextureSys, mMipLevel, D3D11_MAP_WRITE, 0, &info);
	if (FAILED(hr)) {
		parent->ProcessHRESULT(hr);
		return;
	}

	const uint32 bpr = VDTGetBytesPerBlockRow(mDesc.mFormat, w);
	const uint32 bh = VDTGetNumBlockRows(mDesc.mFormat, h);

	VDMemcpyRect((char *)info.pData + info.RowPitch * dy, info.RowPitch, srcData.mpData, srcData.mPitch, bpr, bh);

	devctx->Unmap(mpTextureSys, mMipLevel);

	if (mbOnlyMip && w == mDesc.mWidth && h == mDesc.mHeight)
		devctx->CopyResource(mpTexture, mpTextureSys);
	else
		devctx->CopySubresourceRegion(mpTexture, mMipLevel, 0, 0, 0, mpTextureSys, mMipLevel, NULL);
}

void VDTSurfaceD3D11::Copy(uint32 dx, uint32 dy, IVDTSurface *src0, uint32 sx, uint32 sy, uint32 w, uint32 h) {
	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();
	VDTSurfaceD3D11 *src = static_cast<VDTSurfaceD3D11 *>(src0);

	D3D11_BOX box;
	box.left = sx;
	box.right = sx + w;
	box.top = sy;
	box.bottom = sy+h;
	box.front = 0;
	box.back = 1;
	devctx->CopySubresourceRegion(mpTexture, mMipLevel, dx, dy, 0, src->mpTexture, src->mMipLevel, &box);
}

void VDTSurfaceD3D11::GetDesc(VDTSurfaceDesc& desc) {
	desc = mDesc;
}

bool VDTSurfaceD3D11::Lock(const vdrect32 *r, bool discard, VDTLockData2D& lockData) {
	if (!mpTextureSys)
		return false;

	RECT r2;
	const RECT *pr = NULL;
	if (r) {
		r2.left = r->left;
		r2.top = r->top;
		r2.right = r->right;
		r2.bottom = r->bottom;
		pr = &r2;
	}

	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = devctx->Map(mpTextureSys, mMipLevel, discard ? D3D11_MAP_WRITE : D3D11_MAP_READ_WRITE, 0, &mapped);
	if (FAILED(hr)) {
		VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
		parent->ProcessHRESULT(hr);
		return false;
	}

	lockData.mpData = mapped.pData;
	lockData.mPitch = mapped.RowPitch;

	return true;
}

void VDTSurfaceD3D11::Unlock() {
	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();

	devctx->Unmap(mpTextureSys, mMipLevel);
	devctx->CopySubresourceRegion(mpTexture, mMipLevel, 0, 0, 0, mpTextureSys, mMipLevel, NULL);
}

///////////////////////////////////////////////////////////////////////////////

VDTTexture2DD3D11::VDTTexture2DD3D11()
	: mpTexture(NULL)
	, mpTextureSys(NULL)
	, mpShaderResView(NULL)
{
}

VDTTexture2DD3D11::~VDTTexture2DD3D11() {
	Shutdown();
}

void *VDTTexture2DD3D11::AsInterface(uint32 id) {
	if (id == kTypeD3DShaderResView)
		return mpShaderResView;

	if (id == IVDTTexture2D::kTypeID)
		return static_cast<IVDTTexture2D *>(this);

	return NULL;
}

bool VDTTexture2DD3D11::Init(VDTContextD3D11 *parent, uint32 width, uint32 height, VDTFormat format, uint32 mipcount, VDTUsage usage, const VDTInitData2D *initData) {
	parent->AddResource(this);

	if (!mipcount) {
		uint32 mask = (width - 1) | (height - 1);

		mipcount = VDFindHighestSetBit(mask) + 1;
	}

	DXGI_FORMAT dxgiFormat = GetSurfaceFormatD3D11(format);
	if (!dxgiFormat)
		return false;

	mWidth = width;
	mHeight = height;
	mMipCount = mipcount;
	mUsage = usage;
	mFormat = format;

	if (mpTexture)
		return true;

	ID3D11Device *dev = parent->GetDeviceD3D11();
	if (!dev)
		return false;

	D3D11_TEXTURE2D_DESC desc;
    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.MipLevels = mMipCount;
    desc.ArraySize = 1;
	desc.Format = dxgiFormat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = initData ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
	desc.BindFlags = usage == kVDTUsage_Render ? D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE : D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

	vdfastvector<D3D11_SUBRESOURCE_DATA> subResData;

	if (initData) {
		subResData.resize(mipcount);

		for(uint32 i=0; i<mipcount; ++i) {
			D3D11_SUBRESOURCE_DATA& dst = subResData[i];
			const VDTInitData2D& src = initData[i];

			dst.pSysMem = src.mpData;
			dst.SysMemPitch = (UINT)src.mPitch;
			dst.SysMemSlicePitch = 0;
		}
	}

	HRESULT hr = dev->CreateTexture2D(&desc, initData ? subResData.data() : NULL, &mpTexture);
	
	if (FAILED(hr))
		return false;

	if (!initData && usage != kVDTUsage_Render) {
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		hr = dev->CreateTexture2D(&desc, NULL, &mpTextureSys);

		if (FAILED(hr))
			return false;
	}

	hr = dev->CreateShaderResourceView(mpTexture, NULL, &mpShaderResView);

	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	mMipmaps.reserve(mipcount);

	for(uint32 i=0; i<mipcount; ++i) {
		vdrefptr<VDTSurfaceD3D11> surf(new VDTSurfaceD3D11);

		surf->Init(parent, this, mpTexture, mpTextureSys, i, usage == kVDTUsage_Render, mipcount == 1, false);

		mMipmaps.push_back(surf.release());
	}

	return true;
}

bool VDTTexture2DD3D11::Init(VDTContextD3D11 *parent, ID3D11Texture2D *tex, ID3D11Texture2D *texsys, bool forceSRGB) {
	parent->AddResource(this);

	D3D11_TEXTURE2D_DESC desc;
	tex->GetDesc(&desc);

	mWidth = desc.Width;
	mHeight = desc.Height;
	mMipCount = desc.MipLevels;
	mUsage = (desc.BindFlags & D3D11_BIND_RENDER_TARGET) ? kVDTUsage_Render : kVDTUsage_Default;
	mFormat = GetSurfaceFormatFromD3D11(desc.Format);

	mpTexture = tex;
	tex->AddRef();

	mpTextureSys = texsys;
	if (texsys)
		texsys->AddRef();

	ID3D11Device *dev = parent->GetDeviceD3D11();
	if (!dev)
		return false;

	if (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
		HRESULT hr = dev->CreateShaderResourceView(mpTexture, NULL, &mpShaderResView);

		if (FAILED(hr)) {
			Shutdown();
			return false;
		}
	}

	mMipmaps.reserve(mMipCount);

	for(uint32 i=0; i<mMipCount; ++i) {
		vdrefptr<VDTSurfaceD3D11> surf(new VDTSurfaceD3D11);

		if (!surf->Init(parent, this, mpTexture, mpTextureSys, i, mUsage == kVDTUsage_Render, mMipCount == 1, forceSRGB)) {
			Shutdown();
			return false;
		}

		mMipmaps.push_back(surf);
		surf.release();
	}

	return true;
}

void VDTTexture2DD3D11::Shutdown() {
	while(!mMipmaps.empty()) {
		VDTSurfaceD3D11 *surf = mMipmaps.back();
		mMipmaps.pop_back();

		surf->Shutdown();
		surf->Release();
	}

	vdsaferelease <<= mpShaderResView, mpTextureSys;

	if (mpTexture) {
		if (mpParent)
			static_cast<VDTContextD3D11 *>(mpParent)->UnsetTexture(this);

		mpTexture->Release();
		mpTexture = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTTexture2DD3D11::Restore() {
	return true;
}

IVDTSurface *VDTTexture2DD3D11::GetLevelSurface(uint32 level) {
	return mMipmaps[level];
}

void VDTTexture2DD3D11::GetDesc(VDTTextureDesc& desc) {
	desc.mWidth = mWidth;
	desc.mHeight = mHeight;
	desc.mMipCount = mMipCount;
	desc.mFormat = mFormat;
}

void VDTTexture2DD3D11::Load(uint32 mip, uint32 x, uint32 y, const VDTInitData2D& srcData, uint32 w, uint32 h) {
	mMipmaps[mip]->Load(x, y, srcData, w, h);
}

bool VDTTexture2DD3D11::Lock(uint32 mip, const vdrect32 *r, bool discard, VDTLockData2D& lockData) {
	return mMipmaps[mip]->Lock(r, discard, lockData);
}

void VDTTexture2DD3D11::Unlock(uint32 mip) {
	mMipmaps[mip]->Unlock();
}

///////////////////////////////////////////////////////////////////////////////

VDTVertexBufferD3D11::VDTVertexBufferD3D11()
	: mpVB(NULL)
{
}

VDTVertexBufferD3D11::~VDTVertexBufferD3D11() {
	Shutdown();
}

bool VDTVertexBufferD3D11::Init(VDTContextD3D11 *parent, uint32 size, bool dynamic, const void *initData) {
	parent->AddResource(this);

	mbDynamic = dynamic;
	mByteSize = size;

	if (mpVB)
		return true;

	ID3D11Device *dev = static_cast<VDTContextD3D11 *>(mpParent)->GetDeviceD3D11();
	if (!dev)
		return false;

	D3D11_BUFFER_DESC desc {};
    desc.ByteWidth = mByteSize;
	desc.Usage = mbDynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	desc.CPUAccessFlags = mbDynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    desc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA srdata = {};
	srdata.pSysMem = initData;

	HRESULT hr = dev->CreateBuffer(&desc, initData ? &srdata : NULL, &mpVB);

	if (FAILED(hr))
		return false;

	return true;
}

void VDTVertexBufferD3D11::Shutdown() {
	if (mpVB) {
		if (mpParent)
			static_cast<VDTContextD3D11 *>(mpParent)->UnsetVertexBuffer(this);

		mpVB->Release();
		mpVB = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTVertexBufferD3D11::Restore() {
	return true;
}

bool VDTVertexBufferD3D11::Load(uint32 offset, uint32 size, const void *data) {
	if (!size)
		return true;

	if (offset > mByteSize || mByteSize - offset < size)
		return false;

	D3D11_MAP flags = D3D11_MAP_WRITE;

	if (mbDynamic) {
		if (offset)
			flags = D3D11_MAP_WRITE_NO_OVERWRITE;
		else
			flags = D3D11_MAP_WRITE_DISCARD;
	}

	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();

	D3D11_MAPPED_SUBRESOURCE info;
	HRESULT hr = devctx->Map(mpVB, 0, flags, 0, &info);
	if (FAILED(hr)) {
		static_cast<VDTContextD3D11 *>(mpParent)->ProcessHRESULT(hr);
		return false;
	}

	bool success = true;

	if (mbDynamic)
		success = VDMemcpyGuarded((char *)info.pData + offset, data, size);
	else
		memcpy((char *)info.pData + offset, data, size);

	devctx->Unmap(mpVB, 0);
	return success;
}

///////////////////////////////////////////////////////////////////////////////

VDTIndexBufferD3D11::VDTIndexBufferD3D11()
	: mpIB(NULL)
{
}

VDTIndexBufferD3D11::~VDTIndexBufferD3D11() {
	Shutdown();
}

bool VDTIndexBufferD3D11::Init(VDTContextD3D11 *parent, uint32 size, bool index32, bool dynamic, const void *initData) {
	mByteSize = index32 ? size << 2 : size << 1;
	mbDynamic = dynamic;
	mbIndex32 = index32;

	if (mpIB)
		return true;

	ID3D11Device *dev = static_cast<VDTContextD3D11 *>(parent)->GetDeviceD3D11();
	if (!dev)
		return false;

	D3D11_BUFFER_DESC desc {};
    desc.ByteWidth = mByteSize;
	desc.Usage = mbDynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	desc.CPUAccessFlags = mbDynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    desc.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA srd;

	srd.pSysMem = initData;
	srd.SysMemPitch = 0;
	srd.SysMemSlicePitch = 0;

	HRESULT hr = dev->CreateBuffer(&desc, initData ? &srd : NULL, &mpIB);

	if (FAILED(hr))
		return false;

	parent->AddResource(this);
	return true;
}

void VDTIndexBufferD3D11::Shutdown() {
	if (mpIB) {
		if (mpParent)
			static_cast<VDTContextD3D11 *>(mpParent)->UnsetIndexBuffer(this);

		mpIB->Release();
		mpIB = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTIndexBufferD3D11::Restore() {
	return true;
}

bool VDTIndexBufferD3D11::Load(uint32 offset, uint32 size, const void *data) {
	if (!size)
		return true;

	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	ID3D11DeviceContext *devctx = parent->GetDeviceContextD3D11();

	D3D11_MAP flags = D3D11_MAP_WRITE;

	if (mbDynamic) {
		if (offset)
			flags = D3D11_MAP_WRITE_NO_OVERWRITE;
		else
			flags = D3D11_MAP_WRITE_DISCARD;
	}

	D3D11_MAPPED_SUBRESOURCE info;
	HRESULT hr = devctx->Map(mpIB, 0, flags, 0, &info);
	if (FAILED(hr)) {
		static_cast<VDTContextD3D11 *>(mpParent)->ProcessHRESULT(hr);
		return false;
	}

	bool success = true;
	if (mbDynamic)
		success = VDMemcpyGuarded((char *)info.pData + offset, data, size);
	else
		memcpy((char *)info.pData + offset, data, size);

	devctx->Unmap(mpIB, 0);
	return success;
}

///////////////////////////////////////////////////////////////////////////////

VDTVertexFormatD3D11::VDTVertexFormatD3D11()
	: mpVF(NULL)
{
}

VDTVertexFormatD3D11::~VDTVertexFormatD3D11() {
	Shutdown();
}

bool VDTVertexFormatD3D11::Init(VDTContextD3D11 *parent, const VDTVertexElement *elements, uint32 count, VDTVertexProgramD3D11 *vp) {
	static const char *const kSemanticD3D11[]={
		"position",
		"blendweight",
		"blendindices",
		"normal",
		"texcoord",
		"tangent",
		"binormal",
		"color"
	};

	static const DXGI_FORMAT kFormatD3D11[]={
		DXGI_FORMAT_R32_FLOAT,
		DXGI_FORMAT_R32G32_FLOAT,
		DXGI_FORMAT_R32G32B32_FLOAT,
		DXGI_FORMAT_R32G32B32A32_FLOAT,
		DXGI_FORMAT_R8G8B8A8_UINT,
		DXGI_FORMAT_R8G8B8A8_UNORM
	};

	if (count >= 16) {
		VDASSERT(!"Too many vertex elements.");
		return false;
	}

	D3D11_INPUT_ELEMENT_DESC vxe[16];
	for(uint32 i=0; i<count; ++i) {
		D3D11_INPUT_ELEMENT_DESC& dst = vxe[i];
		const VDTVertexElement& src = elements[i];

		dst.SemanticName = kSemanticD3D11[src.mUsage];
		dst.SemanticIndex = src.mUsageIndex;
		dst.Format = kFormatD3D11[src.mType];
		dst.InputSlot = 0;
		dst.AlignedByteOffset = src.mOffset;
		dst.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		dst.InstanceDataStepRate = 0;
	}

	HRESULT hr = parent->GetDeviceD3D11()->CreateInputLayout(vxe, count, vp->mByteCode.data(), vp->mByteCode.size(), &mpVF);

	if (FAILED(hr))
		return false;

	parent->AddResource(this);
	return true;
}

void VDTVertexFormatD3D11::Shutdown() {
	if (mpVF) {
		if (mpParent)
			static_cast<VDTContextD3D11 *>(mpParent)->UnsetVertexFormat(this);

		mpVF->Release();
		mpVF = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTVertexFormatD3D11::Restore() {
	return true;
}

///////////////////////////////////////////////////////////////////////////////

VDTVertexProgramD3D11::VDTVertexProgramD3D11()
	: mpVS(NULL)
{
}

VDTVertexProgramD3D11::~VDTVertexProgramD3D11() {
	Shutdown();
}

bool VDTVertexProgramD3D11::Init(VDTContextD3D11 *parent, VDTProgramFormat format, const void *data, uint32 size) {
	HRESULT hr = parent->GetDeviceD3D11()->CreateVertexShader(data, size, NULL, &mpVS);

	if (FAILED(hr))
		return false;

	mByteCode.assign((const uint8 *)data, (const uint8 *)data + size);

	parent->AddResource(this);
	return true;
}

void VDTVertexProgramD3D11::Shutdown() {
	if (mpVS) {
		if (mpParent)
			static_cast<VDTContextD3D11 *>(mpParent)->UnsetVertexProgram(this);

		mpVS->Release();
		mpVS = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTVertexProgramD3D11::Restore() {
	return true;
}

///////////////////////////////////////////////////////////////////////////////

VDTFragmentProgramD3D11::VDTFragmentProgramD3D11()
	: mpPS(NULL)
{
}

VDTFragmentProgramD3D11::~VDTFragmentProgramD3D11() {
	Shutdown();
}

bool VDTFragmentProgramD3D11::Init(VDTContextD3D11 *parent, VDTProgramFormat format, const void *data, uint32 size) {
	HRESULT hr = parent->GetDeviceD3D11()->CreatePixelShader(data, size, NULL, &mpPS);

	if (FAILED(hr))
		return false;

	parent->AddResource(this);
	return true;
}

void VDTFragmentProgramD3D11::Shutdown() {
	if (mpPS) {
		if (mpParent)
			static_cast<VDTContextD3D11 *>(mpParent)->UnsetFragmentProgram(this);

		mpPS->Release();
		mpPS = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTFragmentProgramD3D11::Restore() {
	return true;
}

///////////////////////////////////////////////////////////////////////////////

VDTBlendStateD3D11::VDTBlendStateD3D11()
	: mpBlendState(NULL)
{
}

VDTBlendStateD3D11::~VDTBlendStateD3D11() {
	Shutdown();
}

bool VDTBlendStateD3D11::Init(VDTContextD3D11 *parent, const VDTBlendStateDesc& desc) {
	mDesc = desc;

	static const D3D11_BLEND kD3DBlendStateLookup[]={
		D3D11_BLEND_ZERO,
		D3D11_BLEND_ONE,
		D3D11_BLEND_SRC_COLOR,
		D3D11_BLEND_INV_SRC_COLOR,
		D3D11_BLEND_SRC_ALPHA,
		D3D11_BLEND_INV_SRC_ALPHA,
		D3D11_BLEND_DEST_ALPHA,
		D3D11_BLEND_INV_DEST_ALPHA,
		D3D11_BLEND_DEST_COLOR,
		D3D11_BLEND_INV_DEST_COLOR
	};

	static const D3D11_BLEND kD3DAlphaBlendStateLookup[]={
		D3D11_BLEND_ZERO,
		D3D11_BLEND_ONE,
		D3D11_BLEND_SRC_ALPHA,
		D3D11_BLEND_INV_SRC_ALPHA,
		D3D11_BLEND_SRC_ALPHA,
		D3D11_BLEND_INV_SRC_ALPHA,
		D3D11_BLEND_DEST_ALPHA,
		D3D11_BLEND_INV_DEST_ALPHA,
		D3D11_BLEND_DEST_ALPHA,
		D3D11_BLEND_INV_DEST_ALPHA
	};

	static const D3D11_BLEND_OP kD3DBlendOpLookup[]={
		D3D11_BLEND_OP_ADD,
		D3D11_BLEND_OP_SUBTRACT,
		D3D11_BLEND_OP_REV_SUBTRACT,
		D3D11_BLEND_OP_MIN,
		D3D11_BLEND_OP_MAX
	};

	D3D11_BLEND_DESC d3ddesc = {};
	d3ddesc.RenderTarget[0].BlendEnable = desc.mbEnable;
	d3ddesc.RenderTarget[0].SrcBlend = kD3DBlendStateLookup[desc.mSrc];
	d3ddesc.RenderTarget[0].DestBlend = kD3DBlendStateLookup[desc.mDst];
    d3ddesc.RenderTarget[0].BlendOp = kD3DBlendOpLookup[desc.mOp];
	d3ddesc.RenderTarget[0].SrcBlendAlpha = kD3DAlphaBlendStateLookup[desc.mSrc];
    d3ddesc.RenderTarget[0].DestBlendAlpha = kD3DAlphaBlendStateLookup[desc.mDst];
    d3ddesc.RenderTarget[0].BlendOpAlpha = d3ddesc.RenderTarget[0].BlendOp;
	d3ddesc.RenderTarget[0].RenderTargetWriteMask = desc.mbEnableWriteMask ? desc.mWriteMask : D3D11_COLOR_WRITE_ENABLE_ALL;

	HRESULT hr = parent->GetDeviceD3D11()->CreateBlendState(&d3ddesc, &mpBlendState);

	if (FAILED(hr)) {
		parent->ProcessHRESULT(hr);
		return false;
	}

	parent->AddResource(this);
	return true;
}

void VDTBlendStateD3D11::Shutdown() {
	if (mpParent)
		static_cast<VDTContextD3D11 *>(mpParent)->UnsetBlendState(this);

	if (mpBlendState) {
		mpBlendState->Release();
		mpBlendState = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTBlendStateD3D11::Restore() {
	return true;
}

///////////////////////////////////////////////////////////////////////////////

VDTRasterizerStateD3D11::VDTRasterizerStateD3D11()
	: mpRastState(NULL)
{
}

VDTRasterizerStateD3D11::~VDTRasterizerStateD3D11() {
	Shutdown();
}

bool VDTRasterizerStateD3D11::Init(VDTContextD3D11 *parent, const VDTRasterizerStateDesc& desc) {
	mDesc = desc;

	D3D11_RASTERIZER_DESC d3ddesc = {};
	d3ddesc.FillMode = D3D11_FILL_SOLID;
    d3ddesc.DepthBias = 0;
    d3ddesc.DepthBiasClamp = 0;
    d3ddesc.SlopeScaledDepthBias = 0;
    d3ddesc.DepthClipEnable = TRUE;
    d3ddesc.ScissorEnable = desc.mbEnableScissor;
    d3ddesc.MultisampleEnable = FALSE;
    d3ddesc.AntialiasedLineEnable = FALSE;

	switch(desc.mCullMode) {
		case kVDTCull_None:
			d3ddesc.CullMode = D3D11_CULL_NONE;
			break;

		case kVDTCull_Front:
			d3ddesc.CullMode = D3D11_CULL_FRONT;
			break;

		case kVDTCull_Back:
			d3ddesc.CullMode = D3D11_CULL_BACK;
			break;
	}

	d3ddesc.FrontCounterClockwise = desc.mbFrontIsCCW;

	HRESULT hr = parent->GetDeviceD3D11()->CreateRasterizerState(&d3ddesc, &mpRastState);
	if (FAILED(hr)) {
		parent->ProcessHRESULT(hr);
		return false;
	}

	parent->AddResource(this);
	return true;
}

void VDTRasterizerStateD3D11::Shutdown() {
	if (mpParent)
		static_cast<VDTContextD3D11 *>(mpParent)->UnsetRasterizerState(this);

	if (mpRastState) {
		mpRastState->Release();
		mpRastState = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTRasterizerStateD3D11::Restore() {
	return true;
}

///////////////////////////////////////////////////////////////////////////////

VDTSamplerStateD3D11::VDTSamplerStateD3D11()
	: mpSamplerState(NULL)
{
}

VDTSamplerStateD3D11::~VDTSamplerStateD3D11() {
	Shutdown();
}

bool VDTSamplerStateD3D11::Init(VDTContextD3D11 *parent, const VDTSamplerStateDesc& desc) {
	mDesc = desc;

	static const D3D11_FILTER kD3DFilterLookup[]={
		D3D11_FILTER_MIN_MAG_MIP_POINT,
		D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
		D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,
		D3D11_FILTER_MIN_MAG_MIP_LINEAR,
		D3D11_FILTER_ANISOTROPIC
	};

	static const D3D11_TEXTURE_ADDRESS_MODE kD3DAddressLookup[]={
		D3D11_TEXTURE_ADDRESS_CLAMP,
		D3D11_TEXTURE_ADDRESS_WRAP
	};

	D3D11_SAMPLER_DESC d3ddesc = {};
	d3ddesc.Filter = kD3DFilterLookup[desc.mFilterMode];
	d3ddesc.AddressU = kD3DAddressLookup[desc.mAddressU];
    d3ddesc.AddressV = kD3DAddressLookup[desc.mAddressV];
    d3ddesc.AddressW = kD3DAddressLookup[desc.mAddressW];
    d3ddesc.MipLODBias = 0;
    d3ddesc.MaxAnisotropy = 0;
	d3ddesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    d3ddesc.MinLOD = 0;
    d3ddesc.MaxLOD = D3D11_FLOAT32_MAX;

	HRESULT hr = parent->GetDeviceD3D11()->CreateSamplerState(&d3ddesc, &mpSamplerState);
	if (FAILED(hr)) {
		parent->ProcessHRESULT(hr);
		return false;
	}

	parent->AddResource(this);
	return true;
}

void VDTSamplerStateD3D11::Shutdown() {
	if (mpParent)
		static_cast<VDTContextD3D11 *>(mpParent)->UnsetSamplerState(this);

	if (mpSamplerState) {
		mpSamplerState->Release();
		mpSamplerState = NULL;
	}

	VDTResourceD3D11::Shutdown();
}

bool VDTSamplerStateD3D11::Restore() {
	return true;
}

///////////////////////////////////////////////////////////////////////////////

VDTSwapChainD3D11::VDTSwapChainD3D11()
	: VDThread("VSync Thread (D3D11)")
	, mpTexture(NULL)
	, mVSyncPollPendingSema(0)
	, mbVSyncPending(false)
	, mhVSyncMonitor(NULL)
	, mpVSyncCallback(NULL)
	, mbVSyncPollPending(false)
	, mbVSyncExit(false)
	, mAdapterLuidLo(0)
	, mAdapterLuidHi(0)
	, mWaitHandle(nullptr)
{
}

VDTSwapChainD3D11::~VDTSwapChainD3D11() {
	Shutdown();
}

bool VDTSwapChainD3D11::Init(VDTContextD3D11 *parent, const VDTSwapChainDesc& desc) {
	mDesc = desc;

	// Grab the adapter and attempt to pull the adapter LUID.
	DXGI_ADAPTER_DESC adapterDesc;
	IDXGIAdapter *pAdapter = parent->GetDXGIAdapter();
	if (SUCCEEDED(pAdapter->GetDesc(&adapterDesc))) {
		mAdapterLuidLo = adapterDesc.AdapterLuid.LowPart;
		mAdapterLuidHi = adapterDesc.AdapterLuid.HighPart;
	}

	DXGI_SWAP_CHAIN_DESC scdesc = {};

	if (desc.mWidth && desc.mHeight) {
		scdesc.BufferDesc.Width = desc.mWidth;
		scdesc.BufferDesc.Height = desc.mHeight;
		scdesc.BufferDesc.RefreshRate.Numerator = desc.mRefreshRateNumerator;
		scdesc.BufferDesc.RefreshRate.Denominator = desc.mRefreshRateDenominator;

		// Match to actual mode if we are in full screen. This is particularly necessary
		// if we are trying to hit an exact frame rate like 59.94; it may have been rounded
		// on the way.
		if (!desc.mbWindowed) {
			DXGI_MODE_DESC desiredMode = scdesc.BufferDesc;
			DXGI_MODE_DESC closestMode;
			HMONITOR hmon = ::MonitorFromWindow((HWND)mDesc.mhWindow, MONITOR_DEFAULTTONEAREST);

			RECT r;
			GetClientRect((HWND)mDesc.mhWindow, &r);

			if (hmon) {
				UINT outputIdx = 0;
				vdrefptr<IDXGIOutput> pOutput;
				for(;;) {
					HRESULT hr = pAdapter->EnumOutputs(outputIdx++, ~pOutput);

					if (FAILED(hr))
						break;

					DXGI_OUTPUT_DESC outputDesc;
					hr = pOutput->GetDesc(&outputDesc);

					if (SUCCEEDED(hr) && outputDesc.Monitor == hmon) {
						if (SUCCEEDED(pOutput->FindClosestMatchingMode(&desiredMode, &closestMode, parent->GetDeviceD3D11())))
							scdesc.BufferDesc = closestMode;

						break;
					}
				}
			}
		}
	} else {
		scdesc.BufferDesc.Width = ::GetSystemMetrics(SM_CXSCREEN);
		scdesc.BufferDesc.Height = ::GetSystemMetrics(SM_CYSCREEN);
		scdesc.BufferDesc.RefreshRate.Numerator = 0;
		scdesc.BufferDesc.RefreshRate.Denominator = 0;
	}

	const UINT displayableAndRenderable = D3D11_FORMAT_SUPPORT_RENDER_TARGET | D3D11_FORMAT_SUPPORT_DISPLAY;
	bool useSrgb = false;

	if (desc.mbHDR) {
		UINT rgba16FSupport = 0;
		if (FAILED(parent->GetDeviceD3D11()->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT, &rgba16FSupport)) || (rgba16FSupport & displayableAndRenderable) != displayableAndRenderable)
			return false;

		scdesc.BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	} else {
		if (desc.mbSRGB)
			useSrgb = true;

		scdesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	}

	scdesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	scdesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	scdesc.SampleDesc.Count = 1;
	scdesc.SampleDesc.Quality = 0;
	scdesc.BufferUsage = DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT;

	// This is counter-intuitive, but three buffers are required for DXGI_PRESENT_RESTART to work:
	// one for the front buffer, one for the queued buffer, and one for the buffer being presented.
	// If there are only two buffers, there is never an undisplayed queued frame for DXGI to drop,
	// and we can't reduce latency.
    scdesc.BufferCount = 3;

    scdesc.OutputWindow = (HWND)mDesc.mhWindow;
    scdesc.Windowed = TRUE;

	// FLIP_SEQUENTIAL has better performance than SEQUENTIAL, but it requires Windows 8.
	// Note that Windows 7 with the Platform Update does NOT support this, so just checking
	// for DXGI 1.2 is not sufficient.
	
	mbUsingFlip = false;
	if (VDIsAtLeast10W32() && VDTInternalOptions::sbEnableDXFlipDiscard) {
		scdesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

		mbUsingFlip = true;
	} else if (VDIsAtLeast8W32() && desc.mbWindowed && VDTInternalOptions::sbEnableDXFlipMode) {
		scdesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

		mbUsingFlip = true;
	} else if (!desc.mbWindowed)
		scdesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	else
		scdesc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;

	scdesc.Flags = mDesc.mbWindowed ? 0 : DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	mbAllowTearing = false;

	if (mDesc.mbWindowed && mbUsingFlip) {
		vdrefptr<IDXGIFactory5> factory5;
		if (SUCCEEDED(parent->GetDXGIFactory()->QueryInterface(IID_IDXGIFactory5, (void **)~factory5))) {
			BOOL allowTearing = FALSE;

			if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof allowTearing)) && allowTearing) {
				scdesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
				mbAllowTearing = true;
			}
		}
	}

	bool haveDXGI13 = false;
	if (!desc.mbWindowed) {
		scdesc.OutputWindow = ::GetAncestor(scdesc.OutputWindow, GA_ROOT);
	} else if (mDesc.mbWindowed && mbUsingFlip && VDTInternalOptions::sbEnableDXWaitableObject) {
		// See if we have at least DXGI 1.3 and can use waitable objects for vsync. Note that
		// this can only be used in windowed mode.
		vdrefptr<IDXGIFactory3> factory3;
		if (SUCCEEDED(parent->GetDXGIFactory()->QueryInterface(IID_IDXGIFactory3, (void **)~factory3))) {
			scdesc.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
			haveDXGI13 = true;
		}
	}

	HRESULT hr = parent->GetDXGIFactory()->CreateSwapChain(parent->GetDeviceD3D11(), &scdesc, &mpSwapChain);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	mpSwapChain->QueryInterface(IID_IDXGISwapChain1, (void **)&mpSwapChain1);
	mpSwapChain->QueryInterface(IID_IDXGISwapChainMedia, (void **)&mpSwapChainMedia);

	// we need at least DXGI 1.2 and Windows 8 for DXGI_PRESENT_DO_NOT_WAIT
	mbUsingDoNotWait = mpSwapChain1 && (VDIsAtLeast8W32() && VDTInternalOptions::sbEnableDXDoNotWait);

	// we need DXGI 1.4 for HDR
	if (desc.mbHDR) {
		vdrefptr<IDXGISwapChain3> swapChain3;

		hr = mpSwapChain->QueryInterface(IID_IDXGISwapChain4, (void **)~swapChain3);
		if (FAILED(hr)) {
			Shutdown();
			return false;
		}

		swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
	}

	{
		vdrefptr<IDXGIFactory> pSwapChainFactory;

		hr = mpSwapChain->GetParent(IID_IDXGIFactory, (void **)~pSwapChainFactory);
		if (SUCCEEDED(hr))
			pSwapChainFactory->MakeWindowAssociation((HWND)mDesc.mhWindow, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER);
	}

	if (mDesc.mbWindowed) {
		if (haveDXGI13) {
			vdrefptr<IDXGISwapChain2> swapChain2;

			if (SUCCEEDED(mpSwapChain->QueryInterface(IID_IDXGISwapChain2, (void **)~swapChain2))) {
				swapChain2->SetMaximumFrameLatency(2);

				// Note that this returns a new handle each time that must be closed.
				mWaitHandle = swapChain2->GetFrameLatencyWaitableObject();
				WaitForSingleObject(mWaitHandle, INFINITE);
			}
		}
	} else {
		VDASSERT(!(scdesc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT));
		hr = mpSwapChain->SetFullscreenState(TRUE, NULL);

		if (FAILED(hr)) {
			Shutdown();
			return false;
		}

		// This is needed to avoid a warning from DXGI about full screen inefficiency causing a
		// stretch... which is odd, because we originally matched the mode above, and are now doing
		// a ResizeBuffers() with exactly the same parameters. Apparently, DXGI expects you to do
		// this regardless.
		hr = mpSwapChain->ResizeBuffers(scdesc.BufferCount, scdesc.BufferDesc.Width, scdesc.BufferDesc.Height, scdesc.BufferDesc.Format, scdesc.Flags);
		if (FAILED(hr)) {
			Shutdown();
			return false;
		}
	}

	// look up the actual display mode now through DisplayConfig
	vdfastvector<DISPLAYCONFIG_PATH_INFO> paths;
	vdfastvector<DISPLAYCONFIG_MODE_INFO> modes;
	UINT32 numPaths = 0;
	UINT32 numModes = 0;

	for(int tries = 0; tries < 10; ++tries) {
		if (ERROR_SUCCESS != GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPaths, &numModes))
			break;

		paths.resize(numPaths);
		modes.resize(numModes);

		auto result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPaths, paths.data(), &numModes, modes.data(), nullptr);
		if (result == ERROR_SUCCESS) {
			paths.resize(numPaths);
			modes.resize(numModes);
			break;
		}

		if (result != ERROR_INSUFFICIENT_BUFFER) {
			paths.clear();
			modes.clear();
			break;
		}
	}

	for(const DISPLAYCONFIG_PATH_INFO& path : paths) {
		if (path.targetInfo.adapterId.LowPart == mAdapterLuidLo && path.targetInfo.adapterId.HighPart == mAdapterLuidHi) {
			const DISPLAYCONFIG_RATIONAL& refreshRate = path.targetInfo.refreshRate;
			if (refreshRate.Denominator) {
				mVSyncPeriod = (float)refreshRate.Denominator / (float)refreshRate.Numerator * (float)VDGetPreciseTicksPerSecond();
			}
			break;
		}
	}

	// frame statistics are available if either we are in exclusive full screen or using a flip chain
	mbUsingFrameStatistics = !mDesc.mbWindowed || mbUsingFlip;

	// check if we actually have frame statistics, or if we're just going to get E_NOTIMPL (e.g. Wine)
	if (mbUsingFrameStatistics) {
		DXGI_FRAME_STATISTICS stats {};
		hr = mpSwapChain->GetFrameStatistics(&stats);

		if (FAILED(hr) && hr != DXGI_ERROR_FRAME_STATISTICS_DISJOINT)
			mbUsingFrameStatistics = false;
	}

	BOOL dwmEnabled = FALSE;
	DwmIsCompositionEnabled(&dwmEnabled);
	mbUsingComposition = (dwmEnabled != FALSE);

	vdrefptr<ID3D11Texture2D> d3dtex;
	hr = mpSwapChain->GetBuffer(0, IID_ID3D11Texture2D, (void **)~d3dtex);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	vdrefptr<VDTTexture2DD3D11> tex(new VDTTexture2DD3D11);
	if (!tex->Init(parent, d3dtex, NULL, useSrgb)) {
		Shutdown();
		return false;
	}

	mpTexture = tex.release();
	parent->AddResource(this);
	return true;
}

void VDTSwapChainD3D11::Shutdown() {
	if (isThreadAttached()) {
		mVSyncMutex.Lock();
		mbVSyncExit = true;
		mVSyncMutex.Unlock();
		mVSyncPollPendingSema.Post();

		ThreadWait();
	}

	if (mWaitHandle) {
		VDVERIFY(CloseHandle(mWaitHandle));
		mWaitHandle = nullptr;
	}

	if (mpTexture) {
		static_cast<VDTContextD3D11 *>(mpParent)->UnsetRenderTarget(GetBackBuffer());
		vdsaferelease <<= mpTexture;
	}

	vdsaferelease <<= mpSwapChainMedia;
	vdsaferelease <<= mpSwapChain1;
	
	if (mpSwapChain) {
		// The DXGI debug runtime complains if this is called on a swap chain that has a waitable
		// object, even if full screen mode is being disabled. :p
		if (!mDesc.mbWindowed)
			mpSwapChain->SetFullscreenState(FALSE, NULL);

		vdsaferelease <<= mpSwapChain;
	}

	VDTResourceD3D11::Shutdown();
}

void VDTSwapChainD3D11::SetPresentCallback(IVDTAsyncPresent *callback) {
	mpVSyncCallback = callback;
}

void VDTSwapChainD3D11::GetDesc(VDTSwapChainDesc& desc) {
	desc = mDesc;
}

IVDTSurface *VDTSwapChainD3D11::GetBackBuffer() {
	return mpTexture ? mpTexture->GetLevelSurface(0) : NULL;
}

bool VDTSwapChainD3D11::ResizeBuffers(uint32 width, uint32 height) {
	if (!mpSwapChain)
		return false;

	DXGI_SWAP_CHAIN_DESC desc;
	HRESULT hr = mpSwapChain->GetDesc(&desc);
	if (FAILED(hr))
		return false;

	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	parent->SetRenderTarget(0, NULL, false);

	mpTexture->Shutdown();
	vdsaferelease <<= mpTexture;

	hr = mpSwapChain->ResizeBuffers(desc.BufferCount, width, height, desc.BufferDesc.Format, desc.Flags);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	// reconnect back buffer
	vdrefptr<ID3D11Texture2D> d3dtex;
	hr = mpSwapChain->GetBuffer(0, IID_ID3D11Texture2D, (void **)~d3dtex);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	vdrefptr<VDTTexture2DD3D11> tex(new VDTTexture2DD3D11);
	if (!tex->Init(parent, d3dtex, NULL, false)) {
		Shutdown();
		return false;
	}

	mDesc.mWidth = width;
	mDesc.mHeight = height;

	mpTexture = tex.release();
	return true;
}

bool VDTSwapChainD3D11::CheckOcclusion() {
#if 1
	// Window occlusion checks are disabled for now since they never seem to trip on Windows 10,
	// and so nothing good can happen.
	return true;
#else
	if (!mbWasOccluded)
		return true;

	if (mpSwapChain->Present(0, DXGI_PRESENT_TEST) == S_OK)
		mbWasOccluded = false;

	return !mbWasOccluded;
#endif
}

uint32 VDTSwapChainD3D11::GetQueuedFrames() {
	VDTContextD3D11 *parent = static_cast<VDTContextD3D11 *>(mpParent);
	uint32 delay = 0;

	for(uint32 i = 0; i < vdcountof(mPresentFences); ++i) {
		if (!parent->CheckFence(mPresentFences[i])) {
			delay = (uint32)vdcountof(mPresentFences) - i;
			break;
		}
	}

	return delay;
}

void VDTSwapChainD3D11::SetCustomRefreshRate(float hz, float hzmin, float hzmax) {
	mbUsingCustomDuration = false;

	if (hz < 15.0f || hz > 240.0f)
		return;

	if (hz < hzmin || hz > hzmax)
		return;

	if (mpSwapChainMedia) {
		const UINT desiredPeriod = (UINT)(0.5f + 10'000'000 / hz);
		UINT closestLower = 0;
		UINT closestUpper = 0;

		HRESULT hr = mpSwapChainMedia->CheckPresentDurationSupport(desiredPeriod, &closestLower, &closestUpper);
		if (SUCCEEDED(hr) && (closestLower > 0 || closestUpper > 0)) {
			const UINT minPeriod = (UINT)(0.5f + 10'000'000.0f / hzmax);
			const UINT maxPeriod = (UINT)(0.5f + 10'000'000.0f / hzmin);

			UINT period = 0;
			
			if (closestLower > 0 && closestLower >= minPeriod && closestLower <= maxPeriod)
				period = closestLower;

			if (closestUpper > 0 && closestUpper >= minPeriod && closestUpper <= maxPeriod) {
				if (period == 0 || abs((sint32)(period - desiredPeriod)) >= abs((sint32)(closestUpper - desiredPeriod)))
					period = closestUpper;
			}

			if (period) {
				hr = mpSwapChainMedia->SetPresentDuration(period);
				if (SUCCEEDED(hr)) {
					mbUsingCustomDuration = true;

					// Unfortunately, GetMediaFrameStatistics() seems to often return
					// ApprovedPresentDuration = 0 even when the custom duration is in effect,
					// so we can't rely on it.
					mEffectiveCustomRate = 10'000'000.0f / (float)period;
				}
			}
		}
	}
}

float VDTSwapChainD3D11::GetEffectiveCustomRefreshRate() const {
	return mbUsingCustomDuration ? mEffectiveCustomRate : 0;
}

VDTSwapChainCompositionStatus VDTSwapChainD3D11::GetLastCompositionStatus() const {
	return mLastCompositionStatus;
}

void VDTSwapChainD3D11::Present() {
	if (!mpSwapChain)
		return;

	mLastCompositionStatus = VDTSwapChainCompositionStatus::Unknown;

	if (mpSwapChainMedia && (!mDesc.mbWindowed || mbUsingFlip)) {
		DXGI_FRAME_STATISTICS_MEDIA statsMedia {};
			
		if (SUCCEEDED(mpSwapChainMedia->GetFrameStatisticsMedia(&statsMedia)))
			UpdateCompositionStatus(statsMedia.CompositionMode);
	}

	const uint64 t = VDGetPreciseTick();

	// If we are using waitable handles, we must wait for a ready to render signal, or
	// we will end up injecting an extra count into the semaphore that will fark our
	// ability to track queue length.
	if (mWaitHandle) {
		bool threadConflict = false;
		bool waitReady = false;

		vdsynchronized(mVSyncMutex) {
			mVSyncRequest = VSyncRequest::None;

			if (mbWaitReady) {
				mbWaitReady = false;
				waitReady = true;
			} else if (mbVSyncIsWaiting) {
				threadConflict = true;
				mVSyncRequest = VSyncRequest::WaitForEventSync;
			}
		}

		if (!waitReady) {
			if (threadConflict) {
				for(;;) {
					vdsynchronized(mVSyncMutex) {
						if (mbWaitReady) {
							mbWaitReady = false;

							waitReady = true;
						} else {
							mVSyncRequest = VSyncRequest::WaitForEventSync;
						}
					}

					if (waitReady)
						break;

					mWaitSignal.wait();
				}
			} else {
				WaitForSingleObject(mWaitHandle, INFINITE);
			}
		}
	}

	// If the swap chain is in flip mode, it'll unbind the render target, so we need to
	// sync that state.
	static_cast<VDTContextD3D11 *>(mpParent)->SetRenderTarget(0, nullptr, false);
	HRESULT hr = mpSwapChain->Present(0, mbAllowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
	mbWasOccluded = (hr == DXGI_STATUS_OCCLUDED);

	UINT pcount;
	if (SUCCEEDED(mpSwapChain->GetLastPresentCount(&pcount))) {
		mPresentHistory[pcount & (std::size(mPresentHistory) - 1)] = PresentEvent { t, pcount };
	}

	if (mbVSyncPending)
		mbVSyncPending = false;
}

void VDTSwapChainD3D11::PresentVSync(void *monitor, bool adaptive) {	
	const uint64 t = VDGetPreciseTick();
	bool waitActive = false;
	bool waitReady = false;

	mVSyncWaitStartTime = t;
	mVSyncRetryStartTime = 0;

	mVSyncMutex.Lock();
	mhVSyncMonitor = monitor;

	mbVSyncPollAdaptive = adaptive;
	mVSyncCompositionWaitTime = 0;

	waitActive = mbVSyncIsWaiting;

	if (mbWaitReady)
		waitReady = true;

	mVSyncMutex.Unlock();

	// Cases we need to handle:
	//
	// 1. DXGI 1.3+ flip with waitable handle (windowed / borderless only)
	//
	//    Strategy: Semaphore wait and Present(1)
	//
	//    This is the best case. In this case, we can tell when each frame retires from the
	//    present queue, and by eating a count on the semaphore we can limit the delay to
	//    one frame. Thus, we can simply check if the semaphore indicates a free queue slot,
	//    and present immediately if so. Otherwise, a wait is queued on the handle through
	//    the worker thread, and we defer the present until then. Present interval 1 allows
	//    frames to be queued and lets us know via the semaphore if we're starting to back
	//    up and need to drop frames.
	//
	//    A complication occurs if the worker thread is already waiting on the semaphore due
	//    to a previous unfinished call, in which case it's unsafe to also wait on the semaphore
	//    here. If the WaitReady flag is set, we've already successfully gotten a count and
	//    can immediately present. Otherwise, we need to wait for the worker thread to finish
	//    and have it issue the callback.
	//
	//    Note that it is absolutely critical that every Present() be paired with a semaphore
	//    wait in order to keep counts on the semaphore correct. This includes non-vsync
	//    presents, which we do with interval 0 to try to avoid blocking excessively.
	//
	// 2. DXGI 1.2+ flip in windowed / borderless mode with DO_NOT_WAIT
	//
	//    Strategy: Predict vblank times, Present(0), retry on DXGI_STATUS_WAS_STILL_DRAWING
	//
	//    In this case, we don't have a waitable handle, but we can still detect when the
	//    queue is full by using DXGI_PRESENT_DO_NOT_WAIT. When this trips with WAS_STILL_DRAWING,
	//    we use the worker thread to do a short timed wait to clear the vblank so we can retry
	//    without blocking the main thread.
	//
	//    Note that we do not attempt to wait for the vblank, as this can result in dropped
	//    frames due to tight timing resulting in accidentally waiting for the _next_ vblank.
	//
	//    Additionally, because we have frame statistics available, we can also estimate when
	//    the vblank times are, and try to schedule Presents() to avoid overfilling the frame
	//    queue. This reduces the chances of us filling the frame queue and stalling in
	//    Present(). Most of this time, this is successful and we can Present() at interval 0
	//    without stalling and without excessive frame queuing. At most, we may over-queue
	//    by about 0.3 frames or so when we deliberately hop the vblank to avoid jittering
	//    around the time that the DWM compositor grabs frames.
	//
	// 3. DXGI 1.2+ in exclusive full screen mode, with DXGI_PRESENT_DO_NOT_WAIT
	//
	//    Strategy: Predict vblank times, Present(1), and retry on DXGI_STATUS_WAS_STILL_DRAWING
	//
	//    We don't have a waitable handle available here due to exclusive full screen mode, but
	//    we can use the same strategy as for windowed mode due to frame statistics being
	//    available.
	//
	//    This is a little less effective than windowed mode, as we can't use present interval 0
	//    to queue frames -- doing so causes tearing even if we are using FLIP_DISCARD and
	//    haven't passed DXGI_PRESENT_ALLOW_TEARING. Thus, there's more risk in this case that
	//    we fill up the queue and hit WAS_STILL_DRAWING.
	//
	// 4. DXGI 1.1 in exclusive full screen mode, without DXGI_PRESENT_DO_NOT_WAIT
	//
	//    Strategy: Predict vblank times, Present(1), and drop frames when Present() starts
	//              to continuously stall
	//
	//    In this case, we have very few options; we have no waitable event and can't request
	//    a DO_NOT_WAIT present, but we do have frame statistics. In that case, we can use the
	//    frame statistics to predict the vblank times and use that to schedule our presents.
	//    When there is a free queue slot, the present will go through quickly; when the queue
	//    backs up, Present() will begin to block and the main thread will need to drop a frame.
	//
	// 5. DXGI 1.1 non-flip in windowed / borderless mode without DO_NOT_WAIT
	//
	//    Strategy: Wait for vblank and Present(0)
	//
	//    In this case, we need to use present interval 0. Unfortunately we don't have frames
	//    statistics available which makes this tricky. We do have WaitForVblank(), which would
	//    allow creating a clock, but there is a problem: on Windows 7, calling it blocks
	//    Present() on any other threads during its execution. Thus, the only safe way to use
	//    it is as part of the main present path.
	//
	//    We check whether DWM composition is enabled, and use the worker thread to defer
	//    Present() to either vblank or shortly after vblank. When the DWM is enabled, we need
	//    to delay a bit after vblank to ensure that the DWM is consistent about which frame
	//    it picks up, or else the result is a jittery mess as the DWM randomly picks up
	//    frame-1 or frame. This unfortunately incurs about 1.7-2.7 frames of total latency
	//    depending on whether the DWM is presenting 1 or 2 frames ahead.


	const auto getVSyncHazard = [this](uint64 t) -> uint64 {
		if (!mbVSyncStatsValid)
			return 0;

		double offset = (double)(t - mVSyncTickBase);
		offset -= round(offset / mVSyncPeriod) * mVSyncPeriod;

		const double threshold = std::min<double>(VDGetPreciseTicksPerSecond() * 0.005f, mVSyncPeriod / 6);

		if (fabs(offset) > threshold)
			return 0;

		return threshold - offset;
	};

	bool presentImmediate = waitReady;

	if (mWaitHandle) {
		if (waitReady)
			presentImmediate = true;
		else if (!waitActive && WAIT_OBJECT_0 == WaitForSingleObject(mWaitHandle, 0)) {
			mVSyncMutex.Lock();
			mbWaitReady = true;
			mVSyncMutex.Unlock();
			presentImmediate = true;
		} else
			return PresentVSyncQueueRequest(VSyncRequest::WaitForEvent, 0);
	} else if (!mbUsingFrameStatistics) {
		return PresentVSyncQueueRequest(VSyncRequest::WaitForVSync, 0);
	} else {
		if (!mbVSyncStatsValid) {
			presentImmediate = true;
		} else {
			if (t < mVSyncMinNextTime)
				return PresentVSyncQueueRequest(VSyncRequest::WaitForTime, mVSyncMinNextTime);

			// check if we are in the danger zone
			double offset = (double)(t - mVSyncTickBase);
			offset -= round(offset / mVSyncPeriod) * mVSyncPeriod;

			const double threshold = std::min<double>(VDGetPreciseTicksPerSecond() * 0.002f, mVSyncPeriod / 8);

			if (fabs(offset) > threshold)
				presentImmediate = true;
			else
				return PresentVSyncQueueRequest(VSyncRequest::WaitForTime, t + (uint64)(threshold - offset));
		}
	}

	if (presentImmediate) {
		mbVSyncPending = true;
		PresentVSyncComplete();
	} else
		PresentVSyncRestart();
}

void VDTSwapChainD3D11::PresentVSyncRestart() {
	if (mWaitHandle)
		PresentVSyncQueueRequest(VSyncRequest::WaitForEvent, 0);
	else
		PresentVSyncQueueRequest(VSyncRequest::WaitForVSync, 0);
}

void VDTSwapChainD3D11::PresentVSyncQueueRequest(VSyncRequest request, uint64 deadline) {
	mVSyncMutex.Lock();
	mbVSyncExit = false;
	mVSyncRequest = request;
	mVSyncRequestTime = deadline;

	++mLastPresentStatus.mLastPresentFramesQueued;

	bool needPost = !mbVSyncPollPending;
	mbVSyncPollPending = true;
	mVSyncMutex.Unlock();

	if (needPost)
		mVSyncPollPendingSema.Post();

	if (!isThreadAttached())
		ThreadStart();

	mbVSyncPending = true;
}

bool VDTSwapChainD3D11::PresentVSyncComplete() {
	if (!mbVSyncPending)
		return true;

	mbVSyncPending = false;

	if (!mpSwapChain)
		return true;

	float lpDelay = 0;
	uint64 syncTick = 0;
	uint32 syncCount = 0;
	uint32 presentId = 0;

	mLastPresentTick = VDGetPreciseTick();

	const UINT useDurationFlag = mbUsingCustomDuration ? DXGI_PRESENT_USE_DURATION : 0;
	const UINT useRestartFlag = !mDesc.mbWindowed || mbUsingFlip ? DXGI_PRESENT_RESTART : 0;
	const UINT presentFlags = useRestartFlag | useDurationFlag;

	// If we're using flip, we can use interval 0 to queue a vsynced frame while cancelling
	// another frame already in the queue. If we're using blit, we can't do this as 0 would
	// just mean immediate instead. Note that this only works in windowed mode; flip discard
	// in exclusive fullscreen still tears with interval 0.
	UINT presentInterval = 1;

	if (!mWaitHandle && mDesc.mbWindowed && (mbUsingFlip || !mbUsingFrameStatistics))
		presentInterval = 0;

	HRESULT presentResult {};

	// We seem to need Present1() here in order for an interval of 0 to actually
	// skip frames in flip model. Otherwise, eventually the chain gets filled
	// and lag results.
	if (mpSwapChain1) {
		DXGI_PRESENT_PARAMETERS parms = { 0 };

		// If we are in exclusive fullscreen mode, we must use a flip interval of 1 even if flip
		// is enabled, or tearing results.
		const UINT doNotWaitFlag = mbUsingDoNotWait ? DXGI_PRESENT_DO_NOT_WAIT : 0;

		presentResult = mpSwapChain1->Present1(presentInterval, presentFlags | doNotWaitFlag, &parms);
		if (presentResult == DXGI_ERROR_WAS_STILL_DRAWING) {
			if (!mVSyncRetryStartTime)
				mVSyncRetryStartTime = mLastPresentTick;

			PresentVSyncQueueRequest(VSyncRequest::WaitForTime, mLastPresentTick + VDGetPreciseTicksPerSecondI() / 400);
			return false;
		}
	} else
		presentResult = mpSwapChain->Present(presentInterval, presentFlags);

	UINT pcount;
	if (SUCCEEDED(mpSwapChain->GetLastPresentCount(&pcount))) {
		mPresentHistory[pcount & (std::size(mPresentHistory) - 1)] = PresentEvent { mVSyncWaitStartTime, pcount };
	}

	const uint64 postPresentTick = VDGetPreciseTick();
	float presentCallTime = (float)((double)(postPresentTick - mLastPresentTick) * VDGetPreciseSecondsPerTick());

	float vsyncOffset = -1.0f;
	
	mLastCompositionStatus = VDTSwapChainCompositionStatus::Unknown;

	if (mbUsingFrameStatistics) {
		DXGI_FRAME_STATISTICS stats {};
		bool statsValid = false;

		// If we have media statistics, use those to save a call; it returns a superset.
		if (mpSwapChainMedia) {
			DXGI_FRAME_STATISTICS_MEDIA statsMedia {};
			if (SUCCEEDED(mpSwapChainMedia->GetFrameStatisticsMedia(&statsMedia))) {
				stats.PresentCount			= statsMedia.PresentCount;
				stats.PresentRefreshCount	= statsMedia.PresentRefreshCount;
				stats.SyncRefreshCount		= statsMedia.SyncRefreshCount;
				stats.SyncQPCTime			= statsMedia.SyncQPCTime;
				stats.SyncGPUTime			= statsMedia.SyncGPUTime;

				UpdateCompositionStatus(statsMedia.CompositionMode);
				statsValid = true;
			}
		} else {
			if (SUCCEEDED(mpSwapChain->GetFrameStatistics(&stats)))
				statsValid = true;
		}

		if (statsValid) {
			// The docs for DXGI_FRAME_STATISTICS are pretty bad. The docs for the analogous
			// D3DPRESENTSTATS in D3D9Ex are a bit better, but the best docs are for
			// D3DKMT_PRESENT_STATS. Putting it all together:
			//
			// - PresentCount indicates the last present operation that has retired. It
			//   roughly increments for each present, but may also increment for other
			//   calls such as IDirect3DDevice9::SetDialogBoxMode() -- thus the warning
			//   about this not being the Present() count in the DXGI version.
			//
			// - PresentRefreshCount indicates the vblank counter value for the last
			//   retired present. It also increments for each frame, but the comments
			//   for DWM_TIMING_INFO notes that DXGI may miss vblank interrupts.
			//
			// - SyncRefreshCount and SyncQPCTime indicate a sample relating a VBI time
			//   to QueryPerformanceCounter (QPC) time. This is NOT necessarily either the
			//   last vblank time or the vblank corresponding to PresentCount/PresentRefreshCount,
			//   which is why the DWM calls it "_a_ vblank time".
			//
			// - Samples are not guaranteed to be provided for all refreshes, it may be 2+
			//   refreshes between samples.
			//
			// Note that when DWM composition is involved, these timings include delays from
			// the DWM. They indicate when the image has finally been displayed on the output,
			// but CANNOT indicate when the DWM has consumed a frame from the flip queue.
			//
			// Taking this all together, to get useful latency info out of all of this, we need to:
			//
			// - Maintain a history to translate from PresentCount values to the QPC time
			//   that we submitted the frame.
			//
			// - Maintain a queue of SyncRefreshCount/SyncQPCTime pairs to estimate the
			//   relation between refresh number and QPC time.

			// shift sync history
			auto& lastSyncEvent = mSyncHistory[(mSyncHistoryIndex - 1) & (std::ssize(mSyncHistory) - 1)];

			if (lastSyncEvent.mRefreshCount != stats.SyncRefreshCount) {
				mSyncHistory[mSyncHistoryIndex & ((int)std::ssize(mSyncHistory) - 1)] = SyncEvent { (uint64)stats.SyncQPCTime.QuadPart, stats.SyncRefreshCount };
				++mSyncHistoryIndex;

				if (mSyncHistoryLen < std::size(mSyncHistory))
					++mSyncHistoryLen;
			}

			// try to find present ID for the last present that has hit the display
			const PresentEvent& pe = mPresentHistory[stats.PresentCount & (std::size(mPresentHistory) - 1)];

			if (pe.mPresentCount == stats.PresentCount && mSyncHistoryLen >= std::size(mSyncHistory)) {
				// compute linear regression: y = a + bx (=>) b = Cov(X,Y)/Var(X), a = Avg(Y) - b * Avg(X)
				uint32 x0 = mSyncHistory[0].mRefreshCount;
				uint64 y0 = mSyncHistory[0].mSyncQpcTime;
				double xs = 0;
				double xxs = 0;
				double ys = 0;
				double yys = 0;
				double xys = 0;
				for(int i=1; i<std::ssize(mSyncHistory); ++i) {
					double x = (double)(sint32)(mSyncHistory[i].mRefreshCount - x0);
					double y = (double)(sint64)(mSyncHistory[i].mSyncQpcTime - y0);

					xs += x;
					xxs += x*x;
					ys += y;
					yys += y*y;
					xys += x*y;
				}

				const double invN = 1.0f / (double)std::ssize(mSyncHistory);
				const double invN2 = invN * invN;

				double var = xs*xs*invN2 - xxs*invN;

				if (fabs(var) > 0.0001f) {
					double cov = xs*ys*invN2 - xys*invN;

					if (fabs(cov) > fabs(var)) {
						double b = cov / var;
						double a = ys*invN - b*xs*invN;

						// convert refresh count to QPC time
						uint64 presentQpcTime = mSyncHistory[0].mSyncQpcTime + (uint64)(sint64)(a + b * (sint32)(stats.PresentRefreshCount - mSyncHistory[0].mRefreshCount));

						lpDelay = (float)(sint64)(presentQpcTime - pe.mSubmitQpcTime) * (float)VDGetPreciseSecondsPerTick();

						vsyncOffset = fmod((double)(sint64)(presentQpcTime - mLastPresentTick), b);
						if (vsyncOffset < 0)
							vsyncOffset += b;

						vsyncOffset *= (float)VDGetPreciseSecondsPerTick();

						mbVSyncStatsValid = true;
						mVSyncTickBase = mSyncHistory[0].mSyncQpcTime + (uint64)VDRoundToInt64(a);
						mVSyncPeriod = b;
					}
				}
			}

			syncTick = stats.SyncQPCTime.QuadPart;
			syncCount = stats.SyncRefreshCount;
			presentId = stats.PresentCount;
		}
	}

	uint32 framesQueued = GetQueuedFrames();

	if (mbVSyncStatsValid) {
		double offset = (double)(postPresentTick - mVSyncTickBase);
		offset -= round(offset / mVSyncPeriod) * mVSyncPeriod;

		const double threshold = std::min<double>(VDGetPreciseTicksPerSecond() * 0.002f, mVSyncPeriod / 8);

		mVSyncMinNextTime = postPresentTick + (sint64)llrint(threshold - offset + (offset > 0 ? mVSyncPeriod : 0));
	}

	mVSyncMutex.Lock();
	mbWaitReady = false;

	auto status = mLastPresentStatus;
	float compositionWaitTime = mVSyncCompositionWaitTime;
	mVSyncMutex.Unlock();

	status.mPresentWaitTime = (float)((double)(postPresentTick - mVSyncWaitStartTime) * VDGetPreciseSecondsPerTick());
	status.mLastPresentDelay = lpDelay;
	status.mSyncTick = syncTick;
	status.mSyncCount = syncCount;
	status.mLastPresentCallTime = presentCallTime;
	status.mLastPresentFramesQueued = framesQueued;

	// The present queue time is used by the calling code to determine if excessive latency
	// has built up in the frame queue that should be alleviated by dropping a frame. If we
	// have frame statistics and a wait handle, then we have an accurate view of both the
	// vsync and the frame queue latency, and can return the wait time regardless of the
	// actual Present() call time (which should be negligible). Otherwise, we need to return
	// only blocking time in Present(), or equivalent if we are using DO_NOT_WAIT.
	//
	// What must NOT happen is returning the same value here and for vsync offset. This would
	// cause the frame dropping code to overreact as it would trigger in the range that we
	// need for a stable vsync offset target (~0.5 frame).
	//
	if (vsyncOffset >= 0) {
		status.mVSyncOffset = vsyncOffset;
		status.mPresentQueueTime = status.mPresentWaitTime;
	} else {
		if (mVSyncRetryStartTime)
			status.mPresentQueueTime = (float)((double)(postPresentTick - mVSyncRetryStartTime) * VDGetPreciseSecondsPerTick());
		else
			status.mPresentQueueTime = status.mLastPresentCallTime;
	
		// Without frame statistics, we can still estimate the present-to-vblank offset by
		// the time delay to WaitForVblank() -- but we must make sure not to include the
		// extra sleep done to clear the DWM composition time.
		status.mVSyncOffset = std::max<float>(0, status.mPresentWaitTime - compositionWaitTime);
	}

	if (mVSyncPeriod > 0)
		status.mRefreshRate = (float)VDGetPreciseTicksPerSecond() / mVSyncPeriod;
	else
		status.mRefreshRate = -1.0f;

	mpVSyncCallback->OnPresentCompleted(status);

	VDTContextD3D11& ctx = static_cast<VDTContextD3D11&>(*mpParent);
	std::copy(std::begin(mPresentFences) + 1, std::end(mPresentFences), std::begin(mPresentFences));
	std::end(mPresentFences)[-1] = ctx.InsertFence();

	// If the swap chain is in flip mode, it'll unbind the render target, so we need to
	// sync that state.
	ctx.SetRenderTarget(0, nullptr, false);

	mbWasOccluded = (presentResult == DXGI_STATUS_OCCLUDED);

	return true;
}

void VDTSwapChainD3D11::PresentVSyncAbort() {
	mbVSyncPending = false;

	mVSyncMutex.Lock();
	mVSyncRequest = VSyncRequest::None;
	mVSyncMutex.Unlock();
}

void VDTSwapChainD3D11::UpdateCompositionStatus(DXGI_FRAME_PRESENTATION_MODE dxgiPresentationMode) {
	switch(dxgiPresentationMode) {
		case DXGI_FRAME_PRESENTATION_MODE_COMPOSED:
			mLastCompositionStatus = mbUsingFlip ? VDTSwapChainCompositionStatus::ComposedFlip : VDTSwapChainCompositionStatus::ComposedCopy;
			break;

		case DXGI_FRAME_PRESENTATION_MODE_OVERLAY:
			mLastCompositionStatus = VDTSwapChainCompositionStatus::Overlay;
			break;

		case DXGI_FRAME_PRESENTATION_MODE_NONE:
		case DXGI_FRAME_PRESENTATION_MODE_COMPOSITION_FAILURE:
			mLastCompositionStatus = VDTSwapChainCompositionStatus::Unknown;
			break;
	}
}

void VDTSwapChainD3D11::ThreadRun() {
	VDD3D11Holder *pHolder = static_cast<VDTContextD3D11 *>(mpParent)->GetD3D11Holder();

	vdrefptr<IDXGIFactory1> pFactory;
	vdrefptr<IDXGIAdapter1> pAdapter;
	vdrefptr<IDXGIOutput> pOutput;

	HRESULT hr = pHolder->GetCreateDXGIFactoryFn()(IID_IDXGIFactory1, (void **)~pFactory);
	if (SUCCEEDED(hr))
	{
		UINT adapterIdx = 0;

		for(;;) {
			vdrefptr<IDXGIAdapter1> pCurAdapter;
			hr = pFactory->EnumAdapters1(adapterIdx++, ~pCurAdapter);
			if (FAILED(hr))
				break;

			// wine-4.0 has a bug where each DXGI factory instance has its own set of LUIDs, so it is
			// impossible to match LUIDs between two factory instances. In that case, we try to just
			// use the first adapter we find so we have something.
			if (!pAdapter)
				pAdapter = pCurAdapter;

			DXGI_ADAPTER_DESC adapterDesc;
			hr = pCurAdapter->GetDesc(&adapterDesc);

			if (SUCCEEDED(hr)) {
				if (!mAdapterLuidLo && !mAdapterLuidHi) {
					// no adapter LUID -- take the first one
					break;
				}

				if (adapterDesc.AdapterLuid.LowPart == mAdapterLuidLo &&
					adapterDesc.AdapterLuid.HighPart == mAdapterLuidHi)
				{
					// got an adapter match
					pAdapter = pCurAdapter;
					break;
				}
			}
		}
	}

	if (pAdapter)
		pAdapter->EnumOutputs(0, ~pOutput);

	HMONITOR hmonPrev = nullptr;
	bool triedWaitableTimer = false;
	HANDLE hWaitTimer = nullptr;

	for(;;) {
		// wait for something to do
		mVSyncPollPendingSema.Wait();

		bool pollAdaptive = false;

		mVSyncMutex.Lock();
		pollAdaptive = mbVSyncPollAdaptive;

		if (mbVSyncExit) {
			mVSyncMutex.Unlock();
			break;
		}

		VSyncRequest request = mVSyncRequest;
		uint64 requestTime = mVSyncRequestTime;
		HMONITOR hmon = (HMONITOR)mhVSyncMonitor;

		if (request == VSyncRequest::None) {
			mbVSyncPollPending = false;
			mVSyncMutex.Unlock();
			continue;
		}

		bool waitNeeded = !mbWaitReady;

		if (waitNeeded && request == VSyncRequest::WaitForEvent)
			mbVSyncIsWaiting = true;
		else
			mbVSyncIsWaiting = false;

		mVSyncMutex.Unlock();

		bool waitCompleted = false;
		if (request == VSyncRequest::WaitForEvent || request == VSyncRequest::WaitForEventSync) {
			// On Windows 8.1 Update 1, the waitable object that is returned is actually
			// a semaphore which is posted for every completed present. Thus, bad things will
			// happen latency-wise if an extra count gets in there. It is critical
			// that every successful present follow one successful wait.
			if (waitNeeded) {
				if (!mWaitHandle || WAIT_OBJECT_0 != ::WaitForSingleObject(mWaitHandle, INFINITE)) {
					// wait failed - make sure to sleep so we don't spin
					::Sleep(10);
				}

				waitCompleted = true;
			}
		} else if (request == VSyncRequest::WaitForTime) {
			uint64 now = VDGetPreciseTick();
			sint64 delay = requestTime - now;

			if (delay > 0) {
				if (!triedWaitableTimer) {
					triedWaitableTimer = true;

					// This requires at least Windows 10 1803 (RS4) or later.
					if (VDIsAtLeast10_1803W32())
						hWaitTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

					if (!hWaitTimer)
						hWaitTimer = CreateWaitableTimer(nullptr, false, nullptr);
				}

				// make sure we never wait for more than 20ms, just to be safe
				uint64 clampedDelay = std::min<uint64>((uint64)delay, VDGetPreciseTicksPerSecond() * 0.020f);

				if (hWaitTimer) {
					uint32 clampedDelay100Ns = (uint32)((clampedDelay * 10000 - 1) / VDGetPreciseTicksPerSecondI() + 1);

					LARGE_INTEGER delay {};
					delay.QuadPart = -(LONGLONG)clampedDelay100Ns;
					if (SetWaitableTimer(hWaitTimer, &delay, 0, nullptr, nullptr, false))
						WaitForSingleObject(hWaitTimer, INFINITE);
				} else {
					uint32 clampedDelayMs = (uint32)((clampedDelay * 1000 - 1) / VDGetPreciseTicksPerSecondI() + 1);

					::Sleep(clampedDelayMs);
				}
			}
		} else if (request == VSyncRequest::WaitForVSync) {
			if (hmon && hmon != hmonPrev && pAdapter) {
				UINT outputIdx = 0;

				for(;;) {
					hr = pAdapter->EnumOutputs(outputIdx++, ~pOutput);

					if (FAILED(hr))
						break;

					DXGI_OUTPUT_DESC outputDesc;
					hr = pOutput->GetDesc(&outputDesc);

					if (SUCCEEDED(hr) && outputDesc.Monitor == hmon)
						break;
				}

				hmonPrev = hmon;
			}

			if (pOutput) {
				// It is really important that we only call this while the main thread is waiting
				// to Present(). On Windows 7, it takes a shared mutex that causes Present()
				// to block while another thread is in WaitForVBlank(), which prevents us from having
				// a separate thread use it for a clock. D3DKMTWaitForVerticalBlank() is essentially
				// identical and has the same blocking problem.
				pOutput->WaitForVBlank();

				if (mbUsingComposition) {
					// Delay a bit so that we don't race the DWM on the blit. The issue is that the DWM
					// composites at the start of vblank, so if we try to also Present() around that same
					// time, it's a crapshoot whether the frame makes it into the DWM's render pass in
					// time and the result is some rather ugly jitter.
					::Sleep(5);
					mVSyncCompositionWaitTime = 0.005f;
				}
			} else {
				// No output, we'd better just do fake vsync.
				::Sleep(30);
			}
		} else {
			::Sleep(5);
		}

		mVSyncMutex.Lock();
		mbVSyncIsWaiting = false;
		mbVSyncPollPending = false;
		
		if (waitCompleted)
			mbWaitReady = true;

		if (mVSyncRequest != VSyncRequest::None) {
			if (mVSyncRequest == VSyncRequest::WaitForEventSync)
				mWaitSignal.signal();
			else
				mpVSyncCallback->QueuePresent(false);

			mVSyncRequest = VSyncRequest::None;
		}

		mVSyncMutex.Unlock();
	}

	if (hWaitTimer) {
		CloseHandle(hWaitTimer);
		hWaitTimer = nullptr;
	}
}

///////////////////////////////////////////////////////////////////////////////

struct VDTContextD3D11::PrivateData {
	bool mbMinPrecisionSupportedPS = false;
	bool mbMinPrecisionSupportedOther = false;

	VDTFenceManagerD3D11 mFenceManager;
};

const uint8 VDTContextD3D11::kConstLookup[] = {
	0, 0,					// 0-16
	1,						// 17-32
	2, 2,					// 33-64
	3, 3, 3, 3,				// 65-128
	4, 4, 4, 4, 4, 4, 4, 4,	// 129-256
};

VDTContextD3D11::VDTContextD3D11()
	: mRefCount(0)
	, mpData(NULL)
	, mpD3DHolder(NULL)
	, mpDXGIFactory(NULL)
	, mpD3DDevice(NULL)
	, mpD3DDeviceContext(NULL)
	, mpSwapChain(NULL)
	, mpCurrentRT(NULL)
	, mpCurrentVB(NULL)
	, mCurrentVBOffset(NULL)
	, mCurrentVBStride(NULL)
	, mpCurrentIB(NULL)
	, mpCurrentVP(NULL)
	, mpCurrentFP(NULL)
	, mpCurrentVF(NULL)
	, mpCurrentBS(NULL)
	, mpCurrentRS(NULL)
	, mpDefaultBS(NULL)
	, mpDefaultRS(NULL)
	, mpDefaultSS(NULL)
	, mProfChan("Filter 3D accel")
{
	memset(mpCurrentSamplerStates, 0, sizeof mpCurrentSamplerStates);
	memset(mpCurrentTextures, 0, sizeof mpCurrentTextures);
}

VDTContextD3D11::~VDTContextD3D11() {
	Shutdown();
}

int VDTContextD3D11::AddRef() {
	return ++mRefCount;
}

int VDTContextD3D11::Release() {
	int rc = --mRefCount;

	if (!rc)
		delete this;

	return rc;
}

void *VDTContextD3D11::AsInterface(uint32 iid) {
	return NULL;
}

bool VDTContextD3D11::Init(ID3D11Device *dev, ID3D11DeviceContext *devctx, IDXGIAdapter1 *adapter, IDXGIFactory *factory, VDD3D11Holder *pD3DHolder) {
	mpData = new PrivateData;

	// Force max frame latency on the device to 1 frame only. This is required to avoid
	// stacking frames in the present queue in full screen mode, as fences are NOT able
	// to detect buffers pending present. When using a waitable object in windowed
	// mode, we have to set this on the swap chain instead.
	vdrefptr<IDXGIDevice1> dev1;
	dev->QueryInterface(IID_IDXGIDevice1, (void **)~dev1);
	if (dev1) {
		// TODO: This causes stuttering when present is near vblank in exclusive fullscreen,
		// even though there is no stalling in Present().
		dev1->SetMaximumFrameLatency(1);
	}

	if (SUCCEEDED(devctx->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), (void **)&mpD3DAnnotation))) {
		// check if we actually have profiling enabled
		if (!mpD3DAnnotation->GetStatus())
			vdsaferelease <<= mpD3DAnnotation;
	}

	DXGI_ADAPTER_DESC adapterDesc {};
	adapter->GetDesc(&adapterDesc);

	mCaps.mDeviceDescription = adapterDesc.Description;

	switch(dev->GetFeatureLevel()) {
		case D3D_FEATURE_LEVEL_11_0:
			mCaps.mMaxTextureWidth = 16384;
			mCaps.mMaxTextureHeight = 16384;
			mCaps.mbNonPow2 = true;
			mCaps.mbNonPow2Conditional = true;
			break;

		case D3D_FEATURE_LEVEL_10_1:
		case D3D_FEATURE_LEVEL_10_0:
			mCaps.mMaxTextureWidth = 8192;
			mCaps.mMaxTextureHeight = 8192;
			mCaps.mbNonPow2 = true;
			mCaps.mbNonPow2Conditional = true;
			break;

		case D3D_FEATURE_LEVEL_9_3:
			mCaps.mMaxTextureWidth = 4096;
			mCaps.mMaxTextureHeight = 4096;
			mCaps.mbNonPow2 = false;
			mCaps.mbNonPow2Conditional = true;
			break;

		case D3D_FEATURE_LEVEL_9_2:
		case D3D_FEATURE_LEVEL_9_1:
		default:
			mCaps.mMaxTextureWidth = 2048;
			mCaps.mMaxTextureHeight = 2048;
			mCaps.mbNonPow2 = false;
			mCaps.mbNonPow2Conditional = true;
			break;
	}

	mCaps.mbMinPrecisionPS = false;
	mCaps.mbMinPrecisionNonPS = false;

	if (VDIsAtLeast8W32()) {
		D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT support {};
		if (SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &support, sizeof support))) {
			mpData->mbMinPrecisionSupportedPS = (support.PixelShaderMinPrecision > 0);
			mpData->mbMinPrecisionSupportedOther = (support.AllOtherShaderStagesMinPrecision> 0);

			mCaps.mbMinPrecisionPS = mpData->mbMinPrecisionSupportedPS;
			mCaps.mbMinPrecisionNonPS = mpData->mbMinPrecisionSupportedOther;
		}
	}

	mpD3DHolder = pD3DHolder;
	if (mpD3DHolder)
		mpD3DHolder->AddRef();

	mpDXGIFactory = factory;
	mpDXGIFactory->AddRef();

	mpDXGIAdapter = adapter;
	mpDXGIAdapter->AddRef();

	mpD3DDevice = dev;
	mpD3DDevice->AddRef();

	mpD3DDeviceContext = devctx;
	mpD3DDeviceContext->AddRef();

	mpData->mFenceManager.Init(mpD3DDevice, mpD3DDeviceContext);

	mpDefaultBS = new VDTBlendStateD3D11;
	mpDefaultBS->AddRef();
	mpDefaultBS->Init(this, VDTBlendStateDesc());

	mpDefaultRS = new VDTRasterizerStateD3D11;
	mpDefaultRS->AddRef();
	mpDefaultRS->Init(this, VDTRasterizerStateDesc());

	mpDefaultSS = new VDTSamplerStateD3D11;
	mpDefaultSS->AddRef();
	mpDefaultSS->Init(this, VDTSamplerStateDesc());

	mpCurrentBS = NULL;
	mpCurrentRS = NULL;
	mpCurrentRT = NULL;

	for(int i=0; i<16; ++i)
		mpCurrentSamplerStates[i] = mpDefaultSS;

	D3D11_BUFFER_DESC bufdesc = {};
	for(uint32 i=0; i<kConstMaxShift; ++i) {
		bufdesc.ByteWidth = (sizeof(float) * 4) << (kConstBaseShift + i);
		bufdesc.Usage = D3D11_USAGE_DEFAULT;
		bufdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufdesc.CPUAccessFlags = 0;
		bufdesc.MiscFlags = 0;
		bufdesc.StructureByteStride = 0;
		mpD3DDevice->CreateBuffer(&bufdesc, NULL, &mpVSConstBuffers[i]);
		mpD3DDevice->CreateBuffer(&bufdesc, NULL, &mpPSConstBuffers[i]);
	}

	mpD3DDeviceContext->VSSetConstantBuffers(0, 1, &mpVSConstBuffers[0]);
	mpD3DDeviceContext->PSSetConstantBuffers(0, 1, &mpPSConstBuffers[0]);

	SetBlendState(NULL);
	SetRasterizerState(NULL);

	D3D11_VIEWPORT vp = {0,0,0,0,0,0};
	mpD3DDeviceContext->RSSetViewports(1, &vp);

	memset(&mViewport, 0, sizeof mViewport);
	mScissorRect.set(0, 0, 0, 0);

	return true;
}

void VDTContextD3D11::Shutdown() {
	if (mpD3DDeviceContext)
		mpD3DDeviceContext->ClearState();

	// We need to drop the implicit swap chain first so we don't attempt to re-set it.
	// However, the swap chain itself will trigger an unset, so we must do a swaparoo here.
	IVDTSwapChain *sc = mpSwapChain;
	mpSwapChain = NULL;

	vdsaferelease <<= sc;

	ShutdownAllResources();

	vdsaferelease <<= mpDefaultSS, mpDefaultRS, mpDefaultBS;
	vdsaferelease <<= mpPSConstBuffers;
	vdsaferelease <<= mpVSConstBuffers;

	mpData->mFenceManager.Shutdown();

	vdsaferelease <<= mpD3DAnnotation, mpD3DDeviceContext, mpD3DDevice;
	vdsaferelease <<= mpDXGIAdapter, mpDXGIFactory, mpD3DHolder;

	if (mpData) {
		delete mpData;
		mpData = NULL;
	}
}

void VDTContextD3D11::SetImplicitSwapChain(VDTSwapChainD3D11 *sc) {
	mpSwapChain = sc;
	sc->AddRef();

	SetRenderTarget(0, NULL, false);
}

bool VDTContextD3D11::IsFormatSupportedTexture2D(VDTFormat format) {
	DXGI_FORMAT dxgiFormat = GetSurfaceFormatD3D11(format);
	UINT support = 0;

	HRESULT hr = mpD3DDevice->CheckFormatSupport(dxgiFormat, &support);
	return SUCCEEDED(hr) && (support & D3D11_FORMAT_SUPPORT_TEXTURE2D);
}

bool VDTContextD3D11::IsMonitorHDREnabled(void *monitor, bool& systemSupport) {
	vdrefptr<IDXGIOutput> output;
	systemSupport = false;

	for(UINT i = 0; SUCCEEDED(mpDXGIAdapter->EnumOutputs(i, ~output)); ++i) {
		vdrefptr<IDXGIOutput6> output6;
		if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput6), (void **)~output6)))
			continue;

		// If we got this far, we have DXGI 1.6, and thus OS support
		systemSupport = true;

		DXGI_OUTPUT_DESC1 desc;

		if (FAILED(output6->GetDesc1(&desc)))
			continue;

		if (desc.Monitor == (HMONITOR)monitor) {
			switch(desc.ColorSpace) {
				case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
					return true;

				default:
					return false;
			}
			break;
		}
	}

	return false;
}

bool VDTContextD3D11::CreateReadbackBuffer(uint32 width, uint32 height, VDTFormat format, IVDTReadbackBuffer **ppbuffer) {
	vdrefptr<VDTReadbackBufferD3D11> surf(new VDTReadbackBufferD3D11);

	if (!surf->Init(this, width, height, format))
		return false;

	*ppbuffer = surf.release();
	return true;
}

bool VDTContextD3D11::CreateSurface(uint32 width, uint32 height, VDTFormat format, VDTUsage usage, IVDTSurface **ppsurface) {
	vdrefptr<VDTSurfaceD3D11> surf(new VDTSurfaceD3D11);

	if (!surf->Init(this, width, height, format, usage))
		return false;

	*ppsurface = surf.release();
	return true;
}

bool VDTContextD3D11::CreateTexture2D(uint32 width, uint32 height, VDTFormat format, uint32 mipcount, VDTUsage usage, const VDTInitData2D *initData, IVDTTexture2D **pptex) {
	vdrefptr<VDTTexture2DD3D11> tex(new VDTTexture2DD3D11);

	if (!tex->Init(this, width, height, format, mipcount, usage, initData))
		return false;

	*pptex = tex.release();
	return true;
}

bool VDTContextD3D11::CreateVertexProgram(VDTProgramFormat format, VDTData data, IVDTVertexProgram **program) {
	vdrefptr<VDTVertexProgramD3D11> vp(new VDTVertexProgramD3D11);

	if (format == kVDTPF_MultiTarget) {
		static const uint32 kVSTargets[]={
			0x1b39ae13 ^ 1,	// vs_4_0_level_9_1 (min precision)
			0x1b39ae11 ^ 1,	// vs_4_0_level_9_3 (min precision)
			0x62d902b4 ^ 1,	// vs_4_0 (min precision)

			0x1b39ae13,		// vs_4_0_level_9_1
			0x1b39ae11,		// vs_4_0_level_9_3
			0x62d902b4,		// vs_4_0
			0
		};


		if (!VDTExtractMultiTargetProgram(data, mpData->mbMinPrecisionSupportedOther ? kVSTargets : kVSTargets + 3, data))
			return false;

		format = kVDTPF_D3D11ByteCode;
	} else if (format != kVDTPF_D3D11ByteCode)
		return false;

	if (!vp->Init(this, format, data.mpData, data.mLength))
		return false;

	*program = vp.release();
	return true;
}

bool VDTContextD3D11::CreateFragmentProgram(VDTProgramFormat format, VDTData data, IVDTFragmentProgram **program) {
	vdrefptr<VDTFragmentProgramD3D11> fp(new VDTFragmentProgramD3D11);

	if (format == kVDTPF_MultiTarget) {
		static const uint32 kPSTargets[]={
			0x301bfb89 ^ 1,	// ps_4_0_level_9_1
			0x301bfb8b ^ 1,	// ps_4_0_level_9_3
			0x4657d92a ^ 1,	// ps_4_0

			0x301bfb89,		// ps_4_0_level_9_1
			0x301bfb8b,		// ps_4_0_level_9_3
			0x4657d92a,		// ps_4_0
			0
		};

		if (!VDTExtractMultiTargetProgram(data, mpData->mbMinPrecisionSupportedPS ? kPSTargets : kPSTargets + 3, data))
			return false;
	} else if (format != kVDTPF_D3D11ByteCode)
		return false;

	if (!fp->Init(this, format, data.mpData, data.mLength))
		return false;

	*program = fp.release();
	return true;
}

bool VDTContextD3D11::CreateVertexFormat(const VDTVertexElement *elements, uint32 count, IVDTVertexProgram *vp, IVDTVertexFormat **format) {
	VDASSERT(vp);

	vdrefptr<VDTVertexFormatD3D11> vf(new VDTVertexFormatD3D11);

	if (!vf->Init(this, elements, count, static_cast<VDTVertexProgramD3D11 *>(vp)))
		return false;

	*format = vf.release();
	return true;
}

bool VDTContextD3D11::CreateVertexBuffer(uint32 size, bool dynamic, const void *initData, IVDTVertexBuffer **ppbuffer) {
	vdrefptr<VDTVertexBufferD3D11> vb(new VDTVertexBufferD3D11);

	if (!vb->Init(this, size, dynamic, initData))
		return false;

	*ppbuffer = vb.release();
	return true;
}

bool VDTContextD3D11::CreateIndexBuffer(uint32 size, bool index32, bool dynamic, const void *initData, IVDTIndexBuffer **ppbuffer) {
	vdrefptr<VDTIndexBufferD3D11> ib(new VDTIndexBufferD3D11);

	if (!ib->Init(this, size, index32, dynamic, initData))
		return false;

	*ppbuffer = ib.release();
	return true;
}

bool VDTContextD3D11::CreateBlendState(const VDTBlendStateDesc& desc, IVDTBlendState **state) {
	vdrefptr<VDTBlendStateD3D11> bs(new VDTBlendStateD3D11);

	if (!bs->Init(this, desc))
		return false;

	*state = bs.release();
	return true;
}

bool VDTContextD3D11::CreateRasterizerState(const VDTRasterizerStateDesc& desc, IVDTRasterizerState **state) {
	vdrefptr<VDTRasterizerStateD3D11> rs(new VDTRasterizerStateD3D11);

	if (!rs->Init(this, desc))
		return false;

	*state = rs.release();
	return true;
}

bool VDTContextD3D11::CreateSamplerState(const VDTSamplerStateDesc& desc, IVDTSamplerState **state) {
	vdrefptr<VDTSamplerStateD3D11> ss(new VDTSamplerStateD3D11);

	if (!ss->Init(this, desc))
		return false;

	*state = ss.release();
	return true;
}

bool VDTContextD3D11::CreateSwapChain(const VDTSwapChainDesc& desc, IVDTSwapChain **swapChain) {
	vdrefptr<VDTSwapChainD3D11> sc(new VDTSwapChainD3D11);

	if (!sc->Init(this, desc))
		return false;

	*swapChain = sc.release();
	return true;
}

IVDTSurface *VDTContextD3D11::GetRenderTarget(uint32 index) const {
	return index ? NULL : mpCurrentRT;
}

bool VDTContextD3D11::GetRenderTargetBypass(uint32 index) const {
	return index == 0 && mbCurrentRTBypass;
}

void VDTContextD3D11::SetVertexFormat(IVDTVertexFormat *format) {
	if (format == mpCurrentVF)
		return;

	mpCurrentVF = static_cast<VDTVertexFormatD3D11 *>(format);

	mpD3DDeviceContext->IASetInputLayout(mpCurrentVF ? mpCurrentVF->mpVF : NULL);
}

void VDTContextD3D11::SetVertexProgram(IVDTVertexProgram *program) {
	if (program == mpCurrentVP)
		return;

	mpCurrentVP = static_cast<VDTVertexProgramD3D11 *>(program);

	mpD3DDeviceContext->VSSetShader(mpCurrentVP ? mpCurrentVP->mpVS : NULL, NULL, 0);
}

void VDTContextD3D11::SetFragmentProgram(IVDTFragmentProgram *program) {
	if (program == mpCurrentFP)
		return;

	mpCurrentFP = static_cast<VDTFragmentProgramD3D11 *>(program);

	mpD3DDeviceContext->PSSetShader(mpCurrentFP ? mpCurrentFP->mpPS : NULL, NULL, 0);
}

void VDTContextD3D11::SetVertexStream(uint32 index, IVDTVertexBuffer *buffer, uint32 offset, uint32 stride) {
	VDASSERT(index == 0);

	if (buffer == mpCurrentVB && offset == mCurrentVBOffset && offset == mCurrentVBStride)
		return;

	mpCurrentVB = static_cast<VDTVertexBufferD3D11 *>(buffer);
	mCurrentVBOffset = offset;
	mCurrentVBStride = stride;

	ID3D11Buffer *pBuffer = mpCurrentVB ? mpCurrentVB->mpVB : NULL;
	UINT d3doffset = offset;
	UINT d3dstride = stride;
	mpD3DDeviceContext->IASetVertexBuffers(index, 1, &pBuffer, &d3dstride, &d3doffset);
}

void VDTContextD3D11::SetIndexStream(IVDTIndexBuffer *buffer) {
	if (buffer == mpCurrentIB)
		return;

	mpCurrentIB = static_cast<VDTIndexBufferD3D11 *>(buffer);

	mpD3DDeviceContext->IASetIndexBuffer(mpCurrentIB ? mpCurrentIB->mpIB : NULL, mpCurrentIB ? mpCurrentIB->mbIndex32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT : DXGI_FORMAT_UNKNOWN, 0);
}

void VDTContextD3D11::SetRenderTarget(uint32 index, IVDTSurface *surface, bool bypassConversion) {
	VDASSERT(index == 0);

	if (index == 0 && !surface && mpSwapChain)
		surface = mpSwapChain->GetBackBuffer();

	if (mpCurrentRT == surface && mbCurrentRTBypass == bypassConversion)
		return;

	mpCurrentRT = static_cast<VDTSurfaceD3D11 *>(surface);
	mbCurrentRTBypass = bypassConversion;

	if (mpCurrentRT) {
		IVDTTexture *parentTex = mpCurrentRT->mpParentTexture;

		if (parentTex) {
			IVDTTexture *nulltex = nullptr;

			for(int i=0; i<(int)vdcountof(mpCurrentTextures); ++i) {
				if (mpCurrentTextures[i] == parentTex)
					this->SetTextures(i, 1, &nulltex);
			}
		}
	}

	ID3D11RenderTargetView *rtv = nullptr;

	if (mpCurrentRT) {
		if (bypassConversion && mpCurrentRT->mpRTViewNoSrgb)
			rtv = mpCurrentRT->mpRTViewNoSrgb;
		else
			rtv = mpCurrentRT->mpRTView;
	}

	mpD3DDeviceContext->OMSetRenderTargets(1, &rtv, nullptr);
}

void VDTContextD3D11::SetBlendState(IVDTBlendState *state) {
	if (!state)
		state = mpDefaultBS;

	if (mpCurrentBS == state)
		return;

	mpCurrentBS = static_cast<VDTBlendStateD3D11 *>(state);

	float blendfactor[4] = {0};
	mpD3DDeviceContext->OMSetBlendState(mpCurrentBS->mpBlendState, blendfactor, 0xFFFFFFFF);
}

void VDTContextD3D11::SetSamplerStates(uint32 baseIndex, uint32 count, IVDTSamplerState *const *states) {
	VDASSERT(baseIndex <= 16 && 16 - baseIndex >= count);

	for(uint32 i=0; i<count; ++i) {
		uint32 stage = baseIndex + i;
		VDTSamplerStateD3D11 *state = static_cast<VDTSamplerStateD3D11 *>(states[i]);
		if (!state)
			state = mpDefaultSS;

		if (mpCurrentSamplerStates[stage] != state) {
			mpCurrentSamplerStates[stage] = state;
			
			ID3D11SamplerState *d3dss = state->mpSamplerState;
			mpD3DDeviceContext->PSSetSamplers(stage, 1, &d3dss);
		}
	}
}

void VDTContextD3D11::SetTextures(uint32 baseIndex, uint32 count, IVDTTexture *const *textures) {
	VDASSERT(baseIndex <= 16 && 16 - baseIndex >= count);

	for(uint32 i=0; i<count; ++i) {
		uint32 stage = baseIndex + i;
		IVDTTexture *tex = textures[i];

		if (mpCurrentTextures[stage] != tex) {
			mpCurrentTextures[stage] = tex;

			ID3D11ShaderResourceView *pSRV = tex ? (ID3D11ShaderResourceView *)tex->AsInterface(VDTTextureD3D11::kTypeD3DShaderResView) : NULL;
			mpD3DDeviceContext->PSSetShaderResources(stage, 1, &pSRV);
		}
	}
}

void VDTContextD3D11::ClearTexturesStartingAt(uint32 baseIndex) {
	for(uint32 i = baseIndex; i < 16; ++i) {
		if (mpCurrentTextures[i]) {
			mpCurrentTextures[i] = nullptr;

			ID3D11ShaderResourceView *srv = nullptr;
			mpD3DDeviceContext->PSSetShaderResources(i, 1, &srv);
		}
	}
}

void VDTContextD3D11::SetRasterizerState(IVDTRasterizerState *state) {
	if (!state)
		state = mpDefaultRS;

	if (mpCurrentRS == state)
		return;

	mpCurrentRS = static_cast<VDTRasterizerStateD3D11 *>(state);

	mpD3DDeviceContext->RSSetState(mpCurrentRS->mpRastState);
}

VDTViewport VDTContextD3D11::GetViewport() {
	return mViewport;
}

void VDTContextD3D11::SetViewport(const VDTViewport& vp) {
	mViewport = vp;

	D3D11_VIEWPORT d3dvp;
	d3dvp.TopLeftX = (float)vp.mX;
	d3dvp.TopLeftY = (float)vp.mY;
	d3dvp.Width = (float)vp.mWidth;
	d3dvp.Height = (float)vp.mHeight;
	d3dvp.MinDepth = vp.mMinZ;
	d3dvp.MaxDepth = vp.mMaxZ;

	mpD3DDeviceContext->RSSetViewports(1, &d3dvp);
}

vdrect32 VDTContextD3D11::GetScissorRect() {
	return mScissorRect;
}

void VDTContextD3D11::SetScissorRect(const vdrect32& r) {
	if (mScissorRect != r) {
		mScissorRect = r;

		D3D10_RECT dr = { r.left, r.top, r.right, r.bottom };
		mpD3DDeviceContext->RSSetScissorRects(1, &dr);
	}
}

void VDTContextD3D11::SetVertexProgramConstCount(uint32 count) {
	uint32 shift = kConstLookup[(count + 15) >> 4];

	if (mVSConstShift != shift) {
		mVSConstShift = shift;

		mpD3DDeviceContext->VSSetConstantBuffers(0, 1, &mpVSConstBuffers[shift]);
		mbVSConstDirty = true;
	}
}

void VDTContextD3D11::SetVertexProgramConstF(uint32 baseIndex, uint32 count, const float *data) {
	memcpy(&mVSConsts[baseIndex][0], data, count * 16);
	mbVSConstDirty = true;
}

void VDTContextD3D11::SetFragmentProgramConstCount(uint32 count) {
	uint32 shift = kConstLookup[(count + 15) >> 4];

	if (mPSConstShift != shift) {
		mPSConstShift = shift;
		mpD3DDeviceContext->PSSetConstantBuffers(0, 1, &mpPSConstBuffers[shift]);
		mbPSConstDirty = true;
	}
}

void VDTContextD3D11::SetFragmentProgramConstF(uint32 baseIndex, uint32 count, const float *data) {
	memcpy(&mPSConsts[baseIndex][0], data, count * 16);
	mbPSConstDirty = true;
}

void VDTContextD3D11::Clear(VDTClearFlags clearFlags, uint32 color, float depth, uint32 stencil) {
	if (clearFlags & kVDTClear_Color) {
		float color32[4];

		color32[0] = (int)(color >> 16) / 255.0f;
		color32[1] = (int)(color >>  8) / 255.0f;
		color32[2] = (int)(color >>  0) / 255.0f;
		color32[3] = (int)(color >> 24) / 255.0f;

		mpD3DDeviceContext->ClearRenderTargetView(mpCurrentRT->mpRTView, color32);
	}
}

namespace {
	const D3D11_PRIMITIVE_TOPOLOGY kPTLookup[]={
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
		D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
		D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP
	};
}

void VDTContextD3D11::DrawPrimitive(VDTPrimitiveType type, uint32 startVertex, uint32 primitiveCount) {
	if (mLastPrimitiveType != (int)type) {
		mLastPrimitiveType = (int)type;

		mpD3DDeviceContext->IASetPrimitiveTopology(kPTLookup[type]);
	}

	UpdateConstants();

	switch(type) {
		case kVDTPT_Triangles:
			mpD3DDeviceContext->Draw(primitiveCount * 3, startVertex);
			break;

		case kVDTPT_TriangleStrip:
			mpD3DDeviceContext->Draw(primitiveCount + 2, startVertex);
			break;

		case kVDTPT_Lines:
			mpD3DDeviceContext->Draw(primitiveCount * 2, startVertex);
			break;

		case kVDTPT_LineStrip:
			mpD3DDeviceContext->Draw(primitiveCount + 1, startVertex);
			break;
	}
}

void VDTContextD3D11::DrawIndexedPrimitive(VDTPrimitiveType type, uint32 baseVertexIndex, uint32 minVertex, uint32 vertexCount, uint32 startIndex, uint32 primitiveCount) {
	if (mLastPrimitiveType != (int)type) {
		mLastPrimitiveType = (int)type;

		mpD3DDeviceContext->IASetPrimitiveTopology(kPTLookup[type]);
	}

	UpdateConstants();

	uint32 indexCount = 0;
	switch(type) {
		case kVDTPT_Triangles:
			indexCount = primitiveCount * 3;
			break;

		case kVDTPT_TriangleStrip:
			indexCount = primitiveCount + 2;
			break;

		case kVDTPT_Lines:
			indexCount = primitiveCount * 2;
			break;

		case kVDTPT_LineStrip:
			indexCount = primitiveCount + 1;
			break;
	}

	mpD3DDeviceContext->DrawIndexed(indexCount, startIndex, baseVertexIndex);
}

uint32 VDTContextD3D11::InsertFence() {
	return mpData->mFenceManager.InsertFence();
}

bool VDTContextD3D11::CheckFence(uint32 id) {
	return mpData->mFenceManager.CheckFence(id);
}

bool VDTContextD3D11::RecoverDevice() {
	return true;
}

bool VDTContextD3D11::OpenScene() {
	return true;
}

bool VDTContextD3D11::CloseScene() {
	return true;
}

uint32 VDTContextD3D11::GetDeviceLossCounter() const {
	return 0;
}

void VDTContextD3D11::Present() {
	if (!mpSwapChain)
		return;

	mpSwapChain->Present();

	// If the swap chain is in flip mode, it'll unbind the render target, so we need to
	// sync that state.
	if (mpSwapChain->mDesc.mbWindowed)
		SetRenderTarget(0, nullptr, false);
}

void VDTContextD3D11::BeginScope(uint32 color, const char *message) {
	mProfChan.Begin(color, message);

	if (mpD3DAnnotation) {
		WCHAR buf[512];
		int i = 0;

		while(i < 511 && *message)
			buf[i++] = (WCHAR)(unsigned char)*message++;

		buf[i] = 0;
		
		mpD3DAnnotation->BeginEvent(buf);
	}
}

void VDTContextD3D11::EndScope() {
	mProfChan.End();

	if (mpD3DAnnotation)
		mpD3DAnnotation->EndEvent();
}

VDRTProfileChannel *VDTContextD3D11::GetProfileChannel() {
	return &mProfChan;
}

void VDTContextD3D11::UnsetVertexFormat(IVDTVertexFormat *format) {
	if (mpCurrentVF == format)
		SetVertexFormat(NULL);
}

void VDTContextD3D11::UnsetVertexProgram(IVDTVertexProgram *program) {
	if (mpCurrentVP == program)
		SetVertexProgram(NULL);
}

void VDTContextD3D11::UnsetFragmentProgram(IVDTFragmentProgram *program) {
	if (mpCurrentFP == program)
		SetFragmentProgram(NULL);
}

void VDTContextD3D11::UnsetVertexBuffer(IVDTVertexBuffer *buffer) {
	if (mpCurrentVB == buffer)
		SetVertexStream(0, NULL, 0, 0);
}

void VDTContextD3D11::UnsetIndexBuffer(IVDTIndexBuffer *buffer) {
	if (mpCurrentIB == buffer)
		SetIndexStream(NULL);
}

void VDTContextD3D11::UnsetRenderTarget(IVDTSurface *surface) {
	if (mpCurrentRT == surface)
		SetRenderTarget(0, NULL, false);
}

void VDTContextD3D11::UnsetBlendState(IVDTBlendState *state) {
	if (mpCurrentBS == state && state != mpDefaultBS)
		SetBlendState(NULL);
}

void VDTContextD3D11::UnsetRasterizerState(IVDTRasterizerState *state) {
	if (mpCurrentRS == state && state != mpDefaultRS)
		SetRasterizerState(NULL);
}

void VDTContextD3D11::UnsetSamplerState(IVDTSamplerState *state) {
	if (state == mpDefaultSS)
		return;

	for(int i=0; i<16; ++i) {
		if (mpCurrentSamplerStates[i] == state) {
			IVDTSamplerState *ssnull = NULL;
			SetSamplerStates(i, 1, &ssnull);
		}
	}
}

void VDTContextD3D11::UnsetTexture(IVDTTexture *tex) {
	for(int i=0; i<16; ++i) {
		if (mpCurrentTextures[i] == tex) {
			IVDTTexture *tex = NULL;
			SetTextures(i, 1, &tex);
		}
	}
}

void VDTContextD3D11::ProcessHRESULT(uint32 hr) {
}

void VDTContextD3D11::UpdateConstants() {
	if (mbVSConstDirty) {
		mbVSConstDirty = false;

		mpD3DDeviceContext->UpdateSubresource(mpVSConstBuffers[mVSConstShift], 0, nullptr, mVSConsts, 0, 0    );
	}

	if (mbPSConstDirty) {
		mbPSConstDirty = false;

		mpD3DDeviceContext->UpdateSubresource(mpPSConstBuffers[mPSConstShift], 0, nullptr, mPSConsts, 0, 0);
	}
}

///////////////////////////////////////////////////////////////////////////////

bool VDTCreateContextD3D11(IVDTContext **ppctx) {
	vdrefptr<VDD3D11Holder> holder(new VDD3D11Holder);

	if (!holder->Init())
		return false;

	vdrefptr<IDXGIFactory1> factory;
	vdrefptr<IDXGIFactory3> factory3;

	HRESULT hr = E_FAIL;
	if (holder->GetCreateDXGIFactory2Fn()) {
#ifdef _DEBUG
		const DWORD flags = 1;	// DXGI_FACTORY_CREATE_DEBUG
#else
		const DWORD flags = 0;
#endif

		hr = holder->GetCreateDXGIFactory2Fn()(flags, IID_IDXGIFactory3, (void **)~factory3);

		if (factory3)
			factory = factory3;
	}

	if (!factory)
		hr = holder->GetCreateDXGIFactoryFn()(IID_IDXGIFactory1, (void **)~factory);

	if (FAILED(hr))
		return false;

	vdrefptr<IDXGIAdapter1> adapter;
	hr = factory->EnumAdapters1(0, ~adapter);
	if (FAILED(hr))
		return false;

	D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_UNKNOWN;

	vdrefptr<ID3D11Device> dev;
	vdrefptr<ID3D11DeviceContext> devctx;
	UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL actualLevel;
	hr = holder->GetCreateDeviceFn()(adapter, driverType, NULL, flags, NULL, 0, D3D11_SDK_VERSION, ~dev, &actualLevel, ~devctx);
	if (FAILED(hr)) {
#ifdef _DEBUG
		// if we're in debug mode, try again without it
		flags &= ~D3D11_CREATE_DEVICE_DEBUG;
		hr = holder->GetCreateDeviceFn()(adapter, driverType, NULL, flags, NULL, 0, D3D11_SDK_VERSION, ~dev, &actualLevel, ~devctx);

		if (FAILED(hr))
			return false;
#else
		return false;
#endif
	}

	vdrefptr<VDTContextD3D11> ctx(new VDTContextD3D11);
	if (!ctx->Init(dev, devctx, adapter, factory, holder))
		return false;

	*ppctx = ctx.release();
	return true;
}

bool VDTCreateContextD3D11(int width, int height, int refresh, bool fullscreen, bool vsync, void *hwnd, IVDTContext **ppctx) {
	
	vdrefptr<IVDTContext> ctx;
	if (!VDTCreateContextD3D11(~ctx))
		return false;

	VDTSwapChainDesc desc = {};
	desc.mWidth = width;
	desc.mHeight = height;
	desc.mhWindow = hwnd;

	vdrefptr<IVDTSwapChain> sc;
	if (!ctx->CreateSwapChain(desc, ~sc))
		return false;

	static_cast<VDTContextD3D11 *>(&*ctx)->SetImplicitSwapChain(static_cast<VDTSwapChainD3D11 *>(&*sc));

	*ppctx = ctx.release();
	return true;
}

bool VDTCreateContextD3D11(ID3D11Device *dev, ID3D11DeviceContext *devctx, IDXGIFactory *factory, IVDTContext **ppctx) {
	vdrefptr<IDXGIDevice> dxgiDev;
	HRESULT hr = dev->QueryInterface(IID_IDXGIDevice, (void **)~dxgiDev);

	if (FAILED(hr))
		return false;

	vdrefptr<IDXGIAdapter> adapter;
	hr = dxgiDev->GetAdapter(~adapter);
	if (FAILED(hr))
		return false;

	vdrefptr<IDXGIAdapter1> adapter1;
	hr = adapter->QueryInterface(IID_IDXGIAdapter1, (void **)~adapter1);
	if (FAILED(hr))
		return false;

	vdrefptr<VDTContextD3D11> ctx(new VDTContextD3D11);

	if (!ctx->Init(dev, devctx, adapter1, factory, NULL))
		return false;

	*ppctx = ctx.release();
	return true;
}
