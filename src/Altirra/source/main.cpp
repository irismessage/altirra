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
#include <windows.h>
#include <tchar.h>
#include <mmsystem.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <winsock2.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/filesys.h>
#include <vd2/system/fraction.h>
#include <vd2/system/math.h>
#include <vd2/system/time.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/error.h>
#include <vd2/system/registry.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/cmdline.h>
#include <vd2/system/thunk.h>
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/direct3d.h>
#include <vd2/Dita/accel.h>
#include <vd2/Dita/services.h>
#include <at/atappbase/crthooks.h>
#include <at/atappbase/exceptionfilter.h>
#include <at/atcore/media.h>
#include <at/atcore/profile.h>
#include <at/atdevices/devices.h>
#include <at/atui/constants.h>
#include <at/atnativeui/acceleditdialog.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/hotkeyexcontrol.h>
#include <at/atnativeui/messageloop.h>
#include <at/atnativeui/progress.h>
#include <at/atnativeui/uiframe.h>
#include <at/atcore/device.h>
#include <at/atcore/propertyset.h>
#include <at/atdebugger/target.h>
#include "console.h"
#include "simulator.h"
#include "cassette.h"
#include "cassetteimage.h"
#include "debugger.h"
#include "debuggerlog.h"
#include "hostdevice.h"
#include "savestate.h"
#include "resource.h"
#include "oshelper.h"
#include "autotest.h"
#include "audiowriter.h"
#include "sapwriter.h"
#include "inputmanager.h"
#include "inputcontroller.h"
#include "cartridge.h"
#include "version.h"
#include "videowriter.h"
#include "ide.h"
#include "ultimate1mb.h"
#include "options.h"
#include "cheatengine.h"
#include "joystick.h"
#include "uienhancedtext.h"
#include "uikeyboard.h"
#include "uicaptionupdater.h"
#include "uiportmenus.h"
#include "uimenu.h"
#include "uimrulist.h"
#include "uiprogress.h"
#include "uirender.h"
#include "uiinstance.h"
#include "uidisplay.h"
#include "uitypes.h"
#include <at/atui/uicommandmanager.h>
#include "uiprofiler.h"
#include "uiqueue.h"
#include "uicommondialogs.h"
#include "uiaccessors.h"
#include "uifilefilters.h"
#include "uiclipboard.h"
#include "sapconverter.h"
#include "cmdhelpers.h"
#include "settings.h"

#include "firmwaremanager.h"
#include <at/atcore/devicemanager.h>

#pragma comment(lib, "comctl32")
#pragma comment(lib, "shlwapi")

void ATUITriggerButtonDown(uint32 vk);
void ATUITriggerButtonUp(uint32 vk);

///////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogInputMappings(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy);
void ATUIShowDialogInputSetup(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy);
void ATUIShowDiskDriveDialog(VDGUIHandle hParent);
void ATUIShowTapeControlDialog(VDGUIHandle hParent, ATCassetteEmulator& cassette);
int ATUIShowDialogCartridgeMapper(VDGUIHandle h, uint32 cartSize, const void *data);
void ATUIShowDialogLightPen(VDGUIHandle h, ATLightPenPort *lpp);
void ATUIShowDialogCheater(VDGUIHandle hParent, ATCheatEngine *engine);
void ATUIShowDialogDiskExplorer(VDGUIHandle h);
void ATUIShowDialogOptions(VDGUIHandle h);
void ATUIShowDialogAbout(VDGUIHandle h);
bool ATUIShowDialogKeyboardOptions(VDGUIHandle hParent, ATUIKeyboardOptions& opts);
void ATUIShowDialogSetFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly);
void ATUIShowDialogRemoveFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly);
bool ATUIShowDialogVideoEncoding(VDGUIHandle parent, bool hz50, ATVideoEncoding& encoding, ATVideoRecordingFrameRate& frameRate, bool& halfRate, bool& encodeAll);
void ATUIShowDialogSetupWizard(VDGUIHandle hParent);

void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);

void ATRegisterDevices(ATDeviceManager& dm);

void ATInitEmuErrorHandler(VDGUIHandle h, ATSimulator *sim);
void ATShutdownEmuErrorHandler();

bool ATIDEIsPhysicalDiskPath(const wchar_t *path);

void ATUIInitManager();
void ATUIShutdownManager();
void ATUIFlushDisplay();

void ATUIInitFirmwareMenuCallbacks(ATFirmwareManager *fwmgr);
void ATUIInitProfileMenuCallbacks();

///////////////////////////////////////////////////////////////////////////////

void ATInitDebugger();
void ATShutdownDebugger();
void ATInitProfilerUI();
void ATInitUIPanes();
void ATShutdownUIPanes();
void ATShowChangeLog(VDGUIHandle hParent);
void ATUIInitCommandMappings(ATUICommandManager& cmdMgr);

void DoLoad(VDGUIHandle h, const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper, ATLoadType loadType = kATLoadType_Other, bool *suppressColdReset = NULL, int loadIndex = -1);
void DoBootWithConfirm(const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper);

void LoadBaselineSettings();

void ATUICreateMainWindow(ATContainerWindow **);

///////////////////////////////////////////////////////////////////////////////

extern const wchar_t g_wszWarning[]=L"Altirra Warning";

HINSTANCE g_hInst;
HWND g_hwnd;
ATContainerWindow *g_pMainWindow;
IVDVideoDisplay *g_pDisplay;
HMENU g_hMenu;

vdfunction<bool(bool)> g_pATIdle;

VDCommandLine g_ATCmdLine;
bool g_ATCmdLineRead;
bool g_ATCmdLineHadAnything;
bool g_ATRegistryHadAnything;

ATSimulator g_sim;

bool g_ATAutoFrameFlipping = false;
bool g_fullscreen = false;
WINDOWPLACEMENT g_ATWindowPreFSPlacement;
bool g_mouseCaptured = false;
bool g_mouseClipped = false;
bool g_mouseAutoCapture = true;
bool g_pauseInactive = true;
bool g_winActive = true;
bool g_showFps = false;
bool g_xepViewEnabled = false;
bool g_xepViewAutoswitchingEnabled = false;

ATUIKeyboardOptions g_kbdOpts = { true, false };

ATDisplayFilterMode g_dispFilterMode = kATDisplayFilterMode_SharpBilinear;
int g_dispFilterSharpness = +1;
int g_enhancedText = 0;
LOGFONTW g_enhancedTextFont;

ATSaveStateWriter::Storage g_quickSave;

vdautoptr<ATAudioWriter> g_pAudioWriter;
vdautoptr<IATVideoWriter> g_pVideoWriter;
vdautoptr<IATSAPWriter> g_pSapWriter;

ATDisplayStretchMode g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;

ATUIWindowCaptionUpdater g_winCaptionUpdater;

ATUICommandManager g_ATUICommandMgr;

///////////////////////////////////////////////////////////////////////////////

VDGUIHandle ATUIGetMainWindow() {
	return (VDGUIHandle)g_hwnd;
}

bool ATUIGetAppActive() {
	return g_winActive;
}

void ATUISetAppActive(bool active) {
	g_winActive = active;
}

///////////////////////////////////////////////////////////////////////////////

ATFrameRateMode g_frameRateMode = kATFrameRateMode_Hardware;
float	g_speedModifier;
uint8	g_speedFlags;
sint64	g_frameTicks;
uint32	g_frameSubTicks;
sint64	g_frameErrorBound;
sint64	g_frameTimeout;

void ATUIUpdateSpeedTiming() {
	// NTSC: 1.7897725MHz master clock, 262 scanlines of 114 clocks each
	// PAL: 1.773447MHz master clock, 312 scanlines of 114 clocks each
	static const double kPeriods[3][2]={
		{ 1.0 / 59.9227, 1.0 / 49.8607 },
		{ 1.0 / 59.9400, 1.0 / 50.0000 },
		{ 1.0 / 60.0000, 1.0 / 50.0000 },
	};

	const bool hz50 = (g_sim.GetVideoStandard() != kATVideoStandard_NTSC) && (g_sim.GetVideoStandard() != kATVideoStandard_PAL60);
	double rawSecondsPerFrame = kPeriods[g_frameRateMode][hz50];
	
	double cyclesPerSecond;
	if (hz50)
		cyclesPerSecond = 1773447.0 * kPeriods[0][1] / rawSecondsPerFrame;
	else							  
		cyclesPerSecond = 1789772.5 * kPeriods[0][0] / rawSecondsPerFrame;

	double rate = 1.0;
	
	if (!g_sim.IsTurboModeEnabled()) {
		rate = g_speedModifier + 1.0;
		if (g_speedFlags & (kATUISpeedFlags_Slow | kATUISpeedFlags_SlowPulse))
			rate *= 0.5;
	}

	g_sim.GetAudioOutput()->SetCyclesPerSecond(cyclesPerSecond, 1.0 / rate);
	double secondsPerFrame = rawSecondsPerFrame / rate;

	double secondTime = VDGetPreciseTicksPerSecond();
	double frameTimeF = secondTime * secondsPerFrame;

	g_frameTicks = VDFloorToInt64(frameTimeF);
	g_frameSubTicks = VDRoundToInt32((frameTimeF - g_frameTicks) * 65536.0);
	g_frameErrorBound = std::max<sint64>(2 * g_frameTicks, VDRoundToInt64(secondTime * 0.1f));
	g_frameTimeout = std::max<sint64>(5 * g_frameTicks, VDGetPreciseTicksPerSecondI());
}

ATFrameRateMode ATUIGetFrameRateMode() {
	return g_frameRateMode;
}

void ATUISetFrameRateMode(ATFrameRateMode mode) {
	if (g_frameRateMode != mode) {
		g_frameRateMode = mode;

		ATUIUpdateSpeedTiming();
	}
}

float ATUIGetSpeedModifier() {
	return g_speedModifier;
}

void ATUISetSpeedModifier(float modifier) {
	if (g_speedModifier != modifier) {
		g_speedModifier = modifier;

		ATUIUpdateSpeedTiming();
	}
}

void ATUIChangeSpeedFlags(uint8 mask, uint8 value) {
	uint8 delta = (g_speedFlags ^ value) & mask;

	if (!delta)
		return;

	uint8 flags = (g_speedFlags ^ delta);
	g_speedFlags = flags;

	g_sim.SetTurboModeEnabled((flags & (kATUISpeedFlags_Turbo | kATUISpeedFlags_TurboPulse)) != 0);

	ATUIUpdateSpeedTiming();
}

bool ATUIIsMouseCaptured() {
	return g_mouseCaptured || g_mouseClipped;
}

bool ATUIGetMouseAutoCapture() {
	return g_mouseAutoCapture;
}

void ATUISetMouseAutoCapture(bool enabled) {
	g_mouseAutoCapture = enabled;
}

bool ATUIGetTurbo() {
	return (g_speedFlags & kATUISpeedFlags_Turbo) != 0;
}

void ATUISetTurbo(bool turbo) {
	ATUIChangeSpeedFlags(kATUISpeedFlags_Turbo, turbo ? kATUISpeedFlags_Turbo : 0);
}

bool ATUIGetTurboPulse() {
	return (g_speedFlags & kATUISpeedFlags_TurboPulse) != 0;
}

void ATUISetTurboPulse(bool turbo) {
	ATUIChangeSpeedFlags(kATUISpeedFlags_TurboPulse, turbo ? kATUISpeedFlags_TurboPulse : 0);
}

bool ATUIGetSlowMotion() {
	return (g_speedFlags & kATUISpeedFlags_Slow) != 0;
}

void ATUISetSlowMotion(bool slowmo) {
	ATUIChangeSpeedFlags(kATUISpeedFlags_Slow, slowmo ? kATUISpeedFlags_Slow : 0);
}

bool ATUIGetPauseWhenInactive() {
	return g_pauseInactive;
}

void ATUISetPauseWhenInactive(bool enabled) {
	g_pauseInactive = enabled;
}

ATUIRecordingStatus ATUIGetRecordingStatus() {
	if (g_pVideoWriter)
		return kATUIRecordingStatus_Video;

	if (g_pAudioWriter)
		return g_pAudioWriter->IsRecordingRaw() ? kATUIRecordingStatus_RawAudio : kATUIRecordingStatus_Audio;

	if (g_pSapWriter)
		return kATUIRecordingStatus_Sap;

	return kATUIRecordingStatus_None;
}

