//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2019 Avery Lee
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

#ifndef f_AT_POKEYRENDERER_H
#define f_AT_POKEYRENDERER_H

#include <vd2/system/vdstl.h>
#include <at/atcore/audiomixer.h>

class ATScheduler;
struct ATPokeyTables;
struct ATPokeyAudioState;
class IATSyncAudioEdgePlayer;
class IATSyncAudioSamplePlayer;
struct ATPokeyAudioLog;
struct ATSaveStatePokeyRenderer;

class ATPokeyRenderer {
	ATPokeyRenderer(const ATPokeyRenderer&) = delete;
	ATPokeyRenderer& operator=(const ATPokeyRenderer&) = delete;
public:
	ATPokeyRenderer();
	~ATPokeyRenderer();

	void Init(ATScheduler *sch, ATPokeyTables *tables, IATSyncAudioSamplePlayer *edgeSamplePlayer);
	void ColdReset();

	void SyncTo(const ATPokeyRenderer& src);

	bool GetChannelOutput(int index) const { return (mChannelOutputMask & (1 << index)) != 0; }
	const float *GetOutputBuffer() const { return mRawOutputBuffer; }

	bool IsChannelEnabled(int channel) const { return mbChannelEnabled[channel]; }
	void SetChannelEnabled(int channel, bool enable);

	void SetAudioLog(ATPokeyAudioLog *log);
	void RestartAudioLog(bool initial = false);

	void SetFiltersEnabled(bool enable);

	void SetSpeakerFilterEnabled(bool enable);

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

	void AddSerialNoisePulse(uint32 t);

	void StartBlock();

	struct EndBlockInfo {
		uint32 mTimestamp;
		uint32 mSamples;
	};

	EndBlockInfo EndBlock(IATSyncAudioEdgePlayer *edgePlayer);

	void LoadState(const ATSaveStatePokeyRenderer& state);
	ATSaveStatePokeyRenderer SaveState() const;

protected:
	enum class ChangeType : uint8 {
		Audc0,
		Audc1,
		Audc2,
		Audc3,
		Audctl,
		Init,
		ResetOutputs,
		Flush
	};

	void QueueChangeEvent(ChangeType type, uint8 value);
	void ProcessChangeEvents(uint32 t);

	void FlushDeferredEvents(int channel, uint32 t);
	void Flush(const uint32 t);
	void Flush2(const uint32 t);
	static void MergeOutputEvents(const uint32 *VDRESTRICT src1, const uint32 *VDRESTRICT src2, uint32 *VDRESTRICT dst);

	typedef std::pair<uint32 *, const uint32 *> (ATPokeyRenderer::*FireTimerRoutine)(uint32 *VDRESTRICT dst, const uint32 *VDRESTRICT src, uint32 timeBase, uint32 timeLimit);
	FireTimerRoutine GetFireTimerRoutine(int ch) const;
	template<int activeChannel>
	FireTimerRoutine GetFireTimerRoutine() const;

	template<int activeChannel, uint8 audcn, bool outputAffectsSignal, bool T_UsePoly9>
	std::pair<uint32 *, const uint32 *> FireTimer(uint32 *VDRESTRICT dst, const uint32 *VDRESTRICT src, uint32 timeBase, uint32 timeLimit);

	void ProcessOutputEdges(uint32 timeBase, const uint32 *edges, uint32 n);
	void UpdateVolume(int channel);
	void UpdateOutput(uint32 t);
	void UpdateOutput2(uint32 t2);
	void UpdateOutput2(uint32 t2, uint32 vpok);
	void GenerateSamples(uint32 t2);

	void PostFilter();

	void LogOutputChange(uint32 t2) const;
	void LogOutputEdges(uint32 timeBase2, const uint32 *edges, uint32 n) const;

	ATScheduler *mpScheduler;
	ATPokeyTables *mpTables;
	bool mbInitMode;

	float	mHighPassAccum;
	float	mOutputLevel;
	uint32	mLastFlushTime;
	int		mExternalInput;

	bool	mbSpeakerState;
	bool	mbSpeakerFilterEnabled = false;

	// Noise/tone flip-flop state for all four channels. This is the version updated by the
	// FireTimer() routines; change events are then produced to update the analogous bits 0-3
	// in the channel output mask.
	uint8	mNoiseFlipFlops = 0;

