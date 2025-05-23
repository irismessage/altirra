//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
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
#include <vd2/system/hash.h>
#include <vd2/system/int128.h>
#include <at/atcore/blockdevice.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/snapshotimpl.h>
#include "mio.h"
#include "debuggerlog.h"
#include "memorymanager.h"
#include "firmwaremanager.h"
#include "irqcontroller.h"
#include "scsidisk.h"

extern ATDebuggerLogChannel g_ATLCParPrint;

void ATCreateDeviceMIOEmulator(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATMIOEmulator> p(new ATMIOEmulator);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefMIO = { "mio", "mio", L"MIO", ATCreateDeviceMIOEmulator, kATDeviceDefFlag_RebootOnPlug };

ATMIOEmulator::ATMIOEmulator() {
	memset(mFirmware, 0xFF, sizeof mFirmware);

	mACIA.SetInterruptFn([this](bool state) { OnACIAIRQStateChanged(state); });
	mACIA.SetReceiveReadyFn([this]() { OnACIAReceiveReady(); });
	mACIA.SetTransmitFn([this](uint8 data, uint32 baudRate) { OnACIATransmit(data, baudRate); });
	mACIA.SetControlFn([this](bool rts, bool dtr) { OnACIAControlStateChanged(rts, dtr); });

	mSCSIBus.SetBusMonitor(this);
}

ATMIOEmulator::~ATMIOEmulator() {
}

void *ATMIOEmulator::AsInterface(uint32 iid) {
	switch(iid) {
		case IATDeviceMemMap::kTypeID: return static_cast<IATDeviceMemMap *>(this);
		case IATDeviceFirmware::kTypeID: return static_cast<IATDeviceFirmware *>(this);
		case IATDeviceIRQSource::kTypeID: return static_cast<IATDeviceIRQSource *>(this);
		case IATDeviceScheduling::kTypeID: return static_cast<IATDeviceScheduling *>(this);
		case IATDeviceParent::kTypeID: return static_cast<IATDeviceParent *>(this);
		case IATDeviceIndicators::kTypeID: return static_cast<IATDeviceIndicators *>(this);
		case IATDeviceSnapshot::kTypeID: return static_cast<IATDeviceSnapshot *>(this);
	}

	return ATDevice::AsInterface(iid);
}

void ATMIOEmulator::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefMIO;
}

void ATMIOEmulator::GetSettings(ATPropertySet& settings) {
}

bool ATMIOEmulator::SetSettings(const ATPropertySet& settings) {
	return true;
}

void ATMIOEmulator::Init() {
	mSerialBus.Init(this, 0, IATDeviceSerial::kTypeID, "serial", L"Serial Port", "serport");
	mParallelBus.Init(this, 2, IATPrinterOutput::kTypeID, "parallel", L"Parallel Printer Port", "parport");

	mSerialBus.SetOnAttach(
		[this] {
			if (mpSerialDevice)
				return;

			IATDeviceSerial *serdev = mSerialBus.GetChild<IATDeviceSerial>();
			if (serdev) {
				vdsaferelease <<= mpSerialDevice;
				if (serdev)
					serdev->AddRef();
				mpSerialDevice = serdev;
				mpSerialDevice->SetOnStatusChange([this](const ATDeviceSerialStatus& status) { this->OnControlStateChanged(status); });

				UpdateSerialControlLines();
			}
		}
	);

	mSerialBus.SetOnDetach(
		[this] {
			if (mpSerialDevice) {
				mpSerialDevice->SetOnStatusChange(nullptr);
				vdpoly_cast<IATDevice *>(mpSerialDevice)->SetParent(nullptr, 0);
				vdsaferelease <<= mpSerialDevice;
			}

			mSerialCtlInputs = 0;
		}
	);

	mParallelBus.SetOnAttach(
		[this] {
			mpPrinterOutput = nullptr;
		}
	);
}

