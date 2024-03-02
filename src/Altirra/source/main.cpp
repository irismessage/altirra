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

#include "stdafx.h"
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
#include <vd2/Riza/direct3d.h>
#include <vd2/Dita/accel.h>
#include <vd2/Dita/services.h>
#include <at/atui/acceleditdialog.h>
#include <at/atui/hotkeyexcontrol.h>
#include "console.h"
#include "simulator.h"
#include "cassette.h"
#include "ui.h"
#include "uiframe.h"
#include "debugger.h"
#include "hostdevice.h"
#include "pclink.h"
#include <at/atui/dialog.h>
#include "savestate.h"
#include "resource.h"
#include "oshelper.h"
#include "autotest.h"
#include "audiowriter.h"
#include "inputmanager.h"
#include "inputcontroller.h"
#include "joystick.h"
#include "cartridge.h"
#include "version.h"
#include "videowriter.h"
#include "ide.h"
#include "kmkjzide.h"
#include "side.h"
#include "myide.h"
#include "ultimate1mb.h"
#include "rs232.h"
#include "options.h"
#include "dragoncart.h"
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
#include "uicommandmanager.h"
#include "uiprofiler.h"
#include "uiqueue.h"
#include "uicommondialogs.h"

#pragma comment(lib, "comctl32")
#pragma comment(lib, "shlwapi")

void ATUITriggerButtonDown(uint32 vk);
void ATUITriggerButtonUp(uint32 vk);

///////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogInputMappings(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy);
void ATUIShowDiskDriveDialog(VDGUIHandle hParent);
void ATUIShowHardDiskDialog(VDGUIHandle hParent);
void ATUIShowHostDeviceDialog(VDGUIHandle hParent, IATHostDeviceEmulator *hd);
void ATUIShowPCLinkDialog(VDGUIHandle hParent, ATSimulator *sim);
void ATUIOpenAdjustColorsDialog(VDGUIHandle hParent);
void ATUICloseAdjustColorsDialog();
void ATUIShowAudioOptionsDialog(VDGUIHandle hParent);
void ATUIShowTapeControlDialog(VDGUIHandle hParent, ATCassetteEmulator& cassette);
void ATUIShowCPUOptionsDialog(VDGUIHandle h);
void ATUIShowSerialPortsDialog(VDGUIHandle h);
void ATUIShowDialogDragonCart(VDGUIHandle h);
int ATUIShowDialogCartridgeMapper(VDGUIHandle h, uint32 cartSize, const void *data);
void ATUIShowDialogLightPen(VDGUIHandle h, ATLightPenPort *lpp);
void ATUIShowDialogDebugFont(VDGUIHandle hParent);
void ATUIShowDialogCheater(VDGUIHandle hParent, ATCheatEngine *engine);
void ATUIShowDialogROMImages(VDGUIHandle hParent, ATSimulator& sim);
void ATUIShowDialogDiskExplorer(VDGUIHandle h);
void ATUIShowDialogOptions(VDGUIHandle h);
void ATUIShowDialogAbout(VDGUIHandle h);
void ATUIShowDialogVerifier(VDGUIHandle h, ATSimulator& sim);
bool ATUIShowDialogKeyboardOptions(VDGUIHandle hParent, ATUIKeyboardOptions& opts);
void ATUIShowDialogSetFileAssociations(VDGUIHandle parent, bool allowElevation);
void ATUIShowDialogRemoveFileAssociations(VDGUIHandle parent, bool allowElevation);
void ATUIShowDialogSpeedOptions(VDGUIHandle parent);
bool ATUIShowDialogVideoEncoding(VDGUIHandle parent, bool hz50, ATVideoEncoding& encoding, ATVideoRecordingFrameRate& frameRate, bool& halfRate, bool& encodeAll);

void ATUIRegisterDragDropHandler(VDGUIHandle h);
void ATUIRevokeDragDropHandler(VDGUIHandle h);

void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);

void ATInitEmuErrorHandler(VDGUIHandle h, ATSimulator *sim);
void ATShutdownEmuErrorHandler();

bool ATIDEIsPhysicalDiskPath(const wchar_t *path);

void ATUIInitManager();
void ATUIShutdownManager();
void ATUIFlushDisplay();
bool ATUIIsActiveModal();
void ATUISetXEPViewEnabled(bool enabled);

///////////////////////////////////////////////////////////////////////////////

int ATExceptionFilter(DWORD code, EXCEPTION_POINTERS *exp);
LONG WINAPI ATUnhandledExceptionFilter(EXCEPTION_POINTERS *exp);
void ATExceptionFilterSetFullHeapDump(bool enabled);

void ATInitDebugger();
void ATShutdownDebugger();
void ATInitProfilerUI();
void ATInitUIPanes();
void ATShutdownUIPanes();
void ATShowChangeLog(VDGUIHandle hParent);

void SaveInputMaps();

void ProcessKey(char c, bool repeat, bool allowQueue, bool useCooldown);
void ProcessVirtKey(int vkey, uint8 keycode, bool repeat);
void DoLoad(VDGUIHandle h, const wchar_t *path, bool vrw, bool rw, int cartmapper, ATLoadType loadType = kATLoadType_Other, bool *suppressColdReset = NULL, int loadIndex = -1);
void DoBootWithConfirm(const wchar_t *path, bool vrw, bool rw, int cartmapper);

void LoadBaselineSettings();

///////////////////////////////////////////////////////////////////////////////

#define ATWM_APP_PRETRANSLATE (WM_APP + 100)
#define ATWM_APP_DEBUG_TOGGLEBREAKPOINT (WM_APP + 0x200)

///////////////////////////////////////////////////////////////////////////////

extern const wchar_t g_wszWarning[]=L"Altirra Warning";

HINSTANCE g_hInst;
HWND g_hwnd;
ATContainerWindow *g_pMainWindow;
IVDVideoDisplay *g_pDisplay;
HMENU g_hMenu;
HMENU g_hMenuMRU;
ATUIMenu *g_pMenuMRU;

VDCommandLine g_ATCmdLine;
bool g_ATCmdLineRead;

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
bool g_autotestEnabled = false;
bool g_xepViewEnabled = false;
bool g_xepViewAutoswitchingEnabled = false;

ATUIKeyboardOptions g_kbdOpts;

ATDisplayFilterMode g_dispFilterMode = kATDisplayFilterMode_Bilinear;
int g_dispFilterSharpness = 0;
int g_enhancedText = 0;
LOGFONTW g_enhancedTextFont;

ATSaveStateWriter::Storage g_quickSave;

vdautoptr<ATAudioWriter> g_pAudioWriter;
vdautoptr<IATVideoWriter> g_pVideoWriter;

ATDisplayStretchMode g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;

ATUIWindowCaptionUpdater g_winCaptionUpdater;

ATUICommandManager g_ATUICommandMgr;

///////////////////////////////////////////////////////////////////////////////

ATFrameRateMode g_frameRateMode = kATFrameRateMode_Hardware;
float	g_speedModifier;		// -1.0f
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
		{ 1.0 / 59.9700, 1.0 / 50.0000 },
		{ 1.0 / 60.0000, 1.0 / 50.0000 },
	};

	const bool hz50 = (g_sim.GetVideoStandard() != kATVideoStandard_NTSC);
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

