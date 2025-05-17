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
#include <at/atcore/deviceprinter.h>
#include <at/atcore/deviceserial.h>

void ATCreateDeviceParallelToSerial(const ATPropertySet& pset, IATDevice **dev);

extern const ATDeviceDefinition g_ATDeviceDefParallelToSerial = { "par2ser", nullptr, L"Parallel to serial adapter", ATCreateDeviceParallelToSerial };

class ATDeviceParallelToSerial final : public ATDeviceT<IATDeviceParent, IATPrinterOutput>
{
public:
	ATDeviceParallelToSerial();
	~ATDeviceParallelToSerial();

public:
	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;

public:		// IATDeviceParent
	IATDeviceBus *GetDeviceBus(uint32 index) override;

public:		// IATPrinterOutput
	bool WantUnicode() const { return false; }
	void WriteRaw(const uint8 *buf, size_t len) override;

private:
	ATDeviceBusSingleChild mSerialPort;
};

ATDeviceParallelToSerial::ATDeviceParallelToSerial() {
}

ATDeviceParallelToSerial::~ATDeviceParallelToSerial() {
}

void ATDeviceParallelToSerial::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefParallelToSerial;
}

IATDeviceBus *ATDeviceParallelToSerial::GetDeviceBus(uint32 index) {
	if (index == 0)
		return &mSerialPort;
	else
		return nullptr;
}

void ATDeviceParallelToSerial::Init() {
	mSerialPort.Init(this, 0, IATDeviceSerial::kTypeID, "serial", L"Serial port", "serial");
}

void ATDeviceParallelToSerial::Shutdown() {
	mSerialPort.Shutdown();
}

void ATDeviceParallelToSerial::WriteRaw(const uint8 *s, size_t len) {
	IATDeviceSerial *serdev = mSerialPort.GetChild<IATDeviceSerial>();

	if (serdev) {
		while(len--)
			serdev->Write(0, *s++);
	}
}

///////////////////////////////////////////////////////////////////////////

void ATCreateDeviceParallelToSerial(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceParallelToSerial> p(new ATDeviceParallelToSerial);

	*dev = p;
	(*dev)->AddRef();
}
