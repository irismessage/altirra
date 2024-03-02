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

#ifndef f_AT_ATDEVICES_COMPUTEREYES_H
#define f_AT_ATDEVICES_COMPUTEREYES_H

#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/scheduler.h>

class ATDeviceComputerEyes final : public ATDevice {
public:
	void *AsInterface(uint32 id) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;

private:
	uint8 ReadPortASignals() const;
	void OnPIAOutputChanged(uint32 outputState);

	struct FrameTiming {
		uint64 mFrame;
		uint32 mY;
		uint32 mX;
	};

	FrameTiming GetFrameTiming() const;
	void UpdateThreshold();

	ATScheduler *mpScheduler = nullptr;
	IATDevicePIA *mpPIA = nullptr;
	uint32 mPIADynamicInput = 0;
	int mPIAOutput = -1;

	uint64 mVSyncTimeBase = 0;
	uint64 mScanStartFrame = 0;
	bool mbScanEnabled = false;
	uint8 mRawThreshold = 0;
	uint8 mThreshold = 0;
	uint8 mBrightness = 128;

	ATDeviceParentSingleChild mVideoInput;
};

#endif
