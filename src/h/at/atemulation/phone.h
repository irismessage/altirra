//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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

#ifndef f_AT_ATEMULATION_PHONE_H
#define f_AT_ATEMULATION_PHONE_H

#include <at/atcore/audiomixer.h>
#include <at/atcore/scheduler.h>

namespace ATPhoneConstants {
	// Minimum break (on-hook) time of 10ms.
	constexpr float kMinBreakTime = 0.010f;

	// Allow pulse rates of 7.5 - 12 pulses/second. This also limits on-hook
	// time.
	constexpr float kMinPulseTime = 1.0f / 12.0f;
	constexpr float kMaxPulseTime = 1.0f / 7.5f;

	// A make (off-hook) time of at least 300ms ends a digit.
	constexpr float kMinPulseInterdigitTime = 0.300f;

	// A make (off-hook) time of at least 1 second causes an end to the
	// number being dialed and commences dialing.
	constexpr float kMaxPulseInterdigitTime = 1.0f;
}

////////////////////////////////////////////////////////////////////////////////

class ATPhonePulseDialDetector final : public IATSchedulerCallback {
	ATPhonePulseDialDetector(const ATPhonePulseDialDetector&) = delete;
	ATPhonePulseDialDetector& operator=(const ATPhonePulseDialDetector&) = delete;

public:
	ATPhonePulseDialDetector();
	~ATPhonePulseDialDetector();

	void Init(ATScheduler *sch);
	void Shutdown();

	void Reset();

	void OnHook();
	void OffHook();

	bool IsDialing() const;

	enum class EventType : int {
		Reset,
		InvalidDigit,
		ValidDigit,
		InvalidDial,
		ValidDial
	};

	void SetOnEvent(vdfunction<void(EventType, const char *)> fn);

public:
	void OnScheduledEvent(uint32 id) override;

private:
	void EndDigit();

	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEventTimeout = nullptr;
	uint64 mLastTransitionTime = 0;
	bool mbOffHook = false;
	bool mbInvalidNumber = false;
	uint8 mDigitCount = 0;
	uint8 mPulseCount = 0;

	static constexpr uint8 kMaxDigits = 31;

	char mDialNumber[kMaxDigits + 1] {};
	vdfunction<void(EventType, const char *)> mpOnEvent;
};

////////////////////////////////////////////////////////////////////////////////

class ATPhoneToneDialDetector final : public IATSchedulerCallback, public IATInternalAudioTap {
	ATPhoneToneDialDetector(const ATPhoneToneDialDetector&) = delete;
	ATPhoneToneDialDetector& operator=(const ATPhoneToneDialDetector&) = delete;

public:
	ATPhoneToneDialDetector();
	~ATPhoneToneDialDetector();

	void Init(ATScheduler *slowSch, IATAudioMixer *mixer);
	void Shutdown();

	void Reset();

	void SetEnabled(bool enabled);

	enum class EventType : int {
		ValidDigit,
		InvalidDial,
		ValidDial
	};

	void SetOnEvent(vdfunction<void(EventType, const char *)> fn);

public:
	void OnScheduledEvent(uint32 id) override;

public:
	void WriteInternalAudio(const float *samples, uint32 count, uint32 timestamp) override;

private:
	static constexpr uint32 kInputBlockSize = 1024;
	static constexpr uint32 kProcessBlockSize = kInputBlockSize / 8;

	void ProcessBlock();
	void ProcessBlockReduceRate(float *dst);

	ATScheduler *mpSlowScheduler = nullptr;
	ATEvent *mpEventTimeout = nullptr;
	IATAudioMixer *mpMixer = nullptr;
	bool mbEnabled = false;
	bool mbDialing = false;
	uint32 mSampleCount = 0;
	bool mbInvalidNumber = false;
	uint8 mDigitCount = 0;

	static constexpr sint8 kNoTone = -1;
	static constexpr sint8 kInvalidTone = -2;

	sint8 mLastTone = kNoTone;
	uint32 mLastToneRepeatCount = 0;

	static constexpr uint8 kMaxDigits = 31;
	char mDialNumber[kMaxDigits + 1] {};

	vdfunction<void(EventType, const char *)> mpOnEvent;

	float mInputBuffer[kInputBlockSize] {};

	alignas(16) static const float kLPFCoeffs[8][8];
};

#endif
