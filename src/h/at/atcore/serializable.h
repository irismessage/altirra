//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
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

#ifndef f_AT_ATCORE_SERIALIZABLE_H
#define f_AT_ATCORE_SERIALIZABLE_H

#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>

class IVDStream;
class IATDeferredDirectDeserializer;
class ATDeserializer;
class ATSerializer;
struct ATSerializationTypeDef;

enum class ATSerializationTypeToken : uint32 {
	Invalid = 0
};

// IATSerializable
//
// Object holding captured state from a live object. A save state is a
// graph of these objects, which may either be reapplied to a runtime
// object or serialized for persistence on disk.
//
class IATSerializable : public IVDRefUnknown {
public:
	static constexpr uint32 kTypeID = "IATSerializable"_vdtypeid;

	virtual const ATSerializationTypeDef& GetSerializationType() const = 0;

	// Return path if this object should be packaged as a top-level resource, or null if the
	// object should not be direct packaged. The path may include subdirectories with slashes
	// and should include the extension, if any. However, it is not a filesystem path and
	// should not be absolute. The implementation will modify the name as necessary to avoid
	// conflicts.
	virtual const wchar_t *GetDirectPackagingPath() const = 0;

	virtual bool SupportsDirectDeserialization() const = 0;

	// Read object from formatted source.
	//
	// Rules:
	// - Objects are deserialized in unspecified order.
	// - References to other objects will be valid as they are deserialized, but the
	//   referenced objects may not themselves have been deserialized yet. During deserialization,
	//   only the reference itself and the type of the referenced object may be used. The
	//   contents of the referenced objects must not be accessed.
	// - Exceptions may be raised if a validation error occurs during deserialization.
	// - Extended validation or postprocessing requiring referenced objects can be done in
	//   PostDeserialize(), which is run after all objects have been deserialized.
	//
	virtual void Deserialize(ATDeserializer& reader) = 0;

	// Read object from direct stream. This is only called if the object was
	// serialized directly. Most objects do not support this and should raise
	// if they see an unexpected direct stream.
	virtual void DeserializeDirect(IVDStream& stream, uint32 len) = 0;

	virtual void DeserializeDirectDeferred(IATDeferredDirectDeserializer& defSer) = 0;

	// Process after all objects have been loaded. Object order is unspecified.
	virtual void PostDeserialize() = 0;

	// Write object to formatted source. This is not called if the object has a direct
	// packaging path.
	virtual void Serialize(ATSerializer& writer) const = 0;

	// Write object to direct stream. This is only called if the object has a direct packaging
	// path.
	virtual void SerializeDirect(IVDStream& stream) const = 0;

	// Write object to direct stream and free large storage.
	virtual void SerializeDirectAndRelease(IVDStream& stream) = 0;
};

class IATDeferredDirectDeserializer : public IVDRefCount {
protected:
	~IATDeferredDirectDeserializer() = default;

public:
	virtual void DeserializeDirect(IATSerializable& target) const = 0;
	virtual void Prefetch() = 0;
};

// ATExchanger = ATSerializer or ATDeserializer, expressed as a concept here for Intellisense.
template<typename T>
concept ATExchanger = requires(T& rw) {
	{ rw.IsReader } -> std::convertible_to<bool>;
	{ rw.IsWriter } -> std::convertible_to<bool>;

	rw.Transfer("key", (char    *)nullptr, (size_t)0);
	rw.Transfer("key", (wchar_t *)nullptr, (size_t)0);
	rw.Transfer("key", (uint8   *)nullptr, (size_t)0);
	rw.Transfer("key", (sint8   *)nullptr, (size_t)0);
	rw.Transfer("key", (uint16  *)nullptr, (size_t)0);
	rw.Transfer("key", (sint16  *)nullptr, (size_t)0);
	rw.Transfer("key", (uint32  *)nullptr, (size_t)0);
	rw.Transfer("key", (sint32  *)nullptr, (size_t)0);
	rw.Transfer("key", (uint64  *)nullptr, (size_t)0);
	rw.Transfer("key", (sint64  *)nullptr, (size_t)0);
	rw.Transfer("key", (float   *)nullptr, (size_t)0);
	rw.Transfer("key", (double  *)nullptr, (size_t)0);
};

template<typename T>
concept ATExchangeable = requires(T& obj, ATSerializer& ser, ATDeserializer& deser) {
	obj.Exchange(deser);
	obj.Exchange(ser);
};

#endif
