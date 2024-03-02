//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2016 Avery Lee
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
#include <vd2/system/binary.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/registry.h>
#include <at/atcore/media.h>
#include <at/atcore/vfs.h>
#include <at/atio/blobimage.h>
#include <at/atio/cassetteimage.h>
#include "oshelper.h"
#include "cassette.h"
#include "compatdb.h"
#include "compatengine.h"
#include "disk.h"
#include "firmwaremanager.h"
#include "resource.h"
#include "simulator.h"
#include "options.h"
#include "cartridge.h"
#include "hleprogramloader.h"
#include "uiaccessors.h"
#include "uicommondialogs.h"

extern ATSimulator g_sim;

bool ATUISwitchKernel(VDGUIHandle h, uint64 kernelId);

ATCompatDBView g_ATCompatDBView;
ATCompatDBView g_ATCompatDBViewExt;
VDStringW g_ATCompatDBPath;
void *g_pATCompatDBExt;

void ATCompatInit() {
	size_t len;

	const void *data = ATLockResource(IDR_COMPATDB, len);

	if (data) {
		if (len >= sizeof(ATCompatDBHeader)) {
			auto *hdr = (const ATCompatDBHeader *)data;
			if (hdr->Validate(len)) {
				g_ATCompatDBView = ATCompatDBView(hdr);
			} else {
				VDFAIL("Internal compatibility database failed to validate.");
			}
		}
	}

	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			if (!prevOpts || opts.mbCompatEnableExternalDB != prevOpts->mbCompatEnableExternalDB
				|| opts.mCompatExternalDBPath != prevOpts->mCompatExternalDBPath)
			{
				if (g_ATOptions.mbCompatEnableExternalDB && !g_ATOptions.mCompatExternalDBPath.empty()) {
					try {
						ATCompatLoadExtDatabase(g_ATOptions.mCompatExternalDBPath.c_str(), false);
					} catch(const MyError&) {
					}
				}
			}
		},
		nullptr);
}

void ATCompatShutdown() {
	if (g_pATCompatDBExt) {
		g_ATCompatDBViewExt = ATCompatDBView();

		free(g_pATCompatDBExt);
		g_pATCompatDBExt = nullptr;
	}
}

void ATCompatLoadExtDatabase(const wchar_t *path, bool testOnly) {
	vdrefptr<ATVFSFileView> fileView;
	ATVFSOpenFileView(path, false, ~fileView);

	auto& stream = fileView->GetStream();

	auto len = stream.Length();

	if (len > 0x8000000)
		throw MyError("Compatibility engine '%ls' is too big (%llu bytes).", path, (unsigned long long)len);

	size_t lensz = (size_t)len;

	vdautoblockptr p(malloc(lensz));
	if (!p)
		throw MyMemoryError(lensz);

	stream.Read(p, (sint32)lensz);

	auto *hdr = (const ATCompatDBHeader *)p.get();
	if (lensz < sizeof(ATCompatDBHeader) || !hdr->Validate(lensz))
		throw MyError("'%ls' is not a valid compiled compatibility engine file.", path);

	if (!testOnly) {
		if (g_pATCompatDBExt)
			free(g_pATCompatDBExt);

		g_pATCompatDBExt = p.release();
		g_ATCompatDBViewExt = ATCompatDBView(hdr);
		g_ATCompatDBPath = path;
	}
}

void ATCompatReloadExtDatabase() {
	if (!g_ATCompatDBPath.empty() && g_pATCompatDBExt)
		ATCompatLoadExtDatabase(g_ATCompatDBPath.c_str(), false);
}

bool ATCompatIsExtDatabaseLoaded() {
	return g_pATCompatDBExt != nullptr;
}

VDStringW ATCompatGetExtDatabasePath() {
	return g_ATCompatDBPath;
}

bool ATCompatIsAllMuted() {
	if (!g_ATOptions.mbCompatEnable)
		return true;

	return !g_ATOptions.mbCompatEnableInternalDB && !g_ATOptions.mbCompatEnableExternalDB;
}

void ATCompatSetAllMuted(bool mute) {
	if (g_ATOptions.mbCompatEnable == mute) {
		g_ATOptions.mbCompatEnable = !mute;

		ATOptionsSave();
	}
}

