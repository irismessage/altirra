//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <array>
#include <vd2/system/binary.h>
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include <vd2/system/math.h>
#include <at/atcore/audiosource.h>
#include <at/atcore/configvar.h>
#include <at/atcore/crc.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/logging.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/wraptime.h>
#include "audiosampleplayer.h"
#include "diskdrive815.h"
#include "memorymanager.h"
#include "firmwaremanager.h"
#include "debuggerlog.h"

extern ATLogChannel g_ATLCDiskEmu;
extern ATLogChannel g_ATLCDisk;
extern ATLogChannel g_ATLCFDC;
extern ATLogChannel g_ATLCFDCWTData;

////////////////////////////////////////////////////////////////////////////////

static constexpr std::array<uint16, 256> kATMFMBitSpreadTable = 
	[]() -> std::array<uint16, 256> {
		std::array<uint16, 256> table {};

		for(int i=0; i<256; ++i) {
			uint16 v = 0;

			if (i & 0x01) v += 0x0001;
			if (i & 0x02) v += 0x0004;
			if (i & 0x04) v += 0x0010;
			if (i & 0x08) v += 0x0040;
			if (i & 0x10) v += 0x0100;
			if (i & 0x20) v += 0x0400;
			if (i & 0x40) v += 0x1000;
			if (i & 0x80) v += 0x4000;

			table[i] = v;
		}

		return table;
	}();

void ATInsertTrackBits(uint8 *dst, uint32 dstBitOffset, const uint8 *src, uint32 srcBitOffset, uint32 numBits) {
	if (!numBits)
		return;

	// byte align the destination offset
	uint8 firstByteMask = 0xFF;
	uint32 startShift = dstBitOffset & 7;
	if (startShift) {
		dstBitOffset -= startShift;
		srcBitOffset -= startShift;
		firstByteMask >>= startShift;

		numBits += startShift;
	}

	// check if we have a partial write on the end
	uint8 lastByteMask = 0xFF;
	uint32 endBits = numBits & 7;

	if (endBits)
		lastByteMask = 0xFF ^ (lastByteMask >> endBits);

	// compute number of bytes to write, including a masked first byte but not a masked last byte
	uint32 numBytes = numBits >> 3;

	// compute bit shift
	int bitShift = 8 - (srcBitOffset & 7);

	// adjust pointers
	dst += dstBitOffset >> 3;
	src += srcBitOffset >> 3;

	// if we have a masked first byte, do it now
	if (firstByteMask != 0xFF) {
		uint8 v = (uint8)(VDReadUnalignedBEU16(src++) >> bitShift);

		if (--numBytes == 0) {
			// special case -- first and last byte masks overlap
			*dst = *dst ^ ((*dst ^ v) & firstByteMask & lastByteMask);
			return;
		}

		*dst = *dst ^ ((*dst ^ v) & firstByteMask);
		++dst;
	}

	// copy over whole bytes
	if (bitShift == 8) {
		memcpy(dst, src, numBytes);
		dst += numBytes;
		src += numBytes;
	} else {
		while(numBytes--)
			*dst++ = (uint8)(VDReadUnalignedBEU16(src++) >> bitShift);
	}

	// do final partial byte
	if (endBits) {
		const uint8 v = (uint8)(VDReadUnalignedBEU16(src) >> bitShift);

		*dst = *dst ^ ((*dst ^ v) & lastByteMask);
	}
}

void ATWriteRawMFMToTrack(uint8 *track, uint32 trackOffsetBits, uint32 trackLenBits, const uint8 *clockMaskBits, const uint8 *dataBits, uint32 numBytes, uint8 *mfmBuffer) {
	mfmBuffer[0] = 0;
	mfmBuffer[1] = 0;
	mfmBuffer[numBytes * 2 + 2] = 0;
	mfmBuffer[numBytes * 2 + 3] = 0;

	// encode to MFM
	uint8 lastByte = 0xFF;

	for(uint32 i = 0; i < numBytes; ++i) {
		const uint8 d = dataBits[i];
		const uint8 m = clockMaskBits[i];

		const uint16 mfmDataBits = kATMFMBitSpreadTable[d];
		const uint16 mfmClockBits = kATMFMBitSpreadTable[(uint8)(~(d | ((d >> 1) + (lastByte << 7))) & m)];

		VDWriteUnalignedBEU16(&mfmBuffer[2 + i*2], mfmDataBits + (mfmClockBits << 1));

		lastByte = d;
	}

	// check if the write would wrap around the track, and thus we need a split write
	const uint32 numBits = numBytes * 16;
	if (trackLenBits - trackOffsetBits < numBits) {
		// yes - split
		uint32 numBits1 = trackLenBits - trackOffsetBits;
		uint32 numBits2 = numBits - numBits1;

		ATInsertTrackBits(track, trackOffsetBits, mfmBuffer, 16, numBits1);
		ATInsertTrackBits(track, 0, mfmBuffer, 16 + numBits1, numBits2);
	} else {
		// no - single write
		ATInsertTrackBits(track, trackOffsetBits, mfmBuffer, 16, numBits); 
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATCreateDeviceDiskDrive815(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDeviceDiskDrive815> p(new ATDeviceDiskDrive815);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefDiskDrive815 = {
	"diskdrive815",
	"diskdrive815",
	L"815 disk drive (full emulation)",
	ATCreateDeviceDiskDrive815
};

ATDeviceDiskDrive815::ATDeviceDiskDrive815()
	: mCoProc(false, false)
{
	mBreakpointsImpl.BindBPHandler(mCoProc);
	mBreakpointsImpl.SetStepHandler(this);
	mBreakpointsImpl.SetBPsChangedHandler([this](const uint16 *pc) { mCoProc.OnBreakpointsChanged(pc); });

	const VDFraction& clockRate = VDFraction(2000000, 1);
	mDriveScheduler.SetRate(clockRate);

	memset(&mDummyRead, 0xFF, sizeof mDummyRead);

	mTargetProxy.mpDriveScheduler = &mDriveScheduler;
	mTargetProxy.Init(mCoProc);
	InitTargetControl(mTargetProxy, clockRate.asDouble(), kATDebugDisasmMode_6502, &mBreakpointsImpl, this);

	mSerialCmdQueue.SetOnDriveCommandStateChanged(
		[this](bool asserted) {
			// RIOT1 PB6 <- SIO COMMAND (1 = asserted)
			mRIOT1.SetInputB(asserted ? 0xFF : 0x00, 0x40);
		}
	);
}

ATDeviceDiskDrive815::~ATDeviceDiskDrive815() {
}

void *ATDeviceDiskDrive815::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceDiskDrive::kTypeID: return static_cast<IATDeviceDiskDrive *>(this);
		case IATDeviceSIO::kTypeID: return static_cast<IATDeviceSIO *>(this);
		case IATDeviceAudioOutput::kTypeID: return static_cast<IATDeviceAudioOutput *>(&mAudioPlayer);
		case ATRIOT6532Emulator::kTypeID: return &mRIOT1;
	}

	return ATDiskDriveDebugTargetControl::AsInterface(iid);
}

void ATDeviceDiskDrive815::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefDiskDrive815;
}

void ATDeviceDiskDrive815::GetSettingsBlurb(VDStringW& buf) {
	buf.sprintf(L"D%u-%u:", mDriveId + 1, mDriveId + 2);
}

void ATDeviceDiskDrive815::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("id", mDriveId);
	settings.SetBool("accurate_invert", mbAccurateInvert);
}

bool ATDeviceDiskDrive815::SetSettings(const ATPropertySet& settings) {
	uint32 newDriveId = settings.GetUint32("id", mDriveId) & 6;

	if (mDriveId != newDriveId) {
		mDriveId = newDriveId;
		return false;
	}

	mbAccurateInvert = settings.GetBool("accurate_invert", false);
	return true;
}

