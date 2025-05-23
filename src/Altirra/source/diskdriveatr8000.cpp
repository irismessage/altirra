//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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
#include <vd2/system/bitmath.h>
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include <vd2/system/math.h>
#include <vd2/system/vdstl_algorithm.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/wraptime.h>
#include "audiosampleplayer.h"
#include "diskdriveatr8000.h"
#include "memorymanager.h"
#include "firmwaremanager.h"
#include "debuggerlog.h"

extern ATLogChannel g_ATLCDiskEmu;

void ATCreateDeviceDiskDriveATR8000(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDriveATR8000> p(new ATDeviceDiskDriveATR8000);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefDiskDriveATR8000 = { "diskdriveatr8000", "diskdriveatr8000", L"ATR8000 disk drive (full emulation)", ATCreateDeviceDiskDriveATR8000 };

///////////////////////////////////////////////////////////////////////////

void ATDeviceDiskDriveATR8000::Drive::OnDiskChanged(bool mediaRemoved) {
	mpParent->OnDiskChanged(mIndex, mediaRemoved);
}

void ATDeviceDiskDriveATR8000::Drive::OnWriteModeChanged() {
	mpParent->OnWriteModeChanged(mIndex);
}

void ATDeviceDiskDriveATR8000::Drive::OnTimingModeChanged() {
	mpParent->OnTimingModeChanged(mIndex);
}

void ATDeviceDiskDriveATR8000::Drive::OnAudioModeChanged() {
	mpParent->OnAudioModeChanged(mIndex);
}

bool ATDeviceDiskDriveATR8000::Drive::IsImageSupported(const IATDiskImage& image) const {
	return true;
}

///////////////////////////////////////////////////////////////////////////

class ATDeviceDiskDriveATR8000::SerialPort final : public ATDeviceBus, public IATSchedulerCallback {
public:
	enum Signal1Mode {
		kSignal1_RTS,
		kSignal1_DTR,
	};

	enum Signal2Mode {
		kSignal2_CTS,
		kSignal2_DSR,
		kSignal2_CD,
		kSignal2_SRTS,
	};

	~SerialPort();

	void Init(IATDeviceParent *parent, ATScheduler *sch);
	void Shutdown();

	void GetSettings(ATPropertySet& settings) const;
	void SetSettings(const ATPropertySet& settings);

	void SetSignal1Mode(Signal1Mode mode);
	void SetSignal2Mode(Signal2Mode mode);

	void Reset();

	void Poll();


	bool GetReceiveState() const;
	void SetTransmitBaudRate(uint32 baudRate);
	void SetTransmitState(bool state);

	void ReadStatus(bool& signal2, bool& ring) const;
	void SetControlState(bool state);

public:
	const wchar_t *GetBusName() const override;
	const char *GetBusTag() const override;
	const char *GetSupportedType(uint32 index) override;
	void GetChildDevices(vdfastvector<IATDevice *>& devs) override;
	void AddChildDevice(IATDevice *dev) override;
	void RemoveChildDevice(IATDevice *dev) override;

public:
	void OnScheduledEvent(uint32 id) override;

private:
	void AdvanceTransmitShifter(uint32 t);
	void UpdateControlState();

	static constexpr uint32 kEventId_SerialTransmit = 1;
	static constexpr uint32 kEventId_SerialReceive = 2;

	IATDeviceParent *mpParent = nullptr;
	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpTransmitEvent = nullptr;
	ATEvent *mpReceiveEvent = nullptr;

	bool mbTransmitLevel = false;
	bool mbControlState = false;

	Signal1Mode mSignal1Mode = kSignal1_RTS;
	Signal2Mode mSignal2Mode = kSignal2_CTS;

	uint32 mTransmitBaudRate = 1200;
	uint32 mTransmitCyclesPerBitX256 = 0;
	uint32 mTransmitShifter = 0;
	uint32 mTransmitStartTime = 0;
	uint32 mTransmitTimeAccum = 0;
	uint32 mTransmitTimeNextBit = 0;
	uint8 mTransmitState = 0;

	uint64 mReceiveByteStart = 0;
	uint32 mReceiveByteDuration = 0;
	uint32 mReceiveShifter = 0;

	vdrefptr<IATDeviceSerial> mpSerialDevice;

	static constexpr const wchar_t *const kSignal1Names[] = {
		L"rts", L"dtr"
	};

	static constexpr const wchar_t *const kSignal2Names[] = {
		L"cts", L"dsr", L"cd", L"srts"
	};
};

ATDeviceDiskDriveATR8000::SerialPort::~SerialPort() {
	if (mpSerialDevice) {
		vdpoly_cast<IATDevice *>(mpSerialDevice)->SetParent(nullptr, 0);
		mpSerialDevice = nullptr;
	}
}

void ATDeviceDiskDriveATR8000::SerialPort::Init(IATDeviceParent *parent, ATScheduler *sch) {
	mpParent = parent;
	mpScheduler = sch;
}

void ATDeviceDiskDriveATR8000::SerialPort::Shutdown() {
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpTransmitEvent);
		mpScheduler->UnsetEvent(mpReceiveEvent);
		mpScheduler = nullptr;
	}
}

void ATDeviceDiskDriveATR8000::SerialPort::GetSettings(ATPropertySet& settings) const {
	settings.SetString("signal1", kSignal1Names[mSignal1Mode]);
	settings.SetString("signal2", kSignal2Names[mSignal2Mode]);
}

