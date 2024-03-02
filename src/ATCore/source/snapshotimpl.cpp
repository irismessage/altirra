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

void *ATSnapObjectBase::AsInterface(uint32 iid) {
	if (iid == IATSerializable::kTypeID)
		return static_cast<IATSerializable *>(this);

	return nullptr;
}

const wchar_t *ATSnapObjectBase::GetDirectPackagingPath() const {
	return nullptr;
}

bool ATSnapObjectBase::SupportsDirectDeserialization() const {
	return false;
}

void ATSnapObjectBase::Deserialize(ATDeserializer& reader) {
	throw ATSerializationException();
}

void ATSnapObjectBase::DeserializeDirect(IVDStream& stream, uint32 len) {
	throw ATSerializationException();
}

void ATSnapObjectBase::DeserializeDirectDeferred(IATDeferredDirectDeserializer& defSer) {
	throw ATSerializationException();
}

void ATSnapObjectBase::PostDeserialize() {
	// do nothing
}

void ATSnapObjectBase::Serialize(ATSerializer& writer) const {
	throw ATSerializationException();
}

void ATSnapObjectBase::SerializeDirect(IVDStream& stream) const {
	throw ATSerializationException();
}

void ATSnapObjectBase::SerializeDirectAndRelease(IVDStream& stream) {
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

const vdfastvector<uint8>& ATSaveStateMemoryBuffer::GetReadBuffer() const {
	if (mpDeferredSerializer) {
		auto ds { std::move(mpDeferredSerializer) };
		mpDeferredSerializer = nullptr;

		ds->DeserializeDirect(const_cast<ATSaveStateMemoryBuffer&>(*this));
	}

	return mBuffer;
}

void ATSaveStateMemoryBuffer::ReleaseReadBuffer() {
	vdfastvector<uint8> v;
	v.swap(mBuffer);
}

void ATSaveStateMemoryBuffer::PrefetchReadBuffer() {
	if (mpDeferredSerializer)
		mpDeferredSerializer->Prefetch();
}

vdfastvector<uint8>& ATSaveStateMemoryBuffer::GetWriteBuffer() {
	return mBuffer;
}

const wchar_t *ATSaveStateMemoryBuffer::GetDirectPackagingPath() const {
	return mpDirectName;
}

bool ATSaveStateMemoryBuffer::SupportsDirectDeserialization() const {
	return true;
}

void ATSaveStateMemoryBuffer::Deserialize(ATDeserializer& reader) {
	reader.Transfer("data", &mBuffer);
}

void ATSaveStateMemoryBuffer::DeserializeDirect(IVDStream& stream, uint32 len) {
	mBuffer.resize(len);
	stream.Read(mBuffer.data(), len);
}

void ATSaveStateMemoryBuffer::DeserializeDirectDeferred(IATDeferredDirectDeserializer& defSer) {
	mpDeferredSerializer = &defSer;
}

void ATSaveStateMemoryBuffer::Serialize(ATSerializer& writer) const {
	writer.Transfer("data", &mBuffer);
}

void ATSaveStateMemoryBuffer::SerializeDirect(IVDStream& stream) const {
	stream.Write(mBuffer.data(), mBuffer.size());
}

void ATSaveStateMemoryBuffer::SerializeDirectAndRelease(IVDStream& stream) {
	stream.Write(mBuffer.data(), mBuffer.size());

	vdfastvector<uint8> emptyBuf;
	emptyBuf.swap(mBuffer);
}
