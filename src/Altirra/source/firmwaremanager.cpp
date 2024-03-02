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

#include "stdafx.h"
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/thread.h>
#include <vd2/system/date.h>
#include "firmwaremanager.h"
#include "oshelper.h"
#include "resource.h"

const char *ATGetFirmwareTypeName(ATFirmwareType type) {
	static const char *kTypeNames[]={
		"",
		"kernel800_osa",
		"kernel800_osb",
		"kernelxl",
		"kernelxegs",
		"kernel5200",
		"kernel1200xl",
		"basic",
		"5200cart",
		"u1mb",
		"myide",
		"myide2",
		"side",
		"side2",
		"kmkjzide",
		"kmkjzide2",
		"kmkjzide2_sdx",
		"blackbox",
		"game",
		"mio",
		"850handler",
		"850relocator",
		"1030firmware",
	};
	VDASSERTCT(vdcountof(kTypeNames) == kATFirmwareTypeCount);

	return kTypeNames[type];
}

ATFirmwareType ATGetFirmwareTypeFromName(const char *type) {
	     if (!strcmp(type, "kernel800_osa")) return kATFirmwareType_Kernel800_OSA;
	else if (!strcmp(type, "kernel800_osb")) return kATFirmwareType_Kernel800_OSB;
	else if (!strcmp(type, "kernelxl")) return kATFirmwareType_KernelXL;
	else if (!strcmp(type, "kernelxegs")) return kATFirmwareType_KernelXEGS;
	else if (!strcmp(type, "game")) return kATFirmwareType_Game;
	else if (!strcmp(type, "kernel1200xl")) return kATFirmwareType_Kernel1200XL;
	else if (!strcmp(type, "kernel5200")) return kATFirmwareType_Kernel5200;
	else if (!strcmp(type, "basic")) return kATFirmwareType_Basic;
	else if (!strcmp(type, "5200cart")) return kATFirmwareType_5200Cartridge;
	else if (!strcmp(type, "u1mb")) return kATFirmwareType_U1MB;
	else if (!strcmp(type, "myide")) return kATFirmwareType_MyIDE;
	else if (!strcmp(type, "myide2")) return kATFirmwareType_MyIDE2;
	else if (!strcmp(type, "side")) return kATFirmwareType_SIDE;
	else if (!strcmp(type, "side2")) return kATFirmwareType_SIDE2;
	else if (!strcmp(type, "kmkjzide")) return kATFirmwareType_KMKJZIDE;
	else if (!strcmp(type, "kmkjzide2")) return kATFirmwareType_KMKJZIDE2;
	else if (!strcmp(type, "kmkjzide2_sdx")) return kATFirmwareType_KMKJZIDE2_SDX;
	else if (!strcmp(type, "blackbox")) return kATFirmwareType_BlackBox;
	else if (!strcmp(type, "mio")) return kATFirmwareType_MIO;
	else if (!strcmp(type, "850handler")) return kATFirmwareType_850Handler;
	else if (!strcmp(type, "850relocator")) return kATFirmwareType_850Relocator;
	else if (!strcmp(type, "1030firmware")) return kATFirmwareType_1030Firmware;
	else return kATFirmwareType_Unknown;
}

uint64 ATGetFirmwareIdFromPath(const wchar_t *path) {
	VDStringW relPath;
	if (!VDFileIsRelativePath(path)) {
		relPath = VDFileGetRelativePath(VDGetProgramPath().c_str(), path, false);

		if (!relPath.empty())
			path = relPath.c_str();
	}

	uint64 hash = 14695981039346656037ull;

	while(const wchar_t c = towlower(*path++))
		hash = (hash ^ (unsigned)c) * 1099511628211ull;

	return hash | 0x8000000000000000ull;
}

ATFirmwareManager::ATFirmwareManager() {
}

ATFirmwareManager::~ATFirmwareManager() {
}

