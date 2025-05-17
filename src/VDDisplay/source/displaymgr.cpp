//	VirtualDub - Video processing and capture application
//	A/V interface library
//	Copyright (C) 1998-2007 Avery Lee
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

#include <windows.h>
#include <mmsystem.h>
#include <vd2/system/hash.h>
#include <vd2/system/w32assist.h>
#include "displaymgr.h"

///////////////////////////////////////////////////////////////////////////
VDVideoDisplayClient::VDVideoDisplayClient()
	: mpManager(NULL)
	, mbPreciseMode(false)
	, mbTicksEnabled(false)
	, mbRequiresFullScreen(false)
{
}

VDVideoDisplayClient::~VDVideoDisplayClient() {
}

void VDVideoDisplayClient::Attach(VDVideoDisplayManager *pManager) {
	VDASSERT(!mpManager);
	mpManager = pManager;
	if (mbTicksEnabled)
		mpManager->ModifyTicksEnabled(true);
	if (mbPreciseMode)
		mpManager->ModifyPreciseMode(true);
}

void VDVideoDisplayClient::Detach(VDVideoDisplayManager *pManager) {
	VDASSERT(mpManager == pManager);
	if (mbPreciseMode)
		mpManager->ModifyPreciseMode(false);
	if (mbTicksEnabled)
		mpManager->ModifyTicksEnabled(false);
	mpManager = NULL;
}

void VDVideoDisplayClient::SetPreciseMode(bool enabled) {
	if (mbPreciseMode == enabled) {
		if (enabled)
			mpManager->ReaffirmPreciseMode();
		return;
	}

	mbPreciseMode = enabled;
	mpManager->ModifyPreciseMode(enabled);
}

void VDVideoDisplayClient::SetTicksEnabled(bool enabled) {
	if (mbTicksEnabled == enabled)
		return;

	mbTicksEnabled = enabled;
	mpManager->ModifyTicksEnabled(enabled);
}

void VDVideoDisplayClient::SetRequiresFullScreen(bool enabled) {
	if (mbRequiresFullScreen == enabled)
		return;

	mbRequiresFullScreen = enabled;
}

const uint8 *VDVideoDisplayClient::GetLogicalPalette() const {
	return mpManager->GetLogicalPalette();
}

HPALETTE VDVideoDisplayClient::GetPalette() const {
	return mpManager->GetPalette();
}

void VDVideoDisplayClient::RemapPalette() {
	mpManager->RemapPalette();
}

///////////////////////////////////////////////////////////////////////////

VDVideoDisplayManager::VDVideoDisplayManager()
	: VDThread("Video display manager")
	, mTicksEnabledCount(0)
	, mPreciseModeCount(0)
	, mPreciseModePeriod(0)
	, mPreciseModeLastUse(0)
	, mhPalette(NULL)
	, mWndClass(NULL)
	, mhwnd(NULL)
	, mbAppActive(false)
	, mThreadID(0)
	, mOutstandingTicks(0)
{
}

VDVideoDisplayManager::~VDVideoDisplayManager() {
	Shutdown();
}

bool VDVideoDisplayManager::Init() {
	mbAppActive = VDIsForegroundTaskW32();

	if (!RegisterWindowClass()) {
		Shutdown();
		return false;
	}

	mhwnd = CreateWindowEx(WS_EX_NOPARENTNOTIFY, MAKEINTATOM(mWndClass), L"", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, VDGetLocalModuleHandleW32(), this);
	if (!mhwnd) {
		Shutdown();
		return false;
	}

	mThreadID = VDGetCurrentThreadID();

	if (!ThreadStart()) {
		Shutdown();
		return false;
	}

	mStarted.wait();

	return true;
}