VDStringA ATCompatGetTitleMuteKeyName(const ATCompatDBTitle *title) {
	// Take everything alphanumeric up to 15 chars, then add hash of the whole original string
	VDStringA name;
	for(const char *s = title->mName.c_str(); *s; ++s) {
		const char c = *s;

		if ((c >= 0x30 && c <= 0x39) || (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A)) {
			name += c;

			if (name.size() >= 15)
				break;
		}
	}

	name.append_sprintf("_%08X", (unsigned)vdhash<VDStringA>()(title->mName.c_str()));
	return name;
}

bool ATCompatIsTitleMuted(const ATCompatDBTitle *title) {
	if (ATCompatIsAllMuted())
		return true;

	VDRegistryAppKey key("Settings\\MutedCompatMessages", false);
	const auto& name = ATCompatGetTitleMuteKeyName(title);

	if (key.getInt(name.c_str()) & 1)
		return true;

	return false;
}

void ATCompatSetTitleMuted(const ATCompatDBTitle *title, bool mute) {
	VDRegistryAppKey key("Settings\\MutedCompatMessages", true);
	const auto& name = ATCompatGetTitleMuteKeyName(title);

	int flags = key.getInt(name.c_str());
	if (mute) {
		if (!(flags & 1)) {
			++flags;

			key.setInt(name.c_str(), flags);
		}
	} else {
		if (flags & 1) {
			--flags;

			if (flags)
				key.setInt(name.c_str(), flags);
			else
				key.removeValue(name.c_str());
		}
	}
}

void ATCompatUnmuteAllTitles() {
	VDRegistryAppKey key("Settings", true);

	key.removeKeyRecursive("MutedCompatMessages");
}

bool ATHasInternalBASIC(ATHardwareMode hwmode) {
	switch(hwmode) {
		case kATHardwareMode_800XL:
		case kATHardwareMode_130XE:
		case kATHardwareMode_XEGS:
			return true;

		default:
			return false;
	}
}

bool ATCompatIsTagApplicable(ATCompatKnownTag knownTag) {
	const auto needsBasic = [] {
		return ATHasInternalBASIC(g_sim.GetHardwareMode())
			? !g_sim.IsBASICEnabled()
			: !g_sim.IsCartridgeAttached(0);
	};

	switch(knownTag) {
		case kATCompatKnownTag_BASIC:
			if (needsBasic())
				return true;
			break;

		case kATCompatKnownTag_BASICRevA:
			return needsBasic() || g_sim.GetActualBasicId() != g_sim.GetFirmwareManager()->GetSpecificFirmware(kATSpecificFirmwareType_BASICRevA);

		case kATCompatKnownTag_BASICRevB:
			return needsBasic() || g_sim.GetActualBasicId() != g_sim.GetFirmwareManager()->GetSpecificFirmware(kATSpecificFirmwareType_BASICRevB);

		case kATCompatKnownTag_BASICRevC:
			return needsBasic() || g_sim.GetActualBasicId() != g_sim.GetFirmwareManager()->GetSpecificFirmware(kATSpecificFirmwareType_BASICRevC);

		case kATCompatKnownTag_NoBASIC:
			if (ATHasInternalBASIC(g_sim.GetHardwareMode())) {
				if (g_sim.IsBASICEnabled())
					return true;
			} else {
				if (g_sim.IsCartridgeAttached(0))
					return true;
			}
			break;

		case kATCompatKnownTag_OSA:
			return g_sim.GetActualKernelId() != g_sim.GetFirmwareManager()->GetSpecificFirmware(kATSpecificFirmwareType_OSA);

		case kATCompatKnownTag_OSB:
			return g_sim.GetActualKernelId() != g_sim.GetFirmwareManager()->GetSpecificFirmware(kATSpecificFirmwareType_OSB);

		case kATCompatKnownTag_XLOS:
			return g_sim.GetActualKernelId() != g_sim.GetFirmwareManager()->GetSpecificFirmware(kATSpecificFirmwareType_XLOSr2);

		case kATCompatKnownTag_AccurateDiskTiming:
			if (!g_sim.IsDiskAccurateTimingEnabled() || g_sim.IsDiskSIOPatchEnabled())
				return true;
			break;

		case kATCompatKnownTag_NoU1MB:
			if (g_sim.IsUltimate1MBEnabled())
				return true;
			break;

		case kATCompatKnownTag_Undocumented6502:
			{
				auto& cpu = g_sim.GetCPU();

				if (cpu.GetCPUMode() != kATCPUMode_6502 || !cpu.AreIllegalInsnsEnabled())
					return true;
			}
			break;

		case kATCompatKnownTag_No65C816HighAddressing:
			if (g_sim.GetHighMemoryBanks() >= 0 && g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816)
				return true;
			break;

		case kATCompatKnownTag_WritableDisk:
			if (!(g_sim.GetDiskInterface(0).GetWriteMode() & kATMediaWriteMode_AllowWrite))
				return true;
			break;

		case kATCompatKnownTag_NoFloatingDataBus:
			switch(g_sim.GetHardwareMode()) {
				case kATHardwareMode_130XE:
				case kATHardwareMode_XEGS:
					return true;
			}
			break;

		case kATCompatKnownTag_50Hz:
			if (!g_sim.IsVideo50Hz())
				return true;
			break;

		case kATCompatKnownTag_60Hz:
			if (g_sim.IsVideo50Hz())
				return true;
			break;
	}

	return false;
}