bool ATFirmwareManager::GetFirmwareInfo(uint64 id, ATFirmwareInfo& fwinfo) const {
	if (!id)
		return false;

	if (id < kATFirmwareId_Custom) {
		if (id >= kATFirmwareId_PredefCount1)
			return false;

		static const struct {
			bool mbAutoselect;
			bool mbVisible;
			ATFirmwareType mType;
			const wchar_t *mpName;
		} kPredefFirmwares[]={
			{ false, false, kATFirmwareType_KernelXL, L"NoKernel" },
			{ true, true, kATFirmwareType_Kernel800_OSB, L"AltirraOS for 400/800" },
			{ true, true, kATFirmwareType_KernelXL,  L"AltirraOS for XL/XE/XEGS" },
			{ true, true, kATFirmwareType_KernelXL, L"AltirraOS HLE" },
			{ true, true, kATFirmwareType_Basic, L"Altirra BASIC" },
			{ true, true, kATFirmwareType_Kernel5200, L"AltirraOS for 5200" },
			{ true, false, kATFirmwareType_5200Cartridge, L"No5200" },
			{ true, true, kATFirmwareType_U1MB, L"Altirra U1MB Recovery OS" },
			{ true, false, kATFirmwareType_KMKJZIDE, L"Altirra NoBIOS for KMK/JZ IDE" },
			{ true, false, kATFirmwareType_850Relocator, L"Altirra 850 Relocator Firmware" },
			{ true, false, kATFirmwareType_850Handler, L"Altirra 850 Handler Firmware" },
			{ true, true, kATFirmwareType_1030Firmware, L"Altirra 1030 Modem Firmware" },
			{ true, false, kATFirmwareType_MIO, L"Altirra NoFirmware for MIO" },
			{ true, false, kATFirmwareType_BlackBox, L"Altirra NoFirmware for BlackBox" },
		};

		VDASSERTCT(vdcountof(kPredefFirmwares) == kATFirmwareId_PredefCount);

		fwinfo.mId = id;
		fwinfo.mbAutoselect = kPredefFirmwares[id - 1].mbAutoselect;
		fwinfo.mbVisible = kPredefFirmwares[id - 1].mbVisible;
		fwinfo.mType = kPredefFirmwares[id - 1].mType;
		fwinfo.mName = kPredefFirmwares[id - 1].mpName;
		fwinfo.mPath.clear();
		fwinfo.mFlags = 0;

		return true;
	} else {
		VDStringA name;
		name.sprintf("Firmware\\Available\\%016llX", id);

		VDRegistryAppKey key(name.c_str(), false, false);
		if (!key.isReady())
			return false;

		fwinfo.mId = id;
		fwinfo.mbAutoselect = true;
		fwinfo.mbVisible = true;
		fwinfo.mType = kATFirmwareType_Unknown;
		
		if (!key.getString("Path", fwinfo.mPath))
			return false;

		if (VDFileIsRelativePath(fwinfo.mPath.c_str()))
			fwinfo.mPath = VDMakePath(VDGetProgramPath().c_str(), fwinfo.mPath.c_str());

		if (!key.getString("Name", fwinfo.mName))
			return false;

		VDStringA type;
		if (!key.getString("Type", type))
			return false;

		fwinfo.mType = ATGetFirmwareTypeFromName(type.c_str());

		fwinfo.mFlags = key.getInt("Flags");
		return true;
	}
}

void ATFirmwareManager::GetFirmwareList(vdvector<ATFirmwareInfo>& firmwares) const {
	// add predefined
	firmwares.resize(kATFirmwareId_PredefCount);
	for(uint32 i=0; i<kATFirmwareId_PredefCount; ++i)
		VDVERIFY(GetFirmwareInfo(i+1, firmwares[i]));

	// add custom
	VDRegistryAppKey key("Firmware\\Available", false, false);

	VDRegistryKeyIterator it(key);
	while(const char *name = it.Next()) {
		unsigned long long id;
		char dummy;
		if (1 != sscanf(name, "%llx%c", &id, &dummy))
			continue;

		if (id < (1ull << 63))
			continue;

		ATFirmwareInfo fw;
		if (GetFirmwareInfo(id, fw))
			firmwares.push_back(fw);
	}

	std::sort(firmwares.begin(), firmwares.end(),
		[](const ATFirmwareInfo& x, const ATFirmwareInfo& y) {
			return x.mId < y.mId;
		}
	);
}

uint64 ATFirmwareManager::GetCompatibleFirmware(ATFirmwareType type) const {

	uint64 id = GetFirmwareOfType(type, true);
	if (id)
		return id;

	switch(type) {
		case kATFirmwareType_Kernel800_OSA:
			id = GetCompatibleFirmware(kATFirmwareType_Kernel800_OSB);
			break;

		case kATFirmwareType_Kernel1200XL:
		case kATFirmwareType_KernelXEGS:
			id = GetCompatibleFirmware(kATFirmwareType_KernelXL);
			break;
	}

	return id;
}

uint64 ATFirmwareManager::GetFirmwareOfType(ATFirmwareType type, bool allowInternal) const {
	typedef vdvector<ATFirmwareInfo> Firmwares;
	Firmwares firmwares;
	GetFirmwareList(firmwares);

	uint64 bestId = GetDefaultFirmware(type);

	if (bestId)
		return bestId;

	for(Firmwares::const_iterator it(firmwares.begin()), itEnd(firmwares.end());
		it != itEnd;
		++it)
	{
		if (it->mType != type)
			continue;

		if (it->mId >= kATFirmwareId_Custom) {
			bestId = it->mId;
			break;
		}

		if (allowInternal && !bestId && it->mbAutoselect)
			bestId = it->mId;
	}

	return bestId;
}

VDStringW ATFirmwareManager::GetFirmwareRefString(uint64 id) const {
	if (!id)
		return VDStringW();

	if (id < kATFirmwareId_Custom) {
		VDStringW path;
		path.sprintf(L"internal:%08x", (unsigned)id);
		return path;
	}

	ATFirmwareInfo info;
	GetFirmwareInfo(id, info);

	return info.mPath;
}

