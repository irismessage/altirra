//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2011 Avery Lee
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
#include <vd2/system/strutil.h>
#include <at/atcore/cio.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicecio.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include "rs232.h"
#include "debuggerlog.h"

extern ATDebuggerLogChannel g_ATLCModemData;

//////////////////////////////////////////////////////////////////////////

class ATDeviceRVerter final
	: public ATDevice
	, public ATDeviceSIO
	, public IATDeviceRawSIO
	, public IATSchedulerCallback
{
	ATDeviceRVerter(const ATDeviceRVerter&) = delete;
	ATDeviceRVerter& operator=(const ATDeviceRVerter&) = delete;
public:
	ATDeviceRVerter(bool sioAdapter);
	~ATDeviceRVerter();

	void *AsInterface(uint32 id);

	void Init();
	void Shutdown();
	void GetDeviceInfo(ATDeviceInfo& info) override;

	void ColdReset();

public:	// ATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;

public:	// IATDeviceRawSIO
	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

public:
	void OnScheduledEvent(uint32 id) override;

protected:
	void SetSerialDevice(IATDeviceSerial *dev);
	void PollDevice();
	void ScheduleRead();
	void OnControlStateChanged(const ATDeviceSerialStatus& status);
	void UpdateSystemClock();

	ATScheduler *mpScheduler = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;

	ATEvent	*mpReceiveEvent = nullptr;
	bool	mbSIOAdapterMode = false;
	bool	mbActive = false;
	uint32	mCyclesPerByteDevice = 0;
	uint32	mMachineRate = 0;
	uint32	mMachineRateX10 = 0;

	uint32	mBaudRate = 0;
	vdrefptr<IATDeviceSerial> mpDeviceSerial;
	ATDeviceSerialTerminalState	mTerminalState {};

	ATDeviceParentSingleChild mDeviceParent;
};

template<bool T_SIOAdapter>
void ATCreateDeviceRVerter(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceRVerter> p(new ATDeviceRVerter(T_SIOAdapter));

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefRVerter = { "rverter", nullptr, L"R-Verter", ATCreateDeviceRVerter<false> };
extern const ATDeviceDefinition g_ATDeviceDefSIOSerialAdapter = { "sioserial", nullptr, L"SIO serial adapter", ATCreateDeviceRVerter<true> };

ATDeviceRVerter::ATDeviceRVerter(bool sioAdapter)
	: mbSIOAdapterMode(sioAdapter)
{
}

ATDeviceRVerter::~ATDeviceRVerter() {
	Shutdown();
}

void *ATDeviceRVerter::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceParent::kTypeID:		return static_cast<IATDeviceParent *>(&mDeviceParent);
		case IATDeviceSIO::kTypeID:			return static_cast<IATDeviceSIO *>(this);
		case IATDeviceRawSIO::kTypeID:		return static_cast<IATDeviceRawSIO *>(this);
	}

	return ATDevice::AsInterface(id);
}

void ATDeviceRVerter::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = mbSIOAdapterMode ? &g_ATDeviceDefSIOSerialAdapter : &g_ATDeviceDefRVerter;
}

void ATDeviceRVerter::Init() {
	mpScheduler = GetService<IATDeviceSchedulingService>()->GetMachineScheduler();
	UpdateSystemClock();

	mTerminalState.mbDataTerminalReady = true;
	mTerminalState.mbRequestToSend = false;

	if (mpDeviceSerial)
		mpDeviceSerial->SetTerminalState(mTerminalState);

	if (mbSIOAdapterMode)
		mbActive = true;

	ScheduleRead();

	mDeviceParent.Init(IATDeviceSerial::kTypeID, "serial", L"Serial Port", "serial", this);
	mDeviceParent.SetOnAttach([this] { SetSerialDevice(mDeviceParent.GetChild<IATDeviceSerial>()); });
	mDeviceParent.SetOnDetach([this] { SetSerialDevice(nullptr); });
}

void ATDeviceRVerter::Shutdown() {
	mDeviceParent.Shutdown();

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	SetSerialDevice(nullptr);

	if (mpScheduler) {
		if (mpReceiveEvent) {
			mpScheduler->RemoveEvent(mpReceiveEvent);
			mpReceiveEvent = nullptr;
		}

		mpScheduler = nullptr;
	}
}

