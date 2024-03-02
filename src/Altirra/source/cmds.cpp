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
#include <vd2/system/unknown.h>
#include <at/atcore/device.h>
#include <at/atcore/devicemanager.h>
#include "firmwaremanager.h"
#include "cassette.h"
#include "console.h"
#include "cmdhelpers.h"
#include "debugger.h"
#include "simulator.h"
#include "audiooutput.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "uiclipboard.h"
#include "uidisplay.h"
#include "inputmanager.h"
#include "cartridge.h"
#include "settings.h"

extern ATSimulator g_sim;
extern bool g_xepViewEnabled;
extern bool g_xepViewAutoswitchingEnabled;
extern bool g_showFps;

void OnCommandOpenImage();
void OnCommandBootImage();
void OnCommandQuickLoadState();
void OnCommandQuickSaveState();
void OnCommandLoadState();
void OnCommandSaveState();
void OnCommandAttachCartridge();
void OnCommandDetachCartridge();
void OnCommandSaveCartridge();
void OnCommandAttachCartridge2();
void OnCommandDetachCartridge2();
void OnCommandAttachNewCartridge(ATCartridgeMode mode);
void OnCommandAttachCartridgeBASIC();
void OnCommandCartActivateMenuButton();
void OnCommandCartToggleSwitch();
void OnCommandSaveFirmwareIDEMain();
void OnCommandSaveFirmwareIDESDX();
void OnCommandSaveFirmwareU1MB();
void OnCommandExit();
void OnCommandCassetteLoadNew();
void OnCommandCassetteLoad();
void OnCommandCassetteUnload();
void OnCommandCassetteSave();
void OnCommandCassetteTapeControlDialog();
void OnCommandCassetteToggleSIOPatch();
void OnCommandCassetteToggleAutoBoot();
void OnCommandCassetteToggleLoadDataAsAudio();
void OnCommandCassetteToggleRandomizeStartPosition();
void OnCommandDebuggerOpenSourceFile();
void OnCommandDebuggerToggleBreakAtExeRun();
void OnCommandDebugToggleAutoReloadRoms();
void OnCommandDebugToggleAutoLoadKernelSymbols();
void OnCommandDebugChangeFontDialog();
void OnCommandDebugToggleDebugger();
void OnCommandDebugRun();
void OnCommandDebugBreak();
void OnCommandDebugRunStop();
void OnCommandDebugStepInto();
void OnCommandDebugStepOut();
void OnCommandDebugStepOver();
void OnCommandDebugToggleBreakpoint();
void OnCommandDebugVerifierDialog();
void OnCommandViewNextFilterMode();
void OnCommandViewFilterModePoint();
void OnCommandViewFilterModeBilinear();
void OnCommandViewFilterModeSharpBilinear();
void OnCommandViewFilterModeBicubic();
void OnCommandViewFilterModeDefault();
void OnCommandViewFilterSharpnessSofter();
void OnCommandViewFilterSharpnessSoft();
void OnCommandViewFilterSharpnessNormal();
void OnCommandViewFilterSharpnessSharp();
void OnCommandViewFilterSharpnessSharper();
void OnCommandViewStretchFitToWindow();
void OnCommandViewStretchPreserveAspectRatio();
void OnCommandViewStretchSquarePixels();
void OnCommandViewStretchSquarePixelsInt();
void OnCommandViewStretchPreserveAspectRatioInt();
void OnCommandViewOverscanOSScreen();
void OnCommandViewOverscanNormal();
void OnCommandViewOverscanExtended();
void OnCommandViewOverscanFull();
void OnCommandViewVerticalOverscan(ATGTIAEmulator::VerticalOverscanMode mode);
void OnCommandViewTogglePALExtended();
void OnCommandViewToggleFullScreen();
void OnCommandViewToggleVSync();
void OnCommandViewToggleFPS();
void OnCommandViewAdjustWindowSize();
void OnCommandViewResetWindowLayout();
void OnCommandViewEffectReload();
void OnCommandAnticVisualizationNext();
void OnCommandGTIAVisualizationNext();
void OnCommandVideoToggleXEP80View();
void OnCommandVideoToggleXEP80ViewAutoswitching();
void OnCommandPane(uint32 paneId);
void OnCommandEditCopyFrame();
void OnCommandEditSaveFrame();
void OnCommandEditCopyText();
void OnCommandEditPasteText();
void OnCommandSystemWarmReset();
void OnCommandSystemColdReset();
void OnCommandSystemTogglePause();
void OnCommandSystemTogglePauseWhenInactive();
void OnCommandSystemToggleSlowMotion();
void OnCommandSystemToggleWarpSpeed();
void OnCommandSystemPulseWarpOff();
void OnCommandSystemPulseWarpOn();
void OnCommandSystemCPUOptionsDialog();
void OnCommandSystemToggleFPPatch();
void OnCommandSystemSpeedOptionsDialog();
void OnCommandSystemDevicesDialog();
void OnCommandSystemToggleKeyboardPresent();
void OnCommandSystemToggleForcedSelfTest();
void OnCommandConsoleBlackBoxDumpScreen();
void OnCommandConsoleBlackBoxMenu();
void OnCommandConsoleIDEPlus2SwitchDisks();
void OnCommandConsoleIDEPlus2WriteProtect();
void OnCommandConsoleIDEPlus2SDX();
void OnCommandSystemHardwareMode(ATHardwareMode mode);
void OnCommandSystemKernel(ATFirmwareId id);
void OnCommandSystemBasic(ATFirmwareId id);
void OnCommandSystemMemoryMode(ATMemoryMode mode);
void OnCommandSystemAxlonMemoryMode(uint8 bankBits);
void OnCommandSystemHighMemBanks(sint32 banks);
void OnCommandSystemToggleMapRAM();
void OnCommandSystemToggleUltimate1MB();
void OnCommandSystemToggleFloatingIoBus();
void OnCommandSystemMemoryClearMode(ATMemoryClearMode mode);
void OnCommandSystemToggleMemoryRandomizationEXE();
void OnCommandSystemToggleBASIC();
void OnCommandSystemToggleFastBoot();
void OnCommandSystemROMImagesDialog();
void OnCommandSystemToggleRTime8();
void OnCommandSystemEditProfilesDialog();
void OnCommandDiskDrivesDialog();
void OnCommandDiskToggleAllEnabled();
void OnCommandDiskToggleSIOPatch();
void OnCommandDiskToggleSIOOverrideDetection();
void OnCommandDiskToggleAccurateSectorTiming();
void OnCommandDiskToggleDriveSounds();
void OnCommandDiskToggleSectorCounter();
void OnCommandDiskAttach(int index);
void OnCommandDiskDetachAll();
void OnCommandDiskDetach(int index);
void OnCommandDiskRotate(int delta);
void OnCommandVideoStandard(ATVideoStandard mode);
void OnCommandVideoToggleStandardNTSCPAL();
void OnCommandVideoToggleFrameBlending();
void OnCommandVideoToggleInterlace();
void OnCommandVideoToggleScanlines();
void OnCommandVideoToggleXEP80();
void OnCommandVideoToggleVBXE();
void OnCommandVideoToggleVBXESharedMemory();
void OnCommandVideoToggleVBXEAltPage();
void OnCommandVideoAdjustColorsDialog();
void OnCommandVideoArtifacting(ATGTIAEmulator::ArtifactMode mode);
void OnCommandVideoEnhancedModeNone();
void OnCommandVideoEnhancedModeHardware();
void OnCommandVideoEnhancedModeCIO();
void OnCommandVideoEnhancedTextFontDialog();
void OnCommandAudioToggleStereo();
void OnCommandAudioToggleMonitor();
void OnCommandAudioToggleMute();
void OnCommandAudioOptionsDialog();
void OnCommandAudioToggleNonlinearMixing();
void OnCommandAudioToggleSerialNoise();
void OnCommandAudioToggleChannel(int channel);
void OnCommandAudioToggleSlightSid();
void OnCommandAudioToggleCovox();
void OnCommandInputCaptureMouse();
void OnCommandInputToggleAutoCaptureMouse();
void OnCommandInputInputMappingsDialog();
void OnCommandInputKeyboardDialog();
void OnCommandInputLightPenDialog();
void OnCommandInputCycleQuickMaps();
void OnCommandInputInputSetupDialog();
void OnCommandRecordStop();
void OnCommandRecordRawAudio();
void OnCommandRecordAudio();
void OnCommandRecordVideo();
void OnCommandRecordSapTypeR();
void OnCommandCheatTogglePMCollisions();
void OnCommandCheatTogglePFCollisions();
void OnCommandCheatCheatDialog();
void OnCommandToolsDiskExplorer();
void OnCommandToolsConvertSapToExe();
void OnCommandToolsOptionsDialog();
void OnCommandToolsKeyboardShortcutsDialog();
void OnCommandToolsSetupWizard();
void OnCommandWindowClose();
void OnCommandWindowUndock();
void OnCommandHelpContents();
void OnCommandHelpAbout();
void OnCommandHelpChangeLog();

