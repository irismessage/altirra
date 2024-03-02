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

#include <stdafx.h>
#include <vd2/system/file.h>
#include <vd2/system/hash.h>
#include <vd2/system/VDString.h>
#include <at/atcore/serialization.h>

vdfastvector<const ATSerializationTypeDef *> g_ATSerializationTypes;

void ATSerializationRegisterType(const ATSerializationTypeDef *& ref, const ATSerializationTypeDef& def) {
	if (!ref) {
		ref = &def;

		g_ATSerializationTypes.push_back(&def);
	}
}

const ATSerializationTypeDef *ATSerializationFindType(const char *name) {
	uint32 hash = ATSerializationTypeDef::CTHash(name);

	auto it = std::find_if(g_ATSerializationTypes.begin(), g_ATSerializationTypes.end(),
		[=](const ATSerializationTypeDef *e) {
			return e->mNameHash == hash && !strcmp(e->mpName, name);
		}
	);

	return it != g_ATSerializationTypes.end() ? *it : nullptr;
}

vdrefptr<IATSerializable> ATSerializationCreateObject(const ATSerializationTypeDef& def) {
	IATSerializable *obj = def.mpCreate();
	obj->AddRef();

	vdrefptr<IATSerializable> p;
	p.set(obj);
	return p;
}

///////////////////////////////////////////////////////////////////////////

template<> bool		ATDeserializer::Read<bool	>(const char *key) {
	bool v = false;
	return mInput.ReadBool(key, v) && v;
}

template<> char		ATDeserializer::Read<char	>(const char *key) {
	sint64 v = 0;
	return mInput.ReadInt64(key, v) ? (char)v : 0;
}

template<> wchar_t	ATDeserializer::Read<wchar_t>(const char *key) {
	sint64 v = 0;
	return mInput.ReadInt64(key, v) ? (wchar_t)v : 0;
}

template<> uint8	ATDeserializer::Read<uint8	>(const char *key) {
	uint64 v = 0;
	return mInput.ReadUint64(key, v) ? (uint8)v : 0;
}

template<> sint8	ATDeserializer::Read<sint8	>(const char *key) {
	sint64 v = 0;
	return mInput.ReadInt64(key, v) ? (sint8)v : 0;
}

template<> uint16	ATDeserializer::Read<uint16	>(const char *key) {
	uint64 v = 0;
	return mInput.ReadUint64(key, v) ? (uint16)v : 0;
}

template<> sint16	ATDeserializer::Read<sint16	>(const char *key) {
	sint64 v = 0;
	return mInput.ReadInt64(key, v) ? (sint16)v : 0;
}

template<> uint32	ATDeserializer::Read<uint32	>(const char *key) {
	uint64 v = 0;
	return mInput.ReadUint64(key, v) ? (uint32)v : 0;
}

template<> sint32	ATDeserializer::Read<sint32	>(const char *key) {
	sint64 v = 0;
	return mInput.ReadInt64(key, v) ? (sint32)v : 0;
}

template<> uint64	ATDeserializer::Read<uint64	>(const char *key) {
	uint64 v = 0;
	return mInput.ReadUint64(key, v) ? v : 0;
}

template<> sint64	ATDeserializer::Read<sint64	>(const char *key) {
	sint64 v = 0;
	return mInput.ReadInt64(key, v) ? v : 0;
}

template<> float	ATDeserializer::Read<float	>(const char *key) {
	double v = 0;
	return mInput.ReadDouble(key, v) ? (float)v : 0;
}

template<> double	ATDeserializer::Read<double	>(const char *key) {
	double v = 0;
	return mInput.ReadDouble(key, v) ? v : 0;
}

template<> VDStringA ATDeserializer::Read<VDStringA>(const char *key) {
	VDStringA v;
	mInput.ReadStringA(key, v);
	return v;
}

template<> VDStringW ATDeserializer::Read<VDStringW>(const char *key) {
	VDStringW v;
	mInput.ReadStringW(key, v);
	return v;
}

template<typename T>
void ATDeserializer::TransferIntegers(const char *key, T *p, size_t n) {
	if (key)
		mInput.OpenArray(key);

	while(n--) {
		sint64 v = 0;
		mInput.ReadInt64(nullptr, v);
		*p++ = (T)v;
	}

	if (key)
		mInput.Close();
}

