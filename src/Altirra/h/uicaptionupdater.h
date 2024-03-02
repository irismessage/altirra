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

#include <windows.h>
#include "simulator.h"

class ATUIWindowCaptionUpdater {
public:
	ATUIWindowCaptionUpdater();
	~ATUIWindowCaptionUpdater();

	void Init(ATSimulator *sim);

	void SetShowFps(bool showFps) { mbShowFps = showFps; }
	void SetMouseCaptured(bool captured, bool mmbRelease) { mbCaptured = captured; mbCaptureMMBRelease = mmbRelease; }

	void Update(HWND hwnd, bool running, int ticks, float fps, float cpu);
	void CheckForStateChange(bool force);

protected:
	ATSimulator *mpSim;
	bool mbLastRunning;
	bool mbLastCaptured;

	bool mbShowFps;
	bool mbCaptured;
	bool mbCaptureMMBRelease;

	VDStringW	mBasePrefix;
	VDStringW	mPrefix;
	VDStringW	mBuffer;

	ATHardwareMode	mLastHardwareMode;
	ATKernelMode	mLastKernelMode;
	ATMemoryMode	mLastMemoryMode;
	ATVideoStandard	mLastVideoStd;
	bool			mbLastBASICState;
	bool			mbLastVBXEState;
	bool			mbLastSoundBoardState;
	bool			mbLastU1MBState;
	bool			mbForceUpdate;
	bool			mbLastDebugging;
	bool			mbLastShowFPS;
	ATCPUMode		mLastCPUMode;
	uint32			mLastCPUSubCycles;
};

#endif	// f_AT_UICAPTIONUPDATER_H
