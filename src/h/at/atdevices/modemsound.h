//	Altirra - Atari 800/800XL/5200 emulator
//	Device emulation library - modem sound synthesis engine
//	Copyright (C) 2009-2018 Avery Lee
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

#ifndef f_AT_ATDEVICES_MODEMSOUND_H
#define f_AT_ATDEVICES_MODEMSOUND_H

#include <at/atcore/audiosource.h>
#include <at/atcore/device.h>

enum class ATSoundId : uint32;
class ATSoundSourceSingleTone;
class ATSoundSourceDualTone;
class ATSoundSourceModemData;
class ATSoundSourceOnOffHook;
class IATAudioSoundGroup;

class ATModemSoundEngine final
	: public IATSyncAudioSource
	, public IATDeviceAudioOutput
{
	ATModemSoundEngine(const ATModemSoundEngine&) = delete;
	ATModemSoundEngine& operator=(const ATModemSoundEngine&) = delete;

public:
	ATModemSoundEngine();
	~ATModemSoundEngine();

	void Reset();
	void Shutdown();

	// Set whether audio is enabled according to the current sequencing phase.
	void SetAudioEnabledByPhase(bool enable);

	// Set whether audio is enabled by the speaker enable.
	void SetSpeakerEnabled(bool enable);

	void PlayOnOffHookSound();
	void Play1030RelaySound();
	void PlayDialTone();
	void PlayDTMFTone(uint32 index);
	void PlayRingingTone();
	void PlayModemData(float volume);
	void PlayModemDataV22(bool answering, bool scrambled);
	void PlayModemDataBell212A(bool answering, bool scrambled);
	void PlayOriginatingToneBell103();
	void PlayOriginatingToneV32();
	void PlayTrainingToneV32();
	void PlayAnswerTone(bool bell212a);
	void PlayEchoSuppressionTone();

	// Stop all sounds.
	void Stop();

	// Stop primary sound.
	void Stop1();

public:		// IATSyncAudioSource
	bool RequiresStereoMixingNow() const override;
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo) override;

public:		// IATDeviceAudioOutput
	void InitAudioOutput(IATAudioMixer *mixer);

private:
	void UpdateAudioEnabled();

	IATAudioMixer *mpAudioMixer = nullptr;
	ATSoundId mSoundId {};
	ATSoundId mSoundId2 {};
	bool mbSpeakerEnabled = true;
	bool mbAudioEnabled = false;
	bool mbAudioEnabledByPhase = false;

	vdrefptr<ATSoundSourceSingleTone> mpSingleToneSource;
	vdrefptr<ATSoundSourceDualTone> mpDualToneSource;
	vdrefptr<ATSoundSourceOnOffHook> mpOnOffHookSource;

	vdrefptr<IATAudioSoundGroup> mpSoundGroup;
};

#endif