namespace ATCommands {
	template<int Index>
	bool CartAttached() {
		return g_sim.IsCartridgeAttached(Index);
	}

	bool IsCassetteLoaded() {
		return g_sim.GetCassette().IsLoaded();
	}

	bool IsCassetteLoadDataAsAudioEnabled() {
		return g_sim.GetCassette().IsLoadDataAsAudioEnabled();
	}

	bool IsCassetteRandomizeStartPositionEnabled() {
		return g_sim.IsCassetteRandomizedStartEnabled();
	}

	bool IsSlightSidEnabled() {
		return g_sim.GetDeviceManager()->GetDeviceByTag("slightsid") != NULL;
	}

	bool IsCovoxEnabled() {
		return g_sim.GetDeviceManager()->GetDeviceByTag("covox") != NULL;
	}

	bool IsRTime8Enabled() {
		return g_sim.GetDeviceManager()->GetDeviceByTag("rtime8") != NULL;
	}

	bool IsXEP80Enabled() {
		return g_sim.GetDeviceManager()->GetDeviceByTag("xep80") != NULL;
	}

	bool IsXEP80ViewEnabled() {
		return g_xepViewEnabled;
	}

	bool IsXEP80ViewAutoswitchingEnabled() {
		return g_xepViewAutoswitchingEnabled;
	}

	bool IsVBXEEnabled() {
		return g_sim.GetVBXE() != NULL;
	}

	bool IsCartResetButtonPresent() {
		return ATUIGetDeviceButtonSupported(kATDeviceButton_CartridgeResetBank);
	}

	bool IsCartSDXSwitchPresent() {
		return ATUIGetDeviceButtonSupported(kATDeviceButton_CartridgeSDXEnable);
	}

	bool IsSIDESDXEnabled() {
		return ATUIGetDeviceButtonDepressed(kATDeviceButton_CartridgeSDXEnable);
	}

	template<ATHardwareMode T_Mode>
	bool HardwareModeIs() {
		return g_sim.GetHardwareMode() == T_Mode;
	}

	template<ATKernelMode T_Mode>
	bool KernelModeIs() {
		return g_sim.GetKernelMode() == T_Mode;
	}

	template<ATFirmwareId T_Id>
	bool KernelIs() {
		return g_sim.GetKernelId() == T_Id;
	}

	template<ATFirmwareId T_Id>
	bool BasicIs() {
		return g_sim.GetBasicId() == T_Id;
	}

	template<ATMemoryMode T_Mode>
	bool MemoryModeIs() {
		return g_sim.GetMemoryMode() == T_Mode;
	}

	template<uint8 T_BankBits>
	bool AxlonMemoryModeIs() {
		return g_sim.GetAxlonMemoryMode() == T_BankBits;
	}

	template<ATMemoryClearMode T_Mode>
	bool MemoryClearModeIs() {
		return g_sim.GetMemoryClearMode() == T_Mode;
	}

	bool Is65C816() {
		return g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816;
	}

	template<sint32 T_Banks>
	bool HighMemBanksIs() {
		return g_sim.GetHighMemoryBanks() == T_Banks;
	}

	template<ATVideoStandard T_Standard>
	bool VideoStandardIs() {
		return g_sim.GetVideoStandard() == T_Standard;
	}

	template<ATGTIAEmulator::ArtifactMode T_Mode>
	bool VideoArtifactingModeIs() {
		return g_sim.GetGTIA().GetArtifactingMode() == T_Mode;
	}

	template<ATDisplayFilterMode T_Mode>
	bool VideoFilterModeIs() {
		return ATUIGetDisplayFilterMode() == T_Mode;
	}

	template<ATGTIAEmulator::OverscanMode T_Mode>
	bool VideoOverscanModeIs() {
		return g_sim.GetGTIA().GetOverscanMode() == T_Mode;
	}

	template<ATGTIAEmulator::VerticalOverscanMode T_Mode>
	bool VideoVerticalOverscanModeIs() {
		return g_sim.GetGTIA().GetVerticalOverscanMode() == T_Mode;
	}

	template<ATDisplayStretchMode T_Mode>
	bool VideoStretchModeIs() {
		return ATUIGetDisplayStretchMode() == T_Mode;
	}

	template<int T_Mode>
	bool VideoEnhancedTextModeIs() {
		return ATUIGetEnhancedTextMode() == T_Mode;
	}

	template<int T_Value>
	bool VideoFilterSharpnessIs() {
		return ATUIGetViewFilterSharpness() == T_Value;
	}

	bool IsShowFpsEnabled() {
		return g_showFps;
	}

	bool Is800() {
		return g_sim.GetHardwareMode() == kATHardwareMode_800;
	}

	bool Is5200() {
		return g_sim.GetHardwareMode() == kATHardwareMode_5200;
	}

	bool IsNot5200() {
		return g_sim.GetHardwareMode() != kATHardwareMode_5200;
	}

	bool IsXLHardware() {
		switch(g_sim.GetHardwareMode()) {
			case kATHardwareMode_800XL:
			case kATHardwareMode_1200XL:
			case kATHardwareMode_XEGS:
			case kATHardwareMode_130XE:
				return true;

			default:
				return false;
		}
	}

	bool SupportsBASIC() {
		switch(g_sim.GetHardwareMode()) {
			case kATHardwareMode_800XL:
			case kATHardwareMode_130XE:
			case kATHardwareMode_XEGS:
				return true;

			case kATHardwareMode_1200XL:
				return g_sim.IsUltimate1MBEnabled();

			default:
				return false;
		}
	}

	template<ATStorageId T_StorageId>
	bool IsStoragePresent() {
		return g_sim.IsStoragePresent(T_StorageId);
	}

	bool IsDiskEnabled() {
		return g_sim.GetDiskDrive(0).IsEnabled();
	}

	bool IsDiskAccurateSectorTimingEnabled() {
		return g_sim.GetDiskDrive(0).IsAccurateSectorTimingEnabled();
	}

	bool IsDiskDriveEnabledAny() {
		for(int i=0; i<15; ++i) {
			if (g_sim.GetDiskDrive(i).IsEnabled())
				return true;
		}

		return false;
	}

	template<int index>
	bool IsDiskDriveEnabled() {
		return g_sim.GetDiskDrive(index).IsEnabled();
	}

	template<int index>
	void UIFormatMenuItemDiskName(VDStringW& s) {
		const ATDiskEmulator& disk = g_sim.GetDiskDrive(index);

		if (disk.IsEnabled()) {
			if (disk.IsDiskLoaded()) {
				s.append_sprintf(L" [%ls]", disk.GetPath());
			} else {
				s += L" [-]";
			}
		}
	}

	bool AreDriveSoundsEnabled() {
		return ATUIGetDriveSoundsEnabled();
	}

	bool IsDebuggerEnabled() {
		return ATIsDebugConsoleActive();
	}

	bool IsDebuggerRunning() {
		return g_sim.IsRunning() || ATGetDebugger()->AreCommandsQueued();
	}

