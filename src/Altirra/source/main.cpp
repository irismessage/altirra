//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2021 Avery Lee
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
#include <time.h>
#include <regex>
#include <mmsystem.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <commctrl.h>
#include <ole2.h>
#include <uxtheme.h>
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
#include <vd2/system/zip.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/VDDisplay/direct3d.h>
#include <vd2/VDDisplay/logging.h>
#include <vd2/Dita/accel.h>
#include <vd2/Dita/services.h>
#include <at/atappbase/crthooks.h>
#include <at/atappbase/exceptionfilter.h>
#include <at/atcore/asyncdispatcher.h>
#include <at/atcore/configvar.h>
#include <at/atcore/constants.h>
#include <at/atcore/checksum.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/media.h>
#include <at/atcore/profile.h>
#include <at/atcore/serializable.h>
#include <at/atdevices/devices.h>
#include <at/atio/atfs.h>
#include <at/atio/cassetteimage.h>
#include <at/atio/image.h>
#include <at/atui/constants.h>
#include <at/atnativeui/acceleditdialog.h>
#include <at/atnativeui/dialog.h>
#include <at/atnativeui/genericdialog.h>
#include <at/atnativeui/hotkeyexcontrol.h>
#include <at/atnativeui/messageloop.h>
#include <at/atnativeui/progress.h>
#include <at/atnativeui/theme.h>
#include <at/atnativeui/uiframe.h>
#include <at/atcore/device.h>
#include <at/atcore/propertyset.h>
#include <at/atdebugger/target.h>
#include "console.h"
#include "simulator.h"
#include "cassette.h"
#include "debugger.h"
#include "debuggerlog.h"
#include "disk.h"
#include "hostdevice.h"
#include "savestate.h"
#include "resource.h"
#include "oshelper.h"
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
#include "compatdb.h"
#include "compatengine.h"
#include "uicompat.h"
#include "trace.h"
#include "mediamanager.h"
#include "savestateio.h"
#include "versioninfo.h"
#include "directorywatcher.h"

#include "firmwaremanager.h"
#include <at/atcore/devicemanager.h>

#pragma comment(lib, "comctl32")
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")
#pragma comment(lib, "comdlg32")
#pragma comment(lib, "advapi32")

void ATUITriggerButtonDown(uint32 vk);
void ATUITriggerButtonUp(uint32 vk);

///////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogInputMappings(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy);
void ATUIShowDialogInputSetup(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy);
void ATUIShowDiskDriveDialog(VDGUIHandle hParent);
int ATUIShowDialogCartridgeMapper(VDGUIHandle h, uint32 cartSize, const void *data);
void ATUIShowDialogLightPen(VDGUIHandle h, ATLightPenPort *lpp);
void ATUIShowDialogCheater(VDGUIHandle hParent, ATCheatEngine *engine);
void ATUIShowDialogDiskExplorer(VDGUIHandle h);
void ATUIShowDialogOptions(VDGUIHandle h);
void ATUIShowDialogAbout(VDGUIHandle h);
bool ATUIShowDialogKeyboardOptions(VDGUIHandle hParent, ATUIKeyboardOptions& opts);
void ATUIShowDialogSetFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly);
void ATUIShowDialogRemoveFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly);
bool ATUIShowDialogVideoEncoding(VDGUIHandle parent, bool hz50, ATVideoEncoding& encoding,
	uint32& videoBitRate,
	uint32& audioBitRate,
	ATVideoRecordingFrameRate& frameRate,
	ATVideoRecordingAspectRatioMode& aspectRatioMode,
	ATVideoRecordingResamplingMode& resamplingMode,
	ATVideoRecordingScalingMode& scalingMode,
	bool& halfRate, bool& encodeAll);
void ATUIShowDialogSetupWizard(VDGUIHandle hParent);
void ATUIShowDialogCheckForUpdates(VDGUIHandle hParent);

void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);
void ATUIMigrateSettings();

void ATRegisterDevices(ATDeviceManager& dm);

void ATInitEmuErrorHandler(VDGUIHandle h, ATSimulator *sim);
void ATShutdownEmuErrorHandler();

bool ATIDEIsPhysicalDiskPath(const wchar_t *path);

void ATUIInitManager();
void ATUIShutdownManager();
void ATUIFlushDisplay();

void ATUIInitFirmwareMenuCallbacks(ATFirmwareManager *fwmgr);
void ATUIInitProfileMenuCallbacks();
void ATUIInitVideoOutputMenuCallback(IATDeviceVideoManager& devMgr);

void ATSaveRegisterTypes();
void ATInitSaveStateDeserializer();

void ATLoadConfigVars();
void ATResetConfigVars();

///////////////////////////////////////////////////////////////////////////////

void ATInitDebugger();
void ATShutdownDebugger();
void ATDebuggerInitAutotestCommands();
void ATInitProfilerUI();
void ATInitUIPanes();
void ATShutdownUIPanes();
void ATShowChangeLog(VDGUIHandle hParent);
void ATUIShowDialogCmdLineHelp(VDGUIHandle hParent);
void ATUIInitCommandMappings(ATUICommandManager& cmdMgr);

void DoLoad(VDGUIHandle h, const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType = kATImageType_None, bool *suppressColdReset = NULL, int loadIndex = -1, bool autoProfile = false);
void DoBootWithConfirm(const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper);

void LoadBaselineSettings();

void ATUICreateMainWindow(ATContainerWindow **);
void ATUILinkMainWindowToSimulator(ATContainerWindow& w);
void ATUIUnlinkMainWindowFromSimulator();

///////////////////////////////////////////////////////////////////////////////

ATConfigVarBool g_ATCVEngineUseWaitableTimer("engine.use_waitable_timer", true);
ATConfigVarInt32 g_ATCVEngineTurboFPSDivisor("engine.turbo_fps_divisor", 16);
ATConfigVarBool g_ATCVEngineAllowDisplayLibraryOverrides = ATConfigVarBool("engine.allow_display_library_overrides", false,
	[] {
		VDDSetLibraryOverridesEnabled(g_ATCVEngineAllowDisplayLibraryOverrides);
	}
);

extern const wchar_t g_wszWarning[]=L"Altirra Warning";

ATLogChannel g_ATLCHostDisp(false, false, "HOSTDISP", "Host display debug output");

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
bool g_ATRegistryTemp;
VDRegistryProviderMemory *g_pATRegistryMemory;
VDStringW g_ATRegistryPathPortable;

ATSimulator g_sim;

bool g_ATAutoFrameFlipping = false;
bool g_fullscreen = false;
bool g_fullscreenDisplay = false;
WINDOWPLACEMENT g_ATWindowPreFSPlacement;
uint32 g_ATWindowPreFSDpi;
bool g_mouseCaptured = false;
bool g_mouseClipped = false;
bool g_mouseAutoCapture = true;
bool g_ATUIPointerAutoHide = true;
bool g_ATUITargetPointerVisible = true;
bool g_pauseInactive = true;
bool g_winActive = true;
bool g_showFps = false;
bool g_xepViewEnabled = false;
bool g_xepViewAutoswitchingEnabled = false;

ATUIKeyboardOptions g_kbdOpts = {
	.mbRawKeys = true,
	.mbFullRawKeys = false,
	.mbAllowInputMapModifierOverlap = true
};

ATDisplayFilterMode g_dispFilterMode = kATDisplayFilterMode_SharpBilinear;
int g_dispFilterSharpness = +1;
int g_enhancedText = 0;
LOGFONTW g_enhancedTextFont;

vdrefptr<IATSerializable> g_pATQuickState;

uint32 g_ATUIBootUnloadStorageMask = 0;

vdautoptr<ATAudioWriter> g_pAudioWriter;
vdautoptr<IATVideoWriter> g_pVideoWriter;
vdautoptr<IATSAPWriter> g_pSapWriter;

ATDisplayStretchMode g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;

IATUIWindowCaptionUpdater *g_winCaptionUpdater;
VDStringA g_winCaptionTemplate;

ATUICommandManager g_ATUICommandMgr;

///////////////////////////////////////////////////////////////////////////////

ATUICommandManager& ATUIGetCommandManager() {
	return g_ATUICommandMgr;
}

VDGUIHandle ATUIGetMainWindow() {
	return (VDGUIHandle)g_hwnd;
}

VDGUIHandle ATUIGetNewPopupOwner() {
	HWND hwnd = GetFocus();

	if (!hwnd)
		hwnd = GetActiveWindow();

	return (VDGUIHandle)GetAncestor(hwnd, GA_ROOT);
}

bool ATUIGetAppActive() {
	return g_winActive;
}

void ATUISetAppActive(bool active) {
	g_winActive = active;
}

void ATUISetWindowCaptionTemplate(const char *s) {
	g_winCaptionTemplate = s;

	if (g_winCaptionUpdater)
		g_winCaptionUpdater->SetTemplate(s);
}

const char *ATUIGetWindowCaptionTemplate() {
	return g_winCaptionTemplate.c_str();
}