///////////////////////////////////////////////////////////////////////////////
// This is a bad hack which is needed for now since the CPU history state
// is the master state.
void ATSyncCPUHistoryState() {
	const bool historyEnabled = g_sim.GetCPU().IsHistoryEnabled();

	for(IATDeviceDebugTarget *devtarget : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceDebugTarget>(false, false)) {
		uint32 index = 0;

		while(IATDebugTarget *target = devtarget->GetDebugTarget(index++)) {
			auto *thist = vdpoly_cast<IATDebugTargetHistory *>(target);

			if (thist)
				thist->SetHistoryEnabled(historyEnabled);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////

class ATInputConsoleCallback : public IATInputConsoleCallback {
public:
	virtual void SetConsoleTrigger(uint32 id, bool state);
};

void ATInputConsoleCallback::SetConsoleTrigger(uint32 id, bool state) {
	switch(id) {
		case kATInputTrigger_Start:
			g_sim.GetGTIA().SetConsoleSwitch(0x01, state);
			break;
		case kATInputTrigger_Select:
			g_sim.GetGTIA().SetConsoleSwitch(0x02, state);
			break;
		case kATInputTrigger_Option:
			g_sim.GetGTIA().SetConsoleSwitch(0x04, state);
			break;
		case kATInputTrigger_ColdReset:
			if (state)
				g_ATUICommandMgr.ExecuteCommand("System.ColdReset");
			break;
		case kATInputTrigger_WarmReset:
			if (state)
				g_ATUICommandMgr.ExecuteCommand("System.WarmReset");
			break;
		case kATInputTrigger_Turbo:
			g_ATUICommandMgr.ExecuteCommand(state ? "System.PulseWarpOn" : "System.PulseWarpOff");
			break;
		case kATInputTrigger_KeySpace:
			if (g_kbdOpts.mbRawKeys) {
				if (state)
					g_sim.GetPokey().PushRawKey(0x21, !g_kbdOpts.mbFullRawKeys);
				else
					g_sim.GetPokey().ReleaseRawKey(0x21, !g_kbdOpts.mbFullRawKeys);
			} else if (state)
				g_sim.GetPokey().PushKey(0x21, false);
			break;

		case kATInputTrigger_UILeft:
		case kATInputTrigger_UIRight:
		case kATInputTrigger_UIUp:
		case kATInputTrigger_UIDown:
		case kATInputTrigger_UIAccept:
		case kATInputTrigger_UIReject:
		case kATInputTrigger_UIMenu:
		case kATInputTrigger_UIOption:
		case kATInputTrigger_UISwitchLeft:
		case kATInputTrigger_UISwitchRight:
		case kATInputTrigger_UILeftShift:
		case kATInputTrigger_UIRightShift:
			ATUISetNativeDialogMode(false);

			if (state)
				ATUITriggerButtonDown(id);
			else
				ATUITriggerButtonUp(id);
			break;
	}
}

ATInputConsoleCallback g_inputConsoleCallback;

///////////////////////////////////////////////////////////////////////////////

bool ATUIConfirmDiscardCartridge(VDGUIHandle h) {
	if (!g_sim.IsStorageDirty(kATStorageId_Cartridge))
		return true;

	return IDYES == MessageBoxW((HWND)h, L"Modified cartridge image has not been saved. Discard it anyway?", L"Altirra Warning", MB_ICONEXCLAMATION | MB_YESNO);
}

VDStringW ATUIConfirmDiscardAllStorageGetMessage(const wchar_t *prompt, bool includeUnmountables, ATStorageId storageType = kATStorageId_None) {
	typedef vdfastvector<ATStorageId> DirtyIds;
	DirtyIds dirtyIds;

	typedef vdfastvector<ATDebuggerStorageId> DbgDirtyIds;
	DbgDirtyIds dbgDirtyIds;

	g_sim.GetDirtyStorage(dirtyIds, storageType);

	if (includeUnmountables) {
		IATDebugger *dbg = ATGetDebugger();
		if (dbg)
			dbg->GetDirtyStorage(dbgDirtyIds);
	}

	if (dirtyIds.empty() && dbgDirtyIds.empty())
		return VDStringW();

	std::sort(dirtyIds.begin(), dirtyIds.end());
	std::sort(dbgDirtyIds.begin(), dbgDirtyIds.end());

	VDStringW msg;

	msg = L"The following modified items have not been saved:\n\n";
	
	for(DirtyIds::const_iterator it(dirtyIds.begin()), itEnd(dirtyIds.end()); it != itEnd; ++it) {
		ATStorageId id = *it;

		const uint32 type = id & kATStorageId_TypeMask;
		const uint32 unit = id & kATStorageId_UnitMask;

		switch(type) {
			case kATStorageId_Cartridge:
				msg += L"\tCartridge";

				if (unit)
					msg.append_sprintf(L" %u", unit + 1);
				break;

			case kATStorageId_Disk:
				msg.append_sprintf(L"\tDisk (D%u:)", unit + 1);
				break;

			case kATStorageId_Firmware:
				switch(unit) {
					case 0:
						msg += L"IDE main firmware";
						break;

					case 1:
						msg += L"IDE SDX firmware";
						break;

					case 2:
						msg += L"Ultimate1MB firmware";
						break;
				}
				break;
		}

		msg += '\n';
	}

	for(DbgDirtyIds::const_iterator it(dbgDirtyIds.begin()), itEnd(dbgDirtyIds.end()); it != itEnd; ++it) {
		ATDebuggerStorageId id = *it;

		switch(id) {
			case kATDebuggerStorageId_CustomSymbols:
				msg += L"\tDebugger: Custom Symbols\n";
				break;
		}
	}

	msg += L'\n';
	msg += prompt;

	return msg;
}

bool ATUIConfirmDiscardAllStorage(VDGUIHandle h, const wchar_t *prompt, bool includeUnmountables = false) {
	const VDStringW& msg = ATUIConfirmDiscardAllStorageGetMessage(prompt, includeUnmountables);

	if (msg.empty())
		return true;

	return IDYES == MessageBoxW((HWND)h, msg.c_str(), L"Altirra Warning", MB_YESNO | MB_ICONEXCLAMATION);
}

vdrefptr<ATUIFutureWithResult<bool > > ATUIConfirmDiscardAllStorage(const wchar_t *prompt, bool includeUnmountables, ATStorageId storageType = kATStorageId_None) {
	const VDStringW& msg = ATUIConfirmDiscardAllStorageGetMessage(prompt, includeUnmountables, storageType);

	if (msg.empty())
		return vdrefptr<ATUIFutureWithResult<bool> >(new ATUIFutureWithResult<bool>(true));

	return ATUIShowAlert(msg.c_str(), L"Altirra Warning");
}

bool ATUISwitchHardwareMode(VDGUIHandle h, ATHardwareMode mode, bool switchProfiles) {
	ATHardwareMode prevMode = g_sim.GetHardwareMode();
	if (prevMode == mode)
		return true;

	ATDefaultProfile defaultProfile;

	switch(mode) {
		case kATHardwareMode_800:
			defaultProfile = kATDefaultProfile_800;
			break;

		case kATHardwareMode_800XL:
		case kATHardwareMode_130XE:
		default:
			defaultProfile = kATDefaultProfile_XL;
			break;

		case kATHardwareMode_5200:
			defaultProfile = kATDefaultProfile_5200;
			break;

		case kATHardwareMode_XEGS:
			defaultProfile = kATDefaultProfile_XEGS;
			break;

		case kATHardwareMode_1200XL:
			defaultProfile = kATDefaultProfile_1200XL;
			break;
	}

	const uint32 oldProfileId = ATSettingsGetCurrentProfileId();
	const uint32 newProfileId = ATGetDefaultProfileId(defaultProfile);
	const bool switchingProfile = newProfileId != kATProfileId_Invalid && newProfileId != oldProfileId;

	// check if we are switching to or from 5200 mode
	const bool switching5200 = (mode == kATHardwareMode_5200 || prevMode == kATHardwareMode_5200);
	if (switching5200 || switchingProfile) {
		// check if it's OK to unload everything
		if (h && !ATUIConfirmDiscardAllStorage(h, L"OK to switch hardware mode and discard everything?"))
			return false;
	}

	// switch profile if necessary
	if (switchingProfile) {
		ATSettingsSwitchProfile(newProfileId);
	}

	if (switching5200) {
		g_sim.UnloadAll();

		// 5200 mode needs the default cart and 16K memory
		if (mode == kATHardwareMode_5200) {
			g_sim.LoadCartridge5200Default();
			g_sim.SetMemoryMode(kATMemoryMode_16K);
		}
	}

	g_sim.SetHardwareMode(mode);

	// Check for incompatible kernel.
	switch(g_sim.GetKernelMode()) {
		case kATKernelMode_Default:
			break;

		case kATKernelMode_XL:
			if (mode != kATHardwareMode_800XL && mode != kATHardwareMode_1200XL && mode != kATHardwareMode_130XE && mode != kATHardwareMode_XEGS)
				g_sim.SetKernel(0);
			break;

		case kATKernelMode_5200:
			if (mode != kATHardwareMode_5200)
				g_sim.SetKernel(0);
			break;

		default:
			if (mode == kATHardwareMode_5200)
				g_sim.SetKernel(0);
			break;
	}

	// If we are in 5200 mode, we must be in NTSC
	if (mode == kATHardwareMode_5200 && g_sim.GetVideoStandard() != kATVideoStandard_NTSC)
	{
		g_sim.SetVideoStandard(kATVideoStandard_NTSC);
		ATUIUpdateSpeedTiming();
	}

	g_sim.ColdReset();
	return true;
}

bool ATUISwitchHardwareModeComputer(VDGUIHandle h) {
	if (g_sim.GetHardwareMode() != kATHardwareMode_5200)
		return true;

	return ATUISwitchHardwareMode(h, kATHardwareMode_800XL, true);
}

bool ATUISwitchHardwareMode5200(VDGUIHandle h) {
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return true;

	return ATUISwitchHardwareMode(h, kATHardwareMode_5200, true);
}

bool ATUISwitchKernel(VDGUIHandle h, uint64 kernelId) {
	if (g_sim.GetKernelId() == kernelId)
		return true;

	// If the kernel mode is incompatible, check if we can switch the computer
	// mode.
	ATFirmwareManager& fwm = *g_sim.GetFirmwareManager();

	if (kernelId) {
		ATFirmwareInfo fwinfo;
		if (!fwm.GetFirmwareInfo(kernelId, fwinfo))
			return false;

		const auto hwmode = g_sim.GetHardwareMode();
		switch(fwinfo.mType) {
			case kATFirmwareType_Kernel1200XL:
				if (hwmode != kATHardwareMode_800XL
					&& hwmode != kATHardwareMode_130XE
					&& hwmode != kATHardwareMode_1200XL
					&& hwmode != kATHardwareMode_XEGS
					)
				{
					if (!ATUISwitchHardwareMode(h, kATHardwareMode_1200XL, true))
						return false;
				}
				break;

			case kATFirmwareType_KernelXL:
				if (hwmode != kATHardwareMode_800XL
					&& hwmode != kATHardwareMode_130XE
					&& hwmode != kATHardwareMode_1200XL
					&& hwmode != kATHardwareMode_XEGS
					)
				{
					if (!ATUISwitchHardwareMode(h, kATHardwareMode_800XL, true))
						return false;
				}
				break;

			case kATFirmwareType_KernelXEGS:
				if (hwmode != kATHardwareMode_800XL
					&& hwmode != kATHardwareMode_130XE
					&& hwmode != kATHardwareMode_1200XL
					&& hwmode != kATHardwareMode_XEGS
					)
				{
					if (!ATUISwitchHardwareMode(h, kATHardwareMode_XEGS, true))
						return false;
				}
				break;

			case kATFirmwareType_Kernel800_OSA:
			case kATFirmwareType_Kernel800_OSB:
				if (hwmode != kATHardwareMode_800
					&& hwmode != kATHardwareMode_800XL
					&& hwmode != kATHardwareMode_130XE
					&& hwmode != kATHardwareMode_1200XL
					&& hwmode != kATHardwareMode_XEGS
					)
				{
					if (!ATUISwitchHardwareMode(h, kATHardwareMode_800, true))
						return false;
				}
				break;

			case kATFirmwareType_Kernel5200:
				if (!ATUISwitchHardwareMode5200(h))
					return false;
				break;
		}

		// Check if we need to adjust the memory size. XL and Other kernels can't
		// run with 48K or 56K. 16K is OK (600XL configuration). We don't need to
		// check 5200 here as it was already changed in the hardware mode switch.
		switch(fwinfo.mType) {
			case kATFirmwareType_KernelXL:
			case kATFirmwareType_Kernel1200XL:
				switch(g_sim.GetMemoryMode()) {
					case kATMemoryMode_8K:
					case kATMemoryMode_24K:
					case kATMemoryMode_32K:
					case kATMemoryMode_40K:
					case kATMemoryMode_48K:
					case kATMemoryMode_52K:
						g_sim.SetMemoryMode(kATMemoryMode_64K);
						break;
				}
				break;
		}
	}

	g_sim.SetKernel(kernelId);
	g_sim.ColdReset();
	return true;
}

void ATUISwitchKernel(uint64 id) {
	ATUISwitchKernel((VDGUIHandle)g_hwnd, id);
}

void ATUISwitchBasic(uint64 basicId) {
	if (g_sim.GetBasicId() == basicId)
		return;

	g_sim.SetBasic(basicId);
	g_sim.ColdReset();
	return;
}

void ATUISwitchMemoryMode(VDGUIHandle h, ATMemoryMode mode) {
	if (g_sim.GetMemoryMode() == mode)
		return;

	switch(g_sim.GetHardwareMode()) {
		case kATHardwareMode_5200:
			if (mode != kATMemoryMode_16K)
				return;
			break;

		case kATHardwareMode_1200XL:
		case kATHardwareMode_XEGS:
			// don't allow 16K with the 1200XL or XEGS
			if (mode == kATMemoryMode_16K)
				return;
			// fall through
		case kATHardwareMode_800XL:
			if (mode == kATMemoryMode_48K ||
				mode == kATMemoryMode_52K ||
				mode == kATMemoryMode_8K ||
				mode == kATMemoryMode_24K ||
				mode == kATMemoryMode_32K ||
				mode == kATMemoryMode_40K)
				return;
			break;

		case kATHardwareMode_130XE:
			if (mode == kATMemoryMode_48K ||
				mode == kATMemoryMode_52K ||
				mode == kATMemoryMode_8K ||
				mode == kATMemoryMode_16K ||
				mode == kATMemoryMode_24K ||
				mode == kATMemoryMode_32K ||
				mode == kATMemoryMode_40K)
				return;
			break;
	}

	g_sim.SetMemoryMode(mode);
	g_sim.ColdReset();
}

void DoLoadStream(VDGUIHandle h, const wchar_t *origPath, const wchar_t *imageName, IVDRandomAccessStream *stream, const ATMediaWriteMode *writeMode, int cartmapper, ATLoadType loadType, bool *suppressColdReset, int loadIndex) {
	vdfastvector<uint8> captureBuffer;

	ATCartLoadContext cartctx = {};
	cartctx.mbReturnOnUnknownMapper = true;

	if (cartmapper) {
		cartctx.mbReturnOnUnknownMapper = false;
		cartctx.mCartMapper = cartmapper;
	} else
		cartctx.mpCaptureBuffer = &captureBuffer;

	ATStateLoadContext statectx = {};

	ATLoadContext ctx;
	ctx.mLoadType = loadType;
	ctx.mLoadIndex = loadIndex;
	ctx.mpCartLoadContext = &cartctx;
	ctx.mpStateLoadContext = &statectx;
	ctx.mbReturnOnModeIncompatibility = true;

	const ATMediaWriteMode defaultWriteMode = g_ATOptions.mDefaultWriteMode;

	if (!writeMode)
		writeMode = &defaultWriteMode;

	int safetyCounter = 10;
	for(;;) {
		cartctx.mHardwareModeCheck = g_sim.GetHardwareMode();

		if (stream) {
			if (g_sim.Load(origPath, imageName, *stream, *writeMode, &ctx))
				break;
		} else {
			if (g_sim.Load(origPath, *writeMode, &ctx))
				break;
		}

		if (!--safetyCounter)
			return;

		if (ctx.mbMode5200Required) {
			if (!ATUISwitchHardwareMode5200(h))
				return;
		} else if (ctx.mbModeComputerRequired) {
			if (!ATUISwitchHardwareModeComputer(h))
				return;

			continue;
		}

		if (ctx.mLoadType == kATLoadType_Cartridge) {
			if (cartctx.mLoadStatus == kATCartLoadStatus_HardwareMismatch) {
				if (g_sim.GetHardwareMode() == kATHardwareMode_5200) {
					if (!ATUISwitchHardwareModeComputer(h))
						return;
				} else {
					if (!ATUISwitchHardwareMode5200(h))
						return;
				}
				continue;
			}

			int mapper = ATUIShowDialogCartridgeMapper(h, cartctx.mCartSize, captureBuffer.data());
			if (mapper < 0)
				return;

			cartctx.mbReturnOnUnknownMapper = false;
			cartctx.mCartMapper = mapper;
		} else if (ctx.mLoadType == kATLoadType_SaveState) {
			if (statectx.mbKernelMismatchDetected) {
				if (IDOK != MessageBoxW((HWND)h,
					L"The currently loaded kernel ROM image doesn't match the one referenced by the saved state. This may cause the simulated program to fail when resumed. Proceed anyway?",
					L"Altirra Warning", MB_ICONWARNING | MB_OKCANCEL))
					return;

				statectx.mbAllowKernelMismatch = true;
			}
		}
	}

	if (ctx.mLoadType == kATLoadType_SaveState) {
		if (!statectx.mbPrivateStateLoaded) {
			MessageBoxW((HWND)h, L"The save state loaded successfully, but detailed emulation state could not be loaded as it was produced by a different program version. Some glitches may appear in the simulation.", L"Altirra Warning", MB_ICONWARNING | MB_OK);
		}

		if (suppressColdReset)
			*suppressColdReset = true;
	}
}

void DoLoadStream(VDGUIHandle h, const wchar_t *origPath, const wchar_t *imageName, IVDRandomAccessStream& stream, const ATMediaWriteMode *writeMode, int cartmapper, ATLoadType loadType, bool *suppressColdReset, int loadIndex) {
	DoLoadStream(h, origPath, imageName, &stream, writeMode, cartmapper, loadType, suppressColdReset, loadIndex);
}

void DoLoad(VDGUIHandle h, const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper, ATLoadType loadType, bool *suppressColdReset, int loadIndex) {
	DoLoadStream(h, path, path, nullptr, writeMode, cartmapper, loadType, suppressColdReset, loadIndex);
}

void DoBootWithConfirm(const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper) {
	if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?"))
		return;

	try {
		g_sim.UnloadAll();

		DoLoad((VDGUIHandle)g_hwnd, path, writeMode, cartmapper);

		g_sim.ColdReset();

		ATAddMRUListItem(path);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}
}

void DoBootStreamWithConfirm(const wchar_t *origPath, const wchar_t *imageName, IVDRandomAccessStream& stream, const ATMediaWriteMode *writeMode, int cartmapper) {
	if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?"))
		return;

	try {
		g_sim.UnloadAll();

		DoLoadStream((VDGUIHandle)g_hwnd, origPath, imageName, stream, writeMode, cartmapper, kATLoadType_Other, NULL, -1);

		g_sim.ColdReset();
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}
}

class ATUIFutureOpenBootImage : public ATUIFuture {
public:
	ATUIFutureOpenBootImage(bool coldBoot)
		: mbColdBoot(coldBoot)
	{
	}

	virtual void RunInner() {
		switch(mStage) {
			case 0:
				if (mbColdBoot) {
					mpConfirmResult = ATUIConfirmDiscardAllStorage(L"OK to discard?", false);
					Wait(mpConfirmResult);
				}
				++mStage;
				break;

			case 1:
				if (mpConfirmResult && !mpConfirmResult->GetResult()) {
					MarkCompleted();
					break;
				}

				mpConfirmResult.clear();
				mpFileDialogResult = ATUIShowOpenFileDialog('load', L"Load disk, cassette, cartridge, or program image",
					L"All supported types\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.xex;*.obx;*.com;*.car;*.rom;*.a52;*.bin;*.cas;*.wav;*.zip;*.atz;*.gz;*.bas;*.arc;*.sap\0"
					L"Atari program (*.xex,*.obx,*.com)\0*.xex;*.obx;*.com\0"
					L"BASIC program (*.bas)\0*.bas\0"
					L"Atari disk image (*.atr,*.xfd,*.dcm)\0*.atr;*.xfd;*.dcm;*.arc\0"
					L"Protected disk image (*.pro)\0*.pro\0"
					L"VAPI disk image (*.atx)\0*.atx\0"
					L"Cartridge (*.rom,*.bin,*.a52,*.car)\0*.rom;*.bin;*.a52;*.car\0"
					L"Cassette tape (*.cas,*.wav)\0*.cas;*.wav\0"
					L"Zip archive (*.zip)\0*.zip\0"
					L"gzip archive (*.gz;*.atz)\0*.gz;*.atz\0"
					L".ARC archive (*.arc)\0*.arc\0"
					L"SAP file (*.sap)\0*.sap\0"
					L"All files\0*.*\0");

				Wait(mpFileDialogResult);
				++mStage;
				break;

			case 2:
				if (mpFileDialogResult->mbAccepted) {
					if (mbColdBoot)
						g_sim.UnloadAll();

					DoLoad((VDGUIHandle)g_hwnd, mpFileDialogResult->mPath.c_str(), nullptr, 0);

					if (mbColdBoot)
						g_sim.ColdReset();

					ATAddMRUListItem(mpFileDialogResult->mPath.c_str());
				}
				mpFileDialogResult.clear();

				MarkCompleted();
				break;
		}
	}

	const bool mbColdBoot;
	vdrefptr<ATUIFileDialogResult> mpFileDialogResult;
	vdrefptr<ATUIFutureWithResult<bool> > mpConfirmResult;
};

void OnCommandOpen(bool forceColdBoot) {
	vdrefptr<ATUIFutureOpenBootImage> stage(new ATUIFutureOpenBootImage(forceColdBoot));

	ATUIPushStep(stage->GetStep());
}

bool ATUIGetFullscreen() { 
	return g_fullscreen;
}

void ATSetFullscreen(bool fs) {
	ATUIPane *dispPane = ATGetUIPane(kATUIPaneId_Display);
	ATFrameWindow *frame = NULL;

	if (dispPane) {
		HWND parent = ::GetParent(dispPane->GetHandleW32());

		if (parent) {
			frame = ATFrameWindow::GetFrameWindow(parent);
		}
	}

	if (!frame || !frame->GetPane())
		fs = false;

	if (fs == g_fullscreen)
		return;

	ATUISetNativeDialogMode(!fs);

	if (frame)
		frame->SetFullScreen(fs);

	DWORD style = GetWindowLong(g_hwnd, GWL_STYLE);
	if (fs) {
		memset(&g_ATWindowPreFSPlacement, 0, sizeof g_ATWindowPreFSPlacement);
		g_ATWindowPreFSPlacement.length = sizeof(g_ATWindowPreFSPlacement);
		GetWindowPlacement(g_hwnd, &g_ATWindowPreFSPlacement);

		switch(g_ATWindowPreFSPlacement.showCmd) {
			case SW_SHOWMINIMIZED:
				g_ATWindowPreFSPlacement.showCmd = SW_SHOWMINNOACTIVE;
				break;

			case SW_SHOW:
				g_ATWindowPreFSPlacement.showCmd = SW_SHOWNA;
				break;

			case SW_SHOWNORMAL:
				g_ATWindowPreFSPlacement.showCmd = SW_SHOWNOACTIVATE;
				break;
		}
		g_ATWindowPreFSPlacement.flags = 0;

		ShowWindow(g_hwnd, SW_RESTORE);

		SetMenu(g_hwnd, NULL);
		SetWindowLong(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
		SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
		SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
		BringWindowToTop(g_hwnd);
		g_fullscreen = true;
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(true, g_ATOptions.mFullScreenWidth, g_ATOptions.mFullScreenHeight, g_ATOptions.mFullScreenRefreshRate);
		g_sim.SetFrameSkipEnabled(true);
	} else {
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(false);

		SetWindowLong(g_hwnd, GWL_STYLE, (style | WS_OVERLAPPEDWINDOW) & ~WS_POPUP);
		SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE);

		// We _should_ be able to just use SetWindowPlacement() here. However, there's a weird
		// problem on Windows 7 where the window manager picks up the restored size as the maximized
		// size when the frame is changed, so we force the maximization manually here.
		bool wasMaxed = false;
		if (g_ATWindowPreFSPlacement.showCmd == SW_MAXIMIZE) {
			wasMaxed = true;
			g_ATWindowPreFSPlacement.showCmd = SW_RESTORE;
		}

		SetWindowPlacement(g_hwnd, &g_ATWindowPreFSPlacement);
		if (wasMaxed)
			ShowWindow(g_hwnd, SW_MAXIMIZE);

		g_fullscreen = false;
		SetMenu(g_hwnd, g_hMenu);
		g_sim.SetFrameSkipEnabled(true);
	}

	g_winCaptionUpdater.SetFullScreen(g_fullscreen);
}

bool ATUICanManipulateWindows() {
	return g_pMainWindow && g_pMainWindow->GetActiveFrame() && !g_pMainWindow->GetModalFrame() && !g_pMainWindow->GetFullScreenFrame();
}

void OnCommandAnticVisualizationNext() {
	ATAnticEmulator& antic = g_sim.GetAntic();

	ATAnticEmulator::AnalysisMode mode = (ATAnticEmulator::AnalysisMode)(((int)antic.GetAnalysisMode() + 1) % ATAnticEmulator::kAnalyzeModeCount);
	antic.SetAnalysisMode(mode);

	IATUIRenderer *uir = g_sim.GetUIRenderer();

	if (uir) {
		switch(mode) {
			case ATAnticEmulator::kAnalyzeOff:
				uir->SetStatusMessage(L"DMA analysis disabled");
				break;

			case ATAnticEmulator::kAnalyzeDMATiming:
				uir->SetStatusMessage(L"DMA analysis enabled");
				break;
		}
	}
}

void OnCommandGTIAVisualizationNext() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	ATGTIAEmulator::AnalysisMode mode = (ATGTIAEmulator::AnalysisMode)(((int)gtia.GetAnalysisMode() + 1) % ATGTIAEmulator::kAnalyzeCount);
	gtia.SetAnalysisMode(mode);

	IATUIRenderer *uir = g_sim.GetUIRenderer();

	if (uir) {
		switch(mode) {
			case ATGTIAEmulator::kAnalyzeNone:
				uir->SetStatusMessage(L"Display analysis disabled");
				break;

			case ATGTIAEmulator::kAnalyzeColors:
				uir->SetStatusMessage(L"Color analysis");
				break;

			case ATGTIAEmulator::kAnalyzeLayers:
				uir->SetStatusMessage(L"Layer analysis");
				break;

			case ATGTIAEmulator::kAnalyzeDList:
				uir->SetStatusMessage(L"Display list analysis");
				break;
		}
	}
}

bool ATUIGetXEPViewAutoswitchingEnabled() {
	return g_xepViewAutoswitchingEnabled;
}

void ATUISetXEPViewAutoswitchingEnabled(bool enabled) {
	g_xepViewAutoswitchingEnabled = enabled;
}

void OnCommandVideoToggleXEP80ViewAutoswitching() {
	g_xepViewAutoswitchingEnabled = !g_xepViewAutoswitchingEnabled;
}

void OnCommandVideoEnhancedTextFontDialog() {
	CHOOSEFONTW cf = {sizeof(CHOOSEFONTW)};

	cf.hwndOwner	= g_hwnd;
	cf.hDC			= NULL;
	cf.lpLogFont	= &g_enhancedTextFont;
	cf.iPointSize	= 0;
	cf.Flags		= CF_FIXEDPITCHONLY | CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

	if (ChooseFontW(&cf)) {
		g_enhancedTextFont.lfWidth			= 0;
		g_enhancedTextFont.lfEscapement		= 0;
		g_enhancedTextFont.lfOrientation	= 0;
		g_enhancedTextFont.lfWeight			= 0;
		g_enhancedTextFont.lfItalic			= FALSE;
		g_enhancedTextFont.lfUnderline		= FALSE;
		g_enhancedTextFont.lfStrikeOut		= FALSE;
		g_enhancedTextFont.lfCharSet		= DEFAULT_CHARSET;
		g_enhancedTextFont.lfOutPrecision	= OUT_DEFAULT_PRECIS;
		g_enhancedTextFont.lfClipPrecision	= CLIP_DEFAULT_PRECIS;
		g_enhancedTextFont.lfQuality		= DEFAULT_QUALITY;
		g_enhancedTextFont.lfPitchAndFamily	= FF_DONTCARE | DEFAULT_PITCH;

		VDRegistryAppKey key("Settings");
		key.setString("Enhanced video: Font family", g_enhancedTextFont.lfFaceName);
		key.setInt("Enhanced video: Font size", g_enhancedTextFont.lfHeight);

		IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
		if (pane)
			pane->UpdateTextModeFont();
	}
}

void Paste(const char *s, size_t len, bool useCooldown) {
	if (g_enhancedText == 2) {
		IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
		if (pane)
			pane->Paste(s, len);
	} else {
		char skipLT = 0;
		while(len--) {
			char c = *s++;

			if (c == skipLT) {
				skipLT = 0;
				continue;
			}

			skipLT = 0;

			if (c == '\r' || c == '\n') {
				skipLT = c ^ ('\r' ^ '\n');

				g_sim.GetPokey().PushKey(0x0C, false, true, false, useCooldown);
			} else if (c == '\t')
				g_sim.GetPokey().PushKey(0x2C, false, true, false, useCooldown);
			else {
				uint8 ch;

				if (ATUIGetDefaultScanCodeForCharacter(c, ch))
					g_sim.GetPokey().PushKey(ch, false, true, false, useCooldown);
			}
		}
	}
}

void Paste(const wchar_t *s, size_t len, bool useCooldown) {
	vdfastvector<char> s8;

	while(len--) {
		wchar_t c = *s++;

		// fix annoying characters
		int repeat = 1;

		switch(c) {
			case L'\u200B':	// zero width space
			case L'\u200C':	// zero width non-joiner
			case L'\u200D':	// zero width joiner
			case L'\u200E':	// left to right mark
			case L'\u200F':	// right to left mark
				continue;

			case L'\u2010':	// hyphen
			case L'\u2011':	// non-breaking hyphen
			case L'\u2012':	// figure dash
			case L'\u2013':	// en dash
			case L'\u2014':	// em dash
			case L'\u2015':	// horizontal bar
				c = L'-';
				break;

			case L'\u2018':	// left single quotation mark
			case L'\u2019':	// right single quotation mark
				c = L'\'';
				break;

			case L'\u201C':	// left double quotation mark
			case L'\u201D':	// right double quotation mark
				c = L'"';
				break;

			case L'\u2026':	// ellipsis
				c = L'.';
				repeat = 3;
				break;

			case L'\uFEFF':	// byte order mark
				continue;
		}

		if ((unsigned)c >= 0x100)
			continue;

		while(repeat--)
			s8.push_back((char)c);
	}

	Paste(s8.data(), s8.size(), useCooldown);
}

void ResizeDisplay() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->OnSize();
}

void StopAudioRecording() {
	if (g_pAudioWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(NULL);
		g_pAudioWriter->Finalize();
		g_pAudioWriter = NULL;
	}
}

void StopVideoRecording() {
	if (g_pVideoWriter) {
		g_pVideoWriter->Shutdown();
		g_sim.GetAudioOutput()->SetAudioTap(NULL);
		g_sim.GetGTIA().SetVideoTap(NULL);
		g_pVideoWriter = NULL;
	}
}

void StopSapRecording() {
	if (g_pSapWriter) {
		g_pSapWriter->Shutdown();
		g_pSapWriter = nullptr;
	}
}

void StopRecording() {
	StopAudioRecording();
	StopVideoRecording();
	StopSapRecording();
}

void CheckRecordingExceptions() {
	try {
		if (g_pVideoWriter)
			g_pVideoWriter->CheckExceptions();
	} catch(const MyError& e) {
		MyError("Video recording has stopped with an error: %s", e.gets()).post(g_hwnd, "Altirra Error");

		StopVideoRecording();
	}

	try {
		if (g_pAudioWriter)
			g_pAudioWriter->CheckExceptions();
	} catch(const MyError& e) {
		MyError("Audio recording has stopped with an error: %s", e.gets()).post(g_hwnd, "Altirra Error");

		StopAudioRecording();
	}

	try {
		if (g_pSapWriter)
			g_pSapWriter->CheckExceptions();
	} catch(const MyError& e) {
		MyError("SAP recording has stopped with an error: %s", e.gets()).post(g_hwnd, "Altirra Error");

		StopSapRecording();
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandOpenImage() {
	OnCommandOpen(false);
}

void OnCommandBootImage() {
	OnCommandOpen(true);
}

void OnCommandQuickLoadState() {
	if (!g_quickSave.empty()) {
		ATSaveStateReader reader(g_quickSave.data(), (uint32)g_quickSave.size());

		g_sim.LoadState(reader, NULL);
	}
}

void OnCommandQuickSaveState() {
	g_quickSave.clear();

	ATSaveStateWriter writer(g_quickSave);

	g_sim.SaveState(writer);
}

void OnCommandLoadState() {
	const VDStringW fn(VDGetLoadFileName('save', (VDGUIHandle)g_hwnd, L"Load save state",
		g_ATUIFileFilter_LoadState,
		L"altstate"
		));

	if (!fn.empty()) {
		DoLoad((VDGUIHandle)g_hwnd, fn.c_str(), nullptr, 0, kATLoadType_SaveState);
	}
}

void OnCommandSaveState() {
	const VDStringW fn(VDGetSaveFileName('save', (VDGUIHandle)g_hwnd, L"Save save state",
		g_ATUIFileFilter_SaveState,
		L"altstate"
		));

	if (!fn.empty()) {
		ATSaveStateWriter::Storage storage;

		ATSaveStateWriter writer(storage);
		g_sim.SaveState(writer);

		VDFile f(fn.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);

		f.write(storage.data(), (long)storage.size());
	}
}

void OnCommandSaveFirmware(int idx) {
	auto storageId = (ATStorageId)(kATStorageId_Firmware + idx);

	if (!g_sim.IsStoragePresent(storageId))
		throw MyError("The selected type of firmware is not present.");

	VDStringW fn(VDGetSaveFileName('rom ', (VDGUIHandle)g_hwnd, L"Save firmware image",
		L"Raw firmware image\0*.rom\0",
		L"rom"));

	if (!fn.empty()) {
		g_sim.SaveStorage(storageId, fn.c_str());
	}
}

void OnCommandSaveFirmwareIDEMain() {
	OnCommandSaveFirmware(0);
}

void OnCommandSaveFirmwareIDESDX() {
	OnCommandSaveFirmware(1);
}

void OnCommandSaveFirmwareU1MB() {
	OnCommandSaveFirmware(2);
}

void OnCommandExit() {
	if (g_hwnd)
		::SendMessage(g_hwnd, WM_CLOSE, 0, 0);
}

void OnCommandCassetteLoadNew() {
	g_sim.GetCassette().LoadNew();
}

void OnCommandCassetteLoad() {
	VDStringW fn(VDGetLoadFileName('cass', (VDGUIHandle)g_hwnd, L"Load cassette tape", g_ATUIFileFilter_LoadTape, L"wav"));

	if (!fn.empty()) {
		ATCassetteEmulator& cas = g_sim.GetCassette();
		cas.Load(fn.c_str());
		cas.Play();
	}
}

void OnCommandCassetteUnload() {
	g_sim.GetCassette().Unload();
}

void OnCommandCassetteSave() {
	ATCassetteEmulator& cas = g_sim.GetCassette();
	if (!cas.IsLoaded())
		return;

	VDStringW fn(VDGetSaveFileName('cass', (VDGUIHandle)g_hwnd, L"Save cassette tape", g_ATUIFileFilter_SaveTape, L"cas"));

	if (fn.empty())
		return;

	VDFileStream f(fn.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kSequential | nsVDFile::kCreateAlways);

	ATSaveCassetteImageCAS(f, cas.GetImage());
}

void OnCommandCassetteTapeControlDialog() {
	ATUIShowTapeControlDialog((VDGUIHandle)g_hwnd, g_sim.GetCassette());
}

void OnCommandCassetteToggleSIOPatch() {
	g_sim.SetCassetteSIOPatchEnabled(!g_sim.IsCassetteSIOPatchEnabled());
}

void OnCommandCassetteToggleAutoBoot() {
	g_sim.SetCassetteAutoBootEnabled(!g_sim.IsCassetteAutoBootEnabled());
}

void OnCommandCassetteToggleLoadDataAsAudio() {
	ATCassetteEmulator& cas = g_sim.GetCassette();

	cas.SetLoadDataAsAudioEnable(!cas.IsLoadDataAsAudioEnabled());
}

void OnCommandCassetteToggleRandomizeStartPosition() {
	g_sim.SetCassetteRandomizedStartEnabled(!g_sim.IsCassetteRandomizedStartEnabled());
}

ATDisplayFilterMode ATUIGetDisplayFilterMode() {
	return g_dispFilterMode;
}

void ATUISetDisplayFilterMode(ATDisplayFilterMode mode) {
	g_dispFilterMode = mode;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

int ATUIGetViewFilterSharpness() {
	return g_dispFilterSharpness;
}

void ATUISetViewFilterSharpness(int sharpness) {
	g_dispFilterSharpness = sharpness;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

ATDisplayStretchMode ATUIGetDisplayStretchMode() {
	return g_displayStretchMode;
}

void ATUISetDisplayStretchMode(ATDisplayStretchMode mode) {
	if (g_displayStretchMode != mode) {
		g_displayStretchMode = mode;

		ResizeDisplay();
	}
}

void ATUISetOverscanMode(ATGTIAEmulator::OverscanMode mode) {
	g_sim.GetGTIA().SetOverscanMode(mode);
	ResizeDisplay();
}

void OnCommandViewVerticalOverscan(ATGTIAEmulator::VerticalOverscanMode mode) {
	g_sim.GetGTIA().SetVerticalOverscanMode(mode);
	ResizeDisplay();
}

void OnCommandViewTogglePALExtended() {
	g_sim.GetGTIA().SetOverscanPALExtended(!g_sim.GetGTIA().IsOverscanPALExtended());
	ResizeDisplay();
}

void OnCommandViewToggleVSync() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetVsyncEnabled(!gtia.IsVsyncEnabled());
}

bool ATUIGetShowFPS() {
	return g_showFps;
}

void ATUISetShowFPS(bool enabled) {
	g_showFps = enabled;
	g_winCaptionUpdater.SetShowFps(enabled);

	if (enabled)
		g_sim.GetUIRenderer()->SetFpsIndicator(-1.0f);
}

void OnCommandViewAdjustWindowSize() {
	if (g_pMainWindow)
		g_pMainWindow->AutoSize();
}

void OnCommandViewResetWindowLayout() {
	ATLoadDefaultPaneLayout();
}


void OnCommandPane(uint32 paneId) {
	ATActivateUIPane(paneId, true);
}

void OnCommandEditCopyFrame() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const VDPixmap *frame = gtia.GetLastFrameBuffer();

	if (frame) {
		int px = 2;
		int py = 2;
		gtia.GetPixelAspectMultiple(px, py);

		if (px == 1 && py == 1)
			ATCopyFrameToClipboard(g_hwnd, *frame);
		else {
			VDPixmapBuffer buf(frame->w * 2 / px, frame->h * 2 / py, frame->format);

			if (frame->format == nsVDPixmap::kPixFormat_Pal8)
				memcpy((uint32 *)buf.palette, frame->palette, 256*4);

			VDPixmapStretchBltNearest(buf, *frame);

			ATCopyFrameToClipboard(g_hwnd, buf);
		}
	}
}

void OnCommandEditSaveFrame() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const VDPixmap *frame = gtia.GetLastFrameBuffer();

	if (frame) {
		const VDStringW fn(VDGetSaveFileName('scrn', (VDGUIHandle)g_hwnd, L"Save Screenshot", L"Portable Network Graphics (*.png)\0*.png\0", L"png"));

		if (!fn.empty()) {
			int px = 2;
			int py = 2;
			gtia.GetPixelAspectMultiple(px, py);

			if (px == 1 && py == 1)
				ATSaveFrame(g_hwnd, *frame, fn.c_str());
			else {
				VDPixmapBuffer buf(frame->w * 2 / px, frame->h * 2 / py, frame->format);

				if (frame->format == nsVDPixmap::kPixFormat_Pal8)
					memcpy((uint32 *)buf.palette, frame->palette, 256*4);

				VDPixmapStretchBltNearest(buf, *frame);

				ATSaveFrame(g_hwnd, buf, fn.c_str());
			}
		}
	}
}

void OnCommandEditCopyText() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->Copy();
}

void OnCommandEditPasteText() {
	VDStringA s8;
	VDStringW s16;
	bool use16;

	if (ATUIClipGetText(s8, s16, use16)) {
		if (use16)
			Paste(s16.data(), s16.size(), true);
		else
			Paste(s8.data(), s8.size(), true);
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandConsoleBlackBoxDumpScreen() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_BlackBoxDumpScreen, true);
}

void OnCommandConsoleBlackBoxMenu() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_BlackBoxMenu, true);
}

