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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

//=========================================================================
// Coprocessor memory map
//
// The coprocessors access memory through a page map to support control
// registers, unmapped regions, and mirrored regions. The page map consists
// of two arrays of 256 entries, one for reads and another for writes.
// Each entry can be of two types:
//
// - An offset pointer to raw memory. Specifically, a pointer to the
//   memory for a page, with the base address of the page in bytes
//   subtracted off. For instance, a page mapped at $1000 would have
//   (buffer-0x1000) as the page map entry. This allows large regions of
//   memory to be mapped by doing a fill of all page entries with the same
//   value. The mapped memory must be aligned to an even boundary so that
//   the LSB of the page entry is cleared.
//
// - A reference to a custom read/write handler, of either type read node
//   for the read map or write node for the write map. The page map entry
//   is the address of the node with the LSB set.
//
// For read entries, there are two callbacks, one for a regular read and
// another for a debug read. A debug read suppresses all side effects for
// that read and does not change simulation state.
//

#ifndef f_ATCOPROC_MEMORYMAP_H
#define f_ATCOPROC_MEMORYMAP_H

#include <vd2/system/vdtypes.h>

struct ATCoProcReadMemNode {
	uint8 (*mpRead)(uint32 addr, void *thisptr);
	uint8 (*mpDebugRead)(uint32 addr, void *thisptr);
	void *mpThis;

	template<typename T, typename T_Read, typename T_DebugRead>
	requires(
		requires(T& obj, const T& cobj, const T_Read& fn1, const T_DebugRead& fn2, uint8& v) {
			v = fn1(uint32(), &obj);
			v = fn2(uint32(), &cobj);
		}
	)
	void BindLambdas(T *thisptr, const T_Read&, const T_DebugRead&) {
		mpThis = thisptr;

		mpRead = [](uint32 addr, void *thisptr) -> uint8 {
			return T_Read()(addr, (T *)(thisptr));
		};

		mpDebugRead = [](uint32 addr, void *thisptr) -> uint8 {
			return T_DebugRead()(addr, (const T *)(thisptr));
		};
	}

	template<typename T, typename T_DualRead>
	requires(
		requires(const T& cobj, const T_DualRead& fn, uint8& v) {
			v = fn(uint32(), &cobj);
		}
	)
	void BindLambdas(T *thisptr, const T_DualRead&) {
		mpThis = thisptr;

		mpDebugRead = [](uint32 addr, void *thisptr) {
			return T_DualRead()(addr, (const T *)(thisptr));
		};

		mpRead = mpDebugRead;
	}

	template<auto T_Read, auto T_DebugRead, typename T>
	requires(
		requires(T& obj, const T& cobj, uint8& v) {
			v = (obj.*T_Read)(uint32());
			v = (cobj.*T_DebugRead)(uint32());
		}
	)
	void BindMethods(T *thisptr) {
		mpThis = thisptr;

		mpRead = [](uint32 addr, void *thisptr) -> uint8 {
			return ((T *)(thisptr)->*T_Read)(addr);
		};

		mpDebugRead = [](uint32 addr, void *thisptr) -> uint8 {
			return ((const T *)(thisptr)->*T_DebugRead)(addr);
		};
	}

	template<auto T_DualRead, typename T>
	requires(
		requires(const T& cobj, uint8& v) {
			v = (cobj.*T_DualRead)(uint32());
		}
	)
	void BindMethod(const T *thisptr) {
		mpThis = const_cast<T *>(thisptr);

		mpDebugRead = [](uint32 addr, void *thisptr) {
			return (((const T *)(thisptr))->*T_DualRead)(addr);
		};

		mpRead = mpDebugRead;
	}

	uintptr AsBase() const {
		return (uintptr)this + 1;
	}
};

struct ATCoProcWriteMemNode {
	void (*mpWrite)(uint32 addr, uint8 val, void *thisptr);
	void *mpThis;

	template<typename T, typename T_Lambda>
	requires(
		requires(T& obj, const T_Lambda& fn) {
			fn(uint32(), uint8(), &obj);
		}
	)
	void BindLambda(T *thisptr, const T_Lambda&) {
		mpThis = thisptr;
		mpWrite = [](uint32 addr, uint8 val, void *thisptr) {
			return T_Lambda()(addr, val, (T *)(thisptr));
		};
	}

	template<auto T_Method, typename T>
	requires(
		requires(T& obj) {
			(obj.*T_Method)(uint32(), uint8());
		}
	)
	void BindMethod(T *thisptr) {
		mpThis = thisptr;
		mpWrite = [](uint32 addr, uint8 val, void *thisptr) {
			return ((T *)(thisptr)->*T_Method)(addr, val);
		};
	}

	uintptr AsBase() const {
		return (uintptr)this + 1;
	}
};

void ATCoProcDebugReadMemory(const uintptr *readMap, void *dst, uint32 start, uint32 len);
void ATCoProcReadMemory(const uintptr *readMap, void *dst, uint32 start, uint32 len);
void ATCoProcWriteMemory(const uintptr *writeMap, const void *src, uint32 start, uint32 len);

class ATCoProcMemoryMapView {
public:
	ATCoProcMemoryMapView(uintptr *readMap, uintptr *writeMap, uint32 *traceMap = nullptr)
		: mpReadMap(readMap), mpWriteMap(writeMap), mpTraceMap(traceMap) {}

	// Fill the entire memory map with read and write pages (nominally the open bus and
	// dummy write pages). Both must be 256 bytes.
	void Clear(const void *readPage, void *writePage);

	// Set the read, write, or both read/write mappings for a range of pages to the given
	// memory block. The memory block must be aligned to 2 and size 256*n.
	void SetMemory(uint32 basePage, uint32 n, void *mem);
	void SetReadMem(uint32 basePage, uint32 n, const void *mem);
	void SetReadMemTraceable(uint32 basePage, uint32 n, const void *mem);
	void SetWriteMem(uint32 basePage, uint32 n, void *mem);

	// Set the rw/r/w mappings for a range of pages to the given handlers.
	void SetHandlers(uint32 basePage, uint32 n, const ATCoProcReadMemNode& readNode, const ATCoProcWriteMemNode& writeNode);
	void SetReadHandler(uint32 basePage, uint32 n, const ATCoProcReadMemNode& node);
	void SetWriteHandler(uint32 basePage, uint32 n, const ATCoProcWriteMemNode& node);

	// Repeat the same 256 byte page of memory for a range of pages.
	void RepeatPage(uint32 basePage, uint32 n, void *mem);
	void RepeatReadPage(uint32 basePage, uint32 n, const void *mem);
	void RepeatWritePage(uint32 basePage, uint32 n, void *mem);

	// Copy the mappings from a source range to a destination range. The
	// copy is ascending, starting at the given pages, so replication is
	// possible.
	void MirrorFwd(uint32 basePage, uint32 n, uint32 srcBasePage);

private:
	static void Fill(uintptr *p, uint32 n, uintptr key);
	static void FillInc(uintptr *p, uint32 n, uintptr key, uintptr keyInc);

	uintptr *mpReadMap;
	uintptr *mpWriteMap;
	uint32 *mpTraceMap;
};

#endif	// f_ATCOPROC_MEMORYMAP_H
