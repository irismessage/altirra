//	Altirra - Atari 800/800XL/5200 emulator
//	Test module
//	Copyright (C) 2009-2020 Avery Lee
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
#include <vd2/system/file.h>
#include <vd2/system/thread.h>
#include <at/atcore/vfs.h>

#include "blob.h"

class ATTestBlob final : public vdrefcount {
public:
	vdfastvector<uint8> mBuffer;
	uint32 mMinWriteRange = ~(uint32)0;
	uint32 mMaxWriteRange = 0;
};

////////////////////////////////////////////////////////////////////////////////

class ATTestBlobStream final : public ATVFSFileView, public IVDRandomAccessStream {
public:
	ATTestBlobStream(VDStringSpanW fileName, bool readOnly, vdfastvector<uint8>& buffer, ATTestBlob *parent);
	~ATTestBlobStream();

	bool IsSourceReadOnly() const override { return true; }

	const wchar_t *GetNameForError() override;
	sint64	Pos() override;
	void	Read(void *buffer, sint32 bytes) override;
	sint32	ReadData(void *buffer, sint32 bytes) override;
	void	Write(const void *buffer, sint32 bytes) override;
	sint64	Length() override;
	void	Seek(sint64 offset) override;

private:
	sint64 mPos = 0;
	uint32 mMinWriteRange = ~(uint32)0;
	uint32 mMaxWriteRange = 0;
	vdfastvector<uint8>& mBuffer;
	vdrefptr<ATTestBlob> mpParent;
};

ATTestBlobStream::ATTestBlobStream(VDStringSpanW fileName, bool readOnly, vdfastvector<uint8>& buffer, ATTestBlob *parent)
	: mBuffer(buffer)
	, mpParent(parent)
{
	mpStream = this;
	mFileName = fileName;
}

ATTestBlobStream::~ATTestBlobStream() {
	if (mpParent->mMinWriteRange > mMinWriteRange)
		mpParent->mMinWriteRange = mMinWriteRange;

	if (mpParent->mMaxWriteRange < mMaxWriteRange)
		mpParent->mMaxWriteRange = mMaxWriteRange;
}

const wchar_t *ATTestBlobStream::GetNameForError() {
	return L"";
}

sint64 ATTestBlobStream::Pos() {
	return mPos;
}

void ATTestBlobStream::Read(void *buffer, sint32 bytes) {
	if (bytes <= 0)
		return;

	sint64 limit = (sint64)mBuffer.size();

	if (mPos >= limit || limit - mPos < bytes)
		throw MyError("Read beyond EOF");

	memcpy(buffer, mBuffer.data() + mPos, bytes);
	mPos += bytes;
}

sint32 ATTestBlobStream::ReadData(void *buffer, sint32 bytes) {
	if (bytes <= 0)
		return 0;

	sint64 limit = (sint64)mBuffer.size();
	if (mPos >= limit)
		return 0;

	sint64 avail = limit - mPos;
	if (avail < bytes)
		bytes = (sint32)avail;

	memcpy(buffer, mBuffer.data() + mPos, bytes);
	mPos += bytes;

	return bytes;
}

void ATTestBlobStream::Write(const void *buffer, sint32 bytes) {
	if (bytes <= 0)
		return;

	if (mPos > 0x1FFFFFFF || 0x1FFFFFFF - mPos < bytes)
		throw MyError("Maximum size exceeded");

	uint32 start32 = (uint32)mPos;
	uint32 end32 = start32 + (uint32)bytes;

	if (mMinWriteRange > start32)
		mMinWriteRange = start32;

	if (mMaxWriteRange < end32)
		mMaxWriteRange = end32;

	size_t limit = (size_t)(mPos + bytes);
	if (mBuffer.size() < limit)
		mBuffer.resize(limit, 0);

	memcpy(mBuffer.data() + mPos, buffer, bytes);
	mPos += bytes;
}

sint64 ATTestBlobStream::Length() {
	return (sint64)mBuffer.size();
}

void ATTestBlobStream::Seek(sint64 offset) {
	if (offset < 0)
		offset = 0;

	mPos = offset;
}

////////////////////////////////////////////////////////////////////////////////

struct ATTestBlobDatabase {
	vdhashmap<VDStringW, vdrefptr<ATTestBlob>, vdhash<VDStringW>, vdstringpred> mBlobs;

	static ATTestBlobDatabase& Get();
};

ATTestBlobDatabase& ATTestBlobDatabase::Get() {
	static ATTestBlobDatabase sDatabase;

	return sDatabase;
}

void ATTestOpenVFSBlob(const wchar_t *subPath, bool write, bool update, ATVFSFileView **view) {
	vdrefptr<ATTestBlob> blob;

	ATTestBlobDatabase& db = ATTestBlobDatabase::Get();
	auto it = db.mBlobs.find_as(subPath);

	if (it != db.mBlobs.end())
		blob = it->second;
	else if (write) {
		blob = new ATTestBlob;
		db.mBlobs.insert_as(subPath).first->second = blob;
	}

	if (!blob)
		throw MyError("Blob not found");

	if (write && !update)
		blob->mBuffer.clear();

	vdrefptr newView(new ATTestBlobStream(VDStringW(L"blob://") + subPath, !write, blob->mBuffer, blob));

	*view = newView.release();
}

std::tuple<vdvector_view<const uint8>, uint32, uint32> ATTestGetBlob(const wchar_t *s) {
	ATTestBlobDatabase& db = ATTestBlobDatabase::Get();
	auto it = db.mBlobs.find_as(s);
	if (it == db.mBlobs.end())
		throw VDException(L"Blob not found: %ls", s);

	ATTestBlob& blob = *it->second;
	uint32 minRange = blob.mMinWriteRange;
	uint32 maxRange = blob.mMaxWriteRange;

	if (maxRange < minRange)
		minRange = maxRange = 0;

	blob.mMinWriteRange = ~(uint32)0;
	blob.mMaxWriteRange = 0;

	const vdfastvector<uint8>& buffer = blob.mBuffer;
	return { vdvector_view<const uint8>(buffer.data(), buffer.size()), minRange, maxRange };
}

void ATTestInitBlobHandler() {
	ATVFSSetBlobProtocolHandler(ATTestOpenVFSBlob);
}
