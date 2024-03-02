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

#ifndef f_AT_ATCORE_SNAPPABLE_H
#define f_AT_ATCORE_SNAPPABLE_H

#include <vd2/system/refcount.h>

class ATSnapEncoder;
class IATObjectState;

// IATSnappableObject
//
// A runtime object that can produce and consume snapshots.
//
class IATSnappable {
public:
	// Capture state from the live object and store it in a save state object 'result'. This
	// must be an object returned by CreateSnapshot() on the same object.
	virtual void CaptureSnapshot(ATSnapEncoder& encoder, IATObjectState **result) = 0;

	// Apply a snapshot to the live object. Returns true if fully successful, false if
	// partially successful, throws if error.
	virtual bool ApplySnapshot(IATObjectState& state) = 0;
};

#endif