void ATUIChangeSpeedFlags(uint8 mask, uint8 value) {
	uint8 delta = (g_speedFlags ^ value) & mask;

	if (!delta)
		return;

	uint8 flags = (g_speedFlags ^ delta);
	g_speedFlags = flags;

	g_sim.SetTurboModeEnabled((flags & (kATUISpeedFlags_Turbo | kATUISpeedFlags_TurboPulse)) != 0);

	ATUIUpdateSpeedTiming();
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
					g_sim.GetPokey().PushRawKey(0x21);
				else
					g_sim.GetPokey().ReleaseRawKey();
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

bool ATUISwitchHardwareMode(VDGUIHandle h, ATHardwareMode mode) {
	ATHardwareMode prevMode = g_sim.GetHardwareMode();
	if (prevMode == mode)
		return true;

	// check if we are switching to or from 5200 mode
	if (mode == kATHardwareMode_5200 || prevMode == kATHardwareMode_5200) {
		// check if it's OK to unload everything
		if (!ATUIConfirmDiscardAllStorage(h, L"OK to switch hardware mode and discard everything?"))
			return false;

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
		case kATKernelMode_Other:
			if (mode != kATHardwareMode_800XL && mode != kATHardwareMode_1200XL && mode != kATHardwareMode_130XE && mode != kATHardwareMode_XEGS)
				g_sim.SetKernelMode(kATKernelMode_Default);
			break;

		case kATKernelMode_XEGS:
			if (mode != kATHardwareMode_XEGS)
				g_sim.SetKernelMode(kATKernelMode_Default);
			break;

		case kATKernelMode_5200:
		case kATKernelMode_5200_LLE:
			if (mode != kATHardwareMode_5200)
				g_sim.SetKernelMode(kATKernelMode_Default);
			break;

		default:
			if (mode == kATHardwareMode_5200)
				g_sim.SetKernelMode(kATKernelMode_Default);
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

	return ATUISwitchHardwareMode(h, kATHardwareMode_800XL);
}

bool ATUISwitchHardwareMode5200(VDGUIHandle h) {
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return true;

	return ATUISwitchHardwareMode(h, kATHardwareMode_5200);
}

bool ATUISwitchKernelMode(VDGUIHandle h, ATKernelMode mode) {
	if (g_sim.GetKernelMode() == mode)
		return true;

	// If the kernel mode is incompatible, check if we can switch the computer
	// mode.
	switch(mode) {
		case kATKernelMode_Default:
			break;

		case kATKernelMode_XL:
		case kATKernelMode_Other:
			if (!ATUISwitchHardwareMode(h, kATHardwareMode_800XL))
				return false;
			break;

		case kATKernelMode_1200XL:
			if (!ATUISwitchHardwareMode(h, kATHardwareMode_1200XL))
				return false;
			break;

		case kATKernelMode_XEGS:
			if (!ATUISwitchHardwareMode(h, kATHardwareMode_XEGS))
				return false;
			break;

		case kATKernelMode_5200:
		case kATKernelMode_5200_LLE:
			if (!ATUISwitchHardwareMode5200(h))
				return false;
			break;

		default:
			if (!ATUISwitchHardwareModeComputer(h))
				return false;
			break;
	}

	// Check if we need to adjust the memory size. XL and Other kernels can't
	// run with 48K or 56K. 16K is OK (600XL configuration). We don't need to
	// check 5200 here as it was already changed in the hardware mode switch.
	switch(mode) {
		case kATKernelMode_XL:
		case kATKernelMode_Other:
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

	g_sim.SetKernelMode(mode);
	g_sim.ColdReset();
	return true;
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

void DoLoadStream(VDGUIHandle h, const wchar_t *fileName, IVDRandomAccessStream& stream, bool vrw, int cartmapper, ATLoadType loadType, bool *suppressColdReset, int loadIndex) {
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

	if (!g_sim.Load(NULL, fileName, stream, vrw, false, &ctx)) {
		if (ctx.mLoadType == kATLoadType_Cartridge) {
			int mapper = ATUIShowDialogCartridgeMapper(h, cartctx.mCartSize, captureBuffer.data());

			if (mapper >= 0) {
				cartctx.mbReturnOnUnknownMapper = false;
				cartctx.mCartMapper = mapper;

				g_sim.Load(NULL, fileName, stream, vrw, false, &ctx);
			}
		} else if (ctx.mLoadType == kATLoadType_SaveState) {
			if (statectx.mbKernelMismatchDetected) {
				if (IDOK != MessageBoxW((HWND)h,
					L"The currently loaded kernel ROM image doesn't match the one referenced by the saved state. This may cause the simulated program to fail when resumed. Proceed anyway?",
					L"Altirra Warning", MB_ICONWARNING | MB_OKCANCEL))
					return;

				statectx.mbAllowKernelMismatch = true;
			}

			g_sim.Load(NULL, fileName, stream, vrw, false, &ctx);
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

void DoLoad(VDGUIHandle h, const wchar_t *path, bool vrw, bool rw, int cartmapper, ATLoadType loadType, bool *suppressColdReset, int loadIndex) {
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
	if (!g_sim.Load(path, vrw, rw, &ctx)) {
		if (ctx.mLoadType == kATLoadType_Cartridge) {
			int mapper = ATUIShowDialogCartridgeMapper(h, cartctx.mCartSize, captureBuffer.data());

			if (mapper >= 0) {
				cartctx.mbReturnOnUnknownMapper = false;
				cartctx.mCartMapper = mapper;

				g_sim.Load(path, vrw, rw, &ctx);
			}
		} else if (ctx.mLoadType == kATLoadType_SaveState) {
			if (statectx.mbKernelMismatchDetected) {
				if (IDOK != MessageBoxW((HWND)h,
					L"The currently loaded kernel ROM image doesn't match the one referenced by the saved state. This may cause the simulated program to fail when resumed. Proceed anyway?",
					L"Altirra Warning", MB_ICONWARNING | MB_OKCANCEL))
					return;

				statectx.mbAllowKernelMismatch = true;
			}

			g_sim.Load(path, vrw, rw, &ctx);
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

void DoBootWithConfirm(const wchar_t *path, bool vrw, bool rw, int cartmapper) {
	if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?"))
		return;

	try {
		g_sim.UnloadAll();

		DoLoad((VDGUIHandle)g_hwnd, path, vrw, rw, cartmapper);

		g_sim.ColdReset();

		ATAddMRUListItem(path);
		ATUpdateMRUListMenu(g_hMenuMRU, g_pMenuMRU, ID_FILE_MRU_BASE, ID_FILE_MRU_BASE + 99);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}
}

void DoBootStreamWithConfirm(const wchar_t *fileName, IVDRandomAccessStream& stream, bool vrw, int cartmapper) {
	if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?"))
		return;

	try {
		g_sim.UnloadAll();

		DoLoadStream((VDGUIHandle)g_hwnd, fileName, stream, vrw, cartmapper, kATLoadType_Other, NULL, -1);

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
					L"All supported types\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.xex;*.obx;*.com;*.car;*.rom;*.a52;*.bin;*.cas;*.wav;*.zip;*.atz;*.gz;*.bas;*.arc\0"
					L"Atari program (*.xex,*.obx,*.com)\0*.xex;*.obx;*.com\0"
					L"BASIC program (*.bas)\0*.bas\0"
					L"Atari disk image (*.atr,*.xfd,*.dcm)\0*.atr;*.xfd;*.dcm;*.arc\0"
					L"Protected disk image (*.pro)\0*.pro\0"
					L"VAPI disk image (*.atx)\0*.atx\0"
					L"Cartridge (*.rom,*.bin,*.a52,*.car)\0*.rom;*.bin;*.a52;*.car\0"
					L"Cassette tape (*.cas,*.wav)\0*.cas;*.wav\0"
					L"Zip archive (*.zip)\0*.zip\0"
					L"gzip archive (*.gz;*.atz)\0*.gz;*.atz\0"
					L"All files\0*.*\0");

				Wait(mpFileDialogResult);
				++mStage;
				break;

			case 2:
				if (mpFileDialogResult->mbAccepted) {
					if (mbColdBoot)
						g_sim.UnloadAll();

					DoLoad((VDGUIHandle)g_hwnd, mpFileDialogResult->mPath.c_str(), false, false, 0);

					if (mbColdBoot)
						g_sim.ColdReset();

					ATAddMRUListItem(mpFileDialogResult->mPath.c_str());
					ATUpdateMRUListMenu(g_hMenuMRU, g_pMenuMRU, ID_FILE_MRU_BASE, ID_FILE_MRU_BASE + 99);
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

void OnCommandVideoToggleXEP80View() {
	ATUISetXEPViewEnabled(!g_xepViewEnabled);
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
		else
			ProcessKey(c, false, true, useCooldown);
	}
}

void OnCommandSystemTogglePause() {
	if (g_sim.IsRunning())
		g_sim.Pause();
	else
		g_sim.Resume();
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

void StopRecording() {
	StopAudioRecording();
	StopVideoRecording();
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
		L"All supported types\0*.altstate\0"
		L"Altirra save state\0*.altstate\0"
		L"All files\0*.*\0",
		L"altstate"
		));

	if (!fn.empty()) {
		DoLoad((VDGUIHandle)g_hwnd, fn.c_str(), false, false, 0, kATLoadType_SaveState);
	}
}

void OnCommandSaveState() {
	const VDStringW fn(VDGetSaveFileName('save', (VDGUIHandle)g_hwnd, L"Save save state",
		L"Altirra save state (*.altstate)\0*.altstate\0",
		L"altstate"
		));

	if (!fn.empty()) {
		ATSaveStateWriter::Storage storage;

		ATSaveStateWriter writer(storage);
		g_sim.SaveState(writer);

		VDFile f(fn.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);

		f.write(storage.data(), storage.size());
	}
}

void OnCommandAttachCartridge(bool cart2) {
	if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
		VDStringW fn(VDGetLoadFileName('cart', (VDGUIHandle)g_hwnd, L"Load cartridge",
			L"All supported types\0*.bin;*.car;*.rom;*.a52;*.zip\0"
			L"Cartridge image (*.bin,*.car,*.rom,*.a52)\0*.bin;*.car;*.rom;*.a52\0"
			L"Zip archive (*.zip)\0*.zip\0"
			L"All files\0*.*\0",

			L"bin"));

		if (!fn.empty()) {
			vdfastvector<uint8> captureBuffer;

			ATCartLoadContext cartctx = {};
			cartctx.mbReturnOnUnknownMapper = true;
			cartctx.mpCaptureBuffer = &captureBuffer;

			if (!g_sim.LoadCartridge(cart2, fn.c_str(), &cartctx)) {
				int mapper = ATUIShowDialogCartridgeMapper((VDGUIHandle)g_hwnd, cartctx.mCartSize, captureBuffer.data());

				if (mapper >= 0) {
					cartctx.mbReturnOnUnknownMapper = false;
					cartctx.mCartMapper = mapper;

					g_sim.LoadCartridge(cart2, fn.c_str(), &cartctx);
				}
			}

			g_sim.ColdReset();
		}
	}
}

void OnCommandAttachCartridge() {
	OnCommandAttachCartridge(false);
}

void OnCommandAttachCartridge2() {
	OnCommandAttachCartridge(true);
}

void OnCommandDetachCartridge() {
	if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
		if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
			g_sim.LoadCartridge5200Default();
		else
			g_sim.UnloadCartridge(0);

		g_sim.ColdReset();
	}
}

void OnCommandDetachCartridge2() {
	if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
		g_sim.UnloadCartridge(1);
		g_sim.ColdReset();
	}
}

template<ATCartridgeMode T_Mode>
void OnCommandAttachNewCartridge() {
	if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
		g_sim.LoadNewCartridge(T_Mode);
		g_sim.ColdReset();
	}
}

void OnCommandAttachCartridgeBASIC() {
	if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
		g_sim.LoadCartridgeBASIC();
		g_sim.ColdReset();
	}
}

void OnCommandCartActivateMenuButton() {
	ATSIDEEmulator *side = g_sim.GetSIDE();

	if (side)
		side->ResetCartBank();
}

void OnCommandCartToggleSwitch() {
	g_sim.SetCartridgeSwitch(!g_sim.GetCartridgeSwitch());
}

void OnCommandSaveCartridge() {
	ATCartridgeEmulator *cart = g_sim.GetCartridge(0);
	int mode = 0;

	if (cart)
		mode = cart->GetMode();

	if (!mode)
		throw MyError("There is no cartridge to save.");

	if (mode == kATCartridgeMode_SuperCharger3D)
		throw MyError("The current cartridge cannot be saved to an image file.");

	VDFileDialogOption opts[]={
		{VDFileDialogOption::kSelectedFilter, 0, NULL, 0, 0},
		{0}
	};

	int optval[1]={0};

	VDStringW fn(VDGetSaveFileName('cart', (VDGUIHandle)g_hwnd, L"Save cartridge",
		L"Cartridge image with header\0*.bin;*.car;*.rom\0"
		L"Raw cartridge image\0*.bin;*.car;*.rom;*.a52\0",

		L"car", opts, optval));

	if (!fn.empty()) {
		cart->Save(fn.c_str(), optval[0] == 1);
	}
}

void OnCommandSaveFirmware(int idx) {
	ATSIDEEmulator *side = g_sim.GetSIDE();
	ATMyIDEEmulator *myide = g_sim.GetMyIDE();
	ATKMKJZIDE *kjide = g_sim.GetKMKJZIDE();

	if (!g_sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + idx)))
		throw MyError("The selected type of firmware is not present.");

	const ATROMImage image
		= idx==2 ? kATROMImage_Ultimate1MB
		: side ? kATROMImage_SIDE_SDX
		: myide ? kATROMImage_MyIDEII
		: idx ? kATROMImage_KMKJZIDEV2_SDX : kATROMImage_KMKJZIDEV2;

	VDStringW origPath;
	g_sim.GetROMImagePath(image, origPath);
	VDSetLastLoadSaveFileName('rom ', origPath.c_str());

	VDStringW fn(VDGetSaveFileName('rom ', (VDGUIHandle)g_hwnd, L"Save firmware image",
		L"Raw firmware image\0*.rom\0",
		L"rom"));

	if (!fn.empty()) {
		if (idx == 2)
			g_sim.GetUltimate1MB()->SaveFirmware(fn.c_str());
		else if (kjide)
			kjide->SaveFirmware(idx != 0, fn.c_str());
		else if (side)
			side->SaveFirmware(fn.c_str());
		else if (myide)
			myide->SaveFirmware(fn.c_str());

		g_sim.SetROMImagePath(image, fn.c_str());
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

void OnCommandCassetteLoad() {
	VDStringW fn(VDGetLoadFileName('cass', (VDGUIHandle)g_hwnd, L"Load cassette tape", L"All supported types\0*.wav;*.cas\0Atari cassette image (*.cas)\0*.cas\0Waveform audio (*.wav)\0*.wav\0All files\0*.*\0", L"wav"));

	if (!fn.empty()) {
		ATCassetteEmulator& cas = g_sim.GetCassette();
		cas.Load(fn.c_str());
		cas.Play();
	}
}

void OnCommandCassetteUnload() {
	g_sim.GetCassette().Unload();
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

void OnCommandViewNextFilterMode() {
	switch(g_dispFilterMode) {
		case kATDisplayFilterMode_Point:
			g_dispFilterMode = kATDisplayFilterMode_Bilinear;
			break;
		case kATDisplayFilterMode_Bilinear:
			g_dispFilterMode = kATDisplayFilterMode_SharpBilinear;
			break;
		case kATDisplayFilterMode_SharpBilinear:
			g_dispFilterMode = kATDisplayFilterMode_Bicubic;
			break;
		case kATDisplayFilterMode_Bicubic:
			g_dispFilterMode = kATDisplayFilterMode_AnySuitable;
			break;
		case kATDisplayFilterMode_AnySuitable:
			g_dispFilterMode = kATDisplayFilterMode_Point;
			break;
	}

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandViewFilterModePoint() {
	g_dispFilterMode = kATDisplayFilterMode_Point;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandViewFilterModeBilinear() {
	g_dispFilterMode = kATDisplayFilterMode_Bilinear;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandViewFilterModeSharpBilinear() {
	g_dispFilterMode = kATDisplayFilterMode_SharpBilinear;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandViewFilterModeBicubic() {
	g_dispFilterMode = kATDisplayFilterMode_Bicubic;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandViewFilterModeDefault() {
	g_dispFilterMode = kATDisplayFilterMode_AnySuitable;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandViewFilterSharpness(int sharpness) {
	g_dispFilterSharpness = sharpness;

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandViewFilterSharpnessSofter() {
	OnCommandViewFilterSharpness(-2);
}

void OnCommandViewFilterSharpnessSoft() {
	OnCommandViewFilterSharpness(-1);
}

void OnCommandViewFilterSharpnessNormal() {
	OnCommandViewFilterSharpness(0);
}

void OnCommandViewFilterSharpnessSharp() {
	OnCommandViewFilterSharpness(+1);
}

void OnCommandViewFilterSharpnessSharper() {
	OnCommandViewFilterSharpness(+2);
}

void OnCommandViewStretchFitToWindow() {
	g_displayStretchMode = kATDisplayStretchMode_Unconstrained;
	ResizeDisplay();
}

void OnCommandViewStretchPreserveAspectRatio() {
	g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;
	ResizeDisplay();
}

void OnCommandViewStretchSquarePixels() {
	g_displayStretchMode = kATDisplayStretchMode_SquarePixels;
	ResizeDisplay();
}

void OnCommandViewStretchSquarePixelsInt() {
	g_displayStretchMode = kATDisplayStretchMode_Integral;
	ResizeDisplay();
}

void OnCommandViewStretchPreserveAspectRatioInt() {
	g_displayStretchMode = kATDisplayStretchMode_IntegralPreserveAspectRatio;
	ResizeDisplay();
}

void OnCommandViewOverscanOSScreen() {
	g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanOSScreen);
	ResizeDisplay();
}

void OnCommandViewOverscanNormal() {
	g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanNormal);
	ResizeDisplay();
}

void OnCommandViewOverscanExtended() {
	g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanExtended);
	ResizeDisplay();
}

void OnCommandViewOverscanFull() {
	g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanFull);
	ResizeDisplay();
}

void OnCommandViewTogglePALExtended() {
	g_sim.GetGTIA().SetOverscanPALExtended(!g_sim.GetGTIA().IsOverscanPALExtended());
	ResizeDisplay();
}

void OnCommandViewToggleFullScreen() {
	if (!g_enhancedText)
		ATSetFullscreen(!g_fullscreen);
}

void OnCommandViewToggleVSync() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetVsyncEnabled(!gtia.IsVsyncEnabled());
}

void OnCommandViewToggleFPS() {
	g_showFps = !g_showFps;
	g_winCaptionUpdater.SetShowFps(g_showFps);

	if (!g_showFps)
		g_sim.GetUIRenderer()->SetFpsIndicator(-1.0f);
}

void OnCommandViewAdjustWindowSize() {
	if (g_pMainWindow)
		g_pMainWindow->AutoSize();
}

void OnCommandViewResetWindowLayout() {
	ATLoadDefaultPaneLayout();
}


template<uint32 T_PaneId>
void OnCommandPane() {
	ATActivateUIPane(T_PaneId, true);
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

			VDPixmapStretchBltNearest(buf, *frame);

			ATCopyFrameToClipboard(g_hwnd, buf);
		}
	}
}

void OnCommandEditSaveFrame() {
	const VDPixmap *frame = g_sim.GetGTIA().GetLastFrameBuffer();

	if (frame) {
		const VDStringW fn(VDGetSaveFileName('scrn', (VDGUIHandle)g_hwnd, L"Save Screenshot", L"Portable Network Graphics (*.png)\0*.png\0", L"png"));

		if (!fn.empty())
			ATSaveFrame(g_hwnd, *frame, fn.c_str());
	}
}

void OnCommandEditCopyText() {
	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->Copy();
}

void OnCommandEditPasteText() {
	if (OpenClipboard(NULL)) {
		HANDLE hData = GetClipboardData(CF_TEXT);

		if (hData) {
			void *data = GlobalLock(hData);

			if (data) {
				size_t len = GlobalSize(hData);
				const char *s = (const char *)data;

				Paste(s, len, true);
				GlobalUnlock(hData);
			}
		}

		CloseClipboard();
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandSystemWarmReset() {
	g_sim.WarmReset();
	g_sim.Resume();
}

void OnCommandSystemColdReset() {
	g_sim.ColdReset();
	g_sim.Resume();

	if (!g_kbdOpts.mbAllowShiftOnColdReset)
		g_sim.GetPokey().SetShiftKeyState(false);
}

void OnCommandSystemTogglePauseWhenInactive() {
	g_pauseInactive = !g_pauseInactive;
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
	ATUIShowCPUOptionsDialog((VDGUIHandle)g_hwnd);
}

void OnCommandSystemToggleFPPatch() {
	g_sim.SetFPPatchEnabled(!g_sim.IsFPPatchEnabled());
}

void OnCommandSystemHardDiskDialog() {
	ATUIShowHardDiskDialog((VDGUIHandle)g_hwnd);
}

void OnCommandSystemHostDeviceDialog() {
	IATHostDeviceEmulator *hd = g_sim.GetHostDevice();

	if (hd)
		ATUIShowHostDeviceDialog((VDGUIHandle)g_hwnd, hd);
}

void OnCommandSystemPCLinkDialog() {
	ATUIShowPCLinkDialog((VDGUIHandle)g_hwnd, &g_sim);
}

void OnCommandSystemSpeedOptionsDialog() {
	ATUIShowDialogSpeedOptions((VDGUIHandle)g_hwnd);
}

void OnCommandSystemToggleKeyboardPresent() {
	g_sim.SetKeyboardPresent(!g_sim.IsKeyboardPresent());
}

void OnCommandSystemToggleForcedSelfTest() {
	g_sim.SetForcedSelfTest(!g_sim.IsForcedSelfTest());
}

template<ATHardwareMode T_Mode>
void OnCommandSystemHardwareMode() {
	ATUISwitchHardwareMode((VDGUIHandle)g_hwnd, T_Mode);
}

template<ATKernelMode T_Mode>
void OnCommandSystemKernelMode() {
	ATUISwitchKernelMode((VDGUIHandle)g_hwnd, T_Mode);
}

template<ATMemoryMode T_Mode>
void OnCommandSystemMemoryMode() {
	ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, T_Mode);
}

template<uint8 T_BankBits>
void OnCommandSystemAxlonMemoryMode() {
	if (g_sim.GetHardwareMode() != kATHardwareMode_5200 && g_sim.GetAxlonMemoryMode() != T_BankBits) {
		g_sim.SetAxlonMemoryMode(T_BankBits);
		g_sim.ColdReset();
	}
}

template<sint32 T_Banks>
void OnCommandSystemHighMemBanks() {
	if (g_sim.GetCPU().GetCPUMode() == kATCPUMode_65C816 && g_sim.GetHighMemoryBanks() != T_Banks) {
		g_sim.SetHighMemoryBanks(T_Banks);
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

void OnCommandSystemToggleMemoryRandomization() {
	g_sim.SetRandomFillEnabled(!g_sim.IsRandomFillEnabled());
}

void OnCommandSystemToggleBASIC() {
	g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled());
}

void OnCommandSystemToggleFastBoot() {
	g_sim.SetFastBootEnabled(!g_sim.IsFastBootEnabled());
}

void OnCommandSystemROMImagesDialog() {
	ATUIShowDialogROMImages((VDGUIHandle)g_hwnd, g_sim);
}

void OnCommandSystemTogglePrinter() {
	g_sim.SetPrinterEnabled(!g_sim.IsPrinterEnabled());
}

void OnCommandSystemToggleRTime8() {
	g_sim.SetRTime8Enabled(!g_sim.IsRTime8Enabled());
}

void OnCommandSystemSerialPortsDialog() {
	ATUIShowSerialPortsDialog((VDGUIHandle)g_hwnd);
}

void OnCommandSystemDragonCartDialog() {
	ATUIShowDialogDragonCart((VDGUIHandle)g_hwnd);
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

template<ATPokeyEmulator::SerialBurstMode T_BurstMode>
void OnCommandDiskBurstMode() {
	g_sim.GetPokey().SetSerialBurstMode(T_BurstMode);
}

void OnCommandDiskToggleAccurateSectorTiming() {
	for(int i=0; i<15; ++i) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
		disk.SetAccurateSectorTimingEnabled(!disk.IsAccurateSectorTimingEnabled());
	}
}

void OnCommandDiskToggleDriveSounds() {
	for(int i=0; i<15; ++i) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
		disk.SetDriveSoundsEnabled(!disk.AreDriveSoundsEnabled());
	}
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
					L"All supported types\0*.atr;*.pro;*.atx;*.xfd;*.dcm;*.zip\0"
					L"Atari disk image (*.atr, *.xfd)\0*.atr;*.xfd;*.dcm\0"
					L"Protected disk image (*.pro)\0*.pro\0"
					L"VAPI disk image (*.atx)\0*.atx\0"
					L"Zip archive (*.zip)\0*.zip\0"
					L"Gzip archive (*.gz;*.atz)\0*.gz;*.atz\0"
					L"All files\0*.*\0");

				Wait(mpFileDialogResult);
				++mStage;
				break;

			case 2:
				if (mpFileDialogResult->mbAccepted) {
					DoLoad((VDGUIHandle)g_hwnd, mpFileDialogResult->mPath.c_str(), false, false, 0, kATLoadType_Disk, NULL, mIndex);

					ATAddMRUListItem(mpFileDialogResult->mPath.c_str());
					ATUpdateMRUListMenu(g_hMenuMRU, g_pMenuMRU, ID_FILE_MRU_BASE, ID_FILE_MRU_BASE + 99);
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

template<int index>
void OnCommandDiskAttach() {
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
					for(int i=0; i<15; ++i)
						g_sim.GetDiskDrive(i).UnloadDisk();
				} else
					g_sim.GetDiskDrive(mIndex).UnloadDisk();

				MarkCompleted();
				break;
		}
	}

	const int mIndex;
	vdrefptr<ATUIFileDialogResult> mpFileDialogResult;
	vdrefptr<ATUIFutureWithResult<bool> > mpConfirmResult;
};

template<int index>
void OnCommandDiskDetach() {
	vdrefptr<ATUIFutureDetachDisk> stage(new ATUIFutureDetachDisk(index));

	ATUIPushStep(stage->GetStep());
}

void OnCommandDiskDetachAll() {
	vdrefptr<ATUIFutureDetachDisk> stage(new ATUIFutureDetachDisk(-1));

	ATUIPushStep(stage->GetStep());
}

///////////////////////////////////////////////////////////////////////////

void OnCommandVideoToggleFrameBlending() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetBlendModeEnabled(!gtia.IsBlendModeEnabled());
}

void OnCommandVideoToggleInterlace() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetInterlaceEnabled(!gtia.IsInterlaceEnabled());
}

void OnCommandVideoToggleScanlines() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetScanlinesEnabled(!gtia.AreScanlinesEnabled());
}

void OnCommandVideoToggleXEP80() {
	g_sim.SetXEP80Enabled(!g_sim.GetXEP80());

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->OnHardwareChanged();

	g_sim.ColdReset();

}

void OnCommandVideoToggleVBXE() {
	g_sim.SetVBXEEnabled(!g_sim.GetVBXE());
	g_sim.ColdReset();
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
	ATUIOpenAdjustColorsDialog((VDGUIHandle)g_hwnd);
}

template<ATGTIAEmulator::ArtifactMode T_Mode>
void OnCommandVideoArtifacting() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	gtia.SetArtifactingMode(T_Mode);
}

template<ATVideoStandard T_Mode>
void OnCommandVideoStandard() {
	// Don't allow switching to PAL or SECAM in 5200 mode!
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return;

	g_sim.SetVideoStandard(T_Mode);

	ATUIUpdateSpeedTiming();

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->OnSize();
}

void OnCommandVideoToggleStandardNTSCPAL() {
	if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
		return;

	if (g_sim.GetVideoStandard() == kATVideoStandard_NTSC)
		g_sim.SetVideoStandard(kATVideoStandard_PAL);
	else
		g_sim.SetVideoStandard(kATVideoStandard_NTSC);

	ATUIUpdateSpeedTiming();

	IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->OnSize();
}

void OnCommandVideoEnhancedModeNone() {
		g_enhancedText = 0;
		g_sim.SetVirtualScreenEnabled(false);
}

void OnCommandVideoEnhancedModeHardware() {
	g_enhancedText = 1;
	g_sim.SetVirtualScreenEnabled(false);
	ATSetFullscreen(false);
}

void OnCommandVideoEnhancedModeCIO() {
	if (g_enhancedText != 2) {
		g_enhancedText = 2;
		g_sim.SetVirtualScreenEnabled(true);
		ATSetFullscreen(false);

		// push a break to attempt to kick out of the OS get byte routine
		g_sim.GetPokey().PushBreak();
	}
}

///////////////////////////////////////////////////////////////////////////

void OnCommandAudioToggleStereo() {
	g_sim.SetDualPokeysEnabled(!g_sim.IsDualPokeysEnabled());
}

void OnCommandAudioToggleMonitor() {
	g_sim.SetAudioMonitorEnabled(!g_sim.IsAudioMonitorEnabled());
}

void OnCommandAudioToggleMute() {
	IATAudioOutput *out = g_sim.GetAudioOutput();

	if (out)
		out->SetMute(!out->GetMute());
}

void OnCommandAudioOptionsDialog() {
	ATUIShowAudioOptionsDialog((VDGUIHandle)g_hwnd);
}

void OnCommandAudioToggleNonlinearMixing() {
	ATPokeyEmulator& pokey = g_sim.GetPokey();

	pokey.SetNonlinearMixingEnabled(!pokey.IsNonlinearMixingEnabled());
}

template<int T_Channel>
void OnCommandAudioToggleChannel() {
	ATPokeyEmulator& pokey = g_sim.GetPokey();
	pokey.SetChannelEnabled(T_Channel, !pokey.IsChannelEnabled(T_Channel));
}

void OnCommandAudioToggleSoundBoard() {
	g_sim.SetSoundBoardEnabled(!g_sim.GetSoundBoard());
	g_sim.ColdReset();
}

template<uint16 T_BaseAddress>
void OnCommandAudioSoundBoardAddr() {
	g_sim.SetSoundBoardMemBase(T_BaseAddress);

	// The reason why we reset on these is not because the SoundBoard itself needs it,
	// but because we may need to adjust the hook page.
	g_sim.ColdReset();
}

void OnCommandAudioToggleSlightSid() {
	g_sim.SetSlightSIDEnabled(!g_sim.GetSlightSID());
	g_sim.ColdReset();
}

void OnCommandAudioToggleCovox() {
	g_sim.SetCovoxEnabled(!g_sim.GetCovox());
	g_sim.ColdReset();
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
	SaveInputMaps();
	ATReloadPortMenus();
}

void OnCommandInputKeyboardDialog() {
	if (ATUIShowDialogKeyboardOptions((VDGUIHandle)g_hwnd, g_kbdOpts))
		ATUIInitVirtualKeyMap(g_kbdOpts);
}

void OnCommandInputLightPenDialog() {
	ATUIShowDialogLightPen((VDGUIHandle)g_hwnd, g_sim.GetLightPenPort());
}

///////////////////////////////////////////////////////////////////////////

void OnCommandRecordStop() {
	CheckRecordingExceptions();
	StopRecording();
}

void OnCommandRecordRawAudio() {
	if (!g_pAudioWriter && !g_pVideoWriter) {
		VDStringW s(VDGetSaveFileName('raud', (VDGUIHandle)g_hwnd, L"Record raw audio", L"Raw 32-bit float data\0*.pcm\0", L"pcm"));

		if (!s.empty()) {
			g_pAudioWriter = new ATAudioWriter(s.c_str(), true, g_sim.IsDualPokeysEnabled(), g_sim.GetVideoStandard() != kATVideoStandard_NTSC, g_sim.GetUIRenderer());
			g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter);
		}
	}
}

