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

#ifndef f_AT_ATCORE_SNAPSHOTIMPL_H
#define f_AT_ATCORE_SNAPSHOTIMPL_H

#include <vd2/system/function.h>
#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <at/atcore/snapshot.h>
#include <at/atcore/serialization.h>

class ATSnapObjectBase : public vdrefcounted<IATObjectState> {
public:
	void *AsInterface(uint32 iid) override;
	const wchar_t *GetDirectPackagingPath() const override;
	bool SupportsDirectDeserialization() const override;
	void Deserialize(ATDeserializer& reader) override;
	void DeserializeDirect(IVDStream& stream, uint32 len) override;
	void DeserializeDirectDeferred(IATDeferredDirectDeserializer& defSer) override;
	void PostDeserialize() override;
	void Serialize(ATSerializer& writer) const override;
	void SerializeDirect(IVDStream& stream) const override;
	void SerializeDirectAndRelease(IVDStream& stream) override;
	bool Difference(const IATObjectState& base, IATDeltaObject **result) override;
	void Accumulate(const IATDeltaObject& delta) override;
};

template<typename... T_ExtraBases>
class ATSnapObjectBases : public ATSnapObjectBase, public T_ExtraBases... {
public:
	int AddRef() override {
		return ATSnapObjectBase::AddRef();
	}

	int Release() override {
		return ATSnapObjectBase::Release();
	}

	void *AsInterface(uint32 iid) override {
		void *p = nullptr;

		if (( ... || (p = TryAsInterface<T_ExtraBases>(iid))))
			return p;

		return ATSnapObjectBase::AsInterface(iid);
	}

private:
	template<typename T> requires VDUnknownIdentifiable<T>
	void *TryAsInterface(uint32 iid) {
		return iid == T::kTypeID ? static_cast<T *>(this) : nullptr;
	}

	template<typename T> requires (!VDUnknownIdentifiable<T>)
	void *TryAsInterface(uint32 iid) {
		return nullptr;
	}
};

template<typename T, ATSerializationStaticName T_Name, typename... T_ExtraBases>
class ATSnapObject : public std::conditional_t<(sizeof...(T_ExtraBases)>0), ATSnapObjectBases<T_ExtraBases...>, ATSnapObjectBase> {
public:
	const ATSerializationTypeDef& GetSerializationType() const override {
		return g_ATSerializationAutoRegister<T, T_Name>.GetTypeDef();
	}

private:
};

// Based class used by most snapshottable objects.
//
// This base class routes both the Serialize() and Deserialize() calls into a single
// Exchange() method taking the (de)serializer as a template parameter. This allows
// the paths to be mostly merged in source while having separately optimized runtime
// paths. The argument is guaranteed to be convertable to ATSerializer& or
// ATDeserializer& and Exchange() can be pre-instantiated only for those two paths.
//
template<typename T, ATSerializationStaticName T_Name, typename... T_ExtraBases>
class ATSnapExchangeObject : public ATSnapObject<T, T_Name, T_ExtraBases...> {
public:
	void Deserialize(ATDeserializer& reader) override {
		static_cast<T *>(this)->Exchange(reader);
	}

	void Serialize(ATSerializer& writer) const override {
		const_cast<T *>(static_cast<const T *>(this))->Exchange(writer);
	}
};

// Common snappable type for a plain unstructured memory buffer.
class ATSaveStateMemoryBuffer final : public ATSnapObject<ATSaveStateMemoryBuffer, "ATSaveStateMemoryBuffer"> {
public:
	ATSaveStateMemoryBuffer();

	const vdfastvector<uint8>& GetReadBuffer() const;
	void ReleaseReadBuffer();
	void PrefetchReadBuffer();

	vdfastvector<uint8>& GetWriteBuffer();

	const wchar_t *GetDirectPackagingPath() const override;
	bool SupportsDirectDeserialization() const override;
	void Deserialize(ATDeserializer& reader) override;
	void DeserializeDirect(IVDStream& stream, uint32 len) override;
	void DeserializeDirectDeferred(IATDeferredDirectDeserializer& defSer) override;
	void Serialize(ATSerializer& writer) const override;
	void SerializeDirect(IVDStream& stream) const override;
	void SerializeDirectAndRelease(IVDStream& stream) override;

	const wchar_t *mpDirectName = nullptr;

private:
	mutable vdfastvector<uint8> mBuffer;
	mutable vdrefptr<IATDeferredDirectDeserializer> mpDeferredSerializer;
};

#endif
