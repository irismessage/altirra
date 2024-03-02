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
#include <vd2/Riza/display.h>
#include <vd2/Riza/direct3d.h>
#include <vd2/Dita/services.h>
#include "console.h"
#include "simulator.h"
#include "cassette.h"
#include "ui.h"
#include "uiframe.h"
#include "debugger.h"
#include "hostdevice.h"
#include "pclink.h"
#include "Dialog.h"
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
#include "rs232.h"
#include "options.h"
#include "uienhancedtext.h"
#include "uikeyboard.h"
#include "uicaptionupdater.h"
#include "uiportmenus.h"
#include "uimrulist.h"
#include "uiprogress.h"
#include "uirender.h"

#pragma comment(lib, "comctl32")
#pragma comment(lib, "shlwapi")

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

void ATUIRegisterDragDropHandler(VDGUIHandle h);
void ATUIRevokeDragDropHandler(VDGUIHandle h);

void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);

void ATInitEmuErrorHandler(VDGUIHandle h, ATSimulator *sim);
void ATShutdownEmuErrorHandler();

bool ATIDEIsPhysicalDiskPath(const wchar_t *path);

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
bool ProcessRawKeyUp(int vkey);
void DoLoad(VDGUIHandle h, const wchar_t *path, bool vrw, bool rw, int cartmapper, ATLoadType loadType = kATLoadType_Other);
void DoBootWithConfirm(const wchar_t *path, bool vrw, bool rw, int cartmapper);

void LoadBaselineSettings();

///////////////////////////////////////////////////////////////////////////////

// {8437E294-45E9-4f9a-A77E-E8D3AAE385A0}
static const uint8 kATGUID_CopyDataCmdLine[16]={
	0x94, 0xE2, 0x37, 0x84, 0xE9, 0x45, 0x9A, 0x4F, 0xA7, 0x7E, 0xE8, 0xD3, 0xAA, 0xE3, 0x85, 0xA0
};

bool ATGetProcessUser(HANDLE hProcess, vdstructex<TOKEN_USER>& tokenUser) {
	bool success = false;

	HANDLE hToken;
	if (OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
		DWORD actual;
		if (GetTokenInformation(hToken, TokenUser, NULL, 0, &actual) || GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			tokenUser.resize(actual);
			if (GetTokenInformation(hToken, TokenUser, &*tokenUser, tokenUser.size(), &actual)) {
				success = true;
			}
		}

		CloseHandle(hToken);
	}

	return success;
}

class ATFindOtherInstanceHelper {
public:
	ATFindOtherInstanceHelper()
		: mhwndFound(NULL)
	{
	}

	HWND Run() {
		if (!ATGetProcessUser(GetCurrentProcess(), mCurrentUser))
			return NULL;

		EnumWindows(StaticCallback, (LPARAM)this);

		return mhwndFound;
	}

protected:
	static BOOL CALLBACK StaticCallback(HWND hwnd, LPARAM lParam) {
		return ((ATFindOtherInstanceHelper *)lParam)->Callback(hwnd);
	}

	BOOL Callback(HWND hwnd) {
		TCHAR className[64];
		
		if (!GetClassName(hwnd, className, sizeof(className)/sizeof(className[0])))
			return TRUE;

		if (_tcscmp(className, _T("AltirraMainWindow")))
			return TRUE;

		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);

		if (!pid)
			return TRUE;

		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
		if (!hProcess)
			return TRUE;

		vdstructex<TOKEN_USER> processUser;
		if (ATGetProcessUser(hProcess, processUser) && EqualSid(mCurrentUser->User.Sid, processUser->User.Sid))
			mhwndFound = hwnd;

		CloseHandle(hProcess);

		return !mhwndFound;
	}

	vdstructex<TOKEN_USER> mCurrentUser;
	HWND mhwndFound;
};

HWND ATFindOtherInstance() {
	ATFindOtherInstanceHelper helper;

	return helper.Run();
}

bool ATNotifyOtherInstance(const VDCommandLine& cmdLine) {
	HWND hwndOther = ATFindOtherInstance();

	if (!hwndOther)
		return false;

	VDStringW s;

	uint32 n = cmdLine.GetCount();
	for(uint32 i=0; i<n; ++i) {
		const VDStringSpanW arg = cmdLine(i);

		if (i)
			s += L' ';

		if (arg.find(' ') != VDStringSpanW::npos || arg.find('\\') != VDStringSpanW::npos || arg.find('"') != VDStringSpanW::npos) {
			s += L'"';

			for(VDStringSpanW::const_iterator it(arg.begin()), itEnd(arg.end()); it != itEnd; ++it) {
				const wchar_t c = *it;

				if (c == L'\\' || c == L'"')
					s += L'\\';

				s += c;
			}

			s += L'"';
		} else {
			s += arg;
		}
	}

	s += L'\0';

	// Copy the current directory and current drive paths into the block. We need this to resolve
	// relative paths properly.
	wchar_t chdir[MAX_PATH];
	chdir[0] = 0;
	DWORD actual = ::GetCurrentDirectoryW(MAX_PATH, chdir);
	if (actual && actual < MAX_PATH) {
		s += L"chdir";
		s += L'\0';
		s += chdir;
		s += L'\0';
	}

	DWORD driveMask = ::GetLogicalDrives();
	for(int i=0; i<26; ++i) {
		if (driveMask & (1 << i)) {
			wchar_t envVarName[4] = {
				L'=',
				L'A' + i,
				L':',
				0
			};

			chdir[0] = 0;
			actual = GetEnvironmentVariableW(envVarName, chdir, MAX_PATH);
			if (actual && actual < MAX_PATH) {
				s += envVarName;
				s += L'\0';
				s += chdir;
				s += L'\0';
			}
		}
	}

	vdfastvector<uint8> rawdata(16 + s.size() * sizeof(wchar_t));
	memcpy(rawdata.data(), kATGUID_CopyDataCmdLine, 16);
	memcpy(rawdata.data() + 16, s.data(), s.size()*sizeof(wchar_t));

	COPYDATASTRUCT cds;
	cds.dwData = 0xA7000001;
	cds.lpData = rawdata.data();
	cds.cbData = rawdata.size();

	SetForegroundWindow(hwndOther);
	SendMessage(hwndOther, WM_COPYDATA, (WPARAM)hwndOther, (LPARAM)&cds);
	return true;
}

///////////////////////////////////////////////////////////////////////////////

class ATDisplayBasePane : public ATUIPane {
public:
	ATDisplayBasePane();

	virtual void CaptureMouse() = 0;
	virtual void OnSize() = 0;
	virtual void ResetDisplay() = 0;
	virtual bool IsTextSelected() const = 0;
	virtual void Copy() = 0;
	virtual void UpdateTextDisplay(bool enabled) = 0;
	virtual void UpdateTextModeFont() = 0;
	virtual void UpdateFilterMode() = 0;
};

ATDisplayBasePane::ATDisplayBasePane()
	: ATUIPane(kATUIPaneId_Display, L"Display")
{
}

///////////////////////////////////////////////////////////////////////////////
HINSTANCE g_hInst;
HWND g_hwnd;
ATContainerWindow *g_pMainWindow;
HACCEL g_hAccel;
IVDVideoDisplay *g_pDisplay;
HMENU g_hMenu;
HMENU g_hMenuMRU;

VDCommandLine g_ATCmdLine;
bool g_ATCmdLineRead;

ATSimulator g_sim;

bool g_fullscreen = false;
bool g_fullscreenWasMaxed = false;
bool g_mouseCaptured = false;
bool g_mouseClipped = false;
bool g_mouseAutoCapture = true;
bool g_pauseInactive = true;
bool g_winActive = true;
bool g_showFps = false;
int g_lastVkeyPressed;
int g_lastVkeySent;

ATUIKeyboardOptions g_kbdOpts;

enum ATDisplayFilterMode {
	kATDisplayFilterMode_Point,
	kATDisplayFilterMode_Bilinear,
	kATDisplayFilterMode_Bicubic,
	kATDisplayFilterMode_AnySuitable,
	kATDisplayFilterMode_SharpBilinear,
	kATDisplayFilterModeCount
};

ATDisplayFilterMode g_dispFilterMode = kATDisplayFilterMode_Bilinear;
int g_dispFilterSharpness = 0;
int g_enhancedText = 0;
LOGFONTW g_enhancedTextFont;

ATSaveStateWriter::Storage g_quickSave;

vdautoptr<ATAudioWriter> g_pAudioWriter;
vdautoptr<IATVideoWriter> g_pVideoWriter;

enum ATDisplayStretchMode {
	kATDisplayStretchMode_Unconstrained,
	kATDisplayStretchMode_PreserveAspectRatio,
	kATDisplayStretchMode_SquarePixels,
	kATDisplayStretchMode_Integral,
	kATDisplayStretchMode_IntegralPreserveAspectRatio,
	kATDisplayStretchModeCount
};

ATDisplayStretchMode g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;

ATUIWindowCaptionUpdater g_winCaptionUpdater;

#define MYWM_PRESYSKEYDOWN (WM_APP + 0x120)
#define MYWM_PRESYSKEYUP (WM_APP + 0x121)
#define MYWM_PREKEYDOWN (WM_APP + 0x122)
#define MYWM_PREKEYUP (WM_APP + 0x123)

///////////////////////////////////////////////////////////////////////////////

enum {
	kATUISpeedFlags_Turbo = 0x01,
	kATUISpeedFlags_TurboPulse = 0x02,
	kATUISpeedFlags_Slow = 0x04,
	kATUISpeedFlags_SlowPulse = 0x08
};

enum ATFrameRateMode {
	kATFrameRateMode_Hardware,
	kATFrameRateMode_Broadcast,
	kATFrameRateMode_Integral,
	kATFrameRateModeCount
};

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

class ATUIDialogSpeedOptions : public VDDialogFrameW32 {
public:
	ATUIDialogSpeedOptions();

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	void OnHScroll(uint32 id, int code);

	void UpdateSpeedLabel();

	int GetAdjustedSpeedTicks();
};

ATUIDialogSpeedOptions::ATUIDialogSpeedOptions()
	: VDDialogFrameW32(IDD_SPEED)
{
}

bool ATUIDialogSpeedOptions::OnLoaded() {
	TBSetRange(IDC_SPEED_ADJUST, 1, 550);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogSpeedOptions::OnDataExchange(bool write) {
	if (write) {
		if (IsButtonChecked(IDC_RATE_HARDWARE))
			g_frameRateMode = kATFrameRateMode_Hardware;
		else if (IsButtonChecked(IDC_RATE_BROADCAST))
			g_frameRateMode = kATFrameRateMode_Broadcast;
		else if (IsButtonChecked(IDC_RATE_INTEGRAL))
			g_frameRateMode = kATFrameRateMode_Integral;

		g_speedModifier = (float)GetAdjustedSpeedTicks() / 100.0f - 1.0f;

		ATUIUpdateSpeedTiming();
	} else {
		CheckButton(IDC_RATE_HARDWARE, g_frameRateMode == kATFrameRateMode_Hardware);
		CheckButton(IDC_RATE_BROADCAST, g_frameRateMode == kATFrameRateMode_Broadcast);
		CheckButton(IDC_RATE_INTEGRAL, g_frameRateMode == kATFrameRateMode_Integral);

		int rawTicks = VDRoundToInt((g_speedModifier + 1.0f) * 100.0f);

		if (rawTicks >= 100) {
			if (rawTicks > 100)
				rawTicks += 50;
			else
				rawTicks += 25;
		}

		TBSetValue(IDC_SPEED_ADJUST, rawTicks);

		UpdateSpeedLabel();
	}
}

void ATUIDialogSpeedOptions::OnHScroll(uint32 id, int code) {
	UpdateSpeedLabel();
}

void ATUIDialogSpeedOptions::UpdateSpeedLabel() {
	SetControlTextF(IDC_STATIC_SPEED_ADJUST, L"%d%%", GetAdjustedSpeedTicks());
}

int ATUIDialogSpeedOptions::GetAdjustedSpeedTicks() {
	int rawTicks = TBGetValue(IDC_SPEED_ADJUST);

	if (rawTicks >= 100) {
		if (rawTicks >= 150)
			rawTicks -= 50;
		else
			rawTicks = 100;
	}

	return rawTicks;
}

void ATUIShowDialogSpeedOptions(VDGUIHandle parent) {
	ATUIDialogSpeedOptions dlg;

	dlg.ShowDialog(parent);
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
				g_sim.ColdReset();
			break;
		case kATInputTrigger_WarmReset:
			if (state)
				g_sim.ColdReset();
			break;
		case kATInputTrigger_Turbo:
			ATUISetTurboPulse(state);
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
	}
}

ATInputConsoleCallback g_inputConsoleCallback;

///////////////////////////////////////////////////////////////////////////////

bool ATUIConfirmDiscardCartridge(VDGUIHandle h) {
	if (!g_sim.IsStorageDirty(kATStorageId_Cartridge))
		return true;

	return IDYES == MessageBoxW((HWND)h, L"Modified cartridge image has not been saved. Discard it anyway?", L"Altirra Warning", MB_ICONEXCLAMATION | MB_YESNO);
}

bool ATUIConfirmDiscardAllStorage(VDGUIHandle h, const wchar_t *prompt, bool includeUnmountables = false) {
	typedef vdfastvector<ATStorageId> DirtyIds;
	DirtyIds dirtyIds;

	typedef vdfastvector<ATDebuggerStorageId> DbgDirtyIds;
	DbgDirtyIds dbgDirtyIds;

	g_sim.GetDirtyStorage(dirtyIds);

	if (includeUnmountables) {
		IATDebugger *dbg = ATGetDebugger();
		if (dbg)
			dbg->GetDirtyStorage(dbgDirtyIds);
	}

	if (dirtyIds.empty() && dbgDirtyIds.empty())
		return true;

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

			case kATStorageId_IDEFirmware:
				switch(unit) {
					case 0:
						msg += L"IDE main firmware";
						break;

					case 1:
						msg += L"IDE SDX firmware";
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

	return IDYES == MessageBoxW((HWND)h, msg.c_str(), L"Altirra Warning", MB_YESNO | MB_ICONEXCLAMATION);
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
			if (mode != kATHardwareMode_800XL && mode != kATHardwareMode_1200XL)
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
	}

	g_sim.SetMemoryMode(mode);
	g_sim.ColdReset();
}

void DoLoad(VDGUIHandle h, const wchar_t *path, bool vrw, bool rw, int cartmapper, ATLoadType loadType) {
	vdfastvector<uint8> captureBuffer;

	ATCartLoadContext cartctx = {};
	cartctx.mbReturnOnUnknownMapper = true;

	if (cartmapper) {
		cartctx.mbReturnOnUnknownMapper = false;
		cartctx.mCartMapper = cartmapper;
	} else
		cartctx.mpCaptureBuffer = &captureBuffer;

	ATLoadContext ctx;
	ctx.mLoadType = loadType;
	ctx.mpCartLoadContext = &cartctx;
	if (!g_sim.Load(path, vrw, rw, &ctx)) {
		int mapper = ATUIShowDialogCartridgeMapper(h, cartctx.mCartSize, captureBuffer.data());

		if (mapper >= 0) {
			cartctx.mbReturnOnUnknownMapper = false;
			cartctx.mCartMapper = mapper;

			g_sim.Load(path, vrw, rw, &ctx);
		}
	}
}

void DoBootWithConfirm(const wchar_t *path, bool vrw, bool rw, int cartmapper) {
	if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?"))
		return;

	try {
		g_sim.UnloadAll();

		DoLoad((VDGUIHandle)g_hwnd, path, false, false, 0);

		g_sim.ColdReset();

		ATAddMRUListItem(path);
		ATUpdateMRUListMenu(g_hMenuMRU, ID_FILE_MRU_BASE, ID_FILE_MRU_BASE + 99);
	} catch(const MyError& e) {
		e.post(g_hwnd, "Altirra Error");
	}
}

