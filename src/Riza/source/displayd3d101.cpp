//	VirtualDub - Video processing and capture application
//	A/V interface library
//	Copyright (C) 1998-2011 Avery Lee
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

#include <dxgi.h>
#include <d3d10_1.h>
#include <d3d10.h>
#include <vd2/system/refcount.h>
#include <vd2/system/w32assist.h>
#include <vd2/Kasumi/pixmapops.h>
#include "displaydrv.h"
#include "displayd3d10_vertexshader.inl"
#include "displayd3d10_pixelshader.inl"

//////////////////////////////////////////////////////////////////////////////

class VDD3D101Manager {
	VDD3D101Manager(const VDD3D101Manager&);
	VDD3D101Manager& operator=(const VDD3D101Manager&);
public:
	VDD3D101Manager();
	~VDD3D101Manager();

	ID3D10Device1 *GetD3DDevice() const { return mpD3DDevice; }
	IDXGIFactory *GetDXGIFactory() const { return mpDXGIFactory; }

	bool Init();
	void Shutdown();

protected:
	ID3D10Device1 *mpD3DDevice;
	IDXGIFactory *mpDXGIFactory;
	HMODULE mhmodD3D10;
	HMODULE mhmodDXGI;
};

VDD3D101Manager::VDD3D101Manager()
	: mpD3DDevice(NULL)
	, mpDXGIFactory(NULL)
	, mhmodD3D10(NULL)
	, mhmodDXGI(NULL)
{
}

VDD3D101Manager::~VDD3D101Manager() {
}

bool VDD3D101Manager::Init() {
	typedef HRESULT (WINAPI *tpD3D10CreateDevice1)(
	  IDXGIAdapter *pAdapter,
	  D3D10_DRIVER_TYPE DriverType,
	  HMODULE Software,
	  UINT Flags,
	  D3D10_FEATURE_LEVEL1 HardwareLevel,
	  UINT SDKVersion,
	  ID3D10Device1 **ppDevice
	);

	typedef HRESULT (WINAPI *tpCreateDXGIFactory1)(
	  REFIID riid,
	  void **ppFactory
	);

	mhmodDXGI = VDLoadSystemLibraryW32("DXGI.dll");
	if (!mhmodDXGI) {
		Shutdown();
		return false;
	}

	tpCreateDXGIFactory1 pCreateDXGIFactory1 = (tpCreateDXGIFactory1)GetProcAddress(mhmodDXGI, "CreateDXGIFactory1");
	if (!pCreateDXGIFactory1) {
		Shutdown();
		return false;
	}

#if 0
	HRESULT hr = pCreateDXGIFactory1(IID_IDXGIFactory1, (void **)&mpDXGIFactory);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	vdrefptr<IDXGIAdapter> adapter;
	hr = mpDXGIFactory->EnumAdapters(0, ~adapter);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}
#endif

	mhmodD3D10 = VDLoadSystemLibraryW32("D3D10_1.dll");
	if (!mhmodD3D10) {
		Shutdown();
		return false;
	}

	tpD3D10CreateDevice1 pD3D10CreateDevice1 = (tpD3D10CreateDevice1)GetProcAddress(mhmodD3D10, "D3D10CreateDevice1");
	if (!pD3D10CreateDevice1) {
		Shutdown();
		return false;
	}

	HRESULT hr = pD3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION, &mpD3DDevice);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	vdrefptr<IDXGIDevice> device;
	hr = mpD3DDevice->QueryInterface(IID_IDXGIDevice, (void **)~device);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	vdrefptr<IDXGIAdapter> adapter;
	hr = device->GetAdapter(~adapter);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	hr = adapter->GetParent(IID_IDXGIFactory, (void **)&mpDXGIFactory);
	if (FAILED(hr)) {
		Shutdown();
		return false;
	}

	return true;
}

void VDD3D101Manager::Shutdown() {
	if (mpD3DDevice) {
		mpD3DDevice->Release();
		mpD3DDevice = NULL;
	}

	if (mhmodD3D10) {
		FreeLibrary(mhmodD3D10);
		mhmodD3D10 = NULL;
	}

	if (mpDXGIFactory) {
		mpDXGIFactory->Release();
		mpDXGIFactory = NULL;
	}

	if (mhmodDXGI) {
		FreeLibrary(mhmodDXGI);
		mhmodDXGI = NULL;
	}
}