bool ATCompatCheckTitleTags(ATCompatDBView& view, const ATCompatDBTitle *title, vdfastvector<ATCompatKnownTag>& tags) {
	for(const auto& tagId : title->mTagIds) {
		const ATCompatKnownTag knownTag = view.GetKnownTag(tagId);

		if (ATCompatIsTagApplicable(knownTag))
			tags.push_back(knownTag);
	}

	return !tags.empty();
}

const ATCompatDBTitle *ATCompatFindTitle(const ATCompatDBView& view, const vdvector_view<const ATCompatMarker>& markers, vdfastvector<ATCompatKnownTag>& tags, bool applicableTagsOnly) {
	vdfastvector<const ATCompatDBRule *> matchingRules;
	matchingRules.reserve(markers.size());

	for(const auto& marker : markers) {
		uint64 ruleValue;

		if (ATCompatIsLargeRuleType((ATCompatRuleType)marker.mRuleType)) {
			ruleValue = view.FindLargeRuleBlob(marker.mValue);
		} else {
			ruleValue = marker.mValue[0];
		}

		// Find the range of matching rules. More than one will match if the same rule is used by more than one
		// alias; each rule points to a different one.
		const auto matchingRange = view.FindMatchingRules(marker.mRuleType, ruleValue);

		for(auto it = matchingRange.first; it != matchingRange.second; ++it)
			matchingRules.push_back(it);
	}

	vdfastvector<const ATCompatDBTitle *> matchingTitles;
	view.FindMatchingTitles(matchingTitles, matchingRules.data(), matchingRules.size());

	for(const ATCompatDBTitle *title : matchingTitles) {
		if (ATCompatIsTitleMuted(title))
			continue;

		for(const auto& tagId : title->mTagIds) {
			const ATCompatKnownTag knownTag = view.GetKnownTag(tagId);

			if (applicableTagsOnly && !ATCompatIsTagApplicable(knownTag))
				continue;

			tags.push_back(knownTag);
		}

		if (!tags.empty())
			return title;
	}

	return nullptr;
}

void ATCompatMarker::SetSHA256(const ATChecksumSHA256& checksum) {
	memcpy(mValue, checksum.mDigest, 32);
}

ATChecksumSHA256 ATCompatMarker::GetSHA256() const {
	ATChecksumSHA256 checksum;
	memcpy(checksum.mDigest, mValue, 32);
	return checksum;
}

ATCompatMarker ATCompatMarker::FromSHA256(ATCompatRuleType ruleType, const ATChecksumSHA256& checksum) {
	ATCompatMarker marker;
	marker.mRuleType = ruleType;
	marker.SetSHA256(checksum);

	return marker;
}

