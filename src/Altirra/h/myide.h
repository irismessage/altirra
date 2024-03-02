//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2012 Avery Lee
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

#ifndef f_AT_MYIDE_H
#define f_AT_MYIDE_H

#include "flash.h"

class ATMemoryManager;
class ATMemoryLayer;
class ATIDEEmulator;
class ATScheduler;
class ATSimulator;
class IATUIRenderer;
class ATFirmwareManager;

class ATMyIDEEmulator {
	ATMyIDEEmulator(const ATMyIDEEmulator&) = delete;
	ATMyIDEEmulator& operator=(const ATMyIDEEmulator&) = delete;
public:
	ATMyIDEEmulator();
	~ATMyIDEEmulator();

	bool IsVersion2() const { return mbVersion2; }
	bool IsFlashDirty() const { return mFlash.IsDirty(); }
	bool IsLeftCartEnabled() const;
	bool IsUsingD5xx() const { return mbUseD5xx; }

	void Init(ATMemoryManager *memman, IATUIRenderer *uir, ATScheduler *sch, ATSimulator *sim, bool used5xx, bool v2, bool v2ex);
	void Shutdown();

	void SetIDEImage(ATIDEEmulator *ide);

	bool LoadFirmware(const void *ptr, uint32 len);
	bool LoadFirmware(ATFirmwareManager& fwmgr, uint64 id);
	void SaveFirmware(const wchar_t *path);

	void ColdReset();

protected:
	static sint32 OnDebugReadByte_CCTL(void *thisptr, uint32 addr);
	static sint32 OnReadByte_CCTL(void *thisptr, uint32 addr);
	static bool OnWriteByte_CCTL(void *thisptr, uint32 addr, uint8 value);

	static sint32 OnDebugReadByte_CCTL_V2(void *thisptr, uint32 addr);
	static sint32 OnReadByte_CCTL_V2(void *thisptr, uint32 addr);
	static bool OnWriteByte_CCTL_V2(void *thisptr, uint32 addr, uint8 value);

	static sint32 ReadByte_Cart_V2(void *thisptr0, uint32 address);
	static bool WriteByte_Cart_V2(void *thisptr0, uint32 address, uint8 value);

	void UpdateIDEReset();

	void SetCartBank(int bank);
	void SetCartBank2(int bank);

	void UpdateCartBank();
	void UpdateCartBank2();

	ATMemoryManager *mpMemMan;
	ATMemoryLayer *mpMemLayerIDE;
	ATMemoryLayer *mpMemLayerLeftCart;
	ATMemoryLayer *mpMemLayerLeftCartFlash;
	ATMemoryLayer *mpMemLayerRightCart;
	ATMemoryLayer *mpMemLayerRightCartFlash;
	ATIDEEmulator *mpIDE;
	IATUIRenderer *mpUIRenderer;
	ATSimulator *mpSim;
	bool mbCFPower;
	bool mbCFReset;
	bool mbCFAltReg;
	bool mbVersion2;
	bool mbVersion2Ex;
	bool mbUseD5xx;

	// MyIDE II control registers
	int	mCartBank;
	int	mCartBank2;
	uint8	mLeftPage;
	uint8	mRightPage;
	uint32	mKeyHolePage;
	uint8	mControl;

	ATFlashEmulator	mFlash;

	VDALIGN(4) uint8 mFirmware[0x80000];
	VDALIGN(4) uint8 mRAM[0x80000];
};

#endif
