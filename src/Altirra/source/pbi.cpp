#include <stdafx.h>
#include <vd2/system/bitmath.h>
#include "memorymanager.h"
#include "pbi.h"

ATPBIManager::ATPBIManager()
	: mSelRegister(0)
	, mpSelDevice(NULL)
{
	std::fill(mpSelectList, mpSelectList + 8, (IATPBIDevice *)NULL);
}

ATPBIManager::~ATPBIManager() {
	Shutdown();
}

void ATPBIManager::Init(ATMemoryManager *memman) {
	mpMemMan = memman;

	ATMemoryHandlerTable handlers = {};
	handlers.mbPassAnticReads = true;
	handlers.mbPassReads = true;
	handlers.mbPassWrites = true;
	handlers.mpThis = this;
	handlers.mpWriteHandler = OnControlWrite;
	mpMemLayerPBISel = mpMemMan->CreateLayer(kATMemoryPri_PBISelect, handlers, 0xD1, 0x01);
	mpMemMan->EnableLayer(mpMemLayerPBISel, kATMemoryAccessMode_CPUWrite, true);
}

void ATPBIManager::Shutdown() {
	if (mpSelDevice)
		mpSelDevice->Select(false);

	while(!mDevices.empty()) {
		IATPBIDevice *dev = mDevices.back();
		mDevices.pop_back();

		dev->DetachDevice();
	}

	if (mpMemLayerPBISel) {
		mpMemMan->EnableLayer(mpMemLayerPBISel, false);
		mpMemMan->DeleteLayer(mpMemLayerPBISel);
		mpMemLayerPBISel = NULL;
	}

	mpMemMan = NULL;
}

bool ATPBIManager::IsROMOverlayActive() const {
	return mpSelDevice && mpSelDevice->IsPBIOverlayActive();
}

void ATPBIManager::AddDevice(IATPBIDevice *dev) {
	mDevices.push_back(dev);

	dev->AttachDevice(mpMemMan);

	RebuildSelList();
}

void ATPBIManager::RemoveDevice(IATPBIDevice *dev) {
	if (mpSelDevice == dev) {
		mpSelDevice->Select(false);
		mpSelDevice = NULL;
	}

	Devices::iterator it(std::find(mDevices.begin(), mDevices.end(), dev));

	if (it != mDevices.end()) {
		dev->DetachDevice();
		mDevices.erase(it);
	}

	RebuildSelList();
}

void ATPBIManager::ColdReset() {
	Select(0);

	for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end());
		it != itEnd;
		++it)
	{
		IATPBIDevice *dev = *it;

		dev->ColdReset();
	}
}

void ATPBIManager::WarmReset() {
	Select(0);

	for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end());
		it != itEnd;
		++it)
	{
		IATPBIDevice *dev = *it;

		dev->WarmReset();
	}
}

void ATPBIManager::Select(uint8 selval) {
	if (mSelRegister == selval)
		return;

	mSelRegister = selval;

	IATPBIDevice *sel = NULL;

	if (selval)
		sel = mpSelectList[VDFindHighestSetBit(selval)];

	if (mpSelDevice != sel) {
		if (mpSelDevice)
			mpSelDevice->Select(false);

		mpSelDevice = sel;

		if (mpSelDevice)
			mpSelDevice->Select(true);
	}
}

void ATPBIManager::RebuildSelList() {
	std::fill(mpSelectList, mpSelectList + 8, (IATPBIDevice *)NULL);

	IATPBIDevice *sel = NULL;

	for(Devices::const_iterator it(mDevices.begin()), itEnd(mDevices.end()); it != itEnd;
		++it)
	{
		IATPBIDevice *dev = *it;

		ATPBIDeviceInfo devInfo;
		dev->GetDeviceInfo(devInfo);

		if (devInfo.mDeviceId) {
			mpSelectList[VDFindHighestSetBit(devInfo.mDeviceId)] = dev;

			if (mSelRegister & devInfo.mDeviceId)
				sel = dev;
		}
	}

	if (mpSelDevice != sel) {
		if (mpSelDevice)
			mpSelDevice->Select(false);

		mpSelDevice = sel;

		if (mpSelDevice)
			mpSelDevice->Select(true);
	}
}

bool ATPBIManager::OnControlWrite(void *thisptr, uint32 addr, uint8 value) {
	if (addr == 0xD1FF) {
		((ATPBIManager *)thisptr)->Select(value);
		return true;
	}

	return false;
}
