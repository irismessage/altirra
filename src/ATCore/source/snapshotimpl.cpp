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

#include <stdafx.h>
#include <vd2/system/file.h>
#include <vd2/system/hash.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/serialization.h>

class ATInvalidSnapObjectIdException {};

///////////////////////////////////////////////////////////////////////////

const wchar_t *ATSnapObjectBase::GetDirectPackagingPath() const {
	return nullptr;
}

void ATSnapObjectBase::Deserialize(ATDeserializer& reader) {
	throw ATSerializationException();
}

void ATSnapObjectBase::DeserializeDirect(IVDStream& stream, uint32 len) {
	throw ATSerializationException();
}

void ATSnapObjectBase::Serialize(ATSerializer& writer) const {
	throw ATSerializationException();
}

void ATSnapObjectBase::SerializeDirect(IVDStream& stream) const {
	throw ATSerializationException();
}

bool ATSnapObjectBase::Difference(const IATObjectState& base, IATDeltaObject **result) {
	return false;
}

void ATSnapObjectBase::Accumulate(const IATDeltaObject& delta) {
}

///////////////////////////////////////////////////////////////////////////

ATSaveStateMemoryBuffer::ATSaveStateMemoryBuffer() {
}

ATSERIALIZATION_DEFINE(ATSaveStateMemoryBuffer);

const wchar_t *ATSaveStateMemoryBuffer::GetDirectPackagingPath() const {
	return mpDirectName;
}

void ATSaveStateMemoryBuffer::DeserializeDirect(IVDStream& stream, uint32 len) {
	mBuffer.resize(len);
	stream.Read(mBuffer.data(), len);
}

void ATSaveStateMemoryBuffer::SerializeDirect(IVDStream& stream) const {
	stream.Write(mBuffer.data(), mBuffer.size());
}

///////////////////////////////////////////////////////////////////////////

void ATSnapDecoder::Add(ATSerializationObjectId id, IATSnappable *obj, IATSerializable *snap) {
	VDASSERT(id != ATSerializationObjectId::Invalid);

	if (mObjects.size() <= (uint32)id)
		mObjects.resize((uint32)id + 1);

	SnappedObject& so = mObjects[(uint32)id - 1];
	so.mpLiveObject = obj;
	so.mpSnapObject = snap;
}

IATSnappable *ATSnapDecoder::TryGetObject(ATSerializationObjectId id) {
	return nullptr;
}

IATSnappable *ATSnapDecoder::MustGetObject(ATSerializationObjectId id) {
	if (id == ATSerializationObjectId::Invalid)
		return nullptr;

	IATSnappable *obj = TryGetObject(id);
	if (!obj)
		throw ATInvalidSnapObjectIdException();

	return obj;
}