//////////////////////////////////////////////////////////////////////////////

class VDVideoDisplayMinidriverD3D101 : public VDVideoDisplayMinidriver {
public:
	VDVideoDisplayMinidriverD3D101();

	virtual bool Init(HWND hwnd, HMONITOR hmonitor, const VDVideoDisplaySourceInfo& info);
	virtual void Shutdown();

	virtual bool ModifySource(const VDVideoDisplaySourceInfo& info);

	virtual bool IsValid();

	virtual bool Resize(int w, int h);

	virtual bool Update(UpdateMode);
	virtual void Refresh(UpdateMode);
	virtual bool Paint(HDC hdc, const RECT& rClient, UpdateMode lastUpdateMode);

protected:
	struct Vertex {
		float x, y;
	};

	bool Render();
	bool Present(bool vsync);

	bool InitStateObjects();
	void ShutdownStateObjects();

	bool InitSourceTexture(const VDVideoDisplaySourceInfo& info);
	bool UpdateSourceTexture();
	void ShutdownSourceTexture();

	bool InitSwapChain();
	bool ResizeSwapChain();
	void ShutdownSwapChain();

	bool InitSwapChainBuffers();
	void ShutdownSwapChainBuffers();

	HWND mhwnd;

	VDD3D101Manager *mpD3DManager;
	vdrefptr<IDXGISwapChain> mpDXGISwapChain;
	vdrefptr<ID3D10Texture2D> mpD3DBackBuffer;
	vdrefptr<ID3D10RenderTargetView> mpD3DRTView;
	vdrefptr<ID3D10Texture2D> mpD3DSrcTex;
	vdrefptr<ID3D10ShaderResourceView> mpD3DSrcTexSRV;
	vdrefptr<ID3D10RasterizerState> mpD3DRasterizerState;
	vdrefptr<ID3D10SamplerState> mpD3DSamplerState;
	vdrefptr<ID3D10BlendState> mpD3DBlendState;
	vdrefptr<ID3D10Buffer> mpD3DVertexBuffer;
	vdrefptr<ID3D10InputLayout> mpD3DInputLayout;
	vdrefptr<ID3D10VertexShader> mpD3DVertexShader;
	vdrefptr<ID3D10PixelShader> mpD3DPixelShader;

	VDVideoDisplaySourceInfo mSource;
	D3D10_VIEWPORT mD3DRTViewport;
};

IVDVideoDisplayMinidriver *VDCreateVideoDisplayMinidriverD3D101() {
	return new VDVideoDisplayMinidriverD3D101;
}

VDVideoDisplayMinidriverD3D101::VDVideoDisplayMinidriverD3D101()
	: mhwnd(NULL)
	, mpD3DManager(NULL)
{
}

bool VDVideoDisplayMinidriverD3D101::Init(HWND hwnd, HMONITOR hmonitor, const VDVideoDisplaySourceInfo& info) {
	mhwnd = hwnd;

	mpD3DManager = new VDD3D101Manager;
	if (!mpD3DManager->Init()) {
		Shutdown();
		return false;
	}

	if (!InitStateObjects()) {
		Shutdown();
		return false;
	}

	if (!InitSwapChain()) {
		Shutdown();
		return false;
	}

	if (!InitSourceTexture(info)) {
		Shutdown();
		return false;
	}

	mSource = info;
	return true;
}

void VDVideoDisplayMinidriverD3D101::Shutdown() {
	ShutdownSwapChain();
	ShutdownSourceTexture();
	ShutdownStateObjects();

	if (mpD3DManager) {
		mpD3DManager->Shutdown();
		delete mpD3DManager;
		mpD3DManager = NULL;
	}

	mhwnd = NULL;
}

bool VDVideoDisplayMinidriverD3D101::ModifySource(const VDVideoDisplaySourceInfo& info) {
	mSource = info;
	return true;
}

bool VDVideoDisplayMinidriverD3D101::IsValid() {
	return true;
}

bool VDVideoDisplayMinidriverD3D101::Resize(int w, int h) {
	if (!mpD3DManager)
		return true;

	return ResizeSwapChain();
}