void ATDeviceDiskDrive815::Init() {
	mSerialXmitQueue.Init(mpScheduler, mpSIOMgr);
	mSerialCmdQueue.Init(&mDriveScheduler, mpSIOMgr);

	// The 815's memory map:
	//
	//	0000-00FF	RIOT memory
	//	0100-017F	SSDA
	//	0180-01FF	RIOT memory
	//	0200-027F	RIOT registers
	//	0280-02FF	RIOT registers
	//	0380-03FF	RIOT registers
	//	0800-0FFF	ROM
	//	1000-17FF	ROM
	//	1800-1FFF	ROM mirror

	uintptr *readmap = mCoProc.GetReadMap();
	uintptr *writemap = mCoProc.GetWriteMap();

	// set up FDC/6810 handlers
	mReadNodeSSDARAM.mpThis = this;
	mWriteNodeSSDARAM.mpThis = this;

	mReadNodeSSDARAM.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
		auto *thisptr = (ATDeviceDiskDrive815 *)thisptr0;

		if (addr & 0x80)
			return thisptr->mRAM[0x100 + (addr & 0x7F)];

		return thisptr->ReadSSDA(addr);
	};

	mReadNodeSSDARAM.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
		auto *thisptr = (ATDeviceDiskDrive815 *)thisptr0;

		if (addr & 0x80)
			return thisptr->mRAM[0x100 + (addr & 0x7F)];

		return thisptr->DebugReadSSDA(addr);
	};

	mWriteNodeSSDARAM.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
		auto *thisptr = (ATDeviceDiskDrive815 *)thisptr0;

		if (addr & 0x80) {
			thisptr->mRAM[0x100 + (addr & 0x7F)] = val;
			return;
		}

		thisptr->WriteSSDA(addr, val);
	};

	// set up RIOT register handlers
	mReadNodeRIOT1Registers.mpThis = this;
	mReadNodeRIOT23Registers.mpThis = this;
	mWriteNodeRIOT1Registers.mpThis = this;
	mWriteNodeRIOT23Registers.mpThis = this;

	mReadNodeRIOT1Registers.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
		ATDeviceDiskDrive815 *thisptr = (ATDeviceDiskDrive815 *)thisptr0;

		// We need to update the receive FIFO so that sync status is updated.
		thisptr->UpdateSSDAReceiveFIFO();

		//g_ATLCDiskEmu("Reading RIOT1 at time %08X, PC=%04X\n", thisptr->mDriveScheduler.GetTick(), thisptr->mCoProc.GetPC());

		return thisptr->mRIOT1.ReadByte((uint8)addr);
	};

	mReadNodeRIOT23Registers.mpRead = [](uint32 addr, void *thisptr0) -> uint8 {
		ATDeviceDiskDrive815 *thisptr = (ATDeviceDiskDrive815 *)thisptr0;

		if (addr & 0x80)
			return thisptr->mRIOT2.ReadByte((uint8)addr);
		else
			return thisptr->mRIOT3.ReadByte((uint8)addr);
	};

	mReadNodeRIOT1Registers.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
		ATDeviceDiskDrive815 *thisptr = (ATDeviceDiskDrive815 *)thisptr0;

		// We need to update the receive FIFO so that sync status is updated.
		thisptr->UpdateSSDAReceiveFIFO();

		return thisptr->mRIOT1.DebugReadByte((uint8)addr);
	};

	mReadNodeRIOT23Registers.mpDebugRead = [](uint32 addr, void *thisptr0) -> uint8 {
		ATDeviceDiskDrive815 *thisptr = (ATDeviceDiskDrive815 *)thisptr0;

		if (addr & 0x80)
			return thisptr->mRIOT2.DebugReadByte((uint8)addr);
		else
			return thisptr->mRIOT3.DebugReadByte((uint8)addr);
	};

	mWriteNodeRIOT1Registers.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
		((ATDeviceDiskDrive815 *)thisptr0)->OnRIOT1RegisterWrite(addr, val);
	};

	mWriteNodeRIOT23Registers.mpWrite = [](uint32 addr, uint8 val, void *thisptr0) {
		ATDeviceDiskDrive815 *thisptr = (ATDeviceDiskDrive815 *)thisptr0;

		if (addr & 0x80)
			thisptr->OnRIOT2RegisterWrite(addr, val);
		else
			thisptr->OnRIOT3RegisterWrite(addr, val);
	};

	// initialize memory map
	ATCoProcMemoryMapView mmapView(readmap, writemap, mCoProc.GetTraceMap());

	mmapView.Clear(mDummyRead, mDummyWrite);
	mmapView.SetMemory(0x00, 0x01, mRAM);
	mmapView.SetHandlers(0x01, 0x01, mReadNodeSSDARAM, mWriteNodeSSDARAM);
	mmapView.SetHandlers(0x02, 0x01, mReadNodeRIOT23Registers, mWriteNodeRIOT23Registers);
	mmapView.SetHandlers(0x03, 0x01, mReadNodeRIOT1Registers, mWriteNodeRIOT1Registers);

	mmapView.SetReadMem(0x08, 0x08, mROM);
	mmapView.SetReadMem(0x10, 0x08, mROM + 0x800);
	mmapView.SetReadMem(0x18, 0x08, mROM + 0x800);
	mmapView.MirrorFwd(0x20, 0xE0, 0x00);

	mRIOT1.Init(&mDriveScheduler);
	mRIOT2.Init(&mDriveScheduler);
	mRIOT3.Init(&mDriveScheduler);
	mRIOT1.Reset();
	mRIOT2.Reset();
	mRIOT3.Reset();

	// Set port B bit 1 (/READY) and clear bit 6 (COMMAND) and 7 (/DATAOUT)
	mRIOT1.SetInputB(0x00, 0xC2);

	// Set drive select bits
	static constexpr uint8 kDriveSelectCodes[4]={
		0x81, 0x80, 0x01, 0x00
	};

	mRIOT3.SetInputA(kDriveSelectCodes[(mDriveId & 6) >> 1], 0x81);

	OnDiskChanged(false);

	OnWriteModeChanged();
	OnTimingModeChanged();
	OnAudioModeChanged();

	UpdateRotationStatus();
}

void ATDeviceDiskDrive815::Shutdown() {
	mAudioPlayer.Shutdown();
	mSerialXmitQueue.Shutdown();
	mSerialCmdQueue.Shutdown();

	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
	ShutdownTargetControl();

	mpFwMgr = nullptr;

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}

	for(ATDiskInterface *&diskIf : mpDiskInterfaces) {
		diskIf->SetShowMotorActive(false);
		diskIf->SetShowActivity(false, 0);
		diskIf->RemoveClient(this);
		diskIf = nullptr;
	}

	mpDiskDriveManager = nullptr;
}

uint32 ATDeviceDiskDrive815::GetComputerPowerOnDelay() const {
	return 0;
}

void ATDeviceDiskDrive815::WarmReset() {
	// If the computer resets, its transmission is interrupted.
	mDriveScheduler.UnsetEvent(mpEventDriveReceiveBit);
}

void ATDeviceDiskDrive815::ComputerColdReset() {
	WarmReset();
}

void ATDeviceDiskDrive815::PeripheralColdReset() {
	memset(mRAM, 0, sizeof mRAM);

	mRIOT1.Reset();
	mRIOT2.Reset();
	mRIOT3.Reset();

	mSerialXmitQueue.Reset();

	ResetSSDA();
	
	// put receive clock into reset status
	mRIOT1.SetInputA(0x00, 0x80);
	mReceiveClockState = 0;
	mbReceiveClockReset = true;

	ResetDiskWrite();

	// start the disk drive on a track other than 0/20/39, just to make things interesting
	mCurrentTrack[0] = 20;
	mCurrentTrack[1] = 20;

	mCoProc.ColdReset();

	ResetTargetControl();

	// need to update motor and sound status, since the 810 starts with the motor on
	UpdateRotationStatus();

	WarmReset();
}

void ATDeviceDiskDrive815::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMgr = fwman;

	ReloadFirmware();
}

bool ATDeviceDiskDrive815::ReloadFirmware() {
	const uint64 id = mpFwMgr->GetFirmwareOfType(kATFirmwareType_815, true);
	
	const vduint128 oldHash = VDHash128(mROM, sizeof mROM);

	uint32 len = 0;
	mpFwMgr->LoadFirmware(id, mROM, 0, 4096, nullptr, &len, nullptr, nullptr, &mbFirmwareUsable);

	mCoProc.InvalidateTraceCache();

	const vduint128 newHash = VDHash128(mROM, sizeof mROM);

	return oldHash != newHash;
}

const wchar_t *ATDeviceDiskDrive815::GetWritableFirmwareDesc(uint32 idx) const {
	return nullptr;
}

bool ATDeviceDiskDrive815::IsWritableFirmwareDirty(uint32 idx) const {
	return false;
}

void ATDeviceDiskDrive815::SaveWritableFirmware(uint32 idx, IVDStream& stream) {
}

ATDeviceFirmwareStatus ATDeviceDiskDrive815::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

void ATDeviceDiskDrive815::InitDiskDrive(IATDiskDriveManager *ddm) {
	mpDiskDriveManager = ddm;
	mpDiskInterfaces[0] = ddm->GetDiskInterface(mDriveId);
	mpDiskInterfaces[0]->AddClient(this);
	mpDiskInterfaces[1] = ddm->GetDiskInterface(mDriveId + 1);
	mpDiskInterfaces[1]->AddClient(this);
}

ATDeviceDiskDriveInterfaceClient ATDeviceDiskDrive815::GetDiskInterfaceClient(uint32 index) {
	return index ? ATDeviceDiskDriveInterfaceClient{} : ATDeviceDiskDriveInterfaceClient{ this, mDriveId };
}