void OnCommandOpen(bool forceColdBoot) {
	if (forceColdBoot && !ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?"))
		return;

	VDStringW fn(VDGetLoadFileName('load', (VDGUIHandle)g_hwnd, L"Load disk, cassette, cartridge, or program image",
		L"All supported types\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.xex;*.obx;*.com;*.car;*.rom;*.a52;*.bin;*.cas;*.wav;*.zip;*.atz;*.gz;*.bas\0"
		L"Atari program (*.xex,*.obx,*.com)\0*.xex;*.obx;*.com\0"
		L"BASIC program (*.bas)\0*.bas\0"
		L"Atari disk image (*.atr,*.xfd,*.dcm)\0*.atr;*.xfd;*.dcm\0"
		L"Protected disk image (*.pro)\0*.pro\0"
		L"VAPI disk image (*.atx)\0*.atx\0"
		L"Cartridge (*.rom,*.bin,*.a52,*.car)\0*.rom;*.bin;*.a52;*.car\0"
		L"Cassette tape (*.cas,*.wav)\0*.cas;*.wav\0"
		L"Zip archive (*.zip)\0*.zip\0"
		L"gzip archive (*.gz;*.atz)\0*.gz;*.atz\0"
		L"All files\0*.*\0",
		L"atr"
		));

	if (!fn.empty()) {
		try {
			if (forceColdBoot)
				g_sim.UnloadAll();

			DoLoad((VDGUIHandle)g_hwnd, fn.c_str(), false, false, 0);

			if (forceColdBoot)
				g_sim.ColdReset();

			ATAddMRUListItem(fn.c_str());
			ATUpdateMRUListMenu(g_hMenuMRU, ID_FILE_MRU_BASE, ID_FILE_MRU_BASE + 99);
		} catch(const MyError& e) {
			e.post(g_hwnd, "Altirra Error");
		}
	}
}

void ATSetFullscreen(bool fs) {
	if (!g_sim.IsRunning())
		fs = false;

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

	if (frame)
		frame->SetFullScreen(fs);

	DWORD style = GetWindowLong(g_hwnd, GWL_STYLE);
	if (fs) {
		g_fullscreenWasMaxed = ::IsZoomed(g_hwnd) != 0;

		SetMenu(g_hwnd, NULL);
		SetWindowLong(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
		SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOZORDER);
		SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
		ShowWindow(g_hwnd, SW_SHOWNORMAL);
		g_fullscreen = true;
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(true, g_ATOptions.mFullScreenWidth, g_ATOptions.mFullScreenHeight, g_ATOptions.mFullScreenRefreshRate);
		g_sim.SetFrameSkipEnabled(false);
		ShowWindow(g_hwnd, SW_MAXIMIZE);
	} else {
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(false);
		SetWindowLong(g_hwnd, GWL_STYLE, (style | WS_OVERLAPPEDWINDOW) & ~WS_POPUP);
		SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE);

		ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
		if (g_fullscreenWasMaxed)
			ShowWindow(g_hwnd, SW_SHOWMAXIMIZED);

		g_fullscreen = false;
		SetMenu(g_hwnd, g_hMenu);
		g_sim.SetFrameSkipEnabled(true);
	}
}

void OnCommandToggleFullScreen() {
	if (!g_enhancedText)
		ATSetFullscreen(!g_fullscreen);
}

void OnCommandFilterModeNext() {
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

	ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->UpdateFilterMode();
}

void OnCommandAnticVisualizationNext() {
	ATAnticEmulator& antic = g_sim.GetAntic();
	antic.SetAnalysisMode((ATAnticEmulator::AnalysisMode)(((int)antic.GetAnalysisMode() + 1) % ATAnticEmulator::kAnalyzeModeCount));
}

void OnCommandGTIAVisualizationNext() {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetAnalysisMode((ATGTIAEmulator::AnalysisMode)(((int)gtia.GetAnalysisMode() + 1) % ATGTIAEmulator::kAnalyzeCount));
}

void OnCommandEnhancedTextFont() {
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

		ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
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
		} else
			ProcessKey(c, false, true, useCooldown);
	}
}