	// Noise/tone and high-pass flip-flop states. Bits 0-3 contain the noise flip-flop states,
	// updated by the output code from change events generated from mNoiseFlipFlops; bits 4-5
	// contain the high-pass flip-flops for ch1-2.
	uint8	mChannelOutputMask = 0;

	// Bits 0-3 set if ch1-4 is in volume-only mode (AUDCx bit 4 = 1).
	uint8	mVolumeOnlyMask = 0;

	// Bits 0-3 set if AUDCx bit 0-3 > 0. Note that this includes muting, so we cannot use
	// this for architectural state.
	uint8	mNonZeroVolumeMask = 0;

	uint8	mChannelVolume[4] {};
	uint32	mChannelVolMixIndex[4] {};

	struct BufferedState {
		uint8	mAUDC[4];
		uint8	mAUDCTL;
	};

	BufferedState mArchState {};
	BufferedState mRenderState {};

	// True if the channel is enabled for update or muted. This does NOT affect architectural
	// state; it must not affect whether flip-flops are updated.
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

	DeferredEvent mDeferredEvents[4] {};

	struct ChangeEvent {
		uint32 mTime;
		ChangeType mType;
		uint8 mValue;
	};

	vdfastdeque<ChangeEvent> mChangeQueue;

	struct PolyState {
		uint32	mInitMask = 0;

		uintptr mPoly4Offset = 0;
		uintptr mPoly5Offset = 0;
		uintptr mPoly9Offset = 0;
		uintptr mPoly17Offset = 0;

		uint32	mLastPoly17Time = 0;
		uint32	mPoly17Counter = 0;
		uint32	mLastPoly9Time = 0;
		uint32	mPoly9Counter = 0;
		uint32	mLastPoly5Time = 0;
		uint32	mPoly5Counter = 0;
		uint32	mLastPoly4Time = 0;
		uint32	mPoly4Counter = 0;

		void UpdatePoly17Counter(uint32 t);
		void UpdatePoly9Counter(uint32 t);
		void UpdatePoly5Counter(uint32 t);
		void UpdatePoly4Counter(uint32 t);
	} mPolyState;

	uint32	mBlockStartTime = 0;
	uint32	mBlockStartTime2 = 0;
	uint32	mOutputSampleCount = 0;
		
	ATPokeyAudioLog	*mpAudioLog = nullptr;

	// The sorted edge lists hold ordered output change events. The events are stored as
	// packed bitfields for fast merging:
	//
	//	bits 14-31 (18): half-cycle offset from beginning of flush operation
	//	bits  8-13 (6): AND mask to apply to flip/flops
	//	bits  0-5 (6): OR mask to apply to flip/flops
	//
	// Bits 4-5 of the masks are special as they apply to the high-pass flip/flops. The OR
	// mask for these bits is ANDed with the ch1/2 outputs, so they update the HP F/Fs instead
	// of setting them.
	typedef vdfastvector<uint32> SortedEdges;
	SortedEdges mSortedEdgesTemp[4];
	SortedEdges mSortedEdgesHpTemp1;
	SortedEdges mSortedEdgesHpTemp2;
	SortedEdges mSortedEdgesTemp2[2];
	SortedEdges mSortedEdges;

	// The channel edge lists hold an ordered list of timer underflow events. The ticks are
	// in system time (from ATScheduler).
	typedef vdfastvector<uint32> ChannelEdges;
	ChannelEdges mChannelEdges[4];
	uint32 mChannelEdgeBases[4] {};

	vdfastvector<uint32> mSerialPulseTimes;
	float mSerialPulse = 0;

	vdrefptr<ATSyncAudioEdgeBuffer> mpEdgeBuffer;
	IATSyncAudioSamplePlayer *mpEdgeSamplePlayer = nullptr;
	IATSyncAudioConvolutionPlayer *mpEdgeConvPlayer = nullptr;
	IATAudioSoundGroup *mpSoundGroup = nullptr;

	enum : uint32 {
		// 1271 samples is the max (35568 cycles/frame / 28 cycles/sample + 1). We add a little bit here
		// to round it out. We need a 16 sample holdover in order to run the FIR filter.
		kBufferSize = 1536,

		kMaxWriteIndex = kBufferSize - 16
	};

	alignas(16) float mRawOutputBuffer[kBufferSize];

	template<int activeChannel, bool T_UsePoly9>
	static const FireTimerRoutine kFireRoutines[2][16];
};

#endif	// f_AT_POKEYRENDERER_H
