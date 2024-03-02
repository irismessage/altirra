//	Altirra - Atari 800/800XL/5200 emulator
//	Core library -- System control interfaces
//	Copyright (C) 2009-2017 Avery Lee
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

//=========================================================================
// System control interface
//
// The system control interface allows a device to control some central
// aspects of the CPU, notably the ROM mappings and the CPU. This is used
// to emulate some behaviors of an MMU or a CPU accelerator.
//

#ifndef f_AT_ATCORE_SYSTEMCONTROL_H
#define f_AT_ATCORE_SYSTEMCONTROL_H

#include <vd2/system/unknown.h>

class ATMemoryLayer;
class IATDeviceSystemControl;

class IATSystemController {
public:
	// Reset the CPU (and only the CPU).
	virtual void ResetCPU() = 0;

	// Assert /ABORT on the 65C816.
	virtual void AssertABORT() = 0;

	// Override the CPU mode to 6502/65C816 with the given multiplier and
	// then reset the CPU. The override is automatically removed when the
	// device is removed.
	virtual void OverrideCPUMode(IATDeviceSystemControl *source, bool use816, uint32 multiplier) = 0;

	// Modify the kernel ROM used by the system controller for kernel ROM
	// mappings ($5000-57FF, $C000-CFFF, $D800-FFFF). This is required for
	// hooks to properly update. The override is removed on null or implicitly
	// on device removal. 'highSpeed' is <0 for force off, >0 for force on,
	// and 0 for default.
	virtual void OverrideKernelMapping(IATDeviceSystemControl *source, const void *kernelROM, sint8 highSpeed, bool priority) = 0;
};

class IATDeviceSystemControl {
public:
	enum : uint32 { kTypeID = 'dscn' };

	virtual void InitSystemControl(IATSystemController *sysctrl) = 0;
	virtual void SetROMLayers(
		ATMemoryLayer *layerLowerKernelROM,
		ATMemoryLayer *layerUpperKernelROM,
		ATMemoryLayer *layerBASICROM,
		ATMemoryLayer *layerSelfTestROM,
		ATMemoryLayer *layerGameROM,
		const void *kernelROM) = 0;
};

#endif
