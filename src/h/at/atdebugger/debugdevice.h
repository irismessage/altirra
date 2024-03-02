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

#ifndef f_AT_ATDEBUGGER_DEBUGDEVICE_H
#define f_AT_ATDEBUGGER_DEBUGDEVICE_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/unknown.h>

class IATDebugger;

class IATDebugDeviceCallback {
public:
	virtual void OnDebugLinkIdentify() = 0;
	virtual void OnSDXSymbolStart() = 0;
	virtual void OnSDXSymbolAdd(uint32 addr, const char *name) = 0;
	virtual void OnSDXSymbolEnd() = 0;
	virtual void OnSDXProcessStart(const char *name) = 0;
};

class IATDebugDevice : public IVDUnknown {
public:
	static constexpr uint32 kTypeID = "IATDebugDevice"_vdtypeid;

	virtual void SetDebugger(const char *name, IATDebugDeviceCallback& debugger) = 0;
};

void ATCreateDeviceDebug(const ATPropertySet& pset, IATDevice **dev);

#endif