void ATDeserializer::Transfer(const char *key, char *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, wchar_t *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, uint8 *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, sint8 *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, uint16 *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, sint16 *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, uint32 *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, sint32 *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, uint64 *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, sint64 *p, size_t n) {
	TransferIntegers(key, p, n);
}

void ATDeserializer::Transfer(const char *key, float *p, size_t n) {
	mInput.OpenArray(key);

	while(n--) {
		double v = 0;
		mInput.ReadDouble(nullptr, v);
		*p++ = (float)v;
	}

	mInput.Close();
}

void ATDeserializer::Transfer(const char *key, double *p, size_t n) {
	mInput.OpenArray(key);

	while(n--) {
		double v = 0;
		mInput.ReadDouble(nullptr, v);
		*p++ = v;
	}

	mInput.Close();
}

IATSerializable *ATDeserializer::ReadReference(const char *key, const ATSerializationTypeDef *def) {
	IATSerializable *p = nullptr;
	return mInput.ReadObject(key, def, p) ? p : nullptr;
}

///////////////////////////////////////////////////////////////////////////

template<> void ATSerializer::Write<bool	 >(const char *key, bool v) {
	mOutput.CreateMember(key);
	mOutput.WriteBool(v);
}

template<> void ATSerializer::Write<uint8	 >(const char *key, uint8 v) {
	mOutput.CreateMember(key);
	mOutput.WriteUint64(v);
}

template<> void ATSerializer::Write<sint8	 >(const char *key, sint8 v) {
	mOutput.CreateMember(key);
	mOutput.WriteInt64(v);
}

template<> void ATSerializer::Write<uint16 >(const char *key, uint16 v) {
	mOutput.CreateMember(key);
	mOutput.WriteUint64(v);
}

template<> void ATSerializer::Write<sint16 >(const char *key, sint16 v) {
	mOutput.CreateMember(key);
	mOutput.WriteInt64(v);
}

template<> void ATSerializer::Write<uint32 >(const char *key, uint32 v) {
	mOutput.CreateMember(key);
	mOutput.WriteUint64(v);
}

template<> void ATSerializer::Write<sint32 >(const char *key, sint32 v) {
	mOutput.CreateMember(key);
	mOutput.WriteInt64(v);
}

template<> void ATSerializer::Write<uint64 >(const char *key, uint64 v) {
	mOutput.CreateMember(key);
	mOutput.WriteUint64(v);
}

template<> void ATSerializer::Write<sint64 >(const char *key, sint64 v) {
	mOutput.CreateMember(key);
	mOutput.WriteInt64(v);
}

template<> void ATSerializer::Write<float	 >(const char *key, float v) {
	mOutput.CreateMember(key);
	mOutput.WriteDouble(v);
}

template<> void ATSerializer::Write<double >(const char *key, double v) {
	mOutput.CreateMember(key);
	mOutput.WriteDouble(v);
}

template<> void ATSerializer::Write<const VDStringSpanA&>(const char *key, const VDStringSpanA& v) {
	mOutput.CreateMember(key);
	mOutput.WriteStringA(v);
}

template<> void ATSerializer::Write<const VDStringSpanW&>(const char *key, const VDStringSpanW& v) {
	mOutput.CreateMember(key);
	mOutput.WriteStringW(v);
}

void ATSerializer::Transfer(const char *key, const char *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteInt64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const wchar_t *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteUint64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const uint8 *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteUint64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const sint8 *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteInt64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const uint16 *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteUint64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const sint16 *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteInt64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const uint32 *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteUint64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const sint32 *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteInt64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const uint64 *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteUint64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const sint64 *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteInt64(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const float *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteDouble(*p++);
	mOutput.CloseArray();
}

void ATSerializer::Transfer(const char *key, const double *p, size_t n) {
	mOutput.CreateMember(key);
	mOutput.OpenArray();
	while(n--)
		mOutput.WriteDouble(*p++);
	mOutput.CloseArray();
}

void ATSerializer::WriteReference(const char *key, IATSerializable *p) {
	if (key)
		mOutput.CreateMember(key);

	mOutput.WriteObject(p);
}
