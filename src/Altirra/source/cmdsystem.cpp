//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2015 Avery Lee
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
#include <at/atcore/devicemanager.h>
#include <at/atcore/propertyset.h>
#include "console.h"
#include "firmwaremanager.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uidisplay.h"
#include "uikeyboard.h"
#include "uimenu.h"

void ATUIShowCPUOptionsDialog(VDGUIHandle h);
void ATUIShowDialogSpeedOptions(VDGUIHandle parent);
void ATUIShowDialogDevices(VDGUIHandle parent, ATDeviceManager& devMgr);
void ATUIShowDialogFirmware(VDGUIHandle hParent, ATFirmwareManager& fwm, bool *anyChanges);
void ATUIShowDialogProfiles(VDGUIHandle hParent);
void ATUIOpenAdjustColorsDialog(VDGUIHandle hParent);

void ATSyncCPUHistoryState();
void ATUIUpdateSpeedTiming();
void ATUISwitchKernel(uint64 id);
void ATUISwitchBasic(uint64 basicId);
void ATUIResizeDisplay();

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

void OnCommandSystemTogglePause() {
	if (g_sim.IsRunning())
		g_sim.Pause();
	else
		g_sim.Resume();
}

void OnCommandSystemWarmReset() {
	g_sim.WarmReset();
	g_sim.Resume();
}

void OnCommandSystemColdReset() {
	g_sim.ColdReset();
	g_sim.Resume();

	if (!g_kbdOpts.mbAllowShiftOnColdReset)
		g_sim.GetPokey().SetShiftKeyState(false, true);
}

void OnCommandSystemTogglePauseWhenInactive() {
	ATUISetPauseWhenInactive(!ATUIGetPauseWhenInactive());
}

void OnCommandSystemToggleSlowMotion() {
	ATUISetSlowMotion(!ATUIGetSlowMotion());
}

void OnCommandSystemToggleWarpSpeed() {
	ATUISetTurbo(!ATUIGetTurbo());
}

void OnCommandSystemPulseWarpOn() {
	ATUISetTurboPulse(true);
}

void OnCommandSystemPulseWarpOff() {
	ATUISetTurboPulse(false);
}

void OnCommandSystemCPUOptionsDialog() {
	ATUIShowCPUOptionsDialog(ATUIGetMainWindow());
	ATSyncCPUHistoryState();
}

void OnCommandSystemToggleFPPatch() {
	g_sim.SetFPPatchEnabled(!g_sim.IsFPPatchEnabled());
}

void OnCommandSystemSpeedOptionsDialog() {
	ATUIShowDialogSpeedOptions(ATUIGetMainWindow());
}

void OnCommandSystemDevicesDialog() {
	ATUIShowDialogDevices(ATUIGetMainWindow(), *g_sim.GetDeviceManager());
}

void OnCommandSystemToggleKeyboardPresent() {
	g_sim.SetKeyboardPresent(!g_sim.IsKeyboardPresent());
}

void OnCommandSystemToggleForcedSelfTest() {
	g_sim.SetForcedSelfTest(!g_sim.IsForcedSelfTest());
}

void OnCommandSystemHardwareMode(ATHardwareMode mode) {
	ATUISwitchHardwareMode(ATUIGetMainWindow(), mode, false);
}

void OnCommandSystemKernel(ATFirmwareId id) {
	ATUISwitchKernel(id);
}

void OnCommandSystemBasic(ATFirmwareId id) {
	ATUISwitchBasic(id);
}

void OnCommandSystemMemoryMode(ATMemoryMode mode) {
	ATUISwitchMemoryMode(ATUIGetMainWindow(), mode);
}

void OnCommandSystemAxlonMemoryMode(uint8 bankBits) {
	if (g_sim.GetHardwareMode() != kATHardwareMode_5200 && g_sim.GetAxlonMemoryMode() != bankBits) {
		g_sim.SetAxlonMemoryMode(bankBits);
		g_sim.ColdReset();
	}
}

void OnCommandSystemHighMemBanks(sint32 banks) {
	if (g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816 && g_sim.GetHighMemoryBanks() != banks) {
		g_sim.SetHighMemoryBanks(banks);
		g_sim.ColdReset();
	}
}

void OnCommandSystemToggleMapRAM() {
	g_sim.SetMapRAMEnabled(!g_sim.IsMapRAMEnabled());
}

