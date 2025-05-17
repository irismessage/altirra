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

#ifndef f_AT_ATDEVICES_SUPERSALT_H
#define f_AT_ATDEVICES_SUPERSALT_H

#include <at/atcore/audiosource.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceport.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/scheduler.h>

class ATSyncAudioEdgeBuffer;

class ATDeviceSuperSALT final : public ATDeviceT<IATDeviceRawSIO>, IATSchedulerCallback, IATSyncAudioSource {
public:
	ATDeviceSuperSALT();
	~ATDeviceSuperSALT();

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;

public:
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnBreakStateChanged(bool asserted) override;
	void OnBeginReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;
	void OnSerialBiClockChanged(uint32 cyclesPerBit) override;

public:
	void OnScheduledEvent(uint32 id) override;

public:
	bool RequiresStereoMixingNow() const;
	void WriteAudio(const ATSyncAudioMixInfo& mixInfo);

private:
	uint8 ComputeDirBus() const;
	uint32 ComputeSerialCyclesPerByte(uint8 bus) const;
	uint32 ComputeSerialCyclesPerBit(uint8 bus) const;
	void UpdatePortInputs();
	void StartADC();
	void SetAudioInputEnabled(bool enabled);
	void SetAudioInputPeriodBclk(uint32 cycles);
	void SetAudioInputPeriodBaud(uint32 cycles);
	void FlushAudio(uint64 t);

	vdrefptr<IATDeviceControllerPort> mpPorts[4];
	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEventSendByte = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;

	uint8 mADCValue = 0xFF;
	bool mbSIOLoopbackEnabled = false;
	bool mbAudioInputEnabled = false;
	uint64 mLastAudioTickTime = 0;
	uint64 mLastAudioTimeBclk = 0;
	uint64 mLastAudioTimeBaud = 0;
	uint32 mAudioPeriodBclk = 0;
	uint32 mAudioPeriodBaud = 0;
	uint32 mAudioOutputBclk = 0;
	uint32 mAudioOutputBaud = 0;
	bool mLastAudioPolarity = false;
	bool mbRegisteredForClockOutChanges = false;

	vdrefptr<ATSyncAudioEdgeBuffer> mpAudioEdgeBuffer;
};

#endif
