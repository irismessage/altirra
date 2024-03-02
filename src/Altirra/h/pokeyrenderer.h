//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2011 Avery Lee
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

#ifndef f_AT_POKEYRENDERER_H
#define f_AT_POKEYRENDERER_H

#include <vd2/system/vdstl.h>

class ATScheduler;
struct ATPokeyTables;
struct ATPokeyAudioState;

class ATPokeyRenderer {
	ATPokeyRenderer(const ATPokeyRenderer&);
	ATPokeyRenderer& operator=(const ATPokeyRenderer&);
public:
	ATPokeyRenderer();
	~ATPokeyRenderer();

	void Init(ATScheduler *sch, ATPokeyTables *tables);
	void ColdReset();

	void GetAudioState(ATPokeyAudioState& state);
	bool GetChannelOutput(int index) const { return mOutputs[index] != 0; }
	const float *GetOutputBuffer() const { return mRawOutputBuffer; }

	bool IsChannelEnabled(int channel) const { return mbChannelEnabled[channel]; }
	void SetChannelEnabled(int channel, bool enable);

	void SetInitMode(bool init);
	bool SetSpeaker(bool state);
	void SetAudioLine2(int v);

	void ResetTimers();
	void SetAUDCx(int index, uint8 value);
	void SetAUDCTL(uint8 value);

	void AddChannelEvent(int channel);
	void SetChannelDeferredEvents(int channel, uint32 start, uint32 period);
	void SetChannelDeferredEventsLinked(int channel, uint32 loStart, uint32 loPeriod, uint32 hiStart, uint32 hiPeriod, uint32 loOffset);
	void ClearChannelDeferredEvents(int channel, uint32 t);

	uint32 EndBlock();

	void LoadState(ATSaveStateReader& reader);
	void ResetState();
	void SaveState(ATSaveStateWriter& writer);

protected:
	struct Edge;

	void FlushDeferredEvents(int channel, uint32 t);
	void Flush(const uint32 t);
	void MergeEvents(const Edge *src1, const Edge *src2, Edge *dst);

	typedef void (ATPokeyRenderer::*FireTimerRoutine)(uint32 t);
	template<int activeChannel>
	FireTimerRoutine GetFireTimerRoutine() const;

	template<int activeChannel, uint8 audcn, bool outputAffectsSignal, bool highPassEnabled>
	void FireTimer(uint32 t);

	void UpdateVolume(int channel);
	void UpdateOutput();
	void UpdateOutput(uint32 t);
	void GenerateSamples(uint32 t);
	void UpdatePoly17Counter(uint32 t);
	void UpdatePoly9Counter(uint32 t);
	void UpdatePoly5Counter(uint32 t);
	void UpdatePoly4Counter(uint32 t);

	ATScheduler *mpScheduler;
	ATPokeyTables *mpTables;
	uintptr mPoly4Offset;
	uintptr mPoly5Offset;
	uintptr mPoly9Offset;
	uintptr mPoly17Offset;
#if !defined(VD_CPU_X86)
	uint32	mPolyBaseTime;
#endif
	bool mbInitMode;

	float	mAccum;
	float	mOutputLevel;
	uint32	mLastOutputTime;
	uint32	mLastOutputSampleTime;
	int		mAudioInput2;
	int		mExternalInput;

	bool	mbSpeakerState;

	int		mOutputs[4];
	int		mChannelVolume[4];
	uint8	mNoiseFF[4];
	uint8	mHighPassFF[2];

	// AUDCx broken out fields
	uint8	mAUDCTL;
	uint8	mAUDC[4];

	bool	mbChannelEnabled[4];

	struct DeferredEvent {
		bool	mbEnabled;

		/// Set if 16-bit linked mode is enabled; this requires tracking the
		/// high timer to know when to reset the low timer.
		bool	mbLinked;

		/// Timestamp of next lo event.
		uint32	mNextTime;

		/// Period of lo event in clocks.
		uint32	mPeriod;

		/// Timestamp of next hi event.
		uint32	mNextHiTime;

		/// Hi (16-bit) period in clocks.
		uint32	mHiPeriod;

		/// Offset from hi event to next lo event.
		uint32	mHiLoOffset;
	};

	DeferredEvent mDeferredEvents[4];

	uint32	mLastPoly17Time;
	uint32	mPoly17Counter;
	uint32	mLastPoly9Time;
	uint32	mPoly9Counter;
	uint32	mLastPoly5Time;
	uint32	mPoly5Counter;
	uint32	mLastPoly4Time;
	uint32	mPoly4Counter;

	uint32	mOutputSampleCount;

	struct Edge {
		uint32 t;
		int channel;
	};

	typedef vdfastvector<Edge> SortedEdges;
	SortedEdges mSortedEdgesTemp[4];
	SortedEdges mSortedEdgesTemp2[2];
	SortedEdges mSortedEdges;

	typedef vdfastvector<uint32> ChannelEdges;
	ChannelEdges mChannelEdges[4];

	enum {
		// 1271 samples is the max (35568 cycles/frame / 28 cycles/sample + 1). We add a little bit here
		// to round it out. We need a 16 sample holdover in order to run the FIR filter.
		kBufferSize = 1536
	};

	float	mRawOutputBuffer[kBufferSize];
};

#endif	// f_AT_POKEYRENDERER_H
