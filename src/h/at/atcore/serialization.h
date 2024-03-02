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

#ifndef f_AT_ATCORE_SERIALIZATION_H
#define f_AT_ATCORE_SERIALIZATION_H

#include <vd2/system/refcount.h>
#include <at/atcore/serializable.h>
#include <at/atcore/enumparse.h>

class ATDeserializer;
class ATSerializer;
class VDStringSpanA;
class VDStringSpanW;
class VDStringA;
class VDStringW;
template<typename T, typename A> class vdfastvector;

class ATSerializationException {};
class ATSerializationCastException final : public ATSerializationException {};
class ATSerializationNullReferenceException final : public ATSerializationException {};

// ATSnapObjectId
//
// Object IDs used when snapping references to other objects. A snapped
// object has a unique object ID amongst all objects included in a snap
// context. Zero is invalid/null.
//
enum class ATSerializationObjectId : uint32 {
	Invalid = 0
};

struct ATSerializationTypeDef {
	const char *mpName;
	uint32 mNameHash;
	IATSerializable *(*mpCreate)();

	template<typename T>
	static IATSerializable *Factory() {
		return new T;
	}

	static constexpr uint32 CTHash(const char *name) {
		uint32 hash = 2166136261U;

		while(const char c = *name++) {
			hash *= 16777619;
			hash ^= (unsigned char)c;
		}

		return hash;
	}
};

template<typename T>
const ATSerializationTypeDef *ATSerializationTypeRef;

template<typename T>
static constexpr ATSerializationTypeDef ATMakeSerializationTypeDef(const char *name) {
	return ATSerializationTypeDef { name, ATSerializationTypeDef::CTHash(name), ATSerializationTypeDef::Factory<T> };
}

#define ATSERIALIZATION_DEFINE(name) extern const ATSerializationTypeDef g_ATSerTypeDef_##name = ATMakeSerializationTypeDef<name>(#name);
#define ATSERIALIZATION_REGISTER(name) extern const ATSerializationTypeDef g_ATSerTypeDef_##name; ATSerializationRegisterType(ATSerializationTypeRef<name>, g_ATSerTypeDef_##name)

void ATSerializationRegisterType(const ATSerializationTypeDef *& ref, const ATSerializationTypeDef& def);
const ATSerializationTypeDef *ATSerializationFindType(const char *name);
vdrefptr<IATSerializable> ATSerializationCreateObject(const ATSerializationTypeDef& def);

class IATSerializationOutput {
public:
	virtual void CreateMember(const char *key) = 0;
	virtual void OpenArray() = 0;
	virtual void CloseArray() = 0;
	virtual void WriteStringA(VDStringSpanA s) = 0;
	virtual void WriteStringW(VDStringSpanW s) = 0;
	virtual void WriteBool(bool v) = 0;
	virtual void WriteInt64(sint64 v) = 0;
	virtual void WriteUint64(uint64 v) = 0;
	virtual void WriteDouble(double v) = 0;
	virtual void WriteObject(IATSerializable *obj) = 0;
	virtual void WriteBulkData(const void *data, uint32 len) = 0;
};

class IATSerializationInput {
public:
	virtual bool OpenObject(const char *key) = 0;
	virtual uint32 OpenArray(const char *key) = 0;
	virtual void Close() = 0;

	virtual bool ReadStringA(const char *key, VDStringA& value) = 0;
	virtual bool ReadStringW(const char *key, VDStringW& value) = 0;
	virtual bool ReadBool(const char *key, bool& value) = 0;
	virtual bool ReadInt64(const char *key, sint64& value) = 0;
	virtual bool ReadUint64(const char *key, uint64& value) = 0;
	virtual bool ReadDouble(const char *key, double& value) = 0;
	virtual bool ReadObject(const char *key, const ATSerializationTypeDef *def, IATSerializable *& value) = 0;

