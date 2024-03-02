#include <stdafx.h>
#include "myide.h"
#include "memorymanager.h"
#include "ide.h"

ATMyIDEEmulator::ATMyIDEEmulator()
	: mpMemMan(NULL)
	, mpMemLayerIDE(NULL)
	, mpIDE(NULL)
{
}

ATMyIDEEmulator::~ATMyIDEEmulator() {
}

void ATMyIDEEmulator::Init(ATMemoryManager *memman, ATIDEEmulator *ide, bool used5xx) {
	mpMemMan = memman;
	mpIDE = ide;

	ATMemoryHandlerTable handlerTable = {};

	handlerTable.mpThis = mpIDE;
	handlerTable.mbPassAnticReads = true;
	handlerTable.mbPassReads = true;
	handlerTable.mbPassWrites = true;
	handlerTable.mpDebugReadHandler = OnDebugReadByte;
	handlerTable.mpReadHandler = OnReadByte;
	handlerTable.mpWriteHandler = OnWriteByte;
	mpMemLayerIDE = mpMemMan->CreateLayer(kATMemoryPri_Cartridge1 - 1, handlerTable, used5xx ? 0xD5 : 0xD1, 0x01);
	mpMemMan->EnableLayer(mpMemLayerIDE, true);
}

void ATMyIDEEmulator::Shutdown() {
	if (mpMemLayerIDE) {
		mpMemMan->DeleteLayer(mpMemLayerIDE);
		mpMemLayerIDE = NULL;
	}

	mpMemMan = NULL;
	mpIDE = NULL;
}

sint32 ATMyIDEEmulator::OnDebugReadByte(void *thisptr, uint32 addr) {
	return (uint8)((ATIDEEmulator *)thisptr)->DebugReadByte((uint8)addr);
}

sint32 ATMyIDEEmulator::OnReadByte(void *thisptr, uint32 addr) {
	return (uint8)((ATIDEEmulator *)thisptr)->ReadByte((uint8)addr);
}

bool ATMyIDEEmulator::OnWriteByte(void *thisptr, uint32 addr, uint8 value) {
	((ATIDEEmulator *)thisptr)->WriteByte(addr, value);
	return true;
}
