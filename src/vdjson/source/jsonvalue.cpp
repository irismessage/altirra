//	VirtualDub - Video processing and capture application
//	JSON I/O library
//	Copyright (C) 1998-2010 Avery Lee
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
#include <vd2/vdjson/jsonvalue.h>

const VDJSONValue VDJSONValue::null = { kTypeNull };

VDJSONValuePool::VDJSONValuePool(bool enableLineInfo, uint32 initialBlockSize, uint32 maxBlockSize, uint32 largeBlockThreshold)
	: mBlockSize(initialBlockSize)
	, mMaxBlockSize(maxBlockSize)
	, mLargeBlockThreshold(largeBlockThreshold)
	, mbLineInfoEnabled(enableLineInfo)
{
	static_assert(kCellSize >= sizeof(uint32) && (kCellSize & (kCellSize - 1)) == 0, "cell size must be a power of two of at least 4");
	static_assert(sizeof(BlockNode) % kCellSize == 0, "alignment not preserved after block node");
	static_assert(sizeof(BlockLineHeader) <= kCellSize && alignof(BlockLineHeader) <= kCellSize);

	// The difference between the large block threshold and the normal block size needs to be
	// at least the overhead of the block.
	uint32 overhead = sizeof(BlockNode);

	if (enableLineInfo)
		overhead += kLargeBlockLineOverhead;

	if (mLargeBlockThreshold < mBlockSize - overhead)
		mLargeBlockThreshold = mBlockSize - overhead;
}

VDJSONValuePool::~VDJSONValuePool() {
	while(mpHead) {
		void *p = mpHead;
		mpHead = mpHead->mpNext;
		free(p);
	}
}

void VDJSONValuePool::AdvanceLine() {
	// There are potentially two values that need to encode, in order:
	// - The line offset, if it's been more than one line since the last alloc
	// - The cell offset from the last line
	//
	// The line offset goes first as the previous allocations always belong to the
	// last line we were tracking -- otherwise we would have flushed earlier.
	//
	// Each of these can be up to 5 bytes in varint encoding and we need an additional
	// 0 to introduce the line offset, so we may need up to 10 bytes total. If we can't
	// fit it, though, that's OK -- we'll just terminate the block with the two bytes
	// already preallocated. That works because we won't have allocated anything with
	// the new line number yet and the last line number encoded is open-ended up to
	// the end of the block. We do need to allocate the rest of the space in the block
	// to ensure that no smaller allocation can still sneak through.

	if (!mbLineInfoEnabled)
		return;

	++mLineno;

	// no need for deltas if we haven't allocated anything
	const uint32 byteDelta = (uint32)(mpAllocNext - mpAllocNextLineBase);
	if (!byteDelta)
		return;

	const uint32 lineDelta = mLineno - mLinenoLast;
	mLinenoLast = mLineno;

	VDASSERT(byteDelta % kCellSize == 0);
	const uint32 cellDelta = byteDelta / kCellSize;

	uint8 varintBuf[11];
	uint8 *end = varintBuf + 10;
	uint8 *dst = end;
	if (lineDelta > 1) {
		*--dst = 0;
		dst = EncodeRevVarint(dst, lineDelta - 1);
	}

	dst = EncodeRevVarint(dst, cellDelta);

	uint32 lineInfoSize = (uint32)(end - dst);

	if (mAllocLeft <= lineInfoSize) {
		// no space for line info and another alloc -- close this block so next
		// alloc starts new one
		mAllocLeft = 0;
		return;
	}

	// write line info
	mAllocLeft -= lineInfoSize;
	mpLineInfoDst -= lineInfoSize;
	memcpy(mpLineInfoDst, dst, lineInfoSize);

	// reset allocation tracking pointer for next line
	mpAllocNextLineBase = mpAllocNext;
}

uint32 VDJSONValuePool::GetLineForObject(const void *p) const {
	if (!mbLineInfoEnabled)
		return 0;

	uintptr adr = (uintptr)p;
	for(BlockNode *block = mpHead; block; block = block->mpNext) {
		// When line info is enabled, the first cell after the block header is
		// the size of the block, including the block header at the front and the line
		// info at the back.
		const BlockLineHeader& blockLineHdr = *(const BlockLineHeader *)(block + 1);

		if (adr - (uintptr)block < blockLineHdr.mBlockSize) {
			// It's in this block. Check if this is the head block; if so, make sure there's
			// a temporary terminator in place -- we always have room to do this. If we're not
			// done, this will just get overwritten again.
			if (block == mpHead) {
				mpLineInfoDst[-1] = 0;
				mpLineInfoDst[-2] = 0;
			}
			
			// The line info is at the end of the block, written backwards.
			uint32 lineno = blockLineHdr.mStartingLineno;

			// iterate down remaining line data
			uintptr lineBase = (uintptr)(block + 1) + kCellSize;
			const uint8 *src = (const uint8 *)block + blockLineHdr.mBlockSize;
			uint8 c;
			for(;;) {
				// read varint containing cell offset
				uint32 cellOffset = 0;
				do {
					c = *--src;
					cellOffset = (cellOffset << 7) + (c & 0x7F);
				} while(c & 0x80);

				// check if the byte offset is zero
				if (cellOffset == 0) {
					// it is zero -- read line count
					uint32 lineCount = 0;

					do {
						c = *--src;
						lineCount = (lineCount << 7) + (c & 0x7F);
					} while(c & 0x80);

					// if the line count is zero too, we hit the terminator and so the object
					// must be in the last line span of the block
					if (!lineCount)
						return lineno;

					lineno += lineCount;
				} else {
					// it is non-zero -- check if the object is within this span
					uintptr lineEnd = lineBase + cellOffset * kCellSize;
					if (adr < lineEnd)
						return lineno;

					// advance to next line
					++lineno;
					lineBase = lineEnd;
				}
			}
		}
	}

	return 0;
}

