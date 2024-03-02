//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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

#ifndef f_AT_BLACKBOX_H
#define f_AT_BLACKBOX_H

#include <vd2/system/vdstl.h>
#include <vd2/system/refcount.h>
#include <at/atcore/deviceimpl.h>
#include "pia.h"
#include "via.h"
#include "acia.h"
#include "scsi.h"
#include "rs232.h"
#include "scheduler.h"

class ATMemoryLayer;
class ATIRQController;
class IATIDEDisk;
class IATDeviceSerial;
class IATPrinterOutput;

class ATBlackBoxEmulator final : public ATDevice
	, public IATDeviceMemMap
	, public IATDeviceFirmware
	, public IATDeviceIRQSource
	, public IATDeviceScheduling
	, public IATDeviceButtons
	, public IATDeviceParent
	, public IATDeviceIndicators
	, public IATDevicePrinter
	, public IATSCSIBusMonitor
	, public IATSchedulerCallback
{
public:
	ATBlackBoxEmulator();
	~ATBlackBoxEmulator();

	void *AsInterface(uint32 iid);

	virtual void GetDeviceInfo(ATDeviceInfo& info);
	virtual void GetSettings(ATPropertySet& settings);
	virtual bool SetSettings(const ATPropertySet& settings);
	virtual void Init();
	virtual void Shutdown();
	virtual void WarmReset();
	virtual void ColdReset();

public:
	virtual void InitMemMap(ATMemoryManager *memmap) override;
	virtual bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

public:
	virtual void InitFirmware(ATFirmwareManager *fwman);
	virtual bool ReloadFirmware();

public:
	virtual void InitIRQSource(ATIRQController *fwirq);

public:
	virtual void InitScheduling(ATScheduler *sch, ATScheduler *slowsch);

public:
	virtual void ActivateButton(uint32 idx);

public:
	virtual const char *GetSupportedType(uint32 index) override;
	virtual void GetChildDevices(vdfastvector<IATDevice *>& devs) override;
	virtual void AddChildDevice(IATDevice *dev) override;
	virtual void RemoveChildDevice(IATDevice *dev) override;

public:
	virtual void InitIndicators(IATUIRenderer *r);

public:
	void SetPrinterOutput(IATPrinterOutput *out) override;

public:
	virtual void OnSCSIControlStateChanged(uint32 state);

public:
	virtual void OnScheduledEvent(uint32 id);

public:
	void OnControlStateChanged(const ATDeviceSerialStatus& status);

protected:
	static sint32 OnDebugRead(void *thisptr, uint32 addr);
	static sint32 OnRead(void *thisptr, uint32 addr);
	static bool OnWrite(void *thisptr, uint32 addr, uint8 value);
	static void OnPIAOutputChanged(void *data, uint32 outputState);
	static void OnVIAOutputChanged(void *thisptr, uint32 val);
	void OnVIAIRQStateChanged(bool active);

	void OnACIAIRQStateChanged(bool active);
	void OnACIAReceiveReady();
	void OnACIATransmit(uint8 data, uint32 baudRate);
	void UpdateSerialControlLines();

	void SetPBIBANK(uint8 bank);
	void SetRAMPAGE(uint8 page, bool hiselect);
	void UpdateDipSwitches();

	uint8	mPBIBANK;
	uint8	mRAMPAGE;
	uint8	mActiveIRQs;
	uint8	mDipSwitches;
	bool	mbHiRAMEnabled;
	uint8	mRAMPAGEMask;
	bool	mbFirmware32K;
	bool	mbSCSIBlockSize256;

	ATFirmwareManager *mpFwMan;
	IATUIRenderer *mpUIRenderer;

	ATIRQController *mpIRQController;
	uint32	mIRQBit;

	ATScheduler *mpScheduler;
	ATEvent *mpEventsReleaseButton[2];
	ATMemoryManager *mpMemMan;
	ATMemoryLayer *mpMemLayerPBI;
	ATMemoryLayer *mpMemLayerRAM;
	ATMemoryLayer *mpMemLayerFirmware;

	IATPrinterOutput *mpPrinterOutput;

	struct SCSIDiskEntry {
		IATDevice *mpDevice;
		IATSCSIDevice *mpSCSIDevice;
		IATIDEDisk *mpDisk;
	};

	vdfastvector<SCSIDiskEntry> mSCSIDisks;

	IATDeviceSerial *mpSerialDevice;
	bool mbRTS;
	bool mbDTR;
	uint8 mSerialCtlInputs;
	bool mbLastPrinterStrobe;

	ATPIAEmulator mPIA;
	ATVIA6522Emulator mVIA;
	ATACIA6551Emulator mACIA;
	ATSCSIBusEmulator mSCSIBus;

	uint8 mRAM[0x10000];
	uint8 mFirmware[0x10000];
};

#endif
