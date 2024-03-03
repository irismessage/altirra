//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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

#ifndef f_AT_ATCORE_DEVICESAVESTATE_H
#define f_AT_ATCORE_DEVICESAVESTATE_H

#include <vd2/system/unknown.h>
#include <at/atcore/snapshot.h>

template<typename T> class vdrefptr;

struct ATSnapshotContext;

struct ATSnapshotStatus {
	bool mbInDiskTransfer = false;
	bool mbPartialAccuracy = false;
};

// IATDeviceSnapshot
//
// Interface for devices that support snapshotting, which is required for
// save states.
//
class IATDeviceSnapshot {
public:
	static constexpr auto kTypeID = "IATDeviceSnapshot"_vdtypeid;
	
	// Check if there are any caveats to taking a snapshot at the current state.
	// Status is accumulated; impls may set state but should not clear them.
	virtual void GetSnapshotStatus(ATSnapshotStatus& status) const {}

	// Load state from the given state object, which is expected to be of compatible
	// type to that produced by SaveState() (but still needs to be checked). The
	// state object can also be null, in which case it is regarded as missing and
	// the device should be reset as no load state is available.
	virtual void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) = 0;

	// Save device state to a new state object. Returning null is allowed, which
	// means that the device has no state to save.
	virtual vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const = 0;
};

#endif