void ATDeviceDiskDriveATR8000::SerialPort::SetSettings(const ATPropertySet& settings) {
	const VDStringSpanW s1 { settings.GetString("signal1", L"") };
	int idx1 = vdfind_index_if_r(kSignal1Names, [=](const wchar_t *s) { return s1 == s; });
	mSignal1Mode = idx1 >= 0 ? (Signal1Mode)idx1 : kSignal1_RTS;

	const VDStringSpanW s2 { settings.GetString("signal2", L"") };
	int idx2 = vdfind_index_if_r(kSignal2Names, [=](const wchar_t *s) { return s2 == s; });
	mSignal2Mode = idx2 >= 0 ? (Signal2Mode)idx2 : kSignal2_CTS;
}

void ATDeviceDiskDriveATR8000::SerialPort::SetSignal1Mode(Signal1Mode mode) {
	if (mSignal1Mode != mode) {
		mSignal1Mode = mode;

		UpdateControlState();
	}
}

void ATDeviceDiskDriveATR8000::SerialPort::SetSignal2Mode(Signal2Mode mode) {
	mSignal2Mode = mode;
}

void ATDeviceDiskDriveATR8000::SerialPort::Reset() {
	mpScheduler->UnsetEvent(mpTransmitEvent);
	mpScheduler->UnsetEvent(mpReceiveEvent);

	mbTransmitLevel = true;

	mTransmitCyclesPerBitX256 = 0;
	mTransmitShifter = 0;
	mTransmitStartTime = 0;
	mTransmitTimeAccum = 0;
	mTransmitTimeNextBit = 0;
	mTransmitState = 0;

	mTransmitBaudRate = 0;
	SetTransmitBaudRate(1200);

	mReceiveShifter = 0;
	mReceiveByteDuration = 0;

	mbControlState = false;
	UpdateControlState();
}

bool ATDeviceDiskDriveATR8000::SerialPort::GetReceiveState() const {
	if (!mReceiveByteDuration)
		return false;

	const uint64 dt = mpScheduler->GetTick64() - mReceiveByteStart;

	if (dt >= mReceiveByteDuration)
		return true;

	const uint32 bitIndex = (uint32)(dt * 10) / mReceiveByteDuration;

	return ((mReceiveShifter >> bitIndex) & 1) != 0;
}

void ATDeviceDiskDriveATR8000::SerialPort::SetTransmitBaudRate(uint32 baudRate) {
	if (mTransmitBaudRate == baudRate)
		return;

	mTransmitBaudRate = baudRate;
	mTransmitState = 0;
	mTransmitCyclesPerBitX256 = VDRoundToInt32(mpScheduler->GetRate().asDouble() / (double)mTransmitBaudRate * 256.0);
}

void ATDeviceDiskDriveATR8000::SerialPort::SetTransmitState(bool state) {
	if (mbTransmitLevel == state)
		return;

	// check if we're transmitting
	const uint32 t = mpScheduler->GetTick();

	if (mTransmitState) {
		// check for false start bit
		if (state && mTransmitState == 1 && ATWrapTime{t} < mTransmitTimeNextBit) {
			mTransmitState = 0;
			mpScheduler->UnsetEvent(mpTransmitEvent);
		} else {
			AdvanceTransmitShifter(t);
		}
	}

	// must recheck this as we may have just terminated the last send (!)
	if (!mTransmitState) {
		if (!state) {
			mTransmitState = 1;
			mTransmitShifter = 0;
			mTransmitStartTime = t;
			mTransmitTimeAccum = (mTransmitCyclesPerBitX256 >> 1) + 0x80;
			mTransmitTimeNextBit = t + (mTransmitTimeAccum >> 8);
			mTransmitTimeAccum &= 0xFF;

			mpScheduler->SetEvent((mTransmitCyclesPerBitX256 * 10 + 0x80) >> 8, this, kEventId_SerialTransmit, mpTransmitEvent);
		}
	}

	mbTransmitLevel = state;
}

void ATDeviceDiskDriveATR8000::SerialPort::ReadStatus(bool& signal2, bool& ring) const {
	if (!mpSerialDevice) {
		signal2 = false;
		ring = false;
		return;
	}

	const ATDeviceSerialStatus status = mpSerialDevice->GetStatus();

	switch(mSignal2Mode) {
		case kSignal2_CTS:
		default:
			signal2 = status.mbClearToSend;
			break;

		case kSignal2_DSR:
			signal2 = status.mbDataSetReady;
			break;

		case kSignal2_CD:
			signal2 = status.mbCarrierDetect;
			break;

		case kSignal2_SRTS:
			signal2 = false;
	}

	ring = status.mbRinging;
}

void ATDeviceDiskDriveATR8000::SerialPort::SetControlState(bool state) {
	if (mbControlState != state) {
		mbControlState = state;

		UpdateControlState();
	}
}

const wchar_t *ATDeviceDiskDriveATR8000::SerialPort::GetBusName() const {
	return L"Serial Port";
}

const char *ATDeviceDiskDriveATR8000::SerialPort::GetBusTag() const {
	return "serial";
}

const char *ATDeviceDiskDriveATR8000::SerialPort::GetSupportedType(uint32 index) {
	return index ? nullptr : "serial";
}

void ATDeviceDiskDriveATR8000::SerialPort::GetChildDevices(vdfastvector<IATDevice *>& devs) {
	if (mpSerialDevice)
		devs.push_back(vdpoly_cast<IATDevice *>(mpSerialDevice));
}