void VDVideoDisplayManager::Shutdown() {
	VDASSERT(mClients.empty());

	if (isThreadAttached()) {
		PostThreadMessage(getThreadID(), WM_QUIT, 0, 0);
		ThreadWait();
	}

	if (mhwnd) {
		DestroyWindow(mhwnd);
		mhwnd = NULL;
	}

	UnregisterWindowClass();
	mThreadID = 0;
}

void VDVideoDisplayManager::SetProfileHook(const vdfunction<void(IVDVideoDisplay::ProfileEvent)>& profileHook) {
	mpProfileHook = profileHook;
}

void VDVideoDisplayManager::AddClient(VDVideoDisplayClient *pClient) {
	VDASSERT(VDGetCurrentThreadID() == mThreadID);
	mClients.push_back(pClient);
	pClient->Attach(this);
}

void VDVideoDisplayManager::RemoveClient(VDVideoDisplayClient *pClient) {
	VDASSERT(VDGetCurrentThreadID() == mThreadID);
	pClient->Detach(this);
	mClients.erase(mClients.fast_find(pClient));
}

void VDVideoDisplayManager::ModifyPreciseMode(bool enabled) {
	VDASSERT(VDGetCurrentThreadID() == mThreadID);
}

void VDVideoDisplayManager::ModifyTicksEnabled(bool enabled) {
	VDASSERT(VDGetCurrentThreadID() == mThreadID);
	if (enabled) {
		int rc = ++mTicksEnabledCount;
		VDASSERT(rc < 100000);

		if (rc == 1) {
			PostThreadMessage(getThreadID(), WM_NULL, 0, 0);
			mTickTimerId = SetTimer(mhwnd, kTimerID_Tick, 10, NULL);
		}
	} else {
		int rc = --mTicksEnabledCount;
		VDASSERT(rc >= 0);

		if (!rc) {
			if (mTickTimerId) {
				KillTimer(mhwnd, mTickTimerId);
				mTickTimerId = 0;
			}
		}
	}
}

void VDVideoDisplayManager::NotifyGlobalSettingsChanged() {
	for(VDVideoDisplayClient *p : mClients)
		p->OnGlobalSettingsChanged();
}

float VDVideoDisplayManager::GetSystemSDRBrightness(HMONITOR monitor) {
	return GetMonitorHDRInfo(monitor).mSDRLevel;
}

bool VDVideoDisplayManager::IsMonitorHDRCapable(HMONITOR monitor) {
	return GetMonitorHDRInfo(monitor).mbHDRCapable;
}

const VDVideoDisplayManager::MonitorHDRInfo& VDVideoDisplayManager::GetMonitorHDRInfo(HMONITOR monitor) {
	auto it = std::find_if(mMonitorHDRInfoCache.begin(), mMonitorHDRInfoCache.end(),
		[=](const MonitorHDRInfoByHandle& info) { return info.mhMonitor == monitor; });

	if (it != mMonitorHDRInfoCache.end())
		return it->mInfo;

	UpdateDisplayInfoCache();

	MONITORINFOEXW info {};
	info.cbSize = sizeof(MONITORINFOEXW);
	
	auto& monent = mMonitorHDRInfoCache.push_back();
	monent.mhMonitor = monitor;

	if (GetMonitorInfoW(monitor, &info)) {
		uint32 pathHash = VDHashString32I(info.szDevice);

		for(const MonitorHDRInfoByPath& mipath : mMonitorHDRInfoCacheDC) {
			if (mipath.mPathHash == pathHash) {
				monent.mInfo = mipath.mInfo;
				break;
			}
		}
	}

	return monent.mInfo;
}