void OnCommandPaste() {
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

void OnCommandTogglePause() {
	if (g_sim.IsRunning())
		g_sim.Suspend();
	else
		g_sim.Resume();

	g_sim.GetGTIA().UpdateScreen(true, true);
}

void ResizeDisplay() {
	ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
	if (pane)
		pane->OnSize();
}

void StopAudioRecording() {
	if (g_pAudioWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(NULL);
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

enum ATVideoRecordingFrameRate {
	kATVideoRecordingFrameRate_Normal,
	kATVideoRecordingFrameRate_NTSCRatio,
	kATVideoRecordingFrameRate_Integral,
	kATVideoRecordingFrameRateCount
};

class ATUIDialogVideoRecording : public VDDialogFrameW32 {
public:
	ATUIDialogVideoRecording(bool pal)
		: VDDialogFrameW32(IDD_VIDEO_RECORDING)
		, mbPAL(pal)
		, mbHalfRate(false)
		, mEncoding(kATVideoEncoding_ZMBV)
		, mFrameRate(kATVideoRecordingFrameRate_Normal)
	{}

	ATVideoEncoding GetEncoding() const { return mEncoding; }
	ATVideoRecordingFrameRate GetFrameRate() const { return mFrameRate; }
	bool GetHalfRate() const { return mbHalfRate; }

protected:
	bool OnLoaded();
	void OnDataExchange(bool write);
	bool OnCommand(uint32 id, uint32 extcode);
	void UpdateFrameRateControls();

	bool mbPAL;
	bool mbHalfRate;
	ATVideoEncoding mEncoding;
	ATVideoRecordingFrameRate mFrameRate;

	VDStringW mBaseFrameRateLabels[3];

	static const uint32 kFrameRateIds[];
};

const uint32 ATUIDialogVideoRecording::kFrameRateIds[]={
	IDC_FRAMERATE_NORMAL,
	IDC_FRAMERATE_NTSCRATIO,
	IDC_FRAMERATE_INTEGRAL,
};

bool ATUIDialogVideoRecording::OnLoaded() {
	for(int i=0; i<3; ++i)
		GetControlText(kFrameRateIds[i], mBaseFrameRateLabels[i]);

	return VDDialogFrameW32::OnLoaded();
}

void ATUIDialogVideoRecording::OnDataExchange(bool write) {
	VDRegistryAppKey key("Settings");

	if (write) {
		if (IsButtonChecked(IDC_VC_ZMBV))
			mEncoding = kATVideoEncoding_ZMBV;
		else if (IsButtonChecked(IDC_VC_RLE))
			mEncoding = kATVideoEncoding_RLE;
		else
			mEncoding = kATVideoEncoding_Raw;

		if (IsButtonChecked(IDC_FRAMERATE_NORMAL))
			mFrameRate = kATVideoRecordingFrameRate_Normal;
		else if (IsButtonChecked(IDC_FRAMERATE_NTSCRATIO))
			mFrameRate = kATVideoRecordingFrameRate_NTSCRatio;
		else if (IsButtonChecked(IDC_FRAMERATE_INTEGRAL))
			mFrameRate = kATVideoRecordingFrameRate_Integral;

		mbHalfRate = IsButtonChecked(IDC_HALF_RATE);

		key.setInt("Video Recording: Compression Mode", mEncoding);
		key.setInt("Video Recording: Frame Rate", mFrameRate);
		key.setInt("Video Recording: Half Rate", mbHalfRate);
	} else {
		mEncoding = (ATVideoEncoding)key.getEnumInt("Video Recording: Compression Mode", kATVideoEncodingCount, kATVideoEncoding_ZMBV);
		mFrameRate = (ATVideoRecordingFrameRate)key.getEnumInt("Video Recording: FrameRate", kATVideoRecordingFrameRateCount, kATVideoRecordingFrameRate_Normal);
		mbHalfRate = key.getBool("Video Recording: Half Rate", false);

		CheckButton(IDC_HALF_RATE, mbHalfRate);

		switch(mEncoding) {
			case kATVideoEncoding_Raw:
				CheckButton(IDC_VC_NONE, true);
				break;

			case kATVideoEncoding_RLE:
				CheckButton(IDC_VC_RLE, true);
				break;

			case kATVideoEncoding_ZMBV:
				CheckButton(IDC_VC_ZMBV, true);
				break;
		}

		switch(mFrameRate) {
			case kATVideoRecordingFrameRate_Normal:
				CheckButton(IDC_FRAMERATE_NORMAL, true);
				break;

			case kATVideoRecordingFrameRate_NTSCRatio:
				CheckButton(IDC_FRAMERATE_NTSCRATIO, true);
				break;

			case kATVideoRecordingFrameRate_Integral:
				CheckButton(IDC_FRAMERATE_INTEGRAL, true);
				break;
		}

		UpdateFrameRateControls();
	}
}

bool ATUIDialogVideoRecording::OnCommand(uint32 id, uint32 extcode) {
	if (id == IDC_HALF_RATE) {
		bool halfRate = IsButtonChecked(IDC_HALF_RATE);

		if (mbHalfRate != halfRate) {
			mbHalfRate = halfRate;

			UpdateFrameRateControls();
		}
	}

	return false;
}

void ATUIDialogVideoRecording::UpdateFrameRateControls() {
	VDStringW s;

	static const double kFrameRates[][2]={
		{ 3579545.0 / (2.0*114.0*262.0), 1773447.0 / (114.0*312.0) },
		{ 60000.0/1001.0, 50000.0/1001.0 },
		{ 60.0, 50.0 },
	};

	for(int i=0; i<3; ++i) {
		VDStringRefW label(mBaseFrameRateLabels[i]);
		VDStringW::size_type pos = label.find(L'|');

		if (pos != VDStringW::npos) {
			if (mbPAL)
				label = label.subspan(pos+1, VDStringW::npos);
			else
				label = label.subspan(0, pos);
		}

		VDStringW t;

		t.sprintf(L"%.3f fps (%.*ls)", kFrameRates[i][mbPAL] * (mbHalfRate ? 0.5 : 1.0), label.size(), label.data());

		SetControlText(kFrameRateIds[i], t.c_str());
	}
}

bool OnCommand(UINT id) {
	switch(id) {
		case ID_FILE_OPENIMAGE:
			OnCommandOpen(false);
			return true;

		case ID_FILE_BOOTIMAGE:
			OnCommandOpen(true);
			return true;

		case ID_FILE_DISKDRIVES:
			ATUIShowDiskDriveDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_FILE_QUICKLOADSTATE:
			if (!g_quickSave.empty()) {
				try {
					ATSaveStateReader reader(g_quickSave.data(), (uint32)g_quickSave.size());

					g_sim.LoadState(reader);
				} catch(const MyError& e) {
					e.post(g_hwnd, "Load state error");
				}
			}
			return true;

		case ID_FILE_QUICKSAVESTATE:
			try {
				g_quickSave.clear();

				ATSaveStateWriter writer(g_quickSave);

				g_sim.SaveState(writer);
			} catch(const MyError& e) {
				e.post(g_hwnd, "Save state error");
			}
			return true;

		case ID_FILE_ATTACHCARTRIDGE:
		case ID_CART2_ATTACH:
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				VDStringW fn(VDGetLoadFileName('cart', (VDGUIHandle)g_hwnd, L"Load cartridge",
					L"All supported types\0*.bin;*.car;*.rom;*.a52;*.zip\0"
					L"Cartridge image (*.bin,*.car,*.rom,*.a52)\0*.bin;*.car;*.rom;*.a52\0"
					L"Zip archive (*.zip)\0*.zip\0"
					L"All files\0*.*\0",

					L"bin"));

				if (!fn.empty()) {
					try {
						vdfastvector<uint8> captureBuffer;

						ATCartLoadContext cartctx = {};
						cartctx.mbReturnOnUnknownMapper = true;
						cartctx.mpCaptureBuffer = &captureBuffer;

						if (!g_sim.LoadCartridge(id == ID_CART2_ATTACH, fn.c_str(), &cartctx)) {
							int mapper = ATUIShowDialogCartridgeMapper((VDGUIHandle)g_hwnd, cartctx.mCartSize, captureBuffer.data());

							if (mapper >= 0) {
								cartctx.mbReturnOnUnknownMapper = false;
								cartctx.mCartMapper = mapper;

								g_sim.LoadCartridge(id == ID_CART2_ATTACH, fn.c_str(), &cartctx);
							}
						}

						g_sim.ColdReset();
					} catch(const MyError& e) {
						e.post(g_hwnd, "Altirra Error");
					}
				}
			}
			return true;

		case ID_FILE_DETACHCARTRIDGE:
		case ID_CART2_DETACH:
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				uint32 unit = (id == ID_CART2_DETACH);

				if (!unit && g_sim.GetHardwareMode() == kATHardwareMode_5200)
					g_sim.LoadCartridge5200Default();
				else
					g_sim.UnloadCartridge(unit);
				g_sim.ColdReset();
			}
			return true;

		case ID_ATTACHSPECIALCARTRIDGE_SUPERCHARGER3D:
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				g_sim.LoadCartridgeSC3D();
				g_sim.ColdReset();
			}
			return true;

		case ID_ATTACHSPECIALCARTRIDGE_EMPTY1MB:
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				g_sim.LoadCartridgeFlash1Mb(false);
				g_sim.ColdReset();
			}
			return true;

		case ID_ATTACHSPECIALCARTRIDGE_EMPTY1MBMYIDE:
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				g_sim.LoadCartridgeFlash1Mb(true);
				g_sim.ColdReset();
			}
			return true;

		case ID_ATTACHSPECIALCARTRIDGE_EMPTY8MB:
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				g_sim.LoadCartridgeFlash8Mb(false);
				g_sim.ColdReset();
			}
			return true;

		case ID_ATTACHSPECIALCARTRIDGE_EMPTY8MB_NEWER:
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				g_sim.LoadCartridgeFlash8Mb(true);
				g_sim.ColdReset();
			}
			return true;

		case ID_ATTACHSPECIALCARTRIDGE_EMPTYSIC:
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				g_sim.LoadCartridgeFlashSIC();
				g_sim.ColdReset();
			}
			return true;

		case ID_ATTACHSPECIALCARTRIDGE_BASIC:
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				g_sim.LoadCartridgeBASIC();
				g_sim.ColdReset();
			}
			return true;

		case ID_FILE_SAVECARTRIDGE:
			try {
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
			} catch(const MyError& e) {
				e.post(g_hwnd, "Altirra Error");
			}
			return true;

		case ID_FILE_SAVEKMKJZMAIN:
		case ID_FILE_SAVEKMKJZSDX:
			try {
				ATKMKJZIDE *kjide = g_sim.GetKMKJZIDE();

				if (!kjide)
					throw MyError("KMK/JZ IDE is not enabled.");

				bool sdx = (id == ID_FILE_SAVEKMKJZSDX);
				if (sdx && !kjide->IsVersion2())
					throw MyError("KMK/JZ IDE V1 does not have SDX flash.");

				const ATROMImage image = sdx ? kATROMImage_KMKJZIDEV2_SDX : kATROMImage_KMKJZIDEV2;
				VDStringW origPath;
				g_sim.GetROMImagePath(image, origPath);
				VDSetLastLoadSaveFileName('rom ', origPath.c_str());

				VDStringW fn(VDGetSaveFileName('rom ', (VDGUIHandle)g_hwnd, L"Save firmware image",
					L"Raw firmware image\0*.rom\0",
					L"rom"));

				if (!fn.empty()) {
					kjide->SaveFirmware(sdx, fn.c_str());
					g_sim.SetROMImagePath(image, fn.c_str());
				}
			} catch(const MyError& e) {
				e.post(g_hwnd, "Altirra Error");
			}
			return true;

		case ID_FILE_SAVESIDESDX:
			try {
				ATSIDEEmulator *side = g_sim.GetSIDE();

				if (!side)
					throw MyError("SIDE is not enabled.");

				const ATROMImage image = kATROMImage_SIDE_SDX;
				VDStringW origPath;
				g_sim.GetROMImagePath(image, origPath);
				VDSetLastLoadSaveFileName('rom ', origPath.c_str());

				VDStringW fn(VDGetSaveFileName('rom ', (VDGUIHandle)g_hwnd, L"Save firmware image",
					L"Raw firmware image\0*.rom\0",
					L"rom"));

				if (!fn.empty()) {
					side->SaveFirmware(fn.c_str());
					g_sim.SetROMImagePath(image, fn.c_str());
				}
			} catch(const MyError& e) {
				e.post(g_hwnd, "Altirra Error");
			}
			return true;

		case ID_FILE_EXIT:
			PostQuitMessage(0);
			return true;

		case ID_CASSETTE_LOAD:
			{
				VDStringW fn(VDGetLoadFileName('cass', (VDGUIHandle)g_hwnd, L"Load cassette tape", L"All supported types\0*.wav;*.cas\0Atari cassette image (*.cas)\0*.cas\0Waveform audio (*.wav)\0*.wav\0All files\0*.*\0", L"wav"));

				if (!fn.empty()) {
					try {
						ATCassetteEmulator& cas = g_sim.GetCassette();
						cas.Load(fn.c_str());
						cas.Play();
					} catch(const MyError& e) {
						e.post(g_hwnd, "Altirra Error");
					}
				}
			}
			return true;

		case ID_CASSETTE_UNLOAD:
			g_sim.GetCassette().Unload();
			return true;

		case ID_CASSETTE_TAPECONTROL:
			ATUIShowTapeControlDialog((VDGUIHandle)g_hwnd, g_sim.GetCassette());
			return true;

		case ID_CASSETTE_SIOPATCH:
			g_sim.SetCassetteSIOPatchEnabled(!g_sim.IsCassetteSIOPatchEnabled());
			return true;

		case ID_CASSETTE_AUTOBOOT:
			g_sim.SetCassetteAutoBootEnabled(!g_sim.IsCassetteAutoBootEnabled());
			return true;

		case ID_CASSETTE_LOADDATAASAUDIO:
			{
				ATCassetteEmulator& cas = g_sim.GetCassette();

				cas.SetLoadDataAsAudioEnable(!cas.IsLoadDataAsAudioEnabled());
			}
			return true;

		case ID_DEBUG_OPENSOURCE:
			{
				VDStringW fn(VDGetLoadFileName('src ', (VDGUIHandle)g_hwnd, L"Load source file", L"All files (*.*)\0*.*\0", NULL));

				if (!fn.empty()) {
					ATOpenSourceWindow(fn.c_str());
				}
			}
			return true;

		case ID_DEBUG_BREAKATEXERUNADDRESS:
			{
				IATDebugger *dbg = ATGetDebugger();

				dbg->SetBreakOnEXERunAddrEnabled(!dbg->IsBreakOnEXERunAddrEnabled());
			}
			return true;

		case ID_FILTERMODE_NEXTMODE:
			OnCommandFilterModeNext();
			return true;

		case ID_FILTERMODE_POINT:
			g_dispFilterMode = kATDisplayFilterMode_Point;
			ResizeDisplay();
			return true;

		case ID_FILTERMODE_BILINEAR:
			g_dispFilterMode = kATDisplayFilterMode_Bilinear;
			ResizeDisplay();
			return true;

		case ID_FILTERMODE_SHARPBILINEAR:
			g_dispFilterMode = kATDisplayFilterMode_SharpBilinear;
			ResizeDisplay();
			return true;

		case ID_FILTERMODE_BICUBIC:
			g_dispFilterMode = kATDisplayFilterMode_Bicubic;
			ResizeDisplay();
			return true;

		case ID_FILTERMODE_ANYSUITABLE:
			g_dispFilterMode = kATDisplayFilterMode_AnySuitable;
			ResizeDisplay();
			return true;

		case ID_FILTERSHARPNESS_SOFTER:
			g_dispFilterSharpness = -2;
			{
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateFilterMode();
			}
			return true;

		case ID_FILTERSHARPNESS_SOFT:
			g_dispFilterSharpness = -1;
			{
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateFilterMode();
			}
			return true;

		case ID_FILTERSHARPNESS_NORMAL:
			g_dispFilterSharpness = 0;
			{
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateFilterMode();
			}
			return true;

		case ID_FILTERSHARPNESS_SHARP:
			g_dispFilterSharpness = +1;
			{
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateFilterMode();
			}
			return true;

		case ID_FILTERSHARPNESS_SHARPER:
			g_dispFilterSharpness = +2;
			{
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateFilterMode();
			}
			return true;

		case ID_STRETCHMODE_FITTOWINDOW:
			g_displayStretchMode = kATDisplayStretchMode_Unconstrained;
			ResizeDisplay();
			return true;

		case ID_STRETCHMODE_PRESERVEASPECTRATIO:
			g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;
			ResizeDisplay();
			return true;

		case ID_STRETCHMODE_SQUAREPIXELS:
			g_displayStretchMode = kATDisplayStretchMode_SquarePixels;
			ResizeDisplay();
			return true;

		case ID_STRETCHMODE_INTEGRALSQUAREPIXELS:
			g_displayStretchMode = kATDisplayStretchMode_Integral;
			ResizeDisplay();
			return true;

		case ID_STRETCHMODE_INTEGRALPRESERVEASPECTRATIO:
			g_displayStretchMode = kATDisplayStretchMode_IntegralPreserveAspectRatio;
			ResizeDisplay();
			return true;

		case ID_OVERSCAN_OSSCREEN:
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanOSScreen);
			ResizeDisplay();
			return true;

		case ID_OVERSCAN_NORMAL:
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanNormal);
			ResizeDisplay();
			return true;

		case ID_OVERSCAN_EXTENDED:
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanExtended);
			ResizeDisplay();
			return true;

		case ID_OVERSCAN_FULL:
			g_sim.GetGTIA().SetOverscanMode(ATGTIAEmulator::kOverscanFull);
			ResizeDisplay();
			return true;

		case ID_OVERSCAN_PALEXTENDED:
			g_sim.GetGTIA().SetOverscanPALExtended(!g_sim.GetGTIA().IsOverscanPALExtended());
			ResizeDisplay();
			return true;


		case ID_VIEW_FULLSCREEN:
			OnCommandToggleFullScreen();
			return true;

		case ID_VIEW_VSYNC:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetVsyncEnabled(!gtia.IsVsyncEnabled());
			}
			return true;

		case ID_VIEW_SHOWFPS:
			g_showFps = !g_showFps;
			g_winCaptionUpdater.SetShowFps(g_showFps);
			return true;

		case ID_VIEW_DISPLAY:
			ATActivateUIPane(kATUIPaneId_Display, true);
			return true;

		case ID_VIEW_PRINTEROUTPUT:
			ATActivateUIPane(kATUIPaneId_PrinterOutput, true);
			return true;

		case ID_VIEW_CONSOLE:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_Console, true);
			return true;

		case ID_VIEW_REGISTERS:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_Registers, true);
			return true;

		case ID_VIEW_DISASSEMBLY:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_Disassembly, true);
			return true;

		case ID_VIEW_CALLSTACK:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_CallStack, true);
			return true;

		case ID_VIEW_HISTORY:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_History, true);
			return true;

		case ID_VIEW_MEMORY:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_MemoryN + 0, true);
			return true;

		case ID_VIEW_MEMORY2:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_MemoryN + 1, true);
			return true;

		case ID_VIEW_MEMORY3:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_MemoryN + 2, true);
			return true;

		case ID_VIEW_MEMORY4:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_MemoryN + 3, true);
			return true;

		case ID_VIEW_WATCH1:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_WatchN + 0, true);
			return true;

		case ID_VIEW_WATCH2:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_WatchN + 1, true);
			return true;

		case ID_VIEW_WATCH3:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_WatchN + 2, true);
			return true;

		case ID_VIEW_WATCH4:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_WatchN + 3, true);
			return true;

		case ID_VIEW_DEBUGDISPLAY:
			if (ATIsDebugConsoleActive())
				ATActivateUIPane(kATUIPaneId_DebugDisplay, true);
			return true;

		case ID_VIEW_COPYFRAMETOCLIPBOARD:
			{
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
			return true;

		case ID_VIEW_SAVEFRAME:
			try {
				const VDPixmap *frame = g_sim.GetGTIA().GetLastFrameBuffer();

				if (frame) {
					const VDStringW fn(VDGetSaveFileName('scrn', (VDGUIHandle)g_hwnd, L"Save Screenshot", L"Portable Network Graphics (*.png)\0*.png\0", L"png"));

					if (!fn.empty())
						ATSaveFrame(g_hwnd, *frame, fn.c_str());
				}
			} catch(const MyError& e) {
				e.post(g_hwnd, "Altirra Error");
			}
			return true;

		case ID_VIEW_ADJUSTWINDOWSIZE:
			if (g_pMainWindow)
				g_pMainWindow->AutoSize();
			return true;

		case ID_VIEW_RESETWINDOWLAYOUT:
			ATLoadDefaultPaneLayout();
			return true;

		case ID_VIEW_COPY:
			{
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->Copy();
			}
			return true;

		case ID_VIEW_PASTE:
			OnCommandPaste();
			return true;

		case ID_ANTICVISUALIZATION_NEXTMODE:
			OnCommandAnticVisualizationNext();
			return true;

		case ID_GTIAVISUALIZATION_NEXTMODE:
			OnCommandGTIAVisualizationNext();
			return true;

		case ID_HARDWARE_800:
			ATUISwitchHardwareMode((VDGUIHandle)g_hwnd, kATHardwareMode_800);
			return true;

		case ID_HARDWARE_800XL:
			ATUISwitchHardwareMode((VDGUIHandle)g_hwnd, kATHardwareMode_800XL);
			return true;

		case ID_HARDWARE_1200XL:
			ATUISwitchHardwareMode((VDGUIHandle)g_hwnd, kATHardwareMode_1200XL);
			return true;

		case ID_HARDWARE_XEGS:
			ATUISwitchHardwareMode((VDGUIHandle)g_hwnd, kATHardwareMode_XEGS);
			return true;

		case ID_HARDWARE_5200:
			ATUISwitchHardwareMode((VDGUIHandle)g_hwnd, kATHardwareMode_5200);
			return true;

		case ID_FIRMWARE_DEFAULT:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_Default);
			return true;

		case ID_FIRMWARE_OSA:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_OSA);
			return true;

		case ID_FIRMWARE_OSB:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_OSB);
			return true;

		case ID_FIRMWARE_800XL:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_XL);
			return true;

		case ID_FIRMWARE_1200XL:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_1200XL);
			return true;

		case ID_FIRMWARE_XEGS:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_XEGS);
			return true;

		case ID_FIRMWARE_OTHER:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_Other);
			return true;

		case ID_FIRMWARE_HLE:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_HLE);
			return true;

		case ID_FIRMWARE_LLE:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_LLE);
			return true;

		case ID_FIRMWARE_5200:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_5200);
			return true;

		case ID_FIRMWARE_5200LLE:
			ATUISwitchKernelMode((VDGUIHandle)g_hwnd, kATKernelMode_5200_LLE);
			return true;

		case ID_FIRMWARE_BASIC:
			g_sim.SetBASICEnabled(!g_sim.IsBASICEnabled());
			return true;

		case ID_FIRMWARE_FASTBOOT:
			g_sim.SetFastBootEnabled(!g_sim.IsFastBootEnabled());
			return true;

		case ID_FIRMWARE_ROMIMAGES:
			ATUIShowDialogROMImages((VDGUIHandle)g_hwnd, g_sim);
			return true;

		case ID_MEMORYSIZE_8K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_8K);
			return true;

		case ID_MEMORYSIZE_16K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_16K);
			return true;

		case ID_MEMORYSIZE_24K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_24K);
			return true;

		case ID_MEMORYSIZE_32K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_32K);
			return true;

		case ID_MEMORYSIZE_40K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_40K);
			return true;

		case ID_MEMORYSIZE_48K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_48K);
			return true;

		case ID_MEMORYSIZE_52K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_52K);
			return true;

		case ID_MEMORYSIZE_64K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_64K);
			return true;

		case ID_MEMORYSIZE_128K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_128K);
			return true;

		case ID_MEMORYSIZE_320K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_320K);
			return true;

		case ID_MEMORYSIZE_320K_COMPY:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_320K_Compy);
			return true;

		case ID_MEMORYSIZE_576K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_576K);
			return true;

		case ID_MEMORYSIZE_576K_COMPY:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_576K_Compy);
			return true;

		case ID_MEMORYSIZE_1088K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_1088K);
			return true;

		case ID_MEMORY_MAPRAM:
			g_sim.SetMapRAMEnabled(!g_sim.IsMapRAMEnabled());
			return true;

		case ID_MEMORY_RANDOMIZE:
			g_sim.SetRandomFillEnabled(!g_sim.IsRandomFillEnabled());
			return true;

		case ID_SYSTEM_WARMRESET:
			g_sim.WarmReset();
			g_sim.Resume();
			return true;

		case ID_SYSTEM_COLDRESET:
			g_sim.ColdReset();
			g_sim.Resume();
			return true;

		case ID_SYSTEM_PRINTER:
			g_sim.SetPrinterEnabled(!g_sim.IsPrinterEnabled());
			return true;

		case ID_SYSTEM_RTIME8:
			g_sim.SetRTime8Enabled(!g_sim.IsRTime8Enabled());
			return true;

		case ID_SYSTEM_SERIALPORTS:
			ATUIShowSerialPortsDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_SYSTEM_NTSC:
		case ID_SYSTEM_PAL:
		case ID_SYSTEM_SECAM:
			// Don't allow switching to PAL or SECAM in 5200 mode!
			if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
				return true;

			switch(id) {
				case ID_SYSTEM_NTSC:
					g_sim.SetVideoStandard(kATVideoStandard_NTSC);
					break;

				case ID_SYSTEM_PAL:
					g_sim.SetVideoStandard(kATVideoStandard_PAL);
					break;

				case ID_SYSTEM_SECAM:
					g_sim.SetVideoStandard(kATVideoStandard_SECAM);
					break;
			}

			ATUIUpdateSpeedTiming();

			{
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->OnSize();
			}
			return true;

		case ID_CONSOLESWITCHES_KEYBOARDPRESENT:
			g_sim.SetKeyboardPresent(!g_sim.IsKeyboardPresent());
			return true;

		case ID_CONSOLESWITCHES_FORCESELFTEST:
			g_sim.SetForcedSelfTest(!g_sim.IsForcedSelfTest());
			return true;

		case ID_VIDEO_FRAMEBLENDING:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetBlendModeEnabled(!gtia.IsBlendModeEnabled());
			}
			return true;

		case ID_VIDEO_INTERLACE:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetInterlaceEnabled(!gtia.IsInterlaceEnabled());
			}
			return true;

		case ID_VIDEO_SCANLINES:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetScanlinesEnabled(!gtia.AreScanlinesEnabled());
			}
			return true;

		case ID_VIDEO_VBXE:
			g_sim.SetVBXEEnabled(!g_sim.GetVBXE());
			g_sim.ColdReset();
			return true;

		case ID_VIDEO_VBXESHAREDMEMORY:
			g_sim.SetVBXESharedMemoryEnabled(!g_sim.IsVBXESharedMemoryEnabled());
			g_sim.ColdReset();
			return true;

		case ID_VIDEO_VBXEALTPAGE:
			g_sim.SetVBXEAltPageEnabled(!g_sim.IsVBXEAltPageEnabled());
			g_sim.ColdReset();
			return true;

		case ID_VIDEO_NOARTIFACTING:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetArtifactingMode(ATGTIAEmulator::kArtifactNone);
			}
			return true;

		case ID_VIDEO_NTSCARTIFACTING:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetArtifactingMode(ATGTIAEmulator::kArtifactNTSC);
			}
			return true;

		case ID_VIDEO_NTSCARTIFACTINGHI:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetArtifactingMode(ATGTIAEmulator::kArtifactNTSCHi);
			}
			return true;

		case ID_VIDEO_PALARTIFACTING:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetArtifactingMode(ATGTIAEmulator::kArtifactPAL);
			}
			return true;

		case ID_VIDEO_PALARTIFACTINGHI:
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();

				gtia.SetArtifactingMode(ATGTIAEmulator::kArtifactPALHi);
			}
			return true;

		case ID_VIDEO_ADJUSTCOLORS:
			ATUIOpenAdjustColorsDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_SYSTEM_PAUSE:
			OnCommandTogglePause();
			return true;

		case ID_SYSTEM_PAUSEWHENINACTIVE:
			g_pauseInactive = !g_pauseInactive;
			return true;

		case ID_SYSTEM_WARPSPEED:
			ATUISetTurbo(!ATUIGetTurbo());
			return true;

		case ID_SYSTEM_CPUOPTIONS:
			ATUIShowCPUOptionsDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_SYSTEM_FPPATCH:
			g_sim.SetFPPatchEnabled(!g_sim.IsFPPatchEnabled());
			return true;

		case ID_SYSTEM_HARDDISK:
			ATUIShowHardDiskDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_SYSTEM_HOSTDEVICE:
			{
				IATHostDeviceEmulator *hd = g_sim.GetHostDevice();

				if (hd)
					ATUIShowHostDeviceDialog((VDGUIHandle)g_hwnd, hd);
			}
			return true;

		case ID_SYSTEM_PCLINK:
			ATUIShowPCLinkDialog((VDGUIHandle)g_hwnd, &g_sim);
			return true;

		case ID_SYSTEM_SPEEDOPTIONS:
			ATUIShowDialogSpeedOptions((VDGUIHandle)g_hwnd);
			return true;

		case ID_VIDEO_STANDARDVIDEO:
			g_enhancedText = 0;
			g_sim.SetVirtualScreenEnabled(false);
			return true;

		case ID_VIDEO_ENHANCEDTEXT:
			g_enhancedText = 1;
			g_sim.SetVirtualScreenEnabled(false);
			ATSetFullscreen(false);
			return true;

		case ID_VIDEO_ENHANCEDTEXTCIO:
			if (g_enhancedText != 2) {
				g_enhancedText = 2;
				g_sim.SetVirtualScreenEnabled(true);
				ATSetFullscreen(false);

				// push a break to attempt to kick out of the OS get byte routine
				g_sim.GetPokey().PushBreak();
			}
			return true;

		case ID_VIDEO_ENHANCEDTEXTFONT:
			OnCommandEnhancedTextFont();
			return true;

		case ID_DISKDRIVE_ENABLED:
			for(int i=0; i<4; ++i) {
				ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
				disk.SetEnabled(!disk.IsEnabled());
			}
			return true;

		case ID_DISKDRIVE_SIOPATCH:
			g_sim.SetDiskSIOPatchEnabled(!g_sim.IsDiskSIOPatchEnabled());
			return true;

		case ID_DISKDRIVE_SIOOVERRIDEDETECTION:
			g_sim.SetDiskSIOOverrideDetectEnabled(!g_sim.IsDiskSIOOverrideDetectEnabled());
			return true;

		case ID_DISKDRIVE_BURSTDISABLED:
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Disabled);
			return true;

		case ID_DISKDRIVE_BURSTSTANDARD:
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Standard);
			return true;

		case ID_DISKDRIVE_BURSTPOLLED:
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Polled);
			return true;

		case ID_DISKDRIVE_ACCURATESECTORTIMING:
			for(int i=0; i<15; ++i) {
				ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
				disk.SetAccurateSectorTimingEnabled(!disk.IsAccurateSectorTimingEnabled());
			}
			return true;

		case ID_DISKDRIVE_DRIVESOUNDS:
			for(int i=0; i<15; ++i) {
				ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
				disk.SetDriveSoundsEnabled(!disk.AreDriveSoundsEnabled());
			}
			return true;

		case ID_DISKDRIVE_SHOWSECTORCOUNTER:
			g_sim.SetDiskSectorCounterEnabled(!g_sim.IsDiskSectorCounterEnabled());
			return true;

		case ID_AUDIO_STEREO:
			g_sim.SetDualPokeysEnabled(!g_sim.IsDualPokeysEnabled());
			return true;

		case ID_AUDIO_AUDIOMONITOR:
			g_sim.SetAudioMonitorEnabled(!g_sim.IsAudioMonitorEnabled());
			return true;

		case ID_AUDIO_MUTE:
			{
				IATAudioOutput *out = g_sim.GetAudioOutput();

				if (out)
					out->SetMute(!out->GetMute());
			}
			return true;

		case ID_AUDIO_OPTIONS:
			ATUIShowAudioOptionsDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_AUDIO_NONLINEARMIXING:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();

				pokey.SetNonlinearMixingEnabled(!pokey.IsNonlinearMixingEnabled());
			}
			return true;

		case ID_AUDIO_CHANNEL1:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();
				pokey.SetChannelEnabled(0, !pokey.IsChannelEnabled(0));
			}
			return true;

		case ID_AUDIO_CHANNEL2:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();
				pokey.SetChannelEnabled(1, !pokey.IsChannelEnabled(1));
			}
			return true;

		case ID_AUDIO_CHANNEL3:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();
				pokey.SetChannelEnabled(2, !pokey.IsChannelEnabled(2));
			}
			return true;

		case ID_AUDIO_CHANNEL4:
			{
				ATPokeyEmulator& pokey = g_sim.GetPokey();
				pokey.SetChannelEnabled(3, !pokey.IsChannelEnabled(3));
			}
			return true;

		case ID_SOUNDBOARD_ENABLED:
			g_sim.SetSoundBoardEnabled(!g_sim.GetSoundBoard());
			g_sim.ColdReset();
			return true;

		case ID_SOUNDBOARD_ADDR_D2C0:
			g_sim.SetSoundBoardMemBase(0xD2C0);

			// The reason why we reset on these is not because the SoundBoard itself needs it,
			// but because we may need to adjust the hook page.
			g_sim.ColdReset();
			return true;

		case ID_SOUNDBOARD_ADDR_D500:
			g_sim.SetSoundBoardMemBase(0xD500);
			g_sim.ColdReset();
			return true;

		case ID_SOUNDBOARD_ADDR_D600:
			g_sim.SetSoundBoardMemBase(0xD600);
			g_sim.ColdReset();
			return true;

		case ID_SLIGHTSID_ENABLED:
			g_sim.SetSlightSIDEnabled(!g_sim.GetSlightSID());
			g_sim.ColdReset();
			return true;

		case ID_COVOX_ENABLED:
			g_sim.SetCovoxEnabled(!g_sim.GetCovox());
			g_sim.ColdReset();
			return true;

		case ID_INPUT_CAPTUREMOUSE:
			if (g_mouseClipped) {
				::ClipCursor(NULL);
				g_mouseClipped = false;

				g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);
			} else if (g_mouseCaptured) {
				::ReleaseCapture();
			} else {
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->CaptureMouse();
			}
			return true;

		case ID_INPUT_AUTOCAPTUREMOUSE:
			g_mouseAutoCapture = !g_mouseAutoCapture;
			return true;

		case ID_INPUT_INPUTMAPPINGS:
			ATUIShowDialogInputMappings(g_hwnd, *g_sim.GetInputManager(), &g_sim.GetJoystickManager());
			SaveInputMaps();
			ATReloadPortMenus();
			return true;

		case ID_INPUT_KEYBOARD:
			if (ATUIShowDialogKeyboardOptions((VDGUIHandle)g_hwnd, g_kbdOpts))
				ATUIInitVirtualKeyMap(g_kbdOpts);
			return true;

		case ID_INPUT_LIGHTPEN:
			ATUIShowDialogLightPen((VDGUIHandle)g_hwnd, g_sim.GetLightPenPort());
			return true;

		case ID_CHEAT_DISABLEPMCOLLISIONS:
			g_sim.GetGTIA().SetPMCollisionsEnabled(!g_sim.GetGTIA().ArePMCollisionsEnabled());
			return true;

		case ID_CHEAT_DISABLEPFCOLLISIONS:
			g_sim.GetGTIA().SetPFCollisionsEnabled(!g_sim.GetGTIA().ArePFCollisionsEnabled());
			return true;

		case ID_CHEAT_CHEATER:
			g_sim.SetCheatEngineEnabled(true);
			ATUIShowDialogCheater((VDGUIHandle)g_hwnd, g_sim.GetCheatEngine());
			return true;

		case ID_DEBUG_AUTORELOADROMS:
			g_sim.SetROMAutoReloadEnabled(!g_sim.IsROMAutoReloadEnabled());
			return true;

		case ID_DEBUG_AUTOLOADKERNELSYMBOLS:
			g_sim.SetAutoLoadKernelSymbolsEnabled(!g_sim.IsAutoLoadKernelSymbolsEnabled());
			return true;

		case ID_DEBUG_CHANGEFONT:
			ATUIShowDialogDebugFont((VDGUIHandle)g_hwnd);
			return true;

		case ID_DEBUG_ENABLEDEBUGGER:
			if (ATIsDebugConsoleActive()) {
				if (!g_sim.IsRunning())
					ATGetDebugger()->Detach();

				ATCloseConsole();
			} else
				ATOpenConsole();
			return true;

		case ID_DEBUG_RUN:
			ATGetDebugger()->Run(kATDebugSrcMode_Same);
			return true;

		case ID_DEBUG_BREAK:
			ATOpenConsole();
			ATGetDebugger()->Break();
			return true;

		case ID_DEBUG_STEPINTO:
			ATGetDebugger()->StepInto(kATDebugSrcMode_Same);
			return true;

		case ID_DEBUG_STEPOUT:
			ATGetDebugger()->StepOut(kATDebugSrcMode_Same);
			return true;

		case ID_DEBUG_STEPOVER:
			ATGetDebugger()->StepOver(kATDebugSrcMode_Same);
			return true;

		case ID_PROFILE_PROFILEVIEW:
			ATActivateUIPane(kATUIPaneId_Profiler, true);
			return true;

		case ID_DEBUG_VERIFIER:
			ATUIShowDialogVerifier((VDGUIHandle)g_hwnd, g_sim);
			return true;

		case ID_RECORD_STOPRECORDING:
			CheckRecordingExceptions();
			StopRecording();
			return true;

		case ID_RECORD_RECORDRAWAUDIO:
			if (!g_pAudioWriter && !g_pVideoWriter) {
				VDStringW s(VDGetSaveFileName('raud', (VDGUIHandle)g_hwnd, L"Record raw audio", L"Raw 32-bit float data\0*.pcm\0", L"pcm"));

				if (!s.empty()) {
					try {
						g_pAudioWriter = new ATAudioWriter(s.c_str());
						g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter);
					} catch(const MyError& e) {
						StopRecording();
						e.post(g_hwnd, "Altirra Error");
					}
				}
			}
			return true;

		case ID_RECORD_RECORDVIDEO:
			if (!g_pAudioWriter && !g_pVideoWriter) {
				VDStringW s(VDGetSaveFileName('rvid', (VDGUIHandle)g_hwnd, L"Record raw video", L"Audio/visual interleaved (*.avi)\0*.avi\0", L"avi"));

				if (!s.empty()) {
					const bool hz50 = g_sim.GetVideoStandard() != kATVideoStandard_NTSC;
					ATUIDialogVideoRecording dlg(hz50);

					if (dlg.ShowDialog((VDGUIHandle)g_hwnd)) {
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

							switch(dlg.GetFrameRate()) {
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

							g_pVideoWriter->Init(s.c_str(), dlg.GetEncoding(), w, h, frameRate, rgb32 ? NULL : palette, samplingRate, g_sim.IsDualPokeysEnabled(), hz50 ? 1773447.0f : 1789772.5f, dlg.GetHalfRate(), g_sim.GetUIRenderer());

							g_sim.GetAudioOutput()->SetAudioTap(g_pVideoWriter);
							gtia.SetVideoTap(g_pVideoWriter);
						} catch(const MyError& e) {
							StopRecording();
							e.post(g_hwnd, "Altirra Error");
						}
					}
				}
			}
			return true;

		case ID_TOOLS_DISKEXPLORER:
			ATUIShowDialogDiskExplorer((VDGUIHandle)g_hwnd);
			return true;

		case ID_TOOLS_OPTIONS:
			ATUIShowDialogOptions((VDGUIHandle)g_hwnd);
			return true;

		case ID_HELP_CONTENTS:
			ATShowHelp(g_hwnd, NULL);
			return true;

		case ID_HELP_ABOUT:
			ATUIShowDialogAbout((VDGUIHandle)g_hwnd);
			return true;

		case ID_HELP_CHANGELOG:
			ATShowChangeLog((VDGUIHandle)g_hwnd);
			return true;

		default:
			if (ATUIHandlePortMenuCommand(id))
				return true;

			if ((uint32)(id - ID_FILE_MRU_BASE) < 100) {
				int index = id - ID_FILE_MRU_BASE;

				if (index == 99) {
					ATClearMRUList();
					ATUpdateMRUListMenu(g_hMenuMRU, ID_FILE_MRU_BASE, ID_FILE_MRU_BASE + 99);
				} else {
					VDStringW s(ATGetMRUListItem(index));

					if (!s.empty())
						DoBootWithConfirm(s.c_str(), false, false, 0);
				}
			}
			break;
	}
	return false;
}