void ATDeviceRVerter::ColdReset() {
	UpdateSystemClock();
}

void ATDeviceRVerter::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDeviceRVerter::OnCommandStateChanged(bool asserted) {
}

void ATDeviceRVerter::OnMotorStateChanged(bool asserted) {
	if (mbActive == asserted || mbSIOAdapterMode)
		return;

	mbActive = asserted;

	if (asserted) {
		bool proceed = false;
		bool interrupt = false;

		if (mpDeviceSerial) {
			ATDeviceSerialStatus status = mpDeviceSerial->GetStatus();

			proceed = status.mbCarrierDetect;
			interrupt = status.mbDataSetReady;
		}

		mpSIOMgr->SetSIOProceed(this, proceed);
		mpSIOMgr->SetSIOInterrupt(this, interrupt);
	} else {
		mpSIOMgr->SetSIOProceed(this, false);
		mpSIOMgr->SetSIOInterrupt(this, false);
	}

	// motor control is reflected to RTS
	mTerminalState.mbRequestToSend = asserted;
	if (mpDeviceSerial)
		mpDeviceSerial->SetTerminalState(mTerminalState);
}

void ATDeviceRVerter::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	if (!cyclesPerBit)
		return;

	g_ATLCModemData("Sending byte to modem: $%02X\n", c);

	if (mpDeviceSerial)
		mpDeviceSerial->Write(mMachineRate / cyclesPerBit, c);
}

void ATDeviceRVerter::OnSendReady() {
}

void ATDeviceRVerter::OnScheduledEvent(uint32 id) {
	mpReceiveEvent = nullptr;

	PollDevice();
}

void ATDeviceRVerter::SetSerialDevice(IATDeviceSerial *dev) {
	if (mpDeviceSerial == dev)
		return;

	if (mpDeviceSerial) {
		mpDeviceSerial->SetOnStatusChange(nullptr);
		mpDeviceSerial->SetOnReadReady(nullptr);
	}

	mpDeviceSerial = dev;

	if (dev) {
		if (!mbSIOAdapterMode)
			dev->SetOnStatusChange([this](const ATDeviceSerialStatus& status) { this->OnControlStateChanged(status); });

		dev->SetTerminalState(mTerminalState);
		dev->SetOnReadReady(
			[this] {
				ScheduleRead();
			}
		);

		ScheduleRead();
	}
}

void ATDeviceRVerter::PollDevice() {
	if (!mpDeviceSerial)
		return;

	uint8 c;
	uint32 baudRate;

	// If the motor line isn't activated, we won't transmit the byte on the SIO bus. However,
	// we still need to read it from the device, because the byte's getting dropped on the floor.
	if (!mpDeviceSerial->Read(baudRate, c))
		return;

	mCyclesPerByteDevice = mMachineRateX10 / baudRate;

	const uint32 cyclesPerBit = mCyclesPerByteDevice / 10;

	if (mbActive) {
		g_ATLCModemData("Receiving byte from modem: $%02X\n", c);
		// baud rate = bits per second
		// cycles per bit = cycles per second / bits per second

		mpSIOMgr->SendRawByte(c, cyclesPerBit);
	}

	if (!mpReceiveEvent)
		mpScheduler->SetEvent(mCyclesPerByteDevice, this, 1, mpReceiveEvent);
}

void ATDeviceRVerter::ScheduleRead() {
	if (!mpReceiveEvent)
		mpScheduler->SetEvent(1, this, 1, mpReceiveEvent);
}

void ATDeviceRVerter::OnControlStateChanged(const ATDeviceSerialStatus& status) {
	if (mbActive && !mbSIOAdapterMode) {
		mpSIOMgr->SetSIOProceed(this, status.mbCarrierDetect);
		mpSIOMgr->SetSIOInterrupt(this, status.mbDataSetReady);
	}
}

void ATDeviceRVerter::UpdateSystemClock() {
	const double rate = mpScheduler->GetRate().asDouble();

	mMachineRate = (uint32)(0.5 + rate);
	mMachineRateX10 = (uint32)(0.5 + rate * 10);
}
