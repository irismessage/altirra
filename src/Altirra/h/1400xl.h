//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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

#ifndef f_AT_1400XL_H
#define f_AT_1400XL_H

#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicepbi.h>
#include <at/atemulation/acia.h>

class AT1400XLDevice;
class ATMemoryLayer;
class ATModemEmulator;
class IATAudioMixer;

class AT1400XLPBIDevice : public IATPBIDevice {
	AT1400XLPBIDevice(const AT1400XLPBIDevice&) = delete;
	AT1400XLPBIDevice& operator=(const AT1400XLPBIDevice&) = delete;
public:
	AT1400XLPBIDevice(AT1400XLDevice& parent);

	void Init();
	void Shutdown();

protected:
	AT1400XLDevice& mParent;
	IATDevicePBIManager *mpPBIMgr = nullptr;
};

class AT1400XLVDevice final : public AT1400XLPBIDevice, public IATSchedulerCallback {
public:
	using AT1400XLPBIDevice::AT1400XLPBIDevice;

	~AT1400XLVDevice();

	void Init();
	void Shutdown();

	void ColdReset();
	void WarmReset();

	void WriteByte(uint8 addr, uint8 value);

public:
	void OnScheduledEvent(uint32 id) override;

public:
	void GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const override;
	void SelectPBIDevice(bool enable) override;
	bool IsPBIOverlayActive() const override;
	uint8 ReadPBIStatus(uint8 busData, bool debugOnly) override;

private:
	void SetIRQEnabled(bool irqen);

	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEventVotraxIRQ = nullptr;
	uint64 mVotraxRequestTime = 0;
	bool mbIRQEnabled = false;
	uint8 mVotraxLatch = 0;
};

class AT1400XLTDevice final : public AT1400XLPBIDevice, public IATSchedulerCallback {
public:
	using AT1400XLPBIDevice::AT1400XLPBIDevice;

	~AT1400XLTDevice();

	void Init(ATModemEmulator& modemDevice);
	void Shutdown();

	void Reset();

	sint32 DebugReadByte(uint32 addr) const;
	sint32 ReadByte(uint32 addr);
	void WriteByte(uint32 addr, uint8 value);

public:
	void OnScheduledEvent(uint32 id) override;

public:
	void GetPBIDeviceInfo(ATPBIDeviceInfo& devInfo) const override;
	void SelectPBIDevice(bool enable) override;
	bool IsPBIOverlayActive() const override;
	uint8 ReadPBIStatus(uint8 busData, bool debugOnly) override;

private:
	void TryReceive();

	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEventPickUp = nullptr;
	bool mbIRQPending = false;
	uint8 mControlReg = 0;

	IATAudioMixer *mpAudioMixer = nullptr;
	bool mbInternalAudioBlocked = false;

	vdrefptr<ATModemEmulator> mpModemDevice;

	ATACIA6551Emulator mACIA;
};

class AT1400XLDevice final : public ATDeviceT<IATDeviceFirmware, IATDeviceMemMap> {
public:
	static constexpr uint32 kTypeID = "AT1400XLDevice"_vdtypeid;

	AT1400XLDevice();
	~AT1400XLDevice();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& pset) override;
	bool SetSettings(const ATPropertySet& pset) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;
	void WarmReset() override;

	void EnableFirmware(bool enable, uint32 offset, uint8 regmask);

	bool GetDiskAttnState() const;
	void SetDiskAttnState(bool attn);
	void SetOnDiskAttn(vdfunction<void(bool)> fn);

	sint32 DebugReadByte(uint32 addr) const;
	sint32 ReadByte(uint32 addr);
	bool WriteByte(uint32 addr, uint8 value);

public:	// IATDeviceFirmware
	void InitFirmware(ATFirmwareManager *fwman) override;
	bool ReloadFirmware() override;
	const wchar_t *GetWritableFirmwareDesc(uint32 idx) const override;
	bool IsWritableFirmwareDirty(uint32 idx) const override;
	void SaveWritableFirmware(uint32 idx, IVDStream& stream) override;
	ATDeviceFirmwareStatus GetFirmwareStatus() const override;

public:	// IATDeviceMemMap
	void InitMemMap(ATMemoryManager *memmap) override;
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

private:
	ATFirmwareManager *mpFwMgr = nullptr;
	ATDeviceFirmwareStatus mFirmwareStatus = ATDeviceFirmwareStatus::Invalid;

	ATMemoryManager *mpMemMgr = nullptr;
	ATMemoryLayer *mpMemLayerFirmware = nullptr;
	ATMemoryLayer *mpMemLayerHardware = nullptr;
	uint8 mActiveRegMask = 0;

	vdfunction<void(bool)> mpFnOnDiskAttn;
	bool mbDiskAttn = false;

	vdrefptr<ATModemEmulator> mpModemDevice;

	AT1400XLTDevice mTDevice;
	AT1400XLVDevice mVDevice;

	alignas(4) uint8 mFirmware[4096] {};
};

#endif