VDJSONValue *VDJSONValuePool::AddValue() {
	VDJSONValue *value = (VDJSONValue *)Allocate(sizeof(VDJSONValue));

	value->mType = VDJSONValue::kTypeNull;
	return value;
}

void VDJSONValuePool::AddArray(VDJSONValue& dst, const VDJSONValue *const *values, size_t n) {
	VDJSONArray *arr = (VDJSONArray *)Allocate(sizeof(VDJSONArray));
	dst.Set(arr);
	arr->mLength = n;

	auto *els = (const VDJSONValue **)Allocate(sizeof(const VDJSONValue *) * n);
	arr->mpElements = els;

	memcpy(els, values, sizeof(const VDJSONValue *) * n);
}

VDJSONValue *VDJSONValuePool::AddObjectMember(VDJSONValue& dst, uint32 nameToken, VDJSONMember*& tail) {
	VDJSONMember *el = (VDJSONMember *)Allocate(sizeof(VDJSONMember));

	if (dst.mType != VDJSONValue::kTypeObject) {
		dst.mType = VDJSONValue::kTypeObject;
		dst.mpObject = NULL;
	}

	if (tail)
		tail->mpNext = el;
	else
		dst.mpObject = el;

	tail = el;

	el->mNameToken = nameToken;
	el->mValue.Set();
	el->mpNext = nullptr;
	return &el->mValue;
}

const VDJSONString *VDJSONValuePool::AddString(const wchar_t *s) {
	return AddString(s, wcslen(s));
}

const VDJSONString *VDJSONValuePool::AddString(const wchar_t *s, size_t len) {
	VDJSONString *str = (VDJSONString *)Allocate(sizeof(VDJSONString));
	wchar_t *t = (wchar_t *)Allocate(sizeof(wchar_t) * (len + 1));

	memcpy(t, s, len * sizeof(wchar_t));
	t[len] = 0;
	str->mpChars = t;
	str->mLength = len;

	return str;
}

void VDJSONValuePool::AddString(VDJSONValue& dst, const wchar_t *s) {
	AddString(dst, s, wcslen(s));
}

void VDJSONValuePool::AddString(VDJSONValue& dst, const wchar_t *s, size_t len) {
	dst.Set(AddString(s, len));
}

void *VDJSONValuePool::Allocate(size_t n) {
	n = (n + kCellSizeMask) & ~kCellSizeMask;

	if (mAllocLeft < n) {
		if (n >= mLargeBlockThreshold) {
			size_t lbsize = sizeof(BlockNode) + n;

			if (mbLineInfoEnabled) {
				// max varint for 32-bit is 5 bytes, +2 for terminato
				lbsize += kCellSize + 2;
			}

			BlockNode *node = (BlockNode *)malloc(lbsize);
			
			node->mpNext = mpHead->mpNext;
			mpHead->mpNext = node;

			if (mbLineInfoEnabled) {
				BlockLineHeader *lineHdr = (BlockLineHeader *)(node + 1);
				lineHdr->mBlockSize = (uint32)lbsize;
				lineHdr->mStartingLineno = mLineno;

				uint8 *linedst = (uint8 *)node + lbsize;
				linedst[-1] = 0;
				linedst[-2] = 0;

				return lineHdr + 1;
			} else {
				return node + 1;
			}
		}

		// if we had a previous block with line number info, terminate it now
		if (mpLineInfoDst) {
			mpLineInfoDst[-1] = 0;	// cell offset = 0 (special)
			mpLineInfoDst[-2] = 0;	// line offset = 0 (end of block)
		}

		BlockNode *node = (BlockNode *)malloc(mBlockSize);

		node->mpNext = mpHead;
		mpHead = node;
		mAllocLeft = mBlockSize - sizeof(BlockNode);
		mpAllocNext = (char *)(node + 1);

		if (mbLineInfoEnabled) {
			// write the block size and initial line number in the first cell
			BlockLineHeader *lineHdr = (BlockLineHeader *)mpAllocNext;
			lineHdr->mBlockSize = mBlockSize;
			lineHdr->mStartingLineno = mLineno;
			mpAllocNext += kCellSize;

			mpLineInfoDst = (uint8 *)node + mBlockSize;
			mpAllocNextLineBase = mpAllocNext;
			mLinenoLast = mLineno;

			// adjust space remaining to allocate
			mAllocLeft -= kCellSize + 2;
			VDASSERT(mAllocLeft >= n);
		}

		// Double the block size each time so we increase efficiency as we allocate more.
		// The available space more than doubles due to overhead, so doubling the large
		// block threshold is OK as long as we haven't hit the limit.
		mBlockSize += mBlockSize;

		if (mBlockSize > mMaxBlockSize)
			mBlockSize = mMaxBlockSize;
		else
			mLargeBlockThreshold += mLargeBlockThreshold;
	}

	void *p = mpAllocNext;
	mAllocLeft -= n;
	mpAllocNext += n;

	return p;
}

