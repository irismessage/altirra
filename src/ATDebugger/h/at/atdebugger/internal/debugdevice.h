//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2021 Avery Lee
//	Debugger module - debug link device
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

#ifndef f_AT_ATDEBUGGER_INTERNAL_DEBUGDEVICE_H
#define f_AT_ATDEBUGGER_INTERNAL_DEBUGDEVICE_H

#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atdebugger/debugdevice.h>

class ATDebugDevice final : public ATDevice, public ATDeviceSIO, public IATDebugDevice {
public:		// IATDevice
	void *AsInterface(uint32 id) override;
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;

public:		// IATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;

	CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest& request) override;
	void OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) override;

public:		// IATDebugDevice
	void SetDebugger(const char *name, IATDebugDeviceCallback& debugger) override;

private:
	enum ReceiveId : uint32 {
		kReceiveId_AddSDXSymbol = 1,
		kReceiveId_ProcessStartName
	};

	IATDebugDeviceCallback *mpDebugger = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;

	VDStringA mDebuggerName;
};

#endif
