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

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atcore/propertyset.h>
#include "soundboard.h"
#include "irqcontroller.h"
#include "memorymanager.h"

void ATCreateDeviceSoundBoard(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATSoundBoardEmulator> p(new ATSoundBoardEmulator);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefSoundBoard = { "soundboard", "soundboard", L"SoundBoard", ATCreateDeviceSoundBoard };

ATSoundBoardEmulator::ATSoundBoardEmulator() {
}

ATSoundBoardEmulator::~ATSoundBoardEmulator() {
	Shutdown();
}

void *ATSoundBoardEmulator::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceAudioOutput::kTypeID:
			return static_cast<IATDeviceAudioOutput *>(this);

		case IATDeviceMemMap::kTypeID:
			return static_cast<IATDeviceMemMap *>(this);

		case IATDeviceScheduling::kTypeID:
			return static_cast<IATDeviceScheduling *>(this);

		case IATDeviceU1MBControllable::kTypeID:
			return static_cast<IATDeviceU1MBControllable *>(this);
	}

	return ATDevice::AsInterface(id);
}

void ATSoundBoardEmulator::SetMemBase(uint32 membase) {
	if (mMemBase == membase)
		return;

	mMemBase = membase;

	if (mpMemMan)
		UpdateControlLayer();
}

void ATSoundBoardEmulator::InitAudioOutput(IATAudioMixer *mixer) {
	mpAudioMixer = mixer;

	mixer->AddSyncAudioSource(this);
}

void ATSoundBoardEmulator::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;
}

bool ATSoundBoardEmulator::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	const uint32 membase = mMemBaseOverride >= 0 && mCoreVersion != CoreVersion::Version20 ? mMemBaseOverride : mMemBase;

	if (index)
		return false;

	switch(membase) {
		case 0xD280:
			lo = 0xD280;
			hi = 0xD2C0;
			return true;

		case 0xD2C0:
			lo = 0xD2C0;
			hi = 0xD300;
			return true;

		case 0xD600:
			lo = 0xD600;
			hi = 0xD700;
			return true;

		case 0xD700:
			lo = 0xD700;
			hi = 0xD800;
			return true;
	}

	return false;
}

void ATSoundBoardEmulator::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

void ATSoundBoardEmulator::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefSoundBoard;
}

void ATSoundBoardEmulator::GetSettingsBlurb(VDStringW& buf) {
	switch(mCoreVersion) {
		case CoreVersion::Version11:
			buf += L"1.1";
			break;

		case CoreVersion::Version12:
			buf += L"1.2";
			break;

		case CoreVersion::Version20:
			buf += L"2.0 Preview";
			break;
	}

	buf.append_sprintf(L" @ $%04X", mMemBase);
}

void ATSoundBoardEmulator::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("base", mMemBase);

	uint32 versionId = 0;

	switch(mCoreVersion) {
		case CoreVersion::Version11: versionId = 110; break;
		default:
		case CoreVersion::Version12: versionId = 120; break;
		case CoreVersion::Version20: versionId = 200; break;
	}

	settings.SetUint32("version", versionId);
}

bool ATSoundBoardEmulator::SetSettings(const ATPropertySet& settings) {
	SetMemBase(settings.GetUint32("base", mMemBase));

	auto oldCoreVer = mCoreVersion;
	switch(settings.GetUint32("version")) {
		case 110: mCoreVersion = CoreVersion::Version11; break;
		default:
		case 120: mCoreVersion = CoreVersion::Version12; break;
		case 200: mCoreVersion = CoreVersion::Version20; break;
	}

	if (mCoreVersion != oldCoreVer && mCoreVersion == CoreVersion::Version20)
		mChannelCount = 6;

	ApplyCoreLimitations();
	UpdateControlLayer();

	return true;
}

void ATSoundBoardEmulator::Init() {
	mpMemory = new uint8[4*1024*1024];

	mpIrqController = GetService<ATIRQController>();
	mIrqBit = mpIrqController->AllocateIRQ();

	mpLeftEdgeBuffer = new ATSyncAudioEdgeBuffer;
	mpLeftEdgeBuffer->mLeftVolume = 1.0f;
	mpLeftEdgeBuffer->mRightVolume = 0.0f;

	mpRightEdgeBuffer = new ATSyncAudioEdgeBuffer;
	mpRightEdgeBuffer->mLeftVolume = 0.0f;
	mpRightEdgeBuffer->mRightVolume = 1.0f;

	ColdReset();
}