void ATDeviceDiskDriveATR8000::SerialPort::AddChildDevice(IATDevice *dev) {
	IATDeviceSerial *sdev = vdpoly_cast<IATDeviceSerial *>(dev);

	if (sdev && !mpSerialDevice) {
		mpSerialDevice = sdev;
		dev->SetParent(mpParent, 0);
	}
}

void ATDeviceDiskDriveATR8000::SerialPort::RemoveChildDevice(IATDevice *dev) {
	IATDeviceSerial *sdev = vdpoly_cast<IATDeviceSerial *>(dev);

	if (sdev && sdev == mpSerialDevice) {
		dev->SetParent(nullptr, 0);
		mpSerialDevice = nullptr;
	}
}

void ATDeviceDiskDriveATR8000::SerialPort::OnScheduledEvent(uint32 id) {
	switch(id) {
		case kEventId_SerialTransmit:
			mpTransmitEvent = nullptr;
			AdvanceTransmitShifter(mpScheduler->GetTick());
			break;

		case kEventId_SerialReceive:
			mpReceiveEvent = nullptr;
			Poll();
			break;
	}
}

void ATDeviceDiskDriveATR8000::SerialPort::Poll() {
	if (!mpSerialDevice || mpReceiveEvent)
		return;

	uint32 baudRate;
	uint8 c;
	if (mpSerialDevice->Read(baudRate, c) && baudRate) {
		uint32 cyclesToNext = VDRoundToInt32(mpScheduler->GetRate().asDouble() * 10 / (double)baudRate);

		if (!cyclesToNext)
			++cyclesToNext;

		mpScheduler->SetEvent(cyclesToNext, this, kEventId_SerialReceive, mpReceiveEvent);

		mReceiveShifter = ((uint32)c << 1) + 0x200;
		mReceiveByteStart = mpScheduler->GetTick64();
		mReceiveByteDuration = cyclesToNext;
	}
}

void ATDeviceDiskDriveATR8000::SerialPort::AdvanceTransmitShifter(uint32 t) {
	while(mTransmitState && ATWrapTime{t} >= mTransmitTimeNextBit) {
		mTransmitTimeAccum += mTransmitCyclesPerBitX256;
		mTransmitTimeNextBit += (mTransmitTimeAccum >> 8);
		mTransmitTimeAccum &= 0xFF;

		mTransmitShifter >>= 1;
		if (mbTransmitLevel)
			mTransmitShifter += 0x200;

		if (++mTransmitState >= 11) {
			mTransmitState = 0;

			mpScheduler->UnsetEvent(mpTransmitEvent);

			if (mpSerialDevice)
				mpSerialDevice->Write(mTransmitBaudRate, (uint8)(mTransmitShifter >> 1));
		}
	}
}

void ATDeviceDiskDriveATR8000::SerialPort::UpdateControlState() {
	if (!mpSerialDevice)
		return;

	ATDeviceSerialTerminalState tstate {};

	switch(mSignal1Mode) {
		case kSignal1_DTR:
			tstate.mbDataTerminalReady = mbControlState;
			break;

		case kSignal1_RTS:
			tstate.mbRequestToSend = mbControlState;
			break;
	}

	mpSerialDevice->SetTerminalState(tstate);
}

///////////////////////////////////////////////////////////////////////////

ATDeviceDiskDriveATR8000::ATDeviceDiskDriveATR8000() {
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);

	mDriveScheduler.SetRate(VDFraction(4000000, 1));

	mpSerialPort = new SerialPort;

	mTargetProxy.Init(mCoProc);
	InitTargetControl(mTargetProxy, 4000000.0, kATDebugDisasmMode_Z80, &mBreakpointsImpl, this);

	// lower min transmission speed to ~200 baud
	mSerialXmitQueue.SetMaxCyclesPerBit(8900);

	mFirmwareControl.Init(mROM, sizeof mROM, kATFirmwareType_ATR8000);

	mParallelBus.SetOnAttach(
		[this] {
			mpPrinterOutput = nullptr;
		}
	);
}

ATDeviceDiskDriveATR8000::~ATDeviceDiskDriveATR8000() {
	vdsafedelete <<= mpSerialPort;
}

void *ATDeviceDiskDriveATR8000::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(&mFirmwareControl);
		case IATDeviceDiskDrive::kTypeID: return static_cast<IATDeviceDiskDrive *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case IATDeviceAudioOutput::kTypeID: return static_cast<IATDeviceAudioOutput *>(&mAudioPlayer);
		case IATDeviceButtons::kTypeID: return static_cast<IATDeviceButtons *>(this);
		case IATDeviceParent::kTypeID: return static_cast<IATDeviceParent *>(this);
		case ATFDCEmulator::kTypeID: return &mFDC;
		case ATCTCEmulator::kTypeID: return &mCTC;
	}

	void *p = ATDiskDriveDebugTargetControl::AsInterface(iid);
	if (p)
		return p;

	return ATDevice::AsInterface(iid);
}

void ATDeviceDiskDriveATR8000::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefDiskDriveATR8000;
}

void ATDeviceDiskDriveATR8000::GetSettingsBlurb(VDStringW& buf) {
	bool first = true;

	for(uint32 i=0; i<kNumDrives; ++i) {
		if (mDrives[i].mType) {
			if (first)
				first = false;
			else
				buf += ',';

			buf.append_sprintf(L"D%u:", i+1);
		}
	}
}