void OnInitMenu(HMENU hmenu) {
	const bool cartAttached = g_sim.IsCartridgeAttached(0);
	const bool is5200 = g_sim.GetHardwareMode() == kATHardwareMode_5200;

	VDEnableMenuItemByCommandW32(hmenu, ID_CASSETTE_UNLOAD, g_sim.GetCassette().IsLoaded());
	VDEnableMenuItemByCommandW32(hmenu, ID_FILE_DETACHCARTRIDGE, cartAttached);
	VDEnableMenuItemByCommandW32(hmenu, ID_FILE_SAVECARTRIDGE, cartAttached);

	ATKMKJZIDE *kjide = g_sim.GetKMKJZIDE();
	VDEnableMenuItemByCommandW32(hmenu, ID_FILE_SAVEKMKJZMAIN, kjide != NULL);
	VDEnableMenuItemByCommandW32(hmenu, ID_FILE_SAVEKMKJZSDX, kjide != NULL && kjide->IsVersion2());

	const bool cart2Attached = g_sim.IsCartridgeAttached(1);
	VDEnableMenuItemByCommandW32(hmenu, ID_CART2_DETACH, cart2Attached);

	const ATVideoStandard videoStd = g_sim.GetVideoStandard();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_SYSTEM_NTSC, videoStd == kATVideoStandard_NTSC);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_SYSTEM_PAL, videoStd == kATVideoStandard_PAL);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_SYSTEM_SECAM, videoStd == kATVideoStandard_SECAM);
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_PRINTER, g_sim.IsPrinterEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_RTIME8, g_sim.IsRTime8Enabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_SERIALPORTS, g_sim.IsRS232Enabled());

	const ATHardwareMode hardwareMode = g_sim.GetHardwareMode();
	const bool xlhardware = hardwareMode == kATHardwareMode_800XL || hardwareMode == kATHardwareMode_1200XL || hardwareMode == kATHardwareMode_XEGS;
	VDEnableMenuItemByCommandW32(hmenu, ID_CONSOLESWITCHES_KEYBOARDPRESENT, hardwareMode == kATHardwareMode_XEGS);
	VDCheckMenuItemByCommandW32(hmenu, ID_CONSOLESWITCHES_KEYBOARDPRESENT, hardwareMode == kATHardwareMode_XEGS && g_sim.IsKeyboardPresent());

	VDEnableMenuItemByCommandW32(hmenu, ID_CONSOLESWITCHES_FORCESELFTEST, xlhardware);
	VDCheckMenuItemByCommandW32(hmenu, ID_CONSOLESWITCHES_FORCESELFTEST, xlhardware && g_sim.IsForcedSelfTest());

	VDCheckMenuItemByCommandW32(hmenu, ID_VIEW_SHOWFPS, g_showFps);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	ATGTIAEmulator::ArtifactMode artmode = gtia.GetArtifactingMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_NOARTIFACTING, artmode == ATGTIAEmulator::kArtifactNone);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_NTSCARTIFACTING, artmode == ATGTIAEmulator::kArtifactNTSC);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_NTSCARTIFACTINGHI, artmode == ATGTIAEmulator::kArtifactNTSCHi);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_PALARTIFACTING, artmode == ATGTIAEmulator::kArtifactPAL);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_PALARTIFACTINGHI, artmode == ATGTIAEmulator::kArtifactPALHi);
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_FRAMEBLENDING, gtia.IsBlendModeEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_INTERLACE, gtia.IsInterlaceEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_SCANLINES, gtia.AreScanlinesEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_VBXE, g_sim.GetVBXE() != NULL);
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_VBXESHAREDMEMORY, g_sim.IsVBXESharedMemoryEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_VBXEALTPAGE, g_sim.IsVBXEAltPageEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_WARPSPEED, ATUIGetTurbo());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_PAUSEWHENINACTIVE, g_pauseInactive);
	VDCheckMenuItemByCommandW32(hmenu, ID_VIEW_VSYNC, gtia.IsVsyncEnabled());

	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_POINT, g_dispFilterMode == kATDisplayFilterMode_Point);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_BILINEAR, g_dispFilterMode == kATDisplayFilterMode_Bilinear);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_SHARPBILINEAR, g_dispFilterMode == kATDisplayFilterMode_SharpBilinear);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_BICUBIC, g_dispFilterMode == kATDisplayFilterMode_Bicubic);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_ANYSUITABLE, g_dispFilterMode == kATDisplayFilterMode_AnySuitable);

	bool enableFilterSharpness = (g_dispFilterMode == kATDisplayFilterMode_SharpBilinear);
	VDEnableMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_SOFTER, enableFilterSharpness);
	VDEnableMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_SOFT, enableFilterSharpness);
	VDEnableMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_NORMAL, enableFilterSharpness);
	VDEnableMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_SHARP, enableFilterSharpness);
	VDEnableMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_SHARPER, enableFilterSharpness);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_SOFTER, g_dispFilterSharpness <= -2);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_SOFT, g_dispFilterSharpness == -1);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_NORMAL, g_dispFilterSharpness == 0);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_SHARP, g_dispFilterSharpness == 1);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERSHARPNESS_SHARPER, g_dispFilterSharpness >= 2);

	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_FITTOWINDOW, g_displayStretchMode == kATDisplayStretchMode_Unconstrained);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_PRESERVEASPECTRATIO, g_displayStretchMode == kATDisplayStretchMode_PreserveAspectRatio);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_SQUAREPIXELS, g_displayStretchMode == kATDisplayStretchMode_SquarePixels);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_INTEGRALSQUAREPIXELS, g_displayStretchMode == kATDisplayStretchMode_Integral);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_INTEGRALPRESERVEASPECTRATIO, g_displayStretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio);

	ATGTIAEmulator::OverscanMode ovmode = gtia.GetOverscanMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_OSSCREEN, ovmode == ATGTIAEmulator::kOverscanOSScreen);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_NORMAL, ovmode == ATGTIAEmulator::kOverscanNormal);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_EXTENDED, ovmode == ATGTIAEmulator::kOverscanExtended);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_FULL, ovmode == ATGTIAEmulator::kOverscanFull);

	VDCheckMenuItemByCommandW32(hmenu, ID_OVERSCAN_PALEXTENDED, g_sim.GetGTIA().IsOverscanPALExtended());

	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_800, hardwareMode == kATHardwareMode_800);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_800XL, hardwareMode == kATHardwareMode_800XL);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_1200XL, hardwareMode == kATHardwareMode_1200XL);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_XEGS, hardwareMode == kATHardwareMode_XEGS);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_5200, hardwareMode == kATHardwareMode_5200);

	const ATKernelMode kernelMode = g_sim.GetKernelMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_DEFAULT, kernelMode == kATKernelMode_Default);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_OSA, kernelMode == kATKernelMode_OSA);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_OSB, kernelMode == kATKernelMode_OSB);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_800XL, kernelMode == kATKernelMode_XL);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_1200XL, kernelMode == kATKernelMode_1200XL);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_XEGS, kernelMode == kATKernelMode_XEGS);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_OTHER, kernelMode == kATKernelMode_Other);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_LLE, kernelMode == kATKernelMode_LLE);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_HLE, kernelMode == kATKernelMode_HLE);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_5200, kernelMode == kATKernelMode_5200);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_5200LLE, kernelMode == kATKernelMode_5200_LLE);

	// BASIC doesn't exist on 5200, 800, or 1200XL.
	const bool supportsBasic = (hardwareMode == kATHardwareMode_800XL || hardwareMode == kATHardwareMode_XEGS);
	VDEnableMenuItemByCommandW32(hmenu, ID_FIRMWARE_BASIC, supportsBasic);
	VDCheckMenuItemByCommandW32(hmenu, ID_FIRMWARE_BASIC, supportsBasic && g_sim.IsBASICEnabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_FIRMWARE_FASTBOOT, g_sim.IsFastBootEnabled());

	const ATMemoryMode memMode = g_sim.GetMemoryMode();
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_8K, !is5200 && !xlhardware);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_16K, hardwareMode != kATHardwareMode_1200XL);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_24K, !is5200 && !xlhardware);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_32K, !is5200 && !xlhardware);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_40K, !is5200 && !xlhardware);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_48K, !is5200 && !xlhardware);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_52K, !is5200 && !xlhardware);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_64K, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_128K, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_320K, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_320K_COMPY, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_576K, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_576K_COMPY, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_1088K, !is5200);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_8K, memMode == kATMemoryMode_8K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_16K, memMode == kATMemoryMode_16K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_24K, memMode == kATMemoryMode_24K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_32K, memMode == kATMemoryMode_32K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_40K, memMode == kATMemoryMode_40K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_48K, memMode == kATMemoryMode_48K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_52K, memMode == kATMemoryMode_52K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_64K, memMode == kATMemoryMode_64K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_128K, memMode == kATMemoryMode_128K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_320K, memMode == kATMemoryMode_320K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_320K_COMPY, memMode == kATMemoryMode_320K_Compy);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_576K, memMode == kATMemoryMode_576K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_576K_COMPY, memMode == kATMemoryMode_576K_Compy);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_1088K, memMode == kATMemoryMode_1088K);

	VDCheckMenuItemByCommandW32(hmenu, ID_MEMORY_MAPRAM, g_sim.IsMapRAMEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_MEMORY_RANDOMIZE, g_sim.IsRandomFillEnabled());

	// PAL 5200 is bogus
	VDEnableMenuItemByCommandW32(hmenu, ID_SYSTEM_PAL, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_SYSTEM_SECAM, !is5200);

	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_FPPATCH, g_sim.IsFPPatchEnabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_CASSETTE_SIOPATCH, g_sim.IsCassetteSIOPatchEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_CASSETTE_AUTOBOOT, g_sim.IsCassetteAutoBootEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_CASSETTE_LOADDATAASAUDIO, g_sim.GetCassette().IsLoadDataAsAudioEnabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_DEBUG_AUTORELOADROMS, g_sim.IsROMAutoReloadEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DEBUG_AUTOLOADKERNELSYMBOLS, g_sim.IsAutoLoadKernelSymbolsEnabled());

	bool debuggerEnabled = ATIsDebugConsoleActive();
	VDCheckMenuItemByCommandW32(hmenu, ID_DEBUG_ENABLEDEBUGGER, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_CALLSTACK, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_CONSOLE, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_DISASSEMBLY, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_HISTORY, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_REGISTERS, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_MEMORY, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_MEMORY2, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_MEMORY3, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_MEMORY4, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_WATCH1, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_WATCH2, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_WATCH3, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_WATCH4, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_DEBUGDISPLAY, debuggerEnabled);
	VDEnableMenuItemByCommandW32(hmenu, ID_PROFILE_PROFILEVIEW, debuggerEnabled);

	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_FULLSCREEN, g_enhancedText == 0);

	bool frameAvailable = gtia.GetLastFrameBuffer() != NULL;
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_COPYFRAMETOCLIPBOARD, frameAvailable);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_SAVEFRAME, frameAvailable);

	ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_COPY, pane && pane->IsTextSelected());
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_PASTE, !!IsClipboardFormatAvailable(CF_TEXT));

	ATInputManager *const im = g_sim.GetInputManager();
	VDEnableMenuItemByCommandW32(hmenu, ID_INPUT_CAPTUREMOUSE, im->IsMouseMapped());
	VDCheckMenuItemByCommandW32(hmenu, ID_INPUT_CAPTUREMOUSE, g_mouseCaptured || g_mouseClipped);
	VDEnableMenuItemByCommandW32(hmenu, ID_INPUT_AUTOCAPTUREMOUSE, im->IsMouseMapped());
	VDCheckMenuItemByCommandW32(hmenu, ID_INPUT_AUTOCAPTUREMOUSE, g_mouseAutoCapture);

	VDCheckMenuItemByCommandW32(hmenu, ID_CHEAT_DISABLEPMCOLLISIONS, !gtia.ArePMCollisionsEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_CHEAT_DISABLEPFCOLLISIONS, !gtia.ArePFCollisionsEnabled());

	const bool running = g_sim.IsRunning();
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_RUN, !running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_BREAK, running || ATGetDebugger()->AreCommandsQueued());
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_STEPINTO, !running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_STEPOUT, !running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_STEPOVER, !running);

	VDCheckMenuItemByCommandW32(hmenu, ID_DEBUG_VERIFIER, g_sim.IsVerifierEnabled());

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_ENABLED, disk.IsEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_SIOPATCH, g_sim.IsDiskSIOPatchEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_SIOOVERRIDEDETECTION, g_sim.IsDiskSIOOverrideDetectEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_ACCURATESECTORTIMING, disk.IsAccurateSectorTimingEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_DRIVESOUNDS, disk.AreDriveSoundsEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_SHOWSECTORCOUNTER, g_sim.IsDiskSectorCounterEnabled());

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	ATPokeyEmulator::SerialBurstMode burstMode = pokey.GetSerialBurstMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_DISKDRIVE_BURSTDISABLED, burstMode == ATPokeyEmulator::kSerialBurstMode_Disabled);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_DISKDRIVE_BURSTSTANDARD, burstMode == ATPokeyEmulator::kSerialBurstMode_Standard);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_DISKDRIVE_BURSTPOLLED, burstMode == ATPokeyEmulator::kSerialBurstMode_Polled);

	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_STEREO, g_sim.IsDualPokeysEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_AUDIOMONITOR, g_sim.IsAudioMonitorEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_NONLINEARMIXING, pokey.IsNonlinearMixingEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_MUTE, g_sim.GetAudioOutput()->GetMute());

	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL1, pokey.IsChannelEnabled(0));
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL2, pokey.IsChannelEnabled(1));
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL3, pokey.IsChannelEnabled(2));
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL4, pokey.IsChannelEnabled(3));

	VDCheckMenuItemByCommandW32(hmenu, ID_SOUNDBOARD_ENABLED, g_sim.GetSoundBoard() != NULL);
	VDCheckMenuItemByCommandW32(hmenu, ID_SLIGHTSID_ENABLED, g_sim.GetSlightSID() != NULL);
	VDCheckMenuItemByCommandW32(hmenu, ID_COVOX_ENABLED, g_sim.GetCovox() != NULL);

	uint32 sbaddr = g_sim.GetSoundBoardMemBase();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_SOUNDBOARD_ADDR_D2C0, sbaddr == 0xD2C0);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_SOUNDBOARD_ADDR_D500, sbaddr == 0xD500);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_SOUNDBOARD_ADDR_D600, sbaddr == 0xD600);

	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_STANDARDVIDEO, g_enhancedText == 0);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_ENHANCEDTEXT, g_enhancedText == 1);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_ENHANCEDTEXTCIO, g_enhancedText == 2);

	bool recording = g_pAudioWriter != NULL || g_pVideoWriter != NULL;
	VDEnableMenuItemByCommandW32(hmenu, ID_RECORD_STOPRECORDING, recording);
	VDEnableMenuItemByCommandW32(hmenu, ID_RECORD_RECORDRAWAUDIO, !recording);
	VDCheckMenuItemByCommandW32(hmenu, ID_RECORD_RECORDRAWAUDIO, g_pAudioWriter != NULL);
	VDEnableMenuItemByCommandW32(hmenu, ID_RECORD_RECORDVIDEO, !recording);
	VDCheckMenuItemByCommandW32(hmenu, ID_RECORD_RECORDVIDEO, g_pVideoWriter != NULL);

	IATDebugger *dbg = ATGetDebugger();
	VDCheckMenuItemByCommandW32(hmenu, ID_DEBUG_BREAKATEXERUNADDRESS, dbg->IsBreakOnEXERunAddrEnabled());

	ATUpdatePortMenus();
}