const ATCompatDBTitle *ATCompatFindTitle(const vdvector_view<const ATCompatMarker>& markers, vdfastvector<ATCompatKnownTag>& tags, bool applicableTagsOnly) {
	if (ATCompatIsAllMuted())
		return nullptr;

	const ATCompatDBTitle *title;

	if (g_ATOptions.mbCompatEnableExternalDB && g_ATCompatDBViewExt.IsValid()) {
		title = ATCompatFindTitle(g_ATCompatDBViewExt, markers, tags, applicableTagsOnly);
		if (title)
			return title;
	}

	if (g_ATOptions.mbCompatEnableInternalDB && g_ATCompatDBView.IsValid()) {
		title = ATCompatFindTitle(g_ATCompatDBView, markers, tags, applicableTagsOnly);
		if (title)
			return title;
	}

	return nullptr;
}

// Given one or more rules, check if there are any aliases that are matched by those rules
// and return the corresponding title. Returns nullptr if no rules are given. Matching an
// alias only requires all of the rules in the alias to be present; additional rules can be
// ignored.
const ATCompatDBTitle *ATCompatCheckTitle(ATCompatDBView& view, const ATCompatMarker *markers, size_t numMarkers, vdfastvector<ATCompatKnownTag>& tags) {
	if (!numMarkers)
		return nullptr;

	return ATCompatFindTitle(view, vdvector_view<const ATCompatMarker>(markers, numMarkers), tags, true);
}

const ATCompatDBTitle *ATCompatCheckDB(ATCompatDBView& view, vdfastvector<ATCompatKnownTag>& tags) {
	vdfastvector<ATCompatMarker> markers;

	if (IATDiskImage *pImage = g_sim.GetDiskInterface(0).GetDiskImage(); pImage && !pImage->IsDynamic()) {
		markers.clear();
		ATCompatGetMarkersForImage(markers, pImage, true);

		if (auto *title = ATCompatCheckTitle(view, markers.data(), markers.size(), tags))
			return title;
	}

	for(int i=0; i<2; ++i) {
		auto *cart = g_sim.GetCartridge(i);

		if (!cart)
			continue;

		markers.clear();
		ATCompatGetMarkersForImage(markers, cart->GetImage(), true);

		if (auto *title = ATCompatCheckTitle(view, markers.data(), markers.size(), tags))
			return title;
	}

	if (IATCassetteImage *tapeImage = g_sim.GetCassette().GetImage()) {
		markers.clear();
		ATCompatGetMarkersForImage(markers, tapeImage, true);

		if (auto *title = ATCompatCheckTitle(view, markers.data(), markers.size(), tags))
			return title;
	}

	auto *programLoader = g_sim.GetProgramLoader();
	if (programLoader) {
		markers.clear();
		ATCompatGetMarkersForImage(markers, programLoader->GetCurrentImage(), true);

		if (auto *title = ATCompatCheckTitle(view, markers.data(), markers.size(), tags))
			return title;
	}

	return nullptr;
}

const ATCompatDBTitle *ATCompatCheck(vdfastvector<ATCompatKnownTag>& tags) {
	if (ATCompatIsAllMuted())
		return nullptr;

	const ATCompatDBTitle *title;

	if (g_ATOptions.mbCompatEnableExternalDB && g_ATCompatDBViewExt.IsValid()) {
		title = ATCompatCheckDB(g_ATCompatDBViewExt, tags);
		if (title)
			return title;
	}

	if (g_ATOptions.mbCompatEnableInternalDB && g_ATCompatDBView.IsValid()) {
		title = ATCompatCheckDB(g_ATCompatDBView, tags);
		if (title)
			return title;
	}

	return nullptr;
}

bool ATCompatSwitchToSpecificBASIC(ATSpecificFirmwareType specificType) {
	auto *fw = g_sim.GetFirmwareManager();

	const auto id = fw->GetSpecificFirmware(specificType);
	if (!id)
		return false;

	ATFirmwareInfo info;
	if (!fw->GetFirmwareInfo(id, info))
		return false;

	if (!ATIsSpecificFirmwareTypeCompatible(info.mType, specificType))
		return false;

	g_sim.SetBasic(id);
	return true;
}