void ATDeviceDiskDrive815::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDeviceDiskDrive815::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DriveReceiveBit) {
		const bool newState = (mReceiveShiftRegister & 1) != 0;

		mReceiveShiftRegister >>= 1;
		mpEventDriveReceiveBit = nullptr;

		if (mReceiveShiftRegister) {
			mReceiveTimingAccum += mReceiveTimingStep;
			mpEventDriveReceiveBit = mDriveScheduler.AddEvent(mReceiveTimingAccum >> 10, this, kEventId_DriveReceiveBit);
			mReceiveTimingAccum &= 0x3FF;
		}

		// RIOT1 PB7 <- SIO DATA OUT (inverted)
		mRIOT1.SetInputB(newState ? 0x00 : 0x80, 0x80);
	} else
		return ATDiskDriveDebugTargetControl::OnScheduledEvent(id);
}

void ATDeviceDiskDrive815::OnCommandStateChanged(bool asserted) {
	if (mbCommandState != asserted) {
		mbCommandState = asserted;

		// Convert computer time to device time.
		//
		// We have a problem here because transmission is delayed by a byte time but we don't
		// necessarily know that delay when the command line is dropped. The 815 has strict
		// requirements for the command line pulse because it requires the line to still be
		// asserted after the end of the last byte. To solve this, we assert /COMMAND
		// immediately but stretch the deassert a bit.

		const uint32 commandLatency = asserted ? 0 : 400;

		mSerialCmdQueue.AddCommandEdge(MasterTimeToDriveTime() + commandLatency, asserted);
	}
}

void ATDeviceDiskDrive815::OnMotorStateChanged(bool asserted) {
}

void ATDeviceDiskDrive815::OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) {
	Sync();

	mReceiveShiftRegister = c + c + 0x200;

	mReceiveTimingAccum = 0x200;
	mReceiveTimingStep = cyclesPerBit * 1144;

	mDriveScheduler.SetEvent(1, this, kEventId_DriveReceiveBit, mpEventDriveReceiveBit);
}

void ATDeviceDiskDrive815::OnSendReady() {
}

void ATDeviceDiskDrive815::OnDiskChanged(bool mediaRemoved) {
	// don't process this if we're the one initiating it
	if (mInhibitDiskChangeInvalidation)
		return;

	UpdateDiskStatus();

	mLastMaterializedDrive = -1;
	mLastMaterializedTrack = -1;
}

void ATDeviceDiskDrive815::OnWriteModeChanged() {
	UpdateWriteProtectStatus();
}

void ATDeviceDiskDrive815::OnTimingModeChanged() {
}

void ATDeviceDiskDrive815::OnAudioModeChanged() {
	mbSoundsEnabled = mpDiskInterfaces[0]->AreDriveSoundsEnabled();

	UpdateRotationStatus();
}

bool ATDeviceDiskDrive815::IsImageSupported(const IATDiskImage& image) const {
	const auto& geo = image.GetGeometry();

	if (geo.mbHighDensity)
		return false;

	if (!geo.mbMFM)
		return false;

	if (geo.mSectorSize != 256)
		return false;

	if (geo.mSectorsPerTrack > 18)
		return false;

	if (geo.mTrackCount > 40)
		return false;

	if (geo.mSideCount > 1)
		return false;

	return true;
}

void ATDeviceDiskDrive815::Sync() {
	uint32 newDriveCycleLimit = AccumSubCycles();

	bool ranToCompletion = true;

	VDASSERT(mDriveScheduler.mNextEventCounter >= 0xFF000000);
	if (ATSCHEDULER_GETTIME(&mDriveScheduler) - newDriveCycleLimit >= 0x80000000) {
		mDriveScheduler.SetStopTime(newDriveCycleLimit);
		ranToCompletion = mCoProc.Run(mDriveScheduler);

		VDASSERT(ATWrapTime{ATSCHEDULER_GETTIME(&mDriveScheduler)} <= newDriveCycleLimit);
	}

	if (!ranToCompletion)
		ScheduleImmediateResume();

	FlushStepNotifications();
}

void ATDeviceDiskDrive815::AddTransmitEdge(bool polarity) {
	mSerialXmitQueue.AddTransmitBit(DriveTimeToMasterTime() + mSerialXmitQueue.kTransmitLatency, polarity);
}

// MC6852 Synchronous Serial Data Adapter:
//
// $0100 (R)	Status
// $0100 (W)	Control 1
// $0101 (R)	Receive Data FIFO
// $0101 (W)	Control 2/3, Sync Code, Transmit Data FIFO
//
// Note that the SSDA is hooked up with the data bus bit-reversed, so all register
// bits are in reversed order as well. From the 6507's POV:
//
// Status:
//	D7=1	Receive data available
//	D6=1	Transmitter data register available
//	D5		/DCD
//	D4		/CTS
//	D3=1	Transmitter underflow
//	D2=1	Receiver overrun
//	D1=1	Receiver parity error
//	D0=1	IRQ
//
// Control 1:
//	D7=1	Receiver reset
//	D6=1	Transmitter reset
//	D5=1	Strip sync chars
//	D4=1	Clear sync
//	D3=1	Transmitter interrupt enable
//	D2=1	Receiver interrupt enable
//	D1		Address control 1
//	D0		Address control 2
//
// Control 2 (AC1=0, AC2=0):
//	D7		Peripheral control 1
//	D6		Peripheral control 2
//	D5		1-byte/2-byte transfer
//	D4		Word length select 1
//	D3		Word length select 2
//	D2		Word length select 3
//	D1=1	Transmit sync code on underflow
//	D0=1	Error interrupt enable
//
// Control 3 (AC1=1, AC2=0):
//	D7		External/internal sync mode control
//	D6		One-sync-character / two-sync-character mode control
//	D5		Clear /CTS status
//	D4		Clear transmitter underflow status
//	D3-D0	Not used
//
// Sync register (AC1=0, AC2=1)
// Transmit data FIFO (AC1=1, AC2=1)

uint8 ATDeviceDiskDrive815::DebugReadSSDA(uint32 addr) {
	UpdateSSDAReceiveFIFO();

	if (addr & 1)
		return mReceiveFIFO[mReceiveFIFOStart];
	else {
		uint8 status = 0x00;

		// receiver data available
		if (mReceiveFIFOLength >= (mbSSDATwoByteMode ? 2 : 1))
			status |= 0x80;

		// transmit data register available
		const uint64 t = mDriveScheduler.GetTick64();
		if (t + (mbSSDATwoByteMode ? 32 : 64) >= mTransmitUnderflowTime)
			status |= 0x40;

		// transmitter underflow
		if (t >= mTransmitUnderflowTime)
			status |= 0x08;

		return status;
	}
}

uint8 ATDeviceDiskDrive815::ReadSSDA(uint32 addr) {
	if (addr & 1) {
		UpdateSSDAReceiveFIFO();

		const uint8 v = mReceiveFIFO[mReceiveFIFOStart];

		if (mReceiveFIFOLength) {
			--mReceiveFIFOLength;

			mReceiveFIFOStart = (mReceiveFIFOStart + 1) & 3;
		}

		return v;
	} else
		return DebugReadSSDA(addr);
}

void ATDeviceDiskDrive815::WriteSSDA(uint32 addr, uint8 value) {
	if (addr & 1) {
		switch(mSSDARegisterAddr) {
			case SSDARegisterAddr::Control2:
				mbSSDATwoByteMode = !(value & 0x20);

				if (bool newSyncOutputEnabled = (value & 0xC0) == 0x80; mbSSDASyncOutputEnabled != newSyncOutputEnabled) {
					UpdateSSDAReceiveFIFO();

					mbSSDASyncOutputEnabled = newSyncOutputEnabled;
				}

				break;

			case SSDARegisterAddr::Control3:
				break;

			case SSDARegisterAddr::Sync:
				if (mSSDASyncValue != value) {
					if (mSSDASyncState < 2)
						UpdateSSDAReceiveFIFO();

					mSSDASyncValue = value;
				}
				break;

			case SSDARegisterAddr::Transmit:
				OnWriteDiskByte(value);
				break;
		}
	} else {
		const bool newRxReset = (value & 0x80) != 0;
		if (mbSSDARxReset != newRxReset) {
			UpdateSSDAReceiveFIFO();

			//if (mReceiveClockState == 2)
			//	g_ATLCDiskEmu("Resetting SSDA receive at bitPos %d, dt %08X, PC=%04X\n", mHeadPositionLastBitPos, mDriveScheduler.GetTick(), mCoProc.GetPC());

			mReceiveFIFOStart = 0;
			mReceiveFIFOLength = 0;
			mbSSDARxReset = newRxReset;
			mSSDASyncState = 0;
		}

		if (bool newSyncEnabled = !(value & 0x10); mbSSDASyncEnabled != newSyncEnabled) {
			UpdateSSDAReceiveFIFO();

			mbSSDASyncEnabled = newSyncEnabled;
			if (newSyncEnabled)
				mSSDASyncState = 0;
		}

		mSSDARegisterAddr = (SSDARegisterAddr)(value & 3);
	}
}

static const std::array<uint8, 0x56> kATPackBitsTable =
	[]() -> std::array<uint8, 0x56> {
		std::array<uint8, 0x56> table {};

		for (int i=0; i<0x56; ++i) {
			uint8 v = 0;

			if (i & 0x01) v += 0x01;
			if (i & 0x04) v += 0x02;
			if (i & 0x10) v += 0x04;
			if (i & 0x40) v += 0x08;

			table[i] = v;
		}

		return table;
	} ();