	virtual bool ReadFixedBulkData(void *data, uint32 len) = 0;
	virtual bool ReadVariableBulkData(vdfastvector<uint8>& buf) = 0;
};

class ATDeserializer {
public:
	enum : bool { IsReader = true, IsWriter = false };

	ATDeserializer(IATSerializationInput& input)
		: mInput(input)
	{
	}

	template<typename T>
	T Read(const char *key) = delete;

	template<typename T>
	T ReadEnum(const char *key) {
		const VDStringA& s = Read<VDStringA>(key);

		return ATParseEnum<T>(s).mValue;
	}

	template<typename T, typename = std::enable_if_t<std::is_convertible_v<T, const IATSerializable *>>, typename=int>
	T *ReadReference(const char *key) {
		return static_cast<T *>(ReadReference(key, ATSerializationTypeRef<std::remove_cv_t<T>>));
	}

	template<typename T>
	void Transfer(const char *key, T *p) {
		*p = Read<T>(key);
	}

	template<typename T, typename = std::enable_if_t<std::is_convertible_v<T *, const IATSerializable *>>>
	void Transfer(const char *key, vdrefptr<T> *p) {
		*p = static_cast<T *>(ReadReference(key, ATSerializationTypeRef<std::remove_cv_t<T>>));
	}

	template<typename T, typename = std::enable_if_t<std::is_convertible_v<T *, const IATSerializable *>>>
	void Transfer(const char *key, T **p) {
		*p = static_cast<T *>(ReadReference(key, ATSerializationTypeRef<std::remove_cv_t<T>>));
	}

	template<typename T, size_t N>
	void TransferArray(const char *key, T (&p)[N]) {
		Transfer(key, &p[0], N);
	}

	template<typename T>
	void TransferEnum(const char *key, T *p) {
		*p = ReadEnum<T>(key);
	}

	void Transfer(const char *key, char *p, size_t n);
	void Transfer(const char *key, wchar_t *p, size_t n);
	void Transfer(const char *key, uint8 *p, size_t n);
	void Transfer(const char *key, sint8 *p, size_t n);
	void Transfer(const char *key, uint16 *p, size_t n);
	void Transfer(const char *key, sint16 *p, size_t n);
	void Transfer(const char *key, uint32 *p, size_t n);
	void Transfer(const char *key, sint32 *p, size_t n);
	void Transfer(const char *key, uint64 *p, size_t n);
	void Transfer(const char *key, sint64 *p, size_t n);
	void Transfer(const char *key, float *p, size_t n);
	void Transfer(const char *key, double *p, size_t n);

	template<typename T, typename = std::enable_if_t<std::is_convertible_v<T *, const IATSerializable *>>>
	void Transfer(const char *key, T **p, size_t n) {
		if (key)
			mInput.OpenArray(key);

		while(n--)
			Transfer(nullptr, p++);

		if (key)
			mInput.Close();
	}

	template<typename T, typename = std::enable_if_t<std::is_convertible_v<T *, const IATSerializable *>>>
	void Transfer(const char *key, vdrefptr<T> *p, size_t n) {
		if (key)
			mInput.OpenArray(key);

		while(n--)
			Transfer(nullptr, p++);

		if (key)
			mInput.Close();
	}

	template<typename T, typename A>
	void Transfer(const char *key, vdfastvector<T, A> *p) {
		uint32 n = mInput.OpenArray(key);

		p->resize(n);
		if (n)
			Transfer(nullptr, p->data(), n);

		mInput.Close();
	}

	template<typename T, typename A>
	void Transfer(const char *key, vdvector<T, A> *p) {
		uint32 n = mInput.OpenArray(key);

		p->resize(n);
		if (n)
			Transfer(nullptr, p->data(), n);

		mInput.Close();
	}

private:
	template<typename T>
	void TransferIntegers(const char *key, T *p, size_t n);

	IATSerializable *ReadReference(const char *key, const ATSerializationTypeDef *def);

	IATSerializationInput& mInput;
};