void OnActivateApp(HWND hwnd, WPARAM wParam) {
	g_winActive = (wParam != 0);

	if (!wParam) {
		if (g_mouseClipped) {
			ClipCursor(NULL);
			g_mouseClipped = false;
			g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);
		}

		ATSetFullscreen(false);
	}
}

int ATGetInputCodeForRawKey(WPARAM wParam, LPARAM lParam) {
	const bool ext = (lParam & (1 << 24)) != 0;
	int code = LOWORD(wParam);

	switch(code) {
		case VK_RETURN:
			if (ext)
				code = kATInputCode_KeyNumpadEnter;
			break;

		case VK_SHIFT:
			// Windows doesn't set the ext bit for RShift, so we have to use the scan
			// code instead.
			if (MapVirtualKey((UINT)(lParam >> 16) & 0xff, 3) == VK_RSHIFT)
				code = kATInputCode_KeyRShift;
			else
				code = kATInputCode_KeyLShift;
			break;

		case VK_CONTROL:
			code = ext ? kATInputCode_KeyRControl : kATInputCode_KeyLControl;
			break;
	}

	return code;
}

bool OnKeyDown(HWND hwnd, WPARAM wParam, LPARAM lParam, bool enableKeyInput) {
	ATInputManager *im = g_sim.GetInputManager();

	const int key = LOWORD(wParam);
	const bool alt = (GetKeyState(VK_MENU) < 0);

	if (!alt) {
		int inputCode = ATGetInputCodeForRawKey(wParam, lParam);

		if (im->IsInputMapped(0, inputCode)) {
			im->OnButtonDown(0, inputCode);
			return true;
		}
	}

	const bool isRepeat = (lParam & (1<<30)) != 0;
	const bool shift = (GetKeyState(VK_SHIFT) < 0);
	const bool ctrl = (GetKeyState(VK_CONTROL) < 0);
	const uint8 ctrlmod = (ctrl ? 0x80 : 0x00);
	const uint8 shiftmod = (shift ? 0x40 : 0x00);

	const uint8 modifier = ctrlmod + shiftmod;
	if (key == VK_CAPITAL) {
		// drop injected CAPS LOCK keys
		if (!(lParam & 0x00ff0000))
			return true;

		if (!enableKeyInput)
			return false;

		ProcessVirtKey(key, 0x3C + modifier, false);
		return true;
	}

	uint8 scanCode;
	if (ATUIGetScanCodeForVirtualKey(key, alt, ctrl, shift, scanCode)) {
		if (!enableKeyInput)
			return false;

		ProcessVirtKey(key, scanCode, isRepeat);
		return true;
	}

	switch(key) {
		case VK_SHIFT:
			g_sim.GetPokey().SetShiftKeyState(true);
			break;

		case VK_F1:
			if (shift) {
				// used to be toggle joystick
			} else if (ctrl) {
				OnCommandFilterModeNext();
			} else if (alt) {
				OnCommand(ID_INPUT_SWAPPORTASSIGNMENTS);
			} else {
				ATUISetTurboPulse(true);
			}
			return true;

		case VK_F2:
			if (alt)
				return false;

			g_sim.GetGTIA().SetConsoleSwitch(0x01, true);
			return true;

		case VK_F3:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x02, true);
				return true;
			}
			break;

		case VK_F4:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x04, true);
				return true;
			}

			// Note: Particularly important that we don't eat Alt+F4 here.
			break;

		case VK_F5:
			if (!alt && !ctrl) {
				if (shift) {
					g_sim.ColdReset();
					g_sim.Resume();

					if (!g_kbdOpts.mbAllowShiftOnColdReset)
						g_sim.GetPokey().SetShiftKeyState(false);
					return true;
				} else {
					g_sim.WarmReset();
					g_sim.Resume();
					return true;
				}
			}
			break;

		case VK_F6:
			if (!alt) {
				ProcessVirtKey(key, 0x11 | modifier, isRepeat);
				return true;
			}
			break;

		case VK_F7:
			if (ctrl) {
				g_sim.SetVideoStandard(g_sim.GetVideoStandard() != kATVideoStandard_PAL ? kATVideoStandard_PAL : kATVideoStandard_NTSC);
				ATUIUpdateSpeedTiming();
				return true;
			} else if (!shift && !alt) {
				g_sim.GetPokey().PushBreak();
				return true;
			}
			break;

		case VK_F8:
			if (shift) {
				OnCommandAnticVisualizationNext();
			} else if (ctrl) {
				OnCommandGTIAVisualizationNext();
			} else {
				ATOpenConsole();

				if (g_sim.IsRunning())
					ATGetDebugger()->Break();
				else
					ATGetDebugger()->Run(kATDebugSrcMode_Same);
			}
			return true;

		case VK_F9:
			if (!alt && !shift && !ctrl) {
				OnCommandTogglePause();
				return true;
			}
			break;

		case VK_F12:
			if (!alt && !shift && !ctrl) {
				OnCommand(ID_INPUT_CAPTUREMOUSE);
				return true;
			}
			break;

		case VK_CANCEL:
		case VK_PAUSE:
			if (ctrl) {
				ATOpenConsole();
				ATGetDebugger()->Break();
			} else {
				g_sim.GetPokey().PushBreak();
			}
			return true;

		case VK_RETURN:
			if (alt) {
				OnCommandToggleFullScreen();
				return true;
			}
			break;

		case VK_BACK:
			if (alt) {
				ATUISetSlowMotion(!ATUIGetSlowMotion());
				return true;
			}
			break;
	}

	g_lastVkeyPressed = key;
	return false;
}

