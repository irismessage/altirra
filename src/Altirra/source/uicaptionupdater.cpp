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
	, mbLastBASICState(false)
	, mbLastPALState(false)
	, mbLastVBXEState(false)
	, mbLastSoundBoardState(false)
	, mbLastDebugging(false)
	, mbForceUpdate(false)
{
	mBasePrefix =
#if defined(VD_CPU_AMD64)
		"Altirra/x64 "
#else
		"Altirra "
#endif
		AT_VERSION AT_VERSION_DEBUG;
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
			mBuffer.append_sprintf(" - %d (%.3f fps) (%.1f%% CPU)", ticks, fps, cpu);

		if (mbCaptured)
			mBuffer += " (mouse captured - right Alt to release)";

		SetWindowText(hwnd, mBuffer.c_str());
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

	bool pal = mpSim->IsPALMode();
	if (mbLastPALState != pal) {
		mbLastPALState = pal;
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
			mPrefix += " [running]";
		else
			mPrefix += " [stopped]";
	}

	bool showBasic = true;
	switch(mpSim->GetHardwareMode()) {
		case kATHardwareMode_800:
			mPrefix += ": 800";
			break;

		case kATHardwareMode_800XL:
			mPrefix += ": XL/XE";
			break;

		case kATHardwareMode_5200:
			mPrefix += ": 5200";
			showBasic = false;
			break;
	}

	switch(mpSim->GetActualKernelMode()) {
		case kATKernelMode_OSA:
			mPrefix += " OS-A";
			break;

		case kATKernelMode_OSB:
			mPrefix += " OS-B";
			break;

		case kATKernelMode_XL:
			break;

		case kATKernelMode_Other:
			mPrefix += " Other";
			break;

		case kATKernelMode_5200:
			mPrefix += " 5200";
			break;

		case kATKernelMode_HLE:
			mPrefix += " HLE";
			break;

		case kATKernelMode_LLE:
		case kATKernelMode_5200_LLE:
			mPrefix += " LLE";
			break;
	}

	if (pal)
		mPrefix += " PAL";
	else
		mPrefix += " NTSC";

	if (vbxe)
		mPrefix += "-VBXE";

	if (sb)
		mPrefix += "-SB";

	mPrefix += " / ";

	switch(mpSim->GetMemoryMode()) {
		case kATMemoryMode_16K:
			mPrefix += "16K";
			break;

		case kATMemoryMode_48K:
			mPrefix += "48K";
			break;

		case kATMemoryMode_52K:
			mPrefix += "52K";
			break;

		case kATMemoryMode_64K:
			mPrefix += "64K";
			break;

		case kATMemoryMode_128K:
			mPrefix += "128K";
			break;

		case kATMemoryMode_320K:
			mPrefix += "320K";
			break;

		case kATMemoryMode_576K:
			mPrefix += "576K";
			break;

		case kATMemoryMode_1088K:
			mPrefix += "1088K";
			break;
	}

	if (basic && showBasic)
		mPrefix += " / BASIC";

	mbForceUpdate = true;
}
