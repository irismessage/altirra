//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include <vd2/system/binary.h>
#include <at/atcore/device.h>
#include <at/atcore/scheduler.h>
#include "firmwaremanager.h"
#include "memorymanager.h"
#include "bit3.h"

void ATCreateDeviceBit3FullView(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceBit3FullView> p(new ATDeviceBit3FullView);

	*dev = p.release();
}

extern constexpr ATDeviceDefinition g_ATDeviceDefBit3FullView {
	"bit3",
	nullptr,
	L"Bit 3 Full-View 80",
	ATCreateDeviceBit3FullView
};

void *ATDeviceBit3FullView::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceMemMap::kTypeID:			return static_cast<IATDeviceMemMap *>(this);
		case IATDeviceFirmware::kTypeID:		return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceScheduling::kTypeID:		return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceVideoOutput::kTypeID:		return static_cast<IATDeviceVideoOutput *>(this);
	}

	return ATDevice::AsInterface(id);
}

void ATDeviceBit3FullView::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefBit3FullView;
}

void ATDeviceBit3FullView::Init() {
	mpMemLayerFirmware = mpMemMgr->CreateLayer(kATMemoryPri_PBI, mROM, 0xD6, 0x01, true);
	mpMemMgr->SetLayerModes(mpMemLayerFirmware, kATMemoryAccessMode_AR);
	mpMemMgr->SetLayerName(mpMemLayerFirmware, "Bit 3 Full-View 80 firmware");

	ATMemoryHandlerTable handlers {};
	handlers.mpThis = this;
	handlers.mbPassAnticReads = true;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;
	handlers.BindDebugReadHandler<&ATDeviceBit3FullView::OnRegisterDebugRead>();
	handlers.BindReadHandler<&ATDeviceBit3FullView::OnRegisterRead>();
	handlers.BindWriteHandler<&ATDeviceBit3FullView::OnRegisterWrite>();

	mpMemLayerRegisters = mpMemMgr->CreateLayer(kATMemoryPri_PBI, handlers, 0xD5, 0x01);
	mpMemMgr->SetLayerName(mpMemLayerRegisters, "Bit 3 Full-View 80 registers");
	mpMemMgr->EnableLayer(mpMemLayerRegisters, true);

	ReloadFirmware();

	mVideoInfo.mbSignalValid = false;
	mVideoInfo.mbSignalPassThrough = false;
	mVideoInfo.mHorizScanRate = 0;
	mVideoInfo.mVertScanRate = 0;
	mVideoInfo.mFrameBufferLayoutChangeCount = 1;
	mVideoInfo.mFrameBufferChangeCount = 0;
	mVideoInfo.mTextRows = 24;
	mVideoInfo.mTextColumns = 80;
	mVideoInfo.mPixelAspectRatio = 6.0f / 14.0f;		// 4*Fsc (14MHz) dot clock
	mVideoInfo.mDisplayArea = vdrect32(0, 0, 640, 192);
	mVideoInfo.mBorderColor = 0;
	mVideoInfo.mbForceExactPixels = false;
	UpdateVideoTiming();

	VDPixmapLayout layout{};
	layout.format = nsVDPixmap::kPixFormat_Pal1;
	layout.data = 0;
	layout.w = 640;
	layout.h = 192;
	layout.pitch = 80;
	layout.palette = mPalette;

	mFrameBuffer.init(layout);
	mFrameBuffer.palette = mPalette;

	mCRTC.Init(ATCRTCEmulator::ChipModel::SY6545, 14318180.0f / 8.0f);

	ColdReset();

	mpVideoMgr = GetService<IATDeviceVideoManager>();
	mpVideoMgr->AddVideoOutput(this);
}

void ATDeviceBit3FullView::Shutdown() {
	if (mpVideoMgr) {
		mpVideoMgr->RemoveVideoOutput(this);
		mpVideoMgr = nullptr;
	}

	if (mpMemMgr) {
		mpMemMgr->DeleteLayerPtr(&mpMemLayerFirmware);
		mpMemMgr->DeleteLayerPtr(&mpMemLayerRegisters);
		mpMemMgr = nullptr;
	}

	mpFwMgr = nullptr;
	mpScheduler = nullptr;
}