bool VDVideoDisplayMinidriverD3D101::Update(UpdateMode updateMode) {
	if (!UpdateSourceTexture())
		return false;

	return true;
}

void VDVideoDisplayMinidriverD3D101::Refresh(UpdateMode updateMode) {
	HDC hdc = GetDC(mhwnd);
	if (hdc) {
		RECT r;
		GetClientRect(mhwnd, &r);
		Paint(hdc, r, updateMode);
		ReleaseDC(mhwnd, hdc);
	}
}

bool VDVideoDisplayMinidriverD3D101::Paint(HDC hdc, const RECT& rClient, UpdateMode lastUpdateMode) {
	return Render() && Present((lastUpdateMode & kModeVSync) != 0);
}

bool VDVideoDisplayMinidriverD3D101::Render() {
	ID3D10Device1 *pD3DDevice = mpD3DManager->GetD3DDevice();
	
	pD3DDevice->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	pD3DDevice->IASetInputLayout(mpD3DInputLayout);
	ID3D10Buffer *pVB = mpD3DVertexBuffer;
	UINT pVBStride = sizeof(Vertex);
	UINT pVBOffset = 0;
	pD3DDevice->IASetVertexBuffers(0, 1, &pVB, &pVBStride, &pVBOffset);

	// set up vertex shader
	pD3DDevice->VSSetShader(mpD3DVertexShader);

	// set up pixel shader
	ID3D10SamplerState *pSS = mpD3DSamplerState;
	pD3DDevice->PSSetSamplers(0, 1, &pSS);
	ID3D10ShaderResourceView *pSRVSrc = mpD3DSrcTexSRV;
	pD3DDevice->PSSetShaderResources(0, 1, &pSRVSrc);
	pD3DDevice->PSSetShader(mpD3DPixelShader);

	// set up rasterizer
	pD3DDevice->RSSetViewports(1, &mD3DRTViewport);
	pD3DDevice->RSSetState(mpD3DRasterizerState);

	// set up output
	float factors[4] = {0};
	pD3DDevice->OMSetBlendState(mpD3DBlendState, factors, 0xffffffff);
	ID3D10RenderTargetView *rtv = mpD3DRTView;
	pD3DDevice->OMSetRenderTargets(1, &rtv, NULL);

	// draw
	pD3DDevice->Draw(4, 0);

	// done
	pD3DDevice->ClearState();
	return true;
}

bool VDVideoDisplayMinidriverD3D101::Present(bool vsync) {
	HRESULT hr = mpDXGISwapChain->Present(vsync ? 1 : 0, DXGI_PRESENT_RESTART);

	return SUCCEEDED(hr);
}