template<> bool		ATDeserializer::Read<bool	>(const char *key);
template<> char		ATDeserializer::Read<char	>(const char *key);
template<> wchar_t	ATDeserializer::Read<wchar_t>(const char *key);
template<> uint8	ATDeserializer::Read<uint8	>(const char *key);
template<> sint8	ATDeserializer::Read<sint8	>(const char *key);
template<> uint16	ATDeserializer::Read<uint16	>(const char *key);
template<> sint16	ATDeserializer::Read<sint16	>(const char *key);
template<> uint32	ATDeserializer::Read<uint32	>(const char *key);
template<> sint32	ATDeserializer::Read<sint32	>(const char *key);
template<> uint64	ATDeserializer::Read<uint64	>(const char *key);
template<> sint64	ATDeserializer::Read<sint64	>(const char *key);
template<> float	ATDeserializer::Read<float	>(const char *key);
template<> double	ATDeserializer::Read<double	>(const char *key);
template<> VDStringA	ATDeserializer::Read<VDStringA>(const char *key);
template<> VDStringW	ATDeserializer::Read<VDStringW>(const char *key);

class ATSerializer {
public:
	enum : bool { IsReader = false, IsWriter = true };

	ATSerializer(IATSerializationOutput& output)
		: mOutput(output)
	{
	}

	template<typename T>
	void Write(const char *key, T v) = delete;

	template<typename T>
	void WriteEnum(const char *key, T v) {
		Write<const VDStringSpanA&>(key, VDStringSpanA(ATEnumToString(v)));
	}

	inline void Transfer(const char *key, const bool *p);
	inline void Transfer(const char *key, const uint8 *p);
	inline void Transfer(const char *key, const sint8 *p);
	inline void Transfer(const char *key, const uint16 *p);
	inline void Transfer(const char *key, const sint16 *p);
	inline void Transfer(const char *key, const uint32 *p);
	inline void Transfer(const char *key, const sint32 *p);
	inline void Transfer(const char *key, const uint64 *p);
	inline void Transfer(const char *key, const sint64 *p);
	inline void Transfer(const char *key, const float *p);
	inline void Transfer(const char *key, const double *p);
	inline void Transfer(const char *key, const VDStringSpanA *p);
	inline void Transfer(const char *key, const VDStringSpanW *p);

	template<typename T, typename = std::enable_if_t<std::is_convertible_v<T *, const IATSerializable *>>>
	void Transfer(const char *key, T *const *p) {
		WriteReference(key, *p);
	}

	template<typename T, typename = std::enable_if_t<std::is_convertible_v<T *, const IATSerializable *>>>
	void Transfer(const char *key, const vdrefptr<T> *p) {
		WriteReference(key, *p);
	}

	template<typename T, size_t N>
	void TransferArray(const char *key, const T (&p)[N]) {
		Transfer(key, &p[0], N);
	}

	template<typename T>
	void TransferEnum(const char *key, const T *p) {
		WriteEnum(key, *p);
	}

	void Transfer(const char *key, const char *p, size_t n);
	void Transfer(const char *key, const wchar_t *p, size_t n);
	void Transfer(const char *key, const uint8 *p, size_t n);
	void Transfer(const char *key, const sint8 *p, size_t n);
	void Transfer(const char *key, const uint16 *p, size_t n);
	void Transfer(const char *key, const sint16 *p, size_t n);
	void Transfer(const char *key, const uint32 *p, size_t n);
	void Transfer(const char *key, const sint32 *p, size_t n);
	void Transfer(const char *key, const uint64 *p, size_t n);
	void Transfer(const char *key, const sint64 *p, size_t n);
	void Transfer(const char *key, const float *p, size_t n);
	void Transfer(const char *key, const double *p, size_t n);

	template<typename T, typename = std::enable_if_t<std::is_convertible_v<T *, const IATSerializable *>>>
	void Transfer(const char *key, T *const *p, size_t n) {
		mOutput.CreateMember(key);
		mOutput.OpenArray();

		while(n--)
			Transfer(nullptr, p++);

		mOutput.CloseArray();
	}

