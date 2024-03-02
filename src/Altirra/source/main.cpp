//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2010 Avery Lee
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
#include <vd2/system/vdtypes.h>
#include <vd2/system/w32assist.h>
#include <vd2/system/error.h>
#include <vd2/system/registry.h>
#include <vd2/system/registrymemory.h>
#include <vd2/system/cmdline.h>
#include <vd2/system/thunk.h>
#include <vd2/system/binary.h>
#include <vd2/system/strutil.h>
#include <vd2/Riza/display.h>
#include <vd2/Riza/direct3d.h>
#include <vd2/Dita/services.h>
#include "console.h"
#include "simulator.h"
#include "cassette.h"
#include "ui.h"
#include "uiframe.h"
#include "debugger.h"
#include "harddisk.h"
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
#include "rs232.h"
#include "uienhancedtext.h"
#include "uikeyboard.h"

#ifdef _DEBUG
	#define AT_VERSION_DEBUG "-debug"
#else
	#define AT_VERSION_DEBUG ""
#endif

#pragma comment(lib, "comctl32")
#pragma comment(lib, "shlwapi")

///////////////////////////////////////////////////////////////////////////////

void ATUIShowDialogInputMappings(VDZHWND parent, ATInputManager& iman, IATJoystickManager *ijoy);
void ATUIShowDiskDriveDialog(VDGUIHandle hParent);
void ATUIShowHardDiskDialog(VDGUIHandle hParent, IATHardDiskEmulator *hd);
void ATUIOpenAdjustColorsDialog(VDGUIHandle hParent);
void ATUICloseAdjustColorsDialog();
void ATUIShowAudioOptionsDialog(VDGUIHandle hParent);
void ATUIShowTapeControlDialog(VDGUIHandle hParent, ATCassetteEmulator& cassette);
void ATUIShowCPUOptionsDialog(VDGUIHandle h);
void ATUIShowSerialPortsDialog(VDGUIHandle h);
int ATUIShowDialogCartridgeMapper(VDGUIHandle h, uint32 cartSize, bool show2600Warning);
void ATUIShowDialogLightPen(VDGUIHandle h, ATLightPenPort *lpp);
void ATUIShowDialogDebugFont(VDGUIHandle hParent);
void ATUIShowDialogCheater(VDGUIHandle hParent, ATCheatEngine *engine);
void ATUIShowDialogROMImages(VDGUIHandle hParent, ATSimulator& sim);

void ATUIRegisterDragDropHandler(VDGUIHandle h);
void ATUIRevokeDragDropHandler(VDGUIHandle h);

void ATUILoadRegistry(const wchar_t *path);
void ATUISaveRegistry(const wchar_t *fnpath);

///////////////////////////////////////////////////////////////////////////////

int ATExceptionFilter(DWORD code, EXCEPTION_POINTERS *exp);

void ATInitDebugger();
void ATInitProfilerUI();
void ATInitUIPanes();
void ATShutdownUIPanes();
void ATShowChangeLog(VDGUIHandle hParent);

void SaveInputMaps();

void ProcessKey(char c, bool repeat, bool allowQueue, bool useCooldown);
void ProcessVirtKey(int vkey, uint8 keycode, bool repeat);
void ProcessRawKeyUp(int vkey);
void DoLoad(VDGUIHandle h, const wchar_t *path, bool vrw, bool rw, int cartmapper);

///////////////////////////////////////////////////////////////////////////////

class ATDisplayBasePane : public ATUIPane {
public:
	ATDisplayBasePane();

	virtual void CaptureMouse() = 0;
	virtual void OnSize() = 0;
	virtual void UpdateTextDisplay(bool enabled) = 0;
	virtual void UpdateTextModeFont() = 0;
};

ATDisplayBasePane::ATDisplayBasePane()
	: ATUIPane(kATUIPaneId_Display, "Display")
{
}

///////////////////////////////////////////////////////////////////////////////
HINSTANCE g_hInst;
HWND g_hwnd;
ATContainerWindow *g_pMainWindow;
HACCEL g_hAccel;
IVDVideoDisplay *g_pDisplay;
HMENU g_hMenu;

ATSimulator g_sim;

bool g_fullscreen = false;
bool g_mouseCaptured = false;
bool g_mouseClipped = false;
bool g_mouseAutoCapture = true;
bool g_pauseInactive = true;
bool g_winActive = true;
bool g_showFps = false;
bool g_rawKeys = false;
int g_lastVkeyPressed;
int g_lastVkeySent;

IVDVideoDisplay::FilterMode g_dispFilterMode = IVDVideoDisplay::kFilterBilinear;
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
	kATDisplayStretchModeCount
};

ATDisplayStretchMode g_displayStretchMode = kATDisplayStretchMode_PreserveAspectRatio;

#define MYWM_PRESYSKEYDOWN (WM_APP + 0x120)
#define MYWM_PRESYSKEYUP (WM_APP + 0x121)
#define MYWM_PREKEYDOWN (WM_APP + 0x122)
#define MYWM_PREKEYUP (WM_APP + 0x123)

///////////////////////////////////////////////////////////////////////////////

class ATInputPortMenu {
public:
	ATInputPortMenu();
	~ATInputPortMenu();

	void Init(int portIdx, uint32 baseId, HMENU subMenu);
	void Shutdown();

	void Reload();
	void UpdateMenu();
	void HandleCommand(uint32 id);

protected:
	int mPortIdx;
	uint32 mBaseId;
	HMENU mhmenu;

	typedef vdfastvector<ATInputMap *> InputMaps;
	InputMaps mInputMaps;

	struct InputMapSort;
};

struct ATInputPortMenu::InputMapSort {
	bool operator()(const ATInputMap *x, const ATInputMap *y) const {
		return vdwcsicmp(x->GetName(), y->GetName()) < 0;
	}
};

ATInputPortMenu::ATInputPortMenu()
	: mPortIdx(0)
	, mBaseId(0)
	, mhmenu(NULL)
{
}

ATInputPortMenu::~ATInputPortMenu() {
	Shutdown();
}

void ATInputPortMenu::Init(int portIdx, uint32 baseId, HMENU subMenu) {
	mPortIdx = portIdx;
	mBaseId = baseId;
	mhmenu = subMenu;
}

void ATInputPortMenu::Shutdown() {
	while(!mInputMaps.empty()) {
		mInputMaps.back()->Release();
		mInputMaps.pop_back();
	}

	mhmenu = NULL;
}

void ATInputPortMenu::Reload() {
	if (!mhmenu)
		return;

	// clear existing items
	int count = ::GetMenuItemCount(mhmenu);
	for(int i=count; i>=1; --i)
		::DeleteMenu(mhmenu, i, MF_BYPOSITION);

	while(!mInputMaps.empty()) {
		mInputMaps.back()->Release();
		mInputMaps.pop_back();
	}

	// find input maps that touch a port
	ATInputManager& im = *g_sim.GetInputManager();

	uint32 mapCount = im.GetInputMapCount();
	for(uint32 i=0; i<mapCount; ++i) {
		vdrefptr<ATInputMap> imap;
		if (im.GetInputMapByIndex(i, ~imap)) {
			if (imap->UsesPhysicalPort(mPortIdx)) {
				mInputMaps.push_back(imap);
				imap.release();
			}
		}
	}

	// sort input maps
	std::sort(mInputMaps.begin(), mInputMaps.end(), InputMapSort());

	// populate menu
	const uint32 entryCount = mInputMaps.size();
	for(uint32 i=0; i<entryCount; ++i) {
		ATInputMap *imap = mInputMaps[i];

		VDAppendMenuW32(mhmenu, MF_STRING, mBaseId + i + 1, imap->GetName());
	}
}

void ATInputPortMenu::UpdateMenu() {
	ATInputManager& im = *g_sim.GetInputManager();

	uint32 n = mInputMaps.size();
	bool anyActive = false;

	for(uint32 i=0; i<n; ++i) {
		bool active = false;

		if (im.IsInputMapEnabled(mInputMaps[i])) {
			active = true;
			anyActive = true;
		}

		VDCheckRadioMenuItemByPositionW32(mhmenu, i+1, active);
	}

	VDCheckRadioMenuItemByPositionW32(mhmenu, 0, !anyActive);
}

void ATInputPortMenu::HandleCommand(uint32 id) {
	ATInputManager& im = *g_sim.GetInputManager();

	uint32 idx = id - mBaseId;
	uint32 i = 1;

	for(InputMaps::const_iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it, ++i) {
		ATInputMap *imap = *it;

		im.ActivateInputMap(imap, id == (mBaseId + i));
	}
}

ATInputPortMenu g_portMenus[4];

HMENU ATFindParentMenuW32(HMENU hmenuStart, UINT id) {
	int n = GetMenuItemCount(hmenuStart);
	for(int i=0; i<n; ++i) {
		if (GetMenuItemID(hmenuStart, i) == id)
			return hmenuStart;

		HMENU hSubMenu = GetSubMenu(hmenuStart, i);
		if (hSubMenu) {
			HMENU result = ATFindParentMenuW32(hSubMenu, id);
			if (result)
				return result;
		}
	}

	return NULL;
}

void ATInitPortMenus(HMENU hmenu) {
	HMENU hSubMenu;

	hSubMenu = ATFindParentMenuW32(hmenu, ID_INPUT_PORT1_NONE);
	g_portMenus[0].Init(0, ID_INPUT_PORT1_NONE, hSubMenu);

	hSubMenu = ATFindParentMenuW32(hmenu, ID_INPUT_PORT2_NONE);
	g_portMenus[1].Init(1, ID_INPUT_PORT2_NONE, hSubMenu);

	hSubMenu = ATFindParentMenuW32(hmenu, ID_INPUT_PORT3_NONE);
	g_portMenus[2].Init(2, ID_INPUT_PORT3_NONE, hSubMenu);

	hSubMenu = ATFindParentMenuW32(hmenu, ID_INPUT_PORT4_NONE);
	g_portMenus[3].Init(3, ID_INPUT_PORT4_NONE, hSubMenu);
}

void ATUpdatePortMenus() {
	for(int i=0; i<4; ++i)
		g_portMenus[i].UpdateMenu();
}

void ATShutdownPortMenus() {
	for(int i=0; i<4; ++i)
		g_portMenus[i].Shutdown();
}

