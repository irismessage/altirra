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

#include "stdafx.h"
#include <vd2/system/zip.h>
#include <vd2/vdjson/jsonoutput.h>
#include <vd2/vdjson/jsonwriter.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <at/atcore/snapshotimpl.h>
#include <at/atcore/serialization.h>
#include <at/atio/savestate.h>
#include "savestate.h"
#include "savestateio.h"

class ATSnapObjectSerializer final : public IATSaveStateSerializer {
public:
	void Serialize(IVDStream& stream, IATSerializable& object, const wchar_t *packageType) override;
	void Serialize(IVDZipArchiveWriter& zip, IATSerializable& snapshot) override;

public:
	void CreateMember(const char *key) override;
	void OpenArray() override;
	void CloseArray() override;
	void WriteStringA(VDStringSpanA s) override;
	void WriteStringW(VDStringSpanW s) override;
	void WriteBool(bool v) override;
	void WriteInt64(sint64 v) override;
	void WriteUint64(uint64 v) override;
	void WriteDouble(double v) override;
	void WriteObject(IATSerializable *obj) override;
	void WriteBulkData(const void *data, uint32 len) override;

private:
	uint32 AddObject(IATSerializable *obj);

	VDJSONWriter mWriter;
	vdvector<vdrefptr<IATSerializable>> mObjects;
	vdhashmap<IATSerializable *, uint32> mObjectLookup;
};

void ATSnapObjectSerializer::Serialize(IVDStream& stream, IATSerializable& object, const wchar_t *packageType) {
	AddObject(&object);

	VDJSONStreamOutput output(stream);
	mWriter.Begin(&output);

	mWriter.OpenObject();
	mWriter.WriteMemberName(L"type");
	mWriter.WriteString(packageType);
	mWriter.WriteMemberName(L"objects");
	mWriter.OpenArray();

	ATSerializer objWriter(*this);

	for(uint32 i = 0; i < mObjects.size(); ++i) {
		IATSerializable *obj = mObjects[i];
		const wchar_t *directPath = obj->GetDirectPackagingPath();

		mWriter.OpenObject();
		mWriter.WriteMemberName(L"_type");
		mWriter.WriteStringASCII(obj->GetSerializationType().mpName);

		if (directPath) {
			mWriter.WriteMemberName(L"_path");
			mWriter.WriteString(directPath);
		} else {
			obj->Serialize(objWriter);
		}
		mWriter.Close();
	}

	mWriter.Close();
	mWriter.Close();

	mWriter.End();
	output.Flush();
}

void ATSnapObjectSerializer::Serialize(IVDZipArchiveWriter& zip, IATSerializable& snapshot) {
	Serialize(zip.BeginFile(L"savestate.json"), snapshot, L"ATSaveState");
	zip.EndFile();

	for(IATSerializable *obj : mObjects) {
		const wchar_t *directPath = obj->GetDirectPackagingPath();

		if (directPath) {
			auto& dstream = zip.BeginFile(directPath);
			obj->SerializeDirect(dstream);
			zip.EndFile();
		}
	}
}

void ATSnapObjectSerializer::CreateMember(const char *key) {
	mWriter.WriteMemberName(VDTextU8ToW(VDStringSpanA(key)).c_str());
}

void ATSnapObjectSerializer::OpenArray() {
	mWriter.OpenArray();
}

void ATSnapObjectSerializer::CloseArray() {
	mWriter.Close();
}

void ATSnapObjectSerializer::WriteStringA(VDStringSpanA s) {
	mWriter.WriteString(VDTextU8ToW(s).c_str());
}

void ATSnapObjectSerializer::WriteStringW(VDStringSpanW s) {
	mWriter.WriteString(s.data(), s.size());
}

void ATSnapObjectSerializer::WriteBool(bool v) {
	mWriter.WriteBool(v);
}

void ATSnapObjectSerializer::WriteInt64(sint64 v) {
	mWriter.WriteIntSafe(v);
}

void ATSnapObjectSerializer::WriteUint64(uint64 v) {
	WriteInt64((sint64)v);
}

void ATSnapObjectSerializer::WriteDouble(double v) {
	mWriter.WriteReal(v);
}

void ATSnapObjectSerializer::WriteObject(IATSerializable *obj) {
	mWriter.WriteIntSafe(AddObject(obj));
}

void ATSnapObjectSerializer::WriteBulkData(const void *data, uint32 len) {
}

uint32 ATSnapObjectSerializer::AddObject(IATSerializable *obj) {
	if (!obj)
		return 0;

	auto r = mObjectLookup.insert(obj);
	if (r.second) {
		mObjects.emplace_back(obj);

		r.first->second = (uint32)mObjects.size();
	}

	return r.first->second;
}

///////////////////////////////////////////////////////////////////////////

