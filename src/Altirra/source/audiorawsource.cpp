//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2012 Avery Lee
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
#include <vd2/system/binary.h>
#include <at/atcore/wraptime.h>
#include <at/ataudio/audiooutput.h>
#include "audiorawsource.h"

ATAudioRawSource::ATAudioRawSource() {
}

ATAudioRawSource::~ATAudioRawSource() {
	Shutdown();
}

void ATAudioRawSource::Init(IATAudioMixer *mixer) {
	mpAudioMixer = mixer;

	mixer->AddSyncAudioSource(this);
}

void ATAudioRawSource::Shutdown() {
	if (mpAudioMixer) {
		mpAudioMixer->RemoveSyncAudioSource(this);
		mpAudioMixer = nullptr;
	}
}

void ATAudioRawSource::SetOutput(uint32 t, float level) {
	float diff = level - mLevel;

	if (diff == 0.0f)
		return;

	mLevel = level;

	if (mLevelEdges.size() > mNextEdgeIndex && ATWrapTime{mLevelEdges.back().mTime} >= t)
		mLevelEdges.back().mDeltaValue += diff;
	else
		mLevelEdges.push_back({ t, diff });
}

bool ATAudioRawSource::RequiresStereoMixingNow() const {
	return false;
}

void ATAudioRawSource::WriteAudio(const ATSyncAudioMixInfo& mixInfo) {
	const uint32 baseTime = mixInfo.mStartTime;
	const uint32 timeSpan = mixInfo.mCount * 28;
	const float volume = mixInfo.mpMixLevels[kATAudioMix_Other];

	// change all samples before the start time to coincide on the start, so they
	// get accumulated
	const uint32 endIndex = (uint32)mLevelEdges.size();

	while(mNextEdgeIndex < endIndex && ATWrapTime{mLevelEdges[mNextEdgeIndex].mTime} <= baseTime)
		mLevelEdges[mNextEdgeIndex++].mTime = baseTime;

	// find appropriate end point for mixing
	auto itEnd = std::upper_bound(mLevelEdges.begin() + mNextEdgeIndex, mLevelEdges.end(), ATSyncAudioEdge{ baseTime + timeSpan - 1, 0 },
		[](const ATSyncAudioEdge& edge1, const ATSyncAudioEdge& edge2) { return ATWrapTime{edge1.mTime} < edge2.mTime; });

	const uint32 endMixIndex = (uint32)(itEnd - mLevelEdges.begin());

	mpAudioMixer->GetEdgePlayer().AddEdges(mLevelEdges.data() + mNextEdgeIndex, endMixIndex - mNextEdgeIndex, volume);

	mNextEdgeIndex = endMixIndex;

	// if occupancy is below one-quarter, shift the array
	if (mLevelEdges.size() < mNextEdgeIndex * 4) {
		mLevelEdges.erase(mLevelEdges.begin(), mLevelEdges.begin() + mNextEdgeIndex);
		mNextEdgeIndex = 0;
	}
}