void ATDeviceBit3FullView::ColdReset() {
	memset(mVRAM, 0xFF, sizeof mVRAM);

	mRegIndex = 0;
	mCRTC.Reset();
	mCRTC.CheckTimingChanged();
	UpdateVideoTiming();

	mFirmwareBank = 0;
	UpdateFirmwareBank();

	mNotReadyStartTime = 0;
	mNotReadyEndTime = 0;
}

void ATDeviceBit3FullView::InitMemMap(ATMemoryManager *memmap) {
	mpMemMgr = memmap;
}

bool ATDeviceBit3FullView::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index == 0) {
		lo = 0xD508;
		hi = 0xD508;
		return true;
	}

	if (index == 1) {
		lo = 0xD580;
		hi = 0xD58F;
		return true;
	}

	if (index == 2) {
		lo = 0xD600;
		hi = 0xD6FF;
		return true;
	}

	return false;
}

void ATDeviceBit3FullView::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;
}

bool ATDeviceBit3FullView::ReloadFirmware() {
	mFirmwareStatus = ATDeviceFirmwareStatus::OK;

	const uint8 fill = 0xFF;
	bool firmwareChanged = false;
	bool firmwareUsable = false;
	if (!mpFwMgr->LoadFirmware(mpFwMgr->GetCompatibleFirmware(kATFirmwareType_Bit3Firmware), mROM, 0, sizeof mROM, &firmwareChanged, nullptr, nullptr, &fill, &firmwareUsable))
		mFirmwareStatus = ATDeviceFirmwareStatus::Missing;
	else if (!firmwareUsable)
		mFirmwareStatus = ATDeviceFirmwareStatus::Invalid;

	bool charsetChanged = false;
	bool charsetUsable = false;
	if (!mpFwMgr->LoadFirmware(mpFwMgr->GetCompatibleFirmware(kATFirmwareType_Bit3Charset), mCharsetROM, 0, sizeof mCharsetROM, &charsetChanged, nullptr, nullptr, &fill, &charsetUsable)) {
		if (mFirmwareStatus != ATDeviceFirmwareStatus::Invalid)
			mFirmwareStatus = ATDeviceFirmwareStatus::Missing;
	} else if (!charsetUsable)
		mFirmwareStatus = ATDeviceFirmwareStatus::Invalid;

	if (charsetChanged) {
		mCRTC.MarkImageDirty();

		for(int i = 0; i < 2048; ++i) {
			uint8 c = mCharsetROM[i];

			c = (c << 4) + (c >> 4);
			c = ((c & 0xCC) >> 2) + ((c & 0x33) << 2);
			c = ((c & 0xAA) >> 1) + ((c & 0x55) << 1);

			mActiveCharset[i] = c;
			mActiveCharset[i + 2048] = ~c;
		}
	}

	return firmwareChanged || charsetChanged;
}

const wchar_t *ATDeviceBit3FullView::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDeviceBit3FullView::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDeviceBit3FullView::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDeviceBit3FullView::GetFirmwareStatus() const {
	return mFirmwareStatus;
}

void ATDeviceBit3FullView::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

const char *ATDeviceBit3FullView::GetName() const {
	return "bit3";
}

const wchar_t *ATDeviceBit3FullView::GetDisplayName() const {
	return L"Bit 3 Full-View 80";
}

void ATDeviceBit3FullView::Tick(uint32 hz300ticks) {
	mCRTC.Advance((float)hz300ticks / 300.0f);

	if (mCRTC.CheckTimingChanged()) {
		UpdateVideoTiming();
		mCRTC.MarkImageDirty();
	}
}