void ATDeviceDiskDriveATR8000::GetSettings(ATPropertySet& settings) {
	VDStringA s;
	for(uint32 i=0; i<kNumDrives; ++i) {
		s.sprintf("drivetype%u", i);
		settings.SetUint32(s.c_str(), (uint32)mDrives[i].mType);
	}

	mpSerialPort->GetSettings(settings);
}

bool ATDeviceDiskDriveATR8000::SetSettings(const ATPropertySet& settings) {
	VDStringA s;
	bool change = false;

	for(uint32 i=0; i<kNumDrives; ++i) {
		s.sprintf("drivetype%u", i);
		const uint32 driveTypeCode = settings.GetUint32(s.c_str(), i ? kDriveType_None : kDriveType_5_25);

		if (driveTypeCode <= kDriveType_8) {
			if (mDrives[i].mType != driveTypeCode) {
				mDrives[i].mType = (DriveType)driveTypeCode;
				change = true;
			}
		}
	}
	
	mpSerialPort->SetSettings(settings);

	return !change;
}

void ATDeviceDiskDriveATR8000::Init() {
	mParallelBus.Init(this, 1, IATPrinterOutput::kTypeID, "parallel", L"Parallel Printer Port", "parport");

	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);

	// The ATR8000 memory map:
	//
	//	0000-FFFF	RAM
	//	0000-7FFF	4K ROM (switchable; only for reads)

	uintptr *readmap = mCoProc.GetReadMap();
	uintptr *writemap = mCoProc.GetWriteMap();

	// set up RAM
	std::fill(readmap, readmap + 0x100, (uintptr)mRAM);
	std::fill(writemap, writemap + 0x100, (uintptr)mRAM);

	// set up port mapping
	mCoProc.SetPortReadHandler([this](uint8 port) { return OnReadPort(port); });
	mCoProc.SetPortWriteHandler([this](uint8 port, uint8 data) { OnWritePort(port, data); });
	mCoProc.SetIntVectorHandler([this]() { return OnGetIntVector(); });
	mCoProc.SetIntAckHandler([this]() { return OnIntAck(); });
	mCoProc.SetHaltChangeHandler([this](bool halted) { return OnHaltChange(halted); });

	mCTC.Init(&mDriveScheduler);
	mCTC.SetInterruptFn([this](sint32 vec) { OnCTCInterrupt(vec); });

	// ZC/TO2 is looped back to CLK/TRG3
	mCTC.SetUnderflowFn(2,
		[this]
		{
			mCTC.SetInputState(3, true);
			mCTC.SetInputState(3, false);
		}
	);

	// Actually a 179X in the ATR8000, but the difference between the 179X and 279X is
	// whether there is an internal data separator; this makes no difference to us.
	mFDC.Init(&mDriveScheduler, 300.0f, 1.0f, ATFDCEmulator::kType_2793);

	mFDC.SetOnDrqChange([this](bool active) { OnFDCDrq(active); });
	mFDC.SetOnIrqChange([this](bool active) { OnFDCIrq(active); });
	mFDC.SetOnStep([this](bool inward) { OnFDCStep(inward); });

	// The 179X doesn't have a motor output, so the ATR8000 abuses the head load output....
	mFDC.SetOnHeadLoadChange([this](bool active) { OnFDCMotorChange(active); });

	mFDC.SetAutoIndexPulse(true);

	for(auto& drive : mDrives) {
		mDriveScheduler.UnsetEvent(drive.mpEventDriveDiskChange);
		drive.mDiskChangeState = 0;

		drive.OnDiskChanged(false);

		drive.OnWriteModeChanged();
		drive.OnTimingModeChanged();
		drive.OnAudioModeChanged();
	}

	UpdateRotationStatus();

	mpSerialPort->Init(this, mpScheduler);
}

void ATDeviceDiskDriveATR8000::Shutdown() {
	mParallelBus.Shutdown();

	mpSerialPort->Shutdown();

	mpPrinterOutput = nullptr;

	mAudioPlayer.Shutdown();
	mSerialXmitQueue.Shutdown();

	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);

	mFDC.Shutdown();
	mCTC.Shutdown();

	ShutdownTargetControl();

	mFirmwareControl.Shutdown();

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	for(auto& drive : mDrives) {
		if (drive.mpDiskInterface) {
			drive.mpDiskInterface->RemoveClient(&drive);
			drive.mpDiskInterface = nullptr;
		}
	}

	mpDiskDriveManager = nullptr;
}

uint32 ATDeviceDiskDriveATR8000::GetComputerPowerOnDelay() const {
	return 20;
}