void ATSoundBoardEmulator::Shutdown() {
	if (mpIrqController) {
		mpIrqController->FreeIRQ(mIrqBit);
		mIrqBit = 0;
	}

	if (mpAudioMixer) {
		mpAudioMixer->RemoveSyncAudioSource(this);
		mpAudioMixer = nullptr;
	}

	if (mpMemMan) {
		if (mpMemLayerControl) {
			mpMemMan->DeleteLayer(mpMemLayerControl);
			mpMemLayerControl = NULL;
		}

		mpMemMan = NULL;
	}

	if (mpMemory) {
		delete[] mpMemory;
		mpMemory = NULL;
	}

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpChannelIrq);
		mpScheduler = nullptr;
	}
}

void ATSoundBoardEmulator::ColdReset() {
	memset(mpMemory, 0, 0x400000);

	ApplyCoreLimitations();

	if (mCoreVersion == CoreVersion::Version20)
		mChannelCount = 6;
	else
		mChannelCount = 8;

	mLoadAddress = 0;
	mpCurChan = &mChannels[0];
	mCurChanIndex = 0;

	mMultiplierMode = 0;
	mMultiplierArg1[0] = 0xFF;
	mMultiplierArg1[1] = 0xFF;
	mMultiplierArg2[0] = 0xFF;
	mMultiplierArg2[1] = 0xFF;
	mMultiplierResult[0] = 0xFF;
	mMultiplierResult[1] = 0xFF;
	mMultiplierResult[2] = 0xFF;
	mMultiplierResult[3] = 0xFF;

	UpdateControlLayer();

	WarmReset();
}

void ATSoundBoardEmulator::WarmReset() {
	memset(mChannels, 0, sizeof mChannels);

	for(auto& deadline : mChannelIrqDeadlines)
		deadline = ~UINT64_C(0);

	mMainControl = 0;

	mpLeftEdgeBuffer->mEdges.clear();
	mpRightEdgeBuffer->mEdges.clear();

	mLastUpdate = mpScheduler->GetTick64();

	UpdateAllIrqDeadlines();
}

void ATSoundBoardEmulator::SetU1MBControl(ATU1MBControl control, sint32 value) {
	if (control == kATU1MBControl_SoundBoardBase) {
		if (mMemBaseOverride != value) {
			mMemBaseOverride = value;

			if (mpMemMan)
				UpdateControlLayer();
		}
	}
}

uint8 ATSoundBoardEmulator::DebugReadControlV1(uint8 addr) const {
	switch(addr & 0x1F) {
		case 0x00:
			return 0x53;

		case 0x01:
			return 0x42;

		case 0x02:		// major version
			return 0x01;

		case 0x03:		// minor version
			switch(mCoreVersion) {
				case CoreVersion::Version11:
					return 0x01;

				case CoreVersion::Version12:
					return 0x02;

				default:
					break;
			}
			break;

		case 0x13:
			return mpMemory[mLoadAddress & 0x7FFFF];

		case 0x1A:
			if (mCoreVersion == CoreVersion::Version12)
				return mMultiplierResult[0];
			break;

		case 0x1B:
			if (mCoreVersion == CoreVersion::Version12)
				return mMultiplierResult[1];
			break;

		case 0x1C:
			if (mCoreVersion == CoreVersion::Version12)
				return mMultiplierResult[2];
			break;

		case 0x1D:
			if (mCoreVersion == CoreVersion::Version12)
				return mMultiplierResult[3];
			break;

	}

	return 0xFF;
}

uint8 ATSoundBoardEmulator::ReadControlV1(uint8 addr) {
	switch(addr & 0x1F) {
		case 0x13:
			return mpMemory[mLoadAddress++ & 0x7FFFF];

		default:
			break;
	}

	return DebugReadControlV1(addr);
}