uint8 ATPackBits8To4(uint8 v) {
	return kATPackBitsTable[v & 0x55];
}

uint8 ATPackBits16To8(uint8 hi, uint8 lo) {
	return (kATPackBitsTable[hi & 0x55] << 4) + kATPackBitsTable[lo & 0x55];
}

uint8 ATPackBits16To8(uint16 v) {
	return (kATPackBitsTable[(v >> 8) & 0x55] << 4) + kATPackBitsTable[v & 0x55];
}

void ATDeviceDiskDrive815::OnWriteDiskByte(uint8 v) {
	const uint64 t = mDriveScheduler.GetTick64();

	if (t + 64 < mTransmitUnderflowTime) {
		// FIFO is full!
		return;
	}

	if (mTransmitUnderflowTime < t)
		mTransmitUnderflowTime = t;

	mTransmitUnderflowTime += 32;

	++mWriteDataLen;

	mWriteData[mWriteDataIndex] = v;
	if (++mWriteDataIndex >= kTrackByteLen)
		mWriteDataIndex = 0;

	if (mTransmitLastByte == 0x44 && v == 0x89)
		mTransmitState = 1;

	if (mTransmitState == 1) {
		const uint16 mfm = ((uint32)mTransmitLastByte << 8) + v;

		if (mTransmitCurrentWord != mfm) {
			FlushDiskWrite();
			mTransmitCurrentWord = mfm;
		}

		++mTransmitCurrentCount;
	}

	mTransmitState ^= 1;

	mTransmitLastByte = v;
}

void ATDeviceDiskDrive815::StartDiskRead() {
	const uint64 t64 = mDriveScheduler.GetTick64();
	mHeadPositionLastDriveCycle = t64;

	const int readingDrive = !(mRIOT2.ReadOutputA() & 0x10) ? 1 : 0;
	mHeadPositionLastBitPos = GetCurrentBitPos(t64, readingDrive);
	mHeadPositionLastDriveCycle = (uint32)t64;

	// materialize the track if necessary in case the disk was changed
	MaterializeTrack(readingDrive, mCurrentTrack[readingDrive]);
}

void ATDeviceDiskDrive815::StartDiskWrite() {
	mWriteDrive = (mRIOT2.ReadOutputB() & 0xC0) != 0xC0;

	mWriteDataStartBitPos = GetCurrentBitPos(mDriveScheduler.GetTick64(), mWriteDrive);
	mWriteDataIndex = 0;
	mWriteDataLen = 0;
}

void ATDeviceDiskDrive815::FlushDiskWrite() {
	if (mTransmitCurrentCount) {
		if (g_ATLCFDCWTData.IsEnabled()) {
			const uint8 clock = ATPackBits16To8(mTransmitCurrentWord >> 1);
			const uint8 data = ATPackBits16To8(mTransmitCurrentWord);

			g_ATLCFDCWTData("Write %3u x $%04X (%02X/%02X)\n", mTransmitCurrentCount, mTransmitCurrentWord, clock, data);
		}

		mTransmitCurrentCount = 0;
	}
}

void ATDeviceDiskDrive815::ResetDiskWrite() {
	mTransmitState = 0;
	mTransmitLastByte = 0;
	mTransmitCurrentWord = 0;
	mTransmitCurrentCount = 0;
}