bool OnKeyUp(HWND hwnd, WPARAM wParam, LPARAM lParam, bool enableKeyInput) {
	ATInputManager *im = g_sim.GetInputManager();

	const int key = LOWORD(wParam);
	const bool alt = (GetKeyState(VK_MENU) < 0);

	if (!alt) {
		int inputCode = ATGetInputCodeForRawKey(wParam, lParam);

		if (im->IsInputMapped(0, inputCode)) {
			im->OnButtonUp(0, inputCode);
			return true;
		}
	}

	if (key == VK_CAPITAL) {
		if (GetKeyState(VK_CAPITAL) & 1) {
			// force caps lock back off
			keybd_event(VK_CAPITAL, 0, 0, 0);
			keybd_event(VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0);
		}

		if (!enableKeyInput)
			return false;
	}

	if (ProcessRawKeyUp(key))
		return true;

	switch(key) {
		case VK_CAPITAL:
			return true;

		case VK_SHIFT:
			g_sim.GetPokey().SetShiftKeyState(false);
			break;

		case VK_F2:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x01, false);
				return true;
			}
			break;

		case VK_F3:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x02, false);
				return true;
			}
			break;

		case VK_F4:
			if (!alt) {
				g_sim.GetGTIA().SetConsoleSwitch(0x04, false);
				return true;
			}
			break;

		case VK_F1:
			ATUISetTurboPulse(false);
			return true;
	}

	return false;
}

void ProcessKey(char c, bool repeat, bool allowQueue, bool useCooldown) {
	uint8 ch;

	if (!ATUIGetScanCodeForCharacter(c, ch))
		return;

	g_sim.GetPokey().PushKey(ch, repeat, allowQueue, !allowQueue, useCooldown);
}

void ProcessVirtKey(int vkey, uint8 keycode, bool repeat) {
	g_lastVkeySent = vkey;

	if (g_kbdOpts.mbRawKeys) {
		if (!repeat)
			g_sim.GetPokey().PushRawKey(keycode);
	} else
		g_sim.GetPokey().PushKey(keycode, repeat);
}

bool ProcessRawKeyUp(int vkey) {
	if (g_lastVkeySent == vkey) {
		g_sim.GetPokey().ReleaseRawKey();
		return true;
	}

	return false;
}

void OnChar(HWND hwnd, WPARAM wParam, LPARAM lParam) {
	int code = LOWORD(wParam);
	if (code <= 0 || code > 127)
		return;

	const bool repeat = (lParam & 0x40000000) != 0;

	if (g_kbdOpts.mbRawKeys) {
		uint8 ch;

		if (repeat || !ATUIGetScanCodeForCharacter(code, ch))
			return;

		g_lastVkeySent = g_lastVkeyPressed;

		g_sim.GetPokey().PushRawKey(ch);
		return;
	}

	ProcessKey((char)code, repeat, false, true);
}

///////////////////////////////////////////////////////////////////////////

class ATDisplayPane : public ATDisplayBasePane {
public:
	ATDisplayPane();
	~ATDisplayPane();

	void CaptureMouse();
	void ResetDisplay();

	bool IsTextSelected() const { return !mDragPreviewSpans.empty(); }
	void Copy();
	void Paste();

	void OnSize();
	void UpdateFilterMode();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void WarpCapturedMouse();
	void UpdateTextDisplay(bool enabled);
	void UpdateTextModeFont();
	void UpdateMousePosition(int x, int y);
	bool MapPixelToBeamPosition(int x, int y, float& hcyc, float& vcyc) const;
	bool MapPixelToBeamPosition(int x, int y, int& xc, int& yc) const;
	void UpdateDragPreview(int x, int y);
	void ClearDragPreview();
	int GetModeLineYPos(int yc, bool checkValidCopyText) const;

	HWND	mhwndDisplay;
	HMENU	mhmenuContext;
	IVDVideoDisplay *mpDisplay;
	HCURSOR	mhcurTarget;
	int		mLastHoverX;
	int		mLastHoverY;
	int		mLastTrackMouseX;
	int		mLastTrackMouseY;
	int		mMouseCursorLock;
	int		mWidth;
	int		mHeight;

	bool	mbDragActive;
	int		mDragAnchorX;
	int		mDragAnchorY;

	vdrect32	mDisplayRect;

	struct TextSpan {
		int mX;
		int mY;
		int mWidth;
		int mHeight;
		int mCharX;
		int mCharWidth;
	};

	typedef vdfastvector<TextSpan> TextSpans;
	TextSpans mDragPreviewSpans;

	bool	mbTextModeEnabled;

	vdautoptr<IATUIEnhancedTextEngine> mpEnhTextEngine;
};

ATDisplayPane::ATDisplayPane()
	: mhwndDisplay(NULL)
	, mhmenuContext(LoadMenu(NULL, MAKEINTRESOURCE(IDR_DISPLAY_CONTEXT_MENU)))
	, mpDisplay(NULL)
	, mhcurTarget(NULL)
	, mLastHoverX(INT_MIN)
	, mLastHoverY(INT_MIN)
	, mLastTrackMouseX(0)
	, mLastTrackMouseY(0)
	, mMouseCursorLock(0)
	, mbDragActive(false)
	, mDragAnchorX(0)
	, mDragAnchorY(0)
	, mDisplayRect(0,0,0,0)
	, mbTextModeEnabled(false)
	, mWidth(0)
	, mHeight(0)
{
	mPreferredDockCode = kATContainerDockCenter;
}

ATDisplayPane::~ATDisplayPane() {
	if (mhmenuContext)
		DestroyMenu(mhmenuContext);
}

LRESULT ATDisplayPane::WndProc(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch(msg) {
		case MYWM_PRESYSKEYDOWN:
			// Right-Alt kills capture.
			if (wParam == VK_MENU && (lParam & (1 << 24))) {
				if (::GetCapture() == mhwnd) {
					::ReleaseCapture();
					return true;
				}

				if (g_mouseClipped) {
					g_mouseClipped = false;
					g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);

					::ClipCursor(NULL);
					return true;
				}
			}
			// fall through
		case MYWM_PREKEYDOWN:
			if (OnKeyDown(mhwnd, wParam, lParam, !mpEnhTextEngine || mpEnhTextEngine->IsRawInputEnabled()) || (mpEnhTextEngine && mpEnhTextEngine->OnKeyDown(wParam, lParam))) {
				ClearDragPreview();
				return true;
			}
			return false;

		case MYWM_PRESYSKEYUP:
		case MYWM_PREKEYUP:
			if (OnKeyUp(mhwnd, wParam, lParam, !mpEnhTextEngine || mpEnhTextEngine->IsRawInputEnabled()) || (mpEnhTextEngine && mpEnhTextEngine->OnKeyUp(wParam, lParam)))
				return true;
			return false;

		case WM_SIZE:
			mWidth = LOWORD(lParam);
			mHeight = HIWORD(lParam);
			break;

		case WM_CHAR:
			ClearDragPreview();

			if (mpEnhTextEngine && !mpEnhTextEngine->IsRawInputEnabled()) {
				if (mpEnhTextEngine)
					mpEnhTextEngine->OnChar((int)wParam);
			} else {
				OnChar(mhwnd, wParam, lParam);
			}
			return 0;

		case WM_PARENTNOTIFY:
			switch(LOWORD(wParam)) {
			case WM_LBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_RBUTTONDOWN:
				SetFocus(mhwnd);
				break;
			}
			break;

		case WM_LBUTTONUP:
			if (mbDragActive) {
				mbDragActive = false;

				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);
				UpdateDragPreview(x, y);
			} else {
				ATInputManager *im = g_sim.GetInputManager();
				if (im->IsMouseAbsoluteMode() || ::GetCapture() == mhwnd) {

					if (im->IsMouseMapped())
						im->OnButtonUp(0, kATInputCode_MouseLMB);
				}
			}
			break;

		case WM_MBUTTONUP:
			{
				ATInputManager *im = g_sim.GetInputManager();
				if (im->IsMouseAbsoluteMode() || ::GetCapture() == mhwnd) {

					if (im->IsMouseMapped())
						im->OnButtonUp(0, kATInputCode_MouseMMB);
				}
			}
			break;

		case WM_RBUTTONUP:
			{
				ATInputManager *im = g_sim.GetInputManager();
				if (im->IsMouseAbsoluteMode() || ::GetCapture() == mhwnd) {

					if (im->IsMouseMapped()) {
						im->OnButtonUp(0, kATInputCode_MouseRMB);

						// eat the message to prevent WM_CONTEXTMENU
						return 0;
					}
				}
			}
			break;

		case WM_XBUTTONUP:
			if (::GetCapture() == mhwnd) {
				ATInputManager *im = g_sim.GetInputManager();

				if (im->IsMouseMapped()) {
					if (HIWORD(wParam) == XBUTTON1)
						im->OnButtonUp(0, kATInputCode_MouseX1B);
					else if (HIWORD(wParam) == XBUTTON2)
						im->OnButtonUp(0, kATInputCode_MouseX2B);
				}
			}
			break;

		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONDBLCLK:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_XBUTTONDOWN:
		case WM_XBUTTONDBLCLK:
			::SetFocus(mhwnd);

			{
				ATInputManager *im = g_sim.GetInputManager();

				if (im->IsMouseMapped() && g_sim.IsRunning()) {
					bool absMap = im->IsMouseAbsoluteMode();

					if (absMap)
						UpdateMousePosition((short)LOWORD(lParam), (short)HIWORD(lParam));

					if (absMap || ::GetCapture() == mhwnd) {
						if (!g_mouseCaptured && !g_mouseClipped && g_mouseAutoCapture)
							CaptureMouse();

						switch(msg) {
							case WM_LBUTTONDOWN:
							case WM_LBUTTONDBLCLK:
								im->OnButtonDown(0, kATInputCode_MouseLMB);
								break;
							case WM_MBUTTONDOWN:
							case WM_MBUTTONDBLCLK:
								im->OnButtonDown(0, kATInputCode_MouseMMB);
								break;
							case WM_RBUTTONDOWN:
							case WM_RBUTTONDBLCLK:
								im->OnButtonDown(0, kATInputCode_MouseRMB);
								break;
							case WM_XBUTTONDOWN:
							case WM_XBUTTONDBLCLK:
								if (HIWORD(wParam) == XBUTTON1)
									im->OnButtonDown(0, kATInputCode_MouseX1B);
								else if (HIWORD(wParam) == XBUTTON2)
									im->OnButtonDown(0, kATInputCode_MouseX2B);
								break;
						}
					} else if (g_mouseAutoCapture) {
						CaptureMouse();
						::SetCursor(NULL);
					}
				} else if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
					int x = (short)LOWORD(lParam);
					int y = (short)HIWORD(lParam);

					mbDragActive = MapPixelToBeamPosition(x, y, mDragAnchorX, mDragAnchorY);
				}
			}
			break;

		case WM_CONTEXTMENU:
			if (mhmenuContext) {
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				HMENU hmenuPopup = GetSubMenu(mhmenuContext, 0);

				if (hmenuPopup) {
					++mMouseCursorLock;
					EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_COPY, mDragPreviewSpans.empty() ? MF_DISABLED|MF_GRAYED : MF_ENABLED);
					EnableMenuItem(hmenuPopup, ID_DISPLAYCONTEXTMENU_PASTE, IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_DISABLED|MF_GRAYED);

					TrackPopupMenu(hmenuPopup, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, mhwnd, NULL);
					--mMouseCursorLock;
				}
				return 0;
			}
			break;

		case WM_SETCURSOR:
			if (mMouseCursorLock)
				break;

			if (g_mouseAutoCapture ? g_mouseClipped : g_sim.GetInputManager()->IsMouseAbsoluteMode()) {
				SetCursor(mhcurTarget);
				return TRUE;
			} else if (g_fullscreen) {
				SetCursor(NULL);
				return TRUE;
			} else if (::GetCapture() == mhwnd) {
				::SetCursor(NULL);
				return TRUE;
			} else {
				DWORD pos = ::GetMessagePos();
				int x = (short)LOWORD(pos);
				int y = (short)HIWORD(pos);

				if (x == mLastHoverX || y == mLastHoverY) {
					::SetCursor(NULL);
					return TRUE;
				}

				if (!g_sim.IsRunning()) {
					::SetCursor(::LoadCursor(NULL, IDC_NO));
					return TRUE;
				}

				if (!g_mouseAutoCapture || !g_sim.GetInputManager()->IsMouseMapped()) {
					POINT pt = {x, y};
					::ScreenToClient(mhwnd, &pt);

					int xs, ys;
					if (MapPixelToBeamPosition(pt.x, pt.y, xs, ys) && GetModeLineYPos(ys, true) >= 0) {
						::SetCursor(::LoadCursor(NULL, IDC_IBEAM));
						return TRUE;
					}
				}
			}
			break;

		case WM_MOUSEMOVE:
			if (mbDragActive) {
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				UpdateDragPreview(x, y);
			} else {
				int x = (short)LOWORD(lParam);
				int y = (short)HIWORD(lParam);

				if (g_sim.GetInputManager()->IsMouseAbsoluteMode()) {
					UpdateMousePosition(x, y);
				} else if (::GetCapture() == mhwnd) {
					int dx = x - mLastTrackMouseX;
					int dy = y - mLastTrackMouseY;

					if (dx | dy) {
						ATInputManager *im = g_sim.GetInputManager();

						im->OnMouseMove(0, dx, dy);

						WarpCapturedMouse();
					}
				} else {
					if (x != mLastHoverX || y != mLastHoverY) {
						mLastHoverX = INT_MIN;
						mLastHoverY = INT_MIN;
					}

					if (g_sim.IsRunning()) {
						TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT)};
						tme.dwFlags = TME_HOVER;
						tme.hwndTrack = mhwnd;
						tme.dwHoverTime = HOVER_DEFAULT;

						TrackMouseEvent(&tme);
					}
				}
			}
			break;

		case WM_MOUSEHOVER:
			{
				DWORD pos = ::GetMessagePos();
				mLastHoverX = (short)LOWORD(pos);
				mLastHoverY = (short)HIWORD(pos);

				::SetCursor(NULL);
			}
			return 0;

		case WM_KILLFOCUS:
			if (::GetCapture() == mhwnd)
				::ReleaseCapture();

			if (mbDragActive) {
				mbDragActive = false;
			}

			if (g_mouseClipped) {
				g_mouseClipped = false;
				g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);

				::ClipCursor(NULL);
			}
			break;

		case WM_CAPTURECHANGED:
			g_mouseCaptured = false;
			g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);
			break;

		case WM_ERASEBKGND:
			return 0;

		case WM_PAINT:
			if (mbTextModeEnabled && mpEnhTextEngine) {
				PAINTSTRUCT ps;
				if (HDC hdc = BeginPaint(mhwnd, &ps)) {
					mpEnhTextEngine->Paint(hdc);
					EndPaint(mhwnd, &ps);
				}
				return 0;
			}
			break;

		case WM_COMMAND:
			switch(LOWORD(wParam)) {
				case ID_DISPLAYCONTEXTMENU_COPY:
					Copy();
					return 0;

				case ID_DISPLAYCONTEXTMENU_PASTE:
					Paste();
					return 0;
			}
			break;

		case WM_COPY:
			Copy();
			return 0;

		case WM_PASTE:
			Paste();
			return 0;

		case ATWM_GETAUTOSIZE:
			{
				vdsize32& sz = *(vdsize32 *)lParam;

				sz.w = mDisplayRect.width();
				sz.h = mDisplayRect.height();
				return TRUE;
			}
			break;
	}

	return ATUIPane::WndProc(msg, wParam, lParam);
}

