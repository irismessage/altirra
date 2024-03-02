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

#include <stdafx.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/deviceserial.h>

void ATCreateDeviceSerialSplitter(const ATPropertySet& pset, IATDevice **dev);

extern const ATDeviceDefinition g_ATDeviceDefSerialSplitter = { "serialsplitter", nullptr, L"Serial port splitter", ATCreateDeviceSerialSplitter };

class ATDeviceSerialSplitter final : public ATDeviceT<IATDeviceParent, IATDeviceSerial>
{
public:
	ATDeviceSerialSplitter();
	~ATDeviceSerialSplitter();

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;

public:		// IATDeviceParent
	IATDeviceBus *GetDeviceBus(uint32 index) override;

public:		// IATDeviceSerial
	void SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) override;
	void SetTerminalState(const ATDeviceSerialTerminalState&) override;
	ATDeviceSerialStatus GetStatus() override;
	void SetOnReadReady(vdfunction<void()> fn) override;
	bool Read(uint32& baudRate, uint8& c) override;
	bool Read(uint32 baudRate, uint8& c, bool& framingError) override;
	void Write(uint32 baudRate, uint8 c) override;
	void FlushBuffers() override;

private:
	vdfunction<void()> mpOnReadReady;

	ATDeviceBusSingleChild mInputPort;
	ATDeviceBusSingleChild mOutputPort;
};

ATDeviceSerialSplitter::ATDeviceSerialSplitter() {
	mInputPort.SetOnAttach(
		[this] {
			IATDeviceSerial *ser = mInputPort.GetChild<IATDeviceSerial>();
			if (ser) {
				ser->SetOnReadReady(
					[this] {
						if (mpOnReadReady)
							mpOnReadReady();
					}
				);

				// send a read ready notification just in case the incoming
				// socket already has somethinbg
				if (mpOnReadReady)
					mpOnReadReady();
			}
		}
	);

	mInputPort.SetOnDetach(
		[this] {
			IATDeviceSerial *ser = mInputPort.GetChild<IATDeviceSerial>();
			if (ser)
				ser->SetOnReadReady(nullptr);
		}
	);

	mOutputPort.SetOnAttach(
		[this] {
			IATDeviceSerial *ser = mOutputPort.GetChild<IATDeviceSerial>();
			if (ser) {
				ser->SetOnReadReady(
					[ser] {
						uint32 baudRate;
						uint8 c;

						while(ser->Read(baudRate, c)) {
							// eat
						}
					}
				);
			}
		}
	);
	
	mOutputPort.SetOnDetach(
		[this] {
			IATDeviceSerial *ser = mOutputPort.GetChild<IATDeviceSerial>();
			if (ser)
				ser->SetOnReadReady(nullptr);
		}
	);
}

ATDeviceSerialSplitter::~ATDeviceSerialSplitter() {
	Shutdown();
}

void ATDeviceSerialSplitter::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefSerialSplitter;
}

void ATDeviceSerialSplitter::Init() {
	mInputPort.Init(this, 0, IATDeviceSerial::kTypeID, "serial", L"Serial port in", "serial_in");
	mOutputPort.Init(this, 1, IATDeviceSerial::kTypeID, "serial", L"Serial port out", "serial_out");
}

void ATDeviceSerialSplitter::Shutdown() {
	mInputPort.Shutdown();
	mOutputPort.Shutdown();
}

IATDeviceBus *ATDeviceSerialSplitter::GetDeviceBus(uint32 index) {
	if (index == 0)
		return &mInputPort;
	else if (index == 1)
		return &mOutputPort;
	else
		return nullptr;
}

void ATDeviceSerialSplitter::SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>& fn) {
}

void ATDeviceSerialSplitter::SetTerminalState(const ATDeviceSerialTerminalState& state) {
}

ATDeviceSerialStatus ATDeviceSerialSplitter::GetStatus() {
	return {};
}

void ATDeviceSerialSplitter::SetOnReadReady(vdfunction<void()> fn) {
	mpOnReadReady = std::move(fn);
}

bool ATDeviceSerialSplitter::Read(uint32& baudRate, uint8& c) {
	baudRate = 0;
	c = 0;

	IATDeviceSerial *ser = mInputPort.GetChild<IATDeviceSerial>();
	if (!ser)
		return false;

	return ser->Read(baudRate, c);
}

bool ATDeviceSerialSplitter::Read(uint32 baudRate, uint8& c, bool& framingError) {
	framingError = false;
	c = 0;

	IATDeviceSerial *ser = mInputPort.GetChild<IATDeviceSerial>();
	if (!ser)
		return false;

	return ser->Read(baudRate, c, framingError);
}

void ATDeviceSerialSplitter::Write(uint32 baudRate, uint8 c) {
	IATDeviceSerial *ser = mOutputPort.GetChild<IATDeviceSerial>();
	if (ser)
		ser->Write(baudRate, c);
}

void ATDeviceSerialSplitter::FlushBuffers() {
	IATDeviceSerial *serInput = mInputPort.GetChild<IATDeviceSerial>();
	if (serInput)
		serInput->FlushBuffers();

	IATDeviceSerial *serOutput = mOutputPort.GetChild<IATDeviceSerial>();
	if (serOutput)
		serOutput->FlushBuffers();
}

///////////////////////////////////////////////////////////////////////////

void ATCreateDeviceSerialSplitter(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceSerialSplitter> p(new ATDeviceSerialSplitter);

	*dev = p;
	(*dev)->AddRef();
}