void ATUIExit(bool forceNoConfirm) {
	if (!g_hwnd)
		return;

	if (forceNoConfirm)
		DestroyWindow(g_hwnd);
	else
		::SendMessage(g_hwnd, WM_CLOSE, 0, 0);
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
	// SECAM: 1.7815MHz master clock, 312 scanlines of 114 clocks each
	static constexpr double kMasterClocks[3]={
		kATMasterClock_NTSC,
		kATMasterClock_PAL,
		kATMasterClock_SECAM,
	};

	static constexpr double kPeriods[3][3]={
		{ 1.0 / kATFrameRate_NTSC, 1.0f / kATFrameRate_PAL, 1.0f / kATFrameRate_SECAM },
		{ 1.0 / 59.9400, 1.0 / 50.0000, 1.0 / 50.0 },
		{ 1.0 / 60.0000, 1.0 / 50.0000, 1.0 / 50.0 },
	};

	const bool hz50 = (g_sim.GetVideoStandard() != kATVideoStandard_NTSC) && (g_sim.GetVideoStandard() != kATVideoStandard_PAL60);
	const bool isSECAM = (g_sim.GetVideoStandard() == kATVideoStandard_SECAM);
	const int tableIndex = isSECAM ? 2 : hz50 ? 1 : 0;
	double rawSecondsPerFrame = kPeriods[g_frameRateMode][tableIndex];
	
	const double cyclesPerSecond = kMasterClocks[tableIndex] * kPeriods[0][tableIndex] / rawSecondsPerFrame;

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

bool ATUIGetPointerAutoHide() {
	return g_ATUIPointerAutoHide;
}

void ATUISetPointerAutoHide(bool enabled) {
	g_ATUIPointerAutoHide = enabled;
}

bool ATUIGetTargetPointerVisible() {
	return g_ATUITargetPointerVisible;
}

void ATUISetTargetPointerVisible(bool enabled) {
	g_ATUITargetPointerVisible = enabled;
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
				g_ATUICommandMgr.ExecuteCommandNT("System.ColdReset");
			break;
		case kATInputTrigger_WarmReset:
			if (state)
				g_ATUICommandMgr.ExecuteCommandNT("System.WarmReset");
			break;
		case kATInputTrigger_Turbo:
			g_ATUICommandMgr.ExecuteCommandNT(state ? "System.PulseWarpOn" : "System.PulseWarpOff");
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

VDStringW ATUIConfirmDiscardAllStorageGetMessage(const wchar_t *prompt, bool includeUnmountables, uint32 storageTypeMask = ~(uint32)0, bool includeMemory = false) {
	typedef vdfastvector<ATStorageId> DirtyIds;
	DirtyIds dirtyIds;

	typedef vdfastvector<ATDebuggerStorageId> DbgDirtyIds;
	DbgDirtyIds dbgDirtyIds;

	g_sim.GetDirtyStorage(dirtyIds, storageTypeMask);

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
	
	if (includeMemory)
		msg += L"\tContents of emulation memory\n";

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

			case kATStorageId_Tape:
				msg += L"\tTape";
				break;

			case kATStorageId_Firmware:
				switch(unit) {
					case 0:
						msg += L"\tIDE main firmware";
						break;

					case 1:
						msg += L"\tIDE SDX firmware";
						break;

					case 2:
						msg += L"\tUltimate1MB firmware";
						break;

					case 3:
						msg += L"\tRapidus flash firmware";
						break;

					case 4:
						msg += L"\tRapidus PBI firmware";
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

bool ATUIConfirmDiscardMemory(VDGUIHandle h, const wchar_t *title) {
	return ATUIConfirm(h, "DiscardMemory", L"Any unsaved work in emulation memory will be lost. Are you sure?", title);
}

bool ATUIConfirmReset(VDGUIHandle h, const char *key, const wchar_t *message, const wchar_t *title) {
	return g_sim.TimeSinceColdReset() == 0 || ATUIConfirm(h, key, message, title);
}

void ATUIConfirmResetComplete() {
	g_sim.ColdReset();
}

bool ATUIConfirmDiscardAll(VDGUIHandle h, const wchar_t *title, const wchar_t *prompt) {
	const VDStringW& msg = ATUIConfirmDiscardAllStorageGetMessage(prompt, true, ~(uint32)0, true);

	if (msg.empty())
		return ATUIConfirmDiscardMemory(h, title);

	return ATUIConfirm(h, "DiscardStorage", msg.c_str(), title);
}

bool ATUIConfirmDiscardAllStorage(VDGUIHandle h, const wchar_t *prompt, bool includeUnmountables, uint32 storageTypeMask) {
	const VDStringW& msg = ATUIConfirmDiscardAllStorageGetMessage(prompt, includeUnmountables, storageTypeMask);

	if (msg.empty())
		return true;

	return ATUIConfirm(h, "DiscardStorage", msg.c_str(), L"Unsaved Items");
}

vdrefptr<ATUIFutureWithResult<bool> > ATUIConfirmDiscardAllStorage(const wchar_t *prompt, bool includeUnmountables, uint32 storageTypeMask) {
	const VDStringW& msg = ATUIConfirmDiscardAllStorageGetMessage(prompt, includeUnmountables);

	if (msg.empty())
		return vdrefptr<ATUIFutureWithResult<bool> >(new ATUIFutureWithResult<bool>(true));

	if (ATUIGetNativeDialogMode())
		return vdmakerefptr(new ATUIFutureWithResult<bool>(ATUIConfirm(ATUIGetNewPopupOwner(), "DiscardStorage", msg.c_str(), L"Unsaved Items")));

	return ATUIShowAlertWarningConfirm(msg.c_str(), L"Discarding modified storage");
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
		if (!ATSettingsGetBootstrapProfileMode() && h && !ATUIConfirmDiscardAll(h, L"Changing hardware type", L"OK to switch hardware mode and discard everything?"))
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

void DoCompatibilityCheck() {
	vdfastvector<ATCompatKnownTag> tags;
	auto *title = ATCompatCheck(tags);

	if (title) {
		auto result = ATUIShowDialogCompatWarning((VDGUIHandle)g_hwnd, title, tags.data(), tags.size());

		VDTextU8ToW(VDStringSpanA(title->mName.c_str())).c_str();

		if (result == kATUICompatAction_Pause)
			g_sim.Pause();
		else if (result == kATUICompatAction_AutoAdjust)
			ATCompatAdjust((VDGUIHandle)g_hwnd, tags.data(), tags.size());
	}
}

void DoLoadStream(VDGUIHandle h, const wchar_t *origPath, const wchar_t *imageName, IVDRandomAccessStream *stream, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType, bool *suppressColdReset, int loadIndex, bool autoProfile) {
	vdfastvector<uint8> captureBuffer;

	ATCartLoadContext cartctx = {};
	cartctx.mbReturnOnUnknownMapper = true;

	if (cartmapper > 0) {
		cartctx.mbReturnOnUnknownMapper = false;
		cartctx.mCartMapper = cartmapper;
	} else {
		cartctx.mpCaptureBuffer = &captureBuffer;

		if (cartmapper < 0)
			cartctx.mbIgnoreChecksum = true;
	}

	ATStateLoadContext statectx = {};

	ATImageLoadContext ctx;
	ctx.mLoadType = loadType;
	ctx.mLoadIndex = loadIndex;
	ctx.mpCartLoadContext = &cartctx;
	ctx.mpStateLoadContext = &statectx;

	ATMediaLoadContext mctx;
	mctx.mOriginalPath = origPath ? origPath : L"";
	mctx.mImageName = imageName ? imageName : L"";
	mctx.mpStream = stream;
	mctx.mWriteMode = writeMode ? *writeMode : g_ATOptions.mDefaultWriteMode;
	mctx.mbStopOnModeIncompatibility = true;
	mctx.mbStopAfterImageLoaded = true;
	mctx.mbStopOnMemoryConflictBasic = true;
	mctx.mbStopOnIncompatibleDiskFormat = true;
	mctx.mpImageLoadContext = &ctx;

	int safetyCounter = 10;
	for(;;) {
		if (g_sim.Load(mctx))
			break;

		if (!--safetyCounter)
			return;

		if (mctx.mbStopAfterImageLoaded)
			mctx.mbStopAfterImageLoaded = false;

		if (mctx.mbMode5200Required) {
			if (!ATUISwitchHardwareMode5200(h)) {
				if (suppressColdReset)
					*suppressColdReset = true;

				return;
			}

			continue;
		} else if (mctx.mbModeComputerRequired) {
			if (autoProfile) {
				if (!ATUISwitchHardwareMode(h, kATHardwareMode_800XL, true)) {
					if (suppressColdReset)
						*suppressColdReset = true;

					return;
				}
			} else {
				if (!ATUISwitchHardwareModeComputer(h)) {
					if (suppressColdReset)
						*suppressColdReset = true;

					return;
				}
			}

			continue;
		} else if (mctx.mbMemoryConflictBasic) {
			mctx.mbStopOnMemoryConflictBasic = false;

			ATUIGenericDialogOptions opts {};
			opts.mhParent = h;
			opts.mpTitle = L"Memory Conflict";
			opts.mpMessage = L"The program being loaded overlaps internal BASIC and may not work with it enabled. Disable internal BASIC?";
			opts.mpIgnoreTag = "MemoryConflictBasic";
			opts.mIconType = kATUIGenericIconType_Warning;
			opts.mResultMask = kATUIGenericResultMask_YesNoCancel;
			opts.mValidIgnoreMask = kATUIGenericResultMask_Yes | kATUIGenericResultMask_No;
			const auto result = ATUIShowGenericDialogAutoCenter(opts);

			if (result == kATUIGenericResult_Cancel) {
				if (suppressColdReset)
					*suppressColdReset = true;
				return;
			}

			if (result == kATUIGenericResult_Yes)
				g_sim.SetBASICEnabled(false);

			continue;
		} else if (mctx.mbIncompatibleDiskFormat) {
			mctx.mbIncompatibleDiskFormat = false;

			ATUIGenericDialogOptions opts {};
			opts.mhParent = h;
			opts.mpTitle = L"Incompatible Disk Format";
			opts.mpMessage = L"The current disk drive model can't read the type of disk being inserted into it. Mount the disk anyway?";
			opts.mpIgnoreTag = "IncompatibleDiskFormat";
			opts.mIconType = kATUIGenericIconType_Warning;
			opts.mResultMask = kATUIGenericResultMask_OKCancel;
			opts.mValidIgnoreMask = kATUIGenericResultMask_OKCancel;
			const auto result = ATUIShowGenericDialogAutoCenter(opts);

			if (result == kATUIGenericResult_Cancel) {
				if (suppressColdReset)
					*suppressColdReset = true;
				return;
			}

			mctx.mbStopOnIncompatibleDiskFormat = false;
			continue;
		}

		if (ctx.mLoadType == kATImageType_Cartridge) {
			vdfastvector<ATCompatKnownTag> tags;
			ATCompatMarker markers[2];
			size_t numMarkers = 1;

			markers[0] = ATCompatMarker { kATCompatRuleType_CartChecksum, cartctx.mRawImageChecksum };

			if (cartctx.mFileSHA256.has_value())
				markers[numMarkers++] = ATCompatMarker::FromSHA256(kATCompatRuleType_CartFileSHA256, cartctx.mFileSHA256.value());

			ATCompatFindTitle(vdvector_view<ATCompatMarker>(markers, numMarkers), tags, false);

			int mapper = -1;
			for(auto&& tag : tags) {
				switch(tag) {
					case kATCompatKnownTag_Cart52008K:
						mapper = kATCartridgeMode_5200_8K;
						break;

					case kATCompatKnownTag_Cart520016KOneChip:
						mapper = kATCartridgeMode_5200_16K_OneChip;
						break;

					case kATCompatKnownTag_Cart520016KTwoChip:
						mapper = kATCartridgeMode_5200_16K_TwoChip;
						break;

					case kATCompatKnownTag_Cart520032K:
						mapper = kATCartridgeMode_5200_32K;
						break;
				}
			}

			if (mapper < 0) {
				mapper = ATUIShowDialogCartridgeMapper(h, cartctx.mCartSize, captureBuffer.data());
				if (mapper < 0)
					return;
			}

			cartctx.mbReturnOnUnknownMapper = false;
			cartctx.mCartMapper = mapper;
		} else if (ctx.mLoadType == kATImageType_SaveState || ctx.mLoadType == kATImageType_SaveState2) {
			if (statectx.mbKernelMismatchDetected) {
				if (IDOK != MessageBoxW((HWND)h,
					L"The currently loaded kernel ROM image doesn't match the one referenced by the saved state. This may cause the simulated program to fail when resumed. Proceed anyway?",
					L"Altirra Warning", MB_ICONWARNING | MB_OKCANCEL))
					return;

				statectx.mbAllowKernelMismatch = true;
			}
		}
	}

	if (ctx.mLoadType == kATImageType_SaveState) {
		if (!statectx.mbPrivateStateLoaded) {
			MessageBoxW((HWND)h, L"The save state loaded successfully, but detailed emulation state could not be loaded as it was produced by a different program version. Some glitches may appear in the simulation.", L"Altirra Warning", MB_ICONWARNING | MB_OK);
		}

		if (suppressColdReset)
			*suppressColdReset = true;
	} else if (ctx.mLoadType == kATImageType_SaveState2) {
		if (suppressColdReset)
			*suppressColdReset = true;
	}

	DoCompatibilityCheck();
}

void DoLoadStream(VDGUIHandle h, const wchar_t *origPath, const wchar_t *imageName, IVDRandomAccessStream& stream, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType, bool *suppressColdReset, int loadIndex) {
	DoLoadStream(h, origPath, imageName, &stream, writeMode, cartmapper, loadType, suppressColdReset, loadIndex, false);
}

void DoLoad(VDGUIHandle h, const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType, bool *suppressColdReset, int loadIndex, bool autoProfile) {
	DoLoadStream(h, path, path, nullptr, writeMode, cartmapper, loadType, suppressColdReset, loadIndex, autoProfile);
}

uint32 ATUIGetBootUnloadStorageMask() {
	return g_ATUIBootUnloadStorageMask;
}

void ATUISetBootUnloadStorageMask(uint32 mask) {
	g_ATUIBootUnloadStorageMask = mask;
}

void ATUIUnloadStorageForBoot() {
	g_sim.UnloadAll(g_ATUIBootUnloadStorageMask);
}

void ATUIBootImage(const wchar_t *path) {
	bool suppressColdReset = false;
	ATUIUnloadStorageForBoot();

	DoLoad((VDGUIHandle)g_hwnd, path, nullptr, 0);

	if (!suppressColdReset)
		g_sim.ColdReset();
}

void DoBootWithConfirm(const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper) {
	if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?", false, g_ATUIBootUnloadStorageMask))
		return;

	bool suppressColdReset = false;
	try {
		ATUIUnloadStorageForBoot();

		DoLoad((VDGUIHandle)g_hwnd, path, writeMode, cartmapper, kATImageType_None, &suppressColdReset);

		ATAddMRUListItem(path);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}

	if (!suppressColdReset)
		g_sim.ColdReset();
}

void DoBootStreamWithConfirm(const wchar_t *origPath, const wchar_t *imageName, IVDRandomAccessStream& stream, const ATMediaWriteMode *writeMode, int cartmapper) {
	if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?", false, g_ATUIBootUnloadStorageMask))
		return;

	bool suppressColdReset = false;
	try {
		ATUIUnloadStorageForBoot();

		DoLoadStream((VDGUIHandle)g_hwnd, origPath, imageName, stream, writeMode, cartmapper, kATImageType_None, &suppressColdReset, -1);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}

	if (!suppressColdReset)
		g_sim.ColdReset();
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
					mpConfirmResult = ATUIConfirmDiscardAllStorage(L"OK to discard?", false, g_ATUIBootUnloadStorageMask);
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
					L"All supported types\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.xex;*.obx;*.com;*.car;*.rom;*.a52;*.bin;*.cas;*.wav;*.flac;*.zip;*.atz;*.gz;*.bas;*.arc;*.sap\0"
					L"Atari program (*.xex,*.obx,*.com)\0*.xex;*.obx;*.com\0"
					L"BASIC program (*.bas)\0*.bas\0"
					L"Atari disk image (*.atr,*.xfd,*.dcm)\0*.atr;*.xfd;*.dcm;*.arc\0"
					L"Protected disk image (*.pro)\0*.pro\0"
					L"VAPI disk image (*.atx)\0*.atx\0"
					L"Cartridge (*.rom,*.bin,*.a52,*.car)\0*.rom;*.bin;*.a52;*.car\0"
					L"Cassette tape (*.cas,*.wav,*.flac)\0*.cas;*.wav;*.flac\0"
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
						ATUIUnloadStorageForBoot();

					bool suppressColdReset = false;
					DoLoad((VDGUIHandle)g_hwnd, mpFileDialogResult->mPath.c_str(), nullptr, 0, kATImageType_None, &suppressColdReset);

					if (mbColdBoot && !suppressColdReset)
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

bool ATUIGetDisplayFullscreen() {
	return g_fullscreenDisplay;
}

void ATUISaveMainWindowPlacement() {
	if (g_fullscreen) {
		ATUISaveWindowPlacement("Main window",
			vdrect32 {
				g_ATWindowPreFSPlacement.rcNormalPosition.left,
				g_ATWindowPreFSPlacement.rcNormalPosition.top,
				g_ATWindowPreFSPlacement.rcNormalPosition.right,
				g_ATWindowPreFSPlacement.rcNormalPosition.bottom
			},
			g_ATWindowPreFSPlacement.showCmd == SW_MAXIMIZE,
			g_ATWindowPreFSDpi);
	} else {
		ATUISaveWindowPlacement(g_hwnd, "Main window");
	}
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

	bool displayFS = fs && !g_ATOptions.mbFullScreenBorderless;

	ATUISetNativeDialogMode(!displayFS);
	ATUIShowModelessDialogs(!displayFS, g_hwnd);

	if (frame)
		frame->SetFullScreen(fs);

	g_fullscreenDisplay = displayFS;

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

		g_ATWindowPreFSDpi = ATUIGetWindowDpiW32(g_hwnd);

		ATUISetMenuFullScreenHidden(true);

		// We must clear WS_CLIPCHILDREN for D3D9 exclusive fullscreen to work on Windows 10 build 1709 since
		// the top-level window has to be used as the device window and we need the child windows to not be
		// excluded from the window region.
		SetWindowLong(g_hwnd, GWL_STYLE, (style & ~(WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN)) | WS_POPUP);
		
		ShowWindow(g_hwnd, SW_RESTORE);

		if (displayFS) {
			SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
			SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
			BringWindowToTop(g_hwnd);
		} else {
			vdrect32 r { 0, 0, 0, 0 };

			if (g_pDisplay)
				r = g_pDisplay->GetMonitorRect();

			if (!r.empty())
				SetWindowPos(g_hwnd, NULL, r.left, r.top, r.width(), r.height(), SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
			else
				SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOACTIVATE);
		}

		g_fullscreen = true;

		if (displayFS) {
			if (g_pDisplay)
				g_pDisplay->SetFullScreen(true, g_ATOptions.mFullScreenWidth, g_ATOptions.mFullScreenHeight, g_ATOptions.mFullScreenRefreshRate);
			g_sim.SetFrameSkipEnabled(true);
		}
	} else {
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(false);

		SetWindowLong(g_hwnd, GWL_STYLE, (style | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN) & ~WS_POPUP);
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
		ATUISetMenuFullScreenHidden(false);
		g_sim.SetFrameSkipEnabled(true);
	}

	g_winCaptionUpdater->SetFullScreen(g_fullscreen);
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

bool ATUIGetAltViewAutoswitchingEnabled() {
	return g_xepViewAutoswitchingEnabled;
}

void ATUISetAltViewAutoswitchingEnabled(bool enabled) {
	g_xepViewAutoswitchingEnabled = enabled;
}

void OnCommandVideoToggleXEP80Output() {
	ATUIToggleAltOutput("xep80");
}

void OnCommandVideoToggleOutputAutoswitching() {
	g_xepViewAutoswitchingEnabled = !g_xepViewAutoswitchingEnabled;
}

void OnCommandVideoEnhancedTextFontDialog() {
	CHOOSEFONTW cf = {sizeof(CHOOSEFONTW)};

	cf.hwndOwner	= (VDZHWND)ATUIGetNewPopupOwner();
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

namespace {
	constexpr ATEnumEntry<uint8> g_scancodeNameTable[] = {
		{ 0x2C, "tab" },
		{ 0x34, "back" },
		{ 0x34, "backspace" },
		{ 0x34, "bksp" },
		{ 0x0C, "enter" },
		{ 0x0C, "return" },
		{ 0x1C, "esc" },
		{ 0x1C, "escape" },
		{ 0x27, "fuji" },
		{ 0x27, "inv" },
		{ 0x27, "invert" },
		{ 0x11, "help" },
		{ 0x76, "clear" },
		{ 0xB4, "del" },
		{ 0xB4, "delete" },
		{ 0xB7, "ins" },
		{ 0xB7, "insert" },
		{ 0x3C, "caps" },
		{ 0x86, "left" },
		{ 0x87, "right" },
		{ 0x8E, "up" },
		{ 0x8F, "down" },
	};

	constexpr auto g_scancodeNameMap = ATCreateEnumLookupTable(g_scancodeNameTable);
}

void Paste(const wchar_t *s, size_t len, bool useCooldown) {
	vdfastvector<wchar_t> pasteChars;

	while(len--) {
		wchar_t c = *s++;

		if (!c)
			continue;

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

		while(repeat--)
			pasteChars.push_back(c);
	}

	if (g_enhancedText == 2) {
		IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
		if (pane)
			pane->Paste(pasteChars.data(), pasteChars.size());

		return;
	}

	pasteChars.push_back(0);

	auto& pokey = g_sim.GetPokey();
	wchar_t skipLT = 0;
	uint8 scancodeModifier = 0;

	const wchar_t *t = pasteChars.data();

	while(wchar_t c = *t++) {
		if (c == skipLT) {
			skipLT = 0;
			continue;
		}

		skipLT = 0;

		if (c == '{') {
			const wchar_t *start = t;

			while(*t && *t != '}')
				++t;

			if (*t != '}')
				break;

			VDStringSpanW name(start, t);
			++t;

			while(!name.empty()) {
				if (name[0] == L'+') {
					scancodeModifier |= 0x40;
					name = name.subspan(1);
					continue;
				}

				if (name.subspan(0, 6).comparei(L"shift-") == 0 ||
					name.subspan(0, 6).comparei(L"shift+") == 0) {
					scancodeModifier |= 0x40;
					name = name.subspan(6);
					continue;
				}

				if (name[0] == L'^') {
					scancodeModifier |= 0x80;
					name = name.subspan(1);
					continue;
				}

				if (name.subspan(0, 5).comparei(L"ctrl-") == 0 ||
					name.subspan(0, 5).comparei(L"ctrl+") == 0) {
					scancodeModifier |= 0x80;
					name = name.subspan(5);
					continue;
				}

				auto parseResult = ATParseEnum(ATEnumLookupTable { g_scancodeNameMap.mHashEntries, vdcountof(g_scancodeNameMap.mHashEntries), 0}, name);
				if (parseResult.mValid) {
					uint8 scancode = parseResult.mValue;

					if (scancodeModifier)
						scancode = (scancode & 0x3F) | scancodeModifier;

					pokey.PushKey(scancode, false, true, false, useCooldown);
				} else {
					VDStringW err;
					err.sprintf(L"Paste failed: unrecognized token \"%.*ls\"", (int)name.size(), name.data());
					g_sim.GetUIRenderer()->SetStatusMessage(err.c_str());
				}

				scancodeModifier = 0;
				break;
			}

			continue;
		}

		const uint8 kInvalidScancode = 0xFF;
		uint8 scancode = kInvalidScancode;

		switch(c) {
			case L'\r':
			case L'\n':
				skipLT = c ^ (L'\r' ^ L'\n');
				scancode = 0x0C;
				break;

			case L'\t':			// Tab
				scancode = 0x2C;
				break;

			case L'\x001B':		// Esc
				scancode = 0x1C;
				break;

			default:
				if (!ATUIGetDefaultScanCodeForCharacter(c, scancode))
					scancode = kInvalidScancode;

				break;
		}

		if (scancode != kInvalidScancode)
			pokey.PushKey(scancode | scancodeModifier, false, true, false, useCooldown);

		scancodeModifier = 0;
	}
}

void ATUIResizeDisplay() {
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
		g_sim.GetGTIA().RemoveVideoTap(g_pVideoWriter->AsVideoTap());
		g_pVideoWriter->Shutdown();
		g_sim.GetAudioOutput()->SetAudioTap(NULL);
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
		ATUIShowError2((VDGUIHandle)g_hwnd, VDTextAToW(e.c_str()).c_str(), L"Video recording stopped with an error");

		StopVideoRecording();
	}

	try {
		if (g_pAudioWriter)
			g_pAudioWriter->CheckExceptions();
	} catch(const MyError& e) {
		ATUIShowError2((VDGUIHandle)g_hwnd, VDTextAToW(e.c_str()).c_str(), L"Audio recording stopped with an error");

		StopAudioRecording();
	}

	try {
		if (g_pSapWriter)
			g_pSapWriter->CheckExceptions();
	} catch(const MyError& e) {
		ATUIShowError2((VDGUIHandle)g_hwnd, VDTextAToW(e.c_str()).c_str(), L"SAP recording stopped with an error");

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

bool OnTestCommandQuickLoadState() {
	return g_pATQuickState != nullptr;
}

void OnCommandQuickLoadState() {
	try {
		if (g_pATQuickState)
			g_sim.ApplySnapshot(*g_pATQuickState, nullptr);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}
}

void OnCommandQuickSaveState() {
	try {
		g_sim.CreateSnapshot(~g_pATQuickState);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}
}

void OnCommandLoadState() {
	const VDStringW fn(VDGetLoadFileName('save', (VDGUIHandle)g_hwnd, L"Load save state",
		g_ATUIFileFilter_LoadState,
		L"atstate2"
		));

	if (!fn.empty()) {
		DoLoad((VDGUIHandle)g_hwnd, fn.c_str(), nullptr, 0, kATImageType_SaveState);
	}
}

void OnCommandSaveState() {
	const VDStringW fn(VDGetSaveFileName('save', (VDGUIHandle)g_hwnd, L"Save save state",
		g_ATUIFileFilter_SaveState,
		L"atstate2"
		));

	if (!fn.empty()) {
		try {
			g_sim.SaveState(fn.c_str());
		} catch(const MyError& e) {
			ATUIShowError((VDGUIHandle)g_hwnd, e);
		}
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

void OnCommandSaveFirmwareRapidusFlash() {
	OnCommandSaveFirmware(3);
}

void OnCommandExit() {
	ATUIExit(false);
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

		ATUIResizeDisplay();
	}
}

void ATUISetOverscanMode(ATGTIAEmulator::OverscanMode mode) {
	g_sim.GetGTIA().SetOverscanMode(mode);
	ATUIResizeDisplay();
}

void OnCommandViewVerticalOverscan(ATGTIAEmulator::VerticalOverscanMode mode) {
	g_sim.GetGTIA().SetVerticalOverscanMode(mode);
	ATUIResizeDisplay();
}

void OnCommandViewTogglePALExtended() {
	g_sim.GetGTIA().SetOverscanPALExtended(!g_sim.GetGTIA().IsOverscanPALExtended());
	ATUIResizeDisplay();
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
	g_winCaptionUpdater->SetShowFps(enabled);

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
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->CopyFrame(false);
}

void OnCommandEditCopyFrameTrueAspect() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->CopyFrame(true);
}

void OnCommandEditSaveFrame() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->SaveFrame(false);
}

bool ATUISaveFrame(const wchar_t *path) {
	if (!g_sim.GetGTIA().IsLastFrameBufferAvailable())
		return false;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (!pane)
		return false;

	pane->SaveFrame(false, path);
	return true;
}

void OnCommandEditSaveFrameTrueAspect() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->SaveFrame(true);
}

void OnCommandEditCopyText() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->Copy(false);
}

void OnCommandEditCopyEscapedText() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->Copy(true);
}

void OnCommandEditPasteText() {
	VDStringA s8;
	VDStringW s16;
	bool use16;

	if (ATUIClipGetText(s8, s16, use16)) {
		if (use16)
			Paste(s16.data(), s16.size(), true);
		else {
			const VDStringW& s16c = VDTextAToW(s8);
			Paste(s16c.data(), s16c.size(), true);
		}
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandConsoleHoldKeys() {
	ATUIToggleHoldKeys();
}

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

void OnCommandConsoleIndusGTId() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false)) {
		p->ActivateButton(kATDeviceButton_IndusGTId, true);
		p->ActivateButton(kATDeviceButton_IndusGTId, false);
	}
}

void OnCommandConsoleIndusGTError() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false)) {
		p->ActivateButton(kATDeviceButton_IndusGTError, true);
		p->ActivateButton(kATDeviceButton_IndusGTError, false);
	}
}

