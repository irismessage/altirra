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
#include <future>
#include <vd2/system/text.h>
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
	ATSnapObjectSerializer(const wchar_t *rootFileName);

	void SetCompressionLevel(VDDeflateCompressionLevel level) override { mCompressionLevel = level; }
	void SetProgressFn(vdfunction<void(int, int)> fn);

	void Serialize(IVDStream& stream, IATSerializable& object, const wchar_t *packageType) override;
	void Serialize(IVDZipArchiveWriter& zip, IATSerializable& snapshot) override;

	void BeginSerialize(IVDZipArchiveWriter& zip) override;
	void PreSerializeDirect(IATSerializable& object) override;
	void EndSerialize(IATSerializable& snapshot) override;

public:
	void CreateMember(const char *key) override;
	void OpenArray(bool compact) override;
	void CloseArray() override;
	void OpenObject(const char *key) override;
	void CloseObject() override;
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
	VDStringW MakeUniqueDirectName(const wchar_t *baseName, uint32 objectIndex);

	struct ObjectEntry {
		vdrefptr<IATSerializable> mpObject;
		VDStringW mDirectPath;
		bool mbDirectSerialized = false;
	};

	VDJSONWriter mWriter;
	vdvector<ObjectEntry> mObjects;
	vdhashmap<IATSerializable *, uint32> mObjectLookup;
	int mNumDirectObjects = 0;
	IVDZipArchiveWriter *mpZip = nullptr;

	// adjusted path -> mObjectDirectPaths[] index
	vdhashmap<VDStringW, uint32, vdstringhashi> mObjectDirectPathLookup;

	// unadjusted path -> next counter
	vdhashmap<VDStringW, uint32, vdstringhashi> mLastObjectDirectPathLookup;

	VDStringW mRootFileName;
	VDDeflateCompressionLevel mCompressionLevel = VDDeflateCompressionLevel::Best;
	vdfunction<void(int, int)> mpProgressFn;
};

ATSnapObjectSerializer::ATSnapObjectSerializer(const wchar_t *rootFileName)
	: mRootFileName(rootFileName)
{
}

void ATSnapObjectSerializer::SetProgressFn(vdfunction<void(int, int)> fn) {
	mpProgressFn = fn;
}