	bool IsRunning() {
		return g_sim.IsRunning();
	}

	bool IsNotRunning() {
		return !g_sim.IsRunning();
	}

	bool IsSlowMotion() {
		return ATUIGetSlowMotion();
	}

	bool IsWarpSpeed() {
		return ATUIGetTurbo();
	}

	bool IsFullScreen() {
		return ATUIGetFullscreen();
	}

	bool IsVideoFrameAvailable() {
		return g_sim.GetGTIA().GetLastFrameBuffer() != NULL;
	}

	bool IsRecording() {
		return ATUIGetRecordingStatus() != kATUIRecordingStatus_None;
	}

	template<ATUIRecordingStatus T_Status>
	bool RecordingStatusIs() {
		return ATUIGetRecordingStatus() == T_Status;
	}

	bool IsCopyTextAvailable() {
		IATDisplayPane *pane = ATGetUIPaneAs<IATDisplayPane>(kATUIPaneId_Display);
		return pane && pane->IsTextSelected();
	}

	bool IsAudioMuted() {
		return g_sim.GetAudioOutput()->GetMute();
	}

	bool IsMouseMapped() {
		return g_sim.GetInputManager()->IsMouseMapped();
	}

	bool IsBreakAtExeRunAddrEnabled() {
		return ATGetDebugger()->IsBreakOnEXERunAddrEnabled();
	}

	template<int Index>
	bool PokeyChannelEnabled() {
		return g_sim.GetPokey().IsChannelEnabled(Index);
	}

	template<bool (ATSimulator::*T_Method)() const>
	bool SimTest() {
		return (g_sim.*T_Method)() ? true : false;
	}

	template<bool (ATGTIAEmulator::*T_Method)() const>
	bool GTIATest() {
		return (g_sim.GetGTIA().*T_Method)();
	}

	template<bool (ATPokeyEmulator::*T_Method)() const>
	bool PokeyTest() {
		return (g_sim.GetPokey().*T_Method)();
	}

