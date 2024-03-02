//	Altirra - Atari 800/800XL/5200 emulator
//	Core library -- Controller port interfaces
//	Copyright (C) 2009-2016 Avery Lee
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
// Device controller port interface
//
//

#ifndef f_AT_ATCORE_DEVICEPORT_H
#define f_AT_ATCORE_DEVICEPORT_H

#include <vd2/system/unknown.h>

typedef void (*ATPortOutputFn)(void *data, uint32 outputState);

class IATDevicePortManager {
public:
	virtual int AllocInput() = 0;
	virtual void FreeInput(int index) = 0;
	virtual void SetInput(int index, uint32 rval) = 0;

	virtual uint32 GetOutputState() const = 0;
	virtual int AllocOutput(ATPortOutputFn fn, void *ptr, uint32 changeMask) = 0;
	virtual void FreeOutput(int index) = 0;
};

#endif