void ATReloadPortMenus() {
	for(int i=0; i<4; ++i)
		g_portMenus[i].Reload();
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
			g_sim.SetTurboModeEnabled(state);
			break;
		case kATInputTrigger_KeySpace:
			if (g_rawKeys) {
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

class ATAboutDialog : public VDDialogFrameW32 {
public:
	ATAboutDialog();
	~ATAboutDialog();
};

ATAboutDialog::ATAboutDialog()
	: VDDialogFrameW32(IDD_ABOUT)
{
}

ATAboutDialog::~ATAboutDialog() {
}

///////////////////////////////////////////////////////////////////////////////

class ATKeyboardDialog : public VDDialogFrameW32 {
public:
	ATKeyboardDialog();

protected:
	void OnDataExchange(bool write);
};

ATKeyboardDialog::ATKeyboardDialog()
	: VDDialogFrameW32(IDD_KEYBOARD)
{
}

void ATKeyboardDialog::OnDataExchange(bool write) {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();

	if (write) {
		g_rawKeys = IsButtonChecked(IDC_KPMODE_RAW);
	} else {
		CheckButton(g_rawKeys ? IDC_KPMODE_RAW : IDC_KPMODE_COOKED, true);
	}
}

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
				msg += L"\tCartridge\n";
				break;

			case kATStorageId_Disk:
				msg.append_sprintf(L"\tDisk (D%u:)\n", unit + 1);
				break;
		}
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
			if (mode == kATHardwareMode_5200 || mode == kATHardwareMode_800)
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

	// If we are in 5200 mode, we must not be in PAL
	if (mode == kATHardwareMode_5200 && g_sim.IsPALMode())
		g_sim.SetPALMode(false);

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

	switch(g_sim.GetKernelMode()) {
		case kATKernelMode_5200:
		case kATKernelMode_5200_LLE:
			if (mode != kATMemoryMode_16K)
				return;
			break;

		case kATKernelMode_XL:
		case kATKernelMode_Other:
			if (mode == kATMemoryMode_48K || mode == kATMemoryMode_52K)
				return;
			break;
	}

	g_sim.SetMemoryMode(mode);
	g_sim.ColdReset();
}

void DoLoad(VDGUIHandle h, const wchar_t *path, bool vrw, bool rw, int cartmapper) {
	ATCartLoadContext cartctx = {};
	cartctx.mbReturnOnUnknownMapper = true;

	if (cartmapper) {
		cartctx.mbReturnOnUnknownMapper = false;
		cartctx.mCartMapper = cartmapper;
	}

	ATLoadContext ctx;
	ctx.mpCartLoadContext = &cartctx;
	if (!g_sim.Load(path, vrw, rw, &ctx)) {
		int mapper = ATUIShowDialogCartridgeMapper(h, cartctx.mCartSize, cartctx.mbMayBe2600);

		if (mapper >= 0) {
			cartctx.mbReturnOnUnknownMapper = false;
			cartctx.mCartMapper = mapper;

			g_sim.Load(path, vrw, rw, &ctx);
		}
	}
}

void OnCommandOpen(bool forceColdBoot) {
	if (forceColdBoot && !ATUIConfirmDiscardAllStorage((VDGUIHandle)g_hwnd, L"OK to discard?"))
		return;

	VDStringW fn(VDGetLoadFileName('load', (VDGUIHandle)g_hwnd, L"Load disk, cassette, cartridge, or program image",
		L"All supported types\0*.atr;*.xfd;*.dcm;*.pro;*.atx;*.xex;*.obx;*.com;*.car;*.rom;*.a52;*.bin;*.cas;*.wav;*.zip\0"
		L"Atari program (*.xex,*.obx,*.com)\0*.xex;*.obx;*.com\0"
		L"Atari disk image (*.atr,*.xfd,*.dcm)\0*.atr;*.xfd;*.dcm\0"
		L"Protected disk image (*.pro)\0*.pro\0"
		L"VAPI disk image (*.atx)\0*.atx\0"
		L"Cartridge (*.rom,*.bin,*.a52,*.car)\0*.rom;*.bin;*.a52;*.car\0"
		L"Cassette tape (*.cas,*.wav)\0*.cas;*.wav\0"
		L"Zip archive (*.zip)\0*.zip\0"
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
		} catch(const MyError& e) {
			e.post(g_hwnd, "Altirra Error");
		}
	}
}