void OnCommandConsoleIndusGTTrack() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false)) {
		p->ActivateButton(kATDeviceButton_IndusGTTrack, true);
		p->ActivateButton(kATDeviceButton_IndusGTTrack, false);
	}
}

void OnCommandConsoleIndusGTBootCPM() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_IndusGTBootCPM, true);
}

void OnCommandConsoleIndusGTChangeDensity() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_IndusGTChangeDensity, true);
}

void OnCommandConsoleHappyToggleFastSlow() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_HappySlow, !p->IsButtonDepressed(kATDeviceButton_HappySlow));
}

void OnCommandConsoleHappyToggleWriteProtect() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_HappyWPEnable, !p->IsButtonDepressed(kATDeviceButton_HappyWPEnable));
}

void OnCommandConsoleHappyToggleWriteEnable() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false))
		p->ActivateButton(kATDeviceButton_HappyWPDisable, !p->IsButtonDepressed(kATDeviceButton_HappyWPDisable));
}

void OnCommandConsoleATR8000Reset() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false)) {
		p->ActivateButton(kATDeviceButton_ATR8000Reset, true);
		p->ActivateButton(kATDeviceButton_ATR8000Reset, false);
	}
}

void OnCommandConsoleXELCFSwap() {
	for(IATDeviceButtons *p : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceButtons>(false, false)) {
		p->ActivateButton(kATDeviceButton_XELCFSwap, true);
		p->ActivateButton(kATDeviceButton_XELCFSwap, false);
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandDiskDrivesDialog() {
	ATUIShowDiskDriveDialog((VDGUIHandle)g_hwnd);
}

void OnCommandDiskToggleSIOPatch() {
	g_sim.SetDiskSIOPatchEnabled(!g_sim.IsDiskSIOPatchEnabled());
}

void OnCommandDiskToggleSIOOverrideDetection() {
	g_sim.SetDiskSIOOverrideDetectEnabled(!g_sim.IsDiskSIOOverrideDetectEnabled());
}

void OnCommandDiskToggleAccurateSectorTiming() {
	g_sim.SetDiskAccurateTimingEnabled(!g_sim.IsDiskAccurateTimingEnabled());
}

bool ATUIGetDriveSoundsEnabled() {
	const ATDiskInterface& diskIf = g_sim.GetDiskInterface(0);
	return diskIf.AreDriveSoundsEnabled();
}

void ATUISetDriveSoundsEnabled(bool enabled) {
	for(int i=0; i<15; ++i) {
		ATDiskInterface& diskIf = g_sim.GetDiskInterface(i);
		diskIf.SetDriveSoundsEnabled(enabled);
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

					msg.sprintf(L"Modified disk image in D%u: has not been saved. OK to discard it?", mIndex + 1);

					mpConfirmResult = ATUIShowAlertWarningConfirm(msg.c_str(), L"Discarding modified storage");
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
					DoLoad((VDGUIHandle)g_hwnd, mpFileDialogResult->mPath.c_str(), nullptr, 0, kATImageType_Disk, NULL, mIndex);

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
						if (g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + i))) {
							dirtyDisks = true;
							break;
						}
					}

					if (dirtyDisks) {
						mpConfirmResult = ATUIConfirmDiscardAllStorage(L"OK to discard?", false, kATStorageTypeMask_Disk);
						Wait(mpConfirmResult);
					}
				} else {
					if (g_sim.IsStorageDirty(ATStorageId(kATStorageId_Disk + mIndex))) {
						VDStringW msg;

						msg.sprintf(L"Modified disk image in D%u: has not been saved. Discard it to mount a new image?", mIndex + 1);

						mpConfirmResult = ATUIShowAlertWarningConfirm(msg.c_str(), L"Discarding modified storage");
						Wait(mpConfirmResult);
					}
				}
				++mStage;
				break;

			case 1:
				if (mpConfirmResult && !mpConfirmResult->GetResult()) {
					MarkCompleted();
					break;
				}

				if (mIndex < 0) {
					for(int i=0; i<15; ++i) {
						ATDiskInterface& diskIf = g_sim.GetDiskInterface(i);
						diskIf.UnloadDisk();

						ATDiskEmulator& drive = g_sim.GetDiskDrive(i);
						drive.SetEnabled(false);
					}
				} else {
					ATDiskInterface& diskIf = g_sim.GetDiskInterface(mIndex);
					diskIf.UnloadDisk();

					ATDiskEmulator& drive = g_sim.GetDiskDrive(mIndex);
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
		if (g_sim.GetDiskDrive(i).IsEnabled() || g_sim.GetDiskInterface(i).GetClientCount() > 1) {
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
		uir->SetStatusMessage(VDStringW().sprintf(L"Rotated to D1: %ls", g_sim.GetDiskInterface(0).GetMountedImageLabel().c_str()).c_str());
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
	ATUIShowDialogInputMappings(g_hwnd, *g_sim.GetInputManager(), g_sim.GetJoystickManager());
	ATReloadPortMenus();
}

void OnCommandInputInputSetupDialog() {
	ATUIShowDialogInputSetup(g_hwnd, *g_sim.GetInputManager(), g_sim.GetJoystickManager());
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
	if (g_pAudioWriter || g_pVideoWriter || g_pSapWriter)
		return;

	const bool hz50 = g_sim.GetVideoStandard() != kATVideoStandard_NTSC && g_sim.GetVideoStandard() != kATVideoStandard_PAL60;

	ATVideoEncoding encoding;
	ATVideoRecordingFrameRate frameRateOption;
	ATVideoRecordingAspectRatioMode aspectRatioMode;
	ATVideoRecordingResamplingMode resamplingMode;
	ATVideoRecordingScalingMode scalingMode;
	uint32 videoBitRate;
	uint32 audioBitRate;
	bool halfRate;
	bool encodeAll;
	if (!ATUIShowDialogVideoEncoding((VDGUIHandle)g_hwnd, hz50, encoding, videoBitRate, audioBitRate, frameRateOption, aspectRatioMode, resamplingMode, scalingMode, halfRate, encodeAll))
		return;

	VDStringW s;
	
	switch(encoding) {
		case kATVideoEncoding_Raw:
		case kATVideoEncoding_RLE:
		case kATVideoEncoding_ZMBV:
			s = VDGetSaveFileName('rvid', (VDGUIHandle)g_hwnd, L"Record raw video", L"Audio/Visual Interleaved (*.avi)\0*.avi\0", L"avi");
			break;

		case kATVideoEncoding_WMV7:
		case kATVideoEncoding_WMV9:
			s = VDGetSaveFileName('rvid', (VDGUIHandle)g_hwnd, L"Record raw video", L"Windows Media Video (*.wmv)\0*.wmv\0", L"wmv");
			break;

		case kATVideoEncoding_H264_AAC:
		case kATVideoEncoding_H264_MP3:
			s = VDGetSaveFileName('rvid', (VDGUIHandle)g_hwnd, L"Record raw video", L"MPEG-4/AVC (*.mp4)\0*.mp4\0", L"mp4");
			break;
	}

	if (s.empty())
		return;

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

		int px = 2;
		int py = 2;
		gtia.GetPixelAspectMultiple(px, py);

		const bool pal = g_sim.GetVideoStandard() != kATVideoStandard_NTSC && g_sim.GetVideoStandard() != kATVideoStandard_PAL60;
		double par = 1.0f;
		
		if (aspectRatioMode != ATVideoRecordingAspectRatioMode::None) {
			par = (double)py / (double)px;
			
			if (aspectRatioMode == ATVideoRecordingAspectRatioMode::FullCorrection)
				par *= (hz50 ? 1.03964f : 0.857141f);
		}

		g_pVideoWriter->Init(s.c_str(), encoding, videoBitRate, audioBitRate, w, h, frameRate, par, resamplingMode, scalingMode, rgb32 ? NULL : palette, samplingRate, g_sim.IsDualPokeysEnabled(), hz50 ? 1773447.0f : 1789772.5f, halfRate, encodeAll, g_sim.GetUIRenderer());

		g_sim.GetAudioOutput()->SetAudioTap(g_pVideoWriter->AsAudioTap());
		gtia.AddVideoTap(g_pVideoWriter->AsVideoTap());
	} catch(const MyError& e) {
		StopRecording();
		ATUIShowError2(ATUIGetNewPopupOwner(), VDTextAToW(e.c_str()).c_str(), L"Video recording failed");
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

void ATUIShowDialogEditAccelerators(const char *preSelectedCommand) {
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
		ATUIGetAccelTables(), ATUIGetDefaultAccelTables(), kATUIAccelContextCount, kContextNames, preSelectedCommand))
	{
		ATUISaveAccelTables();
		ATUILoadMenu();
	}
}