void OnCommandRecordAudio() {
	if (!g_pAudioWriter && !g_pVideoWriter) {
		VDStringW s(VDGetSaveFileName('raud', (VDGUIHandle)g_hwnd, L"Record audio", L"Wave audio (*.wav)\0*.wav\0", L"wav"));

		if (!s.empty()) {
			g_pAudioWriter = new ATAudioWriter(s.c_str(), false, g_sim.IsDualPokeysEnabled(), g_sim.GetVideoStandard() != kATVideoStandard_NTSC, g_sim.GetUIRenderer());
			g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter);
		}
	}
}

void OnCommandRecordVideo() {
	if (!g_pAudioWriter && !g_pVideoWriter) {
		VDStringW s(VDGetSaveFileName('rvid', (VDGUIHandle)g_hwnd, L"Record raw video", L"Audio/visual interleaved (*.avi)\0*.avi\0", L"avi"));

		if (!s.empty()) {
			const bool hz50 = g_sim.GetVideoStandard() != kATVideoStandard_NTSC;

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

///////////////////////////////////////////////////////////////////////////

void OnCommandDebuggerOpenSourceFile() {
	VDStringW fn(VDGetLoadFileName('src ', (VDGUIHandle)g_hwnd, L"Load source file", L"All files (*.*)\0*.*\0", NULL));

	if (!fn.empty()) {
		ATOpenSourceWindow(fn.c_str());
	}
}

void OnCommandDebuggerToggleBreakAtExeRun() {
	IATDebugger *dbg = ATGetDebugger();

	dbg->SetBreakOnEXERunAddrEnabled(!dbg->IsBreakOnEXERunAddrEnabled());
}

void OnCommandDebugToggleAutoReloadRoms() {
	g_sim.SetROMAutoReloadEnabled(!g_sim.IsROMAutoReloadEnabled());
}

void OnCommandDebugToggleAutoLoadKernelSymbols() {
	g_sim.SetAutoLoadKernelSymbolsEnabled(!g_sim.IsAutoLoadKernelSymbolsEnabled());
}

void OnCommandDebugChangeFontDialog() {
	ATUIShowDialogDebugFont((VDGUIHandle)g_hwnd);
}

void OnCommandDebugToggleDebugger() {
	if (ATIsDebugConsoleActive()) {
		if (!g_sim.IsRunning())
			ATGetDebugger()->Detach();

		ATCloseConsole();
	} else
		ATOpenConsole();
}

void OnCommandDebugRun() {
	ATGetDebugger()->Run(kATDebugSrcMode_Same);
}

void OnCommandDebugBreak() {
	ATOpenConsole();
	ATGetDebugger()->Break();
}

void OnCommandDebugRunStop() {
	if (g_sim.IsRunning() || ATGetDebugger()->AreCommandsQueued())
		OnCommandDebugBreak();
	else
		OnCommandDebugRun();
}

ATDebugSrcMode ATUIGetDebugSrcMode() {
	ATDebugSrcMode mode = kATDebugSrcMode_Same;

	const uint32 activePaneId = ATUIGetActivePaneId();

	if (activePaneId == kATUIPaneId_Disassembly)
		mode = kATDebugSrcMode_Disasm;

	if (activePaneId >= kATUIPaneId_Source)
		mode = kATDebugSrcMode_Source;

	return mode;
}

void OnCommandDebugStepInto() {
	IATUIDebuggerPane *dbgp = vdpoly_cast<IATUIDebuggerPane *>(ATUIGetActivePane());

	if (!dbgp || !dbgp->OnPaneCommand(kATUIPaneCommandId_DebugStepInto))
		ATGetDebugger()->StepInto(ATUIGetDebugSrcMode());
}

void OnCommandDebugStepOut() {
	IATUIDebuggerPane *dbgp = vdpoly_cast<IATUIDebuggerPane *>(ATUIGetActivePane());

	if (!dbgp || !dbgp->OnPaneCommand(kATUIPaneCommandId_DebugStepOut))
		ATGetDebugger()->StepOut(ATUIGetDebugSrcMode());
}

void OnCommandDebugStepOver() {
	IATUIDebuggerPane *dbgp = vdpoly_cast<IATUIDebuggerPane *>(ATUIGetActivePane());

	if (!dbgp || !dbgp->OnPaneCommand(kATUIPaneCommandId_DebugStepOver))
		ATGetDebugger()->StepOver(ATUIGetDebugSrcMode());
}

void OnCommandDebugToggleBreakpoint() {
	IATUIDebuggerPane *dbgp = vdpoly_cast<IATUIDebuggerPane *>(ATUIGetActivePane());

	if (dbgp)
		dbgp->OnPaneCommand(kATUIPaneCommandId_DebugToggleBreakpoint);
}

void OnCommandDebugVerifierDialog() {
	ATUIShowDialogVerifier((VDGUIHandle)g_hwnd, g_sim);
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

void OnCommandWindowClose() {
	if (!g_pMainWindow)
		return;

	if (g_pMainWindow->GetModalFrame() || g_pMainWindow->GetFullScreenFrame())
		return;

	ATFrameWindow *w = g_pMainWindow->GetActiveFrame();
	if (!w)
		return;

	g_pMainWindow->CloseFrame(w);
}

void OnCommandWindowUndock() {
	if (!g_pMainWindow)
		return;

	if (g_pMainWindow->GetModalFrame() || g_pMainWindow->GetFullScreenFrame())
		return;

	ATFrameWindow *w = g_pMainWindow->GetActiveFrame();
	if (!w)
		return;

	g_pMainWindow->UndockFrame(w);
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

namespace {
	typedef bool (*BoolTestFn)();

	template<BoolTestFn A>
	bool Not() { return !A(); }

	template<BoolTestFn A, BoolTestFn B>
	bool And() { return A() && B(); }

	template<BoolTestFn A, BoolTestFn B>
	bool Or() { return A() || B(); }

	template<BoolTestFn A>
	ATUICmdState CheckedIf() { return A() ? kATUICmdState_Checked : kATUICmdState_None; }

	template<BoolTestFn A>
	ATUICmdState RadioCheckedIf() { return A() ? kATUICmdState_RadioChecked : kATUICmdState_None; }

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

	bool IsSoundBoardEnabled() {
		return g_sim.GetSoundBoard() != NULL;
	}

	template<uint16 T_BaseAddr>
	bool SoundBoardBaseIs() {
		return g_sim.GetSoundBoardMemBase() == T_BaseAddr;
	}

	bool IsSlightSidEnabled() {
		return g_sim.GetSlightSID() != NULL;
	}

	bool IsCovoxEnabled() {
		return g_sim.GetCovox() != NULL;
	}

	bool IsXEP80Enabled() {
		return g_sim.GetXEP80() != NULL;
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

	bool IsSIDEEnabled() {
		return g_sim.GetSIDE() != NULL;
	}

	bool IsSIDESDXEnabled() {
		ATSIDEEmulator *side = g_sim.GetSIDE();
		return side && side->IsSDXEnabled();
	}

	template<ATHardwareMode T_Mode>
	bool HardwareModeIs() {
		return g_sim.GetHardwareMode() == T_Mode;
	}

	template<ATKernelMode T_Mode>
	bool KernelModeIs() {
		return g_sim.GetKernelMode() == T_Mode;
	}

	template<ATMemoryMode T_Mode>
	bool MemoryModeIs() {
		return g_sim.GetMemoryMode() == T_Mode;
	}

	template<uint8 T_BankBits>
	bool AxlonMemoryModeIs() {
		return g_sim.GetAxlonMemoryMode() == T_BankBits;
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
		return g_dispFilterMode == T_Mode;
	}

	template<ATGTIAEmulator::OverscanMode T_Mode>
	bool VideoOverscanModeIs() {
		return g_sim.GetGTIA().GetOverscanMode() == T_Mode;
	}

	template<ATDisplayStretchMode T_Mode>
	bool VideoStretchModeIs() {
		return g_displayStretchMode == T_Mode;
	}

	template<int T_Mode>
	bool VideoEnhancedTextModeIs() {
		return g_enhancedText == T_Mode;
	}

	template<int T_Value>
	bool VideoFilterSharpnessIs() {
		return g_dispFilterSharpness == T_Value;
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

	template<int index>
	bool IsDiskAttached() {
		return g_sim.GetDiskDrive(index).IsDiskLoaded();
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
		return g_sim.GetDiskDrive(0).AreDriveSoundsEnabled();
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
		return g_fullscreen;
	}

	bool IsPauseWhenInactiveEnabled() {
		return g_pauseInactive;
	}

	bool IsVideoFrameAvailable() {
		return g_sim.GetGTIA().GetLastFrameBuffer() != NULL;
	}

	bool IsRecording() {
		return g_pAudioWriter != NULL || g_pVideoWriter != NULL;
	}

	bool IsRecordingRawAudio() {
		return g_pAudioWriter != NULL && g_pAudioWriter->IsRecordingRaw();
	}

	bool IsRecordingAudio() {
		return g_pAudioWriter != NULL && !g_pAudioWriter->IsRecordingRaw();
	}

	bool IsRecordingVideo() {
		return g_pVideoWriter != NULL;
	}

	bool IsCopyTextAvailable() {
		IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
		return pane && pane->IsTextSelected();
	}

	bool IsClipboardTextAvailable() {
		return !!IsClipboardFormatAvailable(CF_TEXT);
	}

	bool IsAudioMuted() {
		return g_sim.GetAudioOutput()->GetMute();
	}

	bool IsMouseMapped() {
		return g_sim.GetInputManager()->IsMouseMapped();
	}

	bool IsMouseCaptured() {
		return g_mouseCaptured || g_mouseClipped;
	}

	bool IsMouseAutoCaptureEnabled() {
		return g_mouseAutoCapture;
	}

	bool IsBreakAtExeRunAddrEnabled() {
		return ATGetDebugger()->IsBreakOnEXERunAddrEnabled();
	}

	template<int Index>
	bool PokeyChannelEnabled() {
		return g_sim.GetPokey().IsChannelEnabled(Index);
	}

	template<ATPokeyEmulator::SerialBurstMode T_Mode>
	bool PokeySerialBurstModeIs() {
		return g_sim.GetPokey().GetSerialBurstMode() == T_Mode;
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

	bool CanManipulateWindows() {
		return g_pMainWindow && g_pMainWindow->GetActiveFrame() && !g_pMainWindow->GetModalFrame() && !g_pMainWindow->GetFullScreenFrame();
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

		{ "Cart.AttachSC3D", OnCommandAttachNewCartridge<kATCartridgeMode_SuperCharger3D>, NULL },
		{ "Cart.AttachMaxFlash1MB", OnCommandAttachNewCartridge<kATCartridgeMode_MaxFlash_128K>, NULL },
		{ "Cart.AttachMaxFlash1MBMyIDE", OnCommandAttachNewCartridge<kATCartridgeMode_MaxFlash_128K_MyIDE>, NULL },
		{ "Cart.AttachMaxFlash8MB", OnCommandAttachNewCartridge<kATCartridgeMode_MaxFlash_1024K>, NULL },
		{ "Cart.AttachMaxFlash8MBBank0", OnCommandAttachNewCartridge<kATCartridgeMode_MaxFlash_1024K_Bank0>, NULL },
		{ "Cart.AttachSIC", OnCommandAttachNewCartridge<kATCartridgeMode_SIC>, NULL },
		{ "Cart.AttachMegaCart512K", OnCommandAttachNewCartridge<kATCartridgeMode_MegaCart_512K_3>, NULL },
		{ "Cart.AttachMegaCart4MB", OnCommandAttachNewCartridge<kATCartridgeMode_MegaCart_4M_3>, NULL },
		{ "Cart.AttachTheCart32MB", OnCommandAttachNewCartridge<kATCartridgeMode_TheCart_32M>, NULL },
		{ "Cart.AttachTheCart64MB", OnCommandAttachNewCartridge<kATCartridgeMode_TheCart_64M>, NULL },
		{ "Cart.AttachTheCart128MB", OnCommandAttachNewCartridge<kATCartridgeMode_TheCart_128M>, NULL },
		{ "Cart.AttachBASIC", OnCommandAttachCartridgeBASIC, NULL },

		{ "Cart.ActivateMenuButton", OnCommandCartActivateMenuButton, IsSIDEEnabled },
		{ "Cart.ToggleSwitch", OnCommandCartToggleSwitch, IsSIDEEnabled, CheckedIf<SimTest<&ATSimulator::GetCartridgeSwitch> > },

		{ "System.SaveFirmwareIDEMain", OnCommandSaveFirmwareIDEMain, IsStoragePresent<kATStorageId_Firmware> },
		{ "System.SaveFirmwareIDESDX", OnCommandSaveFirmwareIDESDX, IsStoragePresent<(ATStorageId)(kATStorageId_Firmware + 1)> },
		{ "System.SaveFirmwareU1MB", OnCommandSaveFirmwareU1MB, IsStoragePresent<(ATStorageId)(kATStorageId_Firmware + 2)> },
		{ "File.Exit", OnCommandExit, NULL },

		{ "Cassette.Load", OnCommandCassetteLoad, NULL },
		{ "Cassette.Unload", OnCommandCassetteUnload, IsCassetteLoaded },
		{ "Cassette.TapeControlDialog", OnCommandCassetteTapeControlDialog, NULL },
		{ "Cassette.ToggleSIOPatch", OnCommandCassetteToggleSIOPatch, NULL, CheckedIf<SimTest<&ATSimulator::IsCassetteSIOPatchEnabled> > },
		{ "Cassette.ToggleAutoBoot", OnCommandCassetteToggleAutoBoot, NULL, CheckedIf<SimTest<&ATSimulator::IsCassetteAutoBootEnabled> > },
		{ "Cassette.ToggleLoadDataAsAudio", OnCommandCassetteToggleLoadDataAsAudio, NULL, CheckedIf<IsCassetteLoadDataAsAudioEnabled> },

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
		{ "View.TogglePALExtended", OnCommandViewTogglePALExtended, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::IsOverscanPALExtended> > },

		{ "View.ToggleFullScreen", OnCommandViewToggleFullScreen, VideoEnhancedTextModeIs<0>, CheckedIf<IsFullScreen> },
		{ "View.ToggleVSync", OnCommandViewToggleVSync, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::IsVsyncEnabled> > },
		{ "View.ToggleFPS", OnCommandViewToggleFPS, NULL, CheckedIf<IsShowFpsEnabled> },
		{ "View.AdjustWindowSize", OnCommandViewAdjustWindowSize },
		{ "View.ResetWindowLayout", OnCommandViewResetWindowLayout },

		{ "View.NextANTICVisMode", OnCommandAnticVisualizationNext },
		{ "View.NextGTIAVisMode", OnCommandGTIAVisualizationNext },

		{ "View.ToggleXEP80View", OnCommandVideoToggleXEP80View, IsXEP80Enabled, CheckedIf<IsXEP80ViewEnabled> },
		{ "View.ToggleXEP80ViewAutoswitching", OnCommandVideoToggleXEP80ViewAutoswitching, IsXEP80Enabled, CheckedIf<IsXEP80ViewAutoswitchingEnabled> },

		{ "Pane.Display", OnCommandPane<kATUIPaneId_Display> },
		{ "Pane.PrinterOutput", OnCommandPane<kATUIPaneId_PrinterOutput> },
		{ "Pane.Console", OnCommandPane<kATUIPaneId_Console>, IsDebuggerEnabled },
		{ "Pane.Registers", OnCommandPane<kATUIPaneId_Registers>, IsDebuggerEnabled },
		{ "Pane.Disassembly", OnCommandPane<kATUIPaneId_Disassembly>, IsDebuggerEnabled },
		{ "Pane.CallStack", OnCommandPane<kATUIPaneId_CallStack>, IsDebuggerEnabled },
		{ "Pane.History", OnCommandPane<kATUIPaneId_History>, IsDebuggerEnabled },
		{ "Pane.Memory1", OnCommandPane<kATUIPaneId_MemoryN + 0>, IsDebuggerEnabled },
		{ "Pane.Memory2", OnCommandPane<kATUIPaneId_MemoryN + 1>, IsDebuggerEnabled },
		{ "Pane.Memory3", OnCommandPane<kATUIPaneId_MemoryN + 2>, IsDebuggerEnabled },
		{ "Pane.Memory4", OnCommandPane<kATUIPaneId_MemoryN + 3>, IsDebuggerEnabled },
		{ "Pane.Watch1", OnCommandPane<kATUIPaneId_WatchN + 0>, IsDebuggerEnabled },
		{ "Pane.Watch2", OnCommandPane<kATUIPaneId_WatchN + 1>, IsDebuggerEnabled },
		{ "Pane.Watch3", OnCommandPane<kATUIPaneId_WatchN + 2>, IsDebuggerEnabled },
		{ "Pane.Watch4", OnCommandPane<kATUIPaneId_WatchN + 3>, IsDebuggerEnabled },
		{ "Pane.DebugDisplay", OnCommandPane<kATUIPaneId_DebugDisplay>, IsDebuggerEnabled },
		{ "Pane.ProfileView", OnCommandPane<kATUIPaneId_Profiler>, IsDebuggerEnabled },

		{ "Edit.CopyFrame", OnCommandEditCopyFrame, IsVideoFrameAvailable },
		{ "Edit.SaveFrame", OnCommandEditSaveFrame, IsVideoFrameAvailable },
		{ "Edit.CopyText", OnCommandEditCopyText, IsCopyTextAvailable },
		{ "Edit.PasteText", OnCommandEditPasteText, IsClipboardTextAvailable },

		{ "System.WarmReset", OnCommandSystemWarmReset },
		{ "System.ColdReset", OnCommandSystemColdReset },

		{ "System.TogglePause", OnCommandSystemTogglePause },
		{ "System.TogglePauseWhenInactive", OnCommandSystemTogglePauseWhenInactive, NULL, CheckedIf<IsPauseWhenInactiveEnabled> },
		{ "System.ToggleSlowMotion", OnCommandSystemToggleSlowMotion, NULL, CheckedIf<IsSlowMotion> },
		{ "System.ToggleWarpSpeed", OnCommandSystemToggleWarpSpeed, NULL, CheckedIf<IsWarpSpeed> },
		{ "System.PulseWarpOff", OnCommandSystemPulseWarpOff, NULL, NULL },
		{ "System.PulseWarpOn", OnCommandSystemPulseWarpOn, NULL, NULL },
		{ "System.CPUOptionsDialog", OnCommandSystemCPUOptionsDialog },
		{ "System.ToggleFPPatch", OnCommandSystemToggleFPPatch, NULL, CheckedIf<SimTest<&ATSimulator::IsFPPatchEnabled> > },
		{ "System.HardDiskDialog", OnCommandSystemHardDiskDialog },
		{ "System.HostDeviceDialog", OnCommandSystemHostDeviceDialog },
		{ "System.PCLinkDialog", OnCommandSystemPCLinkDialog },
		{ "System.SpeedOptionsDialog", OnCommandSystemSpeedOptionsDialog },

		{ "System.ToggleKeyboardPresent", OnCommandSystemToggleKeyboardPresent, HardwareModeIs<kATHardwareMode_XEGS>, CheckedIf<And<HardwareModeIs<kATHardwareMode_XEGS>, SimTest<&ATSimulator::IsKeyboardPresent> > >},
		{ "System.ToggleForcedSelfTest", OnCommandSystemToggleForcedSelfTest, IsXLHardware, CheckedIf<And<IsXLHardware, SimTest<&ATSimulator::IsForcedSelfTest> > > },

		{ "System.HardwareMode800", OnCommandSystemHardwareMode<kATHardwareMode_800>, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_800> > },
		{ "System.HardwareMode800XL", OnCommandSystemHardwareMode<kATHardwareMode_800XL>, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_800XL> > },
		{ "System.HardwareMode1200XL", OnCommandSystemHardwareMode<kATHardwareMode_1200XL>, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_1200XL> > },
		{ "System.HardwareModeXEGS", OnCommandSystemHardwareMode<kATHardwareMode_XEGS>, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_XEGS> > },
		{ "System.HardwareMode130XE", OnCommandSystemHardwareMode<kATHardwareMode_130XE>, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_130XE> > },
		{ "System.HardwareMode5200", OnCommandSystemHardwareMode<kATHardwareMode_5200>, NULL, RadioCheckedIf<HardwareModeIs<kATHardwareMode_5200> > },

		{ "System.KernelModeDefault", OnCommandSystemKernelMode<kATKernelMode_Default>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_Default> > },
		{ "System.KernelModeOSA", OnCommandSystemKernelMode<kATKernelMode_OSA>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_OSA> > },
		{ "System.KernelModeOSB", OnCommandSystemKernelMode<kATKernelMode_OSB>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_OSB> > },
		{ "System.KernelModeXL", OnCommandSystemKernelMode<kATKernelMode_XL>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_XL> > },
		{ "System.KernelMode1200XL", OnCommandSystemKernelMode<kATKernelMode_1200XL>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_1200XL> > },
		{ "System.KernelModeXEGS", OnCommandSystemKernelMode<kATKernelMode_XEGS>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_XEGS> > },
		{ "System.KernelModeOther", OnCommandSystemKernelMode<kATKernelMode_Other>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_Other> > },
		{ "System.KernelModeHLE", OnCommandSystemKernelMode<kATKernelMode_HLE>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_HLE> > },
		{ "System.KernelModeLLE", OnCommandSystemKernelMode<kATKernelMode_LLE_OSB>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_LLE_OSB> > },
		{ "System.KernelModeLLEXL", OnCommandSystemKernelMode<kATKernelMode_LLE_XL>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_LLE_XL> > },
		{ "System.KernelMode5200", OnCommandSystemKernelMode<kATKernelMode_5200>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_5200> > },
		{ "System.KernelMode5200LLE", OnCommandSystemKernelMode<kATKernelMode_5200_LLE>, NULL, RadioCheckedIf<KernelModeIs<kATKernelMode_5200_LLE> > },

		{ "System.MemoryMode8K", OnCommandSystemMemoryMode<kATMemoryMode_8K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_8K> > },
		{ "System.MemoryMode16K", OnCommandSystemMemoryMode<kATMemoryMode_16K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Not<HardwareModeIs<kATHardwareMode_1200XL> > >, RadioCheckedIf<MemoryModeIs<kATMemoryMode_16K> > },
		{ "System.MemoryMode24K", OnCommandSystemMemoryMode<kATMemoryMode_24K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_24K> > },
		{ "System.MemoryMode32K", OnCommandSystemMemoryMode<kATMemoryMode_32K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_32K> > },
		{ "System.MemoryMode40K", OnCommandSystemMemoryMode<kATMemoryMode_40K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_40K> > },
		{ "System.MemoryMode48K", OnCommandSystemMemoryMode<kATMemoryMode_48K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_48K> > },
		{ "System.MemoryMode52K", OnCommandSystemMemoryMode<kATMemoryMode_52K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, Is800>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_52K> > },
		{ "System.MemoryMode64K", OnCommandSystemMemoryMode<kATMemoryMode_64K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_64K> > },
		{ "System.MemoryMode128K", OnCommandSystemMemoryMode<kATMemoryMode_128K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_128K> > },
		{ "System.MemoryMode320K", OnCommandSystemMemoryMode<kATMemoryMode_320K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_320K> > },
		{ "System.MemoryMode320KCompy", OnCommandSystemMemoryMode<kATMemoryMode_320K_Compy>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_320K_Compy> > },
		{ "System.MemoryMode576K", OnCommandSystemMemoryMode<kATMemoryMode_576K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_576K> > },
		{ "System.MemoryMode576KCompy", OnCommandSystemMemoryMode<kATMemoryMode_576K_Compy>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_576K_Compy> > },
		{ "System.MemoryMode1088K", OnCommandSystemMemoryMode<kATMemoryMode_1088K>, And<Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> >, IsNot5200>, RadioCheckedIf<MemoryModeIs<kATMemoryMode_1088K> > },

		{ "System.AxlonMemoryNone", OnCommandSystemAxlonMemoryMode<0>, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<0> > },
		{ "System.AxlonMemory64K", OnCommandSystemAxlonMemoryMode<2>, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<2> > },
		{ "System.AxlonMemory128K", OnCommandSystemAxlonMemoryMode<3>, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<3> > },
		{ "System.AxlonMemory256K", OnCommandSystemAxlonMemoryMode<4>, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<4> > },
		{ "System.AxlonMemory512K", OnCommandSystemAxlonMemoryMode<5>, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<5> > },
		{ "System.AxlonMemory1024K", OnCommandSystemAxlonMemoryMode<6>, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<6> > },
		{ "System.AxlonMemory2048K", OnCommandSystemAxlonMemoryMode<7>, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<7> > },
		{ "System.AxlonMemory4096K", OnCommandSystemAxlonMemoryMode<8>, IsNot5200, RadioCheckedIf<AxlonMemoryModeIs<8> > },

		{ "System.HighMemoryNA", OnCommandSystemHighMemBanks<-1>, Is65C816, RadioCheckedIf<HighMemBanksIs<-1> > },
		{ "System.HighMemoryNone", OnCommandSystemHighMemBanks<0>, Is65C816, RadioCheckedIf<HighMemBanksIs<0> > },
		{ "System.HighMemory64K", OnCommandSystemHighMemBanks<1>, Is65C816, RadioCheckedIf<HighMemBanksIs<1> > },
		{ "System.HighMemory192K", OnCommandSystemHighMemBanks<3>, Is65C816, RadioCheckedIf<HighMemBanksIs<3> > },
		{ "System.HighMemory960K", OnCommandSystemHighMemBanks<15>, Is65C816, RadioCheckedIf<HighMemBanksIs<15> > },
		{ "System.HighMemory4032K", OnCommandSystemHighMemBanks<63>, Is65C816, RadioCheckedIf<HighMemBanksIs<63> > },

		{ "System.ToggleMapRAM", OnCommandSystemToggleMapRAM, NULL, CheckedIf<SimTest<&ATSimulator::IsMapRAMEnabled> > },
		{ "System.ToggleUltimate1MB", OnCommandSystemToggleUltimate1MB, NULL, CheckedIf<SimTest<&ATSimulator::IsUltimate1MBEnabled> > },
		{ "System.ToggleMemoryRandomization", OnCommandSystemToggleMemoryRandomization, NULL, CheckedIf<SimTest<&ATSimulator::IsRandomFillEnabled> > },

		{ "System.ToggleBASIC", OnCommandSystemToggleBASIC, SupportsBASIC, CheckedIf<And<SupportsBASIC, SimTest<&ATSimulator::IsBASICEnabled> > > },
		{ "System.ToggleFastBoot", OnCommandSystemToggleFastBoot, IsNot5200, CheckedIf<SimTest<&ATSimulator::IsFastBootEnabled> > },
		{ "System.ROMImagesDialog", OnCommandSystemROMImagesDialog },
		{ "System.TogglePrinter", OnCommandSystemTogglePrinter, NULL, CheckedIf<SimTest<&ATSimulator::IsPrinterEnabled> > },
		{ "System.ToggleRTime8", OnCommandSystemToggleRTime8, NULL, CheckedIf<SimTest<&ATSimulator::IsRTime8Enabled> > },
		{ "System.SerialPortsDialog", OnCommandSystemSerialPortsDialog, NULL, CheckedIf<SimTest<&ATSimulator::IsRS232Enabled> > },

		{ "System.DragonCartDialog", OnCommandSystemDragonCartDialog, NULL, CheckedIf<SimTest<&ATSimulator::IsDragonCartEnabled> > },

		{ "Disk.DrivesDialog", OnCommandDiskDrivesDialog },
		{ "Disk.ToggleAllEnabled", OnCommandDiskToggleAllEnabled, NULL, CheckedIf<IsDiskEnabled> },
		{ "Disk.ToggleSIOPatch", OnCommandDiskToggleSIOPatch, NULL, CheckedIf<SimTest<&ATSimulator::IsDiskSIOPatchEnabled> > },
		{ "Disk.ToggleSIOOverrideDetection", OnCommandDiskToggleSIOOverrideDetection, NULL, CheckedIf<SimTest<&ATSimulator::IsDiskSIOOverrideDetectEnabled> > },
		{ "Disk.BurstModeNone", OnCommandDiskBurstMode<ATPokeyEmulator::kSerialBurstMode_Disabled>, NULL, RadioCheckedIf<PokeySerialBurstModeIs<ATPokeyEmulator::kSerialBurstMode_Disabled> > },
		{ "Disk.BurstModeStandard", OnCommandDiskBurstMode<ATPokeyEmulator::kSerialBurstMode_Standard>, NULL, RadioCheckedIf<PokeySerialBurstModeIs<ATPokeyEmulator::kSerialBurstMode_Standard> > },
		{ "Disk.BurstModePolled", OnCommandDiskBurstMode<ATPokeyEmulator::kSerialBurstMode_Polled>, NULL, RadioCheckedIf<PokeySerialBurstModeIs<ATPokeyEmulator::kSerialBurstMode_Polled> > },
		{ "Disk.ToggleAccurateSectorTiming", OnCommandDiskToggleAccurateSectorTiming, NULL, CheckedIf<IsDiskAccurateSectorTimingEnabled> },
		{ "Disk.ToggleDriveSounds", OnCommandDiskToggleDriveSounds, NULL, CheckedIf<AreDriveSoundsEnabled> },
		{ "Disk.ToggleSectorCounter", OnCommandDiskToggleSectorCounter, NULL, CheckedIf<SimTest<&ATSimulator::IsDiskSectorCounterEnabled> > },

		{ "Disk.Attach1", OnCommandDiskAttach<0>, NULL, NULL, UIFormatMenuItemDiskName<0> },
		{ "Disk.Attach2", OnCommandDiskAttach<1>, NULL, NULL, UIFormatMenuItemDiskName<1> },
		{ "Disk.Attach3", OnCommandDiskAttach<2>, NULL, NULL, UIFormatMenuItemDiskName<2> },
		{ "Disk.Attach4", OnCommandDiskAttach<3>, NULL, NULL, UIFormatMenuItemDiskName<3> },
		{ "Disk.Attach5", OnCommandDiskAttach<4>, NULL, NULL, UIFormatMenuItemDiskName<4> },
		{ "Disk.Attach6", OnCommandDiskAttach<5>, NULL, NULL, UIFormatMenuItemDiskName<5> },
		{ "Disk.Attach7", OnCommandDiskAttach<6>, NULL, NULL, UIFormatMenuItemDiskName<6> },
		{ "Disk.Attach8", OnCommandDiskAttach<7>, NULL, NULL, UIFormatMenuItemDiskName<7> },

		{ "Disk.DetachAll", OnCommandDiskDetachAll, IsDiskAttached<0> },
		{ "Disk.Detach1", OnCommandDiskDetach<0>, IsDiskAttached<0>, NULL, UIFormatMenuItemDiskName<0> },
		{ "Disk.Detach2", OnCommandDiskDetach<1>, IsDiskAttached<1>, NULL, UIFormatMenuItemDiskName<1> },
		{ "Disk.Detach3", OnCommandDiskDetach<2>, IsDiskAttached<2>, NULL, UIFormatMenuItemDiskName<2> },
		{ "Disk.Detach4", OnCommandDiskDetach<3>, IsDiskAttached<3>, NULL, UIFormatMenuItemDiskName<3> },
		{ "Disk.Detach5", OnCommandDiskDetach<4>, IsDiskAttached<4>, NULL, UIFormatMenuItemDiskName<4> },
		{ "Disk.Detach6", OnCommandDiskDetach<5>, IsDiskAttached<5>, NULL, UIFormatMenuItemDiskName<5> },
		{ "Disk.Detach7", OnCommandDiskDetach<6>, IsDiskAttached<6>, NULL, UIFormatMenuItemDiskName<6> },
		{ "Disk.Detach8", OnCommandDiskDetach<7>, IsDiskAttached<7>, NULL, UIFormatMenuItemDiskName<7> },

		{ "Video.StandardNTSC", OnCommandVideoStandard<kATVideoStandard_NTSC>, NULL, RadioCheckedIf<VideoStandardIs<kATVideoStandard_NTSC> > },
		{ "Video.StandardPAL", OnCommandVideoStandard<kATVideoStandard_PAL>, IsNot5200, RadioCheckedIf<VideoStandardIs<kATVideoStandard_PAL> > },
		{ "Video.StandardSECAM", OnCommandVideoStandard<kATVideoStandard_SECAM>, IsNot5200, RadioCheckedIf<VideoStandardIs<kATVideoStandard_SECAM> > },
		{ "Video.ToggleStandardNTSCPAL", OnCommandVideoToggleStandardNTSCPAL, IsNot5200 },

		{ "Video.ToggleFrameBlending", OnCommandVideoToggleFrameBlending, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::IsBlendModeEnabled> > },
		{ "Video.ToggleInterlace", OnCommandVideoToggleInterlace, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::IsInterlaceEnabled> > },
		{ "Video.ToggleScanlines", OnCommandVideoToggleScanlines, NULL, CheckedIf<GTIATest<&ATGTIAEmulator::AreScanlinesEnabled> > },
		{ "Video.ToggleXEP80", OnCommandVideoToggleXEP80, NULL, CheckedIf<IsXEP80Enabled> },
		{ "Video.ToggleVBXE", OnCommandVideoToggleVBXE, NULL, CheckedIf<IsVBXEEnabled> },
		{ "Video.ToggleVBXESharedMemory", OnCommandVideoToggleVBXESharedMemory, IsVBXEEnabled, CheckedIf<SimTest<&ATSimulator::IsVBXESharedMemoryEnabled> > },
		{ "Video.ToggleVBXEAltPage", OnCommandVideoToggleVBXEAltPage, And<IsVBXEEnabled, Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> > >, CheckedIf<SimTest<&ATSimulator::IsVBXEAltPageEnabled> > },
		{ "Video.AdjustColorsDialog", OnCommandVideoAdjustColorsDialog },

		{ "Video.ArtifactingNone", OnCommandVideoArtifacting<ATGTIAEmulator::kArtifactNone>, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactNone> > },
		{ "Video.ArtifactingNTSC", OnCommandVideoArtifacting<ATGTIAEmulator::kArtifactNTSC>, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactNTSC> > },
		{ "Video.ArtifactingNTSCHi", OnCommandVideoArtifacting<ATGTIAEmulator::kArtifactNTSCHi>, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactNTSCHi> > },
		{ "Video.ArtifactingPAL", OnCommandVideoArtifacting<ATGTIAEmulator::kArtifactPAL>, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactPAL> > },
		{ "Video.ArtifactingPALHi", OnCommandVideoArtifacting<ATGTIAEmulator::kArtifactPALHi>, NULL, RadioCheckedIf<VideoArtifactingModeIs<ATGTIAEmulator::kArtifactPALHi> > },

		{ "Video.EnhancedModeNone", OnCommandVideoEnhancedModeNone, NULL, RadioCheckedIf<VideoEnhancedTextModeIs<0> > },
		{ "Video.EnhancedModeHardware", OnCommandVideoEnhancedModeHardware, NULL, RadioCheckedIf<VideoEnhancedTextModeIs<1> > },
		{ "Video.EnhancedModeCIO", OnCommandVideoEnhancedModeCIO, NULL, RadioCheckedIf<VideoEnhancedTextModeIs<2> > },
		{ "Video.EnhancedTextFontDialog", OnCommandVideoEnhancedTextFontDialog },

		{ "Audio.ToggleStereo", OnCommandAudioToggleStereo, NULL, CheckedIf<SimTest<&ATSimulator::IsDualPokeysEnabled> > },
		{ "Audio.ToggleMonitor", OnCommandAudioToggleMonitor, NULL, CheckedIf<SimTest<&ATSimulator::IsAudioMonitorEnabled> > },
		{ "Audio.ToggleMute", OnCommandAudioToggleMute, NULL, CheckedIf<IsAudioMuted> },
		{ "Audio.OptionsDialog", OnCommandAudioOptionsDialog },
		{ "Audio.ToggleNonlinearMixing", OnCommandAudioToggleNonlinearMixing, NULL, CheckedIf<PokeyTest<&ATPokeyEmulator::IsNonlinearMixingEnabled> > },
		{ "Audio.ToggleChannel1", OnCommandAudioToggleChannel<0>, NULL, CheckedIf<PokeyChannelEnabled<0> > },
		{ "Audio.ToggleChannel2", OnCommandAudioToggleChannel<1>, NULL, CheckedIf<PokeyChannelEnabled<1> > },
		{ "Audio.ToggleChannel3", OnCommandAudioToggleChannel<2>, NULL, CheckedIf<PokeyChannelEnabled<2> > },
		{ "Audio.ToggleChannel4", OnCommandAudioToggleChannel<3>, NULL, CheckedIf<PokeyChannelEnabled<3> > },

		{ "Audio.ToggleSoundBoard", OnCommandAudioToggleSoundBoard, IsNot5200, CheckedIf<IsSoundBoardEnabled > },
		{ "Audio.SoundBoardAddrD2C0", OnCommandAudioSoundBoardAddr<0xD2C0>, And<IsSoundBoardEnabled, Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> > >, CheckedIf<SoundBoardBaseIs<0xD2C0> > },
		{ "Audio.SoundBoardAddrD500", OnCommandAudioSoundBoardAddr<0xD500>, And<IsSoundBoardEnabled, Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> > >, CheckedIf<SoundBoardBaseIs<0xD500> > },
		{ "Audio.SoundBoardAddrD600", OnCommandAudioSoundBoardAddr<0xD600>, And<IsSoundBoardEnabled, Not<SimTest<&ATSimulator::IsUltimate1MBEnabled> > >, CheckedIf<SoundBoardBaseIs<0xD600> > },
		{ "Audio.ToggleSlightSid", OnCommandAudioToggleSlightSid, IsNot5200, CheckedIf<IsSlightSidEnabled> },
		{ "Audio.ToggleCovox", OnCommandAudioToggleCovox, IsNot5200, CheckedIf<IsCovoxEnabled> },

		{ "Input.CaptureMouse", OnCommandInputCaptureMouse, IsMouseMapped, CheckedIf<IsMouseCaptured> },
		{ "Input.ToggleAutoCaptureMouse", OnCommandInputToggleAutoCaptureMouse, IsMouseMapped, CheckedIf<IsMouseAutoCaptureEnabled> },
		{ "Input.InputMappingsDialog", OnCommandInputInputMappingsDialog },
		{ "Input.KeyboardDialog", OnCommandInputKeyboardDialog },
		{ "Input.LightPenDialog", OnCommandInputLightPenDialog },

		{ "Record.Stop", OnCommandRecordStop, IsRecording },
		{ "Record.RawAudio", OnCommandRecordRawAudio, Not<IsRecording>, CheckedIf<IsRecordingRawAudio> },
		{ "Record.Audio", OnCommandRecordAudio, Not<IsRecording>, CheckedIf<IsRecordingAudio> },
		{ "Record.Video", OnCommandRecordVideo, Not<IsRecording>, CheckedIf<IsRecordingVideo> },

		{ "Cheat.ToggleDisablePMCollisions", OnCommandCheatTogglePMCollisions, NULL, CheckedIf<Not<GTIATest<&ATGTIAEmulator::ArePMCollisionsEnabled> > > },
		{ "Cheat.ToggleDisablePFCollisions", OnCommandCheatTogglePFCollisions, NULL, CheckedIf<Not<GTIATest<&ATGTIAEmulator::ArePFCollisionsEnabled> > > },
		{ "Cheat.CheatDialog", OnCommandCheatCheatDialog },

		{ "Tools.DiskExplorer", OnCommandToolsDiskExplorer },
		{ "Tools.OptionsDialog", OnCommandToolsOptionsDialog },
		{ "Tools.KeyboardShortcutsDialog", OnCommandToolsKeyboardShortcutsDialog },

		{ "Window.Close", OnCommandWindowClose, CanManipulateWindows },
		{ "Window.Undock", OnCommandWindowUndock, CanManipulateWindows },

		{ "Help.Contents", OnCommandHelpContents },
		{ "Help.About", OnCommandHelpAbout },
		{ "Help.ChangeLog", OnCommandHelpChangeLog },
	};
}

void ATUIInitCommandMappings() {
	g_ATUICommandMgr.RegisterCommands(kATCommands, sizeof(kATCommands)/sizeof(kATCommands[0]));
}

bool OnCommand(UINT id) {
	if (!id)
		return false;

	if (ATUIHandleMenuCommand(id))
		return true;

	if (ATUIHandlePortMenuCommand(id))
		return true;

	if ((uint32)(id - ID_FILE_MRU_BASE) < 100) {
		int index = id - ID_FILE_MRU_BASE;

		if (index == 99) {
			ATClearMRUList();
			ATUpdateMRUListMenu(g_hMenuMRU, g_pMenuMRU, ID_FILE_MRU_BASE, ID_FILE_MRU_BASE + 99);
		} else {
			VDStringW s(ATGetMRUListItem(index));

			if (!s.empty())
				DoBootWithConfirm(s.c_str(), false, false, 0);
		}
	}

	return false;
}

void OnInitMenu(HMENU hmenu) {
	ATUIUpdateMenu();
	ATUpdatePortMenus();
}

void OnActivateApp(HWND hwnd, WPARAM wParam) {
	g_winActive = (wParam != 0);

	if (!wParam) {
		IATDisplayPane *pane = vdpoly_cast<IATDisplayPane *>(ATGetUIPane(kATUIPaneId_Display));
		if (pane)
			pane->ReleaseMouse();

		ATSetFullscreen(false);
	}
}

void ProcessKey(char c, bool repeat, bool allowQueue, bool useCooldown) {
	uint8 ch;

	if (!ATUIGetScanCodeForCharacter(c, ch))
		return;

	g_sim.GetPokey().PushKey(ch, repeat, allowQueue, !allowQueue, useCooldown);
}

///////////////////////////////////////////////////////////////////////////

class ATMainWindow : public ATContainerWindow {
public:
	ATMainWindow();
	~ATMainWindow();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT WndProc2(UINT msg, WPARAM wParam, LPARAM lParam);

	void OnCopyData(HWND hwndReply, const COPYDATASTRUCT& cds);

	virtual void UpdateMonitorDpi(unsigned dpiY) {
		ATConsoleSetFontDpi(dpiY);
	}
};

ATMainWindow::ATMainWindow() {
}

ATMainWindow::~ATMainWindow() {
}

LRESULT ATMainWindow::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	LRESULT r;
	__try {
		r = WndProc2(msg, wParam, lParam);
	} __except(ATExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
	}

	return r;
}

LRESULT ATMainWindow::WndProc2(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case WM_CREATE:
			if (ATContainerWindow::WndProc(msg, wParam, lParam) < 0)
				return -1;

			ATUIRegisterDragDropHandler((VDGUIHandle)mhwnd);
			return 0;

		case WM_CLOSE:
			if (ATUIIsActiveModal()) {
				MessageBeep(MB_ICONASTERISK);
				return 0;
			}

			if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)mhwnd, L"Exit without saving?", true))
				return 0;

			ATSavePaneLayout(NULL);
			break;

		case WM_DESTROY:
			ATUISaveWindowPlacement(mhwnd, "Main window");
			ATUIRevokeDragDropHandler((VDGUIHandle)mhwnd);
			ATUICloseAdjustColorsDialog();

			PostQuitMessage(0);
			break;

		case WM_ACTIVATEAPP:
			OnActivateApp(mhwnd, wParam);
			break;

		case WM_SYSCOMMAND:
			// Need to drop capture for Alt+F4 to work.
			ReleaseCapture();
			break;

		case WM_COMMAND:
			if (ATUIIsActiveModal())
				return 0;

			if (OnCommand(LOWORD(wParam)))
				return 0;
			break;

		case WM_INITMENU:
			OnInitMenu((HMENU)wParam);
			return 0;

		case WM_SETCURSOR:
			break;

		case ATWM_APP_PRETRANSLATE:
			if (!ATUIIsActiveModal()) {
				MSG& globalMsg = *(MSG *)lParam;

				const bool ctrl = GetKeyState(VK_CONTROL) < 0;
				const bool shift = GetKeyState(VK_SHIFT) < 0;
				const bool alt = GetKeyState(VK_MENU) < 0;
				const bool ext = (globalMsg.lParam & (1 << 24)) != 0;

				switch(globalMsg.message) {
					case WM_KEYDOWN:
					case WM_SYSKEYDOWN:
						if (ATUIActivateVirtKeyMapping(globalMsg.wParam, alt, ctrl, shift, ext, false, kATUIAccelContext_Global))
							return TRUE;
						break;

					case WM_KEYUP:
					case WM_SYSKEYUP:
						if (ATUIActivateVirtKeyMapping(globalMsg.wParam, alt, ctrl, shift, ext, true, kATUIAccelContext_Global))
							return TRUE;
						break;

					case WM_CHAR:
						// Currently we have no char-based mappings.
						break;
				}
			}
			break;

		case WM_COPYDATA:
			{
				HWND hwndReply = (HWND)wParam;
				COPYDATASTRUCT *cds = (COPYDATASTRUCT *)lParam;

				OnCopyData(hwndReply, *cds);
			}
			return TRUE;

		case WM_DEVICECHANGE:
			g_sim.GetJoystickManager().RescanForDevices();
			break;

		case WM_ENABLE:
			if (!wParam) {
				if (g_fullscreen)
					ATSetFullscreen(false);
			}

			break;
	}

	return ATContainerWindow::WndProc(msg, wParam, lParam);
}

void ATMainWindow::OnCopyData(HWND hwndReply, const COPYDATASTRUCT& cds) {
	if (!cds.cbData || !cds.lpData)
		return;

	if (cds.dwData == 0xA7000001) {
		// The format of the data we are looking for is as follows:
		//	- validation GUID
		//	- command line
		//	- zero or more properties:
		//		- name
		//		- value
		// All strings are wide char and are null terminated. Note that some 2.x test releases
		// do not send properties and do not null terminate the command string. Also, we want
		// to avoid crashing if the block is malformed.
		//	
		if (cds.cbData < 16 || (cds.cbData - 16) % sizeof(wchar_t))
			return;

		if (memcmp(cds.lpData, kATGUID_CopyDataCmdLine, 16))
			return;

		const wchar_t *s = (const wchar_t *)((const char *)cds.lpData + 16);
		const wchar_t *t;
		const wchar_t *end = s + (cds.cbData - 16) / sizeof(wchar_t);

		// parse out command line string
		for(t = s; t != end && *t; ++t)
			;

		const VDStringW cmdLineStr(s, t);
		if (t != end) {
			s = t + 1;

			VDStringW name;
			VDStringW value;
			for(;;) {
				for(t = s; t != end && *t; ++t)
					;

				if (t == end)
					break;

				name.assign(s, t);

				s = t + 1;
				
				for(t = s; t != end && *t; ++t)
					;

				if (t == end)
					break;

				value.assign(s, t);

				// interpret the string
				if (name == L"chdir") {
					::SetCurrentDirectoryW(value.c_str());
				} else if (name.size() == 3 && name[0] == L'=' && (name[1] >= L'A' && name[1] <= L'Z') && name[2] == L':') {
					::SetEnvironmentVariableW(name.c_str(), value.c_str());
				}
			}
		}

		g_ATCmdLine.InitAlt(cmdLineStr.c_str());
		g_ATCmdLineRead = false;
		return;
	}

	if (cds.dwData != 0xA7000000 || !g_autotestEnabled)
		return;

	vdfastvector<wchar_t> s;

	s.resize(cds.cbData / sizeof(wchar_t) + 1, 0);
	memcpy(s.data(), cds.lpData, (s.size() - 1) * sizeof(wchar_t));

	uint32 rval = 0;
	MyError e;

	try {
		rval = ATExecuteAutotestCommand(s.data(), NULL);
	} catch(MyError& f) {
		e.swap(f);
	}

	if (hwndReply) {
		VDStringW err(VDTextAToW(e.gets()));

		vdfastvector<char> buf(sizeof(uint32) + sizeof(wchar_t) * err.size());
		memcpy(buf.data(), &rval, sizeof(uint32));
		memcpy(buf.data() + sizeof(uint32), err.data(), err.size() * sizeof(wchar_t));

		COPYDATASTRUCT cds2;
		cds2.dwData = 0xA7000001;
		cds2.cbData = (DWORD)buf.size();
		cds2.lpData = buf.data();

		::SendMessage(hwndReply, WM_COPYDATA, (WPARAM)mhwnd, (LPARAM)&cds2);
	}
}

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

void ReadCommandLine(HWND hwnd, VDCommandLine& cmdLine) {
	bool coldReset = false;
	bool debugMode = false;
	bool debugModeSuspend = false;
	bool bootrw = false;
	bool bootvrw = false;
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
			g_autotestEnabled = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"f"))
			ATSetFullscreen(true);

		if (cmdLine.FindAndRemoveSwitch(L"ntsc"))
			g_sim.SetVideoStandard(kATVideoStandard_NTSC);

		if (cmdLine.FindAndRemoveSwitch(L"pal"))
			g_sim.SetVideoStandard(kATVideoStandard_PAL);

		if (cmdLine.FindAndRemoveSwitch(L"secam"))
			g_sim.SetVideoStandard(kATVideoStandard_SECAM);

		if (cmdLine.FindAndRemoveSwitch(L"burstio"))
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Standard);
		if (cmdLine.FindAndRemoveSwitch(L"burstiopolled"))
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Polled);
		else if (cmdLine.FindAndRemoveSwitch(L"noburstio"))
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Disabled);

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
			g_sim.SetSoundBoardEnabled(true);

			if (!vdwcsicmp(arg, L"d2c0"))
				g_sim.SetSoundBoardMemBase(0xD2C0);
			else if (!vdwcsicmp(arg, L"d500"))
				g_sim.SetSoundBoardMemBase(0xD500);
			else if (!vdwcsicmp(arg, L"d600"))
				g_sim.SetSoundBoardMemBase(0xD600);
			else
				throw MyError("Command line error: Invalid SoundBoard memory base: '%ls'", arg);

			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"nosoundboard")) {
			g_sim.SetSoundBoardEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"slightsid", arg)) {
			g_sim.SetSlightSIDEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"noslightsid")) {
			g_sim.SetSlightSIDEnabled(false);
			coldReset = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"covox", arg)) {
			g_sim.SetCovoxEnabled(true);
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"nocovox")) {
			g_sim.SetCovoxEnabled(false);
			coldReset = true;
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
				g_sim.SetKernelMode(kATKernelMode_Default);
			else if (!vdwcsicmp(arg, L"osa"))
				g_sim.SetKernelMode(kATKernelMode_OSA);
			else if (!vdwcsicmp(arg, L"osb"))
				g_sim.SetKernelMode(kATKernelMode_OSB);
			else if (!vdwcsicmp(arg, L"xl"))
				g_sim.SetKernelMode(kATKernelMode_XL);
			else if (!vdwcsicmp(arg, L"1200xl"))
				g_sim.SetKernelMode(kATKernelMode_1200XL);
			else if (!vdwcsicmp(arg, L"lle"))
				g_sim.SetKernelMode(kATKernelMode_LLE_OSB);
			else if (!vdwcsicmp(arg, L"llexl"))
				g_sim.SetKernelMode(kATKernelMode_LLE_XL);
			else if (!vdwcsicmp(arg, L"hle"))
				g_sim.SetKernelMode(kATKernelMode_HLE);
			else if (!vdwcsicmp(arg, L"other"))
				g_sim.SetKernelMode(kATKernelMode_Other);
			else if (!vdwcsicmp(arg, L"5200"))
				g_sim.SetKernelMode(kATKernelMode_5200);
			else if (!vdwcsicmp(arg, L"5200lle"))
				g_sim.SetKernelMode(kATKernelMode_5200_LLE);
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

		if (cmdLine.FindAndRemoveSwitch(L"bootrw")) {
			bootrw = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"bootvrw")) {
			bootvrw = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"type", arg)) {
			keysToType += VDTextWToA(arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"nopclink")) {
			g_sim.SetPCLinkEnabled(false);
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

			g_sim.SetPCLinkEnabled(true);
			
			IATPCLinkDevice *pclink = g_sim.GetPCLink();
			pclink->SetReadOnly(!write);
			pclink->SetBasePath(VDStringW(tokenizer).c_str());
		}

		if (cmdLine.FindAndRemoveSwitch(L"nohdpath", arg)) {
			IATHostDeviceEmulator *hd = g_sim.GetHostDevice();

			if (hd && hd->IsEnabled()) {
				hd->SetEnabled(false);
				coldReset = true;
			}
		}

		if (cmdLine.FindAndRemoveSwitch(L"hdpath", arg)) {
			IATHostDeviceEmulator *hd = g_sim.GetHostDevice();
			if (hd) {
				if (!hd->IsEnabled())
					hd->SetEnabled(true);

				hd->SetReadOnly(true);
				hd->SetBasePath(0, arg);

				coldReset = true;
			}
		}

		if (cmdLine.FindAndRemoveSwitch(L"hdpathrw", arg)) {
			IATHostDeviceEmulator *hd = g_sim.GetHostDevice();
			if (hd) {
				if (!hd->IsEnabled())
					hd->SetEnabled(true);

				hd->SetReadOnly(false);
				hd->SetBasePath(0, arg);

				coldReset = true;
			}
		}

		if (cmdLine.FindAndRemoveSwitch(L"noide")) {
			g_sim.UnloadIDE();
			coldReset = true;
		} else if (cmdLine.FindAndRemoveSwitch(L"ide", arg)) {
			VDStringRefW tokenizer(arg);
			VDStringRefW iorange;
			VDStringRefW mode;
			VDStringRefW cylinders;
			VDStringRefW heads;
			VDStringRefW spt;

			if (!tokenizer.split(',', iorange) ||
				!tokenizer.split(',', mode) ||
				!tokenizer.split(',', cylinders) ||
				!tokenizer.split(',', heads) ||
				!tokenizer.split(',', spt))
				throw MyError("Invalid IDE mount string: %ls", arg);

			ATIDEHardwareMode hwmode;
			if (iorange == L"d5xx")
				hwmode = kATIDEHardwareMode_MyIDE_D5xx;
			else if (iorange == L"d1xx")
				hwmode = kATIDEHardwareMode_MyIDE_D1xx;
			else if (iorange == L"kmkjzv1")
				hwmode = kATIDEHardwareMode_KMKJZ_V1;
			else if (iorange == L"kmkjzv2")
				hwmode = kATIDEHardwareMode_KMKJZ_V2;
			else if (iorange == L"side")
				hwmode = kATIDEHardwareMode_SIDE;
			else if (iorange == L"side2")
				hwmode = kATIDEHardwareMode_SIDE2;
			else if (iorange == L"myidev2_d5xx")
				hwmode = kATIDEHardwareMode_MyIDE_V2_D5xx;
			else
				throw MyError("Invalid IDE hardware type: %.*ls", iorange.size(), iorange.data());

			bool write = false;
			bool fast = false;

			size_t modelen = mode.size();
			if (modelen > 5 && !memcmp(mode.data() + modelen - 4, L"-fast", 5)) {
				fast = true;
				mode = mode.subspan(0, (uint32)modelen - 5);
			}

			if (mode == L"rw")
				write = true;
			else if (mode != L"ro")
				throw MyError("Invalid IDE mount mode: %.*ls", mode.size(), mode.data());

			unsigned cylval;
			unsigned headsval;
			unsigned sptval;
			char dummy;
			if (1 != swscanf(VDStringW(cylinders).c_str(), L"%u%c", &cylval, &dummy) || !cylval || cylval > 16777216 ||
				1 != swscanf(VDStringW(heads).c_str(), L"%u%c", &headsval, &dummy) || !headsval || headsval > 16 ||
				1 != swscanf(VDStringW(spt).c_str(), L"%u%c", &sptval, &dummy) || !sptval || sptval > 255
				)
				throw MyError("Invalid IDE image geometry: %.*ls / %.*ls / %.*ls"
					, cylinders.size(), cylinders.data()
					, heads.size(), heads.data()
					, spt.size(), spt.data()
				);

			g_sim.LoadIDE(hwmode, write, fast, cylval, headsval, sptval, VDStringW(tokenizer).c_str());
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
				_T("/ide:d1xx|d5xx|kmkjzv1|kmkjzv2|side,ro|rw,c,h,s,path - set IDE emulation\n")
				_T("/[no]pclink:ro|rw,path - set PCLink emulation\n")
				_T("/hostcpu:none|mmx|sse|sse2|sse2|sse3|ssse3|sse41 - override host CPU detection\n")
				_T("/cart <image> - load cartridge image\n")
				_T("/tape <image> - load cassette tape image\n")
				_T("/disk <image> - load disk image\n")
				_T("/run <image> - run binary program")
				_T("/runbas <image> - run BASIC program")
				,
				_T("Altirra command-line help"), MB_OK | MB_ICONINFORMATION);
		}

		while(cmdLine.FindAndRemoveSwitch(L"cart", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootvrw, bootrw, cartmapper, kATLoadType_Cartridge);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"disk", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootvrw, bootrw, cartmapper, kATLoadType_Disk);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"run", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootvrw, bootrw, cartmapper, kATLoadType_Program);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"runbas", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootvrw, bootrw, cartmapper, kATLoadType_BasicProgram);
			coldReset = true;// required to set up cassette autoboot
		}

		while(cmdLine.FindAndRemoveSwitch(L"tape", arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootvrw, bootrw, cartmapper, kATLoadType_Tape);
			coldReset = true;// required to set up cassette autoboot
		}

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

			DoLoad((VDGUIHandle)hwnd, arg, bootvrw, bootrw, cartmapper, kATLoadType_Other, &suppressColdReset);

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
}

