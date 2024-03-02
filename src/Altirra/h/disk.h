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
#include <at/atcore/scheduler.h>
#include <at/atio/diskimage.h>
#include "pokey.h"

class ATPokeyEmulator;
class ATAudioSyncMixer;

class ATCPUEmulatorMemory;
class VDFile;
class IVDRandomAccessStream;

class IATDiskActivity {
public:
	virtual void OnDiskActivity(uint8 drive, bool active, uint32 sector) = 0;
	virtual void OnDiskMotorChange(uint8 drive, bool active) = 0;
};

enum ATDiskEmulationMode {
	kATDiskEmulationMode_Generic,
	kATDiskEmulationMode_FastestPossible,
	kATDiskEmulationMode_810,
	kATDiskEmulationMode_1050,
	kATDiskEmulationMode_XF551,
	kATDiskEmulationMode_USDoubler,
	kATDiskEmulationMode_Speedy1050,
	kATDiskEmulationMode_IndusGT,
	kATDiskEmulationMode_Happy,
	kATDiskEmulationMode_1050Turbo,
	kATDiskEmulationMode_Generic57600,
	kATDiskEmulationModeCount
};

class ATDiskEmulator : public IATPokeySIODevice, public IATSchedulerCallback {
public:
	ATDiskEmulator();
	~ATDiskEmulator();

	void Init(int unit, IATDiskActivity *act, ATScheduler *sched, ATScheduler *slowsched, ATAudioSyncMixer *mixer);

	void Rename(int unit);

	bool IsEnabled() const { return mbEnabled; }
	bool IsAccurateSectorTimingEnabled() const { return mbAccurateSectorTiming; }
	bool AreDriveSoundsEnabled() const { return mbDriveSoundsEnabled; }
	bool GetBurstTransfersEnabled() const { return mbBurstTransfersEnabled; }

	void SetEnabled(bool enabled) { mbEnabled = enabled; }
	void SetAccurateSectorTimingEnabled(bool enabled) { mbAccurateSectorTiming = enabled; }
	void SetDriveSoundsEnabled(bool enabled);
	void SetBurstTransfersEnabled(bool enabled) { mbBurstTransfersEnabled = enabled; }

	void SetSectorBreakpoint(int sector) { mSectorBreakpoint = sector; }
	int GetSectorBreakpoint() const { return mSectorBreakpoint; }

	bool IsDirty() const;
	bool IsDiskLoaded() const { return mTotalSectorCount > 0; }
	bool IsDiskBacked() const { return mbHasDiskSource; }
	const wchar_t *GetPath() const { return mPath.empty() ? NULL : mPath.c_str(); }
	IATDiskImage *GetDiskImage() const { return mpDiskImage; }

	bool IsWriteEnabled() const { return mbWriteEnabled; }
	bool IsAutoFlushEnabled() const { return mbAutoFlush; }
	void SetWriteFlushMode(bool writeEnabled, bool autoFlush);

	void ClearAccessedFlag();
	bool IsAccessed() const { return mbAccessed; }

	void SetEmulationMode(ATDiskEmulationMode mode);
	ATDiskEmulationMode GetEmulationMode() { return mEmuMode; }

	void Flush();
	void Reset();
	void MountFolder(const wchar_t *path, bool sdfs);
	void LoadDisk(const wchar_t *s);
	void LoadDisk(const wchar_t *origPath, const wchar_t *imagePath, IVDRandomAccessStream& stream);
	void SaveDisk(const wchar_t *s, ATDiskImageFormat format);
	void CreateDisk(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize);
	void FormatDisk(uint32 sectorCount, uint32 bootSectorCount, uint32 sectorSize);
	void UnloadDisk();

	VDStringW GetMountedImageLabel() const;

	uint32 GetSectorCount() const;
	uint32 GetSectorSize(uint16 sector) const;
	uint32 GetSectorPhantomCount(uint16 sector) const;

	struct SectorInfo {
		float mRotPos;
		uint8 mFDCStatus;
	};

	bool GetSectorInfo(uint16 sector, int phantomIdx, SectorInfo& info) const;
	uint8 ReadSector(uint16 bufadr, uint16 len, uint16 sector, ATCPUEmulatorMemory *mpMem);
	uint8 WriteSector(uint16 bufadr, uint16 len, uint16 sector, ATCPUEmulatorMemory *mpMem);
	void ReadStatus(uint8 dst[5]);
	void ReadPERCOMBlock(uint8 dst[13]);

	void SetForcedPhantomSector(uint16 sector, uint8 index, int order);
	int GetForcedPhantomSector(uint16 sector, uint8 index);

public:
	void OnScheduledEvent(uint32 id);

public:
	void PokeyAttachDevice(ATPokeyEmulator *pokey) override;
	bool PokeyWriteSIO(uint8 c, bool command, uint32 cyclesPerBit) override;
	void PokeyBeginCommand() override;
	void PokeyEndCommand() override;
	void PokeySerInReady() override;

protected:
	void InitSectorInfoArrays();
	void BeginTransferACKCmd();
	void BeginTransferACK();
	void BeginTransferComplete();
	void BeginTransferError();
	void BeginTransferNAK();
	void BeginTransfer(uint32 length, uint32 cyclesToFirstByte, uint32 cyclesToSecondByte, bool useHighSpeedFirstByte, bool useHighSpeed, bool enhancedDensity);
	void UpdateRotationalCounter();
	void QueueAutoSave();
	void AutoSave();
	void SetAutoSaveError(bool error);
	void UpdateDisk();
	void ProcessCommandPacket();
	void ProcessCommandTransmitCompleted();
	void ProcessCommandData();
	void ComputeGeometry();
	void ComputePERCOMBlock();
	void ComputeSupportedProfile();
	bool SetPERCOMData(const uint8 *data);
	bool TurnOnMotor(uint32 delay = 0);
	void PlaySeekSound(uint32 initialDelay, uint32 trackCount);