void ATDeviceDiskDriveATR8000::WarmReset() {
	// If the computer resets, its transmission is interrupted.
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDeviceDiskDriveATR8000::ComputerColdReset() {
	WarmReset();
}

void ATDeviceDiskDriveATR8000::PeripheralColdReset() {
	memset(mRAM, 0xFF, sizeof mRAM);

	// start the disk drive on a track other than 0/20/39, just to make things interesting
	for(Drive& drive : mDrives)
		drive.mCurrentTrack = 2;

	mFDC.SetCurrentTrack(2, false);

	mCoProc.ColdReset();

	ResetTargetControl();

	DeviceReset();
	WarmReset();
}

void ATDeviceDiskDriveATR8000::DeviceReset() {
	mFDC.Reset();
	mCTC.Reset();

	mSerialXmitQueue.Reset();

	SelectDrives(0);

	mbIndexPulseEnabled = true;
	mbIndexPulseDisabled = false;

	mbDirectTransmitOutput = true;

	mbTrigger0SelectData = true;
	UpdateCTCTrigger0Input();

	mbMotorRunning = false;
	mFDC.SetMotorRunning(false);
	mFDC.SetDensity(false);
	mFDC.SetWriteProtectOverride(false);
	mFDC.SetAutoIndexPulse(true);

	mbROMEnabled = true;
	UpdateMemoryMap();

	mCoProc.WarmReset();

	mPrinterData = 0;
	mbPrinterStrobeAsserted = false;

	mpSerialPort->Reset();
}

void ATDeviceDiskDriveATR8000::InitDiskDrive(IATDiskDriveManager *ddm) {
	mpDiskDriveManager = ddm;
	mAvailableDrives = 0;

	for(uint32 i=0; i<kNumDrives; ++i) {
		Drive& drive = mDrives[i];

		drive.mIndex = i;
		drive.mpParent = this;

		if (drive.mType) {
			drive.mpDiskInterface = ddm->GetDiskInterface(i);
			drive.mpDiskInterface->AddClient(&drive);

			mAvailableDrives |= (1 << i);
		}
	}
}

ATDeviceDiskDriveInterfaceClient ATDeviceDiskDriveATR8000::GetDiskInterfaceClient(uint32 index) {
	for(uint32 i=0; i<kNumDrives; ++i) {
		if (mDrives[i].mType && !index--)
			return { &mDrives[i], mDriveId + i };
	}

	return {};
}

void ATDeviceDiskDriveATR8000::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

uint32 ATDeviceDiskDriveATR8000::GetSupportedButtons() const {
	return (1U << kATDeviceButton_ATR8000Reset);
}

bool ATDeviceDiskDriveATR8000::IsButtonDepressed(ATDeviceButton idx) const {
	return false;
}

void ATDeviceDiskDriveATR8000::ActivateButton(ATDeviceButton idx, bool state) {
	switch(idx) {
		case kATDeviceButton_ATR8000Reset:
			if (state)
				DeviceReset();
			break;
	}
}

IATDeviceBus *ATDeviceDiskDriveATR8000::GetDeviceBus(uint32 index) {
	switch(index) {
		case 0: return mpSerialPort;
		case 1: return &mParallelBus;
		default: return nullptr;
	}
}

void ATDeviceDiskDriveATR8000::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveReceiveBit) {
		mReceiveShiftRegister >>= 1;
		mpEventDriveReceiveBit = nullptr;

		if (mReceiveShiftRegister >= 2) {
			mReceiveTimingAccum += mReceiveTimingStep;
			mpEventDriveReceiveBit = mDriveScheduler.AddEvent(mReceiveTimingAccum >> 10, this, kEventId_DriveReceiveBit);
			mReceiveTimingAccum &= 0x3FF;
		}

		UpdateCTCTrigger0Input();
	} else if (id >= kEventId_DriveDiskChange0 && id < kEventId_DriveDiskChange0 + kNumDrives) {
		const uint32 index = id - kEventId_DriveDiskChange0;
		Drive& drive = mDrives[index];

		drive.mpEventDriveDiskChange = nullptr;

		switch(++drive.mDiskChangeState) {
			case 1:		// disk being removed (write protect covered)
			case 2:		// disk removed (write protect clear)
			case 3:		// disk being inserted (write protect covered)
				mDriveScheduler.SetEvent(kDiskChangeStepMS, this, kEventId_DriveDiskChange0 + index, drive.mpEventDriveDiskChange);
				break;

			case 4:		// disk inserted (write protect normal)
				drive.mDiskChangeState = 0;
				break;
		}

		UpdateDiskStatus();
	} else
		ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDeviceDiskDriveATR8000::OnCommandStateChanged(bool asserted) {
	if (mbCommandState != asserted) {
		Sync();

		mbCommandState = asserted;

		UpdateCTCTrigger0Input();
	}
}

void ATDeviceDiskDriveATR8000::OnMotorStateChanged(bool asserted) {
}

void ATDeviceDiskDriveATR8000::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	Sync();

	mReceiveShiftRegister = (c + c + 0x200) * 2 + 1;

	// The conversion fraction we need here is 512/229, but that denominator is awkward.
	// Approximate it with 2289/1024.
	mReceiveTimingAccum = 0x200;
	mReceiveTimingStep = cyclesPerBit * 2289;

	mDriveScheduler.SetEvent(1, this, kEventId_DriveReceiveBit, mpEventDriveReceiveBit);

	mpSerialPort->SetTransmitBaudRate(VDRoundToInt32(mpScheduler->GetRate().asDouble() / (double)cyclesPerBit));
}

void ATDeviceDiskDriveATR8000::OnSendReady() {
}

void ATDeviceDiskDriveATR8000::OnDiskChanged(uint32 index, bool mediaRemoved) {
	Drive& drive = mDrives[index];

	if (mediaRemoved) {
		drive.mDiskChangeState = 0;
		mDriveScheduler.SetEvent(1, this, kEventId_DriveDiskChange0 + index, drive.mpEventDriveDiskChange);
	}

	UpdateDiskStatus();
}

void ATDeviceDiskDriveATR8000::OnWriteModeChanged(uint32 index) {
	if (mSelectedDrives & (1 << index))
		UpdateWriteProtectStatus();
}

