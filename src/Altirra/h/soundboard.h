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

#ifndef f_AT_SOUNDBOARD_H
#define f_AT_SOUNDBOARD_H

#include <at/atcore/audiomixer.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceu1mb.h>
#include <at/atcore/scheduler.h>

class ATScheduler;
class ATMemoryManager;
class ATMemoryLayer;
class ATIRQController;
class IATAudioOutput;

class ATSoundBoardEmulator final : public VDAlignedObject<16>
						, public ATDevice
						, public IATDeviceAudioOutput
						, public IATDeviceMemMap
						, public IATDeviceScheduling
						, public IATDeviceU1MBControllable
						, public IATSyncAudioSource
						, public IATSchedulerCallback
{
	ATSoundBoardEmulator(const ATSoundBoardEmulator&) = delete;
	ATSoundBoardEmulator& operator=(const ATSoundBoardEmulator&) = delete;

	struct Channel;
public:
	ATSoundBoardEmulator();
	~ATSoundBoardEmulator();

	void *AsInterface(uint32 id) override;

	void SetMemBase(uint32 membase);

public:
	void InitAudioOutput(IATAudioMixer *mixer) override;

public:
	void InitMemMap(ATMemoryManager *memmap) override;
	bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const;

public:
	void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettingsBlurb(VDStringW& buf) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Init() override;
	void Shutdown() override;

	void ColdReset() override;
	void WarmReset() override;

	void SetU1MBControl(ATU1MBControl control, sint32 value) override;

	uint8 DebugReadControlV1(uint8 addr) const;
	uint8 ReadControlV1(uint8 addr);
	void WriteControlV1(uint8 addr, uint8 value);

	uint8 DebugReadControlV2(uint8 addr) const;
	uint8 ReadControlV2(uint8 addr);
	void WriteControlV2(uint8 addr, uint8 value);

public:
	bool RequiresStereoMixingNow() const override { return true; }
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

public:
	void OnScheduledEvent(uint32 id) override;

private:
	void Flush();

	template<bool T_V2>
	void Run(uint64 targetTime);

	template<bool T_V2, bool T_OneShot>
	void RunChannel(Channel *VDRESTRICT ch, uint32 baseTime, uint32 cyclesPerSample, uint32 samplesToGenerate);

	void UpdateAllIrqDeadlines();
	bool UpdateChannelIrqDeadline(const Channel& ch, uint32 idx, bool resetIrq);
	void UpdateIrqAndDeadline();
	void UpdateIrq();
	uint64 ComputeNearestIrq() const;

	void AssertIrq();
	void NegateIrq();

	void UpdateControlLayer();
	void ApplyCoreLimitations();

	sint32 HandleDebugReadControlV1(uint32 addr) const;
	sint32 HandleReadControlV1(uint32 addr);
	bool HandleWriteControlV1(uint32 addr, uint8 value);

	sint32 HandleDebugReadControlV2(uint32 addr) const;
	sint32 HandleReadControlV2(uint32 addr);
	bool HandleWriteControlV2(uint32 addr, uint8 value);

	struct Channel {
		uint64	mPhase;
		uint32	mAddress;
		uint32	mLength;
		uint32	mRepeat;
		uint32	mFreq;
		uint8	mVolume;
		uint8	mPan;
		uint8	mAttack;
		uint8	mDecay;
		uint8	mSustain;
		uint8	mRelease;
		uint8	mControl;
		bool	mbInRepeat;
		uint32	mRepeatLen;
		uint32	mOffset;

		float	mLastLeft;
		float	mLastRight;
	};

	uint8	*mpMemory = nullptr;
	ATMemoryLayer *mpMemLayerControl = nullptr;
	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpChannelIrq = nullptr;
	ATIRQController *mpIrqController = nullptr;
	ATMemoryManager *mpMemMan = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;

	enum class CoreVersion : uint8 {
		Version11,
		Version12,
		Version20
	} mCoreVersion = CoreVersion::Version12;

	uint32	mMemBase = 0xD2C0;
	sint32	mMemBaseOverride = -1;
	uint32	mMemRange = 0x0040;
	uint32	mLoadAddress = 0;
	Channel	*mpCurChan = nullptr;
	uint8	mCurChanIndex = 0;
	uint8	mChannelCount = 1;
	uint8	mMainControl = 0;

	uint64	mLastUpdate = 0;
	uint64	mGlobalIrqDeadline = 0;
	uint32	mIrqBit = 0;

	uint8	mMultiplierMode = 0;
	uint8	mMultiplierArg1[2] {};
	uint8	mMultiplierArg2[2] {};
	uint8	mMultiplierResult[4] {};

	Channel mChannels[32] {};

	// It can take 2^33 cycles for a channel IRQ to fire, so these need to be 64-bit.
	uint64	mChannelIrqDeadlines[32] {};

	vdrefptr<ATSyncAudioEdgeBuffer> mpLeftEdgeBuffer;
	vdrefptr<ATSyncAudioEdgeBuffer> mpRightEdgeBuffer;
};

#endif	// f_AT_SOUNDBOARD_H
