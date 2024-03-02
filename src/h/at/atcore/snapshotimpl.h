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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_ATCORE_SNAPSHOTIMPL_H
#define f_AT_ATCORE_SNAPSHOTIMPL_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <at/atcore/snapshot.h>
#include <at/atcore/snappable.h>
#include <at/atcore/serialization.h>

class ATSnapObjectBase : public vdrefcounted<IATObjectState> {
public:
	const wchar_t *GetDirectPackagingPath() const override;
	void Deserialize(ATDeserializer& reader) override;
	void DeserializeDirect(IVDStream& stream, uint32 len) override;
	void Serialize(ATSerializer& writer) const override;
	void SerializeDirect(IVDStream& stream) const override;
	bool Difference(const IATObjectState& base, IATDeltaObject **result) override;
	void Accumulate(const IATDeltaObject& delta) override;
};

template<typename T>
class ATSnapObject : public ATSnapObjectBase {
public:
	const ATSerializationTypeDef& GetSerializationType() const override {
		return *ATSerializationTypeRef<T>;
	}
};

template<typename T>
class ATSnapExchangeObject : public ATSnapObject<T> {
public:
	void Deserialize(ATDeserializer& reader) override {
		static_cast<T *>(this)->Exchange(reader);
	}

	void Serialize(ATSerializer& writer) const override {
		const_cast<T *>(static_cast<const T *>(this))->Exchange(writer);
	}
};

class ATSaveStateMemoryBuffer final : public ATSnapObject<ATSaveStateMemoryBuffer> {
public:
	ATSaveStateMemoryBuffer();

	const wchar_t *GetDirectPackagingPath() const override;
	void DeserializeDirect(IVDStream& stream, uint32 len) override;
	void SerializeDirect(IVDStream& stream) const override;

	const wchar_t *mpDirectName = nullptr;
	vdblock<uint8> mBuffer;
};

class ATSnapDecoder {
public:
	void Add(ATSerializationObjectId id, IATSnappable *obj, IATSerializable *snap);

	// Try to obtain an object by ID; either returns the object or null if
	// it is not available.
	IATSnappable *TryGetObject(ATSerializationObjectId id);

	// Obtain an object by ID; throws if the object is not available. Note
	// that this will legitimately return null if the id is Invalid.
	IATSnappable *MustGetObject(ATSerializationObjectId id);

private:
	struct SnappedObject {
		IATSnappable *mpLiveObject;
		vdrefptr<IATSerializable> mpSnapObject;
	};

	vdvector<SnappedObject> mObjects;
};

#endif
