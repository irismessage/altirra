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

#ifndef f_AT_ATCORE_DEVICE_H
#define f_AT_ATCORE_DEVICE_H

#include <vd2/system/unknown.h>
#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>

class IVDStream;
class ATMemoryManager;
class ATFirmwareManager;
class ATIRQController;
class ATScheduler;
class ATPropertySet;
class IATDevice;
class IATUIRenderer;
class IATAudioOutput;
class ATAudioSyncMixer;
class ATConsoleOutput;
class ATPIAEmulator;
class IATDeviceCartridge;
class IATDevicePortManager;
class IATDeviceIndicatorManager;

typedef void (*ATDeviceFactoryFn)(const ATPropertySet& pset, IATDevice **);

struct ATDeviceDefinition {
	const char *mpTag;
	const char *mpConfigTag;
	const wchar_t *mpName;
	ATDeviceFactoryFn mpFactoryFn;
};

struct ATDeviceInfo {
	const ATDeviceDefinition *mpDef;
};

class IATDeviceMemMap {
public:
	enum { kTypeID = 'admm' };

	virtual void InitMemMap(ATMemoryManager *memmap) = 0;

	/// Enumerates memory regions used by the device in the form [lo, hi] for
	/// indices starting from 0. Returns false if there are no more regions.
	/// For instance, [0xD500, 0xD5FF] indicates full use of the CCTL region.
	///
	/// This routine should indicate what regions may be used, not what regions
	/// are currently in use. This is used by the HLE system to try to determine
	/// what regions are safe to use for callbacks. In particular, this routine
	/// needs to report overlaps in the $D1xx and $D500-D7FF regions.
	///
	virtual bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const = 0;
};

class IATDeviceFirmware {
public:
	enum { kTypeID = 'adfw' };

	virtual void InitFirmware(ATFirmwareManager *fwman) = 0;

	// Reload firmware from firmware manager. Returns true if firmware was actually
	// changed.
	virtual bool ReloadFirmware() = 0;

	virtual const wchar_t *GetWritableFirmwareDesc(uint32 idx) const = 0;
	virtual bool IsWritableFirmwareDirty(uint32 idx) const = 0;
	virtual void SaveWritableFirmware(uint32 idx, IVDStream& stream) = 0;
};

class IATDeviceIRQSource {
public:
	enum { kTypeID = 'adir' };

	virtual void InitIRQSource(ATIRQController *irqc) = 0;
};

class IATDeviceScheduling {
public:
	enum { kTypeID = 'adsc' };

	// The first scheduler is the fast scheduler, which runs at cycle granularity
	// and provides a common timebase for the emulator.
	//
	// The second scheduler is the slow scheduler. It runs at scanline rate (114
	// cycles) and has undefined phase relative to the first scheduler. It's designed
	// for tasks that can afford to run with a small amount of jitter and longer
	// delays, to take load off the fast scheduler (which has to deal with a lot
	// of REALLY high speed events).
	//
	virtual void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) = 0;
};

enum ATDeviceButton : uint32 {
	kATDeviceButton_BlackBoxDumpScreen,
	kATDeviceButton_BlackBoxMenu,
	kATDeviceButton_CartridgeResetBank,
	kATDeviceButton_CartridgeSDXEnable,
	kATDeviceButton_IDEPlus2SwitchDisks,
	kATDeviceButton_IDEPlus2WriteProtect,
	kATDeviceButton_IDEPlus2SDX,
	kATDeviceButton_IndusGTTrack,
	kATDeviceButton_IndusGTId,
	kATDeviceButton_IndusGTError,
	kATDeviceButton_IndusGTBootCPM,
	kATDeviceButton_HappySlow,
	kATDeviceButton_HappyWPEnable,
	kATDeviceButton_HappyWPDisable,
	kATDeviceButton_ATR8000Reset,
};

class IATDeviceButtons {
public:
	enum { kTypeID = 'adbt' };

	virtual uint32 GetSupportedButtons() const = 0;
	virtual bool IsButtonDepressed(ATDeviceButton idx) const = 0;
	virtual void ActivateButton(ATDeviceButton idx, bool state) = 0;
};

class IATDeviceParent {
public:
	enum { kTypeID = 'adpt' };

	virtual const char *GetSupportedType(uint32 index) = 0;
	virtual void GetChildDevices(vdfastvector<IATDevice *>& devs) = 0;
	virtual void AddChildDevice(IATDevice *dev) = 0;
	virtual void RemoveChildDevice(IATDevice *dev) = 0;
};

class IATDeviceIndicators {
public:
	enum { kTypeID = 'adin' };

	virtual void InitIndicators(IATDeviceIndicatorManager *r) = 0;
};

class IATDeviceAudioOutput {
public:
	enum { kTypeID = 'adao' };

	virtual void InitAudioOutput(IATAudioOutput *output, ATAudioSyncMixer *syncmixer) = 0;
};

class IATDeviceDiagnostics {
public:
	enum { kTypeID = 'addd' };

	virtual void DumpStatus(ATConsoleOutput& output) = 0;
};

class IATDevicePortInput {
public:
	enum { kTypeID = 'adpi' };

	virtual void InitPortInput(IATDevicePortManager *portmgr) = 0;
};

class IATDevice : public IVDRefUnknown {
public:
	enum { kTypeID = 'adev' };

	virtual IATDeviceParent *GetParent() = 0;
	virtual void SetParent(IATDeviceParent *parent) = 0;
	virtual void GetDeviceInfo(ATDeviceInfo& info) = 0;
	virtual void GetSettingsBlurb(VDStringW& buf) = 0;
	virtual void GetSettings(ATPropertySet& settings) = 0;
	virtual bool SetSettings(const ATPropertySet& settings) = 0;
	virtual void Init() = 0;
	virtual void Shutdown() = 0;

	// Return recommended power-on delay, in tenths of seconds. If the power-on delay is
	// set to 'auto', the simulator will wait the maximum of all device recommended
	// delays.
	virtual uint32 GetComputerPowerOnDelay() const = 0;

	virtual void WarmReset() = 0;
	virtual void ColdReset() = 0;
	virtual void ComputerColdReset() = 0;
	virtual void PeripheralColdReset() = 0;
};

#endif