void ATSetFullscreen(bool fs) {
	if (!g_sim.IsRunning())
		fs = false;

	if (fs == g_fullscreen)
		return;

	ATUIPane *dispPane = ATGetUIPane(kATUIPaneId_Display);
	ATFrameWindow *frame = NULL;

	if (dispPane) {
		HWND parent = ::GetParent(dispPane->GetHandleW32());

		if (parent) {
			frame = ATFrameWindow::GetFrameWindow(parent);

			if (frame)
				frame->SetFullScreen(fs);
		}
	}

	DWORD style = GetWindowLong(g_hwnd, GWL_STYLE);
	if (fs) {
		SetMenu(g_hwnd, NULL);
		SetWindowLong(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
		SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOZORDER);
		SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
		ShowWindow(g_hwnd, SW_SHOWNORMAL);
		ShowWindow(g_hwnd, SW_MAXIMIZE);
		g_fullscreen = true;
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(true);
		g_sim.SetFrameSkipEnabled(false);
	} else {
		if (g_pDisplay)
			g_pDisplay->SetFullScreen(false);
		SetWindowLong(g_hwnd, GWL_STYLE, (style | WS_OVERLAPPEDWINDOW) & ~WS_POPUP);
		SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED|SWP_NOACTIVATE);
		ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
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
	if (!g_pDisplay)
		return;

	switch(g_pDisplay->GetFilterMode()) {
		case IVDVideoDisplay::kFilterPoint:
			g_dispFilterMode = IVDVideoDisplay::kFilterBilinear;
			break;
		case IVDVideoDisplay::kFilterBilinear:
			g_dispFilterMode = IVDVideoDisplay::kFilterBicubic;
			break;
		case IVDVideoDisplay::kFilterBicubic:
			g_dispFilterMode = IVDVideoDisplay::kFilterAnySuitable;
			break;
		case IVDVideoDisplay::kFilterAnySuitable:
			g_dispFilterMode = IVDVideoDisplay::kFilterPoint;
			break;
	}

	g_pDisplay->SetFilterMode(g_dispFilterMode);
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

void StopRecording() {
	if (g_pAudioWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(NULL);
		g_pAudioWriter = NULL;
	}

	if (g_pVideoWriter) {
		g_pVideoWriter->Shutdown();
		g_sim.GetAudioOutput()->SetAudioTap(NULL);
		g_sim.GetGTIA().SetVideoTap(NULL);
		g_pVideoWriter = NULL;
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
					ATSaveStateReader reader(g_quickSave.data(), g_quickSave.size());

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
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				VDStringW fn(VDGetLoadFileName('cart', (VDGUIHandle)g_hwnd, L"Load cartridge",
					L"All supported types\0*.bin;*.car;*.rom;*.a52;*.zip\0"
					L"Cartridge image (*.bin,*.car,*.rom,*.a52)\0*.bin;*.car;*.rom;*.a52\0"
					L"Zip archive (*.zip)\0*.zip\0"
					L"All files\0*.*\0",

					L"bin"));

				if (!fn.empty()) {
					try {
						ATCartLoadContext cartctx = {};
						cartctx.mbReturnOnUnknownMapper = true;

						if (!g_sim.LoadCartridge(fn.c_str(), &cartctx)) {
							int mapper = ATUIShowDialogCartridgeMapper((VDGUIHandle)g_hwnd, cartctx.mCartSize, cartctx.mbMayBe2600);

							if (mapper >= 0) {
								cartctx.mbReturnOnUnknownMapper = false;
								cartctx.mCartMapper = mapper;

								g_sim.LoadCartridge(fn.c_str(), &cartctx);
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
			if (ATUIConfirmDiscardCartridge((VDGUIHandle)g_hwnd)) {
				if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
					g_sim.LoadCartridge5200Default();
				else
					g_sim.UnloadCartridge();
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
				g_sim.LoadCartridgeFlash8Mb();
				g_sim.ColdReset();
			}
			return true;

		case ID_FILE_SAVECARTRIDGE:
			try {
				ATCartridgeEmulator *cart = g_sim.GetCartridge();
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

		case ID_FILE_EXIT:
			PostQuitMessage(0);
			return true;

		case ID_CASSETTE_LOAD:
			{
				VDStringW fn(VDGetLoadFileName('cass', (VDGUIHandle)g_hwnd, L"Load cassette tape", L"All supported types\0*.wav;*.cas\0Atari cassette image (*.cas)\0*.cas\0Waveform audio (*.wav)\0*.wav\0All files\0*.*\0", L"wav"));

				if (!fn.empty()) {
					try {
						g_sim.GetCassette().Load(fn.c_str());
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

		case ID_FILTERMODE_NEXTMODE:
			OnCommandFilterModeNext();
			return true;

		case ID_FILTERMODE_POINT:
			g_dispFilterMode = IVDVideoDisplay::kFilterPoint;
			if (g_pDisplay)
				g_pDisplay->SetFilterMode(IVDVideoDisplay::kFilterPoint);
			return true;

		case ID_FILTERMODE_BILINEAR:
			g_dispFilterMode = IVDVideoDisplay::kFilterBilinear;
			if (g_pDisplay)
				g_pDisplay->SetFilterMode(IVDVideoDisplay::kFilterBilinear);
			return true;

		case ID_FILTERMODE_BICUBIC:
			g_dispFilterMode = IVDVideoDisplay::kFilterBicubic;
			if (g_pDisplay)
				g_pDisplay->SetFilterMode(IVDVideoDisplay::kFilterBicubic);
			return true;

		case ID_FILTERMODE_ANYSUITABLE:
			g_dispFilterMode = IVDVideoDisplay::kFilterAnySuitable;
			if (g_pDisplay)
				g_pDisplay->SetFilterMode(IVDVideoDisplay::kFilterAnySuitable);
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
				ATActivateUIPane(kATUIPaneId_Memory, true);
			return true;

		case ID_VIEW_COPYFRAMETOCLIPBOARD:
			{
				const VDPixmap *frame = g_sim.GetGTIA().GetLastFrameBuffer();

				if (frame)
					ATCopyFrameToClipboard(g_hwnd, *frame);
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

		case ID_MEMORYSIZE_16K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_16K);
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

		case ID_MEMORYSIZE_576K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_576K);
			return true;

		case ID_MEMORYSIZE_1088K:
			ATUISwitchMemoryMode((VDGUIHandle)g_hwnd, kATMemoryMode_1088K);
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

		case ID_SYSTEM_PAL:
			// Don't allow switching to PAL in 5200 mode!
			if (g_sim.GetHardwareMode() == kATHardwareMode_5200 && !g_sim.IsPALMode())
				return true;

			g_sim.SetPALMode(!g_sim.IsPALMode());
			{
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->OnSize();
			}
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
			g_sim.SetTurboModeEnabled(!g_sim.IsTurboModeEnabled());
			return true;

		case ID_SYSTEM_CPUOPTIONS:
			ATUIShowCPUOptionsDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_SYSTEM_FPPATCH:
			g_sim.SetFPPatchEnabled(!g_sim.IsFPPatchEnabled());
			return true;

		case ID_SYSTEM_HARDDISK:
			{
				IATHardDiskEmulator *hd = g_sim.GetHardDisk();

				if (hd)
					ATUIShowHardDiskDialog((VDGUIHandle)g_hwnd, hd);
			}
			return true;

		case ID_VIDEO_STANDARDVIDEO:
			g_enhancedText = 0;
			return true;

		case ID_VIDEO_ENHANCEDTEXT:
			g_enhancedText = 1;
			ATSetFullscreen(false);
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
			for(int i=0; i<4; ++i) {
				ATDiskEmulator& disk = g_sim.GetDiskDrive(i);
				disk.SetAccurateSectorTimingEnabled(!disk.IsAccurateSectorTimingEnabled());
			}
			return true;

		case ID_DISKDRIVE_SHOWSECTORCOUNTER:
			g_sim.SetDiskSectorCounterEnabled(!g_sim.IsDiskSectorCounterEnabled());
			return true;

		case ID_AUDIO_STEREO:
			g_sim.SetDualPokeysEnabled(!g_sim.IsDualPokeysEnabled());
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

		case ID_INPUT_CAPTUREMOUSE:
			if (g_mouseClipped) {
				::ClipCursor(NULL);
				g_mouseClipped = false;
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
			{
				ATKeyboardDialog dlg;
				dlg.ShowDialog((VDGUIHandle)g_hwnd);
			}
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

		case ID_VERIFIER_ENABLED:
			g_sim.SetVerifierEnabled(!g_sim.IsVerifierEnabled());
			return true;

		case ID_RECORD_STOPRECORDING:
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
					ATUIDialogVideoRecording dlg(g_sim.IsPALMode());

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

							bool pal = g_sim.IsPALMode();
							VDFraction frameRate = g_sim.IsPALMode() ? VDFraction(1773447, 114*312) : VDFraction(3579545, 2*114*262);
							double samplingRate = pal ? 1773447.0 / 28.0 : 3579545.0 / 56.0;

							switch(dlg.GetFrameRate()) {
								case kATVideoRecordingFrameRate_NTSCRatio:
									if (pal) {
										samplingRate = samplingRate * (50000.0 / 1001.0) / frameRate.asDouble();
										frameRate = VDFraction(50000, 1001);
									} else {
										samplingRate = samplingRate * (60000.0 / 1001.0) / frameRate.asDouble();
										frameRate = VDFraction(60000, 1001);
									}
									break;

								case kATVideoRecordingFrameRate_Integral:
									if (pal) {
										samplingRate = samplingRate * 50.0 / frameRate.asDouble();
										frameRate = VDFraction(50, 1);
									} else {
										samplingRate = samplingRate * 60.0 / frameRate.asDouble();
										frameRate = VDFraction(60, 1);
									}
									break;
							}

							g_pVideoWriter->Init(s.c_str(), dlg.GetEncoding(), w, h, frameRate, rgb32 ? NULL : palette, samplingRate, g_sim.IsDualPokeysEnabled(), pal ? 1773447.0f : 1789772.5f, dlg.GetHalfRate(), g_sim.GetUIRenderer());

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

		case ID_HELP_CONTENTS:
			ATShowHelp(g_hwnd, NULL);
			return true;

		case ID_HELP_ABOUT:
			ATAboutDialog().ShowDialog((VDGUIHandle)g_hwnd);
			return true;

		case ID_HELP_CHANGELOG:
			ATShowChangeLog((VDGUIHandle)g_hwnd);
			return true;

		default:
			if ((id - ID_INPUT_PORT1_NONE) < 100) {
				g_portMenus[0].HandleCommand(id);
				return true;
			}

			if ((id - ID_INPUT_PORT2_NONE) < 100) {
				g_portMenus[1].HandleCommand(id);
				return true;
			}

			if ((id - ID_INPUT_PORT3_NONE) < 100) {
				g_portMenus[2].HandleCommand(id);
				return true;
			}

			if ((id - ID_INPUT_PORT4_NONE) < 100) {
				g_portMenus[3].HandleCommand(id);
				return true;
			}

			break;
	}
	return false;
}

void OnInitMenu(HMENU hmenu) {
	const bool cartAttached = g_sim.IsCartridgeAttached();
	const bool is5200 = g_sim.GetHardwareMode() == kATHardwareMode_5200;

	VDEnableMenuItemByCommandW32(hmenu, ID_CASSETTE_UNLOAD, g_sim.GetCassette().IsLoaded());
	VDEnableMenuItemByCommandW32(hmenu, ID_FILE_DETACHCARTRIDGE, cartAttached);
	VDEnableMenuItemByCommandW32(hmenu, ID_FILE_SAVECARTRIDGE, cartAttached);

	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_PAL, g_sim.IsPALMode());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_PRINTER, g_sim.IsPrinterEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_RTIME8, g_sim.IsRTime8Enabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_SERIALPORTS, g_sim.IsRS232Enabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_VIEW_SHOWFPS, g_showFps);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	ATGTIAEmulator::ArtifactMode artmode = gtia.GetArtifactingMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_NOARTIFACTING, artmode == ATGTIAEmulator::kArtifactNone);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_NTSCARTIFACTING, artmode == ATGTIAEmulator::kArtifactNTSC);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_NTSCARTIFACTINGHI, artmode == ATGTIAEmulator::kArtifactNTSCHi);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_PALARTIFACTING, artmode == ATGTIAEmulator::kArtifactPAL);
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_FRAMEBLENDING, gtia.IsBlendModeEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_INTERLACE, gtia.IsInterlaceEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_VBXE, g_sim.GetVBXE() != NULL);
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_VBXESHAREDMEMORY, g_sim.IsVBXESharedMemoryEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_VIDEO_VBXEALTPAGE, g_sim.IsVBXEAltPageEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_WARPSPEED, g_sim.IsTurboModeEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_SYSTEM_PAUSEWHENINACTIVE, g_pauseInactive);
	VDCheckMenuItemByCommandW32(hmenu, ID_VIEW_VSYNC, gtia.IsVsyncEnabled());

	if (g_pDisplay) {
		IVDVideoDisplay::FilterMode filterMode = g_pDisplay->GetFilterMode();
		VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_POINT, filterMode == IVDVideoDisplay::kFilterPoint);
		VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_BILINEAR, filterMode == IVDVideoDisplay::kFilterBilinear);
		VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_BICUBIC, filterMode == IVDVideoDisplay::kFilterBicubic);
		VDCheckRadioMenuItemByCommandW32(hmenu, ID_FILTERMODE_ANYSUITABLE, filterMode == IVDVideoDisplay::kFilterAnySuitable);
	}

	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_FITTOWINDOW, g_displayStretchMode == kATDisplayStretchMode_Unconstrained);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_PRESERVEASPECTRATIO, g_displayStretchMode == kATDisplayStretchMode_PreserveAspectRatio);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_SQUAREPIXELS, g_displayStretchMode == kATDisplayStretchMode_SquarePixels);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_STRETCHMODE_INTEGRALSQUAREPIXELS, g_displayStretchMode == kATDisplayStretchMode_Integral);

	ATGTIAEmulator::OverscanMode ovmode = gtia.GetOverscanMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_NORMAL, ovmode == ATGTIAEmulator::kOverscanNormal);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_EXTENDED, ovmode == ATGTIAEmulator::kOverscanExtended);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_OVERSCAN_FULL, ovmode == ATGTIAEmulator::kOverscanFull);

	VDCheckMenuItemByCommandW32(hmenu, ID_OVERSCAN_PALEXTENDED, g_sim.GetGTIA().IsOverscanPALExtended());

	ATHardwareMode hardwareMode = g_sim.GetHardwareMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_800, hardwareMode == kATHardwareMode_800);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_800XL, hardwareMode == kATHardwareMode_800XL);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_HARDWARE_5200, hardwareMode == kATHardwareMode_5200);

	const ATKernelMode kernelMode = g_sim.GetKernelMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_DEFAULT, kernelMode == kATKernelMode_Default);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_OSA, kernelMode == kATKernelMode_OSA);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_OSB, kernelMode == kATKernelMode_OSB);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_800XL, kernelMode == kATKernelMode_XL);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_OTHER, kernelMode == kATKernelMode_Other);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_LLE, kernelMode == kATKernelMode_LLE);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_HLE, kernelMode == kATKernelMode_HLE);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_5200, kernelMode == kATKernelMode_5200);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_FIRMWARE_5200LLE, kernelMode == kATKernelMode_5200_LLE);

	// Neither BASIC nor the doesn't exist on 5200
	VDEnableMenuItemByCommandW32(hmenu, ID_FIRMWARE_BASIC, !is5200);
	VDCheckMenuItemByCommandW32(hmenu, ID_FIRMWARE_BASIC, g_sim.IsBASICEnabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_FIRMWARE_FASTBOOT, g_sim.IsFastBootEnabled());

	const ATMemoryMode memMode = g_sim.GetMemoryMode();
	const bool xlkernel = kernelMode == kATKernelMode_Other || kernelMode == kATKernelMode_XL;
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_48K, !is5200 && !xlkernel);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_52K, !is5200 && !xlkernel);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_64K, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_128K, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_320K, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_576K, !is5200);
	VDEnableMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_1088K, !is5200);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_16K, memMode == kATMemoryMode_16K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_48K, memMode == kATMemoryMode_48K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_52K, memMode == kATMemoryMode_52K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_64K, memMode == kATMemoryMode_64K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_128K, memMode == kATMemoryMode_128K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_320K, memMode == kATMemoryMode_320K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_576K, memMode == kATMemoryMode_576K);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_MEMORYSIZE_1088K, memMode == kATMemoryMode_1088K);

	VDCheckMenuItemByCommandW32(hmenu, ID_MEMORY_RANDOMIZE, g_sim.IsRandomFillEnabled());

	// PAL 5200 is bogus
	VDEnableMenuItemByCommandW32(hmenu, ID_SYSTEM_PAL, !is5200);

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

	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_FULLSCREEN, g_enhancedText == 0);

	bool frameAvailable = gtia.GetLastFrameBuffer() != NULL;
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_COPYFRAMETOCLIPBOARD, frameAvailable);
	VDEnableMenuItemByCommandW32(hmenu, ID_VIEW_SAVEFRAME, frameAvailable);

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
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_BREAK, running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_STEPINTO, !running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_STEPOUT, !running);
	VDEnableMenuItemByCommandW32(hmenu, ID_DEBUG_STEPOVER, !running);

	VDCheckMenuItemByCommandW32(hmenu, ID_VERIFIER_ENABLED, g_sim.IsVerifierEnabled());

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_ENABLED, disk.IsEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_SIOPATCH, g_sim.IsDiskSIOPatchEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_ACCURATESECTORTIMING, disk.IsAccurateSectorTimingEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_DISKDRIVE_SHOWSECTORCOUNTER, g_sim.IsDiskSectorCounterEnabled());

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	ATPokeyEmulator::SerialBurstMode burstMode = pokey.GetSerialBurstMode();
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_DISKDRIVE_BURSTDISABLED, burstMode == ATPokeyEmulator::kSerialBurstMode_Disabled);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_DISKDRIVE_BURSTSTANDARD, burstMode == ATPokeyEmulator::kSerialBurstMode_Standard);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_DISKDRIVE_BURSTPOLLED, burstMode == ATPokeyEmulator::kSerialBurstMode_Polled);

	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_STEREO, g_sim.IsDualPokeysEnabled());
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_NONLINEARMIXING, pokey.IsNonlinearMixingEnabled());

	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL1, pokey.IsChannelEnabled(0));
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL2, pokey.IsChannelEnabled(1));
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL3, pokey.IsChannelEnabled(2));
	VDCheckMenuItemByCommandW32(hmenu, ID_AUDIO_CHANNEL4, pokey.IsChannelEnabled(3));

	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_STANDARDVIDEO, g_enhancedText == 0);
	VDCheckRadioMenuItemByCommandW32(hmenu, ID_VIDEO_ENHANCEDTEXT, g_enhancedText == 1);

	bool recording = g_pAudioWriter != NULL || g_pVideoWriter != NULL;
	VDEnableMenuItemByCommandW32(hmenu, ID_RECORD_STOPRECORDING, recording);
	VDEnableMenuItemByCommandW32(hmenu, ID_RECORD_RECORDRAWAUDIO, !recording);
	VDCheckMenuItemByCommandW32(hmenu, ID_RECORD_RECORDRAWAUDIO, g_pAudioWriter != NULL);
	VDEnableMenuItemByCommandW32(hmenu, ID_RECORD_RECORDVIDEO, !recording);
	VDCheckMenuItemByCommandW32(hmenu, ID_RECORD_RECORDVIDEO, g_pVideoWriter != NULL);

	ATUpdatePortMenus();
}

