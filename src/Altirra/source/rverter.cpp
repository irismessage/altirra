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
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include "rs232.h"
#include "debuggerlog.h"

extern ATDebuggerLogChannel g_ATLCModemData;

//////////////////////////////////////////////////////////////////////////

class ATDeviceRVerter final
	: public ATDevice
	, public IATDeviceParent
	, public IATDeviceScheduling
	, public IATDeviceIndicators
	, public IATDeviceSIO
	, public IATDeviceRawSIO
	, public IATSchedulerCallback
{
	ATDeviceRVerter(const ATDeviceRVerter&);
	ATDeviceRVerter& operator=(const ATDeviceRVerter&);
public:
	ATDeviceRVerter();
	~ATDeviceRVerter();

	void *AsInterface(uint32 id);

	void Init();
	void Shutdown();
	void GetDeviceInfo(ATDeviceInfo& info) override;

	void ColdReset();

	void GetSettings(ATPropertySet& props);
	bool SetSettings(const ATPropertySet& props);

public:	// IATDeviceParent
	const char *GetSupportedType(uint32 index) override;
	void GetChildDevices(vdfastvector<IATDevice *>& devs) override;
	void AddChildDevice(IATDevice *dev) override;
	void RemoveChildDevice(IATDevice *dev) override;

public:	// IATDeviceScheduling
	void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:	// IATDeviceIndicators
	void InitIndicators(IATDeviceIndicatorManager *r) override;

public:	// IATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;
	CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) override;
	void OnSerialAbortCommand() override;
	void OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) override;
	void OnSerialFence(uint32 id) override; 
	CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest& request) override;

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
	void OnControlStateChanged(const ATDeviceSerialStatus& status);

	ATScheduler *mpScheduler;
	ATScheduler *mpSlowScheduler;
	IATDeviceIndicatorManager *mpUIRenderer;
	IATDeviceSIOManager *mpSIOMgr;

	ATEvent	*mpReceiveEvent;
	bool	mbActive;
	uint32	mCyclesPerByteDevice;

	uint32	mBaudRate;
	vdrefptr<IATDeviceSerial> mpDeviceSerial;
	ATDeviceSerialTerminalState	mTerminalState;
};

void ATCreateDeviceRVerter(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceRVerter> p(new ATDeviceRVerter);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefRVerter = { "rverter", nullptr, L"R-Verter", ATCreateDeviceRVerter };

ATDeviceRVerter::ATDeviceRVerter()
	: mpScheduler(NULL)
	, mpSlowScheduler(NULL)
	, mpUIRenderer(NULL)
	, mpSIOMgr(nullptr)
	, mpReceiveEvent(NULL)
	, mbActive(false)
	, mCyclesPerByteDevice(0)
	, mBaudRate(0)
	, mpDeviceSerial()

{
}

ATDeviceRVerter::~ATDeviceRVerter() {
	Shutdown();
}

void *ATDeviceRVerter::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceParent::kTypeID:		return static_cast<IATDeviceParent *>(this);
		case IATDeviceScheduling::kTypeID:	return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceIndicators::kTypeID:	return static_cast<IATDeviceIndicators *>(this);
		case IATDeviceSIO::kTypeID:			return static_cast<IATDeviceSIO *>(this);
		case IATDeviceRawSIO::kTypeID:		return static_cast<IATDeviceRawSIO *>(this);
	}

	return ATDevice::AsInterface(id);
}

void ATDeviceRVerter::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefRVerter;
}

void ATDeviceRVerter::Init() {
	mCyclesPerByteDevice = 3729;
	mpScheduler->SetEvent(mCyclesPerByteDevice, this, 1, mpReceiveEvent);

	mTerminalState.mbDataTerminalReady = true;
	mTerminalState.mbRequestToSend = false;

	if (mpDeviceSerial)
		mpDeviceSerial->SetTerminalState(mTerminalState);
}

void ATDeviceRVerter::Shutdown() {
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

	mpSlowScheduler = nullptr;

	mpUIRenderer = nullptr;
}

void ATDeviceRVerter::ColdReset() {
}

void ATDeviceRVerter::GetSettings(ATPropertySet& props) {
}