void ATSoundBoardEmulator::WriteControlV1(uint8 addr, uint8 value) {
	if (addr < 0x14)
		Flush();

	switch(addr) {
		case 0x00:	// address low
			mpCurChan->mAddress = (mpCurChan->mAddress & 0x7ff00) + value;
			break;

		case 0x01:	// address mid
			mpCurChan->mAddress = (mpCurChan->mAddress & 0x700ff) + ((uint32)value << 8);
			break;

		case 0x02:	// address high
			mpCurChan->mAddress = (mpCurChan->mAddress & 0xffff) + ((uint32)(value & 0x07) << 16);
			break;

		case 0x03:	// length low
			mpCurChan->mLength = (mpCurChan->mLength & 0xff00) + value;
			break;

		case 0x04:	// length high
			mpCurChan->mLength = (mpCurChan->mLength & 0x00ff) + ((uint32)value << 8);
			break;

		case 0x05:	// repeat low
			mpCurChan->mRepeat = (mpCurChan->mRepeat & 0xff00) + value;
			break;

		case 0x06:	// repeat high
			mpCurChan->mRepeat = (mpCurChan->mRepeat & 0x00ff) + ((uint32)value << 8);
			break;

		case 0x07:	// freq low
			mpCurChan->mFreq = (mpCurChan->mFreq & 0xff00) + value;
			break;

		case 0x08:	// freq high
			mpCurChan->mFreq = (mpCurChan->mFreq & 0x00ff) + ((uint32)value << 8);
			break;

		case 0x09:	// volume
			mpCurChan->mVolume = value;
			break;

		case 0x0A:	// pan
			mpCurChan->mPan = value;
			break;

		case 0x0B:	// attack
			mpCurChan->mAttack = value;
			break;

		case 0x0C:	// decay
			mpCurChan->mDecay = value;
			break;

		case 0x0D:	// sustain
			mpCurChan->mSustain = value;
			break;

		case 0x0E:	// release
			mpCurChan->mRelease = value;
			break;

		case 0x0F:	// control
			value &= 0x01;

			mpCurChan->mControl = value;

			if (!(value & 1)) {
				mpCurChan->mPhase = 0;
				mpCurChan->mbInRepeat = false;
			}
			break;

		case 0x10:	// load address low
			mLoadAddress = (mLoadAddress & 0x7ff00) + value;
			break;

		case 0x11:	// load address med
			mLoadAddress = (mLoadAddress & 0x700ff) + ((uint32)value << 8);
			break;

		case 0x12:	// load address high
			mLoadAddress = (mLoadAddress & 0x0ffff) + ((uint32)(value & 0x07) << 16);
			break;

		case 0x13:	// load byte
			{
				const uint32 offset = mLoadAddress++ & 0x7FFFF;

				mpMemory[offset] = value;
				mpMemory[offset + 0x80000] = value;
			}
			break;

		case 0x14:	// channel select
			mpCurChan = &mChannels[value & 7];
			mCurChanIndex = value & 7;
			break;

		case 0x15:	// multiplier accumulation mode
			mMultiplierMode = value;
			break;

		case 0x16:	// multiplier arg1 lo
			mMultiplierArg1[0] = value;
			break;

		case 0x17:	// multiplier arg1 hi
			mMultiplierArg1[1] = value;
			break;

		case 0x18:	// multiplier arg2 lo
			mMultiplierArg2[0] = value;
			break;

		case 0x19:	// multiplier arg2 hi and go strobe
			mMultiplierArg2[1] = value;

			{
				const sint32 arg1 = VDReadUnalignedLES16(mMultiplierArg1);
				const sint32 arg2 = VDReadUnalignedLES16(mMultiplierArg2);
				sint32 result = VDReadUnalignedLES32(mMultiplierResult);

				switch(mMultiplierMode) {
					case 0:		// 16x16 -> 32 signed multiplication
						result = arg1 * arg2;
						break;

					case 1:		// 16x16 -> 32 signed mul-add
						result += arg1 * arg2;
						break;

					case 2:		// 16/16 -> 16:16 fixed point signed division
						if (!arg2) {
							// Division by zero produces +/-inf.
							result = (arg1 >> 31) ^ 0x7FFFFFFF;
						} else if (arg1 == -0x8000 && arg2 == -1) {
							// This is the singular possible case for integer division overflow.
							result = 0x7FFFFFFF;
						} else {
							result = (arg1 << 16) / arg2;
						}
						break;

					case 4:		// 16/16 -> 16:16 fixed point unsigned division
						if (arg2)
							result = ((uint32)arg1 << 16) / ((uint32)arg2 & 0xffff);
						else
							result = 0xFFFFFFFFU;
						break;
				}

				VDWriteUnalignedLES32(mMultiplierResult, result);
			}
			break;
	}
}