void OnActivateApp(HWND hwnd, WPARAM wParam) {
	g_winActive = (wParam != 0);

	if (!wParam) {
		if (g_mouseClipped) {
			ClipCursor(NULL);
			g_mouseClipped = false;
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
			if (MapVirtualKey((lParam >> 16) & 0xff, 3) == VK_RSHIFT)
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

bool OnKeyDown(HWND hwnd, WPARAM wParam, LPARAM lParam) {
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

	const bool isRepeat = (lParam & 0xffff) > 0;
	const bool shift = (GetKeyState(VK_SHIFT) < 0);
	const bool ctrl = (GetKeyState(VK_CONTROL) < 0);
	const uint8 ctrlmod = (ctrl ? 0x80 : 0x00);
	const uint8 shiftmod = (shift ? 0x40 : 0x00);

	uint8 scanCode;
	if (ATUIGetScanCodeForVirtualKey(key, alt, ctrl, shift, scanCode)) {
		ProcessVirtKey(key, scanCode, isRepeat);
		return true;
	}

	const uint8 modifier = ctrlmod + shiftmod;

	switch(key) {
		case VK_CAPITAL:
			// drop injected CAPS LOCK keys
			if (lParam & 0x00ff0000)
				ProcessVirtKey(key, 0x3C + modifier, isRepeat);
			return true;

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
				g_sim.SetTurboModeEnabled(true);
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
				g_sim.SetPALMode(!g_sim.IsPALMode());
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

		case 'C':
			if (alt && shift) {
				OnCommand(ID_VIEW_COPYFRAMETOCLIPBOARD);
				return true;
			}

			break;

		case VK_RETURN:
			if (alt) {
				OnCommandToggleFullScreen();
				return true;
			}
			break;
	}

	g_lastVkeyPressed = key;
	return false;
}

bool OnKeyUp(HWND hwnd, WPARAM wParam, LPARAM lParam) {
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

	switch(key) {
		case VK_CAPITAL:
			if (GetKeyState(VK_CAPITAL) & 1) {
				// force caps lock back off
				keybd_event(VK_CAPITAL, 0, 0, NULL);
			}
			ProcessRawKeyUp(key);
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
			g_sim.SetTurboModeEnabled(false);
			return true;
	}

	ProcessRawKeyUp(key);

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

	if (g_rawKeys)
		g_sim.GetPokey().PushRawKey(keycode);
	else
		g_sim.GetPokey().PushKey(keycode, repeat);
}

void ProcessRawKeyUp(int vkey) {
	if (g_lastVkeySent == vkey)
		g_sim.GetPokey().ReleaseRawKey();
}

void OnChar(HWND hwnd, WPARAM wParam, LPARAM lParam) {
	int code = LOWORD(wParam);
	if (code <= 0 || code > 127)
		return;

	const bool repeat = (lParam & 0x40000000) != 0;

	if (g_rawKeys) {
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

	void OnSize();

protected:
	LRESULT WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

	bool OnCreate();
	void OnDestroy();
	void WarpCapturedMouse();
	void UpdateTextDisplay(bool enabled);
	void UpdateTextModeFont();
	void UpdateMousePosition(int x, int y);

	HWND	mhwndDisplay;
	IVDVideoDisplay *mpDisplay;
	HCURSOR	mhcurTarget;
	int		mLastHoverX;
	int		mLastHoverY;
	int		mLastTrackMouseX;
	int		mLastTrackMouseY;
	int		mWidth;
	int		mHeight;

	vdrect32	mDisplayRect;

	bool	mbTextModeEnabled;

	vdautoptr<IATUIEnhancedTextEngine> mpEnhTextEngine;
};

ATDisplayPane::ATDisplayPane()
	: mhwndDisplay(NULL)
	, mpDisplay(NULL)
	, mhcurTarget(NULL)
	, mLastHoverX(INT_MIN)
	, mLastHoverY(INT_MIN)
	, mLastTrackMouseX(0)
	, mLastTrackMouseY(0)
	, mDisplayRect(0,0,0,0)
	, mbTextModeEnabled(false)
	, mWidth(0)
	, mHeight(0)
{
	mPreferredDockCode = kATContainerDockCenter;
}

ATDisplayPane::~ATDisplayPane() {
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

					::ClipCursor(NULL);
					return true;
				}
			}
			// fall through
		case MYWM_PREKEYDOWN:
			if (OnKeyDown(mhwnd, wParam, lParam))
				return true;
			return false;

		case MYWM_PRESYSKEYUP:
		case MYWM_PREKEYUP:
			if (OnKeyUp(mhwnd, wParam, lParam))
				return true;
			return false;

		case WM_SIZE:
			mWidth = LOWORD(lParam);
			mHeight = HIWORD(lParam);
			break;

		case WM_CHAR:
			OnChar(mhwnd, wParam, lParam);
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
			{
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

					if (im->IsMouseMapped())
						im->OnButtonUp(0, kATInputCode_MouseRMB);
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
				}
			}
			break;

		case WM_SETCURSOR:
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
			}
			break;

		case WM_MOUSEMOVE:
			{
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

			if (g_mouseClipped) {
				g_mouseClipped = false;

				::ClipCursor(NULL);
			}
			break;

		case WM_CAPTURECHANGED:
			g_mouseCaptured = false;
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
	mpDisplay->SetFilterMode(g_dispFilterMode);
	mpDisplay->SetAccelerationMode(IVDVideoDisplay::kAccelResetInForeground);

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
			}
		} else {
			::SetCapture(mhwnd);
			::SetCursor(NULL);

			g_mouseCaptured = true;

			WarpCapturedMouse();
		}
	}
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
			vdrect32 rd(r.left, r.top, r.right, r.bottom);
			sint32 w = rd.width();
			sint32 h = rd.height();
			int sw;
			int sh;

			g_sim.GetGTIA().GetFrameSize(sw, sh);

			if (w && h) {
				if (g_displayStretchMode == kATDisplayStretchMode_PreserveAspectRatio) {
					const float kRawFrameAspect = (float)sw / (float)sh;
					const float kDesiredAspect = (g_sim.IsPALMode() ? 1.03964f : 0.857141f) * kRawFrameAspect;
					float aspect = (float)w / (float)h;

					if (aspect > kDesiredAspect) {
						sint32 w2 = (sint32)(0.5f + h * kDesiredAspect);

						rd.left = (w - w2) >> 1;
						rd.right = rd.left + w2;
					} else {
						sint32 h2 = (sint32)(0.5f + w / kDesiredAspect);

						rd.top = (h - h2) >> 1;
						rd.bottom = rd.top + h2;
					}
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

	if (mbTextModeEnabled)
		InvalidateRect(mhwnd, NULL, TRUE);
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

	if (!mDisplayRect.contains(vdpoint32(x, y)))
		return;

	x -= mDisplayRect.left;
	y -= mDisplayRect.top;

	int xlo = 44;
	int xhi = 212;
	int ylo = 8;
	int yhi = 248;

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	bool palext = g_sim.IsPALMode() && gtia.IsOverscanPALExtended();
	if (palext) {
		ylo -= 25;
		yhi += 25;
	}

	switch(gtia.GetOverscanMode()) {
		case ATGTIAEmulator::kOverscanFull:
			xlo = 0;
			xhi = 228;
			ylo = 0;
			yhi = 262;

			if (palext) {
				ylo = -25;
				yhi = 287;
			}
			break;

		case ATGTIAEmulator::kOverscanExtended:
			xlo = 34;
			xhi = 222;
			break;
	}

	// map position to cycles
	float xc = (float)xlo + ((float)x + 0.5f) * ((float)xhi - (float)xlo) / (float)mDisplayRect.width() - 0.5f;
	float yc = (float)ylo + ((float)y + 0.5f) * ((float)yhi - (float)ylo) / (float)mDisplayRect.height() - 0.5f;

	// map cycles to normalized position (note that this must match the light pen
	// computations)
	float xn = (xc - 128.0f) * (65536.0f / 94.0f);
	float yn = (yc - 128.0f) * (65536.0f / 188.0f);

	im->SetMouseBeamPos(VDRoundToInt(xn), VDRoundToInt(yn));
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

			ATActivateUIPane(kATUIPaneId_Display, true);
			ATUIRegisterDragDropHandler((VDGUIHandle)mhwnd);
			return 0;

		case WM_CLOSE:
			if (!ATUIConfirmDiscardAllStorage((VDGUIHandle)mhwnd, L"Exit without saving?", true))
				return 0;

			break;

		case WM_DESTROY:
			ATSavePaneLayout(NULL);
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
	}

	return ATContainerWindow::WndProc(msg, wParam, lParam);
}

