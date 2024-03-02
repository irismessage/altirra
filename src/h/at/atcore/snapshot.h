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
//
//=========================================================================
// Snapshot system
//
// Snapshots hold state captured non-destructively from live objects and
// allow that state to be persisted and later restored. Snapshots are a
// middle ground between serialization and object cloning. They allow for
// persistence like serialization, but are meant to be more efficient to
// create and manipulate; at the same time, they are not live objects like
// clones would be. The snapshot system also cannot recreate an object
// graph, only mirror state into an existing one.
//
// The implementation of snapshots involves mirroring state from a live
// object graph into a snapshot object graph. The snapshot objects are
// designed to lightweight, often directly copied versions of memory
// structures. For instance, the snapshot of device memory may simply be
// a wrapped memory buffer. Once captured, the snapshot may then be
// persisted for offline storage. Snapshot objects in memory are opaque
// and assumed valid; validation and versioning are therefore deferred
// until serialization.
//
// Snapshot objects support equality comparison and hashing operations to
// aid with deduplication, since it is highly likely for some objects to
// not change between snapshots. For instance, a peripheral with 512K of
// device memory may not have been accessed between snapshots.
//
// It is also possible to difference two snapshot objects of the same type
// to produce a delta object. This is advantageous for memory buffers that
// are large but only change in portions. Differencing two full objects
// produces a delta object and adding a delta object to a full object
// produces another full object. The difference operation may simply
// result in a full object if it isn't supported or is non-advantageous
// (delta encoding a single int is pointless). Deltas cannot be
// serialized and are not validated.
//
// Snapshot objects are assumed to be matched to their runtime objects and
// are not dynamically type checked.

#ifndef f_AT_ATCORE_SNAPSHOT_H
#define f_AT_ATCORE_SNAPSHOT_H

#include <vd2/system/refcount.h>
#include <at/atcore/serializable.h>

class ATDeserializer;
class ATSerializer;
class IVDStream;

class IATDeltaObject : public IVDRefCount {};

struct ATSnapshotContext {
	// If set, storage is specifically excluded from snapshot operations: they will
	// not be captured to or restored from snapshots. This includes both actual storage
	// data as well as external storage signatures. Setting this flag therefore avoids
	// rolling back storage when restoring a snapshot or save state.
	//
	// A corner case occurs when loading storage is enabled but the snapshot wasn't
	// saved with it. The expectation in that case is that existing storage is kept
	// and not reset. That means that whether storage was skipped also needs to be
	// saved into the snapshot. In most cases, this is done by saving a null memory
	// buffer reference.
	//
	bool mbSkipStorage = false;
};

class IATObjectState : public IATSerializable {
public:
	// Compute the difference between this object and a base object. Returns true and a delta
	// object or false otherwise if no delta can/should be created. If the operation succeeds
	// and no delta object is produced, the base and target objects are equal.
	virtual bool Difference(const IATObjectState& base, IATDeltaObject **result) = 0;

	// Accumulate a delta into this object, updating this object with the delta applied to
	// the base. This is only valid for a delta originally generated off of an equivalent
	// base; extrapolation is not allowed.
	virtual void Accumulate(const IATDeltaObject& delta) = 0;
};

#endif
