//	Altirra - Atari 800/800XL/5200 emulator
//	Core library -- disk drive interfaces
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

//=========================================================================
// Disk drive interface
//
// A device exposes a disk drive interface to interface to the common disk
// drive slots in the emulator. This allows disks to be mounted and
// explored as usual even when an unusual disk drive device is present.
// The built-in disk drive emulator is automatically suppressed.

#ifndef f_AT_ATCORE_DEVICEDISKDRIVE_H
#define f_AT_ATCORE_DEVICEDISKDRIVE_H

#include <vd2/system/vdtypes.h>

class ATDiskInterface;
class IATDiskImage;

class IATDiskDriveManager {
public:
	virtual ATDiskInterface *GetDiskInterface(uint32 index) = 0;
};

class IATDiskInterfaceClient {
public:
	virtual void OnDiskChanged(bool mediaRemoved) = 0;
	virtual void OnWriteModeChanged() = 0;
	virtual void OnTimingModeChanged() = 0;
	virtual void OnAudioModeChanged() = 0;
	virtual bool IsImageSupported(const IATDiskImage& image) const = 0;
};

struct ATDeviceDiskDriveInterfaceClient {
	IATDiskInterfaceClient *mpClient;
	uint32 mInterfaceIndex;
};

class IATDeviceDiskDrive {
public:
	enum : uint32 { kTypeID = 'atdd' };

	virtual void InitDiskDrive(IATDiskDriveManager *ddm) = 0;

	// Returns the given bound disk drive interface client on this device. Index ranges from [0,N-1] where
	// N is the number of bound disk drive interface clients on this device. Higher indices return null.
	virtual ATDeviceDiskDriveInterfaceClient GetDiskInterfaceClient(uint32 index) = 0;
};

#endif
