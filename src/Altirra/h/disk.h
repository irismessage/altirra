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

class IATDiskActivity {
public:
	virtual void OnDiskActivity(uint8 drive, bool active) = 0;
};

class ATDiskEmulator : public IATPokeySIODevice, public IATSchedulerCallback {
public:
	ATDiskEmulator();
	~ATDiskEmulator();

	void Init(int unit, IATDiskActivity *act, ATScheduler *sched);

	bool IsEnabled() const { return mbEnabled; }
	bool IsBurstIOEnabled() const { return mbBurstTransfer; }
	bool IsAccurateSectorTimingEnabled() const { return mbAccurateSectorTiming; }

	void SetEnabled(bool enabled) { mbEnabled = enabled; }
	void SetBurstIOEnabled(bool burst);
	void SetAccurateSectorTimingEnabled(bool enabled) { mbAccurateSectorTiming = enabled; }
	void SetSectorBreakpoint(int sector) { mSectorBreakpoint = sector; }
	int GetSectorBreakpoint() const { return mSectorBreakpoint; }

	bool IsDirty() const;
	const wchar_t *GetPath() const { return mPath.empty() ? NULL : mPath.c_str(); }

	bool IsWriteEnabled() const { return mbWriteEnabled; }
	bool IsAutoFlushEnabled() const { return mbAutoFlush; }
	void SetWriteFlushMode(bool writeEnabled, bool autoFlush);

	void Reset();
	void LoadDisk(const wchar_t *s);
	void SaveDisk(const wchar_t *s);
	void CreateDisk(uint32 sectorCount, uint32 bootSectorCount, bool dd);
	void UnloadDisk();

	uint8 ReadSector(uint16 bufadr, uint16 len, uint16 sector, ATCPUEmulatorMemory *mpMem);
	uint8 WriteSector(uint16 bufadr, uint16 len, uint16 sector, ATCPUEmulatorMemory *mpMem);

public:
	void OnScheduledEvent(uint32 id);

public:
	void PokeyAttachDevice(ATPokeyEmulator *pokey);
	void PokeyWriteSIO(uint8 c);
	void PokeyBeginCommand();
	void PokeyEndCommand();
	void PokeySerInReady();

protected:
	void BeginTransfer(uint32 length, uint32 cyclesToFirstByte);
	void UpdateRotationalCounter();
	void QueueAutoSave();
	void SetAutoSaveError(bool error);
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

	bool	mbWriteEnabled;
	bool	mbAutoFlush;
	bool	mbDirty;
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

	uint8	mSendPacket[528];
	uint8	mReceivePacket[528];

	vdfastvector<uint8>		mDiskImage;

	struct PhysSectorInfo {
		uint32	mOffset;
		uint32	mSize;
		float	mRotPos;
		uint8	mFDCStatus;
	};
	vdfastvector<PhysSectorInfo>	mPhysSectorInfo;

	struct VirtSectorInfo {
		uint32	mStartPhysSector;
		uint32	mNumPhysSectors;
		uint32	mPhantomSectorCounter;
	};
	typedef vdfastvector<VirtSectorInfo> VirtSectors;
	VirtSectors mVirtSectorInfo;
};

#endif
