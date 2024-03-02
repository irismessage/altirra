//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008 Avery Lee
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

#ifndef AT_DISK_H
#define AT_DISK_H

#ifdef _MSC_VER
	#pragma once
#endif

#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>
#include "pokey.h"
#include "scheduler.h"

class ATPokeyEmulator;

class ATCPUEmulatorMemory;
class VDFile;

class IATDiskActivity {
public:
	virtual void OnDiskActivity(uint8 drive, bool active, uint32 sector) = 0;
};

class ATDiskEmulator : public IATPokeySIODevice, public IATSchedulerCallback {
public:
	ATDiskEmulator();
	~ATDiskEmulator();

	void Init(int unit, IATDiskActivity *act, ATScheduler *sched);

	bool IsEnabled() const { return mbEnabled; }
	bool IsAccurateSectorTimingEnabled() const { return mbAccurateSectorTiming; }

	void SetEnabled(bool enabled) { mbEnabled = enabled; }
	void SetAccurateSectorTimingEnabled(bool enabled) { mbAccurateSectorTiming = enabled; }
	void SetSectorBreakpoint(int sector) { mSectorBreakpoint = sector; }
	int GetSectorBreakpoint() const { return mSectorBreakpoint; }

	bool IsDirty() const;
	bool IsDiskBacked() const { return mbHasDiskSource; }
	const wchar_t *GetPath() const { return mPath.empty() ? NULL : mPath.c_str(); }

	bool IsWriteEnabled() const { return mbWriteEnabled; }
	bool IsAutoFlushEnabled() const { return mbAutoFlush; }
	void SetWriteFlushMode(bool writeEnabled, bool autoFlush);

	void Flush();
	void Reset();
	void LoadDisk(const wchar_t *s);
	void SaveDisk(const wchar_t *s);
	void CreateDisk(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize);
	void UnloadDisk();

	uint32 GetSectorCount() const;
	uint32 GetSectorSize(uint16 sector) const;
	uint32 GetSectorPhantomCount(uint16 sector) const;
	float GetSectorTiming(uint16 sector, int phantomIdx) const;
	uint8 ReadSector(uint16 bufadr, uint16 len, uint16 sector, ATCPUEmulatorMemory *mpMem);
	uint8 WriteSector(uint16 bufadr, uint16 len, uint16 sector, ATCPUEmulatorMemory *mpMem);
	void ReadStatus(uint8 dst[5]);
	void ReadPERCOMBlock(uint8 dst[13]);

	void SetForcedPhantomSector(uint16 sector, uint8 index, int order);
	int GetForcedPhantomSector(uint16 sector, uint8 index);

public:
	void OnScheduledEvent(uint32 id);

public:
	void PokeyAttachDevice(ATPokeyEmulator *pokey);
	void PokeyWriteSIO(uint8 c);
	void PokeyBeginCommand();
	void PokeyEndCommand();
	void PokeySerInReady();

protected:
	void LoadDiskDCM(VDFile& file, uint32 len, const wchar_t *s, const uint8 *header);
	void LoadDiskATX(VDFile& file, uint32 len, const wchar_t *s, const uint8 *header);
	void LoadDiskP2(VDFile& file, uint32 len, const wchar_t *s, const uint8 *header);
	void LoadDiskP3(VDFile& file, uint32 len, const wchar_t *s, const uint8 *header);
	void LoadDiskATR(VDFile& file, uint32 len, const wchar_t *s, const uint8 *header);

	void BeginTransfer(uint32 length, uint32 cyclesToFirstByte, bool useRotationalDelay);
	void UpdateRotationalCounter();
	void QueueAutoSave();
	void AutoSave();
	void SetAutoSaveError(bool error);
	void UpdateDisk();
	void ProcessCommandPacket();
	void ProcessCommandTransmitCompleted();
	void ProcessCommandData();

	ATPokeyEmulator	*mpPokey;
	IATDiskActivity *mpActivity;
	ATScheduler *mpScheduler;
	int		mUnit;
	VDStringW	mPath;

	ATEvent		*mpTransferEvent;
	ATEvent		*mpRotationalEvent;
	ATEvent		*mpOperationEvent;
	ATEvent		*mpAutoSaveEvent;
	ATEvent		*mpAutoSaveErrorEvent;

	uint32	mTransferOffset;
	uint32	mTransferLength;
	uint8	mFDCStatus;
	uint8	mActiveCommand;
	uint8	mActiveCommandState;
	uint32	mActiveCommandPhysSector;
	uint32	mPhantomSectorCounter;
	uint32	mRotationalCounter;
	uint32	mRotations;
	uint32	mRotationalPosition;
	uint32	mCurrentTrack;
	uint32	mSectorsPerTrack;

	sint32	mReWriteOffset;
	bool	mbWriteEnabled;
	bool	mbWriteRotationalDelay;
	bool	mbAutoFlush;
	bool	mbDirty;
	bool	mbHasDiskSource;
	bool	mbErrorIndicatorPhase;

	bool	mbWriteMode;
	bool	mbCommandMode;
	bool	mbEnabled;
	bool	mbBurstTransfer;
	bool	mbAccurateSectorTiming;
	bool	mbAccurateSectorPrediction;
	bool	mbLastOpError;

	int		mBootSectorCount;
	int		mTotalSectorCount;
	int		mSectorSize;
	int		mSectorBreakpoint;
	uint32	mLastSector;

	uint8	mSendPacket[528];
	uint8	mReceivePacket[528];

	vdfastvector<uint8>		mDiskImage;

	struct PhysSectorInfo {
		uint32	mOffset;
		uint16	mSize;
		bool	mbDirty;
		float	mRotPos;
		uint8	mFDCStatus;
		sint8	mForcedOrder;
		sint16	mWeakDataOffset;
	};

	struct SortDirtySectors;

	typedef vdfastvector<PhysSectorInfo> PhysSectors;
	PhysSectors mPhysSectorInfo;

	struct VirtSectorInfo {
		uint32	mStartPhysSector;
		uint32	mNumPhysSectors;
		uint32	mPhantomSectorCounter;
	};
	typedef vdfastvector<VirtSectorInfo> VirtSectors;
	VirtSectors mVirtSectorInfo;

	uint32	mWeakBitLFSR;
};

#endif