void OnCommandConsoleIDEPlus2SwitchDisks() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_IDEPlus2SwitchDisks, true);
}

void OnCommandConsoleIDEPlus2WriteProtect() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_IDEPlus2WriteProtect, true);
}

void OnCommandConsoleIDEPlus2SDX() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_IDEPlus2SDX, true);
}

///////////////////////////////////////////////////////////////////////////

void OnCommandDiskDrivesDialog() {
	ATUIShowDiskDriveDialog((VDGUIHandle)g_hwnd);
}

void OnCommandDiskToggleAllEnabled() {
	for(int i=0; i<15; ++i) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
		disk.SetEnabled(!disk.IsEnabled());
	}
}

void OnCommandDiskToggleSIOPatch() {
	g_sim.SetDiskSIOPatchEnabled(!g_sim.IsDiskSIOPatchEnabled());
}

void OnCommandDiskToggleSIOOverrideDetection() {
	g_sim.SetDiskSIOOverrideDetectEnabled(!g_sim.IsDiskSIOOverrideDetectEnabled());
}

void OnCommandDiskToggleAccurateSectorTiming() {
	for(int i=0; i<15; ++i) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
		disk.SetAccurateSectorTimingEnabled(!disk.IsAccurateSectorTimingEnabled());
	}
}

