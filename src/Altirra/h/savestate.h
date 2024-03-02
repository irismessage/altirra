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

#include <vd2/system/linearalloc.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_hashmap.h>
#include <at/atcore/savestate.h>

enum ATSaveStateSection {
	// architectural state
	kATSaveStateSection_Arch,

	// Altirra private state
	kATSaveStateSection_Private,

	kATSaveStateSection_ResetPrivate,
	kATSaveStateSection_End,

	kATSaveStateSectionCount
};

class ATSaveStateReader;

///////////////////////////////////////////////////////////////////////////
struct ATSaveStateReadHandler {
	size_t mSize;
	void (*mpDispatchFn)(ATSaveStateReader& reader, const ATSaveStateReadHandler *handler);
};

template<class T, class M>
struct ATSaveStateMethodReadHandler {
	ATSaveStateReadHandler mHandler;
	T *mpThis;
	M mpMethod;

	static void DispatchFn(ATSaveStateReader& reader, const ATSaveStateReadHandler *handler) {
		ATSaveStateMethodReadHandler *thisptr = (ATSaveStateMethodReadHandler *)handler;

		((thisptr->mpThis)->*(thisptr->mpMethod))(reader);
	}
};

///////////////////////////////////////////////////////////////////////////
class ATSaveStateReader {
	ATSaveStateReader(const ATSaveStateReader&);
	ATSaveStateReader& operator=(const ATSaveStateReader&);
public:
	ATSaveStateReader(const uint8 *src, uint32 len);
	~ATSaveStateReader();

	void RegisterHandler(ATSaveStateSection section, uint32 fcc, const ATSaveStateReadHandler& handler);

	template<class T, typename M>
	void RegisterHandlerMethod(ATSaveStateSection section, uint32 fcc, T *thisptr, M method) {
		const ATSaveStateMethodReadHandler<T,M> handler = {
			{ sizeof(ATSaveStateMethodReadHandler<T,M>), ATSaveStateMethodReadHandler<T,M>::DispatchFn },
			thisptr,
			method
		};

		RegisterHandler(section, fcc, handler.mHandler);
	}

	bool CheckAvailable(uint32 size) const;
	uint32 GetAvailable() const;

	void OpenChunk(uint32 length);
	void CloseChunk();

	void DispatchChunk(ATSaveStateSection section, uint32 fcc);

	bool ReadBool();
	sint8 ReadSint8();
	sint16 ReadSint16();
	sint32 ReadSint32();
	uint8 ReadUint8();
	uint16 ReadUint16();
	uint32 ReadUint32();
	uint64 ReadUint64();
	void ReadString(VDStringW& str);

	template<class T>
	void operator!=(T& val) {
		ReadData(&val, sizeof val);
	}

	void ReadData(void *dst, uint32 count);

protected:
	const uint8 *mpSrc;
	uint32 mPosition;
	uint32 mSize;

	vdfastvector<uint32> mChunkStack;

	struct HandlerEntry {
		HandlerEntry *mpNext;
		const ATSaveStateReadHandler *mpHandler;
	};

	typedef vdhashmap<uint32, HandlerEntry *> HandlerMap;
	HandlerMap mHandlers[kATSaveStateSectionCount];

	VDLinearAllocator mLinearAlloc;
};

#endif