void ATSnapObjectSerializer::Serialize(IVDStream& stream, IATSerializable& object, const wchar_t *packageType) {
	if (mObjects.empty())
		AddObject(&object);
	else {
		VDASSERT(!mObjects[0].mpObject);
		mObjects[0].mpObject = &object;
	}

	VDJSONStreamOutput output(stream);
	mWriter.Begin(&output);

	mWriter.OpenObject();
	mWriter.WriteMemberName(L"type");
	mWriter.WriteString(packageType);
	mWriter.WriteMemberName(L"objects");
	mWriter.OpenArray();

	ATSerializer objWriter(*this);

	for(uint32 i = 0; i < mObjects.size(); ++i) {
		IATSerializable *obj = mObjects[i].mpObject;
		const wchar_t *directPath = obj->GetDirectPackagingPath();

		mWriter.OpenObject();
		mWriter.WriteMemberName(L"_type");
		mWriter.WriteStringASCII(obj->GetSerializationType().mpName);

		if (directPath) {
			VDASSERT(*directPath);

			VDStringW& adjustedDirectPath = mObjects[i].mDirectPath;
			
			if (adjustedDirectPath.empty())
				adjustedDirectPath = MakeUniqueDirectName(directPath, i);

			mWriter.WriteMemberName(L"_path");
			mWriter.WriteString(adjustedDirectPath.c_str());

			++mNumDirectObjects;
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
	BeginSerialize(zip);
	EndSerialize(snapshot);
}

void ATSnapObjectSerializer::BeginSerialize(IVDZipArchiveWriter& zip) {
	mpZip = &zip;
}

void ATSnapObjectSerializer::PreSerializeDirect(IATSerializable& object) {
	const wchar_t *directPath = object.GetDirectPackagingPath();
	if (!directPath) {
		VDFAIL("Attempt to preserialize an object that doesn't support direct serialization.");
		return;
	}

	if (mObjects.empty())
		mObjects.emplace_back();

	const auto prevCount = mObjectLookup.size();
	const uint32 objectIndex = AddObject(&object);

	if (mObjectLookup.size() == prevCount) {
		VDFAIL("Attempt to preserialize an object twice");
		return;
	}

	ObjectEntry& oe = mObjects[objectIndex - 1];
	oe.mDirectPath = MakeUniqueDirectName(directPath, objectIndex);
	oe.mbDirectSerialized = true;

	auto& dstream = mpZip->BeginFile(oe.mDirectPath.c_str(), mCompressionLevel);
	object.SerializeDirectAndRelease(dstream);
	mpZip->EndFile();
}

void ATSnapObjectSerializer::EndSerialize(IATSerializable& snapshot) {
	Serialize(mpZip->BeginFile(mRootFileName.c_str()), snapshot, VDTextAToW(snapshot.GetSerializationType().mpName).c_str());
	mpZip->EndFile();

	int directObjects = 0;

	for(ObjectEntry& oe : mObjects) {
		IATSerializable *obj = oe.mpObject;
		const VDStringW& directPath = oe.mDirectPath;

		if (!directPath.empty() && !oe.mbDirectSerialized) {
			oe.mbDirectSerialized = true;

			if (mpProgressFn)
				mpProgressFn(++directObjects, mNumDirectObjects + 1);

			auto& dstream = mpZip->BeginFile(directPath.c_str(), mCompressionLevel);
			obj->SerializeDirect(dstream);
			mpZip->EndFile();
		}
	}

	mpZip = nullptr;
}

void ATSnapObjectSerializer::CreateMember(const char *key) {
	mWriter.WriteMemberName(VDTextU8ToW(VDStringSpanA(key)).c_str());
}

void ATSnapObjectSerializer::OpenArray(bool compact) {
	mWriter.OpenArray();

	if (compact)
		mWriter.SetArrayCompact();
}

void ATSnapObjectSerializer::CloseArray() {
	mWriter.Close();
}

void ATSnapObjectSerializer::OpenObject(const char *key) {
	if (key)
		CreateMember(key);

	mWriter.OpenObject();
}

void ATSnapObjectSerializer::CloseObject() {
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
		ObjectEntry& oe = mObjects.emplace_back();
		oe.mpObject = obj;

		r.first->second = (uint32)mObjects.size();
	}

	return r.first->second;
}

VDStringW ATSnapObjectSerializer::MakeUniqueDirectName(const wchar_t *baseName, uint32 objectIndex) {
	VDStringW adjustedDirectPath { baseName };

	const auto counterEntry = mLastObjectDirectPathLookup.insert_as(adjustedDirectPath);
	uint32 counter = 0;

	if (!counterEntry.second)
		counter = counterEntry.first->second;

	// disambiguate
	size_t extPos = 0;
	for(;;) {
		++counter;

		if (counter < 2) {
			// counter=1 is the base name
		} else if (!extPos) {
			extPos = std::min(adjustedDirectPath.size(), adjustedDirectPath.find(L'.'));

			VDStringW counterStr;
			counterStr.sprintf(L"-%u", counter);
			adjustedDirectPath.insert(extPos, counterStr);
			extPos += counterStr.size();
		} else {
			// increment
			for(size_t j = extPos; j > 0; --j) {
				wchar_t& c = adjustedDirectPath[j - 1];

				if (c == L'9') {
					c = L'0';
				} else if (c >= L'0' && c <= L'8') {
					++c;
					break;
				} else {
					adjustedDirectPath.insert(j, L'1');
					++extPos;
					break;
				}
			}
		}

		auto insertResult = mObjectDirectPathLookup.insert_as(adjustedDirectPath);
		if (insertResult.second) {
			insertResult.first->second = objectIndex;
			break;
		}
	}

	counterEntry.first->second = counter;

	return adjustedDirectPath;
}

///////////////////////////////////////////////////////////////////////////

struct ATSnapObjectDeferredContext final : public vdrefcount {
	vdfunction<void(const char *, vdfastvector<uint8>&)> mpOpenStream;
	vdfunction<sint32(const char *, vdfastvector<uint8>&)> mpReadRawStream;
	vdfunction<void(sint32, vdfastvector<uint8>&)> mpDecompressStream;
};

struct ATSnapObjectDeferredObject final : public vdrefcounted<IATDeferredDirectDeserializer> {
	void DeserializeDirect(IATSerializable& target) const override {
		vdfastvector<uint8> buf;

		if (mbPrefetched) {
			mDecompressionFuture.wait();

			buf.swap(mPrefetchBuffer);
		} else {
			mpContext->mpOpenStream(mFilename.c_str(), buf);
		}

		VDMemoryStream ms(buf.data(), buf.size());
		target.DeserializeDirect(ms, (uint32)buf.size());
	}

	void Prefetch() override {
		if (mbPrefetched)
			return;

		mPrefetchIndex = mpContext->mpReadRawStream(mFilename.c_str(), mPrefetchBuffer);

		if (mPrefetchIndex < 0) {
			std::promise<void> dummyPromise;

			mDecompressionFuture = dummyPromise.get_future();
			dummyPromise.set_value();
		} else {
			mDecompressionFuture = std::async(
				std::launch::deferred | std::launch::async,
				[this] {
					mpContext->mpDecompressStream(mPrefetchIndex, mPrefetchBuffer);
				}
			);
		}

		mbPrefetched = true;
	}

	vdrefptr<ATSnapObjectDeferredContext> mpContext;
	VDStringA mFilename;

	mutable vdfastvector<uint8> mPrefetchBuffer;
	sint32 mPrefetchIndex = 0;
	bool mbPrefetched = false;
	std::future<void> mDecompressionFuture;
};

class ATSnapObjectDeserializer final : public IATSaveStateDeserializer {
public:
	ATSnapObjectDeserializer(const wchar_t *rootFileName);

	void SetProgressFn(vdfunction<void(int, int)> fn);

	void Deserialize(IVDRandomAccessStream& stream, IATSerializable **snapshot) override;
	void Deserialize(VDZipArchive& zip, IATSerializable **saveState, IVDRefCount *deferredZip = nullptr) override;

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
	void Deserialize(const void *data, uint32 len, const vdfunction<bool(const char *, vdfastvector<uint8>&)>& openStream, ATSnapObjectDeferredContext *context, IATSerializable **snapshot);

	VDJSONValueRef GetNextChild(const char *key);

	VDJSONValueRef mParentValue { nullptr, nullptr };

	uint32 mArrayIndex = 0;
	vdvector<std::pair<VDJSONValueRef, uint32>> mStack;
	vdvector<vdrefptr<IATSerializable>> mObjects;
	VDStringW mRootFileName;
	vdfunction<void(int, int)> mpProgressFn;
};

ATSnapObjectDeserializer::ATSnapObjectDeserializer(const wchar_t *rootFileName)
	: mRootFileName(rootFileName)
{
}

void ATSnapObjectDeserializer::SetProgressFn(vdfunction<void(int, int)> fn) {
	mpProgressFn = fn;
}

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
		nullptr,
		snapshot
	);
}

