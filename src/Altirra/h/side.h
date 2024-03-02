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

	bool IsVersion2() const { return mbVersion2; }
	bool IsCartEnabled() const { return (mbSDXEnable && mSDXBank >= 0) || ((mbTopEnable || !mbSDXEnable) && mTopBank >= 0); }
	bool IsSDXFlashDirty() const { return mFlashCtrl.IsDirty(); }

	void LoadFirmware(const void *ptr, uint32 len);
	void LoadFirmware(const wchar_t *path);
	void SaveFirmware(const wchar_t *path);

	void Init(ATIDEEmulator *ide, ATScheduler *sch, IATUIRenderer *uir, ATMemoryManager *memman, ATSimulator *sim, bool version2);
	void Shutdown();

	void SetExternalEnable(bool enable);

	bool IsSDXEnabled() const { return mbSDXEnable; }
	void SetSDXEnabled(bool enable);

	void ResetCartBank();
	void ColdReset();

	void LoadNVRAM();
	void SaveNVRAM();

	void DumpRTCStatus();

protected:
	void SetSDXBank(sint32 bank, bool topEnable);
	void SetTopBank(sint32 bank, bool topRightEnable);

	static sint32 OnDebugReadByte(void *thisptr, uint32 addr);
	static sint32 OnReadByte(void *thisptr, uint32 addr);
	static bool OnWriteByte(void *thisptr, uint32 addr, uint8 value);

	static sint32 OnCartRead(void *thisptr, uint32 addr);
	static sint32 OnCartRead2(void *thisptr, uint32 addr);
	static bool OnCartWrite(void *thisptr, uint32 addr, uint8 value);
	static bool OnCartWrite2(void *thisptr, uint32 addr, uint8 value);

	void UpdateMemoryLayersCart();

	ATIDEEmulator *mpIDE;
	IATUIRenderer	*mpUIRenderer;
	ATMemoryManager *mpMemMan;
	ATSimulator *mpSim;
	ATMemoryLayer *mpMemLayerIDE;
	ATMemoryLayer *mpMemLayerCart;
	ATMemoryLayer *mpMemLayerCart2;
	ATMemoryLayer *mpMemLayerCartControl;
	ATMemoryLayer *mpMemLayerCartControl2;
	bool	mbExternalEnable;
	bool	mbSDXEnable;
	bool	mbTopEnable;
	bool	mbTopRightEnable;
	bool	mbIDEEnabled;
	bool	mbVersion2;
	uint8	mSDXBankRegister;
	uint8	mTopBankRegister;
	sint32	mSDXBank;
	sint32	mTopBank;
	sint32	mBankOffset;
	sint32	mBankOffset2;
	ATFlashEmulator	mFlashCtrl;
	ATRTCDS1305Emulator mRTC;

	VDALIGN(4) uint8	mFlash[0x80000];
};

#endif