bool ATDisplayPane::OnCreate() {
	if (!ATUIPane::OnCreate())
		return false;

	mhcurTarget = (HCURSOR)LoadImage(g_hInst, MAKEINTRESOURCE(IDC_TARGET), IMAGE_CURSOR, 0, 0, LR_SHARED); 

	mhwndDisplay = (HWND)VDCreateDisplayWindowW32(WS_EX_NOPARENTNOTIFY, WS_CHILD|WS_VISIBLE, 0, 0, 0, 0, (VDGUIHandle)mhwnd);
	if (!mhwndDisplay)
		return false;

	mpDisplay = VDGetIVideoDisplay((VDGUIHandle)mhwndDisplay);
	g_pDisplay = mpDisplay;

	mpDisplay->SetReturnFocus(true);
	UpdateFilterMode();
	mpDisplay->SetAccelerationMode(IVDVideoDisplay::kAccelResetInForeground);

	// We need to push in an initial frame for two reasons: (1) black out immediately, (2) force full
	// screen mode to size the window correctly.
	mpDisplay->SetSourceSolidColor(0);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	gtia.SetVideoOutput(mpDisplay);
	gtia.UpdateScreen(true, true);

	return true;
}

void ATDisplayPane::OnDestroy() {
	if (mpEnhTextEngine) {
		mpEnhTextEngine->Shutdown();
		mpEnhTextEngine = NULL;
	}

	if (mpDisplay) {
		g_sim.GetGTIA().SetVideoOutput(NULL);

		mpDisplay->Destroy();
		mpDisplay = NULL;
		g_pDisplay = NULL;
		mhwndDisplay = NULL;
	}

	ATUIPane::OnDestroy();
}

void ATDisplayPane::CaptureMouse() {
	ATInputManager *im = g_sim.GetInputManager();

	if (im->IsMouseMapped() && g_sim.IsRunning()) {
		if (im->IsMouseAbsoluteMode()) {
			RECT r;

			if (::GetClientRect(mhwnd, &r)) {
				::MapWindowPoints(mhwnd, NULL, (LPPOINT)&r, 2);
				::ClipCursor(&r);

				g_mouseClipped = true;
				g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);
			}
		} else {
			::SetCapture(mhwnd);
			::SetCursor(NULL);

			g_mouseCaptured = true;
			g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);

			WarpCapturedMouse();
		}
	}
}

void ATDisplayPane::ResetDisplay() {
	if (mpDisplay)
		mpDisplay->Reset();
}

void ATDisplayPane::Copy() {
	if (mDragPreviewSpans.empty())
		return;

	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	uint8 data[48];

	VDStringA s;

	for(TextSpans::const_iterator it(mDragPreviewSpans.begin()), itEnd(mDragPreviewSpans.end());
		it != itEnd;
		++it)
	{
		const TextSpan& ts = *it;
		const ATAnticEmulator::DLHistoryEntry& dle = dlhist[ts.mY];

		if (!dle.mbValid)
			continue;

		uint32 len = ts.mCharWidth;
		g_sim.GetMemoryManager()->DebugAnticReadMemory(data, dle.mPFAddress + ts.mCharX, len);

		static const uint8 kInternalToATASCIIXorTab[4]={
			0x20, 0x60, 0x40, 0x00
		};

		uint8 mask = (dle.mControl & 4) ? 0x3f : 0x7f;
		uint8 xor = (dle.mControl & 4) && (dle.mCHBASE & 1) ? 0x40 : 0x00;

		for(uint32 i=0; i<len; ++i) {
			uint8 c = data[i];

			c &= mask;
			c ^= xor;

			c ^= kInternalToATASCIIXorTab[(c & 0x60) >> 5];

			// replace non-printable chars with spaces
			if ((uint8)(c - 0x20) >= 0x5f)
				c = ' ';

			s += (char)c;
		}

		// trim excess spaces at end
		while(!s.empty() && s.back() == ' ')
			s.pop_back();

		if (&*it != &mDragPreviewSpans.back())
			s += "\r\n";
	}

	if (::OpenClipboard((HWND)mhwnd)) {
		if (::EmptyClipboard()) {
			HANDLE hMem;
			void *lpvMem;

			if (hMem = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, s.size() + 1)) {
				if (lpvMem = ::GlobalLock(hMem)) {
					memcpy(lpvMem, s.c_str(), s.size() + 1);

					::GlobalUnlock(lpvMem);
					::SetClipboardData(CF_TEXT, hMem);
					::CloseClipboard();
					return;
				}
				::GlobalFree(hMem);
			}
		}
		::CloseClipboard();
	}
}

void ATDisplayPane::Paste() {
	OnCommandPaste();
}

void ATDisplayPane::OnSize() {
	RECT r;
	GetClientRect(mhwnd, &r);

	if (g_mouseClipped) {
		RECT rs = r;

		MapWindowPoints(mhwnd, NULL, (LPPOINT)&rs, 2);
		ClipCursor(&rs);
	}

	if (mhwndDisplay) {
		if (mpDisplay) {
			UpdateFilterMode();

			vdrect32 rd(r.left, r.top, r.right, r.bottom);
			sint32 w = rd.width();
			sint32 h = rd.height();

			int sw = 1;
			int sh = 1;
			g_sim.GetGTIA().GetFrameSize(sw, sh);

			if (w && h) {
				if (g_displayStretchMode == kATDisplayStretchMode_PreserveAspectRatio || g_displayStretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio) {
					const bool pal = g_sim.GetVideoStandard() != kATVideoStandard_NTSC;
					const float desiredAspect = (pal ? 1.03964f : 0.857141f);
					const float fsw = (float)sw * desiredAspect;
					const float fsh = (float)sh;
					const float fw = (float)w;
					const float fh = (float)h;
					float zoom = std::min<float>(fw / fsw, fh / fsh);

					if (g_displayStretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio && zoom > 1)
						zoom = floorf(zoom);

					sint32 w2 = (sint32)(0.5f + fsw * zoom);
					sint32 h2 = (sint32)(0.5f + fsh * zoom);

					rd.left		= (w - w2) >> 1;
					rd.right	= rd.left + w2;
					rd.top		= (h - h2) >> 1;
					rd.bottom	= rd.top + h2;
				} else if (g_displayStretchMode == kATDisplayStretchMode_SquarePixels || g_displayStretchMode == kATDisplayStretchMode_Integral) {
					int ratio = std::min<int>(w / sw, h / sh);

					if (ratio < 1 || g_displayStretchMode == kATDisplayStretchMode_SquarePixels) {
						if (w*sh < h*sw) {		// (w / sw) < (h / sh) -> w*sh < h*sw
							// width is smaller ratio -- compute restricted height
							int restrictedHeight = (sh * w + (sw >> 1)) / sw;

							rd.top = (h - restrictedHeight) >> 1;
							rd.bottom = rd.top + restrictedHeight;
						} else {
							// height is smaller ratio -- compute restricted width
							int restrictedWidth = (sw * h + (sh >> 1)) / sh;

							rd.left = (w - restrictedWidth) >> 1;
							rd.right = rd.left+ restrictedWidth;
						}
					} else {
						int finalWidth = sw * ratio;
						int finalHeight = sh * ratio;

						rd.left = (w - finalWidth) >> 1;
						rd.right = rd.left + finalWidth;

						rd.top = (h - finalHeight) >> 1;
						rd.bottom = rd.top + finalHeight;
					}
				}
			}

			mDisplayRect = rd;
			mpDisplay->SetDestRect(&rd, 0);
		}

		SetWindowPos(mhwndDisplay, NULL, 0, 0, r.right, r.bottom, SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	}

	if (mbTextModeEnabled) {
		if (mpEnhTextEngine)
			mpEnhTextEngine->OnSize(r.right, r.bottom);

		InvalidateRect(mhwnd, NULL, TRUE);
	}
}

void ATDisplayPane::UpdateFilterMode() {
	if (!mpDisplay)
		return;

	switch(g_dispFilterMode) {
		case kATDisplayFilterMode_Point:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterPoint);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;

		case kATDisplayFilterMode_Bilinear:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;

		case kATDisplayFilterMode_SharpBilinear:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
			{
				ATGTIAEmulator& gtia = g_sim.GetGTIA();
				int pw = 2;
				int ph = 2;

				gtia.GetPixelAspectMultiple(pw, ph);

				static const float kFactors[5] = { 1.259f, 1.587f, 2.0f, 2.520f, 3.175f };

				const float factor = kFactors[std::max(0, std::min(4, g_dispFilterSharpness + 2))];

				mpDisplay->SetPixelSharpness(std::max(1.0f, factor / (float)pw), std::max(1.0f, factor / (float)ph));
			}
			break;

		case kATDisplayFilterMode_Bicubic:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterBicubic);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;

		case kATDisplayFilterMode_AnySuitable:
			mpDisplay->SetFilterMode(IVDVideoDisplay::kFilterAnySuitable);
			mpDisplay->SetPixelSharpness(1.0f, 1.0f);
			break;
	}
}

void ATDisplayPane::WarpCapturedMouse() {
	RECT r;

	if (::GetClientRect(mhwnd, &r)) {
		mLastTrackMouseX = r.right >> 1;
		mLastTrackMouseY = r.bottom >> 1;
		POINT pt = {mLastTrackMouseX, mLastTrackMouseY};
		::ClientToScreen(mhwnd, &pt);
		::SetCursorPos(pt.x, pt.y);
	}
}

void ATDisplayPane::UpdateTextDisplay(bool enabled) {
	if (!enabled) {
		if (mbTextModeEnabled) {
			mbTextModeEnabled = false;

			if (mpEnhTextEngine) {
				mpEnhTextEngine->Shutdown();
				mpEnhTextEngine = NULL;
			}

			::ShowWindow(mhwndDisplay, SW_SHOWNOACTIVATE);
		}

		return;
	}

	bool forceInvalidate = false;
	if (!mbTextModeEnabled) {
		mbTextModeEnabled = true;

		if (!mpEnhTextEngine) {
			mpEnhTextEngine = ATUICreateEnhancedTextEngine();
			mpEnhTextEngine->Init(mhwnd, &g_sim);
		}

		UpdateTextModeFont();

		forceInvalidate = true;

		::ShowWindow(mhwndDisplay, SW_HIDE);
	}

	if (mpEnhTextEngine)
		mpEnhTextEngine->Update(forceInvalidate);
}

void ATDisplayPane::UpdateTextModeFont() {
	if (mpEnhTextEngine)
		mpEnhTextEngine->SetFont(&g_enhancedTextFont);
}

void ATDisplayPane::UpdateMousePosition(int x, int y) {
	int padX = 0;
	int padY = 0;

	if (mWidth)
		padX = VDRoundToInt(x * 131072.0f / ((float)mWidth - 1) - 0x10000);

	if (mHeight)
		padY = VDRoundToInt(y * 131072.0f / ((float)mHeight - 1) - 0x10000);

	ATInputManager *im = g_sim.GetInputManager();
	im->SetMousePadPos(padX, padY);

	float xc, yc;
	if (!MapPixelToBeamPosition(x, y, xc, yc))
		return;

	// map cycles to normalized position (note that this must match the light pen
	// computations)
	float xn = (xc - 128.0f) * (65536.0f / 94.0f);
	float yn = (yc - 128.0f) * (65536.0f / 188.0f);

	im->SetMouseBeamPos(VDRoundToInt(xn), VDRoundToInt(yn));
}

bool ATDisplayPane::MapPixelToBeamPosition(int x, int y, float& hcyc, float& vcyc) const {
	if (!mDisplayRect.contains(vdpoint32(x, y)))
		return false;

	x -= mDisplayRect.left;
	y -= mDisplayRect.top;

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	// map position to cycles
	hcyc = (float)scanArea.left + ((float)x + 0.5f) * (float)scanArea.width()  / (float)mDisplayRect.width()  - 0.5f;
	vcyc = (float)scanArea.top  + ((float)y + 0.5f) * (float)scanArea.height() / (float)mDisplayRect.height() - 0.5f;
	return true;
}

bool ATDisplayPane::MapPixelToBeamPosition(int x, int y, int& xc, int& yc) const {
	float xf, yf;

	if (!MapPixelToBeamPosition(x, y, xf, yf))
		return false;

	xc = (int)floorf(xf + 0.5f);
	yc = (int)floorf(yf + 0.5f);
	return true;
}