void ATSnapObjectDeserializer::Deserialize(const void *data, uint32 len, const vdfunction<bool(const char *, vdfastvector<uint8>&)>& openStream, ATSnapObjectDeferredContext *context, IATSerializable **snapshot) {
	VDJSONReader reader;
	VDJSONDocument doc;
	if (!reader.Parse(data, len, doc))
		throw ATInvalidSaveStateException();

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

		if (mpProgressFn)
			mpProgressFn(i, numObjects);

		if (obj) {
			const wchar_t *path = objectInfo["_path"].AsString();
			if (path && *path) {
				if (context && obj->SupportsDirectDeserialization()) {
					vdrefptr defObj { new ATSnapObjectDeferredObject };
					defObj->mFilename = VDTextWToU8(VDStringSpanW(path));
					defObj->mpContext = context;

					obj->DeserializeDirectDeferred(*defObj);
				} else {
					if (!openStream(VDTextWToU8(VDStringSpanW(path)).c_str(), buf))
						throw ATInvalidSaveStateException();

					VDMemoryStream ms(buf.data(), buf.size());
					obj->DeserializeDirect(ms, (uint32)buf.size());
				}
			} else {
				mParentValue = objectInfo;

				ATDeserializer reader(*this);
				obj->Deserialize(reader);
			}
		}
	}

	for(size_t i = 0; i < numObjects; ++i) {
		IATSerializable *obj = mObjects[i];

		if (obj)
			obj->PostDeserialize();
	}

	*snapshot = mObjects[0].release();
}