	ATPokeyEmulator	*mpPokey;
	IATDiskActivity *mpActivity;
	ATScheduler *mpScheduler;
	ATScheduler *mpSlowScheduler;
	ATAudioSyncMixer *mpAudioSyncMixer;
	int		mUnit;
	VDStringW	mPath;

	ATEvent		*mpTransferEvent;
	ATEvent		*mpOperationEvent;
	ATEvent		*mpAutoSaveEvent;
	ATEvent		*mpAutoSaveErrorEvent;
	ATEvent		*mpMotorOffEvent;

	uint32	mLastRotationUpdateCycle;
	uint32	mTransferOffset;
	uint32	mTransferLength;
	uint32	mTransferRate;
	uint32	mTransferSecondByteDelay;
	uint32	mTransferCyclesPerBit;
	uint32	mTransferCyclesPerBitFirstByte;
	uint32	mTransferCompleteRotPos;
	bool	mbTransferAdjustRotation;
	uint8	mFDCStatus;
	uint8	mActiveCommand;
	bool	mbActiveCommandHighSpeed;
	uint8	mActiveCommandState;
	uint32	mActiveCommandPhysSector;
	uint32	mPhantomSectorCounter;
	uint32	mRotationalCounter;
	uint32	mRotations;
	uint32	mRotationalPosition;
	uint32	mCurrentTrack;
	uint32	mSectorsPerTrack;
	uint32	mTrackCount;
	uint32	mSideCount;
	bool	mbMFM;

	bool	mbWriteEnabled;
	bool	mbWriteHighSpeedFirstByte;
	bool	mbWriteHighSpeed;
	bool	mbAutoFlush;
	bool	mbHasDiskSource;
	bool	mbErrorIndicatorPhase;
	bool	mbAccessed;

	bool	mbWriteMode;
	bool	mbCommandMode;
	bool	mbCommandValid;
	bool	mbCommandFrameHighSpeed;
	bool	mbEnabled;
	bool	mbBurstTransfersEnabled;
	bool	mbDriveSoundsEnabled;
	bool	mbAccurateSectorTiming;
	bool	mbAccurateSectorPrediction;
	bool	mbLastOpError;

	int		mBootSectorCount;
	int		mTotalSectorCount;
	int		mSectorSize;
	int		mSectorBreakpoint;
	uint32	mLastSector;

	uint32	mRotationSoundId;

	uint8	mPERCOM[12];
	int		mFormatSectorSize;
	int		mFormatSectorCount;
	int		mFormatBootSectorCount;

	ATDiskEmulationMode mEmuMode;
	bool	mbSupportedCmdHighSpeed;
	bool	mbSupportedCmdFrameHighSpeed;
	bool	mbSupportedCmdPERCOM;
	bool	mbSupportedCmdFormatSkewed;
	bool	mbSupportedCmdGetHighSpeedIndex;
	uint8	mHighSpeedIndex;
	uint8	mHighSpeedCmdFrameRateLo;
	uint8	mHighSpeedCmdFrameRateHi;
	uint8	mHighSpeedDataFrameRateLo;
	uint8	mHighSpeedDataFrameRateHi;
	uint32	mCyclesPerSIOByte;
	uint32	mCyclesPerSIOBit;
	uint32	mCyclesPerSIOByteHighSpeed;
	uint32	mCyclesPerSIOBitHighSpeed;
	uint32	mCyclesToACKSent;
	uint32	mCyclesToFDCCommand;
	uint32	mCyclesToCompleteAccurate;
	uint32	mCyclesToCompleteAccurateED;
	uint32	mCyclesToCompleteFast;
	uint32	mCyclesPerDiskRotation;
	uint32	mCyclesPerTrackStep;
	uint32	mCyclesForHeadSettle;
	bool	mbSeekHalfTracks;
	bool	mbRetryMode1050;
	bool	mbReverseOnForwardSeeks;

	uint8	mSendPacket[8192 + 16];
	uint8	mReceivePacket[8192 + 16];

	vdautoptr<IATDiskImage> mpDiskImage;

	struct ExtPhysSector {
		sint8	mForcedOrder;
	};

	typedef vdfastvector<ExtPhysSector> ExtPhysSectors;
	ExtPhysSectors mExtPhysSectors;

	struct ExtVirtSector {
		uint32	mPhantomSectorCounter;
	};
	typedef vdfastvector<ExtVirtSector> ExtVirtSectors;
	ExtVirtSectors mExtVirtSectors;

	uint32	mWeakBitLFSR;
};

#endif