void ATMIOEmulator::Shutdown() {
	mpPrinterOutput = nullptr;

	mParallelBus.Shutdown();
	mSerialBus.Shutdown();

	mSCSIBus.Shutdown();

	while(!mSCSIDisks.empty()) {
		const SCSIDiskEntry& ent = mSCSIDisks.back();

		ent.mpSCSIDevice->Release();
		ent.mpDisk->Release();
		if (ent.mpDevice) {
			ent.mpDevice->SetParent(nullptr, 0);
			ent.mpDevice->Release();
		}

		mSCSIDisks.pop_back();
	}

	if (mpIRQController) {
		if (mIRQBit) {
			mpIRQController->FreeIRQ(mIRQBit);
			mIRQBit = 0;
		}

		mpIRQController = nullptr;
	}

	mpPrinterOutput = nullptr;

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventUpdateSCSIBus);
		mpScheduler = nullptr;
	}

	if (mpMemLayerFirmware) {
		mpMemMan->DeleteLayer(mpMemLayerFirmware);
		mpMemLayerFirmware = nullptr;
	}

	if (mpMemLayerRAM) {
		mpMemMan->DeleteLayer(mpMemLayerRAM);
		mpMemLayerRAM = NULL;
	}

	if (mpMemLayerPBI) {
		mpMemMan->DeleteLayer(mpMemLayerPBI);
		mpMemLayerPBI = NULL;
	}

	mACIA.Shutdown();

	mpUIRenderer = nullptr;
}

void ATMIOEmulator::WarmReset() {
	mDataIn = 0xFF;
	mDataOut = 0xFF;
	mSCSIBus.SetControl(0, 0xFF | kATSCSICtrlState_RST, kATSCSICtrlState_All | 0xFF);
	mPrevSCSIState = mSCSIBus.GetBusState();

	// turn off printer IRQ
	mbPrinterIRQEnabled = false;

	mPBIBANK = -2;
	SetPBIBANK(-1);

	mRAMPAGE = 1;
	SetRAMPAGE(0);

	mbRAMEnabled = false;
	mpMemMan->EnableLayer(mpMemLayerRAM, false);

	mbLastPrinterStrobe = true;

	mACIA.Reset();

	mStatus2 = 0x07;
	mpIRQController->Negate(mIRQBit, false);

	UpdateControlState();
	OnSCSIControlStateChanged(mSCSIBus.GetBusState());
}

void ATMIOEmulator::ColdReset() {
	mStatus1 = 0xBF;		// D6=0 because printer is never busy
	mStatus2 = 0x07;

	memset(mRAM, 0x00, sizeof mRAM);

	WarmReset();
}

void ATMIOEmulator::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;

	ATMemoryHandlerTable handlers={};
	handlers.mpDebugReadHandler = OnDebugRead;
	handlers.mpReadHandler = OnRead;
	handlers.mpWriteHandler = OnWrite;
	handlers.mpThis = this;
	handlers.mbPassAnticReads = true;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;

	mpMemLayerPBI = mpMemMan->CreateLayer(kATMemoryPri_PBISelect+1, handlers, 0xD1, 0x01);
	mpMemMan->SetLayerName(mpMemLayerPBI, "MIO I/O");
	mpMemMan->EnableLayer(mpMemLayerPBI, true);

	mpMemLayerRAM = mpMemMan->CreateLayer(kATMemoryPri_PBI, mRAM, 0xD6, 0x01, false);
	mpMemMan->SetLayerName(mpMemLayerRAM, "MIO RAM");

	mpMemLayerFirmware = mpMemMan->CreateLayer(kATMemoryPri_PBI, mFirmware, 0xD8, 0x08, true);
	mpMemMan->SetLayerName(mpMemLayerFirmware, "MIO ROM");
	mpMemMan->EnableLayer(mpMemLayerFirmware, true);
}

bool ATMIOEmulator::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	switch(index) {
	case 0:
		lo = 0xD100;
		hi = 0xD1FF;
		return true;

	case 1:
		lo = 0xD600;
		hi = 0xD6FF;
		return true;

	default:
		return false;
	}
}

