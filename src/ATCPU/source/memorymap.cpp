//	Altirra - Atari 800/800XL/5200 emulator
//	Coprocessor library - CPU memory map support
//	Copyright (C) 2009-2016 Avery Lee
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
#include <at/atcpu/memorymap.h>

void ATCoProcDebugReadMemory(const uintptr *readMap, void *dst, uint32 address, uint32 n) {
	while(n) {
		if (address >= 0x10000) {
			memset(dst, 0, n);
			break;
		}

		uint32 tc = 256 - (address & 0xff);
		if (tc > n)
			tc = n;

		const uintptr pageBase = readMap[address >> 8];

		if (pageBase & 1) {
			const auto *node = (const ATCoProcReadMemNode *)(pageBase - 1);

			for(uint32 i=0; i<tc; ++i)
				((uint8 *)dst)[i] = node->mpDebugRead(address++, node->mpThis);
		} else {
			memcpy(dst, (const uint8 *)(pageBase + address), tc);

			address += tc;
		}

		n -= tc;
		dst = (char *)dst + tc;
	}
}

void ATCoProcReadMemory(const uintptr *readMap, void *dst, uint32 address, uint32 n) {
	while(n) {
		if (address >= 0x10000) {
			memset(dst, 0, n);
			break;
		}

		uint32 tc = 256 - (address & 0xff);
		if (tc > n)
			tc = n;

		const uintptr pageBase = readMap[address >> 8];

		if (pageBase & 1) {
			const auto *node = (const ATCoProcReadMemNode *)(pageBase - 1);

			for(uint32 i=0; i<tc; ++i)
				((uint8 *)dst)[i] = node->mpRead(address++, node->mpThis);
		} else {
			memcpy(dst, (const uint8 *)(pageBase + address), tc);

			address += tc;
		}

		n -= tc;
		dst = (char *)dst + tc;
	}
}

