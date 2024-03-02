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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include "savestate.h"

///////////////////////////////////////////////////////////////////////////

ATInvalidSaveStateException::ATInvalidSaveStateException()
	: MyError("The save state data is invalid.")
{
}

ATUnsupportedSaveStateException::ATUnsupportedSaveStateException()
	: MyError("The saved state uses features unsupported by this version of Altirra and cannot be loaded.")
{
}

///////////////////////////////////////////////////////////////////////////

ATSaveStateReader::ATSaveStateReader(const uint8 *src, uint32 len)
	: mpSrc(src)
	, mSize(len)
	, mPosition(0)
{
}

ATSaveStateReader::~ATSaveStateReader() {
}

void ATSaveStateReader::RegisterHandler(ATSaveStateSection section, uint32 fcc, const ATSaveStateReadHandler& handler) {
	ATSaveStateReadHandler *p = (ATSaveStateReadHandler *)mLinearAlloc.Allocate(handler.mSize);
	memcpy(p, &handler, handler.mSize);

	HandlerEntry *he = (HandlerEntry *)mLinearAlloc.Allocate(sizeof(HandlerEntry));
	he->mpNext = NULL;
	he->mpHandler = p;

	HandlerMap::insert_return_type r = mHandlers[section].insert(fcc);

	if (r.second) {
		// no previous entries... link in directly
		r.first->second = he;
	} else {
		// previous entries... link in at *end* of chain
		HandlerEntry *next = r.first->second;

		while(next->mpNext)
			next = next->mpNext;

		next->mpNext = he;
	}
}

bool ATSaveStateReader::CheckAvailable(uint32 size) const {
	return (mSize - mPosition) >= size;
}

uint32 ATSaveStateReader::GetAvailable() const {
	return mSize - mPosition;
}

void ATSaveStateReader::OpenChunk(uint32 length) {
	if (mSize - mPosition < length)
		throw ATInvalidSaveStateException();

	mChunkStack.push_back(mSize);
	mSize = mPosition + length;
}

void ATSaveStateReader::CloseChunk() {
	mPosition = mSize;
	mSize = mChunkStack.back();
	mChunkStack.pop_back();
}

void ATSaveStateReader::DispatchChunk(ATSaveStateSection section, uint32 fcc) {
	HandlerMap::iterator it = mHandlers[section].find(fcc);

	if (it == mHandlers[section].end())
		return;

	HandlerEntry *he = it->second;

	do {
		const ATSaveStateReadHandler *h = he->mpHandler;

		h->mpDispatchFn(*this, h);

		he = he->mpNext;

		// Zero is a special broadcast case.
	} while(he && !fcc);

	// remove handlers that we processed
	it->second = he;
}

bool ATSaveStateReader::ReadBool() {
	uint8 v = 0;
	ReadData(&v, 1);
	return v != 0;
}

sint8 ATSaveStateReader::ReadSint8() {
	sint8 v = 0;
	ReadData(&v, 1);
	return v;
}

sint16 ATSaveStateReader::ReadSint16() {
	sint16 v = 0;
	ReadData(&v, 2);
	return v;
}

sint32 ATSaveStateReader::ReadSint32() {
	sint32 v = 0;
	ReadData(&v, 4);
	return v;
}

uint8 ATSaveStateReader::ReadUint8() {
	uint8 v = 0;
	ReadData(&v, 1);
	return v;
}

uint16 ATSaveStateReader::ReadUint16() {
	uint16 v = 0;
	ReadData(&v, 2);
	return v;
}

uint32 ATSaveStateReader::ReadUint32() {
	uint32 v = 0;
	ReadData(&v, 4);
	return v;
}

uint64 ATSaveStateReader::ReadUint64() {
	uint64 v = 0;
	ReadData(&v, 8);
	return v;
}

void ATSaveStateReader::ReadString(VDStringW& str) {
	uint32 len = 0;
	int shift = 0;

	for(;;) {
		uint8 v;
		ReadData(&v, 1);

		len += (v & 0x7f) << shift;
		shift += 7;

		if (!(v & 0x80))
			break;
	}

	if (mSize - mPosition < len)
		throw ATInvalidSaveStateException();

	str = VDTextU8ToW((const char *)(mpSrc + mPosition), len);
	mPosition += len;
}

void ATSaveStateReader::ReadData(void *dst, uint32 count) {
	if (mSize - mPosition < count)
		throw ATInvalidSaveStateException();

	memcpy(dst, mpSrc + mPosition, count);
	mPosition += count;
}
