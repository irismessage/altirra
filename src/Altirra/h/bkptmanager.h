//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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

#ifndef f_AT_BKPTMANAGER_H
#define f_AT_BKPTMANAGER_H

#include <map>
#include <vd2/system/vdstl.h>
#include <vd2/system/event.h>

class ATCPUEmulator;
class ATMemoryManager;
class ATMemoryLayer;
class ATSimulator;

typedef vdfastvector<uint32> ATBreakpointIndices;

struct ATBreakpointInfo {
	sint32	mAddress;
	uint32	mLength;
	bool	mbBreakOnPC;
	bool	mbBreakOnRead;
	bool	mbBreakOnWrite;
};

struct ATBreakpointEvent {
	uint32	mIndex;
	uint32	mAddress;
	uint8	mValue;
	bool	mbBreak;
	bool	mbSilentBreak;
};

class ATBreakpointManager {
	ATBreakpointManager(const ATBreakpointManager&);
	ATBreakpointManager& operator=(const ATBreakpointManager&);
public:

	ATBreakpointManager();
	~ATBreakpointManager();

	void Init(ATCPUEmulator *cpu, ATMemoryManager *memmgr, ATSimulator *sim);
	void Shutdown();

	void GetAll(ATBreakpointIndices& indices) const;
	void GetAtPC(uint16 pc, ATBreakpointIndices& indices) const;
	void GetAtAccessAddress(uint32 addr, ATBreakpointIndices& indices) const;
	bool GetInfo(uint32 idx, ATBreakpointInfo& info) const;

	bool IsSetAtPC(uint16 pc) const;
	void ClearAtPC(uint16 pc);
	uint32 SetAtPC(uint16 pc);
	uint32 SetAccessBP(uint16 address, bool read, bool write);
	uint32 SetAccessRangeBP(uint16 address, uint32 len, bool read, bool write);
	bool Clear(uint32 id);
	void ClearAll();

	VDEvent<ATBreakpointManager, ATBreakpointEvent *>& OnBreakpointHit() { return mEventBreakpointHit; }

	inline int TestPCBreakpoint(uint16 pc);
	inline bool TestReadBreakpoint(uint16 addr);
	inline bool TestWriteBreakpoint(uint16 addr);

protected:
	typedef ATBreakpointIndices BreakpointIndices;

	enum BreakpointType {
		kBPT_PC			= 0x01,
		kBPT_Read		= 0x02,
		kBPT_Write		= 0x04,
		kBPT_Range		= 0x08
	};

	struct BreakpointEntry {
		uint32	mAddress;
		uint8	mType;
	};

	struct BreakpointFreePred {
		bool operator()(const BreakpointEntry& x) const {
			return !x.mType;
		}
	};

	struct BreakpointRangeEntry {
		uint32	mAddress;			///< Base address of range.
		uint32	mLength;			///< Length of range in bytes.
		uint32	mIndex;				///< Breakpoint index.
		uint32	mPriorLimit;		///< Highest address+length of any previous range.
		uint8	mAttrFlags;			
	};

	struct BreakpointRangeAddressPred {
		bool operator()(const BreakpointRangeEntry& x, const BreakpointRangeEntry& y) const {
			return x.mAddress < y.mAddress;
		}

		bool operator()(const BreakpointRangeEntry& x, uint32 address) const {
			return x.mAddress < address;
		}

		bool operator()(uint32 address, const BreakpointRangeEntry& y) const {
			return address < y.mAddress;
		}
	};

	void RecomputeRangePriorLimits();
	void RegisterAccessPage(uint32 address, bool read, bool write);
	void UnregisterAccessPage(uint32 address, bool read, bool write);

	int CheckPCBreakpoints(uint32 pc, const BreakpointIndices& bps);	
	static sint32 OnAccessTrapRead(void *thisptr, uint32 addr);
	static bool OnAccessTrapWrite(void *thisptr, uint32 addr, uint8 value);

	ATCPUEmulator *mpCPU;
	ATMemoryManager *mpMemMgr;
	ATSimulator *mpSim;

	typedef vdfastvector<BreakpointEntry> Breakpoints;
	Breakpoints mBreakpoints;

	typedef vdhashmap<uint32, BreakpointIndices> BreakpointsByAddress;
	BreakpointsByAddress mCPUBreakpoints;
	BreakpointsByAddress mAccessBreakpoints;

	typedef vdfastvector<BreakpointRangeEntry> AccessRangeBreakpoints;
	AccessRangeBreakpoints mAccessRangeBreakpoints;

	struct AccessBPLayer {
		uint32 mRefCountRead;
		uint32 mRefCountWrite;
		ATMemoryLayer *mpMemLayer;
	};

	typedef vdhashmap<uint32, AccessBPLayer> AccessBPLayers;
	AccessBPLayers mAccessBPLayers;

	VDEvent<ATBreakpointManager, ATBreakpointEvent *> mEventBreakpointHit;

	enum {
		kAttribReadBkpt = 0x01,
		kAttribWriteBkpt = 0x02,
		kAttribRangeReadBkpt = 0x04,
		kAttribRangeWriteBkpt = 0x08
	};

	uint8 mAttrib[0x10000];
};

inline int ATBreakpointManager::TestPCBreakpoint(uint16 pc) {
	BreakpointsByAddress::const_iterator it(mCPUBreakpoints.find((uint32)pc));
	if (it == mCPUBreakpoints.end())
		return 0;

	return CheckPCBreakpoints(pc, it->second);
}

#endif	// f_AT_BKPTMANAGER_H
