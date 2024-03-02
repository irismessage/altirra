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

#include <stdafx.h>
#include "uicaptionupdater.h"
#include "versioninfo.h"
#include "console.h"
#include "firmwaremanager.h"
#include "settings.h"

#if defined(WIN32) && defined(ATNRELEASE)
#include <intrin.h>
#endif

ATUIWindowCaptionUpdater::ATUIWindowCaptionUpdater() {
	mBasePrefix = AT_FULL_VERSION_STR;

	// Check the Win32 page heap is enabled. There is no documented way to do this, so
	// we must crawl TEB -> PEB.NtGlobalFlags and then check for FLG_HEAP_PAGE_ALLOCS.
	// This isn't useful in Release and it might trip scanners, so it's only enabled
	// in Debug and Profile builds.
#if defined(WIN32) && defined(ATNRELEASE)
	bool pageHeap = false;

	#if defined(VD_CPU_AMD64)
		uint64 peb = __readgsqword(0x60);
		uint32 flags = *(const uint32 *)(peb + 0xBC);

		pageHeap = (flags & 0x02000000) != 0;
	#elif defined(VD_CPU_X86)
		uint32 peb = __readfsdword(0x30);
		uint32 flags = *(const uint32 *)(peb + 0x68);

		pageHeap = (flags & 0x02000000) != 0;
	#endif

		if (pageHeap)
			mBasePrefix += L" [page heap enabled]";
#endif
}

ATUIWindowCaptionUpdater::~ATUIWindowCaptionUpdater() {
}

void ATUIWindowCaptionUpdater::Init(const vdfunction<void(const wchar_t *)>& fn) {
	mpUpdateFn = fn;
	mpUpdateFn(mBasePrefix.c_str());
}

void ATUIWindowCaptionUpdater::InitMonitoring(ATSimulator *sim) {
	mpSim = sim;

	CheckForStateChange(true);
}

void ATUIWindowCaptionUpdater::Update(bool running, int ticks, float fps, float cpu) {
	bool forceUpdate = false;

	DetectChange(forceUpdate, mbLastRunning, running);
	DetectChange(forceUpdate, mbLastCaptured, mbCaptured);

	CheckForStateChange(forceUpdate);

	mBuffer = mPrefix;

	if ((running && mbShowFps && !mbFullScreen) || mbForceUpdate) {
		mbForceUpdate = false;

		if (mbShowFps)
			mBuffer.append_sprintf(L" - %d (%.3f fps) (%.1f%% CPU)", ticks, fps, cpu);

		if (mbCaptured) {
			if (mbCaptureMMBRelease)
				mBuffer += L" (mouse captured - right Alt or MMB to release)";
			else
				mBuffer += L" (mouse captured - right Alt to release)";
		}

		mpUpdateFn(mBuffer.c_str());
	}
}