bool ATUIGetDriveSoundsEnabled() {
	const ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	return disk.AreDriveSoundsEnabled();
}

void ATUISetDriveSoundsEnabled(bool enabled) {
	for(int i=0; i<15; ++i) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
		disk.SetDriveSoundsEnabled(enabled);
	}
}

void OnCommandDiskToggleDriveSounds() {
	ATUISetDriveSoundsEnabled(!ATUIGetDriveSoundsEnabled());
}

void OnCommandDiskToggleSectorCounter() {
	g_sim.SetDiskSectorCounterEnabled(!g_sim.IsDiskSectorCounterEnabled());
}

class ATUIFutureAttachDisk : public ATUIFuture {
public:
	ATUIFutureAttachDisk(int index)
		: mIndex(index)
	{
	}

	virtual void RunInner() {
		switch(mStage) {
			case 0:
				if (g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + mIndex))) {
					VDStringW msg;

					msg.sprintf(L"Modified disk image in D%u: has not been saved. Discard it to mount a new image?", mIndex + 1);

					mpConfirmResult = ATUIShowAlert(msg.c_str(), L"Altirra Warning");
					Wait(mpConfirmResult);
				}
				++mStage;
				break;

			case 1:
				if (mpConfirmResult && !mpConfirmResult->GetResult()) {
					MarkCompleted();
					break;
				}

				mpConfirmResult.clear();
				mpFileDialogResult = ATUIShowOpenFileDialog('disk', L"Attach disk image",
					L"All supported types\0*.atr;*.pro;*.atx;*.xfd;*.dcm;*.zip;*.gz;*.atz;*.arc\0"
					L"Atari disk image (*.atr, *.xfd)\0*.atr;*.xfd;*.dcm\0"
					L"Protected disk image (*.pro)\0*.pro\0"
					L"VAPI disk image (*.atx)\0*.atx\0"
					L"Zip archive (*.zip)\0*.zip\0"
					L"Gzip archive (*.gz;*.atz)\0*.gz;*.atz\0"
					L".ARC archive (*.arc)\0*.arc\0"
					L"All files\0*.*\0");

				Wait(mpFileDialogResult);
				++mStage;
				break;

			case 2:
				if (mpFileDialogResult->mbAccepted) {
					DoLoad((VDGUIHandle)g_hwnd, mpFileDialogResult->mPath.c_str(), nullptr, 0, kATLoadType_Disk, NULL, mIndex);

					ATAddMRUListItem(mpFileDialogResult->mPath.c_str());
				}
				mpFileDialogResult.clear();

				MarkCompleted();
				break;
		}
	}

	const int mIndex;
	vdrefptr<ATUIFileDialogResult> mpFileDialogResult;
	vdrefptr<ATUIFutureWithResult<bool> > mpConfirmResult;
};

void OnCommandDiskAttach(int index) {
	vdrefptr<ATUIFutureAttachDisk> stage(new ATUIFutureAttachDisk(index));

	ATUIPushStep(stage->GetStep());
}

class ATUIFutureDetachDisk : public ATUIFuture {
public:
	ATUIFutureDetachDisk(int index)
		: mIndex(index)
	{
	}

	virtual void RunInner() {
		switch(mStage) {
			case 0:
				if (mIndex < 0) {
					bool dirtyDisks = false;

					for(int i=0; i<15; ++i) {
						if (g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + i)))
							dirtyDisks = true;
							break;
					}

					if (dirtyDisks) {
						mpConfirmResult = ATUIConfirmDiscardAllStorage(L"OK to discard?", false);
						Wait(mpConfirmResult);
					}
				} else {
					if (g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + mIndex))) {
						VDStringW msg;

						msg.sprintf(L"Modified disk image in D%u: has not been saved. Discard it to mount a new image?", mIndex + 1);

						mpConfirmResult = ATUIShowAlert(msg.c_str(), L"Altirra Warning");
						Wait(mpConfirmResult);
					}
				}
				++mStage;
				break;

			case 1:
				if (mIndex < 0) {
					for(int i=0; i<15; ++i) {
						ATDiskEmulator& drive = g_sim.GetDiskDrive(i);
						drive.UnloadDisk();
						drive.Reset();
						drive.SetEnabled(false);
					}
				} else {
					ATDiskEmulator& drive = g_sim.GetDiskDrive(mIndex);
					drive.UnloadDisk();
					drive.Reset();
					drive.SetEnabled(false);
				}

				MarkCompleted();
				break;
		}
	}

	const int mIndex;
	vdrefptr<ATUIFileDialogResult> mpFileDialogResult;
	vdrefptr<ATUIFutureWithResult<bool> > mpConfirmResult;
};

void OnCommandDiskDetach(int index) {
	vdrefptr<ATUIFutureDetachDisk> stage(new ATUIFutureDetachDisk(index));

	ATUIPushStep(stage->GetStep());
}

void OnCommandDiskDetachAll() {
	vdrefptr<ATUIFutureDetachDisk> stage(new ATUIFutureDetachDisk(-1));

	ATUIPushStep(stage->GetStep());
}

void OnCommandDiskRotate(int delta) {
	// find highest drive in use
	int activeDrives = 0;
	for(int i=14; i>=0; --i) {
		if (g_sim.GetDiskDrive(i).IsEnabled()) {
			activeDrives = i+1;
			break;
		}
	}

	IATUIRenderer *uir = g_sim.GetUIRenderer();
	if (!activeDrives) {
		if (uir)
			uir->SetStatusMessage(L"No disk drives are active.");

		return;
	}

	g_sim.RotateDrives(activeDrives, delta);

	if (uir)
		uir->SetStatusMessage(VDStringW().sprintf(L"Rotated to D1: %ls", g_sim.GetDiskDrive(0).GetMountedImageLabel().c_str()).c_str());
}

///////////////////////////////////////////////////////////////////////////

void ATSetVideoStandard(ATVideoStandard mode) {
	// Don't allow switching to PAL or SECAM in 5200 mode!
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return;

	g_sim.SetVideoStandard(mode);

	ATUIUpdateSpeedTiming();

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->OnSize();
}

ATUIEnhancedTextMode ATUIGetEnhancedTextMode() {
	return (ATUIEnhancedTextMode)g_enhancedText;
}