void ATDisplayPane::UpdateDragPreview(int x, int y) {
	int xc2, yc2;

	if (!MapPixelToBeamPosition(x, y, xc2, yc2)) {
		ClearDragPreview();
		return;
	}

	int xc1 = mDragAnchorX;
	int yc1 = mDragAnchorY;

	yc1 = GetModeLineYPos(yc1, false);
	yc2 = GetModeLineYPos(yc2, false);

	if ((yc1 | yc2) < 0) {
		ClearDragPreview();
		return;
	}

	if (yc1 > yc2 || (yc1 == yc2 && xc1 > xc2)) {
		std::swap(xc1, xc2);
		std::swap(yc1, yc2);
	}

	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	IATUIRenderer *uir = g_sim.GetUIRenderer();

	mDragPreviewSpans.clear();
	uir->ClearXorRects();

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	int frameW;
	int frameH;
	bool rgb32;
	gtia.GetRawFrameFormat(frameW, frameH, rgb32);

	int shiftX = frameW > scanArea.width() * 3;
	int shiftY = frameH > (scanArea.height() * 3) / 2;

	for(int yc = yc1; yc <= yc2; ++yc) {
		if (!dlhist[yc].mbValid)
			continue;

		bool textModeLine = false;
		switch(dlhist[yc].mControl & 15) {
			case 2:
			case 3:
			case 6:
			case 7:
				textModeLine = true;
				break;
		}

		if (!textModeLine)
			continue;

		int pfwidth = dlhist[yc].mDMACTL & 3;
		if (!pfwidth)
			continue;

		if (pfwidth < 3 && (dlhist[yc].mControl & 0x10))
			++pfwidth;

		int left = (yc == yc1) ? xc1 : scanArea.left;
		int right = (yc == yc2) ? xc2 : scanArea.right;

		const int leftborder = 0x50 - 0x10*pfwidth;
		left = std::max<int>(left, leftborder);
		right = std::min<int>(right, 0xB0 + 0x10*pfwidth);

		bool dblwide = false;
		switch(dlhist[yc].mControl & 15) {
			case 2:
			case 3:
				left = (left + 2) & ~3;
				right = (right + 2) & ~3;
				break;

			case 6:
			case 7:
				left  = (left + 4) & ~7;
				right = (right + 4) & ~7;
				dblwide = true;
				break;
		}

		if (left >= right)
			continue;

		TextSpan& ts = mDragPreviewSpans.push_back();
		ts.mX = left;
		ts.mWidth = right - left;
		ts.mY = yc;
		ts.mHeight = 0;

		if (dblwide) {
			ts.mCharX = (left - leftborder) >> 3;
			ts.mCharWidth = (right - left) >> 3;
		} else {
			ts.mCharX = (left - leftborder) >> 2;
			ts.mCharWidth = (right - left) >> 2;
		}

		for(int i=0; i<16; ++i) {
			++ts.mHeight;

			if (yc + ts.mHeight >= 248 || dlhist[yc + ts.mHeight].mbValid)
				break;
		}

		int hiX = ts.mX*2 - scanArea.left*2;
		int hiY = ts.mY - scanArea.top;
		int hiW = ts.mWidth*2;
		int hiH = ts.mHeight;

		uir->AddXorRect(hiX << shiftX, hiY << shiftY, hiW << shiftX, hiH << shiftY);
	}
}

void ATDisplayPane::ClearDragPreview() {
	mDragPreviewSpans.clear();

	IATUIRenderer *uir = g_sim.GetUIRenderer();
	uir->ClearXorRects();
}

int ATDisplayPane::GetModeLineYPos(int ys, bool checkValidCopyText) const {
	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	for(int i=0; i<16; ++i, --ys) {
		if (ys >= 8 && ys < 248) {
			if (dlhist[ys].mbValid) {
				if (checkValidCopyText) {
					int mode = dlhist[ys].mControl & 15;

					if (mode != 2 && mode != 3 && mode != 6 && mode != 7)
						return -1;
				}

				return ys;
			}
		}
	}

	return -1;
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
			if (OnCommand(LOWORD(wParam)))
				return 0;
			break;

		case WM_INITMENU:
			OnInitMenu((HMENU)wParam);
			return 0;

		case WM_SETCURSOR:
			if (g_fullscreen) {
				SetCursor(NULL);
				return TRUE;
			}
			break;

		case WM_APP + 100:
			{
				MSG& globalMsg = *(MSG *)lParam;

				if (g_hAccel && TranslateAccelerator(mhwnd, g_hAccel, &globalMsg))
					return TRUE;
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

	if (cds.dwData != 0xA7000000)
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
				g_sim.SetKernelMode(kATKernelMode_LLE);
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

		while(cmdLine.GetNextNonSwitchArgument(it, arg)) {
			if (!unloaded) {
				unloaded = true;
				g_sim.UnloadAll();
			}

			DoLoad((VDGUIHandle)hwnd, arg, bootvrw, bootrw, cartmapper);
			coldReset = true;// required to set up cassette autoboot
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
					ATLoadContext ctx;
					ctx.mLoadType = kATLoadType_Disk;
					ctx.mLoadIndex = i;

					g_sim.Load(imagestr.c_str() + 1, vrw, rw, &ctx);
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
					g_sim.LoadCartridgeSC3D();
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
			config.mListenPort = key.getInt("Serial Ports: Listen Port", config.mListenPort);
			if (config.mListenPort < 1 || config.mListenPort > 65535)
				config.mListenPort = 0;

			config.mConnectionSpeed = key.getInt("Serial Ports: Modem connection rate", config.mConnectionSpeed);
			config.mbListenForIPv6 = key.getBool("Serial Ports: Listen for IPv6 connections", config.mbListenForIPv6);
			config.mbDisableThrottling = key.getBool("Serial Ports: Disable throttling", config.mbDisableThrottling);
			config.mbRequireMatchedDTERate = key.getBool("Serial Ports: Require matched DTE rate", config.mbRequireMatchedDTERate);
			config.mbExtendedBaudRates = key.getBool("Serial Ports: Enable extended baud rates", config.mbExtendedBaudRates);

			key.getString("Serial Ports: Dial address", config.mDialAddress);
			key.getString("Serial Ports: Dial service", config.mDialService);

			g_sim.GetRS232()->SetConfig(config);
		}

		g_sim.SetMapRAMEnabled(key.getBool("Memory: MapRAM", g_sim.IsMapRAMEnabled()));
		g_sim.SetRandomFillEnabled(key.getBool("Memory: Randomize on start", g_sim.IsRandomFillEnabled()));
		g_kbdOpts.mbRawKeys = key.getBool("Keyboard: Raw mode", g_kbdOpts.mbRawKeys);
		g_sim.SetKeyboardPresent(key.getBool("Console: Keyboard present", g_sim.IsKeyboardPresent()));
		g_sim.SetForcedSelfTest(key.getBool("Console: Force self test", g_sim.IsForcedSelfTest()));
	}

	g_kbdOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)key.getEnumInt("Keyboard: Arrow key mode", ATUIKeyboardOptions::kAKMCount, g_kbdOpts.mArrowKeyMode);
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
	cpu.SetCPUMode((ATCPUMode)key.getEnumInt("CPU: Chip type", kATCPUModeCount, cpu.GetCPUMode()));

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

	key.setBool("Memory: MapRAM", g_sim.IsMapRAMEnabled());
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

	ATCPUEmulator& cpu = g_sim.GetCPU();
	key.setBool("CPU: History enabled", cpu.IsHistoryEnabled());
	key.setBool("CPU: Pathfinding enabled", cpu.IsPathfindingEnabled());
	key.setBool("CPU: Stop on BRK", cpu.GetStopOnBRK());
	key.setBool("CPU: Allow NMI blocking", cpu.IsNMIBlockingEnabled());
	key.setInt("CPU: Chip type", cpu.GetCPUMode());

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
		key.setInt("Serial Ports: Listen port", config.mListenPort);
		key.setInt("Serial Ports: Modem connection rate", config.mConnectionSpeed);
		key.setBool("Serial Ports: Listen for IPv6 connections", config.mbListenForIPv6);
		key.setBool("Serial Ports: Disable throttling", config.mbDisableThrottling);
		key.setBool("Serial Ports: Require matched DTE rate", config.mbRequireMatchedDTERate);
		key.setBool("Serial Ports: Enable extended baud rates", config.mbExtendedBaudRates);
		key.setString("Serial Ports: Dial address", config.mDialAddress.c_str());
		key.setString("Serial Ports: Dial service", config.mDialService.c_str());
	}

	key.setBool("Console: Keyboard present", g_sim.IsKeyboardPresent());
	key.setBool("Console: Force self test", g_sim.IsForcedSelfTest());

	key.setBool("Keyboard: Raw mode", g_kbdOpts.mbRawKeys);
	key.setInt("Keyboard: Arrow key mode", g_kbdOpts.mArrowKeyMode);
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

	MSG msg;
	int rcode = 0;
	bool lastIsRunning = false;
	for(;;) {
		for(int i=0; i<2; ++i) {
			DWORD flags = i ? PM_REMOVE : PM_QS_INPUT | PM_REMOVE;

			while(PeekMessage(&msg, NULL, 0, 0, flags)) {
				if (msg.message == WM_QUIT) {
					rcode = (int)msg.wParam;
					goto xit;
				}

				if (msg.hwnd) {
					HWND hwndOwner;
					switch(msg.message) {
						case WM_KEYDOWN:
						case WM_SYSKEYDOWN:
						case WM_KEYUP:
						case WM_SYSKEYUP:
						case WM_CHAR:
							hwndOwner = GetAncestor(msg.hwnd, GA_ROOT);
							if (hwndOwner) {
								if (SendMessage(hwndOwner, WM_APP + 100, 0, (LPARAM)&msg))
									continue;
							}
							break;

						case WM_SYSCHAR:
							if (g_hwnd && msg.hwnd != g_hwnd && GetAncestor(msg.hwnd, GA_ROOT) == g_hwnd)
							{
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
			
					switch(msg.message) {
						case WM_KEYDOWN:
							if (SendMessage(msg.hwnd, MYWM_PREKEYDOWN, msg.wParam, msg.lParam))
								continue;
							break;
						case WM_SYSKEYDOWN:
							if (SendMessage(msg.hwnd, MYWM_PRESYSKEYDOWN, msg.wParam, msg.lParam))
								continue;
							break;
						case WM_KEYUP:
							if (SendMessage(msg.hwnd, MYWM_PREKEYUP, msg.wParam, msg.lParam))
								continue;
							break;
						case WM_SYSKEYUP:
							if (SendMessage(msg.hwnd, MYWM_PRESYSKEYUP, msg.wParam, msg.lParam))
								continue;
							break;
					}
				}

				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		if (!g_ATCmdLineRead) {
			g_ATCmdLineRead = true;

			ReadCommandLine(hwnd, g_ATCmdLine);
		}

		bool isRunning = g_sim.IsRunning();

		if (isRunning != lastIsRunning) {
			if (isRunning)
				timeBeginPeriod(1);
			else
				timeEndPeriod(1);

			lastIsRunning = isRunning;
		}

		if (isRunning && (g_winActive || !g_pauseInactive)) {
			uint32 frame = antic.GetFrameCounter();
			if (frame != lastFrame) {
				CheckRecordingExceptions();

				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateTextDisplay(g_enhancedText != 0);

				lastFrame = frame;

				uint64 curTime = VDGetPreciseTick();

				++ticks;

				if (!g_fullscreen && (ticks - nextTickUpdate) >= 0) {
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
							::MsgWaitForMultipleObjects(0, NULL, FALSE, ticks, QS_ALLINPUT);
							continue;
						}
					}
				}
			}

			// do not allow frame drops if we are recording
			if (g_pVideoWriter)
				dropFrame = false;

			ATSimulator::AdvanceResult ar = g_sim.Advance(dropFrame);

			if (ar == ATSimulator::kAdvanceResult_Stopped) {
				updateScreenPending = true;
			} else {
				updateScreenPending = false;

				if (ar == ATSimulator::kAdvanceResult_WaitingForFrame)
					::MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT);
			}

			continue;
		} else {
			if (ATGetDebugger()->Tick())
				continue;

			if (ATIsDebugConsoleActive()) {
				if (g_fullscreen)
					ATSetFullscreen(false);

				if (g_mouseCaptured)
					ReleaseCapture();

				if (g_mouseClipped) {
					g_mouseClipped = false;
					g_winCaptionUpdater.SetMouseCaptured(g_mouseClipped || g_mouseCaptured);

					::ClipCursor(NULL);
				}
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
xit:
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
			g_ATOptions.mbDisplayOpenGL = false;
		} else if (g_ATCmdLine.FindAndRemoveSwitch(L"ddraw")) {
			g_ATOptions.mbDirty = true;
			g_ATOptions.mbDisplayDDraw = true;
			g_ATOptions.mbDisplayD3D9 = false;
			g_ATOptions.mbDisplayOpenGL = false;
		} else if (g_ATCmdLine.FindAndRemoveSwitch(L"opengl")) {
			g_ATOptions.mbDirty = true;
			g_ATOptions.mbDisplayDDraw = false;
			g_ATOptions.mbDisplayD3D9 = false;
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
	bool wsaInited = false;

	WSADATA wsa;
	if (!WSAStartup(MAKEWORD(2, 0), &wsa))
		wsaInited = true;

	VDRegisterVideoDisplayControl();
	VDShaderEditorBaseWindow::Register();

	ATInitUIFrameSystem();

	struct local {
		static void DisplayUpdateFn(ATOptions& opts, const ATOptions *prevOpts, void *) {
			if (prevOpts) {
				if (prevOpts->mbDisplayDDraw == opts.mbDisplayDDraw
					&& prevOpts->mbDisplayD3D9 == opts.mbDisplayD3D9
					&& prevOpts->mbDisplayOpenGL == opts.mbDisplayOpenGL)
					return;
			}

			VDVideoDisplaySetFeatures(true, false, false, opts.mbDisplayOpenGL, opts.mbDisplayD3D9, false, false);
			VDVideoDisplaySetDDrawEnabled(opts.mbDisplayDDraw);

			ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
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
	ATRegisterUIPaneType(kATUIPaneId_Display, VDRefCountObjectFactory<ATDisplayPane, ATUIPane>);
	ATUIInitVirtualKeyMap(g_kbdOpts);

	g_hMenu = LoadMenu(NULL, MAKEINTRESOURCE(IDR_MENU1));

	HMENU hFileMenu = GetSubMenu(g_hMenu, 0);
	if (hFileMenu) {
		int n = GetMenuItemCount(hFileMenu);
		
		if (n > 0) {
			for(int i=0; i<n; ++i) {
				if (GetMenuItemID(hFileMenu, i) == ID_FILE_RECENTLYBOOTED) {
					g_hMenuMRU = CreateMenu();

					MENUITEMINFOW mii = {};
					mii.cbSize = MENUITEMINFO_SIZE_VERSION_400W;
					mii.fMask = MIIM_SUBMENU;
					mii.hSubMenu = g_hMenuMRU;
					SetMenuItemInfoW(hFileMenu, i, TRUE, &mii);

					ATUpdateMRUListMenu(g_hMenuMRU, ID_FILE_MRU_BASE, ID_FILE_MRU_BASE + 99);
				}
			}
		}
	}

	g_hAccel = LoadAccelerators(NULL, MAKEINTRESOURCE(IDR_ACCELERATOR1));

	ATInitPortMenus(g_hMenu, g_sim.GetInputManager());

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

	g_winCaptionUpdater.Init(&g_sim);

	SetWindowText(hwnd, _T("Altirra"));
	g_hwnd = hwnd;

	SetMenu(hwnd, g_hMenu);

	const bool useHardwareBaseline = g_ATCmdLine.FindAndRemoveSwitch(L"baseline");

	LoadROMImageSettings();
	LoadSettings(useHardwareBaseline);

	try {
		g_sim.LoadROMs();
	} catch(MyError& e) {
		e.post(hwnd, "Altirra Error");
	}

	ATOptionsSave();

	g_sim.Resume();
	ATInitDebugger();
	ATInitEmuErrorHandler((VDGUIHandle)g_hwnd, &g_sim);

	if (!ATRestorePaneLayout(NULL))
		ATActivateUIPane(kATUIPaneId_Display, true);

	if (ATGetUIPane(kATUIPaneId_Display))
		ATActivateUIPane(kATUIPaneId_Display, true);

	g_sim.ColdReset();

	int returnCode = RunMainLoop(hwnd);

	StopRecording();

	SaveSettings();
	VDSaveFilespecSystemData();

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