void ATSnapObjectDeserializer::Deserialize(VDZipArchive& zip, IATSerializable **saveState, IVDRefCount *deferredZip) {
	sint32 n = zip.GetFileCount();

	auto openStream = [&zip, n](const char *name, vdfastvector<uint8>& buf) -> bool {
		sint32 idx = zip.FindFile(name);

		if (idx < 0)
			return false;

		const auto& fi = zip.GetFileInfo(idx);

		if (fi.mbEnhancedDeflate)
			throw MyError("Enhanced Deflate compression is not supported in save state files.");

		vdautoptr zs { zip.OpenDecodedStream(idx) };
		zs->EnableCRC();

		buf.resize(fi.mUncompressedSize);
		zs->Read(buf.data(), fi.mUncompressedSize);
		zs->VerifyCRC();

		return true;
	};

	vdrefptr<ATSnapObjectDeferredContext> context;
	if (deferredZip) {
		context = new ATSnapObjectDeferredContext;
		context->mpOpenStream = [cookie = vdrefptr(deferredZip), openStream](const char *name, vdfastvector<uint8>& buf) {
			if (!openStream(name, buf))
				throw ATInvalidSaveStateException();
		};

		context->mpReadRawStream = [cookie = vdrefptr(deferredZip), &zip](const char *name, vdfastvector<uint8>& buf) -> sint32 {
			sint32 idx = zip.FindFile(name);

			if (idx < 0)
				throw ATInvalidSaveStateException();

			return zip.ReadRawStream(idx, buf) ? -1 : idx;
		};

		context->mpDecompressStream = [cookie = vdrefptr(deferredZip), &zip](sint32 idx, vdfastvector<uint8>& buf) {
			zip.DecompressStream(idx, buf);
		};
	}

	vdfastvector<uint8> buf;

	if (!openStream(VDTextWToA(mRootFileName).c_str(), buf))
		throw ATInvalidSaveStateException();

	Deserialize(buf.data(), (uint32)buf.size(), openStream, context, saveState);
}

bool ATSnapObjectDeserializer::OpenObject(const char *key) {
	VDJSONValueRef child = GetNextChild(key);

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

IATSaveStateSerializer *ATCreateSaveStateSerializer(const wchar_t *rootFileName) {
	return new ATSnapObjectSerializer(rootFileName);
}

IATSaveStateDeserializer *ATCreateSaveStateDeserializer(const wchar_t *rootFileName) {
	return new ATSnapObjectDeserializer(rootFileName);
}

void ATInitSaveStateDeserializer() {
	ATSetSaveState2Reader(
		[](VDZipArchive& zip, const wchar_t *rootFileName, IATSerializable **rootObj) {
			ATSnapObjectDeserializer ds(rootFileName);

			ds.Deserialize(zip, rootObj);
		}
	);
}