bool ATDeviceRVerter::SetSettings(const ATPropertySet& props) {
	return true;
}

const char *ATDeviceRVerter::GetSupportedType(uint32 index) {
	return index ? nullptr : "serial";
}

void ATDeviceRVerter::GetChildDevices(vdfastvector<IATDevice *>& devs) {
	if (mpDeviceSerial)
		devs.push_back(vdpoly_cast<IATDevice *>(mpDeviceSerial));
}

void ATDeviceRVerter::AddChildDevice(IATDevice *dev) {
	IATDeviceSerial *devser = vdpoly_cast<IATDeviceSerial *>(dev);
	if (!devser)
		return;

	if (!mpDeviceSerial)
		SetSerialDevice(devser);
}

void ATDeviceRVerter::RemoveChildDevice(IATDevice *dev) {
	if (mpDeviceSerial) {
		IATDevice *devser0 = vdpoly_cast<IATDevice *>(mpDeviceSerial);

		if (devser0 == dev)
			SetSerialDevice(nullptr);
	}
}

void ATDeviceRVerter::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
	mpSlowScheduler = slowsch;
}

void ATDeviceRVerter::InitIndicators(IATDeviceIndicatorManager *r) {
	mpUIRenderer = r;
}

void ATDeviceRVerter::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

IATDeviceSIO::CmdResponse ATDeviceRVerter::OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) {
	return kCmdResponse_NotHandled;
}

void ATDeviceRVerter::OnSerialAbortCommand() {
}

void ATDeviceRVerter::OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) {
}

void ATDeviceRVerter::OnSerialFence(uint32 id) {
}

IATDeviceSIO::CmdResponse ATDeviceRVerter::OnSerialAccelCommand(const ATDeviceSIORequest& request) {
	return OnSerialBeginCommand(request);
}

void ATDeviceRVerter::OnCommandStateChanged(bool asserted) {
}

void ATDeviceRVerter::OnMotorStateChanged(bool asserted) {
	if (mbActive == asserted)
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
	g_ATLCModemData("Sending byte to modem: $%02X\n", c);

	if (mpDeviceSerial)
		mpDeviceSerial->Write(1789772 / cyclesPerBit, c);
}

void ATDeviceRVerter::OnSendReady() {
}

void ATDeviceRVerter::OnScheduledEvent(uint32 id) {
	mpReceiveEvent = nullptr;

	PollDevice();

	mpScheduler->SetEvent(mCyclesPerByteDevice, this, 1, mpReceiveEvent);
}

void ATDeviceRVerter::SetSerialDevice(IATDeviceSerial *dev) {
	if (mpDeviceSerial == dev)
		return;

	if (mpDeviceSerial)
		mpDeviceSerial->SetOnStatusChange(nullptr);

	mpDeviceSerial = dev;

	if (dev) {
		dev->SetOnStatusChange([this](const ATDeviceSerialStatus& status) { this->OnControlStateChanged(status); });
		dev->SetTerminalState(mTerminalState);
	}
}

void ATDeviceRVerter::PollDevice() {
	if (mpDeviceSerial) {
		uint8 c;
		uint32 baudRate;

		// If the motor line isn't activated, we won't transmit the byte on the SIO bus. However,
		// we still need to read it from the device, because the byte's getting dropped on the floor.
		if (mpDeviceSerial->Read(baudRate, c)) {
			mCyclesPerByteDevice = 17897725 / baudRate;

			const uint32 cyclesPerBit = mCyclesPerByteDevice / 10;

			if (mbActive) {
				g_ATLCModemData("Receiving byte from modem: $%02X\n", c);
				// baud rate = bits per second
				// cycles per bit = cycles per second / bits per second

				mpSIOMgr->SendRawByte(c, cyclesPerBit);
			}
		}
	}
}

void ATDeviceRVerter::OnControlStateChanged(const ATDeviceSerialStatus& status) {
	if (mbActive) {
		mpSIOMgr->SetSIOProceed(this, status.mbCarrierDetect);
		mpSIOMgr->SetSIOInterrupt(this, status.mbDataSetReady);
	}
}