void ATMainWindow::OnCopyData(HWND hwndReply, const COPYDATASTRUCT& cds) {
	if (cds.dwData != 0xA7000000 || !cds.cbData || !cds.lpData)
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
		cds2.cbData = buf.size();
		cds2.lpData = buf.data();

		::SendMessage(hwndReply, WM_COPYDATA, (WPARAM)mhwnd, (LPARAM)&cds2);
	}
}

class D3D9Lock : public VDD3D9Client {
public:
	D3D9Lock() : mpMgr(NULL) {}

	void Lock() {
		if (!mpMgr)
			mpMgr = VDInitDirect3D9(this, NULL);
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
	bool bootrw = false;
	bool bootvrw = false;
	bool unloaded = false;
	VDStringA keysToType;
	int cartmapper = 0;

	try {
		ATDiskEmulator& disk = g_sim.GetDiskDrive(0);

		if (cmdLine.FindAndRemoveSwitch(L"ntsc"))
			g_sim.SetPALMode(false);

		if (cmdLine.FindAndRemoveSwitch(L"pal"))
			g_sim.SetPALMode(true);

		if (cmdLine.FindAndRemoveSwitch(L"burstio"))
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Standard);
		if (cmdLine.FindAndRemoveSwitch(L"burstiopolled"))
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Polled);
		else if (cmdLine.FindAndRemoveSwitch(L"noburstio"))
			g_sim.GetPokey().SetSerialBurstMode(ATPokeyEmulator::kSerialBurstMode_Disabled);

