//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2014 Avery Lee
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

#ifndef f_AT_FIRMWAREMANAGER_H
#define f_AT_FIRMWAREMANAGER_H

#include <vd2/system/VDString.h>
#include <vd2/system/vdstl.h>

enum ATFirmwareId {
	kATFirmwareId_Invalid,
	kATFirmwareId_NoKernel,
	kATFirmwareId_Kernel_LLE,
	kATFirmwareId_Kernel_LLEXL,
	kATFirmwareId_Kernel_HLE,
	kATFirmwareId_Basic_ATBasic,
	kATFirmwareId_5200_LLE,
	kATFirmwareId_5200_NoCartridge,
	kATFirmwareId_U1MB_Recovery,
	kATFirmwareId_KMKJZIDE_NoBIOS,
	kATFirmwareId_850Relocator,
	kATFirmwareId_850Handler,
	kATFirmwareId_1030Firmware,
	kATFirmwareId_NoMIO,
	kATFirmwareId_NoBlackBox,
	kATFirmwareId_PredefCount1,
	kATFirmwareId_PredefCount = kATFirmwareId_PredefCount1 - 1,
	kATFirmwareId_Custom = 0x10000
};

enum ATFirmwareType {
	kATFirmwareType_Unknown,
	kATFirmwareType_Kernel800_OSA,
	kATFirmwareType_Kernel800_OSB,
	kATFirmwareType_KernelXL,
	kATFirmwareType_KernelXEGS,
	kATFirmwareType_Kernel5200,
	kATFirmwareType_Kernel1200XL,
	kATFirmwareType_Basic,
	kATFirmwareType_5200Cartridge,
	kATFirmwareType_U1MB,
	kATFirmwareType_MyIDE,
	kATFirmwareType_MyIDE2,
	kATFirmwareType_SIDE,
	kATFirmwareType_SIDE2,
	kATFirmwareType_KMKJZIDE,
	kATFirmwareType_KMKJZIDE2,
	kATFirmwareType_KMKJZIDE2_SDX,
	kATFirmwareType_BlackBox,
	kATFirmwareType_Game,
	kATFirmwareType_MIO,
	kATFirmwareType_850Relocator,
	kATFirmwareType_850Handler,
	kATFirmwareType_1030Firmware,
	kATFirmwareTypeCount
};

enum ATFirmwareFlags {
	kATFirmwareFlags_None,
	kATFirmwareFlags_InvertOPTION = 0x00000001,
	kATFirmwareFlags_All = 0xFFFFFFFFUL
};

struct ATFirmwareInfo {
	uint64 mId;
	uint32 mFlags;
	bool mbVisible;
	bool mbAutoselect;
	VDStringW mName;
	VDStringW mPath;
	ATFirmwareType mType;
};

VDMOVE_CAPABLE_INLINE(ATFirmwareInfo) {
	dst.mId = src.mId;
	dst.mFlags = src.mFlags;
	dst.mbVisible = src.mbVisible;
	vdmove(dst.mName, src.mName);
	vdmove(dst.mPath, src.mPath);
	dst.mType = src.mType;
}

void ATSetFirmwarePathPortabilityMode(bool portable);

const char *ATGetFirmwareTypeName(ATFirmwareType type);
ATFirmwareType ATGetFirmwareTypeFromName(const char *type);
uint64 ATGetFirmwareIdFromPath(const wchar_t *path);

class ATFirmwareManager {
	ATFirmwareManager(const ATFirmwareManager&);
	ATFirmwareManager& operator=(const ATFirmwareManager&);
public:
	ATFirmwareManager();
	~ATFirmwareManager();

	bool GetFirmwareInfo(uint64 id, ATFirmwareInfo& fwinfo) const;
	void GetFirmwareList(vdvector<ATFirmwareInfo>& firmwares) const;
	uint64 GetCompatibleFirmware(ATFirmwareType type) const;
	uint64 GetFirmwareOfType(ATFirmwareType type, bool allowInternal) const;

	VDStringW GetFirmwareRefString(uint64 id) const;
	uint64 GetFirmwareByRefString(const wchar_t *refstr) const;

	uint64 GetDefaultFirmware(ATFirmwareType type) const;
	void SetDefaultFirmware(ATFirmwareType type, uint64 id);

	bool LoadFirmware(uint64 id, void *dst, uint32 offset, uint32 len, bool *changed = nullptr, uint32 *actualLen = nullptr, vdfastvector<uint8> *dstbuf = nullptr);

	void AddFirmware(const ATFirmwareInfo& info);
	void RemoveFirmware(uint64 id);
};

#endif	// f_AT_FIRMWAREMANAGER_H