void ATDeviceDiskDrive815::EndDiskWrite() {
	// If we don't have a materialized track, exit immediately -- disk was
	// changed during the write (!).
	if (mLastMaterializedDrive < 0)
		return;

	const uint32 drive = mLastMaterializedDrive;
	const uint32 track = mLastMaterializedTrack;

	// Check if the disk interface is write protected and block the write if
	// so -- the firmware should already do this, but the 815 hardware will
	// lock out the write/erase heads as well.
	ATDiskInterface *diskIf = mpDiskInterfaces[mWriteDrive];

	if (!diskIf->IsDiskWritable())
		return;

	// Fetch the disk image -- if there isn't one, the write is going nowhere
	// and we don't need to process it
	IATDiskImage *image = mpDiskInterfaces[mWriteDrive]->GetDiskImage();
	if (!image)
		return;

	// Fetch the disk geometry
	const ATDiskGeometryInfo& geo = image->GetGeometry();

	// Check if we wrapped the write buffer. If so, adjust the start bit position
	// and rotate the buffer to make our life easier.
	if (mWriteDataLen > kTrackByteLen) {
		mWriteDataStartBitPos += (mWriteDataLen - kTrackByteLen) << 3;
		mWriteDataLen = kTrackByteLen;

		std::rotate(mWriteData, mWriteData + mWriteDataIndex, mWriteData + kTrackByteLen);
	}

	// Scan forward and look for address and data field headers.
	struct CapturedSector {
		uint32 mAddressFieldStart;	// MFM byte offset of IDAM -- 0 if only DAM seen
		uint32 mDataFieldStart;		// MFM byte offset of data field start after DAM -- 0 if only IDAM seen
		uint32 mVirtualSector;
		uint32 mPhysicalSector;		// psec in image -- 0 if only IDAM seen
		uint8 mDataAddressMark;		// data address mark code ($F8-FB) -- 0 if only IDAM seen
		uint8 mSectorSizeCode;		// sector size code in address header (0-3 for 128-1024b)
		uint8 mSectorNumber;		// sector number within track (1-18) -- 0 if only DAM seen
	};

	vdfastvector<CapturedSector> capturedSectors;

	bool doingFormat = false;
	int state = 0;
	uint32 lastIDAM = 0;

	for(uint32 i = 0; i < mWriteDataLen; ++i) {
		const uint8 c = mWriteData[i];

		switch(state) {
			case 0:		// --
				if (c == 0x44)
					state = 1;
				break;

			case 1:		// $44xx
				if (c == 0x89)
					state = 2;
				else if (c != 0x44)
					state = 1;
				break;

			case 2:		// $4489
				if (c == 0x44)
					state = 3;
				else
					state = 0;
				break;

			case 3:		// $4489 $44xx
				if (c == 0x89)
					state = 4;
				else
					state = 0;
				break;

			case 4:		// $4489 $4489
				if (c == 0x44)
					state = 5;
				else
					state = 0;
				break;

			case 5:		// $4489 $4489 $44xx
				if (c == 0x89)
					state = 6;
				else
					state = 0;
				break;

			case 6:		// $4489 $4489 $4489
				if (c == 0x55) {
					// possible start to DAM/IDAM
					state = 7;
				} else {
					state = 0;
				}
				break;

			case 7:		// $4489 $4489 $4489 $55xx
				if (c == 0x54) {
					// IDAM detected -- see if we have enough room in the track for a valid address field
					if (mWriteDataLen - i >= 14) {
						// decode the raw MFM
						uint8 addressField[6];

						for(uint32 j=0; j<6; ++j)
							addressField[j] = ATPackBits16To8(mWriteData[i + 2*j + 1], mWriteData[i + 2*j + 2]);

						// check if the track and side matches
						if (addressField[0] == track && addressField[1] == 0) {
							// check if the CRC matches
							static constexpr uint16 kCRC16_SyncIDAM = 0xB230;

							if (VDReadUnalignedBEU16(&addressField[4]) == ATComputeCRC16(kCRC16_SyncIDAM, addressField, 4)) {
								const uint8 sector = addressField[2];

								// discard if sector is invalid
								if (sector) {
									// mark that we've seen an IDAM, so we can try to match it against the next
									// DAM
									lastIDAM = i;

									// if we've seen an IDAM, we're formatting
									doingFormat = true;

									// add new sector entry with header info but no data payload (will be filled in
									// if we see a DAM)
									CapturedSector& cs = capturedSectors.push_back();
									cs.mPhysicalSector = 0;
									cs.mVirtualSector = 0;
									cs.mSectorNumber = sector;
									cs.mAddressFieldStart = i;
									cs.mDataFieldStart = 0;
									cs.mDataAddressMark = 0;
									cs.mSectorSizeCode = addressField[3] & 3;
								}
							}
						}

						// skip the bytes we pre-decoded
						i += 6;
					}
				} else if (c == 0x4C || c == 0x49 || c == 0x44 || c == 0x45) {
					bool haveSector = false;

					// if we have seen an IDAM, try to match it -- needs to be within 43 bytes according
					// to the spec (84 + 14 MFM bytes from IDAM position for us)
					if (lastIDAM && i - lastIDAM < 98) {
						// yup -- adopt the last sector in the list, which will match the recently found IDAM
						haveSector = true;
					} else {
						// no IDAM, so maybe it is just a write-sector -- try to find a rendered sector that
						// contains this DAM; the track is not a integral number of bytes, so we must wrap
						// the bit pos
						const uint32 idamBitPos1 = (mWriteDataStartBitPos + i * 8) % kTrackBitLen;
						const uint32 idamBitPos2 = idamBitPos1 + kTrackBitLen;

						for(const SectorRenderInfo& sri : mRenderedSectors) {
							// check for overlap -- note that the sector may span the index, so we must check
							// two locations
							if ((sri.mStartBitPos <= idamBitPos1 && idamBitPos1 < sri.mEndBitPos)
								|| (sri.mStartBitPos <= idamBitPos2 && idamBitPos2 < sri.mEndBitPos))
							{
								// this should be the case, but it's cheap to double check
								if (sri.mPhysicalSector < image->GetPhysicalSectorCount()) {
									auto& cs = capturedSectors.push_back();
									cs.mAddressFieldStart = 0;
									cs.mVirtualSector = sri.mVirtualSector;
									cs.mPhysicalSector = sri.mPhysicalSector;
									cs.mSectorSizeCode = sri.mSectorSizeCode;
									cs.mSectorNumber = 0;
									haveSector = true;
								}
								break;
							}
						}
					}

					if (haveSector) {
						// decode the DAM
						const uint8 dam = 0xF0 + ATPackBits8To4(c);

						auto& cs = capturedSectors.back();

						cs.mDataAddressMark = dam;
						cs.mDataFieldStart = i+1;
					} else {
						g_ATLCDisk("Cannot match physical sector, ignoring data field.\n");
					}
				}

				state = 0;
				break;
		}
	}

	// do a format operation if we have seen an IDAM, else do write
	static constexpr uint16 kCRC16_SyncX3 = 0xCDB4;

	if (doingFormat) {
		// check if we are allowed to format
		if (!diskIf->IsFormatAllowed()) {
			// not allowed -- ignore the write and allow the firmware to fail the verify
			return;
		}

		// determine the maximum sector number
		uint32 maxSecNo = 0;
		uint32 bytesPerSector = 128;

		for(const CapturedSector& cs : capturedSectors) {
			if (maxSecNo < cs.mSectorNumber)
				maxSecNo = cs.mSectorNumber;

			if (cs.mSectorNumber == 1)
				bytesPerSector = 128 << cs.mSectorSizeCode;
		}

		// cap at 26 for ED if we see higher than that, though the 815 will actually only
		// write up to 18)
		if (maxSecNo > 26)
			maxSecNo = 26;

		// if we are formatting track 0, then format the entire disk if we are changing disk
		// geometry
		if (track == 0) {
			const auto& geo = image->GetGeometry();

			if (geo.mSectorSize != bytesPerSector || !geo.mbMFM || geo.mSectorsPerTrack != maxSecNo + 1 || geo.mbHighDensity) {
				ATDiskGeometryInfo newGeometry = {};
				newGeometry.mSectorSize = bytesPerSector;
				newGeometry.mBootSectorCount = bytesPerSector > 256 ? 0 : 3;
				newGeometry.mTrackCount = 40;
				newGeometry.mSectorsPerTrack = maxSecNo;
				newGeometry.mSideCount = 1;
				newGeometry.mbMFM = true;
				newGeometry.mbHighDensity = false;
				newGeometry.mTotalSectorCount = newGeometry.mTrackCount * newGeometry.mSideCount * newGeometry.mSectorsPerTrack;

				diskIf->FormatDisk(newGeometry);

				// must refetch image as it has changed
				image = diskIf->GetDiskImage();
				if (!image)
					return;
			}
		}

		// take the captured sectors and bin them into new virtual and physical sector entries
		const auto& newGeo = image->GetGeometry();		// reference may have changed after FormatDisk(), so we must refetch
		const uint32 expectedSectorSize = newGeo.mSectorSize;

		ATDiskVirtualSectorInfo newVirtSectors[26] {};
		vdfastvector<ATDiskPhysicalSectorInfo> newPhysSectors;
		newPhysSectors.reserve(capturedSectors.size());

		vdfastvector<uint8> decodedData;

		if (maxSecNo > newGeo.mSectorsPerTrack)
			maxSecNo = newGeo.mSectorsPerTrack;

		for(uint32 secNo = 1; secNo <= maxSecNo; ++secNo) {
			ATDiskVirtualSectorInfo& vsi = newVirtSectors[secNo - 1];

			vsi.mStartPhysSector = (uint32)newPhysSectors.size();

			for(const CapturedSector& cs : capturedSectors) {
				// if we saw a DAM with no IDAM, toss it -- the FDC will never notice it
				if (!cs.mAddressFieldStart)
					continue;

				// skip if not for current logical sector
				if (cs.mSectorNumber != secNo)
					continue;

				newPhysSectors.push_back(ATDiskPhysicalSectorInfo());
				ATDiskPhysicalSectorInfo& psi = newPhysSectors.back();
				++vsi.mNumPhysSectors;

				psi.mOffset				= (sint32)decodedData.size();
				psi.mDiskOffset			= -1;
				psi.mImageSize			= 128 << cs.mSectorSizeCode;
				psi.mPhysicalSize		= 128 << cs.mSectorSizeCode;
				psi.mbDirty				= true;
				psi.mbMFM				= true;
				psi.mRotPos				= (float)(mWriteDataStartBitPos + 8 * cs.mAddressFieldStart) / (float)kTrackBitLen;
				psi.mRotPos -= floorf(psi.mRotPos);
				psi.mFDCStatus			= 0xFF;
				psi.mWeakDataOffset		= -1;

				// decode the sector data and CRC
				const uint32 availableLen = std::min<uint32>(psi.mImageSize + 2, (mWriteDataLen - cs.mDataFieldStart) >> 1);
				decodedData.resize(decodedData.size() + psi.mImageSize + 2, 0);

				if (!cs.mDataFieldStart) {
					// we didn't see a data field -- mark RNF (this is subtly different from a no-address
					// RNF, which we don't include at all)
					psi.mFDCStatus -= 0x10;
				} else {
					uint8 *sectorData = &decodedData[psi.mOffset];

					const uint8 *mfmSrc = &mWriteData[cs.mDataFieldStart];
					for (uint32 i = 0; i < availableLen; ++i)
						sectorData[i] = ATPackBits16To8(mfmSrc[i*2], mfmSrc[i*2 + 1]);

					// check CRC and mark data CRC error if mismatch
					const uint16 recordedCRC = VDReadUnalignedBEU16(&sectorData[psi.mImageSize]);
					const uint16 computedCRC = ATComputeCRC16(ATAdvanceCRC16(kCRC16_SyncX3, cs.mDataAddressMark), sectorData, psi.mImageSize);

					if (recordedCRC != computedCRC)
						psi.mFDCStatus -= 0x08;

					// encode long sector if so
					if (psi.mImageSize > expectedSectorSize)
						psi.mFDCStatus -= 0x04;

					// invert data if accurate mode is requested -- must be done after we compute/check CRC
					if (mbAccurateInvert) {
						for (uint32 i = 0; i < availableLen; ++i)
							sectorData[i] = ~sectorData[i];
					}
				}
			}
		}

		g_ATLCFDC("Formatting track %u with %u sectors (%u physical sectors)\n", track, maxSecNo, (unsigned)newPhysSectors.size());
		image->FormatTrack(track * newGeo.mSectorsPerTrack, newGeo.mSectorsPerTrack, newVirtSectors, (uint32)newPhysSectors.size(), newPhysSectors.data(), decodedData.data());
		diskIf->OnDiskChanged(false);

		MaterializeTrack(drive, track);
	} else {
		// commit sector data
		uint8 sectorData[1026];

		for(const CapturedSector& cs : capturedSectors) {
			// decode the sector data and CRC -- if we're short take whatever we've got
			const uint32 sectorSize = 128 << cs.mSectorSizeCode;
			const uint32 availableLen = std::min<uint32>(sectorSize + 2, (mWriteDataLen - cs.mDataFieldStart) >> 1);

			const uint8 *mfmSrc = &mWriteData[cs.mDataFieldStart];
			for (uint32 j = 0; j < availableLen; ++j)
				sectorData[j] = ATPackBits16To8(mfmSrc[j*2], mfmSrc[j*2 + 1]);

			memset(sectorData + availableLen, 0, (sectorSize + 2) - availableLen);

			// check if CRC matches
			const bool crcOK = VDReadUnalignedBEU16(&sectorData[sectorSize]) == ATComputeCRC16(ATAdvanceCRC16(kCRC16_SyncX3, cs.mDataAddressMark), sectorData, sectorSize);

			// encode FDC status
			uint8 fdcStatus = (cs.mDataAddressMark << 5) | 0x9F;

			if (!crcOK)
				fdcStatus -= 0x08;

			if (sectorSize > geo.mSectorSize)
				fdcStatus -= 0x04;

			g_ATLCDisk("Writing physical sector %u (vsec %u / trk %2u sec %2u): Status=$%02X\n"
				, cs.mPhysicalSector
				, cs.mVirtualSector + 1
				, mCurrentTrack
				, cs.mSectorNumber
				, fdcStatus);

			if (mbAccurateInvert) {
				for(uint32 i = 0; i < sectorSize; ++i)
					sectorData[i] = ~sectorData[i];
			}

			try {
				image->WritePhysicalSector(cs.mPhysicalSector, sectorData, sectorSize, fdcStatus);
			} catch(...) {
				// Ignore error, no way to report it to the firmware or for it to notice until
				// it verifies the sector -- like a bad floppy
			}
		}

		++mInhibitDiskChangeInvalidation;
		diskIf->OnDiskModified();
		--mInhibitDiskChangeInvalidation;

		// shift the data up and zero pad it so insertion is easier
		memmove(mWriteData + 2, mWriteData, mWriteDataLen);
		mWriteData[0] = 0;
		mWriteData[1] = 0;
		mWriteData[mWriteDataLen] = 0;
		mWriteData[mWriteDataLen+1] = 0;

		// splice the write data back into the track
		const uint32 writeLenBits = std::min<uint32>(mWriteDataLen * 8, kTrackBitLen);
		const uint32 writeAvail = kTrackBitLen - mWriteDataStartBitPos;

		if (writeAvail < writeLenBits) {
			ATInsertTrackBits(mTrackData, mWriteDataStartBitPos, mWriteData, 16, writeAvail);
			ATInsertTrackBits(mTrackData, 0, mWriteData, 16 + writeAvail, writeLenBits - writeAvail);
		} else {
			ATInsertTrackBits(mTrackData, mWriteDataStartBitPos, mWriteData, 16, writeLenBits);
		}
	}
}