void VDVideoDisplayManager::UpdateDisplayInfoCache() {
	if (!mbMonitorHDRInfoCacheDCValid) {
		mbMonitorHDRInfoCacheDCValid = true;

		vdfastvector<DISPLAYCONFIG_PATH_INFO> paths;
		vdfastvector<DISPLAYCONFIG_MODE_INFO> modes;
		UINT32 numPaths = 0;
		UINT32 numModes = 0;

		for(int tries = 0; tries < 100; ++tries) {
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
			DISPLAYCONFIG_SOURCE_DEVICE_NAME devname {};
			devname.header.adapterId = path.sourceInfo.adapterId;
			devname.header.id = path.sourceInfo.id;
			devname.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
			devname.header.size = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME);

			if (ERROR_SUCCESS != DisplayConfigGetDeviceInfo(&devname.header))
				continue;

			const uint32 hash = VDHashString32I(devname.viewGdiDeviceName);

			auto& ent = mMonitorHDRInfoCacheDC.push_back();
			ent.mPathHash = hash;

			DISPLAYCONFIG_SDR_WHITE_LEVEL devlevel {};
			devlevel.header.adapterId = path.targetInfo.adapterId;
			devlevel.header.id = path.targetInfo.id;
			devlevel.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
			devlevel.header.size = sizeof(DISPLAYCONFIG_SDR_WHITE_LEVEL);
			
			if (ERROR_SUCCESS == DisplayConfigGetDeviceInfo(&devlevel.header)) {
				// The returned white level is in units of 80/1000 nits, or 0.001x the
				// scRGB framebuffer value.
				ent.mInfo.mSDRLevel = (float)devlevel.SDRWhiteLevel / 1000.0f * 80;
			}

			DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO getinfo {};
			getinfo.header.adapterId = path.targetInfo.adapterId;
			getinfo.header.id = path.targetInfo.id;
			getinfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
			getinfo.header.size = sizeof(DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO);

			if (ERROR_SUCCESS == DisplayConfigGetDeviceInfo(&getinfo.header)) {
				ent.mInfo.mbHDRCapable = getinfo.advancedColorSupported;
				ent.mInfo.mbHDREnabled = getinfo.advancedColorEnabled;
			}
		}
	}
}

void VDVideoDisplayManager::ThreadRun() {
	ThreadRunTimerOnly();
}

void VDVideoDisplayManager::ThreadRunTimerOnly() {
	MSG msg;
	PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);
	mStarted.signal();

	bool precise = false;
	for(;;) {
		uint32 timeSinceLastPrecise = ::GetTickCount() - mPreciseModeLastUse;

		if (precise) {
			if (timeSinceLastPrecise > 1000) {
				precise = false;
				ExitPreciseMode();
			}
		} else {
			if (timeSinceLastPrecise < 500) {
				precise = true;
				EnterPreciseMode();
			}
		}

		DWORD ret = MsgWaitForMultipleObjects(0, NULL, TRUE, 1, QS_ALLINPUT);

		if (ret == WAIT_OBJECT_0) {
			bool success = false;
			while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT)
					return;
				success = true;
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			if (success)
				continue;

			ret = WAIT_TIMEOUT;
			::Sleep(1);
		}
		
		if (ret == WAIT_TIMEOUT) {
			if (mTicksEnabledCount > 0)
				PostTick();
			else
				WaitMessage();
		} else
			break;
	}

	if (precise)
		ExitPreciseMode();
}

void VDVideoDisplayManager::DispatchTicks() {
	if (mpProfileHook)
		mpProfileHook(IVDVideoDisplay::kProfileEvent_BeginTick);

	Clients::iterator it(mClients.begin()), itEnd(mClients.end());
	for(; it!=itEnd; ++it) {
		VDVideoDisplayClient *pClient = *it;

		if (pClient->mbTicksEnabled)
			pClient->OnTick();
	}

	if (mpProfileHook)
		mpProfileHook(IVDVideoDisplay::kProfileEvent_EndTick);
}

void VDVideoDisplayManager::PostTick() {
	if (!mOutstandingTicks.xchg(1)) {
		PostMessage(mhwnd, WM_TIMER, kTimerID_Tick, 0);
	}
}