uint64 ATFirmwareManager::GetFirmwareByRefString(const wchar_t *refstr) const {
	if (!*refstr)
		return 0;

	if (!wcsncmp(refstr, L"internal:", 9))
		return wcstoul(refstr + 9, NULL, 16);

	uint64 id = ATGetFirmwareIdFromPath(refstr);
	ATFirmwareInfo info;
	return GetFirmwareInfo(id, info) ? id : 0;
}

uint64 ATFirmwareManager::GetDefaultFirmware(ATFirmwareType type) const {
	VDRegistryAppKey key("Firmware\\Default", false);
	VDStringW refStr;
	key.getString(ATGetFirmwareTypeName(type), refStr);

	return GetFirmwareByRefString(refStr.c_str());
}

void ATFirmwareManager::SetDefaultFirmware(ATFirmwareType type, uint64 id) {
	VDRegistryAppKey key("Firmware\\Default", true);
	key.setString(ATGetFirmwareTypeName(type), GetFirmwareRefString(id).c_str());
}

bool ATFirmwareManager::LoadFirmware(uint64 id, void *dst, uint32 offset, uint32 len, bool *changed, uint32 *actualLen, vdfastvector<uint8> *dstbuf) {
	ATFirmwareInfo fwinfo;

	if (!GetFirmwareInfo(id, fwinfo))
		return false;

	if (id < kATFirmwareId_Custom) {
		static const uint32 kResourceIds[]={
			IDR_NOKERNEL,
			IDR_KERNEL,
			IDR_KERNELXL,
			IDR_HLEKERNEL,
			IDR_BASIC,
			IDR_5200KERNEL,
			IDR_NOCARTRIDGE,
			IDR_U1MBBIOS,
			IDR_NOHDBIOS,
			IDR_850RELOCATOR,
			IDR_850HANDLER,
			IDR_1030HANDLER,
			IDR_NOMIO,
			IDR_NOBLACKBOX,
		};

		VDASSERTCT(vdcountof(kResourceIds) == kATFirmwareId_PredefCount);

		uint32 resId = kResourceIds[id - 1];

		if (resId != IDR_U1MBBIOS && resId != IDR_NOMIO && resId != IDR_NOBLACKBOX) {
			if (dstbuf)
				return ATLoadKernelResource(resId, *dstbuf);
			else
				return ATLoadKernelResource(resId, dst, offset, len, true);
		}

		vdfastvector<uint8> buffer;
		ATLoadKernelResourceLZPacked(resId, buffer);

		if (dstbuf) {
			if (changed && *dstbuf != buffer)
				*changed = true;

			*dstbuf = buffer;
		} else {
			if (offset >= buffer.size())
				return false;

			size_t avail = buffer.size() - offset;
			if (len > avail)
				len = (uint32)avail;

			if (changed && memcmp(dst, buffer.data() + offset, len))
				*changed = true;

			memcpy(dst, buffer.data() + offset, len);
		}

		if (actualLen)
			*actualLen = len;

		return true;
	}

	long actual;

	try {
		VDFile f;
		f.open(fwinfo.mPath.c_str());
		f.seek(offset);

		if (changed) {
			vdblock<char> tmpData(len);
			actual = f.readData(tmpData.data(), len);

			if (actual >= 0) {
				*changed = memcmp(dst, tmpData.data(), actual) != 0;

				memcpy(dst, tmpData.data(), actual);
			}
		} else {
			actual = f.readData(dst, len);
		}
	} catch(const MyError&) {
		return false;
	}

	if (actualLen)
		*actualLen = (actual < 0) ? 0 : (uint32)actual;

	return true;
}

void ATFirmwareManager::AddFirmware(const ATFirmwareInfo& info) {
	VDASSERT(info.mType != kATFirmwareType_Unknown);

	VDStringA name;
	name.sprintf("Firmware\\Available\\%016llX", ATGetFirmwareIdFromPath(info.mPath.c_str()));
	VDRegistryAppKey key(name.c_str());
	key.setString("Name", info.mName.c_str());

	const VDStringW& programPath = VDGetProgramPath();
	const VDStringW& relPath = VDFileGetRelativePath(programPath.c_str(), info.mPath.c_str(), false);
	const VDStringW *path = relPath.empty() ? &info.mPath : &relPath;

	key.setString("Path", path->c_str());

	key.setString("Type", ATGetFirmwareTypeName(info.mType));
	key.setInt("Flags", info.mFlags);
}

void ATFirmwareManager::RemoveFirmware(uint64 id) {
	if (id < kATFirmwareId_Custom)
		return;

	VDStringA name;
	name.sprintf("%016llX", id);

	VDRegistryAppKey key("Firmware\\Available");

	{
		VDRegistryKey key2(key, name.c_str());

		VDRegistryValueIterator it(key);
		while(const char *name = it.Next())
			key.removeValue(name);
	}

	key.removeKey(name.c_str());
}
