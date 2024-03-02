//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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

#include "stdafx.h"
#include "uicaptionupdater.h"
#include "version.h"
#include "console.h"

#ifdef _DEBUG
	#define AT_VERSION_DEBUG "-debug"
#else
	#define AT_VERSION_DEBUG ""
#endif

ATUIWindowCaptionUpdater::ATUIWindowCaptionUpdater()
	: mbLastRunning(false)
	, mbLastCaptured(false)
	, mbShowFps(false)
	, mbCaptured(false)
	, mLastHardwareMode(kATHardwareModeCount)
	, mLastKernelMode(kATKernelModeCount)
	, mLastMemoryMode(kATMemoryModeCount)
	, mLastVideoStd(kATVideoStandardCount)
	, mbLastBASICState(false)
	, mbLastVBXEState(false)
	, mbLastSoundBoardState(false)
	, mbLastDebugging(false)
	, mbForceUpdate(false)
{
	mBasePrefix =
#if defined(VD_CPU_AMD64)
		L"Altirra/x64 "
#else
		L"Altirra "
#endif
		_T(AT_VERSION) _T(AT_VERSION_DEBUG)
#if AT_VERSION_PRERELEASE
		L" [prerelease]"
#endif
		;
}

ATUIWindowCaptionUpdater::~ATUIWindowCaptionUpdater() {
}

void ATUIWindowCaptionUpdater::Init(ATSimulator *sim) {
	mpSim = sim;
	CheckForStateChange(true);
}

void ATUIWindowCaptionUpdater::Update(HWND hwnd, bool running, int ticks, float fps, float cpu) {
	bool forceUpdate = false;

	if (mbLastRunning != running) {
		mbLastRunning = running;
		forceUpdate = true;
	}

	if (mbLastCaptured != mbCaptured) {
		mbLastCaptured = mbCaptured;
		forceUpdate = true;
	}

	CheckForStateChange(forceUpdate);

	mBuffer = mPrefix;

	if ((running && mbShowFps) || mbForceUpdate) {
		mbForceUpdate = false;

		if (mbShowFps)
			mBuffer.append_sprintf(L" - %d (%.3f fps) (%.1f%% CPU)", ticks, fps, cpu);

		if (mbCaptured)
			mBuffer += L" (mouse captured - right Alt to release)";

		SetWindowTextW(hwnd, mBuffer.c_str());
	}
}

void ATUIWindowCaptionUpdater::CheckForStateChange(bool force) {
	bool change = false;

	ATHardwareMode hwmode = mpSim->GetHardwareMode();
	if (mLastHardwareMode != hwmode) {
		mLastHardwareMode = hwmode;
		change = true;
	}

	ATKernelMode krmode = mpSim->GetKernelMode();
	if (mLastKernelMode != krmode) {
		mLastKernelMode = krmode;
		change = true;
	}

	ATMemoryMode mmmode = mpSim->GetMemoryMode();
	if (mLastMemoryMode != mmmode) {
		mLastMemoryMode = mmmode;
		change = true;
	}

	bool basic = mpSim->IsBASICEnabled();
	if (mbLastBASICState != basic) {
		mbLastBASICState = basic;
		change = true;
	}

	ATVideoStandard vs = mpSim->GetVideoStandard();
	if (mLastVideoStd != vs) {
		mLastVideoStd = vs;
		change = true;
	}

	bool vbxe = mpSim->GetVBXE() != NULL;
	if (mbLastVBXEState != vbxe) {
		mbLastVBXEState = vbxe;
		change = true;
	}

	bool sb = mpSim->GetSoundBoard() != NULL;
	if (mbLastSoundBoardState != sb) {
		mbLastSoundBoardState = sb;
		change = true;
	}

	bool debugging = ATIsDebugConsoleActive();
	if (mbLastDebugging != debugging) {
		mbLastDebugging = debugging;
		change = true;
	}

	if (mbLastShowFPS != mbShowFps) {
		mbLastShowFPS = mbShowFps;
		change = true;
	}

	if (!force && !change)
		return;

	mPrefix = mBasePrefix;

	if (ATIsDebugConsoleActive()) {
		if (mbLastRunning)
			mPrefix += L" [running]";
		else
			mPrefix += L" [stopped]";
	}

	bool showBasic = true;
	switch(mpSim->GetHardwareMode()) {
		case kATHardwareMode_800:
			mPrefix += L": 800";
			break;

		case kATHardwareMode_800XL:
			mPrefix += L": XL/XE";
			break;

		case kATHardwareMode_XEGS:
			mPrefix += L": XEGS";
			break;

		case kATHardwareMode_5200:
			mPrefix += L": 5200";
			showBasic = false;
			break;
	}

	switch(mpSim->GetActualKernelMode()) {
		case kATKernelMode_OSA:
			mPrefix += L" OS-A";
			break;

		case kATKernelMode_OSB:
			mPrefix += L" OS-B";
			break;

		case kATKernelMode_XL:
			break;

		case kATKernelMode_Other:
			mPrefix += L" Other";
			break;

		case kATKernelMode_5200:
			mPrefix += L" 5200";
			break;

		case kATKernelMode_HLE:
			mPrefix += L" HLE";
			break;

		case kATKernelMode_LLE:
		case kATKernelMode_5200_LLE:
			mPrefix += L" LLE";
			break;
	}

	switch(mLastVideoStd) {
		case kATVideoStandard_NTSC:
		default:
			mPrefix += L" NTSC";
			break;

		case kATVideoStandard_PAL:
			mPrefix += L" PAL";
			break;

		case kATVideoStandard_SECAM:
			mPrefix += L" SECAM";
			break;
	}

	if (vbxe)
		mPrefix += L"-VBXE";

	if (sb)
		mPrefix += L"-SB";

	mPrefix += L" / ";

	switch(mpSim->GetMemoryMode()) {
		case kATMemoryMode_8K:
			mPrefix += L"8K";
			break;

		case kATMemoryMode_16K:
			mPrefix += L"16K";
			break;

		case kATMemoryMode_24K:
			mPrefix += L"24K";
			break;

		case kATMemoryMode_32K:
			mPrefix += L"32K";
			break;

		case kATMemoryMode_40K:
			mPrefix += L"40K";
			break;

		case kATMemoryMode_48K:
			mPrefix += L"48K";
			break;

		case kATMemoryMode_52K:
			mPrefix += L"52K";
			break;

		case kATMemoryMode_64K:
			mPrefix += L"64K";
			break;

		case kATMemoryMode_128K:
			mPrefix += L"128K";
			break;

		case kATMemoryMode_320K:
			mPrefix += L"320K Rambo";
			break;

		case kATMemoryMode_320K_Compy:
			mPrefix += L"320K Compy";
			break;

		case kATMemoryMode_576K:
			mPrefix += L"576K";
			break;

		case kATMemoryMode_576K_Compy:
			mPrefix += L"576K Compy";
			break;

		case kATMemoryMode_1088K:
			mPrefix += L"1088K";
			break;
	}

	if (basic && showBasic)
		mPrefix += L" / BASIC";

	mbForceUpdate = true;
}