//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2022 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

//=========================================================================
// PIA interface
//
// The PIA interface provides access to the PIA inputs and outputs
// for the controller ports. Two types of connections can be registered,
// inputs to the PIA and outputs from the PIA. For all I/O operations,
// bits 0-7 contain port A data (controller ports 1-2) and bits 8-15
// contain port B data (controller ports 3-4). For the XL/XE series, which
// only has two controller ports, port B contains the banking data instead.
//
// "Input" and "output" in the PIA interface are from the standpoint of the
// PIA. Thus, inputs provide signals to the PIA, and outputs read signals
// from it.
//
// The device PIA interface is similar to the device port interface, with
// a couple of differences. The PIA interface is intended for devices that
// hook to the PIA internally while the device port interface is for
// external connections. In particular, this means that the port interface
// will block access to port B on XL/XE configurations while the PIA
// interface does not. The port interface also provides access to trigger
// inputs that are not connected to PIA.
//

#ifndef f_AT_ATCORE_DEVICEPIA_H
#define f_AT_ATCORE_DEVICEPIA_H

#include <vd2/system/function.h>
#include <vd2/system/unknown.h>

typedef void (*ATPIAOutputFn)(void *data, uint32 outputState);

class IATDevicePIA {
public:
	static constexpr uint32 kTypeID = "IATDevicePIA"_vdtypeid;

	static constexpr uint32 kMask_PortA = 0x00FF;
	static constexpr uint32 kMask_PortB = 0xFF00;
	static constexpr uint32 kMask_PB0 = 0x0100;
	static constexpr uint32 kMask_PB1 = 0x0200;
	static constexpr uint32 kMask_PB4 = 0x1000;
	static constexpr uint32 kMask_PB7 = 0x8000;

	// Allocate a new input to supply signals to the PIA. Returns an input index
	// or -1 if no inputs are available.
	virtual int AllocInput() = 0;

	// Free an input from AllocInput(). Silently ignored for invalid index (-1).
	virtual void FreeInput(int index) = 0;

	// Change the signals supplied to the PIA by an input. Redundant sets are
	// tossed and the call is silently ignored for an invalid index (-1).
	virtual void SetInput(int index, uint32 rval) = 0;

	// Get the current outputs from the PIA.
	virtual uint32 GetOutputState() const = 0;

	// Allocate a new output from the PIA. The output function is called
	// whenever a relevant change occurs, according to the supplied change
	// mask. The function is not called initially, so self-init must occur.
	// -1 is returned if no more output slots are available.
	virtual int AllocOutput(ATPIAOutputFn fn, void *ptr, uint32 changeMask) = 0;

	// Modify the change mask used to filter output change notifications to an
	// output.
	virtual void ModifyOutputMask(int index, uint32 changeMask) = 0;

	// Free an output allocated by AllocOutput(). It is OK to call this with
	// the invalid output ID (-1).
	virtual void FreeOutput(int index) = 0;

	virtual uint32 RegisterDynamicInput(bool portb, vdfunction<uint8()> fn) = 0;
	virtual void UnregisterDynamicInput(bool portb, uint32 token) = 0;
};

#endif
