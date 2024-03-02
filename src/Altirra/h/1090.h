//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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

#ifndef f_AT_1090_H
#define f_AT_1090_H

#include <vd2/system/vdtypes.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicepbi.h>
#include <at/atcore/devicevideo.h>
#include <at/atemulation/crtc.h>

class ATMemoryLayer;
class ATMemoryManager;

class ATDevice1090 final : public ATDevice
	, public IATDeviceMemMap
	, public IATDeviceFirmware
	, public IATDeviceVideoOutput
	, public IATDevicePBIConnection
	, public IATPBIDevice
{
public:
	void *AsInterface(uint32 id) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;

public:		// IATDeviceMemMap
	void InitMemMap(ATMemoryManager *memmap) override;
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

public:		// IATDeviceFirmware
	void InitFirmware(ATFirmwareManager *fwman) override;
	bool ReloadFirmware() override;
	const wchar_t *GetWritableFirmwareDesc(uint32 idx) const override;
	bool IsWritableFirmwareDirty(uint32 idx) const override;
	void SaveWritableFirmware(uint32 idx, IVDStream& stream) override;
	ATDeviceFirmwareStatus GetFirmwareStatus() const override;

public:		// IATDeviceVideo
	const char *GetName() const override;
	const wchar_t *GetDisplayName() const override;
	void Tick(uint32 hz300ticks) override;
	void UpdateFrame() override;
	const VDPixmap& GetFrameBuffer() override;
	const ATDeviceVideoInfo& GetVideoInfo() override;
	vdpoint32 PixelToCaretPos(const vdpoint32& pixelPos) override;
	vdrect32 CharToPixelRect(const vdrect32& r) override;
	int ReadRawText(uint8 *dst, int x, int y, int n) override;
	uint32 GetActivityCounter() override;

public:		// IATDevicePBIConnection
	void InitPBI(IATDevicePBIManager *pbiman) override;

public:		// IATPBIDevice
	void GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const override;
	void SelectPBIDevice(bool enable) override;
	bool IsPBIOverlayActive() const override;
	uint8 ReadPBIStatus(uint8 busData, bool debugOnly) override;

private:
	sint32 OnRegisterDebugRead(uint32 addr);
	sint32 OnRegisterRead(uint32 addr);
	bool OnRegisterWrite(uint32 addr, uint8 value);
	bool OnVRAMWrite(uint32 addr, uint8 value);

	void GenerateActiveCharset();
	void UpdateVideoTiming();

	bool mbSelected = false;
	bool mbCharsetDirty = false;
	ATDeviceFirmwareStatus mFirmwareStatus = ATDeviceFirmwareStatus::Missing;
	uint32 mActivityCounter = 0;
	uint8 mRegIndex = 0;

	ATMemoryManager *mpMemMgr = nullptr;
	ATMemoryLayer *mpMemLayerFirmware = nullptr;
	ATMemoryLayer *mpMemLayerVRAM = nullptr;
	ATMemoryLayer *mpMemLayerRegisters = nullptr;

	ATFirmwareManager *mpFwMgr = nullptr;
	IATDevicePBIManager *mpPBIMgr = nullptr;
	IATDeviceVideoManager *mpVideoMgr = nullptr;

	ATCRTCEmulator mCRTC;

	ATDeviceVideoInfo mVideoInfo {};
	VDPixmapBuffer mFrameBuffer;
	uint32 mPalette[2] { 0, 0xFFFFFF };

	uint8 mROM[0x800] {};
	uint8 mVRAM[0x1000] {};
	uint8 mCharsetROM[0x800] {};
	uint8 mActiveCharset[256][8] {};
};

#endif