void ATUISetEnhancedTextMode(ATUIEnhancedTextMode mode) {
	if (g_enhancedText == mode)
		return;

	g_enhancedText = mode;

	switch(mode) {
		case kATUIEnhancedTextMode_None:
		case kATUIEnhancedTextMode_Hardware:
			g_sim.SetVirtualScreenEnabled(false);
			break;

		case kATUIEnhancedTextMode_Software:
			g_sim.SetVirtualScreenEnabled(true);

			// push a break to attempt to kick out of the OS get byte routine
			g_sim.GetPokey().PushBreak();
			break;
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandInputCaptureMouse() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->ToggleCaptureMouse();
}

void OnCommandInputToggleAutoCaptureMouse() {
	g_mouseAutoCapture = !g_mouseAutoCapture;
}

void OnCommandInputInputMappingsDialog() {
	ATUIShowDialogInputMappings(g_hwnd, *g_sim.GetInputManager(), &g_sim.GetJoystickManager());
	ATReloadPortMenus();
}

void OnCommandInputInputSetupDialog() {
	ATUIShowDialogInputSetup(g_hwnd, *g_sim.GetInputManager(), &g_sim.GetJoystickManager());
}

void OnCommandInputKeyboardDialog() {
	if (ATUIShowDialogKeyboardOptions((VDGUIHandle)g_hwnd, g_kbdOpts))
		ATUIInitVirtualKeyMap(g_kbdOpts);
}

void OnCommandInputLightPenDialog() {
	ATUIShowDialogLightPen((VDGUIHandle)g_hwnd, g_sim.GetLightPenPort());
}

void OnCommandInputCycleQuickMaps() {
	auto *pIM = g_sim.GetInputManager();

	if (pIM) {
		ATInputMap *pMap = pIM->CycleQuickMaps();

		auto *pUIR = g_sim.GetUIRenderer();

		if (pUIR) {
			if (pMap)
				pUIR->SetStatusMessage((VDStringW(L"Quick map: ") + pMap->GetName()).c_str());
			else
				pUIR->SetStatusMessage(L"Quick maps disabled");
		}
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandRecordStop() {
	CheckRecordingExceptions();
	StopRecording();
}

void OnCommandRecordRawAudio() {
	if (!g_pAudioWriter && !g_pVideoWriter && !g_pSapWriter) {
		VDStringW s(VDGetSaveFileName('raud', (VDGUIHandle)g_hwnd, L"Record raw audio", L"Raw 32-bit float data\0*.pcm\0", L"pcm"));

		if (!s.empty()) {
			const auto vs = g_sim.GetVideoStandard();
			g_pAudioWriter = new ATAudioWriter(s.c_str(), true, g_sim.IsDualPokeysEnabled(), vs != kATVideoStandard_NTSC && vs != kATVideoStandard_PAL60, g_sim.GetUIRenderer());
			g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter);
		}
	}
}

void OnCommandRecordAudio() {
	if (!g_pAudioWriter && !g_pVideoWriter && !g_pSapWriter) {
		VDStringW s(VDGetSaveFileName('raud', (VDGUIHandle)g_hwnd, L"Record audio", L"Wave audio (*.wav)\0*.wav\0", L"wav"));

		if (!s.empty()) {
			const auto vs = g_sim.GetVideoStandard();
			g_pAudioWriter = new ATAudioWriter(s.c_str(), false, g_sim.IsDualPokeysEnabled(), vs != kATVideoStandard_NTSC && vs != kATVideoStandard_PAL60, g_sim.GetUIRenderer());
			g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter);
		}
	}
}

void OnCommandRecordVideo() {
	if (!g_pAudioWriter && !g_pVideoWriter && !g_pSapWriter) {
		VDStringW s(VDGetSaveFileName('rvid', (VDGUIHandle)g_hwnd, L"Record raw video", L"Audio/visual interleaved (*.avi)\0*.avi\0", L"avi"));

		if (!s.empty()) {
			const bool hz50 = g_sim.GetVideoStandard() != kATVideoStandard_NTSC && g_sim.GetVideoStandard() != kATVideoStandard_PAL60;

			ATVideoEncoding encoding;
			ATVideoRecordingFrameRate frameRateOption;
			bool halfRate;
			bool encodeAll;
			if (ATUIShowDialogVideoEncoding((VDGUIHandle)g_hwnd, hz50, encoding, frameRateOption, halfRate, encodeAll)) {
				try {
					ATGTIAEmulator& gtia = g_sim.GetGTIA();

					ATCreateVideoWriter(~g_pVideoWriter);

					int w;
					int h;
					bool rgb32;
					gtia.GetRawFrameFormat(w, h, rgb32);

					uint32 palette[256];
					if (!rgb32)
						gtia.GetPalette(palette);

					VDFraction frameRate = hz50 ? VDFraction(1773447, 114*312) : VDFraction(3579545, 2*114*262);
					double samplingRate = hz50 ? 1773447.0 / 28.0 : 3579545.0 / 56.0;

					switch(frameRateOption) {
						case kATVideoRecordingFrameRate_NTSCRatio:
							if (hz50) {
								samplingRate = samplingRate * (50000.0 / 1001.0) / frameRate.asDouble();
								frameRate = VDFraction(50000, 1001);
							} else {
								samplingRate = samplingRate * (60000.0 / 1001.0) / frameRate.asDouble();
								frameRate = VDFraction(60000, 1001);
							}
							break;

						case kATVideoRecordingFrameRate_Integral:
							if (hz50) {
								samplingRate = samplingRate * 50.0 / frameRate.asDouble();
								frameRate = VDFraction(50, 1);
							} else {
								samplingRate = samplingRate * 60.0 / frameRate.asDouble();
								frameRate = VDFraction(60, 1);
							}
							break;
					}

					g_pVideoWriter->Init(s.c_str(), encoding, w, h, frameRate, rgb32 ? NULL : palette, samplingRate, g_sim.IsDualPokeysEnabled(), hz50 ? 1773447.0f : 1789772.5f, halfRate, encodeAll, g_sim.GetUIRenderer());

					g_sim.GetAudioOutput()->SetAudioTap(g_pVideoWriter);
					gtia.SetVideoTap(g_pVideoWriter);
				} catch(const MyError& e) {
					StopRecording();
					e.post(g_hwnd, "Altirra Error");
				}
			}
		}
	}
}

void OnCommandRecordSapTypeR() {
	if (!g_pAudioWriter && !g_pVideoWriter && !g_pSapWriter) {
		VDStringW s(VDGetSaveFileName('rsap', (VDGUIHandle)g_hwnd, L"Record SAP type R music file", L"SAP Type R\0*.sap\0", L"sap"));

		if (!s.empty()) {
			const auto vs = g_sim.GetVideoStandard();
			vdautoptr<IATSAPWriter> sw(ATCreateSAPWriter());
			sw->Init(g_sim.GetEventManager(), &g_sim.GetPokey(), g_sim.GetUIRenderer(), s.c_str(), vs != kATVideoStandard_NTSC && vs != kATVideoStandard_PAL60);

			g_pSapWriter.from(sw);
		}
	}
}

class ATUIFutureConvertSapToExe : public ATUIFuture {
public:
	virtual void RunInner() {
		switch(mStage) {
			case 0:
				mpSrcFileDialogResult = ATUIShowOpenFileDialog('lsap', L"Select source SAP file",
					g_ATUIFileFilter_LoadSAP);

				Wait(mpSrcFileDialogResult);
				++mStage;
				break;

			case 1:
				if (!mpSrcFileDialogResult->mbAccepted) {
					MarkCompleted();
					break;
				}

				mpDstFileDialogResult = ATUIShowSaveFileDialog('sxex', L"Select output executable name",
					g_ATUIFileFilter_SaveXEX);

				Wait(mpDstFileDialogResult);
				++mStage;
				break;

			case 2:
				if (!mpDstFileDialogResult->mbAccepted) {
					MarkCompleted();
					break;
				}

				ATConvertSAPToPlayer(mpDstFileDialogResult->mPath.c_str(), mpSrcFileDialogResult->mPath.c_str());

				MarkCompleted();
				break;
		}
	}

	vdrefptr<ATUIFileDialogResult> mpSrcFileDialogResult;
	vdrefptr<ATUIFileDialogResult> mpDstFileDialogResult;
};

void OnCommandToolsConvertSapToExe() {
	vdrefptr<ATUIFutureConvertSapToExe> stage(new ATUIFutureConvertSapToExe);

	ATUIPushStep(stage->GetStep());
}

///////////////////////////////////////////////////////////////////////////

void OnCommandCheatTogglePMCollisions() {
	g_sim.GetGTIA().SetPMCollisionsEnabled(!g_sim.GetGTIA().ArePMCollisionsEnabled());
}

void OnCommandCheatTogglePFCollisions() {
	g_sim.GetGTIA().SetPFCollisionsEnabled(!g_sim.GetGTIA().ArePFCollisionsEnabled());
}

void OnCommandCheatCheatDialog() {
	g_sim.SetCheatEngineEnabled(true);
	ATUIShowDialogCheater((VDGUIHandle)g_hwnd, g_sim.GetCheatEngine());
}

///////////////////////////////////////////////////////////////////////////

void OnCommandToolsDiskExplorer() {
	ATUIShowDialogDiskExplorer((VDGUIHandle)g_hwnd);
}

void OnCommandToolsOptionsDialog() {
	ATUIShowDialogOptions((VDGUIHandle)g_hwnd);
}

void OnCommandToolsKeyboardShortcutsDialog() {
	vdfastvector<VDAccelToCommandEntry> commands;

	g_ATUICommandMgr.ListCommands(commands);

	static const wchar_t *const kContextNames[]={
		L"Global",
		L"Display",
		L"Debugger",
	};

	VDASSERTCT(vdcountof(kContextNames) == kATUIAccelContextCount);

	if (ATUIShowDialogEditAccelerators((VDGUIHandle)g_hwnd,
		commands.data(),
		(uint32)commands.size(),
		ATUIGetAccelTables(), ATUIGetDefaultAccelTables(), kATUIAccelContextCount, kContextNames))
	{
		ATUISaveAccelTables();
		ATUILoadMenu();
	}
}

void OnCommandToolsSetupWizard() {
	void ATUIShowDialogSetupWizard(VDGUIHandle hParent);

	ATUIShowDialogSetupWizard((VDGUIHandle)g_hwnd);
}

void OnCommandHelpContents() {
	ATShowHelp(g_hwnd, NULL);
}

void OnCommandHelpAbout() {
	ATUIShowDialogAbout((VDGUIHandle)g_hwnd);
}

void OnCommandHelpChangeLog() {
	ATShowChangeLog((VDGUIHandle)g_hwnd);
}

///////////////////////////////////////////////////////////////////////////

uint32 g_ATDeviceButtonChangeCount = 0;
uint32 g_ATDeviceButtonMask = 0;

bool ATUIGetDeviceButtonSupported(uint32 idx) {
	ATDeviceManager& dm = *g_sim.GetDeviceManager();
	const uint32 cc = dm.GetChangeCounter();

	if (g_ATDeviceButtonChangeCount != cc) {
		g_ATDeviceButtonChangeCount = cc;

		uint32 mask = 0;

		for(IATDeviceButtons *devbtns : dm.GetInterfaces<IATDeviceButtons>(false, false))
			mask |= devbtns->GetSupportedButtons();

		g_ATDeviceButtonMask = mask;
	}

	return (g_ATDeviceButtonMask & (1 << idx)) != 0;
}

bool ATUIGetDeviceButtonDepressed(uint32 idx) {
	if (!(g_ATDeviceButtonMask & (1 << idx)))
		return false;

	ATDeviceManager& dm = *g_sim.GetDeviceManager();
	bool depressed = false;

	for(IATDeviceButtons *devbtns : dm.GetInterfaces<IATDeviceButtons>(false, false)) {
		if (devbtns->IsButtonDepressed((ATDeviceButton)idx))
			depressed = true;
	}

	return depressed;
}

void ATUIActivateDeviceButton(uint32 idx, bool state) {
	if (!(g_ATDeviceButtonMask & (1 << idx)))
		return;

	ATDeviceManager& dm = *g_sim.GetDeviceManager();
	for(IATDeviceButtons *devbtns : dm.GetInterfaces<IATDeviceButtons>(false, false))
		devbtns->ActivateButton((ATDeviceButton)idx, state);
}

///////////////////////////////////////////////////////////////////////////

class D3D9Lock : public VDD3D9Client {
public:
	D3D9Lock() : mpMgr(NULL) {}

	void Lock() {
		if (!mpMgr)
			mpMgr = VDInitDirect3D9(this, NULL, false);
	}

	void Unlock() {
		if (mpMgr) {
			VDDeinitDirect3D9(mpMgr, this);
			mpMgr = NULL;
		}
	}

	void OnPreDeviceReset() {}
	void OnPostDeviceReset() {}
private:
	VDD3D9Manager *mpMgr;
} g_d3d9Lock;

void SetKernelType(ATFirmwareType type) {
	uint64 id = g_sim.GetFirmwareManager()->GetFirmwareOfType(type, false);
	g_sim.SetKernel(id ? id : kATFirmwareId_NoKernel);
}

void ReadCommandLine(HWND hwnd, VDCommandLine& cmdLine) {
	bool coldReset = false;
	bool debugMode = false;
	bool debugModeSuspend = false;
	const ATMediaWriteMode *bootWriteMode = nullptr;
	bool unloaded = false;
	VDStringA keysToType;
	int cartmapper = 0;

	try {
		// This is normally intercepted early. If we got here, it was because of
		// another instance.
		if (cmdLine.FindAndRemoveSwitch(L"baseline")) {
			LoadBaselineSettings();
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"autotest")) {
			ATSetAutotestEnabled(true);
		}

		if (cmdLine.FindAndRemoveSwitch(L"f"))
			ATSetFullscreen(true);

		if (cmdLine.FindAndRemoveSwitch(L"ntsc"))
			g_sim.SetVideoStandard(kATVideoStandard_NTSC);
		else if (cmdLine.FindAndRemoveSwitch(L"pal"))
			g_sim.SetVideoStandard(kATVideoStandard_PAL);
		else if (cmdLine.FindAndRemoveSwitch(L"secam"))
			g_sim.SetVideoStandard(kATVideoStandard_SECAM);
		else if (cmdLine.FindAndRemoveSwitch(L"ntsc50"))
			g_sim.SetVideoStandard(kATVideoStandard_NTSC50);
		else if (cmdLine.FindAndRemoveSwitch(L"pal60"))
			g_sim.SetVideoStandard(kATVideoStandard_PAL60);

		if (cmdLine.FindAndRemoveSwitch(L"burstio")) {
			g_sim.SetDiskBurstTransfersEnabled(true);
		} else if (cmdLine.FindAndRemoveSwitch(L"burstiopolled")) {
			g_sim.SetDiskBurstTransfersEnabled(true);
		} else if (cmdLine.FindAndRemoveSwitch(L"noburstio")) {
			g_sim.SetDiskBurstTransfersEnabled(false);
		}

		if (cmdLine.FindAndRemoveSwitch(L"siopatch")) {
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetDiskSIOOverrideDetectEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(true);
		} else if (cmdLine.FindAndRemoveSwitch(L"siopatchsafe")) {
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetDiskSIOOverrideDetectEnabled(true);
			g_sim.SetCassetteSIOPatchEnabled(true);
		} else if (cmdLine.FindAndRemoveSwitch(L"nosiopatch")) {
			g_sim.SetDiskSIOPatchEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(false);
		}

		if (cmdLine.FindAndRemoveSwitch(L"fastboot")) {
			g_sim.SetFastBootEnabled(true);
		} else if (cmdLine.FindAndRemoveSwitch(L"nofastboot")) {
			g_sim.SetFastBootEnabled(false);
		}

		if (cmdLine.FindAndRemoveSwitch(L"casautoboot")) {
			g_sim.SetCassetteAutoBootEnabled(true);
		} else if (cmdLine.FindAndRemoveSwitch(L"nocasautoboot")) {
			g_sim.SetCassetteAutoBootEnabled(false);
		}

		if (cmdLine.FindAndRemoveSwitch(L"accuratedisk")) {
			for(int i=0; i<15; ++i) {
				ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
				disk.SetAccurateSectorTimingEnabled(true);
			}
		} else if (cmdLine.FindAndRemoveSwitch(L"noaccuratedisk")) {
			for(int i=0; i<15; ++i) {
				ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
				disk.SetAccurateSectorTimingEnabled(false);
			}
		}

		if (cmdLine.FindAndRemoveSwitch(L"stereo")) {
			g_sim.SetDualPokeysEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"nostereo")) {
			g_sim.SetDualPokeysEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"basic")) {
			g_sim.SetBASICEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"nobasic")) {
			g_sim.SetBASICEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"vbxe")) {
			g_sim.SetVBXEEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"novbxe")) {
			g_sim.SetVBXEEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"vbxeshared")) {
			g_sim.SetVBXEEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"novbxeshared")) {
			g_sim.SetVBXEEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"vbxealtpage")) {
			g_sim.SetVBXEAltPageEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"novbxealtpage")) {
			g_sim.SetVBXEAltPageEnabled(false);
			coldReset = true;
		}

		const wchar_t *arg;
		if (cmdLine.FindAndRemoveSwitch(L"soundboard", arg)) {
			ATPropertySet pset;
			
			uint32 base = 0;

			if (!vdwcsicmp(arg, L"d2c0"))
				base = 0xD2C0;
			else if (!vdwcsicmp(arg, L"d500"))
				base = 0xD500;
			else if (!vdwcsicmp(arg, L"d600"))
				base = 0xD600;
			else
				throw MyError("Command line error: Invalid SoundBoard memory base: '%ls'", arg);

			auto *dm = g_sim.GetDeviceManager();
			auto *dev = dm->GetDeviceByTag("soundboard");

			if (dev)
				dev->SetSettings(pset);
			else
				dm->AddDevice("soundboard", pset, false, false);

			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"nosoundboard")) {
			g_sim.GetDeviceManager()->RemoveDevice("soundboard");
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"slightsid", arg)) {
			auto& dm = *g_sim.GetDeviceManager();
			if (!dm.GetDeviceByTag("slightsid"))
				dm.AddDevice("slightsid", ATPropertySet(), false, false);
		} else if (cmdLine.FindAndRemoveSwitch(L"noslightsid")) {
			g_sim.GetDeviceManager()->RemoveDevice("slightsid");
		}

		if (cmdLine.FindAndRemoveSwitch(L"covox", arg)) {
			auto& dm = *g_sim.GetDeviceManager();
			if (!dm.GetDeviceByTag("covox"))
				dm.AddDevice("covox", ATPropertySet(), false, false);
		} else if (cmdLine.FindAndRemoveSwitch(L"nocovox")) {
			g_sim.GetDeviceManager()->RemoveDevice("covox");
		}

		if (cmdLine.FindAndRemoveSwitch(L"hardware", arg)) {
			if (!vdwcsicmp(arg, L"800"))
				g_sim.SetHardwareMode(kATHardwareMode_800);
			else if (!vdwcsicmp(arg, L"800xl"))
				g_sim.SetHardwareMode(kATHardwareMode_800XL);
			else if (!vdwcsicmp(arg, L"1200xl"))
				g_sim.SetHardwareMode(kATHardwareMode_1200XL);
			else if (!vdwcsicmp(arg, L"130xe"))
				g_sim.SetHardwareMode(kATHardwareMode_130XE);
			else if (!vdwcsicmp(arg, L"xegs"))
				g_sim.SetHardwareMode(kATHardwareMode_XEGS);
			else if (!vdwcsicmp(arg, L"5200"))
				g_sim.SetHardwareMode(kATHardwareMode_5200);
			else
				throw MyError("Command line error: Invalid hardware mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"kernel", arg)) {
			if (!vdwcsicmp(arg, L"default"))
				g_sim.SetKernel(0);
			else if (!vdwcsicmp(arg, L"osa"))
				SetKernelType(kATFirmwareType_Kernel800_OSA);
			else if (!vdwcsicmp(arg, L"osb"))
				SetKernelType(kATFirmwareType_Kernel800_OSB);
			else if (!vdwcsicmp(arg, L"xl"))
				SetKernelType(kATFirmwareType_KernelXL);
			else if (!vdwcsicmp(arg, L"xegs"))
				SetKernelType(kATFirmwareType_KernelXEGS);
			else if (!vdwcsicmp(arg, L"1200xl"))
				SetKernelType(kATFirmwareType_Kernel1200XL);
			else if (!vdwcsicmp(arg, L"5200"))
				SetKernelType(kATFirmwareType_Kernel5200);
			else if (!vdwcsicmp(arg, L"lle"))
				g_sim.SetKernel(kATFirmwareId_Kernel_LLE);
			else if (!vdwcsicmp(arg, L"llexl"))
				g_sim.SetKernel(kATFirmwareId_Kernel_LLEXL);
			else if (!vdwcsicmp(arg, L"hle"))
				g_sim.SetKernel(kATFirmwareId_Kernel_HLE);
			else if (!vdwcsicmp(arg, L"5200lle"))
				g_sim.SetKernel(kATFirmwareId_5200_LLE);
			else
				throw MyError("Command line error: Invalid kernel mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"memsize", arg)) {
			if (!vdwcsicmp(arg, L"8K"))
				g_sim.SetMemoryMode(kATMemoryMode_8K);
			else if (!vdwcsicmp(arg, L"16K"))
				g_sim.SetMemoryMode(kATMemoryMode_16K);
			else if (!vdwcsicmp(arg, L"24K"))
				g_sim.SetMemoryMode(kATMemoryMode_24K);
			else if (!vdwcsicmp(arg, L"32K"))
				g_sim.SetMemoryMode(kATMemoryMode_32K);
			else if (!vdwcsicmp(arg, L"40K"))
				g_sim.SetMemoryMode(kATMemoryMode_40K);
			else if (!vdwcsicmp(arg, L"48K"))
				g_sim.SetMemoryMode(kATMemoryMode_48K);
			else if (!vdwcsicmp(arg, L"52K"))
				g_sim.SetMemoryMode(kATMemoryMode_52K);
			else if (!vdwcsicmp(arg, L"64K"))
				g_sim.SetMemoryMode(kATMemoryMode_64K);
			else if (!vdwcsicmp(arg, L"128K"))
				g_sim.SetMemoryMode(kATMemoryMode_128K);
			else if (!vdwcsicmp(arg, L"256K"))
				g_sim.SetMemoryMode(kATMemoryMode_256K);
			else if (!vdwcsicmp(arg, L"320K"))
				g_sim.SetMemoryMode(kATMemoryMode_320K);
			else if (!vdwcsicmp(arg, L"320KCOMPY"))
				g_sim.SetMemoryMode(kATMemoryMode_320K_Compy);
			else if (!vdwcsicmp(arg, L"576K"))
				g_sim.SetMemoryMode(kATMemoryMode_576K);
			else if (!vdwcsicmp(arg, L"576KCOMPY"))
				g_sim.SetMemoryMode(kATMemoryMode_576K_Compy);
			else if (!vdwcsicmp(arg, L"1088K"))
				g_sim.SetMemoryMode(kATMemoryMode_1088K);
			else
				throw MyError("Command line error: Invalid memory mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"axlonmemsize", arg)) {
			if (!vdwcsicmp(arg, L"none"))
				g_sim.SetAxlonMemoryMode(0);
			if (!vdwcsicmp(arg, L"64K"))
				g_sim.SetAxlonMemoryMode(2);
			if (!vdwcsicmp(arg, L"128K"))
				g_sim.SetAxlonMemoryMode(3);
			if (!vdwcsicmp(arg, L"256K"))
				g_sim.SetAxlonMemoryMode(4);
			if (!vdwcsicmp(arg, L"512K"))
				g_sim.SetAxlonMemoryMode(5);
			if (!vdwcsicmp(arg, L"1024K"))
				g_sim.SetAxlonMemoryMode(6);
			if (!vdwcsicmp(arg, L"2048K"))
				g_sim.SetAxlonMemoryMode(7);
			if (!vdwcsicmp(arg, L"4096K"))
				g_sim.SetAxlonMemoryMode(7);
		}

		if (cmdLine.FindAndRemoveSwitch(L"highbanks", arg)) {
			if (!vdwcsicmp(arg, L"na"))
				g_sim.SetHighMemoryBanks(-1);
			else if (!vdwcsicmp(arg, L"0"))
				g_sim.SetHighMemoryBanks(0);
			else if (!vdwcsicmp(arg, L"1"))
				g_sim.SetHighMemoryBanks(1);
			else if (!vdwcsicmp(arg, L"3"))
				g_sim.SetHighMemoryBanks(3);
			else if (!vdwcsicmp(arg, L"15"))
				g_sim.SetHighMemoryBanks(15);
			else if (!vdwcsicmp(arg, L"63"))
				g_sim.SetHighMemoryBanks(63);
		}

		if (cmdLine.FindAndRemoveSwitch(L"artifact", arg)) {
			if (!vdwcsicmp(arg, L"none"))
				g_sim.GetGTIA().SetArtifactingMode(ATGTIAEmulator::kArtifactNone);
			else if (!vdwcsicmp(arg, L"ntsc"))
				g_sim.GetGTIA().SetArtifactingMode(ATGTIAEmulator::kArtifactNTSC);
			else if (!vdwcsicmp(arg, L"ntschi"))
				g_sim.GetGTIA().SetArtifactingMode(ATGTIAEmulator::kArtifactNTSCHi);
			else if (!vdwcsicmp(arg, L"pal"))
				g_sim.GetGTIA().SetArtifactingMode(ATGTIAEmulator::kArtifactPAL);
			else if (!vdwcsicmp(arg, L"palhi"))
				g_sim.GetGTIA().SetArtifactingMode(ATGTIAEmulator::kArtifactPALHi);
			else
				throw MyError("Command line error: Invalid hardware mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"vsync"))
			g_sim.GetGTIA().SetVsyncEnabled(true);
		else if (cmdLine.FindAndRemoveSwitch(L"novsync"))
			g_sim.GetGTIA().SetVsyncEnabled(false);

		if (cmdLine.FindAndRemoveSwitch(L"debug")) {
			// Open the console now so we see load messages.
			ATShowConsole();
			debugMode = true;
		}

		IATDebugger *dbg = ATGetDebugger();
		if (cmdLine.FindAndRemoveSwitch(L"debugbrkrun"))
			dbg->SetBreakOnEXERunAddrEnabled(true);
		else if (cmdLine.FindAndRemoveSwitch(L"nodebugbrkrun"))
			dbg->SetBreakOnEXERunAddrEnabled(false);

		while(cmdLine.FindAndRemoveSwitch(L"debugcmd", arg)) {
			debugMode = true;
			debugModeSuspend = true;

			dbg->QueueCommand(VDTextWToA(arg).c_str(), false);
		}

		if (cmdLine.FindAndRemoveSwitch(L"bootro")) {
			static const auto modeRO = kATMediaWriteMode_RO;
			bootWriteMode = &modeRO;
		}

		if (cmdLine.FindAndRemoveSwitch(L"bootrw")) {
			static const auto modeRW = kATMediaWriteMode_RW;
			bootWriteMode = &modeRW;
		}

		if (cmdLine.FindAndRemoveSwitch(L"bootvrw")) {
			static const auto modeVRW = kATMediaWriteMode_VRW;
			bootWriteMode = &modeVRW;
		}

		if (cmdLine.FindAndRemoveSwitch(L"bootvrwsafe")) {
			static const auto modeVRWSafe = kATMediaWriteMode_VRWSafe;
			bootWriteMode = &modeVRWSafe;
		}

		if (cmdLine.FindAndRemoveSwitch(L"type", arg)) {
			keysToType += VDTextWToA(arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"nopclink")) {
			g_sim.GetDeviceManager()->RemoveDevice("pclink");
		} else if (cmdLine.FindAndRemoveSwitch(L"pclink", arg)) {
			VDStringRefW tokenizer(arg);
			VDStringRefW mode;

			if (!tokenizer.split(',', mode))
				throw MyError("Invalid PCLink mount string: %ls", arg);

			bool write = false;

			if (mode == L"rw")
				write = true;
			else if (mode != L"ro")
				throw MyError("Invalid PCLink mount mode: %.*ls", mode.size(), mode.data());

			ATPropertySet pset;
			pset.SetString("path", VDStringW(tokenizer).c_str());

			if (write)
				pset.SetBool("write", true);

			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("pclink");

			if (dev)
				dev->SetSettings(pset);
			else
				dm->AddDevice("pclink", pset, false, false);
		}

		if (cmdLine.FindAndRemoveSwitch(L"nohdpath", arg)) {
			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("hostfs");

			if (dev) {
				dm->RemoveDevice(dev);
				coldReset = true;
			}
		}

		if (cmdLine.FindAndRemoveSwitch(L"hdpath", arg)) {
			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("hostfs");

			if (!dev)
				dev = dm->AddDevice("hostfs", ATPropertySet(), false, false);

			IATHostDeviceEmulator *hd = vdpoly_cast<IATHostDeviceEmulator *>(dev);
			if (hd) {
				hd->SetReadOnly(true);
				hd->SetBasePath(0, arg);

				coldReset = true;
			}
		}

		if (cmdLine.FindAndRemoveSwitch(L"hdpathrw", arg)) {
			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("hostfs");

			if (!dev)
				dev = dm->AddDevice("hostfs", ATPropertySet(), false, false);

			IATHostDeviceEmulator *hd = vdpoly_cast<IATHostDeviceEmulator *>(dev);
			if (hd) {
				hd->SetReadOnly(false);
				hd->SetBasePath(0, arg);

				coldReset = true;
			}
		}

		if (cmdLine.FindAndRemoveSwitch(L"rawkeys"))
			g_kbdOpts.mbRawKeys = true;
		else if (cmdLine.FindAndRemoveSwitch(L"norawkeys"))
			g_kbdOpts.mbRawKeys = false;

		if (cmdLine.FindAndRemoveSwitch(L"cartmapper", arg)) {
			cartmapper = ATGetCartridgeModeForMapper(wcstol(arg, NULL, 10));

			if (cartmapper <= 0 || cartmapper >= kATCartridgeModeCount)
				throw MyError("Unsupported or invalid cartridge mapper: %ls", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"nocheats")) {
			g_sim.SetCheatEngineEnabled(false);
		} else if (cmdLine.FindAndRemoveSwitch(L"cheats", arg)) {
			g_sim.SetCheatEngineEnabled(true);
			g_sim.GetCheatEngine()->Load(arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"?")) {
			MessageBox(g_hwnd,
				_T("Usage: Altirra [switches] <disk/cassette/cartridge images...>\n")
				_T("\n")
				_T("/resetall - Reset all settings to default\n")
				_T("/baseline - Reset hardware settings to default\n")
				_T("/ntsc - select NTSC timing and behavior\n")
				_T("/pal - select PAL timing and behavior\n")
				_T("/secam - select SECAM timing and behavior\n")
				_T("/artifact:none|ntsc|ntschi|pal|palhi - set video artifacting\n")
				_T("/[no]burstio|burstiopolled - control SIO burst I/O mode\n")
				_T("/[no]siopatch[safe] - control SIO kernel patch\n")
				_T("/[no]fastboot - control fast kernel boot initialization\n")
				_T("/[no]casautoboot - control cassette auto-boot\n")
				_T("/[no]accuratedisk - control accurate sector timing\n")
				_T("/[no]basic - enable/disable BASIC ROM\n")
				_T("/[no]vbxe - enable/disable VBXE support\n")
				_T("/[no]vbxeshared - enable/disable VBXE shared memory\n")
				_T("/[no]vbxealtpage - enable/disable VBXE $D7xx register window\n")
				_T("/[no]soundboard:d2c0|d500|d600 - enable/disable SoundBoard\n")
				_T("/kernel:default|osa|osb|xl|lle|hle|other|5200|5200lle - select kernel ROM\n")
				_T("/hardware:800|800xl|5200 - select hardware type\n")
				_T("/memsize:16K|48K|52K|64K|128K|320K|576K|1088K - select memory size\n")
				_T("/[no]stereo - enable dual pokeys\n")
				_T("/gdi - force GDI display mode\n")
				_T("/ddraw - force DirectDraw display mode\n")
				_T("/opengl - force OpenGL display mode\n")
				_T("/[no]vsync - synchronize to vertical blank\n")
				_T("/debug - launch in debugger mode")
				_T("/[no]debugbrkrun - break into debugger at EXE run address\n")
				_T("/f - start full screen")
				_T("/bootvrw - boot disk images in virtual R/W mode\n")
				_T("/bootrw - boot disk images in read/write mode\n")
				_T("/type \"keys\" - type keys on keyboard (~ for enter, ` for \")\n")
				_T("/[no]hdpath|hdpathrw <path> - mount H: device\n")
				_T("/[no]rawkeys - enable/disable raw keyboard presses\n")
				_T("/cartmapper <mapper> - set cartridge mapper for untagged image\n")
				_T("/portable - create Altirra.ini file and switch to portable mode\n")
				_T("/portablealt:<file> - temporarily enable portable mode with alternate INI file\n")
				_T("/ide:d1xx|d5xx|kmkjzv1|kmkjzv2|side,ro|rw,c,h,s,path - set IDE emulation\n")
				_T("/[no]pclink:ro|rw,path - set PCLink emulation\n")
				_T("/hostcpu:none|mmx|sse|sse2|sse2|sse3|ssse3|sse41 - override host CPU detection\n")
				_T("/cart <image> - load cartridge image\n")
				_T("/tape <image> - load cassette tape image\n")
				_T("/disk <image> - load disk image\n")
				_T("/run <image> - run binary program")
				_T("/runbas <image> - run BASIC program")
				_T("/[no]cheats <file> - load cheats")
				,
				_T("Altirra command-line help"), MB_OK | MB_ICONINFORMATION);
		}

		while(cmdLine.FindAndRemoveSwitch(L"cart", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATLoadType_Cartridge);
			coldReset = true;// required to set up cassette autoboot
		}

		int diskIndex = 0;
		while(cmdLine.FindAndRemoveSwitch(L"disk", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATLoadType_Disk, nullptr, diskIndex++);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"run", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATLoadType_Program);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"runbas", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATLoadType_BasicProgram);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"tape", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATLoadType_Tape);
			coldReset = true;// required to set up cassette autoboot
		}

		// We don't actually need to do anything with this switch. Its mere presence would have
		// suppressed the setup wizard as any other switch does.
		cmdLine.FindAndRemoveSwitch(L"skipsetup");

		VDCommandLineIterator it;
		if (cmdLine.GetNextSwitchArgument(it, arg)) {
			throw MyError("Unknown command-line switch: %ls. Use /? for help.", arg);
		}

		bool suppressColdReset = false;
		while(cmdLine.GetNextNonSwitchArgument(it, arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATLoadType_Other, &suppressColdReset);

			VDSetLastLoadSavePath('load', VDGetFullPath(arg).c_str());

			coldReset = !suppressColdReset; // required to set up cassette autoboot
		}

	} catch(const MyError& e) {
		e.post(hwnd, "Altirra error");
	}

	if (coldReset)
		g_sim.ColdReset();

	const VDStringW dbgInitPath(VDMakePath(VDGetProgramPath().c_str(), L"startup.atdbg"));

	if (VDDoesPathExist(dbgInitPath.c_str())) {
		try {
			ATGetDebugger()->QueueBatchFile(dbgInitPath.c_str());
		} catch(const MyError&) {
			// ignore startup error
		}
		debugModeSuspend = true;
	}

	if (debugModeSuspend) {
		g_sim.Suspend();
		ATGetDebugger()->QueueCommand("`g -n", false);
	}

	if (!keysToType.empty()) {
		VDStringA::size_type i = 0;
		while((i = keysToType.find('~', i)) != VDStringA::npos) {
			keysToType[i] = '\n';
			++i;
		}

		i = 0;
		while((i = keysToType.find('`', i)) != VDStringA::npos) {
			keysToType[i] = '"';
			++i;
		}

		Paste(keysToType.data(), keysToType.size(), false);
	}

	ATUIUpdateSpeedTiming();

	// Check if we should display the setup wizard at this time. We skip it if:
	//
	// - We've already tried to show it
	// - There was something on the command line other than /portable or /portablealt
	// - We think the program's been run before
	//
	{
		VDRegistryAppKey key;

		if (!key.getBool("ShownSetupWizard")) {
			key.setBool("ShownSetupWizard", true);

			if (!g_ATCmdLineHadAnything && !g_ATRegistryHadAnything) {
				ATUIShowDialogSetupWizard(ATUIGetMainWindow());
			}
		}
	}
}