void LoadColorParams(VDRegistryKey& key, ATColorParams& colpa) {
	colpa.mHueStart = VDGetIntAsFloat(key.getInt("Hue Start", VDGetFloatAsInt(colpa.mHueStart)));
	colpa.mHueRange = VDGetIntAsFloat(key.getInt("Hue Range", VDGetFloatAsInt(colpa.mHueRange)));
	colpa.mBrightness = VDGetIntAsFloat(key.getInt("Brightness", VDGetFloatAsInt(colpa.mBrightness)));
	colpa.mContrast = VDGetIntAsFloat(key.getInt("Contrast", VDGetFloatAsInt(colpa.mContrast)));
	colpa.mSaturation = VDGetIntAsFloat(key.getInt("Saturation", VDGetFloatAsInt(colpa.mSaturation)));
	colpa.mArtifactHue = VDGetIntAsFloat(key.getInt("Artifact Hue", VDGetFloatAsInt(colpa.mArtifactHue)));
	colpa.mArtifactSat = VDGetIntAsFloat(key.getInt("Artifact Saturation", VDGetFloatAsInt(colpa.mArtifactSat)));
	colpa.mArtifactBias = VDGetIntAsFloat(key.getInt("Artifact Bias", VDGetFloatAsInt(colpa.mArtifactBias)));
	colpa.mbUsePALQuirks = key.getBool("PAL quirks", colpa.mbUsePALQuirks);
}