void ATMIOEmulator::InitFirmware(ATFirmwareManager *fwman) {
	mpFwMan = fwman;

	ReloadFirmware();
}

bool ATMIOEmulator::ReloadFirmware() {
	// We only need an 8K firmware (64Kbit), but it's common to see MIO images stored in EPROMs
	// bigger than that, with only part used. We use a 16K buffer to read and check if the first
	// 8K is empty; if so, then we use the second 8K instead. MIO11OS.ROM is laid out this way.
	constexpr uint32 kMaxSize = 16384;
	vdblock<uint8> tmp(kMaxSize);

	memset(tmp.data(), 0xFF, kMaxSize);

	uint32 actual = 0;
	mpFwMan->LoadFirmware(mpFwMan->GetCompatibleFirmware(kATFirmwareType_MIO), tmp.data(), 0, kMaxSize, nullptr, &actual, nullptr, nullptr, &mbFirmwareUsable);

	// check if the first 8K is blank
	bool first8KBlank = true;
	const uint8 c = tmp[0];
	for(uint32 i=1; i<8192; ++i) {
		if (tmp[i] != c) {
			first8KBlank = false;
			break;
		}
	}

	// save off the old firmware checksum
	vduint128 checksum = VDHash128(mFirmware, sizeof mFirmware);

	// choose the first 8K if it is not blank, else use second 8K
	memcpy(mFirmware, tmp.data() + (first8KBlank ? 8192 : 0), sizeof mFirmware);

	// re-checksum and check for a change
	return checksum != VDHash128(mFirmware, sizeof mFirmware);
}

ATDeviceFirmwareStatus ATMIOEmulator::GetFirmwareStatus() const {
	return mbFirmwareUsable ? ATDeviceFirmwareStatus::OK : ATDeviceFirmwareStatus::Missing;
}

void ATMIOEmulator::InitIRQSource(ATIRQController *irqc) {
	mpIRQController = irqc;
	mIRQBit = irqc->AllocateIRQ();
}

void ATMIOEmulator::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;

	mSCSIBus.Init(sch);
	mACIA.Init(sch, slowsch);
}

void ATMIOEmulator::InitIndicators(IATDeviceIndicatorManager *r) {
	mpUIRenderer = r;
}

void ATMIOEmulator::GetSnapshotStatus(ATSnapshotStatus& status) const {
	mSCSIBus.GetSnapshotStatus(status);
}

struct ATSaveStateMIO final : public ATSnapExchangeObject<ATSaveStateMIO, "ATSaveStateMIO"> {
public:
	template<ATExchanger T>
	void Exchange(T& ex);

	uint8 mArchDataOut = 0;

	uint8 mArchRAMPage = 0;
	uint8 mArchControl1 = 0;
	uint8 mArchControl2 = 0;

	vdrefptr<IATObjectState> mpACIAState;
	vdrefptr<ATSaveStateMemoryBuffer> mpRAM;
};

template<ATExchanger T>
void ATSaveStateMIO::Exchange(T& ex) {
	ex.Transfer("arch_data_out", &mArchDataOut);

	ex.Transfer("arch_ram_page", &mArchRAMPage);
	ex.Transfer("arch_control_1", &mArchControl1);
	ex.Transfer("arch_control_2", &mArchControl2);

	ex.Transfer("acia_state", &mpACIAState);
	ex.Transfer("ram", &mpRAM);
}

