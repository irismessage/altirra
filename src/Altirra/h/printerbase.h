//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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

#ifndef f_AT_PRINTERBASE_H
#define f_AT_PRINTERBASE_H

#include <at/atcore/audiosource.h>
#include <at/atcore/audiomixer.h>

class ATSyncAudioEdgeBuffer;

class ATPrinterSoundSource final : public IATSyncAudioSource {
	ATPrinterSoundSource(const ATPrinterSoundSource&) = delete;
	ATPrinterSoundSource& operator=(const ATPrinterSoundSource&) = delete;
public:
	ATPrinterSoundSource();
	~ATPrinterSoundSource();

	void Init(IATAudioMixer& mixer, ATScheduler& sch, const char *debugName);
	void Shutdown();

	void AddPinSound(uint32 t, int numPins);
	void ScheduleSound(ATAudioSampleId sampleId, bool looping, float delay, float duration, float volume);
	void EnablePlatenSound(bool enable);
	void EnableRetractSound(bool enable);
	void PlayHomeSound();

public:	// IATSyncAudioSource
	bool RequiresStereoMixingNow() const override;
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

private:
	struct PinEvent {
		uint32 mTime;
		int mNumPins;
	};

	vdfastvector<PinEvent> mPinTimes;

	vdrefptr<ATSyncAudioEdgeBuffer> mpCurrentEdgeBuffer;
	vdrefptr<ATSyncAudioEdgeBuffer> mpNextEdgeBuffer;
	vdrefptr<IATAudioSoundGroup> mpSoundGroup;
	ATSoundId mPlatenSoundId {};
	ATSoundId mRetractSoundId {};

	IATAudioMixer *mpAudioMixer = nullptr;
	ATScheduler *mpScheduler = nullptr;
	sint32 mPulseWidth = 300;
};

#endif