void ATDeviceDiskDriveATR8000::OnTimingModeChanged(uint32 index) {
	if (mSelectedDrives & (1 << index)) {
		bool accurateTiming = false;

		for(uint32 i=0; i<kNumDrives; ++i) {
			if (!(mSelectedDrives & (1 << i)))
				continue;

			if (mDrives[i].mpDiskInterface->IsAccurateSectorTimingEnabled())
				accurateTiming = true;
		}

		mFDC.SetAccurateTimingEnabled(accurateTiming);
	}
}

void ATDeviceDiskDriveATR8000::OnAudioModeChanged(uint32 index) {
	if (mSelectedDrives & (1 << index)) {
		bool driveSounds = false;

		for(uint32 i=0; i<kNumDrives; ++i) {
			if (!(mSelectedDrives & (1 << i)))
				continue;

			if (mDrives[i].mpDiskInterface->AreDriveSoundsEnabled())
				driveSounds = true;
		}

		mbSoundsEnabled = driveSounds;

		UpdateRotationStatus();
	}
}

void ATDeviceDiskDriveATR8000::Sync() {
	const uint32 limit = AccumSubCycles();

	mDriveScheduler.SetStopTime(limit);

	for(;;) {
		if (!mCoProc.GetCyclesLeft()) {
			uint32 tc = ATSCHEDULER_GETTIMETONEXT(&mDriveScheduler);
			if (tc <= 0)
				break;

			uint32 tc2 = mCoProc.GetTStatesPending();

			if (!tc2)
				tc2 = 1;

			if (tc > tc2)
				tc = tc2;

			ATSCHEDULER_ADVANCE_N(&mDriveScheduler, tc);
			mCoProc.AddCycles(tc);
		}

		mCoProc.Run();

		if (mCoProc.GetCyclesLeft()) {
			ScheduleImmediateResume();
			break;
		}
	}

	FlushStepNotifications();
}

void ATDeviceDiskDriveATR8000::AddTransmitEdge(bool polarity) {
	mSerialXmitQueue.AddTransmitBit(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, polarity);
}

uint8 ATDeviceDiskDriveATR8000::OnDebugReadFDC(uint32 addr) const {
	return mFDC.DebugReadByte((uint8)addr);
}

uint8 ATDeviceDiskDriveATR8000::OnReadFDC(uint32 addr) {
	return mFDC.ReadByte((uint8)addr);
}

void ATDeviceDiskDriveATR8000::OnWriteFDC(uint32 addr, uint8 val) {
	return mFDC.WriteByte((uint8)addr, val);
}

uint8 ATDeviceDiskDriveATR8000::OnReadPort(uint8 addr) {
	// IN from ports 00-7FH are handled by 1-of-8 demux U28 (74LS138).
	//
	//	00-0FH: Not connected
	//	10-1FH: Not connected
	//	20-2FH: U12 latch 2 (74LS244)
	//		Bit 7: Printer pin 21 (BUSY)
	//		Bit 6: Printer pin 23 (ERROR)
	//		Bit 5: Printer pin 25 (/BUSY)
	//		Bit 4: Printer pin 19 (/ACK)
	//	30-3FH: Not connected
	//	40-4FH: FDC
	//	50-5FH: RS-232 read
	//		Bit 7: RS-232 pin 3 (RD / received data, normal polarity)
	//		Bit 3: Jumper J11 (0 if connected)
	//		Bit 2: RS-232 pin 22 (RI / ring indicator)
	//		Bit 1: RS-232 pin 11 (SRTS), 8 (DCD), 6 (DSR), or 5 (CTS)
	//	60-6FH: Not connected
	//	70-7FH: U12 latch 1 (74LS744)
	//		Bit 7: SIO pin 5 (DATA OUT)
	//		Bit 6:
	//		Bit 5:
	//		Bit 4:
	//		Bit 3: SIO pin 10 (READY)
	//		Bit 1: SIO pin 7 (COMMAND)
	//		Bit 0:
	//
	// Ports 80-BFH read from the CTC.

	switch(addr >> 4) {
		case 2:
			return 0x30;

		case 4:
			return mFDC.ReadByte(addr & 3);

		case 5: {
			uint8 v = (uint8)~0x86;

			bool signal2;
			bool ring;
			mpSerialPort->ReadStatus(signal2, ring);
			if (!signal2)
				v += 0x02;

			if (!ring)
				v += 0x04;

			mpSerialPort->Poll();

			if (mpSerialPort->GetReceiveState())
				v += 0x80;

			return v;
		}

		case 7:
			return (mbCommandState ? 0x00 : 0x02) + 0x08 + (mReceiveShiftRegister & 1 ? 0x80 : 0x00) + 0x75;

		case 8:
		case 9:
		case 10:
		case 11:
			return mCTC.ReadByte(addr);
	}

	return 0xFF;
}