uint8 ATSoundBoardEmulator::DebugReadControlV2(uint8 addr) const {
	switch(addr & 0x1F) {
		case 0x00:	// address low
			return (uint8)(mpCurChan->mAddress >> 0);

		case 0x01:	// address mid
			return (uint8)(mpCurChan->mAddress >> 8);

		case 0x02:	// address high
			return (uint8)(mpCurChan->mAddress >> 16);

		case 0x03:	// length low
			return (uint8)(mpCurChan->mLength >> 0);

		case 0x04:	// length high
			return (uint8)(mpCurChan->mLength >> 8);

		case 0x05:	// repeat low
			return (uint8)(mpCurChan->mRepeat >> 0);

		case 0x06:	// repeat high
			return (uint8)(mpCurChan->mRepeat >> 8);

		case 0x07:	// freq low
			return (uint8)(mpCurChan->mFreq >> 0);

		case 0x08:	// freq high
			return (uint8)(mpCurChan->mFreq >> 8);

		case 0x09:	// volume
			return mpCurChan->mVolume;

		case 0x0A:	// pan
			return mpCurChan->mPan;

		case 0x0B:	// repeat low
			return (uint8)(mpCurChan->mRepeatLen >> 0);

		case 0x0C:	// repeat high
			return (uint8)(mpCurChan->mRepeatLen >> 8);

		case 0x0D:	// offset low
			return (uint8)(mpCurChan->mOffset >> 0);

		case 0x0E:	// offset high
			return (uint8)(mpCurChan->mOffset >> 8);

		case 0x0F:	// control
			{
				uint8 v = mpCurChan->mControl;

				if ((v & 0x40) && mpScheduler->GetTick64() >= mChannelIrqDeadlines[mCurChanIndex])
					v |= 0x80;

				return v;
			}

		case 0x10:
			return (uint8)mLoadAddress;

		case 0x11:
			return (uint8)(mLoadAddress >> 8);

		case 0x12:
			return (uint8)((mLoadAddress >> 16) & 0x1F);

		case 0x13:
			return mpMemory[mLoadAddress & 0x1FFFFF];

		case 0x14:
			return (uint8)(mpCurChan - mChannels);

		case 0x15:
			return mMainControl + (mpScheduler->GetTick64() >= mGlobalIrqDeadline ? 0x80 : 0x00);

		case 0x16:
			return mChannelCount - 1;

		case 0x1D:
			return 0x53;

		case 0x1E:
			return 0x42;

		case 0x1F:
			return 0x20;
	}

	return 0xFF;
}

uint8 ATSoundBoardEmulator::ReadControlV2(uint8 addr) {
	switch(addr & 0x1F) {
		case 0x13:
			return mpMemory[mLoadAddress++ & 0x1FFFFF];

		default:
			break;
	}

	return DebugReadControlV2(addr);
}