void OnCommandToolsKeyboardShortcutsDialog() {
	ATUIShowDialogEditAccelerators(nullptr);
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

void OnCommandHelpCmdLine() {
	ATUIShowDialogCmdLineHelp((VDGUIHandle)g_hwnd);
}

void OnCommandHelpOnline() {
	ATLaunchURL(AT_PRIMARY_URL);
}

void OnCommandHelpCheckForUpdates() {
	ATUIShowDialogCheckForUpdates((VDGUIHandle)g_hwnd);
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

class ATTimeArgumentParseException final : public MyError {
public:
	ATTimeArgumentParseException() : MyError("Invalid time argument format.") {}
};

float ATParseTimeArgument(const wchar_t *s) {
	// Formats we support:

	// [[hh:]mm:]ss.sss
	const std::wregex timeRegex1(LR"--((?:(?:([0-9]+)\:|)([0-9]+)\:|)([0-9]+(?:|\.[0-9]+)))--", std::regex_constants::icase);

	// [00h][00m][00.000s]
	const std::wregex timeRegex2(LR"--((?:([0-9]+)h|)(?:([0-9]+)m|)(?:([0-9]+(?:|\.[0-9]+))s|))--", std::regex_constants::icase);

	float t = 0;

	std::match_results<const wchar_t *> m;
	if (!std::regex_match(s, m, timeRegex1) && !std::regex_match(s, m, timeRegex2))
		throw ATTimeArgumentParseException();

	if (m[1].matched)
		t += (float)wcstol(m[1].str().c_str(), nullptr, 10) * 3600.0f;

	if (m[2].matched)
		t += (float)wcstol(m[2].str().c_str(), nullptr, 10) * 60.0f;

	if (m[3].matched)
		t += (float)wcstod(m[3].str().c_str(), nullptr);

	return t;
}

void ReadCommandLine(HWND hwnd, VDCommandLine& cmdLine) {
	bool coldReset = false;
	bool debugMode = false;
	bool debugModeSuspend = false;
	const ATMediaWriteMode *bootWriteMode = nullptr;
	bool unloaded = false;
	VDStringW keysToType;
	int cartmapper = 0;

	if (cmdLine.FindAndRemoveSwitch(L"crash")) {
		volatile char *volatile ptr = nullptr;
		*ptr = 0;
	}

	bool autoProfile = cmdLine.FindAndRemoveSwitch(L"autoprofile");

	if (cmdLine.FindAndRemoveSwitch(L"launch")) {
		if (g_ATOptions.mbLaunchAutoProfile)
			autoProfile = true;
	}

	if (cmdLine.FindAndRemoveSwitch(L"noautoprofile"))
		autoProfile = false;

	ATSettingsSetBootstrapProfileMode(autoProfile);

	float tapePos = 0;

	try {
		// This is normally intercepted early. If we got here, it was because of
		// another instance.
		if (cmdLine.FindAndRemoveSwitch(L"baseline")) {
			LoadBaselineSettings();
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"autotest")) {
			ATDebuggerInitAutotestCommands();
		}

		if (cmdLine.FindAndRemoveSwitch(L"borderless")) {
			g_ATOptions.mbFullScreenBorderless = true;
			g_ATOptions.mbDirty = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"noborderless")) {
			g_ATOptions.mbFullScreenBorderless = false;
			g_ATOptions.mbDirty = true;
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
			g_sim.SetDiskAccurateTimingEnabled(true);
		} else if (cmdLine.FindAndRemoveSwitch(L"noaccuratedisk")) {
			g_sim.SetDiskAccurateTimingEnabled(false);
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
				g_sim.SetKernel(kATFirmwareId_Kernel_LLE);
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
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None);
			else if (!vdwcsicmp(arg, L"ntsc"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSC);
			else if (!vdwcsicmp(arg, L"ntschi"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSCHi);
			else if (!vdwcsicmp(arg, L"pal"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PAL);
			else if (!vdwcsicmp(arg, L"palhi"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PALHi);
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
			keysToType += arg;
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

		if (cmdLine.FindAndRemoveSwitch(L"nocartchecksum")) {
			cartmapper = -1;
		} else if (cmdLine.FindAndRemoveSwitch(L"cartmapper", arg)) {
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

		while(cmdLine.FindAndRemoveSwitch(L"cart", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATImageType_Cartridge, nullptr, -1, autoProfile);
			coldReset = true;// required to set up cassette autoboot
		}

		int diskIndex = 0;
		while(cmdLine.FindAndRemoveSwitch(L"disk", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATImageType_Disk, nullptr, diskIndex++, autoProfile);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"run", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATImageType_Program, nullptr, -1, autoProfile);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"runbas", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATImageType_BasicProgram, nullptr, -1, autoProfile);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"tape", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATImageType_Tape, nullptr, -1, autoProfile);
			coldReset = true;// required to set up cassette autoboot
		}

		if (cmdLine.FindAndRemoveSwitch(L"tapepos", arg))
			tapePos = ATParseTimeArgument(arg);

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

			DoLoad((VDGUIHandle)hwnd, arg, bootWriteMode, cartmapper, kATImageType_None, &suppressColdReset, -1, autoProfile);

			VDSetLastLoadSavePath('load', VDGetFullPath(arg).c_str());

			coldReset = !suppressColdReset; // required to set up cassette autoboot
		}

	} catch(const MyError& e) {
		ATUIShowError2((VDGUIHandle)hwnd, VDTextAToW(e.c_str()).c_str(), L"Command-line error");
	}

	ATSettingsSetBootstrapProfileMode(false);

	if (coldReset)
		g_sim.ColdReset();

	if (tapePos > 0) {
		g_sim.GetCassette().SeekToTime(tapePos);
	}

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
				ATUIShowDialogSetupWizard(ATUIGetNewPopupOwner());
			}
		}
	}