bool ATCompatTrySwitchToSpecificKernel(VDGUIHandle h, ATSpecificFirmwareType specificType) {
	auto *fw = g_sim.GetFirmwareManager();

	const auto id = fw->GetSpecificFirmware(specificType);
	if (!id)
		return false;

	ATFirmwareInfo info;
	if (!fw->GetFirmwareInfo(id, info))
		return false;

	if (!ATIsSpecificFirmwareTypeCompatible(info.mType, specificType))
		return false;

	ATHardwareMode hardwareMode = g_sim.GetHardwareMode();

	switch(specificType) {
		case kATSpecificFirmwareType_OSA:
		case kATSpecificFirmwareType_OSB:
			hardwareMode = kATHardwareMode_800;
			break;

		case kATSpecificFirmwareType_XLOSr2:
		default:
			if (hardwareMode != kATHardwareMode_800XL && hardwareMode != kATHardwareMode_130XE)
				hardwareMode = kATHardwareMode_800XL;
			break;

		case kATSpecificFirmwareType_XLOSr4:
			hardwareMode = kATHardwareMode_XEGS;
			break;
	}

	return ATUISwitchHardwareMode(h, hardwareMode, true) && ATUISwitchKernel(h, id);
}

void ATCompatSwitchToSpecificKernel(VDGUIHandle h, ATSpecificFirmwareType specificType) {
	if (!ATCompatTrySwitchToSpecificKernel(h, specificType)) {
		ATUIShowWarning(
			h,
			L"The ROM image required by this program could not be found. If you have it, make sure it is set under \"Use For...\" in Firmware Images. (Rescan will do this automatically for any images it finds.)",
			L"Altirra Warning");
	}
}

void ATCompatAdjust(VDGUIHandle h, const ATCompatKnownTag *tags, size_t numTags) {
	bool basic = false;
	bool nobasic = false;

	// handle tags that can switch profile first
	for(size_t i=0; i<numTags; ++i) {
		switch(tags[i]) {
			case kATCompatKnownTag_OSA:
				ATCompatSwitchToSpecificKernel(h, kATSpecificFirmwareType_OSA);
				break;

			case kATCompatKnownTag_OSB:
				ATCompatSwitchToSpecificKernel(h, kATSpecificFirmwareType_OSB);
				break;

			case kATCompatKnownTag_XLOS:
				ATCompatSwitchToSpecificKernel(h, kATSpecificFirmwareType_XLOSr2);
				break;

			case kATCompatKnownTag_NoFloatingDataBus:
				switch(g_sim.GetHardwareMode()) {
					case kATHardwareMode_130XE:
					case kATHardwareMode_XEGS:
						ATUISwitchHardwareMode(h, kATHardwareMode_800XL, true);
						break;
				}
				break;

			default:
				break;
		}
	}
	
	for(size_t i=0; i<numTags; ++i) {
		switch(tags[i]) {
			case kATCompatKnownTag_OSA:
			case kATCompatKnownTag_OSB:
			case kATCompatKnownTag_XLOS:
			case kATCompatKnownTag_NoFloatingDataBus:
				// handled in earlier pass
				break;

			case kATCompatKnownTag_BASIC:
				// Handle this last, as it has to be done after the hardware mode is known.
				basic = true;
				break;

			case kATCompatKnownTag_BASICRevA:
				ATCompatSwitchToSpecificBASIC(kATSpecificFirmwareType_BASICRevA);
				basic = true;
				break;

			case kATCompatKnownTag_BASICRevB:
				ATCompatSwitchToSpecificBASIC(kATSpecificFirmwareType_BASICRevB);
				basic = true;
				break;

			case kATCompatKnownTag_BASICRevC:
				ATCompatSwitchToSpecificBASIC(kATSpecificFirmwareType_BASICRevC);
				basic = true;
				break;

			case kATCompatKnownTag_NoBASIC:
				nobasic = true;
				break;

			case kATCompatKnownTag_AccurateDiskTiming:
				g_sim.SetDiskAccurateTimingEnabled(true);
				g_sim.SetDiskSIOPatchEnabled(false);
				break;

			case kATCompatKnownTag_NoU1MB:
				g_sim.SetUltimate1MBEnabled(false);
				break;

			case kATCompatKnownTag_Undocumented6502:
				{
					auto& cpu = g_sim.GetCPU();
					g_sim.SetCPUMode(kATCPUMode_6502, 1);
					cpu.SetIllegalInsnsEnabled(true);
				}
				break;

			case kATCompatKnownTag_No65C816HighAddressing:
				g_sim.SetHighMemoryBanks(-1);
				break;

			case kATCompatKnownTag_WritableDisk:
				{
					ATDiskInterface& diskIf = g_sim.GetDiskInterface(0);
					auto writeMode = diskIf.GetWriteMode();

					if (!(writeMode & kATMediaWriteMode_AllowWrite))
						diskIf.SetWriteMode(kATMediaWriteMode_VRWSafe);
				}
				break;

			case kATCompatKnownTag_Cart52008K:
			case kATCompatKnownTag_Cart520016KOneChip:
			case kATCompatKnownTag_Cart520016KTwoChip:
			case kATCompatKnownTag_Cart520032K:
				// We ignore these tags at compat checking time. They're used to feed the
				// mapper detection instead.
				break;

			case kATCompatKnownTag_60Hz:
				if (g_sim.IsVideo50Hz())
					g_sim.SetVideoStandard(kATVideoStandard_NTSC);
				break;

			case kATCompatKnownTag_50Hz:
				if (!g_sim.IsVideo50Hz())
					g_sim.SetVideoStandard(kATVideoStandard_PAL);
				break;

		}
	}

	if (basic || nobasic) {
		// Check if this is a machine type that has internal BASIC, or if we need to insert
		// the BASIC cartridge.
		bool internalBASIC = ATHasInternalBASIC(g_sim.GetHardwareMode());

		if (basic) {
			g_sim.SetBASICEnabled(true);

			if (!internalBASIC)
				g_sim.LoadCartridgeBASIC();
		} else if (nobasic) {
			g_sim.SetBASICEnabled(false);

			if (!internalBASIC)
				g_sim.UnloadCartridge(0);
		}
	}

	g_sim.ColdReset();
}