void ATSoundBoardEmulator::WriteControlV2(uint8 addr, uint8 value) {
	if (addr < 0x14)
		Flush();

	switch(addr) {
		case 0x00:	// address low
			mpCurChan->mAddress = (mpCurChan->mAddress & 0x1FFF00) + value;
			break;

		case 0x01:	// address mid
			mpCurChan->mAddress = (mpCurChan->mAddress & 0x1F00FF) + ((uint32)value << 8);
			break;

		case 0x02:	// address high
			mpCurChan->mAddress = (mpCurChan->mAddress & 0xFFFF) + ((uint32)(value & 0x1F) << 16);
			break;

		case 0x03:	// length low
			mpCurChan->mLength = (mpCurChan->mLength & 0xff00) + value;
			break;

		case 0x04:	// length high
			mpCurChan->mLength = (mpCurChan->mLength & 0x00ff) + ((uint32)value << 8);
			break;

		case 0x05:	// repeat low
			mpCurChan->mRepeat = (mpCurChan->mRepeat & 0xff00) + value;
			break;

		case 0x06:	// repeat high
			mpCurChan->mRepeat = (mpCurChan->mRepeat & 0x00ff) + ((uint32)value << 8);
			break;

		case 0x07:	// freq low
			mpCurChan->mFreq = (mpCurChan->mFreq & 0xff00) + value;
			break;

		case 0x08:	// freq high
			mpCurChan->mFreq = (mpCurChan->mFreq & 0x00ff) + ((uint32)value << 8);
			break;

		case 0x09:	// volume
			mpCurChan->mVolume = value;
			break;

		case 0x0A:	// pan
			mpCurChan->mPan = value;
			break;

		case 0x0B:	// repeat low
			mpCurChan->mRepeatLen = (mpCurChan->mRepeatLen & 0xff00) + value;
			break;

		case 0x0C:	// repeat high
			mpCurChan->mRepeatLen = (mpCurChan->mRepeatLen & 0x00ff) + ((uint32)value << 8);
			break;

		case 0x0D:	// offset low
			mpCurChan->mOffset = (mpCurChan->mOffset & 0xff00) + value;
			break;

		case 0x0E:	// offset high
			mpCurChan->mOffset = (mpCurChan->mOffset & 0x00ff) + ((uint32)value << 8);
			break;

		case 0x0F:	// control
			mpCurChan->mControl = value & 0b0'01001111;

			if (UpdateChannelIrqDeadline(*mpCurChan, mCurChanIndex, true))
				UpdateIrqAndDeadline();

			// reset phase on gate rearm, even if already active
			if (!(value & 1)) {
				mpCurChan->mPhase = (uint64)mpCurChan->mOffset << 47;

				if (mpCurChan->mControl & 0x08)
					mpCurChan->mPhase += mpCurChan->mPhase;

				mpCurChan->mbInRepeat = false;
			}
			break;

		case 0x10:	// load address low
			mLoadAddress = (mLoadAddress & 0x1FFF00) + value;
			break;

		case 0x11:	// load address med
			mLoadAddress = (mLoadAddress & 0x1F00FF) + ((uint32)value << 8);
			break;

		case 0x12:	// load address high
			mLoadAddress = (mLoadAddress & 0x00FFFF) + ((uint32)(value & 0x1F) << 16);
			break;

		case 0x13:	// load byte
			{
				uint32 offset = mLoadAddress++ & 0x1FFFFF;
				mpMemory[offset] = value;
				mpMemory[offset + 0x200000] = value;
			}
			break;

		case 0x14:	// channel select
			mpCurChan = &mChannels[value & 31];
			mCurChanIndex = value & 31;
			break;

		case 0x15:	// main control
			// check if global IRQ is being toggled -- if so, we need to update all channel deadlines
			if ((mMainControl ^ value) & 0x40) {
				mMainControl ^= 0x40;

				UpdateAllIrqDeadlines();
			}
			break;

		case 0x16:	// channel count
			{
				uint8 newChannelCount = value > 31 ? 32 : value + 1;;

				if (mChannelCount != newChannelCount) {
					mChannelCount = newChannelCount;

					UpdateAllIrqDeadlines();
				}
			}
			break;
	}

	// if the frequency or length were changed with the IRQ enabled, then update the deadlines
	if (mpCurChan->mControl & 0x40) {
		switch(addr) {
			case 0x03:	// length low
			case 0x04:	// length high
			case 0x07:	// freq low
			case 0x08:	// freq high
				if (UpdateChannelIrqDeadline(*mpCurChan, mCurChanIndex, false))
					UpdateIrqAndDeadline();
				break;
		}
	}
}

template<bool T_V2>
void ATSoundBoardEmulator::Run(uint64 targetTime) {
	uint32 dt = (uint32)(targetTime - mLastUpdate);

	// if we can't generate anything, we're done
	const uint32 cyclesPerSample = T_V2 ? mChannelCount : 6;
	const uint32 channelCount = mChannelCount;
	if (dt < channelCount)
		return;

	// compute samples we can generate based on time
	const uint32 samplesToGenerate = dt / cyclesPerSample;

	// advance tracked time
	const uint32 baseTime = (uint32)mLastUpdate;
	mLastUpdate += samplesToGenerate * cyclesPerSample;

	// process channels
	for(uint32 chidx = 0; chidx < mChannelCount; ++chidx) {
		Channel *VDRESTRICT ch = &mChannels[chidx];

		if (!(ch->mControl & 0x01))
			continue;

		if constexpr (!T_V2)
			RunChannel<false, false>(ch, baseTime, cyclesPerSample, samplesToGenerate);
		else  if (ch->mControl & (mMainControl & 0x40))
			RunChannel<true, true>(ch, baseTime, cyclesPerSample, samplesToGenerate);
		else
			RunChannel<true, false>(ch, baseTime, cyclesPerSample, samplesToGenerate);
	}
}