#ifndef VD_CPU_ARM64
	// check if we are running emulated on ARM64, and pop up warning if so
	auto pIsWow64Process2 = (BOOL (WINAPI *)(HANDLE, USHORT *, USHORT *))GetProcAddress(GetModuleHandle(_T("kernel32")), "IsWow64Process2");

	USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
	USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
	if (pIsWow64Process2 && pIsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)) {
		// We ignore processMachine as it is UNKNOWN for AMD64 on ARM64, since technically there is no
		// WOW64 layer involved.
		if (nativeMachine == IMAGE_FILE_MACHINE_ARM64) {
			ATUIGenericDialogOptions opts;
			opts.mhParent = ATUIGetNewPopupOwner();
			opts.mpTitle = L"Native ARM64 version available";
			opts.mpMessage = L"This version of the program is running in emulation on this computer. A native ARM64 version is available that would run faster.\n\n"
				L"Would you like to visit the download page?"
				;
			opts.mpIgnoreTag = "ARM64Emu";
			opts.mIconType = kATUIGenericIconType_Info;
			opts.mAspectLimit = 4.0f;
			opts.mResultMask = kATUIGenericResultMask_OKCancel;
			opts.mValidIgnoreMask = kATUIGenericResultMask_Cancel;
			if (ATUIShowGenericDialogAutoCenter(opts) == kATUIGenericResult_OK) {
				ATLaunchURL(AT_DOWNLOAD_URL);
			}
		}
	}
#endif
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
	auto *jm = g_sim.GetJoystickManager();
	if (!jm)
		return;

	switch(jm->Poll()) {
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

void ATInitJoysticks() {
	vdautoptr<IATJoystickManager> jm { ATCreateJoystickManager() };

	if (!jm->Init(g_hwnd, g_sim.GetInputManager()))
		return;

	jm->SetOnActivity(ATOnJoystickActivity);

	g_sim.SetJoystickManager(jm.release());
}

void ATShutdownJoysticks() {
	IATJoystickManager *jm = g_sim.GetJoystickManager();

	if (jm) {
		g_sim.SetJoystickManager(nullptr);

		delete jm;
	}
}

int RunMainLoop2(HWND hwnd) {
	int ticks = 0;

	ATAnticEmulator& antic = g_sim.GetAntic();
	uint32 lastFrame = antic.GetPresentedFrameCounter();
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

	g_winCaptionUpdater->Update(true, 0, 0, 0);

	int rcode = 0;
	bool lastIsRunning = false;

	HANDLE hTimer = nullptr;

	if (g_ATCVEngineUseWaitableTimer)
		hTimer = CreateWaitableTimer(nullptr, FALSE, nullptr);

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
			uint64 curTime;
			bool curTimeValid = false;

			g_ATAutoFrameFlipping = true;

			uint32 frame = antic.GetPresentedFrameCounter();
			if (frame != lastFrame) {
				CheckRecordingExceptions();

				IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateTextDisplay(g_enhancedText != 0);

				lastFrame = frame;

				curTime = VDGetPreciseTick();
				curTimeValid = true;

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
						g_winCaptionUpdater->Update(true, ticks, fps, cpu);

					lastStatusTime = curTime;
					nextTickUpdate = ticks - ticks % 60 + 60;
				}

				sint64 lastFrameDuration = curTime - lastTime;
				lastTime = curTime;

				error += lastFrameDuration - g_frameTicks;
				frameTimeErrorAccum += g_frameSubTicks;

				if (frameTimeErrorAccum >= 0x10000) {
					frameTimeErrorAccum &= 0xFFFF;

					--error;
				}

				if (error > g_frameErrorBound || error < -g_frameErrorBound)
					error = -g_frameTicks;

				nextFrameTimeValid = false;
				if (g_sim.IsTurboModeEnabled())
					error = 0;
				else if (error < 0) {
					nextFrameTimeValid = true;
					nextFrameTime = curTime - error;

					if (hTimer) {
						int timerTicks = -(sint64)(-error * 10000000 / secondTime);

						if (timerTicks >= 0)
							timerTicks = -1;

						LARGE_INTEGER deadline { .QuadPart = timerTicks };
						SetWaitableTimerEx(hTimer, &deadline, 0, nullptr, nullptr, nullptr, 0);
					}
				}
			}

			bool dropFrame = g_sim.IsTurboModeEnabled() && (lastFrame % std::clamp<uint32>(g_ATCVEngineTurboFPSDivisor, 1, 100)) != 0;
			if (nextFrameTimeValid) {
				if (hTimer) {
					ATProfileBeginRegion(kATProfileRegion_Idle);
					bool timerElapsed = ::MsgWaitForMultipleObjects(1, &hTimer, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0;
					ATProfileEndRegion(kATProfileRegion_Idle);

					if (!timerElapsed)
						return true;

					nextFrameTimeValid = false;
				} else {
					if (!curTimeValid) {
						curTime = VDGetPreciseTick();
						curTimeValid = true;
					}

					sint64 ticksToNextFrame = nextFrameTime - curTime;

					if (ticksToNextFrame <= 0 || ticksToNextFrame > g_frameTimeout) {
						// we're already at or past next frame, or it's way off (>1 second);
						// just render
						nextFrameTimeValid = false;
					} else {
						int msToDelay = (int)(ticksToNextFrame * 1000 / secondTime);

						if (msToDelay <= 0) {
							// sub-1ms delay not possible, so just render frame
							nextFrameTimeValid = false;
						} else if (g_sim.IsTurboModeEnabled()) {
							// turbo mode, render frame but allow frame drop
							dropFrame = true;
						} else {
							// delay to next frame
							ATProfileBeginRegion(kATProfileRegion_Idle);
							::MsgWaitForMultipleObjects(0, NULL, FALSE, msToDelay, QS_ALLINPUT);
							ATProfileEndRegion(kATProfileRegion_Idle);
							return true;
						}
					}
				}
			}

			if (g_sim.GetAntic().GetBeamY() == 248)
				ATProfileMarkEvent(kATProfileEvent_BeginFrame);

			ATProfileBeginRegion(kATProfileRegion_Simulation);
			ATSimulator::AdvanceResult ar = g_sim.Advance(dropFrame);
			ATProfileEndRegion(kATProfileRegion_Simulation);

			if (ar == ATSimulator::kAdvanceResult_Stopped) {
				updateScreenPending = !g_sim.IsPaused();
			} else {
				updateScreenPending = false;

				if (ar == ATSimulator::kAdvanceResult_WaitingForFrame) {
					ATProfileBeginRegion(kATProfileRegion_IdleFrameDelay);
					::MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT);
					ATProfileEndRegion(kATProfileRegion_IdleFrameDelay);
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

			g_winCaptionUpdater->Update(false, 0, 0, 0);
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

	if (hTimer)
		CloseHandle(hTimer);

	if (lastIsRunning) {
		ATEndIdleJoystickPoll();
		timeEndPeriod(1);
	}

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

int RunInstance(int nCmdShow, class ATStartupLogger&, ATSettingsCategory categoriesToIgnore);

void ATUISetCommandLine(const wchar_t *s) {
	g_ATCmdLine.Init(s);
	g_ATCmdLineRead = false;
}

bool ATInitRegistry(ATSettingsCategory& categoriesToIgnore) {
	// setup registry
	VDRegistryAppKey::setDefaultKey("Software\\virtualdub.org\\Altirra\\");

	bool resetAll = g_ATCmdLine.FindAndRemoveSwitch(L"resetall");

	const wchar_t *portableAltFile = nullptr;
	g_ATCmdLine.FindAndRemoveSwitch(L"portablealt", portableAltFile);

	g_ATRegistryTemp = g_ATCmdLine.FindAndRemoveSwitch(L"portabletemp");
	const bool portable = g_ATRegistryTemp || g_ATCmdLine.FindAndRemoveSwitch(L"portable");

	// Check for any switches other than /resetall, /portable, and /portablealt
	g_ATCmdLineHadAnything = g_ATCmdLine.GetCount() > 1;

	if (portableAltFile)
		g_ATRegistryPathPortable = VDGetFullPath(portableAltFile);
	else
		g_ATRegistryPathPortable = ATSettingsGetDefaultPortablePath();

	if (portableAltFile || portable || VDDoesPathExist(g_ATRegistryPathPortable.c_str())) {
		g_pATRegistryMemory = new VDRegistryProviderMemory;
		VDSetRegistryProvider(g_pATRegistryMemory);

		ATSettingsSetInPortableMode(true);

		if (!resetAll && !g_ATRegistryTemp && VDDoesPathExist(g_ATRegistryPathPortable.c_str())) {
			try {
				ATUILoadRegistry(g_ATRegistryPathPortable.c_str());
			} catch(const MyError& err) {
				VDStringW message;

				message.sprintf(L"There was an error loading the settings file:\n\n%s\n\nDo you want to continue? If so, settings will be reset to defaults and may not be saved.", VDTextAToW(err.c_str()).c_str());
				if (IDYES != MessageBox(NULL, message.c_str(), _T("Altirra Warning"), MB_YESNO | MB_ICONWARNING))
					return false;
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

	const wchar_t *resetTags = nullptr;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"reset", resetTags)) {
		VDStringRefW resetTagsRef(resetTags);
		VDStringRefW resetToken;

		for(;;) {
			bool didSplit = resetTagsRef.split(L',', resetToken);

			if (!didSplit)
				resetToken = resetTagsRef;

			if (resetToken == L"windowlayouts") {
				VDRegistryAppKey appKey;
				appKey.removeKeyRecursive("Pane layouts 2");
			} else if (resetToken == L"display") {
				VDRegistryAppKey appKey("Settings");
				vdvector<VDStringA> valuesToDelete;

				VDRegistryValueIterator it(appKey);

				while(const char *valueName = it.Next()) {
					if (!vdstricmp(valueName, "Display:", 8))
						valuesToDelete.emplace_back(valueName);
				}

				for(const VDStringA& valueName : valuesToDelete)
					appKey.removeValue(valueName.c_str());
			} else if (resetToken == L"advconfig") {
				ATResetConfigVars();
			} else if (resetToken == L"devices") {
				categoriesToIgnore = ATSettingsCategory(categoriesToIgnore | kATSettingsCategory_Devices);
			} else {
				VDStringW msg;
				msg.sprintf(L"Unknown settings type to reset: '%*ls'", (int)resetToken.size(), resetToken.data());

				MessageBoxW(nullptr, msg.c_str(), L"Altirra Warning", MB_OK | MB_ICONWARNING);
			}

			if (!didSplit)
				break;
		}
	}

	return true;
}

void ATShutdownRegistry() {
	bool migrated = false;

	if (ATSettingsIsMigrationScheduled()) {
		ATUIMigrateSettings();
		migrated = true;
	}

	const bool shouldReset = ATSettingsIsResetPending();

	if (g_pATRegistryMemory) {
		if (migrated) {
			// if we migrated, we should delete
			VDRemoveFile(g_ATRegistryPathPortable.c_str());
		} else if (!g_ATRegistryTemp) {
			try {
				if (shouldReset) {
					// reset pending -- truncate the file instead of saving
					VDFile f(g_ATRegistryPathPortable.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kTruncateExisting);
				} else {
					// no reset or migration -- save registry
					ATUISaveRegistry(g_ATRegistryPathPortable.c_str());
				}
			} catch(const MyError&) {
			}
		}

		VDSetRegistryProvider(nullptr);
		delete g_pATRegistryMemory;
		g_pATRegistryMemory = nullptr;
	} else {
		if (shouldReset || migrated)
			SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\virtualdub.org\\Altirra"));
	}
}

void ATInitCPUOptions() {
	uint32 cpuext = CPUCheckForExtensions();

	const wchar_t *token;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"hostcpu", token)) {
#if VD_CPU_X86 || VD_CPU_X64
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
		else if (!vdwcsicmp(token, L"sse42"))
			cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SSE41
					| CPUF_SUPPORTS_CLMUL;
		else if (!vdwcsicmp(token, L"avx"))
			cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SSE41
					| CPUF_SUPPORTS_CLMUL | CPUF_SUPPORTS_AVX;
		else if (!vdwcsicmp(token, L"avx2"))
			cpuext &= CPUF_SUPPORTS_CPUID | CPUF_SUPPORTS_FPU | CPUF_SUPPORTS_MMX | CPUF_SUPPORTS_INTEGER_SSE | CPUF_SUPPORTS_SSE
					| CPUF_SUPPORTS_SSE2 | CPUF_SUPPORTS_SSE3 | CPUF_SUPPORTS_SSSE3 | CPUF_SUPPORTS_SSE41
					| CPUF_SUPPORTS_CLMUL | CPUF_SUPPORTS_LZCNT | CPUF_SUPPORTS_AVX | CPUF_SUPPORTS_AVX2;
#endif
	}

	CPUEnableExtensions(cpuext);
	VDFastMemcpyAutodetect();
}

void ATInitDisplayOptions() {
	if (g_ATCmdLine.FindAndRemoveSwitch(L"gdi")) {
		g_ATOptions.mbDirty = true;
		g_ATOptions.mbDisplayD3D9 = false;
		g_ATOptions.mbDisplay3D = false;
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"d3d9")) {
		g_ATOptions.mbDirty = true;
		g_ATOptions.mbDisplayD3D9 = true;
		g_ATOptions.mbDisplay3D = false;
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"d3d11")) {
		g_ATOptions.mbDirty = true;
		g_ATOptions.mbDisplayD3D9 = false;
		g_ATOptions.mbDisplay3D = true;
	}
}