void SaveColorParams(VDRegistryKey& key, const ATColorParams& colpa) {
	key.setInt("Hue Start", VDGetFloatAsInt(colpa.mHueStart));
	key.setInt("Hue Range", VDGetFloatAsInt(colpa.mHueRange));
	key.setInt("Brightness", VDGetFloatAsInt(colpa.mBrightness));
	key.setInt("Contrast", VDGetFloatAsInt(colpa.mContrast));
	key.setInt("Saturation", VDGetFloatAsInt(colpa.mSaturation));
	key.setInt("Artifact Hue", VDGetFloatAsInt(colpa.mArtifactHue));
	key.setInt("Artifact Saturation", VDGetFloatAsInt(colpa.mArtifactSat));
	key.setInt("Artifact Bias", VDGetFloatAsInt(colpa.mArtifactBias));
	key.setBool("PAL quirks", colpa.mbUsePALQuirks);
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

	g_displayStretchMode = (ATDisplayStretchMode)key.getEnumInt("Display: Stretch mode", kATDisplayStretchModeCount, g_displayStretchMode);
}

void SaveMountedImages() {
	VDRegistryAppKey key("Mounted Images", true);
	VDStringA name;
	VDStringW imagestr;

	for(int i=0; i<15; ++i) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
		name.sprintf("Disk %d", i);


		if (disk.IsEnabled()) {
			const wchar_t *path = disk.GetPath();
			
			wchar_t c = 'R';

			if (disk.IsWriteEnabled()) {
				if (disk.IsAutoFlushEnabled())
					c = 'W';
				else
					c = 'V';
			}

			if (path && disk.IsDiskBacked())
				imagestr.sprintf(L"%c%ls", c, path);
			else
				imagestr.sprintf(L"%c", c);

			key.setString(name.c_str(), imagestr.c_str());
		} else {
			key.removeValue(name.c_str());
		}
	}

	for(uint32 cartUnit = 0; cartUnit < 2; ++cartUnit) {
		ATCartridgeEmulator *cart = g_sim.GetCartridge(cartUnit);
		const wchar_t *cartPath = NULL;
		int cartMode = 0;
		if (cart) {
			cartPath = cart->GetPath();
			cartMode = cart->GetMode();
		}

		VDStringA keyName;
		VDStringA keyNameMode;
		keyName.sprintf("Cartridge %u", cartUnit);
		keyNameMode.sprintf("Cartridge %u Mode", cartUnit);

		if (cartPath && *cartPath) {
			key.setString(keyName.c_str(), cartPath);
			key.setInt(keyNameMode.c_str(), cartMode);
		} else if (cartMode == kATCartridgeMode_SuperCharger3D) {
			key.setString(keyName.c_str(), "special:sc3d");
			key.removeValue(keyNameMode.c_str());
		} else {
			key.removeValue(keyName.c_str());
			key.removeValue(keyNameMode.c_str());
		}
	}

	ATIDEEmulator *ide = g_sim.GetIDEEmulator();

	if (ide) {
		key.setString("IDE: Image path", ide->GetImagePath());
		key.setInt("IDE: Hardware mode", g_sim.GetIDEHardwareMode());
		key.setBool("IDE: Write enabled", ide->IsWriteEnabled());
		key.setBool("IDE: Fast device", ide->IsFastDevice());
		key.setInt("IDE: Image cylinders", ide->GetCylinderCount());
		key.setInt("IDE: Image heads", ide->GetHeadCount());
		key.setInt("IDE: Image sectors per track", ide->GetSectorsPerTrack());
	} else {
		key.removeValue("IDE: Image path");
		key.removeValue("IDE: Hardware mode");
		key.removeValue("IDE: Write enabled");
		key.removeValue("IDE: Image cylinders");
		key.removeValue("IDE: Image heads");
		key.removeValue("IDE: Image sectors per track");
	}
}