void VDVideoDisplayManager::EnterPreciseMode() {
	TIMECAPS tc;
	if (!mPreciseModePeriod &&
		TIMERR_NOERROR == ::timeGetDevCaps(&tc, sizeof tc) &&
		TIMERR_NOERROR == ::timeBeginPeriod(tc.wPeriodMin))
	{
		mPreciseModePeriod = tc.wPeriodMin;
		SetThreadPriority(getThreadHandle(), THREAD_PRIORITY_HIGHEST);
	}
}

void VDVideoDisplayManager::ExitPreciseMode() {
	if (mPreciseModePeriod) {
		timeEndPeriod(mPreciseModePeriod);
		mPreciseModePeriod = 0;
	}
}

void VDVideoDisplayManager::ReaffirmPreciseMode() {
	mPreciseModeLastUse = ::GetTickCount();
}

///////////////////////////////////////////////////////////////////////////////

bool VDVideoDisplayManager::RegisterWindowClass() {
	WNDCLASS wc;
	HMODULE hInst = VDGetLocalModuleHandleW32();

	wc.style			= 0;
	wc.lpfnWndProc		= StaticWndProc;
	wc.cbClsExtra		= 0;
	wc.cbWndExtra		= sizeof(VDVideoDisplayManager *);
	wc.hInstance		= hInst;
	wc.hIcon			= 0;
	wc.hCursor			= 0;
	wc.hbrBackground	= 0;
	wc.lpszMenuName		= 0;

	wchar_t buf[64];
	swprintf(buf, vdcountof(buf), L"VDVideoDisplayManager(%p)", this);
	wc.lpszClassName	= buf;

	mWndClass = RegisterClass(&wc);

	return mWndClass != NULL;
}

void VDVideoDisplayManager::UnregisterWindowClass() {
	if (mWndClass) {
		HMODULE hInst = VDGetLocalModuleHandleW32();
		UnregisterClass(MAKEINTATOM(mWndClass), hInst);
		mWndClass = NULL;
	}
}

void VDVideoDisplayManager::RemapPalette() {
	PALETTEENTRY pal[216];
	struct {
		LOGPALETTE hdr;
		PALETTEENTRY palext[255];
	} physpal;

	physpal.hdr.palVersion = 0x0300;
	physpal.hdr.palNumEntries = 256;

	int i;

	for(i=0; i<216; ++i) {
		pal[i].peRed	= (BYTE)((i / 36) * 51);
		pal[i].peGreen	= (BYTE)(((i%36) / 6) * 51);
		pal[i].peBlue	= (BYTE)((i%6) * 51);
	}

	for(i=0; i<256; ++i) {
		physpal.hdr.palPalEntry[i].peRed	= 0;
		physpal.hdr.palPalEntry[i].peGreen	= 0;
		physpal.hdr.palPalEntry[i].peBlue	= (BYTE)i;
		physpal.hdr.palPalEntry[i].peFlags	= PC_EXPLICIT;
	}

	if (HDC hdc = GetDC(0)) {
		GetSystemPaletteEntries(hdc, 0, 256, physpal.hdr.palPalEntry);
		ReleaseDC(0, hdc);
	}

	if (HPALETTE hpal = CreatePalette(&physpal.hdr)) {
		for(i=0; i<216; ++i) {
			mLogicalPalette[i] = (uint8)GetNearestPaletteIndex(hpal, RGB(pal[i].peRed, pal[i].peGreen, pal[i].peBlue));
		}

		DeleteObject(hpal);
	}
}

bool VDVideoDisplayManager::IsDisplayPaletted() {
	bool bPaletted = false;

	if (HDC hdc = GetDC(0)) {
		if (GetDeviceCaps(hdc, BITSPIXEL) <= 8)		// RC_PALETTE doesn't seem to be set if you switch to 8-bit in Win98 without rebooting.
			bPaletted = true;
		ReleaseDC(0, hdc);
	}

	return bPaletted;
}