void ATInitRand() {
	// Visual C++'s rand() is a 48-bit LCG with a factor of 214013 and returning
	// the high 16-bits, which unfortunately means that the output of the first
	// call to rand() highly tracks the argument to srand() x3. This makes
	// srand(time()) unacceptable, even for our minimal requirements. To fix
	// this, we mix in the system uptime, thread ID, and process ID.

	const struct ChecksumData {
		time_t t;
		uint32 pid;
		uint32 tid;
	} data = {
		time(nullptr),
		VDGetCurrentProcessId(),
		VDGetCurrentThreadID()
	};

	const uint64 hash = ATComputeBlockChecksum(kATBaseChecksum, &data, sizeof data);

	uint32 hash32 = (uint32)(hash ^ (hash >> 32));
	srand((unsigned)hash32);
}

///////////////////////////////////////////////////////////////////////////

class ATStartupLogger {
public:
	~ATStartupLogger() {
		sCrashLogger = nullptr;
		sHookLogger = nullptr;
	}

	void Init(const wchar_t *channels) {
		mbEnabled = true;

		// attach to parent console if we can, otherwise allocate a new one
		if (!AttachConsole(ATTACH_PARENT_PROCESS))
			AllocConsole();

		DWORD actual;
		WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "\r\n", 2, &actual, nullptr);

		mStartTick = VDGetPreciseTick();
		mLastMsgTick = mStartTick;

		ATSetExceptionPreFilter(ExceptionPreFilter);

		sCrashLogger = this;

		OSVERSIONINFOW osinfo { sizeof(OSVERSIONINFOW) };
#pragma warning(push)
#pragma warning(disable:4996)
		if (GetVersionExW(&osinfo)) {
#pragma warning(pop)
			VDStringA s;
			s.sprintf("Windows %u.%u.%u"
				, osinfo.dwMajorVersion
				, osinfo.dwMinorVersion
				, osinfo.dwBuildNumber
			);

			Log(s.c_str());
		}

		if (channels && *channels) {
			VDStringRefW s(channels);
			VDStringA channelName;

			for(;;) {
				VDStringRefW token;
				bool last = !s.split(L',', token);

				if (last)
					token = s;

				bool found = false;
				if (token.comparei(L"hostwinmsg") == 0) {
					// special case -- hook message loop and log window messages
					if (!sHookLogger) {
						sHookLogger = this;
						sMsgHook = SetWindowsHookExW(WH_CALLWNDPROC, LogWindowMessage, VDGetLocalModuleHandleW32(), GetCurrentThreadId());
						sMsgHookRet = SetWindowsHookExW(WH_CALLWNDPROCRET, LogWindowMessageRet, VDGetLocalModuleHandleW32(), GetCurrentThreadId());
					}

					found = true;
				} else if (token.comparei(L"time") == 0) {
					mbShowTimeDeltas = true;
					found = true;
				} else {
					for(ATLogChannel *p = ATLogGetFirstChannel();
						p;
						p = ATLogGetNextChannel(p))
					{
						if (token.comparei(VDTextU8ToW(VDStringSpanA(p->GetName()))) == 0) {
							p->SetEnabled(true);
							found = true;
							break;
						}
					}
				}

				if (!found)
					Log("Warning: A log channel specified in /startuplog was not found.");

				if (last)
					break;
			}
		}
	}

	bool IsEnabled() const {
		return mbEnabled;
	}

	void Log(const char *msg) {
		Log(VDStringSpanA(msg));
	}

	void Log(VDStringSpanA msg) {
		if (mbEnabled) {
			DWORD actual;
			HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
			char buf[32];

			const uint64 t = VDGetPreciseTick();
			const double scale = VDGetPreciseSecondsPerTick();
			snprintf(buf, 32, "[%6.3f] ", (double)(t - mStartTick) * scale);
			buf[31] = 0;

			WriteFile(h, buf, (DWORD)strlen(buf), &actual, nullptr);

			if (mbShowTimeDeltas) {
				snprintf(buf, 32, "[%+6.3f] ", (double)(t - mLastMsgTick) * scale);
				buf[31] = 0;

				WriteFile(h, buf, (DWORD)strlen(buf), &actual, nullptr);

				mLastMsgTick = t;
			}

			WriteFile(h, msg.data(), (DWORD)msg.size(), &actual, nullptr);
			WriteFile(h, "\r\n", 2, &actual, nullptr);
		}
	}

	static LRESULT CALLBACK LogWindowMessage(int code, WPARAM localSendFlag, LPARAM msgInfo) {
		if (sHookLogger) {
			CWPSTRUCT& cwp = *(CWPSTRUCT *)msgInfo;

			VDStringA s;

#if VD_PTR_SIZE > 4
			s.sprintf("HOSTWINMSG: %c %08X %08X %016llX %016llX", localSendFlag ? L'S' : L'P',
				(unsigned)(uintptr)cwp.hwnd,
				(unsigned)cwp.message,
				(unsigned long long)cwp.wParam,
				(unsigned long long)cwp.lParam);
#else
			s.sprintf("HOSTWINMSG: %c %08X %08X %08X %08X", localSendFlag ? L'S' : L'P',
				(unsigned)cwp.hwnd,
				(unsigned)cwp.message,
				(unsigned)cwp.wParam,
				(unsigned)cwp.lParam);
#endif

			sHookLogger->Log(s);
		}

		return CallNextHookEx(sMsgHook, code, localSendFlag, msgInfo);
	}

	static LRESULT CALLBACK LogWindowMessageRet(int code, WPARAM localSendFlag, LPARAM msgInfo) {
		if (sHookLogger) {
			CWPRETSTRUCT& cwp = *(CWPRETSTRUCT *)msgInfo;

			VDStringA s;

#if VD_PTR_SIZE > 4
			s.sprintf("HOSTWINMSG: R %08X %08X %016llX %016llX -> %016llX",
				(unsigned)(uintptr)cwp.hwnd,
				(unsigned)cwp.message,
				(unsigned long long)cwp.wParam,
				(unsigned long long)cwp.lParam,
				(unsigned long long)cwp.lResult
			);
#else
			s.sprintf("HOSTWINMSG: R %08X %08X %08X %08X -> %08X",
				(unsigned)cwp.hwnd,
				(unsigned)cwp.message,
				(unsigned)cwp.wParam,
				(unsigned)cwp.lParam,
				(unsigned)cwp.lResult
			);
#endif

			sHookLogger->Log(s);
		}

		return CallNextHookEx(sMsgHookRet, code, localSendFlag, msgInfo);
	}

	static void ExceptionPreFilter(DWORD code, const EXCEPTION_POINTERS *exptrs) {
		// kill window message hook logging, we do not want to log any messages that
		// might arrive due to the message box
		sHookLogger = nullptr;

		HMODULE hmod = VDGetLocalModuleHandleW32();
		VDStringA s;

#ifdef VD_CPU_X86
		s.sprintf("CRASH: Code: %08X  PC: %08X  ExeBase: %08X", code, exptrs->ContextRecord->Eip, (unsigned)hmod);
#elif defined(VD_CPU_AMD64)
		s.sprintf("CRASH: Code: %08X  PC: %08X`%08X  ExeBase: %08X`%08X"
			, code
			, (unsigned)(exptrs->ContextRecord->Rip >> 32)
			, (unsigned)exptrs->ContextRecord->Rip
			, (unsigned)((uintptr)hmod >> 32)
			, (unsigned)(uintptr)hmod
		);
#elif defined(VD_CPU_ARM64)
		s.sprintf("CRASH: Code: %08X  PC: %08X`%08X  ExeBase: %08X`%08X"
			, code
			, (unsigned)(exptrs->ContextRecord->Pc >> 32)
			, (unsigned)exptrs->ContextRecord->Pc
			, (unsigned)((uintptr)hmod >> 32)
			, (unsigned)(uintptr)hmod
		);
#else
	#error Platform not supported
#endif

		sCrashLogger->Log(s);

#if defined(VD_CPU_X86) || defined(VD_CPU_AMD64)
		MEMORY_BASIC_INFORMATION mbi {};
		if (VirtualQuery(hmod, &mbi, sizeof mbi)) {
			// try to determine extent
			uintptr extent = 0;
			while(mbi.AllocationBase == hmod && mbi.RegionSize > 0) {
				extent += mbi.RegionSize;

				if (!VirtualQuery((char *)hmod + extent, &mbi, sizeof mbi))
					break;

				// we shouldn't be this big, if we are then something's wrong
				if ((extent | mbi.RegionSize) >= 0x10000000)
					break;
			}

#ifdef VD_CPU_X86
			const uintptr *sp = (const uintptr *)exptrs->ContextRecord->Esp;
#elif defined(VD_CPU_AMD64)
			const uintptr *sp = (const uintptr *)exptrs->ContextRecord->Rsp;
#endif

			int n = 0;
			for(int i=0; i<500; ++i) {
				bool valid = true;

				// can't mix EH types in the same function, so...
				uintptr v = [p = &sp[i], &valid]() -> uintptr {
					__try {
						return *p;
					} __except(EXCEPTION_EXECUTE_HANDLER) {
						valid = false;
						return 0;
					}
				}();

				if (!valid)
					break;

				// check if stack entry is within EXE range
				uintptr offset = v - (uintptr)hmod;
				if (offset < extent) {
					// check if stack entry is in executable code
					if (VirtualQuery((LPCVOID)v, &mbi, sizeof mbi) && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
						s.sprintf("CRASH: [%2d] exe+%08X", n, (unsigned)offset);
						sCrashLogger->Log(s);

						if (++n >= 30)
							break;
					}
				}
			}
		}
#endif
	}

	static ATStartupLogger *sCrashLogger;
	static ATStartupLogger *sHookLogger;
	static HHOOK sMsgHook;
	static HHOOK sMsgHookRet;

	bool mbEnabled = false;
	bool mbShowTimeDeltas = false;
	uint64 mStartTick = 0;
	uint64 mLastMsgTick = 0;
};