void OnCommandSystemToggleUltimate1MB() {
	g_sim.SetUltimate1MBEnabled(!g_sim.IsUltimate1MBEnabled());
	g_sim.ColdReset();
}

void OnCommandSystemToggleFloatingIoBus() {
	g_sim.SetFloatingIoBusEnabled(!g_sim.IsFloatingIoBusEnabled());
	g_sim.ColdReset();
}

void OnCommandSystemMemoryClearMode(ATMemoryClearMode mode) {
	g_sim.SetMemoryClearMode(mode);
}

void OnCommandSystemToggleMemoryRandomizationEXE() {
	g_sim.SetRandomFillEXEEnabled(!g_sim.IsRandomFillEXEEnabled());
}

void OnCommandSystemToggleBASIC() {
	g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled());
}

void OnCommandSystemToggleFastBoot() {
	g_sim.SetFastBootEnabled(!g_sim.IsFastBootEnabled());
}

void OnCommandSystemROMImagesDialog() {
	bool anyChanges = false;
	ATUIShowDialogFirmware(ATUIGetMainWindow(), *g_sim.GetFirmwareManager(), &anyChanges);

	if (anyChanges) {
		if (g_sim.LoadROMs())
			g_sim.ColdReset();
	}

	ATUIRebuildDynamicMenu(0);
	ATUIRebuildDynamicMenu(1);
}

void OnCommandSystemToggleRTime8() {
	auto *dm = g_sim.GetDeviceManager();

	if (dm->GetDeviceByTag("rtime8"))
		dm->RemoveDevice("rtime8");
	else
		dm->AddDevice("rtime8", ATPropertySet(), false, false);
}

void OnCommandVideoToggleCTIA() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetCTIAMode(!gtia.IsCTIAMode());
}

void OnCommandVideoToggleFrameBlending() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetBlendModeEnabled(!gtia.IsBlendModeEnabled());
}

void OnCommandVideoToggleInterlace() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetInterlaceEnabled(!gtia.IsInterlaceEnabled());
	ATUIResizeDisplay();
}

void OnCommandVideoToggleScanlines() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetScanlinesEnabled(!gtia.AreScanlinesEnabled());
	ATUIResizeDisplay();
}

void OnCommandVideoToggleXEP80() {
	g_sim.GetDeviceManager()->ToggleDevice("xep80");
}

void OnCommandVideoToggleVBXE() {
	g_sim.SetVBXEEnabled(!g_sim.GetVBXE());
	g_sim.ColdReset();
	ATUIResizeDisplay();
}

void OnCommandVideoToggleVBXESharedMemory() {
	g_sim.SetVBXESharedMemoryEnabled(!g_sim.IsVBXESharedMemoryEnabled());
	g_sim.ColdReset();
}

void OnCommandVideoToggleVBXEAltPage() {
	g_sim.SetVBXEAltPageEnabled(!g_sim.IsVBXEAltPageEnabled());
	g_sim.ColdReset();
}

void OnCommandVideoAdjustColorsDialog() {
	ATUIOpenAdjustColorsDialog(ATUIGetMainWindow());
}

void OnCommandVideoArtifacting(ATGTIAEmulator::ArtifactMode mode) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetArtifactingMode(mode);
}

void OnCommandVideoStandard(ATVideoStandard mode) {
	ATSetVideoStandard(mode);
}

void OnCommandVideoToggleStandardNTSCPAL() {
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return;

	if (g_sim.GetVideoStandard() == kATVideoStandard_NTSC)
		g_sim.SetVideoStandard(kATVideoStandard_PAL);
	else
		g_sim.SetVideoStandard(kATVideoStandard_NTSC);

	ATUIUpdateSpeedTiming();

	IATDisplayPane *pane = ATGetUIPaneAs<IATDisplayPane>(kATUIPaneId_Display);
	if (pane)
		pane->OnSize();
}

void OnCommandVideoEnhancedModeNone() {
	ATUISetEnhancedTextMode(kATUIEnhancedTextMode_None);
}

void OnCommandVideoEnhancedModeHardware() {
	ATUISetEnhancedTextMode(kATUIEnhancedTextMode_Hardware);
}

void OnCommandVideoEnhancedModeCIO() {
	ATUISetEnhancedTextMode(kATUIEnhancedTextMode_Software);
}

void OnCommandSystemEditProfilesDialog() {
	ATUIShowDialogProfiles(ATUIGetMainWindow());
	ATUIRebuildDynamicMenu(kATUIDynamicMenu_Profile);
}