bool VDVideoDisplayMinidriverD3D101::InitStateObjects() {
	ID3D10Device1 *pD3DDevice = mpD3DManager->GetD3DDevice();

	D3D10_RASTERIZER_DESC rasdesc;
	rasdesc.FillMode = D3D10_FILL_SOLID;
	rasdesc.CullMode = D3D10_CULL_NONE;
	rasdesc.FrontCounterClockwise = TRUE;
	rasdesc.DepthBias = 0;
	rasdesc.DepthBiasClamp = 0;
	rasdesc.SlopeScaledDepthBias = 0;
	rasdesc.DepthClipEnable = TRUE;
	rasdesc.ScissorEnable = FALSE;
	rasdesc.MultisampleEnable = FALSE;
	rasdesc.AntialiasedLineEnable = FALSE;
	HRESULT hr = pD3DDevice->CreateRasterizerState(&rasdesc, ~mpD3DRasterizerState);
	if (FAILED(hr))
		return false;

	D3D10_SAMPLER_DESC sampdesc;
	sampdesc.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
	sampdesc.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
	sampdesc.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
	sampdesc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
	sampdesc.MipLODBias = 0;
	sampdesc.MaxAnisotropy = 1;
	sampdesc.ComparisonFunc = D3D10_COMPARISON_ALWAYS;
	sampdesc.BorderColor[0] = 0;
	sampdesc.BorderColor[1] = 0;
	sampdesc.BorderColor[2] = 0;
	sampdesc.BorderColor[3] = 0;
	sampdesc.MinLOD = 0;
	sampdesc.MaxLOD = D3D10_FLOAT32_MAX;
	hr = pD3DDevice->CreateSamplerState(&sampdesc, ~mpD3DSamplerState);
	if (FAILED(hr)) {
		ShutdownStateObjects();
		return false;
	}

	D3D10_BLEND_DESC blenddesc = {};
	blenddesc.SrcBlend = D3D10_BLEND_ONE;
	blenddesc.SrcBlendAlpha = D3D10_BLEND_ONE;
	blenddesc.DestBlend = D3D10_BLEND_ONE;
	blenddesc.DestBlendAlpha = D3D10_BLEND_ONE;
	blenddesc.BlendOp = D3D10_BLEND_OP_ADD;
	blenddesc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
	blenddesc.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
	hr = pD3DDevice->CreateBlendState(&blenddesc, ~mpD3DBlendState);
	if (FAILED(hr)) {
		ShutdownStateObjects();
		return false;
	}

	D3D10_BUFFER_DESC vbdesc;
	vbdesc.ByteWidth = sizeof(Vertex) * 4;
	vbdesc.Usage = D3D10_USAGE_DEFAULT;
	vbdesc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
	vbdesc.CPUAccessFlags = 0;
	vbdesc.MiscFlags = 0;

	static const Vertex kVBData[4]={
		{0,0},
		{0,1},
		{1,0},
		{1,1},
	};

	D3D10_SUBRESOURCE_DATA vbdata;
	vbdata.pSysMem = kVBData;
	vbdata.SysMemPitch = 0;
	vbdata.SysMemSlicePitch = 0;

	hr = pD3DDevice->CreateBuffer(&vbdesc, &vbdata, ~mpD3DVertexBuffer);
	if (FAILED(hr)) {
		ShutdownStateObjects();
		return false;
	}

	hr = pD3DDevice->CreateVertexShader(g_VDD3D10_VertexShader, sizeof g_VDD3D10_VertexShader, ~mpD3DVertexShader);
	if (FAILED(hr)) {
		ShutdownStateObjects();
		return false;
	}

	D3D10_INPUT_ELEMENT_DESC kElementDescs[]={
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
	};

	hr = pD3DDevice->CreateInputLayout(kElementDescs, 1, g_VDD3D10_VertexShader, sizeof g_VDD3D10_VertexShader, ~mpD3DInputLayout);
	if (FAILED(hr)) {
		ShutdownStateObjects();
		return false;
	}

	hr = pD3DDevice->CreatePixelShader(g_VDD3D10_PixelShader, sizeof g_VDD3D10_PixelShader, ~mpD3DPixelShader);
	if (FAILED(hr)) {
		ShutdownStateObjects();
		return false;
	}

	return true;
}

void VDVideoDisplayMinidriverD3D101::ShutdownStateObjects() {
	mpD3DPixelShader.clear();
	mpD3DInputLayout.clear();
	mpD3DVertexShader.clear();
	mpD3DVertexBuffer.clear();
	mpD3DBlendState.clear();
	mpD3DSamplerState.clear();
	mpD3DRasterizerState.clear();
	mpD3DVertexBuffer.clear();
}

bool VDVideoDisplayMinidriverD3D101::InitSourceTexture(const VDVideoDisplaySourceInfo& info) {
	ID3D10Device1 *pD3DDevice = mpD3DManager->GetD3DDevice();
	
	D3D10_TEXTURE2D_DESC texdesc;
	texdesc.Width = info.pixmap.w;
	texdesc.Height = info.pixmap.h;
	texdesc.MipLevels = 1;
	texdesc.ArraySize = 1;
	texdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texdesc.SampleDesc.Count = 1;
	texdesc.SampleDesc.Quality = 0;
	texdesc.Usage = D3D10_USAGE_DYNAMIC;
	texdesc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
	texdesc.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
	texdesc.MiscFlags = 0;
	HRESULT hr = pD3DDevice->CreateTexture2D(&texdesc, NULL, ~mpD3DSrcTex);

	if (FAILED(hr))
		return false;

	D3D10_SHADER_RESOURCE_VIEW_DESC srvdesc;
	srvdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvdesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
	srvdesc.Texture2D.MostDetailedMip = 0;
	srvdesc.Texture2D.MipLevels = 1;

	hr = pD3DDevice->CreateShaderResourceView(mpD3DSrcTex, &srvdesc, ~mpD3DSrcTexSRV);
	if (FAILED(hr)) {
		ShutdownSourceTexture();
		return false;
	}

	return true;
}

