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

#ifndef f_AT_SAVESTATE_H
#define f_AT_SAVESTATE_H

#ifdef _MSC_VER
#pragma once
#endif

#include <vd2/system/vdstl.h>

class ATSaveStateReader {
	ATSaveStateReader(const ATSaveStateReader&);
	ATSaveStateReader& operator=(const ATSaveStateReader&);
public:
	ATSaveStateReader(const uint8 *src, uint32 len);
	~ATSaveStateReader();

	bool CheckAvailable(uint32 size) const;

	bool ReadBool();
	sint8 ReadSint8();
	sint16 ReadSint16();
	sint32 ReadSint32();
	uint8 ReadUint8();
	uint16 ReadUint16();
	uint32 ReadUint32();

	template<class T>
	void operator!=(T& val) {
		ReadData(&val, sizeof val);
	}

	void ReadData(void *dst, uint32 count);

protected:
	const uint8 *mpSrc;
	uint32 mPosition;
	uint32 mSize;
};

class ATSaveStateWriter {
	ATSaveStateWriter(const ATSaveStateWriter&);
	ATSaveStateWriter& operator=(const ATSaveStateWriter&);
public:
	typedef vdfastvector<uint8> Storage;

	ATSaveStateWriter(Storage& dst);
	~ATSaveStateWriter();

	void WriteBool(bool b);
	void WriteSint8(sint8 v);
	void WriteSint16(sint16 v);
	void WriteSint32(sint32 v);
	void WriteUint8(uint8 v);
	void WriteUint16(uint16 v);
	void WriteUint32(uint32 v);

	template<class T>
	void operator!=(const T& val) {
		WriteData(&val, sizeof val);
	}

	void WriteData(const void *src, uint32 count);

protected:
	Storage& mDst;
};

#endif	// f_AT_SAVESTATE_H