void ATMIOEmulator::LoadState(const IATObjectState *state, ATSnapshotContext& ctx) {
	if (!state) {
		const ATSaveStateMIO kDefaultState {};

		return LoadState(&kDefaultState, ctx);
	}

	const ATSaveStateMIO& mstate = atser_cast<const ATSaveStateMIO&>(*state);
	
	// we need to update the SCSI bus output, but only if the data bus is being driven
	// from the incoming SCSI I/O line, and without the ACK side effects of the normal
	// update.
	mDataOut = mstate.mArchDataOut;

	if (mStatus1 & 0x04)
		mSCSIBus.SetControl(0, (~mDataOut & 0xff), 0xff);

	// Restore $D1FE. We do need to dodge the side effect if strobe gets pulled low, so
	// pre-assert strobe so a trailing edge is not possible.
	mbLastPrinterStrobe = true;
	OnWrite(this, 0xD1FE, mstate.mArchControl1);

	// apply PBI ROM bank to $D1FF -- no side effects so reuse the write path
	OnWrite(this, 0xD1FF, mstate.mArchControl2);

	// restore lower 8 bits of RAM bank
	SetRAMPAGE(mstate.mArchRAMPage + (mRAMPAGE & 0xF00));

	mACIA.LoadState(mstate.mpACIAState);

	memset(mRAM, 0xFF, sizeof mRAM);

	if (mstate.mpRAM) {
		const auto& readBuffer = mstate.mpRAM->GetReadBuffer();
		
		if (!readBuffer.empty())
			memcpy(mRAM, readBuffer.data(), std::min<size_t>(readBuffer.size(), sizeof mRAM));
	}
}

vdrefptr<IATObjectState> ATMIOEmulator::SaveState(ATSnapshotContext& ctx) const {
	vdrefptr state { new ATSaveStateMIO };

	// Memory state
	state->mArchRAMPage = mRAMPAGE;

	// SCSI port state
	state->mArchDataOut = mDataOut;

	// reconstitute equivalent control register values from state
	state->mArchRAMPage = (uint8)mRAMPAGE;

	state->mArchControl1
		= (mbPrinterIRQEnabled ? 0x80 : 0x00)
		+ (mbLastPrinterStrobe ? 0x40 : 0x00)
		+ (mbRAMEnabled ? 0x20 : 0x00)
		+ (mbSCSISelAsserted ? 0x10 : 0x00)
		+ ((mRAMPAGE >> 8) & 0x0F);

	state->mArchControl2 = mPBIBANK >= 0 ? 1 << mPBIBANK : 0;

	state->mpACIAState = mACIA.SaveState();

	state->mpRAM = new ATSaveStateMemoryBuffer;
	state->mpRAM->mpDirectName = L"mio-ram.bin";
	state->mpRAM->GetWriteBuffer().assign(std::begin(mRAM), std::end(mRAM));

	return state;
}

IATDeviceBus *ATMIOEmulator::GetDeviceBus(uint32 index) {
	switch(index) {
		case 0:
			return &mSerialBus;

		case 1:
			return this;

		case 2:
			return &mParallelBus;

		default:
			return nullptr;
	}
}

const wchar_t *ATMIOEmulator::GetBusName() const {
	return L"SCSI Bus";
}

const char *ATMIOEmulator::GetBusTag() const {
	return "scsibus";
}

const char *ATMIOEmulator::GetSupportedType(uint32 index) {
	switch(index) {
		case 0: return "harddisk";
		default:
			return nullptr;
	}
}

void ATMIOEmulator::GetChildDevices(vdfastvector<IATDevice *>& devs) {
	for(auto it = mSCSIDisks.begin(), itEnd = mSCSIDisks.end();
		it != itEnd;
		++it)
	{
		const SCSIDiskEntry& ent = *it;

		devs.push_back(vdpoly_cast<IATDevice *>(ent.mpDisk));
	}
}

void ATMIOEmulator::GetChildDevicePrefix(uint32 index, VDStringW& s) {
	if (index < mSCSIDisks.size())
		s.sprintf(L"SCSI ID %u: ", index);
}

void ATMIOEmulator::AddChildDevice(IATDevice *dev) {
	IATBlockDevice *disk = vdpoly_cast<IATBlockDevice *>(dev);
	if (disk) {
		VDASSERT(vdpoly_cast<IATDevice *>(disk));

		if (mSCSIDisks.size() >= 8)
			return;

		vdrefptr<IATSCSIDiskDevice> scsidev;
		ATCreateSCSIDiskDevice(disk, ~scsidev);

		const uint32 id = (uint32)mSCSIDisks.size();

		SCSIDiskEntry entry = { dev, scsidev, disk };
		mSCSIDisks.push_back(entry);
		dev->AddRef();
		dev->SetParent(this, 1);
		scsidev->AddRef();
		disk->AddRef();

		scsidev->SetBlockSize(256);
		scsidev->SetUIRenderer(mpUIRenderer);

		mSCSIBus.AttachDevice(id, scsidev);
	}
}