void ATUIWindowCaptionUpdater::CheckForStateChange(bool force) {
	if (!mpSim)
		return;

	bool change = false;

	DetectChange(change, mbTemporaryProfile, ATSettingsGetTemporaryProfileMode());

	DetectChange(change, mLastHardwareMode, mpSim->GetHardwareMode());
	DetectChange(change, mLastKernelId, mpSim->GetActualKernelId());
	DetectChange(change, mLastMemoryMode, mpSim->GetMemoryMode());

	bool basic = mpSim->IsBASICEnabled();

	if (mLastHardwareMode != kATHardwareMode_800XL && mLastHardwareMode != kATHardwareMode_XEGS && mLastHardwareMode != kATHardwareMode_130XE)
		basic = false;

	DetectChange(change, mbLastBASICState, basic);
	DetectChange(change, mLastVideoStd, mpSim->GetVideoStandard());
	DetectChange(change, mbLastVBXEState, mpSim->GetVBXE() != NULL);
	DetectChange(change, mbLastU1MBState, mpSim->IsUltimate1MBEnabled());
	DetectChange(change, mbLastDebugging, ATIsDebugConsoleActive());

	const ATCPUEmulator& cpu = mpSim->GetCPU();
	ATCPUMode cpuMode = cpu.GetCPUMode();
	uint32 cpuSubCycles = cpu.GetSubCycles();

	DetectChange(change, mLastCPUMode, cpuMode);
	DetectChange(change, mLastCPUSubCycles, cpuSubCycles);
	DetectChange(change, mbLastShowFPS, mbShowFps);

	if (!force && !change)
		return;

	mPrefix.clear();

	if (mbTemporaryProfile)
		mPrefix += '*';

	mPrefix.append(mBasePrefix);

	if (ATIsDebugConsoleActive()) {
		if (mbLastRunning)
			mPrefix += L" [running]";
		else
			mPrefix += L" [stopped]";
	}

	bool showBasic = true;
	switch(mLastHardwareMode) {
		case kATHardwareMode_800:
			mPrefix += L": 800";
			showBasic = false;
			break;

		case kATHardwareMode_800XL:
			mPrefix += L": XL";
			break;

		case kATHardwareMode_130XE:
			mPrefix += L": XE";
			break;

		case kATHardwareMode_1200XL:
			mPrefix += L": 1200XL";
			showBasic = false;
			break;

		case kATHardwareMode_XEGS:
			mPrefix += L": XEGS";
			break;

		case kATHardwareMode_5200:
			mPrefix += L": 5200";
			showBasic = false;
			break;
	}

	if (!mbLastU1MBState) {
		ATFirmwareInfo fwInfo;
		if (mpSim->GetFirmwareManager()->GetFirmwareInfo(mLastKernelId, fwInfo)) {
			switch(fwInfo.mId) {
				case kATFirmwareId_Kernel_HLE:
					mPrefix += L" ATOS/HLE";
					break;

				case kATFirmwareId_Kernel_LLE:
					if (mLastHardwareMode == kATHardwareMode_800)
						mPrefix += L" ATOS";
					else
						mPrefix += L" ATOS/800";
					break;

				case kATFirmwareId_Kernel_LLEXL:
					switch(mLastHardwareMode) {
						case kATHardwareMode_800XL:
						case kATHardwareMode_1200XL:
						case kATHardwareMode_XEGS:
						case kATHardwareMode_130XE:
							mPrefix += L" ATOS";
							break;

						default:
							mPrefix += L" ATOS/XL";
							break;
					}
					break;

				case kATFirmwareId_5200_LLE:
					mPrefix += L" ATOS";
					break;

				default:
					switch(fwInfo.mType) {
						case kATFirmwareType_Kernel800_OSA:
							mPrefix += L" OS-A";
							break;

						case kATFirmwareType_Kernel800_OSB:
							mPrefix += L" OS-B";
							break;

						case kATFirmwareType_KernelXL:
							if (mLastHardwareMode != kATHardwareMode_800XL && mLastHardwareMode != kATHardwareMode_130XE)
								mPrefix += L" XL/XE";
							break;

						case kATFirmwareType_Kernel1200XL:
							if (mLastHardwareMode != kATHardwareMode_1200XL)
								mPrefix += L" 1200XL";
							break;

						case kATFirmwareType_KernelXEGS:
							if (mLastHardwareMode != kATHardwareMode_XEGS)
								mPrefix += L" XEGS";
							break;

						case kATFirmwareType_Kernel5200:
							if (mLastHardwareMode != kATHardwareMode_5200)
								mPrefix += L" 5200";
							break;
					}
					break;
			}
		}
	}

	const bool is5200 = (mLastHardwareMode == kATHardwareMode_5200);
	if (!is5200) {
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

			case kATVideoStandard_NTSC50:
				mPrefix += L" NTSC-50";
				break;

			case kATVideoStandard_PAL60:
				mPrefix += L" PAL-60";
				break;
		}
	}

	if (mbLastVBXEState)
		mPrefix += L"+VBXE";

	if (mpSim->IsRapidusEnabled()) {
		mPrefix += L"+Rapidus";
	} else {
		switch(cpuMode) {
			case kATCPUMode_65C02:
				mPrefix += L"+C02";
				break;

			case kATCPUMode_65C816:
				mPrefix += L"+816";
				if (cpuSubCycles > 1)
					mPrefix.append_sprintf(L" @ %.3gMHz", (double)cpuSubCycles * 1.79);
				break;

			default:
				break;
		}
	}
	
	if (!is5200)
		mPrefix += L" / ";

	if (mbLastU1MBState) {
		mPrefix += L"U1MB";
	} else if (!is5200) {
		switch(mLastMemoryMode) {
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

			case kATMemoryMode_256K:
				mPrefix += L"256K Rambo";
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
	}

	if (basic && showBasic)
		mPrefix += L" / BASIC";

	mbForceUpdate = true;
}

template<class T>
void ATUIWindowCaptionUpdater::DetectChange(bool& changed, T& cachedValue, const T& currentValue) {
	if (cachedValue != currentValue) {
		cachedValue = currentValue;
		changed = true;
	}
}
