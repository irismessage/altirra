//	Altirra - Atari 800/800XL/5200 emulator
//	I/O library - save state common definitions
//	Copyright (C) 2008-2016 Avery Lee
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
// Save states are currently read as blobs, so there's not a lot here.
//

#ifndef f_AT_ATIO_SAVESTATE_H
#define f_AT_ATIO_SAVESTATE_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <vd2/system/function.h>
#include <at/atio/image.h>

class IATSerializable;
class VDZipArchive;

extern const uint8 kATSaveStateHeader[12];

class IATSaveStateImage2 : public IATImage {
public:
	enum : uint32 { kTypeID = 'ssim' };
	
	virtual const IATSerializable *GetRoot() const = 0;
};

void ATSetSaveState2Reader(vdfunction<void (VDZipArchive&, IATSerializable **)> fn);
void ATReadSaveState2(VDZipArchive& zip, IATSaveStateImage2 **saveState);

#endif	// f_AT_ATIO_SAVESTATE_H