void ATDeviceDiskDrive815::ResetSSDA() {
	mReceiveFIFOStart = 0;
	mReceiveFIFOLength = 0;
	mbSSDATwoByteMode = false;
	mSSDARegisterAddr = SSDARegisterAddr::Control2;
	mbSSDARxReset = true;
	mbSSDASyncEnabled = false;
	mSSDASyncState = 0;
	mbSSDASyncOutputEnabled = false;
	mSSDABitOffset = 0;
}

void ATDeviceDiskDrive815::UpdateSSDAReceiveFIFO() {
	// if the receive section is in reset/inhibited state, nothing to do
	if (mbSSDARxReset)
		return;

	// compute how many drive cycles have passed since we last fed the FIFO
	const uint32 t = mDriveScheduler.GetTick();
	const uint32 deltaCycles = t - mHeadPositionLastDriveCycle;

	if (!deltaCycles)
		return;

	// convert cycle offset to a bit cell offset (2us @ 2MHz = 8 drive cycles)
	const uint32 deltaBits = deltaCycles >> 2;

	// advance head position to the current position
	mHeadPositionLastDriveCycle += deltaBits << 2;
	mHeadPositionLastBitPos += deltaBits;

	if (mHeadPositionLastBitPos >= kTrackBitLen)
		mHeadPositionLastBitPos %= kTrackBitLen;

	// Compute the number of bits we need to process -- since the FIFO is
	// 3-deep, we need enough to be able to put 4 bytes into the FIFO -- 3 to
	// fill it and one to signal an overflow. In double-clocked mode, this can
	// require 15 bits to achieve bit sync and 64 bits after that.
	uint32 bitsToProcess = std::min<uint32>(deltaBits, 79);

	// compute starting bit offset by rewinding the head position
	uint32 bitPos = mHeadPositionLastBitPos + (mHeadPositionLastBitPos < bitsToProcess ? kTrackBitLen : 0) - bitsToProcess;

	// process each incoming bit
	for(uint32 i = 0; i < bitsToProcess; ++i) {
		// Bits are stored on the medium MSB first, so extracting an unaligned
		// byte from it is similar to a bitmap.
		uint16 v = (uint16)(VDReadUnalignedBEU32(&mTrackData[bitPos >> 3]) >> (16 - (bitPos & 7)));
		if (++bitPos >= kTrackBitLen)
			bitPos -= kTrackBitLen;

		if (mReceiveClockState >= 2) {
			// collect the data bits only

			if (mSSDASyncState && (mSSDABitOffset & 15)) {
				++mSSDABitOffset;
				continue;
			}

			++mSSDABitOffset;

			if (!(mSSDABitOffset & 1)) {
				continue;
			} else {
				v = (kATPackBitsTable[(v >> 8) & 0x55] << 4) + kATPackBitsTable[v & 0x55];
			}
		} else {
			if (mSSDASyncState && (mSSDABitOffset & 14)) {
				mSSDABitOffset += 2;
				continue;
			}

			mSSDABitOffset += 2;

			// data and clock bits being returned
			v >>= 8;
		}

		if (mSSDASyncState < 2) {
			if (v == mSSDASyncValue && mbSSDASyncEnabled) {
				//g_ATLCDiskEmu("Matched sync byte $%02X at bitPos %d (%04X), dt %08X, PC=%04X\n", v, bitPos - 1, ov, t, mCoProc.GetPC());

				if (mbSSDATwoByteMode)
					++mSSDASyncState;
				else
					mSSDASyncState = 2;

				mSSDABitOffset = (mReceiveClockState >= 2) ? 1 : 2;

				// send out the sync pulse and advance the receive clock state
				if (mbSSDASyncOutputEnabled && mReceiveClockState < 2 && !mbReceiveClockReset) {
					++mReceiveClockState;
					mRIOT1.SetInputA(mReceiveClockState == 1 ? 0x80 : 0x00, 0x80);

					if (mReceiveClockState == 2) {
						//g_ATLCDiskEmu("Switching to data mode at bitPos %d, dt %08X, PC=%04X\n", bitPos - 1, t, mCoProc.GetPC());
						mSSDABitOffset = 9;
					}
				}
			} else
				mSSDASyncState = 0;
		} else {
			// we are in sync, which means we should start pushing bytes into the FIFO
			//g_ATLCDiskEmu("Queuing %02X at bitPos %d\n", (uint8)v, bitPos - 1);
			mReceiveFIFO[(mReceiveFIFOStart + mReceiveFIFOLength) & 3] = (uint8)v;

			if (mReceiveFIFOLength < 3)
				++mReceiveFIFOLength;
			else
				++mReceiveFIFOStart;
		}
	}
}

// RIOT1 ($0380-03FF):
//  PA7 (I)     Sync status (0 = unsynced or fully synced, 1 = first byte synced)
//  PA6 (O)     Clock reset output
//  PA5 (O)     /CTS reset output
//  PA4 (I)     Drive 1 write protect (1 = protected)
//  PA3 (O)     SSDA master reset
//  PA2 (O)     Not used
//  PA1 (O)     Drive 1 motor enable and LED (1 = running)
//  PA0 (I)     Drive 2 write protect (1 = protected)
//
//  PB7 (I)     SIO DATA OUT (inverted)
//  PB6 (I)     SIO COMMAND (non-inverted)
//  PB5 (O)     Drive 1 stepper phase
//  PB4 (O)     Drive 1 stepper phase
//  PB3 (O)     Drive 1 stepper phase
//  PB2 (O)     Drive 1 stepper phase
//  PB1 (I)     SIO READY
//  PB0 (I)     SIO DATA IN (non-inverted)

void ATDeviceDiskDrive815::OnRIOT1RegisterWrite(uint32 addr, uint8 val) {
	// check for a write to DRA or DDRA
	if ((addr & 6) == 0) {
		// compare outputs before and after write
		const uint8 prev = mRIOT1.ReadOutputA();
		mRIOT1.WriteByte((uint8)addr, val);
		const uint8 next = mRIOT1.ReadOutputA();

		// check for density change
		const uint8 delta = prev ^ next;

		// check for a drive 1 spindle motor state change
		if (delta & 0x02)
			UpdateRotationStatus();

		// check for receive clock reset
		if (delta & 0x40) {
			const bool rxClkReset = !(next & 0x40);

			if (mbReceiveClockReset != rxClkReset) {
				UpdateSSDAReceiveFIFO();

				mbReceiveClockReset = rxClkReset;
				mReceiveClockState = 0;
				mRIOT1.SetInputA(0x00, 0x80);
			}
		}

		return;
	}

	// check for a write to DRB or DDRB
	if ((addr & 6) == 2) {
		// compare outputs before and after write
		const uint8 prev = mRIOT1.ReadOutputB();
		mRIOT1.WriteByte((uint8)addr, val);
		const uint8 next = mRIOT1.ReadOutputB();
		const uint8 delta = prev ^ next;

		// check for stepping transition
		if (delta & 0x3C)
			StepDrive(0, next);

		// check for transition on PB0 (SIO input)
		if (delta & 0x01)
			AddTransmitEdge((next & 0x01) != 0);

		return;
	}

	mRIOT1.WriteByte((uint8)addr, val);
}