void ATMIOEmulator::RemoveChildDevice(IATDevice *dev) {
	IATBlockDevice *disk = vdpoly_cast<IATBlockDevice *>(dev);

	if (!disk)
		return;

	for(auto it = mSCSIDisks.begin(), itEnd = mSCSIDisks.end();
		it != itEnd;
		++it)
	{
		const SCSIDiskEntry& ent = *it;

		if (ent.mpDisk == disk) {
			dev->SetParent(nullptr, 0);
			mSCSIBus.DetachDevice(ent.mpSCSIDevice);
			ent.mpDisk->Release();
			ent.mpSCSIDevice->Release();
			ent.mpDevice->Release();

			const uint32 eraseIndex = (uint32)(it - mSCSIDisks.begin());
			mSCSIDisks.erase(it);

			for(uint32 i=eraseIndex; i<7; ++i)
				mSCSIBus.SwapDevices(i, i+1);
		}
	}
}

void ATMIOEmulator::OnSCSIControlStateChanged(uint32 state) {
	// Check for state changes:
	//	- IO change -> flip data bus driver
	//	- REQ deassert -> ACK deassert
	//
	// Note that we must NOT issue control changes from here. It will screw up the
	// SCSI bus!
	if (((mPrevSCSIState ^ state) & kATSCSICtrlState_IO) || (mPrevSCSIState & ~state & kATSCSICtrlState_REQ))
		mpScheduler->SetEvent(1, this, 1, mpEventUpdateSCSIBus);

	mPrevSCSIState = state;

	mDataIn = (uint8)~state;

	mStatus1 |= 0xA7;

	if (state & kATSCSICtrlState_CD)
		mStatus1 -= 0x01;

	if (state & kATSCSICtrlState_MSG)
		mStatus1 -= 0x02;

	if (state & kATSCSICtrlState_IO)
		mStatus1 -= 0x04;

	if (state & kATSCSICtrlState_BSY)
		mStatus1 -= 0x20;

	if (state & kATSCSICtrlState_REQ)
		mStatus1 -= 0x80;
}

void ATMIOEmulator::OnScheduledEvent(uint32 id) {
	mpEventUpdateSCSIBus = nullptr;

	// IO controls data bus driver
	uint32 newState = 0;

	if (mPrevSCSIState & kATSCSICtrlState_IO)
		newState |= 0xff;
	else
		newState |= ~mDataOut & 0xff;

	// sense deassert REQ -> auto deassert ACK
	uint32 newMask = 0xff;
	if (!(mPrevSCSIState & kATSCSICtrlState_REQ))
		newMask |= kATSCSICtrlState_ACK;

	if (newMask)
		mSCSIBus.SetControl(0, newState, newMask);
}

void ATMIOEmulator::UpdateControlState() {
	ATDeviceSerialStatus status {};

	if (mpSerialDevice)
		status = mpSerialDevice->GetStatus();

	OnControlStateChanged(status);
}

void ATMIOEmulator::OnControlStateChanged(const ATDeviceSerialStatus& status) {
	mStatus2 &= ~7;

	if (status.mbDataSetReady)
		mStatus2 += 0x02;

	if (status.mbClearToSend)
		mStatus2 += 0x04;

	if (status.mbCarrierDetect)
		mStatus2 += 0x01;
}