		if (cmdLine.FindAndRemoveSwitch(L"siopatch")) {
			g_sim.SetDiskSIOPatchEnabled(true);
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

		if (cmdLine.FindAndRemoveSwitch(L"accuratedisk"))
			disk.SetAccurateSectorTimingEnabled(true);
		else if (cmdLine.FindAndRemoveSwitch(L"noaccuratedisk"))
			disk.SetAccurateSectorTimingEnabled(false);

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
		if (cmdLine.FindAndRemoveSwitch(L"hardware", arg)) {
			if (!_wcsicmp(arg, L"800"))
				g_sim.SetHardwareMode(kATHardwareMode_800);
			else if (!_wcsicmp(arg, L"800xl"))
				g_sim.SetHardwareMode(kATHardwareMode_800XL);
			else if (!_wcsicmp(arg, L"5200"))
				g_sim.SetHardwareMode(kATHardwareMode_5200);
			else
				throw MyError("Command line error: Invalid hardware mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"kernel", arg)) {
			if (!_wcsicmp(arg, L"default"))
				g_sim.SetKernelMode(kATKernelMode_Default);
			else if (!_wcsicmp(arg, L"osa"))
				g_sim.SetKernelMode(kATKernelMode_OSA);
			else if (!_wcsicmp(arg, L"osb"))
				g_sim.SetKernelMode(kATKernelMode_OSB);
			else if (!_wcsicmp(arg, L"xl"))
				g_sim.SetKernelMode(kATKernelMode_XL);
			else if (!_wcsicmp(arg, L"lle"))
				g_sim.SetKernelMode(kATKernelMode_LLE);
			else if (!_wcsicmp(arg, L"hle"))
				g_sim.SetKernelMode(kATKernelMode_HLE);
			else if (!_wcsicmp(arg, L"other"))
				g_sim.SetKernelMode(kATKernelMode_Other);
			else if (!_wcsicmp(arg, L"5200"))
				g_sim.SetKernelMode(kATKernelMode_5200);
			else if (!_wcsicmp(arg, L"5200lle"))
				g_sim.SetKernelMode(kATKernelMode_5200_LLE);
			else
				throw MyError("Command line error: Invalid kernel mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"memsize", arg)) {
			if (!_wcsicmp(arg, L"16K"))
				g_sim.SetMemoryMode(kATMemoryMode_16K);
			else if (!_wcsicmp(arg, L"48K"))
				g_sim.SetMemoryMode(kATMemoryMode_48K);
			else if (!_wcsicmp(arg, L"52K"))
				g_sim.SetMemoryMode(kATMemoryMode_52K);
			else if (!_wcsicmp(arg, L"64K"))
				g_sim.SetMemoryMode(kATMemoryMode_64K);
			else if (!_wcsicmp(arg, L"128K"))
				g_sim.SetMemoryMode(kATMemoryMode_128K);
			else if (!_wcsicmp(arg, L"320K"))
				g_sim.SetMemoryMode(kATMemoryMode_320K);
			else if (!_wcsicmp(arg, L"576K"))
				g_sim.SetMemoryMode(kATMemoryMode_576K);
			else if (!_wcsicmp(arg, L"1088K"))
				g_sim.SetMemoryMode(kATMemoryMode_1088K);
			else
				throw MyError("Command line error: Invalid memory mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"artifact", arg)) {
			if (!_wcsicmp(arg, L"none"))
				g_sim.GetGTIA().SetArtifactingMode(ATGTIAEmulator::kArtifactNone);
			else if (!_wcsicmp(arg, L"ntsc"))
				g_sim.GetGTIA().SetArtifactingMode(ATGTIAEmulator::kArtifactNTSC);
			else if (!_wcsicmp(arg, L"ntschi"))
				g_sim.GetGTIA().SetArtifactingMode(ATGTIAEmulator::kArtifactNTSCHi);
			else if (!_wcsicmp(arg, L"pal"))
				g_sim.GetGTIA().SetArtifactingMode(ATGTIAEmulator::kArtifactPAL);
			else
				throw MyError("Command line error: Invalid hardware mode '%ls'", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"vsync"))
			g_sim.GetGTIA().SetVsyncEnabled(true);
		else if (cmdLine.FindAndRemoveSwitch(L"novsync"))
			g_sim.GetGTIA().SetVsyncEnabled(false);

		if (cmdLine.FindAndRemoveSwitch(L"debug"))
			debugMode = true;

		if (cmdLine.FindAndRemoveSwitch(L"bootrw")) {
			bootrw = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"bootvrw")) {
			bootvrw = true;
		}

		if (cmdLine.FindAndRemoveSwitch(L"type", arg)) {
			keysToType += VDTextWToA(arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"nohdpath", arg)) {
			IATHardDiskEmulator *hd = g_sim.GetHardDisk();

			if (hd && hd->IsEnabled()) {
				hd->SetEnabled(false);
				coldReset = true;
			}
		}

		if (cmdLine.FindAndRemoveSwitch(L"hdpath", arg)) {
			IATHardDiskEmulator *hd = g_sim.GetHardDisk();
			if (hd) {
				if (!hd->IsEnabled())
					hd->SetEnabled(true);

				hd->SetReadOnly(true);
				hd->SetBasePath(arg);

				coldReset = true;
			}
		}

		if (cmdLine.FindAndRemoveSwitch(L"hdpathrw", arg)) {
			IATHardDiskEmulator *hd = g_sim.GetHardDisk();
			if (hd) {
				if (!hd->IsEnabled())
					hd->SetEnabled(true);

				hd->SetReadOnly(false);
				hd->SetBasePath(arg);

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

			bool useD5xx = false;
			if (iorange == L"d5xx")
				useD5xx = true;
			else if (iorange != L"d1xx")
				throw MyError("Invalid IDE I/O range: %.*ls", iorange.size(), iorange.data());

			bool write = false;
			bool fast = false;

			size_t modelen = mode.size();
			if (modelen > 5 && !memcmp(mode.data() + modelen - 4, L"-fast", 5)) {
				fast = true;
				mode = mode.subspan(0, modelen - 5);
			}

			if (mode == L"rw")
				write = true;
			else if (mode != L"ro")
				throw MyError("Invalid IDE mount mode: %.*ls", mode.size(), mode.data());

			unsigned cylval;
			unsigned headsval;
			unsigned sptval;
			char dummy;
			if (1 != swscanf(VDStringW(cylinders).c_str(), L"%u%c", &cylval, &dummy) || !cylval || cylval > 65536 ||
				1 != swscanf(VDStringW(heads).c_str(), L"%u%c", &headsval, &dummy) || !headsval || headsval > 16 ||
				1 != swscanf(VDStringW(spt).c_str(), L"%u%c", &sptval, &dummy) || !sptval || sptval > 63
				)
				throw MyError("Invalid IDE image geometry: %.*ls / %.*ls / %.*ls"
					, cylinders.size(), cylinders.data()
					, heads.size(), heads.data()
					, spt.size(), spt.data()
				);

			g_sim.LoadIDE(useD5xx, write, fast, cylval, headsval, sptval, VDStringW(tokenizer).c_str());
		}

		if (cmdLine.FindAndRemoveSwitch(L"rawkeys"))
			g_rawKeys = true;
		else if (cmdLine.FindAndRemoveSwitch(L"norawkeys"))
			g_rawKeys = false;

		if (cmdLine.FindAndRemoveSwitch(L"cartmapper", arg)) {
			cartmapper = ATGetCartridgeModeForMapper(wcstol(arg, NULL, 10));

			if (cartmapper <= 0 || cartmapper >= kATCartridgeModeCount)
				throw MyError("Unsupported or invalid cartridge mapper: %ls", arg);
		}

		if (cmdLine.FindAndRemoveSwitch(L"?")) {
			MessageBox(g_hwnd,
				"Usage: Altirra [switches] <disk/cassette/cartridge images...>\n"
				"\n"
				"/resetall - Reset all settings to default\n"
				"/baseline - Reset hardware settings to default\n"
				"/ntsc - select NTSC timing\n"
				"/pal - select PAL timing\n"
				"/artifact:none|ntsc|ntschi|pal - set video artifacting\n"
				"/[no]burstio|burstiopolled - control SIO burst I/O mode\n"
				"/[no]siopatch - control SIO kernel patch\n"
				"/[no]fastboot - control fast kernel boot initialization\n"
				"/[no]casautoboot - control cassette auto-boot\n"
				"/[no]accuratedisk - control accurate sector timing\n"
				"/[no]basic - enable/disable BASIC ROM\n"
				"/[no]vbxe - enable/disable VBXE support\n"
				"/[no]vbxeshared - enable/disable VBXE shared memory\n"
				"/[no]vbxealtpage - enable/disable VBXE $D7xx register window\n"
				"/kernel:default|osa|osb|xl|lle|hle|other|5200|5200lle - select kernel ROM\n"
				"/hardware:800|800xl|5200 - select hardware type\n"
				"/memsize:16K|48K|52K|64K|128K|320K|576K|1088K - select memory size\n"
				"/[no]stereo - enable dual pokeys\n"
				"/gdi - force GDI display mode\n"
				"/ddraw - force DirectDraw display mode\n"
				"/opengl - force OpenGL display mode\n"
				"/[no]vsync - synchronize to vertical blank\n"
				"/debug - launch in debugger mode"
				"/f - start full screen"
				"/bootvrw - boot disk images in virtual R/W mode\n"
				"/bootrw - boot disk images in read/write mode\n"
				"/type \"keys\" - type keys on keyboard (~ for enter, ` for \")\n"
				"/[no]hdpath|hdpathrw <path> - mount H: device\n"
				"/[no]rawkeys - enable/disable raw keyboard presses\n"
				"/cartmapper <mapper> - set cartridge mapper for untagged image\n"
				"/portable - create Altirra.ini file and switch to portable mode\n"
				"/ide:d1xx|d5xx,ro|rw,c,h,s,path - set IDE emulation\n"
				,
				"Altirra command-line help", MB_OK | MB_ICONINFORMATION);
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

	if (debugMode)
		ATShowConsole();

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
}

void LoadSettingsEarly(bool useHardwareBaseline) {
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

	ATCartridgeEmulator *cart = g_sim.GetCartridge();
	const wchar_t *cartPath = NULL;
	int cartMode = 0;
	if (cart) {
		cartPath = cart->GetPath();
		cartMode = cart->GetMode();
	}

	if (cartPath && *cartPath) {
		key.setString("Cartridge", cartPath);
		key.setInt("Cartridge Mode", cartMode);
	} else if (cartMode == kATCartridgeMode_SuperCharger3D) {
		key.setString("Cartridge", "special:sc3d");
		key.removeValue("Cartridge Mode");
	} else {
		key.removeValue("Cartridge");
		key.removeValue("Cartridge Mode");
	}

	ATIDEEmulator *ide = g_sim.GetIDEEmulator();

	if (ide) {
		key.setString("IDE: Image path", ide->GetImagePath());
		key.setBool("IDE: Use D5xx", g_sim.IsIDEUsingD5xx());
		key.setBool("IDE: Write enabled", ide->IsWriteEnabled());
		key.setBool("IDE: Fast device", ide->IsFastDevice());
		key.setInt("IDE: Image cylinders", ide->GetCylinderCount());
		key.setInt("IDE: Image heads", ide->GetHeadCount());
		key.setInt("IDE: Image sectors per track", ide->GetSectorsPerTrack());
	} else {
		key.removeValue("IDE: Image path");
		key.removeValue("IDE: Use D5xx");
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

	if (key.getString("Cartridge", imagestr)) {
		int cartMode = key.getInt("Cartridge Mode", 0);

		try {
			ATCartLoadContext cartLoadCtx = {0};
			cartLoadCtx.mbReturnOnUnknownMapper = false;
			cartLoadCtx.mCartMapper = cartMode;

			if (imagestr == L"special:sc3d")
				g_sim.LoadCartridgeSC3D();
			else
				g_sim.LoadCartridge(imagestr.c_str(), &cartLoadCtx);
		} catch(const MyError&) {
		}
	} else {
		if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
			g_sim.LoadCartridge5200Default();
	}

	VDStringW idePath;
	if (key.getString("IDE: Image path", idePath)) {
		bool d5xx = key.getBool("IDE: Use D5xx", true);
		bool write = key.getBool("IDE: Write enabled", false);
		bool fast = key.getBool("IDE: Fast device", false);
		uint32 cyls = key.getInt("IDE: Image cylinders", 0);
		uint32 heads = key.getInt("IDE: Image heads", 0);
		uint32 sectors = key.getInt("IDE: Image sectors per track", 0);

		try {
			g_sim.LoadIDE(d5xx, write, fast, cyls, heads, sectors, idePath.c_str());
		} catch(const MyError&) {
		}
	}
}

namespace {
	static const char *kSettingNamesROMImages[]={
		"OS-A",
		"OS-B",
		"XL",
		"Other",
		"5200",
		"Basic"
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

void LoadSettings(bool useHardwareBaseline) {
	VDRegistryAppKey key("Settings", false);

	ATPokeyEmulator& pokey = g_sim.GetPokey();
	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);

	if (!useHardwareBaseline) {
		g_sim.SetKernelMode((ATKernelMode)key.getEnumInt("Kernel mode", kATKernelModeCount, g_sim.GetKernelMode()));
		g_sim.SetHardwareMode((ATHardwareMode)key.getEnumInt("Hardware mode", kATHardwareModeCount, g_sim.GetHardwareMode()));
		g_sim.SetBASICEnabled(key.getBool("BASIC enabled", g_sim.IsBASICEnabled()));
		g_sim.SetPALMode(key.getBool("PAL mode", g_sim.IsPALMode()));
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
		disk.SetAccurateSectorTimingEnabled(key.getBool("Disk: Accurate sector timing", disk.IsAccurateSectorTimingEnabled()));
		g_sim.SetDualPokeysEnabled(key.getBool("Audio: Dual POKEYs enabled", g_sim.IsDualPokeysEnabled()));

		g_enhancedText = key.getInt("Video: Enhanced text mode", 0);
		if (g_enhancedText > 1)
			g_enhancedText = 1;

		g_sim.SetPrinterEnabled(key.getBool("Printer: Enabled", g_sim.IsPrinterEnabled()));
		g_sim.SetRTime8Enabled(key.getBool("Misc: R-Time 8 Enabled", g_sim.IsRTime8Enabled()));
		g_sim.SetRS232Enabled(key.getBool("Serial Ports: Enabled", g_sim.IsRS232Enabled()));

		if (g_sim.IsRS232Enabled()) {
			ATRS232Config config;

			config.mbTelnetEmulation = key.getBool("Serial Ports: Telnet Emulation", config.mbTelnetEmulation);

			g_sim.GetRS232()->SetConfig(config);
		}

		g_sim.SetRandomFillEnabled(key.getBool("Memory: Randomize on start", g_sim.IsRandomFillEnabled()));
		g_rawKeys = key.getBool("Keyboard: Raw mode", g_rawKeys);
	}

	g_sim.SetTurboModeEnabled(key.getBool("Turbo mode", g_sim.IsTurboModeEnabled()));
	g_pauseInactive = key.getBool("Pause when inactive", g_pauseInactive);

	g_sim.GetCassette().SetLoadDataAsAudioEnable(key.getBool("Cassette: Load data as audio", g_sim.GetCassette().IsLoadDataAsAudioEnabled()));

	IATAudioOutput *audioOut = g_sim.GetAudioOutput();
	float volume = VDGetIntAsFloat(key.getInt("Audio: Volume", VDGetFloatAsInt(0.5f)));
	if (!(volume >= 0.0f && volume <= 1.0f))
		volume = 0.5f;
	audioOut->SetVolume(volume);

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

	ATColorParams colpa(gtia.GetColorParams());
	colpa.mHueStart = VDGetIntAsFloat(key.getInt("Colors: Hue Start", VDGetFloatAsInt(colpa.mHueStart)));
	colpa.mHueRange = VDGetIntAsFloat(key.getInt("Colors: Hue Range", VDGetFloatAsInt(colpa.mHueRange)));
	colpa.mBrightness = VDGetIntAsFloat(key.getInt("Colors: Brightness", VDGetFloatAsInt(colpa.mBrightness)));
	colpa.mContrast = VDGetIntAsFloat(key.getInt("Colors: Contrast", VDGetFloatAsInt(colpa.mContrast)));
	colpa.mSaturation = VDGetIntAsFloat(key.getInt("Colors: Saturation", VDGetFloatAsInt(colpa.mSaturation)));
	colpa.mArtifactHue = VDGetIntAsFloat(key.getInt("Colors: Artifact Hue", VDGetFloatAsInt(colpa.mArtifactHue)));
	colpa.mArtifactSat = VDGetIntAsFloat(key.getInt("Colors: Artifact Saturation", VDGetFloatAsInt(colpa.mArtifactSat)));
	colpa.mArtifactBias = VDGetIntAsFloat(key.getInt("Colors: Artifact Bias", VDGetFloatAsInt(colpa.mArtifactBias)));
	gtia.SetColorParams(colpa);

	g_sim.SetDiskSectorCounterEnabled(key.getBool("Disk: Sector counter enabled", g_sim.IsDiskSectorCounterEnabled()));

	ATCPUEmulator& cpu = g_sim.GetCPU();
	cpu.SetHistoryEnabled(key.getBool("CPU: History enabled", cpu.IsHistoryEnabled()));
	cpu.SetPathfindingEnabled(key.getBool("CPU: Pathfinding enabled", cpu.IsPathfindingEnabled()));
	cpu.SetStopOnBRK(key.getBool("CPU: Stop on BRK", cpu.GetStopOnBRK()));
	cpu.SetCPUMode((ATCPUMode)key.getEnumInt("CPU: Chip type", kATCPUModeCount, cpu.GetCPUMode()));

	IATHardDiskEmulator *hd = g_sim.GetHardDisk();
	if (hd) {
		VDStringW path;
		if (key.getString("Hard disk: Host path", path))
			hd->SetBasePath(path.c_str());

		hd->SetEnabled(key.getBool("Hard disk: Enabled", hd->IsEnabled()));
		hd->SetReadOnly(key.getBool("Hard disk: Read only", hd->IsReadOnly()));
		hd->SetBurstIOEnabled(key.getBool("Hard disk: Burst IO enabled", hd->IsBurstIOEnabled()));
	}

	pokey.SetNonlinearMixingEnabled(key.getBool("Audio: Non-linear mixing", pokey.IsNonlinearMixingEnabled()));

	g_dispFilterMode = (IVDVideoDisplay::FilterMode)key.getEnumInt("Display: Filter mode", IVDVideoDisplay::kFilterBicubic + 1, g_dispFilterMode);
	if (g_pDisplay)
		g_pDisplay->SetFilterMode(g_dispFilterMode);

	g_showFps = key.getBool("View: Show FPS", g_showFps);
	gtia.SetVsyncEnabled(key.getBool("View: Vertical sync", gtia.IsVsyncEnabled()));

	ATLightPenPort *lpp = g_sim.GetLightPenPort();
	lpp->SetAdjust(key.getInt("Light Pen: Adjust X", lpp->GetAdjustX()), key.getInt("Light Pen: Adjust Y", lpp->GetAdjustY()));

	g_mouseAutoCapture = key.getBool("Mouse: Auto-capture", g_mouseAutoCapture);

	VDRegistryAppKey imapKey("Settings\\Input maps");
	g_sim.GetInputManager()->Load(imapKey);

	ATReloadPortMenus();

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
	key.setBool("PAL mode", g_sim.IsPALMode());
	key.setInt("Memory mode", g_sim.GetMemoryMode());
	key.setBool("Turbo mode", g_sim.IsTurboModeEnabled());
	key.setBool("Pause when inactive", g_pauseInactive);

	key.setBool("Memory: Randomize on start", g_sim.IsRandomFillEnabled());

	key.setBool("Kernel: Floating-point patch enabled", g_sim.IsFPPatchEnabled());
	key.setBool("Kernel: Fast boot enabled", g_sim.IsFastBootEnabled());

	key.setBool("Cassette: SIO patch enabled", g_sim.IsCassetteSIOPatchEnabled());
	key.setBool("Cassette: Auto-boot enabled", g_sim.IsCassetteAutoBootEnabled());
	key.setBool("Cassette: Load data as audio", g_sim.GetCassette().IsLoadDataAsAudioEnabled());

	IATAudioOutput *audioOut = g_sim.GetAudioOutput();
	key.setInt("Audio: Volume", VDGetFloatAsInt(audioOut->GetVolume()));
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
	key.setBool("GTIA: VBXE support", g_sim.GetVBXE() != NULL);
	key.setBool("GTIA: VBXE shared memory", g_sim.IsVBXESharedMemoryEnabled());
	key.setBool("GTIA: VBXE alternate page", g_sim.IsVBXEAltPageEnabled());

	ATColorParams colpa(gtia.GetColorParams());
	key.setInt("Colors: Hue Start", VDGetFloatAsInt(colpa.mHueStart));
	key.setInt("Colors: Hue Range", VDGetFloatAsInt(colpa.mHueRange));
	key.setInt("Colors: Brightness", VDGetFloatAsInt(colpa.mBrightness));
	key.setInt("Colors: Contrast", VDGetFloatAsInt(colpa.mContrast));
	key.setInt("Colors: Saturation", VDGetFloatAsInt(colpa.mSaturation));
	key.setInt("Colors: Artifact Hue", VDGetFloatAsInt(colpa.mArtifactHue));
	key.setInt("Colors: Artifact Saturation", VDGetFloatAsInt(colpa.mArtifactSat));
	key.setInt("Colors: Artifact Bias", VDGetFloatAsInt(colpa.mArtifactBias));

	ATDiskEmulator& disk = g_sim.GetDiskDrive(0);
	key.setBool("Disk: SIO patch enabled", g_sim.IsDiskSIOPatchEnabled());
	key.setBool("Disk: Accurate sector timing", disk.IsAccurateSectorTimingEnabled());
	key.setBool("Disk: Sector counter enabled", g_sim.IsDiskSectorCounterEnabled());

	key.setBool("Audio: Dual POKEYs enabled", g_sim.IsDualPokeysEnabled());
	key.setBool("Audio: Non-linear mixing", pokey.IsNonlinearMixingEnabled());

	ATCPUEmulator& cpu = g_sim.GetCPU();
	key.setBool("CPU: History enabled", cpu.IsHistoryEnabled());
	key.setBool("CPU: Pathfinding enabled", cpu.IsPathfindingEnabled());
	key.setBool("CPU: Stop on BRK", cpu.GetStopOnBRK());
	key.setInt("CPU: Chip type", cpu.GetCPUMode());

	IATHardDiskEmulator *hd = g_sim.GetHardDisk();
	if (hd) {
		key.setString("Hard disk: Host path", hd->GetBasePath());
		key.setBool("Hard disk: Enabled", hd->IsEnabled());
		key.setBool("Hard disk: Read only", hd->IsReadOnly());
		key.setBool("Hard disk: Burst IO enabled", hd->IsBurstIOEnabled());
	}

	key.setInt("Display: Filter mode", g_dispFilterMode);
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

		key.setBool("Serial Ports: Telnet Emulation", config.mbTelnetEmulation);
	}

	key.setBool("Keyboard: Raw mode", g_rawKeys);

	ATLightPenPort *lpp = g_sim.GetLightPenPort();
	key.setInt("Light Pen: Adjust X", lpp->GetAdjustX());
	key.setInt("Light Pen: Adjust Y", lpp->GetAdjustY());

	key.setBool("Mouse: Auto-capture", g_mouseAutoCapture);

	SaveInputMaps();
	SaveMountedImages();
}

void SaveInputMaps() {
	VDRegistryAppKey imapKey("Settings\\Input maps");
	g_sim.GetInputManager()->Save(imapKey);
}

class ATUIWindowCaptionUpdater {
public:
	ATUIWindowCaptionUpdater();

	void Update(HWND hwnd, bool running, int ticks, double fps);
	void CheckForStateChange(bool force);

protected:
	bool mbLastRunning;
	bool mbLastCaptured;

	VDStringA	mBasePrefix;
	VDStringA	mPrefix;
	VDStringA	mBuffer;

	ATHardwareMode	mLastHardwareMode;
	ATKernelMode	mLastKernelMode;
	ATMemoryMode	mLastMemoryMode;
	bool			mbLastBASICState;
	bool			mbLastPALState;
	bool			mbLastVBXEState;
	bool			mbForceUpdate;
	bool			mbLastDebugging;
	bool			mbLastShowFPS;
};

ATUIWindowCaptionUpdater::ATUIWindowCaptionUpdater()
	: mbLastRunning(false)
	, mbLastCaptured(false)
	, mLastHardwareMode(kATHardwareModeCount)
	, mLastKernelMode(kATKernelModeCount)
	, mLastMemoryMode(kATMemoryModeCount)
	, mbLastBASICState(false)
	, mbLastPALState(false)
	, mbLastVBXEState(false)
	, mbLastDebugging(false)
	, mbForceUpdate(false)
{
	mBasePrefix =
#if defined(VD_CPU_AMD64)
		"Altirra-64"
#else
		"Altirra "
#endif
		AT_VERSION AT_VERSION_DEBUG;

	CheckForStateChange(true);
}

void ATUIWindowCaptionUpdater::Update(HWND hwnd, bool running, int ticks, double fps) {
	bool forceUpdate = false;

	if (mbLastRunning != running) {
		mbLastRunning = running;
		forceUpdate = true;
	}

	bool captured = g_mouseCaptured || g_mouseClipped;
	if (mbLastCaptured != captured) {
		mbLastCaptured = captured;
		forceUpdate = true;
	}

	CheckForStateChange(forceUpdate);

	mBuffer = mPrefix;

	if ((running && g_showFps) || mbForceUpdate) {
		mbForceUpdate = false;

		if (g_showFps)
			mBuffer.append_sprintf(" - %d (%.3f fps)", ticks, fps);

		if (captured)
			mBuffer += " (mouse captured - right Alt to release)";

		SetWindowText(hwnd, mBuffer.c_str());
	}
}

void ATUIWindowCaptionUpdater::CheckForStateChange(bool force) {
	bool change = false;

	ATHardwareMode hwmode = g_sim.GetHardwareMode();
	if (mLastHardwareMode != hwmode) {
		mLastHardwareMode = hwmode;
		change = true;
	}

	ATKernelMode krmode = g_sim.GetKernelMode();
	if (mLastKernelMode != krmode) {
		mLastKernelMode = krmode;
		change = true;
	}

	ATMemoryMode mmmode = g_sim.GetMemoryMode();
	if (mLastMemoryMode != mmmode) {
		mLastMemoryMode = mmmode;
		change = true;
	}

	bool basic = g_sim.IsBASICEnabled();
	if (mbLastBASICState != basic) {
		mbLastBASICState = basic;
		change = true;
	}

	bool pal = g_sim.IsPALMode();
	if (mbLastPALState != pal) {
		mbLastPALState = pal;
		change = true;
	}

	bool vbxe = g_sim.GetVBXE() != NULL;
	if (mbLastVBXEState != vbxe) {
		mbLastVBXEState = vbxe;
		change = true;
	}

	bool debugging = ATIsDebugConsoleActive();
	if (mbLastDebugging != debugging) {
		mbLastDebugging = debugging;
		change = true;
	}

	if (mbLastShowFPS != g_showFps) {
		mbLastShowFPS = g_showFps;
		change = true;
	}

	if (!force && !change)
		return;

	mPrefix = mBasePrefix;

	if (ATIsDebugConsoleActive()) {
		if (mbLastRunning)
			mPrefix += " [running]";
		else
			mPrefix += " [stopped]";
	}

	bool showBasic = true;
	switch(g_sim.GetHardwareMode()) {
		case kATHardwareMode_800:
			mPrefix += ": 800";
			break;

		case kATHardwareMode_800XL:
			mPrefix += ": XL/XE";
			break;

		case kATHardwareMode_5200:
			mPrefix += ": 5200";
			showBasic = false;
			break;
	}

	switch(g_sim.GetActualKernelMode()) {
		case kATKernelMode_OSA:
			mPrefix += " OS-A";
			break;

		case kATKernelMode_OSB:
			mPrefix += " OS-B";
			break;

		case kATKernelMode_XL:
			break;

		case kATKernelMode_Other:
			mPrefix += " Other";
			break;

		case kATKernelMode_5200:
			mPrefix += " 5200";
			break;

		case kATKernelMode_HLE:
			mPrefix += " HLE";
			break;

		case kATKernelMode_LLE:
		case kATKernelMode_5200_LLE:
			mPrefix += " LLE";
			break;
	}

	if (pal)
		mPrefix += " PAL";
	else
		mPrefix += " NTSC";

	if (vbxe)
		mPrefix += "-VBXE";

	mPrefix += " / ";

	switch(g_sim.GetMemoryMode()) {
		case kATMemoryMode_16K:
			mPrefix += "16K";
			break;

		case kATMemoryMode_48K:
			mPrefix += "48K";
			break;

		case kATMemoryMode_52K:
			mPrefix += "52K";
			break;

		case kATMemoryMode_64K:
			mPrefix += "64K";
			break;

		case kATMemoryMode_128K:
			mPrefix += "128K";
			break;

		case kATMemoryMode_320K:
			mPrefix += "320K";
			break;

		case kATMemoryMode_576K:
			mPrefix += "576K";
			break;

		case kATMemoryMode_1088K:
			mPrefix += "1088K";
			break;
	}

	if (basic && showBasic)
		mPrefix += " / BASIC";

	mbForceUpdate = true;
}

int RunMainLoop2(HWND hwnd, VDCommandLine& cmdLine) {
	bool commandLineRead = false;
	int ticks = 0;

	ATAnticEmulator& antic = g_sim.GetAntic();
	uint32 lastFrame = antic.GetFrameCounter();
	LARGE_INTEGER secondTime;
	LARGE_INTEGER lastTime;
	LARGE_INTEGER errorBound;
	uint32 frameTimeErrorAccum = 0;
	sint64 error = 0;
	QueryPerformanceFrequency(&secondTime);

	// NTSC: 1.7897725MHz master clock, 262 scanlines of 114 clocks each
	double ntscFrameTimeF = (double)secondTime.QuadPart / 59.9227;
	sint64 ntscFrameTime = VDFloorToInt64(ntscFrameTimeF);
	uint32 ntscFrameTimeFrac = VDRoundToInt32((ntscFrameTimeF - ntscFrameTime) * 65536.0);

	// PAL: 1.773447MHz master clock, 312 scanlines of 114 clocks each
	double palFrameTimeF = (double)secondTime.QuadPart / 49.8607;
	sint64 palFrameTime = VDFloorToInt64(palFrameTimeF);
	uint32 palFrameTimeFrac = VDRoundToInt32((palFrameTimeF - palFrameTime) * 65536.0);
	errorBound.QuadPart = palFrameTime * 5;
	QueryPerformanceCounter(&lastTime);

	LARGE_INTEGER lastStatusTime;
	QueryPerformanceCounter(&lastStatusTime);

	sint64 nextFrameTime;
	int nextTickUpdate = 60;
	bool nextFrameTimeValid = false;

	ATUIWindowCaptionUpdater winCaptionUpdater;
	winCaptionUpdater.Update(hwnd, true, 0, 0);

	MSG msg;
	int rcode = 0;
	for(;;) {
		for(int i=0; i<2; ++i) {
			DWORD flags = i ? PM_REMOVE : PM_QS_INPUT | PM_REMOVE;

			while(PeekMessage(&msg, NULL, 0, 0, flags)) {
				if (msg.message == WM_QUIT) {
					rcode = (int)msg.wParam;
					goto xit;
				}

				if (msg.hwnd) {
					if (msg.message == WM_SYSCHAR) {
						if (g_hwnd && msg.hwnd != g_hwnd && GetAncestor(msg.hwnd, GA_ROOT) == g_hwnd)
						{
							msg.hwnd = g_hwnd;
						}
					}

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

		if (!commandLineRead) {
			commandLineRead = true;

			ReadCommandLine(hwnd, cmdLine);
		}

		if (g_sim.IsRunning() && (g_winActive || !g_pauseInactive)) {
			uint32 frame = antic.GetFrameCounter();
			if (frame != lastFrame) {
				ATDisplayBasePane *pane = static_cast<ATDisplayBasePane *>(ATGetUIPane(kATUIPaneId_Display));
				if (pane)
					pane->UpdateTextDisplay(g_enhancedText != 0);

				lastFrame = frame;

				LARGE_INTEGER curTime;
				QueryPerformanceCounter(&curTime);

				++ticks;

				if (!g_fullscreen && (ticks - nextTickUpdate) >= 0) {
					double fps = 0;

					if (g_showFps)
						fps = (double)secondTime.QuadPart / (double)(curTime.QuadPart - lastStatusTime.QuadPart) * 60;

					winCaptionUpdater.Update(hwnd, true, ticks, fps);

					lastStatusTime = curTime;
					nextTickUpdate = ticks - ticks % 60 + 60;
				}

				sint64 delta = curTime.QuadPart - lastTime.QuadPart;

				error += delta;

				if (g_sim.IsPALMode()) {
					error -= palFrameTime;
					frameTimeErrorAccum += palFrameTimeFrac;
				} else {
					error -= ntscFrameTime;
					frameTimeErrorAccum += ntscFrameTimeFrac;
				}

				if (frameTimeErrorAccum >= 0x10000) {
					frameTimeErrorAccum &= 0xFFFF;

					--error;
				}

				if (error > errorBound.QuadPart || error < -errorBound.QuadPart)
					error = 0;

				lastTime = curTime;

				nextFrameTimeValid = false;
				if (g_sim.IsTurboModeEnabled() && false)
					error = 0;
				else if (error < 0) {
					nextFrameTimeValid = true;
					nextFrameTime = curTime.QuadPart - error;
				}
			}

			bool dropFrame = false;
			if (nextFrameTimeValid) {
				LARGE_INTEGER curTime;
				QueryPerformanceCounter(&curTime);

				sint64 delta = nextFrameTime - curTime.QuadPart;

				if (delta <= 0 || delta > secondTime.QuadPart)
					nextFrameTimeValid = false;
				else {
					int ticks = (int)(delta * 1000 / secondTime.QuadPart);

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

				// do not allow frame drops if we are recording
				if (g_pVideoWriter)
					dropFrame = false;
			}

			ATSimulator::AdvanceResult ar = g_sim.Advance(dropFrame);

			if (ar != ATSimulator::kAdvanceResult_Stopped) {
				if (ar == ATSimulator::kAdvanceResult_WaitingForFrame)
					::MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT);

				continue;
			}
		} else {
			if (ATIsDebugConsoleActive()) {
				if (g_fullscreen)
					ATSetFullscreen(false);

				if (g_mouseCaptured)
					ReleaseCapture();

				if (g_mouseClipped) {
					g_mouseClipped = false;

					::ClipCursor(NULL);
				}
			}

			winCaptionUpdater.Update(hwnd, false, 0, 0);
			nextTickUpdate = ticks - ticks % 60;
		}

		WaitMessage();
	}
xit:
	return 0;
}

int RunMainLoop(HWND hwnd, VDCommandLine& cmdLine) {
	int rc;

	__try {
		rc = RunMainLoop2(hwnd, cmdLine);
	} __except(ATExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
	}

	return rc;
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int nCmdShow) {
	#ifdef _MSC_VER
		_CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
	#endif

	CPUEnableExtensions(CPUCheckForExtensions());
	VDFastMemcpyAutodetect();
	VDInitThunkAllocator();

	OleInitialize(NULL);

	bool wsaInited = false;

	WSADATA wsa;
	if (!WSAStartup(MAKEWORD(2, 0), &wsa))
		wsaInited = true;

	InitCommonControls();

	VDCommandLine cmdLine(GetCommandLineW());

	VDRegistryProviderMemory *rpmem = NULL;

	const bool resetAll = cmdLine.FindAndRemoveSwitch(L"resetall");
	const VDStringW portableRegPath(VDMakePath(VDGetProgramPath().c_str(), L"Altirra.ini"));

	if (cmdLine.FindAndRemoveSwitch(L"portable") || VDDoesPathExist(portableRegPath.c_str())) {
		rpmem = new VDRegistryProviderMemory;
		VDSetRegistryProvider(rpmem);

		if (!resetAll && VDDoesPathExist(portableRegPath.c_str())) {
			try {
				ATUILoadRegistry(portableRegPath.c_str());
			} catch(const MyError& err) {
				VDStringA message;

				message.sprintf("There was an error loading the settings file:\n\n%s\n\nDo you want to continue? If so, settings will be reset to defaults and may not be saved.", err.c_str());
				if (IDYES != MessageBox(NULL, message.c_str(), "Altirra Warning", MB_YESNO | MB_ICONWARNING))
					return 5;
			}
		}
	} else {
		if (resetAll)
			SHDeleteKey(HKEY_CURRENT_USER, "Software\\virtualdub.org\\Altirra");
	}

	VDRegisterVideoDisplayControl();

	VDRegistryAppKey::setDefaultKey("Software\\virtualdub.org\\Altirra\\");

	const bool useHardwareBaseline = cmdLine.FindAndRemoveSwitch(L"baseline");

	LoadSettingsEarly(useHardwareBaseline);

	VDShaderEditorBaseWindow::Register();

	ATInitUIFrameSystem();

	bool fsmode = false;

	if (cmdLine.FindAndRemoveSwitch(L"f"))
		fsmode = true;

	if (cmdLine.FindAndRemoveSwitch(L"lockd3d"))
		g_d3d9Lock.Lock();

	if (cmdLine.FindAndRemoveSwitch(L"gdi"))
		VDVideoDisplaySetFeatures(false, false, false, false, false, false, false);
	else if (cmdLine.FindAndRemoveSwitch(L"ddraw"))
		VDVideoDisplaySetFeatures(true, true, false, false, false, false, false);
	else if (cmdLine.FindAndRemoveSwitch(L"opengl"))
		VDVideoDisplaySetFeatures(true, false, false, true, false, false, false);
	else
		VDVideoDisplaySetFeatures(true, true, false, false, true, false, false);
	//VDVideoDisplaySetD3DFXFileName(L"d:\\shaders\\tvela2.fx");
	VDVideoDisplaySetDebugInfoEnabled(false);
	VDVideoDisplaySetMonitorSwitchingDXEnabled(true);
	VDVideoDisplaySetSecondaryDXEnabled(true);

	VDInitFilespecSystem();
	VDLoadFilespecSystemData();

	ATInitUIPanes();
	ATInitProfilerUI();
	ATRegisterUIPaneType(kATUIPaneId_Display, VDRefCountObjectFactory<ATDisplayPane, ATUIPane>);
	ATUIInitVirtualKeyMap();

	g_hMenu = LoadMenu(NULL, MAKEINTRESOURCE(IDR_MENU1));
	g_hAccel = LoadAccelerators(NULL, MAKEINTRESOURCE(IDR_ACCELERATOR1));

	ATInitPortMenus(g_hMenu);

	g_hInst = VDGetLocalModuleHandleW32();

	g_pMainWindow = new ATMainWindow;
	g_pMainWindow->AddRef();
	HWND hwnd = (HWND)g_pMainWindow->Create(150, 100, 640, 480, NULL, false);

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

	SetWindowText(hwnd, "Altirra");
	g_hwnd = hwnd;

	SetMenu(hwnd, g_hMenu);

	LoadROMImageSettings();

	try {
		g_sim.LoadROMs();
	} catch(MyError& e) {
		e.post(hwnd, "Altirra Error");
	}

	g_sim.Resume();
	ATInitDebugger();

	LoadSettings(useHardwareBaseline);

	ATRestorePaneLayout(NULL);
	g_sim.ColdReset();

	timeBeginPeriod(1);

	if (fsmode)
		ATSetFullscreen(true);

	int returnCode = RunMainLoop(hwnd, cmdLine);

	timeEndPeriod(1);

	StopRecording();

	SaveSettings();
	VDSaveFilespecSystemData();

	g_sim.Shutdown();

	g_d3d9Lock.Unlock();

	g_pMainWindow->Destroy();
	g_pMainWindow->Release();
	g_pMainWindow = NULL;
	g_hwnd = NULL;

	ATShutdownPortMenus();

	ATShutdownUIPanes();
	ATShutdownUIFrameSystem();

	if (rpmem) {
		try {
			ATUISaveRegistry(portableRegPath.c_str());
		} catch(const MyError&) {
		}

		VDSetRegistryProvider(NULL);
		delete rpmem;
	}

	if (wsaInited)
		WSACleanup();

	OleUninitialize();

	VDShutdownThunkAllocator();

	return returnCode;
}