	template<typename T, typename = std::enable_if_t<std::is_convertible_v<T *, const IATSerializable *>>>
	void Transfer(const char *key, const vdrefptr<T> *p, size_t n) {
		mOutput.CreateMember(key);
		mOutput.OpenArray();

		while(n--)
			Transfer(nullptr, p++);

		mOutput.CloseArray();
	}

	template<typename T, typename A>
	void Transfer(const char *key, const vdfastvector<T, A> *v) {
		Transfer(key, v->data(), v->size());
	}

	template<typename T, typename A>
	void Transfer(const char *key, const vdvector<T, A> *v) {
		Transfer(key, v->data(), v->size());
	}

private:
	void WriteReference(const char *key, IATSerializable *p);

	IATSerializationOutput& mOutput;
};

template<> void ATSerializer::Write<bool  >(const char *key, bool v);
template<> void ATSerializer::Write<uint8 >(const char *key, uint8 v);
template<> void ATSerializer::Write<sint8 >(const char *key, sint8 v);
template<> void ATSerializer::Write<uint16>(const char *key, uint16 v);
template<> void ATSerializer::Write<sint16>(const char *key, sint16 v);
template<> void ATSerializer::Write<uint32>(const char *key, uint32 v);
template<> void ATSerializer::Write<sint32>(const char *key, sint32 v);
template<> void ATSerializer::Write<uint64>(const char *key, uint64 v);
template<> void ATSerializer::Write<sint64>(const char *key, sint64 v);
template<> void ATSerializer::Write<float >(const char *key, float v);
template<> void ATSerializer::Write<double>(const char *key, double v);
template<> void ATSerializer::Write<ATSerializationObjectId>(const char *key, ATSerializationObjectId v);
template<> void ATSerializer::Write<const VDStringSpanA&>(const char *key, const VDStringSpanA& v);
template<> void ATSerializer::Write<const VDStringSpanW&>(const char *key, const VDStringSpanW& v);

inline void ATSerializer::Transfer(const char *key, const VDStringSpanA *p) { Write<const VDStringSpanA&>(key, *p); }
inline void ATSerializer::Transfer(const char *key, const VDStringSpanW *p) { Write<const VDStringSpanW&>(key, *p); }
inline void ATSerializer::Transfer(const char *key, const bool *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const uint8 *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const sint8 *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const uint16 *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const sint16 *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const uint32 *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const sint32 *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const uint64 *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const sint64 *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const float *p) { Write(key, *p); }
inline void ATSerializer::Transfer(const char *key, const double *p) { Write(key, *p); }

template<typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
T atser_cast(std::conditional_t<std::is_const_v<std::remove_pointer_t<T>>, const IATSerializable *, IATSerializable *> src) {
	return src && &src->GetSerializationType() == ATSerializationTypeRef<std::remove_const_t<std::remove_pointer_t<T>>> ? static_cast<T>(src) : nullptr;
}

template<typename T, typename = std::enable_if_t<std::is_reference_v<T>>>
T atser_cast(std::conditional_t<std::is_const_v<std::remove_reference_t<T>>, const IATSerializable&, IATSerializable&> src) {
	if (&src.GetSerializationType() != ATSerializationTypeRef<std::remove_const_t<std::remove_reference_t<T>>>)
		throw ATSerializationCastException();

	return static_cast<T>(src);
}

template<typename T, typename = std::enable_if_t<!std::is_reference_v<T> && !std::is_pointer_v<T> && !std::is_const_v<T>>>
const T& atser_unpack(const IATSerializable *src) {
	if (!src)
		throw ATSerializationNullReferenceException();

	if (&src->GetSerializationType() != ATSerializationTypeRef<T>)
		throw ATSerializationCastException();

	return static_cast<const T&>(*src);
}

#endif
