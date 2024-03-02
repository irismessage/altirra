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
#include "firmwaremanager.h"
#include "memorymanager.h"
#include "1090.h"

void ATCreateDevice1090(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevice1090> p(new ATDevice1090);

	*dev = p.release();
}

extern constexpr ATDeviceDefinition g_ATDeviceDef1090 {
	"1090",
	nullptr,
	L"1090 80 Column Video Card",
	ATCreateDevice1090
};

void *ATDevice1090::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceMemMap::kTypeID:			return static_cast<IATDeviceMemMap *>(this);
		case IATDeviceFirmware::kTypeID:		return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceVideoOutput::kTypeID:		return static_cast<IATDeviceVideoOutput *>(this);
		case IATDevicePBIConnection::kTypeID:	return static_cast<IATDevicePBIConnection *>(this);
	}

	return ATDevice::AsInterface(id);
}

void ATDevice1090::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDef1090;
}

void ATDevice1090::Init() {
	mpMemLayerFirmware = mpMemMgr->CreateLayer(kATMemoryPri_PBI, mROM, 0xD8, 0x08, true);
	mpMemMgr->SetLayerName(mpMemLayerFirmware, "1090 firmware");

	ATMemoryHandlerTable handlers {};
	handlers.mpThis = this;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;
	handlers.BindDebugReadHandler<&ATDevice1090::OnRegisterDebugRead>();
	handlers.BindReadHandler<&ATDevice1090::OnRegisterRead>();
	handlers.BindWriteHandler<&ATDevice1090::OnRegisterWrite>();

	mpMemLayerRegisters = mpMemMgr->CreateLayer(kATMemoryPri_PBI, handlers, 0xD1, 0x01);
	mpMemMgr->SetLayerName(mpMemLayerRegisters, "1090 registers");

	ATMemoryHandlerTable handlers2 {};
	handlers2.mpThis = this;
	handlers2.mbPassReads = true;
	handlers2.mbPassWrites = true;
	handlers2.BindWriteHandler<&ATDevice1090::OnVRAMWrite>();

	mpMemLayerVRAM = mpMemMgr->CreateLayer(kATMemoryPri_PBI, handlers2, 0x00, 0x08);
	mpMemMgr->SetLayerName(mpMemLayerVRAM, "1090 VRAM write-through aperture");

	mpMemMgr->SetLayerModes(mpMemLayerVRAM, kATMemoryAccessMode_W);

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

	VDPixmapLayout layout{};
	layout.format = nsVDPixmap::kPixFormat_Pal1;
	layout.data = 0;
	layout.w = 640;
	layout.h = 192;
	layout.pitch = 80;
	layout.palette = mPalette;

	mFrameBuffer.init(layout);
	mFrameBuffer.palette = mPalette;

	mCRTC.Init(ATCRTCEmulator::ChipModel::MC6845, 14318180.0f / 8.0f);

	ColdReset();

	mpPBIMgr->AddDevice(this);

	mpVideoMgr = GetService<IATDeviceVideoManager>();
	mpVideoMgr->AddVideoOutput(this);
}

void ATDevice1090::Shutdown() {
	if (mpVideoMgr) {
		mpVideoMgr->RemoveVideoOutput(this);
		mpVideoMgr = nullptr;
	}

	if (mpPBIMgr) {
		mpPBIMgr->RemoveDevice(this);
		mpPBIMgr = nullptr;
	}

	if (mpMemMgr) {
		mpMemMgr->DeleteLayerPtr(&mpMemLayerFirmware);
		mpMemMgr->DeleteLayerPtr(&mpMemLayerRegisters);
		mpMemMgr->DeleteLayerPtr(&mpMemLayerVRAM);
		mpMemMgr = nullptr;
	}

	mpFwMgr = nullptr;
}

void ATDevice1090::ColdReset() {
	memset(mVRAM, 0xFF, sizeof mVRAM);

	mRegIndex = 0;
	mCRTC.Reset();
	mCRTC.CheckTimingChanged();
	UpdateVideoTiming();

	mpMemMgr->SetLayerAddressRange(mpMemLayerVRAM, 0x00, 0x08);
}

void ATDevice1090::InitMemMap(ATMemoryManager *memmap) {
	mpMemMgr = memmap;
}