template<bool T_V2, bool T_OneShot>
void ATSoundBoardEmulator::RunChannel(Channel *VDRESTRICT ch, uint32 baseTime, uint32 cyclesPerSample, uint32 samplesToGenerate) {
	using PhaseValue = std::conditional_t<T_V2, uint64, uint32>;
	constexpr int kPhaseShift = T_V2 ? 47 : 16;

	const uint32 vol = ch->mVolume;

	PhaseValue phase = T_V2 ? ch->mPhase : (PhaseValue)(ch->mPhase >> 31);
	PhaseValue freq = (PhaseValue)ch->mFreq << (kPhaseShift - 16);
	PhaseValue length = (PhaseValue)ch->mLength << kPhaseShift;
	PhaseValue repeat = (PhaseValue)ch->mRepeat << kPhaseShift;
	PhaseValue repeatLength = T_V2 && (ch->mControl & 0x04) ? (PhaseValue)ch->mRepeatLen << kPhaseShift : length;

	if constexpr (T_V2) {
		if (ch->mControl & 0x08) {
			length += length;
			repeat += repeat;
			repeatLength += repeatLength;
		}

		if (mChannelCount > 8) {
			freq += freq;

			if (mChannelCount > 16)
				freq += freq;
		}
	}

	PhaseValue currentLength = T_V2 && ch->mbInRepeat ? repeatLength : length;

	if (!vol) {
		// If the channel has DMA enabled but is shut off, we still have to update
		// the phase.

		// this is pretty lame, but it correctly handles the lost phase on
		// the repeat
		for(uint32 i = 0; i < samplesToGenerate; ++i) {
			phase += freq;

			if (phase >= currentLength) {
				if constexpr (T_OneShot) {
					ch->mControl &= 0xFE;
					break;
				} else {
					phase = repeat;

					if constexpr (T_V2)
						currentLength = repeatLength;

					ch->mbInRepeat = true;
				}
			}
		}

		ch->mPhase = phase;
		return;
	}

	const auto leftN0 = mpLeftEdgeBuffer->mEdges.size();
	const auto rightN0 = mpRightEdgeBuffer->mEdges.size();

	mpLeftEdgeBuffer->mEdges.resize(leftN0 + samplesToGenerate);
	mpRightEdgeBuffer->mEdges.resize(rightN0 + samplesToGenerate);

	auto *leftEdges0 = &*(mpLeftEdgeBuffer->mEdges.end() - samplesToGenerate);
	auto *rightEdges0 = &*(mpRightEdgeBuffer->mEdges.end() - samplesToGenerate);

	auto *VDRESTRICT leftEdges = leftEdges0;
	auto *VDRESTRICT rightEdges = rightEdges0;

	const uint32 panleft = 255 - ch->mPan;
	const uint32 panright = ch->mPan;
	float volf = (float)vol;
	float volPanLeft = volf * (float)panleft;
	float volPanRight = volf * (float)panright;

	uint32 sampleTime = baseTime;
	constexpr uint32 addrMask = T_V2 ? 0x1FFFFF : 0x7FFFF;
	const uint8 *VDRESTRICT mem = mpMemory + (ch->mAddress & addrMask);

	const int sampleXor = T_V2 && (ch->mControl & 0x02) ? 0x80 : 0x00;
	float lastLeft = ch->mLastLeft;
	float lastRight = ch->mLastRight;

	for(uint32 i = 0; i < samplesToGenerate; ++i) {
		float sample = (float)(((int)mem[(ptrdiff_t)(phase >> kPhaseShift)] ^ sampleXor) - 0x80);

		phase += freq;

		if (phase >= currentLength) {
			if constexpr (T_OneShot) {
				ch->mControl &= 0xFE;
				break;
			} else {
				phase = repeat;

				if constexpr (T_V2)
					currentLength = repeatLength;

				ch->mbInRepeat = true;
			}
		}

		const float sampleL = sample * volPanLeft;
		const float sampleR = sample * volPanRight;

		float deltaLeft = sampleL - lastLeft;
		float deltaRight = sampleR - lastRight;

		sampleTime += cyclesPerSample;
		leftEdges->mTime = sampleTime;
		leftEdges->mDeltaValue = deltaLeft;
		rightEdges->mTime = sampleTime;
		rightEdges->mDeltaValue = deltaRight;

		leftEdges += (deltaLeft != 0);
		rightEdges += (deltaRight != 0);

		lastLeft = sampleL;
		lastRight = sampleR;
	}

	if constexpr (T_V2)
		ch->mPhase = phase;
	else
		ch->mPhase = (uint64)phase << 31;

	ch->mLastLeft = lastLeft;
	ch->mLastRight = lastRight;

	mpLeftEdgeBuffer->mEdges.resize(leftN0 + (leftEdges - leftEdges0));
	mpRightEdgeBuffer->mEdges.resize(rightN0 + (rightEdges - rightEdges0));
}