ATStartupLogger *ATStartupLogger::sCrashLogger;
ATStartupLogger *ATStartupLogger::sHookLogger;
HHOOK ATStartupLogger::sMsgHook;
HHOOK ATStartupLogger::sMsgHookRet;

#ifdef ATNRELEASE
extern int ATTestMain();
#endif

#ifdef VD_CPU_X64

extern "C" {
	void __cdecl __acrt_get_process_end_policy();
	void __cdecl __acrt_get_begin_thread_init_policy();
}

VDNOINLINE void ATEnableWin64MacOSWorkaround(ATStartupLogger& logger) {
	// Wine64 5.7 on macOS has a critical problem with its ABI emulation where it
	// can't properly emulate TEB access through the GS: selector due to macOS
	// already using it for pthread local storage. It tries to work around this
	// by hijacking the slots as gs:[30h] and gs:[58h] for the self and thread-
	// local-storage pointers. Unfortunately, this runs afoul of code in the UCRT
	// that directly accesses gs:[60h] to get to the PEB for Windows Runtime
	// support... so, despite us not caring about UWP, we're screwed by this
	// compatibility issue. The result is a null pointer crash in
	// _beginthreadex(), typically when we try to spin up the display manager.
	//
	// Ultimately, this is a really bad bug that Wine64 needs to fix. It is
	// rumored to be fixed in Crossover 20 betas, but in the meantime, while we
	// don't *offically* support Wine or macOS, we have some users that are kind
	// of stuck.
	//
	// To hack around it, we first check whether the environment has this problem
	// by checking whether the direct TEB access returns the same PEB pointer as
	// going through the TEB->Self pointer. This is a safe access because even
	// Wine64 on macOS does set up a proper TEB/PEB. This check will pass under
	// Windows or any environment where Wine64 can set up proper GS: addressing,
	// such as Linux. If that check fails, then we scan the code segments around
	// the pertinent UCRT functions for this offending instruction:
	//
	//		mov rax, qword ptr gs:[60h]
	//
	// ...and replace it with a call that effectively does NtCurrentTeb()->Peb instead:
	//
	//		call read_peb_indirect
	//
	//	read_peb_indirect:
	//		mov rax, qword ptr gs:[30h]
	//		mov rax, qword ptr [rax+60h]
	//		ret
	//
	// As this is super ugly nasty hackery, currently this is opt-in only via
	// the /macwine64hack switch or if Wine is detected by NTDLL export.
	//
	// More info:
	//	https://bugs.winehq.org/show_bug.cgi?id=49802
	//	https://developercommunity.visualstudio.com/t/64-bit-EXEs-built-with-VS2019-crash-unde/1248753

	const auto readPebIndirect = []() -> unsigned long long {
		unsigned long long tebSelf = __readgsqword(0x30);
		unsigned long long pebIndirect = *(const unsigned long long *)(tebSelf + 0x60);

		return pebIndirect;
	};
	
	logger.Log("Wine64 on macOS hack enabled -- checking GS: access to TEB.");

	unsigned long long pebDirect = __readgsqword(0x60);

	if (pebDirect == readPebIndirect()) {
		logger.Log("GS segment register points to proper TEB, no hack required.");
	} else {
		logger.Log("GS segment register points to bogus TEB, patching UCRT code.");

		char *routines[2] {
			(char *)__acrt_get_begin_thread_init_policy,
			(char *)__acrt_get_process_end_policy
		};

		static const uint8 kBadCodeSequence[] = {
			// mov rax,qword ptr gs:[60h]
			0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00
		};

		for (char *p : routines) {
			for (int i=0; i<256; ++i) {
				if (!memcmp(p + i, kBadCodeSequence, vdcountof(kBadCodeSequence))) {
					// We got a hit. Encode a CALL rel32 instruction to the corrective thunk
					// and patch it into the code segment.

					uint8 fixSequence[] = {
						// CALL rel32
						0xE8, 0x00, 0x00, 0x00, 0x00,

						// NOP dword ptr [rax]
						0x0F, 0x1F, 0x40, 0x00
					};

					static_assert(vdcountof(kBadCodeSequence) == vdcountof(fixSequence));

					uint32 offset = (uint32)((uint64)(unsigned long long (*)())readPebIndirect - (uint64)(p + i + 5));
					memcpy(&fixSequence[1], &offset, 4);

					// patch the code segment
					DWORD dwOldProtect = 0;
					if (VirtualProtect(p + i, vdcountof(kBadCodeSequence), PAGE_EXECUTE_READWRITE, &dwOldProtect)) {
						memcpy(p + i, fixSequence, vdcountof(fixSequence));

						DWORD dwOldProtect2 = 0;
						VirtualProtect(p + i, vdcountof(kBadCodeSequence), dwOldProtect, &dwOldProtect2);
					}
					break;
				}
			}
		}
	}
}
#endif

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int nCmdShow) {
#ifdef ATNRELEASE
	// In Debug and Profile configurations, the main Altirra executable now also
	// doubles as the unit test driver so that code within the main project can
	// be tested. However, we want unit tests to run in console mode for easier
	// logging and batching. To deal with this, we check the PE header to see if
	// the executable has been built or changed to the console subsystem. If so,
	// we invoke the test driver instead of the main executable.
	const IMAGE_NT_HEADERS *imageHeaders = (const IMAGE_NT_HEADERS *)((char *)&__ImageBase + ((const IMAGE_DOS_HEADER *)&__ImageBase)->e_lfanew);

	if (imageHeaders->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI)
		return ATTestMain();
#endif

	ATStartupLogger startupLogger;

	ATSaveRegisterTypes();
	ATInitSaveStateDeserializer();

	ATInitCRTHooks();

	ATUISetCommandLine(GetCommandLineW());

	BOOL suppressTimerExceptions = FALSE;
	::SetUserObjectInformation(GetCurrentProcess(), UOI_TIMERPROC_EXCEPTION_SUPPRESSION, &suppressTimerExceptions, sizeof suppressTimerExceptions);
	::SetUnhandledExceptionFilter(ATUnhandledExceptionFilter);

	const wchar_t *token = nullptr;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"startuplog", token)) {
		startupLogger.Init(token);
		startupLogger.Log("Startup logging enabled.");
	}

#ifdef VD_CPU_X64
	if (!g_ATCmdLine.FindAndRemoveSwitch(L"nomacwine64hack")) {
		bool doTebCheck = g_ATCmdLine.FindAndRemoveSwitch(L"macwine64hack");

		if (!doTebCheck) {
			const HMODULE hmodNtdll = GetModuleHandleW(L"ntdll");
			if (hmodNtdll && GetProcAddress(hmodNtdll, "wine_get_version")) {
				startupLogger.Log("Wine detected -- checking TEB for Wine bug 49802.");
				doTebCheck = true;
			}
		}

		if (doTebCheck)
			ATEnableWin64MacOSWorkaround(startupLogger);
	}
#endif

	if (g_ATCmdLine.FindAndRemoveSwitch(L"fullheapdump"))
		ATExceptionFilterSetFullHeapDump(true);

	ATInitCPUOptions();

	VDInitThunkAllocator();

	startupLogger.Log("Initializing OLE.");
	OleInitialize(NULL);

	ATInitRand();
	ATVFSInstallAtfsHandler();

	InitCommonControls();
	BufferedPaintInit();

	startupLogger.Log("Initializing themes");
	ATUIInitThemes();

	int rval = 0;
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
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"?")) {
		// This is normally done by ATInitUIPanes(), but we are skipping that here.
		HMODULE hm = VDLoadSystemLibraryW32("msftedit");

		OnCommandHelpCmdLine();

		if (hm)
			FreeLibrary(hm);
	} else {
		ATSettingsCategory categoriesToIgnore {};

		if (!ATInitRegistry(categoriesToIgnore)) {
			rval = 5;
		} else {
			if (g_ATCmdLine.FindAndRemoveSwitch(L"advconfig")) {
				extern void ATUIShowDialogAdvancedConfiguration(VDGUIHandle h);
				ATUIShowDialogAdvancedConfiguration(nullptr);
			} else {
				startupLogger.Log("Loading config var overrides");
				ATLoadConfigVars();

				startupLogger.Log("Loading options");
				ATOptionsLoad();

				startupLogger.Log("Loading settings");
				LoadSettingsEarly();

				ATInitDisplayOptions();

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

				ATNotifyOtherInstanceResult notifyResult = ATNotifyOtherInstanceResult::NoOtherInstance;
				if (singleInstance) {
					notifyResult = ATNotifyOtherInstance(g_ATCmdLine);

					if (notifyResult == ATNotifyOtherInstanceResult::Failed)
						rval = 20;
				}

				if (notifyResult == ATNotifyOtherInstanceResult::NoOtherInstance) {
					if (g_ATCmdLine.FindAndRemoveSwitch(L"lockd3d"))
						g_d3d9Lock.Lock();

					startupLogger.Log("Running instance");
					rval = RunInstance(nCmdShow, startupLogger, categoriesToIgnore);
				}
			}

			startupLogger.Log("Shutting down registry");
			ATShutdownRegistry();
		}
	}

	BufferedPaintUnInit();

	startupLogger.Log("Shutting down OLE");
	OleUninitialize();

	startupLogger.Log("Shutting down thunk allocator");
	VDShutdownThunkAllocator();

	startupLogger.Log("Exiting (end of log).");
	return rval;
}