void LoadSettingsEarly() {
	g_enhancedTextFont.lfHeight			= 16;
    g_enhancedTextFont.lfWidth			= 0;
    g_enhancedTextFont.lfEscapement		= 0;
    g_enhancedTextFont.lfOrientation	= 0;
    g_enhancedTextFont.lfWeight			= 0;
    g_enhancedTextFont.lfItalic			= FALSE;
    g_enhancedTextFont.lfUnderline		= FALSE;
    g_enhancedTextFont.lfStrikeOut		= FALSE;
	g_enhancedTextFont.lfCharSet		= DEFAULT_CHARSET;
	g_enhancedTextFont.lfOutPrecision	= OUT_DEFAULT_PRECIS;
	g_enhancedTextFont.lfClipPrecision	= CLIP_DEFAULT_PRECIS;
    g_enhancedTextFont.lfQuality		= DEFAULT_QUALITY;
	g_enhancedTextFont.lfPitchAndFamily	= FF_DONTCARE | DEFAULT_PITCH;
    wcscpy(g_enhancedTextFont.lfFaceName, L"Lucida Console");

	VDRegistryAppKey key("Settings");
	VDStringW family;
	int fontSize;
	if (key.getString("Enhanced video: Font family", family)
		&& (fontSize = key.getInt("Enhanced video: Font size", 0))) {

		g_enhancedTextFont.lfHeight = fontSize;
		vdwcslcpy(g_enhancedTextFont.lfFaceName, family.c_str(), sizeof(g_enhancedTextFont.lfFaceName)/sizeof(g_enhancedTextFont.lfFaceName[0]));
	}
}