void LoadMountedImages() {
	VDRegistryAppKey key("Mounted Images");
	VDStringA name;
	VDStringW imagestr;

	for(int i=0; i<15; ++i) {
		name.sprintf("Disk %d", i);

		if (key.getString(name.c_str(), imagestr) && !imagestr.empty()) {
			ATDiskEmulator& disk = g_sim.GetDiskDrive(i);

			wchar_t mode = imagestr[0];
			bool vrw;
			bool rw;

			if (mode == L'V') {
				vrw = true;
				rw = false;
			} else if (mode == L'R') {
				vrw = false;
				rw = false;
			} else if (mode == L'W') {
				vrw = false;
				rw = true;
			} else
				continue;

			if (imagestr.size() > 1) {
				try {
					const wchar_t *star = wcschr(imagestr.c_str(), L'*');
					if (star) {
						disk.LoadDisk(imagestr.c_str() + 1);
					} else {
						ATLoadContext ctx;
						ctx.mLoadType = kATLoadType_Disk;
						ctx.mLoadIndex = i;

						g_sim.Load(imagestr.c_str() + 1, vrw, rw, &ctx);
					}
				} catch(const MyError&) {
				}
			} else {
				disk.SetEnabled(true);
				disk.SetWriteFlushMode(vrw || rw, rw);
			}
		}
	}

	for(uint32 cartUnit = 0; cartUnit < 2; ++cartUnit) {
		VDStringA keyName;
		VDStringA keyNameMode;
		keyName.sprintf("Cartridge %u", cartUnit);
		keyNameMode.sprintf("Cartridge %u Mode", cartUnit);

		if (key.getString(keyName.c_str(), imagestr)) {
			int cartMode = key.getInt(keyNameMode.c_str(), 0);

			try {
				ATCartLoadContext cartLoadCtx = {0};
				cartLoadCtx.mbReturnOnUnknownMapper = false;
				cartLoadCtx.mCartMapper = cartMode;

				if (imagestr == L"special:sc3d")
					g_sim.LoadNewCartridge(kATCartridgeMode_SuperCharger3D);
				else
					g_sim.LoadCartridge(cartUnit, imagestr.c_str(), &cartLoadCtx);
			} catch(const MyError&) {
			}
		} else {
			if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
				g_sim.LoadCartridge5200Default();
		}
	}

	VDStringW idePath;
	if (key.getString("IDE: Image path", idePath)) {
		ATIDEHardwareMode hwmode = key.getBool("IDE: Use D5xx", true) ? kATIDEHardwareMode_MyIDE_D5xx : kATIDEHardwareMode_MyIDE_D1xx;

		hwmode = (ATIDEHardwareMode)key.getEnumInt("IDE: Hardware mode", kATIDEHardwareModeCount, hwmode);

		bool write = key.getBool("IDE: Write enabled", false);
		bool fast = key.getBool("IDE: Fast device", false);
		uint32 cyls = key.getInt("IDE: Image cylinders", 0);
		uint32 heads = key.getInt("IDE: Image heads", 0);
		uint32 sectors = key.getInt("IDE: Image sectors per track", 0);

		try {
			// Force read-only if we have a device or volume path; never auto-mount read-write.
			if (write && ATIDEIsPhysicalDiskPath(idePath.c_str()))
				write = false;

			g_sim.LoadIDE(hwmode, write, fast, cyls, heads, sectors, idePath.c_str());
		} catch(const MyError&) {
		}
	}
}

namespace {
	static const char *kSettingNamesROMImages[]={
		"OS-A",
		"OS-B",
		"XL",
		"XEGS",
		"Other",
		"5200",
		"Basic",
		"Game",
		"KMKJZIDE",
		"KMKJZIDEV2",
		"KMKJZIDEV2_SDX",
		"SIDE_SDX",
		"1200XL",
		"MyIDEII",
		"Ultimate1MB"
	};
}

void LoadROMImageSettings() {
	VDRegistryAppKey imageKey("Settings\\ROM Images", false);
	for(int i=0; i<kATROMImageCount; ++i) {
		VDStringW s;
		if (imageKey.getString(kSettingNamesROMImages[i], s))
			g_sim.SetROMImagePath((ATROMImage)i, s.c_str());
	}
}

void LoadBaselineSettings() {
	ATPokeyEmulator& pokey = g_sim.GetPokey();

	g_sim.SetKernelMode(kATKernelMode_Default);
	g_sim.SetHardwareMode(kATHardwareMode_800XL);
	g_sim.SetBASICEnabled(false);
	g_sim.SetVideoStandard(kATVideoStandard_NTSC);
	g_sim.SetMemoryMode(kATMemoryMode_320K);
	g_sim.SetCassetteSIOPatchEnabled(true);
	g_sim.SetCassetteAutoBootEnabled(true);
	g_sim.SetFPPatchEnabled(false);
	g_sim.SetFastBootEnabled(true);
	pokey.SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Disabled);
	g_sim.SetVBXEEnabled(false);
	g_sim.SetDiskSIOPatchEnabled(true);
	g_sim.SetDiskSIOOverrideDetectEnabled(false);

	for(int i=0; i<15; ++i) {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
		disk.SetAccurateSectorTimingEnabled(false);
		disk.SetDriveSoundsEnabled(false);
		disk.SetEmulationMode(kATDiskEmulationMode_Generic);
	}

	g_sim.SetDualPokeysEnabled(false);
	g_sim.SetSoundBoardEnabled(false);
	g_sim.SetSlightSIDEnabled(false);
	g_sim.SetCovoxEnabled(false);

	g_enhancedText = 0;

	g_sim.SetPrinterEnabled(false);
	g_sim.SetRTime8Enabled(false);
	g_sim.SetRS232Enabled(false);

	g_sim.SetRandomFillEnabled(false);
	g_kbdOpts.mbRawKeys = false;
	g_sim.SetKeyboardPresent(true);
	g_sim.SetForcedSelfTest(false);
}