void ATSoundBoardEmulator::WriteAudio(const ATSyncAudioMixInfo& mixInfo) {
	Flush();

	// 1/255 for uint8 volume normalization.
	// 1/255 for uint8 panning normalization.
	// 1/127 for signed to normalized sample conversion.
	// 1/4 for channel mixing (taking some clipping, vs. worst case 1/8)
	static constexpr float kChannelScale = 1.0f / 255.0f / 255.0f / 127.0f / 4.0f;
	const float volume = mixInfo.mpMixLevels[kATAudioMix_Other] * kChannelScale;

	mpLeftEdgeBuffer->mLeftVolume = volume;
	mpRightEdgeBuffer->mRightVolume = volume;

	auto& edgePlayer = mpAudioMixer->GetEdgePlayer();

	if (!mpLeftEdgeBuffer->mEdges.empty())
		edgePlayer.AddEdgeBuffer(mpLeftEdgeBuffer);

	if (!mpRightEdgeBuffer->mEdges.empty())
		edgePlayer.AddEdgeBuffer(mpRightEdgeBuffer);
}

void ATSoundBoardEmulator::OnScheduledEvent(uint32 id) {
	mpChannelIrq = nullptr;

	if (mpScheduler->GetTick64() >= mGlobalIrqDeadline)
		AssertIrq();
	else
		UpdateIrq();
}

void ATSoundBoardEmulator::Flush() {
	uint32 t = ATSCHEDULER_GETTIME(mpScheduler);

	if (mCoreVersion == CoreVersion::Version20)
		Run<true>(t);
	else
		Run<false>(t);
}

void ATSoundBoardEmulator::UpdateAllIrqDeadlines() {
	for(uint32 i = 0; i < mChannelCount; ++i)
		UpdateChannelIrqDeadline(mChannels[i], i, false);

	uint64 t = mpScheduler->GetTick64();

	for(uint32 i = mChannelCount; i < 32; ++i) {
		if (t >= mChannelIrqDeadlines[i])
			mChannelIrqDeadlines[i] = ~UINT64_C(0);
	}

	if (mMainControl & 0x40)
		mGlobalIrqDeadline = ~UINT64_C(0);
	else
		mGlobalIrqDeadline = ComputeNearestIrq();

	UpdateIrq();
}

bool ATSoundBoardEmulator::UpdateChannelIrqDeadline(const Channel& ch, uint32 idx, bool resetIrq) {
	uint64 deadline = ~UINT64_C(0);

	if ((ch.mControl & 0x41) == 0x40 && ch.mFreq > 0 && (mMainControl & 0x40)) {
		const uint32 length = ch.mbInRepeat && (ch.mControl & 0x04) ? ch.mRepeatLen : ch.mLength;
		const uint64 phaseLimit = (uint64)length << 47;

		uint64 samplesToIrq = ch.mPhase >= phaseLimit ? 1 : (((uint64)(phaseLimit - ch.mPhase) >> 31) + ch.mFreq - 1) / ch.mFreq;

		deadline = mpScheduler->GetTick64() + samplesToIrq * mChannelCount;
	}

	// if the channel IRQ has already fired, do not clear it even if parameters have
	// changed
	if (resetIrq) {
		if (mChannelIrqDeadlines[idx] == deadline)
			return false;
	} else {
		if (mChannelIrqDeadlines[idx] <= deadline)
			return false;
	}

	mChannelIrqDeadlines[idx] = deadline;
	return true;
}

void ATSoundBoardEmulator::UpdateIrqAndDeadline() {
	mGlobalIrqDeadline = ComputeNearestIrq();
	UpdateIrq();
}

void ATSoundBoardEmulator::UpdateIrq() {
	if ((mMainControl & 0x40) && mGlobalIrqDeadline != ~UINT64_C(0)) {
		const uint64 t = mpScheduler->GetTick64();

		if (t < mGlobalIrqDeadline) {
			mpScheduler->SetEvent((uint32)std::min<uint64>(mGlobalIrqDeadline - t, 1000000), this, 1, mpChannelIrq);
			return;
		}

		AssertIrq();
		return;
	}

	mpScheduler->UnsetEvent(mpChannelIrq);
	NegateIrq();
}

uint64 ATSoundBoardEmulator::ComputeNearestIrq() const {
	uint64 earliest = ~UINT64_C(0);

	for(const uint64 deadline : mChannelIrqDeadlines)
		earliest = std::min<uint64>(earliest, deadline);

	return earliest;
}

void ATSoundBoardEmulator::AssertIrq() {
	mpIrqController->Assert(mIrqBit, false);
}