uint64 ATGetCumulativeCPUTime() {
	FILETIME ct;
	FILETIME et;
	FILETIME kt;
	FILETIME ut;
	if (!::GetProcessTimes(GetCurrentProcess(), &ct, &et, &kt, &ut))
		return 0;

	return ((uint64)kt.dwHighDateTime << 32) + kt.dwLowDateTime
		+ ((uint64)ut.dwHighDateTime << 32) + ut.dwLowDateTime;
}

// The joystick poll is normally done once a frame. However, when we aren't
// running, we need to do an idle poll. We poll only a 100ms timer normally
// and speed that up to 10ms for five seconds when activity is detected.
VDLazyTimer g_joystickPollTimer;
uint32 g_joystickPollLastFast;
bool g_joystickPollActive;
bool g_joystickPollFast;
uint32 g_joystickLastActivity;

void ATPollJoysticks() {
	auto& jm = g_sim.GetJoystickManager();

	switch(jm.Poll()) {
		case IATJoystickManager::kPollResult_NoControllers:
			g_joystickPollTimer.Stop();
			break;

		case IATJoystickManager::kPollResult_NoActivity:
			if (g_joystickPollFast && VDGetCurrentTick() - g_joystickPollLastFast > 5000) {
				g_joystickPollFast = false;
				g_joystickPollTimer.SetPeriodicFn(ATPollJoysticks, 100);
			}
			break;

		case IATJoystickManager::kPollResult_OK:
			g_joystickPollLastFast = VDGetCurrentTick();
			if (!g_joystickPollFast) {
				g_joystickPollFast = true;
				g_joystickPollTimer.SetPeriodicFn(ATPollJoysticks, 10);
			}
			break;
	}
}

void ATBeginIdleJoystickPoll() {
	if (g_joystickPollActive)
		return;
	g_joystickPollActive = true;

	g_joystickPollLastFast = VDGetCurrentTick();
	g_joystickPollFast = true;
	g_joystickPollTimer.SetPeriodicFn(ATPollJoysticks, 10);
}

void ATEndIdleJoystickPoll() {
	if (g_joystickPollActive) {
		g_joystickPollActive = false;
		g_joystickPollTimer.Stop();
	}
}

void ATOnJoystickActivity() {
	uint32 t = VDGetCurrentTick();

	if (t - g_joystickLastActivity >= 2000) {
		g_joystickLastActivity = t;

		// send dummy mouse input to reset the screensaver timer
		INPUT input = {};
		input.type = INPUT_MOUSE;
		input.mi.dx = 0;
		input.mi.dy = 0;
		input.mi.mouseData = 0;
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		input.mi.time = 0;
		input.mi.dwExtraInfo = 0;

		SendInput(1, &input, sizeof input);
	}
}

void ATUIUpdateWindowCaption() {
}

int RunMainLoop2(HWND hwnd) {
	int ticks = 0;

	ATAnticEmulator& antic = g_sim.GetAntic();
	uint32 lastFrame = antic.GetFrameCounter();
	uint32 frameTimeErrorAccum = 0;
	sint64 error = 0;
	uint64 secondTime = VDGetPreciseTicksPerSecondI();
	float invSecondTime = 1.0f / (float)secondTime;

	uint64 lastTime = VDGetPreciseTick();
	uint64 lastStatusTime = lastTime;

	sint64 nextFrameTime;
	int nextTickUpdate = 60;
	bool nextFrameTimeValid = false;
	bool updateScreenPending = false;
	uint64 lastCPUTime = 0;

	g_winCaptionUpdater.Update(true, 0, 0, 0);

	g_sim.GetJoystickManager().SetOnActivity(ATOnJoystickActivity);

	int rcode = 0;
	bool lastIsRunning = false;

	g_pATIdle = [&](bool modalLoop) -> bool {
		if (modalLoop && g_ATOptions.mbPauseDuringMenu)
			return false;

		bool isRunning = g_sim.IsRunning();
		if (!isRunning)
			ATUIFlushDisplay();

		if (ATUIGetQueue().Run()) {
			ATBeginIdleJoystickPoll();
			return true;
		}

		if (isRunning)
			ATEndIdleJoystickPoll();

		if (isRunning != lastIsRunning) {
			if (isRunning) {
				timeBeginPeriod(1);
			} else {
				timeEndPeriod(1);
				ATBeginIdleJoystickPoll();
			}

			lastIsRunning = isRunning;
		}

		if (isRunning && (g_winActive || !g_pauseInactive)) {
			g_ATAutoFrameFlipping = true;

			uint32 frame = antic.GetFrameCounter();
			if (frame != lastFrame) {
				CheckRecordingExceptions();

				IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateTextDisplay(g_enhancedText != 0);

				lastFrame = frame;

				uint64 curTime = VDGetPreciseTick();

				++ticks;

				if ((ticks - nextTickUpdate) >= 0) {
					float fps = 0;
					float cpu = 0;

					if (g_showFps) {
						float deltaPreciseTicks = (float)(curTime - lastStatusTime);
						fps = (float)secondTime / deltaPreciseTicks * 60;

						uint64 newCPUTime = ATGetCumulativeCPUTime();

						sint64 deltaCPUTime = newCPUTime - lastCPUTime;
						lastCPUTime = newCPUTime;

						cpu = ((float)deltaCPUTime / 10000000.0f) / (deltaPreciseTicks * invSecondTime) * 100.0f;
					}

					g_sim.GetUIRenderer()->SetFpsIndicator(g_fullscreen && g_showFps ? fps : -1.0f);

					if (!g_fullscreen)
						g_winCaptionUpdater.Update(true, ticks, fps, cpu);

					lastStatusTime = curTime;
					nextTickUpdate = ticks - ticks % 60 + 60;
				}

				sint64 delta = curTime - lastTime;

				error += delta;

				error -= g_frameTicks;
				frameTimeErrorAccum += g_frameSubTicks;

				if (frameTimeErrorAccum >= 0x10000) {
					frameTimeErrorAccum &= 0xFFFF;

					--error;
				}

				if (error > g_frameErrorBound || error < -g_frameErrorBound)
					error = 0;

				lastTime = curTime;

				nextFrameTimeValid = false;
				if (g_sim.IsTurboModeEnabled())
					error = 0;
				else if (error < 0) {
					nextFrameTimeValid = true;
					nextFrameTime = curTime - error;
				}
			}

			bool dropFrame = g_sim.IsTurboModeEnabled() && (lastFrame & 15);
			if (nextFrameTimeValid) {
				uint64 curTime = VDGetPreciseTick();

				sint64 delta = nextFrameTime - curTime;

				if (delta <= 0 || delta > g_frameTimeout)
					nextFrameTimeValid = false;
				else {
					int ticks = (int)(delta * 1000 / secondTime);

					if (ticks <= 0)
						nextFrameTimeValid = false;
					else {
						if (g_sim.IsTurboModeEnabled())
							dropFrame = true;
						else {
							ATProfileBeginRegion(kATProfileRegion_Idle);
							::MsgWaitForMultipleObjects(0, NULL, FALSE, ticks, QS_ALLINPUT);
							ATProfileEndRegion(kATProfileRegion_Idle);
							return true;
						}
					}
				}
			}

			// do not allow frame drops if we are recording
			if (g_pVideoWriter)
				dropFrame = false;

			if (g_sim.GetAntic().GetBeamY() == 0)
				ATProfileMarkEvent(kATProfileEvent_BeginFrame);

			ATProfileBeginRegion(kATProfileRegion_Simulation);
			ATSimulator::AdvanceResult ar = g_sim.Advance(dropFrame);
			ATProfileEndRegion(kATProfileRegion_Simulation);

			if (ar == ATSimulator::kAdvanceResult_Stopped) {
				updateScreenPending = !g_sim.IsPaused();
			} else {
				updateScreenPending = false;

				if (ar == ATSimulator::kAdvanceResult_WaitingForFrame) {
					ATProfileBeginRegion(kATProfileRegion_Idle);
					::MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT);
					ATProfileEndRegion(kATProfileRegion_Idle);
				}
			}

			return true;
		} else {
			if (ATGetDebugger()->Tick())
				return true;

			g_ATAutoFrameFlipping = false;
			ATUIFlushDisplay();

			if (ATIsDebugConsoleActive()) {
				if (g_fullscreen)
					ATSetFullscreen(false);

				IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateTextDisplay(g_enhancedText != 0);
			}

			if (updateScreenPending) {
				updateScreenPending = false;
				g_sim.GetGTIA().UpdateScreen(true, false);
			}

			g_winCaptionUpdater.Update(false, 0, 0, 0);
			nextTickUpdate = ticks - ticks % 60;

			g_sim.FlushDeferredEvents();
		}

		return false;
	};

	for(;;) {
		if (!ATUIProcessMessages(false, rcode))
			break;

		if (!g_ATCmdLineRead) {
			g_ATCmdLineRead = true;

			ReadCommandLine(g_hwnd, g_ATCmdLine);
		}

		if (!g_pATIdle(false))
			WaitMessage();

	}

	if (lastIsRunning) {
		ATEndIdleJoystickPoll();
		timeEndPeriod(1);
	}

	g_sim.GetJoystickManager().SetOnActivity({});

	return 0;
}