	const struct ATUICommand kATCommands[]={
		{ "File.OpenImage", OnCommandOpenImage, NULL },
		{ "File.BootImage", OnCommandBootImage, NULL },
		{ "File.QuickLoadState", OnCommandQuickLoadState, NULL },
		{ "File.QuickSaveState", OnCommandQuickSaveState, NULL },
		{ "File.LoadState", OnCommandLoadState, NULL },
		{ "File.SaveState", OnCommandSaveState, NULL },

		{ "Cart.Attach", OnCommandAttachCartridge, NULL },
		{ "Cart.Detach", OnCommandDetachCartridge, CartAttached<0> },
		{ "Cart.Save", OnCommandSaveCartridge, CartAttached<0> },

		{ "Cart.AttachSecond", OnCommandAttachCartridge2, NULL },
		{ "Cart.DetachSecond", OnCommandDetachCartridge2, CartAttached<1> },

		{ "Cart.AttachSC3D",				[]() { OnCommandAttachNewCartridge(kATCartridgeMode_SuperCharger3D); }, NULL },
		{ "Cart.AttachMaxFlash1MB",			[]() { OnCommandAttachNewCartridge(kATCartridgeMode_MaxFlash_128K); }, NULL },
		{ "Cart.AttachMaxFlash1MBMyIDE",	[]() { OnCommandAttachNewCartridge(kATCartridgeMode_MaxFlash_128K_MyIDE); }, NULL },
		{ "Cart.AttachMaxFlash8MB",			[]() { OnCommandAttachNewCartridge(kATCartridgeMode_MaxFlash_1024K); }, NULL },
		{ "Cart.AttachMaxFlash8MBBank0",	[]() { OnCommandAttachNewCartridge(kATCartridgeMode_MaxFlash_1024K_Bank0); }, NULL },
		{ "Cart.AttachSIC",					[]() { OnCommandAttachNewCartridge(kATCartridgeMode_SIC); }, NULL },
		{ "Cart.AttachMegaCart512K",		[]() { OnCommandAttachNewCartridge(kATCartridgeMode_MegaCart_512K_3); }, NULL },
		{ "Cart.AttachMegaCart4MB",			[]() { OnCommandAttachNewCartridge(kATCartridgeMode_MegaCart_4M_3); }, NULL },
		{ "Cart.AttachTheCart32MB",			[]() { OnCommandAttachNewCartridge(kATCartridgeMode_TheCart_32M); }, NULL },
		{ "Cart.AttachTheCart64MB",			[]() { OnCommandAttachNewCartridge(kATCartridgeMode_TheCart_64M); }, NULL },
		{ "Cart.AttachTheCart128MB",		[]() { OnCommandAttachNewCartridge(kATCartridgeMode_TheCart_128M); }, NULL },
		{ "Cart.AttachBASIC",				OnCommandAttachCartridgeBASIC, NULL },

		{ "Cart.ActivateMenuButton", OnCommandCartActivateMenuButton, IsCartResetButtonPresent },
		{ "Cart.ToggleSwitch", OnCommandCartToggleSwitch, IsCartSDXSwitchPresent, CheckedIf<SimTest<&ATSimulator::GetCartridgeSwitch> > },

		{ "System.SaveFirmwareIDEMain", OnCommandSaveFirmwareIDEMain, IsStoragePresent<kATStorageId_Firmware> },
		{ "System.SaveFirmwareIDESDX", OnCommandSaveFirmwareIDESDX, IsStoragePresent<(ATStorageId)(kATStorageId_Firmware + 1)> },
		{ "System.SaveFirmwareU1MB", OnCommandSaveFirmwareU1MB, IsStoragePresent<(ATStorageId)(kATStorageId_Firmware + 2)> },
		{ "File.Exit", OnCommandExit, NULL },

		{ "Cassette.LoadNew", OnCommandCassetteLoadNew, NULL },
		{ "Cassette.Load", OnCommandCassetteLoad, NULL },
		{ "Cassette.Unload", OnCommandCassetteUnload, IsCassetteLoaded },
		{ "Cassette.Save", OnCommandCassetteSave, IsCassetteLoaded },
		{ "Cassette.TapeControlDialog", OnCommandCassetteTapeControlDialog, NULL },
		{ "Cassette.ToggleSIOPatch", OnCommandCassetteToggleSIOPatch, NULL, CheckedIf<SimTest<&ATSimulator::IsCassetteSIOPatchEnabled> > },
		{ "Cassette.ToggleAutoBoot", OnCommandCassetteToggleAutoBoot, NULL, CheckedIf<SimTest<&ATSimulator::IsCassetteAutoBootEnabled> > },
		{ "Cassette.ToggleLoadDataAsAudio", OnCommandCassetteToggleLoadDataAsAudio, NULL, CheckedIf<IsCassetteLoadDataAsAudioEnabled> },
		{ "Cassette.ToggleRandomizeStartPosition", OnCommandCassetteToggleRandomizeStartPosition, NULL, CheckedIf<IsCassetteRandomizeStartPositionEnabled> },

		{ "Debug.OpenSourceFile", OnCommandDebuggerOpenSourceFile, NULL },
		{ "Debug.ToggleBreakAtExeRun", OnCommandDebuggerToggleBreakAtExeRun, NULL, CheckedIf<IsBreakAtExeRunAddrEnabled> },

		{ "Debug.ToggleAutoReloadRoms", OnCommandDebugToggleAutoReloadRoms, NULL, CheckedIf<SimTest<&ATSimulator::IsROMAutoReloadEnabled> > },
		{ "Debug.ToggleAutoLoadKernelSymbols", OnCommandDebugToggleAutoLoadKernelSymbols, NULL, CheckedIf<SimTest<&ATSimulator::IsAutoLoadKernelSymbolsEnabled> > },
		{ "Debug.ChangeFontDialog", OnCommandDebugChangeFontDialog },
		{ "Debug.ToggleDebugger", OnCommandDebugToggleDebugger, NULL, CheckedIf<IsDebuggerEnabled> },
		{ "Debug.Run", OnCommandDebugRun, IsNotRunning },
		{ "Debug.Break", OnCommandDebugBreak, IsDebuggerRunning },
		{ "Debug.RunStop", OnCommandDebugRunStop },
		{ "Debug.StepInto", OnCommandDebugStepInto, IsNotRunning },
		{ "Debug.StepOut", OnCommandDebugStepOut, IsNotRunning },
		{ "Debug.StepOver", OnCommandDebugStepOver, IsNotRunning },
		{ "Debug.ToggleBreakpoint", OnCommandDebugToggleBreakpoint, IsDebuggerEnabled },
		{ "Debug.VerifierDialog", OnCommandDebugVerifierDialog, NULL, CheckedIf<SimTest<&ATSimulator::IsVerifierEnabled> > },

		{ "View.NextFilterMode", OnCommandViewNextFilterMode, NULL },
		{ "View.FilterModePoint", OnCommandViewFilterModePoint, NULL, RadioCheckedIf<VideoFilterModeIs<kATDisplayFilterMode_Point> > },
		{ "View.FilterModeBilinear", OnCommandViewFilterModeBilinear, NULL, RadioCheckedIf<VideoFilterModeIs<kATDisplayFilterMode_Bilinear> > },
		{ "View.FilterModeSharpBilinear", OnCommandViewFilterModeSharpBilinear, NULL, RadioCheckedIf<VideoFilterModeIs<kATDisplayFilterMode_SharpBilinear> > },
		{ "View.FilterModeBicubic", OnCommandViewFilterModeBicubic, NULL, RadioCheckedIf<VideoFilterModeIs<kATDisplayFilterMode_Bicubic> > },
		{ "View.FilterModeDefault", OnCommandViewFilterModeDefault, NULL, RadioCheckedIf<VideoFilterModeIs<kATDisplayFilterMode_AnySuitable> > },

		{ "View.FilterSharpnessSofter", OnCommandViewFilterSharpnessSofter, VideoFilterModeIs<kATDisplayFilterMode_SharpBilinear>, RadioCheckedIf<VideoFilterSharpnessIs<-2> > },
		{ "View.FilterSharpnessSoft", OnCommandViewFilterSharpnessSoft, VideoFilterModeIs<kATDisplayFilterMode_SharpBilinear>, RadioCheckedIf<VideoFilterSharpnessIs<-1> > },
		{ "View.FilterSharpnessNormal", OnCommandViewFilterSharpnessNormal, VideoFilterModeIs<kATDisplayFilterMode_SharpBilinear>, RadioCheckedIf<VideoFilterSharpnessIs<0> > },
		{ "View.FilterSharpnessSharp", OnCommandViewFilterSharpnessSharp, VideoFilterModeIs<kATDisplayFilterMode_SharpBilinear>, RadioCheckedIf<VideoFilterSharpnessIs<+1> > },
		{ "View.FilterSharpnessSharper", OnCommandViewFilterSharpnessSharper, VideoFilterModeIs<kATDisplayFilterMode_SharpBilinear>, RadioCheckedIf<VideoFilterSharpnessIs<+2> > },

		{ "View.StretchFitToWindow", OnCommandViewStretchFitToWindow, NULL, RadioCheckedIf<VideoStretchModeIs<kATDisplayStretchMode_Unconstrained> > },
		{ "View.StretchPreserveAspectRatio", OnCommandViewStretchPreserveAspectRatio, NULL, RadioCheckedIf<VideoStretchModeIs<kATDisplayStretchMode_PreserveAspectRatio> > },
		{ "View.StretchSquarePixels", OnCommandViewStretchSquarePixels, NULL, RadioCheckedIf<VideoStretchModeIs<kATDisplayStretchMode_SquarePixels> > },
		{ "View.StretchSquarePixelsInt", OnCommandViewStretchSquarePixelsInt, NULL, RadioCheckedIf<VideoStretchModeIs<kATDisplayStretchMode_Integral> > },
		{ "View.StretchPreserveAspectRatioInt", OnCommandViewStretchPreserveAspectRatioInt, NULL, RadioCheckedIf<VideoStretchModeIs<kATDisplayStretchMode_IntegralPreserveAspectRatio> > },

		{ "View.OverscanOSScreen", OnCommandViewOverscanOSScreen, NULL, RadioCheckedIf<VideoOverscanModeIs<ATGTIAEmulator::kOverscanOSScreen> > },
		{ "View.OverscanNormal", OnCommandViewOverscanNormal, NULL, RadioCheckedIf<VideoOverscanModeIs<ATGTIAEmulator::kOverscanNormal> > },
		{ "View.OverscanExtended", OnCommandViewOverscanExtended, NULL, RadioCheckedIf<VideoOverscanModeIs<ATGTIAEmulator::kOverscanExtended> > },
		{ "View.OverscanFull", OnCommandViewOverscanFull, NULL, RadioCheckedIf<VideoOverscanModeIs<ATGTIAEmulator::kOverscanFull> > },
		{ "View.VerticalOverrideOff",		[]() { OnCommandViewVerticalOverscan(ATGTIAEmulator::kVerticalOverscan_Default); },		NULL, RadioCheckedIf<VideoVerticalOverscanModeIs<ATGTIAEmulator::kVerticalOverscan_Default> > },
		{ "View.VerticalOverrideOSScreen",	[]() { OnCommandViewVerticalOverscan(ATGTIAEmulator::kVerticalOverscan_OSScreen); },	NULL, RadioCheckedIf<VideoVerticalOverscanModeIs<ATGTIAEmulator::kVerticalOverscan_OSScreen> > },
		{ "View.VerticalOverrideNormal",	[]() { OnCommandViewVerticalOverscan(ATGTIAEmulator::kVerticalOverscan_Normal); },		NULL, RadioCheckedIf<VideoVerticalOverscanModeIs<ATGTIAEmulator::kVerticalOverscan_Normal> > },
		{ "View.VerticalOverrideExtended",	[]() { OnCommandViewVerticalOverscan(ATGTIAEmulator::kVerticalOverscan_Extended); },	NULL, RadioCheckedIf<VideoVerticalOverscanModeIs<ATGTIAEmulator::kVerticalOverscan_Extended> > },
		{ "View.VerticalOverrideFull",		[]() { OnCommandViewVerticalOverscan(ATGTIAEmulator::kVerticalOverscan_Full); },		NULL, RadioCheckedIf<VideoVerticalOverscanModeIs<ATGTIAEmulator::kVerticalOverscan_Full> > },
		{ "View.TogglePALExtended", OnCommandViewTogglePALExtended, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::IsOverscanPALExtended> > },

		{ "View.ToggleFullScreen", OnCommandViewToggleFullScreen, NULL, CheckedIf<IsFullScreen> },
		{ "View.ToggleVSync", OnCommandViewToggleVSync, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::IsVsyncEnabled> > },
		{ "View.ToggleFPS", OnCommandViewToggleFPS, NULL, CheckedIf<IsShowFpsEnabled> },
		{ "View.AdjustWindowSize", OnCommandViewAdjustWindowSize },
		{ "View.ResetWindowLayout", OnCommandViewResetWindowLayout },

		{ "View.NextANTICVisMode", OnCommandAnticVisualizationNext },
		{ "View.NextGTIAVisMode", OnCommandGTIAVisualizationNext },

		{ "View.ToggleXEP80View", OnCommandVideoToggleXEP80View, IsXEP80Enabled, CheckedIf<IsXEP80ViewEnabled> },
		{ "View.ToggleXEP80ViewAutoswitching", OnCommandVideoToggleXEP80ViewAutoswitching, IsXEP80Enabled, CheckedIf<IsXEP80ViewAutoswitchingEnabled> },

		{ "View.EffectReload", OnCommandViewEffectReload },

		{ "Pane.Display",			[]() { OnCommandPane(kATUIPaneId_Display); }		},
		{ "Pane.PrinterOutput",		[]() { OnCommandPane(kATUIPaneId_PrinterOutput); }	},
		{ "Pane.Console",			[]() { OnCommandPane(kATUIPaneId_Console); },		IsDebuggerEnabled },
		{ "Pane.Registers",			[]() { OnCommandPane(kATUIPaneId_Registers); },		IsDebuggerEnabled },
		{ "Pane.Disassembly",		[]() { OnCommandPane(kATUIPaneId_Disassembly); },	IsDebuggerEnabled },
		{ "Pane.CallStack",			[]() { OnCommandPane(kATUIPaneId_CallStack); },		IsDebuggerEnabled },
		{ "Pane.History",			[]() { OnCommandPane(kATUIPaneId_History); },		IsDebuggerEnabled },
		{ "Pane.Memory1",			[]() { OnCommandPane(kATUIPaneId_MemoryN + 0); },	IsDebuggerEnabled },
		{ "Pane.Memory2",			[]() { OnCommandPane(kATUIPaneId_MemoryN + 1); },	IsDebuggerEnabled },
		{ "Pane.Memory3",			[]() { OnCommandPane(kATUIPaneId_MemoryN + 2); },	IsDebuggerEnabled },
		{ "Pane.Memory4",			[]() { OnCommandPane(kATUIPaneId_MemoryN + 3); },	IsDebuggerEnabled },
		{ "Pane.Watch1",			[]() { OnCommandPane(kATUIPaneId_WatchN + 0); },	IsDebuggerEnabled },
		{ "Pane.Watch2",			[]() { OnCommandPane(kATUIPaneId_WatchN + 1); },	IsDebuggerEnabled },
		{ "Pane.Watch3",			[]() { OnCommandPane(kATUIPaneId_WatchN + 2); },	IsDebuggerEnabled },
		{ "Pane.Watch4",			[]() { OnCommandPane(kATUIPaneId_WatchN + 3); },	IsDebuggerEnabled },
		{ "Pane.DebugDisplay",		[]() { OnCommandPane(kATUIPaneId_DebugDisplay); },	IsDebuggerEnabled },
		{ "Pane.ProfileView",		[]() { OnCommandPane(kATUIPaneId_Profiler); },		IsDebuggerEnabled },

		{ "Edit.CopyFrame", OnCommandEditCopyFrame, IsVideoFrameAvailable },
		{ "Edit.SaveFrame", OnCommandEditSaveFrame, IsVideoFrameAvailable },
		{ "Edit.CopyText", OnCommandEditCopyText, IsCopyTextAvailable },
		{ "Edit.PasteText", OnCommandEditPasteText, ATUIClipIsTextAvailable },

		{ "System.WarmReset", OnCommandSystemWarmReset },
		{ "System.ColdReset", OnCommandSystemColdReset },

		{ "System.TogglePause", OnCommandSystemTogglePause },
		{ "System.TogglePauseWhenInactive", OnCommandSystemTogglePauseWhenInactive, NULL, CheckedIf<ATUIGetPauseWhenInactive> },
		{ "System.ToggleSlowMotion", OnCommandSystemToggleSlowMotion, NULL, CheckedIf<IsSlowMotion> },
		{ "System.ToggleWarpSpeed", OnCommandSystemToggleWarpSpeed, NULL, CheckedIf<IsWarpSpeed> },
		{ "System.PulseWarpOff", OnCommandSystemPulseWarpOff, NULL, NULL },
		{ "System.PulseWarpOn", OnCommandSystemPulseWarpOn, NULL, NULL },
		{ "System.CPUOptionsDialog", OnCommandSystemCPUOptionsDialog },
		{ "System.ToggleFPPatch", OnCommandSystemToggleFPPatch, NULL, CheckedIf<SimTest<&ATSimulator::IsFPPatchEnabled> > },
		{ "System.SpeedOptionsDialog", OnCommandSystemSpeedOptionsDialog },
		{ "System.DevicesDialog", OnCommandSystemDevicesDialog },

		{ "System.ToggleKeyboardPresent", OnCommandSystemToggleKeyboardPresent, HardwareModeIs<kATHardwareMode_XEGS>, CheckedIf<And<HardwareModeIs<kATHardwareMode_XEGS>, SimTest<&ATSimulator::IsKeyboardPresent> > >},
		{ "System.ToggleForcedSelfTest", OnCommandSystemToggleForcedSelfTest, IsXLHardware, CheckedIf<And<IsXLHardware, SimTest<&ATSimulator::IsForcedSelfTest> > > },
		{ "Console.BlackBoxDumpScreen", OnCommandConsoleBlackBoxDumpScreen, []() { return ATUIGetDeviceButtonSupported(kATDeviceButton_BlackBoxDumpScreen); } },
		{ "Console.BlackBoxMenu", OnCommandConsoleBlackBoxMenu, []() { return ATUIGetDeviceButtonSupported(kATDeviceButton_BlackBoxMenu); } },
		{ "Console.IDEPlus2SwitchDisks",	OnCommandConsoleIDEPlus2SwitchDisks, []() { return ATUIGetDeviceButtonSupported(kATDeviceButton_IDEPlus2SwitchDisks); } },
		{ "Console.IDEPlus2WriteProtect",
			OnCommandConsoleIDEPlus2WriteProtect,
			[] { return ATUIGetDeviceButtonSupported(kATDeviceButton_IDEPlus2WriteProtect); },
			[] { return ATUIGetDeviceButtonDepressed(kATDeviceButton_IDEPlus2WriteProtect) ? kATUICmdState_Checked : kATUICmdState_None; }
		},
		{ "Console.IDEPlus2SDX",
			OnCommandConsoleIDEPlus2SDX,
			[] { return ATUIGetDeviceButtonSupported(kATDeviceButton_IDEPlus2SDX); },
			[] { return ATUIGetDeviceButtonDepressed(kATDeviceButton_IDEPlus2SDX) ? kATUICmdState_Checked : kATUICmdState_None; }
		},

		{ "System.HardwareMode800",		[]() { OnCommandSystemHardwareMode(kATHardwareMode_800); }, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_800> > },
		{ "System.HardwareMode800XL",	[]() { OnCommandSystemHardwareMode(kATHardwareMode_800XL); }, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_800XL> > },
		{ "System.HardwareMode1200XL",	[]() { OnCommandSystemHardwareMode(kATHardwareMode_1200XL); }, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_1200XL> > },
		{ "System.HardwareModeXEGS",	[]() { OnCommandSystemHardwareMode(kATHardwareMode_XEGS); }, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_XEGS> > },
		{ "System.HardwareMode130XE",	[]() { OnCommandSystemHardwareMode(kATHardwareMode_130XE); }, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_130XE> > },
		{ "System.HardwareMode5200",	[]() { OnCommandSystemHardwareMode(kATHardwareMode_5200); }, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_5200> > },

		{ "System.KernelModeDefault",	[]() { OnCommandSystemKernel(kATFirmwareId_Invalid); }, NULL, RadioCheckedIf<KernelIs<kATFirmwareId_Invalid> > },
		{ "System.KernelModeHLE",		[]() { OnCommandSystemKernel(kATFirmwareId_Kernel_HLE); }, NULL, RadioCheckedIf<KernelIs<kATFirmwareId_Kernel_HLE> > },
		{ "System.KernelModeLLE",		[]() { OnCommandSystemKernel(kATFirmwareId_Kernel_LLE); }, NULL, RadioCheckedIf<KernelIs<kATFirmwareId_Kernel_LLE> > },
		{ "System.KernelModeLLEXL",		[]() { OnCommandSystemKernel(kATFirmwareId_Kernel_LLEXL); }, NULL, RadioCheckedIf<KernelIs<kATFirmwareId_Kernel_LLEXL> > },
		{ "System.KernelMode5200LLE",	[]() { OnCommandSystemKernel(kATFirmwareId_5200_LLE); }, NULL, RadioCheckedIf<KernelIs<kATFirmwareId_5200_LLE> > },

		{ "System.BasicDefault",		[]() { OnCommandSystemBasic(kATFirmwareId_Invalid); }, NULL, RadioCheckedIf<BasicIs<kATFirmwareId_Invalid> > },

		{ "System.MemoryMode8K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_8K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_8K> > },
		{ "System.MemoryMode16K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_16K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Not<HardwareModeIs<kATHardwareMode_1200XL> > >, RadioCheckedIf<MemoryModeIs<kATMemoryMode_16K> > },
		{ "System.MemoryMode24K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_24K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_24K> > },
		{ "System.MemoryMode32K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_32K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_32K> > },
		{ "System.MemoryMode40K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_40K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_40K> > },
		{ "System.MemoryMode48K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_48K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_48K> > },
		{ "System.MemoryMode52K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_52K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_52K> > },
		{ "System.MemoryMode64K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_64K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_64K> > },
		{ "System.MemoryMode128K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_128K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_128K> > },
		{ "System.MemoryMode256K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_256K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_256K> > },
		{ "System.MemoryMode320K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_320K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_320K> > },
		{ "System.MemoryMode320KCompy",	[]() { OnCommandSystemMemoryMode(kATMemoryMode_320K_Compy); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_320K_Compy> > },
		{ "System.MemoryMode576K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_576K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_576K> > },
		{ "System.MemoryMode576KCompy",	[]() { OnCommandSystemMemoryMode(kATMemoryMode_576K_Compy); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_576K_Compy> > },
		{ "System.MemoryMode1088K",		[]() { OnCommandSystemMemoryMode(kATMemoryMode_1088K); }, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_1088K> > },

		{ "System.AxlonMemoryNone",		[]() { OnCommandSystemAxlonMemoryMode(0); }, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<0> > },
		{ "System.AxlonMemory64K",		[]() { OnCommandSystemAxlonMemoryMode(2); }, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<2> > },
		{ "System.AxlonMemory128K",		[]() { OnCommandSystemAxlonMemoryMode(3); }, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<3> > },
		{ "System.AxlonMemory256K",		[]() { OnCommandSystemAxlonMemoryMode(4); }, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<4> > },
		{ "System.AxlonMemory512K",		[]() { OnCommandSystemAxlonMemoryMode(5); }, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<5> > },
		{ "System.AxlonMemory1024K",	[]() { OnCommandSystemAxlonMemoryMode(6); }, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<6> > },
		{ "System.AxlonMemory2048K",	[]() { OnCommandSystemAxlonMemoryMode(7); }, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<7> > },
		{ "System.AxlonMemory4096K",	[]() { OnCommandSystemAxlonMemoryMode(8); }, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<8> > },

		{ "System.ToggleAxlonAliasing",
			[]() { g_sim.SetAxlonAliasingEnabled(!g_sim.GetAxlonAliasingEnabled()); },
			IsNot5200,
			[]() {
				return g_sim.GetAxlonAliasingEnabled() ? kATUICmdState_Checked : kATUICmdState_None;
			}
		},

		{ "System.HighMemoryNA",		[]() { OnCommandSystemHighMemBanks(-1); }, Is65C816, RadioCheckedIf<HighMemBanksIs<-1> > },
		{ "System.HighMemoryNone",		[]() { OnCommandSystemHighMemBanks(0); }, Is65C816, RadioCheckedIf<HighMemBanksIs<0> > },
		{ "System.HighMemory64K",		[]() { OnCommandSystemHighMemBanks(1); }, Is65C816, RadioCheckedIf<HighMemBanksIs<1> > },
		{ "System.HighMemory192K",		[]() { OnCommandSystemHighMemBanks(3); }, Is65C816, RadioCheckedIf<HighMemBanksIs<3> > },
		{ "System.HighMemory960K",		[]() { OnCommandSystemHighMemBanks(15); }, Is65C816, RadioCheckedIf<HighMemBanksIs<15> > },
		{ "System.HighMemory4032K",		[]() { OnCommandSystemHighMemBanks(63); }, Is65C816, RadioCheckedIf<HighMemBanksIs<63> > },

		{ "System.ToggleMapRAM", OnCommandSystemToggleMapRAM, NULL, CheckedIf<SimTest<&ATSimulator::IsMapRAMEnabled> > },
		{ "System.ToggleUltimate1MB", OnCommandSystemToggleUltimate1MB, NULL, CheckedIf<SimTest<&ATSimulator::IsUltimate1MBEnabled> > },
		{ "System.ToggleFloatingIOBus", OnCommandSystemToggleFloatingIoBus, Is800, CheckedIf<SimTest<&ATSimulator::IsFloatingIoBusEnabled> > },
		{ "System.MemoryClearZero",		[]() { OnCommandSystemMemoryClearMode(kATMemoryClearMode_Zero); }, NULL, RadioCheckedIf<MemoryClearModeIs<kATMemoryClearMode_Zero>> },
		{ "System.MemoryClearRandom",	[]() { OnCommandSystemMemoryClearMode(kATMemoryClearMode_Random); }, NULL, RadioCheckedIf<MemoryClearModeIs<kATMemoryClearMode_Random>> },
		{ "System.MemoryClearDRAM1",	[]() { OnCommandSystemMemoryClearMode(kATMemoryClearMode_DRAM1); }, NULL, RadioCheckedIf<MemoryClearModeIs<kATMemoryClearMode_DRAM1>> },
		{ "System.MemoryClearDRAM2",	[]() { OnCommandSystemMemoryClearMode(kATMemoryClearMode_DRAM2); }, NULL, RadioCheckedIf<MemoryClearModeIs<kATMemoryClearMode_DRAM2>> },
		{ "System.MemoryClearDRAM3",	[]() { OnCommandSystemMemoryClearMode(kATMemoryClearMode_DRAM3); }, NULL, RadioCheckedIf<MemoryClearModeIs<kATMemoryClearMode_DRAM3>> },
		{ "System.ToggleMemoryRandomizationEXE", OnCommandSystemToggleMemoryRandomizationEXE, NULL, CheckedIf<SimTest<&ATSimulator::IsRandomFillEXEEnabled> > },

		{ "System.ToggleBASIC", OnCommandSystemToggleBASIC, SupportsBASIC, CheckedIf<And<SupportsBASIC, SimTest<&ATSimulator::IsBASICEnabled> > > },
		{ "System.ToggleFastBoot", OnCommandSystemToggleFastBoot, IsNot5200, CheckedIf<SimTest<&ATSimulator::IsFastBootEnabled> > },
		{ "System.ROMImagesDialog", OnCommandSystemROMImagesDialog },
		{ "System.ToggleRTime8", OnCommandSystemToggleRTime8, NULL, CheckedIf<IsRTime8Enabled> },

		{ "System.EditProfilesDialog", OnCommandSystemEditProfilesDialog  },
		{ "System.ToggleTemporaryProfile",
				[] { ATSettingsSetTemporaryProfileMode(!ATSettingsGetTemporaryProfileMode()); },
				NULL,
				[] { return ATSettingsGetTemporaryProfileMode() ? kATUICmdState_Checked : kATUICmdState_None; }
		},

		{ "Disk.DrivesDialog", OnCommandDiskDrivesDialog },
		{ "Disk.ToggleAllEnabled", OnCommandDiskToggleAllEnabled, NULL, CheckedIf<IsDiskEnabled> },
		{ "Disk.ToggleSIOPatch", OnCommandDiskToggleSIOPatch, NULL, CheckedIf<SimTest<&ATSimulator::IsDiskSIOPatchEnabled> > },
		{ "Disk.ToggleSIOOverrideDetection", OnCommandDiskToggleSIOOverrideDetection, NULL, CheckedIf<SimTest<&ATSimulator::IsDiskSIOOverrideDetectEnabled> > },
		{ "Disk.ToggleAccurateSectorTiming", OnCommandDiskToggleAccurateSectorTiming, NULL, CheckedIf<IsDiskAccurateSectorTimingEnabled> },
		{ "Disk.ToggleDriveSounds", OnCommandDiskToggleDriveSounds, NULL, CheckedIf<AreDriveSoundsEnabled> },
		{ "Disk.ToggleSectorCounter", OnCommandDiskToggleSectorCounter, NULL, CheckedIf<SimTest<&ATSimulator::IsDiskSectorCounterEnabled> > },

		{ "Disk.Attach1", []() { OnCommandDiskAttach(0); }, NULL, NULL, UIFormatMenuItemDiskName<0> },
		{ "Disk.Attach2", []() { OnCommandDiskAttach(1); }, NULL, NULL, UIFormatMenuItemDiskName<1> },
		{ "Disk.Attach3", []() { OnCommandDiskAttach(2); }, NULL, NULL, UIFormatMenuItemDiskName<2> },
		{ "Disk.Attach4", []() { OnCommandDiskAttach(3); }, NULL, NULL, UIFormatMenuItemDiskName<3> },
		{ "Disk.Attach5", []() { OnCommandDiskAttach(4); }, NULL, NULL, UIFormatMenuItemDiskName<4> },
		{ "Disk.Attach6", []() { OnCommandDiskAttach(5); }, NULL, NULL, UIFormatMenuItemDiskName<5> },
		{ "Disk.Attach7", []() { OnCommandDiskAttach(6); }, NULL, NULL, UIFormatMenuItemDiskName<6> },
		{ "Disk.Attach8", []() { OnCommandDiskAttach(7); }, NULL, NULL, UIFormatMenuItemDiskName<7> },

		{ "Disk.DetachAll", OnCommandDiskDetachAll, IsDiskDriveEnabledAny },
		{ "Disk.Detach1", []() { OnCommandDiskDetach(0); }, IsDiskDriveEnabled<0>, NULL, UIFormatMenuItemDiskName<0> },
		{ "Disk.Detach2", []() { OnCommandDiskDetach(1); }, IsDiskDriveEnabled<1>, NULL, UIFormatMenuItemDiskName<1> },
		{ "Disk.Detach3", []() { OnCommandDiskDetach(2); }, IsDiskDriveEnabled<2>, NULL, UIFormatMenuItemDiskName<2> },
		{ "Disk.Detach4", []() { OnCommandDiskDetach(3); }, IsDiskDriveEnabled<3>, NULL, UIFormatMenuItemDiskName<3> },
		{ "Disk.Detach5", []() { OnCommandDiskDetach(4); }, IsDiskDriveEnabled<4>, NULL, UIFormatMenuItemDiskName<4> },
		{ "Disk.Detach6", []() { OnCommandDiskDetach(5); }, IsDiskDriveEnabled<5>, NULL, UIFormatMenuItemDiskName<5> },
		{ "Disk.Detach7", []() { OnCommandDiskDetach(6); }, IsDiskDriveEnabled<6>, NULL, UIFormatMenuItemDiskName<6> },
		{ "Disk.Detach8", []() { OnCommandDiskDetach(7); }, IsDiskDriveEnabled<7>, NULL, UIFormatMenuItemDiskName<7> },

		{ "Disk.RotatePrev", []() { OnCommandDiskRotate(-1); }, IsDiskDriveEnabledAny },
		{ "Disk.RotateNext", []() { OnCommandDiskRotate(+1); }, IsDiskDriveEnabledAny },

		{ "Disk.ToggleBurstTransfers",
			[]() {
				g_sim.SetDiskBurstTransfersEnabled(!g_sim.GetDiskBurstTransfersEnabled());
			},
			nullptr,
			[]() {
				return g_sim.GetDiskBurstTransfersEnabled() ? kATUICmdState_Checked : kATUICmdState_None;
			}
		},

		{ "Devices.ToggleCIOBurstTransfers",
			[]() {
				g_sim.SetDeviceCIOBurstTransfersEnabled(!g_sim.GetDeviceCIOBurstTransfersEnabled());
			},
			nullptr,
			[]() {
				return g_sim.GetDeviceCIOBurstTransfersEnabled() ? kATUICmdState_Checked : kATUICmdState_None;
			}
		},

		{ "Devices.ToggleSIOBurstTransfers",
			[]() {
				g_sim.SetDeviceSIOBurstTransfersEnabled(!g_sim.GetDeviceSIOBurstTransfersEnabled());
			},
			nullptr,
			[]() {
				return g_sim.GetDeviceSIOBurstTransfersEnabled() ? kATUICmdState_Checked : kATUICmdState_None;
			}
		},

		{ "Devices.ToggleSIOPatch",
			[]() { g_sim.SetDeviceSIOPatchEnabled(!g_sim.GetDeviceSIOPatchEnabled()); },
			nullptr,
			[]() { return g_sim.GetDeviceSIOPatchEnabled() ? kATUICmdState_Checked : kATUICmdState_None; },
		},

		{ "Devices.ToggleCIOPatchH",
			[]() { g_sim.SetCIOPatchEnabled('H', !g_sim.GetCIOPatchEnabled('H')); },
			[]() { return g_sim.HasCIODevice('H'); },
			[]() { return g_sim.GetCIOPatchEnabled('H') ? kATUICmdState_Checked : kATUICmdState_None; },
		},

		{ "Devices.ToggleCIOPatchP",
			[]() { g_sim.SetCIOPatchEnabled('P', !g_sim.GetCIOPatchEnabled('P')); },
			[]() { return g_sim.HasCIODevice('P'); },
			[]() { return g_sim.GetCIOPatchEnabled('P') ? kATUICmdState_Checked : kATUICmdState_None; },
		},

		{ "Devices.ToggleCIOPatchR",
			[]() { g_sim.SetCIOPatchEnabled('R', !g_sim.GetCIOPatchEnabled('R')); },
			[]() { return g_sim.HasCIODevice('R'); },
			[]() { return g_sim.GetCIOPatchEnabled('R') ? kATUICmdState_Checked : kATUICmdState_None; },
		},

		{ "Devices.ToggleCIOPatchT",
			[]() { g_sim.SetCIOPatchEnabled('T', !g_sim.GetCIOPatchEnabled('T')); },
			[]() { return g_sim.HasCIODevice('T'); },
			[]() { return g_sim.GetCIOPatchEnabled('T') ? kATUICmdState_Checked : kATUICmdState_None; },
		},

		{ "Devices.SIOAccelModePatch",
			[] { g_sim.SetSIOPatchEnabled(true); g_sim.SetPBIPatchEnabled(false); },
			nullptr,
			[] { return g_sim.IsSIOPatchEnabled() && !g_sim.IsPBIPatchEnabled() ? kATUICmdState_RadioChecked : kATUICmdState_None; }
		},
		{ "Devices.SIOAccelModePBI",
			[] { g_sim.SetSIOPatchEnabled(false); g_sim.SetPBIPatchEnabled(true); },
			nullptr,
			[] { return !g_sim.IsSIOPatchEnabled() && g_sim.IsPBIPatchEnabled() ? kATUICmdState_RadioChecked : kATUICmdState_None; }
		},
		{ "Devices.SIOAccelModeBoth",
			[] { g_sim.SetSIOPatchEnabled(true); g_sim.SetPBIPatchEnabled(true); },
			nullptr,
			[] { return g_sim.IsSIOPatchEnabled() && g_sim.IsPBIPatchEnabled() ? kATUICmdState_RadioChecked : kATUICmdState_None; }
		},

		{ "Video.StandardNTSC",		[]() { OnCommandVideoStandard(kATVideoStandard_NTSC); }, NULL, RadioCheckedIf<VideoStandardIs<kATVideoStandard_NTSC> > },
		{ "Video.StandardPAL",		[]() { OnCommandVideoStandard(kATVideoStandard_PAL); }, IsNot5200, RadioCheckedIf<VideoStandardIs<kATVideoStandard_PAL> > },
		{ "Video.StandardSECAM",	[]() { OnCommandVideoStandard(kATVideoStandard_SECAM); }, IsNot5200, RadioCheckedIf<VideoStandardIs<kATVideoStandard_SECAM> > },
		{ "Video.StandardNTSC50",	[]() { OnCommandVideoStandard(kATVideoStandard_NTSC50); }, NULL, RadioCheckedIf<VideoStandardIs<kATVideoStandard_NTSC50> > },
		{ "Video.StandardPAL60",	[]() { OnCommandVideoStandard(kATVideoStandard_PAL60); }, IsNot5200, RadioCheckedIf<VideoStandardIs<kATVideoStandard_PAL60> > },
		{ "Video.ToggleStandardNTSCPAL", OnCommandVideoToggleStandardNTSCPAL, IsNot5200 },

		{ "Video.ToggleFrameBlending", OnCommandVideoToggleFrameBlending, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::IsBlendModeEnabled> > },
		{ "Video.ToggleInterlace", OnCommandVideoToggleInterlace, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::IsInterlaceEnabled> > },
		{ "Video.ToggleScanlines", OnCommandVideoToggleScanlines, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::AreScanlinesEnabled> > },
		{ "Video.ToggleXEP80", OnCommandVideoToggleXEP80, NULL, CheckedIf<IsXEP80Enabled> },
		{ "Video.ToggleVBXE", OnCommandVideoToggleVBXE, NULL, CheckedIf<IsVBXEEnabled> },
		{ "Video.ToggleVBXESharedMemory", OnCommandVideoToggleVBXESharedMemory, IsVBXEEnabled, CheckedIf<SimTest<&ATSimulator::IsVBXESharedMemoryEnabled> > },
		{ "Video.ToggleVBXEAltPage", OnCommandVideoToggleVBXEAltPage, And<IsVBXEEnabled, Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> > >, CheckedIf<SimTest<&ATSimulator::IsVBXEAltPageEnabled> > },
		{ "Video.AdjustColorsDialog", OnCommandVideoAdjustColorsDialog },

		{ "Video.ArtifactingNone",		[]() { OnCommandVideoArtifacting(ATGTIAEmulator::kArtifactNone); }, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactNone> > },
		{ "Video.ArtifactingNTSC",		[]() { OnCommandVideoArtifacting(ATGTIAEmulator::kArtifactNTSC); }, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactNTSC> > },
		{ "Video.ArtifactingNTSCHi",	[]() { OnCommandVideoArtifacting(ATGTIAEmulator::kArtifactNTSCHi); }, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactNTSCHi> > },
		{ "Video.ArtifactingPAL",		[]() { OnCommandVideoArtifacting(ATGTIAEmulator::kArtifactPAL); }, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactPAL> > },
		{ "Video.ArtifactingPALHi",		[]() { OnCommandVideoArtifacting(ATGTIAEmulator::kArtifactPALHi); }, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactPALHi> > },

		{ "Video.EnhancedModeNone", OnCommandVideoEnhancedModeNone, NULL, RadioCheckedIf<VideoEnhancedTextModeIs<0> > },
		{ "Video.EnhancedModeHardware", OnCommandVideoEnhancedModeHardware, NULL, RadioCheckedIf<VideoEnhancedTextModeIs<1> > },
		{ "Video.EnhancedModeCIO", OnCommandVideoEnhancedModeCIO, NULL, RadioCheckedIf<VideoEnhancedTextModeIs<2> > },
		{ "Video.EnhancedTextFontDialog", OnCommandVideoEnhancedTextFontDialog },

		{ "Audio.ToggleStereo", OnCommandAudioToggleStereo, NULL, CheckedIf<SimTest<&ATSimulator::IsDualPokeysEnabled> > },
		{ "Audio.ToggleMonitor", OnCommandAudioToggleMonitor, NULL, CheckedIf<SimTest<&ATSimulator::IsAudioMonitorEnabled> > },
		{ "Audio.ToggleMute", OnCommandAudioToggleMute, NULL, CheckedIf<IsAudioMuted> },
		{ "Audio.OptionsDialog", OnCommandAudioOptionsDialog },
		{ "Audio.ToggleNonlinearMixing", OnCommandAudioToggleNonlinearMixing, NULL, CheckedIf<PokeyTest<&ATPokeyEmulator::IsNonlinearMixingEnabled> > },
		{ "Audio.ToggleSerialNoise", OnCommandAudioToggleSerialNoise, NULL, CheckedIf<PokeyTest<&ATPokeyEmulator::IsSerialNoiseEnabled> > },
		{ "Audio.ToggleChannel1", []() { OnCommandAudioToggleChannel(0); }, NULL, CheckedIf<PokeyChannelEnabled<0> > },
		{ "Audio.ToggleChannel2", []() { OnCommandAudioToggleChannel(1); }, NULL, CheckedIf<PokeyChannelEnabled<1> > },
		{ "Audio.ToggleChannel3", []() { OnCommandAudioToggleChannel(2); }, NULL, CheckedIf<PokeyChannelEnabled<2> > },
		{ "Audio.ToggleChannel4", []() { OnCommandAudioToggleChannel(3); }, NULL, CheckedIf<PokeyChannelEnabled<3> > },

		{ "Audio.ToggleSlightSid", OnCommandAudioToggleSlightSid, IsNot5200, CheckedIf<IsSlightSidEnabled> },
		{ "Audio.ToggleCovox", OnCommandAudioToggleCovox, IsNot5200, CheckedIf<IsCovoxEnabled> },

		{ "Input.CaptureMouse", OnCommandInputCaptureMouse, IsMouseMapped, CheckedIf<ATUIIsMouseCaptured> },
		{ "Input.ToggleAutoCaptureMouse", OnCommandInputToggleAutoCaptureMouse, IsMouseMapped, CheckedIf<ATUIGetMouseAutoCapture> },
		{ "Input.InputMappingsDialog", OnCommandInputInputMappingsDialog },
		{ "Input.KeyboardDialog", OnCommandInputKeyboardDialog },
		{ "Input.LightPenDialog", OnCommandInputLightPenDialog },
		{ "Input.CycleQuickMaps", OnCommandInputCycleQuickMaps },
		{ "Input.InputSetupDialog", OnCommandInputInputSetupDialog },

		{ "Record.Stop", OnCommandRecordStop, IsRecording },
		{ "Record.RawAudio", OnCommandRecordRawAudio, Not<IsRecording>, CheckedIf<RecordingStatusIs<kATUIRecordingStatus_RawAudio>> },
		{ "Record.Audio", OnCommandRecordAudio, Not<IsRecording>, CheckedIf<RecordingStatusIs<kATUIRecordingStatus_Audio>> },
		{ "Record.Video", OnCommandRecordVideo, Not<IsRecording>, CheckedIf<RecordingStatusIs<kATUIRecordingStatus_Video>> },
		{ "Record.SAPTypeR", OnCommandRecordSapTypeR, Not<IsRecording>, CheckedIf<RecordingStatusIs<kATUIRecordingStatus_Sap>> },

		{ "Cheat.ToggleDisablePMCollisions", OnCommandCheatTogglePMCollisions, NULL, CheckedIf<Not<GTIATest<&ATGTIAEmulator::ArePMCollisionsEnabled> > > },
		{ "Cheat.ToggleDisablePFCollisions", OnCommandCheatTogglePFCollisions, NULL, CheckedIf<Not<GTIATest<&ATGTIAEmulator::ArePFCollisionsEnabled> > > },
		{ "Cheat.CheatDialog", OnCommandCheatCheatDialog },

		{ "Tools.DiskExplorer", OnCommandToolsDiskExplorer },
		{ "Tools.ConvertSAPToEXE", OnCommandToolsConvertSapToExe },
		{ "Tools.OptionsDialog", OnCommandToolsOptionsDialog },
		{ "Tools.KeyboardShortcutsDialog", OnCommandToolsKeyboardShortcutsDialog },
		{ "Tools.SetupWizard", OnCommandToolsSetupWizard },

		{ "Window.Close", OnCommandWindowClose, ATUICanManipulateWindows },
		{ "Window.Undock", OnCommandWindowUndock, ATUICanManipulateWindows },

		{ "Help.Contents", OnCommandHelpContents },
		{ "Help.About", OnCommandHelpAbout },
		{ "Help.ChangeLog", OnCommandHelpChangeLog },
	};
}

void ATUIInitCommandMappings(ATUICommandManager& cmdMgr) {
	using namespace ATCommands;

	cmdMgr.RegisterCommands(kATCommands, sizeof(kATCommands)/sizeof(kATCommands[0]));
}