void ATSoundBoardEmulator::NegateIrq() {
	mpIrqController->Negate(mIrqBit, false);
}

void ATSoundBoardEmulator::UpdateControlLayer() {
	if (!mpMemMan)
		return;

	if (mpMemLayerControl) {
		mpMemMan->DeleteLayer(mpMemLayerControl);
		mpMemLayerControl = NULL;
	}

	uint32 membase = mMemBaseOverride >= 0 && mCoreVersion != CoreVersion::Version20 ? mMemBaseOverride : mMemBase;

	ATMemoryHandlerTable handlers = {};
	handlers.mpThis = this;
	handlers.mbPassAnticReads = true;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;

	if (mCoreVersion == CoreVersion::Version20) {
		handlers.BindDebugReadHandler<&ATSoundBoardEmulator::HandleDebugReadControlV2>();
		handlers.BindReadHandler<&ATSoundBoardEmulator::HandleReadControlV2>();
		handlers.BindWriteHandler<&ATSoundBoardEmulator::HandleWriteControlV2>();
	} else {
		handlers.BindDebugReadHandler<&ATSoundBoardEmulator::HandleDebugReadControlV1>();
		handlers.BindReadHandler<&ATSoundBoardEmulator::HandleReadControlV1>();
		handlers.BindWriteHandler<&ATSoundBoardEmulator::HandleWriteControlV1>();
	}

	switch(membase) {
		case 0:
			break;

		case 0xD280:
		case 0xD2C0:
			mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xD2, 0x01);
			break;

		case 0xD500:
			mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xD5, 0x01);
			break;

		case 0xD600:
			mpMemLayerControl = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xD6, 0x01);
			break;
	}

	if (mCoreVersion == CoreVersion::Version20)
		mMemRange = 0x20;
	else
		mMemRange = 0x40;

	if (mpMemLayerControl) {
		mpMemMan->SetLayerName(mpMemLayerControl, "SoundBoard");
		mpMemMan->EnableLayer(mpMemLayerControl, true);
	}
}

void ATSoundBoardEmulator::ApplyCoreLimitations() {
	if (mCoreVersion != CoreVersion::Version20) {
		std::fill(&mChannels[8], &mChannels[32], Channel{});

		for(Channel& ch : mChannels) {
			ch.mControl &= 0x01;
			ch.mAddress &= 0x7FFFF;
		}

		mChannelCount = 8;

		if (mpMemory)
			memcpy(mpMemory + 0x80000, mpMemory, 0x80000);

		for(uint64& deadline : mChannelIrqDeadlines)
			deadline = ~UINT64_C(0);
	} else {
		if (mpMemory)
			memcpy(mpMemory + 0x200000, mpMemory, 0x200000);
	}

	if (mpIrqController)
		UpdateIrq();
}

sint32 ATSoundBoardEmulator::HandleDebugReadControlV1(uint32 addr) const {
	const uint32 addrOffset = addr - mMemBase;
	if (addrOffset >= mMemRange)
		return -1;

	return DebugReadControlV1(addrOffset & 0x3F);
}

sint32 ATSoundBoardEmulator::HandleReadControlV1(uint32 addr) {
	const uint32 addrOffset = addr - mMemBase;
	if (addrOffset >= mMemRange)
		return -1;

	return ReadControlV1(addrOffset & 0x3F);
}

bool ATSoundBoardEmulator::HandleWriteControlV1(uint32 addr, uint8 value) {
	const uint32 addrOffset = addr - mMemBase;
	if (addrOffset >= mMemRange)
		return false;

	WriteControlV1(addrOffset & 0x3F, value);
	return true;
}

sint32 ATSoundBoardEmulator::HandleDebugReadControlV2(uint32 addr) const {
	const uint32 addrOffset = addr - mMemBase;
	if (addrOffset >= mMemRange)
		return -1;

	return DebugReadControlV2(addrOffset & 0x3F);
}

sint32 ATSoundBoardEmulator::HandleReadControlV2(uint32 addr) {
	const uint32 addrOffset = addr - mMemBase;
	if (addrOffset >= mMemRange)
		return -1;

	return ReadControlV2(addrOffset & 0x3F);
}

bool ATSoundBoardEmulator::HandleWriteControlV2(uint32 addr, uint8 value) {
	const uint32 addrOffset = addr - mMemBase;
	if (addrOffset >= mMemRange)
		return false;

	WriteControlV2(addrOffset & 0x3F, value);
	return true;
}