void LoadSettings(bool useHardwareBaseline) {
	VDRegistryAppKey key("Settings", false);

	ATPokeyEmulator& pokey = g_sim.GetPokey();

	if (!useHardwareBaseline) {
		g_sim.SetKernelMode((ATKernelMode)key.getEnumInt("Kernel mode", kATKernelModeCount, g_sim.GetKernelMode()));
		g_sim.SetHardwareMode((ATHardwareMode)key.getEnumInt("Hardware mode", kATHardwareModeCount, g_sim.GetHardwareMode()));
		g_sim.SetBASICEnabled(key.getBool("BASIC enabled", g_sim.IsBASICEnabled()));

		const bool isPAL = key.getBool("PAL mode", g_sim.GetVideoStandard() == kATVideoStandard_PAL);
		const bool isSECAM = key.getBool("SECAM mode", g_sim.GetVideoStandard() == kATVideoStandard_SECAM);
		g_sim.SetVideoStandard(isSECAM ? kATVideoStandard_SECAM : isPAL ? kATVideoStandard_PAL : kATVideoStandard_NTSC);

		g_sim.SetMemoryMode((ATMemoryMode)key.getEnumInt("Memory mode", kATMemoryModeCount, g_sim.GetMemoryMode()));
		g_sim.SetCassetteSIOPatchEnabled(key.getBool("Cassette: SIO patch enabled", g_sim.IsCassetteSIOPatchEnabled()));
		g_sim.SetCassetteAutoBootEnabled(key.getBool("Cassette: Auto-boot enabled", g_sim.IsCassetteAutoBootEnabled()));
		g_sim.SetFPPatchEnabled(key.getBool("Kernel: Floating-point patch enabled", g_sim.IsFPPatchEnabled()));
		g_sim.SetFastBootEnabled(key.getBool("Kernel: Fast boot enabled", g_sim.IsFastBootEnabled()));
		pokey.SetSerialBurstMode((ATPokeyEmulator::SerialBurstMode)key.getEnumInt("POKEY: Burst mode", ATPokeyEmulator::kSerialBurstModeCount, pokey.GetSerialBurstMode()));
		g_sim.SetVBXEEnabled(key.getBool("GTIA: VBXE support", g_sim.GetVBXE() != NULL));
		g_sim.SetVBXESharedMemoryEnabled(key.getBool("GTIA: VBXE shared memory", g_sim.IsVBXESharedMemoryEnabled()));
		g_sim.SetVBXEAltPageEnabled(key.getBool("GTIA: VBXE alternate page", g_sim.IsVBXEAltPageEnabled()));
		g_sim.SetDiskSIOPatchEnabled(key.getBool("Disk: SIO patch enabled", g_sim.IsDiskSIOPatchEnabled()));
		g_sim.SetDiskSIOOverrideDetectEnabled(key.getBool("Disk: SIO override detection enabled", g_sim.IsDiskSIOOverrideDetectEnabled()));

		for(int i=0; i<15; ++i) {
			ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
			disk.SetAccurateSectorTimingEnabled(key.getBool("Disk: Accurate sector timing", disk.IsAccurateSectorTimingEnabled()));
			disk.SetDriveSoundsEnabled(key.getBool("Disk: Drive sounds", disk.AreDriveSoundsEnabled()));
			disk.SetEmulationMode((ATDiskEmulationMode)key.getEnumInt("Disk: Emulation mode", kATDiskEmulationModeCount, disk.GetEmulationMode()));
		}

		g_sim.SetDualPokeysEnabled(key.getBool("Audio: Dual POKEYs enabled", g_sim.IsDualPokeysEnabled()));

		g_sim.SetSoundBoardEnabled(key.getBool("SoundBoard: Enabled", g_sim.GetSoundBoard() != NULL));
		g_sim.SetSoundBoardMemBase(key.getInt("SoundBoard: Memory base", g_sim.GetSoundBoardMemBase()));

		g_sim.SetSlightSIDEnabled(key.getBool("SlightSID: Enabled", g_sim.GetSlightSID() != NULL));
		g_sim.SetCovoxEnabled(key.getBool("Covox: Enabled", g_sim.GetCovox() != NULL));

		g_sim.SetXEP80Enabled(key.getBool("XEP80: Enabled", g_sim.GetXEP80() != NULL));

		g_enhancedText = key.getInt("Video: Enhanced text mode", 0);
		if (g_enhancedText > 2)
			g_enhancedText = 2;

		g_sim.SetVirtualScreenEnabled(g_enhancedText == 2);

		g_sim.SetPrinterEnabled(key.getBool("Printer: Enabled", g_sim.IsPrinterEnabled()));
		g_sim.SetRTime8Enabled(key.getBool("Misc: R-Time 8 Enabled", g_sim.IsRTime8Enabled()));
		g_sim.SetRS232Enabled(key.getBool("Serial Ports: Enabled", g_sim.IsRS232Enabled()));

		if (g_sim.IsRS232Enabled()) {
			ATRS232Config config;

			config.mDeviceMode = (ATRS232DeviceMode)key.getEnumInt("Serial Ports: Device mode", kATRS232DeviceModeCount, config.mDeviceMode);
			config.mbTelnetEmulation = key.getBool("Serial Ports: Telnet emulation", config.mbTelnetEmulation);
			config.mbTelnetLFConversion = key.getBool("Serial Ports: Telnet LF conversion", config.mbTelnetLFConversion);
			config.mbAllowOutbound = key.getBool("Serial Ports: Allow outbound connections", config.mbAllowOutbound);
			key.getString("Serial Ports: Telnet terminal type", config.mTelnetTermType);
			config.mListenPort = key.getInt("Serial Ports: Listen Port", config.mListenPort);
			if (config.mListenPort < 1 || config.mListenPort > 65535)
				config.mListenPort = 0;

			config.mConnectionSpeed = key.getInt("Serial Ports: Modem connection rate", config.mConnectionSpeed);
			config.mbListenForIPv6 = key.getBool("Serial Ports: Listen for IPv6 connections", config.mbListenForIPv6);
			config.mbDisableThrottling = key.getBool("Serial Ports: Disable throttling", config.mbDisableThrottling);
			config.mbRequireMatchedDTERate = key.getBool("Serial Ports: Require matched DTE rate", config.mbRequireMatchedDTERate);
			config.mbExtendedBaudRates = key.getBool("Serial Ports: Enable extended baud rates", config.mbExtendedBaudRates);
			config.m850SIOLevel = (AT850SIOEmulationLevel)key.getEnumInt("Serial Ports; SIO emulation level", kAT850SIOEmulationLevelCount, config.m850SIOLevel);

			key.getString("Serial Ports: Dial address", config.mDialAddress);
			key.getString("Serial Ports: Dial service", config.mDialService);

			g_sim.GetRS232()->SetConfig(config);
		}

		g_sim.SetAxlonMemoryMode(key.getInt("Memory: Axlon size", g_sim.GetAxlonMemoryMode()));
		g_sim.SetHighMemoryBanks(key.getInt("Memory: High banks", g_sim.GetHighMemoryBanks()));
		g_sim.SetMapRAMEnabled(key.getBool("Memory: MapRAM", g_sim.IsMapRAMEnabled()));
		g_sim.SetUltimate1MBEnabled(key.getBool("Memory: Ultimate1MB", g_sim.IsUltimate1MBEnabled()));
		g_sim.SetRandomFillEnabled(key.getBool("Memory: Randomize on start", g_sim.IsRandomFillEnabled()));
		g_kbdOpts.mbRawKeys = key.getBool("Keyboard: Raw mode", g_kbdOpts.mbRawKeys);
		g_sim.SetKeyboardPresent(key.getBool("Console: Keyboard present", g_sim.IsKeyboardPresent()));
		g_sim.SetForcedSelfTest(key.getBool("Console: Force self test", g_sim.IsForcedSelfTest()));
		g_sim.SetCartridgeSwitch(key.getBool("Console: Cartridge switch", g_sim.GetCartridgeSwitch()));

		if (key.getBool("DragonCart: Enabled", false)) {
			ATDragonCartSettings settings;
			settings.SetDefault();

			settings.mNetAddr = key.getInt("DragonCart: Network address", settings.mNetAddr);
			settings.mNetMask = key.getInt("DragonCart: Network mask", settings.mNetMask);
			settings.mAccessMode = (ATDragonCartSettings::AccessMode)key.getEnumInt("DragonCart: Access mode", ATDragonCartSettings::kAccessModeCount, settings.mAccessMode);

			g_sim.SetDragonCartEnabled(&settings);
		} else {
			g_sim.SetDragonCartEnabled(NULL);
		}
	}

	g_kbdOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)key.getEnumInt("Keyboard: Arrow key mode", ATUIKeyboardOptions::kAKMCount, g_kbdOpts.mArrowKeyMode);
	g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)key.getEnumInt("Keyboard: Layout mode", ATUIKeyboardOptions::kLMCount, g_kbdOpts.mLayoutMode);
	g_kbdOpts.mbAllowShiftOnColdReset = key.getBool("Keyboard: Allow shift on cold reset", g_kbdOpts.mbAllowShiftOnColdReset);
	g_kbdOpts.mbEnableFunctionKeys = key.getBool("Keyboard: Enable function keys", g_kbdOpts.mbEnableFunctionKeys);
	ATUIInitVirtualKeyMap(g_kbdOpts);

	ATUISetTurbo(key.getBool("Turbo mode", ATUIGetTurbo()));
	g_pauseInactive = key.getBool("Pause when inactive", g_pauseInactive);

	g_sim.GetCassette().SetLoadDataAsAudioEnable(key.getBool("Cassette: Load data as audio", g_sim.GetCassette().IsLoadDataAsAudioEnabled()));

	IATAudioOutput *audioOut = g_sim.GetAudioOutput();
	float volume = VDGetIntAsFloat(key.getInt("Audio: Volume", VDGetFloatAsInt(0.5f)));
	if (!(volume >= 0.0f && volume <= 1.0f))
		volume = 0.5f;
	audioOut->SetVolume(volume);
	audioOut->SetMute(key.getBool("Audio: Mute", false));

	audioOut->SetLatency(key.getInt("Audio: Latency", 80));
	audioOut->SetExtraBuffer(key.getInt("Audio: Extra buffer", 100));
	audioOut->SetApi((ATAudioApi)key.getEnumInt("Audio: Api", kATAudioApiCount, kATAudioApi_WaveOut));

	if (key.getBool("Audio: Show debug info", false))
		audioOut->SetStatusRenderer(g_sim.GetUIRenderer());
	else
		audioOut->SetStatusRenderer(NULL);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetArtifactingMode((ATGTIAEmulator::ArtifactMode)key.getEnumInt("GTIA: Artifacting mode", ATGTIAEmulator::kArtifactCount, gtia.GetArtifactingMode()));
	gtia.SetOverscanMode((ATGTIAEmulator::OverscanMode)key.getEnumInt("GTIA: Overscan mode", ATGTIAEmulator::kOverscanCount, gtia.GetOverscanMode()));
	gtia.SetOverscanPALExtended(key.getBool("GTIA: PAL extended height", gtia.IsOverscanPALExtended()));
	gtia.SetBlendModeEnabled(key.getBool("GTIA: Frame blending", gtia.IsBlendModeEnabled()));
	gtia.SetInterlaceEnabled(key.getBool("GTIA: Interlace", gtia.IsInterlaceEnabled()));
	gtia.SetScanlinesEnabled(key.getBool("GTIA: Scanlines", gtia.AreScanlinesEnabled()));

	VDRegistryKey colKey(key, "Colors", false);
	ATColorSettings cols(gtia.GetColorSettings());

	VDRegistryKey ntscRegKey(colKey, "NTSC", false);
	LoadColorParams(ntscRegKey, cols.mNTSCParams);

	VDRegistryKey palRegKey(colKey, "PAL", false);
	LoadColorParams(palRegKey, cols.mPALParams);
	cols.mbUsePALParams = colKey.getBool("Use separate color profiles", cols.mbUsePALParams);
	gtia.SetColorSettings(cols);

	g_sim.SetDiskSectorCounterEnabled(key.getBool("Disk: Sector counter enabled", g_sim.IsDiskSectorCounterEnabled()));

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetHistoryEnabled(key.getBool("CPU: History enabled", cpu.IsHistoryEnabled()));
	cpu.SetPathfindingEnabled(key.getBool("CPU: Pathfinding enabled", cpu.IsPathfindingEnabled()));
	cpu.SetStopOnBRK(key.getBool("CPU: Stop on BRK", cpu.GetStopOnBRK()));
	cpu.SetNMIBlockingEnabled(key.getBool("CPU: Allow NMI blocking", cpu.IsNMIBlockingEnabled()));
	cpu.SetIllegalInsnsEnabled(key.getBool("CPU: Allow illegal instructions", cpu.AreIllegalInsnsEnabled()));

	g_sim.SetShadowROMEnabled(key.getBool("CPU: Shadow ROMs", g_sim.GetShadowROMEnabled()));
	g_sim.SetShadowCartridgeEnabled(key.getBool("CPU: Shadow cartridges", g_sim.GetShadowCartridgeEnabled()));

	ATCPUMode cpuMode = (ATCPUMode)key.getEnumInt("CPU: Chip type", kATCPUModeCount, cpu.GetCPUMode());
	uint32 cpuMultiplier = key.getInt("CPU: Clock multiplier", cpu.GetSubCycles());
	cpu.SetCPUMode(cpuMode, cpuMultiplier);

	IATHostDeviceEmulator *hd = g_sim.GetHostDevice();
	if (hd) {
		VDStringW path;
		VDStringA keyName;

		for(int i=0; i<4; ++i) {
			keyName = "Hard disk: Host path";

			if (i)
				keyName.append_sprintf(" %u", i + 2);

			if (key.getString(keyName.c_str(), path))
				hd->SetBasePath(i, path.c_str());
		}

		hd->SetEnabled(key.getBool("Hard disk: Enabled", hd->IsEnabled()));
		hd->SetReadOnly(key.getBool("Hard disk: Read only", hd->IsReadOnly()));
		hd->SetBurstIOEnabled(key.getBool("Hard disk: Burst IO enabled", hd->IsBurstIOEnabled()));
		hd->SetLongNameEncodingEnabled(key.getBool("Hard disk: Long name encoding", hd->IsLongNameEncodingEnabled()));
	}

	g_sim.SetPCLinkEnabled(key.getBool("PCLink: Enabled", g_sim.GetPCLink() != NULL));
	IATPCLinkDevice *pclink = g_sim.GetPCLink();
	if (pclink) {
		VDStringW path;
		if (key.getString("PCLink: Host path", path))
			pclink->SetBasePath(path.c_str());

		pclink->SetReadOnly(key.getBool("PCLink: Read only", pclink->IsReadOnly()));
	}

	pokey.SetNonlinearMixingEnabled(key.getBool("Audio: Non-linear mixing", pokey.IsNonlinearMixingEnabled()));

	g_dispFilterMode = (ATDisplayFilterMode)key.getEnumInt("Display: Filter mode", kATDisplayFilterModeCount, g_dispFilterMode);
	g_dispFilterSharpness = key.getInt("Display: Filter sharpness", g_dispFilterSharpness);

	ResizeDisplay();

	g_showFps = key.getBool("View: Show FPS", g_showFps);
	g_winCaptionUpdater.SetShowFps(g_showFps);
	gtia.SetVsyncEnabled(key.getBool("View: Vertical sync", gtia.IsVsyncEnabled()));

	g_xepViewEnabled = key.getBool("View: 80-column view enabled", g_xepViewEnabled);
	g_xepViewAutoswitchingEnabled = key.getBool("View: 80-column view autoswitching enabled", g_xepViewAutoswitchingEnabled);

	ATLightPenPort *lpp = g_sim.GetLightPenPort();
	lpp->SetAdjust(key.getInt("Light Pen: Adjust X", lpp->GetAdjustX()), key.getInt("Light Pen: Adjust Y", lpp->GetAdjustY()));

	g_mouseAutoCapture = key.getBool("Mouse: Auto-capture", g_mouseAutoCapture);

	g_speedModifier = key.getInt("Speed: Frame rate modifier", VDRoundToInt((g_speedModifier + 1.0f) * 100.0f)) / 100.0f - 1.0f;
	g_frameRateMode = (ATFrameRateMode)key.getEnumInt("Speed: Frame rate mode", kATFrameRateModeCount, g_frameRateMode);

	IATDebugger *dbg = ATGetDebugger();
	if (dbg) {
		dbg->SetBreakOnEXERunAddrEnabled(key.getBool("Debugger: Break on EXE run address", dbg->IsBreakOnEXERunAddrEnabled()));
	}

	VDRegistryAppKey imapKey("Settings\\Input maps");
	g_sim.GetInputManager()->Load(imapKey);

	ATReloadPortMenus();
	ATUIUpdateSpeedTiming();

	if (!useHardwareBaseline)
		LoadMountedImages();
}