void ATDeviceBit3FullView::UpdateFrame() {
	if (!mCRTC.CheckImageInvalidated())
		return;

	// double-check that timing is correct, just in case
	if (mCRTC.CheckTimingChanged())
		UpdateVideoTiming();
	
	if (mbUnblanked) {
		const auto& di = mCRTC.GetDisplayInfo();

		sint32 w = di.mCols * 8;
		sint32 h = di.mRows * di.mLinesPerRow;

		if (mFrameBuffer.w != w || mFrameBuffer.h != h) {
			VDPixmapLayout layout {};
			layout.data = 0;
			layout.w = w;
			layout.h = h;
			layout.format = nsVDPixmap::kPixFormat_Pal1;
			layout.pitch = ((w + 127) >> 7) * 16;
			mFrameBuffer.init(layout);
			mFrameBuffer.palette = mPalette;
			++mVideoInfo.mFrameBufferLayoutChangeCount;

			mVideoInfo.mDisplayArea.set(0, 0, w, h);
		}

		const uint32 baseAddr = di.mBaseAddr & 0x7FF;
		const uint8 *VDRESTRICT src = &mVRAM[baseAddr];
		uint8 *VDRESTRICT dst = (uint8 *)mFrameBuffer.data;
		const ptrdiff_t pitch = mFrameBuffer.pitch;

		for(uint32 y = 0; y < di.mRows; ++y) {
			for(uint32 x = 0; x < di.mCols; ++x) {
				const uint8 *VDRESTRICT chardat = &mActiveCharset[*src++ * 16];

				for(uint32 i = 0; i < di.mLinesPerRow; ++i)
					dst[pitch * i] = chardat[i];

				++dst;
			}

			dst += pitch * (di.mLinesPerRow - 1);
		}

		// cursor processing
		const auto& ci = mCRTC.GetCursorInfo();

		if (ci.mLineMask && ci.mRow < di.mRows && ci.mCol < di.mCols) {
			uint8 *VDRESTRICT cursorDst = (uint8 *)mFrameBuffer.data + pitch * di.mLinesPerRow * ci.mRow + ci.mCol;

			// invert rows according to cursor row mask
			uint32 mask = ci.mLineMask;
			for(uint32 i = 0; i < di.mLinesPerRow; ++i) {
				if (mask & 1)
					*cursorDst = ~*cursorDst;

				mask >>= 1;
				cursorDst += pitch;
			}
		}
	} else {
		memset(mFrameBuffer.base(), 0, mFrameBuffer.size());
	}

	++mVideoInfo.mFrameBufferChangeCount;
}

const VDPixmap& ATDeviceBit3FullView::GetFrameBuffer() {
	return mFrameBuffer;
}

const ATDeviceVideoInfo& ATDeviceBit3FullView::GetVideoInfo() {
	return mVideoInfo;
}

vdpoint32 ATDeviceBit3FullView::PixelToCaretPos(const vdpoint32& pixelPos) {
	if (pixelPos.y < 0)
		return vdpoint32(0, 0);

	const auto& di = mCRTC.GetDisplayInfo();
	uint32 row = (uint32)pixelPos.y / di.mLinesPerRow;

	if (row >= di.mRows)
		return vdpoint32(di.mCols, di.mRows);

	return vdpoint32(std::clamp((pixelPos.x + 4) >> 3, 0, 80), row);
}

vdrect32 ATDeviceBit3FullView::CharToPixelRect(const vdrect32& r) {
	const auto& di = mCRTC.GetDisplayInfo();

	return vdrect32(r.left * 8, r.top * di.mLinesPerRow, r.right * 8, r.bottom * di.mLinesPerRow);
}

int ATDeviceBit3FullView::ReadRawText(uint8 *dst, int x, int y, int n) {
	const auto& di = mCRTC.GetDisplayInfo();

	if (x < 0 || x >= (int)di.mCols)
		return 0;

	if (y < 0 || y >= (int)di.mRows)
		return 0;

	n = std::min(n, (int)di.mCols - x);

	const uint32 baseAddr = (di.mBaseAddr + di.mCols * y + x) & 0x07FF;
	const uint8 *src = &mVRAM[baseAddr];

	memcpy(dst, src, n);
	return n;
}

uint32 ATDeviceBit3FullView::GetActivityCounter() {
	return mActivityCounter;
}

sint32 ATDeviceBit3FullView::OnRegisterDebugRead(uint32 addr) {
	switch(addr) {
		case 0xD580: {	// status port
			const uint64 t = mpScheduler->GetTick64();
			return (t < mNotReadyStartTime || t >= mNotReadyEndTime ? 0x80 : 0x00) + 0x40 + (mCRTC.IsInVBLANK(t) ? 0x20 : 0x00);
		}

		case 0xD581:	// CRTC register read port
			return mCRTC.DebugReadByte(addr);

		case 0xD583:	// VRAM read port
			return mVRAMReadLatch;
	}

	return -1;
}

sint32 ATDeviceBit3FullView::OnRegisterRead(uint32 addr) {
	switch(addr) {
		case 0xD581:	// CRTC register read port
			return mCRTC.ReadByte(addr);

		case 0xD583: {	// VRAM read port
			const uint8 v = mVRAMReadLatch;

			// increment address and schedule next automatic read
			mVRAMReadLatch = mVRAM[mCRTC.GetTranspAccessAddr() & 0x07FF];
			mCRTC.IncrementTranspAccessAddr();

			const uint64 t = mpScheduler->GetTick64();

			mNotReadyStartTime = t;
			mNotReadyEndTime = mCRTC.AccessTranspLatch(mNotReadyStartTime);

			return v;
		}
	}

	return OnRegisterDebugRead(addr);
}