class ATSnapObjectDeserializer final : public IATSaveStateDeserializer {
public:
	void Deserialize(IVDRandomAccessStream& stream, IATSerializable **snapshot) override;
	void Deserialize(VDZipArchive& zip, IATSerializable **saveState) override;

public:
	bool OpenObject(const char *key) override;
	uint32 OpenArray(const char *key) override;
	void Close() override;

	bool ReadStringA(const char *key, VDStringA& value) override;
	bool ReadStringW(const char *key, VDStringW& value) override;
	bool ReadBool(const char *key, bool& value) override;
	bool ReadInt64(const char *key, sint64& value) override;
	bool ReadUint64(const char *key, uint64& value) override;
	bool ReadDouble(const char *key, double& value) override;
	bool ReadObject(const char *key, const ATSerializationTypeDef *type, IATSerializable*& value) override;

	bool ReadFixedBulkData(void *data, uint32 len) override;
	bool ReadVariableBulkData(vdfastvector<uint8>& buf) override;

private:
	void Deserialize(const void *data, uint32 len, const vdfunction<bool(const char *, vdfastvector<uint8>&)>& openStream, IATSerializable **snapshot);

	VDJSONValueRef GetNextChild(const char *key);

	VDJSONValueRef mParentValue { nullptr, nullptr };

	uint32 mArrayIndex = 0;
	vdvector<std::pair<VDJSONValueRef, uint32>> mStack;
	vdvector<vdrefptr<IATSerializable>> mObjects;
};

void ATSnapObjectDeserializer::Deserialize(IVDRandomAccessStream& stream, IATSerializable **snapshot) {
	sint64 len = stream.Length();

	if ((uint64)len > 384*1024*1024)
		throw MyError("Data package is too large to read: %llu bytes", (unsigned long long)len);

	vdfastvector<uint8> buf((size_t)len);
	stream.Read(buf.data(), (sint32)buf.size());

	Deserialize(buf.data(),
		buf.size(),
		[](auto&&...) -> bool {
			throw MyError("Data package contains an unsupported external reference.");
		},
		snapshot
	);
}

void ATSnapObjectDeserializer::Deserialize(const void *data, uint32 len, const vdfunction<bool(const char *, vdfastvector<uint8>&)>& openStream, IATSerializable **snapshot) {
	VDJSONReader reader;
	VDJSONDocument doc;
	reader.Parse(data, len, doc);

	const auto objectSet = doc.Root()["objects"].AsArray();

	const auto numObjects = objectSet.size();
	if (!numObjects)
		throw ATInvalidSaveStateException();

	mObjects.clear();
	mObjects.resize(numObjects);

	for(size_t i = 0; i < numObjects; ++i) {
		const VDJSONValueRef& objectInfo = objectSet[i];
		const wchar_t *typeName = objectInfo["_type"].AsString();

		const ATSerializationTypeDef *def = ATSerializationFindType(VDTextWToA(typeName).c_str());

		if (def) {
			mObjects[i] = ATSerializationCreateObject(*def);
		}
	}

	vdfastvector<uint8> buf;
	for(size_t i = 0; i < numObjects; ++i) {
		const VDJSONValueRef& objectInfo = objectSet[i];
		IATSerializable *obj = mObjects[i];

		if (obj) {
			const wchar_t *path = objectInfo["_path"].AsString();
			if (path && *path) {
				if (!openStream(VDTextWToU8(VDStringSpanW(path)).c_str(), buf))
					throw ATInvalidSaveStateException();

				VDMemoryStream ms(buf.data(), buf.size());
				obj->DeserializeDirect(ms, (uint32)buf.size());
			} else {
				mParentValue = objectInfo;

				ATDeserializer reader(*this);
				obj->Deserialize(reader);
			}
		}
	}

	*snapshot = mObjects[0].release();
}

void ATSnapObjectDeserializer::Deserialize(VDZipArchive& zip, IATSerializable **saveState) {
	sint32 n = zip.GetFileCount();

	auto openStream = [&zip, n](const char *name, vdfastvector<uint8>& buf) -> bool {
		for(sint32 i=0; i<n; ++i) {
			const auto& fi = zip.GetFileInfo(i);

			if (fi.mFileName == name) {
				if (fi.mUncompressedSize > 384 * 1024 * 1024)
					throw MyError("The zip item is too large (%llu bytes).", (unsigned long long)fi.mUncompressedSize);

				vdautoptr<VDZipStream> zs(new VDZipStream(zip.OpenRawStream(i), fi.mCompressedSize, !fi.mbPacked));
				zs->EnableCRC();

				buf.resize(fi.mUncompressedSize);
				zs->Read(buf.data(), fi.mUncompressedSize);

				if (zs->CRC() != fi.mCRC32)
					throw MyError("The zip item could not be extracted (bad CRC).");

				return true;
			}
		}

		return false;
	};

	vdfastvector<uint8> buf;

	if (!openStream("savestate.json", buf))
		throw ATInvalidSaveStateException();

	Deserialize(buf.data(), (uint32)buf.size(), openStream, saveState);
}

