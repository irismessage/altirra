//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2023 Avery Lee
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

#ifndef f_AT_DISKTRACE_H
#define f_AT_DISKTRACE_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>

class ATTraceChannelFormatted;
class ATTraceGroup;
class IATDiskImage;

class ATDiskRotationTracer {
	ATDiskRotationTracer(const ATDiskRotationTracer&) = delete;
	ATDiskRotationTracer& operator=(const ATDiskRotationTracer&) = delete;
public:
	ATDiskRotationTracer();
	~ATDiskRotationTracer();

	void Init(ATScheduler& sched, uint64 baseTick, uint32 ticksPerRotation, ATTraceGroup& traceGroup);
	void Shutdown();
	void SetDiskImage(IATDiskImage *image);
	void SetTrack(int track);
	void SetMotorRunning(bool running);
	void InvalidateTrack();
	void Flush();
	void Warp(uint64 offset);

private:
	void RescanTrack();

	ATScheduler *mpScheduler = nullptr;
	ATTraceChannelFormatted *mpTraceChannel = nullptr;
	uint64 mLastFlushTick = 0;
	uint64 mRotationBase = 0;
	uint32 mTicksPerRotation = 1;
	int mCurrentTrack = -1;
	int mNextSectorIndex = -1;
	bool mbTrackCached = false;
	bool mbMotorRunning = false;

	struct SectorEntry {
		uint32 mTickOffset = 0;
		uint16 mVSec = 0;
		sint16 mPSecIdx = 0;
	};

	vdfastvector<SectorEntry> mSectors;
	vdrefptr<IATDiskImage> mpDiskImage;
};

#endif