void ATDeviceDiskDriveATR8000::OnWritePort(uint8 addr, uint8 val) {
	// OUT to ports 00-7FH are handled by 1-of-8 demux U33 (74LS138).
	//
	//	00-0FH: Not connected
	//	10-1FH: Not connected
	//	20-2FH: Printer port (cleared to 0 on reset)
	//	30-3FH: Drive control
	//		Bit 0: Drive select 1
	//		Bit 1: Drive select 2
	//		Bit 2: Drive select 3
	//		Bit 3: Drive select 4
	//		Bit 4: FDC reset
	//		Bit 5: Side select
	//		Bit 6: Clock rate (0 = big (8"), 1 = small (5"))
	//		Bit 7: Density (0 = double, 1 = single)
	//	40-4FH: FDC
	//	50-5FH: U52 (74LS259?) - misc functions
	//		50/58H: SIO DATA IN
	//		51/59H: U53 to jumper block pin 8 (RS-232 TD, normal polarity)
	//		52/5AH: ROM enable/disable (0 = enabled)
	//		53/5BH: Printer strobe (active high)
	//		54/5CH: Reset for U47 (index pulse flip/flop)
	//		55/5DH: U53 to jumper block pin 2 (RS-232 DTR or RTS)
	//		56/5EH: Preset for U47 (index pulse flip/flop)
	//		57/5FH: CTC TRG0 control (1 = SIO COMMAND, 0 = SIO DATA OUT)
	//	60-6FH: Not connected
	//	70-7FH: Not connected
	//
	// Ports 80-BFH write to the CTC.

	switch(addr >> 4) {
		case 2:		// printer data latch
			mPrinterData = val;
			break;

		case 3:		// drive control
			SelectDrives(val & 15);

			{
				bool fastClock = !(val & 0x40);

				if (mbFastClock != fastClock) {
					mbFastClock = fastClock;

					UpdateFDCSpeed();
				}
			}

			mFDC.SetDensity(!(val & 0x80));
			mFDC.SetSide((val & 0x20) != 0);
			break;

		case 4:
			mFDC.WriteByte(addr & 3, val);
			break;

		case 5:
			{
				const bool state = (val & 1) != 0;

				switch(addr & 7) {
					case 0:
						if (mbDirectTransmitOutput != state) {
							mbDirectTransmitOutput = state;
							AddTransmitEdge(state);
						}
						break;

					case 1:
						mpSerialPort->SetTransmitState(state);
						break;

					case 2:
						if (mbROMEnabled != !state) {
							mbROMEnabled = !state;

							UpdateMemoryMap();
						}
						break;

					case 3:
						if (mbPrinterStrobeAsserted != state) {
							mbPrinterStrobeAsserted = state;

							if (state) {
								if (auto *printer = mParallelBus.GetChild<IATPrinterOutput>())
									printer->WriteRaw(&mPrinterData, 1);
								else {
									if (!mpPrinterOutput)
										mpPrinterOutput = GetService<IATPrinterOutputManager>()->CreatePrinterOutput();

									mpPrinterOutput->WriteRaw(&mPrinterData, 1);
								}
							}
						}
						break;

					case 4:
						mbIndexPulseDisabled = !state;
						UpdateIndexPulseMode();
						break;

					case 5:
						mpSerialPort->SetControlState(!state);
						break;

					case 6:
						mbIndexPulseEnabled = !state;
						UpdateIndexPulseMode();
						break;

					case 7:
						if (mbTrigger0SelectData != state) {
							mbTrigger0SelectData = state;

							UpdateCTCTrigger0Input();
						}
						break;
				}
			}
			break;

		case 8:
		case 9:
		case 10:
		case 11:
			mCTC.WriteByte(addr, val);
			break;
	}
}

uint8 ATDeviceDiskDriveATR8000::OnGetIntVector() {
	mCoProc.NegateIrq();

	return mPendingIntVector;
}

void ATDeviceDiskDriveATR8000::OnIntAck() {
	mCTC.AcknowledgeInterrupt();
}

void ATDeviceDiskDriveATR8000::OnHaltChange(bool halted) {
	if (halted && (mFDC.GetDrqStatus() || mFDC.GetIrqStatus()))
		mCoProc.AssertNmi();
}

void ATDeviceDiskDriveATR8000::OnCTCInterrupt(sint32 vec) {
	if (vec >= 0) {
		mPendingIntVector = vec;
		mCoProc.AssertIrq();
	}
}

void ATDeviceDiskDriveATR8000::OnFDCStep(bool inward) {
	if (!mSelectedDrives)
		return;

	bool playStepSound = false;
	bool track0 = false;
	uint32 fdcTrack = 0;

	for(uint32 i=0; i<kNumDrives; ++i) {
		if (!(mSelectedDrives & (1 << i)))
			continue;

		Drive& drive = mDrives[i];
		if (inward) {
			// step in (increasing track number)
			const uint32 trackLimit = drive.mType == kDriveType_8 ? 82*2 : 45*2;
			if (drive.mCurrentTrack < trackLimit) {
				drive.mCurrentTrack += 2;
			}

			playStepSound = true;
		} else {
			// step out (decreasing track number)
			if (drive.mCurrentTrack > 0) {
				drive.mCurrentTrack -= 2;

				playStepSound = true;
			}
		}

		if (drive.mCurrentTrack < 2)
			track0 = true;

		fdcTrack = drive.mCurrentTrack;
	}

	mFDC.SetCurrentTrack(fdcTrack, track0);

	if (playStepSound)
		PlayStepSound();
}

void ATDeviceDiskDriveATR8000::OnFDCDrq(bool active) {
	if (mCoProc.IsHalted() && active)
		mCoProc.AssertNmi();
}

void ATDeviceDiskDriveATR8000::OnFDCIrq(bool active) {
	if (mCoProc.IsHalted() && active)
		mCoProc.AssertNmi();
}

void ATDeviceDiskDriveATR8000::OnFDCMotorChange(bool enabled) {
	if (mbMotorRunning != enabled) {
		mbMotorRunning = enabled;

		mFDC.SetMotorRunning(enabled);
		UpdateRotationStatus();
	}
}