void VDVideoDisplayManager::CreateDitheringPalette() {
	if (mhPalette)
		return;

	struct {
		LOGPALETTE hdr;
		PALETTEENTRY palext[255];
	} pal;

	pal.hdr.palVersion = 0x0300;
	pal.hdr.palNumEntries = 216;

	for(int i=0; i<216; ++i) {
		pal.hdr.palPalEntry[i].peRed	= (BYTE)((i / 36) * 51);
		pal.hdr.palPalEntry[i].peGreen	= (BYTE)(((i%36) / 6) * 51);
		pal.hdr.palPalEntry[i].peBlue	= (BYTE)((i%6) * 51);
		pal.hdr.palPalEntry[i].peFlags	= 0;
	}

	mhPalette = CreatePalette(&pal.hdr);
}

void VDVideoDisplayManager::DestroyDitheringPalette() {
	if (mhPalette) {
		DeleteObject(mhPalette);
		mhPalette = NULL;
	}
}

void VDVideoDisplayManager::CheckForegroundState() {
	bool appActive = VDIsForegroundTaskW32();

	if (mbAppActive != appActive) {
		mbAppActive = appActive;

		// Don't handle this synchronously in case we're handling a message in the minidriver!
		PostMessage(mhwnd, WM_USER + 100, 0, 0);
	}
}

LRESULT CALLBACK VDVideoDisplayManager::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_NCCREATE) {
		const CREATESTRUCT& cs = *(const CREATESTRUCT *)lParam;

		SetWindowLongPtr(hwnd, 0, (LONG_PTR)cs.lpCreateParams);
	} else {
		VDVideoDisplayManager *pThis = (VDVideoDisplayManager *)GetWindowLongPtr(hwnd, 0);

		if (pThis)
			return pThis->WndProc(hwnd, msg, wParam, lParam);
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK VDVideoDisplayManager::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			break;

		case WM_ACTIVATEAPP:
			mMonitorHDRInfoCache.clear();
			mMonitorHDRInfoCacheDC.clear();
			mbMonitorHDRInfoCacheDCValid = false;

			CheckForegroundState();
			break;

		case WM_TIMER:
			switch(wParam) {
			case kTimerID_ForegroundPoll:
				CheckForegroundState();
				break;

			case kTimerID_Tick:
				if (mOutstandingTicks.xchg(0))
					DispatchTicks();
				break;
			}
			break;

		case WM_DISPLAYCHANGE:
			{
				bool bPaletted = IsDisplayPaletted();

				if (bPaletted)
					CreateDitheringPalette();

				for(Clients::iterator it(mClients.begin()), itEnd(mClients.end()); it!=itEnd; ++it) {
					VDVideoDisplayClient *p = *it;

					if (!p->mbRequiresFullScreen)
						p->OnDisplayChange();
				}

				if (!bPaletted)
					DestroyDitheringPalette();
			}
			break;

		// Yes, believe it or not, we still support palettes, even when DirectDraw is active.
		// Why?  Very occasionally, people still have to run in 8-bit mode, and a program
		// should still display something half-decent in that case.  Besides, it's kind of
		// neat to be able to dither in safe mode.
		case WM_PALETTECHANGED:
			{
				DWORD dwProcess;

				GetWindowThreadProcessId((HWND)wParam, &dwProcess);

				if (dwProcess != GetCurrentProcessId()) {
					for(Clients::iterator it(mClients.begin()), itEnd(mClients.end()); it!=itEnd; ++it) {
						VDVideoDisplayClient *p = *it;

						if (!p->mbRequiresFullScreen)
							p->OnRealizePalette(mbAppActive);
					}
				}
			}
			break;

		case WM_USER+100:
			{
				for(Clients::iterator it(mClients.begin()), itEnd(mClients.end()); it!=itEnd; ++it) {
					VDVideoDisplayClient *p = *it;

					p->OnForegroundChange(mbAppActive);
				}
			}
			break;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