// $0280-029F      RIOT 2
//     PA7 (O)     Drive 2 write LED (1 = on)
//     PA6 (O)     Drive 2 read LED (1 = on)
//     PA5 (O)     Not used
//     PA4 (O)     Drive 2 bottom head read enable (0 = enabled)
//     PA3 (O)     Drive 2 top head read enable (not used)
//     PA2 (O)     Not used
//     PA1 (O)     Drive 1 bottom head read enable (0 = enabled)
//     PA0 (O)     Drive 1 top head read enable (not used)
// 
//     PB7 (O)     Drive 2 bottom erase head enable (0 = enabled)
//     PB6 (O)     Drive 2 bottom write head enable (0 = enabled)
//     PB5 (O)     Drive 2 top erase head enable (not used)
//     PB4 (O)     Drive 2 top write head enable (not used)
//     PB3 (O)     Drive 1 bottom erase head enable (0 = enabled)
//     PB2 (O)     Drive 1 bottom write head enable (0 = enabled)
//     PB1 (O)     Drive 1 top erase head enable (not used)
//     PB0 (O)     Drive 1 top write head enable (not used)

void ATDeviceDiskDrive815::OnRIOT2RegisterWrite(uint32 addr, uint8 val) {
	// check for a write to DRA or DDRA
	if ((addr & 6) == 0) {
		// compare outputs before and after write
		const uint8 prev = mRIOT2.ReadOutputA();
		mRIOT2.WriteByte((uint8)addr, val);
		const uint8 next = mRIOT2.ReadOutputA();
		const uint8 delta = prev ^ next;

		if ((delta & 0x02) && !(next & 0x02)) {
			// drive 1 turning read head on
			MaterializeTrack(0, mCurrentTrack[0]);
			StartDiskRead();
		} else if ((delta & 0x10) && !(next & 0x10)) {
			// drive 2 turning read head on
			MaterializeTrack(1, mCurrentTrack[1]);
			StartDiskRead();
		}

		return;
	}

	// check for a write to DRB or DDRB
	if ((addr & 6) == 2) {
		// compare outputs before and after write
		const uint8 prev = mRIOT2.ReadOutputB();
		mRIOT2.WriteByte((uint8)addr, val);
		const uint8 next = mRIOT2.ReadOutputB();
		const uint8 delta = prev ^ next;

		// if there is a change in write/erase head state, clear the disk transmit state
		if (delta & 0x0C) {
			if ((next & 0x0C) == 0x0C)
				EndDiskWrite();

			if ((prev & 0x0C) == 0x0C || (next & 0x0C) == 0x0C) {
				FlushDiskWrite();
				ResetDiskWrite();
			}

			if ((prev & 0x0C) == 0x0C)
				StartDiskWrite();
		}

		if (delta & 0xC0) {
			if ((next & 0xC0) == 0xC0)
				EndDiskWrite();

			if ((prev & 0xC0) == 0xC0 || (next & 0xC0) == 0xC0) {
				FlushDiskWrite();
				ResetDiskWrite();
			}

			if ((prev & 0xC0) == 0xC0)
				StartDiskWrite();
		}

		return;
	}

	mRIOT2.WriteByte((uint8)addr, val);
}

// $0200-027F      RIOT 3
//     PA7 (I)     Drive ID select high (1 = D1-D4:, 0 = D5-D8:)
//     PA6 (I)     Not used
//     PA5 (I)     Not used
//     PA4 (O)     Drive 1 write LED (1 = on)
//     PA3 (O)     Drive 1 read LED (1 = on)
//     PA2 (I)     40/80 track jumper input (not used)
//     PA1 (O)     Drive 2 motor enable and LED (1 = running)
//     PA0 (I)     Drive ID select low (1 = D1-D2: or D5-D6:, 0 = D3-D4: or D7-D8:)
// 
//     PB7 (I)     Drive mechanism type jumper input (not used)
//     PB6 (I)     Double-sided jumper input (not used)
//     PB5 (O)     Drive 2 stepper phase
//     PB4 (O)     Drive 2 stepper phase
//     PB3 (O)     Drive 2 stepper phase
//     PB2 (O)     Drive 2 stepper phase
//     PB1 (I)     Extended PROM space jumper input (not used)
//     PB0 (O)     Write precompensation (0 = enabled)

void ATDeviceDiskDrive815::OnRIOT3RegisterWrite(uint32 addr, uint8 val) {
	// check for a write to DRA or DDRA
	if ((addr & 6) == 0) {
		// compare outputs before and after write
		const uint8 prev = mRIOT3.ReadOutputA();
		mRIOT3.WriteByte((uint8)addr, val);
		const uint8 next = mRIOT3.ReadOutputA();
		const uint8 delta = prev ^ next;

		if (delta & 0x02)
			UpdateRotationStatus();

		return;
	}

	// check for a write to DRB or DDRB
	if ((addr & 6) == 2) {
		// compare outputs before and after write
		const uint8 prev = mRIOT3.ReadOutputB();
		mRIOT3.WriteByte((uint8)addr, val);
		const uint8 next = mRIOT3.ReadOutputB();
		const uint8 delta = prev ^ next;

		if (delta & 0x3C)
			StepDrive(1, next);

		return;
	}

	mRIOT3.WriteByte((uint8)addr, val);
}

void ATDeviceDiskDrive815::StepDrive(int drive, uint8 newPhases) {
	static const sint8 kOffsetTable[16] = {
		// 815 (two adjacent phases required, noninverted)
		-1, -1, -1,  1,
		-1, -1,  2, -1,
		-1,  0, -1, -1,
		3, -1, -1, -1
	};

	const sint8 newOffset = kOffsetTable[(newPhases >> 2) & 15];

	bool changed = false;

	if (newOffset >= 0) {
		switch(((uint32)newOffset - mCurrentTrack[drive]) & 3) {
			case 1:		// step in (increasing track number)
				if (mCurrentTrack[drive] < 45) {
					++mCurrentTrack[drive];
					changed = true;
				}

				PlayStepSound();
				break;

			case 3:		// step out (decreasing track number)
				if (mCurrentTrack[drive] > 0) {
					--mCurrentTrack[drive];

					PlayStepSound();
					changed = true;
				}
				break;

			case 0:
			case 2:
			default:
				// no step or indeterminate -- ignore
				break;
		}
	}

	mpDiskInterfaces[drive]->SetShowActivity(mbMotorEnabled[drive], mCurrentTrack[drive]);

	g_ATLCDiskEmu("Stepper phases now: %X (track: %d)\n", newPhases & 0x3C, mCurrentTrack[drive]);

	// The 815 doesn't even turn off the read head when stepping, so we must
	// materialize each track for each step.
	MaterializeTrack(drive, mCurrentTrack[drive]);
}

void ATDeviceDiskDrive815::PlayStepSound() {
	if (!mbSoundsEnabled)
		return;

	const uint32 t = ATSCHEDULER_GETTIME(&mDriveScheduler);
	
	if (t - mLastStepSoundTime > 50000)
		mLastStepPhase = 0;

	mAudioPlayer.PlayStepSound(kATAudioSampleId_DiskStep1, 0.3f + 0.7f * cosf((float)mLastStepPhase++ * nsVDMath::kfPi * 0.5f));

	mLastStepSoundTime = t;
}

uint32 ATDeviceDiskDrive815::GetCurrentBitPos(uint64 t64, int drive) const {
	return (uint32)((mRotationStartBitPos[drive] + ((t64 - mRotationStartTime[drive]) >> 2)) % kTrackBitLen);
}

void ATDeviceDiskDrive815::UpdateRotationStatus() {
	const bool motorEnabled[2] = {
		(mRIOT1.ReadOutputA() & 0x02) != 0,
		(mRIOT3.ReadOutputA() & 0x02) != 0
	};

	for(int i=0; i<2; ++i) {
		if (mbMotorEnabled[i] != motorEnabled[i]) {

			const uint64 t = mDriveScheduler.GetTick64();

			if (!motorEnabled[i]) {
				mRotationStartBitPos[i] = GetCurrentBitPos(t, i);
			}

			mRotationStartTime[i] = t;
			mbMotorEnabled[i] = motorEnabled[i];

			mpDiskInterfaces[i]->SetShowMotorActive(motorEnabled[i]);
			mpDiskInterfaces[i]->SetShowActivity(motorEnabled[i], mCurrentTrack[i]);
		}
	}

	mAudioPlayer.SetRotationSoundEnabled(motorEnabled[0] || motorEnabled[1]);
}

void ATDeviceDiskDrive815::UpdateDiskStatus() {
	UpdateWriteProtectStatus();
}

void ATDeviceDiskDrive815::UpdateWriteProtectStatus() {
	const bool wpsense1 = mpDiskInterfaces[0]->GetDiskImage() && !mpDiskInterfaces[0]->IsDiskWritable();
	const bool wpsense2 = mpDiskInterfaces[1]->GetDiskImage() && !mpDiskInterfaces[1]->IsDiskWritable();

	mRIOT1.SetInputA((wpsense1 ? 0x10 : 0x00) + (wpsense2 ? 0x01 : 0x00), 0x11);
}