bool ATDevice1090::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index == 0) {
		lo = 0xD1FA;
		hi = 0xD1FF;
		return true;
	}

	return false;
}

void ATDevice1090::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;
}

bool ATDevice1090::ReloadFirmware() {
	mFirmwareStatus = ATDeviceFirmwareStatus::OK;

	const uint8 fill = 0xFF;
	bool firmwareChanged = false;
	bool firmwareUsable = false;
	if (!mpFwMgr->LoadFirmware(mpFwMgr->GetCompatibleFirmware(kATFirmwareType_1090Firmware), mROM, 0, sizeof mROM, &firmwareChanged, nullptr, nullptr, &fill, &firmwareUsable))
		mFirmwareStatus = ATDeviceFirmwareStatus::Missing;
	else if (!firmwareUsable)
		mFirmwareStatus = ATDeviceFirmwareStatus::Invalid;

	bool charsetChanged = false;
	bool charsetUsable = false;
	if (!mpFwMgr->LoadFirmware(mpFwMgr->GetCompatibleFirmware(kATFirmwareType_1090Charset), mCharsetROM, 0, sizeof mCharsetROM, &charsetChanged, nullptr, nullptr, &fill, &charsetUsable)) {
		if (mFirmwareStatus != ATDeviceFirmwareStatus::Invalid)
			mFirmwareStatus = ATDeviceFirmwareStatus::Missing;
	} else if (!charsetUsable)
		mFirmwareStatus = ATDeviceFirmwareStatus::Invalid;

	if (charsetChanged)
		mbCharsetDirty = true;

	return firmwareChanged || charsetChanged;
}

const wchar_t *ATDevice1090::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDevice1090::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDevice1090::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDevice1090::GetFirmwareStatus() const {
	return mFirmwareStatus;
}

const char *ATDevice1090::GetName() const {
	return "1090";
}

const wchar_t *ATDevice1090::GetDisplayName() const {
	return L"1090";
}

void ATDevice1090::Tick(uint32 hz300ticks) {
	mCRTC.Advance((float)hz300ticks / 300.0f);

	if (mCRTC.CheckTimingChanged())
		UpdateVideoTiming();
}

void ATDevice1090::UpdateFrame() {
	if (!mCRTC.CheckImageInvalidated())
		return;

	if (mbCharsetDirty)
		GenerateActiveCharset();

	const auto& di = mCRTC.GetDisplayInfo();

	const uint32 baseAddr = di.mBaseAddr & 0x7FF;
	const uint8 *VDRESTRICT src = &mVRAM[baseAddr];
	uint8 *VDRESTRICT dst = (uint8 *)mFrameBuffer.data;

	for(uint32 y = 0; y < 24; ++y) {
		for(uint32 x = 0; x < 80; ++x) {
			const uint8 *VDRESTRICT chardat = mActiveCharset[*src++];
			dst[80*0] = chardat[0];
			dst[80*1] = chardat[1];
			dst[80*2] = chardat[2];
			dst[80*3] = chardat[3];
			dst[80*4] = chardat[4];
			dst[80*5] = chardat[5];
			dst[80*6] = chardat[6];
			dst[80*7] = chardat[7];
			++dst;
		}

		dst += 80*7;
	}

	// cursor processing
	const auto& ci = mCRTC.GetCursorInfo();

	if (ci.mLineMask && ci.mRow < 24 && ci.mCol < 80) {
		uint8 *VDRESTRICT cursorDst = (uint8 *)mFrameBuffer.data + 640*ci.mRow + ci.mCol;

		// invert rows according to cursor row mask
		uint32 mask = ci.mLineMask;
		for(int i = 0; i < 8; ++i) {
			if (mask & 1)
				*cursorDst = ~*cursorDst;

			mask >>= 1;
			cursorDst += 80;
		}
	}

	++mVideoInfo.mFrameBufferChangeCount;
}

const VDPixmap& ATDevice1090::GetFrameBuffer() {
	return mFrameBuffer;
}

const ATDeviceVideoInfo& ATDevice1090::GetVideoInfo() {
	return mVideoInfo;
}