sint32 ATMIOEmulator::OnDebugRead(void *thisptr0, uint32 addr) {
	const auto thisptr = (ATMIOEmulator *)thisptr0;

	if ((addr - 0xD1C0) < 0x20)
		return thisptr->mACIA.DebugReadByte(addr);

	if ((addr - 0xD1E0) < 0x20) {
		switch(addr & 3) {
			case 0:		// reset SCSI bus
				break;

			case 1:
				return thisptr->mDataIn;

			case 2:
				return thisptr->mStatus1;

			case 3:
				return thisptr->mStatus2;
		}
	}

	return -1;
}

sint32 ATMIOEmulator::OnRead(void *thisptr0, uint32 addr) {
	const auto thisptr = (ATMIOEmulator *)thisptr0;

	if ((addr - 0xD1C0) < 0x20)
		return thisptr->mACIA.ReadByte(addr);

	if ((addr - 0xD1E0) < 0x20) {
		switch(addr & 3) {
			case 0:		// reset SCSI bus
				thisptr->mSCSIBus.SetControl(0, kATSCSICtrlState_RST, kATSCSICtrlState_RST);
				break;

			case 1: {
				uint8 value = thisptr->mDataIn;

				thisptr->mSCSIBus.SetControl(0, kATSCSICtrlState_ACK, kATSCSICtrlState_ACK);

				return value;
			}

			case 2:
				// clear RST
				thisptr->mSCSIBus.SetControl(0, 0, kATSCSICtrlState_RST);
				return thisptr->mStatus1;

			case 3:
				return thisptr->mStatus2;
		}
	}

	return -1;
}

bool ATMIOEmulator::OnWrite(void *thisptr0, uint32 addr, uint8 value) {
	const auto thisptr = (ATMIOEmulator *)thisptr0;

	if ((addr - 0xD1C0) < 0x20) {
		thisptr->mACIA.WriteByte(addr, value);
		return false;
	}

	if ((addr - 0xD1E0) < 0x20) {
		switch(addr & 3) {
			case 0:		// RAM page A8-A15
				thisptr->SetRAMPAGE((thisptr->mRAMPAGE & 0xF00) + value);
				break;

			case 1:
				// Writing this register sets ACK.
				// The MIO does not invert data for the SCSI bus, so we need to invert it here.
				thisptr->mDataOut = value;

				// check if REQ is set
				if (thisptr->mStatus1 & 0x80) {
					// nope -- don't assert ACK, only possibly output state
					if (thisptr->mStatus1 & 0x04)
						thisptr->mSCSIBus.SetControl(0, (~value & 0xff), 0xff);
				} else {
					// yes -- assert AC, and check output latch state to see if we are driving data bus
					if (thisptr->mStatus1 & 0x04)
						thisptr->mSCSIBus.SetControl(0, (~value & 0xff) + kATSCSICtrlState_ACK, 0xff + kATSCSICtrlState_ACK);
					else
						thisptr->mSCSIBus.SetControl(0, kATSCSICtrlState_ACK, kATSCSICtrlState_ACK);
				}
				break;

			case 2:
				// D0-D3 controls RAM A16-A19
				thisptr->SetRAMPAGE((thisptr->mRAMPAGE & 0x0FF) + ((uint32)value << 8));

				// D4 controls SCSI SEL
				thisptr->mbSCSISelAsserted = (value & 0x10) != 0;
				thisptr->mSCSIBus.SetControl(0, thisptr->mbSCSISelAsserted ? kATSCSICtrlState_SEL : 0, kATSCSICtrlState_SEL);

				// D5=1 enables RAM access
				thisptr->mbRAMEnabled = (value & 0x20) != 0;
				thisptr->mpMemMan->EnableLayer(thisptr->mpMemLayerRAM, thisptr->mbRAMEnabled);

				// D7 controls printer BUSY IRQ
				thisptr->SetPrinterIRQEnabled((value & 0x80) != 0);

				// D6 controls printer STROBE
				if (value & 0x40) {
					if (!thisptr->mbLastPrinterStrobe) {
						uint8 c = thisptr->mDataOut;

						g_ATLCParPrint("Sending byte to printer: $%02X\n", c);

						if (auto *printer = thisptr->mParallelBus.GetChild<IATPrinterOutput>())
							printer->WriteRaw(&c, 1);
						else {
							if (!thisptr->mpPrinterOutput)
								thisptr->mpPrinterOutput = thisptr->GetService<IATPrinterOutputManager>()->CreatePrinterOutput();

							thisptr->mpPrinterOutput->WriteRaw(&c, 1);
						}
					}
				}

				thisptr->mbLastPrinterStrobe = (value & 0x40) != 0;
				break;

			case 3:
				{
					// This is driven by the following logic:
					// A11 = /D0 * /D2
					// A12 = /D0 * /D1

					static const sint8 kBankMap[] = { -1, 0, 1, 0, 2, 0, 0, 0, 3, 0, 1, 0, 2, 0, 0, 0 };

					thisptr->SetPBIBANK(kBankMap[(value >> 2) & 15]);
				}
				break;
		}

		return true;
	}

	return false;
}

