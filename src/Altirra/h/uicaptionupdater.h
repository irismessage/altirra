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

#ifndef f_AT_UICAPTIONUPDATER_H
#define f_AT_UICAPTIONUPDATER_H

#include <vd2/system/function.h>
#include "simulator.h"

class ATUIWindowCaptionUpdater {
public:
	ATUIWindowCaptionUpdater();
	~ATUIWindowCaptionUpdater();

	void Init(ATSimulator *sim, const vdfunction<void(const wchar_t *)>& fn);

	void SetShowFps(bool showFps) { mbShowFps = showFps; }
	void SetFullScreen(bool fs) { mbFullScreen = fs; }
	void SetMouseCaptured(bool captured, bool mmbRelease) { mbCaptured = captured; mbCaptureMMBRelease = mmbRelease; }

	void Update(bool running, int ticks, float fps, float cpu);
	void CheckForStateChange(bool force);

protected:
	template<class T> void DetectChange(bool& changed, T& cachedValue, const T& currentValue);

	ATSimulator *mpSim = nullptr;
	bool mbLastRunning = false;
	bool mbLastCaptured = false;

	bool mbShowFps = false;
	bool mbFullScreen = false;
	bool mbCaptured = false;
	bool mbCaptureMMBRelease = false;

	VDStringW	mBasePrefix;
	VDStringW	mPrefix;
	VDStringW	mBuffer;

	ATHardwareMode	mLastHardwareMode = kATHardwareModeCount;
	uint64			mLastKernelId = 0;
	ATMemoryMode	mLastMemoryMode = kATMemoryModeCount;
	ATVideoStandard	mLastVideoStd = kATVideoStandardCount;
	bool			mbLastBASICState = false;
	bool			mbLastVBXEState = false;
	bool			mbLastSoundBoardState = false;
	bool			mbLastU1MBState = false;
	bool			mbForceUpdate = false;
	bool			mbLastDebugging = false;
	bool			mbLastShowFPS = false;;
	bool			mbTemporaryProfile = false;
	ATCPUMode		mLastCPUMode = kATCPUModeCount;
	uint32			mLastCPUSubCycles = 0;

	vdfunction<void(const wchar_t *)> mpUpdateFn;
};

#endif	// f_AT_UICAPTIONUPDATER_H