int RunMainLoop(HWND hwnd) {
	int rc;

	__try {
		rc = RunMainLoop2(hwnd);
	} __except(ATExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
	}

	return rc;
}

int RunInstance(int nCmdShow);

void ATUISetCommandLine(const wchar_t *s) {
	g_ATCmdLine.InitAlt(s);
	g_ATCmdLineRead = false;
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int nCmdShow) {
	ATInitCRTHooks();

	ATUISetCommandLine(GetCommandLineW());

	::SetUnhandledExceptionFilter(ATUnhandledExceptionFilter);

	if (g_ATCmdLine.FindAndRemoveSwitch(L"fullheapdump"))
		ATExceptionFilterSetFullHeapDump(true);

	uint32 cpuext = CPUCheckForExtensions();

	{
		const wchar_t *token;
		if (g_ATCmdLine.FindAndRemoveSwitch(L"hostcpu", token)) {
			if (!vdwcsicmp(token, L"none"))
				cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU;
			else if (!vdwcsicmp(token, L"mmx"))
				cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX;
			else if (!vdwcsicmp(token, L"isse"))
				cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE;
			else if (!vdwcsicmp(token, L"sse"))
				cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2;
			else if (!vdwcsicmp(token, L"sse2"))
				cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2;
			else if (!vdwcsicmp(token, L"sse3"))
				cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3;
			else if (!vdwcsicmp(token, L"ssse3"))
				cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3;
			else if (!vdwcsicmp(token, L"sse41"))
				cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
						| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SSE41;
		}
	}

	CPUEnableExtensions(cpuext);
	VDFastMemcpyAutodetect();
	VDInitThunkAllocator();

	OleInitialize(NULL);

	InitCommonControls();

	// setup registry
	VDRegistryAppKey::setDefaultKey("Software\\virtualdub.org\\Altirra\\");

	VDRegistryProviderMemory *rpmem = NULL;

	bool resetAll = g_ATCmdLine.FindAndRemoveSwitch(L"resetall");

	const wchar_t *portableAltFile = nullptr;
	g_ATCmdLine.FindAndRemoveSwitch(L"portablealt", portableAltFile);

	const bool portable =  g_ATCmdLine.FindAndRemoveSwitch(L"portable");

	// Check for any switches other than /resetall, /portable, and /portablealt
	g_ATCmdLineHadAnything = g_ATCmdLine.GetCount() > 1;

	VDStringW portableRegPath;
	if (portableAltFile)
		portableRegPath = VDGetFullPath(portableAltFile);
	else
		portableRegPath = VDMakePath(VDGetProgramPath().c_str(), L"Altirra.ini");

	if (portableAltFile || portable || VDDoesPathExist(portableRegPath.c_str())) {
		rpmem = new VDRegistryProviderMemory;
		VDSetRegistryProvider(rpmem);

		if (!resetAll && VDDoesPathExist(portableRegPath.c_str())) {
			try {
				ATUILoadRegistry(portableRegPath.c_str());
			} catch(const MyError& err) {
				VDStringW message;

				message.sprintf(L"There was an error loading the settings file:\n\n%s\n\nDo you want to continue? If so, settings will be reset to defaults and may not be saved.", VDTextAToW(err.c_str()).c_str());
				if (IDYES != MessageBox(NULL, message.c_str(), _T("Altirra Warning"), MB_YESNO | MB_ICONWARNING))
					return 5;
			}
		}

		ATSetFirmwarePathPortabilityMode(true);
	} else {
		if (!resetAll)
			resetAll = ATSettingsIsResetPending();

		if (resetAll)
			SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\virtualdub.org\\Altirra"));
	}

	g_ATRegistryHadAnything = VDRegistryAppKey("", false).isReady();

	int rval = 0;
	const wchar_t *token;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"showfileassocdlg", token)) {
		// We don't _really_ need to do this as Win64 kernel handles are documented as
		// being 32-bit significant, but it's cheap.
		unsigned long long hwndParent;
		wchar_t dummy;

		if (1 == swscanf(token, L"%llx%c", &hwndParent, &dummy))
			ATUIShowDialogSetFileAssociations((VDGUIHandle)(UINT_PTR)hwndParent, false, false);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"removefileassocs", token)) {
		unsigned long long hwndParent;
		wchar_t dummy;

		if (1 == swscanf(token, L"%llx%c", &hwndParent, &dummy))
			ATUIShowDialogRemoveFileAssociations((VDGUIHandle)(UINT_PTR)hwndParent, false, false);
	} else {
		ATOptionsLoad();
		LoadSettingsEarly();

		bool d3d9lock = false;
		if (g_ATCmdLine.FindAndRemoveSwitch(L"lockd3d"))
			d3d9lock = true;

		if (g_ATCmdLine.FindAndRemoveSwitch(L"gdi")) {
			g_ATOptions.mbDirty = true;
			g_ATOptions.mbDisplayDDraw = false;
			g_ATOptions.mbDisplayD3D9 = false;
			g_ATOptions.mbDisplay3D = false;
			g_ATOptions.mbDisplayOpenGL = false;
		} else if (g_ATCmdLine.FindAndRemoveSwitch(L"ddraw")) {
			g_ATOptions.mbDirty = true;
			g_ATOptions.mbDisplayDDraw = true;
			g_ATOptions.mbDisplayD3D9 = false;
			g_ATOptions.mbDisplay3D = false;
			g_ATOptions.mbDisplayOpenGL = false;
		} else if (g_ATCmdLine.FindAndRemoveSwitch(L"opengl")) {
			g_ATOptions.mbDirty = true;
			g_ATOptions.mbDisplayDDraw = false;
			g_ATOptions.mbDisplayD3D9 = false;
			g_ATOptions.mbDisplay3D = false;
			g_ATOptions.mbDisplayOpenGL = true;
		}

		bool singleInstance = g_ATOptions.mbSingleInstance;
		if (g_ATCmdLine.FindAndRemoveSwitch(L"singleinstance") ||
			g_ATCmdLine.FindAndRemoveSwitch(L"si"))
		{
			singleInstance = true;
		}
		else if (g_ATCmdLine.FindAndRemoveSwitch(L"nosingleinstance") ||
			g_ATCmdLine.FindAndRemoveSwitch(L"nosi"))
		{
			singleInstance = false;
		}

		if (!singleInstance || !ATNotifyOtherInstance(g_ATCmdLine)) {
			if (d3d9lock)
				g_d3d9Lock.Lock();

			rval = RunInstance(nCmdShow);
		}
	}

	const bool shouldReset = ATSettingsIsResetPending();

	if (rpmem) {
		try {
			if (shouldReset) {
				VDFile f(portableRegPath.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kTruncateExisting);
			} else {
				ATUISaveRegistry(portableRegPath.c_str());
			}
		} catch(const MyError&) {
		}

		VDSetRegistryProvider(NULL);
		delete rpmem;
	} else {
		if (shouldReset)
			SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\virtualdub.org\\Altirra"));
	}

	OleUninitialize();

	VDShutdownThunkAllocator();

	return rval;
}

int RunInstance(int nCmdShow) {
	VDRegisterVideoDisplayControl();
	VDUIRegisterHotKeyExControl();
	ATUINativeWindow::Register();

	ATInitUIFrameSystem();
	ATUIInitCommandMappings(g_ATUICommandMgr);
	ATUIInitDefaultAccelTables();
	
	VDDialogFrameW32::SetDefaultCaption(L"Altirra");

	struct local {
		static void DisplayUpdateFn(ATOptions& opts, const ATOptions *prevOpts, void *) {
			if (prevOpts) {
				if (prevOpts->mbDisplayDDraw == opts.mbDisplayDDraw
					&& prevOpts->mbDisplayD3D9 == opts.mbDisplayD3D9
					&& prevOpts->mbDisplayOpenGL == opts.mbDisplayOpenGL
					&& prevOpts->mbDisplay3D == opts.mbDisplay3D
					)
					return;
			}

			VDVideoDisplaySetFeatures(true, false, false, opts.mbDisplayOpenGL, opts.mbDisplayD3D9, false, false);
			VDVideoDisplaySetDDrawEnabled(opts.mbDisplayDDraw);
			VDVideoDisplaySet3DEnabled(opts.mbDisplay3D);

			IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
			if (pane)
				pane->ResetDisplay();
		}
	};

	ATOptionsAddUpdateCallback(true, local::DisplayUpdateFn);

	VDVideoDisplaySetDebugInfoEnabled(g_ATCmdLine.FindAndRemoveSwitch(L"displaydebug"));
	VDVideoDisplaySetMonitorSwitchingDXEnabled(true);
	VDVideoDisplaySetSecondaryDXEnabled(true);
	VDVideoDisplaySetD3D9ExEnabled(false);
	VDVideoDisplaySetTermServ3DEnabled(true);

	VDInitFilespecSystem();
	VDLoadFilespecSystemData();

	ATInitUIPanes();
	ATInitDebuggerLogging();
	ATInitProfilerUI();
	ATUIRegisterDisplayPane();
	ATUIInitVirtualKeyMap(g_kbdOpts);
	ATUILoadAccelTables();

	ATUIInitFirmwareMenuCallbacks(g_sim.GetFirmwareManager());
	ATUIInitProfileMenuCallbacks();

	try {
		ATUILoadMenu();
	} catch(const MyError& e) {
		e.post(NULL, "Altirra Error");
	}

	g_hInst = VDGetLocalModuleHandleW32();

	WNDCLASS wc = {};
	wc.lpszClassName = _T("AltirraMainWindow");
	ATOM atom = ATContainerWindow::RegisterCustom(wc);
	ATUICreateMainWindow(&g_pMainWindow);
	HWND hwnd = (HWND)g_pMainWindow->Create(atom, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, false);

	ATUIInitProgressDialog();
	ATUIPushProgressParent((VDGUIHandle)hwnd);

	HICON hIcon = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
	if (hIcon)
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);

	HICON hSmallIcon = (HICON)LoadImage(g_hInst, MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
	if (hSmallIcon)
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);

	ATUIRestoreWindowPlacement(hwnd, "Main window", nCmdShow);

	if (!(GetWindowLong(hwnd, GWL_STYLE) & WS_VISIBLE))
		ShowWindow(hwnd, nCmdShow);

	g_sim.Init(hwnd);
	g_sim.GetInputManager()->SetConsoleCallback(&g_inputConsoleCallback);

	ATRegisterDevices(*g_sim.GetDeviceManager());

	ATInitPortMenus(g_sim.GetInputManager());

	g_winCaptionUpdater.Init(&g_sim, [hwnd](const wchar_t *s) { SetWindowTextW(hwnd, s); } );

	SetWindowText(hwnd, _T("Altirra"));
	g_hwnd = hwnd;

	SetMenu(hwnd, g_hMenu);

	ATUIInitManager();

	// Init Winsock. We want to delay this as much as possible, but it must be done before we
	// attempt to bring up a modem (which LoadSettings or a command prompt can do).
	bool wsaInited = false;

	WSADATA wsa;
	if (!WSAStartup(MAKEWORD(2, 0), &wsa))
		wsaInited = true;

	const bool useHardwareBaseline = g_ATCmdLine.FindAndRemoveSwitch(L"baseline");

	ATInitDebugger();

	if (ATLoadDefaultProfiles())
		ATUIRebuildDynamicMenu(kATUIDynamicMenu_Profile);

	const ATSettingsCategory settingsToLoad = useHardwareBaseline
			? ATSettingsCategory(kATSettingsCategory_All & ~(kATSettingsCategory_FullScreen | kATSettingsCategory_Baseline))
			: ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen);

	const wchar_t *profileName;
	uint32 profileToLoad = kATProfileId_Invalid;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"profile", profileName)) {
		profileToLoad = ATSettingsFindProfileByName(profileName);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"defprofile", profileName)) {
		VDStringSpanW profileNameSpan(profileName);

		if (profileNameSpan == L"xl") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_XL);
		} else if (profileNameSpan == L"xegs") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_XEGS);
		} else if (profileNameSpan == L"800") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_800);
		} else if (profileNameSpan == L"1200xl") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_1200XL);
		} else if (profileNameSpan == L"5200") {
			profileToLoad = ATGetDefaultProfileId(kATDefaultProfile_5200);
		} else {
			MyError("Unknown default profile: %ls", profileName).post(hwnd, "Altirra error");
		}
	}

	if (profileToLoad != kATProfileId_Invalid && ATSettingsIsValidProfile(profileToLoad))
		ATSettingsLoadProfile(profileToLoad, settingsToLoad);
	else
		ATSettingsLoadLastProfile(settingsToLoad);

	if (g_ATCmdLine.FindAndRemoveSwitch(L"tempprofile"))
		ATSettingsSetTemporaryProfileMode(true);

	if (useHardwareBaseline)
		LoadBaselineSettings();

	try {
		g_sim.LoadROMs();
	} catch(MyError& e) {
		e.post(hwnd, "Altirra Error");
	}

	ATOptionsSave();

	g_sim.Resume();
	ATInitEmuErrorHandler((VDGUIHandle)g_hwnd, &g_sim);

	if (!ATRestorePaneLayout(NULL))
		ATActivateUIPane(kATUIPaneId_Display, true);

	if (ATGetUIPane(kATUIPaneId_Display))
		ATActivateUIPane(kATUIPaneId_Display, true);

	g_sim.ColdReset();

	// we can't go full screen until the display panes have been created
	if (!g_ATCmdLine.FindAndRemoveSwitch(L"w"))
		ATLoadSettings(kATSettingsCategory_FullScreen);

	int returnCode = RunMainLoop(hwnd);

	ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));

	ATSetFullscreen(false);

	StopRecording();

	VDSaveFilespecSystemData();

	ATUIShutdownManager();

	ATShutdownEmuErrorHandler();
	ATShutdownDebugger();
	g_sim.Shutdown();

	g_d3d9Lock.Unlock();

	ATUIShutdownProgressDialog();
	ATUIPopProgressParent((VDGUIHandle)g_hwnd, nullptr);

	g_pMainWindow->Destroy();
	g_pMainWindow->Release();
	g_pMainWindow = NULL;
	g_hwnd = NULL;

	ATShutdownPortMenus();

	ATShutdownUIPanes();
	ATShutdownUIFrameSystem();

	if (wsaInited)
		WSACleanup();

	return returnCode;
}
