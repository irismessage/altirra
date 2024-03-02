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

#include <stdafx.h>
#include <at/atcore/scheduler.h>
#include <at/atio/diskimage.h>
#include "disktrace.h"
#include "trace.h"

ATDiskRotationTracer::ATDiskRotationTracer() {
}

ATDiskRotationTracer::~ATDiskRotationTracer() {
}

void ATDiskRotationTracer::Init(ATScheduler& sched, uint64 baseTick, uint32 ticksPerRotation, ATTraceGroup& traceGroup) {
	mpScheduler = &sched;

	const uint64 t = sched.GetTick64();
	mLastFlushTick = t;
	mRotationBase = t;
	mTicksPerRotation = ticksPerRotation;

	mpTraceChannel = traceGroup.AddFormattedChannel(baseTick, sched.GetRate().AsInverseDouble(), L"Disk head");
}

void ATDiskRotationTracer::Shutdown() {
	Flush();

	mpDiskImage = nullptr;

	if (mpTraceChannel) {
		mpTraceChannel->TruncateLastEvent(mpScheduler->GetTick64());
		mpTraceChannel = nullptr;
	}
}

void ATDiskRotationTracer::SetDiskImage(IATDiskImage *image) {
	if (mpDiskImage != image) {
		InvalidateTrack();

		mpDiskImage = image;
	}
}

void ATDiskRotationTracer::SetTrack(int track) {
	if (mCurrentTrack != track) {
		InvalidateTrack();
		
		mpTraceChannel->TruncateLastEvent(mpScheduler->GetTick64());

		mCurrentTrack = track;
	}
}

void ATDiskRotationTracer::SetMotorRunning(bool running) {
	if (mbMotorRunning != running) {
		Flush();

		mbMotorRunning = running;
	}
}

void ATDiskRotationTracer::InvalidateTrack() {
	Flush();

	mbTrackCached = false;
}

void ATDiskRotationTracer::Flush() {
	const uint64 t = mpScheduler->GetTick64();

	if (t <= mLastFlushTick)
		return;

	uint64 tcur = mLastFlushTick;
	mLastFlushTick = t;

	if (!mbMotorRunning) {
		mRotationBase += (t - tcur);
		return;
	}

	if (!mbTrackCached)
		RescanTrack();

	if (mSectors.empty())
		return;

	// if we don't have a cached next sector index, recompute it
	if (mNextSectorIndex < 0) {
		// compute offset from index to start of time range
		uint64 tickOffset64 = tcur - mRotationBase;

		// check if we need to catch up on rotations -- this can happen if we had no sectors or were
		// seeking (since there are no valid sectors reported while on half tracks)
		if (tickOffset64 >= mTicksPerRotation) {
			uint64 newTickOffset64 = tickOffset64 % mTicksPerRotation;

			mRotationBase += tickOffset64 - newTickOffset64;

			tickOffset64 = newTickOffset64;
		}

		mNextSectorIndex = (int)(std::lower_bound(mSectors.begin(), mSectors.end(), (uint32)tickOffset64,
			[](const SectorEntry& se, uint32 offset) {
				return se.mTickOffset < offset;
			}
		) - mSectors.begin());

		if ((size_t)mNextSectorIndex < mSectors.size()) {
			VDASSERT(mSectors[mNextSectorIndex].mTickOffset + mRotationBase >= tcur);
		} else {
			VDASSERT(mSectors[0].mTickOffset + mRotationBase + mTicksPerRotation >= tcur);
		}
	}

	for(;;) {
		if (mNextSectorIndex >= (int)mSectors.size()) {
			mNextSectorIndex = 0;
			mRotationBase += mTicksPerRotation;
		}

		const SectorEntry& se = mSectors[mNextSectorIndex];
		const uint64 sectorTime = mRotationBase + se.mTickOffset;

		if (t < sectorTime)
			break;

		VDASSERT(sectorTime >= tcur);

		++mNextSectorIndex;

		mpTraceChannel->TruncateLastEvent(sectorTime);

		const uint32 color = mNextSectorIndex == 1 ? 0x4080FF : kATTraceColor_Default;

		if (se.mPSecIdx < 0)
			mpTraceChannel->AddOpenTickEventF(sectorTime, color, L"%u", se.mVSec + 1);
		else
			mpTraceChannel->AddOpenTickEventF(sectorTime, color, L"%u/%u", se.mVSec + 1, se.mPSecIdx + 1);
	}
}

void ATDiskRotationTracer::Warp(uint64 offset) {
	Flush();

	mRotationBase = mLastFlushTick - offset % mTicksPerRotation;
	mNextSectorIndex = -1;
}

void ATDiskRotationTracer::RescanTrack() {
	mSectors.clear();
	mbTrackCached = true;

	if (mCurrentTrack < 0 || !mpDiskImage)
		return;

	const ATDiskGeometryInfo& geo = mpDiskImage->GetGeometry();

	// if there is only one track (hard disk), don't report the track -- it'll be
	// ridiculous
	if (geo.mTrackCount <= 1)
		return;

	const uint32 spt = geo.mSectorsPerTrack;
	uint32 vsecStart = spt * (uint32)mCurrentTrack;
	uint32 vsecEnd = std::min<uint32>(vsecStart + spt, mpDiskImage->GetVirtualSectorCount());

	ATDiskVirtualSectorInfo vsi {};
	ATDiskPhysicalSectorInfo psi {};
	for(uint32 vsec = vsecStart; vsec < vsecEnd; ++vsec) {
		mpDiskImage->GetVirtualSectorInfo(vsec, vsi);

		for(uint32 psIdx = 0; psIdx < vsi.mNumPhysSectors; ++psIdx) {
			mpDiskImage->GetPhysicalSectorInfo(vsi.mStartPhysSector + psIdx, psi);

			SectorEntry& se = mSectors.emplace_back();
			se.mTickOffset = (uint32)VDRoundToInt32(psi.mRotPos * (double)mTicksPerRotation) % mTicksPerRotation;
			se.mVSec = vsec;
			se.mPSecIdx = vsi.mNumPhysSectors > 1 ? psIdx : -1;
		}
	}

	std::sort(mSectors.begin(), mSectors.end(),
		[](const SectorEntry& a, const SectorEntry& b) {
			return a.mTickOffset < b.mTickOffset;
		}
	);

	// remove any sectors that have the exact same offset, to handle pathological
	// cases
	mSectors.erase(
		std::unique(mSectors.begin(), mSectors.end(),
			[](const SectorEntry& a, const SectorEntry& b) {
				return a.mTickOffset == b.mTickOffset;
			}
		),
		mSectors.end()
	);

	mNextSectorIndex = -1;
}