void ATDeviceDiskDrive815::MaterializeTrack(int drive, int track) {
	if (mLastMaterializedDrive == drive && mLastMaterializedTrack == track)
		return;

	mRenderedSectors.clear();

	IATDiskImage *image = mpDiskInterfaces[drive]->GetDiskImage();
	if (!image) {
		// no disk in drive -- clear the track
		memset(mTrackData, 0, sizeof mTrackData);
		return;
	}

	// Preformat the entire track with $4E bytes.
	//
	// Data bits:		%00101110
	// MFM encoded:		%10100100 01010100
	// 
	static_assert(vdcountof(mTrackData) % 2 == 0);
	for(int i=0; i<vdcountof(mTrackData); i += 2) {
		mTrackData[i+0] = 0xA4;
		mTrackData[i+1] = 0x54;
	}

	mLastMaterializedDrive = drive;
	mLastMaterializedTrack = track;

	const ATDiskGeometryInfo& geometry = image->GetGeometry();

	if (geometry.mbHighDensity)
		return;

	static const auto bitSpread = [](uint8 v) -> uint16 {
		static constexpr std::array<uint16, 256> kBitSpreadTable = []() -> std::array<uint16, 256> {
			std::array<uint16, 256> table {};

			for(int i=0; i<256; ++i) {
				uint16 v = 0;
				if (i & 0x01) v += 0x0001;
				if (i & 0x02) v += 0x0004;
				if (i & 0x04) v += 0x0010;
				if (i & 0x08) v += 0x0040;
				if (i & 0x10) v += 0x0100;
				if (i & 0x20) v += 0x0400;
				if (i & 0x40) v += 0x1000;
				if (i & 0x80) v += 0x4000;
				table[i] = v;
			}

			return table;
		}();

		return kBitSpreadTable[v];
	};

	// For each sector, we emit:
	//	8 x $00
	//	3 x A1_sync
	//	$FE			IDAM
	//	$xx			Track number
	//	$00			Side number
	//	$xx			Sector number
	//	$01			Sector size
	//	$xx			Address CRC 1
	//	$xx			Address CRC 2
	//	19 x $4E
	//		-- write splice here --
	//	12 x $00
	//	3 x A1_sync
	//	$FB			DAM
	//	256 x xx	Data
	//	2 x xx		Data CRC 1/2
	//	$FF
	//		-- write splice here --
	//
	// As the address field is normally laid down as part of a single Write Track
	// operation, it should be byte aligned, although the FDC does not require
	// this. The data field can be skewed by any arbitrary offset as it is
	// re-laid down each time the sector is written.
	const uint8 addressFieldClock[19] = {
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0xFB,
		0xFB,
		0xFB,
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0xFF
	};

	uint8 addressFieldData[19] = {
		// These $00s are critical for the 815. Unlike an FD279X, which can
		// sync to any arbitrary sync offset, the 815 can't sync to a $4489
		// if it is too close to a previous $44xx pattern that triggers the
		// first byte sync.
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0xA1,
		0xA1,
		0xA1,
		0xFE,
		(uint8)track,
		0x00,
		0x00,
		0x01,
		0x00,
		0x00,
		// We need this byte to ensure that the trailing clock bit is correct.
		0x4E,
	};

	uint8 dataFieldClock[1024 + 19];
	memset(dataFieldClock, 0xFF, sizeof dataFieldClock);

	uint8 dataFieldData[1024 + 19] {};

	// sync
	for(int i=0; i<3; ++i) {
		dataFieldClock[i+12] = 0xFB;
		dataFieldData[i+12] = 0xA1;
	}

	// DAM
	dataFieldData[15] = 0xFB;

	uint8 mfmBuffer[(1024+19)*2 + 4];

	uint32 vsecStart = geometry.mSectorsPerTrack * track;
	uint32 vsecCount = image->GetVirtualSectorCount();

	for(uint32 i = 0; i < geometry.mSectorsPerTrack; ++i) {
		if (vsecStart + i >= vsecCount)
			break;

		ATDiskVirtualSectorInfo vsi;
		image->GetVirtualSectorInfo(vsecStart + i, vsi);

		for(uint32 j = 0; j < vsi.mNumPhysSectors; ++j) {
			ATDiskPhysicalSectorInfo psi;
			image->GetPhysicalSectorInfo(vsi.mStartPhysSector + j, psi);

			// toss FM sectors, they won't get even close to recognized
			if (!psi.mbMFM)
				continue;

			// if the sector has RNF status without RNF+CRC, just omit it
			if ((psi.mFDCStatus & 0x18) == 0x08)
				continue;

			// convert sector size to code; truncate to 1K if needed
			uint32 sectorSizeCode;

			if (psi.mPhysicalSize <= 128)
				sectorSizeCode = 0;
			else if (psi.mPhysicalSize <= 256)
				sectorSizeCode = 1;
			else if (psi.mPhysicalSize <= 512)
				sectorSizeCode = 2;
			else
				sectorSizeCode = 3;

			uint32 sectorSize = 128 << sectorSizeCode;

			// write sector number and size code into address field and update the CRC-16
			addressFieldData[14] = (uint8)(i + 1);
			addressFieldData[15] = sectorSizeCode;

			VDWriteUnalignedBEU16(&addressFieldData[16], ATComputeCRC16(0xFFFF, addressFieldData + 8, 8));

			// corrupt the CRC if the sector has an address CRC error (RNF + CRC bits set)
			if ((psi.mFDCStatus & 0x18) == 0x00) {
				addressFieldData[16] = ~addressFieldData[16];
				addressFieldData[17] = ~addressFieldData[17];
			}

			// write the address field
			uint32 addressFieldOffset = VDRoundToInt32((psi.mRotPos - floorf(psi.mRotPos)) * (float)kTrackBitLen) % kTrackBitLen;

			//g_ATLCDiskEmu("Writing address field for track %d, sector %d at bit position %d\n", track, i + 1, addressFieldOffset & ~15);

			ATWriteRawMFMToTrack(mTrackData, addressFieldOffset & ~15, kTrackBitLen, addressFieldClock, addressFieldData, (uint32)vdcountof(addressFieldClock), mfmBuffer);

			// update DAM
			dataFieldData[15] = 0xF8 + ((psi.mFDCStatus & 0x60) >> 5);

			// update sector data
			memset(dataFieldData + 16, 0xB6, sectorSize);

			try {
				image->ReadPhysicalSector(vsi.mStartPhysSector + j, dataFieldData + 16, sectorSize);
			} catch(const MyError&) {
			}

			if (mbAccurateInvert) {
				for(uint32 i = 0; i < sectorSize; ++i)
					dataFieldData[i + 16] = ~dataFieldData[i + 16];
			}

			// if the sector is short due to being a boot sector, we need to fill it with $B6, or else
			// format verify will fail
			if (psi.mImageSize < sectorSize)
				memset(dataFieldData + 16 + psi.mImageSize, 0xB6, sectorSize - psi.mImageSize);

			// byte after CRC
			dataFieldData[sectorSize + 18] = 0xFF;

			// update data field CRC-16
			VDWriteUnalignedBEU16(&dataFieldData[sectorSize + 16], ATComputeCRC16(0xFFFF, dataFieldData + 12, sectorSize + 4));

			// corrupt data field CRC-16 if sector has CRC error
			if ((psi.mFDCStatus & 0x08) == 0x00) {
				dataFieldData[sectorSize + 16] = ~dataFieldData[sectorSize + 16];
				dataFieldData[sectorSize + 17] = ~dataFieldData[sectorSize + 17];
			}

			// write the data field
			const uint32 rawDataFieldOffset = addressFieldOffset + 29 * 16;
			const uint32 dataFieldOffset = rawDataFieldOffset % kTrackBitLen;

			SectorRenderInfo sri {};
			sri.mSector = i + 1;
			sri.mVirtualSector = vsecStart + i + 1;
			sri.mPhysicalSector = vsi.mStartPhysSector + j;
			sri.mStartBitPos = addressFieldOffset;
			sri.mStartDataFieldBitPos = rawDataFieldOffset;
			sri.mEndBitPos = rawDataFieldOffset + (uint32)vdcountof(dataFieldClock) * 16;
			sri.mSectorSizeCode = sectorSize >= 1024 ? 3 : sectorSize >= 512 ? 2 : sectorSize >= 256 ? 1 : 0;
			mRenderedSectors.push_back(sri);

			g_ATLCFDC("Rendered track %u, sector %2u to %u-%u\n", track, i + 1, sri.mStartBitPos, sri.mEndBitPos);

			ATWriteRawMFMToTrack(mTrackData, dataFieldOffset, kTrackBitLen, dataFieldClock, dataFieldData, sectorSize + 19, mfmBuffer);
		}
	}

	// repeat the start of the track after the end so we can easily extract words
	// overlapping the track start
	uint8 *tail = &mTrackData[kTrackByteLen];
	constexpr int tailBitShift = kTrackBitLen & 7;
	constexpr uint32 tailMask = 0xFFFFFFFFU >> tailBitShift;

	VDWriteUnalignedBEU32(tail, (VDReadUnalignedBEU32(tail) & ~tailMask) + (VDReadUnalignedBEU32(mTrackData) >> tailBitShift));
}
