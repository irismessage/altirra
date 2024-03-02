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

#ifndef f_AT_SIDE_H
#define f_AT_SIDE_H

#include "flash.h"
#include "rtcds1305.h"

class IATUIRenderer;
class ATMemoryManager;
class ATMemoryLayer;
class ATIDEEmulator;
class ATSimulator;

class ATSIDEEmulator {
	ATSIDEEmulator(const ATSIDEEmulator&);
	ATSIDEEmulator& operator=(const ATSIDEEmulator&);
public:
	ATSIDEEmulator();
	~ATSIDEEmulator();

	bool IsSDXEnabled() const { return mbSDXEnabled; }
	bool IsSDXFlashDirty() const { return mSDXCtrl.IsDirty(); }

	void LoadFirmware(const void *ptr, uint32 len);
	void LoadFirmware(const wchar_t *path);
	void SaveFirmware(const wchar_t *path);

	void Init(ATIDEEmulator *ide, ATScheduler *sch, IATUIRenderer *uir, ATMemoryManager *memman, ATSimulator *sim);
	void Shutdown();

	void ColdReset();

	void LoadNVRAM();
	void SaveNVRAM();

	void DumpRTCStatus();

protected:
	static sint32 OnDebugReadByte(void *thisptr, uint32 addr);
	static sint32 OnReadByte(void *thisptr, uint32 addr);
	static bool OnWriteByte(void *thisptr, uint32 addr, uint8 value);

	static sint32 OnSDXRead(void *thisptr, uint32 addr);
	static bool OnSDXWrite(void *thisptr, uint32 addr, uint8 value);

	void UpdateMemoryLayersSDX();

	ATIDEEmulator *mpIDE;
	IATUIRenderer	*mpUIRenderer;
	ATMemoryManager *mpMemMan;
	ATSimulator *mpSim;
	ATMemoryLayer *mpMemLayerIDE;
	ATMemoryLayer *mpMemLayerSDX;
	ATMemoryLayer *mpMemLayerSDXControl;
	bool	mbSDXEnabled;
	uint32	mSDXBankOffset;
	ATFlashEmulator	mSDXCtrl;
	ATRTCDS1305Emulator mRTC;

	VDALIGN(4) uint8	mSDX[0x80000];
};

#endif
