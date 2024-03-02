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

class ATMemoryManager;
class ATFirmwareManager;
class ATIRQController;
class ATScheduler;
class ATPropertySet;
class IATDevice;
class IATUIRenderer;
class IATAudioOutput;
class ATConsoleOutput;
class ATPIAEmulator;

struct ATDeviceInfo {
	VDStringA mTag;
	VDStringA mConfigTag;
	VDStringW mName;
};

class IATDeviceMemMap {
public:
	enum { kTypeID = 'admm' };

	virtual void InitMemMap(ATMemoryManager *memmap) = 0;

	virtual bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const = 0;
};

class IATDeviceFirmware {
public:
	enum { kTypeID = 'adfw' };

	virtual void InitFirmware(ATFirmwareManager *fwman) = 0;

	// Reload firmware from firmware manager. Returns true if firmware was actually
	// changed.
	virtual bool ReloadFirmware() = 0;
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

class IATDeviceButtons {
public:
	enum { kTypeID = 'adbt' };

	virtual void ActivateButton(uint32 idx) = 0;
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

	virtual void InitIndicators(IATUIRenderer *r) = 0;
};

class IATDeviceAudioOutput {
public:
	enum { kTypeID = 'adao' };

	virtual void InitAudioOutput(IATAudioOutput *output) = 0;
};

class IATDeviceDiagnostics {
public:
	enum { kTypeID = 'addd' };

	virtual void DumpStatus(ATConsoleOutput& output) = 0;
};

class IATDevicePortInput {
public:
	enum { kTypeID = 'adpi' };

	virtual void InitPortInput(ATPIAEmulator *pia) = 0;
};

class IATDevice : public IVDRefUnknown {
public:
	enum { kTypeID = 'adev' };

	virtual IATDeviceParent *GetParent() = 0;
	virtual void SetParent(IATDeviceParent *parent) = 0;
	virtual void GetDeviceInfo(ATDeviceInfo& info) = 0;
	virtual void GetSettings(ATPropertySet& settings) = 0;
	virtual bool SetSettings(const ATPropertySet& settings) = 0;
	virtual void Init() = 0;
	virtual void Shutdown() = 0;
	virtual void WarmReset() = 0;
	virtual void ColdReset() = 0;
};

#endif