vdpoint32 ATDevice1090::PixelToCaretPos(const vdpoint32& pixelPos) {
	if (pixelPos.y < 0)
		return vdpoint32(0, 0);

	if (pixelPos.y >= 192)
		return vdpoint32(80, 24);

	return vdpoint32(std::clamp((pixelPos.x + 4) >> 3, 0, 80), pixelPos.y >> 3);
}

vdrect32 ATDevice1090::CharToPixelRect(const vdrect32& r) {
	return vdrect32(r.left * 8, r.top * 8, r.right * 8, r.bottom * 8);
}

int ATDevice1090::ReadRawText(uint8 *dst, int x, int y, int n) {
	const auto& di = mCRTC.GetDisplayInfo();

	if (x < 0 || x >= (int)di.mCols)
		return 0;

	if (y < 0 || y >= (int)di.mRows)
		return 0;

	n = std::min(n, (int)di.mCols - x);

	const uint32 baseAddr = (di.mBaseAddr + di.mCols * y + x) & 0x07FF;
	const uint8 *src = &mVRAM[baseAddr];

	// convert INTERNAL to ATASCII
	static constexpr uint8 kConvTab[8] {
		0x20, 0x60, 0x40, 0x00,
		0x20, 0x60, 0x40, 0x00,
	};

	for(int i = 0; i < n; ++i) {
		const uint8 c = src[i];

		dst[i] = c ^ kConvTab[(c >> 5)];
	}

	return n;
}

uint32 ATDevice1090::GetActivityCounter() {
	return mActivityCounter;
}

void ATDevice1090::InitPBI(IATDevicePBIManager *pbiman) {
	mpPBIMgr = pbiman;
}

void ATDevice1090::GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const {
	devInfo.mbHasIrq = false;
	devInfo.mDeviceId = 0x02;
}

void ATDevice1090::SelectPBIDevice(bool enable) {
	if (mbSelected != enable) {
		mbSelected = enable;

		mpMemMgr->EnableLayer(mpMemLayerRegisters, enable);
		mpMemMgr->EnableLayer(mpMemLayerFirmware, enable);
	}
}

bool ATDevice1090::IsPBIOverlayActive() const {
	return mbSelected;
}

uint8 ATDevice1090::ReadPBIStatus(uint8 busData, bool debugOnly) {
	return busData;
}

sint32 ATDevice1090::OnRegisterDebugRead(uint32 addr) {
	switch(addr) {
		case 0xD1FB:
			return mCRTC.DebugReadByte(addr);
	}

	return -1;
}

sint32 ATDevice1090::OnRegisterRead(uint32 addr) {
	switch(addr) {
		case 0xD1FB:
			return mCRTC.ReadByte(addr);
	}

	return -1;
}

bool ATDevice1090::OnRegisterWrite(uint32 addr, uint8 value) {
	switch(addr) {
		case 0xD1FA:
			mRegIndex = value & 0x1F;
			break;

		case 0xD1FB:
			mCRTC.WriteByte(mRegIndex, value);

			if (value)
				++mActivityCounter;
			break;

		case 0xD1FC:
			mpMemMgr->SetLayerAddressRange(mpMemLayerVRAM, value & 0xF8, 0x08);
			break;
	}

	return false;
}

bool ATDevice1090::OnVRAMWrite(uint32 addr, uint8 value) {
	const uint32 offset = addr & 0x7FF;

	if (mVRAM[offset] != value) {
		mVRAM[offset] = value;
		mVRAM[offset + 0x800] = value;

		mCRTC.MarkImageDirty();
	}

	return false;
}

void ATDevice1090::GenerateActiveCharset() {
	memcpy(&mActiveCharset[0], mCharsetROM, 1024);
	memcpy(&mActiveCharset[128], mCharsetROM, 1024);

	VDInvertMemory(&mActiveCharset[128], 8 * 128);

	mbCharsetDirty = false;
}

void ATDevice1090::UpdateVideoTiming() {
	// Dot clock = 8x machine rate (14.31818MHz)
	// Character width = 8 pixels
	// Character clock = 14.31818MHz / 8 = 1.7898825MHz
	
	auto [horiz, vert] = mCRTC.GetScanRates();
	mVideoInfo.mHorizScanRate = horiz;
	mVideoInfo.mVertScanRate = vert;

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