class ATSystemLibraryLockW32 {
public:
	ATSystemLibraryLockW32(const char *name)
		: mhmod(VDLoadSystemLibraryW32(name))
	{
	}

	~ATSystemLibraryLockW32() {
		if (mhmod)
			FreeLibrary(mhmod);
	}

private:
	HMODULE mhmod = nullptr;
};

int RunInstance(int nCmdShow, ATStartupLogger& startupLogger, ATSettingsCategory categoriesToIgnore) {
	startupLogger.Log("Preloading DLLs");
	ATSystemLibraryLockW32 lockd3d9("d3d9");
	ATSystemLibraryLockW32 lockd3d11("d3d11");
	ATSystemLibraryLockW32 lockdxgi("dxgi");
	ATSystemLibraryLockW32 lockdwmapi("dwmapi");

	g_hInst = VDGetLocalModuleHandleW32();

	startupLogger.Log("Registering controls");
	VDRegisterVideoDisplayControl();
	VDUIRegisterHotKeyExControl();
	ATUINativeWindow::Register();
	
	startupLogger.Log("Initializing frame system");
	ATInitUIFrameSystem();

	startupLogger.Log("Initializing commands and accelerators");
	ATUIInitCommandMappings(g_ATUICommandMgr);
	ATUIInitDefaultAccelTables();
	
	VDDialogFrameW32::SetDefaultCaption(L"Altirra");
	ATUISetDefaultGenericDialogCaption(L"Altirra");

	startupLogger.Log("Applying options");

	struct local {
		static void DisplayUpdateFn(ATOptions& opts, const ATOptions *prevOpts, void *) {
			if (prevOpts) {
				if (prevOpts->mbDisplayD3D9 == opts.mbDisplayD3D9
					&& prevOpts->mbDisplay3D == opts.mbDisplay3D
					&& prevOpts->mbDisplay16Bit == opts.mbDisplay16Bit
					)
					return;
			}

			VDVideoDisplaySetFeatures(true, false, false, opts.mbDisplayD3D9, false, false);
			VDVideoDisplaySet3DEnabled(opts.mbDisplay3D);

			IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
			if (pane)
				pane->ResetDisplay();
		}
	};

	ATOptionsAddUpdateCallback(true, local::DisplayUpdateFn);
	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			g_sim.GetGTIA().SetAccelScreenFXEnabled(opts.mbDisplayAccelScreenFX);
		}
	);

	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			ATUISetDarkThemeActive(opts.mbDarkTheme);
		}
	);

	ATOptionsAddUpdateCallback(true,
		[](ATOptions& opts, const ATOptions *prevOpts, void *) {
			ATDirectoryWatcher::SetShouldUsePolling(opts.mbPollDirectories);
		}
	);

	VDVideoDisplaySetDebugInfoEnabled(g_ATCmdLine.FindAndRemoveSwitch(L"displaydebug"));
	VDVideoDisplaySetMonitorSwitchingDXEnabled(true);
	VDVideoDisplaySetSecondaryDXEnabled(true);
	VDVideoDisplaySetD3D9ExEnabled(false);
	VDVideoDisplaySetTermServ3DEnabled(true);

	VDDispSetLogHook([](const char *s) { g_ATLCHostDisp("%s\n", s); });

	startupLogger.Log("Initializing filespec system");
	VDInitFilespecSystem();
	VDLoadFilespecSystemData();

	startupLogger.Log("Initializing UI panes");
	ATInitUIPanes();

	startupLogger.Log("Initializing logging");
	ATInitDebuggerLogging(
		startupLogger.IsEnabled()
			? vdfunction<void(const char *)>([&startupLogger](const char *msg) {
					VDStringRefA msg2(msg);

					startupLogger.Log(msg2.trim_end("\r\n"));
				})
			: nullptr
	);

	startupLogger.Log("Initializing native UI");
	ATInitProfilerUI();
	ATUIRegisterDisplayPane();
	ATUIInitVirtualKeyMap(g_kbdOpts);
	ATUILoadAccelTables();

	startupLogger.Log("Creating main window");
	WNDCLASS wc = {};
	wc.lpszClassName = _T("AltirraMainWindow");
	ATOM atom = ATContainerWindow::RegisterCustom(wc);
	ATUICreateMainWindow(&g_pMainWindow);
	HWND hwnd = (HWND)g_pMainWindow->Create(atom, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, false);
	g_hwnd = hwnd;

	ATUIInitProgressDialog();
	ATUIPushProgressParent((VDGUIHandle)hwnd);

	startupLogger.Log("Restoring main window");
	ATUIRestoreWindowPlacement(hwnd, "Main window", nCmdShow);

	if (!(GetWindowLong(hwnd, GWL_STYLE) & WS_VISIBLE))
		ShowWindow(hwnd, nCmdShow);

	ATUICreateWindowCaptionUpdater(&g_winCaptionUpdater);
	g_winCaptionUpdater->Init([hwnd](const wchar_t *s) { SetWindowTextW(hwnd, s); } );

	// Init Winsock. We want to delay this as much as possible, but it must be done before we
	// attempt to bring up a modem (which LoadSettings or a command prompt can do).
	bool wsaInited = false;

	startupLogger.Log("Initializing WinSock");

	WSADATA wsa;
	if (!WSAStartup(MAKEWORD(2, 0), &wsa))
		wsaInited = true;

	// bring up simulator
	startupLogger.Log("Initializing simulator");
	g_sim.Init();
	g_sim.SetRandomSeed(rand() ^ (rand() << 15));

	ATUISetDispatcher(g_sim.GetDeviceManager()->GetService<IATAsyncDispatcher>());
	ATUILinkMainWindowToSimulator(*g_pMainWindow);

	startupLogger.Log("Initializing game controllers");
	ATInitJoysticks();

	g_sim.GetInputManager()->SetConsoleCallback(&g_inputConsoleCallback);
	g_winCaptionUpdater->InitMonitoring(&g_sim);
	g_winCaptionUpdater->SetTemplate(g_winCaptionTemplate.c_str());
	ATRegisterDevices(*g_sim.GetDeviceManager());

	// initialize menus (requires simulator)
	startupLogger.Log("Initializing firmware menus");
	ATUIInitFirmwareMenuCallbacks(g_sim.GetFirmwareManager());

	startupLogger.Log("Initializing profile menus");
	ATUIInitProfileMenuCallbacks();

	startupLogger.Log("Initializing video output menu");
	ATUIInitVideoOutputMenuCallback(*g_sim.GetDeviceManager()->GetService<IATDeviceVideoManager>());

	startupLogger.Log("Loading menu");
	try {
		ATUILoadMenu();
	} catch(const MyError& e) {
		e.post(hwnd, "Altirra Error");
	}

	startupLogger.Log("Initializing port menus");
	ATInitPortMenus(g_sim.GetInputManager());

	// bring up debugger
	startupLogger.Log("Initializing debugger");
	ATInitDebugger();

	// init compatibility database
	startupLogger.Log("Initializing compatibility system");
	ATCompatInit();

	// initialize runtime UI (requires simulator)
	startupLogger.Log("Initializing display UI");
	ATUIInitManager();

	// load available profile set
	startupLogger.Log("Loading profiles");
	if (ATLoadDefaultProfiles())
		ATUIRebuildDynamicMenu(kATUIDynamicMenu_Profile);

	// load initial profile
	const bool useHardwareBaseline = g_ATCmdLine.FindAndRemoveSwitch(L"baseline");

	ATSettingsCategory settingsToLoad = useHardwareBaseline
			? ATSettingsCategory(kATSettingsCategory_All & ~(kATSettingsCategory_FullScreen | kATSettingsCategory_Baseline))
			: ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen);

	settingsToLoad = ATSettingsCategory(settingsToLoad & ~categoriesToIgnore);

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

	startupLogger.Log("Loading current profile");
	if (profileToLoad != kATProfileId_Invalid && ATSettingsIsValidProfile(profileToLoad))
		ATSettingsLoadProfile(profileToLoad, settingsToLoad);
	else
		ATSettingsLoadLastProfile(settingsToLoad);

	if (g_ATCmdLine.FindAndRemoveSwitch(L"tempprofile"))
		ATSettingsSetTemporaryProfileMode(true);

	if (g_ATCmdLine.FindSwitch(L"autoprofile"))
		ATSettingsSetBootstrapProfileMode(true);

	if (useHardwareBaseline)
		LoadBaselineSettings();
	
	startupLogger.Log("Initializing native audio");

	g_sim.GetAudioOutput()->InitNativeAudio();
	
	startupLogger.Log("Loading ROMs");

	try {
		g_sim.LoadROMs();
	} catch(MyError& e) {
		e.post(hwnd, "Altirra Error");
	}

	startupLogger.Log("Saving options");
	ATOptionsSave();

	startupLogger.Log("Starting emulation");
	g_sim.Resume();
	ATInitEmuErrorHandler((VDGUIHandle)g_hwnd, &g_sim);

	startupLogger.Log("Restoring pane layout");

	if (!ATRestorePaneLayout(NULL))
		ATActivateUIPane(kATUIPaneId_Display, true);

	if (ATGetUIPane(kATUIPaneId_Display))
		ATActivateUIPane(kATUIPaneId_Display, true);

	startupLogger.Log("Initiating cold reset");
	g_sim.ColdReset();

	// we can't go full screen until the display panes have been created
	if (!g_ATCmdLine.FindAndRemoveSwitch(L"w")) {
		startupLogger.Log("Initializing full screen mode");
		ATLoadSettings(kATSettingsCategory_FullScreen);
	}

	startupLogger.Log("Running main loop");
	int returnCode = RunMainLoop(hwnd);

	startupLogger.Log("Saving settings");
	ATSaveSettings(ATSettingsCategory(kATSettingsCategory_All & ~kATSettingsCategory_FullScreen));

	startupLogger.Log("Exiting fullscreen mode");
	ATSetFullscreen(false);

	startupLogger.Log("Stopping recording");
	StopRecording();

	startupLogger.Log("Shutting down filespec system");
	VDSaveFilespecSystemData();

	startupLogger.Log("Shutting down compat system");
	ATCompatShutdown();

	startupLogger.Log("Shutting down UI");
	ATUIShutdownManager();

	startupLogger.Log("Shutting down debugger");
	ATShutdownEmuErrorHandler();
	ATShutdownDebugger();
	ATConsoleCloseLogFileNT();

	startupLogger.Log("Shutting down game controllers");
	ATShutdownJoysticks();

	startupLogger.Log("Shutting down simulator");
	ATUISetDispatcher(nullptr);
	ATUIUnlinkMainWindowFromSimulator();
	g_sim.Shutdown();

	startupLogger.Log("Shutting down native UI");
	g_d3d9Lock.Unlock();

	ATUIShutdownProgressDialog();
	ATUIPopProgressParent((VDGUIHandle)g_hwnd, nullptr);

	g_pMainWindow->Destroy();
	g_pMainWindow->Release();
	g_pMainWindow = NULL;
	g_hwnd = NULL;

	g_winCaptionUpdater->Release();
	g_winCaptionUpdater = nullptr;

	ATShutdownPortMenus();

	ATShutdownUIPanes();
	ATShutdownUIFrameSystem();
	ATUIShutdownThemes();

	if (wsaInited) {
		startupLogger.Log("Shutting down WinSock");
		WSACleanup();
	}

	return returnCode;
}
