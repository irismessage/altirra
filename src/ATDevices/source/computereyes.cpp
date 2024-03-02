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
#include <vd2/system/math.h>
#include <vd2/system/vectors.h>
#include <vd2/Kasumi/pixmap.h>
#include <at/atcore/devicepia.h>
#include <at/atcore/devicevideosource.h>
#include <at/atcore/propertyset.h>
#include <at/atdevices/computereyes.h>

extern const ATDeviceDefinition g_ATDeviceDefComputerEyes {
	"computereyes",
	"computereyes",
	L"ComputerEyes Video Acquisition System",
	[](const ATPropertySet& pset, IATDevice** dev) {
		vdrefptr<ATDeviceComputerEyes> devp(new ATDeviceComputerEyes);
		
		*dev = devp.release();
	}
};

void *ATDeviceComputerEyes::AsInterface(uint32 id) {
	if (id == IATDeviceParent::kTypeID)
		return static_cast<IATDeviceParent *>(&mVideoInput);

	return ATDevice::AsInterface(id);
}

void ATDeviceComputerEyes::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefComputerEyes;
}

void ATDeviceComputerEyes::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("brightness", mBrightness);
}

bool ATDeviceComputerEyes::SetSettings(const ATPropertySet& settings) {
	mBrightness = (uint8)std::clamp<uint32>(settings.GetUint32("brightness", 50), 0, 100);

	UpdateThreshold();
	return true;
}

void ATDeviceComputerEyes::Init() {
	mVideoInput.Init(IATDeviceVideoSource::kTypeID, "videosource", L"Composite video input", "videoin", this);

	mpPIA = GetService<IATDevicePIA>();
	mPIADynamicInput = mpPIA->RegisterDynamicInput(false, [this] { return ReadPortASignals(); });

	mPIAOutput = mpPIA->AllocOutput(
		[](void *data, uint32 outputState) {
			((ATDeviceComputerEyes *)data)->OnPIAOutputChanged(outputState);
		},
		this,
		0x2E
	);

	mpScheduler = GetService<IATDeviceSchedulingService>()->GetMachineScheduler();
	ColdReset();
}

void ATDeviceComputerEyes::Shutdown() {
	if (mpPIA) {
		if (mPIADynamicInput) {
			mpPIA->UnregisterDynamicInput(false, mPIADynamicInput);
			mPIADynamicInput = 0;
		}

		if (mPIAOutput >= 0) {
			mpPIA->FreeOutput(mPIAOutput);
			mPIAOutput = -1;
		}
	}

	mVideoInput.Shutdown();
}

void ATDeviceComputerEyes::ColdReset() {
	mVSyncTimeBase = mpScheduler->GetTick64();
}

uint8 ATDeviceComputerEyes::ReadPortASignals() const {
	// An NTSC field has 262.5 scanlines of 227.5 color clocks, or 113.75 machine
	// cycles. That means every 238,875 cycles every 8 frames.
	auto [frame, y, x] = GetFrameTiming();

	uint8 v = 0xFF;

	IATDeviceVideoSource *vs = mVideoInput.GetChild<IATDeviceVideoSource>();
	if (!vs)
		return v;

	static constexpr int kPulseStretchTicks = 50;
	static constexpr int kHsyncWidth = 67 + kPulseStretchTicks;
	static constexpr int kSerrationPulseWidth = std::max(0, 67 - kPulseStretchTicks);
	static constexpr int kEqualizingPulseWidth = std::max(0, 33 - kPulseStretchTicks); 

	if (y < 9) {
		if (y >= 3 && y < 6) {
			// vsync serration pulses (4.7us + stretch)
			if ((x >= 455 - kSerrationPulseWidth && x < 455) || x < 910 - kSerrationPulseWidth)
				v = 0x7F;
		} else {
			// equalizing pulses (2.3us + stretch)
			if (x < kEqualizingPulseWidth || (x >= 455 && x < kEqualizingPulseWidth))
				v = 0x7F;
		}
	} else {
		if (x < kHsyncWidth)
			v = 0x7F;
	}

	if (mbScanEnabled) {
		uint64 frameOffset = frame - mScanStartFrame;
		if (frameOffset < 50 || frameOffset >= 370) {
			v &= 0xBF;
		} else {
			// We sample at 2xFsc (7.16MHz), which we must convert to CCIR 601 (13.5MHz).
			float sampleX = ((float)frameOffset - 50.0f - 160.0f + 0.5f) / 7.15909f * 13.5f + 360.0f - 0.5f;
			int sampleY = (int)(y - 20) * 2 + (int)((uint32)frameOffset & 1);
				
			uint32 px = vs->ReadVideoSample(sampleX, sampleY);

			// crudely convert to luma
			uint8 level = ((px & 0xFF00FF) * ((19 << 16) + 54) + (px & 0xFF00) * (183 << 8) + 0x800000) >> 24;

			if (level < mThreshold)
				v &= 0xBF;
		}
	}

	return v;
}

void ATDeviceComputerEyes::OnPIAOutputChanged(uint32 outputState) {
	mRawThreshold = outputState & 0x0F;
	UpdateThreshold();

	const bool scanEnabled = (outputState & 0x20) != 0;
	if (mbScanEnabled != scanEnabled) {
		mbScanEnabled = scanEnabled;

		if (scanEnabled) {
			mScanStartFrame = GetFrameTiming().mFrame;
		}
	}
}

ATDeviceComputerEyes::FrameTiming ATDeviceComputerEyes::GetFrameTiming() const {
	FrameTiming ft;

	const uint64 cycleOffset = mpScheduler->GetTick64() - mVSyncTimeBase;
	const uint64 cycleOffsetX8 = cycleOffset * 8;
	ft.mFrame = cycleOffsetX8 / 238875;

	const uint32 frameOffsetX8 = (uint32)(cycleOffsetX8 % 238875);

	ft.mY = frameOffsetX8 / 910;
	ft.mX = frameOffsetX8 % 910;

	return ft;
}

void ATDeviceComputerEyes::UpdateThreshold() {
	int rawThreshold = ((int)mRawThreshold + 1) * 15;
	int threshold = rawThreshold + (((50 - (int)mBrightness) * 255 + 25) / 50);

	mThreshold = (uint8)std::clamp<int>(threshold, 0, 255);
}