void ATDeviceDiskDriveATR8000::PlayStepSound() {
	if (!mbSoundsEnabled)
		return;

	const uint32 t = ATSCHEDULER_GETTIME(&mDriveScheduler);
	
	if (t - mLastStepSoundTime > 50000)
		mLastStepPhase = 0;

	mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep2H, 0.3f + 0.7f * cosf((float)mLastStepPhase++ * nsVDMath::kfPi));

	mLastStepSoundTime = t;
}

void ATDeviceDiskDriveATR8000::UpdateMemoryMap() {
	uintptr *VDRESTRICT readmap = mCoProc.GetReadMap();
	uintptr *VDRESTRICT writemap = mCoProc.GetWriteMap();

	ATCoProcMemoryMapView mmap(readmap, writemap);

	if (mbROMEnabled) {
		for(int i=0; i<128; i+=16)
			mmap.SetReadMem((uint32)i, 0x10, mROM);
	} else {
		mmap.SetReadMem(0x00, 0x80, mRAM);
	}
}

void ATDeviceDiskDriveATR8000::UpdateRotationStatus() {
	if (mSelectedDrives == 0) {
		mAudioPlayer.SetRotationSoundEnabled(false);
		return;
	}

	const bool driveMotorRunning = mbMotorRunning;

	for(uint32 i=0; i<kNumDrives; ++i) {
		if (mSelectedDrives & (1 << i))
			mDrives[i].mpDiskInterface->SetShowMotorActive(driveMotorRunning);
	}

	mAudioPlayer.SetRotationSoundEnabled(driveMotorRunning && mbSoundsEnabled);
}

void ATDeviceDiskDriveATR8000::UpdateDiskStatus() {
	// The ATR8000 has RDY tied to +5V, so the FDC thinks the disk is always ready.
	// Only give the FDC a disk if exactly one drive is selected, however.

	if (!mSelectedDrives || (mSelectedDrives & (mSelectedDrives - 1))) {
		mFDC.SetDiskImage(nullptr, true);
	} else {
		Drive& drive = mDrives[VDFindLowestSetBitFast(mSelectedDrives)];
		IATDiskImage *image = drive.mpDiskInterface ? drive.mpDiskInterface->GetDiskImage() : nullptr;

		mFDC.SetDiskImage(image, true);
	}

	UpdateWriteProtectStatus();
}

void ATDeviceDiskDriveATR8000::UpdateWriteProtectStatus() {
	bool wpOverride = false;

	// check if any selected drives need WP override due to disk change emulation
	if (mSelectedDrives) {
		for(uint32 i=0; i<kNumDrives; ++i) {
			if (!(mSelectedDrives & (1 << i)))
				continue;

			Drive& drive = mDrives[i];

			if ((drive.mDiskChangeState & 1) != 0) {
				wpOverride = true;
				break;
			}
		}
	}

	mFDC.SetWriteProtectOverride(wpOverride);
}

void ATDeviceDiskDriveATR8000::UpdateCTCTrigger0Input() {
	mCTC.SetInputState(0, mbTrigger0SelectData ? (mReceiveShiftRegister & 1) != 0 : mbCommandState);
}

void ATDeviceDiskDriveATR8000::UpdateIndexPulseMode() {
	// The index pulse is ORed by a D flip flop's /Q output. If both
	// preset and reset are set, the /Q output goes high, which disables
	// the index pulse.
	mFDC.SetAutoIndexPulse(mbIndexPulseEnabled && !mbIndexPulseDisabled);
}

void ATDeviceDiskDriveATR8000::UpdateFDCSpeed() {
	if (mSelectedDrives) {
		int firstDrive = VDFindLowestSetBitFast(mSelectedDrives);
		Drive& drive = mDrives[firstDrive];

		const float rpm = drive.mType == kDriveType_8 ? 360.0f : 300.0f;
		mFDC.SetSpeeds(rpm, 1.0f, mbFastClock);
	}
}

void ATDeviceDiskDriveATR8000::SelectDrives(uint8 mask) {
	mask &= mAvailableDrives;

	if (mSelectedDrives == mask)
		return;

	const uint8 deactivatedDrives = mSelectedDrives & ~mask;

	if (deactivatedDrives) {
		for(uint32 i=0; i<kNumDrives; ++i) {
			if (!(deactivatedDrives & (1 << i)))
				continue;

			Drive& oldDrive = mDrives[i];

			oldDrive.mpDiskInterface->SetShowMotorActive(false);
		}
	}

	mSelectedDrives = mask;

	if (mask) {
		int firstDrive = VDFindLowestSetBitFast(mask);

		Drive& drive = mDrives[firstDrive];
		mFDC.SetDiskInterface(drive.mpDiskInterface);
		mFDC.SetCurrentTrack(drive.mCurrentTrack, drive.mCurrentTrack == 0);
		mFDC.SetSideMapping(ATFDCEmulator::SideMapping::Side2Forward, drive.mType == kDriveType_8 ? 77 : 40);

		UpdateFDCSpeed();
		OnWriteModeChanged(firstDrive);
		OnTimingModeChanged(firstDrive);
		OnAudioModeChanged(firstDrive);
	} else {
		mFDC.SetDiskInterface(nullptr);
		mFDC.SetCurrentTrack(20, false);
	}

	UpdateDiskStatus();
	UpdateRotationStatus();
}