void ATCompatGetMarkersForImage(vdfastvector<ATCompatMarker>& markers, IATImage *image, bool includeLegacyChecksums) {
	if (auto *diskImage = vdpoly_cast<IATDiskImage *>(image); diskImage && !diskImage->IsDynamic()) {
		if (const auto& sha256 = diskImage->GetImageFileSHA256(); sha256.has_value())
			markers.push_back(ATCompatMarker::FromSHA256(kATCompatRuleType_DiskFileSHA256, sha256.value()));

		if (includeLegacyChecksums)
			markers.push_back(ATCompatMarker { kATCompatRuleType_DiskChecksum, { diskImage->GetImageChecksum() } });
	}

	if (auto *cartImage = vdpoly_cast<IATCartridgeImage *>(image)) {
		if (const auto& sha256 = cartImage->GetImageFileSHA256(); sha256.has_value())
			markers.push_back(ATCompatMarker::FromSHA256(kATCompatRuleType_CartFileSHA256, sha256.value()));

		if (includeLegacyChecksums)
			markers.push_back(ATCompatMarker { kATCompatRuleType_CartChecksum, { cartImage->GetChecksum() } });
	}

	if (auto *tapeImage = vdpoly_cast<IATCassetteImage *>(image)) {
		if (const auto& sha256 = image->GetImageFileSHA256(); sha256.has_value())
			markers.push_back(ATCompatMarker::FromSHA256(kATCompatRuleType_TapeFileSHA256, sha256.value()));
	}

	if (auto *programImage = vdpoly_cast<IATBlobImage *>(image); programImage && programImage->GetImageType() == kATImageType_Program) {
		if (const auto& sha256 = programImage->GetImageFileSHA256(); sha256.has_value())
			markers.push_back(ATCompatMarker::FromSHA256(kATCompatRuleType_ExeFileSHA256, sha256.value()));

		if (includeLegacyChecksums)
			markers.push_back(ATCompatMarker { kATCompatRuleType_ExeChecksum, { programImage->GetChecksum() } });
	}
}