void ATCoProcWriteMemory(const uintptr *writeMap, const void *src, uint32 address, uint32 n) {
	while(n) {
		if (address >= 0x10000)
			break;

		const uintptr pageBase = writeMap[address >> 8];

		if (pageBase & 1) {
			auto& writeNode = *(ATCoProcWriteMemNode *)(pageBase - 1);

			writeNode.mpWrite(address, *(const uint8 *)src, writeNode.mpThis);
			++address;
			src = (const uint8 *)src + 1;
			--n;
		} else {
			uint32 tc = 256 - (address & 0xff);
			if (tc > n)
				tc = n;

			memcpy((uint8 *)(pageBase + address), src, tc);

			n -= tc;
			address += tc;
			src = (const char *)src + tc;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

void ATCoProcMemoryMapView::Clear(const void *readPage, void *writePage) {
	RepeatReadPage(0, 256, readPage);
	RepeatWritePage(0, 256, writePage);

	if (mpTraceMap)
		memset(mpTraceMap, 0, 256*sizeof(mpTraceMap[0]));
}

void ATCoProcMemoryMapView::SetMemory(uint32 basePage, uint32 n, void *mem) {
	VDASSERT(basePage <= 0x100 && n <= 0x100 - basePage);
	VDASSERT(!((uintptr)mem & 1));

	uintptr key = (uintptr)mem - (basePage << 8);

	Fill(&mpReadMap[basePage], n, key);
	Fill(&mpWriteMap[basePage], n, key);

	if (mpTraceMap) {
		for(uint32 i=0; i<n; ++i)
			mpTraceMap[basePage + i] = 0;
	}
}

void ATCoProcMemoryMapView::SetReadMem(uint32 basePage, uint32 n, const void *mem) {
	VDASSERT(basePage <= 0x100 && n <= 0x100 - basePage);
	VDASSERT(!((uintptr)mem & 1));

	Fill(&mpReadMap[basePage], n, (uintptr)mem - (basePage << 8));

	if (mpTraceMap) {
		for(uint32 i=0; i<n; ++i)
			mpTraceMap[basePage + i] = 0;
	}
}

void ATCoProcMemoryMapView::SetReadMemTraceable(uint32 basePage, uint32 n, const void *mem) {
	VDASSERT(basePage <= 0x100 && n <= 0x100 - basePage);
	VDASSERT(!((uintptr)mem & 1));

	Fill(&mpReadMap[basePage], n, (uintptr)mem - (basePage << 8));

	if (mpTraceMap) {
		for(uint32 i=0; i<n; ++i)
			mpTraceMap[basePage + i] = 1;
	}
}

void ATCoProcMemoryMapView::SetWriteMem(uint32 basePage, uint32 n, void *mem) {
	VDASSERT(basePage <= 0x100 && n <= 0x100 - basePage);
	VDASSERT(!((uintptr)mem & 1));

	Fill(&mpWriteMap[basePage], n, (uintptr)mem - (basePage << 8));
}

void ATCoProcMemoryMapView::SetHandlers(uint32 basePage, uint32 n, const ATCoProcReadMemNode& readNode, const ATCoProcWriteMemNode& writeNode) {
	SetReadHandler(basePage, n, readNode);
	SetWriteHandler(basePage, n, writeNode);
}

void ATCoProcMemoryMapView::SetReadHandler(uint32 basePage, uint32 n, const ATCoProcReadMemNode& node) {
	VDASSERT(basePage <= 0x100 && n <= 0x100 - basePage);

	Fill(&mpReadMap[basePage], n, node.AsBase());
}

void ATCoProcMemoryMapView::SetWriteHandler(uint32 basePage, uint32 n, const ATCoProcWriteMemNode& node) {
	VDASSERT(basePage <= 0x100 && n <= 0x100 - basePage);

	Fill(&mpWriteMap[basePage], n, node.AsBase());
}

void ATCoProcMemoryMapView::RepeatPage(uint32 basePage, uint32 n, void *mem) {
	RepeatReadPage(basePage, n, mem);
	RepeatWritePage(basePage, n, mem);
}

void ATCoProcMemoryMapView::RepeatReadPage(uint32 basePage, uint32 n, const void *mem) {
	VDASSERT(basePage <= 0x100 && n <= 0x100 - basePage);
	VDASSERT(!((uintptr)mem & 1));

	FillInc(&mpReadMap[basePage], n, (uintptr)mem - (basePage << 8), (uintptr)0-0x100);
}

void ATCoProcMemoryMapView::RepeatWritePage(uint32 basePage, uint32 n, void *mem) {
	VDASSERT(basePage <= 0x100 && n <= 0x100 - basePage);
	VDASSERT(!((uintptr)mem & 1));

	FillInc(&mpWriteMap[basePage], n, (uintptr)mem - (basePage << 8), (uintptr)0-0x100);
}

void ATCoProcMemoryMapView::MirrorFwd(uint32 basePage, uint32 n, uint32 srcBasePage) {
	VDASSERT(basePage <= 0x100 && n <= 0x100 - basePage);
	VDASSERT(srcBasePage <= 0x100 && n <= 0x100 - srcBasePage);

	if (mpTraceMap) {
		for(uint32 i = 0; i < n; ++i)
			mpTraceMap[basePage + i] = mpTraceMap[srcBasePage + i];
	}

	uintptr *VDRESTRICT dr = &mpReadMap[basePage];
	uintptr *VDRESTRICT sr = &mpReadMap[srcBasePage];
	uintptr *VDRESTRICT dw = &mpWriteMap[basePage];
	uintptr *VDRESTRICT sw = &mpWriteMap[srcBasePage];
	uintptr keyAdjustment = ((uintptr)srcBasePage - (uintptr)basePage) << 8;

	while(n--) {
		uintptr rkey = *sr++;
		*dr++ = rkey & 1 ? rkey : rkey + keyAdjustment;

		uintptr wkey = *sw++;
		*dw++ = wkey & 1 ? wkey : wkey + keyAdjustment;
	}
}

void ATCoProcMemoryMapView::Fill(uintptr *p, uint32 n, uintptr key) {
	while(n--)
		*p++ = key;
}

void ATCoProcMemoryMapView::FillInc(uintptr *p, uint32 n, uintptr key, uintptr keyInc) {
	while(n--) {
		*p++ = key;
		key += keyInc;
	}
}