bool VDVideoDisplayMinidriverD3D101::UpdateSourceTexture() {
	D3D10_MAPPED_TEXTURE2D mt;
	HRESULT hr = mpD3DSrcTex->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &mt);
	if (FAILED(hr))
		return false;

	VDPixmap pxtex;
	pxtex.data = mt.pData;
	pxtex.pitch = mt.RowPitch;
	pxtex.w = mSource.pixmap.w;
	pxtex.h = mSource.pixmap.h;
	pxtex.format = nsVDPixmap::kPixFormat_XRGB8888;
	VDPixmapBlt(pxtex, mSource.pixmap);

	mpD3DSrcTex->Unmap(0);
	return true;
}

void VDVideoDisplayMinidriverD3D101::ShutdownSourceTexture() {
	mpD3DSrcTexSRV = NULL;
	mpD3DSrcTex = NULL;
}

bool VDVideoDisplayMinidriverD3D101::InitSwapChain() {
	ID3D10Device1 *pD3DDevice = mpD3DManager->GetD3DDevice();
	IDXGIFactory *pDXGIFactory = mpD3DManager->GetDXGIFactory();

	DXGI_SWAP_CHAIN_DESC desc;
	desc.BufferDesc.Width = 0;
	desc.BufferDesc.Height = 0;
	desc.BufferDesc.RefreshRate.Numerator = 0;
	desc.BufferDesc.RefreshRate.Denominator = 0;
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.BufferUsage = DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.BufferCount = 3;
	desc.OutputWindow = mhwnd;
	desc.Windowed = TRUE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	desc.Flags = 0;
	HRESULT hr = pDXGIFactory->CreateSwapChain(pD3DDevice, &desc, ~mpDXGISwapChain);

	if (FAILED(hr))
		return false;

	return InitSwapChainBuffers();
}

bool VDVideoDisplayMinidriverD3D101::ResizeSwapChain() {
	if (!mpDXGISwapChain)
		return InitSwapChain();

	ShutdownSwapChainBuffers();

	HRESULT hr = mpDXGISwapChain->ResizeBuffers(2, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	if (FAILED(hr)) {
		ShutdownSwapChain();
		return false;
	}

	return InitSwapChainBuffers();
}

void VDVideoDisplayMinidriverD3D101::ShutdownSwapChain() {
	ShutdownSwapChainBuffers();

	mpDXGISwapChain = NULL;
}

bool VDVideoDisplayMinidriverD3D101::InitSwapChainBuffers() {
	ID3D10Device1 *pD3DDevice = mpD3DManager->GetD3DDevice();

	HRESULT hr = mpDXGISwapChain->GetBuffer(0, IID_ID3D10Texture2D, (void **)~mpD3DBackBuffer);
	if (FAILED(hr)) {
		ShutdownSwapChain();
		return false;
	}

	D3D10_RENDER_TARGET_VIEW_DESC rtvdesc;
	rtvdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvdesc.ViewDimension = D3D10_RTV_DIMENSION_TEXTURE2D;
	rtvdesc.Texture2D.MipSlice = 0;

	hr = pD3DDevice->CreateRenderTargetView(mpD3DBackBuffer, &rtvdesc, ~mpD3DRTView);
	if (FAILED(hr)) {
		ShutdownSwapChain();
		return false;
	}

	ID3D10RenderTargetView *rtv = mpD3DRTView;
	pD3DDevice->OMSetRenderTargets(1, &rtv, NULL);

	D3D10_TEXTURE2D_DESC texdesc;
	mpD3DBackBuffer->GetDesc(&texdesc);
	mD3DRTViewport.TopLeftX = 0;
	mD3DRTViewport.TopLeftY = 0;
	mD3DRTViewport.Width = texdesc.Width;
	mD3DRTViewport.Height = texdesc.Height;
	mD3DRTViewport.MinDepth = 0;
	mD3DRTViewport.MaxDepth = 1;
	return true;
}

void VDVideoDisplayMinidriverD3D101::ShutdownSwapChainBuffers() {
	mpD3DRTView = NULL;
	mpD3DBackBuffer = NULL;
}

