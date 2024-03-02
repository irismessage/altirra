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

#ifndef f_AT_KMKJZIDE_H
#define f_AT_KMKJZIDE_H

#include "pbi.h"
#include "flash.h"
#include "rtcv3021.h"

class ATMemoryManager;
class ATMemoryLayer;
class ATIDEEmulator;
class IATUIRenderer;

class ATKMKJZIDE : public IATPBIDevice {
	ATKMKJZIDE(const ATKMKJZIDE&);
	ATKMKJZIDE& operator=(const ATKMKJZIDE&);

public:
	ATKMKJZIDE(bool version2);
	~ATKMKJZIDE();

	bool IsVersion2() const { return mbVersion2; }
	bool IsMainFlashDirty() const { return mFlashCtrl.IsDirty(); }
	bool IsSDXFlashDirty() const { return mSDXCtrl.IsDirty(); }

	void LoadFirmware(bool sdx, const void *ptr, uint32 len);
	void LoadFirmware(bool sdx, const wchar_t *path);
	void SaveFirmware(bool sdx, const wchar_t *path);

	void Init(ATIDEEmulator *ide, ATScheduler *sch, IATUIRenderer *uir);
	void Shutdown();

	void AttachDevice(ATMemoryManager *memman);
	void DetachDevice();

	void GetDeviceInfo(ATPBIDeviceInfo& devInfo) const;
	void Select(bool enable);

	void ColdReset();
	void WarmReset();

	void LoadNVRAM();
	void SaveNVRAM();

protected:
	static sint32 OnControlDebugRead(void *thisptr, uint32 addr);
	static sint32 OnControlRead(void *thisptr, uint32 addr);
	static bool OnControlWrite(void *thisptr, uint32 addr, uint8 value);

	static sint32 OnFlashRead(void *thisptr, uint32 addr);
	static bool OnFlashWrite(void *thisptr, uint32 addr, uint8 value);

	static sint32 OnSDXRead(void *thisptr, uint32 addr);
	static bool OnSDXWrite(void *thisptr, uint32 addr, uint8 value);

	void UpdateMemoryLayersFlash();
	void UpdateMemoryLayersSDX();

	ATIDEEmulator	*mpIDE;
	IATUIRenderer	*mpUIRenderer;
	ATMemoryManager *mpMemMan;
	ATMemoryLayer	*mpMemLayerControl;
	ATMemoryLayer	*mpMemLayerFlash;
	ATMemoryLayer	*mpMemLayerFlashControl;
	ATMemoryLayer	*mpMemLayerRAM;
	ATMemoryLayer	*mpMemLayerSDX;
	ATMemoryLayer	*mpMemLayerSDXControl;

	uint8	mHighDataLatch;
	bool	mbVersion2;
	bool	mbSDXEnabled;
	bool	mbSelected;

	uint32	mFlashBankOffset;
	uint32	mSDXBankOffset;

	ATFlashEmulator	mFlashCtrl;
	ATFlashEmulator	mSDXCtrl;

	ATRTCV3021Emulator mRTC;

	__declspec(align(4)) uint8	mRAM[0x8000];
	__declspec(align(4)) uint8	mFlash[0x20000];
	__declspec(align(4)) uint8	mSDX[0x80000];
};

#endif