uint8 *VDJSONValuePool::EncodeRevVarint(uint8 *dst, uint32 value) {
	if (value >= (UINT32_C(1) << 28))
		*--dst = (uint8)((value >> 28) | 0x80);

	if (value >= (UINT32_C(1) << 21))
		*--dst = (uint8)((value >> 21) | 0x80);

	if (value >= (UINT32_C(1) << 14))
		*--dst = (uint8)((value >> 14) | 0x80);

	if (value >= (UINT32_C(1) << 7))
		*--dst = (uint8)((value >> 7) | 0x80);

	*--dst = (uint8)(value & 0x7F);
	return dst;
}

///////////////////////////////////////////////////////////////////////////

size_t VDJSONValueRef::GetMemberCount() const {
	if (mpRef->mType != VDJSONValue::kTypeObject)
		return 0;

	size_t n = 0;

	for(auto *p = mpRef->mpObject; p; p = p->mpNext)
		++n;

	return n;
}

const VDJSONValueRef VDJSONValueRef::operator[](size_t index) const {
	if (mpRef->mType != VDJSONValue::kTypeArray)
		return VDJSONValueRef(mpDoc, &VDJSONValue::null);

	VDJSONArray *arr = mpRef->mpArray;
	if (index >= arr->mLength)
		return VDJSONValueRef(mpDoc, &VDJSONValue::null);

	return VDJSONValueRef(mpDoc, arr->mpElements[index]);
}

const VDJSONValueRef VDJSONValueRef::operator[](VDJSONNameToken nameToken) const {
	if (mpRef->mType == VDJSONValue::kTypeObject) {
		uint32 token = nameToken.mToken;
		if (token) {
			for(VDJSONMember *p = mpRef->mpObject; p; p = p->mpNext) {
				if (p->mNameToken == token)
					return VDJSONValueRef(mpDoc, &p->mValue);
			}
		}
	}

	return VDJSONValueRef(mpDoc, &VDJSONValue::null);
}

const VDJSONValueRef VDJSONValueRef::operator[](const char *s) const {
	return operator[](mpDoc->mNameTable.GetToken(s));
}

const VDJSONValueRef VDJSONValueRef::operator[](const wchar_t *s) const {
	return operator[](mpDoc->mNameTable.GetToken(s));
}

void VDJSONValueRef::RequireObject() const {
	if (!IsObject())
		throw VDParseException("A required object was not found.");
}

void VDJSONValueRef::RequireInt() const {
	if (!IsInt())
		throw VDParseException("A required integer was not found.");
}

void VDJSONValueRef::RequireString() const {
	if (!IsString())
		throw VDParseException("A required string was not found.");
}

const VDJSONArrayEnum VDJSONValueRef::GetRequiredArray(const char *key) const {
	const auto& node = operator[](key);
	if (!node.IsValid())
		throw VDParseException("A required array element was not found: %s", key);

	if (!node.IsArray())
		throw VDParseException("An element was not of array type: %s", key);

	return node.AsArray();
}

bool VDJSONValueRef::GetRequiredBool(const char *key) const {
	const auto& node = operator[](key);
	if (!node.IsValid())
		throw VDParseException("A required boolean element was not found: %s", key);

	if (!node.IsBool())
		throw VDParseException("An element was not of boolean type: %s", key);

	return node.AsBool();
}

sint64 VDJSONValueRef::GetRequiredInt64(const char *key) const {
	const auto& node = operator[](key);
	if (!node.IsValid())
		throw VDParseException("A required boolean element was not found: %s", key);

	if (!node.IsInt())
		throw VDParseException("An element was not of integer type: %s", key);

	return node.AsInt64();
}

const wchar_t *VDJSONValueRef::GetRequiredString(const char *key) const {
	const auto& node = operator[](key);
	if (!node.IsValid())
		throw VDParseException("A required string element was not found: %s", key);

	if (!node.IsString())
		throw VDParseException("An element was not of string type: %s", key);

	return node.AsString();
}

double VDJSONValueRef::ConvertToReal() const {
	if (mpRef->mType == VDJSONValue::kTypeInt)
		return (double)mpRef->mIntValue;

	return 0;
}

uint32 VDJSONValueRef::GetLineNumber() const {
	return mpDoc ? mpDoc->mPool.GetLineForObject(mpRef) : 0;
}