void ATMIOEmulator::OnACIAControlStateChanged(bool rts, bool dtr) {
	if (mbRTS != rts || mbDTR != dtr) {
		mbRTS = rts;
		mbDTR = dtr;

		UpdateSerialControlLines();
	}
}

void ATMIOEmulator::OnACIAIRQStateChanged(bool active) {
	if (mbACIAIRQActive == active)
		return;

	mbACIAIRQActive = active;

	if (active) {
		if (!(mStatus2 & 0x08)) {
			mStatus2 |= 0x10;
			mpIRQController->Assert(mIRQBit, false);
		}

	} else {
		if (!(mStatus2 & 0x08)) {
			mStatus2 &= ~0x10;
			mpIRQController->Negate(mIRQBit, false);
		}
	}
}

void ATMIOEmulator::OnACIAReceiveReady() {
	if (mpSerialDevice) {
		uint32 baudRate;
		uint8 c;
		if (mpSerialDevice->Read(baudRate, c))
			mACIA.ReceiveByte(c, baudRate);
	}
}

void ATMIOEmulator::OnACIATransmit(uint8 data, uint32 baudRate) {
	if (mpSerialDevice)
		mpSerialDevice->Write(baudRate, data);
}

void ATMIOEmulator::UpdateSerialControlLines() {
	if (mpSerialDevice) {
		ATDeviceSerialTerminalState state;
		state.mbDataTerminalReady = mbDTR;
		state.mbRequestToSend = mbRTS;

		mpSerialDevice->SetTerminalState(state);
	}
}

void ATMIOEmulator::SetPBIBANK(sint8 value) {
	if (mPBIBANK == value)
		return;

	mPBIBANK = value;

	if (value >= 0) {
		uint32 offset = value << 11;

		mpMemMan->SetLayerMemory(mpMemLayerFirmware, mFirmware + offset);
		mpMemMan->EnableLayer(mpMemLayerFirmware, true);
	} else {
		mpMemMan->EnableLayer(mpMemLayerFirmware, false);
	}
}

void ATMIOEmulator::SetRAMPAGE(uint32 page) {
	page &= 0xFFF;	// 4K pages (1MB)

	if (mRAMPAGE == page)
		return;

	mRAMPAGE = page;

	mpMemMan->SetLayerMemory(mpMemLayerRAM, mRAM + ((uint32)page << 8));
}

void ATMIOEmulator::SetPrinterIRQEnabled(bool enabled) {
	if (mbPrinterIRQEnabled == enabled)
		return;

	mbPrinterIRQEnabled = enabled;

	if (mbPrinterIRQEnabled) {
		// Currently the printer never goes BUSY, so if the IRQ is enabled, it is
		// active.
		if (!(mStatus2 & 0x08)) {
			mStatus2 |= 0x18;

			if (!mbACIAIRQActive)
				mpIRQController->Assert(mIRQBit, false);
		}
	} else {
		if (mStatus2 & 0x08) {
			mStatus2 &= ~0x08;

			if (!mbACIAIRQActive) {
				mStatus2 &= ~0x10;
				mpIRQController->Negate(mIRQBit, false);
			}
		}
	}
}