void SaveSettings() {
	VDRegistryAppKey key("Settings");

	VDRegistryAppKey imageKey("Settings\\ROM Images");
	for(int i=0; i<kATROMImageCount; ++i) {
		VDStringW s;
		g_sim.GetROMImagePath((ATROMImage)i, s);

		imageKey.setString(kSettingNamesROMImages[i], s.c_str());
	}

	key.setInt("Kernel mode", g_sim.GetKernelMode());
	key.setInt("Hardware mode", g_sim.GetHardwareMode());
	key.setBool("BASIC enabled", g_sim.IsBASICEnabled());
	key.setBool("PAL mode", g_sim.GetVideoStandard() != kATVideoStandard_NTSC);
	key.setBool("SECAM mode", g_sim.GetVideoStandard() == kATVideoStandard_SECAM);
	key.setInt("Memory mode", g_sim.GetMemoryMode());
	key.setBool("Turbo mode", ATUIGetTurbo());
	key.setBool("Pause when inactive", g_pauseInactive);

	key.setInt("Memory: Axlon size", g_sim.GetAxlonMemoryMode());
	key.setInt("Memory: High banks", g_sim.GetHighMemoryBanks());
	key.setBool("Memory: MapRAM", g_sim.IsMapRAMEnabled());
	key.setBool("Memory: Ultimate1MB", g_sim.IsUltimate1MBEnabled());
	key.setBool("Memory: Randomize on start", g_sim.IsRandomFillEnabled());

	key.setBool("Kernel: Floating-point patch enabled", g_sim.IsFPPatchEnabled());
	key.setBool("Kernel: Fast boot enabled", g_sim.IsFastBootEnabled());

	key.setBool("Cassette: SIO patch enabled", g_sim.IsCassetteSIOPatchEnabled());
	key.setBool("Cassette: Auto-boot enabled", g_sim.IsCassetteAutoBootEnabled());
	key.setBool("Cassette: Load data as audio", g_sim.GetCassette().IsLoadDataAsAudioEnabled());

	IATAudioOutput *audioOut = g_sim.GetAudioOutput();
	key.setInt("Audio: Volume", VDGetFloatAsInt(audioOut->GetVolume()));
	key.setBool("Audio: Mute", audioOut->GetMute());
	key.setInt("Audio: Latency", audioOut->GetLatency());
	key.setInt("Audio: Extra buffer", audioOut->GetExtraBuffer());
	key.setInt("Audio: Api", audioOut->GetApi());
	key.setBool("Audio: Show debug info", audioOut->GetStatusRenderer() != NULL);

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	key.setInt("POKEY: Burst mode", pokey.GetSerialBurstMode());
	
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	key.setInt("GTIA: Artifacting mode", gtia.GetArtifactingMode());
	key.setInt("GTIA: Overscan mode", gtia.GetOverscanMode());
	key.setBool("GTIA: PAL extended height", gtia.IsOverscanPALExtended());
	key.setBool("GTIA: Frame blending", gtia.IsBlendModeEnabled());
	key.setBool("GTIA: Interlace", gtia.IsInterlaceEnabled());
	key.setBool("GTIA: Scanlines", gtia.AreScanlinesEnabled());
	key.setBool("GTIA: VBXE support", g_sim.GetVBXE() != NULL);
	key.setBool("GTIA: VBXE shared memory", g_sim.IsVBXESharedMemoryEnabled());
	key.setBool("GTIA: VBXE alternate page", g_sim.IsVBXEAltPageEnabled());

	ATColorSettings cols(gtia.GetColorSettings());

	VDRegistryKey colKey(key, "Colors", true);
	VDRegistryKey ntscColKey(colKey, "NTSC");
	SaveColorParams(ntscColKey, cols.mNTSCParams);

	VDRegistryKey palColKey(colKey, "PAL");
	SaveColorParams(palColKey, cols.mPALParams);
	colKey.setBool("Use separate color profiles", cols.mbUsePALParams);

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	key.setBool("Disk: SIO patch enabled", g_sim.IsDiskSIOPatchEnabled());
	key.setBool("Disk: SIO override detection enabled", g_sim.IsDiskSIOOverrideDetectEnabled());
	key.setBool("Disk: Accurate sector timing", disk.IsAccurateSectorTimingEnabled());
	key.setBool("Disk: Drive sounds", disk.AreDriveSoundsEnabled());
	key.setInt("Disk: Emulation mode", disk.GetEmulationMode());
	key.setBool("Disk: Sector counter enabled", g_sim.IsDiskSectorCounterEnabled());

	key.setBool("Audio: Dual POKEYs enabled", g_sim.IsDualPokeysEnabled());
	key.setBool("Audio: Non-linear mixing", pokey.IsNonlinearMixingEnabled());

	key.setBool("SoundBoard: Enabled", g_sim.GetSoundBoard() != NULL);
	key.setInt("SoundBoard: Memory base", g_sim.GetSoundBoardMemBase());

	key.setBool("SlightSID: Enabled", g_sim.GetSlightSID() != NULL);
	key.setBool("Covox: Enabled", g_sim.GetCovox() != NULL);
	key.setBool("XEP80: Enabled", g_sim.GetXEP80() != NULL);

	ATCPUEmulator& cpu = g_sim.GetCPU();
	key.setBool("CPU: History enabled", cpu.IsHistoryEnabled());
	key.setBool("CPU: Pathfinding enabled", cpu.IsPathfindingEnabled());
	key.setBool("CPU: Stop on BRK", cpu.GetStopOnBRK());
	key.setBool("CPU: Allow NMI blocking", cpu.IsNMIBlockingEnabled());
	key.setBool("CPU: Allow illegal instructions", cpu.AreIllegalInsnsEnabled());
	key.setInt("CPU: Chip type", cpu.GetCPUMode());
	key.setInt("CPU: Clock multiplier", cpu.GetSubCycles());

	key.setBool("CPU: Shadow ROMs", g_sim.GetShadowROMEnabled());
	key.setBool("CPU: Shadow cartridges", g_sim.GetShadowCartridgeEnabled());

	IATHostDeviceEmulator *hd = g_sim.GetHostDevice();
	if (hd) {
		VDStringA keyName;

		for(int i=0; i<4; ++i) {
			keyName = "Hard disk: Host path";

			if (i)
				keyName.append_sprintf(" %u", i+2);

			key.setString(keyName.c_str(), hd->GetBasePath(i));
		}

		key.setBool("Hard disk: Enabled", hd->IsEnabled());
		key.setBool("Hard disk: Read only", hd->IsReadOnly());
		key.setBool("Hard disk: Burst IO enabled", hd->IsBurstIOEnabled());
		key.setBool("Hard disk: Long name encoding", hd->IsLongNameEncodingEnabled());
	}

	IATPCLinkDevice *pclink = g_sim.GetPCLink();
	key.setBool("PCLink: Enabled", g_sim.GetPCLink() != NULL);
	if (pclink) {
		VDStringW path;
		if (key.setString("PCLink: Host path", pclink->GetBasePath()))

		key.setBool("PCLink: Read only", pclink->IsReadOnly());
	}

	key.setInt("Display: Filter mode", g_dispFilterMode);
	key.setInt("Display: Filter sharpness", g_dispFilterSharpness);
	key.setInt("Display: Stretch mode", g_displayStretchMode);

	key.setInt("Video: Enhanced text mode", g_enhancedText);

	key.setBool("View: Show FPS", g_showFps);
	key.setBool("View: Vertical sync", gtia.IsVsyncEnabled());
	key.setBool("View: 80-column view enabled", g_xepViewEnabled);
	key.setBool("View: 80-column view autoswitching enabled", g_xepViewAutoswitchingEnabled);

	key.setBool("Printer: Enabled", g_sim.IsPrinterEnabled());
	key.setBool("Misc: R-Time 8 Enabled", g_sim.IsRTime8Enabled());
	key.setBool("Serial Ports: Enabled", g_sim.IsRS232Enabled());

	if (g_sim.IsRS232Enabled()) {
		ATRS232Config config;
		g_sim.GetRS232()->GetConfig(config);

		key.setInt("Serial Ports: Device mode", config.mDeviceMode);
		key.setBool("Serial Ports: Telnet emulation", config.mbTelnetEmulation);
		key.setBool("Serial Ports: Telnet LF conversion", config.mbTelnetLFConversion);
		key.setBool("Serial Ports: Allow outbound connections", config.mbAllowOutbound);
		key.setString("Serial Ports: Telnet terminal type", config.mTelnetTermType.c_str());
		key.setInt("Serial Ports: Listen port", config.mListenPort);
		key.setInt("Serial Ports: Modem connection rate", config.mConnectionSpeed);
		key.setBool("Serial Ports: Listen for IPv6 connections", config.mbListenForIPv6);
		key.setBool("Serial Ports: Disable throttling", config.mbDisableThrottling);
		key.setBool("Serial Ports: Require matched DTE rate", config.mbRequireMatchedDTERate);
		key.setBool("Serial Ports: Enable extended baud rates", config.mbExtendedBaudRates);
		key.setInt("Serial Ports; SIO emulation level", config.m850SIOLevel);
		key.setString("Serial Ports: Dial address", config.mDialAddress.c_str());
		key.setString("Serial Ports: Dial service", config.mDialService.c_str());
	}

	key.setBool("Console: Keyboard present", g_sim.IsKeyboardPresent());
	key.setBool("Console: Force self test", g_sim.IsForcedSelfTest());
	key.setBool("Console: Cartridge switch", g_sim.GetCartridgeSwitch());

	const bool dragonCartEnabled = g_sim.IsDragonCartEnabled();
	key.setBool("DragonCart: Enabled", dragonCartEnabled);

	if (dragonCartEnabled) {
		const ATDragonCartSettings& settings = g_sim.GetDragonCart()->GetSettings();

		key.setInt("DragonCart: Network address", settings.mNetAddr);
		key.setInt("DragonCart: Network mask", settings.mNetMask);
		key.setInt("DragonCart: Access mode", settings.mAccessMode);
	}

	key.setBool("Keyboard: Raw mode", g_kbdOpts.mbRawKeys);
	key.setInt("Keyboard: Arrow key mode", g_kbdOpts.mArrowKeyMode);
	key.setInt("Keyboard: Layout mode", g_kbdOpts.mLayoutMode);
	key.setBool("Keyboard: Allow shift on cold reset", g_kbdOpts.mbAllowShiftOnColdReset);
	key.setBool("Keyboard: Enable function keys", g_kbdOpts.mbEnableFunctionKeys);

	ATLightPenPort *lpp = g_sim.GetLightPenPort();
	key.setInt("Light Pen: Adjust X", lpp->GetAdjustX());
	key.setInt("Light Pen: Adjust Y", lpp->GetAdjustY());

	key.setBool("Mouse: Auto-capture", g_mouseAutoCapture);

	key.setInt("Speed: Frame rate modifier", VDRoundToInt((g_speedModifier + 1.0f) * 100.0f));
	key.setInt("Speed: Frame rate mode", g_frameRateMode);

	SaveInputMaps();
	SaveMountedImages();

	IATDebugger *dbg = ATGetDebugger();
	if (dbg) {
		key.setBool("Debugger: Break on EXE run address", dbg->IsBreakOnEXERunAddrEnabled());
	}
}

void SaveInputMaps() {
	VDRegistryAppKey imapKey("Settings\\Input maps");
	g_sim.GetInputManager()->Save(imapKey);
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

bool ATUIProcessMessages(bool waitForMessage, int& returnCode) {
	ATUIProfileBeginRegion(kATUIProfileRegion_NativeEvents);

	HWND hwndChain[16];

	for(int i=0; i<2; ++i) {
		DWORD flags = i ? PM_REMOVE : PM_QS_INPUT | PM_REMOVE;

		MSG msg;
		while(PeekMessage(&msg, NULL, 0, 0, flags)) {
			if (msg.message == WM_QUIT) {
				ATUIProfileEndRegion();
				returnCode = (int)msg.wParam;
				return false;
			}

			if (msg.hwnd) {
				HWND hwndOwner;
				switch(msg.message) {
					case WM_KEYDOWN:
					case WM_SYSKEYDOWN:
					case WM_KEYUP:
					case WM_SYSKEYUP:
					case WM_CHAR:
						{
							hwndOwner = GetAncestor(msg.hwnd, GA_ROOT);
							if (hwndOwner) {
								if (SendMessage(hwndOwner, ATWM_APP_PRETRANSLATE, 0, (LPARAM)&msg))
									continue;
							}

							UINT preMsg = 0;

							switch(msg.message) {
								case WM_KEYDOWN:	preMsg = ATWM_PREKEYDOWN;		break;
								case WM_SYSKEYDOWN:	preMsg = ATWM_PRESYSKEYDOWN;	break;
								case WM_KEYUP:		preMsg = ATWM_PREKEYUP;			break;
								case WM_SYSKEYUP:	preMsg = ATWM_PRESYSKEYUP;		break;
							}

							if (preMsg) {
								size_t n = 0;

								for(HWND hwndTarget = msg.hwnd; hwndTarget && hwndTarget != hwndOwner; hwndTarget = GetAncestor(hwndTarget, GA_PARENT)) {
									hwndChain[n++] = hwndTarget;

									if (n >= vdcountof(hwndChain))
										break;
								}

								bool eat = false;
								while(n > 0) {
									if (SendMessage(hwndChain[--n], preMsg, msg.wParam, msg.lParam)) {
										eat = true;
										break;
									}
								}

								if (eat)
									continue;
							}
						}
						break;

					case WM_SYSCHAR:
						if (g_hwnd && msg.hwnd != g_hwnd && GetAncestor(msg.hwnd, GA_ROOT) == g_hwnd) {
							msg.hwnd = g_hwnd;
						}
						break;

					case WM_MOUSEWHEEL:
						{
							POINT pt = { (short)LOWORD(msg.lParam), (short)HIWORD(msg.lParam) };
							HWND hwndUnder = WindowFromPoint(pt);

							if (hwndUnder && GetWindowThreadProcessId(hwndUnder, NULL) == GetCurrentThreadId())
								msg.hwnd = hwndUnder;
						}
						break;
				}
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	if (!g_ATCmdLineRead) {
		g_ATCmdLineRead = true;

		ReadCommandLine(g_hwnd, g_ATCmdLine);
	}
	ATUIProfileEndRegion();

	if (waitForMessage)
		WaitMessage();

	return true;
}

int RunMainLoop2(HWND hwnd) {
	bool commandLineRead = false;
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

	g_winCaptionUpdater.Update(hwnd, true, 0, 0, 0);

	int rcode = 0;
	bool lastIsRunning = false;
	for(;;) {
		if (!ATUIProcessMessages(false, rcode))
			break;

		bool isRunning = g_sim.IsRunning();
		if (!isRunning)
			ATUIFlushDisplay();

		if (ATUIGetQueue().Run())
			continue;


		if (isRunning != lastIsRunning) {
			if (isRunning)
				timeBeginPeriod(1);
			else
				timeEndPeriod(1);

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
						g_winCaptionUpdater.Update(hwnd, true, ticks, fps, cpu);

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
							ATUIProfileBeginRegion(kATUIProfileRegion_Idle);
							::MsgWaitForMultipleObjects(0, NULL, FALSE, ticks, QS_ALLINPUT);
							ATUIProfileEndRegion();
							continue;
						}
					}
				}
			}

			// do not allow frame drops if we are recording
			if (g_pVideoWriter)
				dropFrame = false;

			if (g_sim.GetAntic().GetBeamY() == 0)
				ATUIProfileBeginFrame();

			ATUIProfileBeginRegion(kATUIProfileRegion_Simulation);
			ATSimulator::AdvanceResult ar = g_sim.Advance(dropFrame);
			ATUIProfileEndRegion();

			if (ar == ATSimulator::kAdvanceResult_Stopped) {
				updateScreenPending = !g_sim.IsPaused();
			} else {
				updateScreenPending = false;

				if (ar == ATSimulator::kAdvanceResult_WaitingForFrame) {
					ATUIProfileBeginRegion(kATUIProfileRegion_Idle);
					::MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT);
					ATUIProfileEndRegion();
				}
			}

			continue;
		} else {
			if (ATGetDebugger()->Tick())
				continue;

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

			g_winCaptionUpdater.Update(hwnd, false, 0, 0, 0);
			nextTickUpdate = ticks - ticks % 60;
		}

		WaitMessage();
	}

	if (lastIsRunning)
		timeEndPeriod(1);

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

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int nCmdShow) {
	#ifdef _MSC_VER
		_CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
	#endif
	
	g_ATCmdLine.InitAlt(GetCommandLineW());

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

	VDRegistryProviderMemory *rpmem = NULL;

	const bool resetAll = g_ATCmdLine.FindAndRemoveSwitch(L"resetall");
	const VDStringW portableRegPath(VDMakePath(VDGetProgramPath().c_str(), L"Altirra.ini"));

	if (g_ATCmdLine.FindAndRemoveSwitch(L"portable") || VDDoesPathExist(portableRegPath.c_str())) {
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
	} else {
		if (resetAll)
			SHDeleteKey(HKEY_CURRENT_USER, _T("Software\\virtualdub.org\\Altirra"));
	}

	VDRegistryAppKey::setDefaultKey("Software\\virtualdub.org\\Altirra\\");

	int rval = 0;
	const wchar_t *token;
	if (g_ATCmdLine.FindAndRemoveSwitch(L"showfileassocdlg", token)) {
		unsigned long hwndParent = wcstoul(token, NULL, 16);

		ATUIShowDialogSetFileAssociations((VDGUIHandle)hwndParent, false);
	} else if (g_ATCmdLine.FindAndRemoveSwitch(L"removefileassocs", token)) {
		unsigned long hwndParent = wcstoul(token, NULL, 16);

		ATUIShowDialogRemoveFileAssociations((VDGUIHandle)hwndParent, false);
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
		if (g_ATCmdLine.FindAndRemoveSwitch(L"singleinstance"))
			singleInstance = true;
		else if (g_ATCmdLine.FindAndRemoveSwitch(L"nosingleinstance"))
			singleInstance = false;

		if (!singleInstance || !ATNotifyOtherInstance(g_ATCmdLine)) {
			if (d3d9lock)
				g_d3d9Lock.Lock();

			rval = RunInstance(nCmdShow);
		}
	}

	if (rpmem) {
		try {
			ATUISaveRegistry(portableRegPath.c_str());
		} catch(const MyError&) {
		}

		VDSetRegistryProvider(NULL);
		delete rpmem;
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
	ATUIInitCommandMappings();
	ATUIInitDefaultAccelTables();

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

	//VDVideoDisplaySetD3DFXFileName(L"d:\\shaders\\tvela2.fx");
	VDVideoDisplaySetDebugInfoEnabled(false);
	VDVideoDisplaySetMonitorSwitchingDXEnabled(true);
	VDVideoDisplaySetSecondaryDXEnabled(true);
	VDVideoDisplaySetD3D9ExEnabled(false);
	VDVideoDisplaySetTermServ3DEnabled(true);

	VDInitFilespecSystem();
	VDLoadFilespecSystemData();

	ATInitUIPanes();
	ATInitProfilerUI();
	ATUIRegisterDisplayPane();
	ATUIInitVirtualKeyMap(g_kbdOpts);
	ATUILoadAccelTables();

	try {
		ATUILoadMenu();
	} catch(const MyError& e) {
		e.post(NULL, "Altirra Error");
	}

	g_hInst = VDGetLocalModuleHandleW32();

	WNDCLASS wc = {};
	wc.lpszClassName = _T("AltirraMainWindow");
	ATOM atom = ATContainerWindow::RegisterCustom(wc);
	g_pMainWindow = new ATMainWindow;
	g_pMainWindow->AddRef();
	HWND hwnd = (HWND)g_pMainWindow->Create(atom, 150, 100, 640, 480, NULL, false);

	ATUISetProgressWindowParentW32(hwnd);

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

	ATInitPortMenus(g_sim.GetInputManager());

	g_winCaptionUpdater.Init(&g_sim);

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
	LoadROMImageSettings();
	LoadSettings(useHardwareBaseline);

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

	int returnCode = RunMainLoop(hwnd);

	SaveSettings();

	ATSetFullscreen(false);

	StopRecording();

	VDSaveFilespecSystemData();

	ATUIShutdownManager();

	ATShutdownEmuErrorHandler();
	ATShutdownDebugger();
	g_sim.Shutdown();

	g_d3d9Lock.Unlock();

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
