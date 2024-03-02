//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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

#include "stdafx.h"
#include "savestate.h"

ATSaveStateReader::ATSaveStateReader(const uint8 *src, uint32 len)
	: mpSrc(src)
	, mSize(len)
	, mPosition(0)
{
}

ATSaveStateReader::~ATSaveStateReader() {
}

bool ATSaveStateReader::CheckAvailable(uint32 size) const {
	return (mSize - mPosition) >= size;
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

void ATSaveStateReader::ReadData(void *dst, uint32 count) {
	if (mSize - mPosition < count)
		return;

	memcpy(dst, mpSrc + mPosition, count);
	mPosition += count;
}

///////////////////////////////////////////////////////////////////////////

ATSaveStateWriter::ATSaveStateWriter(Storage& dst)
	: mDst(dst)
{
}

ATSaveStateWriter::~ATSaveStateWriter() {
}

void ATSaveStateWriter::WriteBool(bool b) {
	uint8 v = b ? 1 : 0;
	WriteData(&v, 1);
}

void ATSaveStateWriter::WriteSint8(sint8 v) {
	WriteData(&v, 1);
}

void ATSaveStateWriter::WriteSint16(sint16 v) {
	WriteData(&v, 2);
}

void ATSaveStateWriter::WriteSint32(sint32 v) {
	WriteData(&v, 4);
}

void ATSaveStateWriter::WriteUint8(uint8 v) {
	WriteData(&v, 1);
}

void ATSaveStateWriter::WriteUint16(uint16 v) {
	WriteData(&v, 2);
}

void ATSaveStateWriter::WriteUint32(uint32 v) {
	WriteData(&v, 4);
}

void ATSaveStateWriter::WriteData(const void *src, uint32 count) {
	const uint8 *p = (const uint8 *)src;

	mDst.insert(mDst.end(), p, p+count);
}
