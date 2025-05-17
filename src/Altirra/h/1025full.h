//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#ifndef f_AT_1025FULL_H
#define f_AT_1025FULL_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <at/atcore/devicediskdrive.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcpu/co8051.h>
#include <at/atcpu/breakpoints.h>
#include <at/atcpu/history.h>
#include <at/atcpu/memorymap.h>
#include <at/atdebugger/breakpointsimpl.h>
#include <at/atdebugger/target.h>
#include <at/atcore/scheduler.h>
#include "diskdrivefullbase.h"
#include "diskinterface.h"
#include "printerbase.h"

class ATIRQController;
class ATModemEmulator;

class ATDevice1025Full final : public ATDevice
	, public IATDeviceFirmware
	, public ATDeviceSIO
	, public ATDiskDriveDebugTargetControl
	, public IATDeviceRawSIO
{
public:
	ATDevice1025Full();
	~ATDevice1025Full();

	void *AsInterface(uint32 iid) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;
	void WarmReset() override;
	void ComputerColdReset() override;
	void PeripheralColdReset() override;

public:		// IATDeviceFirmware
	void InitFirmware(ATFirmwareManager *fwman) override;
	bool ReloadFirmware() override;
	const wchar_t *GetWritableFirmwareDesc(uint32 idx) const override;
	bool IsWritableFirmwareDirty(uint32 idx) const override;
	void SaveWritableFirmware(uint32 idx, IVDStream& stream) override;
	ATDeviceFirmwareStatus GetFirmwareStatus() const override;

public:		// ATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;

public:	// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

public:	// IATDeviceRawSIO
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnBeginReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnTruncateByte() override;
	void OnSendReady() override;

protected:
	void Sync();

	void AddCommandEdge(uint32 polarity);

	bool OnReadT0() const;
	bool OnReadT1() const;
	uint8 OnReadPort(uint8 addr, uint8 output);
	void OnWritePort(uint8 addr, uint8 output);

	uint8 OnReadXRAM(uint16 addr) const;
	void OnWriteXRAM(uint16 addr, uint8 v);

	void OnSerialXmit(uint8 v);

	enum : uint32 {
		kEventId_DriveReceiveByte = kEventId_FirstCustom,
		kEventId_DriveSentByte,
	};

	ATEvent *mpEventDriveReceiveByte = nullptr;
	ATEvent *mpEventDriveSentByte = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;

	ATFirmwareManager *mpFwMgr = nullptr;
	bool mbFirmwareUsable = false;

	uint8 mSerialPendingReceiveByte = 0;

	bool mbCommandState = false;
	bool mbDirectReceiveOutput = true;
	bool mbDirectTransmitOutput = true;

	uint8 mP0 = 0xFF;
	uint8 mP1 = 0xFF;

	uint8 m8155PortA = 0;
	uint8 m8155PortB = 0;

	uint8 mFeedPosition = 0;
	int mPrintHeadPosition = 0;
	uint32 mLastHeadStepTime = 0;
	uint32 mLastHeadStepPeriod = 0;
	int mLastHeadStepDistance = 0;

	vdrefptr<IATPrinterGraphicalOutput> mpPrinterGraphicalOutput;

	ATDiskDriveSerialByteTransmitQueue mSerialXmitQueue;
	ATDiskDriveSerialCommandQueue mSerialCmdQueue;
		
	class TargetProxy final : public IATDiskDriveDebugTargetProxy {
	public:
		TargetProxy(ATDevice1025Full& parent);

		std::pair<const uintptr *, const uintptr *> GetReadWriteMaps() const;
		void SetHistoryBuffer(ATCPUHistoryEntry *harray) override;
		uint32 GetHistoryCounter() const override;
		uint32 GetTime() const override;
		uint32 GetStepStackLevel() const override;
		void GetExecState(ATCPUExecState& state) const override;
		void SetExecState(const ATCPUExecState& state) override;

	private:
		ATDevice1025Full& mParent;
	};

	TargetProxy mTargetProxy;
	ATCoProc8051 mCoProc;

	ATPrinterSoundSource mPrinterSoundSource;

	alignas(2) uint8 m8155RAM[256] {};
	alignas(2) uint8 mXROM[0x10000 + 4] = {};
	ATDebugTargetBreakpointsImplT<0x1000> mBreakpointsImpl;

	uintptr mDebugReadMap[256];
	uintptr mDebugWriteMap[256];
	uint8 mDummyRead[256] {};
	uint8 mDummyWrite[256] {};
};

#endif