bool ATDeviceBit3FullView::OnRegisterWrite(uint32 addr, uint8 value) {
	switch(addr) {
		case 0xD508:	// misc control port
			// D0/1/2/5: select firmware bank
			// D3 = 1: unblank display
			// D4 = 1: passthrough control (1 = Bit3, 0 = computer)
			if (const uint8 bank = (value & 7) + ((value & 0x20) >> 2);
				mFirmwareBank != bank)
			{
				mFirmwareBank = bank;
				UpdateFirmwareBank();
			}

			if (const bool unblanked = (value & 0x08) != 0;
				mbUnblanked != unblanked)
			{
				mbUnblanked = unblanked;
				mCRTC.MarkImageDirty();
			}

			if (const bool passThrough = !(value & 0x10);
				mVideoInfo.mbSignalPassThrough != passThrough)
			{
				mVideoInfo.mbSignalPassThrough = passThrough;
				++mVideoInfo.mFrameBufferChangeCount;
			}
			break;

		case 0xD580:	// CRTC register address port
			mRegIndex = value & 0x1F;

			if (mRegIndex == 0x1F) {
				// Dummy register selected -- schedule automatic read of update address
				mVRAMReadLatch = mVRAM[mCRTC.GetTranspAccessAddr() & 0x07FF];

				const uint64 t = mpScheduler->GetTick64();

				mNotReadyStartTime = t;
				mNotReadyEndTime = mCRTC.AccessTranspLatch(mNotReadyStartTime);
			}
			break;

		case 0xD581:	// CRTC register write port
			mCRTC.WriteByte(mRegIndex, value);

			if (value)
				++mActivityCounter;
			break;

		case 0xD585:	// VRAM write port
			{
				const uint32 vramAddr = mCRTC.GetTranspAccessAddr() & 0x07FF;

				if (mVRAM[vramAddr] != value) {
					mVRAM[vramAddr + 0x800] = mVRAM[vramAddr] = value;

					mCRTC.MarkImageDirty();
				}

				// schedule write and automatic read of next update address
				mCRTC.IncrementTranspAccessAddr();
				mVRAMReadLatch = mVRAM[mCRTC.GetTranspAccessAddr() & 0x07FF];

				const uint64 t = mpScheduler->GetTick64();

				mNotReadyStartTime = t;
				mNotReadyEndTime = mCRTC.AccessTranspLatch(mCRTC.AccessTranspLatch(mNotReadyStartTime));
			}
			break;
	}

	return false;
}

void ATDeviceBit3FullView::UpdateVideoTiming() {
	// Dot clock = 8x machine rate (14.31818MHz)
	// Character width = 8 pixels
	// Character clock = 14.31818MHz / 8 = 1.7898825MHz
	
	auto [horiz, vert] = mCRTC.GetScanRates();
	if (vert > 240.0f || vert < 10.0f) {
		horiz = 0;
		vert = 0;
	}

	mVideoInfo.mHorizScanRate = horiz;
	mVideoInfo.mVertScanRate = vert;

	const auto& di = mCRTC.GetDisplayInfo();
	mVideoInfo.mTextRows = di.mRows;
	mVideoInfo.mTextColumns = di.mCols;

	mVideoInfo.mbSignalValid = true;

	if (mVideoInfo.mHorizScanRate < 14700 || mVideoInfo.mHorizScanRate > 16700)
		mVideoInfo.mbSignalValid = false;

	[[maybe_unused]] bool mbPAL;
	if (mVideoInfo.mVertScanRate >= 48 && mVideoInfo.mVertScanRate <= 52)
		mbPAL = true;
	else if (mVideoInfo.mVertScanRate >= 58 && mVideoInfo.mVertScanRate <= 62)
		mbPAL = false;
	else
		mVideoInfo.mbSignalValid = false;
}

void ATDeviceBit3FullView::UpdateFirmwareBank() {
	mpMemMgr->SetLayerMemory(mpMemLayerFirmware, mROM + 0x100 * mFirmwareBank);
}