bool ATSnapObjectDeserializer::OpenObject(const char *key) {
	VDJSONValueRef child = mParentValue[key];

	mStack.push_back({mParentValue, mArrayIndex});
	mParentValue = child;
	return child.IsObject();
}

uint32 ATSnapObjectDeserializer::OpenArray(const char *key) {
	VDJSONValueRef child = mParentValue[key];

	mStack.push_back({mParentValue, mArrayIndex});
	mParentValue = child;
	mArrayIndex = 0;

	return child.IsArray() ? child.GetArrayLength() : 0;
}

void ATSnapObjectDeserializer::Close() {
	const auto& [value, arrayIndex] = mStack.back();

	mParentValue = value;
	mArrayIndex = arrayIndex;

	mStack.pop_back();
}

bool ATSnapObjectDeserializer::ReadStringA(const char *key, VDStringA& value) {
	VDJSONValueRef child = GetNextChild(key);

	if (child.IsString()) {
		value = VDTextWToA(child.AsString());
		return true;
	} else {
		value.clear();
		return false;
	}
}

bool ATSnapObjectDeserializer::ReadStringW(const char *key, VDStringW& value) {
	VDJSONValueRef child = GetNextChild(key);

	if (child.IsString()) {
		value = child.AsString();
		return true;
	} else {
		value.clear();
		return false;
	}
}

bool ATSnapObjectDeserializer::ReadBool(const char *key, bool& value) {
	VDJSONValueRef child = GetNextChild(key);

	if (child.IsBool()) {
		value = child.AsBool();
		return true;
	}

	value = false;
	return false;
}

bool ATSnapObjectDeserializer::ReadInt64(const char *key, sint64& value) {
	VDJSONValueRef child = GetNextChild(key);

	if (child.IsString()) {
		const wchar_t *s = child.AsString();
		wchar_t *t = const_cast<wchar_t *>(s);
		sint64 v = wcstoll(s, &t, 10);

		if (t && !*t) {
			value = v;
			return true;
		}
	} else if (child.IsInt()) {
		value = child.AsInt64();
		return true;
	}

	value = 0;
	return false;
}

bool ATSnapObjectDeserializer::ReadUint64(const char *key, uint64& value) {
	VDJSONValueRef child = GetNextChild(key);

	if (child.IsString()) {
		const wchar_t *s = child.AsString();
		wchar_t *t = const_cast<wchar_t *>(s);
		uint64 v = wcstoull(s, &t, 10);

		if (t && !*t) {
			value = v;
			return true;
		}
	} else if (child.IsInt()) {
		value = (uint64)child.AsInt64();
		return true;
	}

	value = 0;
	return false;
}

bool ATSnapObjectDeserializer::ReadDouble(const char *key, double& value) {
	VDJSONValueRef child = GetNextChild(key);

	value = child.AsDouble();
	return true;
}

bool ATSnapObjectDeserializer::ReadObject(const char *key, const ATSerializationTypeDef *type, IATSerializable*& value) {
	VDJSONValueRef child = GetNextChild(key);

	if (!child.IsInt())
		return false;

	sint64 rawv = child.AsInt64();
	uint32 idx = (uint32)child.AsInt64();

	if (idx != rawv)
		return false;

	if (!idx) {
		value = nullptr;
		return true;
	}

	if (idx > mObjects.size())
		return false;

	IATSerializable *obj = mObjects[idx - 1];
	if (obj && type) {
		if (&obj->GetSerializationType() != type)
			return false;
	}

	value = obj;
	return true;
}

bool ATSnapObjectDeserializer::ReadFixedBulkData(void *data, uint32 len) {
	return false;
}

bool ATSnapObjectDeserializer::ReadVariableBulkData(vdfastvector<uint8>& buf) {
	return false;
}

VDJSONValueRef ATSnapObjectDeserializer::GetNextChild(const char *key) {
	if (mParentValue.IsArray()) {
		const VDJSONArrayEnum& ae = mParentValue.AsArray();
		
		return mArrayIndex >= mParentValue.GetArrayLength() ? VDJSONValueRef() : ae[mArrayIndex++];
	} else if (mParentValue.IsObject()) {
		return mParentValue[key];
	} else
		return VDJSONValueRef();
}

///////////////////////////////////////////////////////////////////////////

IATSaveStateSerializer *ATCreateSaveStateSerializer() {
	return new ATSnapObjectSerializer;
}

IATSaveStateDeserializer *ATCreateSaveStateDeserializer() {
	return new ATSnapObjectDeserializer;
}

void ATInitSaveStateDeserializer() {
	ATSetSaveState2Reader(
		[](VDZipArchive& zip, IATSerializable **rootObj) {
			ATSnapObjectDeserializer ds;

			ds.Deserialize(zip, rootObj);
		}
	);
}
